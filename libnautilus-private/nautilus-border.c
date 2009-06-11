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

#include <config.h>
#include "nautilus-border.h"
#include <eel/eel-gtk-macros.h>

struct NautilusBorderDetails {
	gboolean is_active;
};

static void nautilus_border_init      (NautilusBorder      *border);
static void nautilus_border_class_init (NautilusBorderClass *class);

static GtkFrameClass *parent_class = NULL;

static void
draw_colored_border (GtkWidget *widget, GdkRectangle *area)
{
	GtkFrame *frame;
	GdkWindow *window;
	GdkGC *gc;
	
	frame = GTK_FRAME (widget);
	window = widget->window;

	gint x, y, width, height;
	x = frame->child_allocation.x - widget->style->xthickness;
	y = frame->child_allocation.y - widget->style->ythickness;
	width = frame->child_allocation.width + 2 * widget->style->xthickness;
	height =  frame->child_allocation.height + 2 * widget->style->ythickness;

	/* sanitize size */
	if ((width == -1) && (height == -1)) {
		gdk_drawable_get_size (window, &width, &height);
	}
	else if (width == -1) {
		gdk_drawable_get_size (window, &width, NULL);
	}
	else if (height == -1) {
		gdk_drawable_get_size (window, NULL, &height);
	}

	gc = widget->style->bg_gc[GTK_STATE_SELECTED];
	if (area) {
		gdk_gc_set_clip_rectangle (gc, area);
	}

	gdk_draw_rectangle (window, gc, FALSE, x+1, y+1, width-2, height-2);
}

static gboolean
nautilus_border_expose (GtkWidget *widget, GdkEventExpose *event)
{
	if (GTK_WIDGET_DRAWABLE (widget)) {
		NautilusBorder *border;
		border = NAUTILUS_BORDER (widget);
		if (border->details->is_active) {
			draw_colored_border (widget, &event->area);
		}
		GTK_WIDGET_CLASS (parent_class)->expose_event (widget, event);
	}

	return TRUE;
}

static void
nautilus_border_init (NautilusBorder *border)
{
	GtkWidget *widget;

	widget = GTK_WIDGET (border);
	border->details = g_new0 (NautilusBorderDetails, 1);
	
	nautilus_border_set_active_appearance (border, TRUE);
}

GtkWidget *
nautilus_border_new (void)
{
	return gtk_widget_new (NAUTILUS_TYPE_BORDER, NULL);
}

static void
nautilus_border_finalize (GObject *object)
{
	NautilusBorder *border;

	border = NAUTILUS_BORDER (object);
	g_free (border->details);
	EEL_CALL_PARENT (G_OBJECT_CLASS, finalize, (object));
}

static void
nautilus_border_class_init (NautilusBorderClass *class)
{
	GObjectClass *gobject_class;
	GtkWidgetClass *widget_class;

	parent_class = g_type_class_peek_parent (class);

	gobject_class = G_OBJECT_CLASS (class);
	widget_class = GTK_WIDGET_CLASS (class);

	gobject_class->finalize = nautilus_border_finalize;
	widget_class->expose_event = nautilus_border_expose;
}

GType
nautilus_border_get_type (void)
{
	static GType border_type = 0;

	if (border_type == 0) {						
		const GTypeInfo object_info = {
		    sizeof (NautilusBorderClass),
		    NULL,		/* base_init */
		    NULL,		/* base_finalize */
		    (GClassInitFunc) nautilus_border_class_init,
		    NULL,		/* class_finalize */		
		    NULL,               /* class_data */		
		    sizeof (NautilusBorder),
		    0,                  /* n_preallocs */		
		    (GInstanceInitFunc) nautilus_border_init
		};							
		border_type = g_type_register_static (
			GTK_TYPE_FRAME, "NautilusBorder",
			&object_info, 0);
	}

	return border_type;
}

void
nautilus_border_set_active_appearance (NautilusBorder *border, gboolean is_active)
{
	g_return_if_fail (NAUTILUS_IS_BORDER (border));

	border->details->is_active = is_active;

	if (!is_active) {
		gtk_frame_set_shadow_type (GTK_FRAME (border), GTK_SHADOW_ETCHED_IN);
	}
	else {
		gtk_frame_set_shadow_type (GTK_FRAME (border), GTK_SHADOW_NONE);
	}
}
