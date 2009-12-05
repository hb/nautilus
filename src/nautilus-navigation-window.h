/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */

/*
 *  Nautilus
 *
 *  Copyright (C) 1999, 2000 Red Hat, Inc.
 *  Copyright (C) 1999, 2000, 2001 Eazel, Inc.
 *  Copyright (C) 2003 Ximian, Inc.
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
/* nautilus-navigation-window.h: Interface of the navigation window object */

#ifndef NAUTILUS_NAVIGATION_WINDOW_H
#define NAUTILUS_NAVIGATION_WINDOW_H

#include <gtk/gtk.h>
#include <eel/eel-glib-extensions.h>
#include <libnautilus-private/nautilus-bookmark.h>
#include <libnautilus-private/nautilus-sidebar.h>
#include "nautilus-application.h"
#include "nautilus-information-panel.h"
#include "nautilus-side-pane.h"
#include "nautilus-window.h"

#define NAUTILUS_TYPE_NAVIGATION_WINDOW nautilus_navigation_window_get_type()
#define NAUTILUS_NAVIGATION_WINDOW(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), NAUTILUS_TYPE_NAVIGATION_WINDOW, NautilusNavigationWindow))
#define NAUTILUS_NAVIGATION_WINDOW_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), NAUTILUS_TYPE_NAVIGATION_WINDOW, NautilusNavigationWindowClass))
#define NAUTILUS_IS_NAVIGATION_WINDOW(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), NAUTILUS_TYPE_NAVIGATION_WINDOW))
#define NAUTILUS_IS_NAVIGATION_WINDOW_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), NAUTILUS_TYPE_NAVIGATION_WINDOW))
#define NAUTILUS_NAVIGATION_WINDOW_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), NAUTILUS_TYPE_NAVIGATION_WINDOW, NautilusNavigationWindowClass))

typedef struct _NautilusNavigationWindow        NautilusNavigationWindow;
typedef struct _NautilusNavigationWindowClass   NautilusNavigationWindowClass;
typedef struct _NautilusNavigationWindowDetails NautilusNavigationWindowDetails; 


struct _NautilusNavigationWindow {
        NautilusWindow parent_object;
        
        NautilusNavigationWindowDetails *details;
        
        /** UI stuff **/
        NautilusSidePane *sidebar;

        /* Current views stuff */
        GList *sidebar_panels;
};


struct _NautilusNavigationWindowClass {
        NautilusWindowClass parent_spot;
};

GType    nautilus_navigation_window_get_type             (void);
void     nautilus_navigation_window_allow_back           (NautilusNavigationWindow *window,
                                                          gboolean                  allow);
void     nautilus_navigation_window_allow_forward        (NautilusNavigationWindow *window,
                                                          gboolean                  allow);
void     nautilus_navigation_window_clear_back_list      (NautilusNavigationWindow *window);
void     nautilus_navigation_window_clear_forward_list   (NautilusNavigationWindow *window);
void     nautilus_forget_history                         (void);
gint     nautilus_navigation_window_get_base_page_index  (NautilusNavigationWindow *window);
void     nautilus_navigation_window_hide_toolbar         (NautilusNavigationWindow *window);
void     nautilus_navigation_window_show_toolbar         (NautilusNavigationWindow *window);
gboolean nautilus_navigation_window_toolbar_showing      (NautilusNavigationWindow *window);
void     nautilus_navigation_window_hide_sidebar         (NautilusNavigationWindow *window);
void     nautilus_navigation_window_show_sidebar         (NautilusNavigationWindow *window);
gboolean nautilus_navigation_window_sidebar_showing      (NautilusNavigationWindow *window);
void     nautilus_navigation_window_add_sidebar_panel    (NautilusNavigationWindow *window,
                                                          NautilusSidebar          *sidebar_panel);
void     nautilus_navigation_window_remove_sidebar_panel (NautilusNavigationWindow *window,
                                                          NautilusSidebar          *sidebar_panel);
void     nautilus_navigation_window_hide_status_bar      (NautilusNavigationWindow *window);
void     nautilus_navigation_window_show_status_bar      (NautilusNavigationWindow *window);
gboolean nautilus_navigation_window_status_bar_showing   (NautilusNavigationWindow *window);
void     nautilus_navigation_window_back_or_forward      (NautilusNavigationWindow *window,
                                                          gboolean                  back,
                                                          guint                     distance,
							  gboolean                  new_tab);
void     nautilus_navigation_window_show_search          (NautilusNavigationWindow *window);
void     nautilus_navigation_window_unset_focus_widget   (NautilusNavigationWindow *window);
gboolean nautilus_navigation_window_hide_temporary_bars (NautilusNavigationWindow *window);
void     nautilus_navigation_window_split_view_on        (NautilusNavigationWindow *window);
void     nautilus_navigation_window_split_view_off       (NautilusNavigationWindow *window);

void     nautilus_navigation_window_hide_search          (NautilusNavigationWindow *window);
void     nautilus_navigation_window_set_search_button	  (NautilusNavigationWindow *window,
							   gboolean		    state);
void     nautilus_navigation_window_restore_focus_widget (NautilusNavigationWindow *window);

inline gboolean nautilus_navigation_window_is_in_temporary_navigation_bar (GtkWidget *widget,
				NautilusNavigationWindow *window);
inline gboolean nautilus_navigation_window_is_in_temporary_search_bar (GtkWidget *widget,
			    NautilusNavigationWindow *window);

#endif
