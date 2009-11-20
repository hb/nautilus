/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nautilus-dnd.c - Common Drag & drop handling code shared by the icon container
   and the list view.

   Copyright (C) 2000, 2001 Eazel, Inc.

   The Gnome Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   The Gnome Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with the Gnome Library; see the file COPYING.LIB.  If not,
   write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.

   Authors: Pavel Cisler <pavel@eazel.com>,
   	    Ettore Perazzoli <ettore@gnu.org>
*/

/* FIXME: This should really be back in Nautilus, not here in Eel. */

#include <config.h>
#include "nautilus-dnd.h"

#include "nautilus-program-choosing.h"
#include "nautilus-link.h"
#include "nautilus-window-slot-info.h"
#include "nautilus-window-info.h"
#include "nautilus-view.h"
#include <eel/eel-glib-extensions.h>
#include <eel/eel-gtk-extensions.h>
#include <eel/eel-string.h>
#include <eel/eel-vfs-extensions.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <libnautilus-private/nautilus-file-utilities.h>
#include <stdio.h>
#include <string.h>

/* a set of defines stolen from the eel-icon-dnd.c file.
 * These are in microseconds.
 */
#define AUTOSCROLL_TIMEOUT_INTERVAL 100
#define AUTOSCROLL_INITIAL_DELAY 100000

/* drag this close to the view edge to start auto scroll*/
#define AUTO_SCROLL_MARGIN 30

/* the smallest amount of auto scroll used when we just enter the autoscroll
 * margin
 */
#define MIN_AUTOSCROLL_DELTA 5
	 
/* the largest amount of auto scroll used when we are right over the view
 * edge
 */
#define MAX_AUTOSCROLL_DELTA 50

void
nautilus_drag_init (NautilusDragInfo     *drag_info,
		    const GtkTargetEntry *drag_types,
		    int                   drag_type_count,
		    gboolean              add_text_targets)
{
	drag_info->target_list = gtk_target_list_new (drag_types,
						   drag_type_count);

	if (add_text_targets) {
		gtk_target_list_add_text_targets (drag_info->target_list,
						  NAUTILUS_ICON_DND_TEXT);
	}

	drag_info->drop_occured = FALSE;
	drag_info->need_to_destroy = FALSE;
}

void
nautilus_drag_finalize (NautilusDragInfo *drag_info)
{
	gtk_target_list_unref (drag_info->target_list);
	nautilus_drag_destroy_selection_list (drag_info->selection_list);

	g_free (drag_info);
}


/* Functions to deal with NautilusDragSelectionItems.  */

NautilusDragSelectionItem *
nautilus_drag_selection_item_new (void)
{
	return g_new0 (NautilusDragSelectionItem, 1);
}

static void
drag_selection_item_destroy (NautilusDragSelectionItem *item)
{
	g_free (item->uri);
	g_free (item);
}

void
nautilus_drag_destroy_selection_list (GList *list)
{
	GList *p;

	if (list == NULL)
		return;

	for (p = list; p != NULL; p = p->next)
		drag_selection_item_destroy (p->data);

	g_list_free (list);
}

char **
nautilus_drag_uri_array_from_selection_list (const GList *selection_list)
{
	GList *uri_list;
	char **uris;

	uri_list = nautilus_drag_uri_list_from_selection_list (selection_list);
	uris = nautilus_drag_uri_array_from_list (uri_list);
	eel_g_list_free_deep (uri_list);

	return uris;
}

GList *
nautilus_drag_uri_list_from_selection_list (const GList *selection_list)
{
	NautilusDragSelectionItem *selection_item;
	GList *uri_list;
	const GList *l;

	uri_list = NULL;
	for (l = selection_list; l != NULL; l = l->next) {
		selection_item = (NautilusDragSelectionItem *) l->data;
		if (selection_item->uri != NULL) {
			uri_list = g_list_prepend (uri_list, g_strdup (selection_item->uri));
		}
	}

	return g_list_reverse (uri_list);
}

char **
nautilus_drag_uri_array_from_list (const GList *uri_list)
{
	const GList *l;
	char **uris;
	int i;

	if (uri_list == NULL) {
		return NULL;
	}

	uris = g_new0 (char *, g_list_length ((GList *) uri_list));
	for (i = 0, l = uri_list; l != NULL; l = l->next) {
		uris[i++] = g_strdup ((char *) l->data);
	}
	uris[i] = NULL;

	return uris;
}

GList *
nautilus_drag_uri_list_from_array (const char **uris)
{
	GList *uri_list;
	int i;

	if (uris == NULL) {
		return NULL;
	}

	uri_list = NULL;

	for (i = 0; uris[i] != NULL; i++) {
		uri_list = g_list_prepend (uri_list, g_strdup (uris[i]));
	}

	return g_list_reverse (uri_list);
}

GList *
nautilus_drag_build_selection_list (GtkSelectionData *data)
{
	GList *result;
	const guchar *p, *oldp;
	int size;

	result = NULL;
	oldp = data->data;
	size = data->length;

	while (size > 0) {
		NautilusDragSelectionItem *item;
		guint len;

		/* The list is in the form:

		   name\rx:y:width:height\r\n

		   The geometry information after the first \r is optional.  */

		/* 1: Decode name. */

		p = memchr (oldp, '\r', size);
		if (p == NULL) {
			break;
		}

		item = nautilus_drag_selection_item_new ();

		len = p - oldp;

		item->uri = g_malloc (len + 1);
		memcpy (item->uri, oldp, len);
		item->uri[len] = 0;

		p++;
		if (*p == '\n' || *p == '\0') {
			result = g_list_prepend (result, item);
			if (p == 0) {
				g_warning ("Invalid x-special/gnome-icon-list data received: "
					   "missing newline character.");
				break;
			} else {
				oldp = p + 1;
				continue;
			}
		}

		size -= p - oldp;
		oldp = p;

		/* 2: Decode geometry information.  */

		item->got_icon_position = sscanf (p, "%d:%d:%d:%d%*s",
						  &item->icon_x, &item->icon_y,
						  &item->icon_width, &item->icon_height) == 4;
		if (!item->got_icon_position) {
			g_warning ("Invalid x-special/gnome-icon-list data received: "
				   "invalid icon position specification.");
		}

		result = g_list_prepend (result, item);

		p = memchr (p, '\r', size);
		if (p == NULL || p[1] != '\n') {
			g_warning ("Invalid x-special/gnome-icon-list data received: "
				   "missing newline character.");
			if (p == NULL) {
				break;
			}
		} else {
			p += 2;
		}

		size -= p - oldp;
		oldp = p;
	}

	return g_list_reverse (result);
}

static gboolean
nautilus_drag_file_local_internal (const char *target_uri_string,
				   const char *first_source_uri)
{
	/* check if the first item on the list has target_uri_string as a parent
	 * FIXME:
	 * we should really test each item but that would be slow for large selections
	 * and currently dropped items can only be from the same container
	 */
	GFile *target, *item, *parent;
	gboolean result;

	result = FALSE;

	target = g_file_new_for_uri (target_uri_string);

	/* get the parent URI of the first item in the selection */
	item = g_file_new_for_uri (first_source_uri);
	parent = g_file_get_parent (item);
	g_object_unref (item);
	
	if (parent != NULL) {
		result = g_file_equal (parent, target);
		g_object_unref (parent);
	}
	
	g_object_unref (target);
	
	return result;
}	

gboolean
nautilus_drag_uris_local (const char *target_uri,
			  const GList *source_uri_list)
{
	/* must have at least one item */
	g_assert (source_uri_list);
	
	return nautilus_drag_file_local_internal (target_uri, source_uri_list->data);
}

gboolean
nautilus_drag_items_local (const char *target_uri_string,
			   const GList *selection_list)
{
	/* must have at least one item */
	g_assert (selection_list);

	return nautilus_drag_file_local_internal (target_uri_string,
						  ((NautilusDragSelectionItem *)selection_list->data)->uri);
}

gboolean
nautilus_drag_items_in_trash (const GList *selection_list)
{
	/* check if the first item on the list is in trash.
	 * FIXME:
	 * we should really test each item but that would be slow for large selections
	 * and currently dropped items can only be from the same container
	 */
	return eel_uri_is_trash (((NautilusDragSelectionItem *)selection_list->data)->uri);
}

gboolean
nautilus_drag_items_on_desktop (const GList *selection_list)
{
	char *uri;
	GFile *desktop, *item, *parent;
	gboolean result;
	
	/* check if the first item on the list is in trash.
	 * FIXME:
	 * we should really test each item but that would be slow for large selections
	 * and currently dropped items can only be from the same container
	 */
	uri = ((NautilusDragSelectionItem *)selection_list->data)->uri;
	if (eel_uri_is_desktop (uri)) {
		return TRUE;
	}
	
	desktop = nautilus_get_desktop_location ();
	
	item = g_file_new_for_uri (uri);
	parent = g_file_get_parent (item);
	g_object_unref (item);

	result = FALSE;
	
	if (parent) {
		result = g_file_equal (desktop, parent);
		g_object_unref (parent);
	}
	g_object_unref (desktop);
	
	return result;
	
}

GdkDragAction
nautilus_drag_default_drop_action_for_netscape_url (GdkDragContext *context)
{
	/* Mozilla defaults to copy, but unless thats the
	   only allowed thing (enforced by ctrl) we want to ASK */
	if (context->suggested_action == GDK_ACTION_COPY &&
	    context->actions != GDK_ACTION_COPY) {
		return GDK_ACTION_ASK;
	} else if (context->suggested_action == GDK_ACTION_MOVE) {
		/* Don't support move */
		return GDK_ACTION_COPY;
	}
	
	return context->suggested_action;
}

static gboolean
check_same_fs (NautilusFile *file1,
	       NautilusFile *file2)
{
	char *id1, *id2;
	gboolean result;

	result = FALSE;

	if (file1 != NULL && file2 != NULL) {
		id1 = nautilus_file_get_filesystem_id (file1);
		id2 = nautilus_file_get_filesystem_id (file2);

		if (id1 != NULL && id2 != NULL) {
			result = (strcmp (id1, id2) == 0);
		}

		g_free (id1);
		g_free (id2);
	}

	return result;
}

static gboolean
source_is_deletable (GFile *file)
{
	NautilusFile *naut_file;
	gboolean ret;

	/* if there's no a cached NautilusFile, it returns NULL */
	naut_file = nautilus_file_get_existing (file);
	if (naut_file == NULL) {
		return FALSE;
	}
	
	ret = nautilus_file_can_delete (naut_file);
	nautilus_file_unref (naut_file);

	return ret;
}

void
nautilus_drag_default_drop_action_for_icons (GdkDragContext *context,
					     const char *target_uri_string, const GList *items,
					     int *action)
{
	gboolean same_fs;
	gboolean target_is_source_parent;
	gboolean source_deletable;
	const char *dropped_uri;
	GFile *target, *dropped, *dropped_directory;
	GdkDragAction actions;
	NautilusFile *dropped_file, *target_file;

	if (target_uri_string == NULL) {
		*action = 0;
		return;
	}

	actions = context->actions & (GDK_ACTION_MOVE | GDK_ACTION_COPY);
	if (actions == 0) {
		 /* We can't use copy or move, just go with the suggested action. */
		*action = context->suggested_action;
		return;
	}

	if (context->suggested_action == GDK_ACTION_ASK) {
		/* Don't override ask */
		*action = context->suggested_action;
		return;
	}
	
	dropped_uri = ((NautilusDragSelectionItem *)items->data)->uri;
	dropped_file = nautilus_file_get_existing_by_uri (dropped_uri);
	target_file = nautilus_file_get_existing_by_uri (target_uri_string);
	
	/*
	 * Check for trash URI.  We do a find_directory for any Trash directory.
	 * Passing 0 permissions as gnome-vfs would override the permissions
	 * passed with 700 while creating .Trash directory
	 */
	if (eel_uri_is_trash (target_uri_string)) {
		/* Only move to Trash */
		if (actions & GDK_ACTION_MOVE) {
			*action = GDK_ACTION_MOVE;
		}

		nautilus_file_unref (dropped_file);
		nautilus_file_unref (target_file);
		return;

	} else if (dropped_file != NULL && nautilus_file_is_launcher (dropped_file)) {
		if (actions & GDK_ACTION_MOVE) {
			*action = GDK_ACTION_MOVE;
		}
		nautilus_file_unref (dropped_file);
		nautilus_file_unref (target_file);
		return;
	} else if (eel_uri_is_desktop (target_uri_string)) {
		target = nautilus_get_desktop_location ();

		nautilus_file_unref (target_file);
		target_file = nautilus_file_get (target);

		if (eel_uri_is_desktop (dropped_uri)) {
			/* Only move to Desktop icons */
			if (actions & GDK_ACTION_MOVE) {
				*action = GDK_ACTION_MOVE;
			}
			
			g_object_unref (target);
			nautilus_file_unref (dropped_file);
			nautilus_file_unref (target_file);
			return;
		}
	} else if (target_file != NULL && nautilus_file_is_archive (target_file)) {
		*action = GDK_ACTION_COPY;

		nautilus_file_unref (dropped_file);
		nautilus_file_unref (target_file);
		return;
	} else {
		target = g_file_new_for_uri (target_uri_string);
	}

	same_fs = check_same_fs (target_file, dropped_file);

	nautilus_file_unref (dropped_file);
	nautilus_file_unref (target_file);
	
	/* Compare the first dropped uri with the target uri for same fs match. */
	dropped = g_file_new_for_uri (dropped_uri);
	dropped_directory = g_file_get_parent (dropped);
	target_is_source_parent = FALSE;
	if (dropped_directory != NULL) {
		/* If the dropped file is already in the same directory but
		   is in another filesystem we still want to move, not copy
		   as this is then just a move of a mountpoint to another
		   position in the dir */
		target_is_source_parent = g_file_equal (dropped_directory, target);
		g_object_unref (dropped_directory);
	}
	source_deletable = source_is_deletable (dropped);

	if ((same_fs && source_deletable) || target_is_source_parent ||
	    g_file_has_uri_scheme (dropped, "trash")) {
		if (actions & GDK_ACTION_MOVE) {
			*action = GDK_ACTION_MOVE;
		} else {
			*action = context->suggested_action;
		}
	} else {
		if (actions & GDK_ACTION_COPY) {
			*action = GDK_ACTION_COPY;
		} else {
			*action = context->suggested_action;
		}
	}

	g_object_unref (target);
	g_object_unref (dropped);
	
}

GdkDragAction
nautilus_drag_default_drop_action_for_uri_list (GdkDragContext *context,
						const char *target_uri_string)
{
	if (eel_uri_is_trash (target_uri_string) && (context->actions & GDK_ACTION_MOVE)) {
		/* Only move to Trash */
		return GDK_ACTION_MOVE;
	} else {
		return context->suggested_action;
	}
}

/* Encode a "x-special/gnome-icon-list" selection.
   Along with the URIs of the dragged files, this encodes
   the location and size of each icon relative to the cursor.
*/
static void
add_one_gnome_icon (const char *uri, int x, int y, int w, int h, 
		    gpointer data)
{
	GString *result;

	result = (GString *) data;

	g_string_append_printf (result, "%s\r%d:%d:%hu:%hu\r\n",
				uri, x, y, w, h);
}

/*
 * Cf. #48423
 */
#ifdef THIS_WAS_REALLY_BROKEN
static gboolean
is_path_that_gnome_uri_list_extract_filenames_can_parse (const char *path)
{
	if (path == NULL || path [0] == '\0') {
		return FALSE;
	}

	/* It strips leading and trailing spaces. So it can't handle
	 * file names with leading and trailing spaces.
	 */
	if (g_ascii_isspace (path [0])) {
		return FALSE;
	}
	if (g_ascii_isspace (path [strlen (path) - 1])) {
		return FALSE;
	}

	/* # works as a comment delimiter, and \r and \n are used to
	 * separate the lines, so it can't handle file names with any
	 * of these.
	 */
	if (strchr (path, '#') != NULL
	    || strchr (path, '\r') != NULL
	    || strchr (path, '\n') != NULL) {
		return FALSE;
	}

	return TRUE;
}

/* Encode a "text/plain" selection; this is a broken URL -- just
 * "file:" with a path after it (no escaping or anything). We are
 * trying to make the old gnome_uri_list_extract_filenames function
 * happy, so this is coded to its idiosyncrasises.
 */
static void
add_one_compatible_uri (const char *uri, int x, int y, int w, int h, gpointer data)
{
	GString *result;
	char *local_path;
	
	result = (GString *) data;

	/* For URLs that do not have a file: scheme, there's no harm
	 * in passing the real URL. But for URLs that do have a file:
	 * scheme, we have to send a URL that will work with the old
	 * gnome-libs function or nothing will be able to understand
	 * it.
	 */
	if (!eel_istr_has_prefix (uri, "file:")) {
		g_string_append (result, uri);
		g_string_append (result, "\r\n");
	} else {
		local_path = g_filename_from_uri (uri, NULL, NULL);

		/* Check for characters that confuse the old
		 * gnome_uri_list_extract_filenames implementation, and just leave
		 * out any paths with those in them.
		 */
		if (is_path_that_gnome_uri_list_extract_filenames_can_parse (local_path)) {
			g_string_append (result, "file:");
			g_string_append (result, local_path);
			g_string_append (result, "\r\n");
		}

		g_free (local_path);
	}
}
#endif

static void
add_one_uri (const char *uri, int x, int y, int w, int h, gpointer data)
{
	GString *result;
	
	result = (GString *) data;

	g_string_append (result, uri);
	g_string_append (result, "\r\n");
}

/* Common function for drag_data_get_callback calls.
 * Returns FALSE if it doesn't handle drag data */
gboolean
nautilus_drag_drag_data_get (GtkWidget *widget,
			GdkDragContext *context,
			GtkSelectionData *selection_data,
			guint info,
			guint32 time,
			gpointer container_context,
			NautilusDragEachSelectedItemIterator each_selected_item_iterator)
{
	GString *result;
		
	switch (info) {
	case NAUTILUS_ICON_DND_GNOME_ICON_LIST:
		result = g_string_new (NULL);
		(* each_selected_item_iterator) (add_one_gnome_icon, container_context, result);
		break;
		
	case NAUTILUS_ICON_DND_URI_LIST:
	case NAUTILUS_ICON_DND_TEXT:
		result = g_string_new (NULL);
		(* each_selected_item_iterator) (add_one_uri, container_context, result);
		break;

	default:
		return FALSE;
	}
	
	gtk_selection_data_set (selection_data,
				selection_data->target,
				8, result->str, result->len);
	g_string_free (result, TRUE);

	return TRUE;
}

typedef struct
{
	GMainLoop *loop;
	GdkDragAction chosen;
} DropActionMenuData;

static void
menu_deactivate_callback (GtkWidget *menu,
			  gpointer   data)
{
	DropActionMenuData *damd;
	
	damd = data;

	if (g_main_loop_is_running (damd->loop))
		g_main_loop_quit (damd->loop);
}

static void
drop_action_activated_callback (GtkWidget  *menu_item,
				gpointer    data)
{
	DropActionMenuData *damd;
	
	damd = data;

	damd->chosen = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (menu_item),
							   "action"));

	if (g_main_loop_is_running (damd->loop))
		g_main_loop_quit (damd->loop);
}

static void
append_drop_action_menu_item (GtkWidget          *menu,
			      const char         *text,
			      GdkDragAction       action,
			      gboolean            sensitive,
			      DropActionMenuData *damd)
{
	GtkWidget *menu_item;

	menu_item = gtk_menu_item_new_with_mnemonic (text);
	gtk_widget_set_sensitive (menu_item, sensitive);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);

	g_object_set_data (G_OBJECT (menu_item),
			   "action",
			   GINT_TO_POINTER (action));
	
	g_signal_connect (menu_item, "activate",
			  G_CALLBACK (drop_action_activated_callback),
			  damd);

	gtk_widget_show (menu_item);
}

/* Pops up a menu of actions to perform on dropped files */
GdkDragAction
nautilus_drag_drop_action_ask (GtkWidget *widget,
			       GdkDragAction actions)
{
	GtkWidget *menu;
	GtkWidget *menu_item;
	DropActionMenuData damd;
	
	/* Create the menu and set the sensitivity of the items based on the
	 * allowed actions.
	 */
	menu = gtk_menu_new ();
	gtk_menu_set_screen (GTK_MENU (menu), gtk_widget_get_screen (widget));
	
	append_drop_action_menu_item (menu, _("_Move Here"),
				      GDK_ACTION_MOVE,
				      (actions & GDK_ACTION_MOVE) != 0,
				      &damd);

	append_drop_action_menu_item (menu, _("_Copy Here"),
				      GDK_ACTION_COPY,
				      (actions & GDK_ACTION_COPY) != 0,
				      &damd);
	
	append_drop_action_menu_item (menu, _("_Link Here"),
				      GDK_ACTION_LINK,
				      (actions & GDK_ACTION_LINK) != 0,
				      &damd);

	append_drop_action_menu_item (menu, _("Set as _Background"),
				      NAUTILUS_DND_ACTION_SET_AS_BACKGROUND,
				      (actions & NAUTILUS_DND_ACTION_SET_AS_BACKGROUND) != 0,
				      &damd);

	eel_gtk_menu_append_separator (GTK_MENU (menu));
	
	menu_item = gtk_menu_item_new_with_mnemonic (_("Cancel"));
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);
	gtk_widget_show (menu_item);
	
	damd.chosen = 0;
	damd.loop = g_main_loop_new (NULL, FALSE);

	g_signal_connect (menu, "deactivate",
			  G_CALLBACK (menu_deactivate_callback),
			  &damd);
	
	gtk_grab_add (menu);
	
	gtk_menu_popup (GTK_MENU (menu), NULL, NULL,
			NULL, NULL, 0, GDK_CURRENT_TIME);
	
	g_main_loop_run (damd.loop);

	gtk_grab_remove (menu);
	
	g_main_loop_unref (damd.loop);

	g_object_ref_sink (menu);
	g_object_unref (menu);

	return damd.chosen;
}

GdkDragAction
nautilus_drag_drop_background_ask (GtkWidget *widget, 
				   GdkDragAction actions)
{
	GtkWidget *menu;
	GtkWidget *menu_item;
	DropActionMenuData damd;
	
	/* Create the menu and set the sensitivity of the items based on the
	 * allowed actions.
	 */
	menu = gtk_menu_new ();
	gtk_menu_set_screen (GTK_MENU (menu), gtk_widget_get_screen (widget));
	
	append_drop_action_menu_item (menu, _("Set as background for _all folders"),
				      NAUTILUS_DND_ACTION_SET_AS_GLOBAL_BACKGROUND,
				      (actions & NAUTILUS_DND_ACTION_SET_AS_GLOBAL_BACKGROUND) != 0,
				      &damd);

	append_drop_action_menu_item (menu, _("Set as background for _this folder"),
				      NAUTILUS_DND_ACTION_SET_AS_FOLDER_BACKGROUND,
				      (actions & NAUTILUS_DND_ACTION_SET_AS_FOLDER_BACKGROUND) != 0,
				      &damd);

	eel_gtk_menu_append_separator (GTK_MENU (menu));
	
	menu_item = gtk_menu_item_new_with_mnemonic (_("Cancel"));
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);
	gtk_widget_show (menu_item);
	
	damd.chosen = 0;
	damd.loop = g_main_loop_new (NULL, FALSE);

	g_signal_connect (menu, "deactivate",
			  G_CALLBACK (menu_deactivate_callback),
			  &damd);
	
	gtk_grab_add (menu);
	
	gtk_menu_popup (GTK_MENU (menu), NULL, NULL,
			NULL, NULL, 0, GDK_CURRENT_TIME);
	
	g_main_loop_run (damd.loop);

	gtk_grab_remove (menu);
	
	g_main_loop_unref (damd.loop);

	g_object_ref_sink (menu);
	g_object_unref (menu);

	return damd.chosen;	
}

gboolean
nautilus_drag_autoscroll_in_scroll_region (GtkWidget *widget)
{
	float x_scroll_delta, y_scroll_delta;

	nautilus_drag_autoscroll_calculate_delta (widget, &x_scroll_delta, &y_scroll_delta);

	return x_scroll_delta != 0 || y_scroll_delta != 0;
}


void
nautilus_drag_autoscroll_calculate_delta (GtkWidget *widget, float *x_scroll_delta, float *y_scroll_delta)
{
	int x, y;

	g_assert (GTK_IS_WIDGET (widget));

	gdk_window_get_pointer (widget->window, &x, &y, NULL);

	/* Find out if we are anywhere close to the tree view edges
	 * to see if we need to autoscroll.
	 */
	*x_scroll_delta = 0;
	*y_scroll_delta = 0;
	
	if (x < AUTO_SCROLL_MARGIN) {
		*x_scroll_delta = (float)(x - AUTO_SCROLL_MARGIN);
	}

	if (x > widget->allocation.width - AUTO_SCROLL_MARGIN) {
		if (*x_scroll_delta != 0) {
			/* Already trying to scroll because of being too close to 
			 * the top edge -- must be the window is really short,
			 * don't autoscroll.
			 */
			return;
		}
		*x_scroll_delta = (float)(x - (widget->allocation.width - AUTO_SCROLL_MARGIN));
	}

	if (y < AUTO_SCROLL_MARGIN) {
		*y_scroll_delta = (float)(y - AUTO_SCROLL_MARGIN);
	}

	if (y > widget->allocation.height - AUTO_SCROLL_MARGIN) {
		if (*y_scroll_delta != 0) {
			/* Already trying to scroll because of being too close to 
			 * the top edge -- must be the window is really narrow,
			 * don't autoscroll.
			 */
			return;
		}
		*y_scroll_delta = (float)(y - (widget->allocation.height - AUTO_SCROLL_MARGIN));
	}

	if (*x_scroll_delta == 0 && *y_scroll_delta == 0) {
		/* no work */
		return;
	}

	/* Adjust the scroll delta to the proper acceleration values depending on how far
	 * into the sroll margins we are.
	 * FIXME bugzilla.eazel.com 2486:
	 * we could use an exponential acceleration factor here for better feel
	 */
	if (*x_scroll_delta != 0) {
		*x_scroll_delta /= AUTO_SCROLL_MARGIN;
		*x_scroll_delta *= (MAX_AUTOSCROLL_DELTA - MIN_AUTOSCROLL_DELTA);
		*x_scroll_delta += MIN_AUTOSCROLL_DELTA;
	}
	
	if (*y_scroll_delta != 0) {
		*y_scroll_delta /= AUTO_SCROLL_MARGIN;
		*y_scroll_delta *= (MAX_AUTOSCROLL_DELTA - MIN_AUTOSCROLL_DELTA);
		*y_scroll_delta += MIN_AUTOSCROLL_DELTA;
	}

}



void
nautilus_drag_autoscroll_start (NautilusDragInfo *drag_info,
				GtkWidget        *widget,
				GtkFunction       callback,
				gpointer          user_data)
{
	if (nautilus_drag_autoscroll_in_scroll_region (widget)) {
		if (drag_info->auto_scroll_timeout_id == 0) {
			drag_info->waiting_to_autoscroll = TRUE;
			drag_info->start_auto_scroll_in = eel_get_system_time() 
				+ AUTOSCROLL_INITIAL_DELAY;
			
			drag_info->auto_scroll_timeout_id = g_timeout_add
				(AUTOSCROLL_TIMEOUT_INTERVAL,
				 callback,
			 	 user_data);
		}
	} else {
		if (drag_info->auto_scroll_timeout_id != 0) {
			g_source_remove (drag_info->auto_scroll_timeout_id);
			drag_info->auto_scroll_timeout_id = 0;
		}
	}
}

void
nautilus_drag_autoscroll_stop (NautilusDragInfo *drag_info)
{
	if (drag_info->auto_scroll_timeout_id != 0) {
		g_source_remove (drag_info->auto_scroll_timeout_id);
		drag_info->auto_scroll_timeout_id = 0;
	}
}

gboolean
nautilus_drag_selection_includes_special_link (GList *selection_list)
{
	GList *node;
	char *uri;

	for (node = selection_list; node != NULL; node = node->next) {
		uri = ((NautilusDragSelectionItem *) node->data)->uri;

		if (eel_uri_is_desktop (uri)) {
			return TRUE;
		}
	}
	
	return FALSE;
}

static gboolean
slot_proxy_drag_motion (GtkWidget          *widget,
			GdkDragContext     *context,
			int                 x,
			int                 y,
			unsigned int        time,
			gpointer            user_data)
{
	NautilusDragSlotProxyInfo *drag_info;
	NautilusWindowSlotInfo *target_slot;
	GtkWidget *window;
	GdkAtom target;
	int action;
	char *target_uri;

	drag_info = user_data;

	action = 0;

	if (gtk_drag_get_source_widget (context) == widget) {
		goto out;
	}

	window = gtk_widget_get_toplevel (widget);
	g_assert (NAUTILUS_IS_WINDOW_INFO (window));

	if (!drag_info->have_data) {
		target = gtk_drag_dest_find_target (widget, context, NULL);

		if (target == GDK_NONE) {
			goto out;
		}

		gtk_drag_get_data (widget, context, target, time);
	}

	target_uri = NULL;
	if (drag_info->target_location != NULL) {
		target_uri = g_file_get_uri (drag_info->target_location);
	} else {
		if (drag_info->target_slot != NULL) {
			target_slot = drag_info->target_slot;
		} else {
			target_slot = nautilus_window_info_get_active_slot (NAUTILUS_WINDOW_INFO (window));
		}

		if (target_slot != NULL) {
			target_uri = nautilus_window_slot_info_get_current_location (target_slot);
		}
	}

	if (drag_info->have_data &&
	    drag_info->have_valid_data) {
		if (drag_info->info == NAUTILUS_ICON_DND_GNOME_ICON_LIST) {
			nautilus_drag_default_drop_action_for_icons (context, target_uri,
								     drag_info->data.selection_list,
								     &action);
		} else if (drag_info->info == NAUTILUS_ICON_DND_URI_LIST) {
			action = nautilus_drag_default_drop_action_for_uri_list (context, target_uri);
		} else if (drag_info->info == NAUTILUS_ICON_DND_NETSCAPE_URL) {
			action = nautilus_drag_default_drop_action_for_netscape_url (context);
		}
	}

	g_free (target_uri);

out:
	if (action != 0) {
		gtk_drag_highlight (widget);
	} else {
		gtk_drag_unhighlight (widget);
	}

	gdk_drag_status (context, action, time);

	return TRUE;
}

static void
drag_info_clear (NautilusDragSlotProxyInfo *drag_info)
{
	if (!drag_info->have_data) {
		goto out;
	}

	if (drag_info->info == NAUTILUS_ICON_DND_GNOME_ICON_LIST) {
		nautilus_drag_destroy_selection_list (drag_info->data.selection_list);
	} else if (drag_info->info == NAUTILUS_ICON_DND_URI_LIST) {
		g_list_free (drag_info->data.uri_list);
	} else if (drag_info->info == NAUTILUS_ICON_DND_NETSCAPE_URL) {
		g_free (drag_info->data.netscape_url);
	}

out:
	drag_info->have_data = FALSE;
	drag_info->have_valid_data = FALSE;

	drag_info->drop_occured = FALSE;
}

static void
slot_proxy_drag_leave (GtkWidget          *widget,
		       GdkDragContext     *context,
		       unsigned int        time,
		       gpointer            user_data)
{
	NautilusDragSlotProxyInfo *drag_info;

	drag_info = user_data;

	gtk_drag_unhighlight (widget);
	drag_info_clear (drag_info);
}

static gboolean
slot_proxy_drag_drop (GtkWidget          *widget,
		      GdkDragContext     *context,
		      int                 x,
		      int                 y,
		      unsigned int        time,
		      gpointer            user_data)
{
	GdkAtom target;
	NautilusDragSlotProxyInfo *drag_info;

	drag_info = user_data;
	g_assert (!drag_info->have_data);

	drag_info->drop_occured = TRUE;

	target = gtk_drag_dest_find_target (widget, context, NULL);
	gtk_drag_get_data (widget, context, target, time);

	return TRUE;
}


static void
slot_proxy_handle_drop (GtkWidget                *widget,
			GdkDragContext           *context,
			unsigned int              time,
			NautilusDragSlotProxyInfo *drag_info)
{
	GtkWidget *window;
	NautilusWindowSlotInfo *target_slot;
	NautilusView *target_view;
	char *target_uri;
	GList *uri_list;

	if (!drag_info->have_data ||
	    !drag_info->have_valid_data) {
		gtk_drag_finish (context, FALSE, FALSE, time);
		drag_info_clear (drag_info);
		return;
	}

	window = gtk_widget_get_toplevel (widget);
	g_assert (NAUTILUS_IS_WINDOW_INFO (window));

	if (drag_info->target_slot != NULL) {
		target_slot = drag_info->target_slot;
	} else {
		target_slot = nautilus_window_info_get_active_slot (NAUTILUS_WINDOW_INFO (window));
	}

	target_uri = NULL;
	if (drag_info->target_location != NULL) {
		target_uri = g_file_get_uri (drag_info->target_location);
	} else if (target_slot != NULL) {
		target_uri = nautilus_window_slot_info_get_current_location (target_slot);
	}

	target_view = NULL;
	if (target_slot != NULL) {
		target_view = nautilus_window_slot_info_get_current_view (target_slot);
	}

	if (target_slot != NULL && target_view != NULL) {
		if (drag_info->info == NAUTILUS_ICON_DND_GNOME_ICON_LIST) {
			uri_list = nautilus_drag_uri_list_from_selection_list (drag_info->data.selection_list);
			g_assert (uri_list != NULL);

			nautilus_view_drop_proxy_received_uris (target_view,
								uri_list,
								target_uri,
								context->action);
			eel_g_list_free_deep (uri_list);
		} else if (drag_info->info == NAUTILUS_ICON_DND_URI_LIST) {
			nautilus_view_drop_proxy_received_uris (target_view,
								drag_info->data.uri_list,
								target_uri,
								context->action);
		} if (drag_info->info == NAUTILUS_ICON_DND_NETSCAPE_URL) {
			nautilus_view_drop_proxy_received_netscape_url (target_view,
									drag_info->data.netscape_url,
									target_uri,
									context->action);
		}


		gtk_drag_finish (context, TRUE, FALSE, time);
	} else {
		gtk_drag_finish (context, FALSE, FALSE, time);
	}

	if (target_view != NULL) {
		g_object_unref (target_view);
	}

	g_free (target_uri);

	drag_info_clear (drag_info);
}

static void
slot_proxy_drag_data_received (GtkWidget          *widget,
			       GdkDragContext     *context,
			       int                 x,
			       int                 y,
			       GtkSelectionData   *data,
			       unsigned int        info,
			       unsigned int        time,
			       gpointer            user_data)
{
	NautilusDragSlotProxyInfo *drag_info;
	char **uris;

	drag_info = user_data;

	g_assert (!drag_info->have_data);

	drag_info->have_data = TRUE;
	drag_info->info = info;

	if (data->length < 0) {
		drag_info->have_valid_data = FALSE;
		return;
	}

	if (info == NAUTILUS_ICON_DND_GNOME_ICON_LIST) {
		drag_info->data.selection_list = nautilus_drag_build_selection_list (data);

		drag_info->have_valid_data = drag_info->data.selection_list != NULL;
	} else if (info == NAUTILUS_ICON_DND_URI_LIST) {
		uris = gtk_selection_data_get_uris (data);
		drag_info->data.uri_list = nautilus_drag_uri_list_from_array ((const char **) uris);
		g_strfreev (uris);

		drag_info->have_valid_data = drag_info->data.uri_list != NULL;
	} else if (info == NAUTILUS_ICON_DND_NETSCAPE_URL) {
		drag_info->data.netscape_url = g_strdup ((char *) data->data);

		drag_info->have_valid_data = drag_info->data.netscape_url != NULL;
	}

	if (drag_info->drop_occured) {
		slot_proxy_handle_drop (widget, context, time, drag_info);
	}
}

void
nautilus_drag_slot_proxy_init (GtkWidget *widget,
			       NautilusDragSlotProxyInfo *drag_info)
{
        const GtkTargetEntry targets[] = {
		{ NAUTILUS_ICON_DND_GNOME_ICON_LIST_TYPE, 0, NAUTILUS_ICON_DND_GNOME_ICON_LIST },
		{ NAUTILUS_ICON_DND_NETSCAPE_URL_TYPE, 0, NAUTILUS_ICON_DND_NETSCAPE_URL }
        };
	GtkTargetList *target_list;

	g_assert (GTK_IS_WIDGET (widget));
	g_assert (drag_info != NULL);

	gtk_drag_dest_set (widget, 0,
			   NULL, 0,
			   GDK_ACTION_MOVE |
			   GDK_ACTION_COPY |
			   GDK_ACTION_LINK |
			   GDK_ACTION_ASK);

	target_list = gtk_target_list_new (targets, G_N_ELEMENTS (targets));
	gtk_target_list_add_uri_targets (target_list, NAUTILUS_ICON_DND_URI_LIST);
	gtk_drag_dest_set_target_list (widget, target_list);
	gtk_target_list_unref (target_list);

	g_signal_connect (widget, "drag-motion",
			  G_CALLBACK (slot_proxy_drag_motion),
			  drag_info);
	g_signal_connect (widget, "drag-drop",
			  G_CALLBACK (slot_proxy_drag_drop),
			  drag_info);
	g_signal_connect (widget, "drag-data-received",
			  G_CALLBACK (slot_proxy_drag_data_received),
			  drag_info);
	g_signal_connect (widget, "drag-leave",
			  G_CALLBACK (slot_proxy_drag_leave),
			  drag_info);
}


