/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* test-source-selector.c - Test program for the ESourceSelector widget.
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include <e-util/e-util.h>

#define OPENED_KEY "sources-opened-key"
#define SOURCE_TYPE_KEY "sources-source-type-key"
#define EXTENSION_NAME_KEY "sources-extension-name-key"

static void
dump_selection (ESourceSelector *selector,
		const gchar *extension_name)
{
	GList *list, *link;

	list = e_source_selector_get_selection (selector);

	g_print ("Current selection at %s:\n", extension_name);

	if (list == NULL)
		g_print ("\t(None)\n");

	for (link = list; link != NULL; link = g_list_next (link->next)) {
		ESource *source = E_SOURCE (link->data);
		ESourceBackend *extension;

		extension = e_source_get_extension (source, extension_name);

		g_print (
			"\tSource %s (backend %s)\n",
			e_source_get_display_name (source),
			e_source_backend_get_backend_name (extension));
	}

	g_list_free_full (list, (GDestroyNotify) g_object_unref);
}

static void
selection_changed_callback (ESourceSelector *selector)
{
	g_print ("Selection changed!\n");
	dump_selection (selector, g_object_get_data (G_OBJECT (selector), EXTENSION_NAME_KEY));
}

static void
enable_widget_if_opened_cb (ESourceSelector *selector,
			    GtkWidget *widget)
{
	GHashTable *opened_sources;
	ESource *source;

	opened_sources = g_object_get_data (G_OBJECT (selector), OPENED_KEY);
	g_return_if_fail (opened_sources != NULL);

	source = e_source_selector_ref_primary_selection (selector);
	gtk_widget_set_sensitive (widget, source && g_hash_table_lookup (opened_sources, source) != NULL);
	if (source)
		g_object_unref (source);
}

static void
disable_widget_if_opened_cb (ESourceSelector *selector,
			     GtkWidget *widget)
{
	GHashTable *opened_sources;
	ESource *source;

	opened_sources = g_object_get_data (G_OBJECT (selector), OPENED_KEY);
	g_return_if_fail (opened_sources != NULL);

	source = e_source_selector_ref_primary_selection (selector);
	gtk_widget_set_sensitive (widget, source && g_hash_table_lookup (opened_sources, source) == NULL);
	if (source)
		g_object_unref (source);
}

static void
open_selected_clicked_cb (GtkWidget *button,
			  ESourceSelector *selector)
{
	GHashTable *opened_sources;
	ESource *source;

	opened_sources = g_object_get_data (G_OBJECT (selector), OPENED_KEY);
	g_return_if_fail (opened_sources != NULL);

	source = e_source_selector_ref_primary_selection (selector);
	if (!source)
		return;

	if (!g_hash_table_lookup (opened_sources, source)) {
		EClient *client;
		GError *error = NULL;
		ECalClientSourceType source_type;

		source_type = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (selector), SOURCE_TYPE_KEY));
		if (source_type == E_CAL_CLIENT_SOURCE_TYPE_LAST)
			client = e_book_client_connect_sync (source, NULL, &error);
		else
			client = e_cal_client_connect_sync (source, source_type, NULL, &error);
		if (error) {
			g_warning ("Failed to open '%s': %s", e_source_get_display_name (source), error->message);
		} else {
			g_hash_table_insert (opened_sources, g_object_ref (source), client);
			g_signal_emit_by_name (selector, "primary-selection-changed", 0);
		}
	}

	g_object_unref (source);
}

static void
close_selected_clicked_cb (GtkWidget *button,
			   ESourceSelector *selector)
{
	GHashTable *opened_sources;
	ESource *source;

	opened_sources = g_object_get_data (G_OBJECT (selector), OPENED_KEY);
	g_return_if_fail (opened_sources != NULL);

	source = e_source_selector_ref_primary_selection (selector);
	if (!source)
		return;

	if (g_hash_table_remove (opened_sources, source))
		g_signal_emit_by_name (selector, "primary-selection-changed", 0);

	g_object_unref (source);
}

static GtkWidget *
create_page (ESourceRegistry *registry,
	     const gchar *extension_name,
	     ECalClientSourceType source_type)
{
	GtkWidget *widget, *subwindow, *selector, *button_box;
	GtkGrid *grid;
	GHashTable *opened_sources;

	grid = GTK_GRID (gtk_grid_new ());

	subwindow = gtk_scrolled_window_new (NULL, NULL);
	g_object_set (G_OBJECT (subwindow),
		"halign", GTK_ALIGN_FILL,
		"hexpand", TRUE,
		"valign", GTK_ALIGN_FILL,
		"vexpand", TRUE,
		NULL);

	selector = e_source_selector_new (registry, extension_name);
	g_object_set (G_OBJECT (selector),
		"halign", GTK_ALIGN_FILL,
		"hexpand", TRUE,
		"valign", GTK_ALIGN_FILL,
		"vexpand", TRUE,
		"show-toggles", FALSE,
		"show-colors", source_type != E_CAL_CLIENT_SOURCE_TYPE_LAST,
		NULL);
	gtk_container_add (GTK_CONTAINER (subwindow), selector);

	gtk_grid_attach (grid, subwindow, 0, 0, 1, 5);

	button_box = gtk_button_box_new (GTK_ORIENTATION_VERTICAL);
	g_object_set (G_OBJECT (button_box),
		"halign", GTK_ALIGN_START,
		"hexpand", FALSE,
		"valign", GTK_ALIGN_START,
		"vexpand", FALSE,
		NULL);
	gtk_grid_attach (grid, button_box, 1, 0, 1, 1);

	widget = gtk_button_new_with_label ("Open selected");
	gtk_container_add (GTK_CONTAINER (button_box), widget);
	g_signal_connect (widget, "clicked", G_CALLBACK (open_selected_clicked_cb), selector);
	g_signal_connect (selector, "primary-selection-changed", G_CALLBACK (disable_widget_if_opened_cb), widget);

	widget = gtk_button_new_with_label ("Close selected");
	gtk_container_add (GTK_CONTAINER (button_box), widget);
	g_signal_connect (widget, "clicked", G_CALLBACK (close_selected_clicked_cb), selector);
	g_signal_connect (selector, "primary-selection-changed", G_CALLBACK (enable_widget_if_opened_cb), widget);

	widget = gtk_label_new ("");
	g_object_set (G_OBJECT (widget),
		"halign", GTK_ALIGN_FILL,
		"hexpand", FALSE,
		"valign", GTK_ALIGN_FILL,
		"vexpand", TRUE,
		NULL);
	gtk_grid_attach (grid, widget, 1, 1, 1, 1);

	widget = gtk_check_button_new_with_label ("Show colors");
	g_object_set (G_OBJECT (widget),
		"halign", GTK_ALIGN_START,
		"hexpand", FALSE,
		"valign", GTK_ALIGN_END,
		"vexpand", FALSE,
		NULL);
	gtk_grid_attach (grid, widget, 1, 2, 1, 1);

	g_object_bind_property (
		selector, "show-colors",
		widget, "active",
		G_BINDING_BIDIRECTIONAL |
		G_BINDING_SYNC_CREATE);

	widget = gtk_check_button_new_with_label ("Show icons");
	g_object_set (G_OBJECT (widget),
		"halign", GTK_ALIGN_START,
		"hexpand", FALSE,
		"valign", GTK_ALIGN_END,
		"vexpand", FALSE,
		NULL);
	gtk_grid_attach (grid, widget, 1, 3, 1, 1);

	g_object_bind_property (
		selector, "show-icons",
		widget, "active",
		G_BINDING_BIDIRECTIONAL |
		G_BINDING_SYNC_CREATE);

	widget = gtk_check_button_new_with_label ("Show toggles");
	g_object_set (G_OBJECT (widget),
		"halign", GTK_ALIGN_START,
		"hexpand", FALSE,
		"valign", GTK_ALIGN_END,
		"vexpand", FALSE,
		NULL);
	gtk_grid_attach (grid, widget, 1, 4, 1, 1);

	g_object_bind_property (
		selector, "show-toggles",
		widget, "active",
		G_BINDING_BIDIRECTIONAL |
		G_BINDING_SYNC_CREATE);

	opened_sources = g_hash_table_new_full (g_direct_hash, g_direct_equal, g_object_unref, g_object_unref);
	g_object_set_data_full (G_OBJECT (selector), OPENED_KEY, opened_sources, (GDestroyNotify) g_hash_table_unref);
	g_object_set_data (G_OBJECT (selector), SOURCE_TYPE_KEY, GUINT_TO_POINTER (source_type));
	g_object_set_data_full (G_OBJECT (selector), EXTENSION_NAME_KEY, g_strdup (extension_name), g_free);

	/* update buttons */
	g_signal_emit_by_name (selector, "primary-selection-changed", 0);

	g_signal_connect (
		selector, "selection-changed",
		G_CALLBACK (selection_changed_callback), NULL);

	return GTK_WIDGET (grid);
}

static gint
on_idle_create_widget (ESourceRegistry *registry)
{
	GtkWidget *window, *notebook;

	window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_window_set_default_size (GTK_WINDOW (window), 300, 400);

	g_signal_connect (
		window, "delete-event",
		G_CALLBACK (gtk_main_quit), NULL);

	notebook = gtk_notebook_new ();
	gtk_container_add (GTK_CONTAINER (window), GTK_WIDGET (notebook));

	gtk_notebook_append_page (
		GTK_NOTEBOOK (notebook),
		create_page (registry, E_SOURCE_EXTENSION_CALENDAR, E_CAL_CLIENT_SOURCE_TYPE_EVENTS),
		gtk_label_new ("Calendars"));

	gtk_notebook_append_page (
		GTK_NOTEBOOK (notebook),
		create_page (registry, E_SOURCE_EXTENSION_MEMO_LIST, E_CAL_CLIENT_SOURCE_TYPE_MEMOS),
		gtk_label_new ("Memos"));

	gtk_notebook_append_page (
		GTK_NOTEBOOK (notebook),
		create_page (registry, E_SOURCE_EXTENSION_TASK_LIST, E_CAL_CLIENT_SOURCE_TYPE_TASKS),
		gtk_label_new ("Tasks"));

	gtk_notebook_append_page (
		GTK_NOTEBOOK (notebook),
		create_page (registry, E_SOURCE_EXTENSION_ADDRESS_BOOK, E_CAL_CLIENT_SOURCE_TYPE_LAST),
		gtk_label_new ("Books"));

	gtk_widget_show_all (window);

	return FALSE;
}

gint
main (gint argc,
      gchar **argv)
{
	ESourceRegistry *registry;
	GError *error = NULL;

	gtk_init (&argc, &argv);

	registry = e_source_registry_new_sync (NULL, &error);

	if (error != NULL) {
		g_error (
			"Failed to load ESource registry: %s",
			error->message);
		g_assert_not_reached ();
	}

	g_idle_add ((GSourceFunc) on_idle_create_widget, registry);

	gtk_main ();

	g_object_unref (registry);

	return 0;
}
