/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */

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
 *  Author: Darin Adler <darin@bentspoon.com>
 *
 */

#ifndef NAUTILUS_WINDOW_MANAGE_VIEWS_H
#define NAUTILUS_WINDOW_MANAGE_VIEWS_H

#include "nautilus-window.h"
#include "nautilus-navigation-window.h"

struct _NautilusWindowPane;

void nautilus_window_manage_views_close_slot (struct _NautilusWindowPane *pane,
					      NautilusWindowSlot         *slot);


/* NautilusWindowInfo implementation: */
void nautilus_window_report_load_underway     (NautilusWindow     *window,
                                               NautilusView       *view);
void nautilus_window_report_selection_changed (NautilusWindowInfo *window);
void nautilus_window_report_view_failed       (NautilusWindow     *window,
                                               NautilusView       *view);
void nautilus_window_report_load_complete     (NautilusWindow     *window,
                                               NautilusView       *view);
void nautilus_window_report_location_change   (NautilusWindow     *window);

#endif /* NAUTILUS_WINDOW_MANAGE_VIEWS_H */
