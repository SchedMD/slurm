/****************************************************************************\
 *  grid.c - put display grid info here
 *****************************************************************************
 *  Copyright (C) 2002-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>, et. al.
 *  UCRL-CODE-217948.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  In addition, as a special exception, the copyright holders give permission 
 *  to link the code of portions of this program with the OpenSSL library under
 *  certain conditions as described in each individual source file, and 
 *  distribute linked combinations including the two. You must obey the GNU 
 *  General Public License in all respects for all of the code used other than 
 *  OpenSSL. If you modify file(s) with this exception, you may extend this 
 *  exception to your version of the file(s), but you are not obligated to do 
 *  so. If you do not wish to do so, delete this exception statement from your
 *  version.  If you delete this exception statement from all source files in 
 *  the program, then also delete it here.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/
#include "sview.h"

List grid_button_list = NULL;

char *sview_colors[] = {"#0000FF", "#00FF00", "#00FFFF", "#FFFF00", 
			"#FF0000", "#4D4DC6", "#F09A09", "#BDFA19",
			"#715627", "#6A8CA2", "#4C7127", "#25B9B9",
			"#A020F0", "#8293ED", "#FFA500", "#FFC0CB",
			"#8B6914", "#18A24E", "#F827FC", "#B8A40C"};
int sview_colors_cnt = 20;

void *_blink_thr(void *arg)
{
	ListIterator itr = NULL;
	grid_button_t *grid_button = NULL;
	uint16_t node_base_state;
	GdkColor color;
	static bool flash = true;
	while(1) {
		if(flash) {
			flash = false;
			gdk_color_parse("red", &color);
		
		} else {
			flash = true;
			gdk_color_parse("black", &color);
		
		}
		sleep(1);
		gdk_threads_enter();
		itr = list_iterator_create(grid_button_list);
		while((grid_button = list_next(itr))) {
			node_base_state = grid_button->state & NODE_STATE_BASE;
			if ((node_base_state == NODE_STATE_DOWN)
			    || (grid_button->state & NODE_STATE_DRAIN)) {
				gtk_widget_modify_bg(grid_button->button, 
						     GTK_STATE_NORMAL, &color);
			}
		}
		list_iterator_destroy(itr);
		gdk_flush();
		gdk_threads_leave();
	}
	
	return NULL;
}

static void _open_node(GtkWidget *widget, GdkEventButton *event, 
		       grid_button_t *grid_button)
{
	int error_code = SLURM_SUCCESS;
	node_info_msg_t *node_info_ptr = NULL;
	node_info_t *node_ptr = NULL;
	GError *error = NULL;
	char title[100];
	ListIterator itr = NULL;
	popup_info_t *popup_win = NULL;
	
	if((error_code = get_new_info_node(&node_info_ptr, force_refresh))
	   == SLURM_NO_CHANGE_IN_DATA) { 
		goto update_it;
	} 
	if (error_code != SLURM_SUCCESS) {
		printf("slurm_load_node: %s\n", slurm_strerror(error_code));
		return;
	}
update_it:
	node_ptr = &node_info_ptr->node_array[grid_button->indecies];
#ifdef HAVE_BG
	snprintf(title, 100, 
		 "Info about base partition %s", node_ptr->name);
#else
	snprintf(title, 100, "Info about node %s", node_ptr->name);
#endif
	itr = list_iterator_create(popup_list);
	while((popup_win = list_next(itr))) {
		if(popup_win->spec_info)
			if(!strcmp(popup_win->spec_info->title, title)) {
				break;
			} 
	}
	list_iterator_destroy(itr);

	if(!popup_win) {
		popup_win = create_popup_info(PART_PAGE, NODE_PAGE, title);
	}
	popup_win->spec_info->data = g_strdup(node_ptr->name);
	if (!g_thread_create((gpointer)popup_thr, popup_win, FALSE, &error))
	{
		g_printerr ("Failed to create part popup thread: %s\n", 
			    error->message);
		return;
	}	
	return;
}

static void _destroy_grid_button(void *arg)
{
	grid_button_t *grid_button = (grid_button_t *)arg;
	if(grid_button) {
		if(grid_button->button) {
			gtk_widget_destroy(grid_button->button);
			grid_button->button = NULL;
		}
		xfree(grid_button);
	}
}

extern void add_button_to_grid_table(GtkTable *table, char *name, int color)
{
	
}

extern char *change_grid_color(List button_list, int start, int end,
			      int color_inx)
{
	ListIterator itr = NULL;
	grid_button_t *grid_button = NULL;
	uint16_t node_base_state;
	GdkColor color;

	if(!button_list)
		return NULL;

	itr = list_iterator_create(button_list);
	color_inx %= sview_colors_cnt;
	gdk_color_parse(sview_colors[color_inx], &color);
		
	while((grid_button = list_next(itr))) {
		if ((grid_button->indecies < start)
		    ||  (grid_button->indecies > end)) 
			continue;
		node_base_state = grid_button->state & NODE_STATE_BASE;
		if ((node_base_state == NODE_STATE_DOWN)
		    || (grid_button->state & NODE_STATE_DRAIN))
			continue;
		grid_button->color = sview_colors[color_inx];
		gtk_widget_modify_bg(grid_button->button, 
				     GTK_STATE_NORMAL, &color);
	}
	list_iterator_destroy(itr);
	return sview_colors[color_inx];
}

extern void set_grid_size(GtkTable *table, int node_cnt)
{
	int y=0, x=0;
	// FIX ME!!!! these are bad values
#ifndef HAVE_BG
       	x=20;
	y = node_cnt/20;
	if(y < 1)
		y=1;
#else
#endif
	gtk_table_resize(table, y, x);

}

extern int get_system_stats()
{
	int error_code = SLURM_SUCCESS;
	node_info_msg_t *node_info_ptr = NULL;
	int x=0, table_x=0, table_y=0;
	int coord_x=0, coord_y=0, i=0;
	grid_button_t *grid_button = NULL;
/* 	GtkWidget *event_box = NULL; */
	GdkColor color;
	GError *error = NULL;
	
#ifdef HAVE_BG
	int y=0, z=0;
#endif
	if((error_code = get_new_info_node(&node_info_ptr, force_refresh))
	   == SLURM_NO_CHANGE_IN_DATA) { 
		goto update_it;
	} 
	if (error_code != SLURM_SUCCESS) {
		printf("slurm_load_node: %s\n", slurm_strerror(error_code));
		return SLURM_ERROR;
	}
update_it:
	ba_init(node_info_ptr);	
	if(grid_button_list)
		return SLURM_SUCCESS;
	grid_button_list = list_create(_destroy_grid_button);
#ifndef HAVE_BG
	if(DIM_SIZE[X] < 50) {
		table_x = 1;
		table_y = DIM_SIZE[X];
	} else if(DIM_SIZE[X] < 500) {
		table_x = DIM_SIZE[X];
		table_y = 1;
	} else {
		table_x=20;
		table_y = DIM_SIZE[X]/20;
		if(table_y < 1)
			table_y=1;
		else
			table_y++;
	}
#else
	if(DIM_SIZE[X] < 12) {
		table_x = DIM_SIZE[X];
		table_y = 1;
	} else {
		table_x=12;
		table_y = DIM_SIZE[X]/12;
		if(table_y < 1)
			table_y=1;
		else
			table_y++;
	}
#endif
	gtk_table_resize(main_grid_table, table_y, table_x);
	gdk_color_parse("red", &color);
	
	for (x=0; x<DIM_SIZE[X]; x++) {
#ifdef HAVE_BG
		for (y=0; y<DIM_SIZE[Y]; y++) {
			for (z=0; z<DIM_SIZE[Z]; z++){
				grid_button = xmalloc(sizeof(grid_button_t));
				grid_button->coord[X] = x;
				grid_button->coord[Y] = y;
				grid_button->coord[Z] = z;
				grid_button->indecies = i++;
				grid_button->button = gtk_button_new();
				list_push(grid_button_list, grid_button);
				/* FIXME! we need to make sure this
				   gets laid out correctly on Bluegene
				   systems. */
			}
		}
#else
		grid_button = xmalloc(sizeof(grid_button_t));
		grid_button->coord[X] = x;
		grid_button->indecies = i++;
		grid_button->button = gtk_button_new();
		gtk_widget_set_size_request(grid_button->button, 10, 10);
		gtk_widget_modify_fg(grid_button->button, 
				     GTK_STATE_NORMAL, &color);
		g_signal_connect(G_OBJECT(grid_button->button), 
				 "button-press-event",
				 G_CALLBACK(_open_node),
				 grid_button);
		list_push(grid_button_list, grid_button);
		gtk_table_attach(main_grid_table, grid_button->button,
				 coord_x, (coord_x+1), coord_y, (coord_y+1),
				 GTK_SHRINK, GTK_SHRINK,
				 1, 1);
		
		coord_x++;
		if(coord_x == table_x) {
			coord_x = 0;
			coord_y++;
		}
		if(coord_y == table_y)
			break;
#endif
	}
	if (!g_thread_create(_blink_thr, NULL, FALSE, &error))
	{
		g_printerr ("Failed to create page thread: %s\n", 
			    error->message);
		return SLURM_ERROR;
	}
		
	return SLURM_SUCCESS;
}

extern void sview_init_grid()
{
	node_info_msg_t *node_info_ptr = NULL;
	int error_code = SLURM_SUCCESS;
	node_info_t *node_ptr = NULL;
	int i = 0;
	uint16_t node_base_state;
	ListIterator itr = NULL;
	grid_button_t *grid_button = NULL;
	GdkColor color;
	GdkColor color2;
	
	if((error_code = get_new_info_node(&node_info_ptr, force_refresh))
	   == SLURM_NO_CHANGE_IN_DATA) { 
		return;
	} 
	if (error_code != SLURM_SUCCESS) {
		g_print("slurm_load_node: %s\n", slurm_strerror(error_code));
		return;
	}
	if(!grid_button_list) {
		g_print("you need to run get_system_stats() first\n");
		exit(0);
	}
	
	gdk_color_parse("black", &color);
	gdk_color_parse("white", &color2);
	
	itr = list_iterator_create(grid_button_list);
	for(i=0; i<node_info_ptr->record_count; i++) {
		node_ptr = &node_info_ptr->node_array[i];
		list_iterator_reset(itr);
		while((grid_button = list_next(itr))) {
			if (grid_button->indecies != i)
				continue;
			node_base_state = node_ptr->node_state 
				& NODE_STATE_BASE;
			if ((node_base_state == NODE_STATE_DOWN) || 
			    (node_ptr->node_state & NODE_STATE_DRAIN)) {
				gtk_widget_modify_bg(grid_button->button, 
				     GTK_STATE_NORMAL, &color);
			} else 
				gtk_widget_modify_bg(grid_button->button, 
				     GTK_STATE_NORMAL, &color2);
			grid_button->state = node_ptr->node_state;
			break;
		}
	}
	list_iterator_destroy(itr);
}

extern void sview_reset_grid()
{
	grid_button_t *grid_button = NULL;
	uint16_t node_base_state;
	ListIterator itr = NULL;
	GdkColor color;
	
	if(!grid_button_list) {
		g_print("you need to run get_system_stats() first\n");
		exit(0);
	}
	gdk_color_parse("white", &color);
		
	itr = list_iterator_create(grid_button_list);
	while((grid_button = list_next(itr))) {
		node_base_state = grid_button->state & NODE_STATE_BASE;
		if ((node_base_state == NODE_STATE_DOWN)
		    || (grid_button->state & NODE_STATE_DRAIN))
			continue;
		gtk_widget_modify_bg(grid_button->button, 
				     GTK_STATE_NORMAL, &color);
	}
	list_iterator_destroy(itr);
}
