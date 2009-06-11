/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* NautilusBorder: Draw a colored border around a widget.
 *
 * Copyright (C) 2009 Holger Berndt <berndth@gmx.de>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef NAUTILUS_BORDER_H
#define NAUTILUS_BORDER_H

#include <gtk/gtkframe.h>

G_BEGIN_DECLS

#define NAUTILUS_TYPE_BORDER            (nautilus_border_get_type ())
#define NAUTILUS_BORDER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), NAUTILUS_TYPE_BORDER, NautilusBorderClass))
#define NAUTILUS_BORDER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), NAUTILUS_TYPE_BORDER, NautilusBorder))
#define NAUTILUS_IS_BORDER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), NAUTILUS_TYPE_BORDER))
#define NAUTILUS_IS_BORDER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),	NAUTILUS_TYPE_BORDER))

typedef struct NautilusBorderDetails NautilusBorderDetails;

typedef struct {
	GtkFrame parent;
	NautilusBorderDetails *details;
} NautilusBorder;

typedef struct {
	GtkFrameClass parent_class;
} NautilusBorderClass;

GType       nautilus_border_get_type (void);
GtkWidget * nautilus_border_new      (void);

void nautilus_border_set_active_appearance (NautilusBorder *border, gboolean is_active);

G_END_DECLS

#endif /* NAUTILUS_BORDER_H */
