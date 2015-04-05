/*
 * ROX-Filer, filer for the ROX desktop project
 * Copyright (C) 2006, Thomas Leonard and others (see changelog for details).
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA  02111-1307  USA
 */

/* view_collection.c - a subclass of Collection, used for displaying files */

#include "config.h"

#include <gtk/gtk.h>
#include <time.h>
#include <math.h>
#include <string.h>

#include "global.h"

#include "collection.h"
#include "view_iface.h"
#include "view_collection.h"
#include "type.h"
#include "pixmaps.h"
#include "dir.h"
#include "diritem.h"
#include "gui_support.h"
#include "support.h"
#include "dnd.h"
#include "bind.h"
#include "options.h"

#include "toolbar.h"	/* for resizing */
#include "filer.h"
#include "display.h"

#define MIN_ITEM_WIDTH 64

static gpointer parent_class = NULL;

struct _ViewCollectionClass {
	GtkViewportClass parent;
};

typedef struct _Template Template;

struct _Template {
	GdkRectangle	icon;
	GdkRectangle	leafname;
	GdkRectangle	details;
};

/* GC for drawing colour filenames */
static GdkGC	*type_gc = NULL;

/* Static prototypes */
static void view_collection_finialize(GObject *object);
static void view_collection_class_init(gpointer gclass, gpointer data);
static void view_collection_init(GTypeInstance *object, gpointer gclass);

static void draw_item(GtkWidget *widget,
			CollectionItem *item,
			GdkRectangle *area,
			gpointer user_data);
static void fill_template(GdkRectangle *area, CollectionItem *item,
			ViewCollection *view_collection, Template *template);
static void huge_template(GdkRectangle *area, CollectionItem *colitem,
			   ViewCollection *view_collection, Template *template);
static void large_template(GdkRectangle *area, CollectionItem *colitem,
			   ViewCollection *view_collection, Template *template);
static void small_template(GdkRectangle *area, CollectionItem *colitem,
			   ViewCollection *view_collection, Template *template);
static void huge_full_template(GdkRectangle *area, CollectionItem *colitem,
			   ViewCollection *view_collection, Template *template);
static void large_full_template(GdkRectangle *area, CollectionItem *colitem,
			   ViewCollection *view_collection, Template *template);
static void small_full_template(GdkRectangle *area, CollectionItem *colitem,
			   ViewCollection *view_collection, Template *template);
static gboolean test_point(Collection *collection,
				int point_x, int point_y,
				CollectionItem *item,
				int width, int height,
				gpointer user_data);
static void draw_string(GtkWidget *widget,
		PangoLayout *layout,
		GdkRectangle *area,	/* Area available on screen */
		int 	width,		/* Width of the full string */
		GtkStateType selection_state,
		gboolean box);
static void view_collection_iface_init(gpointer giface, gpointer iface_data);
static gint coll_motion_notify(GtkWidget *widget,
			       GdkEventMotion *event,
			       ViewCollection *view_collection);
static gint coll_button_release(GtkWidget *widget,
			        GdkEventButton *event,
			        ViewCollection *view_collection);
static gint coll_button_press(GtkWidget *widget,
			      GdkEventButton *event,
			      ViewCollection *view_collection);
static void size_allocate(GtkWidget *w, GtkAllocation *a, gpointer data);
static void style_set(Collection 	*collection,
		      GtkStyle		*style,
		      ViewCollection	*view_collection);
static void display_free_colitem(Collection *collection,
				 CollectionItem *colitem);
static void lost_selection(Collection  *collection,
			   guint        time,
			   gpointer     user_data);
static void selection_changed(Collection *collection,
			      gint time,
			      gpointer user_data);
static void calc_size(FilerWindow *filer_window, CollectionItem *colitem,
		int *width, int *height);
static void make_iter(ViewCollection *view_collection, ViewIter *iter,
		      IterFlags flags);
static void make_item_iter(ViewCollection *vc, ViewIter *iter, int i);

static void view_collection_sort(ViewIface *view);
static void view_collection_style_changed(ViewIface *view, int flags);
static void view_collection_add_items(ViewIface *view, GPtrArray *items);
static void view_collection_update_items(ViewIface *view, GPtrArray *items);
static void view_collection_delete_if(ViewIface *view,
			  gboolean (*test)(gpointer item, gpointer data),
			  gpointer data);
static void view_collection_clear(ViewIface *view);
static void view_collection_select_all(ViewIface *view);
static void view_collection_clear_selection(ViewIface *view);
static int view_collection_count_items(ViewIface *view);
static int view_collection_count_selected(ViewIface *view);
static void view_collection_show_cursor(ViewIface *view);
static void view_collection_get_iter(ViewIface *view,
				     ViewIter *iter, IterFlags flags);
static void view_collection_get_iter_at_point(ViewIface *view, ViewIter *iter,
					      GdkWindow *src, int x, int y);
static void view_collection_cursor_to_iter(ViewIface *view, ViewIter *iter);
static void view_collection_set_selected(ViewIface *view,
					 ViewIter *iter,
					 gboolean selected);
static gboolean view_collection_get_selected(ViewIface *view, ViewIter *iter);
static void view_collection_select_only(ViewIface *view, ViewIter *iter);
static void view_collection_set_frozen(ViewIface *view, gboolean frozen);
static void view_collection_wink_item(ViewIface *view, ViewIter *iter);
static void view_collection_autosize(ViewIface *view);
static gboolean view_collection_cursor_visible(ViewIface *view);
static void view_collection_set_base(ViewIface *view, ViewIter *iter);
static void view_collection_start_lasso_box(ViewIface *view,
					     GdkEventButton *event);
static void view_collection_extend_tip(ViewIface *view, ViewIter *iter,
					GString *tip);
static gboolean view_collection_auto_scroll_callback(ViewIface *view);

static DirItem *iter_next(ViewIter *iter);
static DirItem *iter_prev(ViewIter *iter);
static DirItem *iter_peek(ViewIter *iter);


/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/

GtkWidget *view_collection_new(FilerWindow *filer_window)
{
	ViewCollection *view_collection;

	view_collection = g_object_new(view_collection_get_type(), NULL);
	view_collection->filer_window = filer_window;

	/* Starting with GTK+-2.2.2, the vadjustment is reset after init
	 * (even though it's already set during init) to a new adjustment.
	 * Change it back:
	 */
	gtk_viewport_set_vadjustment(GTK_VIEWPORT(view_collection),
				 view_collection->collection->vadj);
				 
	gtk_range_set_adjustment(GTK_RANGE(filer_window->scrollbar),
				 view_collection->collection->vadj);

	return GTK_WIDGET(view_collection);
}

GType view_collection_get_type(void)
{
	static GType type = 0;

	if (!type)
	{
		static const GTypeInfo info =
		{
			sizeof (ViewCollectionClass),
			NULL,			/* base_init */
			NULL,			/* base_finalise */
			view_collection_class_init,
			NULL,			/* class_finalise */
			NULL,			/* class_data */
			sizeof(ViewCollection),
			0,			/* n_preallocs */
			view_collection_init
		};
		static const GInterfaceInfo iface_info =
		{
			view_collection_iface_init, NULL, NULL
		};

		type = g_type_register_static(gtk_viewport_get_type(),
						"ViewCollection", &info, 0);
		g_type_add_interface_static(type, VIEW_TYPE_IFACE, &iface_info);
	}

	return type;
}

/****************************************************************
 *			INTERNAL FUNCTIONS			*
 ****************************************************************/

static void view_collection_destroy(GtkObject *view_collection)
{
	VIEW_COLLECTION(view_collection)->filer_window = NULL;
}

static void view_collection_finialize(GObject *object)
{
	/* ViewCollection *view_collection = (ViewCollection *) object; */

	G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void view_collection_grab_focus(GtkWidget *focus_widget)
{
	ViewCollection *view_collection = VIEW_COLLECTION(focus_widget);
	gtk_widget_grab_focus(GTK_WIDGET(view_collection->collection));
}

static void view_collection_class_init(gpointer gclass, gpointer data)
{
	GObjectClass *object = (GObjectClass *) gclass;
	GtkWidgetClass *widget = (GtkWidgetClass *) gclass;

	parent_class = g_type_class_peek_parent(gclass);

	widget->grab_focus = view_collection_grab_focus;

	object->finalize = view_collection_finialize;
	GTK_OBJECT_CLASS(object)->destroy = view_collection_destroy;
}

static gboolean transparent_expose(GtkWidget *widget, GdkEventExpose *event)
{
	cairo_t *cr = gdk_cairo_create(widget->window);
	GdkColor fg = widget->style->fg[GTK_STATE_NORMAL];
	GdkRectangle *rects;
	int i, n_rects;
	static const float p = 65535.0;

	cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
	gdk_cairo_region(cr, event->region);
	cairo_paint_with_alpha(cr, o_view_alpha.int_value / 100.0);

	cairo_new_path(cr);
	cairo_set_line_width(cr, 1.0);
	cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
	cairo_set_source_rgba(cr,
			fg.red / p,
			fg.green / p,
			fg.blue / p,
			(100 - o_view_alpha.int_value) / 100.0);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
	cairo_set_tolerance(cr, 0.1);

	gdk_region_get_rectangles(event->region, &rects, &n_rects);
	for (i = 0; i < n_rects; i++)
	{
		if (rects[i].x > 4)
			continue;

		cairo_move_to(cr, 4, rects[i].y);
		cairo_line_to(cr, 4, rects[i].y + rects[i].height);
		cairo_move_to(cr, 2, rects[i].y);
		cairo_line_to(cr, 2, rects[i].y + rects[i].height);
		cairo_move_to(cr, 1, rects[i].y);
		cairo_line_to(cr, 1, rects[i].y + rects[i].height);
	}
	cairo_stroke(cr);
	g_free(rects);

	cairo_destroy(cr);
	return FALSE;
}
static void view_collection_init(GTypeInstance *object, gpointer gclass)
{
	ViewCollection *view_collection = (ViewCollection *) object;
	GtkViewport *viewport = (GtkViewport *) object;
	GtkWidget *collection;
	GtkAdjustment *adj;

	collection = collection_new();

	if (o_view_alpha.int_value > 0)
	{
		GdkScreen *screen = gtk_widget_get_screen(collection);
		GdkColormap *rgba = gdk_screen_get_rgba_colormap(screen);
		if (rgba)
		{
			gtk_widget_set_colormap(collection, rgba);
			g_signal_connect(collection, "expose-event",
						G_CALLBACK(transparent_expose), NULL);
		}
	}

	view_collection->collection = COLLECTION(collection);

	adj = view_collection->collection->vadj;
	gtk_viewport_set_vadjustment(viewport, adj);
	gtk_viewport_set_hadjustment(viewport, NULL); /* Or Gtk will crash */
	gtk_viewport_set_shadow_type(viewport, GTK_SHADOW_NONE);
	gtk_container_add(GTK_CONTAINER(object), collection);
	gtk_widget_show(collection);
	gtk_widget_set_size_request(GTK_WIDGET(view_collection), 4, 4);

	gtk_container_set_resize_mode(GTK_CONTAINER(viewport),
			GTK_RESIZE_IMMEDIATE);

	view_collection->collection->free_item = display_free_colitem;
	view_collection->collection->draw_item = draw_item;
	view_collection->collection->test_point = test_point;
	view_collection->collection->cb_user_data = view_collection;

	g_signal_connect(collection, "style_set",
			G_CALLBACK(style_set),
			view_collection);

	g_signal_connect(collection, "lose_selection",
			G_CALLBACK(lost_selection), view_collection);
	g_signal_connect(collection, "selection_changed",
			G_CALLBACK(selection_changed), view_collection);

	g_signal_connect(collection, "button-release-event",
			G_CALLBACK(coll_button_release), view_collection);
	g_signal_connect(collection, "button-press-event",
			G_CALLBACK(coll_button_press), view_collection);
	g_signal_connect(collection, "motion-notify-event",
			G_CALLBACK(coll_motion_notify), view_collection);
	g_signal_connect(viewport, "size-allocate",
			G_CALLBACK(size_allocate), view_collection);

	gtk_widget_set_events(collection,
			GDK_BUTTON1_MOTION_MASK | GDK_BUTTON2_MOTION_MASK |
			GDK_BUTTON3_MOTION_MASK | GDK_POINTER_MOTION_MASK |
			GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);
}


static void draw_item(GtkWidget *widget,
			CollectionItem *colitem,
			GdkRectangle *area,
			gpointer user_data)
{
	DirItem		*item = (DirItem *) colitem->data;
	gboolean	selected = colitem->selected;
	Template	template;
	ViewData *view = (ViewData *) colitem->view_data;
	ViewCollection	*view_collection = (ViewCollection *) user_data;
	FilerWindow	*filer_window = view_collection->filer_window;
	GtkStateType	selection_state;
	GdkColor	*color;

	g_return_if_fail(view != NULL);

	if (selected)
		selection_state = filer_window->selection_state;
	else
		selection_state = GTK_STATE_NORMAL;

	color = &widget->style->base[selection_state];
	
	fill_template(area, colitem, view_collection, &template);
		
	/* Set up GC for coloured file types */
	if (!type_gc)
		type_gc = gdk_gc_new(widget->window);

	gdk_gc_set_foreground(type_gc, type_get_colour(item,
					&widget->style->text[GTK_STATE_NORMAL]));

	if (template.icon.width <= small_width &&
			template.icon.height <= font_height)
	{
		draw_small_icon(widget->window, widget->style, &template.icon,
				item, view->image, selected, color);
	}
	else if (template.icon.width <= ICON_WIDTH &&
			template.icon.height <= ICON_HEIGHT)
	{
		draw_large_icon(widget->window, widget->style, &template.icon,
				item, view->image, selected, color);
	}
	else
	{
		draw_huge_icon(filer_window, widget->window, widget->style, &template.icon,
				item, view->image, selected, color);
	}
	
	draw_string(widget, view->layout,
			&template.leafname,
			view->name_width,
			selection_state,
			TRUE);
	if (view->details)
		draw_string(widget, view->details,
				&template.details,
				template.details.width,
				selection_state,
				TRUE);
}

/* A template contains the locations of the three rectangles (for the icon,
 * name and extra details).
 * Fill in the empty 'template' with the rectanges for this item.
 */
static void fill_template(GdkRectangle *area, CollectionItem *colitem,
			ViewCollection *view_collection, Template *template)
{
	DisplayStyle	style = view_collection->filer_window->display_style;
	ViewData 	*view = (ViewData *) colitem->view_data;

	if (view->details)
	{
		template->details.width = view->details_width;
		template->details.height = view->details_height;

		if (style == SMALL_ICONS)
			small_full_template(area, colitem,
						view_collection, template);
		else if (style == LARGE_ICONS)
			large_full_template(area, colitem,
						view_collection, template);
		else
			huge_full_template(area, colitem,
						view_collection, template);
	}
	else
	{
		if (style == HUGE_ICONS)
			huge_template(area, colitem,
					view_collection, template);
		else if (style == LARGE_ICONS)
			large_template(area, colitem,
					view_collection, template);
		else
			small_template(area, colitem,
					view_collection, template);
	}
}

static void huge_template(GdkRectangle *area, CollectionItem *colitem,
			   ViewCollection *view_collection, Template *template)
{
	gfloat scale = view_collection->filer_window->icon_scale;
	int	col_width = view_collection->collection->item_width;
	int		text_x, text_y;
	ViewData	*view = (ViewData *) colitem->view_data;
	MaskedPixmap	*image = view->image;

	if (image)
	{
		if (!image->huge_pixbuf)
			pixmap_make_huge(image);

		if (image->huge_width <= ICON_WIDTH &&
			image->huge_height <= ICON_HEIGHT)
			scale = 1.0;

		template->icon.width = image->huge_width * scale;
		template->icon.height = image->huge_height * scale;
	}
	else
	{
		template->icon.width = HUGE_WIDTH * scale;
		template->icon.height = HUGE_HEIGHT * scale;
	}

	template->leafname.width = view->name_width;
	template->leafname.height = view->name_height;

	text_x = area->x + MAX((col_width - template->leafname.width) >> 1, 3);
	text_y = area->y + area->height - template->leafname.height;

	template->leafname.x = text_x;
	template->leafname.y = text_y;

	template->icon.x = area->x + ((col_width - template->icon.width) >> 1);
	template->icon.y = template->leafname.y - template->icon.height;
}

static void large_template(GdkRectangle *area, CollectionItem *colitem,
			   ViewCollection *view_collection, Template *template)
{
	int	col_width = view_collection->collection->item_width;
	int		iwidth, iheight;
	int		image_x;
	int		image_y;
	ViewData	*view = (ViewData *) colitem->view_data;
	MaskedPixmap	*image = view->image;
	
	int		text_x, text_y;
	
	if (image)
	{
		iwidth = MIN(image->width, ICON_WIDTH);
		iheight = MIN(image->height, ICON_HEIGHT);
	}
	else
	{
		iwidth = ICON_WIDTH;
		iheight = ICON_HEIGHT;
	}
	image_x = area->x + ((col_width - iwidth) >> 1);

	template->leafname.width = view->name_width;
	template->leafname.height = view->name_height;

	text_x = area->x + MAX((col_width - template->leafname.width) >> 1, 3);
	text_y = area->y + ICON_HEIGHT + 2;

	template->leafname.x = text_x;
	template->leafname.y = text_y;

	image_y = text_y - iheight;
	image_y = MAX(area->y, image_y);
	
	template->icon.x = image_x;
	template->icon.y = image_y;
	template->icon.width = iwidth;
	template->icon.height = MIN(ICON_HEIGHT, iheight);
}

static void small_template(GdkRectangle *area, CollectionItem *colitem,
			   ViewCollection *view_collection, Template *template)
{
	int	text_x = area->x + small_width + 4;
	int	low_text_y;
	int	max_text_width = area->width - small_width - 4;
	ViewData *view = (ViewData *) colitem->view_data;

	low_text_y = area->y + area->height / 2 - view->name_height / 2;

	template->leafname.x = text_x;
	template->leafname.y = low_text_y;
	template->leafname.width = MIN(max_text_width, view->name_width);
	template->leafname.height = view->name_height;
	
	template->icon.x = area->x + 2;
	template->icon.y = area->y + 1;
	template->icon.width = small_width;
	template->icon.height = font_height;
}

static void huge_full_template(GdkRectangle *area, CollectionItem *colitem,
			   ViewCollection *view_collection, Template *template)
{
	int	max_text_width;
	ViewData *view = (ViewData *) colitem->view_data;
	MaskedPixmap	*image = view->image;
	gfloat scale = view_collection->filer_window->icon_scale;

	if (image)
	{
		if (!image->huge_pixbuf)
			pixmap_make_huge(image);

		if (image->huge_width <= ICON_WIDTH &&
			image->huge_height <= ICON_HEIGHT)
			scale = 1.0;

		template->icon.width = image->huge_width * scale;
		template->icon.height = image->huge_height * scale;
	}
	else
	{
		template->icon.width = HUGE_WIDTH * scale;
		template->icon.height = HUGE_HEIGHT * scale;
	}

	max_text_width = area->width - HUGE_WIDTH * scale - 4;

	template->icon.x = area->x + (HUGE_WIDTH * scale - template->icon.width) / 2 + 2;
	template->icon.y = area->y + (area->height - template->icon.height) / 2 + 1;

	template->leafname.x = area->x + HUGE_WIDTH * scale + 4;
	template->leafname.y = area->y + area->height / 2
			- (view->name_height + 2 + view->details_height) / 2;
	template->leafname.width = MIN(max_text_width, view->name_width);
	template->leafname.height = view->name_height;

	if (!image)
		return;		/* Not scanned yet */

	template->details.x = template->leafname.x;
	template->details.y = template->leafname.y + view->name_height + 2;
}

static void large_full_template(GdkRectangle *area, CollectionItem *colitem,
			   ViewCollection *view_collection, Template *template)
{
	int	max_text_width = area->width - ICON_WIDTH - 4;
	ViewData *view = (ViewData *) colitem->view_data;
	MaskedPixmap *image = view->image;

	if (image)
	{
		template->icon.width = image->width;
		template->icon.height = image->height;
	}
	else
	{
		template->icon.width = ICON_WIDTH;
		template->icon.height = ICON_HEIGHT;
	}

	template->icon.x = area->x + (ICON_WIDTH - template->icon.width) / 2 + 2;
	template->icon.y = area->y + (area->height - template->icon.height) / 2 + 1;


	template->leafname.x = area->x + ICON_WIDTH + 4;
	template->leafname.y = area->y + area->height / 2
			- (view->name_height + 2 + view->details_height) / 2;
	template->leafname.width = MIN(max_text_width, view->name_width);
	template->leafname.height = view->name_height;

	if (!image)
		return;		/* Not scanned yet */

	template->details.x = template->leafname.x;
	template->details.y = template->leafname.y + view->name_height + 2;
}

static void small_full_template(GdkRectangle *area, CollectionItem *colitem,
			   ViewCollection *view_collection, Template *template)
{
	int	col_width = view_collection->collection->item_width;
	ViewData *view = (ViewData *) colitem->view_data;

	small_template(area, colitem, view_collection, template);

	if (!view->image)
		return;		/* Not scanned yet */

	template->icon.x = area->x + 2;
	template->icon.y = area->y + 1;
	template->details.x = area->x + col_width - template->details.width;
	template->details.y = area->y + area->height / 2 - \
				view->details_height / 2;
}

#define INSIDE(px, py, area)	\
	(px >= area.x && py >= area.y && \
	 px <= area.x + area.width && py <= area.y + area.height)

static gboolean test_point(Collection *collection,
				int point_x, int point_y,
				CollectionItem *colitem,
				int width, int height,
				gpointer user_data)
{
	Template	template;
	GdkRectangle	area;
	ViewData	*view = (ViewData *) colitem->view_data;
	ViewCollection	*view_collection = (ViewCollection *) user_data;

	area.x = 0;
	area.y = 0;
	area.width = width;
	area.height = height;

	fill_template(&area, colitem, view_collection, &template);

	return INSIDE(point_x, point_y, template.leafname) ||
	       INSIDE(point_x, point_y, template.icon) ||
	       (view->details && INSIDE(point_x, point_y, template.details));
}

/* 'box' renders a background box if the string is also selected */
static void draw_string(GtkWidget *widget,
		PangoLayout *layout,
		GdkRectangle *area,	/* Area available on screen */
		int 	width,		/* Width of the full string */
		GtkStateType selection_state,
		gboolean box)
{
	if (width > area->width)
	{
		gdk_gc_set_clip_origin(type_gc, 0, 0);
		gdk_gc_set_clip_rectangle(type_gc, area);
	}

	if (selection_state != GTK_STATE_NORMAL && box)
		gdk_draw_layout_with_colors(widget->window, type_gc, area->x, area->y, layout,
			&widget->style->text[selection_state], &widget->style->base[selection_state]);
	else
		gdk_draw_layout(widget->window, type_gc, area->x, area->y, layout);

	if (width > area->width)
	{
		static GdkGC *red_gc = NULL;

		if (!red_gc)
		{
			gboolean success;
			GdkColor red = {0, 0xffff, 0, 0};

			red_gc = gdk_gc_new(widget->window);
			gdk_colormap_alloc_colors(
					gtk_widget_get_colormap(widget),
					&red, 1, FALSE, TRUE, &success);
			gdk_gc_set_foreground(red_gc, &red);
		}
		gdk_draw_rectangle(widget->window, red_gc, TRUE,
				area->x + area->width - 1, area->y,
				1, area->height);
		gdk_gc_set_clip_rectangle(type_gc, NULL);
	}
}

/* Create the handers for the View interface */
static void view_collection_iface_init(gpointer giface, gpointer iface_data)
{
	ViewIfaceClass *iface = giface;

	g_assert(G_TYPE_FROM_INTERFACE(iface) == VIEW_TYPE_IFACE);

	/* override stuff */
	iface->sort = view_collection_sort;
	iface->style_changed = view_collection_style_changed;
	iface->add_items = view_collection_add_items;
	iface->update_items = view_collection_update_items;
	iface->delete_if = view_collection_delete_if;
	iface->clear = view_collection_clear;
	iface->select_all = view_collection_select_all;
	iface->clear_selection = view_collection_clear_selection;
	iface->count_items = view_collection_count_items;
	iface->count_selected = view_collection_count_selected;
	iface->show_cursor = view_collection_show_cursor;
	iface->get_iter = view_collection_get_iter;
	iface->get_iter_at_point = view_collection_get_iter_at_point;
	iface->cursor_to_iter = view_collection_cursor_to_iter;
	iface->set_selected = view_collection_set_selected;
	iface->get_selected = view_collection_get_selected;
	iface->set_frozen = view_collection_set_frozen;
	iface->select_only = view_collection_select_only;
	iface->wink_item = view_collection_wink_item;
	iface->autosize = view_collection_autosize;
	iface->cursor_visible = view_collection_cursor_visible;
	iface->set_base = view_collection_set_base;
	iface->start_lasso_box = view_collection_start_lasso_box;
	iface->extend_tip = view_collection_extend_tip;
	iface->auto_scroll_callback = view_collection_auto_scroll_callback;
}

static void view_collection_extend_tip(ViewIface *view, ViewIter *iter,
					GString *tip)
{
	ViewCollection*view_collection = (ViewCollection *) view;
	Collection *collection = view_collection->collection;
	FilerWindow *filer_window = view_collection->filer_window;
	Template template;
	int i = iter->i;
	CollectionItem	*colitem = &collection->items[i];
	ViewData *view_data = (ViewData *) colitem->view_data;
	GdkRectangle area;
	int row,col;
	
	collection_item_to_rowcol(collection, i, &row, &col);

	g_return_if_fail(iter->view == (ViewIface *) view_collection);
	g_return_if_fail(i >= 0 && i < collection->number_of_items);

	/* TODO: What if the window is narrower than 1 column? */
	if (filer_window->display_style == LARGE_ICONS ||
	    filer_window->display_style == HUGE_ICONS)
		return;		/* These wrap rather than truncate */

	area.x = col * collection->item_width;
	area.y = row * collection->item_height;
	area.height = collection->item_height;

	if (col == collection->columns - 1)
		area.width = GTK_WIDGET(collection)->allocation.width - area.x;
	else
		area.width = collection->item_width;

	fill_template(&area, colitem, view_collection, &template);

	if (template.leafname.width < view_data->name_width)
	{
		DirItem *item = (DirItem *) collection->items[i].data;

		g_string_append(tip, item->leafname);
		g_string_append_c(tip, '\n');
	}
}

static gint coll_motion_notify(GtkWidget *widget,
			       GdkEventMotion *event,
			       ViewCollection *view_collection)
{
	return filer_motion_notify(view_collection->filer_window, event);
}

/* Viewport is to be resized, so calculate increments */
static void size_allocate(GtkWidget *w, GtkAllocation *a, gpointer data)
{
	Collection *col = ((ViewCollection *) data)->collection;

	col->vadj->step_increment = col->item_height;
	col->vadj->page_increment = col->vadj->page_size;
}

static gint coll_button_release(GtkWidget *widget,
			        GdkEventButton *event,
				ViewCollection *view_collection)
{
	if (dnd_motion_release(event))
	{
		if (motion_buttons_pressed == 0 &&
					view_collection->collection->lasso_box)
		{
			filer_set_autoscroll(view_collection->filer_window,
					     FALSE);
			collection_end_lasso(view_collection->collection,
				event->button == 1 ? GDK_SET : GDK_INVERT);
		}
		return FALSE;
	}

	filer_perform_action(view_collection->filer_window, event);

	return FALSE;
}

static gint coll_button_press(GtkWidget *widget,
			      GdkEventButton *event,
			      ViewCollection *view_collection)
{
	collection_set_cursor_item(view_collection->collection, -1, TRUE);

	if (dnd_motion_press(widget, event))
		filer_perform_action(view_collection->filer_window, event);

	return FALSE;
}

/* Nothing is selected anymore - give up primary */
static void lost_selection(Collection  *collection,
			   guint        time,
			   gpointer     user_data)
{
	ViewCollection *view_collection = VIEW_COLLECTION(user_data);

	filer_lost_selection(view_collection->filer_window, time);
}

static void selection_changed(Collection *collection,
			      gint time,
			      gpointer user_data)
{
	ViewCollection *view_collection = VIEW_COLLECTION(user_data);

	filer_selection_changed(view_collection->filer_window, time);
}

static void display_free_colitem(Collection *collection,
				 CollectionItem *colitem)
{
	ViewData	*view = (ViewData *) colitem->view_data;

	if (!view)
		return;

	if (view->layout)
	{
		g_object_unref(G_OBJECT(view->layout));
		view->layout = NULL;
	}
	if (view->details)
		g_object_unref(G_OBJECT(view->details));

	if (view->image)
		g_object_unref(view->image);
	
	g_free(view);
}

static void add_item(ViewCollection *view_collection, DirItem *item)
{
	Collection *collection = view_collection->collection;
	FilerWindow	*filer_window = view_collection->filer_window;
	int		old_w = collection->item_width;
	int		old_h = collection->item_height;
	int		w, h, i;
	
	i = collection_insert(collection, item,
				display_create_viewdata(filer_window, item));

	calc_size(filer_window, &collection->items[i], &w, &h); 

	if (w > old_w || h > old_h)
		collection_set_item_size(collection,
					 MAX(old_w, w),
					 MAX(old_h, h));
}

static void style_set(Collection 	*collection,
		      GtkStyle		*style,
		      ViewCollection	*view_collection)
{
	view_collection_style_changed(VIEW(view_collection),
			VIEW_UPDATE_VIEWDATA | VIEW_UPDATE_NAME);
}

/* Return the size needed for this item */
static void calc_size(FilerWindow *filer_window, CollectionItem *colitem,
		int *width, int *height)
{
	int		pix_width, pix_height;
	int		w;
	DisplayStyle	style = filer_window->display_style;
	ViewData	*view = (ViewData *) colitem->view_data;
	gfloat scale = filer_window->icon_scale;

	if (filer_window->details_type == DETAILS_NONE)
	{
		if (style == HUGE_ICONS)
		{
			if (view->image)
			{
				if (!view->image->huge_pixbuf)
					pixmap_make_huge(view->image);
				
				if (view->image->huge_width <= ICON_WIDTH &&
					view->image->huge_height <= ICON_HEIGHT)
					scale = 1.0;

				pix_width = view->image->huge_width * scale;
				pix_height = view->image->huge_height * scale;
			}
			else
			{
				pix_width = HUGE_WIDTH * scale;
				pix_height = HUGE_HEIGHT * scale;
			}
			*width = MAX(pix_width, view->name_width);
			*height = MAX(view->name_height + pix_height,
					HUGE_HEIGHT * scale * 3 / 4);
		}
		else if (style == SMALL_ICONS)
		{
			w = MIN(view->name_width, o_small_width.int_value);
			*width = small_width + 12 + w;
			*height = MAX(view->name_height, font_height);
		}
		else
		{
			if (view->image)
				pix_width = view->image->width;
			else
				pix_width = ICON_WIDTH;
			*width = MAX(pix_width, view->name_width);
			*height = view->name_height + ICON_HEIGHT;
		}
	}
	else
	{
		w = view->details_width;
		if (style == HUGE_ICONS)
		{
			*width = HUGE_WIDTH * scale + 12 + MAX(w, view->name_width);
			*height = HUGE_HEIGHT * scale;
		}
		else if (style == SMALL_ICONS)
		{
			int	text_height;

			*width = small_width + view->name_width + 12 + w;
			text_height = MAX(view->name_height,
					  view->details_height);
			*height = MAX(text_height, font_height);
		}
		else
		{
			*width = ICON_WIDTH + 12 + MAX(w, view->name_width);
			*height = ICON_HEIGHT;
		}
	}

	/* margin */
	*width += 4;
	*height += 4;
}

static void update_item(ViewCollection *view_collection, int i)
{
	Collection *collection = view_collection->collection;
	int	old_w = collection->item_width;
	int	old_h = collection->item_height;
	int	w, h;
	CollectionItem *colitem;
	FilerWindow *filer_window = view_collection->filer_window;

	g_return_if_fail(i >= 0 && i < collection->number_of_items);
	
	colitem = &collection->items[i];

	display_update_view(filer_window,
			(DirItem *) colitem->data,
			(ViewData *) colitem->view_data,
			FALSE);
	
	calc_size(filer_window, colitem, &w, &h); 
	if (w > old_w || h > old_h)
		collection_set_item_size(collection,
					 MAX(old_w, w),
					 MAX(old_h, h));

	collection_draw_item(collection, i, TRUE);
}

/* Implementations of the View interface. See view_iface.c for comments. */

static void view_collection_style_changed(ViewIface *view, int flags)
{
	ViewCollection *view_collection = VIEW_COLLECTION(view);
	FilerWindow	*filer_window = view_collection->filer_window;
	int		i;
	Collection	*col = view_collection->collection;
	int		width = MIN_ITEM_WIDTH;
	int		height = font_height;
	int		n = col->number_of_items;

	if (n == 0 && filer_window->display_style != SMALL_ICONS)
		height = ICON_HEIGHT;

	view_collection->collection->vertical_order = FALSE;
	if (filer_window->display_style == SMALL_ICONS && 
	    o_vertical_order_small.int_value)
	  view_collection->collection->vertical_order = TRUE;
	if (filer_window->display_style != SMALL_ICONS && 
	    o_vertical_order_large.int_value)
	  view_collection->collection->vertical_order = TRUE;


	

	/* Recalculate all the ViewData structs for this window
	 * (needed if the text or image has changed in any way) and
	 * get the size of each item.
	 */
	for (i = 0; i < n; i++)
	{
		CollectionItem *ci = &col->items[i];
		int	w, h;

		if (flags & (VIEW_UPDATE_VIEWDATA | VIEW_UPDATE_NAME))
			display_update_view(filer_window,
					(DirItem *) ci->data,
					(ViewData *) ci->view_data,
					(flags & VIEW_UPDATE_NAME) != 0);

		calc_size(filer_window, ci, &w, &h);
		if (w > width)
			width = w;
		if (h > height)
			height = h;
	}

	collection_set_item_size(col, width, height);
	
	gtk_widget_queue_draw(GTK_WIDGET(view_collection));
}

typedef int (*SortFn)(gconstpointer a, gconstpointer b);

static SortFn sort_fn(FilerWindow *fw)
{
	switch (fw->sort_type)
	{
		case SORT_NAME: return sort_by_name;
		case SORT_TYPE: return sort_by_type;
		case SORT_DATE: return sort_by_date;
		case SORT_SIZE: return sort_by_size;
		case SORT_OWNER: return sort_by_owner;
		case SORT_GROUP: return sort_by_group;
		default:
			g_assert_not_reached();
	}

	return NULL;
}

static void view_collection_sort(ViewIface *view)
{
	ViewCollection	*view_collection = VIEW_COLLECTION(view);
	FilerWindow	*filer_window = view_collection->filer_window;

	collection_qsort(view_collection->collection, sort_fn(filer_window),
			filer_window->sort_order);
}

static void view_collection_add_items(ViewIface *view, GPtrArray *items)
{
	ViewCollection	*view_collection = VIEW_COLLECTION(view);
	Collection	*collection = view_collection->collection;
	FilerWindow	*filer_window = view_collection->filer_window;
	int old_num, i;

	old_num = collection->number_of_items;
	for (i = 0; i < items->len; i++)
	{
		DirItem *item = (DirItem *) items->pdata[i];

		if (!filer_match_filter(filer_window, item))
			continue;

		add_item(view_collection, item);
	}

	if (old_num != collection->number_of_items)
		view_collection_sort(view);
}

static void view_collection_update_items(ViewIface *view, GPtrArray *items)
{
	ViewCollection	*view_collection = VIEW_COLLECTION(view);
	Collection	*collection = view_collection->collection;
	FilerWindow	*filer_window = view_collection->filer_window;
	int		i;

	g_return_if_fail(items->len > 0);
	
	/* The item data has already been modified, so this gives the
	 * final sort order...
	 */
	collection_qsort(collection, sort_fn(filer_window),
			 filer_window->sort_order);

	for (i = 0; i < items->len; i++)
	{
		DirItem *item = (DirItem *) items->pdata[i];
		const gchar *leafname = item->leafname;
		int j;

		if (!filer_match_filter(filer_window, item))
			continue;

		j = collection_find_item(collection, item,
					 sort_fn(filer_window),
					 filer_window->sort_order);

		if (j < 0)
			g_warning("Failed to find '%s'\n", leafname);
		else
			update_item(view_collection, j);
	}
}

static void view_collection_delete_if(ViewIface *view,
			  gboolean (*test)(gpointer item, gpointer data),
			  gpointer data)
{
	ViewCollection	*view_collection = VIEW_COLLECTION(view);
	Collection	*collection = view_collection->collection;

	collection_delete_if(collection, test, data);
}

static void view_collection_clear(ViewIface *view)
{
	ViewCollection	*view_collection = VIEW_COLLECTION(view);
	Collection	*collection = view_collection->collection;

	collection_clear(collection);

	/* since fg is not used then use it */
	if (view_collection->filer_window->dir_color)
		gtk_widget_modify_fg(GTK_WIDGET(collection),
				GTK_STATE_NORMAL,
				view_collection->filer_window->dir_color);
	else
		gtk_widget_modify_fg(GTK_WIDGET(collection),
				GTK_STATE_NORMAL,
				&GTK_WIDGET(collection)->style->base[GTK_STATE_NORMAL]);
}

static void view_collection_select_all(ViewIface *view)
{
	ViewCollection	*view_collection = VIEW_COLLECTION(view);
	Collection	*collection = view_collection->collection;
	
	collection_select_all(collection);
}

static void view_collection_clear_selection(ViewIface *view)
{
	ViewCollection	*view_collection = VIEW_COLLECTION(view);
	Collection	*collection = view_collection->collection;
	
	collection_clear_selection(collection);
}

static int view_collection_count_items(ViewIface *view)
{
	ViewCollection	*view_collection = VIEW_COLLECTION(view);
	Collection	*collection = view_collection->collection;
	
	return collection->number_of_items;
}

static int view_collection_count_selected(ViewIface *view)
{
	ViewCollection	*view_collection = VIEW_COLLECTION(view);
	Collection	*collection = view_collection->collection;
	
	return collection->number_selected;
}

static void view_collection_show_cursor(ViewIface *view)
{
	ViewCollection	*view_collection = VIEW_COLLECTION(view);
	Collection	*collection = view_collection->collection;

	collection_move_cursor(collection, 0, 0, 0);
}

/* The first time the next() method is used, this is called */
static DirItem *iter_init(ViewIter *iter)
{
	ViewCollection *view_collection = (ViewCollection *) iter->view;
	Collection *collection = view_collection->collection;
	int i = -1;
	int n = collection->number_of_items;
	int flags = iter->flags;

	iter->peek = iter_peek;

	if (iter->n_remaining == 0)
		return NULL;

	if (flags & VIEW_ITER_FROM_CURSOR)
	{
		i = collection->cursor_item;
		if (i == -1)
			return NULL;	/* No cursor */
	}
	else if (flags & VIEW_ITER_FROM_BASE)
		i = view_collection->cursor_base;
	
	if (i < 0 || i >= n)
	{
		/* Either a normal iteration, or an iteration from an
		 * invalid starting point.
		 */
		if (flags & VIEW_ITER_BACKWARDS)
			i = n - 1;
		else
			i = 0;
	}

	if (i < 0 || i >= n)
		return NULL;	/* No items at all! */

	iter->next = flags & VIEW_ITER_BACKWARDS ? iter_prev : iter_next;
	iter->n_remaining--;
	iter->i = i;

	if (flags & VIEW_ITER_SELECTED && !collection->items[i].selected)
		return iter->next(iter);
	return iter->peek(iter);
}
/* Advance iter to point to the next item and return the new item
 * (this saves you calling peek after next each time).
 */
static DirItem *iter_next(ViewIter *iter)
{
	Collection *collection = ((ViewCollection *) iter->view)->collection;
	int n = collection->number_of_items;
	int i = iter->i;

	g_return_val_if_fail(iter->n_remaining >= 0, NULL);

	/* i is the last item returned (always valid) */

	g_return_val_if_fail(i >= 0 && i < n, NULL);
	
	while (iter->n_remaining)
	{
		i++;
		iter->n_remaining--;

		if (i == n)
			i = 0;

		g_return_val_if_fail(i >= 0 && i < n, NULL);

		if (iter->flags & VIEW_ITER_SELECTED &&
		    !collection->items[i].selected)
			continue;

		iter->i = i;
		return collection->items[i].data;
	}
	
	iter->i = -1;
	return NULL;
}

/* Like iter_next, but in the other direction */
static DirItem *iter_prev(ViewIter *iter)
{
	Collection *collection = ((ViewCollection *) iter->view)->collection;
	int n = collection->number_of_items;
	int i = iter->i;

	g_return_val_if_fail(iter->n_remaining >= 0, NULL);

	/* i is the last item returned (always valid) */

	g_return_val_if_fail(i >= 0 && i < n, NULL);

	while (iter->n_remaining)
	{
		i--;
		iter->n_remaining--;

		if (i == -1)
			i = collection->number_of_items - 1;

		g_return_val_if_fail(i >= 0 && i < n, NULL);

		if (iter->flags & VIEW_ITER_SELECTED &&
		    !collection->items[i].selected)
			continue;

		iter->i = i;
		return collection->items[i].data;
	}
	
	iter->i = -1;
	return NULL;
}

static DirItem *iter_peek(ViewIter *iter)
{
	Collection *collection = ((ViewCollection *) iter->view)->collection;
	int i = iter->i;

	if (i == -1)
		return NULL;
	
	g_return_val_if_fail(i >= 0 && i < collection->number_of_items, NULL);

	return collection->items[i].data;
}

static void make_iter(ViewCollection *view_collection, ViewIter *iter,
		      IterFlags flags)
{
	Collection *collection = view_collection->collection;

	iter->view = (ViewIface *) view_collection;
	iter->next = iter_init;
	iter->peek = NULL;
	iter->i = -1;

	iter->flags = flags;

	if (flags & VIEW_ITER_ONE_ONLY)
	{
		iter->n_remaining = 1;
		iter->next(iter);
	}
	else
		iter->n_remaining = collection->number_of_items;
}

/* Set the iterator to return 'i' on the next peek() */
static void make_item_iter(ViewCollection *view_collection,
			   ViewIter *iter, int i)
{
	Collection *collection = view_collection->collection;

	g_return_if_fail(i >= -1 && i < collection->number_of_items);

	make_iter(view_collection, iter, 0);

	iter->i = i;
	iter->next = iter_next;
	iter->peek = iter_peek;
	iter->n_remaining = 0;
}

static void view_collection_get_iter(ViewIface *view,
				     ViewIter *iter, IterFlags flags)
{
	ViewCollection	*view_collection = VIEW_COLLECTION(view);

	make_iter(view_collection, iter, flags);
}

static void view_collection_get_iter_at_point(ViewIface *view, ViewIter *iter,
					      GdkWindow *src, int x, int y)
{
	ViewCollection *view_collection = VIEW_COLLECTION(view);
	Collection *collection = view_collection->collection;
	int i;

	if (src == ((GtkWidget *) view)->window)
	{
		/* The event is on the Viewport, not the collection... */
		y += collection->vadj->value;
	}

	i = collection_get_item(collection, x, y);
	make_item_iter(view_collection, iter, i);
}

static void view_collection_cursor_to_iter(ViewIface *view, ViewIter *iter)
{
	ViewCollection	*view_collection = VIEW_COLLECTION(view);
	Collection	*collection = view_collection->collection;
	FilerWindow	*filer_window = view_collection->filer_window;
	
	g_return_if_fail(iter == NULL ||
			 iter->view == (ViewIface *) view_collection);

	collection_set_cursor_item(collection, iter ? iter->i : -1,
			filer_window->auto_scroll == -1);
}

static void view_collection_set_selected(ViewIface *view,
					 ViewIter *iter,
					 gboolean selected)
{
	ViewCollection	*view_collection = VIEW_COLLECTION(view);
	Collection	*collection = view_collection->collection;
	
	g_return_if_fail(iter != NULL &&
			 iter->view == (ViewIface *) view_collection);
	g_return_if_fail(iter->i >= 0 && iter->i < collection->number_of_items);

	if (selected)
		collection_select_item(collection, iter->i);
	else
		collection_unselect_item(collection, iter->i);
}

static gboolean view_collection_get_selected(ViewIface *view, ViewIter *iter)
{
	ViewCollection	*view_collection = VIEW_COLLECTION(view);
	Collection	*collection = view_collection->collection;

	g_return_val_if_fail(iter != NULL &&
			iter->view == (ViewIface *) view_collection, FALSE);
	g_return_val_if_fail(iter->i >= 0 &&
				iter->i < collection->number_of_items, FALSE);
	
	return collection->items[iter->i].selected;
}

static void view_collection_select_only(ViewIface *view, ViewIter *iter)
{
	ViewCollection	*view_collection = VIEW_COLLECTION(view);
	Collection	*collection = view_collection->collection;

	g_return_if_fail(iter != NULL &&
			 iter->view == (ViewIface *) view_collection);
	g_return_if_fail(iter->i >= 0 && iter->i < collection->number_of_items);

	collection_clear_except(collection, iter->i);
}

static void view_collection_set_frozen(ViewIface *view, gboolean frozen)
{
	ViewCollection	*view_collection = VIEW_COLLECTION(view);
	Collection	*collection = view_collection->collection;

	if (frozen)
		collection->block_selection_changed++;
	else
		collection_unblock_selection_changed(collection,
				gtk_get_current_event_time(), TRUE);
}

static void view_collection_wink_item(ViewIface *view, ViewIter *iter)
{
	ViewCollection	*view_collection = VIEW_COLLECTION(view);
	Collection	*collection = view_collection->collection;

	if (!iter)
	{
		collection_wink_item(collection, -1);
		return;
	}

	g_return_if_fail(iter != NULL &&
			 iter->view == (ViewIface *) view_collection);
	g_return_if_fail(iter->i >= 0 && iter->i < collection->number_of_items);

	collection_wink_item(collection, iter->i);
}

static void view_collection_autosize(ViewIface *view)
{
	ViewCollection	*view_collection = VIEW_COLLECTION(view);
	FilerWindow	*filer_window = view_collection->filer_window;
	Collection	*collection = view_collection->collection;
	int 		n;
	int		w = collection->item_width;
	int		h = collection->item_height;
	int 		x;
	int		rows, cols;
	int		max_x, max_y, min_x = 0;
	const float	r = 1.6;
	int		t = 0, tn;
	int		space = 0;

	/* Get the extra height required for the toolbar and minibuffer,
	 * if visible.
	 */
	if (o_toolbar.int_value != TOOLBAR_NONE)
		t = filer_window->toolbar->allocation.height;
	if (filer_window->message)
		t += filer_window->message->allocation.height;
	if (GTK_WIDGET_VISIBLE(filer_window->minibuffer_area))
	{
		GtkRequisition req;

		gtk_widget_size_request(filer_window->minibuffer_area, &req);
		space = req.height + 2;
		t += space;
	}

	n = collection->number_of_items;
	if (n == 0)
		h = (filer_window->display_style != SMALL_ICONS ? ICON_HEIGHT : 0) +
			font_height + 4;
	n = MAX(n, 2);

	max_x = (o_filer_size_limit.int_value * monitor_width) / 100;
	max_y = (o_filer_size_limit.int_value * monitor_height) / 100;

	if (filer_window->toolbar)
	{
		gtk_widget_get_size_request(filer_window->toolbar, &min_x, NULL);
		if (filer_window->scrollbar)
			min_x -= filer_window->scrollbar->allocation.width;
	}

	/* Leave some room for extra icons, but only in Small Icons mode
	 * otherwise it takes up too much space.
	 * Also, don't add space if the minibuffer is open.
	 */
	if (space == 0)
		space = filer_window->display_style == SMALL_ICONS ? h : 2;

	tn = t + space;
	t = tn + 44 /* window decoration and charm. when small then wide */;

	/* Aim for a size where
	 * 	   x = r(y + t + h),		(1)
	 * unless that's too wide.
	 *
	 * t = toolbar (and minibuffer) height
	 * r = desired (width / height) ratio
	 *
	 * Want to display all items:
	 * 	   (x/w)(y/h) = n
	 * 	=> xy = nwh
	 *	=> x(x/r - t - h) = nwh		(from 1)
	 *	=> xx - x.rt - hr(1 - nw) = 0
	 *	=> 2x = rt +/- sqrt(rt.rt + 4hr(nw - 1))
	 * Now,
	 * 	   4hr(nw - 1) > 0
	 * so
	 * 	   sqrt(rt.rt + ...) > rt
	 *
	 * So, the +/- must be +:
	 * 	
	 *	=> x = (rt + sqrt(rt.rt + 4hr(nw - 1))) / 2
	 */

	x = (r * t + sqrt(r*r*t*t + 4*h*r * (n*w - 1))) / 2 ;
	x = ((x + w / 2) / w) * w;

	/* Limit x */
	if (x < min_x)
	{
		if (n * w > min_x)
		{
			cols = min_x / w;
			x = cols * w;
			if (min_x != x &&
				n % cols &&
				n % cols <= n / cols &&
				/* Put window decoration away
				 * because in this case, wide is good */
				(tn + h * (n / cols)) * (x + w - min_x)
					< (min_x * h))
				x += w;
		}
		else
			x = min_x;
	}

	if (x > max_x)
		x = max_x;

	cols = x / w;
	cols = MAX(cols, 1);

	rows = MAX((n + cols - 1) / cols, 1);

	filer_window_set_size(filer_window,
			w * MAX(cols, 1),
			MIN(max_y, h * rows + space));
}

static gboolean view_collection_cursor_visible(ViewIface *view)
{
	ViewCollection	*view_collection = VIEW_COLLECTION(view);
	Collection	*collection = view_collection->collection;

	return collection->cursor_item != -1;
}

static void view_collection_set_base(ViewIface *view, ViewIter *iter)
{
	ViewCollection	*view_collection = VIEW_COLLECTION(view);

	view_collection->cursor_base = iter->i;
}

static void view_collection_start_lasso_box(ViewIface *view,
					    GdkEventButton *event)
{
	ViewCollection	*view_collection = (ViewCollection *) view;
	Collection	*collection = view_collection->collection;

	filer_set_autoscroll(view_collection->filer_window, TRUE);
	collection_lasso_box(collection, event->x, event->y);
}


/* Change the adjustment by this amount. Bounded. */
static void diff_vpos(Collection *collection, int diff)
{
	int	old = collection->vadj->value;
	int	value = old + diff;

	value = CLAMP(value, 0,
			collection->vadj->upper - collection->vadj->page_size);
	gtk_adjustment_set_value(collection->vadj, value);

	if (collection->vadj->value != old)
		dnd_spring_abort();
}

static gboolean view_collection_auto_scroll_callback(ViewIface *view)
{
	ViewCollection	*view_collection = (ViewCollection *) view;
	Collection	*collection = view_collection->collection;
	GdkWindow	*window = ((GtkWidget *) collection)->window;
	gint		x, y, w, h;
	GdkModifierType	mask;
	int		diff = 0;

	gdk_window_get_pointer(window, &x, &y, &mask);
	gdk_drawable_get_size(window, &w, NULL);

	h = collection->vadj->page_size;
	y -= collection->vadj->value;

	if ((x < 0 || x > w || y < 0 || y > h) && !collection->lasso_box)
		return FALSE;		/* Out of window - stop */

	if (y < AUTOSCROLL_STEP)
		diff = y - AUTOSCROLL_STEP;
	else if (y > h - AUTOSCROLL_STEP)
		diff = AUTOSCROLL_STEP + y - h;

	if (diff)
		diff_vpos(collection, diff);

	return TRUE;
}
