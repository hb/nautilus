/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/*
 * nautilus-window-pane.c: Nautilus window pane
 *
 * Copyright (C) 2008 Free Software Foundation, Inc.
 * Copyright (C) 2011 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Authors: Holger Berndt <berndth@gmx.de>
 *          Cosimo Cecchi <cosimoc@redhat.com>
 *
 */

#include "nautilus-window-pane.h"

#include "nautilus-actions.h"
#include "nautilus-location-bar.h"
#include "nautilus-notebook.h"
#include "nautilus-pathbar.h"
#include "nautilus-toolbar.h"
#include "nautilus-window-manage-views.h"
#include "nautilus-window-private.h"

#include <glib/gi18n.h>

#include <libnautilus-private/nautilus-clipboard.h>
#include <libnautilus-private/nautilus-global-preferences.h>
#include <libnautilus-private/nautilus-entry.h>

#define DEBUG_FLAG NAUTILUS_DEBUG_WINDOW
#include <libnautilus-private/nautilus-debug.h>

enum {
	PROP_WINDOW = 1,
	NUM_PROPERTIES
};

static GParamSpec *properties[NUM_PROPERTIES] = { NULL, };

G_DEFINE_TYPE (NautilusWindowPane, nautilus_window_pane,
	       GTK_TYPE_BOX)

static gboolean
widget_is_in_temporary_bars (GtkWidget *widget,
			     NautilusWindowPane *pane)
{
	gboolean res = FALSE;

	if ((gtk_widget_get_ancestor (widget, NAUTILUS_TYPE_LOCATION_BAR) != NULL &&
	     pane->temporary_navigation_bar) ||
	    (gtk_widget_get_ancestor (widget, NAUTILUS_TYPE_SEARCH_BAR) != NULL &&
	     pane->temporary_search_bar))
		res = TRUE;

	return res;
}

static void
unset_focus_widget (NautilusWindowPane *pane)
{
	if (pane->last_focus_widget != NULL) {
		g_object_remove_weak_pointer (G_OBJECT (pane->last_focus_widget),
					      (gpointer *) &pane->last_focus_widget);
		pane->last_focus_widget = NULL;
	}
}

static void
remember_focus_widget (NautilusWindowPane *pane)
{
	GtkWidget *focus_widget;

	focus_widget = gtk_window_get_focus (GTK_WINDOW (pane->window));
	if (focus_widget != NULL &&
	    !widget_is_in_temporary_bars (focus_widget, pane)) {
		unset_focus_widget (pane);

		pane->last_focus_widget = focus_widget;
		g_object_add_weak_pointer (G_OBJECT (focus_widget),
					   (gpointer *) &(pane->last_focus_widget));
	}
}

static void
restore_focus_widget (NautilusWindowPane *pane)
{
	if (pane->last_focus_widget != NULL) {
		if (NAUTILUS_IS_VIEW (pane->last_focus_widget)) {
			nautilus_view_grab_focus (NAUTILUS_VIEW (pane->last_focus_widget));
		} else {
			gtk_widget_grab_focus (pane->last_focus_widget);
		}

		unset_focus_widget (pane);
	}
}

static inline NautilusWindowSlot *
get_first_inactive_slot (NautilusWindowPane *pane)
{
	GList *l;
	NautilusWindowSlot *slot;

	for (l = pane->slots; l != NULL; l = l->next) {
		slot = NAUTILUS_WINDOW_SLOT (l->data);
		if (slot != pane->active_slot) {
			return slot;
		}
	}

	return NULL;
}

static int
bookmark_list_get_uri_index (GList *list, GFile *location)
{
	NautilusBookmark *bookmark;
	GList *l;
	GFile *tmp;
	int i;

	g_return_val_if_fail (location != NULL, -1);

	for (i = 0, l = list; l != NULL; i++, l = l->next) {
		bookmark = NAUTILUS_BOOKMARK (l->data);

		tmp = nautilus_bookmark_get_location (bookmark);
		if (g_file_equal (location, tmp)) {
			g_object_unref (tmp);
			return i;
		}
		g_object_unref (tmp);
	}

	return -1;
}

static void
search_bar_activate_callback (NautilusSearchBar *bar,
			      NautilusWindowPane *pane)
{
	char *uri, *current_uri;
	NautilusDirectory *directory;
	NautilusSearchDirectory *search_directory;
	NautilusQuery *query;
	GFile *location;

	uri = nautilus_search_directory_generate_new_uri ();
	location = g_file_new_for_uri (uri);

	directory = nautilus_directory_get (location);
	g_assert (NAUTILUS_IS_SEARCH_DIRECTORY (directory));

	search_directory = NAUTILUS_SEARCH_DIRECTORY (directory);
	query = nautilus_search_bar_get_query (NAUTILUS_SEARCH_BAR (pane->search_bar));

	if (query != NULL) {
		current_uri = nautilus_window_slot_get_location_uri (pane->active_slot);

		nautilus_query_set_location (query, current_uri);
		nautilus_search_directory_set_query (search_directory, query);

		g_free (current_uri);
		g_object_unref (query);
	}

	nautilus_window_slot_open_location (pane->active_slot, location, 0);

	nautilus_directory_unref (directory);
	g_object_unref (location);	
	g_free (uri);
}

static void
nautilus_window_pane_hide_temporary_bars (NautilusWindowPane *pane)
{
	NautilusWindowSlot *slot;
	NautilusDirectory *directory;

	slot = pane->active_slot;

	if (pane->temporary_navigation_bar) {
		directory = nautilus_directory_get (slot->location);

		pane->temporary_navigation_bar = FALSE;

		/* if we're in a search directory, hide the main bar, and show the search
		 * bar again; otherwise, just hide the whole toolbar.
		 */
		if (NAUTILUS_IS_SEARCH_DIRECTORY (directory)) {
			nautilus_toolbar_set_show_main_bar (NAUTILUS_TOOLBAR (pane->tool_bar), FALSE);
			nautilus_toolbar_set_show_search_bar (NAUTILUS_TOOLBAR (pane->tool_bar), TRUE);
		} else {
			gtk_widget_hide (pane->tool_bar);
		}

		nautilus_directory_unref (directory);
	}
}

static void
search_bar_cancel_callback (GtkWidget *widget,
			    NautilusWindowPane *pane)
{
	GtkAction *search;

	search = gtk_action_group_get_action (pane->action_group,
					      NAUTILUS_ACTION_SEARCH);

	gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (search), FALSE);
}

static void
navigation_bar_cancel_callback (GtkWidget *widget,
				NautilusWindowPane *pane)
{
	nautilus_toolbar_set_show_location_entry (NAUTILUS_TOOLBAR (pane->tool_bar), FALSE);

	nautilus_window_pane_hide_temporary_bars (pane);
	restore_focus_widget (pane);
}

static void
nautilus_window_pane_ensure_search_bar (NautilusWindowPane *pane)
{
	remember_focus_widget (pane);

	nautilus_toolbar_set_show_search_bar (NAUTILUS_TOOLBAR (pane->tool_bar), TRUE);

	if (!g_settings_get_boolean (nautilus_window_state,
				     NAUTILUS_WINDOW_STATE_START_WITH_TOOLBAR)) {
		nautilus_toolbar_set_show_main_bar (NAUTILUS_TOOLBAR (pane->tool_bar), FALSE);
		gtk_widget_show (pane->tool_bar);
		nautilus_search_bar_clear (NAUTILUS_SEARCH_BAR (pane->search_bar));

		pane->temporary_search_bar = TRUE;
	}

	nautilus_search_bar_grab_focus (NAUTILUS_SEARCH_BAR (pane->search_bar));
}

static void
nautilus_window_pane_hide_search_bar (NautilusWindowPane *pane)
{
	nautilus_toolbar_set_show_search_bar (NAUTILUS_TOOLBAR (pane->tool_bar), FALSE);
	restore_focus_widget (pane);

	if (pane->temporary_search_bar) {
		pane->temporary_search_bar = FALSE;

		gtk_widget_hide (pane->tool_bar);
	}
}

static void
navigation_bar_location_changed_callback (GtkWidget *widget,
					  GFile *location,
					  NautilusWindowPane *pane)
{
	nautilus_toolbar_set_show_location_entry (NAUTILUS_TOOLBAR (pane->tool_bar), FALSE);
	nautilus_window_pane_hide_search_bar (pane);
	nautilus_window_pane_hide_temporary_bars (pane);

	restore_focus_widget (pane);

	nautilus_window_slot_open_location (pane->active_slot, location, 0);
}

static gboolean
toolbar_focus_in_callback (GtkWidget *widget,
			   GdkEventFocus *event,
			   gpointer user_data)
{
	NautilusWindowPane *pane = user_data;
	nautilus_window_set_active_pane (pane->window, pane);

	return FALSE;
}

static void
path_bar_location_changed_callback (GtkWidget *widget,
				    GFile *location,
				    NautilusWindowPane *pane)
{
	NautilusWindowSlot *slot;
	int i;

	slot = pane->active_slot;
	nautilus_window_set_active_pane (pane->window, pane);

	/* check whether we already visited the target location */
	i = bookmark_list_get_uri_index (slot->back_list, location);
	if (i >= 0) {
		nautilus_window_back_or_forward (pane->window, TRUE, i, 0);
	} else {
		nautilus_window_slot_open_location (pane->active_slot, location, 0);
	}
}

static void
path_bar_path_event_callback (NautilusPathBar *path_bar,
			      GFile *location,
			      GdkEventButton *event,
			      NautilusWindowPane *pane)
{
	NautilusWindowSlot *slot;
	NautilusWindowOpenFlags flags;
	int mask;
	NautilusView *view;
	char *uri;

	if (event->type == GDK_BUTTON_RELEASE) {
		mask = event->state & gtk_accelerator_get_default_mod_mask ();
		flags = 0;

		if (event->button == 2 && mask == 0) {
			flags = NAUTILUS_WINDOW_OPEN_FLAG_NEW_TAB;
		} else if (event->button == 1 && mask == GDK_CONTROL_MASK) {
			flags = NAUTILUS_WINDOW_OPEN_FLAG_NEW_WINDOW;
		}

		if (flags != 0) {
			slot = nautilus_window_get_active_slot (pane->window);
			nautilus_window_slot_open_location (slot, location, flags);
		}
	} else if (event->button == 3) {
		slot = nautilus_window_get_active_slot (pane->window);
		view = slot->content_view;
		if (view != NULL) {
			uri = g_file_get_uri (location);
			nautilus_view_pop_up_location_context_menu (view, event, uri);
			g_free (uri);
		}
	}
}

static void
notebook_popup_menu_new_tab_cb (GtkMenuItem *menuitem,
				gpointer user_data)
{
	NautilusWindowPane *pane;

	pane = user_data;
	nautilus_window_new_tab (pane->window);
}

static void
notebook_popup_menu_move_left_cb (GtkMenuItem *menuitem,
				  gpointer user_data)
{
	NautilusWindowPane *pane;

	pane = NAUTILUS_WINDOW_PANE (user_data);
	nautilus_notebook_reorder_current_child_relative (NAUTILUS_NOTEBOOK (pane->notebook), -1);
}

static void
notebook_popup_menu_move_right_cb (GtkMenuItem *menuitem,
				   gpointer user_data)
{
	NautilusWindowPane *pane;

	pane = NAUTILUS_WINDOW_PANE (user_data);
	nautilus_notebook_reorder_current_child_relative (NAUTILUS_NOTEBOOK (pane->notebook), 1);
}

static void
notebook_popup_menu_close_cb (GtkMenuItem *menuitem,
			      gpointer user_data)
{
	NautilusWindowPane *pane;
	NautilusWindowSlot *slot;

	pane = NAUTILUS_WINDOW_PANE (user_data);
	slot = pane->active_slot;
	nautilus_window_pane_slot_close (pane, slot);
}

static void
notebook_popup_menu_show (NautilusWindowPane *pane,
			  GdkEventButton *event)
{
	GtkWidget *popup;
	GtkWidget *item;
	GtkWidget *image;
	int button, event_time;
	gboolean can_move_left, can_move_right;
	NautilusNotebook *notebook;

	notebook = NAUTILUS_NOTEBOOK (pane->notebook);

	can_move_left = nautilus_notebook_can_reorder_current_child_relative (notebook, -1);
	can_move_right = nautilus_notebook_can_reorder_current_child_relative (notebook, 1);

	popup = gtk_menu_new();

	item = gtk_menu_item_new_with_mnemonic (_("_New Tab"));
	g_signal_connect (item, "activate",
			  G_CALLBACK (notebook_popup_menu_new_tab_cb),
			  pane);
	gtk_menu_shell_append (GTK_MENU_SHELL (popup),
			       item);

	gtk_menu_shell_append (GTK_MENU_SHELL (popup),
			       gtk_separator_menu_item_new ());

	item = gtk_menu_item_new_with_mnemonic (_("Move Tab _Left"));
	g_signal_connect (item, "activate",
			  G_CALLBACK (notebook_popup_menu_move_left_cb),
			  pane);
	gtk_menu_shell_append (GTK_MENU_SHELL (popup),
			       item);
	gtk_widget_set_sensitive (item, can_move_left);

	item = gtk_menu_item_new_with_mnemonic (_("Move Tab _Right"));
	g_signal_connect (item, "activate",
			  G_CALLBACK (notebook_popup_menu_move_right_cb),
			  pane);
	gtk_menu_shell_append (GTK_MENU_SHELL (popup),
			       item);
	gtk_widget_set_sensitive (item, can_move_right);

	gtk_menu_shell_append (GTK_MENU_SHELL (popup),
			       gtk_separator_menu_item_new ());

	item = gtk_image_menu_item_new_with_mnemonic (_("_Close Tab"));
	image = gtk_image_new_from_stock (GTK_STOCK_CLOSE, GTK_ICON_SIZE_MENU);
	gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
	g_signal_connect (item, "activate",
			  G_CALLBACK (notebook_popup_menu_close_cb), pane);
	gtk_menu_shell_append (GTK_MENU_SHELL (popup),
			       item);

	gtk_widget_show_all (popup);

	if (event) {
		button = event->button;
		event_time = event->time;
	} else {
		button = 0;
		event_time = gtk_get_current_event_time ();
	}

	/* TODO is this correct? */
	gtk_menu_attach_to_widget (GTK_MENU (popup),
				   pane->notebook,
				   NULL);

	gtk_menu_popup (GTK_MENU (popup), NULL, NULL, NULL, NULL,
			button, event_time);
}

/* emitted when the user clicks the "close" button of tabs */
static void
notebook_tab_close_requested (NautilusNotebook *notebook,
			      NautilusWindowSlot *slot,
			      NautilusWindowPane *pane)
{
	nautilus_window_pane_slot_close (pane, slot);
}

static gboolean
notebook_button_press_cb (GtkWidget *widget,
			  GdkEventButton *event,
			  gpointer user_data)
{
	NautilusWindowPane *pane;

	pane = user_data;
	if (GDK_BUTTON_PRESS == event->type && 3 == event->button) {
		notebook_popup_menu_show (pane, event);
		return TRUE;
	}

	return FALSE;
}

static gboolean
notebook_popup_menu_cb (GtkWidget *widget,
			gpointer user_data)
{
	NautilusWindowPane *pane;

	pane = user_data;
	notebook_popup_menu_show (pane, NULL);
	return TRUE;
}

static gboolean
notebook_switch_page_cb (GtkNotebook *notebook,
			 GtkWidget *page,
			 unsigned int page_num,
			 NautilusWindowPane *pane)
{
	NautilusWindowSlot *slot;
	GtkWidget *widget;

	widget = gtk_notebook_get_nth_page (GTK_NOTEBOOK (pane->notebook), page_num);
	g_assert (widget != NULL);

	/* find slot corresponding to the target page */
	slot = NAUTILUS_WINDOW_SLOT (widget);
	g_assert (slot != NULL);

	nautilus_window_set_active_slot (nautilus_window_slot_get_window (slot),
					 slot);

	return FALSE;
}

static void
action_show_hide_search_callback (GtkAction *action,
				  gpointer user_data)
{
	NautilusWindowPane *pane = user_data;
	NautilusWindow *window = pane->window;

	if (gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (action))) {
		nautilus_window_pane_ensure_search_bar (pane);
	} else {
		NautilusWindowSlot *slot;
		GFile *location = NULL;

		slot = pane->active_slot;
		nautilus_window_pane_hide_search_bar (pane);

		/* Use the location bar as the return location */
		if (slot->query_editor != NULL) {
			NautilusQuery *query;
			char *uri;

			query = nautilus_query_editor_get_query (slot->query_editor);
			if (query != NULL) {
				uri = nautilus_query_get_location (query);
				if (uri != NULL) {
					location = g_file_new_for_uri (uri);
					g_free (uri);
				}
				g_object_unref (query);
			}

			/* Last try: use the home directory as the return location */
			if (location == NULL) {
				location = g_file_new_for_path (g_get_home_dir ());
			}

			nautilus_window_go_to (window, location);
			g_object_unref (location);
		}
	}
}

static void
setup_search_action (NautilusWindowPane *pane)
{
	GtkActionGroup *group = pane->action_group;
	GtkAction *action;

	action = gtk_action_group_get_action (group, NAUTILUS_ACTION_SEARCH);
	g_signal_connect (action, "activate",
			  G_CALLBACK (action_show_hide_search_callback), pane);
}

static void
toolbar_action_group_activated_callback (GtkActionGroup *action_group,
					 GtkAction *action,
					 gpointer user_data)
{
	NautilusWindowPane *pane = user_data;
	nautilus_window_set_active_pane (pane->window, pane);
}

static void
nautilus_window_pane_set_property (GObject *object,
				   guint arg_id,
				   const GValue *value,
				   GParamSpec *pspec)
{
	NautilusWindowPane *self = NAUTILUS_WINDOW_PANE (object);

	switch (arg_id) {
	case PROP_WINDOW:
		self->window = g_value_get_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, arg_id, pspec);
		break;
	}
}

static void
nautilus_window_pane_get_property (GObject *object,
				   guint arg_id,
				   GValue *value,
				   GParamSpec *pspec)
{
	NautilusWindowPane *self = NAUTILUS_WINDOW_PANE (object);

	switch (arg_id) {
	case PROP_WINDOW:
		g_value_set_object (value, self->window);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, arg_id, pspec);
		break;
	}
}

static void
nautilus_window_pane_dispose (GObject *object)
{
	NautilusWindowPane *pane = NAUTILUS_WINDOW_PANE (object);

	unset_focus_widget (pane);

	pane->window = NULL;
	g_clear_object (&pane->action_group);

	g_assert (pane->slots == NULL);

	G_OBJECT_CLASS (nautilus_window_pane_parent_class)->dispose (object);
}

static void
nautilus_window_pane_constructed (GObject *obj)
{
	NautilusWindowPane *pane = NAUTILUS_WINDOW_PANE (obj);
	GtkSizeGroup *header_size_group;
	NautilusWindow *window;
	GtkActionGroup *action_group;

	G_OBJECT_CLASS (nautilus_window_pane_parent_class)->constructed (obj);

	window = pane->window;

	header_size_group = gtk_size_group_new (GTK_SIZE_GROUP_VERTICAL);
	gtk_size_group_set_ignore_hidden (header_size_group, FALSE);

	/* build the toolbar */
	action_group = nautilus_window_create_toolbar_action_group (window);
	pane->tool_bar = nautilus_toolbar_new (action_group);
	g_object_bind_property (window, "disable-chrome",
				pane->tool_bar, "visible",
				G_BINDING_INVERT_BOOLEAN);
	pane->action_group = action_group;

	setup_search_action (pane);
	g_signal_connect (pane->action_group, "pre-activate",
			  G_CALLBACK (toolbar_action_group_activated_callback), pane);

	gtk_box_pack_start (GTK_BOX (pane),
			    pane->tool_bar,
			    FALSE, FALSE, 0);

	/* start as non-active */
	nautilus_window_pane_set_active (pane, FALSE);

	g_settings_bind_with_mapping (nautilus_window_state,
				      NAUTILUS_WINDOW_STATE_START_WITH_TOOLBAR,
				      pane->tool_bar,
				      "visible",
				      G_SETTINGS_BIND_GET,
				      nautilus_window_disable_chrome_mapping, NULL,
				      window, NULL);

	/* connect to the pathbar signals */
	pane->path_bar = nautilus_toolbar_get_path_bar (NAUTILUS_TOOLBAR (pane->tool_bar));
	gtk_size_group_add_widget (header_size_group, pane->path_bar);

	g_signal_connect_object (pane->path_bar, "path-clicked",
				 G_CALLBACK (path_bar_location_changed_callback), pane, 0);
	g_signal_connect_object (pane->path_bar, "path-event",
				 G_CALLBACK (path_bar_path_event_callback), pane, 0);

	/* connect to the location bar signals */
	pane->location_bar = nautilus_toolbar_get_location_bar (NAUTILUS_TOOLBAR (pane->tool_bar));
	gtk_size_group_add_widget (header_size_group, pane->location_bar);

	nautilus_clipboard_set_up_editable
		(GTK_EDITABLE (nautilus_location_bar_get_entry (NAUTILUS_LOCATION_BAR (pane->location_bar))),
		 nautilus_window_get_ui_manager (NAUTILUS_WINDOW (window)),
		 TRUE);

	g_signal_connect_object (pane->location_bar, "location-changed",
				 G_CALLBACK (navigation_bar_location_changed_callback), pane, 0);
	g_signal_connect_object (pane->location_bar, "cancel",
				 G_CALLBACK (navigation_bar_cancel_callback), pane, 0);
	g_signal_connect_object (nautilus_location_bar_get_entry (NAUTILUS_LOCATION_BAR (pane->location_bar)), "focus-in-event",
				 G_CALLBACK (toolbar_focus_in_callback), pane, 0);

	/* connect to the search bar signals */
	pane->search_bar = nautilus_toolbar_get_search_bar (NAUTILUS_TOOLBAR (pane->tool_bar));
	gtk_size_group_add_widget (header_size_group, pane->search_bar);

	g_signal_connect_object (pane->search_bar, "activate",
				 G_CALLBACK (search_bar_activate_callback), pane, 0);
	g_signal_connect_object (pane->search_bar, "cancel",
				 G_CALLBACK (search_bar_cancel_callback), pane, 0);
	g_signal_connect_object (nautilus_search_bar_get_entry (NAUTILUS_SEARCH_BAR (pane->search_bar)), "focus-in-event",
				 G_CALLBACK (toolbar_focus_in_callback), pane, 0);

	/* initialize the notebook */
	pane->notebook = g_object_new (NAUTILUS_TYPE_NOTEBOOK, NULL);
	gtk_box_pack_start (GTK_BOX (pane), pane->notebook,
			    TRUE, TRUE, 0);
	g_signal_connect (pane->notebook,
			  "tab-close-request",
			  G_CALLBACK (notebook_tab_close_requested),
			  pane);
	g_signal_connect_after (pane->notebook,
				"button_press_event",
				G_CALLBACK (notebook_button_press_cb),
				pane);
	g_signal_connect (pane->notebook, "popup-menu",
			  G_CALLBACK (notebook_popup_menu_cb),
			  pane);
	g_signal_connect (pane->notebook,
			  "switch-page",
			  G_CALLBACK (notebook_switch_page_cb),
			  pane);

	gtk_notebook_set_show_tabs (GTK_NOTEBOOK (pane->notebook), FALSE);
	gtk_notebook_set_show_border (GTK_NOTEBOOK (pane->notebook), FALSE);
	gtk_widget_show (pane->notebook);
	gtk_container_set_border_width (GTK_CONTAINER (pane->notebook), 0);

	/* Ensure that the view has some minimal size and that other parts
	 * of the UI (like location bar and tabs) don't request more and
	 * thus affect the default position of the split view paned.
	 */
	gtk_widget_set_size_request (GTK_WIDGET (pane), 60, 60);

	/* we can unref the size group now */
	g_object_unref (header_size_group);
}

static void
nautilus_window_pane_class_init (NautilusWindowPaneClass *klass)
{
	GObjectClass *oclass = G_OBJECT_CLASS (klass);

	oclass->constructed = nautilus_window_pane_constructed;
	oclass->dispose = nautilus_window_pane_dispose;
	oclass->set_property = nautilus_window_pane_set_property;
	oclass->get_property = nautilus_window_pane_get_property;

	properties[PROP_WINDOW] =
		g_param_spec_object ("window",
				     "The NautilusWindow",
				     "The parent NautilusWindow",
				     NAUTILUS_TYPE_WINDOW,
				     G_PARAM_READWRITE |
				     G_PARAM_CONSTRUCT_ONLY |
				     G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (oclass, NUM_PROPERTIES, properties);
}

static void
nautilus_window_pane_init (NautilusWindowPane *pane)
{
	pane->slots = NULL;
	pane->active_slot = NULL;

	gtk_orientable_set_orientation (GTK_ORIENTABLE (pane), GTK_ORIENTATION_VERTICAL);
}

NautilusWindowPane *
nautilus_window_pane_new (NautilusWindow *window)
{
	return g_object_new (NAUTILUS_TYPE_WINDOW_PANE,
			     "window", window,
			     NULL);
}

static void
nautilus_window_pane_set_active_style (NautilusWindowPane *pane,
				       gboolean is_active)
{
	GtkStyleContext *style;
	gboolean has_inactive;

	style = gtk_widget_get_style_context (GTK_WIDGET (pane));
	has_inactive = gtk_style_context_has_class (style, "nautilus-inactive-pane");

	if (has_inactive == !is_active) {
		return;
	}

	if (is_active) {
		gtk_style_context_remove_class (style, "nautilus-inactive-pane");
	} else {
		gtk_style_context_add_class (style, "nautilus-inactive-pane");
	}

	gtk_widget_reset_style (GTK_WIDGET (pane));
}

void
nautilus_window_pane_set_active (NautilusWindowPane *pane,
				 gboolean is_active)
{
	NautilusNavigationState *nav_state;

	if (is_active) {
		nav_state = nautilus_window_get_navigation_state (pane->window);
		nautilus_navigation_state_set_master (nav_state, pane->action_group);
	}

	/* pane inactive style */
	nautilus_window_pane_set_active_style (pane, is_active);
}

void
nautilus_window_pane_sync_location_widgets (NautilusWindowPane *pane)
{
	NautilusWindowSlot *slot, *active_slot;
	NautilusNavigationState *nav_state;

	slot = pane->active_slot;

	nautilus_window_pane_hide_temporary_bars (pane);

	/* Change the location bar and path bar to match the current location. */
	if (slot->location != NULL) {
		char *uri;

		/* this may be NULL if we just created the slot */
		uri = nautilus_window_slot_get_location_uri (slot);
		nautilus_location_bar_set_location (NAUTILUS_LOCATION_BAR (pane->location_bar), uri);
		g_free (uri);
		nautilus_path_bar_set_path (NAUTILUS_PATH_BAR (pane->path_bar), slot->location);
	}

	/* Update window global UI if this is the active pane */
	if (pane == nautilus_window_get_active_pane (pane->window)) {
		nautilus_window_sync_up_button (pane->window);

		/* Check if the back and forward buttons need enabling or disabling. */
		active_slot = nautilus_window_get_active_slot (pane->window);
		nav_state = nautilus_window_get_navigation_state (pane->window);

		nautilus_navigation_state_set_boolean (nav_state,
						       NAUTILUS_ACTION_BACK,
						       active_slot->back_list != NULL);
		nautilus_navigation_state_set_boolean (nav_state,
						       NAUTILUS_ACTION_FORWARD,
						       active_slot->forward_list != NULL);
	}
}

static void
toggle_toolbar_search_button (NautilusWindowPane *pane)
{
	GtkActionGroup *group;
	GtkAction *action;

	group = pane->action_group;
	action = gtk_action_group_get_action (group, NAUTILUS_ACTION_SEARCH);

	g_signal_handlers_block_by_func (action,
					 action_show_hide_search_callback, pane);
	gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action), TRUE);
	g_signal_handlers_unblock_by_func (action,
					   action_show_hide_search_callback, pane);	
}

void
nautilus_window_pane_sync_search_widgets (NautilusWindowPane *pane)
{
	NautilusWindowSlot *slot;
	NautilusDirectory *directory;
	NautilusSearchDirectory *search_directory;

	slot = pane->active_slot;
	search_directory = NULL;

	directory = nautilus_directory_get (slot->location);
	if (NAUTILUS_IS_SEARCH_DIRECTORY (directory)) {
		search_directory = NAUTILUS_SEARCH_DIRECTORY (directory);
	}

	if (search_directory != NULL) {
		if (!nautilus_search_directory_is_saved_search (search_directory)) {
			nautilus_toolbar_set_show_search_bar (NAUTILUS_TOOLBAR (pane->tool_bar), TRUE);
			pane->temporary_search_bar = FALSE;
		} else {
			toggle_toolbar_search_button (pane);
		}
	} else {
		search_bar_cancel_callback (pane->search_bar, pane);
	}

	nautilus_directory_unref (directory);
}

void
nautilus_window_pane_slot_close (NautilusWindowPane *pane,
				 NautilusWindowSlot *slot)
{
	NautilusWindowSlot *next_slot;

	DEBUG ("Requesting to remove slot %p from pane %p", slot, pane);

	if (pane->window) {
		NautilusWindow *window;

		window = pane->window;

		if (pane->active_slot == slot) {
			next_slot = get_first_inactive_slot (NAUTILUS_WINDOW_PANE (pane));
			nautilus_window_set_active_slot (window, next_slot);
		}

		nautilus_window_pane_close_slot (pane, slot);

		/* If that was the last slot in the pane, close the pane or even the whole window. */
		if (pane->slots == NULL) {
			if (nautilus_window_split_view_showing (window)) {
				NautilusWindowPane *new_pane;

				DEBUG ("Last slot removed from the pane %p, closing it", pane);
				nautilus_window_close_pane (window, pane);

				new_pane = g_list_nth_data (window->details->panes, 0);

				if (new_pane->active_slot == NULL) {
					new_pane->active_slot = get_first_inactive_slot (new_pane);
				}

				DEBUG ("Calling set_active_pane, new slot %p", new_pane->active_slot);
				nautilus_window_set_active_pane (window, new_pane);
				nautilus_window_update_show_hide_menu_items (window);
			} else {
				DEBUG ("Last slot removed from the last pane, close the window");
				nautilus_window_close (window);
			}
		}
	}
}

void
nautilus_window_pane_grab_focus (NautilusWindowPane *pane)
{
	if (NAUTILUS_IS_WINDOW_PANE (pane) && pane->active_slot) {
		nautilus_view_grab_focus (pane->active_slot->content_view);
	}	
}

void
nautilus_window_pane_ensure_location_bar (NautilusWindowPane *pane)
{
	remember_focus_widget (pane);

	nautilus_toolbar_set_show_main_bar (NAUTILUS_TOOLBAR (pane->tool_bar), TRUE);
	nautilus_toolbar_set_show_location_entry (NAUTILUS_TOOLBAR (pane->tool_bar), TRUE);

	if (!g_settings_get_boolean (nautilus_window_state,
				     NAUTILUS_WINDOW_STATE_START_WITH_TOOLBAR)) {
		gtk_widget_show (pane->tool_bar);
		pane->temporary_navigation_bar = TRUE;
	}

	nautilus_location_bar_activate
		(NAUTILUS_LOCATION_BAR (pane->location_bar));
}

void
nautilus_window_pane_close_slot (NautilusWindowPane *pane,
				 NautilusWindowSlot *slot)
{
	int page_num;
	GtkNotebook *notebook;

	g_assert (NAUTILUS_IS_WINDOW_SLOT (slot));
	g_assert (NAUTILUS_IS_WINDOW_PANE (slot->pane));

	DEBUG ("Closing slot %p", slot);

	/* save pane because slot is not valid anymore after this call */
	pane = slot->pane;
	notebook = GTK_NOTEBOOK (pane->notebook);

	nautilus_window_manage_views_close_slot (slot);

	page_num = gtk_notebook_page_num (notebook, GTK_WIDGET (slot));
	g_assert (page_num >= 0);

	g_signal_handlers_block_by_func (notebook,
					 G_CALLBACK (notebook_switch_page_cb),
					 pane);
	/* this will call gtk_widget_destroy on the slot */
	gtk_notebook_remove_page (notebook, page_num);
	g_signal_handlers_unblock_by_func (notebook,
					   G_CALLBACK (notebook_switch_page_cb),
					   pane);

	gtk_notebook_set_show_tabs (notebook,
				    gtk_notebook_get_n_pages (notebook) > 1);
	pane->slots = g_list_remove (pane->slots, slot);
}

NautilusWindowSlot *
nautilus_window_pane_open_slot (NautilusWindowPane *pane,
				NautilusWindowOpenSlotFlags flags)
{
	NautilusWindowSlot *slot;

	g_assert (NAUTILUS_IS_WINDOW_PANE (pane));
	g_assert (NAUTILUS_IS_WINDOW (pane->window));

	slot = nautilus_window_slot_new (pane);

	g_signal_handlers_block_by_func (pane->notebook,
					 G_CALLBACK (notebook_switch_page_cb),
					 pane);
	nautilus_notebook_add_tab (NAUTILUS_NOTEBOOK (pane->notebook),
				   slot,
				   (flags & NAUTILUS_WINDOW_OPEN_SLOT_APPEND) != 0 ?
				   -1 :
				   gtk_notebook_get_current_page (GTK_NOTEBOOK (pane->notebook)) + 1,
				   FALSE);
	g_signal_handlers_unblock_by_func (pane->notebook,
					   G_CALLBACK (notebook_switch_page_cb),
					   pane);

	pane->slots = g_list_append (pane->slots, slot);

	return slot;
}
