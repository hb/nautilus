/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  Nautilus
 *
 *  Copyright (C) 1999, 2000, 2004 Red Hat, Inc.
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
 *           Alexander Larsson <alexl@redhat.com>
 */

/* nautilus-window.c: Implementation of the main window object */

#include <config.h>

#include "nautilus-window-private.h"

#include "nautilus-actions.h"
#include "nautilus-bookmarks-window.h"
#include "nautilus-location-bar.h"
#include "nautilus-mime-actions.h"
#include "nautilus-notebook.h"
#include "nautilus-places-sidebar.h"
#include "nautilus-search-bar.h"
#include "nautilus-tree-sidebar.h"
#include "nautilus-view-factory.h"
#include "nautilus-window-manage-views.h"
#include "nautilus-window-bookmarks.h"
#include "nautilus-window-slot.h"

#include <eel/eel-debug.h>
#include <eel/eel-gtk-extensions.h>
#include <eel/eel-string.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/gdkx.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>
#ifdef HAVE_X11_XF86KEYSYM_H
#include <X11/XF86keysym.h>
#endif
#include <libnautilus-private/nautilus-file-utilities.h>
#include <libnautilus-private/nautilus-file-attributes.h>
#include <libnautilus-private/nautilus-global-preferences.h>
#include <libnautilus-private/nautilus-metadata.h>
#include <libnautilus-private/nautilus-clipboard.h>
#include <libnautilus-private/nautilus-search-directory.h>
#include <libnautilus-private/nautilus-signaller.h>

#define DEBUG_FLAG NAUTILUS_DEBUG_WINDOW
#include <libnautilus-private/nautilus-debug.h>

#include <math.h>
#include <sys/time.h>

/* dock items */
#define NAUTILUS_MENU_PATH_SHORT_LIST_PLACEHOLDER  	"/MenuBar/View/View Choices/Short List"

#define MAX_TITLE_LENGTH 180

/* Forward and back buttons on the mouse */
static gboolean mouse_extra_buttons = TRUE;
static int mouse_forward_button = 9;
static int mouse_back_button = 8;

static void mouse_back_button_changed		     (gpointer                  callback_data);
static void mouse_forward_button_changed	     (gpointer                  callback_data);
static void use_extra_mouse_buttons_changed          (gpointer                  callback_data);

/* Sanity check: highest mouse button value I could find was 14. 5 is our 
 * lower threshold (well-documented to be the one of the button events for the 
 * scrollwheel), so it's hardcoded in the functions below. However, if you have
 * a button that registers higher and want to map it, file a bug and 
 * we'll move the bar. Makes you wonder why the X guys don't have 
 * defined values for these like the XKB stuff, huh?
 */
#define UPPER_MOUSE_LIMIT 14

enum {
	PROP_DISABLE_CHROME = 1,
	NUM_PROPERTIES,
};

enum {
	GO_UP,
	RELOAD,
	PROMPT_FOR_LOCATION,
	LOADING_URI,
	HIDDEN_FILES_MODE_CHANGED,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };
static GParamSpec *properties[NUM_PROPERTIES] = { NULL, };

typedef struct  {
	NautilusWindow *window;
	char *id;
} ActivateViewData;

static void cancel_view_as_callback         (NautilusWindowSlot      *slot);
static void action_view_as_callback         (GtkAction               *action,
					     ActivateViewData        *data);

G_DEFINE_TYPE (NautilusWindow, nautilus_window, GTK_TYPE_APPLICATION_WINDOW);

static const struct {
	unsigned int keyval;
	const char *action;
} extra_window_keybindings [] = {
#ifdef HAVE_X11_XF86KEYSYM_H
	{ XF86XK_AddFavorite,	NAUTILUS_ACTION_ADD_BOOKMARK },
	{ XF86XK_Favorites,	NAUTILUS_ACTION_EDIT_BOOKMARKS },
	{ XF86XK_Go,		NAUTILUS_ACTION_GO_TO_LOCATION },
	{ XF86XK_HomePage,      NAUTILUS_ACTION_GO_HOME },
	{ XF86XK_OpenURL,	NAUTILUS_ACTION_GO_TO_LOCATION },
	{ XF86XK_Refresh,	NAUTILUS_ACTION_RELOAD },
	{ XF86XK_Reload,	NAUTILUS_ACTION_RELOAD },
	{ XF86XK_Search,	NAUTILUS_ACTION_SEARCH },
	{ XF86XK_Start,		NAUTILUS_ACTION_GO_HOME },
	{ XF86XK_Stop,		NAUTILUS_ACTION_STOP },
	{ XF86XK_ZoomIn,	NAUTILUS_ACTION_ZOOM_IN },
	{ XF86XK_ZoomOut,	NAUTILUS_ACTION_ZOOM_OUT },
	{ XF86XK_Back,		NAUTILUS_ACTION_BACK },
	{ XF86XK_Forward,	NAUTILUS_ACTION_FORWARD }

#endif
};

void
nautilus_window_push_status (NautilusWindow *window,
			     const char *text)
{
	g_return_if_fail (NAUTILUS_IS_WINDOW (window));

	/* clear any previous message, underflow is allowed */
	gtk_statusbar_pop (GTK_STATUSBAR (window->details->statusbar), 0);

	if (text != NULL && text[0] != '\0') {
		gtk_statusbar_push (GTK_STATUSBAR (window->details->statusbar), 0, text);
	}
}

void
nautilus_window_go_to (NautilusWindow *window, GFile *location)
{
	g_return_if_fail (NAUTILUS_IS_WINDOW (window));

	nautilus_window_slot_open_location (nautilus_window_get_active_slot (window),
					    location, 0);
}

void
nautilus_window_go_to_full (NautilusWindow *window,
			    GFile          *location,
			    NautilusWindowGoToCallback callback,
			    gpointer        user_data)
{
	g_return_if_fail (NAUTILUS_IS_WINDOW (window));

	nautilus_window_slot_open_location_full (nautilus_window_get_active_slot (window),
						 location, 0, NULL, callback, user_data);
}

static void
nautilus_window_go_up_signal (NautilusWindow *window)
{
	nautilus_window_slot_go_up (nautilus_window_get_active_slot (window), 0);
}

void
nautilus_window_new_tab (NautilusWindow *window)
{
	NautilusWindowSlot *current_slot;
	NautilusWindowSlot *new_slot;
	NautilusWindowOpenFlags flags;
	GFile *location;
	int new_slot_position;
	char *scheme;

	current_slot = nautilus_window_get_active_slot (window);
	location = nautilus_window_slot_get_location (current_slot);

	if (location != NULL) {
		flags = 0;

		new_slot_position = g_settings_get_enum (nautilus_preferences, NAUTILUS_PREFERENCES_NEW_TAB_POSITION);
		if (new_slot_position == NAUTILUS_NEW_TAB_POSITION_END) {
			flags = NAUTILUS_WINDOW_OPEN_SLOT_APPEND;
		}

		scheme = g_file_get_uri_scheme (location);
		if (!strcmp (scheme, "x-nautilus-search")) {
			g_object_unref (location);
			location = g_file_new_for_path (g_get_home_dir ());
		}
		g_free (scheme);

		new_slot = nautilus_window_pane_open_slot (current_slot->pane, flags);
		nautilus_window_set_active_slot (window, new_slot);
		nautilus_window_slot_open_location (new_slot, location, 0);
		g_object_unref (location);
	}
}

static void
update_cursor (NautilusWindow *window)
{
	NautilusWindowSlot *slot;
	GdkCursor *cursor;

	slot = nautilus_window_get_active_slot (window);

	if (slot->allow_stop) {
		cursor = gdk_cursor_new (GDK_WATCH);
		gdk_window_set_cursor (gtk_widget_get_window (GTK_WIDGET (window)), cursor);
		g_object_unref (cursor);
	} else {
		gdk_window_set_cursor (gtk_widget_get_window (GTK_WIDGET (window)), NULL);
	}
}

void
nautilus_window_sync_allow_stop (NautilusWindow *window,
				 NautilusWindowSlot *slot)
{
	GtkAction *action;
	gboolean allow_stop, slot_is_active;
	NautilusNotebook *notebook;

	action = gtk_action_group_get_action (nautilus_window_get_main_action_group (window),
					      NAUTILUS_ACTION_STOP);
	allow_stop = gtk_action_get_sensitive (action);

	slot_is_active = (slot == nautilus_window_get_active_slot (window));

	if (!slot_is_active ||
	    allow_stop != slot->allow_stop) {
		if (slot_is_active) {
			gtk_action_set_sensitive (action, slot->allow_stop);
		}

		if (gtk_widget_get_realized (GTK_WIDGET (window))) {
			update_cursor (window);
		}


		notebook = NAUTILUS_NOTEBOOK (slot->pane->notebook);
		nautilus_notebook_sync_loading (notebook, slot);
	}
}

static void
nautilus_window_prompt_for_location (NautilusWindow *window,
				     const char     *initial)
{	
	NautilusWindowPane *pane;

	g_return_if_fail (NAUTILUS_IS_WINDOW (window));

	pane = window->details->active_pane;
	nautilus_window_pane_ensure_location_bar (pane);

	if (initial) {
		nautilus_location_bar_set_location (NAUTILUS_LOCATION_BAR (pane->location_bar),
						    initial);
	}
}

/* Code should never force the window taller than this size.
 * (The user can still stretch the window taller if desired).
 */
static guint
get_max_forced_height (GdkScreen *screen)
{
	return (gdk_screen_get_height (screen) * 90) / 100;
}

/* Code should never force the window wider than this size.
 * (The user can still stretch the window wider if desired).
 */
static guint
get_max_forced_width (GdkScreen *screen)
{
	return (gdk_screen_get_width (screen) * 90) / 100;
}

/* This must be called when construction of NautilusWindow is finished,
 * since it depends on the type of the argument, which isn't decided at
 * construction time.
 */
static void
nautilus_window_set_initial_window_geometry (NautilusWindow *window)
{
	GdkScreen *screen;
	guint max_width_for_screen, max_height_for_screen;
	guint default_width, default_height;

	screen = gtk_window_get_screen (GTK_WINDOW (window));
	
	max_width_for_screen = get_max_forced_width (screen);
	max_height_for_screen = get_max_forced_height (screen);
	
	default_width = NAUTILUS_WINDOW_DEFAULT_WIDTH;
	default_height = NAUTILUS_WINDOW_DEFAULT_HEIGHT;

	gtk_window_set_default_size (GTK_WINDOW (window), 
				     MIN (default_width, 
				          max_width_for_screen), 
				     MIN (default_height, 
				          max_height_for_screen));
}

static gboolean
save_sidebar_width_cb (gpointer user_data)
{
	NautilusWindow *window = user_data;

	window->details->sidebar_width_handler_id = 0;

	DEBUG ("Saving sidebar width: %d", window->details->side_pane_width);

	g_settings_set_int (nautilus_window_state,
			    NAUTILUS_WINDOW_STATE_SIDEBAR_WIDTH,
			    window->details->side_pane_width);

	return FALSE;
}

/* side pane helpers */
static void
side_pane_size_allocate_callback (GtkWidget *widget,
				  GtkAllocation *allocation,
				  gpointer user_data)
{
	NautilusWindow *window;

	window = user_data;

	if (window->details->sidebar_width_handler_id != 0) {
		g_source_remove (window->details->sidebar_width_handler_id);
		window->details->sidebar_width_handler_id = 0;
	}

	if (allocation->width != window->details->side_pane_width &&
	    allocation->width > 1) {
		window->details->side_pane_width = allocation->width;

		window->details->sidebar_width_handler_id =
			g_idle_add (save_sidebar_width_cb, window);
	}
}

static void
setup_side_pane_width (NautilusWindow *window)
{
	g_return_if_fail (window->details->sidebar != NULL);

	window->details->side_pane_width =
		g_settings_get_int (nautilus_window_state,
				    NAUTILUS_WINDOW_STATE_SIDEBAR_WIDTH);

	gtk_paned_set_position (GTK_PANED (window->details->content_paned),
				window->details->side_pane_width);
}

static gboolean
sidebar_id_is_valid (const gchar *sidebar_id)
{
	return (g_strcmp0 (sidebar_id, NAUTILUS_WINDOW_SIDEBAR_PLACES) == 0 ||
		g_strcmp0 (sidebar_id, NAUTILUS_WINDOW_SIDEBAR_TREE) == 0);
}

static void
nautilus_window_set_up_sidebar (NautilusWindow *window)
{
	GtkWidget *sidebar;

	DEBUG ("Setting up sidebar id %s", window->details->sidebar_id);

	window->details->sidebar = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
	gtk_style_context_add_class (gtk_widget_get_style_context (window->details->sidebar),
				     GTK_STYLE_CLASS_SIDEBAR);

	gtk_paned_pack1 (GTK_PANED (window->details->content_paned),
			 GTK_WIDGET (window->details->sidebar),
			 FALSE, FALSE);

	setup_side_pane_width (window);
	g_signal_connect (window->details->sidebar, 
			  "size_allocate",
			  G_CALLBACK (side_pane_size_allocate_callback),
			  window);

	if (g_strcmp0 (window->details->sidebar_id, NAUTILUS_WINDOW_SIDEBAR_PLACES) == 0) {
		sidebar = nautilus_places_sidebar_new (window);
	} else if (g_strcmp0 (window->details->sidebar_id, NAUTILUS_WINDOW_SIDEBAR_TREE) == 0) {
		sidebar = nautilus_tree_sidebar_new (window);
	} else {
		g_assert_not_reached ();
	}

	gtk_box_pack_start (GTK_BOX (window->details->sidebar), sidebar, TRUE, TRUE, 0);
	gtk_widget_show (sidebar);
	gtk_widget_show (GTK_WIDGET (window->details->sidebar));
}

static void
nautilus_window_tear_down_sidebar (NautilusWindow *window)
{
	DEBUG ("Destroying sidebar");

	if (window->details->sidebar != NULL) {
		gtk_widget_destroy (GTK_WIDGET (window->details->sidebar));
		window->details->sidebar = NULL;
	}
}

void
nautilus_window_hide_sidebar (NautilusWindow *window)
{
	DEBUG ("Called hide_sidebar()");

	if (window->details->sidebar == NULL) {
		return;
	}

	nautilus_window_tear_down_sidebar (window);
	nautilus_window_update_show_hide_menu_items (window);

	g_settings_set_boolean (nautilus_window_state, NAUTILUS_WINDOW_STATE_START_WITH_SIDEBAR, FALSE);
}

void
nautilus_window_show_sidebar (NautilusWindow *window)
{
	DEBUG ("Called show_sidebar()");

	if (window->details->sidebar != NULL) {
		return;
	}

	if (window->details->disable_chrome) {
		return;
	}

	nautilus_window_set_up_sidebar (window);
	nautilus_window_update_show_hide_menu_items (window);
	g_settings_set_boolean (nautilus_window_state, NAUTILUS_WINDOW_STATE_START_WITH_SIDEBAR, TRUE);
}

static void
side_pane_id_changed (NautilusWindow *window)
{
	gchar *sidebar_id;

	sidebar_id = g_settings_get_string (nautilus_window_state,
					    NAUTILUS_WINDOW_STATE_SIDE_PANE_VIEW);

	DEBUG ("Sidebar id changed to %s", sidebar_id);

	if (g_strcmp0 (sidebar_id, window->details->sidebar_id) == 0) {
		g_free (sidebar_id);
		return;
	}

	if (!sidebar_id_is_valid (sidebar_id)) {
		g_free (sidebar_id);
		return;
	}

	g_free (window->details->sidebar_id);
	window->details->sidebar_id = sidebar_id;

	if (window->details->sidebar != NULL) {
		/* refresh the sidebar setting */
		nautilus_window_tear_down_sidebar (window);
		nautilus_window_set_up_sidebar (window);
	}
}

gboolean
nautilus_window_disable_chrome_mapping (GValue *value,
					GVariant *variant,
					gpointer user_data)
{
	NautilusWindow *window = user_data;

	g_value_set_boolean (value,
			     g_variant_get_boolean (variant) &&
			     !window->details->disable_chrome);

	return TRUE;
}

static void
nautilus_window_constructed (GObject *self)
{
	NautilusWindow *window;
	GtkWidget *grid;
	GtkWidget *menu;
	GtkWidget *statusbar;
	GtkWidget *hpaned;
	GtkWidget *vbox;
	NautilusWindowPane *pane;
	NautilusWindowSlot *slot;

	window = NAUTILUS_WINDOW (self);

	G_OBJECT_CLASS (nautilus_window_parent_class)->constructed (self);

	/* disable automatic menubar handling, since we show our regular
	 * menubar together with the app menu.
	 */
	gtk_application_window_set_show_menubar (GTK_APPLICATION_WINDOW (self), FALSE);

	grid = gtk_grid_new ();
	gtk_orientable_set_orientation (GTK_ORIENTABLE (grid), GTK_ORIENTATION_VERTICAL);
	gtk_widget_show (grid);
	gtk_container_add (GTK_CONTAINER (window), grid);

	statusbar = gtk_statusbar_new ();
	window->details->statusbar = statusbar;
	window->details->help_message_cid = gtk_statusbar_get_context_id
		(GTK_STATUSBAR (statusbar), "help_message");
	/* Statusbar is packed in the subclasses */

	nautilus_window_initialize_menus (window);
	nautilus_window_initialize_actions (window);

	menu = gtk_ui_manager_get_widget (window->details->ui_manager, "/MenuBar");
	window->details->menubar = menu;
	gtk_widget_set_hexpand (menu, TRUE);
	gtk_widget_show (menu);
	gtk_container_add (GTK_CONTAINER (grid), menu);

	/* Register to menu provider extension signal managing menu updates */
	g_signal_connect_object (nautilus_signaller_get_current (), "popup_menu_changed",
			 G_CALLBACK (nautilus_window_load_extension_menus), window, G_CONNECT_SWAPPED);

	window->details->content_paned = gtk_paned_new (GTK_ORIENTATION_HORIZONTAL);
	gtk_widget_set_hexpand (window->details->content_paned, TRUE);
	gtk_widget_set_vexpand (window->details->content_paned, TRUE);

	gtk_container_add (GTK_CONTAINER (grid), window->details->content_paned);
	gtk_widget_show (window->details->content_paned);

	vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
	gtk_paned_pack2 (GTK_PANED (window->details->content_paned), vbox,
			 TRUE, FALSE);
	gtk_widget_show (vbox);

	hpaned = gtk_paned_new (GTK_ORIENTATION_HORIZONTAL);
	gtk_box_pack_start (GTK_BOX (vbox), hpaned, TRUE, TRUE, 0);
	gtk_widget_show (hpaned);
	window->details->split_view_hpane = hpaned;

	gtk_box_pack_start (GTK_BOX (vbox), window->details->statusbar, FALSE, FALSE, 0);

	g_settings_bind_with_mapping (nautilus_window_state,
				      NAUTILUS_WINDOW_STATE_START_WITH_STATUS_BAR,
				      window->details->statusbar,
				      "visible",
				      G_SETTINGS_BIND_DEFAULT,
				      nautilus_window_disable_chrome_mapping, NULL,
				      window, NULL);

	pane = nautilus_window_pane_new (window);
	window->details->panes = g_list_prepend (window->details->panes, pane);

	gtk_paned_pack1 (GTK_PANED (hpaned), GTK_WIDGET (pane), TRUE, FALSE);

	/* this has to be done after the location bar has been set up,
	 * but before menu stuff is being called */
	nautilus_window_set_active_pane (window, pane);

	g_signal_connect_swapped (nautilus_window_state,
				  "changed::" NAUTILUS_WINDOW_STATE_SIDE_PANE_VIEW,
				  G_CALLBACK (side_pane_id_changed),
				  window);

	side_pane_id_changed (window);

	nautilus_window_initialize_bookmarks_menu (window);
	nautilus_window_set_initial_window_geometry (window);

	slot = nautilus_window_pane_open_slot (window->details->active_pane, 0);
	nautilus_window_set_active_slot (window, slot);
}

static void
nautilus_window_set_property (GObject *object,
			      guint arg_id,
			      const GValue *value,
			      GParamSpec *pspec)
{
	NautilusWindow *window;

	window = NAUTILUS_WINDOW (object);
	
	switch (arg_id) {
	case PROP_DISABLE_CHROME:
		window->details->disable_chrome = g_value_get_boolean (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, arg_id, pspec);
		break;
	}
}

static void
nautilus_window_get_property (GObject *object,
			      guint arg_id,
			      GValue *value,
			      GParamSpec *pspec)
{
	NautilusWindow *window;

	window = NAUTILUS_WINDOW (object);

	switch (arg_id) {
	case PROP_DISABLE_CHROME:
		g_value_set_boolean (value, window->details->disable_chrome);
		break;
	}
}

static void
free_stored_viewers (NautilusWindow *window)
{
	g_list_free_full (window->details->short_list_viewers, g_free);
	window->details->short_list_viewers = NULL;
}

static void
destroy_panes_foreach (gpointer data,
		       gpointer user_data)
{
	NautilusWindowPane *pane = data;
	NautilusWindow *window = user_data;

	nautilus_window_close_pane (window, pane);
}

static void
nautilus_window_destroy (GtkWidget *object)
{
	NautilusWindow *window;
	GList *panes_copy;

	window = NAUTILUS_WINDOW (object);

	DEBUG ("Destroying window");

	/* close the sidebar first */
	nautilus_window_tear_down_sidebar (window);

	/* close all panes safely */
	panes_copy = g_list_copy (window->details->panes);
	g_list_foreach (panes_copy, (GFunc) destroy_panes_foreach, window);
	g_list_free (panes_copy);

	/* the panes list should now be empty */
	g_assert (window->details->panes == NULL);
	g_assert (window->details->active_pane == NULL);

	GTK_WIDGET_CLASS (nautilus_window_parent_class)->destroy (object);
}

static void
nautilus_window_finalize (GObject *object)
{
	NautilusWindow *window;

	window = NAUTILUS_WINDOW (object);

	if (window->details->sidebar_width_handler_id != 0) {
		g_source_remove (window->details->sidebar_width_handler_id);
		window->details->sidebar_width_handler_id = 0;
	}

	nautilus_window_finalize_menus (window);
	g_signal_handlers_disconnect_by_func (nautilus_window_state,
					      side_pane_id_changed, window);

	g_clear_object (&window->details->nav_state);
	g_clear_object (&window->details->bookmark_list);
	g_clear_object (&window->details->ui_manager);

	g_free (window->details->sidebar_id);
	free_stored_viewers (window);

	/* nautilus_window_close() should have run */
	g_assert (window->details->panes == NULL);

	G_OBJECT_CLASS (nautilus_window_parent_class)->finalize (object);
}

void
nautilus_window_view_visible (NautilusWindow *window,
			      NautilusView *view)
{
	NautilusWindowSlot *slot;
	NautilusWindowPane *pane;
	GList *l, *walk;

	g_return_if_fail (NAUTILUS_IS_WINDOW (window));

	slot = nautilus_window_get_slot_for_view (window, view);

	if (slot->visible) {
		return;
	}

	slot->visible = TRUE;
	pane = slot->pane;

	if (gtk_widget_get_visible (GTK_WIDGET (pane))) {
		return;
	}

	/* Look for other non-visible slots */
	for (l = pane->slots; l != NULL; l = l->next) {
		slot = l->data;

		if (!slot->visible) {
			return;
		}
	}

	/* None, this pane is visible */
	gtk_widget_show (GTK_WIDGET (pane));

	/* Look for other non-visible panes */
	for (walk = window->details->panes; walk; walk = walk->next) {
		pane = walk->data;

		if (!gtk_widget_get_visible (GTK_WIDGET (pane))) {
			return;
		}

		for (l = pane->slots; l != NULL; l = l->next) {
			slot = l->data;

			nautilus_window_slot_update_title (slot);
			nautilus_window_slot_update_icon (slot);
		}
	}

	nautilus_window_pane_grab_focus (window->details->active_pane);

	/* All slots and panes visible, show window */
	gtk_widget_show (GTK_WIDGET (window));
}

static void
nautilus_window_save_geometry (NautilusWindow *window)
{
	char *geometry_string;
	gboolean is_maximized;

	g_assert (NAUTILUS_IS_WINDOW (window));

	if (gtk_widget_get_window (GTK_WIDGET (window))) {
		geometry_string = eel_gtk_window_get_geometry_string (GTK_WINDOW (window));
		is_maximized = gdk_window_get_state (gtk_widget_get_window (GTK_WIDGET (window)))
				& GDK_WINDOW_STATE_MAXIMIZED;

		if (!is_maximized) {
			g_settings_set_string
				(nautilus_window_state, NAUTILUS_WINDOW_STATE_GEOMETRY,
				 geometry_string);
		}
		g_free (geometry_string);

		g_settings_set_boolean
			(nautilus_window_state, NAUTILUS_WINDOW_STATE_MAXIMIZED,
			 is_maximized);
	}
}

void
nautilus_window_close (NautilusWindow *window)
{
	NAUTILUS_WINDOW_CLASS (G_OBJECT_GET_CLASS (window))->close (window);
}

void
nautilus_window_close_pane (NautilusWindow *window,
			    NautilusWindowPane *pane)
{
	g_assert (NAUTILUS_IS_WINDOW_PANE (pane));

	while (pane->slots != NULL) {
		NautilusWindowSlot *slot = pane->slots->data;

		nautilus_window_pane_close_slot (pane, slot);
	}

	/* If the pane was active, set it to NULL. The caller is responsible
	 * for setting a new active pane with nautilus_window_set_active_pane()
	 * if it wants to continue using the window. */
	if (window->details->active_pane == pane) {
		window->details->active_pane = NULL;
	}

	window->details->panes = g_list_remove (window->details->panes, pane);

	gtk_widget_destroy (GTK_WIDGET (pane));
}

NautilusWindowPane*
nautilus_window_get_active_pane (NautilusWindow *window)
{
	g_assert (NAUTILUS_IS_WINDOW (window));
	return window->details->active_pane;
}

static void
real_set_active_pane (NautilusWindow *window, NautilusWindowPane *new_pane)
{
	/* make old pane inactive, and new one active.
	 * Currently active pane may be NULL (after init). */
	if (window->details->active_pane &&
	    window->details->active_pane != new_pane) {
		nautilus_window_pane_set_active (window->details->active_pane, FALSE);
	}
	nautilus_window_pane_set_active (new_pane, TRUE);

	window->details->active_pane = new_pane;
}

/* Make the given pane the active pane of its associated window. This
 * always implies making the containing active slot the active slot of
 * the window. */
void
nautilus_window_set_active_pane (NautilusWindow *window,
				 NautilusWindowPane *new_pane)
{
	g_assert (NAUTILUS_IS_WINDOW_PANE (new_pane));

	DEBUG ("Setting new pane %p as active", new_pane);

	if (new_pane->active_slot) {
		nautilus_window_set_active_slot (window, new_pane->active_slot);
	} else if (new_pane != window->details->active_pane) {
		real_set_active_pane (window, new_pane);
	}
}

/* Make both, the given slot the active slot and its corresponding
 * pane the active pane of the associated window.
 * new_slot may be NULL. */
void
nautilus_window_set_active_slot (NautilusWindow *window, NautilusWindowSlot *new_slot)
{
	NautilusWindowSlot *old_slot;

	g_assert (NAUTILUS_IS_WINDOW (window));

	DEBUG ("Setting new slot %p as active", new_slot);

	if (new_slot) {
		g_assert ((window == nautilus_window_slot_get_window (new_slot)));
		g_assert (NAUTILUS_IS_WINDOW_PANE (new_slot->pane));
		g_assert (g_list_find (new_slot->pane->slots, new_slot) != NULL);
	}

	old_slot = nautilus_window_get_active_slot (window);

	if (old_slot == new_slot) {
		return;
	}

	/* make old slot inactive if it exists (may be NULL after init, for example) */
	if (old_slot != NULL) {
		/* inform window */
		if (old_slot->content_view != NULL) {
			nautilus_window_disconnect_content_view (window, old_slot->content_view);
		}

		/* inform slot & view */
		g_signal_emit_by_name (old_slot, "inactive");
	}

	/* deal with panes */
	if (new_slot &&
	    new_slot->pane != window->details->active_pane) {
		real_set_active_pane (window, new_slot->pane);
	}

	window->details->active_pane->active_slot = new_slot;

	/* make new slot active, if it exists */
	if (new_slot) {
		/* inform sidebar panels */
                nautilus_window_report_location_change (window);
		/* TODO decide whether "selection-changed" should be emitted */

		if (new_slot->content_view != NULL) {
                        /* inform window */
                        nautilus_window_connect_content_view (window, new_slot->content_view);
                }

		/* inform slot & view */
                g_signal_emit_by_name (new_slot, "active");
	}
}

static void
nautilus_window_get_preferred_width (GtkWidget *widget,
				     gint *minimal_width,
				     gint *natural_width)
{
	GdkScreen *screen;
	gint max_w, min_w, default_w;

	screen = gtk_window_get_screen (GTK_WINDOW (widget));	

	max_w = get_max_forced_width (screen);
	min_w = NAUTILUS_WINDOW_MIN_WIDTH;

	default_w = NAUTILUS_WINDOW_DEFAULT_WIDTH;

	*minimal_width = MIN (min_w, max_w);
	*natural_width = MIN (default_w, max_w);
}

static void
nautilus_window_get_preferred_height (GtkWidget *widget,
				      gint *minimal_height,
				      gint *natural_height)
{
	GdkScreen *screen;
	gint max_h, min_h, default_h;

	screen = gtk_window_get_screen (GTK_WINDOW (widget));	

	max_h = get_max_forced_height (screen);

	min_h = NAUTILUS_WINDOW_MIN_HEIGHT;

	default_h = NAUTILUS_WINDOW_DEFAULT_HEIGHT;

	*minimal_height = MIN (min_h, max_h);
	*natural_height = MIN (default_h, max_h);
}

static void
nautilus_window_realize (GtkWidget *widget)
{
	GTK_WIDGET_CLASS (nautilus_window_parent_class)->realize (widget);
	update_cursor (NAUTILUS_WINDOW (widget));
}

static gboolean
nautilus_window_key_press_event (GtkWidget *widget,
				 GdkEventKey *event)
{
	NautilusWindow *window;
	NautilusWindowSlot *active_slot;
	NautilusView *view;
	GtkWidget *focus_widget;
	int i;

	window = NAUTILUS_WINDOW (widget);

	active_slot = nautilus_window_get_active_slot (window);
	view = active_slot->content_view;

	if (view != NULL && nautilus_view_get_is_renaming (view)) {
		/* if we're renaming, just forward the event to the
		 * focused widget and return. We don't want to process the window
		 * accelerator bindings, as they might conflict with the 
		 * editable widget bindings.
		 */
		if (gtk_window_propagate_key_event (GTK_WINDOW (window), event)) {
			return TRUE;
		}
	}

	focus_widget = gtk_window_get_focus (GTK_WINDOW (window));
	if (view != NULL && focus_widget != NULL &&
	    GTK_IS_EDITABLE (focus_widget)) {
		/* if we have input focus on a GtkEditable (e.g. a GtkEntry), forward
		 * the event to it before activating accelerator bindings too.
		 */
		if (gtk_window_propagate_key_event (GTK_WINDOW (window), event)) {
			return TRUE;
		}
	}

	for (i = 0; i < G_N_ELEMENTS (extra_window_keybindings); i++) {
		if (extra_window_keybindings[i].keyval == event->keyval) {
			const GList *action_groups;
			GtkAction *action;

			action = NULL;

			action_groups = gtk_ui_manager_get_action_groups (window->details->ui_manager);
			while (action_groups != NULL && action == NULL) {
				action = gtk_action_group_get_action (action_groups->data, extra_window_keybindings[i].action);
				action_groups = action_groups->next;
			}

			g_assert (action != NULL);
			if (gtk_action_is_sensitive (action)) {
				gtk_action_activate (action);
				return TRUE;
			}

			break;
		}
	}

	return GTK_WIDGET_CLASS (nautilus_window_parent_class)->key_press_event (widget, event);
}

/*
 * Main API
 */

static void
free_activate_view_data (gpointer data)
{
	ActivateViewData *activate_data;

	activate_data = data;

	g_free (activate_data->id);

	g_slice_free (ActivateViewData, activate_data);
}

static void
action_view_as_callback (GtkAction *action,
			 ActivateViewData *data)
{
	NautilusWindow *window;
	NautilusWindowSlot *slot;

	window = data->window;

	if (gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (action))) {
		slot = nautilus_window_get_active_slot (window);
		nautilus_window_slot_set_content_view (slot,
						       data->id);
	}
}

static GtkRadioAction *
add_view_as_menu_item (NautilusWindow *window,
		       const char *placeholder_path,
		       const char *identifier,
		       int index, /* extra_viewer is always index 0 */
		       guint merge_id)
{
	const NautilusViewInfo *info;
	GtkRadioAction *action;
	char action_name[32];
	ActivateViewData *data;

	char accel[32];
	char accel_path[48];
	unsigned int accel_keyval;

	info = nautilus_view_factory_lookup (identifier);
	
	g_snprintf (action_name, sizeof (action_name), "view_as_%d", index);
	action = gtk_radio_action_new (action_name,
				       _(info->view_menu_label_with_mnemonic),
				       _(info->display_location_label),
				       NULL,
				       0);

	if (index >= 1 && index <= 9) {
		g_snprintf (accel, sizeof (accel), "%d", index);
		g_snprintf (accel_path, sizeof (accel_path), "<Nautilus-Window>/%s", action_name);

		accel_keyval = gdk_keyval_from_name (accel);
		g_assert (accel_keyval != GDK_KEY_VoidSymbol);

		gtk_accel_map_add_entry (accel_path, accel_keyval, GDK_CONTROL_MASK);
		gtk_action_set_accel_path (GTK_ACTION (action), accel_path);
	}

	if (window->details->view_as_radio_action != NULL) {
		gtk_radio_action_set_group (action,
					    gtk_radio_action_get_group (window->details->view_as_radio_action));
	} else if (index != 0) {
		/* Index 0 is the extra view, and we don't want to use that here,
		   as it can get deleted/changed later */
		window->details->view_as_radio_action = action;
	}

	data = g_slice_new (ActivateViewData);
	data->window = window;
	data->id = g_strdup (identifier);
	g_signal_connect_data (action, "activate",
			       G_CALLBACK (action_view_as_callback),
			       data, (GClosureNotify) free_activate_view_data, 0);
	
	gtk_action_group_add_action (window->details->view_as_action_group,
				     GTK_ACTION (action));
	g_object_unref (action);

	gtk_ui_manager_add_ui (window->details->ui_manager,
			       merge_id,
			       placeholder_path,
			       action_name,
			       action_name,
			       GTK_UI_MANAGER_MENUITEM,
			       FALSE);

	return action; /* return value owned by group */
}

/**
 * nautilus_window_sync_view_as_menus:
 * 
 * Set the visible item of the "View as" option menu and
 * the marked "View as" item in the View menu to
 * match the current content view.
 * 
 * @window: The NautilusWindow whose "View as" option menu should be synched.
 */
static void
nautilus_window_sync_view_as_menus (NautilusWindow *window)
{
	NautilusWindowSlot *slot;
	int index;
	char action_name[32];
	GList *node;
	GtkAction *action;

	g_assert (NAUTILUS_IS_WINDOW (window));

	slot = nautilus_window_get_active_slot (window);

	if (slot->content_view == NULL) {
		return;
	}
	for (node = window->details->short_list_viewers, index = 1;
	     node != NULL;
	     node = node->next, ++index) {
		if (nautilus_window_slot_content_view_matches_iid (slot, (char *)node->data)) {
			break;
		}
	}

	g_snprintf (action_name, sizeof (action_name), "view_as_%d", index);
	action = gtk_action_group_get_action (window->details->view_as_action_group,
					      action_name);

	/* Don't trigger the action callback when we're synchronizing */
	g_signal_handlers_block_matched (action,
					 G_SIGNAL_MATCH_FUNC,
					 0, 0,
					 NULL,
					 action_view_as_callback,
					 NULL);
	gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action), TRUE);
	g_signal_handlers_unblock_matched (action,
					   G_SIGNAL_MATCH_FUNC,
					   0, 0,
					   NULL,
					   action_view_as_callback,
					   NULL);
}

static void
refresh_stored_viewers (NautilusWindow *window)
{
	NautilusWindowSlot *slot;
	GList *viewers;
	char *uri, *mimetype;

	slot = nautilus_window_get_active_slot (window);

	uri = nautilus_file_get_uri (slot->viewed_file);
	mimetype = nautilus_file_get_mime_type (slot->viewed_file);
	viewers = nautilus_view_factory_get_views_for_uri (uri,
							   nautilus_file_get_file_type (slot->viewed_file),
							   mimetype);
	g_free (uri);
	g_free (mimetype);

        free_stored_viewers (window);
	window->details->short_list_viewers = viewers;
}

static void
load_view_as_menu (NautilusWindow *window)
{
	GList *node;
	int index;
	guint merge_id;

	if (window->details->short_list_merge_id != 0) {
		gtk_ui_manager_remove_ui (window->details->ui_manager,
					  window->details->short_list_merge_id);
		window->details->short_list_merge_id = 0;
	}
	if (window->details->view_as_action_group != NULL) {
		gtk_ui_manager_remove_action_group (window->details->ui_manager,
						    window->details->view_as_action_group);
		window->details->view_as_action_group = NULL;
	}

	
	refresh_stored_viewers (window);

	merge_id = gtk_ui_manager_new_merge_id (window->details->ui_manager);
	window->details->short_list_merge_id = merge_id;
	window->details->view_as_action_group = gtk_action_group_new ("ViewAsGroup");
	gtk_action_group_set_translation_domain (window->details->view_as_action_group, GETTEXT_PACKAGE);
	window->details->view_as_radio_action = NULL;
	
        /* Add a menu item for each view in the preferred list for this location. */
	/* Start on 1, because extra_viewer gets index 0 */
        for (node = window->details->short_list_viewers, index = 1; 
             node != NULL; 
             node = node->next, ++index) {
		/* Menu item in View menu. */
                add_view_as_menu_item (window, 
				       NAUTILUS_MENU_PATH_SHORT_LIST_PLACEHOLDER, 
				       node->data, 
				       index,
				       merge_id);
        }
	gtk_ui_manager_insert_action_group (window->details->ui_manager,
					    window->details->view_as_action_group,
					    -1);
	g_object_unref (window->details->view_as_action_group); /* owned by ui_manager */

	nautilus_window_sync_view_as_menus (window);
}

static void
load_view_as_menus_callback (NautilusFile *file, 
			    gpointer callback_data)
{
	NautilusWindow *window;
	NautilusWindowSlot *slot;

	slot = callback_data;
	window = nautilus_window_slot_get_window (slot);

	if (slot == nautilus_window_get_active_slot (window)) {
		load_view_as_menu (window);
	}
}

static void
cancel_view_as_callback (NautilusWindowSlot *slot)
{
	nautilus_file_cancel_call_when_ready (slot->viewed_file, 
					      load_view_as_menus_callback,
					      slot);
}

void
nautilus_window_load_view_as_menus (NautilusWindow *window)
{
	NautilusWindowSlot *slot;
	NautilusFileAttributes attributes;

        g_return_if_fail (NAUTILUS_IS_WINDOW (window));

	attributes = nautilus_mime_actions_get_required_file_attributes ();

	slot = nautilus_window_get_active_slot (window);

	cancel_view_as_callback (slot);
	nautilus_file_call_when_ready (slot->viewed_file,
				       attributes, 
				       load_view_as_menus_callback,
				       slot);
}

void
nautilus_window_sync_up_button (NautilusWindow *window)
{
	GtkAction *action;
	GtkActionGroup *action_group;
	NautilusWindowSlot *slot;
	gboolean allowed;
	GFile *parent;

	slot = nautilus_window_get_active_slot (window);

	allowed = FALSE;
	if (slot->location != NULL) {
		parent = g_file_get_parent (slot->location);
		allowed = parent != NULL;

		g_clear_object (&parent);
	}

	action_group = nautilus_window_get_main_action_group (window);

	action = gtk_action_group_get_action (action_group,
					      NAUTILUS_ACTION_UP);
	gtk_action_set_sensitive (action, allowed);
	action = gtk_action_group_get_action (action_group,
					      NAUTILUS_ACTION_UP_ACCEL);
	gtk_action_set_sensitive (action, allowed);
}

void
nautilus_window_sync_title (NautilusWindow *window,
			    NautilusWindowSlot *slot)
{
	NautilusWindowPane *pane;
	NautilusNotebook *notebook;
	char *full_title;
	char *window_title;

	if (NAUTILUS_WINDOW_CLASS (G_OBJECT_GET_CLASS (window))->sync_title != NULL) {
		NAUTILUS_WINDOW_CLASS (G_OBJECT_GET_CLASS (window))->sync_title (window, slot);

		return;
	}

	if (slot == nautilus_window_get_active_slot (window)) {
		/* if spatial mode is default, we keep "File Browser" in the window title
		 * to recognize browser windows. Otherwise, we default to the directory name.
		 */
		if (!g_settings_get_boolean (nautilus_preferences, NAUTILUS_PREFERENCES_ALWAYS_USE_BROWSER)) {
			full_title = g_strdup_printf (_("%s - File Browser"), slot->title);
			window_title = eel_str_middle_truncate (full_title, MAX_TITLE_LENGTH);
			g_free (full_title);
		} else {
			window_title = eel_str_middle_truncate (slot->title, MAX_TITLE_LENGTH);
		}

		gtk_window_set_title (GTK_WINDOW (window), window_title);
		g_free (window_title);
	}

	pane = slot->pane;
	notebook = NAUTILUS_NOTEBOOK (pane->notebook);
	nautilus_notebook_sync_tab_label (notebook, slot);
}

void
nautilus_window_sync_zoom_widgets (NautilusWindow *window)
{
	NautilusWindowSlot *slot;
	NautilusView *view;
	GtkActionGroup *action_group;
	GtkAction *action;
	gboolean supports_zooming;
	gboolean can_zoom, can_zoom_in, can_zoom_out;
	NautilusZoomLevel zoom_level;

	slot = nautilus_window_get_active_slot (window);
	view = slot->content_view;

	if (view != NULL) {
		supports_zooming = nautilus_view_supports_zooming (view);
		zoom_level = nautilus_view_get_zoom_level (view);
		can_zoom = supports_zooming &&
			   zoom_level >= NAUTILUS_ZOOM_LEVEL_SMALLEST &&
			   zoom_level <= NAUTILUS_ZOOM_LEVEL_LARGEST;
		can_zoom_in = can_zoom && nautilus_view_can_zoom_in (view);
		can_zoom_out = can_zoom && nautilus_view_can_zoom_out (view);
	} else {
		supports_zooming = FALSE;
		can_zoom = FALSE;
		can_zoom_in = FALSE;
		can_zoom_out = FALSE;
	}

	action_group = nautilus_window_get_main_action_group (window);

	action = gtk_action_group_get_action (action_group,
					      NAUTILUS_ACTION_ZOOM_IN);
	gtk_action_set_visible (action, supports_zooming);
	gtk_action_set_sensitive (action, can_zoom_in);
	
	action = gtk_action_group_get_action (action_group,
					      NAUTILUS_ACTION_ZOOM_OUT);
	gtk_action_set_visible (action, supports_zooming);
	gtk_action_set_sensitive (action, can_zoom_out);

	action = gtk_action_group_get_action (action_group,
					      NAUTILUS_ACTION_ZOOM_NORMAL);
	gtk_action_set_visible (action, supports_zooming);
	gtk_action_set_sensitive (action, can_zoom);
}

static void
zoom_level_changed_callback (NautilusView *view,
                             NautilusWindow *window)
{
	g_assert (NAUTILUS_IS_WINDOW (window));

	/* This is called each time the component in
	 * the active slot successfully completed
	 * a zooming operation.
	 */
	nautilus_window_sync_zoom_widgets (window);
}


/* These are called
 *   A) when switching the view within the active slot
 *   B) when switching the active slot
 *   C) when closing the active slot (disconnect)
*/
void
nautilus_window_connect_content_view (NautilusWindow *window,
				      NautilusView *view)
{
	NautilusWindowSlot *slot;

	g_assert (NAUTILUS_IS_WINDOW (window));
	g_assert (NAUTILUS_IS_VIEW (view));

	slot = nautilus_window_get_slot_for_view (window, view);

	if (slot != nautilus_window_get_active_slot (window)) {
		return;
	}

	g_signal_connect (view, "zoom-level-changed",
			  G_CALLBACK (zoom_level_changed_callback),
			  window);

      /* Update displayed view in menu. Only do this if we're not switching
       * locations though, because if we are switching locations we'll
       * install a whole new set of views in the menu later (the current
       * views in the menu are for the old location).
       */
	if (slot->pending_location == NULL) {
		nautilus_window_load_view_as_menus (window);
	}

	nautilus_view_grab_focus (view);
}

void
nautilus_window_disconnect_content_view (NautilusWindow *window,
					 NautilusView *view)
{
	NautilusWindowSlot *slot;

	g_assert (NAUTILUS_IS_WINDOW (window));
	g_assert (NAUTILUS_IS_VIEW (view));

	slot = nautilus_window_get_slot_for_view (window, view);

	if (slot != nautilus_window_get_active_slot (window)) {
		return;
	}

	g_signal_handlers_disconnect_by_func (view, G_CALLBACK (zoom_level_changed_callback), window);
}

/**
 * nautilus_window_show:
 * @widget:	GtkWidget
 *
 * Call parent and then show/hide window items
 * base on user prefs.
 */
static void
nautilus_window_show (GtkWidget *widget)
{	
	NautilusWindow *window;

	window = NAUTILUS_WINDOW (widget);

	if (g_settings_get_boolean (nautilus_window_state, NAUTILUS_WINDOW_STATE_START_WITH_SIDEBAR)) {
		nautilus_window_show_sidebar (window);
	} else {
		nautilus_window_hide_sidebar (window);
	}

	GTK_WIDGET_CLASS (nautilus_window_parent_class)->show (widget);	

	gtk_ui_manager_ensure_update (window->details->ui_manager);
}

GtkUIManager *
nautilus_window_get_ui_manager (NautilusWindow *window)
{
	g_return_val_if_fail (NAUTILUS_IS_WINDOW (window), NULL);

	return window->details->ui_manager;
}

GtkActionGroup *
nautilus_window_get_main_action_group (NautilusWindow *window)
{
	g_return_val_if_fail (NAUTILUS_IS_WINDOW (window), NULL);

	return window->details->main_action_group;
}

NautilusNavigationState *
nautilus_window_get_navigation_state (NautilusWindow *window)
{
	g_return_val_if_fail (NAUTILUS_IS_WINDOW (window), NULL);

	return window->details->nav_state;
}

NautilusWindowPane *
nautilus_window_get_next_pane (NautilusWindow *window)
{
       NautilusWindowPane *next_pane;
       GList *node;

       /* return NULL if there is only one pane */
       if (!window->details->panes || !window->details->panes->next) {
	       return NULL;
       }

       /* get next pane in the (wrapped around) list */
       node = g_list_find (window->details->panes, window->details->active_pane);
       g_return_val_if_fail (node, NULL);
       if (node->next) {
	       next_pane = node->next->data;
       } else {
	       next_pane =  window->details->panes->data;
       }

       return next_pane;
}


void
nautilus_window_slot_set_viewed_file (NautilusWindowSlot *slot,
				      NautilusFile *file)
{
	NautilusFileAttributes attributes;

	if (slot->viewed_file == file) {
		return;
	}

	nautilus_file_ref (file);

	cancel_view_as_callback (slot);

	if (slot->viewed_file != NULL) {
		nautilus_file_monitor_remove (slot->viewed_file,
					      slot);
	}

	if (file != NULL) {
		attributes =
			NAUTILUS_FILE_ATTRIBUTE_INFO |
			NAUTILUS_FILE_ATTRIBUTE_LINK_INFO;
		nautilus_file_monitor_add (file, slot, attributes);
	}

	nautilus_file_unref (slot->viewed_file);
	slot->viewed_file = file;
}

NautilusWindowSlot *
nautilus_window_get_slot_for_view (NautilusWindow *window,
				   NautilusView *view)
{
	NautilusWindowSlot *slot;
	GList *l, *walk;

	for (walk = window->details->panes; walk; walk = walk->next) {
		NautilusWindowPane *pane = walk->data;

		for (l = pane->slots; l != NULL; l = l->next) {
			slot = l->data;
			if (slot->content_view == view ||
			    slot->new_content_view == view) {
				return slot;
			}
		}
	}

	return NULL;
}

NautilusWindowShowHiddenFilesMode
nautilus_window_get_hidden_files_mode (NautilusWindow *window)
{
	return window->details->show_hidden_files_mode;
}

void
nautilus_window_set_hidden_files_mode (NautilusWindow *window,
				       NautilusWindowShowHiddenFilesMode  mode)
{
	window->details->show_hidden_files_mode = mode;

	g_signal_emit_by_name (window, "hidden_files_mode_changed");
}

NautilusWindowSlot *
nautilus_window_get_active_slot (NautilusWindow *window)
{
	g_assert (NAUTILUS_IS_WINDOW (window));

	if (window->details->active_pane == NULL) {
		return NULL;
	}

	return window->details->active_pane->active_slot;
}

NautilusWindowSlot *
nautilus_window_get_extra_slot (NautilusWindow *window)
{
	NautilusWindowPane *extra_pane;
	GList *node;

	g_assert (NAUTILUS_IS_WINDOW (window));


	/* return NULL if there is only one pane */
	if (window->details->panes == NULL ||
	    window->details->panes->next == NULL) {
		return NULL;
	}

	/* get next pane in the (wrapped around) list */
	node = g_list_find (window->details->panes,
			    window->details->active_pane);
	g_return_val_if_fail (node, FALSE);

	if (node->next) {
		extra_pane = node->next->data;
	}
	else {
		extra_pane =  window->details->panes->data;
	}

	return extra_pane->active_slot;
}

static void
window_set_search_action_text (NautilusWindow *window,
			       gboolean setting)
{
	GtkAction *action;
	NautilusWindowPane *pane;
	GList *l;

	for (l = window->details->panes; l != NULL; l = l->next) {
		pane = l->data;
		action = gtk_action_group_get_action (pane->action_group,
						      NAUTILUS_ACTION_SEARCH);

		gtk_action_set_is_important (action, setting);
	}
}

static NautilusWindowSlot *
create_extra_pane (NautilusWindow *window)
{
	NautilusWindowPane *pane;
	NautilusWindowSlot *slot;
	GtkPaned *paned;

	/* New pane */
	pane = nautilus_window_pane_new (window);
	window->details->panes = g_list_append (window->details->panes, pane);

	paned = GTK_PANED (window->details->split_view_hpane);
	if (gtk_paned_get_child1 (paned) == NULL) {
		gtk_paned_pack1 (paned, GTK_WIDGET (pane), TRUE, FALSE);
	} else {
		gtk_paned_pack2 (paned, GTK_WIDGET (pane), TRUE, FALSE);
	}

	/* slot */
	slot = nautilus_window_pane_open_slot (NAUTILUS_WINDOW_PANE (pane),
					       NAUTILUS_WINDOW_OPEN_SLOT_APPEND);
	pane->active_slot = slot;

	return slot;
}

static void
nautilus_window_reload (NautilusWindow *window)
{
	NautilusWindowSlot *active_slot;

	active_slot = nautilus_window_get_active_slot (window);
	nautilus_window_slot_reload (active_slot);
}

static gboolean
nautilus_window_state_event (GtkWidget *widget,
			     GdkEventWindowState *event)
{
	if (event->changed_mask & GDK_WINDOW_STATE_MAXIMIZED) {
		g_settings_set_boolean (nautilus_window_state, NAUTILUS_WINDOW_STATE_MAXIMIZED,
					event->new_window_state & GDK_WINDOW_STATE_MAXIMIZED);
	}

	if (GTK_WIDGET_CLASS (nautilus_window_parent_class)->window_state_event != NULL) {
		return GTK_WIDGET_CLASS (nautilus_window_parent_class)->window_state_event (widget, event);
	}

	return FALSE;
}

static gboolean
nautilus_window_delete_event (GtkWidget *widget,
			      GdkEventAny *event)
{
	nautilus_window_close (NAUTILUS_WINDOW (widget));
	return FALSE;
}

static gboolean
nautilus_window_button_press_event (GtkWidget *widget,
				    GdkEventButton *event)
{
	NautilusWindow *window;
	gboolean handled;

	window = NAUTILUS_WINDOW (widget);

	if (mouse_extra_buttons && (event->button == mouse_back_button)) {
		nautilus_window_back_or_forward (window, TRUE, 0, 0);
		handled = TRUE; 
	} else if (mouse_extra_buttons && (event->button == mouse_forward_button)) {
		nautilus_window_back_or_forward (window, FALSE, 0, 0);
		handled = TRUE;
	} else if (GTK_WIDGET_CLASS (nautilus_window_parent_class)->button_press_event) {
		handled = GTK_WIDGET_CLASS (nautilus_window_parent_class)->button_press_event (widget, event);
	} else {
		handled = FALSE;
	}
	return handled;
}

static void
mouse_back_button_changed (gpointer callback_data)
{
	int new_back_button;

	new_back_button = g_settings_get_int (nautilus_preferences, NAUTILUS_PREFERENCES_MOUSE_BACK_BUTTON);

	/* Bounds checking */
	if (new_back_button < 6 || new_back_button > UPPER_MOUSE_LIMIT)
		return;

	mouse_back_button = new_back_button;
}

static void
mouse_forward_button_changed (gpointer callback_data)
{
	int new_forward_button;

	new_forward_button = g_settings_get_int (nautilus_preferences, NAUTILUS_PREFERENCES_MOUSE_FORWARD_BUTTON);

	/* Bounds checking */
	if (new_forward_button < 6 || new_forward_button > UPPER_MOUSE_LIMIT)
		return;

	mouse_forward_button = new_forward_button;
}

static void
use_extra_mouse_buttons_changed (gpointer callback_data)
{
	mouse_extra_buttons = g_settings_get_boolean (nautilus_preferences, NAUTILUS_PREFERENCES_MOUSE_USE_EXTRA_BUTTONS);
}


/*
 * Main API
 */

static void
nautilus_window_init (NautilusWindow *window)
{
	window->details = G_TYPE_INSTANCE_GET_PRIVATE (window, NAUTILUS_TYPE_WINDOW, NautilusWindowDetails);

	window->details->panes = NULL;
	window->details->active_pane = NULL;

	window->details->show_hidden_files_mode = NAUTILUS_WINDOW_SHOW_HIDDEN_FILES_DEFAULT;

	/* Set initial window title */
	gtk_window_set_title (GTK_WINDOW (window), _("Files"));
}

static NautilusIconInfo *
real_get_icon (NautilusWindow *window,
               NautilusWindowSlot *slot)
{
        return nautilus_file_get_icon (slot->viewed_file, 48,
				       NAUTILUS_FILE_ICON_FLAGS_IGNORE_VISITING |
				       NAUTILUS_FILE_ICON_FLAGS_USE_MOUNT_ICON);
}

static void
real_window_close (NautilusWindow *window)
{
	g_return_if_fail (NAUTILUS_IS_WINDOW (window));

	nautilus_window_save_geometry (window);

	gtk_widget_destroy (GTK_WIDGET (window));
}

static void
nautilus_window_class_init (NautilusWindowClass *class)
{
	GtkBindingSet *binding_set;
	GObjectClass *oclass = G_OBJECT_CLASS (class);
	GtkWidgetClass *wclass = GTK_WIDGET_CLASS (class);

	oclass->finalize = nautilus_window_finalize;
	oclass->constructed = nautilus_window_constructed;
	oclass->get_property = nautilus_window_get_property;
	oclass->set_property = nautilus_window_set_property;

	wclass->destroy = nautilus_window_destroy;
	wclass->show = nautilus_window_show;
	wclass->get_preferred_width = nautilus_window_get_preferred_width;
	wclass->get_preferred_height = nautilus_window_get_preferred_height;
	wclass->realize = nautilus_window_realize;
	wclass->key_press_event = nautilus_window_key_press_event;
	wclass->window_state_event = nautilus_window_state_event;
	wclass->button_press_event = nautilus_window_button_press_event;
	wclass->delete_event = nautilus_window_delete_event;

	class->get_icon = real_get_icon;
	class->close = real_window_close;

	properties[PROP_DISABLE_CHROME] =
		g_param_spec_boolean ("disable-chrome",
				      "Disable chrome",
				      "Disable window chrome, for the desktop",
				      FALSE,
				      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
				      G_PARAM_STATIC_STRINGS);

	signals[GO_UP] =
		g_signal_new ("go-up",
			      G_TYPE_FROM_CLASS (class),
			      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
			      G_STRUCT_OFFSET (NautilusWindowClass, go_up),
			      NULL, NULL,
			      g_cclosure_marshal_generic,
			      G_TYPE_NONE, 0);
	signals[RELOAD] =
		g_signal_new ("reload",
			      G_TYPE_FROM_CLASS (class),
			      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
			      G_STRUCT_OFFSET (NautilusWindowClass, reload),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	signals[PROMPT_FOR_LOCATION] =
		g_signal_new ("prompt-for-location",
			      G_TYPE_FROM_CLASS (class),
			      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
			      G_STRUCT_OFFSET (NautilusWindowClass, prompt_for_location),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE, 1, G_TYPE_STRING);
	signals[HIDDEN_FILES_MODE_CHANGED] =
		g_signal_new ("hidden_files_mode_changed",
			      G_TYPE_FROM_CLASS (class),
			      G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	signals[LOADING_URI] =
		g_signal_new ("loading_uri",
			      G_TYPE_FROM_CLASS (class),
			      G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL,
			      g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE, 1,
			      G_TYPE_STRING);

	binding_set = gtk_binding_set_by_class (class);
	gtk_binding_entry_add_signal (binding_set, GDK_KEY_BackSpace, 0,
				      "go-up", 0);
	gtk_binding_entry_add_signal (binding_set, GDK_KEY_F5, 0,
				      "reload", 0);
	gtk_binding_entry_add_signal (binding_set, GDK_KEY_slash, 0,
				      "prompt-for-location", 1,
				      G_TYPE_STRING, "/");

	class->reload = nautilus_window_reload;
	class->go_up = nautilus_window_go_up_signal;
	class->prompt_for_location = nautilus_window_prompt_for_location;

	g_signal_connect_swapped (nautilus_preferences,
				  "changed::" NAUTILUS_PREFERENCES_MOUSE_BACK_BUTTON,
				  G_CALLBACK(mouse_back_button_changed),
				  NULL);

	g_signal_connect_swapped (nautilus_preferences,
				  "changed::" NAUTILUS_PREFERENCES_MOUSE_FORWARD_BUTTON,
				  G_CALLBACK(mouse_forward_button_changed),
				  NULL);

	g_signal_connect_swapped (nautilus_preferences,
				  "changed::" NAUTILUS_PREFERENCES_MOUSE_USE_EXTRA_BUTTONS,
				  G_CALLBACK(use_extra_mouse_buttons_changed),
				  NULL);

	g_object_class_install_properties (oclass, NUM_PROPERTIES, properties);
	g_type_class_add_private (oclass, sizeof (NautilusWindowDetails));
}

NautilusWindow *
nautilus_window_new (GtkApplication *application,
		     GdkScreen      *screen)
{
	return g_object_new (NAUTILUS_TYPE_WINDOW,
			     "application", application,
			     "screen", screen,
			     NULL);
}

void
nautilus_window_split_view_on (NautilusWindow *window)
{
	NautilusWindowSlot *slot, *old_active_slot;
	GFile *location;

	old_active_slot = nautilus_window_get_active_slot (window);
	slot = create_extra_pane (window);

	location = NULL;
	if (old_active_slot != NULL) {
		location = nautilus_window_slot_get_location (old_active_slot);
		if (location != NULL) {
			if (g_file_has_uri_scheme (location, "x-nautilus-search")) {
				g_object_unref (location);
				location = NULL;
			}
		}
	}
	if (location == NULL) {
		location = g_file_new_for_path (g_get_home_dir ());
	}

	nautilus_window_slot_open_location (slot, location, 0);
	g_object_unref (location);

	window_set_search_action_text (window, FALSE);
}

void
nautilus_window_split_view_off (NautilusWindow *window)
{
	NautilusWindowPane *pane, *active_pane;
	GList *l, *next;

	active_pane = nautilus_window_get_active_pane (window);

	/* delete all panes except the first (main) pane */
	for (l = window->details->panes; l != NULL; l = next) {
		next = l->next;
		pane = l->data;
		if (pane != active_pane) {
			nautilus_window_close_pane (window, pane);
		}
	}

	nautilus_window_set_active_pane (window, active_pane);
	nautilus_navigation_state_set_master (window->details->nav_state,
					      active_pane->action_group);

	nautilus_window_update_show_hide_menu_items (window);
	window_set_search_action_text (window, TRUE);
}

gboolean
nautilus_window_split_view_showing (NautilusWindow *window)
{
	return g_list_length (NAUTILUS_WINDOW (window)->details->panes) > 1;
}

NautilusWindowOpenFlags
nautilus_event_get_window_open_flags (void)
{
	NautilusWindowOpenFlags flags = 0;
	GdkEvent *event;

	event = gtk_get_current_event ();

	if (event == NULL) {
		return flags;
	}

	if ((event->type == GDK_BUTTON_PRESS || event->type == GDK_BUTTON_RELEASE) &&
	    (event->button.button == 2)) {
		flags |= NAUTILUS_WINDOW_OPEN_FLAG_NEW_TAB;
	}

	gdk_event_free (event);

	return flags;
}

void
nautilus_window_show_about_dialog (NautilusWindow *window)
{
	const gchar *authors[] = {
		"Alexander Larsson",
		"Ali Abdin",
		"Anders Carlsson",
		"Andrew Walton",
		"Andy Hertzfeld",
		"Arlo Rose",
		"Christian Neumair",
		"Cosimo Cecchi",
		"Darin Adler",
		"David Camp",
		"Eli Goldberg",
		"Elliot Lee",
		"Eskil Heyn Olsen",
		"Ettore Perazzoli",
		"Gene Z. Ragan",
		"George Lebl",
		"Ian McKellar",
		"J Shane Culpepper",
		"James Willcox",
		"Jan Arne Petersen",
		"John Harper",
		"John Sullivan",
		"Josh Barrow",
		"Maciej Stachowiak",
		"Mark McLoughlin",
		"Mathieu Lacage",
		"Mike Engber",
		"Mike Fleming",
		"Pavel Cisler",
		"Ramiro Estrugo",
		"Raph Levien",
		"Rebecca Schulman",
		"Robey Pointer",
		"Robin * Slomkowski",
		"Seth Nickell",
		"Susan Kare",
		"Tomas Bzatek",
		"William Jon McCann",
		NULL
	};
	const gchar *documenters[] = {
		"GNOME Documentation Team",
		"Sun Microsystems",
		NULL
	};
	const gchar *license[] = {
		N_("Files is free software; you can redistribute it and/or modify "
		   "it under the terms of the GNU General Public License as published by "
		   "the Free Software Foundation; either version 2 of the License, or "
		   "(at your option) any later version."),
		N_("Files is distributed in the hope that it will be useful, "
		   "but WITHOUT ANY WARRANTY; without even the implied warranty of "
		   "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the "
		   "GNU General Public License for more details."),
		N_("You should have received a copy of the GNU General Public License "
		   "along with Nautilus; if not, write to the Free Software Foundation, Inc., "
		   "51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA")
	};
	gchar *license_trans, *copyright_str;
	GDateTime *date;

	license_trans = g_strjoin ("\n\n", _(license[0]), _(license[1]),
					     _(license[2]), NULL);

	date = g_date_time_new_now_local ();

	/* Translators: these two strings here indicate the copyright time span,
	 * e.g. 1999-2011.
	 */
	copyright_str = g_strdup_printf (_("Copyright \xC2\xA9 %Id\xE2\x80\x93%Id "
					   "The Files authors"), 1999, g_date_time_get_year (date));

	gtk_show_about_dialog (window ? GTK_WINDOW (window) : NULL,
			       "program-name", _("Files"),
			       "version", VERSION,
			       "comments", _("Access and organize your files."),
			       "copyright", copyright_str,
			       "license", license_trans,
			       "wrap-license", TRUE,
			       "authors", authors,
			       "documenters", documenters,
				/* Translators should localize the following string
				 * which will be displayed at the bottom of the about
				 * box to give credit to the translator(s).
				 */
			      "translator-credits", _("translator-credits"),
			      "logo-icon-name", "system-file-manager",
			      NULL);

	g_free (license_trans);
	g_free (copyright_str);
	g_date_time_unref (date);
}
