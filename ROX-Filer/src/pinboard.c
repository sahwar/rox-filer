/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * Copyright (C) 2002, the ROX-Filer team.
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

/* pinboard.c - icons on the desktop background */

#include "config.h"

#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <gtk/gtkinvisible.h>
#include <stdlib.h>
#include <libxml/parser.h>

#include "global.h"

#include "pinboard.h"
#include "main.h"
#include "dnd.h"
#include "pixmaps.h"
#include "type.h"
#include "choices.h"
#include "support.h"
#include "gui_support.h"
#include "options.h"
#include "diritem.h"
#include "bind.h"
#include "icon.h"
#include "run.h"
#include "appinfo.h"
#include "menu.h"

static gboolean tmp_icon_selected = FALSE;		/* When dragging */

struct _Pinboard {
	guchar		*name;		/* Leaf name */
	GList		*icons;
	GtkStyle	*style;
};

#define IS_PIN_ICON(obj) G_TYPE_CHECK_INSTANCE_TYPE((obj), pin_icon_get_type())

typedef struct _PinIconClass PinIconClass;
typedef struct _PinIcon PinIcon;

struct _PinIconClass {
	IconClass parent;
};

struct _PinIcon {
	Icon		icon;

	int		x, y, width, height;
	int		name_width;
	PangoLayout	*layout;	/* The label */
	GtkWidget	*win;
	GtkWidget	*widget;	/* The drawing area */
	GdkBitmap	*mask;
};

/* The number of pixels between the bottom of the image and the top
 * of the text.
 */
#define GAP 4

/* The size of the border around the icon which is used when winking */
#define WINK_FRAME 2

/* Grid sizes */
#define GRID_STEP_FINE   2
#define GRID_STEP_MED    16
#define GRID_STEP_COARSE 32

static PinIcon	*current_wink_icon = NULL;
static gint	wink_timeout;

/* Used for the text colours (only) in the icons */
static GdkColor text_fg_col, text_bg_col;

/* Style that all the icons should use. NULL => regenerate from text_fg/bg */
static GtkStyle *pinicon_style = NULL;

Pinboard	*current_pinboard = NULL;
static gint	loading_pinboard = 0;		/* Non-zero => loading */

static GdkColor	mask_solid = {1, 1, 1, 1};
static GdkColor	mask_transp = {0, 0, 0, 0};
static GdkGC	*mask_gc = NULL;

/* Proxy window for DnD and clicks on the desktop */
static GtkWidget *proxy_invisible;

/* The window (owned by the wm) which root clicks are forwarded to.
 * NULL if wm does not support forwarding clicks.
 */
static GdkWindow *click_proxy_gdk_window = NULL;
static GdkAtom   win_button_proxy; /* _WIN_DESKTOP_BUTTON_PROXY */

/* The Icon that was used to start the current drag, if any */
Icon *pinboard_drag_in_progress = NULL;

/* Used when dragging icons around... */
static gboolean pinboard_modified = FALSE;

typedef enum {
	TEXT_BG_NONE = 0,
	TEXT_BG_OUTLINE = 1,
	TEXT_BG_SOLID = 2,
} TextBgType;

static Option o_pinboard_clamp_icons, o_pinboard_grid_step;
static Option o_pinboard_fg_colour, o_pinboard_bg_colour;

/* Static prototypes */
static GType pin_icon_get_type(void);
static void set_size_and_shape(PinIcon *pi, int *rwidth, int *rheight);
static gint draw_icon(GtkWidget *widget, GdkEventExpose *event, PinIcon *pi);
static void mask_wink_border(PinIcon *pi, GdkColor *alpha);
static gint end_wink(gpointer data);
static gboolean button_release_event(GtkWidget *widget,
			    	     GdkEventButton *event,
                            	     PinIcon *pi);
static gboolean root_property_event(GtkWidget *widget,
			    	    GdkEventProperty *event,
                            	    gpointer data);
static gboolean root_button_press(GtkWidget *widget,
			    	  GdkEventButton *event,
                            	  gpointer data);
static gboolean enter_notify(GtkWidget *widget,
			     GdkEventCrossing *event,
			     PinIcon *pi);
static gboolean button_press_event(GtkWidget *widget,
			    GdkEventButton *event,
                            PinIcon *pi);
static gint icon_motion_notify(GtkWidget *widget,
			       GdkEventMotion *event,
			       PinIcon *pi);
static const char *pin_from_file(gchar *line);
static gboolean add_root_handlers(void);
static GdkFilterReturn proxy_filter(GdkXEvent *xevent,
				    GdkEvent *event,
				    gpointer data);
static void snap_to_grid(int *x, int *y);
static void offset_from_centre(PinIcon *pi,
			       int width, int height,
			       int *x, int *y);
static void offset_to_centre(PinIcon *pi,
			     int width, int height,
			     int *x, int *y);
static gboolean drag_motion(GtkWidget		*widget,
                            GdkDragContext	*context,
                            gint		x,
                            gint		y,
                            guint		time,
			    PinIcon		*pi);
static void drag_set_pinicon_dest(PinIcon *pi);
static void drag_leave(GtkWidget	*widget,
                       GdkDragContext	*context,
		       guint32		time,
		       PinIcon		*pi);
static void forward_root_clicks(void);
static gboolean bg_drag_motion(GtkWidget	*widget,
                               GdkDragContext	*context,
                               gint		x,
                               gint		y,
                               guint		time,
			       gpointer		data);
static gboolean bg_drag_leave(GtkWidget		*widget,
			      GdkDragContext	*context,
			      guint32		time,
			      gpointer		data);
static void bg_expose(GdkRectangle *area);
static void drag_end(GtkWidget *widget,
			GdkDragContext *context,
			PinIcon *pi);
static void reshape_all(void);
static void pinboard_check_options(void);
static void pinboard_load_from_xml(xmlDocPtr doc);
static void pinboard_clear(void);
static void pinboard_save(void);
static PinIcon *pin_icon_new(const char *pathname, const char *name);
static void pin_icon_destroyed(PinIcon *pi);
static void pin_icon_set_tip(PinIcon *pi);
static void pinboard_show_menu(GdkEventButton *event, PinIcon *pi);

/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/

void pinboard_init(void)
{
	option_add_string(&o_pinboard_fg_colour, "pinboard_fg_colour", "#000");
	option_add_string(&o_pinboard_bg_colour, "pinboard_bg_colour", "#ddd");

	option_add_int(&o_pinboard_clamp_icons, "pinboard_clamp_icons", 1);
	option_add_int(&o_pinboard_grid_step, "pinboard_grid_step",
							GRID_STEP_COARSE);
	option_add_notify(pinboard_check_options);

	gdk_color_parse(o_pinboard_fg_colour.value, &text_fg_col);
	gdk_color_parse(o_pinboard_bg_colour.value, &text_bg_col);
}

/* Load 'pb_<pinboard>' config file from Choices (if it exists)
 * and make it the current pinboard.
 * Any existing pinned items are removed. You must call this
 * at least once before using the pinboard. NULL disables the
 * pinboard.
 */
void pinboard_activate(const gchar *name)
{
	Pinboard	*old_board = current_pinboard;
	guchar		*path, *slash;

	/* Treat an empty name the same as NULL */
	if (name && !*name)
		name = NULL;

	if (old_board)
		pinboard_clear();

	if (!name)
	{
		if (number_of_windows < 1 && gtk_main_level() > 0)
			gtk_main_quit();
		return;
	}

	if (!add_root_handlers())
	{
		delayed_error(_("Another application is already "
					"managing the pinboard!"));
		return;
	}

	number_of_windows++;
	
	slash = strchr(name, '/');
	if (slash)
	{
		if (access(name, F_OK))
			path = NULL;	/* File does not (yet) exist */
		else
			path = g_strdup(name);
	}
	else
	{
		guchar	*leaf;

		leaf = g_strconcat("pb_", name, NULL);
		path = choices_find_path_load(leaf, PROJECT);
		g_free(leaf);
	}

	current_pinboard = g_new(Pinboard, 1);
	current_pinboard->name = g_strdup(name);
	current_pinboard->icons = NULL;

	loading_pinboard++;
	if (path)
	{
		xmlDocPtr doc;
		doc = xmlParseFile(path);
		if (doc)
		{
			pinboard_load_from_xml(doc);
			xmlFreeDoc(doc);
		}
		else
		{
			parse_file(path, pin_from_file);
			info_message(_("Your old pinboard file has been "
					"converted to the new XML format."));
			pinboard_save();
		}
		g_free(path);
	}
	else
		pinboard_pin(home_dir, "Home",
				4 + ICON_WIDTH / 2,
				4 + ICON_HEIGHT / 2);
	loading_pinboard--;
}

const char *pinboard_get_name(void)
{
	g_return_val_if_fail(current_pinboard != NULL, NULL);

	return current_pinboard->name;
}

/* Add a new icon to the background.
 * 'path' should be an absolute pathname.
 * 'x' and 'y' are the coordinates of the point in the middle of the text
 * if 'corner' is FALSE, and as the top-left corner of where the icon
 * image should be if it is TRUE.
 * 'name' is the name to use. If NULL then the leafname of path is used.
 *
 * name and path are in UTF-8 for Gtk+-2.0 only.
 */
void pinboard_pin(const gchar *path, const gchar *name, int x, int y)
{
	PinIcon		*pi;
	Icon		*icon;
	int		width, height;

	g_return_if_fail(path != NULL);
	g_return_if_fail(current_pinboard != NULL);

	pi = pin_icon_new(path, name);
	icon = (Icon *) pi;
	pi->x = x;
	pi->y = y;

	/* Window takes the initial ref of Icon */
	pi->win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_wmclass(GTK_WINDOW(pi->win), "ROX-Pinboard", PROJECT);

	pi->widget = gtk_drawing_area_new();
	gtk_widget_set_name(pi->widget, "pinboard-icon");
	
	pi->layout = gtk_widget_create_pango_layout(pi->widget, NULL);
	pango_layout_set_width(pi->layout, 140 * PANGO_SCALE);

	gtk_container_add(GTK_CONTAINER(pi->win), pi->widget);
	drag_set_pinicon_dest(pi);
	g_signal_connect(pi->widget, "drag_data_get",
				G_CALLBACK(drag_data_get), NULL);

	gtk_widget_realize(pi->win);
	gtk_widget_realize(pi->widget);

	set_size_and_shape(pi, &width, &height);
	snap_to_grid(&x, &y);
	offset_from_centre(pi, width, height, &x, &y);
	gtk_window_move(GTK_WINDOW(pi->win), x, y);
	/* Set the correct position in the icon */
	offset_to_centre(pi, width, height, &x, &y);
	pi->x = x;
	pi->y = y;
	
	make_panel_window(pi->win);

	/* TODO: Use gdk function when it supports this type */
	{
		GdkAtom desktop_type;

		desktop_type = gdk_atom_intern("_NET_WM_WINDOW_TYPE_DESKTOP",
						FALSE);
		gdk_property_change(pi->win->window,
			gdk_atom_intern("_NET_WM_WINDOW_TYPE", FALSE),
			gdk_atom_intern("ATOM", FALSE), 32,
			GDK_PROP_MODE_REPLACE, (guchar *) &desktop_type, 1);
	}

	gtk_widget_add_events(pi->widget,
			GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
			GDK_BUTTON1_MOTION_MASK | GDK_ENTER_NOTIFY_MASK |
			GDK_BUTTON2_MOTION_MASK | GDK_BUTTON3_MOTION_MASK);
	g_signal_connect(pi->widget, "enter-notify-event",
			G_CALLBACK(enter_notify), pi);
	g_signal_connect(pi->widget, "button-press-event",
			G_CALLBACK(button_press_event), pi);
	g_signal_connect(pi->widget, "button-release-event",
			G_CALLBACK(button_release_event), pi);
	g_signal_connect(pi->widget, "motion-notify-event",
			G_CALLBACK(icon_motion_notify), pi);
	g_signal_connect(pi->widget, "expose-event",
			G_CALLBACK(draw_icon), pi);
	g_signal_connect_swapped(pi->win, "destroy",
			G_CALLBACK(pin_icon_destroyed), pi);

	current_pinboard->icons = g_list_prepend(current_pinboard->icons,
						 pi);
	pin_icon_set_tip(pi);
	gtk_widget_show_all(pi->win);
	gdk_window_lower(pi->win->window);

	if (!loading_pinboard)
		pinboard_save();
}

/* Remove an icon from the pinboard */
/* XXX: use destroy */
void pinboard_unpin(PinIcon *pi)
{
	g_return_if_fail(pi != NULL);

	gtk_widget_destroy(pi->win);
	pinboard_save();
}

/* Put a border around the icon, briefly.
 * If icon is NULL then cancel any existing wink.
 * The icon will automatically unhighlight unless timeout is FALSE,
 * in which case you must call this function again (with NULL or another
 * icon) to remove the highlight.
 */
static void pinboard_wink_item(PinIcon *pi, gboolean timeout)
{
	if (current_wink_icon == pi)
		return;

	if (current_wink_icon)
	{
		mask_wink_border(current_wink_icon, &mask_transp);
		if (wink_timeout != -1)
			gtk_timeout_remove(wink_timeout);
	}

	current_wink_icon = pi;

	if (current_wink_icon)
	{
		mask_wink_border(current_wink_icon, &mask_solid);
		if (timeout)
			wink_timeout = gtk_timeout_add(300, end_wink, NULL);
		else
			wink_timeout = -1;
	}
}

/* Icon's size, shape or appearance has changed - update the display */
void pinboard_reshape_icon(Icon *icon)
{
	PinIcon	*pi = (PinIcon *) icon;
	int	x = pi->x, y = pi->y;
	int	width, height;

	set_size_and_shape(pi, &width, &height);
	gdk_window_resize(pi->win->window, width, height);
	offset_from_centre(pi, width, height, &x, &y);
	gtk_window_move(GTK_WINDOW(pi->win), x, y);
	gtk_widget_queue_draw(pi->win);
}


/****************************************************************
 *			INTERNAL FUNCTIONS			*
 ****************************************************************/

static void pinboard_check_options(void)
{
	GdkColor	n_fg, n_bg;

	gdk_color_parse(o_pinboard_fg_colour.value, &n_fg);
	gdk_color_parse(o_pinboard_bg_colour.value, &n_bg);

	if (gdk_color_equal(&n_fg, &text_fg_col) == 0 ||
		gdk_color_equal(&n_bg, &text_bg_col) == 0)
	{
		memcpy(&text_fg_col, &n_fg, sizeof(GdkColor));
		memcpy(&text_bg_col, &n_bg, sizeof(GdkColor));

		if (pinicon_style)
		{
			g_object_unref(G_OBJECT(pinicon_style));
			pinicon_style = NULL;
		}

		if (current_pinboard)
			reshape_all();
	}
}

static gint end_wink(gpointer data)
{
	pinboard_wink_item(NULL, FALSE);
	return FALSE;
}

/* Make the wink border solid or transparent */
static void mask_wink_border(PinIcon *pi, GdkColor *alpha)
{
	if (!current_pinboard)
		return;
	
	gdk_gc_set_foreground(mask_gc, alpha);
	gdk_draw_rectangle(pi->mask, mask_gc, FALSE,
			0, 0, pi->width - 1, pi->height - 1);
	gdk_draw_rectangle(pi->mask, mask_gc, FALSE,
			1, 1, pi->width - 3, pi->height - 3);

	gtk_widget_shape_combine_mask(pi->win, pi->mask, 0, 0);

	gtk_widget_queue_draw(pi->widget);
	gdk_window_process_updates(pi->widget->window, TRUE);
}

/* Updates the name_width and layout fields, and resizes and masks the window.
 * Also sets the style to pinicon_style, generating it if needed.
 * Returns the new width and height.
 */
static void set_size_and_shape(PinIcon *pi, int *rwidth, int *rheight)
{
	int		width, height;
	int		font_height;
	Icon		*icon = (Icon *) pi;
	MaskedPixmap	*image = icon->item->image;
	int		iwidth = image->width;
	int		iheight = image->height;
	DirItem		*item = icon->item;
	int		text_x, text_y;

	if (!pinicon_style)
	{
		pinicon_style = gtk_style_copy(pi->widget->style);
		memcpy(&pinicon_style->fg[GTK_STATE_NORMAL],
			&text_fg_col, sizeof(GdkColor));
		memcpy(&pinicon_style->bg[GTK_STATE_NORMAL],
			&text_bg_col, sizeof(GdkColor));
	}
	gtk_widget_set_style(pi->widget, pinicon_style);

	{
		PangoRectangle logical;
		pango_layout_set_text(pi->layout, icon->item->leafname, -1);
		pango_layout_get_pixel_extents(pi->layout, NULL, &logical);

		pi->name_width = logical.width - logical.x;
		font_height = logical.height - logical.y;
	}

	width = MAX(iwidth, pi->name_width + 2) + 2 * WINK_FRAME;
	height = iheight + GAP + (font_height + 2) + 2 * WINK_FRAME;
	gtk_widget_set_size_request(pi->win, width, height);
	pi->width = width;
	pi->height = height;

	if (pi->mask)
		g_object_unref(pi->mask);
	pi->mask = gdk_pixmap_new(pi->win->window, width, height, 1);
	if (!mask_gc)
		mask_gc = gdk_gc_new(pi->mask);

	/* Clear the mask to transparent */
	gdk_gc_set_foreground(mask_gc, &mask_transp);
	gdk_draw_rectangle(pi->mask, mask_gc, TRUE, 0, 0, width, height);

	gdk_gc_set_foreground(mask_gc, &mask_solid);
	/* Make the icon area solid */
	if (image->mask)
	{
		gdk_draw_drawable(pi->mask, mask_gc, image->mask,
				0, 0,
				(width - iwidth) >> 1,
				WINK_FRAME,
				image->width,
				image->height);
	}
	else
	{
		gdk_draw_rectangle(pi->mask, mask_gc, TRUE,
				(width - iwidth) >> 1,
				WINK_FRAME,
				iwidth,
				iheight);
	}

	gdk_gc_set_function(mask_gc, GDK_OR);
	if (item->flags & ITEM_FLAG_SYMLINK)
	{
		gdk_draw_drawable(pi->mask, mask_gc, im_symlink->mask,
				0, 0,		/* Source x,y */
				(width - iwidth) >> 1,		/* Dest x */
				WINK_FRAME,			/* Dest y */
				-1, -1);
	}
	else if (item->flags & ITEM_FLAG_MOUNT_POINT)
	{
		/* Note: Both mount state pixmaps must have the same mask */
		gdk_draw_drawable(pi->mask, mask_gc, im_mounted->mask,
				0, 0,		/* Source x,y */
				(width - iwidth) >> 1,		/* Dest x */
				WINK_FRAME,			/* Dest y */
				-1, -1);
	}
	gdk_gc_set_function(mask_gc, GDK_COPY);

	/* Mask off an area for the text */

	text_x = (width - pi->name_width) >> 1;
	text_y = WINK_FRAME + iheight + GAP + 1;

	gdk_draw_rectangle(pi->mask, mask_gc, TRUE,
			(width - (pi->name_width + 2)) >> 1,
			WINK_FRAME + iheight + GAP,
			pi->name_width + 2, font_height + 2);
	
	gtk_widget_shape_combine_mask(pi->win, pi->mask, 0, 0);

	*rwidth = width;
	*rheight = height;
}

static gint draw_icon(GtkWidget *widget, GdkEventExpose *event, PinIcon *pi)
{
	int		text_x, text_y;
	Icon		*icon = (Icon *) pi;
	DirItem		*item = icon->item;
	MaskedPixmap	*image = item->image;
	int		iwidth = image->width;
	int		iheight = image->height;
	int		image_x;
	GdkGC		*gc = widget->style->black_gc;
	GtkStateType	state = icon->selected ? GTK_STATE_SELECTED
					       : GTK_STATE_NORMAL;
	PangoRectangle	logical;

	image_x = (pi->width - iwidth) >> 1;

	/* TODO: If the shape extension is missing we might need to set
	 * the clip mask here...
	 */
	gdk_draw_drawable(widget->window, gc,
			image->pixmap,
			0, 0,
			image_x,
			WINK_FRAME,
			iwidth,
			iheight);

	if (item->flags & ITEM_FLAG_SYMLINK)
	{
		gdk_gc_set_clip_origin(gc, image_x, WINK_FRAME);
		gdk_gc_set_clip_mask(gc, im_symlink->mask);
		gdk_draw_drawable(widget->window, gc,
				im_symlink->pixmap,
				0, 0,		/* Source x,y */
				image_x, WINK_FRAME,	/* Dest x,y */
				-1, -1);
		gdk_gc_set_clip_mask(gc, NULL);
		gdk_gc_set_clip_origin(gc, 0, 0);
	}
	else if (item->flags & ITEM_FLAG_MOUNT_POINT)
	{
		MaskedPixmap	*mp = item->flags & ITEM_FLAG_MOUNTED
					? im_mounted
					: im_unmounted;
					
		gdk_gc_set_clip_origin(gc, image_x, WINK_FRAME);
		gdk_gc_set_clip_mask(gc, mp->mask);
		gdk_draw_drawable(widget->window, gc,
				mp->pixmap,
				0, 0,		/* Source x,y */
				image_x, WINK_FRAME,	/* Dest x,y */
				-1, -1);
		gdk_gc_set_clip_mask(gc, NULL);
		gdk_gc_set_clip_origin(gc, 0, 0);
	}

	text_x = (pi->width - pi->name_width) >> 1;
	text_y = WINK_FRAME + iheight + GAP + 1;

	pango_layout_get_pixel_extents(pi->layout, NULL, &logical);

	gtk_paint_flat_box(widget->style, widget->window,
			state,
			GTK_SHADOW_NONE,
			NULL, widget, "text",
			text_x - 1,
			text_y - 1,
			pi->name_width + 2,
			logical.height - logical.y + 2);

	gtk_paint_layout(widget->style, widget->window,
			state,
			FALSE, NULL, widget, "text",
			text_x,
			text_y,
			pi->layout);

	if (current_wink_icon == pi)
	{
		gdk_draw_rectangle(pi->widget->window,
				pi->widget->style->white_gc,
				FALSE,
				0, 0, pi->width - 1, pi->height - 1);
		gdk_draw_rectangle(pi->widget->window,
				pi->widget->style->black_gc,
				FALSE,
				1, 1, pi->width - 3, pi->height - 3);
	}

	return FALSE;
}

static gboolean root_property_event(GtkWidget *widget,
			    	    GdkEventProperty *event,
                            	    gpointer data)
{
	if (event->atom == win_button_proxy &&
			event->state == GDK_PROPERTY_NEW_VALUE)
	{
		/* Setup forwarding on the new proxy window, if possible */
		forward_root_clicks();
	}

	return FALSE;
}

static gboolean root_button_press(GtkWidget *widget,
			    	  GdkEventButton *event,
                            	  gpointer data)
{
	BindAction	action;

	action = bind_lookup_bev(BIND_PINBOARD, event);

	switch (action)
	{
		case ACT_CLEAR_SELECTION:
			icon_select_only(NULL);
			break;
		case ACT_POPUP_MENU:
			dnd_motion_ungrab();
			pinboard_show_menu(event, NULL);
			break;
		case ACT_IGNORE:
			break;
		default:
			g_warning("Unsupported action : %d\n", action);
			break;
	}

	return TRUE;
}

static gboolean enter_notify(GtkWidget *widget,
			     GdkEventCrossing *event,
			     PinIcon *pi)
{
	icon_may_update((Icon *) pi);

	return FALSE;
}

static void perform_action(PinIcon *pi, GdkEventButton *event)
{
	BindAction	action;
	Icon		*icon = (Icon *) pi;
	
	action = bind_lookup_bev(BIND_PINBOARD_ICON, event);

	switch (action)
	{
		case ACT_OPEN_ITEM:
			dnd_motion_ungrab();
			pinboard_wink_item(pi, TRUE);
			if (event->type == GDK_2BUTTON_PRESS)
				icon_set_selected(icon, FALSE);
			run_diritem(icon->path, icon->item, NULL, NULL, FALSE);
			break;
		case ACT_EDIT_ITEM:
			dnd_motion_ungrab();
			pinboard_wink_item(pi, TRUE);
			if (event->type == GDK_2BUTTON_PRESS)
				icon_set_selected(icon, FALSE);
			run_diritem(icon->path, icon->item, NULL, NULL, TRUE);
			break;
		case ACT_POPUP_MENU:
			dnd_motion_ungrab();
			pinboard_show_menu(event, pi);
			break;
		case ACT_PRIME_AND_SELECT:
			if (!icon->selected)
				icon_select_only(icon);
			dnd_motion_start(MOTION_READY_FOR_DND);
			break;
		case ACT_PRIME_AND_TOGGLE:
			icon_set_selected(icon, !icon->selected);
			dnd_motion_start(MOTION_READY_FOR_DND);
			break;
		case ACT_PRIME_FOR_DND:
			dnd_motion_start(MOTION_READY_FOR_DND);
			break;
		case ACT_TOGGLE_SELECTED:
			icon_set_selected(icon, !icon->selected);
			break;
		case ACT_SELECT_EXCL:
			icon_select_only(icon);
			break;
		case ACT_IGNORE:
			break;
		default:
			g_warning("Unsupported action : %d\n", action);
			break;
	}
}

static gboolean button_release_event(GtkWidget *widget,
			    	     GdkEventButton *event,
                            	     PinIcon *pi)
{
	if (pinboard_modified)
		pinboard_save();

	if (dnd_motion_release(event))
		return TRUE;

	perform_action(pi, event);
	
	return TRUE;
}

static gboolean button_press_event(GtkWidget *widget,
			    	   GdkEventButton *event,
                            	   PinIcon *pi)
{
	if (dnd_motion_press(widget, event))
		perform_action(pi, event);

	return TRUE;
}

static void start_drag(PinIcon *pi, GdkEventMotion *event)
{
	GtkWidget *widget = pi->widget;
	Icon	  *icon = (Icon *) pi;

	if (!icon->selected)
	{
		tmp_icon_selected = TRUE;
		icon_select_only(icon);
	}
	
	g_return_if_fail(icon_selection != NULL);

	pinboard_drag_in_progress = icon;

	if (icon_selection->next == NULL)
		drag_one_item(widget, event, icon->path, icon->item, NULL);
	else
	{
		guchar	*uri_list;

		uri_list = icon_create_uri_list();
		drag_selection(widget, event, uri_list);
		g_free(uri_list);
	}
}

/* An icon is being dragged around... */
static gint icon_motion_notify(GtkWidget *widget,
			       GdkEventMotion *event,
			       PinIcon *pi)
{
	if (motion_state == MOTION_READY_FOR_DND)
	{
		if (dnd_motion_moved(event))
			start_drag(pi, event);
		return TRUE;
	}

	return FALSE;
}

/* Create one pinboard icon for each icon in the doc */
static void pinboard_load_from_xml(xmlDocPtr doc)
{
	xmlNodePtr node, root;
	char	   *tmp, *label, *path;
	int	   x, y;

	root = xmlDocGetRootElement(doc);

	for (node = root->xmlChildrenNode; node; node = node->next)
	{
		if (node->type != XML_ELEMENT_NODE)
			continue;
		if (strcmp(node->name, "icon") != 0)
			continue;

		tmp = xmlGetProp(node, "x");
		if (!tmp)
			continue;
		x = atoi(tmp);
		g_free(tmp);

		tmp = xmlGetProp(node, "y");
		if (!tmp)
			continue;
		y = atoi(tmp);
		g_free(tmp);

		label = xmlGetProp(node, "label");
		if (!label)
			label = g_strdup("<missing label>");
		path = xmlNodeGetContent(node);
		if (!path)
			path = g_strdup("<missing path>");

		pinboard_pin(path, label, x, y);

		g_free(path);
		g_free(label);
	}
}

/* Called for each line in the pinboard file while loading a new board.
 * Only used for old-format files when converting to XML.
 */
static const char *pin_from_file(gchar *line)
{
	gchar	*leaf = NULL;
	int	x, y, n;

	if (*line == '<')
	{
		gchar	*end;

		end = strchr(line + 1, '>');
		if (!end)
			return _("Missing '>' in icon label");

		leaf = g_strndup(line + 1, end - line - 1);

		line = end + 1;

		while (isspace(*line))
			line++;
		if (*line != ',')
			return _("Missing ',' after icon label");
		line++;
	}

	if (sscanf(line, " %d , %d , %n", &x, &y, &n) < 2)
		return NULL;		/* Ignore format errors */

	pinboard_pin(line + n, leaf, x, y);

	g_free(leaf);

	return NULL;
}

/* Make sure that clicks and drops on the root window come to us...
 * False if an error occurred (ie, someone else is using it).
 */
static gboolean add_root_handlers(void)
{
	GdkWindow	*root;

	if (!proxy_invisible)
	{
		win_button_proxy = gdk_atom_intern("_WIN_DESKTOP_BUTTON_PROXY",
						   FALSE);
		proxy_invisible = gtk_invisible_new();
		gtk_widget_show(proxy_invisible);
		/*
		gdk_window_add_filter(proxy_invisible->window,
					proxy_filter, NULL);
					*/

		g_signal_connect(proxy_invisible, "property_notify_event",
				G_CALLBACK(root_property_event), NULL);
		g_signal_connect(proxy_invisible, "button_press_event",
				G_CALLBACK(root_button_press), NULL);

		/* Drag and drop handlers */
		drag_set_pinboard_dest(proxy_invisible);
		g_signal_connect(proxy_invisible, "drag_motion",
				G_CALLBACK(bg_drag_motion), NULL);
		g_signal_connect(proxy_invisible, "drag_leave",
				G_CALLBACK(bg_drag_leave), NULL);
	}

	root = gdk_window_lookup(GDK_ROOT_WINDOW());
	if (!root)
		root = gdk_window_foreign_new(GDK_ROOT_WINDOW());
		
	if (!setup_xdnd_proxy(GDK_ROOT_WINDOW(), proxy_invisible->window))
		return FALSE;

	/* Forward events from the root window to our proxy window */
	gdk_window_add_filter(gdk_get_default_root_window(),
			      proxy_filter, NULL);
	gdk_window_set_user_data(gdk_get_default_root_window(),
				 proxy_invisible);
	gdk_window_set_events(gdk_get_default_root_window(),
			gdk_window_get_events(gdk_get_default_root_window()) |
				GDK_EXPOSURE_MASK |
				GDK_PROPERTY_CHANGE_MASK);

	forward_root_clicks();
	
	return TRUE;
}

/* See if the window manager is offering to forward root window clicks.
 * If so, grab them. Otherwise, do nothing.
 * Call this whenever the _WIN_DESKTOP_BUTTON_PROXY property changes.
 */
static void forward_root_clicks(void)
{
	click_proxy_gdk_window = find_click_proxy_window();
	if (!click_proxy_gdk_window)
		return;

	/* Events on the wm's proxy are dealt with by our proxy widget */
	gdk_window_set_user_data(click_proxy_gdk_window, proxy_invisible);
	gdk_window_add_filter(click_proxy_gdk_window, proxy_filter, NULL);

	/* The proxy window for clicks sends us button press events with
	 * SubstructureNotifyMask. We need StructureNotifyMask to receive
	 * DestroyNotify events, too.
	 */
	XSelectInput(GDK_DISPLAY(),
			GDK_WINDOW_XWINDOW(click_proxy_gdk_window),
			SubstructureNotifyMask | StructureNotifyMask);
}

/* Write the current state of the pinboard to the current pinboard file */
static void pinboard_save(void)
{
	guchar	*save = NULL;
	guchar	*save_new = NULL;
	GList	*next;
	xmlDocPtr doc = NULL;
	xmlNodePtr root;

	g_return_if_fail(current_pinboard != NULL);

	pinboard_modified = FALSE;
	
	if (strchr(current_pinboard->name, '/'))
		save = g_strdup(current_pinboard->name);
	else
	{
		guchar	*leaf;

		leaf = g_strconcat("pb_", current_pinboard->name, NULL);
		save = choices_find_path_save(leaf, PROJECT, TRUE);
		g_free(leaf);
	}

	if (!save)
		return;

	doc = xmlNewDoc("1.0");
	xmlDocSetRootElement(doc, xmlNewDocNode(doc, NULL, "pinboard", NULL));

	root = xmlDocGetRootElement(doc);

	for (next = current_pinboard->icons; next; next = next->next)
	{
		xmlNodePtr tree;
		PinIcon *pi = (PinIcon *) next->data;
		Icon	*icon = (Icon *) pi;
		char *tmp;

		tree = xmlNewTextChild(root, NULL, "icon", icon->src_path);

		tmp = g_strdup_printf("%d", pi->x);
		xmlSetProp(tree, "x", tmp);
		g_free(tmp);
		
		tmp = g_strdup_printf("%d", pi->y);
		xmlSetProp(tree, "y", tmp);
		g_free(tmp);

		xmlSetProp(tree, "label", icon->item->leafname);
	}

	save_new = g_strconcat(save, ".new", NULL);
	if (save_xml_file(doc, save_new) || rename(save_new, save))
		delayed_error(_("Error saving pinboard %s: %s"),
				save, g_strerror(errno));
	g_free(save_new);

	g_free(save);
	if (doc)
		xmlFreeDoc(doc);
}

/*
 * Filter that translates proxied events from virtual root windows into normal
 * Gdk events for the proxy_invisible widget. Stolen from gmc.
 *
 * Also gets events from the root window.
 */
static GdkFilterReturn proxy_filter(GdkXEvent *xevent,
				    GdkEvent *event,
				    gpointer data)
{
	XEvent 		*xev;
	GdkWindow	*proxy = proxy_invisible->window;
	GdkRectangle	area;

	xev = xevent;

	switch (xev->type) {
		case ButtonPress:
		case ButtonRelease:
			/* Translate button events into events that come from
			 * the proxy window, so that we can catch them as a
			 * signal from the invisible widget.
			 */
			if (xev->type == ButtonPress)
				event->button.type = GDK_BUTTON_PRESS;
			else
				event->button.type = GDK_BUTTON_RELEASE;

			g_object_ref(proxy);

			event->button.window = proxy;
			event->button.send_event = xev->xbutton.send_event;
			event->button.time = xev->xbutton.time;
			event->button.x_root = xev->xbutton.x_root;
			event->button.y_root = xev->xbutton.y_root;
			event->button.x = xev->xbutton.x;
			event->button.y = xev->xbutton.y;
			event->button.state = xev->xbutton.state;
			event->button.button = xev->xbutton.button;
			event->button.axes = NULL;

			return GDK_FILTER_TRANSLATE;

		case Expose:
			area.x = xev->xexpose.x;
			area.y = xev->xexpose.y;
			area.width = xev->xexpose.width;
			area.height = xev->xexpose.height;
			bg_expose(&area);
			return GDK_FILTER_REMOVE;

		case DestroyNotify:
			/* XXX: I have no idea why this helps, but it does! */
			/* The proxy window was destroyed (i.e. the window
			 * manager died), so we have to cope with it
			 */
			if (((GdkEventAny *) event)->window == proxy)
				gdk_window_destroy_notify(proxy);

			return GDK_FILTER_REMOVE;

		default:
			break;
	}

	return GDK_FILTER_CONTINUE;
}

static void snap_to_grid(int *x, int *y)
{
	int step = o_pinboard_grid_step.int_value;

	*x = ((*x + step / 2) / step) * step;
	*y = ((*y + step / 2) / step) * step;
}

/* Convert (x,y) from a centre point to a window position */
static void offset_from_centre(PinIcon *pi,
			       int width, int height,
			       int *x, int *y)
{
	gboolean clamp = o_pinboard_clamp_icons.int_value;

	*x -= width >> 1;
	*y -= height >> 1;
	*x = CLAMP(*x, 0, screen_width - (clamp ? width : 0));
	*y = CLAMP(*y, 0, screen_height - (clamp ? height : 0));
}

/* Convert (x,y) from a window position to a centre point */
static void offset_to_centre(PinIcon *pi,
			     int width, int height,
			     int *x, int *y)
{
  *x += width >> 1;
  *y += height >> 1;
}

/* Same as drag_set_dest(), but for pinboard icons */
static void drag_set_pinicon_dest(PinIcon *pi)
{
	GtkObject	*obj = GTK_OBJECT(pi->widget);

	make_drop_target(pi->widget, 0);

	g_signal_connect(obj, "drag_motion", G_CALLBACK(drag_motion), pi);
	g_signal_connect(obj, "drag_leave", G_CALLBACK(drag_leave), pi);
	g_signal_connect(obj, "drag_end", G_CALLBACK(drag_end), pi);
}

/* Called during the drag when the mouse is in a widget registered
 * as a drop target. Returns TRUE if we can accept the drop.
 */
static gboolean drag_motion(GtkWidget		*widget,
                            GdkDragContext	*context,
                            gint		x,
                            gint		y,
                            guint		time,
			    PinIcon		*pi)
{
	GdkDragAction	action = context->suggested_action;
	char		*type = NULL;
	Icon		*icon = (Icon *) pi;
	DirItem		*item = icon->item;

	if (gtk_drag_get_source_widget(context) == widget)
		goto out;	/* Can't drag something to itself! */

	if (icon->selected)
		goto out;	/* Can't drag a selection to itself */

	type = dnd_motion_item(context, &item);

	if (!item)
		type = NULL;
out:
	/* We actually must pretend to accept the drop, even if the
	 * directory isn't writeable, so that the spring-opening
	 * thing works.
	 */

	/* Don't allow drops to non-writeable directories */
	if (o_dnd_spring_open.int_value == FALSE &&
			type == drop_dest_dir &&
			access(icon->path, W_OK) != 0)
	{
		type = NULL;
	}

	g_dataset_set_data(context, "drop_dest_type", type);
	if (type)
	{
		gdk_drag_status(context, action, time);
		g_dataset_set_data_full(context, "drop_dest_path",
				g_strdup(icon->path), g_free);
		if (type == drop_dest_dir)
			dnd_spring_load(context, NULL);

		pinboard_wink_item(pi, FALSE);
	}

	return type != NULL;
}

static gboolean pinboard_shadow = FALSE;
static gint shadow_x, shadow_y;
#define SHADOW_SIZE (ICON_WIDTH)

static void bg_expose(GdkRectangle *area)
{
	GdkWindow *root;
	static GdkGC *shadow_gc = NULL;
	static GdkColor white, black;

	root = gdk_get_default_root_window();	 /* XXX */

	if (!pinboard_shadow)
	{
		/* XXX: Should just disable the events */
		return;
	}
	
	if (!shadow_gc)
	{
		GdkColormap *cm;
		gboolean success;

		white.red = white.green = white.blue = 0xffff;
		black.red = black.green = black.blue = 0;
		
		cm = gdk_drawable_get_colormap(root);
		shadow_gc = gdk_gc_new(root);

		gdk_colormap_alloc_colors(cm, &white, 1, FALSE, TRUE, &success);
		gdk_colormap_alloc_colors(cm, &black, 1, FALSE, TRUE, &success);
	}

	gdk_gc_set_clip_rectangle(shadow_gc, area);
	gdk_gc_set_foreground(shadow_gc, &white);
	gdk_draw_rectangle(root, shadow_gc, FALSE, shadow_x, shadow_y,
			SHADOW_SIZE, SHADOW_SIZE);
	gdk_gc_set_foreground(shadow_gc, &black);
	gdk_draw_rectangle(root, shadow_gc, FALSE, shadow_x + 1, shadow_y + 1,
			SHADOW_SIZE - 2, SHADOW_SIZE - 2);
	gdk_gc_set_clip_rectangle(shadow_gc, NULL);
}

/* Draw a 'shadow' under an icon being dragged, showing where
 * it will land.
 */
static void pinboard_set_shadow(gboolean on)
{
	GdkWindow *root;
	
	root = gdk_get_default_root_window();

	if (pinboard_shadow)
	{
		gdk_window_clear_area_e(root, shadow_x, shadow_y,
					SHADOW_SIZE + 1, SHADOW_SIZE + 1);
	}

	if (on)
	{
		int	old_x = shadow_x, old_y = shadow_y;

		gdk_window_get_pointer(root, &shadow_x, &shadow_y, NULL);
		snap_to_grid(&shadow_x, &shadow_y);
		shadow_x -= SHADOW_SIZE / 2;
		shadow_y -= SHADOW_SIZE / 2;


		if (pinboard_shadow && shadow_x == old_x && shadow_y == old_y)
			return;

		gdk_window_clear_area_e(root, shadow_x, shadow_y,
					SHADOW_SIZE + 1, SHADOW_SIZE + 1);
	}

	pinboard_shadow = on;
}

/* Called when dragging some pinboard icons finishes */
void pinboard_move_icons(void)
{
	int	x = shadow_x, y = shadow_y;
	PinIcon	*pi = (PinIcon *) pinboard_drag_in_progress;
	int	width, height;

	g_return_if_fail(pi != NULL);

	x += SHADOW_SIZE / 2;
	y += SHADOW_SIZE / 2;
	snap_to_grid(&x, &y);

	if (pi->x == x && pi->y == y)
		return;

	pi->x = x;
	pi->y = y;
	gdk_drawable_get_size(pi->win->window, &width, &height);
	offset_from_centre(pi, width, height, &x, &y);

	gdk_window_move(pi->win->window, x, y);

	pinboard_save();
}

static void drag_leave(GtkWidget	*widget,
                       GdkDragContext	*context,
		       guint32		time,
		       PinIcon		*pi)
{
	pinboard_wink_item(NULL, FALSE);
	dnd_spring_abort();
}

static gboolean bg_drag_leave(GtkWidget		*widget,
			      GdkDragContext	*context,
			      guint32		time,
			      gpointer		data)
{
	pinboard_set_shadow(FALSE);
	return TRUE;
}


static gboolean bg_drag_motion(GtkWidget	*widget,
                               GdkDragContext	*context,
                               gint		x,
                               gint		y,
                               guint		time,
			       gpointer		data)
{
	/* Dragging from the pinboard to the pinboard is not allowed */

	if (!provides(context, text_uri_list))
		return FALSE;

	pinboard_set_shadow(TRUE);
	
	gdk_drag_status(context,
			context->suggested_action == GDK_ACTION_ASK
				? GDK_ACTION_LINK : context->suggested_action,
			time);
	return TRUE;
}

static void drag_end(GtkWidget *widget,
		     GdkDragContext *context,
		     PinIcon *pi)
{
	pinboard_drag_in_progress = NULL;
	if (tmp_icon_selected)
	{
		icon_select_only(NULL);
		tmp_icon_selected = FALSE;
	}
}

/* Something which affects all the icons has changed - reshape
 * and redraw all of them.
 */
static void reshape_all(void)
{
	GList	*next;

	g_return_if_fail(current_pinboard != NULL);

	for (next = current_pinboard->icons; next; next = next->next)
	{
		Icon *icon = (Icon *) next->data;
		pinboard_reshape_icon(icon);
	}
}

/* Turns off the pinboard. Does not call gtk_main_quit. */
static void pinboard_clear(void)
{
	GList	*next;

	g_return_if_fail(current_pinboard != NULL);

	next = current_pinboard->icons;
	while (next)
	{
		PinIcon	*pi = (PinIcon *) next->data;

		next = next->next;

		gtk_widget_destroy(pi->win);
	}
	
	g_free(current_pinboard->name);
	g_free(current_pinboard);
	current_pinboard = NULL;

	release_xdnd_proxy(GDK_ROOT_WINDOW());
	gdk_window_remove_filter(gdk_get_default_root_window(),
				 proxy_filter, NULL);
	gdk_window_set_user_data(gdk_get_default_root_window(), NULL);

	number_of_windows--;
}

static gpointer parent_class;

static void pin_icon_destroy(Icon *icon)
{
	PinIcon *pi = (PinIcon *) icon;

	g_return_if_fail(pi->win != NULL);

	gtk_widget_destroy(pi->win);
}

static void pin_icon_finalize(GObject *icon)
{
	PinIcon *pi = (PinIcon *) icon;

	if (pi->layout)
	{
		g_object_unref(G_OBJECT(pi->layout));
		pi->layout = NULL;
	}

	((GObjectClass *) parent_class)->finalize(icon);
}

static void pinboard_remove_items(void)
{
	g_return_if_fail(icon_selection != NULL);

	while (icon_selection)
		icon_destroy((Icon *) icon_selection->data);

	pinboard_save();
}

static void pin_icon_update(Icon *icon)
{
	pinboard_reshape_icon(icon);
	pinboard_save();
}

static gboolean pin_icon_same_group(Icon *icon, Icon *other)
{
	return IS_PIN_ICON(other);
}

static void pin_icon_class_init(gpointer gclass, gpointer data)
{
	IconClass *icon = (IconClass *) gclass;

	parent_class = g_type_class_peek_parent(gclass);
	
	((GObjectClass *) icon)->finalize = pin_icon_finalize;
	
	icon->destroy = pin_icon_destroy;
	icon->redraw = pinboard_reshape_icon;
	icon->update = pin_icon_update;
	icon->remove_items = pinboard_remove_items;
	icon->same_group = pin_icon_same_group;
}

static void pin_icon_init(GTypeInstance *object, gpointer gclass)
{
	PinIcon *pi = (PinIcon *) object;

	pi->layout = NULL;
	pi->mask = NULL;
}

static GType pin_icon_get_type(void)
{
	static GType type = 0;

	if (!type)
	{
		static const GTypeInfo info =
		{
			sizeof (PinIconClass),
			NULL,			/* base_init */
			NULL,			/* base_finalise */
			pin_icon_class_init,
			NULL,			/* class_finalise */
			NULL,			/* class_data */
			sizeof(PinIcon),
			0,			/* n_preallocs */
			pin_icon_init
		};

		type = g_type_register_static(icon_get_type(),
						"PinIcon", &info, 0);
	}

	return type;
}

static PinIcon *pin_icon_new(const char *pathname, const char *name)
{
	PinIcon *pi;
	Icon	  *icon;

	pi = g_object_new(pin_icon_get_type(), NULL);
	icon = (Icon *) pi;

	icon_set_path(icon, pathname, name);
	
	return pi;
}

/* Called when the window widget is somehow destroyed */
static void pin_icon_destroyed(PinIcon *pi)
{
	g_return_if_fail(pi->win != NULL);

	pi->win = NULL;

	pinboard_wink_item(NULL, FALSE);

	if (pinboard_drag_in_progress == (Icon *) pi)
		pinboard_drag_in_progress = NULL;

	if (current_pinboard)
		current_pinboard->icons =
			g_list_remove(current_pinboard->icons, pi);

	g_object_unref(pi);
}

/* Set the tooltip */
static void pin_icon_set_tip(PinIcon *pi)
{
	XMLwrapper	*ai;
	xmlNode 	*node;
	Icon		*icon = (Icon *) pi;

	g_return_if_fail(pi != NULL);

	ai = appinfo_get(icon->path, icon->item);

	if (ai && ((node = appinfo_get_section(ai, "Summary"))))
	{
		guchar *str;
		str = xmlNodeListGetString(node->doc,
				node->xmlChildrenNode, 1);
		if (str)
		{
			gtk_tooltips_set_tip(tooltips, pi->win, str, NULL);
			g_free(str);
		}
	}
	else
		gtk_tooltips_set_tip(tooltips, pi->widget, NULL, NULL);

	if (ai)
		g_object_unref(ai);
}

static void pinboard_show_menu(GdkEventButton *event, PinIcon *pi)
{
	int		pos[3];

	pos[0] = event->x_root;
	pos[1] = event->y_root;
	pos[2] = 1;

	icon_prepare_menu((Icon *) pi);

	gtk_menu_popup(GTK_MENU(icon_menu), NULL, NULL,
			position_menu,
			(gpointer) pos, event->button, event->time);
}
