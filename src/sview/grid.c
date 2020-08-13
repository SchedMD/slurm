/****************************************************************************\
 *  grid.c - put display grid info here
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>, et. al.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include "config.h"

#include "sview.h"

#define RESET_GRID -2
#define TOPO_DEBUG  0

List grid_button_list = NULL;
List blinking_button_list = NULL;
List multi_button_list = NULL;

char *sview_colors[] = {"#0000FF", "#00FF00", "#00FFFF", "#FFFF00",
			"#FF0000", "#4D4DC6", "#F09A09", "#BDFA19",
			"#715627", "#6A8CA2", "#4C7127", "#25B9B9",
			"#A020F0", "#8293ED", "#FFA500", "#FFC0CB",
			"#8B6914", "#18A24E", "#F827FC", "#B8A40C"};
char *blank_color = "#919191";
char *white_color = "#FFFFFF";
char *topo1_color = "honeydew"; //"seashell";DarkTurquoise
char *topo2_color = "gray94";//honeydew";

int sview_colors_cnt = 20;

typedef struct {
	int node_inx_id;
	int color_inx_id;
	List button_list;
} grid_foreach_t;

typedef struct {
	List button_list;
	int *coord_x;
	int *coord_y;
	int default_y_offset;
	grid_button_t *grid_button;
	int *inx;
	GtkTable *table;
	int table_y;
	bool force_row_break;
} button_processor_t;

static gboolean _mouseover_node(GtkWidget *widget, GdkEventButton *event,
				grid_button_t *grid_button)
{
	gboolean rc = true;

	grid_button->last_state = gtk_widget_get_state(widget);
#ifdef GTK2_USE_TOOLTIP
	gtk_widget_set_tooltip_text(grid_button->button,
				    grid_button->node_name);
#else
	if (!grid_button->tip)
		grid_button->tip = gtk_tooltips_new();
	gtk_tooltips_set_tip(grid_button->tip,
			     grid_button->button,
			     grid_button->node_name,
			     "click for node stats");
#endif
	//g_print("on at %s\n", grid_button->node_name);
	gtk_widget_set_state(grid_button->button, GTK_STATE_PRELIGHT);

	return rc;
}

static gboolean _mouseoff_node(GtkWidget *widget, GdkEventButton *event,
			       grid_button_t *grid_button)
{
	gboolean rc = false;

	if (grid_button->last_state == GTK_STATE_ACTIVE) {
		gtk_widget_set_state(grid_button->button, GTK_STATE_ACTIVE);
		rc = true;
		//g_print("off of %s\n", grid_button->node_name);
	}
	return rc;
}

static gboolean _open_node(GtkWidget *widget, GdkEventButton *event,
			   grid_button_t *grid_button)
{
	if (event->button == 1) {
		popup_all_node_name(grid_button->node_name, INFO_PAGE, NULL);
	} else if (event->button == 3) {
		/* right click */
		admin_menu_node_name(grid_button->node_name, event);
	}

	return false;
}

/* static void _state_changed(GtkWidget *button, GtkStateType state, */
/* 			   grid_button_t *grid_button) */
/* { */
/* 	g_print("state of %s is now %d\n", grid_button->node_name, state); */
/* } */

static void _add_button_signals(grid_button_t *grid_button)
{
	/* g_signal_connect(G_OBJECT(grid_button->button), */
	/* 		 "state-changed", */
	/* 		 G_CALLBACK(_state_changed), */
	/* 		 grid_button); */
	g_signal_connect(G_OBJECT(grid_button->button),
			 "button-press-event",
			 G_CALLBACK(_open_node),
			 grid_button);
	g_signal_connect(G_OBJECT(grid_button->button),
			 "enter-notify-event",
			 G_CALLBACK(_mouseover_node),
			 grid_button);
	g_signal_connect(G_OBJECT(grid_button->button),
			 "leave-notify-event",
			 G_CALLBACK(_mouseoff_node),
			 grid_button);
}

/*
 * Comparator used for sorting buttons
 *
 * returns: -1: button_a->inx > button_b->inx
 *           0: rec_a == rec_b
 *           1: rec_a < rec_b
 *
 */
static int _sort_button_inx(void *b1, void *b2)
{
	grid_button_t *button_a = *(grid_button_t **)b1;
	grid_button_t *button_b = *(grid_button_t **)b2;
	int inx_a;
	int inx_b;

	inx_a = button_a->inx;
	inx_b = button_b->inx;

	if (inx_a < inx_b)
		return -1;
	else if (inx_a > inx_b)
		return 1;
	return 0;
}

void _put_button_as_down(grid_button_t *grid_button, int state)
{
	GtkWidget *image = NULL;
/* 	GdkColor color; */

	if (GTK_IS_EVENT_BOX(grid_button->button)) {
		//gtk_widget_set_sensitive (grid_button->button, true);
		return;
	}

	gtk_widget_destroy(grid_button->button);
	grid_button->color = NULL;
	grid_button->color_inx = MAKE_DOWN;
	grid_button->button = gtk_event_box_new();
	gtk_widget_set_size_request(grid_button->button,
				    working_sview_config.button_size,
				    working_sview_config.button_size);
	gtk_event_box_set_above_child(GTK_EVENT_BOX(grid_button->button),
				      false);
	_add_button_signals(grid_button);

/* 	if (grid_button->frame) */
/* 		gtk_container_add(GTK_CONTAINER(grid_button->frame), */
/* 				  grid_button->button); */
	if (grid_button->table)
		gtk_table_attach(grid_button->table, grid_button->button,
				 grid_button->table_x,
				 (grid_button->table_x+1),
				 grid_button->table_y,
				 (grid_button->table_y+1),
				 GTK_SHRINK, GTK_SHRINK,
				 1, 1);

	//gdk_color_parse("black", &color);
	//sview_widget_modify_bg(grid_button->button, GTK_STATE_NORMAL, color);
	//gdk_color_parse(white_color, &color);
	//sview_widget_modify_bg(grid_button->button, GTK_STATE_ACTIVE, color);
	if (state == NODE_STATE_DRAIN)
		image = gtk_image_new_from_stock(GTK_STOCK_DIALOG_ERROR,
						 GTK_ICON_SIZE_SMALL_TOOLBAR);
	else
		image = gtk_image_new_from_stock(GTK_STOCK_CANCEL,
						 GTK_ICON_SIZE_SMALL_TOOLBAR);
	gtk_container_add(GTK_CONTAINER(grid_button->button), image);
	gtk_widget_show_all(grid_button->button);
	return;
}


void _put_button_as_up(grid_button_t *grid_button)
{
	if (GTK_IS_BUTTON(grid_button->button)) {
		return;
	}
	gtk_widget_destroy(grid_button->button);
	grid_button->button = gtk_button_new();
	gtk_widget_set_size_request(grid_button->button,
				    working_sview_config.button_size,
				    working_sview_config.button_size);
	_add_button_signals(grid_button);

/* 	if (grid_button->frame) */
/* 		gtk_container_add(GTK_CONTAINER(grid_button->frame), */
/* 				  grid_button->button); */
	if (grid_button->table)
		gtk_table_attach(grid_button->table, grid_button->button,
				 grid_button->table_x,
				 (grid_button->table_x+1),
				 grid_button->table_y,
				 (grid_button->table_y+1),
				 GTK_SHRINK, GTK_SHRINK,
				 1, 1);
	gtk_widget_show_all(grid_button->button);
	return;
}

void _put_button_as_inactive(grid_button_t *grid_button)
{
	if (GTK_IS_BUTTON(grid_button->button)) {
		//gtk_widget_set_sensitive (grid_button->button, false);
		return;
	}
	gtk_widget_destroy(grid_button->button);
	grid_button->button = gtk_button_new();
	gtk_widget_set_size_request(grid_button->button,
				    working_sview_config.button_size,
				    working_sview_config.button_size);
	//gtk_widget_set_sensitive (grid_button->button, false);

	_add_button_signals(grid_button);

/* 	if (grid_button->frame) */
/* 		gtk_container_add(GTK_CONTAINER(grid_button->frame), */
/* 				  grid_button->button); */
	if (grid_button->table)
		gtk_table_attach(grid_button->table, grid_button->button,
				 grid_button->table_x,
				 (grid_button->table_x+1),
				 grid_button->table_y,
				 (grid_button->table_y+1),
				 GTK_SHRINK, GTK_SHRINK,
				 1, 1);
	gtk_widget_show_all(grid_button->button);
	return;
}

static bool _change_button_color(grid_button_t *grid_button,
				 int color_inx, char *new_col, GdkColor color,
				 bool only_change_unused,
				 enum node_states state_override)
{
	enum node_states state;
	uint16_t node_base_state;
	bool changed = 0;

	xassert(grid_button);
	if (only_change_unused && grid_button->used)
		return 0;

	grid_button->used = true;
	if (color_inx == MAKE_BLACK) {
		if (grid_button->color_inx != color_inx) {
			_put_button_as_inactive(grid_button);
			grid_button->color = new_col;
			grid_button->color_inx = color_inx;
			sview_widget_modify_bg(grid_button->button,
					       GTK_STATE_NORMAL, color);
/* 				sview_widget_modify_bg(grid_button->button,  */
/* 						       GTK_STATE_ACTIVE, */
/* 						       color); */
			changed = 1;
		}

		return changed;
	}

	if (state_override != NODE_STATE_UNKNOWN)
		state = state_override;
	else
		state = grid_button->state;

	node_base_state = state & NODE_STATE_BASE;

	if (node_base_state == NODE_STATE_DOWN) {
		_put_button_as_down(grid_button, NODE_STATE_DOWN);
	} else if (state & NODE_STATE_DRAIN) {
		_put_button_as_down(grid_button, NODE_STATE_DRAIN);
	} else if (grid_button->node_name &&
		   !xstrcmp(grid_button->node_name, "EMPTY")) {
		grid_button->color_inx = MAKE_BLACK;
//		_put_button_as_up(grid_button);
	} else if (grid_button->color_inx != color_inx) {
		_put_button_as_up(grid_button);
		grid_button->color = new_col;
		grid_button->color_inx = color_inx;
		sview_widget_modify_bg(grid_button->button,
				       GTK_STATE_NORMAL, color);
/* 			sview_widget_modify_bg(grid_button->button,  */
/* 					       GTK_STATE_ACTIVE, color); */
		changed = 1;
	}

	return changed;
}


static void _each_highlightd(GtkTreeModel *model,
			     GtkTreePath *path,
			     GtkTreeIter *iter,
			     gpointer userdata)
{
	ListIterator itr = NULL;
	grid_button_t *grid_button = NULL;
	int *node_inx = NULL;
	int color_inx;

	int j=0;
	GdkColor color;

	grid_foreach_t *grid_foreach = userdata;

	gtk_tree_model_get(model, iter, grid_foreach->node_inx_id,
			   &node_inx, -1);
	gtk_tree_model_get(model, iter, grid_foreach->color_inx_id,
			   &color_inx, -1);

	if (!node_inx)
		return;

	if (color_inx > sview_colors_cnt) {
		g_print("hey the color_inx from %d was set to %d > %d\n",
			grid_foreach->color_inx_id, color_inx,
			sview_colors_cnt);
		color_inx %= sview_colors_cnt;
	}
	gdk_color_parse(sview_colors[color_inx], &color);

	itr = list_iterator_create(grid_foreach->button_list);
	while ((grid_button = list_next(itr))) {
		/*For multiple selections, need to retain all selected.
		 *(previously this assumed only one selected).
		 */

		if ((node_inx[j] < 0)
		    || (grid_button->inx < node_inx[j])
		    || (grid_button->inx > node_inx[j+1]))
			continue;

		(void)_change_button_color(grid_button, color_inx,
				     sview_colors[color_inx],
				     color, 0, 0);

		if (gtk_widget_get_state(grid_button->button) != GTK_STATE_NORMAL)
			gtk_widget_set_state(grid_button->button,
					     GTK_STATE_NORMAL);

		if (grid_button->inx == node_inx[j+1])
			j+=2;

	}

	list_iterator_destroy(itr);
	return;
}

static void _each_highlight_selected(GtkTreeModel *model,
				     GtkTreePath *path,
				     GtkTreeIter *iter,
				     gpointer userdata)
{

	grid_button_t *grid_button = NULL;
	int node_inx = 0;
	bool speedup_break = true;
	grid_foreach_t *grid_foreach = userdata;
	ListIterator itr = NULL;

	xassert(grid_foreach);
	if (working_sview_config.grid_topological)
		speedup_break = false;

	gtk_tree_model_get(model, iter, grid_foreach->node_inx_id,
			   &node_inx, -1);

	if (node_inx < 0 || !grid_foreach->button_list)
		return;
	itr = list_iterator_create(grid_foreach->button_list);
	while ((grid_button = list_next(itr))) {
		/* For multiple selections, need to retain all selected.
		 * (previously this assumed only one selected). */
		if (grid_button->inx != node_inx)
			continue;
		else if (gtk_widget_get_state(grid_button->button)
			 != GTK_STATE_NORMAL) {
			gtk_widget_set_state(grid_button->button,
					     GTK_STATE_NORMAL);
			change_grid_color(grid_button_list, grid_button->inx,
					  grid_button->inx,
					  grid_button->inx, true, 0);
		}
		if (speedup_break)
			break;
		speedup_break = true; //allow for secondary grid button
	}
	list_iterator_destroy(itr);
	return;

}

/*
 * This is used to add an entry to the grid for a node which is not configured
 * in the system (e.g. there is a gap in the 3-D torus for a service or login
 * node.
 */
static void _build_empty_node(int coord_x, int coord_y,
			      button_processor_t *button_processor)
{
	grid_button_t *grid_button;

	(*button_processor->coord_x) = coord_x;
	(*button_processor->coord_y) = coord_y;
	grid_button = xmalloc(sizeof(grid_button_t));
	grid_button->color_inx = MAKE_BLACK;
	grid_button->inx = (*button_processor->inx);
	grid_button->state = NODE_STATE_FUTURE;
	grid_button->table = button_processor->table;
	grid_button->table_x = (*button_processor->coord_x);
	grid_button->table_y = (*button_processor->coord_y);
	grid_button->button = gtk_button_new();
	grid_button->node_name = xstrdup("EMPTY");	/* Needed by popups */

	gtk_widget_set_state(grid_button->button, GTK_STATE_ACTIVE);
	list_append(button_processor->button_list, grid_button);

	gtk_table_attach(button_processor->table, grid_button->button,
			 (*button_processor->coord_x),
			 ((*button_processor->coord_x) + 1),
			 (*button_processor->coord_y),
			 ((*button_processor->coord_y) + 1),
			 GTK_SHRINK, GTK_SHRINK, 1, 1);
}

static void _calc_coord_3d(int x, int y, int z, int default_y_offset,
			   int *coord_x, int *coord_y, int *dim_size)
{
	int y_offset;

	*coord_x = (x + (dim_size[2] - 1)) - z;
	y_offset = default_y_offset - (dim_size[2] * y);
	*coord_y = (y_offset - y) + z;
}

/* Add a button for a given node. If node_ptr == NULL then fill in any gaps
 * in the grid just for a clean look. Always call with node_ptr == NULL for
 * the last call in the sequence. */
static int _add_button_to_list(node_info_t *node_ptr,
			       button_processor_t *button_processor)
{
	static bool *node_exists = NULL;
	static int node_exists_cnt = 1;
	grid_button_t *grid_button = button_processor->grid_button;
	int *dim_size = NULL, i, coord_x = 0, coord_y = 0;
	int len = 0;

	if (cluster_dims > 1) {
		dim_size = slurmdb_setup_cluster_dim_size();
		if (dim_size == NULL) {
			g_error("Could not read dim_size\n");
			return SLURM_ERROR;
		}
		if ((dim_size[0] < 1) || (cluster_dims < 1)) {
			g_error("Invalid dim_size %d or cluster_dims %d\n",
				dim_size[0], cluster_dims);
			return SLURM_ERROR;
		}

		/* Translate a 3D or 4D space into a 2D space to the extent
		 * possible. */
		if (node_exists == NULL) {
			node_exists_cnt = 1;
			for (i = 0; i < cluster_dims; i++)
				node_exists_cnt *= dim_size[i];
			node_exists = xmalloc(sizeof(bool) * node_exists_cnt);
		}
		if (node_ptr) {
			len = strlen(node_ptr->name);
			if (len < cluster_dims) {
				g_error("bad node name %s\n", node_ptr->name);
				return SLURM_ERROR;
			}
		}
	}

	if (cluster_dims == 3) {
		int x, y, z;
		if (node_ptr) {
			x = select_char2coord(node_ptr->name[len-3]);
			y = select_char2coord(node_ptr->name[len-2]);
			z = select_char2coord(node_ptr->name[len-1]);

			i = (x * dim_size[1] + y) * dim_size[2] + z;
			node_exists[i] = true;
			_calc_coord_3d(x, y, z,
				       button_processor->default_y_offset,
				       &coord_x, &coord_y, dim_size);
		} else {
			for (x = 0; x < dim_size[0]; x++) {
				for (y = 0; y < dim_size[1]; y++) {
					for (z = 0; z < dim_size[2]; z++) {
						i = (x * dim_size[1] + y) *
							dim_size[2] + z;
						if (node_exists[i])
							continue;
						_calc_coord_3d(x, y, z,
				      			button_processor->
							default_y_offset,
							&coord_x, &coord_y,
							dim_size);
						_build_empty_node(
							coord_x, coord_y,
							button_processor);
					}
				}
			}
			xfree(node_exists);
			return SLURM_SUCCESS;
		}
	}
	if (node_ptr == NULL)
		return SLURM_SUCCESS;

	if (cluster_dims > 1) {
		(*button_processor->coord_x) = coord_x;
		(*button_processor->coord_y) = coord_y;
#if 0
		g_print("%s %d:%d\n", node_ptr->name, coord_x, coord_y);
#endif
	}

	if (!grid_button) {
		grid_button = xmalloc(sizeof(grid_button_t));
		grid_button->color_inx = MAKE_INIT;
		grid_button->inx = (*button_processor->inx);
		grid_button->table = button_processor->table;
		grid_button->table_x = (*button_processor->coord_x);
		grid_button->table_y = (*button_processor->coord_y);
		grid_button->button = gtk_button_new();
		grid_button->node_name = xstrdup(node_ptr->name);

		gtk_widget_set_size_request(grid_button->button,
					    working_sview_config.button_size,
					    working_sview_config.button_size);
		_add_button_signals(grid_button);
		list_append(button_processor->button_list, grid_button);

		gtk_table_attach(button_processor->table, grid_button->button,
				 (*button_processor->coord_x),
				 ((*button_processor->coord_x)+1),
				 (*button_processor->coord_y),
				 ((*button_processor->coord_y)+1),
				 GTK_SHRINK, GTK_SHRINK,
				 1, 1);
	} else {
		grid_button->table_x = (*button_processor->coord_x);
		grid_button->table_y = (*button_processor->coord_y);
		gtk_container_child_set(
			GTK_CONTAINER(button_processor->table),
			grid_button->button,
			"left-attach", (*button_processor->coord_x),
			"right-attach", ((*button_processor->coord_x)+1),
			"top-attach", (*button_processor->coord_y),
			"bottom-attach", ((*button_processor->coord_y)+1),
			NULL);
	}
	/* gtk_container_add(GTK_CONTAINER(grid_button->frame),  */
/* 				  grid_button->button); */
/* 		gtk_frame_set_shadow_type(GTK_FRAME(grid_button->frame), */
/* 					  GTK_SHADOW_ETCHED_OUT); */
	if (cluster_dims < 3) {
		/* On linear systems we just up the x_coord until we hit the
		 * side of the table and then increment the coord_y.  We add
		 * space between each tenth row. */
		(*button_processor->coord_x)++;

		if (button_processor->force_row_break) {
			(*button_processor->coord_x) = 0;
			(*button_processor->coord_y)++;
			gtk_table_set_row_spacing(
				button_processor->table,
				(*button_processor->coord_y)-1,
				working_sview_config.gap_size);
			return SLURM_SUCCESS;
		}

		if ((*button_processor->coord_x)
		    == working_sview_config.grid_x_width) {
			(*button_processor->coord_x) = 0;
			(*button_processor->coord_y)++;
			if (!((*button_processor->coord_y)
			      % working_sview_config.grid_vert))
				gtk_table_set_row_spacing(
					button_processor->table,
					(*button_processor->coord_y)-1,
					working_sview_config.gap_size);
		}

		if ((*button_processor->coord_y) == button_processor->table_y)
			return SLURM_SUCCESS;

		if ((*button_processor->coord_x) &&
		    !((*button_processor->coord_x)
		      % working_sview_config.grid_hori))
			gtk_table_set_col_spacing(
				button_processor->table,
				(*button_processor->coord_x)-1,
				working_sview_config.gap_size);
	}
	return SLURM_SUCCESS;
}

static int _grid_table_by_switch(button_processor_t *button_processor,
				 List node_list)
{
	int rc = SLURM_SUCCESS;
	int inx = 0, ii = 0;
	switch_record_bitmaps_t *sw_nodes_bitmaps_ptr = g_switch_nodes_maps;
#if TOPO_DEBUG
	/* engage if want original display below switched */
	ListIterator itr = list_iterator_create(node_list);
	sview_node_info_t *sview_node_info_ptr = NULL;
#endif
	button_processor->inx = &inx;
	for (ii=0; ii<g_topo_info_msg_ptr->record_count;
	     ii++, sw_nodes_bitmaps_ptr++) {
		int j = 0, first, last;
		if (g_topo_info_msg_ptr->topo_array[ii].level)
			continue;
		first = bit_ffs(sw_nodes_bitmaps_ptr->node_bitmap);
		if (first == -1)
			continue;
		last = bit_fls(sw_nodes_bitmaps_ptr->node_bitmap);
		button_processor->inx = &j;
		button_processor->force_row_break = false;
		for (j = first; j <= last; j++) {
			if (TOPO_DEBUG)
				g_print("allocated node = %s button# %d\n",
					g_node_info_ptr->node_array[j].name,
					j);
			if (!bit_test(sw_nodes_bitmaps_ptr->node_bitmap, j))
				continue;
			/* if (!working_sview_config.show_hidden) { */
			/* 	if (!check_part_includes_node(j)) */
			/* 		continue; */
			/* } */
			if (j == last)
				button_processor->force_row_break = true;
			if ((rc = _add_button_to_list(
				     &g_node_info_ptr->node_array[j],
				     button_processor)) != SLURM_SUCCESS)
				break;
			button_processor->force_row_break = false;
		}
		rc = _add_button_to_list(NULL, button_processor);
	}

#if TOPO_DEBUG
	/* engage this if want original display below
	 * switched grid */
	 button_processor->inx = &inx;
	 while ((sview_node_info_ptr = list_next(itr))) {
		 if ((rc = _add_button_to_list(
				sview_node_info_ptr->node_ptr,
	 			button_processor)) != SLURM_SUCCESS)
			 break;
	 	inx++;
	 }
	 list_iterator_destroy(itr);
#endif

	/* This is needed to get the correct width of the grid window.
	 * If it is not given then we get a really narrow window. */
	gtk_table_set_row_spacing(button_processor->table,
				  (*button_processor->coord_y)?
				  ((*button_processor->coord_y)-1):0, 1);

	return rc;

}

static int _grid_table_by_list(button_processor_t *button_processor,
			       List node_list)
{
	sview_node_info_t *sview_node_info_ptr = NULL;
	int inx = 0, rc = SLURM_SUCCESS;
	ListIterator itr = list_iterator_create(node_list);
	button_processor->inx = &inx;

	while ((sview_node_info_ptr = list_next(itr))) {
		/* if (!working_sview_config.show_hidden) { */
		/* 	if (!check_part_includes_node(inx)) { */
		/* 		inx++; */
		/* 		continue; */
		/* 	} */
		/* } */
		if ((rc = _add_button_to_list(
			     sview_node_info_ptr->node_ptr,
			     button_processor)) != SLURM_SUCCESS)
			break;
		inx++;
	}
	list_iterator_destroy(itr);
	rc = _add_button_to_list(NULL, button_processor);

	/* This is needed to get the correct width of the grid window.
	 * If it is not given then we get a really narrow window. */
	gtk_table_set_row_spacing(button_processor->table,
				  (*button_processor->coord_y)?
				  ((*button_processor->coord_y)-1):0, 1);


	return rc;
}

static int _init_button_processor(button_processor_t *button_processor,
				  int node_count)
{
	int *dim_size = NULL;

	if (node_count == 0) {
		g_print("_init_button_processor: no nodes selected\n");
		return SLURM_ERROR;
	}

	memset(button_processor, 0, sizeof(button_processor_t));

	if (cluster_dims > 1) {
		dim_size = slurmdb_setup_cluster_dim_size();
		if (dim_size == NULL) {
			g_error("could not read dim_size\n");
			return SLURM_ERROR;
		}
	}

	if (cluster_dims == 3) {
		button_processor->default_y_offset = (dim_size[2] * dim_size[1])
			+ (dim_size[1] - dim_size[2]);
		working_sview_config.grid_x_width = dim_size[0] + dim_size[2];
		button_processor->table_y = (dim_size[2] * dim_size[1])
					    + dim_size[1];
	} else {
		if (!working_sview_config.grid_x_width) {
			if (node_count < 50) {
				working_sview_config.grid_x_width = 1;
			} else if (node_count < 500) {
				working_sview_config.grid_x_width = 10;
			} else {
				working_sview_config.grid_x_width = 20;
			}
		}
		button_processor->table_y =
			(node_count / working_sview_config.grid_x_width) + 1;
	}

	button_processor->force_row_break = false;

	return SLURM_SUCCESS;
}
/* static void _destroy_grid_foreach(void *arg) */
/* { */
/* 	grid_foreach_t *grid_foreach = (grid_foreach_t *)arg; */

/* 	if (grid_foreach) { */
/* 		xfree(grid_foreach); */
/* 	} */
/* } */

extern void destroy_grid_button(void *arg)
{
	grid_button_t *grid_button = (grid_button_t *)arg;
	if (grid_button) {
		if (grid_button->button) {
			gtk_widget_destroy(grid_button->button);
			grid_button->button = NULL;
		}
		xfree(grid_button->node_name);
		xfree(grid_button);
	}
}

/* we don't set the call back for the button here because sometimes we
 * need to get a different call back based on what we are doing with
 * the button, an example of this would be in
 * add_extra_bluegene_buttons were the small block buttons do
 * something different than they do regularly
 * TODO - this may be simplified now that bluegene is gone.
 */

extern grid_button_t *create_grid_button_from_another(
	grid_button_t *grid_button, char *name, int color_inx)
{
	grid_button_t *send_grid_button = NULL;
	GdkColor color;
	uint16_t node_base_state;
	char *new_col = NULL;

	if (!grid_button || !name)
		return NULL;
	if (color_inx >= 0) {
		color_inx %= sview_colors_cnt;
		new_col = sview_colors[color_inx];
	} else if (color_inx == MAKE_BLACK)
		new_col = blank_color;
	else
		new_col = white_color;

	send_grid_button = xmalloc(sizeof(grid_button_t));
	memcpy(send_grid_button, grid_button, sizeof(grid_button_t));
	node_base_state = send_grid_button->state & NODE_STATE_BASE;
	send_grid_button->color_inx = color_inx;

	/* need to set the table to empty because we will want to fill
	   this into the new table later */
	send_grid_button->table = NULL;
	if (color_inx == MAKE_BLACK) {
		send_grid_button->button = gtk_button_new();
		//gtk_widget_set_sensitive (send_grid_button->button, false);
		gdk_color_parse(new_col, &color);
		send_grid_button->color = new_col;
		sview_widget_modify_bg(send_grid_button->button,
				       GTK_STATE_NORMAL, color);
/* 		sview_widget_modify_bg(send_grid_button->button,  */
/* 				       GTK_STATE_ACTIVE, color); */
	} else if ((color_inx >= 0) && node_base_state == NODE_STATE_DOWN) {
		GtkWidget *image = gtk_image_new_from_stock(
			GTK_STOCK_CANCEL,
			GTK_ICON_SIZE_SMALL_TOOLBAR);
		send_grid_button->button = gtk_event_box_new();
		gtk_event_box_set_above_child(
			GTK_EVENT_BOX(send_grid_button->button),
			false);
		gdk_color_parse("black", &color);
		sview_widget_modify_bg(send_grid_button->button,
				       GTK_STATE_NORMAL, color);
		//gdk_color_parse("white", &color);
/* 		sview_widget_modify_bg(send_grid_button->button,  */
/* 				     GTK_STATE_ACTIVE, color); */
		gtk_container_add(
			GTK_CONTAINER(send_grid_button->button),
			image);
	} else if ((color_inx >= 0)
		   && (send_grid_button->state & NODE_STATE_DRAIN)) {
		GtkWidget *image = gtk_image_new_from_stock(
			GTK_STOCK_DIALOG_ERROR,
			GTK_ICON_SIZE_SMALL_TOOLBAR);

		send_grid_button->button = gtk_event_box_new();
		gtk_event_box_set_above_child(
			GTK_EVENT_BOX(send_grid_button->button),
			false);
		gdk_color_parse("black", &color);
/* 		sview_widget_modify_bg(send_grid_button->button,  */
/* 				       GTK_STATE_NORMAL, color); */
		//gdk_color_parse("white", &color);
/* 		sview_widget_modify_bg(send_grid_button->button,  */
/* 				       GTK_STATE_ACTIVE, color); */
		gtk_container_add(
			GTK_CONTAINER(send_grid_button->button),
			image);
	} else {
		send_grid_button->button = gtk_button_new();
		send_grid_button->color = new_col;
		gdk_color_parse(new_col, &color);
		sview_widget_modify_bg(send_grid_button->button,
				       GTK_STATE_NORMAL, color);
/* 		sview_widget_modify_bg(send_grid_button->button,  */
/* 				       GTK_STATE_ACTIVE, color); */
	}
	gtk_widget_set_size_request(send_grid_button->button,
				    working_sview_config.button_size,
				    working_sview_config.button_size);

	send_grid_button->node_name = xstrdup(name);

	return send_grid_button;
}

/* start == -1 for all */
extern void change_grid_color(List button_list, int start, int end,
			      int color_inx, bool only_change_unused,
			      enum node_states state_override)
{
	ListIterator itr = NULL;
	grid_button_t *grid_button = NULL;
	GdkColor color;
	char *new_col = NULL;

	if (!button_list)
		return;

	if (color_inx >= 0) {
		color_inx %= sview_colors_cnt;
		new_col = sview_colors[color_inx];
	} else if (color_inx == MAKE_BLACK) {
		new_col = blank_color;
	} else if (color_inx == MAKE_TOPO_1) {
		new_col = topo1_color;
	} else if (color_inx == MAKE_TOPO_2) {
		new_col = topo2_color;
	} else
		new_col = white_color;

	gdk_color_parse(new_col, &color);

	itr = list_iterator_create(button_list);
	while ((grid_button = list_next(itr))) {
		if ((start != -1) &&
		    ((grid_button->inx < start) || (grid_button->inx > end)))
			continue;
		_change_button_color(grid_button, color_inx, new_col,
				     color, only_change_unused, state_override);
	}
	list_iterator_destroy(itr);
}

/* This variation of change_grid_color() is faster when changing many
 * button colors at the same time since we can issue a single call to
 * _change_button_color() and eliminate a nested loop. */
extern void change_grid_color_array(List button_list, int array_len,
				    int *color_inx, bool *color_set_flag,
				    bool only_change_unused,
				    enum node_states state_override)
{
	ListIterator itr = NULL;
	grid_button_t *grid_button = NULL;
	GdkColor color;
	char *new_col = NULL;

	if (!button_list)
		return;

	itr = list_iterator_create(button_list);
	while ((grid_button = list_next(itr))) {
		if ((grid_button->inx < 0) || (grid_button->inx >= array_len))
			continue;
		if (!color_set_flag[grid_button->inx])
			continue;

		if (color_inx[grid_button->inx] >= 0) {
			color_inx[grid_button->inx] %= sview_colors_cnt;
			new_col = sview_colors[color_inx[grid_button->inx]];
		} else if (color_inx[grid_button->inx] == MAKE_BLACK) {
			new_col = blank_color;
		} else if (color_inx[grid_button->inx] == MAKE_TOPO_1) {
			new_col = topo1_color;
		} else if (color_inx[grid_button->inx] == MAKE_TOPO_2) {
			new_col = topo2_color;
		} else
			new_col = white_color;
		gdk_color_parse(new_col, &color);

		_change_button_color(grid_button, color_inx[grid_button->inx],
				     new_col, color, only_change_unused,
				     state_override);
	}
	list_iterator_destroy(itr);
	return;
}

extern void highlight_grid(GtkTreeView *tree_view,
			   int node_inx_id, int color_inx_id, List button_list)
{
	ListIterator itr = NULL;
	grid_button_t *grid_button = NULL;
	grid_foreach_t grid_foreach;

	if (!button_list || !tree_view)
		return;

	/*first clear all grid buttons*/
	itr = list_iterator_create(button_list);
	while ((grid_button = list_next(itr))) {
		/* clear everyone */
		if ((gtk_widget_get_state(grid_button->button)
		     != GTK_STATE_ACTIVE)) {
			gtk_widget_set_state(grid_button->button,
					     GTK_STATE_ACTIVE);
		}
	}
	list_iterator_destroy(itr);

	/* for each currently selected row,go back & ensure the
	 * corresponding grid button is highlighted */
	memset(&grid_foreach, 0, sizeof(grid_foreach_t));
	grid_foreach.node_inx_id = node_inx_id;
	grid_foreach.color_inx_id = color_inx_id;
	grid_foreach.button_list = button_list;
	if (grid_foreach.color_inx_id != (int)NO_VAL)
		gtk_tree_selection_selected_foreach(
			gtk_tree_view_get_selection(tree_view),
			_each_highlightd, &grid_foreach);
	else
		gtk_tree_selection_selected_foreach(
			gtk_tree_view_get_selection(tree_view),
			_each_highlight_selected, &grid_foreach);

	return;
}

/* start == -1 for all */
extern void highlight_grid_range(int start, int end, List button_list)
{
	ListIterator itr = NULL;
	grid_button_t *grid_button = NULL;

	if (!button_list)
		return;

	itr = list_iterator_create(button_list);
	while ((grid_button = list_next(itr))) {
		if (start != -1)
			if ((grid_button->inx < start)
			    || (grid_button->inx > end)) {
				/* clear everyone else */
				if ((gtk_widget_get_state(grid_button->button)
				     != GTK_STATE_ACTIVE))
					gtk_widget_set_state(
						grid_button->button,
						GTK_STATE_ACTIVE);
				continue;
			}
		/* highlight this one, if it is already hightlighted,
		 * put it back to normal */
		//g_print("highlighting %d\n", grid_button->inx);
		if ((gtk_widget_get_state(grid_button->button)
		     != GTK_STATE_NORMAL))
			gtk_widget_set_state(grid_button->button,
					     GTK_STATE_NORMAL);
	}
	list_iterator_destroy(itr);

	return;
}

extern void set_grid_used(List button_list, int start, int end,
			  bool used, bool reset_highlight)
{
	ListIterator itr = NULL;
	grid_button_t *grid_button = NULL;

	if (!button_list)
		return;

	itr = list_iterator_create(button_list);
	while ((grid_button = list_next(itr))) {
		if (start != -1)
			if ((grid_button->inx < start)
			    || (grid_button->inx > end))
				continue;
		grid_button->used = used;
		if (reset_highlight)
			gtk_widget_set_state(grid_button->button,
					     GTK_STATE_NORMAL);

	}
	list_iterator_destroy(itr);

	return;
}

extern void get_button_list_from_main(List *button_list, int start, int end,
				      int color_inx)
{
	ListIterator itr = NULL;
	ListIterator button_itr = NULL;
	grid_button_t *grid_button = NULL;
	grid_button_t *send_grid_button = NULL;

	if (!*button_list)
		*button_list = list_create(destroy_grid_button);

	color_inx %= sview_colors_cnt;
	itr = list_iterator_create(grid_button_list);
	while ((grid_button = list_next(itr))) {
		if ((grid_button->inx < start)
		    ||  (grid_button->inx > end))
			continue;
		button_itr = list_iterator_create(*button_list);
		while ((send_grid_button = list_next(button_itr))) {
			if (send_grid_button->inx == grid_button->inx)
				break;
		}
		list_iterator_destroy(button_itr);
		if (send_grid_button)
			continue;

		send_grid_button = create_grid_button_from_another(
			grid_button, grid_button->node_name, color_inx);
		if (send_grid_button) {
			send_grid_button->button_list = *button_list;
			_add_button_signals(send_grid_button);
			list_append(*button_list, send_grid_button);
		}
	}
	list_iterator_destroy(itr);
	return;
}

extern List copy_main_button_list(int initial_color)
{
	ListIterator itr = NULL;
	grid_button_t *grid_button = NULL;
	grid_button_t *send_grid_button = NULL;
	List button_list = list_create(destroy_grid_button);

	itr = list_iterator_create(grid_button_list);
	while ((grid_button = list_next(itr))) {
		send_grid_button = create_grid_button_from_another(
			grid_button, grid_button->node_name, initial_color);
		if (send_grid_button) {
			send_grid_button->button_list = button_list;
			_add_button_signals(send_grid_button);
			send_grid_button->used = false;
			list_append(button_list, send_grid_button);
		}
	}
	list_iterator_destroy(itr);
	return button_list;
}

extern void put_buttons_in_table(GtkTable *table, List button_list)
{
	int coord_x=0, coord_y=0;
	button_processor_t button_processor;
	grid_button_t *grid_button = NULL;
	ListIterator itr = NULL;
	list_sort(button_list, (ListCmpF) _sort_button_inx);

	if (!button_list) {
		g_print("put_buttons_in_table: no node_list given\n");
		return;
	}

	if (_init_button_processor(&button_processor, list_count(button_list))
	    != SLURM_SUCCESS)
		return;

	button_processor.table = table;
	button_processor.button_list = button_list;
	button_processor.coord_x = &coord_x;
	button_processor.coord_y = &coord_y;

	gtk_table_resize(table, button_processor.table_y,
			 working_sview_config.grid_x_width);

	itr = list_iterator_create(button_list);
	while ((grid_button = list_next(itr))) {
		if (cluster_dims == 3) {
			grid_button->table = table;
			gtk_table_attach(table, grid_button->button,
					 grid_button->table_x,
					 (grid_button->table_x+1),
					 grid_button->table_y,
					 (grid_button->table_y+1),
					 GTK_SHRINK, GTK_SHRINK,
					 1, 1);
			if (!grid_button->table_x) {
				gtk_table_set_row_spacing(table,
						grid_button->table_y,
						working_sview_config.gap_size);
			}
		} else {
			grid_button->table = table;
			grid_button->table_x = coord_x;
			grid_button->table_y = coord_y;
			gtk_table_attach(table, grid_button->button,
					 coord_x, (coord_x+1),
					 coord_y, (coord_y+1),
					 GTK_SHRINK, GTK_SHRINK,
					 1, 1);
			coord_x++;
			if (coord_x == working_sview_config.grid_x_width) {
				coord_x = 0;
				coord_y++;
				if (!(coord_y % working_sview_config.grid_vert))
					gtk_table_set_row_spacing(
						table, coord_y-1,
						working_sview_config.gap_size);
			}

			if (coord_y == button_processor.table_y)
				break;

			if (coord_x
			    && !(coord_x % working_sview_config.grid_hori))
				gtk_table_set_col_spacing(table, coord_x-1, 5);
		}
	}
	list_iterator_destroy(itr);

	if (cluster_dims == 0) {
		/* This is needed to get the correct width of the grid window.
		 * If it is not given then we get a really narrow window. */
		gtk_table_set_row_spacing(table, coord_y?(coord_y-1):0, 1);
	}
	gtk_widget_show_all(GTK_WIDGET(table));
}

extern int update_grid_table(GtkTable *table, List button_list, List node_list)
{
	int rc = SLURM_SUCCESS;
	int coord_x=0, coord_y=0, inx=0;
	ListIterator itr = NULL, itr2 = NULL;
	sview_node_info_t *sview_node_info_ptr = NULL;
	button_processor_t button_processor;

	if (!node_list) {
		g_print("update_grid_table: no node_list given\n");
		return SLURM_ERROR;
	}

	if (_init_button_processor(&button_processor, list_count(node_list))
	    != SLURM_SUCCESS)
		return SLURM_ERROR;

	button_processor.table = table;
	button_processor.button_list = button_list;
	button_processor.coord_x = &coord_x;
	button_processor.coord_y = &coord_y;
	button_processor.inx = &inx;

	gtk_table_resize(table, button_processor.table_y,
			 working_sview_config.grid_x_width);
	gtk_table_set_row_spacings(table, 0);
	gtk_table_set_col_spacings(table, 0);
	itr = list_iterator_create(node_list);
	itr2 = list_iterator_create(button_list);

	while ((sview_node_info_ptr = list_next(itr))) {
		int found = 0;
		/* if (!working_sview_config.show_hidden */
		/*     && !check_part_includes_node(inx)) { */
		/* 	inx++; */
		/* 	continue; */
		/* } */

//	again:
		while ((button_processor.grid_button = list_next(itr2))) {
			if (button_processor.grid_button->inx != inx) {
				continue;
			}
			found = 1;

			if ((rc = _add_button_to_list(
				     sview_node_info_ptr->node_ptr,
				     &button_processor)) != SLURM_SUCCESS)
				goto end_it;
			break;
		}
		if (!found) {
			//list_iterator_reset(itr2);
			//goto again;
			return RESET_GRID;
		}
		inx++;
	}
	rc = _add_button_to_list(NULL, &button_processor);

	/* This is needed to get the correct width of the grid window.
	 * If it is not given then we get a really narrow window. */
	gtk_table_set_row_spacing(table, coord_y?(coord_y-1):0, 1);

end_it:
	list_iterator_destroy(itr);
	list_iterator_destroy(itr2);
	return rc;
}

extern int get_system_stats(GtkTable *table)
{
	int rc = SLURM_SUCCESS;
	node_info_msg_t *node_info_ptr = NULL;
	List node_list = NULL;

	if ((rc = get_new_info_node(&node_info_ptr, force_refresh))
	    == SLURM_NO_CHANGE_IN_DATA) {
	} else if (rc != SLURM_SUCCESS)
		return SLURM_ERROR;

	node_list = create_node_info_list(node_info_ptr, false);
	if (grid_button_list) {
		rc = update_grid_table(main_grid_table, grid_button_list,
				       node_list);
		if (rc == RESET_GRID) {
			FREE_NULL_LIST(grid_button_list);
			grid_button_list = list_create(destroy_grid_button);
			setup_grid_table(main_grid_table, grid_button_list,
					 node_list);
		}
	} else {
		grid_button_list = list_create(destroy_grid_button);
		setup_grid_table(main_grid_table, grid_button_list, node_list);
	}

	gtk_widget_show_all(GTK_WIDGET(main_grid_table));

	return SLURM_SUCCESS;
}

extern int setup_grid_table(GtkTable *table, List button_list, List node_list)
{
	int rc = SLURM_SUCCESS;
	button_processor_t button_processor;
	int coord_x=0, coord_y=0;

	if (!node_list) {
		g_print("setup_grid_table: no node_list given\n");
		return SLURM_ERROR;
	}

	if (_init_button_processor(&button_processor, list_count(node_list))
	    != SLURM_SUCCESS)
		return SLURM_ERROR;

	button_processor.table = table;
	button_processor.button_list = button_list;
	button_processor.coord_x = &coord_x;
	button_processor.coord_y = &coord_y;

	gtk_table_resize(table, button_processor.table_y,
			 working_sview_config.grid_x_width);

	if (default_sview_config.grid_topological && g_topo_info_msg_ptr)
		rc = _grid_table_by_switch(&button_processor, node_list);
	else
		rc = _grid_table_by_list(&button_processor, node_list);

	list_sort(button_list, (ListCmpF) _sort_button_inx);

	return rc;
}

extern void sview_init_grid(bool reset_highlight)
{
	static node_info_msg_t *node_info_ptr = NULL;
	int rc = SLURM_SUCCESS;
	node_info_t *node_ptr = NULL;
	int i = 0;
	ListIterator itr = NULL;
	grid_button_t *grid_button = NULL;

	rc = get_new_info_node(&node_info_ptr, force_refresh);
	if (rc == SLURM_NO_CHANGE_IN_DATA) {
		/* need to clear out old data */
		set_grid_used(grid_button_list, -1, -1, false, reset_highlight);
		return;
	} else if (rc != SLURM_SUCCESS) {
		return;
	}

	if (!grid_button_list) {
		g_print("you need to run get_system_stats() first\n");
		exit(0);
	}

	itr = list_iterator_create(grid_button_list);
	for (i = 0; i < node_info_ptr->record_count; i++) {
		int tried_again = 0;
		node_ptr = &node_info_ptr->node_array[i];
	try_again:
		while ((grid_button = list_next(itr))) {
			if (grid_button->inx != i)
				continue;
			grid_button->state = node_ptr->node_state;
			gtk_widget_set_state(grid_button->button,
					     GTK_STATE_NORMAL);
			grid_button->used = false;
			break;
		}
		if (!grid_button && !tried_again) {
			/* the order should never change but just to
			 * make sure we don't miss it */
			list_iterator_reset(itr);
			tried_again = 1;
			goto try_again;
		}
	}
	list_iterator_destroy(itr);
}

/* make grid if it doesn't exist and set the buttons to unused */
extern void setup_popup_grid_list(popup_info_t *popup_win)
{
	int def_color = MAKE_BLACK;

	if (popup_win->grid_button_list) {
		set_grid_used(popup_win->grid_button_list,
			      -1, -1, false, false);
	} else {
		popup_win->grid_button_list =
			copy_main_button_list(def_color);
		put_buttons_in_table(popup_win->grid_table,
				     popup_win->grid_button_list);
		popup_win->full_grid = 1;
	}
}

/* clear extra buttons to N/A and if model then set those as white */
extern void post_setup_popup_grid_list(popup_info_t *popup_win)
{
	/* refresh the pointer */
	if (popup_win->model
	    && gtk_tree_store_iter_is_valid(GTK_TREE_STORE(popup_win->model),
					    &popup_win->iter)) {
		gtk_tree_model_get(popup_win->model, &popup_win->iter,
				   popup_win->node_inx_id,
				   &popup_win->node_inx, -1);
	} else {
		popup_win->node_inx = NULL;
	}

	if (popup_win->node_inx) {
		int j=0;
		while (popup_win->node_inx[j] >= 0) {
			change_grid_color(
				popup_win->grid_button_list,
				popup_win->node_inx[j],
				popup_win->node_inx[j+1], MAKE_WHITE, true, 0);
			j += 2;
		}
	}

	change_grid_color(popup_win->grid_button_list, -1, -1,
			  MAKE_BLACK, true, NODE_STATE_IDLE);
}
