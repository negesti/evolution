/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camelFolder.c : Abstract class for an email folder */

/* 
 *
 * Copyright (C) 1999 Bertrand Guiheneuf <Bertrand.Guiheneuf@aful.org> .
 *
 * This program is free software; you can redistribute it and/or 
 * modify it under the terms of the GNU General Public License as 
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */
#include <config.h>
#include "camel-folder.h"
#include "camel-log.h"
#include "string-utils.h"

static GtkObjectClass *parent_class=NULL;

/* Returns the class for a CamelFolder */
#define CF_CLASS(so) CAMEL_FOLDER_CLASS (GTK_OBJECT(so)->klass)

static void _init_with_store (CamelFolder *folder, CamelStore *parent_store, CamelException *ex);
static void _open (CamelFolder *folder, 
		   CamelFolderOpenMode mode, 
		   CamelFolderAsyncCallback callback, 
		   gpointer user_data, 
		   CamelException *ex);
static void _close (CamelFolder *folder, 
		    gboolean expunge, 
		    CamelFolderAsyncCallback callback, 
		    gpointer user_data, 
		    CamelException *ex);
/*  static void _set_name (CamelFolder *folder, const gchar *name, CamelException *ex); */
static void _set_name (CamelFolder *folder, 
		       const gchar *name, 
		       CamelFolderAsyncCallback callback, 
		       gpointer user_data, 
		       CamelException *ex);
/*  static void _set_full_name (CamelFolder *folder, const gchar *name); */
static const gchar *_get_name (CamelFolder *folder, CamelException *ex);
static const gchar *_get_full_name (CamelFolder *folder, CamelException *ex);
static gboolean _can_hold_folders (CamelFolder *folder, CamelException *ex);
static gboolean _can_hold_messages(CamelFolder *folder, CamelException *ex);
static gboolean _exists (CamelFolder  *folder, CamelException *ex);
static gboolean _is_open (CamelFolder *folder, CamelException *ex);
static CamelFolder *_get_folder (CamelFolder *folder, const gchar *folder_name, CamelException *ex);
static gboolean _create (CamelFolder *folder, CamelException *ex);
static gboolean _delete (CamelFolder *folder, gboolean recurse, CamelException *ex);
static gboolean _delete_messages (CamelFolder *folder, CamelException *ex);
static CamelFolder *_get_parent_folder (CamelFolder *folder, CamelException *ex);
static CamelStore *_get_parent_store (CamelFolder *folder, CamelException *ex);
static CamelFolderOpenMode _get_mode (CamelFolder *folder, CamelException *ex);
static GList *_list_subfolders (CamelFolder *folder, CamelException *ex);
static void _expunge (CamelFolder *folder, CamelException *ex);
static CamelMimeMessage *_get_message (CamelFolder *folder, gint number, CamelException *ex);
static gint _get_message_count (CamelFolder *folder, CamelException *ex);
static gint _append_message (CamelFolder *folder, CamelMimeMessage *message, CamelException *ex);
static const GList *_list_permanent_flags (CamelFolder *folder, CamelException *ex);
static void _copy_message_to (CamelFolder *folder, CamelMimeMessage *message, CamelFolder *dest_folder, CamelException *ex);

static const gchar *_get_message_uid (CamelFolder *folder, CamelMimeMessage *message, CamelException *ex);
static CamelMimeMessage *_get_message_by_uid (CamelFolder *folder, const gchar *uid, CamelException *ex);
static GList *_get_uid_list  (CamelFolder *folder, CamelException *ex);


static void _finalize (GtkObject *object);


static void
camel_folder_class_init (CamelFolderClass *camel_folder_class)
{
	GtkObjectClass *gtk_object_class = GTK_OBJECT_CLASS (camel_folder_class);
	
	parent_class = gtk_type_class (gtk_object_get_type ());
	
	/* virtual method definition */
	camel_folder_class->init_with_store = _init_with_store;
	camel_folder_class->open = _open;
	camel_folder_class->close = _close;
	camel_folder_class->set_name = _set_name;
	camel_folder_class->get_name = _get_name;
	camel_folder_class->get_full_name = _get_full_name;
	camel_folder_class->can_hold_folders = _can_hold_folders;
	camel_folder_class->can_hold_messages = _can_hold_messages;
	camel_folder_class->exists = _exists;
	camel_folder_class->is_open = _is_open;
	camel_folder_class->get_folder = _get_folder;
	camel_folder_class->create = _create;
	camel_folder_class->delete = _delete;
	camel_folder_class->delete_messages = _delete_messages;
	camel_folder_class->get_parent_folder = _get_parent_folder;
	camel_folder_class->get_parent_store = _get_parent_store;
	camel_folder_class->get_mode = _get_mode;
	camel_folder_class->list_subfolders = _list_subfolders;
	camel_folder_class->expunge = _expunge;
	camel_folder_class->get_message = _get_message;
	camel_folder_class->get_message_count = _get_message_count;
	camel_folder_class->append_message = _append_message;
	camel_folder_class->list_permanent_flags = _list_permanent_flags;
	camel_folder_class->copy_message_to;
	camel_folder_class->get_message_uid = _get_message_uid;
	camel_folder_class->get_message_by_uid = _get_message_by_uid;
	camel_folder_class->get_uid_list = _get_uid_list;

	/* virtual method overload */
	gtk_object_class->finalize = _finalize;
}







GtkType
camel_folder_get_type (void)
{
	static GtkType camel_folder_type = 0;
	
	if (!camel_folder_type)	{
		GtkTypeInfo camel_folder_info =	
		{
			"CamelFolder",
			sizeof (CamelFolder),
			sizeof (CamelFolderClass),
			(GtkClassInitFunc) camel_folder_class_init,
			(GtkObjectInitFunc) NULL,
				/* reserved_1 */ NULL,
				/* reserved_2 */ NULL,
			(GtkClassInitFunc) NULL,
		};
		
		camel_folder_type = gtk_type_unique (gtk_object_get_type (), &camel_folder_info);
	}
	
	return camel_folder_type;
}


static void           
_finalize (GtkObject *object)
{
	CamelFolder *camel_folder = CAMEL_FOLDER (object);
	GList *message_node;

	CAMEL_LOG_FULL_DEBUG ("Entering CamelFolder::finalize\n");

	g_free (camel_folder->name);
	g_free (camel_folder->full_name);
	g_free (camel_folder->permanent_flags);
	if (camel_folder->message_list) {
		/* unref all messages got from the folder */
		message_node = camel_folder->message_list;
		while (message_node) {
			gtk_object_unref (GTK_OBJECT (message_node->data));
			g_list_next (message_node);
		}
		g_list_free (camel_folder->message_list);
	}
	GTK_OBJECT_CLASS (parent_class)->finalize (object);
	CAMEL_LOG_FULL_DEBUG ("Leaving CamelFolder::finalize\n");
}


/**
 * _init_with_store: init the folder by setting its parent store.
 * @folder: folder object to initialize
 * @parent_store: parent store object of the folder
 * 
 * 
 **/
static void 
_init_with_store (CamelFolder *folder, CamelStore *parent_store, CamelException *ex)
{
	g_assert(folder);
	g_assert(parent_store);
	
	if (folder->parent_store) gtk_object_unref (GTK_OBJECT (folder->parent_store));
	folder->parent_store = parent_store;
	if (parent_store) gtk_object_ref (GTK_OBJECT (parent_store));
	folder->open_mode = FOLDER_OPEN_UNKNOWN;
	folder->open_state = FOLDER_CLOSE;
	folder->name = NULL;
	folder->full_name = NULL;
}





static void
_open (CamelFolder *folder, 
       CamelFolderOpenMode mode, 
       CamelFolderAsyncCallback callback, 
       gpointer user_data, 
       CamelException *ex)
{
/*  	folder->open_state = FOLDER_OPEN; */
/*  	folder->open_mode = mode; */
}

/**
 * camel_folder_open: Open a folder
 * @folder: The folder object
 * @mode: open mode (R/W/RW ?)
 * @callback: function to call when the operation is over
 * @user_data: data to pass to the callback 
 * @ex: exception object
 *
 * Open a folder in a given mode. When the opration is over
 * the callback is called and the client program can determine
 * if the operation suceeded by examining the exception. 
 * 
 **/
void 
camel_folder_open (CamelFolder *folder, 
		   CamelFolderOpenMode mode, 
		   CamelFolderAsyncCallback callback, 
		   gpointer user_data, 
		   CamelException *ex)
{	
	CF_CLASS(folder)->open (folder, mode, callback, user_data, ex);
}




static void
_close (CamelFolder *folder, 
	gboolean expunge, 
	CamelFolderAsyncCallback callback, 
	gpointer user_data, 
	CamelException *ex)
{	
	folder->open_state = FOLDER_CLOSE;
}

/**
 * camel_folder_close: Close a folder.
 * @folder: The folder object
 * @expunge: if TRUE, the flagged message are deleted.
 * @callback: function to call when the operation is over
 * @user_data: data to pass to the callback 
 * @ex: exception object
 *
 * Put a folder in its closed state, and possibly 
 * expunge the flagged messages. The callback is called 
 * when the operation is over and the client program can determine
 * if the operation suceeded by examining the exception. 
 * 
 **/
void 
camel_folder_close (CamelFolder *folder, 
		    gboolean expunge, 
		    CamelFolderAsyncCallback callback, 
		    gpointer user_data, 
		    CamelException *ex)
{
	CF_CLASS(folder)->close (folder, expunge, callback, user_data, ex);
}



static void
_set_name (CamelFolder *folder, 
	   const gchar *name, 
	   CamelFolderAsyncCallback callback, 
	   gpointer user_data, 
	   CamelException *ex)
{
	gchar separator;
	gchar *full_name;
	const gchar *parent_full_name;
	
	g_assert (folder);
	g_assert (name);
	g_assert (folder->parent_store);
	
	g_free (folder->name);
	g_free (folder->full_name);
	
	separator = camel_store_get_separator (folder->parent_store);
	
	if (folder->parent_folder) {
		parent_full_name = camel_folder_get_full_name (folder->parent_folder, ex);
		full_name = g_strdup_printf ("%s%d%s", parent_full_name, separator, name);
		
	} else {
		full_name = g_strdup (name);
	}
	
	folder->name = g_strdup (name);
	folder->full_name = full_name;
	
}


/**
 * camel_folder_set_name:set the (short) name of the folder
 * @folder: folder
 * @name: new name of the folder
 * 
 * set the name of the folder. 
 * 
 * 
 **/
void 
camel_folder_set_name (CamelFolder *folder, 
		       const gchar *name, 
		       CamelFolderAsyncCallback callback, 
		       gpointer user_data,
		       CamelException *ex)
{
	CF_CLASS(folder)->set_name (folder, name, callback, user_data, ex);
}


#if 0
static void
_set_full_name (CamelFolder *folder, const gchar *name, CamelException *ex)
{
	g_free(folder->full_name);
	folder->full_name = g_strdup (name);
}


/**
 * camel_folder_set_full_name:set the (full) name of the folder
 * @folder: folder
 * @name: new name of the folder
 * 
 * set the name of the folder. 
 * 
 **/
void 
camel_folder_set_full_name (CamelFolder *folder, const gchar *name, CamelException *ex)
{
	CF_CLASS(folder)->set_full_name (folder, name, ex);
}
#endif

static const gchar *
_get_name (CamelFolder *folder, CamelException *ex)
{
	return folder->name;
}


/**
 * camel_folder_get_name: get the (short) name of the folder
 * @folder: 
 * 
 * get the name of the folder. The fully qualified name
 * can be obtained with the get_full_ame method (not implemented)
 *
 * Return value: name of the folder
 **/
const gchar *
camel_folder_get_name (CamelFolder *folder, CamelException *ex)
{
	return CF_CLASS(folder)->get_name (folder, ex);
}



static const gchar *
_get_full_name (CamelFolder *folder, CamelException *ex)
{
	return folder->full_name;
}

/**
 * camel_folder_get_full_name:get the (full) name of the folder
 * @folder: folder to get the name 
 * 
 * get the name of the folder. 
 * 
 * Return value: full name of the folder
 **/
const gchar *
camel_folder_get_full_name (CamelFolder *folder, CamelException *ex)
{
	return CF_CLASS(folder)->get_full_name (folder, ex);
}


/**
 * _can_hold_folders: tests if the folder can contain other folders
 * @folder: The folder object 
 * 
 * Tests if a folder can contain other folder 
 * (as for example MH folders)
 * 
 * Return value: 
 **/
static gboolean
_can_hold_folders (CamelFolder *folder, CamelException *ex)
{
	return folder->can_hold_folders;
}




/**
 * _can_hold_messages: tests if the folder can contain messages
 * @folder: The folder object
 * 
 * Tests if a folder object can contain messages. 
 * In the case it can not, it most surely can only 
 * contain folders (rare).
 * 
 * Return value: true if it can contain messages false otherwise
 **/
static gboolean
_can_hold_messages (CamelFolder *folder, CamelException *ex)
{
	return folder->can_hold_messages;
}



static gboolean
_exists (CamelFolder *folder, CamelException *ex)
{
	return FALSE;
}


/**
 * _exists: tests if the folder object exists in its parent store.
 * @folder: folder object
 * 
 * Test if a folder exists on a store. A folder can be 
 * created without physically on a store. In that case, 
 * use CamelFolder::create to create it 
 * 
 * Return value: true if the folder exists on the store false otherwise 
 **/
gboolean
camel_folder_exists (CamelFolder *folder, CamelException *ex)
{
	return (CF_CLASS(folder)->exists (folder, ex));
}



/**
 * _is_open: test if the folder is open 
 * @folder: The folder object
 * 
 * Tests if a folder is open. If not open it can be opened 
 * CamelFolder::open
 * 
 * Return value: true if the folder exists, false otherwise
 **/
static gboolean
_is_open (CamelFolder *folder, CamelException *ex)
{
	return (folder->open_state == FOLDER_OPEN);
} 





static CamelFolder *
_get_folder (CamelFolder *folder, const gchar *folder_name, CamelException *ex)
{
	CamelFolder *new_folder;
	gchar *full_name;
	const gchar *current_folder_full_name;
	gchar separator;
	
	g_assert (folder);
	g_assert (folder_name);
	
	if (!folder->parent_store) return NULL;
	
	current_folder_full_name = camel_folder_get_full_name (folder, ex);
	if (!current_folder_full_name) return NULL;
	
	separator = camel_store_get_separator (folder->parent_store);
	full_name = g_strdup_printf ("%s%d%s", current_folder_full_name, separator, folder_name);
	
	new_folder = camel_store_get_folder (folder->parent_store, full_name);
	return new_folder;
}



/**
 * camel_folder_get_folder: return the (sub)folder object that is specified
 * @folder: the folder
 * @folder_name: subfolder path
 * 
 * This method returns a folder objects. This folder
 * is necessarily a subfolder of the current folder. 
 * It is an error to ask a folder begining with the 
 * folder separator character.  
 * 
 * Return value: Required folder. NULL if the subfolder object  could not be obtained
 **/
CamelFolder *
camel_folder_get_folder (CamelFolder *folder, gchar *folder_name, CamelException *ex)
{
	return (CF_CLASS(folder)->get_folder(folder,folder_name, ex));
}




/**
 * _create: creates a folder on its store
 * @folder: a CamelFolder object.
 * 
 * this routine handles the recursion mechanism.
 * Children classes have to implement the actual
 * creation mechanism. They must call this method
 * before physically creating the folder in order
 * to be sure the parent folder exists.
 * Calling this routine on an existing folder is
 * not an error, and returns %TRUE.
 * 
 * Return value: %TRUE if the folder exists, %FALSE otherwise 
 **/
static gboolean
_create(CamelFolder *folder, CamelException *ex)
{
	gchar *prefix;
	gchar dich_result;
	CamelFolder *parent;
	gchar sep;
	
	g_assert (folder);
	g_assert (folder->parent_store);
	g_assert (folder->name);
	
	if (CF_CLASS(folder)->exists (folder, ex))
		return TRUE;
	
	sep = camel_store_get_separator (folder->parent_store);	
	if (folder->parent_folder)
		camel_folder_create (folder->parent_folder, ex);
	else {   
		if (folder->full_name) {
			dich_result = string_dichotomy (
							folder->full_name, sep, &prefix, NULL,
							STRING_DICHOTOMY_STRIP_TRAILING | STRING_DICHOTOMY_RIGHT_DIR);
			if (dich_result!='o') {
				g_warning("I have to handle the case where the path is not OK\n"); 
				return FALSE;
			} else {
				parent = camel_store_get_folder (folder->parent_store, prefix);
				camel_folder_create (parent, ex);
			}
		}
	}	
	return TRUE;
}


/**
 * camel_folder_create: create the folder object on the physical store
 * @folder: folder object to create
 * 
 * This routine physically creates the folder object on 
 * the store. Having created the  object does not
 * mean the folder physically exists. If it does not
 * exists, this routine will create it.
 * if the folder full name contains more than one level
 * of hierarchy, all folders between the current folder
 * and the last folder name will be created if not existing.
 * 
 * Return value: 
 **/
gboolean
camel_folder_create (CamelFolder *folder, CamelException *ex)
{
	return (CF_CLASS(folder)->create(folder, ex));
}





/**
 * _delete: delete folder 
 * @folder: folder to delete
 * @recurse: true is subfolders must also be deleted
 * 
 * Delete a folder and its subfolders (if recurse is TRUE).
 * The scheme is the following:
 * 1) delete all messages in the folder
 * 2) if recurse is FALSE, and if there are subfolders
 *    return FALSE, else delete current folder and retuen TRUE
 *    if recurse is TRUE, delete subfolders, delete
 *    current folder and return TRUE
 * 
 * subclasses implementing a protocol with a different 
 * deletion behaviour must emulate this one or implement
 * empty folders deletion and call  this routine which 
 * will do all the works for them.
 * Opertions must be done in the folllowing order:
 *  - call this routine
 *  - delete empty folder
 * 
 * Return value: true if the folder has been deleted
 **/
static gboolean
_delete (CamelFolder *folder, gboolean recurse, CamelException *ex)
{
	GList *subfolders=NULL;
	GList *sf;
	gboolean ok;
	
	g_assert(folder);
	
	/* method valid only on closed folders */
	if (folder->open_state != FOLDER_CLOSE) return FALSE;
	
	/* delete all messages in the folder */
	CF_CLASS(folder)->delete_messages(folder, ex);
	
	subfolders = CF_CLASS(folder)->list_subfolders(folder, ex); 
	if (recurse) { /* delete subfolders */
		if (subfolders) {
			sf = subfolders;
			do {
				/*  CF_CLASS(sf->data)->delete(sf->data, TRUE, ex); */
			} while (sf = sf->next);
		}
	} else if (subfolders) return FALSE;
	
	
	return TRUE;
}



/**
 * camel_folder_delete: delete a folder
 * @folder: folder to delete
 * @recurse: TRUE if subfolders must be deleted
 * 
 * Delete a folder. All messages in the folder 
 * are deleted before the folder is deleted. 
 * When recurse is true, all subfolders are
 * deleted too. When recurse is FALSE and folder 
 * contains subfolders, all messages are deleted,
 * but folder deletion fails. 
 * 
 * Return value: TRUE if deletion was successful
 **/
gboolean camel_folder_delete (CamelFolder *folder, gboolean recurse, CamelException *ex)
{
	return CF_CLASS(folder)->delete(folder, recurse, ex);
}





/**
 * _delete_messages: delete all messages in the folder
 * @folder: 
 * 
 * 
 * 
 * Return value: 
 **/
static gboolean 
_delete_messages (CamelFolder *folder, CamelException *ex)
{
	return TRUE;
}


/**
 * camel_folder_delete_messages: delete all messages in the folder
 * @folder: folder 
 * 
 * delete all messages stored in a folder
 * 
 * Return value: TRUE if the messages could be deleted
 **/
gboolean
camel_folder_delete_messages (CamelFolder *folder, CamelException *ex)
{
	return CF_CLASS(folder)->delete_messages(folder, ex);
}






/**
 * _get_parent_folder: return parent folder
 * @folder: folder to get the parent
 * 
 * 
 * 
 * Return value: 
 **/
static CamelFolder *
_get_parent_folder (CamelFolder *folder, CamelException *ex)
{
	return folder->parent_folder;
}


/**
 * camel_folder_get_parent_folder:return parent folder
 * @folder: folder to get the parent
 * 
 * 
 * 
 * Return value: 
 **/
CamelFolder *
camel_folder_get_parent_folder (CamelFolder *folder, CamelException *ex)
{
	return CF_CLASS(folder)->get_parent_folder(folder, ex);
}


/**
 * _get_parent_store: return parent store
 * @folder: folder to get the parent
 * 
 * 
 * 
 * Return value: 
 **/
static CamelStore *
_get_parent_store (CamelFolder *folder, CamelException *ex)
{
	return folder->parent_store;
}


/**
 * camel_folder_get_parent_store:return parent store
 * @folder: folder to get the parent
 * 
 * 
 * 
 * Return value: 
 **/
CamelStore *
camel_folder_get_parent_store (CamelFolder *folder, CamelException *ex)
{
	return CF_CLASS(folder)->get_parent_store(folder, ex);
}



/**
 * _get_mode: return the open mode of a folder
 * @folder: 
 * 
 * 
 * 
 * Return value:  open mode of the folder
 **/
static CamelFolderOpenMode
_get_mode (CamelFolder *folder, CamelException *ex)
{
	return folder->open_mode;
}


/**
 * camel_folder_get_mode: return the open mode of a folder
 * @folder: 
 * 
 * 
 * 
 * Return value:  open mode of the folder
 **/
CamelFolderOpenMode
camel_folder_get_mode (CamelFolder *folder, CamelException *ex)
{
	return CF_CLASS(folder)->get_mode(folder, ex);
}




static GList *
_list_subfolders (CamelFolder *folder, CamelException *ex)
{
	return NULL;
}


/**
 * camel_folder_list_subfolders: list subfolders in a folder
 * @folder: the folder
 * 
 * List subfolders in a folder. 
 * 
 * Return value: list of subfolders
 **/
GList *
camel_folder_list_subfolders (CamelFolder *folder, CamelException *ex)
{
	return CF_CLASS(folder)->list_subfolders(folder, ex);
}




static void
_expunge (CamelFolder *folder, CamelException *ex)
{

}

/* util func. Should not stay here */
gint
camel_mime_message_number_cmp (gconstpointer a, gconstpointer b)
{
	CamelMimeMessage *m_a = CAMEL_MIME_MESSAGE (a);
	CamelMimeMessage *m_b = CAMEL_MIME_MESSAGE (b);

	return (m_a->message_number - (m_b->message_number));
}

/**
 * camel_folder_expunge: physically delete messages marked as "DELETED"
 * @folder: the folder
 * 
 * Delete messages which have been marked as  "DELETED"
 * 
 * 
 * Return value: list of expunged message objects.
 **/
GList *
camel_folder_expunge (CamelFolder *folder, gboolean want_list, CamelException *ex)
{
	GList *expunged_list = NULL;
	CamelMimeMessage *message;
	GList *message_node;
	GList *next_message_node;
	guint nb_expunged = 0;

	
	/* sort message list by ascending message number */
	if (folder->message_list)
		folder->message_list = g_list_sort (folder->message_list, camel_mime_message_number_cmp);

	/* call provider method, 
	 *  PROVIDERS MUST SET THE EXPUNGED FLAGS TO TRUE
	 * when they expunge a message of the active message list */
	CF_CLASS (folder)->expunge (folder, ex);
	
	message_node = folder->message_list;

	/* look in folder message list which messages
	 * need to be expunged  */
	while ( message_node) {
		message = CAMEL_MIME_MESSAGE (message_node->data);

		/* we may free message_node so get the next node now */
		next_message_node = message_node->next;

		if (message) {
			CAMEL_LOG_FULL_DEBUG ("CamelFolder::expunge, examining message %d\n", message->message_number);
			if (message->expunged) {
				if (want_list) 
					expunged_list = g_list_append (expunged_list, message);
				/* remove the message from active message list */
				g_list_remove_link (folder->message_list, message_node);
				g_list_free_1 (message_node);
				nb_expunged++;
			} else {
				/* readjust message number */
				CAMEL_LOG_FULL_DEBUG ("CamelFolder:: Readjusting message number %d", 
						      message->message_number);
				message->message_number -= nb_expunged;
				CAMEL_LOG_FULL_DEBUG (" to %d\n", message->message_number);
			}
		}
		else {
			CAMEL_LOG_WARNING ("CamelFolder::expunge warning message_node contains no message\n");
		}
		message_node = next_message_node;
		CAMEL_LOG_FULL_DEBUG ("CamelFolder::expunge, examined message node %p\n", message_node);
	}
	
	return expunged_list;
}



static CamelMimeMessage *
_get_message (CamelFolder *folder, gint number, CamelException *ex)
{
	return NULL;
}




/**
 * _get_message: return the message corresponding to that number in the folder
 * @folder: a CamelFolder object
 * @number: the number of the message within the folder.
 * 
 * Return the message corresponding to that number within the folder.
 * 
 * Return value: A pointer on the corresponding message or NULL if no corresponding message exists
 **/
CamelMimeMessage *
camel_folder_get_message (CamelFolder *folder, gint number, CamelException *ex)
{
#warning this code has nothing to do here. 
	CamelMimeMessage *a_message;
	CamelMimeMessage *new_message = NULL;
	GList *message_node;
	
	message_node = folder->message_list;
	CAMEL_LOG_FULL_DEBUG ("CamelFolder::get_message Looking for message nummber %d\n", number);
	/* look in folder message list if the 
	 * if the message has not already been retreived */
	while ((!new_message) && message_node) {
		a_message = CAMEL_MIME_MESSAGE (message_node->data);
		
		if (a_message) {
			CAMEL_LOG_FULL_DEBUG ("CamelFolder::get_message "
					      "found message number %d in the active list\n",
					      a_message->message_number);
			if (a_message->message_number == number) {
				CAMEL_LOG_FULL_DEBUG ("CamelFolder::get_message message "
						      "%d already retreived once: returning %pOK\n", 
						      number, a_message);
				new_message = a_message;
			} 
		} else {
			CAMEL_LOG_WARNING ("CamelFolder::get_message "
					   " problem in the active list, a message was NULL\n");
		}
		message_node = message_node->next;
		
		CAMEL_LOG_FULL_DEBUG ("CamelFolder::get_message message node = %p\n", message_node);
	}
	if (!new_message) new_message = CF_CLASS (folder)->get_message (folder, number, ex);
	if (!new_message) return NULL;

	/* if the message has not been already put in 
	 * this folder active message list, put it in */
	if ((!folder->message_list) || (!g_list_find (folder->message_list, new_message)))
	    folder->message_list = g_list_append (folder->message_list, new_message);
	
	return new_message;
}


static gint
_get_message_count (CamelFolder *folder, CamelException *ex)
{
	return -1;
}



/**
 * camel_folder_get_message_count: get the number of messages in the folder
 * @folder: A CamelFolder object
 * 
 * Returns the number of messages in the folder.
 * 
 * Return value: the number of messages or -1 if unknown.
 **/
gint
camel_folder_get_message_count (CamelFolder *folder, CamelException *ex)
{
	return CF_CLASS (folder)->get_message_count (folder, ex);
}


static gint
_append_message (CamelFolder *folder, CamelMimeMessage *message, CamelException *ex)
{
	return -1;
}


gint camel_folder_append_message (CamelFolder *folder, CamelMimeMessage *message, CamelException *ex)
{	
	return  CF_CLASS (folder)->append_message (folder, message, ex);
}


static const GList *
_list_permanent_flags (CamelFolder *folder, CamelException *ex)
{
	return folder->permanent_flags;
}


const GList *
camel_folder_list_permanent_flags (CamelFolder *folder, CamelException *ex)
{
	return CF_CLASS (folder)->list_permanent_flags (folder, ex);
}




static void
_copy_message_to (CamelFolder *folder, CamelMimeMessage *message, CamelFolder *dest_folder, CamelException *ex)
{
	camel_folder_append_message (dest_folder, message, ex);
}


void
camel_folder_copy_message_to (CamelFolder *folder, CamelMimeMessage *message, CamelFolder *dest_folder, CamelException *ex)
{
	CF_CLASS (folder)->copy_message_to (folder, message, dest_folder, ex);;
}





/* summary stuff */

gboolean
camel_folder_has_summary_capability (CamelFolder *folder, CamelException *ex)
{
	return folder->has_summary_capability;
}


CamelFolderSummary *
camel_folder_get_summary (CamelFolder *folder, CamelException *ex)
{
	return folder->summary;
}




/* UIDs stuff */

/**
 * camel_folder_has_uid_capability: detect if the folder support UIDs
 * @folder: Folder object
 * 
 * Detects if a folder supports UID operations, that is
 * reference messages by a Unique IDentifier instead
 * of by message number.  
 * 
 * Return value: TRUE if the folder supports UIDs 
 **/
gboolean
camel_folder_has_uid_capability (CamelFolder *folder, CamelException *ex)
{
	return folder->has_uid_capability;
}



static const gchar *
_get_message_uid (CamelFolder *folder, CamelMimeMessage *message, CamelException *ex)
{
	return NULL;
}

/**
 * camel_folder_get_message_uid: get the UID of a message in a folder
 * @folder: Folder in which the UID must refer to
 * @message: Message object 
 * 
 * Return the UID of a message relatively to a folder.
 * A message can have different UID, each one corresponding
 * to a different folder, if the message is referenced in
 * several folders. 
 * 
 * Return value: The UID of the message in the folder
 **/
const gchar * 
camel_folder_get_message_uid (CamelFolder *folder, CamelMimeMessage *message, CamelException *ex)
{
	if (!folder->has_uid_capability) return NULL;
	return CF_CLASS (folder)->get_message_uid (folder, message, ex);
}



/* the next two func are left there temporarily */
static const gchar *
_get_message_uid_by_number (CamelFolder *folder, gint message_number, CamelException *ex)
{
	return NULL;
}

/**
 * camel_folder_get_message_uid_by_number: get the UID corresponding to a message number
 * @folder: Folder object
 * @message_number: Message number
 * 
 * get the UID corresponding to a message number. 
 * Use of this routine should be avoiding, as on 
 * folders supporting UIDs, message numbers should
 * not been used.
 * 
 * Return value: 
 **/
const gchar * 
camel_folder_get_message_uid_by_number (CamelFolder *folder, gint message_number, CamelException *ex)
{
	//if (!folder->has_uid_capability) return NULL;
	//return CF_CLASS (folder)->get_message_uid_by_number (folder, message_number, ex);
}


static CamelMimeMessage *
_get_message_by_uid (CamelFolder *folder, const gchar *uid, CamelException *ex)
{
	return NULL;
}


/**
 * camel_folder_get_message_by_uid: Get a message by its UID in a folder
 * @folder: the folder object
 * @uid: the UID
 * 
 * Get a message from its UID in the folder. Messages 
 * are cached within a folder, that is, asking twice
 * for the same UID returns the same message object.
 * 
 * Return value: Message corresponding to the UID
 **/
CamelMimeMessage *
camel_folder_get_message_by_uid  (CamelFolder *folder, const gchar *uid, CamelException *ex)
{
	if (!folder->has_uid_capability) return NULL;
	return CF_CLASS (folder)->get_message_by_uid (folder, uid, ex);
}

static GList *
_get_uid_list  (CamelFolder *folder, CamelException *ex)
{
	return NULL;
}

/**
 * camel_folder_get_uid_list: get the list of UID in a folder
 * @folder: folder object
 * 
 * get the list of UID available in a folder. This
 * routine is usefull to know what messages are
 * available when the folder does not support
 * summaries. The UIDs in the list must not be freed,
 * the folder object caches them.
 * 
 * Return value: Glist of UID correspondind to the messages available in the folder.
 **/
GList *
camel_folder_get_uid_list  (CamelFolder *folder, CamelException *ex)
{
	if (!folder->has_uid_capability) return NULL;
	return CF_CLASS (folder)->get_uid_list (folder, ex);
}


/* **** */
