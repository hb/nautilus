/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  Nautilus
 *
 *  Copyright (C) 1999, 2000 Red Hat, Inc.
 *  Copyright (C) 1999, 2000, 2001 Eazel, Inc.
 *  Copyright (C) 2003 Ximian, Inc.
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
#include "nautilus-window-private.h"

#include "nautilus-actions.h"
#include "nautilus-application.h"
#include "nautilus-bookmarks-window.h"
#include "nautilus-main.h"
#include "nautilus-location-bar.h"
#include "nautilus-query-editor.h"
#include "nautilus-search-bar.h"
#include "nautilus-navigation-window-slot.h"
#include "nautilus-notebook.h"
#include "nautilus-window-manage-views.h"
#include "nautilus-zoom-control.h"
#include "nautilus-navigation-window-pane.h"
#include "nautilus-border.h"
#include "file-manager/fm-list-view.h"
#include <eel/eel-gtk-extensions.h>
#include <eel/eel-gtk-macros.h>
#include <eel/eel-string.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>
#ifdef HAVE_X11_XF86KEYSYM_H
#include <X11/XF86keysym.h>
#endif
#include <libnautilus-private/nautilus-file-utilities.h>
#include <libnautilus-private/nautilus-file-attributes.h>
#include <libnautilus-private/nautilus-global-preferences.h>
#include <libnautilus-private/nautilus-horizontal-splitter.h>
#include <libnautilus-private/nautilus-icon-info.h>
#include <libnautilus-private/nautilus-metadata.h>
#include <libnautilus-private/nautilus-mime-actions.h>
#include <libnautilus-private/nautilus-program-choosing.h>
#include <libnautilus-private/nautilus-sidebar.h>
#include <libnautilus-private/nautilus-view-factory.h>
#include <libnautilus-private/nautilus-clipboard.h>
#include <libnautilus-private/nautilus-undo.h>
#include <libnautilus-private/nautilus-module.h>
#include <libnautilus-private/nautilus-sidebar-provider.h>
#include <libnautilus-private/nautilus-search-directory.h>
#include <libnautilus-private/nautilus-signaller.h>
#include <math.h>
#include <sys/time.h>


/* FIXME bugzilla.gnome.org 41243: 
 * We should use inheritance instead of these special cases
 * for the desktop window.
 */
#include "nautilus-desktop-window.h"

#define MAX_TITLE_LENGTH 180

#define MENU_PATH_BOOKMARKS_PLACEHOLDER			"/MenuBar/Other Menus/Bookmarks/Bookmarks Placeholder"

enum {
	ARG_0,
	ARG_APP_ID,
	ARG_APP
};

static int side_pane_width_auto_value = 0;


/* Forward and back buttons on the mouse */
static gboolean mouse_extra_buttons = TRUE;
static int mouse_forward_button = 8;
static int mouse_back_button = 9;

static void add_sidebar_panels                       (NautilusNavigationWindow *window);
static void side_panel_image_changed_callback        (NautilusSidebar          *side_panel,
						      gpointer                  callback_data);
static void always_use_location_entry_changed        (gpointer                  callback_data);
static void always_use_browser_changed               (gpointer                  callback_data);
static void enable_tabs_changed			     (gpointer                  callback_data);
static void mouse_back_button_changed		     (gpointer                  callback_data);
static void mouse_forward_button_changed	     (gpointer                  callback_data);
static void use_extra_mouse_buttons_changed          (gpointer                  callback_data);


G_DEFINE_TYPE (NautilusNavigationWindow, nautilus_navigation_window, NAUTILUS_TYPE_WINDOW)
#define parent_class nautilus_navigation_window_parent_class

static const struct {
	unsigned int keyval;
	const char *action;
} extra_navigation_window_keybindings [] = {
#ifdef HAVE_X11_XF86KEYSYM_H
	{ XF86XK_Back,		NAUTILUS_ACTION_BACK },
	{ XF86XK_Forward,	NAUTILUS_ACTION_FORWARD }
#endif
};

gboolean
nautilus_navigation_window_hide_temporary_bars (NautilusNavigationWindow *window)
{
	gboolean any = TRUE;
	GList *walk;
	for (walk = NAUTILUS_WINDOW(window)->details->panes; walk; walk = walk->next) {
		if(!nautilus_navigation_window_pane_hide_temporary_bars (walk->data)) {
			any = FALSE;
		}
	}
	return any;
}

static void
nautilus_navigation_window_init (NautilusNavigationWindow *window)
{
	GtkUIManager *ui_manager;
	GtkWidget *toolbar;
	NautilusWindow *win;
	NautilusNavigationWindowPane *pane;

	win = NAUTILUS_WINDOW(window);
	
	window->details = G_TYPE_INSTANCE_GET_PRIVATE (window, NAUTILUS_TYPE_NAVIGATION_WINDOW, NautilusNavigationWindowDetails);


    pane = nautilus_navigation_window_pane_new (win);

	window->details->content_paned = nautilus_horizontal_splitter_new ();
	gtk_table_attach (GTK_TABLE (NAUTILUS_WINDOW (window)->details->table),
			  window->details->content_paned,
			  /* X direction */                   /* Y direction */
			  0, 1,                               3, 4,
			  GTK_EXPAND | GTK_FILL | GTK_SHRINK, GTK_EXPAND | GTK_FILL | GTK_SHRINK,
			  0,                                  0);
	gtk_widget_show (window->details->content_paned);

    nautilus_navigation_window_pane_setup_notebook (pane);
	
    nautilus_horizontal_splitter_pack2 (
		NAUTILUS_HORIZONTAL_SPLITTER (window->details->content_paned),
		pane->notebook);

    nautilus_navigation_window_pane_setup_location_bar(pane);
    gtk_widget_show (pane->location_bar);

	gtk_table_attach (GTK_TABLE (NAUTILUS_WINDOW (window)->details->table),
			  pane->location_bar,
			  /* X direction */                    /* Y direction */
			  0, 1,                                2, 3,
			  GTK_EXPAND | GTK_FILL | GTK_SHRINK,  0,
			  0,                                   0);

	/* this has to be done after the location bar has been set up,
	 * but before menu stuff is being called */
    nautilus_window_set_active_pane (win, NAUTILUS_WINDOW_PANE (pane));

	nautilus_navigation_window_initialize_actions (window);
	nautilus_navigation_window_initialize_menus (window);
	
	ui_manager = nautilus_window_get_ui_manager (NAUTILUS_WINDOW (window));
	toolbar = gtk_ui_manager_get_widget (ui_manager, "/Toolbar");
	window->details->toolbar = toolbar;
	gtk_table_attach (GTK_TABLE (NAUTILUS_WINDOW (window)->details->table),
			  toolbar,
			  /* X direction */                   /* Y direction */
			  0, 1,                               1, 2,
			  GTK_EXPAND | GTK_FILL | GTK_SHRINK, 0,
			  0,                                  0);
	gtk_widget_show (toolbar);

	nautilus_navigation_window_initialize_toolbars (window);

	/* Set initial sensitivity of some buttons & menu items 
	 * now that they're all created.
	 */
	nautilus_navigation_window_allow_back (window, FALSE);
	nautilus_navigation_window_allow_forward (window, FALSE);

	eel_preferences_add_callback_while_alive (NAUTILUS_PREFERENCES_ALWAYS_USE_LOCATION_ENTRY,
						  always_use_location_entry_changed,
						  window, G_OBJECT (window));

	eel_preferences_add_callback_while_alive (NAUTILUS_PREFERENCES_ALWAYS_USE_BROWSER,
						  always_use_browser_changed,
						  window, G_OBJECT (window));

	eel_preferences_add_callback_while_alive (NAUTILUS_PREFERENCES_ENABLE_TABS,
						  enable_tabs_changed,
						  window, G_OBJECT (window));
}

static void
always_use_location_entry_changed (gpointer callback_data)
{
	NautilusNavigationWindow *window;
    GList *walk;
	gboolean use_entry;

	window = NAUTILUS_NAVIGATION_WINDOW (callback_data);

	use_entry = eel_preferences_get_boolean (NAUTILUS_PREFERENCES_ALWAYS_USE_LOCATION_ENTRY);

	for (walk = NAUTILUS_WINDOW(window)->details->panes; walk; walk = walk->next) {
		nautilus_navigation_window_pane_always_use_location_entry (walk->data, use_entry);
	}
}

static void
always_use_browser_changed (gpointer callback_data)
{
	NautilusNavigationWindow *window;

	window = NAUTILUS_NAVIGATION_WINDOW (callback_data);

	nautilus_navigation_window_update_spatial_menu_item (window);
}

static void
enable_tabs_changed (gpointer callback_data)
{
	NautilusNavigationWindow *window;

	window = NAUTILUS_NAVIGATION_WINDOW (callback_data);

	nautilus_navigation_window_update_tab_menu_item_visibility (window);
}

/* Sanity check: highest mouse button value I could find was 14. 5 is our 
 * lower threshold (well-documented to be the one of the button events for the 
 * scrollwheel), so it's hardcoded in the functions below. However, if you have
 * a button that registers higher and want to map it, file a bug and 
 * we'll move the bar. Makes you wonder why the X guys don't have 
 * defined values for these like the XKB stuff, huh?
 */
#define UPPER_MOUSE_LIMIT 14

static void
mouse_back_button_changed (gpointer callback_data)
{
	int new_back_button;	

	new_back_button = eel_preferences_get_integer (NAUTILUS_PREFERENCES_MOUSE_BACK_BUTTON);

	/* Bounds checking */
	if (new_back_button < 6 || new_back_button > UPPER_MOUSE_LIMIT)
		return;

	mouse_back_button = new_back_button;
}

static void
mouse_forward_button_changed (gpointer callback_data)
{
	int new_forward_button;	

	new_forward_button = eel_preferences_get_integer (NAUTILUS_PREFERENCES_MOUSE_FORWARD_BUTTON);

	/* Bounds checking */
	if (new_forward_button < 6 || new_forward_button > UPPER_MOUSE_LIMIT)
		return;

	mouse_forward_button = new_forward_button;
}

static void
use_extra_mouse_buttons_changed (gpointer callback_data)
{
	mouse_extra_buttons = eel_preferences_get_boolean (NAUTILUS_PREFERENCES_MOUSE_USE_EXTRA_BUTTONS);
}

void
nautilus_navigation_window_unset_focus_widget (NautilusNavigationWindow *window)
{
	if (window->details->last_focus_widget != NULL) {
		g_object_remove_weak_pointer (G_OBJECT (window->details->last_focus_widget),
					      (gpointer *) &window->details->last_focus_widget);
		window->details->last_focus_widget = NULL;
	}
}

static inline gboolean
is_in_temporary_navigation_bar (GtkWidget *widget,
				NautilusNavigationWindow *window)
{
	GList *walk;
	gboolean is_in_any = FALSE;

	for (walk = NAUTILUS_WINDOW(window)->details->panes; walk; walk = walk->next) {
		NautilusNavigationWindowPane *pane = walk->data;
		if(gtk_widget_get_ancestor (widget, NAUTILUS_TYPE_NAVIGATION_BAR) != NULL &&
			       pane->temporary_navigation_bar)
			is_in_any = TRUE;
	}
	return is_in_any;
}

static inline gboolean
is_in_temporary_search_bar (GtkWidget *widget,
			    NautilusNavigationWindow *window)
{
	GList *walk;
	gboolean is_in_any = FALSE;

	for (walk = NAUTILUS_WINDOW(window)->details->panes; walk; walk = walk->next) {
		NautilusNavigationWindowPane *pane = walk->data;
		if(gtk_widget_get_ancestor (widget, NAUTILUS_TYPE_SEARCH_BAR) != NULL &&
				       pane->temporary_search_bar)
			is_in_any = TRUE;
	}
	return is_in_any;
}

static void
remember_focus_widget (NautilusNavigationWindow *window)
{
	NautilusNavigationWindow *navigation_window;
	GtkWidget *focus_widget;

	navigation_window = NAUTILUS_NAVIGATION_WINDOW (window);

	focus_widget = gtk_window_get_focus (GTK_WINDOW (window));
	if (focus_widget != NULL &&
	    !is_in_temporary_navigation_bar (focus_widget, navigation_window) &&
	    !is_in_temporary_search_bar (focus_widget, navigation_window)) {
		nautilus_navigation_window_unset_focus_widget (navigation_window);

		navigation_window->details->last_focus_widget = focus_widget;
		g_object_add_weak_pointer (G_OBJECT (focus_widget),
					   (gpointer *) &(NAUTILUS_NAVIGATION_WINDOW (window)->details->last_focus_widget));
	}
}

static void
side_pane_close_requested_callback (GtkWidget *widget,
				    gpointer user_data)
{
	NautilusNavigationWindow *window;
	
	window = NAUTILUS_NAVIGATION_WINDOW (user_data);

	nautilus_navigation_window_hide_sidebar (window);
}

static void
side_pane_size_allocate_callback (GtkWidget *widget,
				  GtkAllocation *allocation,
				  gpointer user_data)
{
	NautilusNavigationWindow *window;
	
	window = NAUTILUS_NAVIGATION_WINDOW (user_data);
	
	if (allocation->width != window->details->side_pane_width) {
		window->details->side_pane_width = allocation->width;
		if (eel_preferences_key_is_writable (NAUTILUS_PREFERENCES_SIDEBAR_WIDTH)) {
			eel_preferences_set_integer
				(NAUTILUS_PREFERENCES_SIDEBAR_WIDTH, 
				 allocation->width <= 1 ? 0 : allocation->width);
		}
	}
}

static void
setup_side_pane_width (NautilusNavigationWindow *window)
{
	static gboolean setup_auto_value= TRUE;

	g_return_if_fail (window->sidebar != NULL);
	
	if (setup_auto_value) {
		setup_auto_value = FALSE;
		eel_preferences_add_auto_integer 
			(NAUTILUS_PREFERENCES_SIDEBAR_WIDTH,
			 &side_pane_width_auto_value);
	}

	window->details->side_pane_width = side_pane_width_auto_value;

	/* FIXME bugzilla.gnome.org 41245: Saved in pixels instead of in %? */
        /* FIXME bugzilla.gnome.org 41245: No reality check on the value? */
	
	gtk_paned_set_position (GTK_PANED (window->details->content_paned), 
				side_pane_width_auto_value);
}

static void
set_current_side_panel (NautilusNavigationWindow *window,
			NautilusSidebar *panel)
{
	if (window->details->current_side_panel) {
		nautilus_sidebar_is_visible_changed (window->details->current_side_panel,
						     FALSE);
		eel_remove_weak_pointer (&window->details->current_side_panel);
	}

	if (panel != NULL) {
		nautilus_sidebar_is_visible_changed (panel, TRUE);
	}
	window->details->current_side_panel = panel;
	eel_add_weak_pointer (&window->details->current_side_panel);
}

static void
side_pane_switch_page_callback (NautilusSidePane *side_pane,
				GtkWidget *widget,
				NautilusNavigationWindow *window)
{
	const char *id;
	NautilusSidebar *sidebar;

	sidebar = NAUTILUS_SIDEBAR (widget);

	if (sidebar == NULL) {
		return;
	}
		
	set_current_side_panel (window, sidebar);

	id = nautilus_sidebar_get_sidebar_id (sidebar);
	if (eel_preferences_key_is_writable (NAUTILUS_PREFERENCES_SIDE_PANE_VIEW)) {
		eel_preferences_set (NAUTILUS_PREFERENCES_SIDE_PANE_VIEW, id);
	}
}

static void
nautilus_navigation_window_set_up_sidebar (NautilusNavigationWindow *window)
{
	window->sidebar = nautilus_side_pane_new ();

	gtk_paned_pack1 (GTK_PANED (window->details->content_paned),
			 GTK_WIDGET (window->sidebar),
			 FALSE, TRUE);

	setup_side_pane_width (window);
	g_signal_connect (window->sidebar, 
			  "size_allocate",
			  G_CALLBACK (side_pane_size_allocate_callback),
			  window);
	
	add_sidebar_panels (window);

	g_signal_connect (window->sidebar,
			  "close_requested",
			  G_CALLBACK (side_pane_close_requested_callback),
			  window);

	g_signal_connect (window->sidebar,
			  "switch_page",
			  G_CALLBACK (side_pane_switch_page_callback),
			  window);
	
	gtk_widget_show (GTK_WIDGET (window->sidebar));
}

static void
nautilus_navigation_window_tear_down_sidebar (NautilusNavigationWindow *window)
{
	GList *node, *next;
	NautilusSidebar *sidebar_panel;
	
	g_signal_handlers_disconnect_by_func (window->sidebar,
					      side_pane_switch_page_callback,
					      window);

	for (node = window->sidebar_panels; node != NULL; node = next) {
		next = node->next;

		sidebar_panel = NAUTILUS_SIDEBAR (node->data);
		
		nautilus_navigation_window_remove_sidebar_panel (window,
								 sidebar_panel);
        }

	gtk_widget_destroy (GTK_WIDGET (window->sidebar));
	window->sidebar = NULL;
}

static void
nautilus_navigation_window_unrealize (GtkWidget *widget)
{
	NautilusNavigationWindow *window;
	
	window = NAUTILUS_NAVIGATION_WINDOW (widget);

	GTK_WIDGET_CLASS (parent_class)->unrealize (widget);
}

static gboolean
nautilus_navigation_window_state_event (GtkWidget *widget,
					GdkEventWindowState *event)
{
	if (event->changed_mask & GDK_WINDOW_STATE_MAXIMIZED) {
		eel_preferences_set_boolean (NAUTILUS_PREFERENCES_NAVIGATION_WINDOW_MAXIMIZED,
					     event->new_window_state & GDK_WINDOW_STATE_MAXIMIZED);
	}

	if (GTK_WIDGET_CLASS (parent_class)->window_state_event != NULL) {
		return GTK_WIDGET_CLASS (parent_class)->window_state_event (widget, event);
	}

	return FALSE;
}

static gboolean
nautilus_navigation_window_key_press_event (GtkWidget *widget,
					    GdkEventKey *event)
{
	NautilusNavigationWindow *window;
	int i;

	window = NAUTILUS_NAVIGATION_WINDOW (widget);

	for (i = 0; i < G_N_ELEMENTS (extra_navigation_window_keybindings); i++) {
		if (extra_navigation_window_keybindings[i].keyval == event->keyval) {
			GtkAction *action;

			action = gtk_action_group_get_action (window->details->navigation_action_group,
							      extra_navigation_window_keybindings[i].action);

			g_assert (action != NULL);
			if (gtk_action_is_sensitive (action)) {
				gtk_action_activate (action);
				return TRUE;
			}

			break;
		}
	}

	return GTK_WIDGET_CLASS (nautilus_navigation_window_parent_class)->key_press_event (widget, event);
}

static gboolean
nautilus_navigation_window_button_press_event (GtkWidget *widget,
					       GdkEventButton *event)
{
	NautilusNavigationWindow *window;
	gboolean handled;
	
	handled = FALSE;
	window = NAUTILUS_NAVIGATION_WINDOW (widget);
	
	if (mouse_extra_buttons && (event->button == mouse_back_button)) {
		nautilus_navigation_window_go_back (window);
		handled = TRUE; 
	} else if (mouse_extra_buttons && (event->button == mouse_forward_button)) {
		nautilus_navigation_window_go_forward (window);
		handled = TRUE;
	} else {
		handled = GTK_WIDGET_CLASS (nautilus_navigation_window_parent_class)->button_press_event (widget, event);
	}
	return handled;
}

static void
nautilus_navigation_window_destroy (GtkObject *object)
{
	NautilusNavigationWindow *window;
	
	window = NAUTILUS_NAVIGATION_WINDOW (object);

	nautilus_navigation_window_unset_focus_widget (window);

	window->sidebar = NULL;
	g_list_foreach (window->sidebar_panels, (GFunc)g_object_unref, NULL);
	g_list_free (window->sidebar_panels);
	window->sidebar_panels = NULL;

	window->details->content_paned = NULL;
	window->details->split_view_hpane = NULL;

	GTK_OBJECT_CLASS (parent_class)->destroy (object);
}

static void
nautilus_navigation_window_finalize (GObject *object)
{
	NautilusNavigationWindow *window;
	
	window = NAUTILUS_NAVIGATION_WINDOW (object);

	nautilus_navigation_window_remove_go_menu_callback (window);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

/*
 * Main API
 */

void
nautilus_navigation_window_add_sidebar_panel (NautilusNavigationWindow *window,
					      NautilusSidebar *sidebar_panel)
{
	const char *sidebar_id;
	char *label;
	char *tooltip;
	char *default_id;
	GdkPixbuf *icon;

	g_return_if_fail (NAUTILUS_IS_NAVIGATION_WINDOW (window));
	g_return_if_fail (NAUTILUS_IS_SIDEBAR (sidebar_panel));
	g_return_if_fail (NAUTILUS_IS_SIDE_PANE (window->sidebar));
	g_return_if_fail (g_list_find (window->sidebar_panels, sidebar_panel) == NULL);	

	label = nautilus_sidebar_get_tab_label (sidebar_panel);
	tooltip = nautilus_sidebar_get_tab_tooltip (sidebar_panel);
	nautilus_side_pane_add_panel (window->sidebar, 
				      GTK_WIDGET (sidebar_panel), 
				      label,
				      tooltip);
	g_free (tooltip);
	g_free (label);

	icon = nautilus_sidebar_get_tab_icon (sidebar_panel);
	nautilus_side_pane_set_panel_image (NAUTILUS_NAVIGATION_WINDOW (window)->sidebar,
					    GTK_WIDGET (sidebar_panel),
					    icon);
	if (icon) {
		g_object_unref (icon);
	}

	g_signal_connect (sidebar_panel, "tab_icon_changed",
			  (GCallback)side_panel_image_changed_callback, window);

	window->sidebar_panels = g_list_prepend (window->sidebar_panels,
						 g_object_ref (sidebar_panel));

	/* Show if default */
	sidebar_id = nautilus_sidebar_get_sidebar_id (sidebar_panel);
	default_id = eel_preferences_get (NAUTILUS_PREFERENCES_SIDE_PANE_VIEW);
	if (sidebar_id && default_id && !strcmp (sidebar_id, default_id)) {
		nautilus_side_pane_show_panel (window->sidebar,
					       GTK_WIDGET (sidebar_panel));
	}	
	g_free (default_id);
}

void
nautilus_navigation_window_remove_sidebar_panel (NautilusNavigationWindow *window,
						 NautilusSidebar *sidebar_panel)
{
	g_return_if_fail (NAUTILUS_IS_NAVIGATION_WINDOW (window));
	g_return_if_fail (NAUTILUS_IS_SIDEBAR (sidebar_panel));

	if (g_list_find (window->sidebar_panels, sidebar_panel) == NULL) {
		return;
	}

	g_signal_handlers_disconnect_by_func (sidebar_panel, side_panel_image_changed_callback, window);
	
	nautilus_side_pane_remove_panel (window->sidebar, 
					 GTK_WIDGET (sidebar_panel));
	window->sidebar_panels = g_list_remove (window->sidebar_panels, sidebar_panel);
	g_object_unref (sidebar_panel);
}

void
nautilus_navigation_window_go_back (NautilusNavigationWindow *window)
{
	nautilus_navigation_window_back_or_forward (window, TRUE, 0, FALSE);
}

void
nautilus_navigation_window_go_forward (NautilusNavigationWindow *window)
{
	nautilus_navigation_window_back_or_forward (window, FALSE, 0, FALSE);
}

void
nautilus_navigation_window_allow_back (NautilusNavigationWindow *window, gboolean allow)
{
	GtkAction *action;

	action = gtk_action_group_get_action (window->details->navigation_action_group,
					      NAUTILUS_ACTION_BACK);
	
	gtk_action_set_sensitive (action, allow);
}

void
nautilus_navigation_window_allow_forward (NautilusNavigationWindow *window, gboolean allow)
{
	GtkAction *action;

	action = gtk_action_group_get_action (window->details->navigation_action_group,
					      NAUTILUS_ACTION_FORWARD);
	
	gtk_action_set_sensitive (action, allow);
}

static void
real_load_view_as_menu (NautilusWindow *window)
{
    GList *walk;

	EEL_CALL_PARENT (NAUTILUS_WINDOW_CLASS,
			 load_view_as_menu, (window));

	for (walk = window->details->panes; walk; walk = walk->next) {
		nautilus_navigation_window_pane_load_view_as_menu (walk->data);
	}    
}

static void
real_sync_title (NautilusWindow *window,
		 NautilusWindowSlot *slot)
{
	NautilusNavigationWindow *navigation_window;
	NautilusNotebook *notebook;
	char *full_title;
	char *window_title;

	navigation_window = NAUTILUS_NAVIGATION_WINDOW (window);

	EEL_CALL_PARENT (NAUTILUS_WINDOW_CLASS,
			 sync_title, (window, slot));

	if (slot == window->details->active_pane->active_slot) {
		full_title = g_strdup_printf (_("%s - File Browser"), slot->title);

		window_title = eel_str_middle_truncate (full_title, MAX_TITLE_LENGTH);
		gtk_window_set_title (GTK_WINDOW (window), window_title);
		g_free (window_title);
		g_free (full_title);
	}

	notebook = NAUTILUS_NOTEBOOK (NAUTILUS_NAVIGATION_WINDOW_PANE (slot->pane)->notebook);
	nautilus_notebook_sync_tab_label (notebook, slot);

	nautilus_navigation_window_pane_sync_tab_menu_title (NAUTILUS_NAVIGATION_WINDOW_PANE (nautilus_window_get_pane_from_slot (window, slot)), slot);
}

static NautilusIconInfo *
real_get_icon (NautilusWindow *window,
	       NautilusWindowSlot *slot)
{
	return nautilus_icon_info_lookup_from_name ("system-file-manager", 48);
}

static void
real_sync_allow_stop (NautilusWindow *window,
		      NautilusWindowSlot *slot)
{
	NautilusNavigationWindow *navigation_window;
	NautilusNotebook *notebook;

	navigation_window = NAUTILUS_NAVIGATION_WINDOW (window);
	nautilus_navigation_window_set_throbber_active (navigation_window, slot->allow_stop);

	notebook = NAUTILUS_NOTEBOOK (NAUTILUS_NAVIGATION_WINDOW_PANE (slot->pane)->notebook);
	nautilus_notebook_sync_loading (notebook, slot);
}

static void
real_prompt_for_location (NautilusWindow *window, const char *initial)
{
    NautilusNavigationWindowPane *pane;
    
	remember_focus_widget (NAUTILUS_NAVIGATION_WINDOW (window));

    pane = NAUTILUS_NAVIGATION_WINDOW_PANE (window->details->active_pane);
    
	nautilus_navigation_window_pane_show_location_bar_temporarily (pane);
	nautilus_navigation_window_pane_show_navigation_bar_temporarily (pane);
	
	if (initial) {
		nautilus_navigation_bar_set_location (NAUTILUS_NAVIGATION_BAR (pane->navigation_bar),
						      initial);
	}
}

void 
nautilus_navigation_window_show_search (NautilusNavigationWindow *window)
{
	NautilusNavigationWindowPane *pane;

	pane = NAUTILUS_NAVIGATION_WINDOW_PANE (NAUTILUS_WINDOW (window)->details->active_pane);
	if (!nautilus_navigation_window_pane_search_bar_showing (pane)) {
		remember_focus_widget (window);

		nautilus_navigation_window_pane_show_location_bar_temporarily (pane);
		nautilus_navigation_window_pane_set_bar_mode (pane, NAUTILUS_BAR_SEARCH);
		pane->temporary_search_bar = TRUE;
		nautilus_search_bar_clear (NAUTILUS_SEARCH_BAR (pane->search_bar));
	}
	
	nautilus_search_bar_grab_focus (NAUTILUS_SEARCH_BAR (pane->search_bar));
}

/* either called due to slot change, or due to location change in the current slot. */
static void
real_sync_search_widgets (NautilusWindow *window)
{
	NautilusNavigationWindow *navigation_window;
    NautilusNavigationWindowPane *pane;
	NautilusWindowSlot *slot;
	NautilusDirectory *directory;
	NautilusSearchDirectory *search_directory;

	navigation_window = NAUTILUS_NAVIGATION_WINDOW (window);
    pane = NAUTILUS_NAVIGATION_WINDOW_PANE (window->details->active_pane);
	slot = window->details->active_pane->active_slot;

	search_directory = NULL;

	directory = nautilus_directory_get (slot->location);
	if (NAUTILUS_IS_SEARCH_DIRECTORY (directory)) {
		search_directory = NAUTILUS_SEARCH_DIRECTORY (directory);
	}

	if (search_directory != NULL &&
	    !nautilus_search_directory_is_saved_search (search_directory)) {
		nautilus_navigation_window_pane_show_location_bar_temporarily (pane);
		nautilus_navigation_window_pane_set_bar_mode (pane, NAUTILUS_BAR_SEARCH);
		pane->temporary_search_bar = FALSE;
	} else {
		pane->temporary_search_bar = TRUE;
		nautilus_navigation_window_hide_temporary_bars (navigation_window);
	}
}


static void
real_sync_zoom_widgets (NautilusWindow *nautilus_window)
{
	NautilusNavigationWindow *window;
	NautilusNavigationWindowPane *pane;
	NautilusWindowSlot *slot;
	NautilusView *view;
	gboolean supports_zooming, can_zoom;
	GList *walk;
	
	window = NAUTILUS_NAVIGATION_WINDOW (nautilus_window);
	EEL_CALL_PARENT (NAUTILUS_WINDOW_CLASS,
			sync_zoom_widgets, (nautilus_window));
	for (walk = nautilus_window->details->panes; walk; walk = walk->next) {
		pane = NAUTILUS_NAVIGATION_WINDOW_PANE (walk->data);
		slot = NAUTILUS_WINDOW_PANE (pane)->active_slot;
		view = slot->content_view;

		if (view == NULL) {
			/* don't toggle UI state at all. This might be
			 * wrong, but it prevents flickering when opening
			 * a new tab and immediately switching to it -
			 * before view selection.
			 */
			return;
		}

		supports_zooming = nautilus_view_supports_zooming (view);
		can_zoom = supports_zooming &&
			   nautilus_view_get_zoom_level (view) >= NAUTILUS_ZOOM_LEVEL_SMALLEST &&
			   nautilus_view_get_zoom_level (view) <= NAUTILUS_ZOOM_LEVEL_LARGEST;
		
		if (pane->zoom_control != NULL) {
			if (supports_zooming) {
				gtk_widget_set_sensitive (pane->zoom_control, can_zoom);
				gtk_widget_show (pane->zoom_control);
				if (can_zoom) {
					nautilus_zoom_control_set_zoom_level (NAUTILUS_ZOOM_CONTROL (pane->zoom_control),
									      nautilus_view_get_zoom_level (view));
				}
			} else {
				gtk_widget_hide (pane->zoom_control);
			}
		}
	}
}

static void
side_panel_image_changed_callback (NautilusSidebar *side_panel,
                                   gpointer callback_data)
{
        NautilusWindow *window;
	GdkPixbuf *icon;

        window = NAUTILUS_WINDOW (callback_data);

	icon = nautilus_sidebar_get_tab_icon (side_panel);
        nautilus_side_pane_set_panel_image (NAUTILUS_NAVIGATION_WINDOW (window)->sidebar,
                                            GTK_WIDGET (side_panel),
                                            icon);
	if (icon != NULL) {
		g_object_unref (icon);
	}
}

/**
 * add_sidebar_panels:
 * @window:	A NautilusNavigationWindow
 *
 * Adds all sidebars available
 *
 */
static void
add_sidebar_panels (NautilusNavigationWindow *window)
{
	GtkWidget *current;
	GList *providers;
	GList *p;
	NautilusSidebar *sidebar_panel;

	g_assert (NAUTILUS_IS_NAVIGATION_WINDOW (window));

	if (window->sidebar == NULL) {
		return;
	}

 	providers = nautilus_module_get_extensions_for_type (NAUTILUS_TYPE_SIDEBAR_PROVIDER);
	
	for (p = providers; p != NULL; p = p->next) {
		NautilusSidebarProvider *provider;

		provider = NAUTILUS_SIDEBAR_PROVIDER (p->data);
		
		sidebar_panel = nautilus_sidebar_provider_create (provider,
								  NAUTILUS_WINDOW_INFO (window));
		nautilus_navigation_window_add_sidebar_panel (window,
							      sidebar_panel);
		
		g_object_unref (sidebar_panel);
	}

	nautilus_module_extension_list_free (providers);

	current = nautilus_side_pane_get_current_panel (window->sidebar);
	set_current_side_panel
		(window, 
		 NAUTILUS_SIDEBAR (current));
}

gboolean
nautilus_navigation_window_toolbar_showing (NautilusNavigationWindow *window)
{
	if (window->details->toolbar != NULL) {
		return GTK_WIDGET_VISIBLE (window->details->toolbar);
	}
	/* If we're not visible yet we haven't changed visibility, so its TRUE */
	return TRUE;
}

void 
nautilus_navigation_window_hide_status_bar (NautilusNavigationWindow *window)
{
	gtk_widget_hide (NAUTILUS_WINDOW (window)->details->statusbar);

	nautilus_navigation_window_update_show_hide_menu_items (window);
	if (eel_preferences_key_is_writable (NAUTILUS_PREFERENCES_START_WITH_STATUS_BAR) &&
	    eel_preferences_get_boolean (NAUTILUS_PREFERENCES_START_WITH_STATUS_BAR)) {
		eel_preferences_set_boolean (NAUTILUS_PREFERENCES_START_WITH_STATUS_BAR, FALSE);
	}
}

void 
nautilus_navigation_window_show_status_bar (NautilusNavigationWindow *window)
{
	gtk_widget_show (NAUTILUS_WINDOW (window)->details->statusbar);

	nautilus_navigation_window_update_show_hide_menu_items (window);
	if (eel_preferences_key_is_writable (NAUTILUS_PREFERENCES_START_WITH_STATUS_BAR) &&
	    !eel_preferences_get_boolean (NAUTILUS_PREFERENCES_START_WITH_STATUS_BAR)) {
		eel_preferences_set_boolean (NAUTILUS_PREFERENCES_START_WITH_STATUS_BAR, TRUE);
	}
}

gboolean
nautilus_navigation_window_status_bar_showing (NautilusNavigationWindow *window)
{
	if (NAUTILUS_WINDOW (window)->details->statusbar != NULL) {
		return GTK_WIDGET_VISIBLE (NAUTILUS_WINDOW (window)->details->statusbar);
	}
	/* If we're not visible yet we haven't changed visibility, so its TRUE */
	return TRUE;
}


void 
nautilus_navigation_window_hide_toolbar (NautilusNavigationWindow *window)
{
	gtk_widget_hide (window->details->toolbar);
	nautilus_navigation_window_update_show_hide_menu_items (window);
	if (eel_preferences_key_is_writable (NAUTILUS_PREFERENCES_START_WITH_TOOLBAR) &&
	    eel_preferences_get_boolean (NAUTILUS_PREFERENCES_START_WITH_TOOLBAR)) {
		eel_preferences_set_boolean (NAUTILUS_PREFERENCES_START_WITH_TOOLBAR, FALSE);
	}
}

void 
nautilus_navigation_window_show_toolbar (NautilusNavigationWindow *window)
{
	gtk_widget_show (window->details->toolbar);
	nautilus_navigation_window_update_show_hide_menu_items (window);
	if (eel_preferences_key_is_writable (NAUTILUS_PREFERENCES_START_WITH_TOOLBAR) &&
	    !eel_preferences_get_boolean (NAUTILUS_PREFERENCES_START_WITH_TOOLBAR)) {
		eel_preferences_set_boolean (NAUTILUS_PREFERENCES_START_WITH_TOOLBAR, TRUE);
	}
}

void 
nautilus_navigation_window_hide_sidebar (NautilusNavigationWindow *window)
{
	if (window->sidebar == NULL) {
		return;
	}

	nautilus_navigation_window_tear_down_sidebar (window);
	nautilus_navigation_window_update_show_hide_menu_items (window);

	if (eel_preferences_key_is_writable (NAUTILUS_PREFERENCES_START_WITH_SIDEBAR) &&
	    eel_preferences_get_boolean (NAUTILUS_PREFERENCES_START_WITH_SIDEBAR)) {
		eel_preferences_set_boolean (NAUTILUS_PREFERENCES_START_WITH_SIDEBAR, FALSE);
	}
}

void 
nautilus_navigation_window_show_sidebar (NautilusNavigationWindow *window)
{
	if (window->sidebar != NULL) {
		return;
	}

	nautilus_navigation_window_set_up_sidebar (window);
	nautilus_navigation_window_update_show_hide_menu_items (window);
	if (eel_preferences_key_is_writable (NAUTILUS_PREFERENCES_START_WITH_SIDEBAR) &&
	    !eel_preferences_get_boolean (NAUTILUS_PREFERENCES_START_WITH_SIDEBAR)) {
		eel_preferences_set_boolean (NAUTILUS_PREFERENCES_START_WITH_SIDEBAR, TRUE);
	}
}

gboolean
nautilus_navigation_window_sidebar_showing (NautilusNavigationWindow *window)
{
	g_return_val_if_fail (NAUTILUS_IS_NAVIGATION_WINDOW (window), FALSE);

	return (window->sidebar != NULL)
		&& nautilus_horizontal_splitter_is_hidden (NAUTILUS_HORIZONTAL_SPLITTER (window->details->content_paned));
}

/**
 * nautilus_navigation_window_get_base_page_index:
 * @window:	Window to get index from
 *
 * Returns the index of the base page in the history list.
 * Base page is not the currently displayed page, but the page
 * that acts as the base from which the back and forward commands
 * navigate from.
 */
gint 
nautilus_navigation_window_get_base_page_index (NautilusNavigationWindow *window)
{
	NautilusNavigationWindowSlot *slot;
	gint forward_count;

	slot = NAUTILUS_NAVIGATION_WINDOW_SLOT (NAUTILUS_WINDOW (window)->details->active_pane->active_slot);

	forward_count = g_list_length (slot->forward_list); 

	/* If forward is empty, the base it at the top of the list */
	if (forward_count == 0) {
		return 0;
	}

	/* The forward count indicate the relative postion of the base page
	 * in the history list
	 */ 
	return forward_count;
}



/**
 * nautilus_navigation_window_show:
 * @widget: a #GtkWidget.
 *
 * Call parent and then show/hide window items
 * base on user prefs.
 */
static void
nautilus_navigation_window_show (GtkWidget *widget)
{	
	NautilusNavigationWindow *window;
	gboolean show_location_bar;
	gboolean always_use_location_entry;
	GList *walk;

	window = NAUTILUS_NAVIGATION_WINDOW (widget);

	/* Initially show or hide views based on preferences; once the window is displayed
	 * these can be controlled on a per-window basis from View menu items. 
	 */

	if (eel_preferences_get_boolean (NAUTILUS_PREFERENCES_START_WITH_TOOLBAR)) {
		nautilus_navigation_window_show_toolbar (window);
	} else {
		nautilus_navigation_window_hide_toolbar (window);
	}

	show_location_bar = eel_preferences_get_boolean (NAUTILUS_PREFERENCES_START_WITH_LOCATION_BAR);
	always_use_location_entry = eel_preferences_get_boolean (NAUTILUS_PREFERENCES_ALWAYS_USE_LOCATION_ENTRY);
	for (walk = NAUTILUS_WINDOW(window)->details->panes; walk; walk = walk->next) {
		NautilusNavigationWindowPane *pane = walk->data;
		if (show_location_bar) {
			nautilus_navigation_window_pane_show_location_bar (pane, FALSE);
		} else {
			nautilus_navigation_window_pane_hide_location_bar (pane, FALSE);
		}

		if (always_use_location_entry) {
			nautilus_navigation_window_pane_set_bar_mode (pane, NAUTILUS_BAR_NAVIGATION);
		} else {
			nautilus_navigation_window_pane_set_bar_mode (pane, NAUTILUS_BAR_PATH);
		}
	}
	
	if (eel_preferences_get_boolean (NAUTILUS_PREFERENCES_START_WITH_SIDEBAR)) {
		nautilus_navigation_window_show_sidebar (window);
	} else {
		nautilus_navigation_window_hide_sidebar (window);
	}

	if (eel_preferences_get_boolean (NAUTILUS_PREFERENCES_START_WITH_STATUS_BAR)) {
		nautilus_navigation_window_show_status_bar (window);
	} else {
		nautilus_navigation_window_hide_status_bar (window);
	}

	GTK_WIDGET_CLASS (parent_class)->show (widget);
}

static void
nautilus_navigation_window_save_geometry (NautilusNavigationWindow *window)
{
	char *geometry_string;
	gboolean is_maximized;

	g_assert (NAUTILUS_IS_WINDOW (window));

	if (GTK_WIDGET(window)->window) {
		geometry_string = eel_gtk_window_get_geometry_string (GTK_WINDOW (window));
		is_maximized = gdk_window_get_state (GTK_WIDGET (window)->window)
				& GDK_WINDOW_STATE_MAXIMIZED;
		
		if (eel_preferences_key_is_writable (NAUTILUS_PREFERENCES_NAVIGATION_WINDOW_SAVED_GEOMETRY) &&
		    !is_maximized) {
			eel_preferences_set
				(NAUTILUS_PREFERENCES_NAVIGATION_WINDOW_SAVED_GEOMETRY, 
				 geometry_string);
		}
		g_free (geometry_string);

		if (eel_preferences_key_is_writable (NAUTILUS_PREFERENCES_NAVIGATION_WINDOW_MAXIMIZED)) {
			eel_preferences_set_boolean
				(NAUTILUS_PREFERENCES_NAVIGATION_WINDOW_MAXIMIZED, 
				 is_maximized);
		}
	}
}



static void
real_window_close (NautilusWindow *window)
{
	nautilus_navigation_window_save_geometry (NAUTILUS_NAVIGATION_WINDOW (window));
}

static void 
real_get_default_size (NautilusWindow *window,
		       guint *default_width, guint *default_height)
{
	if (default_width) {
		*default_width = NAUTILUS_NAVIGATION_WINDOW_DEFAULT_WIDTH;
	}
	
	if (default_height) {
		*default_height = NAUTILUS_NAVIGATION_WINDOW_DEFAULT_HEIGHT;	
	}
}

static NautilusWindowSlot *
real_open_slot (NautilusWindowPane *pane,
		NautilusWindowOpenSlotFlags flags)
{
	NautilusWindowSlot *slot;

	slot = (NautilusWindowSlot *) g_object_new (NAUTILUS_TYPE_NAVIGATION_WINDOW_SLOT, NULL);
	
	slot->pane = pane;

	nautilus_navigation_window_pane_add_slot_in_tab (NAUTILUS_NAVIGATION_WINDOW_PANE (pane), slot, flags);
	gtk_widget_show (slot->content_box);

	return slot;
}

static void
real_close_slot (NautilusWindowPane *pane,
		 NautilusWindowSlot *slot)
{
	int page_num;
	GtkNotebook *notebook;

	notebook = GTK_NOTEBOOK (NAUTILUS_NAVIGATION_WINDOW_PANE (pane)->notebook);

	page_num = gtk_notebook_page_num (notebook, slot->content_box);
	g_assert (page_num >= 0);
	
	nautilus_navigation_window_pane_remove_page (NAUTILUS_NAVIGATION_WINDOW_PANE (pane), page_num);

	gtk_notebook_set_show_tabs (notebook,
				    gtk_notebook_get_n_pages (notebook) > 1);

	EEL_CALL_PARENT (NAUTILUS_WINDOW_CLASS,
			 close_slot, (pane, slot));
}

static void
nautilus_navigation_window_class_init (NautilusNavigationWindowClass *class)
{
	NAUTILUS_WINDOW_CLASS (class)->window_type = NAUTILUS_WINDOW_NAVIGATION;
	NAUTILUS_WINDOW_CLASS (class)->bookmarks_placeholder = MENU_PATH_BOOKMARKS_PLACEHOLDER;
	
	G_OBJECT_CLASS (class)->finalize = nautilus_navigation_window_finalize;
	GTK_OBJECT_CLASS (class)->destroy = nautilus_navigation_window_destroy;
	GTK_WIDGET_CLASS (class)->show = nautilus_navigation_window_show;
	GTK_WIDGET_CLASS (class)->unrealize = nautilus_navigation_window_unrealize;
	GTK_WIDGET_CLASS (class)->window_state_event = nautilus_navigation_window_state_event;
	GTK_WIDGET_CLASS (class)->key_press_event = nautilus_navigation_window_key_press_event;
	GTK_WIDGET_CLASS (class)->button_press_event = nautilus_navigation_window_button_press_event;
	NAUTILUS_WINDOW_CLASS (class)->load_view_as_menu = real_load_view_as_menu;
	NAUTILUS_WINDOW_CLASS (class)->sync_allow_stop = real_sync_allow_stop;
	NAUTILUS_WINDOW_CLASS (class)->prompt_for_location = real_prompt_for_location;
	NAUTILUS_WINDOW_CLASS (class)->sync_search_widgets = real_sync_search_widgets;
	NAUTILUS_WINDOW_CLASS (class)->sync_zoom_widgets = real_sync_zoom_widgets;
	NAUTILUS_WINDOW_CLASS (class)->sync_title = real_sync_title;
	NAUTILUS_WINDOW_CLASS (class)->get_icon = real_get_icon;
	NAUTILUS_WINDOW_CLASS (class)->get_default_size = real_get_default_size;
	NAUTILUS_WINDOW_CLASS (class)->close = real_window_close;

	NAUTILUS_WINDOW_CLASS (class)->open_slot = real_open_slot;
	NAUTILUS_WINDOW_CLASS (class)->close_slot = real_close_slot;

	g_type_class_add_private (G_OBJECT_CLASS (class), sizeof (NautilusNavigationWindowDetails));


	eel_preferences_add_callback (NAUTILUS_PREFERENCES_MOUSE_BACK_BUTTON, 
				      mouse_back_button_changed, 
				      NULL);

	eel_preferences_add_callback (NAUTILUS_PREFERENCES_MOUSE_FORWARD_BUTTON, 
				      mouse_forward_button_changed, 
				      NULL);

	eel_preferences_add_callback (NAUTILUS_PREFERENCES_MOUSE_USE_EXTRA_BUTTONS,
				      use_extra_mouse_buttons_changed,
				      NULL);
}

static void
split_view_added_to_container_callback (GtkContainer *container, GtkWidget *widget, gpointer user_data)
{
	NautilusNavigationWindowPane *pane;

	/* list view doesn't focus automatically */
	if (FM_IS_LIST_VIEW (widget)) {
		GtkWidget *focus_widget;
		focus_widget = GTK_WIDGET (fm_list_view_get_tree_view (FM_LIST_VIEW (widget)));
		gtk_widget_grab_focus (focus_widget); 
	}

	/* now that view is ready, show the location bar */
	pane = NAUTILUS_NAVIGATION_WINDOW_PANE (user_data);
	gtk_widget_show (pane->location_bar);
}

void nautilus_navigation_window_split_view_on (NautilusNavigationWindow *window)
{
    NautilusWindow *win;
    NautilusNavigationWindowPane *pane, *main_pane;
    GtkWidget *hpaned;
    GtkWidget *vbox;
    gint idx;
    NautilusWindowSlot *slot;
    GFile *location;

    g_print("hhb: split view on\n");
    
    win = NAUTILUS_WINDOW (window);
    pane = NAUTILUS_NAVIGATION_WINDOW_PANE (win->details->active_pane);
    main_pane = pane;
    
    /* remove folder view and location bar from ui */
    g_object_ref (pane->notebook);
    gtk_container_remove (GTK_CONTAINER (window->details->content_paned), pane->notebook);
    g_object_ref (pane->location_bar);
    gtk_container_remove (GTK_CONTAINER (win->details->table), pane->location_bar);
    
    /* put a horizontal splitter where the notebook used to be */
    hpaned = gtk_hpaned_new ();
    gtk_widget_show (hpaned);
    nautilus_horizontal_splitter_pack2 (NAUTILUS_HORIZONTAL_SPLITTER (window->details->content_paned), hpaned);
    window->details->split_view_hpane = hpaned;
    
    /* left side: move original folder view and location bar here */
    pane->border = nautilus_border_new();
    gtk_paned_pack1 (GTK_PANED(hpaned), pane->border, TRUE, TRUE);
    gtk_widget_show(pane->border);
    
    vbox = gtk_vbox_new (FALSE, 4);
    gtk_container_add (GTK_CONTAINER (pane->border), vbox);
    gtk_box_pack_start (GTK_BOX (vbox), pane->location_bar, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (vbox), pane->notebook,TRUE, TRUE, 0);
    gtk_widget_show(vbox);

    /* pack view as combo box into toolbar */
    pane->view_as_combo_box_item_index = gtk_toolbar_get_item_index (GTK_TOOLBAR (pane->location_bar), pane->view_as_combo_box_item);
    g_object_ref (pane->view_as_combo_box_item);
    gtk_container_remove (GTK_CONTAINER (pane->location_bar), GTK_WIDGET (pane->view_as_combo_box_item));
    idx = gtk_toolbar_get_n_items (GTK_TOOLBAR (window->details->toolbar));
    gtk_toolbar_insert (GTK_TOOLBAR (window->details->toolbar), pane->view_as_combo_box_item, idx-1);
    g_object_unref (pane->view_as_combo_box_item);
    
	/* right side */
    pane = nautilus_navigation_window_pane_new (win);
	
    pane->border = nautilus_border_new();
    gtk_paned_pack2 (GTK_PANED(hpaned), pane->border, TRUE, TRUE);
    gtk_widget_show(pane->border);
    
    vbox = gtk_vbox_new (FALSE, 4);
    gtk_container_add (GTK_CONTAINER (pane->border), vbox);
    gtk_widget_show(vbox);

    /* location bar */
    nautilus_navigation_window_pane_setup_location_bar (pane);
    gtk_box_pack_start (GTK_BOX(vbox), pane->location_bar, FALSE, FALSE, 0);
    
    /* notebook */
    nautilus_navigation_window_pane_setup_notebook (pane);
    gtk_box_pack_start (GTK_BOX(vbox), pane->notebook, TRUE, TRUE, 0);
    
    /* slot */
    slot = nautilus_window_open_slot (NAUTILUS_WINDOW_PANE (pane), NAUTILUS_WINDOW_OPEN_SLOT_APPEND);

    nautilus_navigation_window_pane_initialize_tabs_menu(pane);        

    nautilus_window_set_active_slot (win, slot);

    location = nautilus_window_slot_get_location (NAUTILUS_WINDOW_PANE (main_pane)->active_slot);
    if (!location) {
            char *scheme;
            scheme = g_file_get_uri_scheme (location);
        if (!strcmp (scheme, "x-nautilus-search")) {
                    g_object_unref (location);
            }
        g_free (scheme);
        location = g_file_new_for_path (g_get_home_dir ());
    }

    nautilus_window_slot_go_to (slot, location, FALSE);
    g_object_unref (location);
    nautilus_navigation_window_pane_sync_location_widgets(pane);
    
    /* listen when view is finally added */
    g_signal_connect_object (GTK_CONTAINER (NAUTILUS_WINDOW_PANE (pane)->active_slot->view_box), "add",
        G_CALLBACK (split_view_added_to_container_callback), pane, 0);
    
    /* move view as combo box of second pane to main toolbar, also */
    g_object_ref (pane->view_as_combo_box_item);
    pane->view_as_combo_box_item_index = gtk_toolbar_get_item_index (GTK_TOOLBAR (pane->location_bar), pane->view_as_combo_box_item);
    gtk_container_remove (GTK_CONTAINER (pane->location_bar), GTK_WIDGET (pane->view_as_combo_box_item));
    idx = gtk_toolbar_get_n_items (GTK_TOOLBAR (window->details->toolbar));
    gtk_toolbar_insert (GTK_TOOLBAR (window->details->toolbar), pane->view_as_combo_box_item, idx-1);
    g_object_unref (pane->view_as_combo_box_item);
}

void nautilus_navigation_window_split_view_off (NautilusNavigationWindow *window)
{
    NautilusWindow *win;
    NautilusNavigationWindowPane *main_pane;
    GList *walk;
    GtkWidget *vbox;
    GtkWidget *border;

    g_print("hhb: split view off\n");

    win = NAUTILUS_WINDOW (window);
    
    g_return_if_fail (win);
    g_return_if_fail (window->details->split_view_hpane);

    /* no matter what the active pane was before: now, it's the main pane */
    main_pane = NAUTILUS_NAVIGATION_WINDOW_PANE (win->details->panes->data);
    nautilus_window_set_active_pane (win, NAUTILUS_WINDOW_PANE (main_pane));
    
    /* delete all panes except the first (main) pane */
    for (walk = win->details->panes->next; walk; walk = walk->next) {
        nautilus_window_close_pane (walk->data);
    }

    /* remove folder view and location bar from left side, and destroy hpane */
    border = gtk_paned_get_child1 (GTK_PANED (window->details->split_view_hpane));
    vbox = gtk_bin_get_child (GTK_BIN (border));
    g_object_ref (main_pane->notebook);
    gtk_container_remove (GTK_CONTAINER (vbox), main_pane->notebook);
    g_object_ref (main_pane->location_bar);
    gtk_container_remove (GTK_CONTAINER (vbox), main_pane->location_bar);
    gtk_widget_destroy (window->details->split_view_hpane);
    window->details->split_view_hpane = NULL;
    main_pane->border = NULL;

    /* put widgets from left pane back to their original places */
    gtk_table_attach (GTK_TABLE (win->details->table),
		      main_pane->location_bar,
		      /* X direction */                    /* Y direction */
		      0, 1,                                2, 3,
		      GTK_EXPAND | GTK_FILL | GTK_SHRINK,  0,
		      0,                                   0);
    nautilus_horizontal_splitter_pack2 (NAUTILUS_HORIZONTAL_SPLITTER (window->details->content_paned),
					main_pane->notebook);

    /* put view as combo box back */
    g_object_ref (main_pane->view_as_combo_box_item);
    gtk_container_remove (GTK_CONTAINER (window->details->toolbar), GTK_WIDGET (main_pane->view_as_combo_box_item));
    gtk_toolbar_insert (GTK_TOOLBAR (main_pane->location_bar), main_pane->view_as_combo_box_item, -1);
    g_object_unref (main_pane->view_as_combo_box_item);
}
