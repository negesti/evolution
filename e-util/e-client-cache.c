/*
 * e-client-cache.c
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
 */

/**
 * SECTION: e-client-cache
 * @include: e-util/e-util.h
 * @short_description: Shared #EClient instances
 *
 * #EClientCache provides for application-wide sharing of #EClient
 * instances and centralized rebroadcasting of #EClient::backend-died
 * and #EClient::backend-error signals from cached #EClient instances.
 *
 * #EClientCache automatically invalidates cache entries in response to
 * #EClient::backend-died signals.  The #EClient instance is discarded,
 * and a new instance is created on the next request.
 **/

#include "e-client-cache.h"

#include <config.h>
#include <glib/gi18n-lib.h>

#include <libecal/libecal.h>
#include <libebook/libebook.h>
#include <libebackend/libebackend.h>

#define E_CLIENT_CACHE_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_CLIENT_CACHE, EClientCachePrivate))

typedef struct _ClientData ClientData;
typedef struct _SignalClosure SignalClosure;

struct _EClientCachePrivate {
	ESourceRegistry *registry;

	GHashTable *client_ht;
	GMutex client_ht_lock;

	/* For signal emissions. */
	GMainContext *main_context;
};

struct _ClientData {
	volatile gint ref_count;
	GMutex lock;
	GWeakRef cache;
	EClient *client;
	GQueue connecting;
	gulong backend_died_handler_id;
	gulong backend_error_handler_id;
	gulong notify_handler_id;
};

struct _SignalClosure {
	EClientCache *cache;
	EClient *client;
	GParamSpec *pspec;
	gchar *error_message;
};

G_DEFINE_TYPE_WITH_CODE (
	EClientCache,
	e_client_cache,
	G_TYPE_OBJECT,
	G_IMPLEMENT_INTERFACE (
		E_TYPE_EXTENSIBLE, NULL))

enum {
	PROP_0,
	PROP_REGISTRY
};

enum {
	BACKEND_DIED,
	BACKEND_ERROR,
	CLIENT_CREATED,
	CLIENT_NOTIFY,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

static ClientData *
client_data_new (EClientCache *cache)
{
	ClientData *client_data;

	client_data = g_slice_new0 (ClientData);
	client_data->ref_count = 1;
	g_mutex_init (&client_data->lock);
	g_weak_ref_set (&client_data->cache, cache);

	return client_data;
}

static ClientData *
client_data_ref (ClientData *client_data)
{
	g_return_val_if_fail (client_data != NULL, NULL);
	g_return_val_if_fail (client_data->ref_count > 0, NULL);

	g_atomic_int_inc (&client_data->ref_count);

	return client_data;
}

static void
client_data_unref (ClientData *client_data)
{
	g_return_if_fail (client_data != NULL);
	g_return_if_fail (client_data->ref_count > 0);

	if (g_atomic_int_dec_and_test (&client_data->ref_count)) {

		/* The signal handlers hold a reference on client_data,
		 * so we should not be here unless the signal handlers
		 * have already been disconnected. */
		g_warn_if_fail (client_data->backend_died_handler_id == 0);
		g_warn_if_fail (client_data->backend_error_handler_id == 0);
		g_warn_if_fail (client_data->notify_handler_id == 0);

		g_mutex_clear (&client_data->lock);
		g_clear_object (&client_data->client);
		g_weak_ref_set (&client_data->cache, NULL);

		/* There should be no connect() operations in progress. */
		g_warn_if_fail (g_queue_is_empty (&client_data->connecting));

		g_slice_free (ClientData, client_data);
	}
}

static void
client_data_dispose (ClientData *client_data)
{
	g_mutex_lock (&client_data->lock);

	if (client_data->client != NULL) {
		g_signal_handler_disconnect (
			client_data->client,
			client_data->backend_died_handler_id);
		client_data->backend_died_handler_id = 0;

		g_signal_handler_disconnect (
			client_data->client,
			client_data->backend_error_handler_id);
		client_data->backend_error_handler_id = 0;

		g_signal_handler_disconnect (
			client_data->client,
			client_data->notify_handler_id);
		client_data->notify_handler_id = 0;

		g_clear_object (&client_data->client);
	}

	g_mutex_unlock (&client_data->lock);

	client_data_unref (client_data);
}

static void
signal_closure_free (SignalClosure *signal_closure)
{
	g_clear_object (&signal_closure->cache);
	g_clear_object (&signal_closure->client);

	if (signal_closure->pspec != NULL)
		g_param_spec_unref (signal_closure->pspec);

	g_free (signal_closure->error_message);

	g_slice_free (SignalClosure, signal_closure);
}

static ClientData *
client_ht_lookup (EClientCache *cache,
                  ESource *source,
                  const gchar *extension_name)
{
	GHashTable *client_ht;
	GHashTable *inner_ht;
	ClientData *client_data = NULL;

	g_return_val_if_fail (E_IS_SOURCE (source), NULL);
	g_return_val_if_fail (extension_name != NULL, NULL);

	client_ht = cache->priv->client_ht;

	g_mutex_lock (&cache->priv->client_ht_lock);

	/* We pre-load the hash table with supported extension names,
	 * so lookup failures indicate an unsupported extension name. */
	inner_ht = g_hash_table_lookup (client_ht, extension_name);
	if (inner_ht != NULL) {
		client_data = g_hash_table_lookup (inner_ht, source);
		if (client_data == NULL) {
			g_object_ref (source);
			client_data = client_data_new (cache);
			g_hash_table_insert (inner_ht, source, client_data);
		}
		client_data_ref (client_data);
	}

	g_mutex_unlock (&cache->priv->client_ht_lock);

	return client_data;
}

static gchar *
client_cache_build_source_description (EClientCache *cache,
                                       ESource *source)
{
	ESourceRegistry *registry;
	ESource *parent;
	GString *description;
	gchar *display_name;
	gchar *parent_uid;

	description = g_string_sized_new (128);

	registry = e_client_cache_ref_registry (cache);

	parent_uid = e_source_dup_parent (source);
	parent = e_source_registry_ref_source (registry, parent_uid);
	g_free (parent_uid);

	if (parent != NULL) {
		display_name = e_source_dup_display_name (parent);
		g_string_append (description, display_name);
		g_string_append (description, " / ");
		g_free (display_name);

		g_object_unref (parent);
	}

	display_name = e_source_dup_display_name (source);
	g_string_append (description, display_name);
	g_free (display_name);

	g_object_unref (registry);

	return g_string_free (description, FALSE);
}

static gboolean
client_cache_emit_backend_died_idle_cb (gpointer user_data)
{
	SignalClosure *signal_closure = user_data;
	EAlert *alert;
	ESource *source;
	const gchar *alert_id = NULL;
	const gchar *extension_name;
	gchar *description;

	source = e_client_get_source (signal_closure->client);

	extension_name = E_SOURCE_EXTENSION_ADDRESS_BOOK;
	if (e_source_has_extension (source, extension_name))
		alert_id = "system:address-book-backend-died";

	extension_name = E_SOURCE_EXTENSION_CALENDAR;
	if (e_source_has_extension (source, extension_name))
		alert_id = "system:calendar-backend-died";

	extension_name = E_SOURCE_EXTENSION_MEMO_LIST;
	if (e_source_has_extension (source, extension_name))
		alert_id = "system:memo-list-backend-died";

	extension_name = E_SOURCE_EXTENSION_TASK_LIST;
	if (e_source_has_extension (source, extension_name))
		alert_id = "system:task-list-backend-died";

	g_return_val_if_fail (alert_id != NULL, FALSE);

	description = client_cache_build_source_description (
		signal_closure->cache, source);
	alert = e_alert_new (alert_id, description, NULL);
	g_free (description);

	g_signal_emit (
		signal_closure->cache,
		signals[BACKEND_DIED], 0,
		signal_closure->client,
		alert);

	g_object_unref (alert);

	return FALSE;
}

static gboolean
client_cache_emit_backend_error_idle_cb (gpointer user_data)
{
	SignalClosure *signal_closure = user_data;
	EAlert *alert;
	ESource *source;
	const gchar *alert_id = NULL;
	const gchar *extension_name;
	gchar *description;

	source = e_client_get_source (signal_closure->client);

	extension_name = E_SOURCE_EXTENSION_ADDRESS_BOOK;
	if (e_source_has_extension (source, extension_name))
		alert_id = "system:address-book-backend-error";

	extension_name = E_SOURCE_EXTENSION_CALENDAR;
	if (e_source_has_extension (source, extension_name))
		alert_id = "system:calendar-backend-error";

	extension_name = E_SOURCE_EXTENSION_MEMO_LIST;
	if (e_source_has_extension (source, extension_name))
		alert_id = "system:memo-list-backend-error";

	extension_name = E_SOURCE_EXTENSION_TASK_LIST;
	if (e_source_has_extension (source, extension_name))
		alert_id = "system:task-list-backend-error";

	g_return_val_if_fail (alert_id != NULL, FALSE);

	description = client_cache_build_source_description (
		signal_closure->cache, source);
	alert = e_alert_new (
		alert_id, description,
		signal_closure->error_message, NULL);
	g_free (description);

	g_signal_emit (
		signal_closure->cache,
		signals[BACKEND_ERROR], 0,
		signal_closure->client,
		alert);

	g_object_unref (alert);

	return FALSE;
}

static gboolean
client_cache_emit_client_notify_idle_cb (gpointer user_data)
{
	SignalClosure *signal_closure = user_data;
	const gchar *name;

	name = g_param_spec_get_name (signal_closure->pspec);

	g_signal_emit (
		signal_closure->cache,
		signals[CLIENT_NOTIFY],
		g_quark_from_string (name),
		signal_closure->client,
		signal_closure->pspec);

	return FALSE;
}

static gboolean
client_cache_emit_client_created_idle_cb (gpointer user_data)
{
	SignalClosure *signal_closure = user_data;

	g_signal_emit (
		signal_closure->cache,
		signals[CLIENT_CREATED], 0,
		signal_closure->client);

	return FALSE;
}

static void
client_cache_backend_died_cb (EClient *client,
                              ClientData *client_data)
{
	EClientCache *cache;

	cache = g_weak_ref_get (&client_data->cache);

	if (cache != NULL) {
		GSource *idle_source;
		SignalClosure *signal_closure;

		signal_closure = g_slice_new0 (SignalClosure);
		signal_closure->cache = g_object_ref (cache);
		signal_closure->client = g_object_ref (client);

		idle_source = g_idle_source_new ();
		g_source_set_callback (
			idle_source,
			client_cache_emit_backend_died_idle_cb,
			signal_closure,
			(GDestroyNotify) signal_closure_free);
		g_source_attach (idle_source, cache->priv->main_context);
		g_source_unref (idle_source);

		g_object_unref (cache);
	}
}

static void
client_cache_backend_error_cb (EClient *client,
                               const gchar *error_message,
                               ClientData *client_data)
{
	EClientCache *cache;

	cache = g_weak_ref_get (&client_data->cache);

	if (cache != NULL) {
		GSource *idle_source;
		SignalClosure *signal_closure;

		signal_closure = g_slice_new0 (SignalClosure);
		signal_closure->cache = g_object_ref (cache);
		signal_closure->client = g_object_ref (client);
		signal_closure->error_message = g_strdup (error_message);

		idle_source = g_idle_source_new ();
		g_source_set_callback (
			idle_source,
			client_cache_emit_backend_error_idle_cb,
			signal_closure,
			(GDestroyNotify) signal_closure_free);
		g_source_attach (idle_source, cache->priv->main_context);
		g_source_unref (idle_source);

		g_object_unref (cache);
	}
}

static void
client_cache_notify_cb (EClient *client,
                        GParamSpec *pspec,
                        ClientData *client_data)
{
	EClientCache *cache;

	cache = g_weak_ref_get (&client_data->cache);

	if (cache != NULL) {
		GSource *idle_source;
		SignalClosure *signal_closure;

		signal_closure = g_slice_new0 (SignalClosure);
		signal_closure->cache = g_object_ref (cache);
		signal_closure->client = g_object_ref (client);
		signal_closure->pspec = g_param_spec_ref (pspec);

		idle_source = g_idle_source_new ();
		g_source_set_callback (
			idle_source,
			client_cache_emit_client_notify_idle_cb,
			signal_closure,
			(GDestroyNotify) signal_closure_free);
		g_source_attach (idle_source, cache->priv->main_context);
		g_source_unref (idle_source);

		g_object_unref (cache);
	}
}

static void
client_cache_process_results (ClientData *client_data,
                              EClient *client,
                              const GError *error)
{
	GQueue queue = G_QUEUE_INIT;

	/* Sanity check. */
	g_return_if_fail (
		((client != NULL) && (error == NULL)) ||
		((client == NULL) && (error != NULL)));

	g_mutex_lock (&client_data->lock);

	/* Complete async operations outside the lock. */
	e_queue_transfer (&client_data->connecting, &queue);

	if (client != NULL) {
		EClientCache *cache;

		client_data->client = g_object_ref (client);

		cache = g_weak_ref_get (&client_data->cache);

		/* If the EClientCache has been disposed already,
		 * there's no point in connecting signal handlers. */
		if (cache != NULL) {
			GSource *idle_source;
			SignalClosure *signal_closure;
			gulong handler_id;

			/* client_data_dispose() will break the
			 * reference cycles we're creating here. */

			handler_id = g_signal_connect_data (
				client, "backend-died",
				G_CALLBACK (client_cache_backend_died_cb),
				client_data_ref (client_data),
				(GClosureNotify) client_data_unref,
				0);
			client_data->backend_died_handler_id = handler_id;

			handler_id = g_signal_connect_data (
				client, "backend-error",
				G_CALLBACK (client_cache_backend_error_cb),
				client_data_ref (client_data),
				(GClosureNotify) client_data_unref,
				0);
			client_data->backend_error_handler_id = handler_id;

			handler_id = g_signal_connect_data (
				client, "notify",
				G_CALLBACK (client_cache_notify_cb),
				client_data_ref (client_data),
				(GClosureNotify) client_data_unref,
				0);
			client_data->notify_handler_id = handler_id;

			signal_closure = g_slice_new0 (SignalClosure);
			signal_closure->cache = g_object_ref (cache);
			signal_closure->client = g_object_ref (client);

			idle_source = g_idle_source_new ();
			g_source_set_callback (
				idle_source,
				client_cache_emit_client_created_idle_cb,
				signal_closure,
				(GDestroyNotify) signal_closure_free);
			g_source_attach (
				idle_source, cache->priv->main_context);
			g_source_unref (idle_source);

			g_object_unref (cache);
		}
	}

	g_mutex_unlock (&client_data->lock);

	while (!g_queue_is_empty (&queue)) {
		GSimpleAsyncResult *simple;

		simple = g_queue_pop_head (&queue);
		if (client != NULL)
			g_simple_async_result_set_op_res_gpointer (
				simple, g_object_ref (client),
				(GDestroyNotify) g_object_unref);
		if (error != NULL)
			g_simple_async_result_set_from_error (simple, error);
		g_simple_async_result_complete (simple);
		g_object_unref (simple);
	}
}

static void
client_cache_book_connect_cb (GObject *source_object,
                              GAsyncResult *result,
                              gpointer user_data)
{
	ClientData *client_data = user_data;
	EClient *client;
	GError *error = NULL;

	client = e_book_client_connect_finish (result, &error);

	client_cache_process_results (client_data, client, error);

	if (client != NULL)
		g_object_unref (client);

	if (error != NULL)
		g_error_free (error);

	client_data_unref (client_data);
}

static void
client_cache_cal_connect_cb (GObject *source_object,
                             GAsyncResult *result,
                             gpointer user_data)
{
	ClientData *client_data = user_data;
	EClient *client;
	GError *error = NULL;

	client = e_cal_client_connect_finish (result, &error);

	client_cache_process_results (client_data, client, error);

	if (client != NULL)
		g_object_unref (client);

	if (error != NULL)
		g_error_free (error);

	client_data_unref (client_data);
}

static void
client_cache_set_registry (EClientCache *cache,
                           ESourceRegistry *registry)
{
	g_return_if_fail (E_IS_SOURCE_REGISTRY (registry));
	g_return_if_fail (cache->priv->registry == NULL);

	cache->priv->registry = g_object_ref (registry);
}

static void
client_cache_set_property (GObject *object,
                           guint property_id,
                           const GValue *value,
                           GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_REGISTRY:
			client_cache_set_registry (
				E_CLIENT_CACHE (object),
				g_value_get_object (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
client_cache_get_property (GObject *object,
                           guint property_id,
                           GValue *value,
                           GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_REGISTRY:
			g_value_take_object (
				value,
				e_client_cache_ref_registry (
				E_CLIENT_CACHE (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
client_cache_dispose (GObject *object)
{
	EClientCachePrivate *priv;

	priv = E_CLIENT_CACHE_GET_PRIVATE (object);

	g_clear_object (&priv->registry);

	g_hash_table_remove_all (priv->client_ht);

	if (priv->main_context != NULL) {
		g_main_context_unref (priv->main_context);
		priv->main_context = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_client_cache_parent_class)->dispose (object);
}

static void
client_cache_finalize (GObject *object)
{
	EClientCachePrivate *priv;

	priv = E_CLIENT_CACHE_GET_PRIVATE (object);

	g_hash_table_destroy (priv->client_ht);
	g_mutex_clear (&priv->client_ht_lock);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_client_cache_parent_class)->finalize (object);
}

static void
client_cache_constructed (GObject *object)
{
	/* Chain up to parent's constructed() method. */
	G_OBJECT_CLASS (e_client_cache_parent_class)->constructed (object);

	e_extensible_load_extensions (E_EXTENSIBLE (object));
}

static void
e_client_cache_class_init (EClientCacheClass *class)
{
	GObjectClass *object_class;

	g_type_class_add_private (class, sizeof (EClientCachePrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = client_cache_set_property;
	object_class->get_property = client_cache_get_property;
	object_class->dispose = client_cache_dispose;
	object_class->finalize = client_cache_finalize;
	object_class->constructed = client_cache_constructed;

	/**
	 * EClientCache:registry:
	 *
	 * The #ESourceRegistry manages #ESource instances.
	 **/
	g_object_class_install_property (
		object_class,
		PROP_REGISTRY,
		g_param_spec_object (
			"registry",
			"Registry",
			"Data source registry",
			E_TYPE_SOURCE_REGISTRY,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_STATIC_STRINGS));

	/**
	 * EClientCache::backend-died:
	 * @cache: the #EClientCache that received the signal
	 * @client: the #EClient that received the D-Bus notification
	 * @alert: an #EAlert with a user-friendly error description
	 *
	 * Rebroadcasts a #EClient::backend-died signal emitted by @client,
	 * along with a pre-formatted #EAlert.
	 *
	 * As a convenience to signal handlers, this signal is always emitted
	 * from the #GMainContext that was thread-default when the @cache was
	 * created.
	 **/
	signals[BACKEND_DIED] = g_signal_new (
		"backend-died",
		G_TYPE_FROM_CLASS (class),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (EClientCacheClass, backend_died),
		NULL, NULL, NULL,
		G_TYPE_NONE, 2,
		E_TYPE_CLIENT,
		E_TYPE_ALERT);

	/**
	 * EClientCache::backend-error:
	 * @cache: the #EClientCache that received the signal
	 * @client: the #EClient that received the D-Bus notification
	 * @alert: an #EAlert with a user-friendly error description
	 *
	 * Rebroadcasts a #EClient::backend-error signal emitted by @client,
	 * along with a pre-formatted #EAlert.
	 *
	 * As a convenience to signal handlers, this signal is always emitted
	 * from the #GMainContext that was thread-default when the @cache was
	 * created.
	 **/
	signals[BACKEND_ERROR] = g_signal_new (
		"backend-error",
		G_TYPE_FROM_CLASS (class),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (EClientCacheClass, backend_error),
		NULL, NULL, NULL,
		G_TYPE_NONE, 2,
		E_TYPE_CLIENT,
		E_TYPE_ALERT);

	/**
	 * EClientCache::client-created:
	 * @cache: the #EClientCache that received the signal
	 * @client: the newly-created #EClient
	 *
	 * This signal is emitted when a call to e_client_cache_get_client()
	 * triggers the creation of a new #EClient instance.
	 **/
	signals[CLIENT_CREATED] = g_signal_new (
		"client-created",
		G_TYPE_FROM_CLASS (class),
		G_SIGNAL_RUN_FIRST,
		G_STRUCT_OFFSET (EClientCacheClass, client_created),
		NULL, NULL, NULL,
		G_TYPE_NONE, 1,
		E_TYPE_CLIENT);

	/**
	 * EClientCache::client-notify:
	 * @cache: the #EClientCache that received the signal
	 * @client: the #EClient whose property changed
	 * @pspec: the #GParamSpec of the property that changed
	 *
	 * Rebroadcasts a #GObject::notify signal emitted by @client.
	 *
	 * This signal supports "::detail" appendices to the signal name
	 * just like the #GObject::notify signal, so you can connect to
	 * change notification signals for specific #EClient properties.
	 *
	 * As a convenience to signal handlers, this signal is always emitted
	 * from the #GMainContext that was thread-default when the @cache was
	 * created.
	 **/
	signals[CLIENT_NOTIFY] = g_signal_new (
		"client-notify",
		G_TYPE_FROM_CLASS (class),
		/* same flags as GObject::notify */
		G_SIGNAL_RUN_FIRST |
		G_SIGNAL_NO_RECURSE |
		G_SIGNAL_DETAILED |
		G_SIGNAL_NO_HOOKS |
		G_SIGNAL_ACTION,
		G_STRUCT_OFFSET (EClientCacheClass, client_notify),
		NULL, NULL, NULL,
		G_TYPE_NONE, 2,
		E_TYPE_CLIENT,
		G_TYPE_PARAM);
}

static void
e_client_cache_init (EClientCache *cache)
{
	GHashTable *client_ht;
	gint ii;

	const gchar *extension_names[] = {
		E_SOURCE_EXTENSION_ADDRESS_BOOK,
		E_SOURCE_EXTENSION_CALENDAR,
		E_SOURCE_EXTENSION_MEMO_LIST,
		E_SOURCE_EXTENSION_TASK_LIST
	};

	client_ht = g_hash_table_new_full (
		(GHashFunc) g_str_hash,
		(GEqualFunc) g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) g_hash_table_unref);

	cache->priv = E_CLIENT_CACHE_GET_PRIVATE (cache);

	cache->priv->main_context = g_main_context_ref_thread_default ();
	cache->priv->client_ht = client_ht;

	g_mutex_init (&cache->priv->client_ht_lock);

	/* Pre-load the extension names that can be used to instantiate
	 * EClients.  Then we can validate an extension name by testing
	 * for a matching hash table key. */

	for (ii = 0; ii < G_N_ELEMENTS (extension_names); ii++) {
		GHashTable *inner_ht;

		inner_ht = g_hash_table_new_full (
			(GHashFunc) e_source_hash,
			(GEqualFunc) e_source_equal,
			(GDestroyNotify) g_object_unref,
			(GDestroyNotify) client_data_dispose);

		g_hash_table_insert (
			client_ht,
			g_strdup (extension_names[ii]),
			g_hash_table_ref (inner_ht));

		g_hash_table_unref (inner_ht);
	}
}

/**
 * e_client_cache_new:
 * @registry: an #ESourceRegistry
 *
 * Creates a new #EClientCache instance.
 *
 * Returns: an #EClientCache
 **/
EClientCache *
e_client_cache_new (ESourceRegistry *registry)
{
	g_return_val_if_fail (E_IS_SOURCE_REGISTRY (registry), NULL);

	return g_object_new (
		E_TYPE_CLIENT_CACHE,
		"registry", registry, NULL);
}

/**
 * e_client_cache_ref_registry:
 * @cache: an #EClientCache
 *
 * Returns the #ESourceRegistry passed to e_client_cache_new().
 *
 * The returned #ESourceRegistry is referenced for thread-safety and must be
 * unreferenced with g_object_unref() when finished with it.
 *
 * Returns: an #ESourceRegistry
 **/
ESourceRegistry *
e_client_cache_ref_registry (EClientCache *cache)
{
	g_return_val_if_fail (E_IS_CLIENT_CACHE (cache), NULL);

	return g_object_ref (cache->priv->registry);
}

/**
 * e_client_cache_get_client_sync:
 * @cache: an #EClientCache
 * @source: an #ESource
 * @extension_name: an extension name
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Obtains a shared #EClient instance for @source, or else creates a new
 * #EClient instance to be shared.
 *
 * The @extension_name determines the type of #EClient to obtain.  Valid
 * @extension_name values are:
 *
 * #E_SOURCE_EXTENSION_ADDRESS_BOOK will obtain an #EBookClient.
 *
 * #E_SOURCE_EXTENSION_CALENDAR will obtain an #ECalClient with a
 * #ECalClient:source-type of #E_CAL_CLIENT_SOURCE_TYPE_EVENTS.
 *
 * #E_SOURCE_EXTENSION_MEMO_LIST will obtain an #ECalClient with a
 * #ECalClient:source-type of #E_CAL_CLIENT_SOURCE_TYPE_MEMOS.
 *
 * #E_SOURCE_EXTENSION_TASK_LIST will obtain an #ECalClient with a
 * #ECalClient:source-type of #E_CAL_CLIENT_SOURCE_TYPE_TASKS.
 *
 * The @source must already have an #ESourceExtension by that name
 * for this function to work.  All other @extension_name values will
 * result in an error.
 *
 * If a request for the same @source and @extension_name is already in
 * progress when this function is called, this request will "piggyback"
 * on the in-progress request such that they will both succeed or fail
 * simultaneously.
 *
 * Unreference the returned #EClient with g_object_unref() when finished
 * with it.  If an error occurs, the function will set @error and return
 * %NULL.
 *
 * Returns: an #EClient, or %NULL
 **/
EClient *
e_client_cache_get_client_sync (EClientCache *cache,
                                ESource *source,
                                const gchar *extension_name,
                                GCancellable *cancellable,
                                GError **error)
{
	EAsyncClosure *closure;
	GAsyncResult *result;
	EClient *client;

	g_return_val_if_fail (E_IS_CLIENT_CACHE (cache), NULL);
	g_return_val_if_fail (E_IS_SOURCE (source), NULL);
	g_return_val_if_fail (extension_name != NULL, NULL);

	closure = e_async_closure_new ();

	e_client_cache_get_client (
		cache, source, extension_name, cancellable,
		e_async_closure_callback, closure);

	result = e_async_closure_wait (closure);

	client = e_client_cache_get_client_finish (cache, result, error);

	e_async_closure_free (closure);

	return client;
}

/**
 * e_client_cache_get_client:
 * @cache: an #EClientCache
 * @source: an #ESource
 * @extension_name: an extension name
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: data to pass to the callback function
 *
 * Asynchronously obtains a shared #EClient instance for @source, or else
 * creates a new #EClient instance to be shared.
 *
 * The @extension_name determines the type of #EClient to obtain.  Valid
 * @extension_name values are:
 *
 * #E_SOURCE_EXTENSION_ADDRESS_BOOK will obtain an #EBookClient.
 *
 * #E_SOURCE_EXTENSION_CALENDAR will obtain an #ECalClient with a
 * #ECalClient:source-type of #E_CAL_CLIENT_SOURCE_TYPE_EVENTS.
 *
 * #E_SOURCE_EXTENSION_MEMO_LIST will obtain an #ECalClient with a
 * #ECalClient:source-type of #E_CAL_CLIENT_SOURCE_TYPE_MEMOS.
 *
 * #E_SOURCE_EXTENSION_TASK_LIST will obtain an #ECalClient with a
 * #ECalClient:source-type of #E_CAL_CLIENT_SOURCE_TYPE_TASKS.
 *
 * The @source must already have an #ESourceExtension by that name
 * for this function to work.  All other @extension_name values will
 * result in an error.
 *
 * If a request for the same @source and @extension_name is already in
 * progress when this function is called, this request will "piggyback"
 * on the in-progress request such that they will both succeed or fail
 * simultaneously.
 *
 * When the operation is finished, @callback will be called.  You can
 * then call e_client_cache_get_client_finish() to get the result of the
 * operation.
 **/
void
e_client_cache_get_client (EClientCache *cache,
                           ESource *source,
                           const gchar *extension_name,
                           GCancellable *cancellable,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
	GSimpleAsyncResult *simple;
	ClientData *client_data;
	EClient *client = NULL;
	gboolean connect_in_progress = FALSE;

	g_return_if_fail (E_IS_CLIENT_CACHE (cache));
	g_return_if_fail (E_IS_SOURCE (source));
	g_return_if_fail (extension_name != NULL);

	simple = g_simple_async_result_new (
		G_OBJECT (cache), callback,
		user_data, e_client_cache_get_client);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	client_data = client_ht_lookup (cache, source, extension_name);

	if (client_data == NULL) {
		g_simple_async_result_set_error (
			simple, G_IO_ERROR,
			G_IO_ERROR_INVALID_ARGUMENT,
			_("Cannot create a client object from "
			"extension name '%s'"), extension_name);
		g_simple_async_result_complete_in_idle (simple);
		goto exit;
	}

	g_mutex_lock (&client_data->lock);

	if (client_data->client != NULL) {
		client = g_object_ref (client_data->client);
	} else {
		GQueue *connecting = &client_data->connecting;
		connect_in_progress = !g_queue_is_empty (connecting);
		g_queue_push_tail (connecting, g_object_ref (simple));
	}

	g_mutex_unlock (&client_data->lock);

	/* If a cached EClient already exists, we're done. */
	if (client != NULL) {
		g_simple_async_result_set_op_res_gpointer (
			simple, client, (GDestroyNotify) g_object_unref);
		g_simple_async_result_complete_in_idle (simple);
		goto exit;
	}

	/* If an EClient connection attempt is already in progress, our
	 * cache request will complete when it finishes, so now we wait. */
	if (connect_in_progress)
		goto exit;

	/* Create an appropriate EClient instance for the extension
	 * name.  The client_ht_lookup() call above ensures us that
	 * one of these options will match. */

	if (g_str_equal (extension_name, E_SOURCE_EXTENSION_ADDRESS_BOOK)) {
		e_book_client_connect (
			source, cancellable,
			client_cache_book_connect_cb,
			client_data_ref (client_data));
		goto exit;
	}

	if (g_str_equal (extension_name, E_SOURCE_EXTENSION_CALENDAR)) {
		e_cal_client_connect (
			source, E_CAL_CLIENT_SOURCE_TYPE_EVENTS,
			cancellable, client_cache_cal_connect_cb,
			client_data_ref (client_data));
		goto exit;
	}

	if (g_str_equal (extension_name, E_SOURCE_EXTENSION_MEMO_LIST)) {
		e_cal_client_connect (
			source, E_CAL_CLIENT_SOURCE_TYPE_MEMOS,
			cancellable, client_cache_cal_connect_cb,
			client_data_ref (client_data));
		goto exit;
	}

	if (g_str_equal (extension_name, E_SOURCE_EXTENSION_TASK_LIST)) {
		e_cal_client_connect (
			source, E_CAL_CLIENT_SOURCE_TYPE_TASKS,
			cancellable, client_cache_cal_connect_cb,
			client_data_ref (client_data));
		goto exit;
	}

	g_warn_if_reached ();  /* Should never happen. */

exit:
	g_object_unref (simple);
}

/**
 * e_client_cache_get_client_finish:
 * @cache: an #EClientCache
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with e_client_cache_get_client().
 *
 * Unreference the returned #EClient with g_object_unref() when finished
 * with it.  If an error occurred, the function will set @error and return
 * %NULL.
 *
 * Returns: an #EClient, or %NULL
 **/
EClient *
e_client_cache_get_client_finish (EClientCache *cache,
                                  GAsyncResult *result,
                                  GError **error)
{
	GSimpleAsyncResult *simple;
	EClient *client;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (cache),
		e_client_cache_get_client), NULL);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	if (g_simple_async_result_propagate_error (simple, error))
		return NULL;

	client = g_simple_async_result_get_op_res_gpointer (simple);
	g_return_val_if_fail (client != NULL, NULL);

	return g_object_ref (client);
}

/**
 * e_client_cache_ref_cached_client:
 * @cache: an #EClientCache
 * @source: an #ESource
 * @extension_name: an extension name
 *
 * Returns a shared #EClient instance for @source and @extension_name if
 * such an instance is already cached, or else %NULL.  This function does
 * not create a new #EClient instance, and therefore does not block.
 *
 * See e_client_cache_get_client() for valid @extension_name values.
 *
 * The returned #EClient is referenced for thread-safety and must be
 * unreferenced with g_object_unref() when finished with it.
 *
 * Returns: an #EClient, or %NULL
 **/
EClient *
e_client_cache_ref_cached_client (EClientCache *cache,
                                  ESource *source,
                                  const gchar *extension_name)
{
	ClientData *client_data;
	EClient *client = NULL;

	g_return_val_if_fail (E_IS_CLIENT_CACHE (cache), NULL);
	g_return_val_if_fail (E_IS_SOURCE (source), NULL);
	g_return_val_if_fail (extension_name != NULL, NULL);

	client_data = client_ht_lookup (cache, source, extension_name);

	if (client_data != NULL) {
		g_mutex_lock (&client_data->lock);
		if (client_data->client != NULL)
			client = g_object_ref (client_data->client);
		g_mutex_unlock (&client_data->lock);

		client_data_unref (client_data);
	}

	return client;
}
