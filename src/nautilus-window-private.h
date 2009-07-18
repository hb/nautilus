/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */

/*
 *  Nautilus
 *
 *  Copyright (C) 1999, 2000 Red Hat, Inc.
 *  Copyright (C) 1999, 2000, 2001 Eazel, Inc.
 *
 *  Nautilus is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License as
 *  published by the Free Software Foundation; either version 2 of the
 *  License, or (at your option) any later version.
 *
 *  Nautilus is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  Authors: Elliot Lee <sopwith@redhat.com>
 *           Darin Adler <darin@bentspoon.com>
 *
 */

#ifndef NAUTILUS_WINDOW_PRIVATE_H
#define NAUTILUS_WINDOW_PRIVATE_H

#include "nautilus-window.h"
#include "nautilus-window-slot.h"
#include "nautilus-window-pane.h"
#include "nautilus-spatial-window.h"
#include "nautilus-navigation-window.h"

#include <libnautilus-private/nautilus-directory.h>

struct _NautilusNavigationWindowPane;

/* FIXME bugzilla.gnome.org 42575: Migrate more fields into here. */
struct NautilusWindowDetails
{
        GtkWidget *table;
        GtkWidget *statusbar;
        GtkWidget *menubar;
        
        GtkUIManager *ui_manager;
        GtkActionGroup *main_action_group; /* owned by ui_manager */
        guint help_message_cid;

        /* Menus. */
        guint extensions_menu_merge_id;
        GtkActionGroup *extensions_menu_action_group;

        GtkActionGroup *bookmarks_action_group;
        guint refresh_bookmarks_menu_idle_id;
        guint bookmarks_merge_id;

	NautilusWindowShowHiddenFilesMode show_hidden_files_mode;

	/* View As menu */
	GList *short_list_viewers;
	char *extra_viewer;

	/* View As choices */
	GtkActionGroup *view_as_action_group; /* owned by ui_manager */
	GtkRadioAction *view_as_radio_action;
	GtkRadioAction *extra_viewer_radio_action;
	guint short_list_merge_id;
	guint extra_viewer_merge_id;

	/* Ensures that we do not react on signals of a
	 * view that is re-used as new view when its loading
	 * is cancelled
	 */
	gboolean temporarily_ignore_view_signals;

        /* available panes, and active pane.
         * Both of them may never be NULL.
         */
        GList *panes;
        NautilusWindowPane *active_pane;
};

struct _NautilusNavigationWindowDetails {
        GtkWidget *content_paned;
        GtkWidget *content_box;
        GtkActionGroup *navigation_action_group; /* owned by ui_manager */
        
        /* Side Pane */
        int side_pane_width;
        NautilusSidebar *current_side_panel;

	/* Menus */
        GtkActionGroup *go_menu_action_group;
	guint refresh_go_menu_idle_id;
        guint go_menu_merge_id;
        
        /* Toolbar */
        GtkWidget *toolbar;

        guint extensions_toolbar_merge_id;
        GtkActionGroup *extensions_toolbar_action_group;

	/* Throbber */
        gboolean    throbber_active;
        GtkWidget  *throbber;

        /* focus widget before the location bar has been shown temporarily */
        GtkWidget *last_focus_widget;
        	
        /* split view */
        GtkWidget *split_view_hpane;
};

#define NAUTILUS_MENU_PATH_BACK_ITEM			"/menu/Go/Back"
#define NAUTILUS_MENU_PATH_FORWARD_ITEM			"/menu/Go/Forward"
#define NAUTILUS_MENU_PATH_UP_ITEM			"/menu/Go/Up"

#define NAUTILUS_MENU_PATH_RELOAD_ITEM			"/menu/View/Reload"
#define NAUTILUS_MENU_PATH_ZOOM_IN_ITEM			"/menu/View/Zoom Items Placeholder/Zoom In"
#define NAUTILUS_MENU_PATH_ZOOM_OUT_ITEM		"/menu/View/Zoom Items Placeholder/Zoom Out"
#define NAUTILUS_MENU_PATH_ZOOM_NORMAL_ITEM		"/menu/View/Zoom Items Placeholder/Zoom Normal"

#define NAUTILUS_COMMAND_BACK				"/commands/Back"
#define NAUTILUS_COMMAND_FORWARD			"/commands/Forward"
#define NAUTILUS_COMMAND_UP				"/commands/Up"

#define NAUTILUS_COMMAND_RELOAD				"/commands/Reload"
#define NAUTILUS_COMMAND_BURN_CD			"/commands/Burn CD"
#define NAUTILUS_COMMAND_STOP				"/commands/Stop"
#define NAUTILUS_COMMAND_ZOOM_IN			"/commands/Zoom In"
#define NAUTILUS_COMMAND_ZOOM_OUT			"/commands/Zoom Out"
#define NAUTILUS_COMMAND_ZOOM_NORMAL			"/commands/Zoom Normal"

/* window geometry */
/* These are very small, and a Nautilus window at this tiny size is *almost*
 * completely unusable. However, if all the extra bits (sidebar, location bar, etc)
 * are turned off, you can see an icon or two at this size. See bug 5946.
 */
#define NAUTILUS_WINDOW_MIN_WIDTH			200
#define NAUTILUS_WINDOW_MIN_HEIGHT			200

#define NAUTILUS_SPATIAL_WINDOW_MIN_WIDTH			100
#define NAUTILUS_SPATIAL_WINDOW_MIN_HEIGHT			100
#define NAUTILUS_SPATIAL_WINDOW_DEFAULT_WIDTH			500
#define NAUTILUS_SPATIAL_WINDOW_DEFAULT_HEIGHT			300

#define NAUTILUS_NAVIGATION_WINDOW_DEFAULT_WIDTH        800
#define NAUTILUS_NAVIGATION_WINDOW_DEFAULT_HEIGHT       550

typedef void (*NautilusBookmarkFailedCallback) (NautilusWindow *window,
                                                NautilusBookmark *bookmark);

void               nautilus_window_set_status                            (NautilusWindow    *window,
									  NautilusWindowSlot *slot,
                                                                          const char        *status);
void               nautilus_window_load_view_as_menus                    (NautilusWindow    *window);
void               nautilus_window_load_extension_menus                  (NautilusWindow    *window);
void               nautilus_window_initialize_menus                      (NautilusWindow    *window);
void               nautilus_navigation_window_pane_initialize_tabs_menu  (struct _NautilusNavigationWindowPane *pane);
void               nautilus_window_remove_trash_monitor_callback         (NautilusWindow    *window);
NautilusWindowPane *nautilus_window_get_next_pane                        (NautilusWindow *window);
void               nautilus_menus_append_bookmark_to_menu                (NautilusWindow    *window, 
                                                                          NautilusBookmark  *bookmark, 
                                                                          const char        *parent_path,
                                                                          const char        *parent_id,
                                                                          guint              index_in_parent,
                                                                          GtkActionGroup    *action_group,
                                                                          guint              merge_id,
                                                                          GCallback          refresh_callback,
                                                                          NautilusBookmarkFailedCallback failed_callback);
#ifdef NEW_UI_COMPLETE
void               nautilus_window_go_up                                 (NautilusWindow    *window);
#endif
void               nautilus_window_update_find_menu_item                 (NautilusWindow    *window);

NautilusWindowSlot *nautilus_window_open_slot                            (NautilusWindowPane *pane,
									  NautilusWindowOpenSlotFlags flags);
void                nautilus_window_close_slot                           (NautilusWindowSlot *slot);

NautilusWindowSlot *nautilus_window_get_slot_for_view                    (NautilusWindow *window,
									  NautilusView   *view);

GList *              nautilus_window_get_slots                           (NautilusWindow    *window);
NautilusWindowSlot * nautilus_window_get_active_slot                     (NautilusWindow    *window);
void                 nautilus_window_set_active_slot                     (NautilusWindow    *window,
									  NautilusWindowSlot *slot);
void                 nautilus_window_set_active_pane                     (NautilusWindow *window,
                                                                          NautilusWindowPane *new_pane);
NautilusWindowPane * nautilus_window_get_active_pane                     (NautilusWindow *window);

void               nautilus_send_history_list_changed                    (void);
void               nautilus_remove_from_history_list_no_notify           (GFile             *location);
gboolean           nautilus_add_bookmark_to_history_list                 (NautilusBookmark  *bookmark);
gboolean           nautilus_add_to_history_list_no_notify                (GFile             *location,
									  const char        *name,
									  gboolean           has_custom_name,
									  GIcon            *icon);
GList *            nautilus_get_history_list                             (void);
void               nautilus_window_bookmarks_preference_changed_callback (gpointer           user_data);
void               nautilus_window_constructed                           (NautilusWindow    *window);
NautilusWindowPane* nautilus_window_get_pane_from_slot                   (NautilusWindow *window,
									  NautilusWindowSlot *slot);


/* sync window GUI with current slot. Used when changing slots,
 * and when updating the slot state.
 */
void nautilus_window_sync_status           (NautilusWindow *window);
void nautilus_window_sync_allow_stop       (NautilusWindow *window,
					    NautilusWindowSlot *slot);
void nautilus_window_sync_title            (NautilusWindow *window,
					    NautilusWindowSlot *slot);
void nautilus_window_sync_location_widgets (NautilusWindow *window);
void nautilus_window_sync_zoom_widgets     (NautilusWindow *window);

/* Navigation window menus */
void               nautilus_navigation_window_initialize_actions                    (NautilusNavigationWindow    *window);
void               nautilus_navigation_window_initialize_menus                      (NautilusNavigationWindow    *window);
void               nautilus_navigation_window_remove_bookmarks_menu_callback        (NautilusNavigationWindow    *window);

void               nautilus_navigation_window_remove_bookmarks_menu_items           (NautilusNavigationWindow    *window);
void               nautilus_navigation_window_update_show_hide_menu_items           (NautilusNavigationWindow     *window);
void               nautilus_navigation_window_update_spatial_menu_item              (NautilusNavigationWindow     *window);
void               nautilus_navigation_window_update_tab_menu_item_visibility       (NautilusNavigationWindow     *window);
void               nautilus_navigation_window_pane_sync_tab_menu_title              (struct _NautilusNavigationWindowPane *pane,
										     NautilusWindowSlot           *slot);
void               nautilus_navigation_window_remove_go_menu_callback    (NautilusNavigationWindow    *window);
void               nautilus_navigation_window_remove_go_menu_items       (NautilusNavigationWindow    *window);

/* Navigation window toolbar */
void               nautilus_navigation_window_activate_throbber                     (NautilusNavigationWindow    *window);
void               nautilus_navigation_window_initialize_toolbars                   (NautilusNavigationWindow    *window);
void               nautilus_navigation_window_load_extension_toolbar_items          (NautilusNavigationWindow    *window);
void               nautilus_navigation_window_set_throbber_active                   (NautilusNavigationWindow    *window, 
                                                                                     gboolean                     active);
void               nautilus_navigation_window_go_back                               (NautilusNavigationWindow    *window);
void               nautilus_navigation_window_go_forward                            (NautilusNavigationWindow    *window);
void               nautilus_window_close_pane                                       (NautilusWindowPane *pane);
void               nautilus_navigation_window_update_split_view_actions_sensitivity (NautilusNavigationWindow    *window);

#endif /* NAUTILUS_WINDOW_PRIVATE_H */
