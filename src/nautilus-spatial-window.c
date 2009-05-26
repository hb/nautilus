/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  Nautilus
 *
 *  Copyright (C) 1999, 2000 Red Hat, Inc.
 *  Copyright (C) 1999, 2000, 2001 Eazel, Inc.
 *
 *  Nautilus is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  Nautilus is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public
 *  License along with this program; if not, write to the Free
 *  Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  Authors: Elliot Lee <sopwith@redhat.com>
 *  	     John Sullivan <sullivan@eazel.com>
 *
 */

/* nautilus-window.c: Implementation of the main window object */

#include <config.h>
#include "nautilus-spatial-window.h"
#include "nautilus-window-private.h"
#include "nautilus-window-bookmarks.h"

#include "nautilus-actions.h"
#include "nautilus-application.h"
#include "nautilus-desktop-window.h"
#include "nautilus-bookmarks-window.h"
#include "nautilus-location-dialog.h"
#include "nautilus-main.h"
#include "nautilus-query-editor.h"
#include "nautilus-search-bar.h"
#include "nautilus-window-manage-views.h"
#include "nautilus-zoom-control.h"
#include <eel/eel-glib-extensions.h>
#include <eel/eel-gtk-extensions.h>
#include <eel/eel-gtk-macros.h>
#include <eel/eel-string.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/gdkkeysyms.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <libnautilus-private/nautilus-dnd.h>
#include <libnautilus-private/nautilus-file-utilities.h>
#include <libnautilus-private/nautilus-ui-utilities.h>
#include <libnautilus-private/nautilus-file-attributes.h>
#include <libnautilus-private/nautilus-global-preferences.h>
#include <libnautilus-private/nautilus-horizontal-splitter.h>
#include <libnautilus-private/nautilus-metadata.h>
#include <libnautilus-private/nautilus-mime-actions.h>
#include <libnautilus-private/nautilus-program-choosing.h>
#include <libnautilus-private/nautilus-undo.h>
#include <libnautilus-private/nautilus-search-directory.h>
#include <libnautilus-private/nautilus-search-engine.h>
#include <libnautilus-private/nautilus-signaller.h>

#define MAX_TITLE_LENGTH 180
#define MAX_SHORTNAME_PATH 16

#define SPATIAL_ACTION_PLACES               "Places"
#define SPATIAL_ACTION_GO_TO_LOCATION       "Go to Location"
#define SPATIAL_ACTION_CLOSE_PARENT_FOLDERS "Close Parent Folders"
#define SPATIAL_ACTION_CLOSE_ALL_FOLDERS    "Close All Folders"
#define MENU_PATH_SPATIAL_BOOKMARKS_PLACEHOLDER	"/MenuBar/Other Menus/Places/Bookmarks Placeholder"

struct _NautilusSpatialWindowDetails {
	GtkActionGroup *spatial_action_group; /* owned by ui_manager */
	char *last_geometry;
	guint save_geometry_timeout_id;

	gboolean saved_data_on_close;
	GtkWidget *content_box;
	GtkWidget *location_button;
	GtkWidget *location_label;
	GtkWidget *location_icon;
};

static const GtkTargetEntry location_button_drag_types[] = {
	{ NAUTILUS_ICON_DND_GNOME_ICON_LIST_TYPE, 0, NAUTILUS_ICON_DND_GNOME_ICON_LIST },
	{ NAUTILUS_ICON_DND_URI_LIST_TYPE, 0, NAUTILUS_ICON_DND_URI_LIST },
};

G_DEFINE_TYPE(NautilusSpatialWindow, nautilus_spatial_window, NAUTILUS_TYPE_WINDOW)
#define parent_class nautilus_spatial_window_parent_class

static void nautilus_spatial_window_save_geometry (NautilusWindowSlot *slot);

static gboolean
save_window_geometry_timeout (gpointer callback_data)
{
	NautilusSpatialWindow *window;
	NautilusWindowSlot *slot;
	
	window = NAUTILUS_SPATIAL_WINDOW (callback_data);
	slot = nautilus_window_get_active_slot (NAUTILUS_WINDOW (window));

	if (slot != NULL) {
		nautilus_spatial_window_save_geometry (slot);
	}

	window->details->save_geometry_timeout_id = 0;

	return FALSE;
}

static gboolean
nautilus_spatial_window_configure_event (GtkWidget *widget,
					GdkEventConfigure *event)
{
	NautilusSpatialWindow *window;
	char *geometry_string;
	
	window = NAUTILUS_SPATIAL_WINDOW (widget);

	GTK_WIDGET_CLASS (nautilus_spatial_window_parent_class)->configure_event (widget, event);
	
	/* Only save the geometry if the user hasn't resized the window
	 * for a second. Otherwise delay the callback another second.
	 */
	if (window->details->save_geometry_timeout_id != 0) {
		g_source_remove (window->details->save_geometry_timeout_id);
	}
	if (GTK_WIDGET_VISIBLE (GTK_WIDGET (window)) && !NAUTILUS_IS_DESKTOP_WINDOW (window)) {
		geometry_string = eel_gtk_window_get_geometry_string (GTK_WINDOW (window));
	
		/* If the last geometry is NULL the window must have just
		 * been shown. No need to save geometry to disk since it
		 * must be the same.
		 */
		if (window->details->last_geometry == NULL) {
			window->details->last_geometry = geometry_string;
			return FALSE;
		}
	
		/* Don't save geometry if it's the same as before. */
		if (!strcmp (window->details->last_geometry, 
			     geometry_string)) {
			g_free (geometry_string);
			return FALSE;
		}

		g_free (window->details->last_geometry);
		window->details->last_geometry = geometry_string;

		window->details->save_geometry_timeout_id = 
			g_timeout_add_seconds (1, save_window_geometry_timeout, window);
	}
	
	return FALSE;
}

static void
nautilus_spatial_window_unrealize (GtkWidget *widget)
{
	NautilusSpatialWindow *window;
	NautilusWindowSlot *slot;
	
	window = NAUTILUS_SPATIAL_WINDOW (widget);
	slot = nautilus_window_get_active_slot (NAUTILUS_WINDOW (window));

	GTK_WIDGET_CLASS (nautilus_spatial_window_parent_class)->unrealize (widget);

	if (window->details->save_geometry_timeout_id != 0) {
		g_source_remove (window->details->save_geometry_timeout_id);
		window->details->save_geometry_timeout_id = 0;

		if (slot != NULL) {
			nautilus_spatial_window_save_geometry (slot);
		}
	}
}

static gboolean
nautilus_spatial_window_state_event (GtkWidget *widget,
				     GdkEventWindowState *event)
{
	NautilusWindow *window;
	NautilusWindowSlot *slot;
	NautilusFile *viewed_file;

	window = NAUTILUS_WINDOW (widget);
	slot = window->details->active_pane->active_slot;
	viewed_file = slot->viewed_file;

	if (!NAUTILUS_IS_DESKTOP_WINDOW (widget)) {
		
		if (event->changed_mask & GDK_WINDOW_STATE_MAXIMIZED &&
		    viewed_file != NULL) {
			nautilus_file_set_boolean_metadata (viewed_file,
							    NAUTILUS_METADATA_KEY_WINDOW_MAXIMIZED,
							    FALSE,
							    event->new_window_state & GDK_WINDOW_STATE_MAXIMIZED);
		}
		
		if (event->changed_mask & GDK_WINDOW_STATE_STICKY &&
		    viewed_file != NULL) {
			nautilus_file_set_boolean_metadata (viewed_file,
							    NAUTILUS_METADATA_KEY_WINDOW_STICKY,
							    FALSE,
							    event->new_window_state & GDK_WINDOW_STATE_STICKY);
		}
		
		if (event->changed_mask & GDK_WINDOW_STATE_ABOVE &&
		    viewed_file != NULL) {
			nautilus_file_set_boolean_metadata (viewed_file,
							    NAUTILUS_METADATA_KEY_WINDOW_KEEP_ABOVE,
							    FALSE,
							    event->new_window_state & GDK_WINDOW_STATE_ABOVE);
		}

	}
	
	if (GTK_WIDGET_CLASS (nautilus_spatial_window_parent_class)->window_state_event != NULL) {
		return GTK_WIDGET_CLASS (nautilus_spatial_window_parent_class)->window_state_event (widget, event);
	}

	return FALSE;
}

static void
nautilus_spatial_window_destroy (GtkObject *object)
{
	NautilusSpatialWindow *window;

	window = NAUTILUS_SPATIAL_WINDOW (object);

	window->details->content_box = NULL;

	GTK_OBJECT_CLASS (nautilus_spatial_window_parent_class)->destroy (object);
}

static void
nautilus_spatial_window_finalize (GObject *object)
{
	NautilusSpatialWindow *window;
	
	window = NAUTILUS_SPATIAL_WINDOW (object);

	if (window->details->last_geometry != NULL) {
		g_free (window->details->last_geometry);
	}

	G_OBJECT_CLASS (nautilus_spatial_window_parent_class)->finalize (object);
}

static void
nautilus_spatial_window_save_geometry (NautilusWindowSlot *slot)
{
	NautilusWindow *window;
	NautilusFile *viewed_file;
	char *geometry_string;

	window = NAUTILUS_WINDOW (slot->pane->window);

	viewed_file = slot->viewed_file;

	if (viewed_file == NULL) {
		/* We never showed a file */
		return;
	}
	
	if (GTK_WIDGET(window)->window &&
	    !(gdk_window_get_state (GTK_WIDGET(window)->window) & GDK_WINDOW_STATE_MAXIMIZED)) {
		geometry_string = eel_gtk_window_get_geometry_string (GTK_WINDOW (window));
		
		nautilus_file_set_metadata (viewed_file,
					    NAUTILUS_METADATA_KEY_WINDOW_GEOMETRY,
					    NULL,
					    geometry_string);
		
		g_free (geometry_string);
	}
}

static void
nautilus_spatial_window_save_scroll_position (NautilusWindowSlot *slot)
{
	NautilusWindow *window;
	char *scroll_string;

	window = NAUTILUS_WINDOW (slot->pane->window);

	if (slot->content_view == NULL ||
	    slot->viewed_file == NULL) {
		return;
	}
	
	scroll_string = nautilus_view_get_first_visible_file (slot->content_view);
	nautilus_file_set_metadata (slot->viewed_file,
				    NAUTILUS_METADATA_KEY_WINDOW_SCROLL_POSITION,
				    NULL,
				    scroll_string);
	g_free (scroll_string);
}

static void
nautilus_spatial_window_save_show_hidden_files_mode (NautilusWindowSlot *slot)
{
	NautilusWindow *window;
	char *show_hidden_file_setting;
	NautilusWindowShowHiddenFilesMode mode;

	if (slot->viewed_file == NULL) {
		return;
	}
	
	window = NAUTILUS_WINDOW (slot->pane->window);

	mode = NAUTILUS_WINDOW (window)->details->show_hidden_files_mode;
	if (mode != NAUTILUS_WINDOW_SHOW_HIDDEN_FILES_DEFAULT) {
		if (mode == NAUTILUS_WINDOW_SHOW_HIDDEN_FILES_ENABLE) {
			show_hidden_file_setting = "1";
		} else {
			show_hidden_file_setting = "0";
		}
		nautilus_file_set_metadata (slot->viewed_file,
			 		    NAUTILUS_METADATA_KEY_WINDOW_SHOW_HIDDEN_FILES,
				    	    NULL,
				    	    show_hidden_file_setting);
	} 
}

static void
nautilus_spatial_window_show (GtkWidget *widget)
{	
	NautilusWindow *window;
	NautilusWindowSlot *slot;

	window = NAUTILUS_WINDOW (widget);
	slot = nautilus_window_get_active_slot (window);

	GTK_WIDGET_CLASS (nautilus_spatial_window_parent_class)->show (widget);

	if (slot != NULL && slot->query_editor != NULL) {
		nautilus_query_editor_grab_focus (NAUTILUS_QUERY_EDITOR (slot->query_editor));
	}
}

static void
action_close_parent_folders_callback (GtkAction *action, 
				      gpointer user_data)
{
	nautilus_application_close_parent_windows (NAUTILUS_SPATIAL_WINDOW (user_data));
}

static void
action_close_all_folders_callback (GtkAction *action, 
				   gpointer user_data)
{
	nautilus_application_close_all_spatial_windows ();
}

static void
real_prompt_for_location (NautilusWindow *window,
			  const char     *initial)
{
	GtkWidget *dialog;
	
	dialog = nautilus_location_dialog_new (window);
	if (initial != NULL) {
		nautilus_location_dialog_set_location (NAUTILUS_LOCATION_DIALOG (dialog),
						       initial);
	}
		
	gtk_widget_show (dialog);
}

static NautilusIconInfo *
real_get_icon (NautilusWindow *window,
	       NautilusWindowSlot *slot)
{
	return nautilus_file_get_icon (slot->viewed_file, 48,
				       NAUTILUS_FILE_ICON_FLAGS_IGNORE_VISITING);
}

static void
sync_window_title (NautilusWindow *window)
{
	NautilusWindowSlot *slot;

	slot = nautilus_window_get_active_slot (window);

	if (slot->title == NULL || slot->title[0] == '\0') {
		gtk_window_set_title (GTK_WINDOW (window), _("Nautilus"));
	} else {
		char *window_title;

		window_title = eel_str_middle_truncate (slot->title, MAX_TITLE_LENGTH);
		gtk_window_set_title (GTK_WINDOW (window), window_title);
		g_free (window_title);
	}
}

static void
real_sync_title (NautilusWindow *window,
		 NautilusWindowSlot *slot)
{
	g_assert (slot == nautilus_window_get_active_slot (window));

	sync_window_title (window);
}

static void 
real_get_default_size (NautilusWindow *window,
		       guint *default_width, guint *default_height)
{
	if (default_width) {
		*default_width = NAUTILUS_SPATIAL_WINDOW_DEFAULT_WIDTH;
	}
	if (default_height) {
		*default_height = NAUTILUS_SPATIAL_WINDOW_DEFAULT_HEIGHT;	
	}
}

static void
real_sync_allow_stop (NautilusWindow *window,
		      NautilusWindowSlot *slot)
{
}

static void
real_set_allow_up (NautilusWindow *window, gboolean allow)
{
	NautilusSpatialWindow *spatial;
	GtkAction *action;

	spatial = NAUTILUS_SPATIAL_WINDOW (window);

	action = gtk_action_group_get_action (spatial->details->spatial_action_group,
					      SPATIAL_ACTION_CLOSE_PARENT_FOLDERS);
	gtk_action_set_sensitive (action, allow);

	NAUTILUS_WINDOW_CLASS (nautilus_spatial_window_parent_class)->set_allow_up (window, allow);
}

static NautilusWindowSlot *
real_open_slot (NautilusWindowPane *pane,
		NautilusWindowOpenSlotFlags flags)
{
	NautilusWindowSlot *slot;
	GList *slots;

	g_assert (nautilus_window_get_active_slot (pane->window) == NULL);

	slots = nautilus_window_get_slots (pane->window);
	g_assert (slots == NULL);
	g_list_free (slots);

	slot = g_object_new (NAUTILUS_TYPE_WINDOW_SLOT, NULL);
	slot->pane = pane;
	gtk_container_add (GTK_CONTAINER (NAUTILUS_SPATIAL_WINDOW (pane->window)->details->content_box),
			   slot->content_box);
	gtk_widget_show (slot->content_box);
	return slot;
}

static void
save_spatial_data (NautilusWindowSlot *slot)
{
	nautilus_spatial_window_save_geometry (slot);
	nautilus_spatial_window_save_scroll_position (slot);
	nautilus_spatial_window_save_show_hidden_files_mode (slot);
}

static void
real_close_slot (NautilusWindowPane *pane,
		 NautilusWindowSlot *slot)
{
	/* Save spatial data for close if we didn't already */
	if (!NAUTILUS_SPATIAL_WINDOW (pane->window)->details->saved_data_on_close) {
		save_spatial_data (slot);
	}

	EEL_CALL_PARENT (NAUTILUS_WINDOW_CLASS,
			 close_slot, (pane, slot));
}

static void
real_window_close (NautilusWindow *window)
{
	NautilusWindowSlot *slot;

	/* We're closing the window, save the geometry. */
	/* Note that we do this in window close, not slot close, because slot
	 * close is too late, by then the widgets have been unrealized.
	 * This is for the close by WM case, if you're closing via Ctrl-W that
	 * means we close the slots first and this is not an issue */
	if (window->details->panes != NULL) {
		slot = window->details->active_pane->active_slot;

		save_spatial_data (slot);
		NAUTILUS_SPATIAL_WINDOW (window)->details->saved_data_on_close = TRUE;
	}

	EEL_CALL_PARENT (NAUTILUS_WINDOW_CLASS,
			 close, (window));
}

static void
location_menu_item_activated_callback (GtkWidget *menu_item,
				       NautilusWindow *window)
{
	NautilusWindowSlot *slot;
	char *location;
	GFile *current;
	GFile *dest;
	GdkEvent *event;

	slot = window->details->active_pane->active_slot;

	location = nautilus_window_slot_get_location_uri (slot);
	current = g_file_new_for_uri (location);
	g_free (location);

	dest = g_object_get_data (G_OBJECT (menu_item), "uri");

	event = gtk_get_current_event();

	if (!g_file_equal (current, dest))
	{
		GFile *child;
		gboolean close_behind;
		GList *selection;

		close_behind = FALSE;
		selection = NULL;

		child = g_object_get_data (G_OBJECT(menu_item), "child_uri");
		if (child != NULL) {
			selection = g_list_prepend (NULL, g_object_ref (child));
		}

		if (event != NULL && ((GdkEventAny *) event)->type == GDK_BUTTON_RELEASE &&
		   (((GdkEventButton *) event)->button == 2 ||
		   (((GdkEventButton *) event)->state & GDK_SHIFT_MASK) != 0))
		{
			close_behind = TRUE;
		}

		nautilus_window_slot_open_location_with_selection
			(slot, dest, selection, close_behind);

		eel_g_object_list_free (selection);
	}

	if (event != NULL) {
		gdk_event_free (event);
	}

	g_object_unref (current);
}

static void
got_file_info_for_location_menu_callback (NautilusFile *file,
					  gpointer callback_data)
{	
	GtkWidget *menu_item = callback_data;
	GtkWidget *label;
	GtkWidget *icon;
	GdkPixbuf *pixbuf;
	char *name;

	g_return_if_fail (NAUTILUS_IS_FILE (file));

	pixbuf = NULL;

	name = nautilus_file_get_display_name (file);
	label = gtk_bin_get_child (GTK_BIN (menu_item));
	gtk_label_set_label (GTK_LABEL (label), name);
	g_free (name);

	pixbuf = nautilus_file_get_icon_pixbuf (file,
						nautilus_get_icon_size_for_stock_size (GTK_ICON_SIZE_MENU),
						TRUE,
						NAUTILUS_FILE_ICON_FLAGS_IGNORE_VISITING);

	if (pixbuf != NULL) {
		icon = gtk_image_new_from_pixbuf (pixbuf);
		g_object_unref (pixbuf);
	} else {
		icon = gtk_image_new_from_stock (GTK_STOCK_OPEN, GTK_ICON_SIZE_MENU);
	}
	
	if (icon) {
		gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (menu_item), icon);
	}
	g_object_unref (file);
	g_object_unref (menu_item);
}

static void
menu_deactivate_callback (GtkWidget *menu,
			  gpointer   data)
{
	GMainLoop *loop;

	loop = data;

	if (g_main_loop_is_running (loop)) {
		g_main_loop_quit (loop);
	}
}

static void
menu_popup_pos (GtkMenu   *menu,
		gint      *x,
		gint      *y,
		gboolean  *push_in,
		gpointer	user_data)
{
	GtkWidget *widget;
	GtkRequisition menu_requisition, button_requisition;

	widget = user_data;

	gtk_widget_size_request (GTK_WIDGET (menu), &menu_requisition);
	gtk_widget_size_request (widget, &button_requisition);

	gdk_window_get_origin (widget->window, x, y);
	*x += widget->allocation.x;
	*y += widget->allocation.y;
	
	*y -= menu_requisition.height - button_requisition.height;

	*push_in = TRUE;
}

static gboolean
location_button_pressed_callback (GtkWidget      *widget,
				  GdkEventButton *event,
				  NautilusWindow *window)
{
	NautilusView *view;

	view = window->details->active_pane->active_slot->content_view;

	if (event->button == 3 && view != NULL) {
		nautilus_view_pop_up_location_context_menu (view, event, NULL);
	}

	return FALSE;
}

static void
location_button_clicked_callback (GtkWidget             *widget,
				  NautilusSpatialWindow *window)
{
	NautilusWindowSlot *slot;
	GtkWidget *popup, *menu_item, *first_item = NULL;
	char *location;
	GFile *uri;
	GFile *child_uri;
	GMainLoop *loop;

	slot = NAUTILUS_WINDOW (window)->details->active_pane->active_slot;

	popup = gtk_menu_new ();
	first_item = NULL;

	location = nautilus_window_slot_get_location_uri (slot);
	g_return_if_fail (location != NULL);

	uri = g_file_new_for_uri (location);
	g_free (location);

	child_uri = NULL;
	while (uri != NULL) {
		NautilusFile *file;
		char *name;

		file = nautilus_file_get (uri);

		name = nautilus_file_get_display_name (file);
		menu_item = gtk_image_menu_item_new_with_label (name);
		g_free (name);

		if (first_item == NULL) {
			first_item = menu_item;
		}

		g_object_ref (menu_item);
		nautilus_file_call_when_ready (file,
					       NAUTILUS_FILE_ATTRIBUTE_INFO,
					       got_file_info_for_location_menu_callback,
					       menu_item);

		gtk_widget_show (menu_item);
		g_signal_connect (menu_item, "activate",
				  G_CALLBACK (location_menu_item_activated_callback),
				  window);

		g_object_set_data_full (G_OBJECT (menu_item),
					"uri",
					g_object_ref (uri),
					(GDestroyNotify)g_object_unref);

		if (child_uri) {
			g_object_set_data_full (G_OBJECT (menu_item),
						"child_uri",
						g_object_ref (child_uri),
						(GDestroyNotify)g_object_unref);
		}

		gtk_menu_shell_prepend (GTK_MENU_SHELL (popup), menu_item);

		if (child_uri){
			g_object_unref (child_uri);
		}
		child_uri = uri;
		uri = g_file_get_parent (uri);
	}

	if (child_uri){
		g_object_unref (child_uri);
	}
	if (uri){
		g_object_unref (uri);
	}

	gtk_menu_set_screen (GTK_MENU (popup), gtk_widget_get_screen (widget));

	loop = g_main_loop_new (NULL, FALSE);

	g_signal_connect (popup, "deactivate",
			  G_CALLBACK (menu_deactivate_callback),
			  loop);

	gtk_grab_add (popup);
	gtk_menu_popup (GTK_MENU (popup), NULL, NULL, menu_popup_pos, widget, 1, GDK_CURRENT_TIME);
	gtk_menu_shell_select_item (GTK_MENU_SHELL (popup), first_item);
	g_main_loop_run (loop);
	gtk_grab_remove (popup);
	g_main_loop_unref (loop);
	g_object_ref_sink (popup);
}

static int
get_dnd_icon_size (NautilusSpatialWindow *window)
{
	NautilusWindow *parent;
	NautilusView *view;
	NautilusZoomLevel zoom_level;

	parent = NAUTILUS_WINDOW(window);
	view = parent->details->active_pane->active_slot->content_view;

	if (view == NULL) {
		return NAUTILUS_ICON_SIZE_STANDARD;
	} else {
		zoom_level = nautilus_view_get_zoom_level (view);
		return nautilus_get_icon_size_for_zoom_level (zoom_level);
	}
	
}

static void
location_button_drag_begin_callback (GtkWidget             *widget,
				     GdkDragContext        *context,
				     NautilusSpatialWindow *window)
{
	NautilusWindowSlot *slot;
	GdkPixbuf *pixbuf;

	slot = NAUTILUS_WINDOW (window)->details->active_pane->active_slot;

	pixbuf = nautilus_file_get_icon_pixbuf (slot->viewed_file,
						get_dnd_icon_size (window),
						FALSE,
						NAUTILUS_FILE_ICON_FLAGS_IGNORE_VISITING | NAUTILUS_FILE_ICON_FLAGS_FOR_DRAG_ACCEPT);

	gtk_drag_set_icon_pixbuf (context, pixbuf, 0, 0);

	g_object_unref (pixbuf);
}

/* build GNOME icon list, which only contains the window's URI.
 * If we just used URIs, moving the folder to trash
 * wouldn't work */
static void
get_data_binder (NautilusDragEachSelectedItemDataGet iteratee,
		 gpointer                            iterator_context,
		 gpointer                            data)
{
	NautilusSpatialWindow *window;
	NautilusWindowSlot *slot;
	char *location;
	int icon_size;

	g_assert (NAUTILUS_IS_SPATIAL_WINDOW (iterator_context));
	window = NAUTILUS_SPATIAL_WINDOW (iterator_context);

	slot = NAUTILUS_WINDOW (window)->details->active_pane->active_slot;

	location = nautilus_window_slot_get_location_uri (slot);
	icon_size = get_dnd_icon_size (window);

	iteratee (location,
		  0,
		  0,
		  icon_size,
		  icon_size,
		  data);

	g_free (location);
}

static void
location_button_drag_data_get_callback (GtkWidget             *widget,
					GdkDragContext        *context,
					GtkSelectionData      *selection_data,
					guint                  info,
					guint                  time,
					NautilusSpatialWindow *window)
{
	nautilus_drag_drag_data_get (widget, context, selection_data,
				     info, time, window, get_data_binder); 
}

void
nautilus_spatial_window_set_location_button  (NautilusSpatialWindow *window,
					      GFile                 *location)
{
	if (location != NULL) {
		NautilusFile *file;
		char *name;
		GError *error;

		file = nautilus_file_get (location);

		/* FIXME: monitor for name change... */
		name = nautilus_file_get_display_name (file);
		gtk_label_set_label (GTK_LABEL (window->details->location_label),
				     name);
		g_free (name);
		gtk_widget_set_sensitive (window->details->location_button, TRUE);

		error = nautilus_file_get_file_info_error (file);
		if (error == NULL) {
			GdkPixbuf *pixbuf;

			pixbuf = nautilus_file_get_icon_pixbuf (file,
								nautilus_get_icon_size_for_stock_size (GTK_ICON_SIZE_MENU),
								TRUE,
								NAUTILUS_FILE_ICON_FLAGS_IGNORE_VISITING);

			if (pixbuf != NULL) {
				gtk_image_set_from_pixbuf (GTK_IMAGE (window->details->location_icon),  pixbuf);
				g_object_unref (pixbuf);
			} else {
				gtk_image_set_from_stock (GTK_IMAGE (window->details->location_icon),
							  GTK_STOCK_OPEN, GTK_ICON_SIZE_MENU);
			}
		}
		g_object_unref (file);

	} else {
		gtk_label_set_label (GTK_LABEL (window->details->location_label),
				     "");
		gtk_widget_set_sensitive (window->details->location_button, FALSE);
	}
}

static void
action_go_to_location_callback (GtkAction *action,
				gpointer user_data)
{
	NautilusWindow *window;

	window = NAUTILUS_WINDOW (user_data);

	nautilus_window_prompt_for_location (window, NULL);
}

static void
action_add_bookmark_callback (GtkAction *action,
			      gpointer user_data)
{
	NautilusWindow *window;

	window = NAUTILUS_WINDOW (user_data);

	if (!NAUTILUS_IS_DESKTOP_WINDOW (window)) { /* don't bookmark x-nautilus-desktop:/// */
		nautilus_window_add_bookmark_for_current_location (window);
	}
}

static void
action_edit_bookmarks_callback (GtkAction *action, 
				gpointer user_data)
{
        nautilus_window_edit_bookmarks (NAUTILUS_WINDOW (user_data));
}

static void
action_search_callback (GtkAction *action,
			gpointer user_data)
{
	NautilusWindow *window;
	char *uri;
	GFile *f;

	window = NAUTILUS_WINDOW (user_data);

	uri = nautilus_search_directory_generate_new_uri ();
	f = g_file_new_for_uri (uri);
	nautilus_window_go_to (window, f);
	g_object_unref (f);
	g_free (uri);
}

static const GtkActionEntry spatial_entries[] = {
  /* name, stock id, label */  { SPATIAL_ACTION_PLACES, NULL, N_("_Places") },
  /* name, stock id, label */  { SPATIAL_ACTION_GO_TO_LOCATION, NULL, N_("Open _Location..."),
                                 "<control>L", N_("Specify a location to open"),
                                 G_CALLBACK (action_go_to_location_callback) },
  /* name, stock id, label */  { SPATIAL_ACTION_CLOSE_PARENT_FOLDERS, NULL, N_("Close P_arent Folders"),
                                 "<control><shift>W", N_("Close this folder's parents"),
                                 G_CALLBACK (action_close_parent_folders_callback) },
  /* name, stock id, label */  { SPATIAL_ACTION_CLOSE_ALL_FOLDERS, NULL, N_("Clos_e All Folders"), 
                                 "<control>Q", N_("Close all folder windows"),
                                 G_CALLBACK (action_close_all_folders_callback) },
  /* name, stock id, label */  { "Add Bookmark", GTK_STOCK_ADD, N_("_Add Bookmark"),
                                 "<control>d", N_("Add a bookmark for the current location to this menu"),
                                 G_CALLBACK (action_add_bookmark_callback) },
  /* name, stock id, label */  { "Edit Bookmarks", NULL, N_("_Edit Bookmarks..."),
                                 "<control>b", N_("Display a window that allows editing the bookmarks in this menu"),
                                 G_CALLBACK (action_edit_bookmarks_callback) },
  /* name, stock id, label */  { "Search", "gtk-find", N_("_Search for Files..."),
                                 "<control>F", N_("Locate documents and folders on this computer by name or content"),
                                 G_CALLBACK (action_search_callback) },
};

static void
nautilus_spatial_window_init (NautilusSpatialWindow *window)
{
	GtkRcStyle *rc_style;
	GtkWidget *arrow;
	GtkWidget *hbox, *vbox;
	GtkActionGroup *action_group;
	GtkUIManager *ui_manager;
	GtkTargetList *targets;
	const char *ui;
	NautilusWindow *win;
	NautilusWindowPane *pane;

	window->details = G_TYPE_INSTANCE_GET_PRIVATE (window,
						       NAUTILUS_TYPE_SPATIAL_WINDOW,
						       NautilusSpatialWindowDetails);

	win = NAUTILUS_WINDOW(window);

	pane = nautilus_window_pane_new (win);

	window->affect_spatial_window_on_next_location_change = TRUE;

	vbox = gtk_vbox_new (FALSE, 0);
	gtk_table_attach (GTK_TABLE (NAUTILUS_WINDOW (window)->details->table),
			  vbox,
			  /* X direction */                   /* Y direction */
			  0, 1,                               1, 4,
			  GTK_EXPAND | GTK_FILL | GTK_SHRINK, GTK_EXPAND | GTK_FILL | GTK_SHRINK,
			  0,                                  0);
	gtk_widget_show (vbox);
	window->details->content_box = vbox;

	window->details->location_button = gtk_button_new ();
	g_signal_connect (window->details->location_button,
			  "button-press-event",
			  G_CALLBACK (location_button_pressed_callback),
			  window);
	gtk_button_set_relief (GTK_BUTTON (window->details->location_button),
			       GTK_RELIEF_NORMAL);
	rc_style = gtk_widget_get_modifier_style (window->details->location_button);
	rc_style->xthickness = 0;
	rc_style->ythickness = 0;
	gtk_widget_modify_style (window->details->location_button, 
				 rc_style);

	gtk_widget_show (window->details->location_button);
	hbox = gtk_hbox_new (FALSE, 3);
	gtk_container_add (GTK_CONTAINER (window->details->location_button), 
			   hbox);
	gtk_widget_show (hbox);

	window->details->location_icon = gtk_image_new_from_stock (GTK_STOCK_OPEN, GTK_ICON_SIZE_MENU);
	gtk_box_pack_start (GTK_BOX (hbox), window->details->location_icon, FALSE, FALSE, 0);
	gtk_widget_show (window->details->location_icon);
	
	window->details->location_label = gtk_label_new ("");
	gtk_label_set_ellipsize (GTK_LABEL (window->details->location_label), PANGO_ELLIPSIZE_END);
	gtk_label_set_max_width_chars (GTK_LABEL (window->details->location_label), MAX_SHORTNAME_PATH);
	gtk_box_pack_start (GTK_BOX (hbox), window->details->location_label,
			    FALSE, FALSE, 0);
	gtk_widget_show (window->details->location_label);
	
	arrow = gtk_arrow_new (GTK_ARROW_DOWN, GTK_SHADOW_NONE);
	gtk_box_pack_start (GTK_BOX (hbox), arrow, FALSE, FALSE, 0);
	gtk_widget_show (arrow);

	gtk_drag_source_set (window->details->location_button,
			     GDK_BUTTON1_MASK | GDK_BUTTON2_MASK, location_button_drag_types,
			     G_N_ELEMENTS (location_button_drag_types),
			     GDK_ACTION_MOVE | GDK_ACTION_COPY | GDK_ACTION_LINK | GDK_ACTION_ASK);
	g_signal_connect (window->details->location_button,
			  "drag_begin",
			  G_CALLBACK (location_button_drag_begin_callback),
			  window);
	g_signal_connect (window->details->location_button,
			  "drag_data_get",
			  G_CALLBACK (location_button_drag_data_get_callback),
			  window);

	targets = gtk_drag_source_get_target_list (window->details->location_button);
	gtk_target_list_add_text_targets (targets, NAUTILUS_ICON_DND_TEXT);

	gtk_widget_set_sensitive (window->details->location_button, FALSE);
	g_signal_connect (window->details->location_button, 
			  "clicked", 
			  G_CALLBACK (location_button_clicked_callback), window);
	gtk_box_pack_start (GTK_BOX (NAUTILUS_WINDOW (window)->details->statusbar),
			    window->details->location_button,
			    FALSE, TRUE, 0);

	gtk_box_reorder_child (GTK_BOX (NAUTILUS_WINDOW (window)->details->statusbar), 
			       window->details->location_button, 0);
	
	action_group = gtk_action_group_new ("SpatialActions");
	gtk_action_group_set_translation_domain (action_group, GETTEXT_PACKAGE);
	window->details->spatial_action_group = action_group;
	gtk_action_group_add_actions (action_group, 
				      spatial_entries, G_N_ELEMENTS (spatial_entries),
				      window);
	
	ui_manager = nautilus_window_get_ui_manager (NAUTILUS_WINDOW (window));
	gtk_ui_manager_insert_action_group (ui_manager, action_group, 0);
	g_object_unref (action_group); /* owned by ui manager */
	
	ui = nautilus_ui_string_get ("nautilus-spatial-window-ui.xml");
	gtk_ui_manager_add_ui_from_string (ui_manager, ui, -1, NULL);

	nautilus_window_set_active_pane (win, pane);
}

static void
nautilus_spatial_window_class_init (NautilusSpatialWindowClass *class)
{
	GtkBindingSet *binding_set;
	
	NAUTILUS_WINDOW_CLASS (class)->window_type = NAUTILUS_WINDOW_SPATIAL;
	NAUTILUS_WINDOW_CLASS (class)->bookmarks_placeholder = MENU_PATH_SPATIAL_BOOKMARKS_PLACEHOLDER;

	G_OBJECT_CLASS (class)->finalize = nautilus_spatial_window_finalize;
	GTK_OBJECT_CLASS (class)->destroy = nautilus_spatial_window_destroy;
	GTK_WIDGET_CLASS (class)->show = nautilus_spatial_window_show;
	GTK_WIDGET_CLASS (class)->configure_event = nautilus_spatial_window_configure_event;
	GTK_WIDGET_CLASS (class)->unrealize = nautilus_spatial_window_unrealize;
	GTK_WIDGET_CLASS (class)->window_state_event = nautilus_spatial_window_state_event;

	NAUTILUS_WINDOW_CLASS (class)->prompt_for_location = 
		real_prompt_for_location;
	NAUTILUS_WINDOW_CLASS (class)->get_icon =
		real_get_icon;
	NAUTILUS_WINDOW_CLASS (class)->sync_title = 
		real_sync_title;
	NAUTILUS_WINDOW_CLASS(class)->get_default_size = real_get_default_size;

	NAUTILUS_WINDOW_CLASS(class)->sync_allow_stop =
		real_sync_allow_stop;
	NAUTILUS_WINDOW_CLASS(class)->set_allow_up =
		real_set_allow_up;

	NAUTILUS_WINDOW_CLASS (class)->open_slot = real_open_slot;
	NAUTILUS_WINDOW_CLASS (class)->close = real_window_close;
	NAUTILUS_WINDOW_CLASS (class)->close_slot = real_close_slot;

	binding_set = gtk_binding_set_by_class (class);
	gtk_binding_entry_add_signal (binding_set, GDK_BackSpace, GDK_SHIFT_MASK,
				      "go_up", 1,
				      G_TYPE_BOOLEAN, TRUE);
	gtk_binding_entry_add_signal (binding_set, GDK_Up, GDK_SHIFT_MASK | GDK_MOD1_MASK,
				      "go_up", 1,
				      G_TYPE_BOOLEAN, TRUE);

	g_type_class_add_private (G_OBJECT_CLASS (class), sizeof(NautilusSpatialWindowDetails));
}
