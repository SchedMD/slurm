/*****************************************************************************\
 *  common.c - common functions used by tabs in sview
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *
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

#include "src/sview/sview.h"
#include "src/common/parse_time.h"
#include <gdk/gdkkeysyms.h>

#define TOPO_DEBUG 0
#define _DEBUG 0
static bool menu_right_pressed = false;

typedef struct {
	display_data_t *display_data;
	void (*pfunc)(GtkTreeModel*, GtkTreeIter*, int);
	GtkTreeView  *tree_view;
} each_t;

typedef struct {
	GtkTreeIter iter;
	GtkTreeModel *model;
	GtkTreeView  *treeview;
} treedata_t;

static gboolean control_key_in_effect = false;
static gboolean enter_key_in_effect = false;

static int _find_node_inx (char *name)
{
	int i;

	if ((name == NULL) || (name[0] == '\0')) {
		info("_find_node_inx passed NULL name");
		return -1;
	}


	for (i = 0; i < g_node_info_ptr->record_count; i++) {
		if (g_node_info_ptr->node_array[i].name == NULL)
			continue;	/* Future node or other anomaly */
		if (!xstrcmp(name, g_node_info_ptr->node_array[i].name))
			return i;
	}

	return -1;
}

static void _display_topology(void)
{
	int i, one_liner = 1;

	if (TOPO_DEBUG) {
		g_print("_display_topology,  record_count = %d\n",
			g_topo_info_msg_ptr->record_count);
	}

	for (i = 0; i < g_topo_info_msg_ptr->record_count; i++) {
		slurm_print_topo_record(stdout,
					&g_topo_info_msg_ptr->topo_array[i],
					one_liner);
	}
}

static void _foreach_popup_all(GtkTreeModel  *model,
			       GtkTreePath   *path,
			       GtkTreeIter   *iter,
			       gpointer       userdata)
{

	each_t *each = userdata;
	each->pfunc(model, iter, each->display_data->id);
}

static void _foreach_full_info(GtkTreeModel  *model,
			       GtkTreePath   *path,
			       GtkTreeIter   *iter,
			       gpointer       userdata)
{

	each_t *each = userdata;
	(each->display_data->set_menu)(each->tree_view, NULL, path,
				       FULL_CLICKED);
	popup_pos.x = popup_pos.slider + popup_pos.cntr * 10;
	popup_pos.y = popup_pos.cntr * 22;
	popup_pos.cntr++;
	if (popup_pos.cntr > 10) {
		popup_pos.cntr = 1;
		popup_pos.slider += 100;
	}
}

/* These next 2 functions are here to make it so we don't magically
 * click on something before we really want to in a menu.
 */
static gboolean _menu_button_pressed(GtkWidget *widget, GdkEventButton *event,
				     gpointer extra)
{
	if (event->button == 3) {
		menu_right_pressed = true;
		return true;
	}
	return false;
}

static gboolean _menu_button_released(GtkWidget *widget, GdkEventButton *event,
				      gpointer extra)
{
	if (event->button == 3 && !menu_right_pressed)
		return true;
	menu_right_pressed = false;
	return false;
}

static gboolean _frame_callback(GtkWindow *window,
				GdkEvent *event, gpointer data)
{

	if (event->expose.send_event == 0) {
		default_sview_config.fi_popup_width = event->configure.width;
		default_sview_config.fi_popup_height = event->configure.height;
		working_sview_config.fi_popup_width = event->configure.width;
		working_sview_config.fi_popup_height = event->configure.height;

		ListIterator itr = list_iterator_create(popup_list);
		popup_info_t *popup_win = NULL;

		while ((popup_win = list_next(itr))) {
			gtk_window_resize(GTK_WINDOW(popup_win->popup),
					  working_sview_config.fi_popup_width,
					  working_sview_config.fi_popup_height);
		}
		list_iterator_destroy(itr);
	}


	return false;
}

static void _handle_response(GtkDialog *dialog, gint response_id,
			     popup_info_t *popup_win)
{

	switch(response_id) {
	case GTK_RESPONSE_OK: //refresh
		(popup_win->display_data->refresh)(NULL, popup_win);
		break;
	case GTK_RESPONSE_DELETE_EVENT: // exit
	case GTK_RESPONSE_CLOSE: // close
		delete_popup(NULL, NULL, popup_win->spec_info->title);
		break;
	case GTK_RESPONSE_CANCEL: // cancel
		delete_popups();
		break;
	default:
		g_print("handle unknown response %d\n", response_id);
		break;
	}
	return;
}

static int _sort_iter_compare_func_char(GtkTreeModel *model,
					GtkTreeIter  *a,
					GtkTreeIter  *b,
					gpointer      userdata)
{
	int sortcol = GPOINTER_TO_INT(userdata);
	int ret = 0;
	int len1 = 0, len2 = 0;
	gchar *name1 = NULL, *name2 = NULL;

	gtk_tree_model_get(model, a, sortcol, &name1, -1);
	gtk_tree_model_get(model, b, sortcol, &name2, -1);

	if (!name1 && !name2)
		goto cleanup; /* both equal => ret = 0 */
	else if (!name1 || !name2) {
		ret = (name1 == NULL) ? -1 : 1;
	} else {
		/* sort like a human would
		   meaning snowflake2 would be greater than
		   snowflake12 */
		len1 = strlen(name1);
		len2 = strlen(name2);
		while ((ret < len1) && (!g_ascii_isdigit(name1[ret])))
			ret++;
		if (ret < len1) {
			if (!g_ascii_strncasecmp(name1, name2, ret)) {
				if (len1 > len2)
					ret = 1;
				else if (len1 < len2)
					ret = -1;
				else
					ret = g_ascii_strcasecmp(name1, name2);
			} else
				ret = g_ascii_strcasecmp(name1, name2);
		} else
			ret = g_ascii_strcasecmp(name1, name2);
	}
cleanup:
	g_free(name1);
	g_free(name2);

	return ret;
}

static int _sort_iter_compare_func_int(GtkTreeModel *model,
				       GtkTreeIter  *a,
				       GtkTreeIter  *b,
				       gpointer      userdata)
{
	int sortcol = GPOINTER_TO_INT(userdata);
	int ret = 0;
	gint int1, int2;

	gtk_tree_model_get(model, a, sortcol, &int1, -1);
	gtk_tree_model_get(model, b, sortcol, &int2, -1);

	if (int1 != int2)
		ret = (int1 > int2) ? 1 : -1;

	return ret;
}

static int _sort_iter_compare_func_nodes(GtkTreeModel *model,
					 GtkTreeIter  *a,
					 GtkTreeIter  *b,
					 gpointer      userdata)
{
	int sortcol = GPOINTER_TO_INT(userdata);
	int ret = 0;
	gchar *name1 = NULL, *name2 = NULL;

	gtk_tree_model_get(model, a, sortcol, &name1, -1);
	gtk_tree_model_get(model, b, sortcol, &name2, -1);

	if (!name1 && !name2)
		goto cleanup; /* both equal => ret = 0 */
	else if (!name1 || !name2)
		ret = (name1 == NULL) ? -1 : 1;
	else {
		uint64_t int1=0, int2=0, tmp_int;
		int spot=0;
		/* If this is in a mixed state we need to get them all */
		while (name1[spot]) {
			while (name1[spot]
			       && !g_ascii_isdigit(name1[spot])) {
				spot++;
			}
			if (!name1[spot])
				break;
			tmp_int = atoi(name1+spot);
			while (name1[spot] && g_ascii_isdigit(name1[spot])) {
				spot++;
			}

			if (!name1[spot]) {
			} else if (name1[spot] == 'K')
				tmp_int *= 1024;
			else if (name1[spot] == 'M')
				tmp_int *= 1048576;
			else if (name1[spot] == 'G')
				tmp_int *= 1073741824;

			int1 += tmp_int;
		}

		spot=0;
		while (name2[spot]) {
			while (name2[spot]
			       && !g_ascii_isdigit(name2[spot])) {
				spot++;
			}
			if (!name2[spot])
				break;
			tmp_int = atoi(name2+spot);
			while (name2[spot] && g_ascii_isdigit(name2[spot])) {
				spot++;
			}
			if (!name2[spot]) {
			} else if (name2[spot] == 'K')
				tmp_int *= 1024;
			else if (name2[spot] == 'M')
				tmp_int *= 1048576;
			else if (name2[spot] == 'G')
				tmp_int *= 1073741824;

			int2 += tmp_int;
		}

		if (int1 != int2)
			ret = (int1 > int2) ? 1 : -1;
	}
cleanup:
	g_free(name1);
	g_free(name2);

	return ret;
}

static void _editing_started(GtkCellRenderer *cell,
			     GtkCellEditable *editable,
			     const gchar     *path,
			     gpointer         data)
{
	gdk_threads_leave();
	g_mutex_lock(sview_mutex);
}

static void _editing_canceled(GtkCellRenderer *cell,
			      gpointer         data)
{
	g_mutex_unlock(sview_mutex);
}

static void *_editing_thr(gpointer arg)
{
	int msg_id = 0;
	sleep(5);
	gdk_threads_enter();
	msg_id = GPOINTER_TO_INT(arg);
	gtk_statusbar_remove(GTK_STATUSBAR(main_statusbar),
			     STATUS_ADMIN_EDIT, msg_id);
	//gdk_flush();
	gdk_threads_leave();
	return NULL;
}

static void _cell_data_func(GtkTreeViewColumn *col,
			    GtkCellRenderer *renderer,
			    GtkTreeModel *model,
			    GtkTreeIter *iter,
			    gpointer data)
{
	GdkPixbuf *pixbuf = NULL;
	char *color_char, *color_char2;
	uint32_t color;

	g_object_get(renderer, "pixbuf", &pixbuf, NULL);
	if (!pixbuf)
		return;

	gtk_tree_model_get(model, iter,
			   GPOINTER_TO_INT(g_object_get_data(G_OBJECT(renderer),
							     "column")),
			   &color_char, -1);
	if (!color_char)
		return;

	color_char2 = color_char+1;
	color = strtoul(color_char2, (char **)&color_char2, 16);
	g_free(color_char);

	/* we need to shift over 2 spots for the alpha */
	gdk_pixbuf_fill(pixbuf, color << 8);
	/* This only has to be done once, but I can't find any way to
	 * set something to only make it happen once.  It only takes
	 * 3-5 usecs to do it so I didn't worry about it doing it
	 * multiple times.  If you can figure out how to make this
	 * happen only once please fix, but the pointers for the col,
	 * renderer, and pixbuf are all the same.  You could put in
	 * some think in the tree_model, but that seemed a bit more
	 * cumbersome. - da
	 */
}

static void _add_col_to_treeview(GtkTreeView *tree_view,
				 display_data_t *display_data, int color_column)
{
	GtkTreeViewColumn *col;
	GtkListStore *model;
	GtkCellRenderer *renderer = NULL;

	/* Since some systems have different default columns (some
	 * which aren't displayed on all types of clusters only add a
	 * column if there is a name for it. */
	if (!display_data->name && (display_data->extra != EDIT_COLOR))
		return;

	col = gtk_tree_view_column_new();
	model = (display_data->create_model)(display_data->id);

	if (model && display_data->extra != EDIT_NONE) {
		renderer = gtk_cell_renderer_combo_new();
		g_object_set(renderer,
			     "model", model,
			     "text-column", 0,
			     "has-entry", 1,
			     "editable", true,
			     NULL);
	} else if (display_data->extra == EDIT_TEXTBOX) {
		renderer = gtk_cell_renderer_text_new();
		g_object_set(renderer,
			     "editable", true,
			     NULL);
	} else if (display_data->extra == EDIT_COLOR) {
		GdkPixbuf *pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, false,
						   8, 10, 20);
		renderer = gtk_cell_renderer_pixbuf_new();
		g_object_set(renderer, "pixbuf", pixbuf, NULL);
		g_object_unref(pixbuf);
	} else
		renderer = gtk_cell_renderer_text_new();

	if (model)
		g_object_unref(model);

	gtk_tree_view_column_pack_start(col, renderer, true);

	g_object_set_data(G_OBJECT(renderer), "column",
			  GINT_TO_POINTER(display_data->id));

	if (display_data->extra == EDIT_COLOR) {
		gtk_tree_view_column_set_cell_data_func(
			col, renderer, _cell_data_func,
			NULL, NULL);
	} else {
		g_signal_connect(renderer, "editing-started",
				 G_CALLBACK(_editing_started), NULL);
		g_signal_connect(renderer, "editing-canceled",
				 G_CALLBACK(_editing_canceled), NULL);
		g_signal_connect(renderer, "edited",
				 G_CALLBACK(display_data->admin_edit),
				 gtk_tree_view_get_model(tree_view));

		gtk_tree_view_column_add_attribute(col, renderer,
						   "text", display_data->id);
		gtk_tree_view_column_set_expand(col, true);
		gtk_tree_view_column_set_reorderable(col, true);
		gtk_tree_view_column_set_resizable(col, true);
		gtk_tree_view_column_set_sort_column_id(col, display_data->id);
		gtk_tree_view_column_set_title(col, display_data->name);
	}

	gtk_tree_view_append_column(tree_view, col);
}

static void _toggle_state_changed(GtkCheckMenuItem *menuitem,
				  display_data_t *display_data)
{
	if (display_data->show)
		display_data->show = false;
	else
		display_data->show = true;
	toggled = true;
	refresh_main(NULL, NULL);
}

static void _popup_state_changed(GtkCheckMenuItem *menuitem,
				 display_data_t *display_data)
{
	popup_info_t *popup_win = (popup_info_t *) display_data->user_data;
	if (display_data->show)
		display_data->show = false;
	else
		display_data->show = true;
	popup_win->toggled = 1;
	(display_data->refresh)(NULL, display_data->user_data);
}

static void _selected_page(GtkMenuItem *menuitem, display_data_t *display_data)
{
	treedata_t *treedata = (treedata_t *)display_data->user_data;
	each_t each;
	memset(&each, 0, sizeof(each_t));
	each.tree_view = treedata->treeview;
	each.display_data = display_data;
	global_row_count = gtk_tree_selection_count_selected_rows(
		gtk_tree_view_get_selection(treedata->treeview));
	switch(display_data->extra & EXTRA_BASE) {
	case PART_PAGE:
		each.pfunc = &popup_all_part;
		break;
	case JOB_PAGE:
		each.pfunc = &popup_all_job;
		break;
	case NODE_PAGE:
		each.pfunc = &popup_all_node;
		break;
	case RESV_PAGE:
		each.pfunc = &popup_all_resv;
		break;
	case BB_PAGE:
		each.pfunc = &popup_all_bb;
		break;
	case FRONT_END_PAGE:
		each.pfunc = &popup_all_front_end;
		break;
	case ADMIN_PAGE:
		switch(display_data->id) {
		case JOB_PAGE:
			admin_job(treedata->model, &treedata->iter,
				  display_data->name,treedata->treeview);
			break;
		case PART_PAGE:
			select_admin_partitions(treedata->model,
						&treedata->iter,
						display_data,
						treedata->treeview);
			break;
		case FRONT_END_PAGE:
			select_admin_front_end(treedata->model,
					       &treedata->iter,
					       display_data,
					       treedata->treeview);
			break;
		case RESV_PAGE:
			select_admin_resv(treedata->model, &treedata->iter,
					  display_data, treedata->treeview);
			break;
		case NODE_PAGE:
			select_admin_nodes(treedata->model, &treedata->iter,
					   display_data, NO_VAL,
					   treedata->treeview);
			break;
		case BB_PAGE:
			select_admin_bb(treedata->model, &treedata->iter,
					   display_data, treedata->treeview);
			break;
		default:
			g_print("common admin got %d %d\n",
				display_data->extra,
				display_data->id);
		}
		break;
	default:
		g_print("common got %d %d\n", display_data->extra,
			display_data->id);
	}
	if (each.pfunc)
		gtk_tree_selection_selected_foreach(
			gtk_tree_view_get_selection(treedata->treeview),
			_foreach_popup_all, &each);
	xfree(treedata);
}

extern char * replspace (char *str)
{
	int pntr = 0;
	while (str[pntr]) {
		if (str[pntr] == ' ')
			str[pntr] = '_';
		pntr++;
	}
	return str;
}

extern char * replus (char *str)
{
	int pntr = 0;
	while (str[pntr]) {
		if (str[pntr] == '_')
			str[pntr] = ' ';
		pntr++;
	}
	return str;
}

extern void free_switch_nodes_maps(
	switch_record_bitmaps_t *sw_nodes_bitmaps_ptr)
{
	while (sw_nodes_bitmaps_ptr++) {
		if (!sw_nodes_bitmaps_ptr->node_bitmap)
			break;
		bit_free(sw_nodes_bitmaps_ptr->node_bitmap);
		if (sw_nodes_bitmaps_ptr->node_bitmap)
			xfree(sw_nodes_bitmaps_ptr->nodes);
	}
	g_switch_nodes_maps = NULL;
}

extern int build_nodes_bitmap(char *node_names, bitstr_t **bitmap)

{
	char *this_node_name;
	bitstr_t *my_bitmap;
	hostlist_t host_list;
	int node_inx = -1;

	if (TOPO_DEBUG)
		g_print("...............build_nodes_bitmap............%s\n",
			node_names);
	my_bitmap = (bitstr_t *) bit_alloc(g_node_info_ptr->record_count);
	*bitmap = my_bitmap;

	if (!node_names) {
		error("build_nodes_bitmap: node_names is NULL");
		return EINVAL;
	}

	if (!(host_list = hostlist_create(node_names))) {
		error("build_nodes_bitmap: hostlist_create(%s) error",
		      node_names);
		return EINVAL;
	}

	/*spin hostlist and map nodes into a bitmap*/
	while ((this_node_name = hostlist_shift(host_list))) {
		node_inx = _find_node_inx(this_node_name);
		free(this_node_name);

		if (node_inx == -1)
			continue;

		bit_set(my_bitmap, (bitoff_t)node_inx);
	}
	hostlist_destroy(host_list);

	return SLURM_SUCCESS;
}

extern int get_topo_conf(void)
{
	int i;
	switch_record_bitmaps_t sw_nodes_bitmaps;
	switch_record_bitmaps_t *sw_nodes_bitmaps_ptr;

	if (TOPO_DEBUG)
		g_print("get_topo_conf\n");

	if (!g_topo_info_msg_ptr && slurm_load_topo(&g_topo_info_msg_ptr)) {
		slurm_perror ("slurm_load_topo error");
		if (TOPO_DEBUG)
			g_print("get_topo_conf error !!\n");
		return SLURM_ERROR;
	}

	if (g_topo_info_msg_ptr->record_count == 0) {
		slurm_free_topo_info_msg(g_topo_info_msg_ptr);
		g_topo_info_msg_ptr = NULL;
		return SLURM_ERROR;
	}

	if (g_switch_nodes_maps)
		free_switch_nodes_maps(g_switch_nodes_maps);

	g_switch_nodes_maps = xmalloc(sizeof(sw_nodes_bitmaps)
				      * g_topo_info_msg_ptr->record_count);
	sw_nodes_bitmaps_ptr = g_switch_nodes_maps;

	if (TOPO_DEBUG)
		g_print("_display_topology,  record_count = %d\n",
			g_topo_info_msg_ptr->record_count);
	for (i = 0; i < g_topo_info_msg_ptr->record_count;
	     i++, sw_nodes_bitmaps_ptr++) {
		if (!g_topo_info_msg_ptr->topo_array[i].nodes)
			continue;
		if (TOPO_DEBUG)  {
			g_print("ptr->nodes =  %s \n",
				g_topo_info_msg_ptr->topo_array[i].nodes);
		}
		if (build_nodes_bitmap(
			    g_topo_info_msg_ptr->topo_array[i].nodes,
			    &sw_nodes_bitmaps_ptr->node_bitmap)) {
			g_print("Invalid node name (%s) in switch %s\n",
				g_topo_info_msg_ptr->topo_array[i].nodes,
				g_topo_info_msg_ptr->topo_array[i].name);
		}
	}

	if (TOPO_DEBUG)
		_display_topology();

	return SLURM_SUCCESS;
}

extern int get_row_number(GtkTreeView *tree_view, GtkTreePath *path)
{
	GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
	GtkTreeIter iter;
	int line = 0;

	if (!model) {
		g_error("error getting the model from the tree_view");
		return -1;
	}

	if (!gtk_tree_model_get_iter(model, &iter, path)) {
		g_error("get row, error getting iter from model");
		return -1;
	}
	gtk_tree_model_get(model, &iter, POS_LOC, &line, -1);
	return line;
}

extern int find_col(display_data_t *display_data, int type)
{
	int i = 0;

	while (display_data++) {
		if (display_data->id == -1)
			break;
		if (display_data->id == type)
			return i;
		i++;
	}
	return -1;
}

extern const char *find_col_name(display_data_t *display_data, int type)
{
	while (display_data++) {
		if (display_data->id == -1)
			break;
		if (display_data->id == type)
			return display_data->name;
	}
	return NULL;
}

extern void *get_pointer(GtkTreeView *tree_view, GtkTreePath *path, int loc)
{
	GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
	GtkTreeIter iter;
	void *ptr = NULL;

	if (!model) {
		g_error("error getting the model from the tree_view");
		return ptr;
	}

	if (!gtk_tree_model_get_iter(model, &iter, path)) {
		g_error("get pointer, error getting iter from model");
		return ptr;
	}
	gtk_tree_model_get(model, &iter, loc, &ptr, -1);
	return ptr;
}

extern void make_fields_menu(popup_info_t *popup_win, GtkMenu *menu,
			     display_data_t *display_data, int count)
{
	GtkWidget *menuitem = NULL;
	display_data_t *first_display_data = display_data;
	int i = 0;

	/* we don't want to display anything on the full info page */
	if (popup_win && popup_win->spec_info->type == INFO_PAGE)
		return;

	g_signal_connect(G_OBJECT(menu), "button-press-event",
			 G_CALLBACK(_menu_button_pressed),
			 NULL);
	g_signal_connect(G_OBJECT(menu), "button-release-event",
			 G_CALLBACK(_menu_button_released),
			 NULL);

	for(i=0; i<count; i++) {
		while (display_data++) {
			if (display_data->id == -1)
				break;
			if (!display_data->name)
				continue;
			if (display_data->id != i)
				continue;

			menuitem = gtk_check_menu_item_new_with_label(
				display_data->name);

			gtk_check_menu_item_set_active(
				GTK_CHECK_MENU_ITEM(menuitem),
				display_data->show);
			if (popup_win) {
				display_data->user_data = popup_win;
				g_signal_connect(
					menuitem, "toggled",
					G_CALLBACK(_popup_state_changed),
					display_data);
			} else {
				g_signal_connect(
					menuitem, "toggled",
					G_CALLBACK(_toggle_state_changed),
					display_data);
			}
			gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
			break;
		}
		display_data = first_display_data;
	}
}

extern void set_page_opts(int page, display_data_t *display_data,
			  int count, char* initial_opts)
{
	page_opts_t *page_opts;
	ListIterator itr = NULL;
	char *col_name = NULL;

	xassert(page < PAGE_CNT);

	page_opts = &working_sview_config.page_opts[page];
	if (!page_opts->col_list) {
		page_opts->def_col_list = 1;
		page_opts->col_list = list_create(slurm_destroy_char);
		slurm_addto_char_list(page_opts->col_list, initial_opts);
	}

	page_opts->display_data = display_data;

	itr = list_iterator_create(page_opts->col_list);
	while ((col_name = list_next(itr))) {
		replus(col_name);
		while (display_data++) {
			if (display_data->id == -1)
				break;
			if (!display_data->name)
				continue;
			if (!xstrncasecmp(col_name, display_data->name,
					  strlen(col_name))) {
				display_data->show = true;
				break;
			}
		}
		display_data = page_opts->display_data;
	}
	list_iterator_destroy(itr);
}

extern void make_options_menu(GtkTreeView *tree_view, GtkTreePath *path,
			      GtkMenu *menu, display_data_t *display_data)
{
	GtkWidget *menuitem = NULL;
	treedata_t *treedata = xmalloc(sizeof(treedata_t));
	treedata->model = gtk_tree_view_get_model(tree_view);
	treedata->treeview = tree_view;

	g_signal_connect(G_OBJECT(menu), "button-press-event",
			 G_CALLBACK(_menu_button_pressed),
			 NULL);
	g_signal_connect(G_OBJECT(menu), "button-release-event",
			 G_CALLBACK(_menu_button_released),
			 NULL);

	if (!gtk_tree_model_get_iter(treedata->model, &treedata->iter, path)) {
		g_error("make menus error getting iter from model\n");
		return;
	}

	/* check selection list */
	global_row_count = gtk_tree_selection_count_selected_rows(
		gtk_tree_view_get_selection(tree_view));

	if (display_data->user_data)
		xfree(display_data->user_data);

	while (display_data++) {
		if (display_data->id == -1) {
			break;
		}

		if (!display_data->name)
			continue;
		display_data->user_data = treedata;
		menuitem = gtk_menu_item_new_with_label(display_data->name);

		g_signal_connect(menuitem, "activate",
				 G_CALLBACK(_selected_page),
				 display_data);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);

	}
}

extern GtkScrolledWindow *create_scrolled_window(void)
{
	GtkScrolledWindow *scrolled_window = NULL;
	GtkWidget *table = NULL;
	table = gtk_table_new(1, 1, false);

	gtk_container_set_border_width(GTK_CONTAINER(table), 10);

	scrolled_window = GTK_SCROLLED_WINDOW(gtk_scrolled_window_new(
						      NULL, NULL));
	gtk_container_set_border_width(GTK_CONTAINER(scrolled_window), 10);

	gtk_scrolled_window_set_policy(scrolled_window,
				       GTK_POLICY_AUTOMATIC,
				       GTK_POLICY_AUTOMATIC);

	gtk_scrolled_window_add_with_viewport(scrolled_window, table);

	return scrolled_window;
}

extern GtkWidget *create_entry(void)
{
	GtkWidget *entry = gtk_entry_new();

	gtk_entry_set_activates_default(GTK_ENTRY(entry), true);

	return entry;
}

extern void create_page(GtkNotebook *notebook, display_data_t *display_data)
{
	GtkScrolledWindow *scrolled_window = create_scrolled_window();
	GtkWidget *event_box = gtk_event_box_new();
	GtkWidget *label = gtk_label_new(display_data->name);
	GtkWidget *close_button = gtk_event_box_new();
	GtkWidget *table;
	GtkWidget *image = NULL;
	int err;

	if (display_data->id == TAB_PAGE) {
		table = gtk_table_new(PAGE_CNT, 3, false);
		image = gtk_image_new_from_stock(
			GTK_STOCK_ADD, GTK_ICON_SIZE_SMALL_TOOLBAR);
	} else {
		table = gtk_table_new(1, 3, false);
		image = gtk_image_new_from_stock(
			GTK_STOCK_DIALOG_ERROR, GTK_ICON_SIZE_SMALL_TOOLBAR);
		g_signal_connect(G_OBJECT(close_button), "button-press-event",
				 G_CALLBACK(close_tab),
				 display_data);
	}

	gtk_container_add(GTK_CONTAINER(close_button), image);
	gtk_widget_set_size_request(close_button, 10, 10);

	//gtk_event_box_set_above_child(GTK_EVENT_BOX(close_button), false);

	gtk_container_add(GTK_CONTAINER(event_box), label);
	gtk_event_box_set_above_child(GTK_EVENT_BOX(event_box), false);
	g_signal_connect(G_OBJECT(event_box), "button-press-event",
			 G_CALLBACK(tab_pressed),
			 display_data);

	gtk_table_set_homogeneous(GTK_TABLE(table), false);
	gtk_table_set_col_spacings(GTK_TABLE(table), 5);
	gtk_container_set_border_width(GTK_CONTAINER(table), 1);

	gtk_table_attach_defaults(GTK_TABLE(table), event_box, 0, 1, 0, 1);
	gtk_table_attach_defaults(GTK_TABLE(table), close_button, 2, 3, 0, 1);
	gtk_container_set_focus_child(GTK_CONTAINER(table), label);

	gtk_widget_show_all(table);
	//(display_data->set_fields)(GTK_MENU(menu));
	if ((err = gtk_notebook_append_page(GTK_NOTEBOOK(notebook),
					    GTK_WIDGET(scrolled_window),
					    table)) == -1) {
		g_error("Couldn't add page to notebook\n");
	}

	display_data->extra = err;

}

extern GtkTreeView *create_treeview(display_data_t *local, List *button_list)
{
	signal_params_t *signal_params = xmalloc(sizeof(signal_params_t));
	GtkTreeView *tree_view = GTK_TREE_VIEW(gtk_tree_view_new());
	local->user_data = NULL;

	signal_params->display_data = local;
	signal_params->button_list = button_list;
	if (working_sview_config.ruled_treeview)
		gtk_tree_view_set_rules_hint (tree_view, true);

	g_signal_connect(G_OBJECT(tree_view), "button-press-event",
			 G_CALLBACK(row_clicked),
			 signal_params);
	g_signal_connect(G_OBJECT(tree_view), "key_release_event",
			 G_CALLBACK(key_released),
			 signal_params);
	g_signal_connect(G_OBJECT(tree_view), "key_press_event",
			 G_CALLBACK(key_pressed),
			 signal_params);
	g_signal_connect(G_OBJECT(tree_view), "row-activated",
			 G_CALLBACK(row_activated),
			 signal_params);
	gtk_widget_show(GTK_WIDGET(tree_view));
	list_push(signal_params_list, signal_params);
	return tree_view;

}

extern GtkTreeView *create_treeview_2cols_attach_to_table(GtkTable *table)
{
	GtkTreeView *tree_view = GTK_TREE_VIEW(gtk_tree_view_new());
	GtkTreeStore *treestore =
		gtk_tree_store_new(3, GTK_TYPE_STRING,
				   GTK_TYPE_STRING, GTK_TYPE_STRING);
	GtkTreeViewColumn *col = gtk_tree_view_column_new();
	GtkCellRenderer *renderer = gtk_cell_renderer_text_new();

	if (working_sview_config.ruled_treeview)
		gtk_tree_view_set_rules_hint (tree_view, true);

	gtk_table_attach_defaults(table,
				  GTK_WIDGET(tree_view),
				  0, 1, 0, 1);

	gtk_tree_view_set_model(tree_view, GTK_TREE_MODEL(treestore));

	gtk_tree_view_column_pack_start(col, renderer, true);
	gtk_tree_view_column_add_attribute(col, renderer,
					   "text", DISPLAY_NAME);
	gtk_tree_view_column_add_attribute(col, renderer,
					   "font", DISPLAY_FONT);
	gtk_tree_view_column_set_title(col, "Name");
	gtk_tree_view_column_set_resizable(col, true);
	gtk_tree_view_column_set_expand(col, true);
	gtk_tree_view_append_column(tree_view, col);

	col = gtk_tree_view_column_new();
	renderer = gtk_cell_renderer_text_new();
	gtk_tree_view_column_pack_start(col, renderer, true);
	gtk_tree_view_column_add_attribute(col, renderer,
					   "text", DISPLAY_VALUE);
	gtk_tree_view_column_add_attribute(col, renderer,
					   "font", DISPLAY_FONT);
	gtk_tree_view_column_set_title(col, "Value");
	gtk_tree_view_column_set_resizable(col, true);
	gtk_tree_view_column_set_expand(col, true);
	gtk_tree_view_append_column(tree_view, col);

	col = gtk_tree_view_column_new();
	renderer = gtk_cell_renderer_text_new();
	gtk_tree_view_column_pack_start(col, renderer, true);
	gtk_tree_view_column_set_visible(col, false);
	gtk_tree_view_column_add_attribute(col, renderer,
					   "text", DISPLAY_FONT);
	gtk_tree_view_append_column(tree_view, col);

	g_object_unref(treestore);
	return tree_view;
}

extern GtkTreeStore *create_treestore(GtkTreeView *tree_view,
				      display_data_t *display_data,
				      int count, int sort_column,
				      int color_column)
{
	GtkTreeStore *treestore = NULL;
	GType types[count];
	int i=0;

	/*set up the types defined in the display_data_t */
	for(i=0; i<count; i++) {
		types[display_data[i].id] = display_data[i].type;
	}

	treestore = gtk_tree_store_newv(count, types);
	if (!treestore) {
		g_print("Can't create treestore.\n");
		return NULL;
	}

	gtk_tree_view_set_model(tree_view, GTK_TREE_MODEL(treestore));

	for(i=1; i<count; i++) {
		if (!display_data[i].show)
			continue;

		_add_col_to_treeview(tree_view, &display_data[i], color_column);
		if (!display_data[i].name)
			continue;

		switch(display_data[i].type) {
		case G_TYPE_INT:
			gtk_tree_sortable_set_sort_func(
				GTK_TREE_SORTABLE(treestore),
				display_data[i].id,
				_sort_iter_compare_func_int,
				GINT_TO_POINTER(display_data[i].id),
				NULL);

			break;
		case G_TYPE_STRING:
			if (!xstrcasecmp(display_data[i].name, "Node Count")
			    || !xstrcasecmp(display_data[i].name, "CPU Count")
			    || !xstrcasecmp(display_data[i].name, "Real Memory")
			    || !xstrcasecmp(display_data[i].name, "Port")
			    || !xstrcasecmp(display_data[i].name, "Tmp Disk")) {
				gtk_tree_sortable_set_sort_func(
					GTK_TREE_SORTABLE(treestore),
					display_data[i].id,
					_sort_iter_compare_func_nodes,
					GINT_TO_POINTER(display_data[i].id),
					NULL);
				break;
			} else {
				gtk_tree_sortable_set_sort_func(
					GTK_TREE_SORTABLE(treestore),
					display_data[i].id,
					_sort_iter_compare_func_char,
					GINT_TO_POINTER(display_data[i].id),
					NULL);
				break;
			}
		default:
			g_print("unknown type %d",
				(int)display_data[i].type);
		}
	}

	if (sort_column >= 0) {
		gtk_tree_sortable_set_sort_column_id(
					GTK_TREE_SORTABLE(treestore),
					sort_column,
					GTK_SORT_ASCENDING);
	}

	g_object_unref(treestore);

	return treestore;
}

extern gboolean right_button_pressed(GtkTreeView *tree_view,
				     GtkTreePath *path,
				     GdkEventButton *event,
				     const signal_params_t *signal_params,
				     int type)
{
	GtkMenu *menu = GTK_MENU(gtk_menu_new());
	display_data_t *display_data = signal_params->display_data;

	if (type == ROW_CLICKED) {
		if (_DEBUG)
			g_print("right_button_pressed:global_row_count : %d\n",
				global_row_count);

		/* These next 2 functions are there to keep the keyboard in
		   sync */
		if (!(event->state & GDK_CONTROL_MASK)
		    && (!(global_row_count > 0)))
			gtk_tree_view_set_cursor(tree_view, path, NULL, false);

		gtk_widget_grab_focus(GTK_WIDGET(tree_view));

		/* highlight the nodes from this row */
		(display_data->set_menu)(tree_view, *signal_params->button_list,
					 path, ROW_LEFT_CLICKED);
	}

	(display_data->set_menu)(tree_view, menu, path, type);
	gtk_widget_show_all(GTK_WIDGET(menu));
	gtk_menu_popup(menu, NULL, NULL, NULL, NULL,
		       event ? event->button : 0,
		       gdk_event_get_time((GdkEvent*)event));
	return true;
}

extern gboolean left_button_pressed(GtkTreeView *tree_view,
				    GtkTreePath *path,
				    const signal_params_t *signal_params,
				    GdkEventButton *event)
{
	static time_t last_time = 0;
	time_t now = time(NULL);
	gboolean rc = false;
	GtkTreeIter iter;
	GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
	display_data_t *display_data = signal_params->display_data;
	static gpointer *last_user_data = NULL;

	/* These next 2 functions are there to keep the keyboard in
	   sync */
	if (!((event->state & GDK_CONTROL_MASK)
	      || (event->state & GDK_SHIFT_MASK)))
		gtk_tree_view_set_cursor(tree_view, path, NULL, false);

	gtk_widget_grab_focus(GTK_WIDGET(tree_view)); /*give keyboard focus*/

	if (signal_params->button_list) {
		(display_data->set_menu)(tree_view, *signal_params->button_list,
					 path, ROW_LEFT_CLICKED);
	} else
		(display_data->set_menu)(tree_view, NULL, path, FULL_CLICKED);

	/* make sure it was a double click */
	if (!gtk_tree_model_get_iter(model, &iter, path)) {
		g_error("left pressed, error getting iter from model\n");
		return rc;
	}
	if (!(now-last_time)
	    && (!last_user_data || (iter.user_data == last_user_data))) {
		/* double click */
		(display_data->set_menu)(tree_view, NULL, path, FULL_CLICKED);
	}
	last_user_data = iter.user_data;

	if (!working_sview_config.admin_mode)
		rc = true;

	last_time = now;

	return rc;
}

extern gboolean row_activated(GtkTreeView *tree_view, GtkTreePath *path,
			      GtkTreeViewColumn *column,
			      const signal_params_t *signal_params)
{
	display_data_t *display_data = signal_params->display_data;
	/* highlight the nodes from this row */
	(display_data->set_menu)(tree_view, *signal_params->button_list,
				 path, ROW_LEFT_CLICKED);
	/* display the full info */
	if (!enter_key_in_effect)
		(display_data->set_menu)(tree_view, NULL, path, FULL_CLICKED);
	enter_key_in_effect = false;

	return false;
}

extern gboolean key_pressed(GtkTreeView *tree_view,
			    GdkEventKey *event,
			    const signal_params_t *signal_params)
{
	control_key_in_effect = false;
	enter_key_in_effect = false;

	if ((event->keyval == GDK_Control_L) ||
	    (event->keyval == GDK_Control_R))
		control_key_in_effect = true;
	else if (event->keyval == GDK_Return) {
		each_t each;
		GtkTreeSelection *selection = NULL;

		selection = gtk_tree_view_get_selection(tree_view);
		memset(&each, 0, sizeof(each_t));
		each.tree_view = tree_view;
		each.display_data = signal_params->display_data;
		global_row_count =
			gtk_tree_selection_count_selected_rows(selection);
		popup_pos.x = 10;
		popup_pos.x = 10;
		popup_pos.cntr = 1;
		popup_pos.slider = 0;
		gtk_tree_selection_selected_foreach(
			selection, _foreach_full_info, &each);
		/*prevent row_activation from
		 * performing a redundant 'full info'*/
		enter_key_in_effect = true;

	}

	return false;
}/*key_pressed ^^^*/


extern gboolean key_released(GtkTreeView *tree_view,
			     GdkEventKey *event,
			     const signal_params_t *signal_params)
{

	GtkTreePath *path = NULL;
	GtkTreeViewColumn *column;
	GtkTreeSelection *selection = NULL;

	if ((event->keyval != GDK_Up) &&
	    (event->keyval != GDK_Down) &&
	    (event->keyval != GDK_Return))
		return true;

	gtk_tree_view_get_cursor(GTK_TREE_VIEW(tree_view), &path, &column);
	if (path) {
		selection = gtk_tree_view_get_selection(tree_view);
		gtk_tree_selection_select_path(selection, path);
		gtk_tree_path_free(path);
	}
	return true;


}/*key_released ^^^*/

extern gboolean row_clicked(GtkTreeView *tree_view, GdkEventButton *event,
			    const signal_params_t *signal_params)
{
	GtkTreePath *path = NULL;
	GtkTreePath *last_path = NULL;
	GtkTreeSelection *selection = NULL;
	gboolean did_something = false;
	gboolean selected_in_current_mix = false;

	if (!gtk_tree_view_get_path_at_pos(tree_view,
					   (gint) event->x,
					   (gint) event->y,
					   &path, NULL, NULL, NULL)) {
		selection = gtk_tree_view_get_selection(tree_view);

		/* If there is a selection AND we are not under
		 * multi-selection processing via the ctrl key clear
		 * it up by doing a refresh.  If there wasn't a
		 * selection before OR we are stacking selections via
		 * the ctrl key do nothing here. */
		if (gtk_tree_selection_count_selected_rows(selection)){
			if (!(event->state & GDK_CONTROL_MASK))
				gtk_tree_selection_unselect_all(selection);
			refresh_main(NULL, NULL);
			return true;
		}
		return false;
	}

	/* make the selection (highlight) here */
	selection = gtk_tree_view_get_selection(tree_view);
	global_row_count =
		gtk_tree_selection_count_selected_rows(selection);

	/*flag this for rightclick to unselect on*/
	selected_in_current_mix =
		gtk_tree_selection_path_is_selected(selection, path);

	if (event->button != 3) {
		/*if Lshift is down then pull a range out*/
		if ((event->state & GDK_SHIFT_MASK)) {
			if (last_event_x != 0) {
				if (gtk_tree_view_get_path_at_pos(
					    tree_view,
					    (gint) last_event_x,
					    (gint) last_event_y,
					    &last_path, NULL, NULL, NULL)) {
					if (last_path) {
						gtk_tree_selection_select_range(
							selection, last_path,
							path);
						gtk_tree_path_free(last_path);
					}
				}
			} else if (path) {
				/*ignore shift and pull single row anyway*/
				gtk_tree_selection_select_path(selection,
							       path);
			}
		} /*shift down^^*/
	}
	last_event_x = event->x; /*save THIS x*/
	last_event_y = event->y; /*save THIS y*/

	if (event->x <= 28) {
		/* When you try to resize a column this event happens
		   for some reason.  Resizing always happens in the
		   first 2 of x so if that happens just return and
		   continue. Also if we want to expand/collapse a
		   column, that happens in the first 28 (The default
		   expander size is 14, and as of writing this we
		   could expand 2 levels, so just skip
		   that also.  If anyone in the future can figure out
		   a way to know for sure the expander was clicked
		   and not the actual column please fix this :).
		*/
		did_something = false;
	} else if (event->button == 1) {
		/*  left click */
		if (!(event->state & GDK_CONTROL_MASK)
		    && !(event->state & GDK_SHIFT_MASK)) {
			/* unselect current on naked left clicks..*/
			gtk_tree_selection_unselect_all(selection);
		}
		did_something = left_button_pressed(
			tree_view, path, signal_params, event);
	} else if (event->button == 3) {
		/*  right click */
		if (!selected_in_current_mix) {
			if (!(event->state & GDK_CONTROL_MASK)){
				gtk_tree_selection_unselect_all(selection);
			} else if (path)
				gtk_tree_selection_select_path(selection, path);
		}
		global_row_count =
			gtk_tree_selection_count_selected_rows(selection);
		if (_DEBUG)
			g_print("row_clicked:global_row_count2 : %d \n",
				global_row_count);
		/*prevent rc processing if under contol/shift*/
		if (!(event->state & GDK_CONTROL_MASK)
		    && !(event->state & GDK_SHIFT_MASK))
			right_button_pressed(tree_view, path, event,
					     signal_params, ROW_CLICKED);
		did_something = true;
	} else if (!working_sview_config.admin_mode)
		did_something = true;
	gtk_tree_path_free(path);

	/* If control key held refresh main (which does the grid and
	   exit with false to reset the treeview.  This has to happen
	   after left_button_pressed to get other things correct. */
	if (event->state & GDK_CONTROL_MASK) {
		refresh_main(NULL, NULL);
		return false; /*propagate event*/
	}
	return did_something;
}

extern popup_info_t *create_popup_info(int type, int dest_type, char *title)
{
	GtkScrolledWindow *window = NULL, *grid_window = NULL;
	GtkBin *bin = NULL;
	GtkViewport *view = NULL;
	GtkWidget *label = NULL;
	GtkWidget *table = NULL;
	GtkWidget *close_btn = NULL;
	popup_info_t *popup_win = xmalloc(sizeof(popup_info_t));
//	int i=0;

	list_push(popup_list, popup_win);

	popup_win->spec_info = xmalloc(sizeof(specific_info_t));
	popup_win->spec_info->search_info =
		xmalloc(sizeof(sview_search_info_t));
	popup_win->spec_info->search_info->search_type = 0;
	popup_win->spec_info->search_info->gchar_data = NULL;
	popup_win->spec_info->search_info->int_data = NO_VAL;
	popup_win->spec_info->search_info->int_data2 = NO_VAL;

	popup_win->spec_info->type = type;
	popup_win->spec_info->title = xstrdup(title);
	popup_win->popup = gtk_dialog_new_with_buttons(
		title,
		GTK_WINDOW(main_window),
		GTK_DIALOG_DESTROY_WITH_PARENT,
		GTK_STOCK_REFRESH,
		GTK_RESPONSE_OK,
		NULL);
	close_btn = gtk_dialog_add_button(GTK_DIALOG(popup_win->popup),
					  GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE);
	gtk_window_set_type_hint(GTK_WINDOW(popup_win->popup),
				 GDK_WINDOW_TYPE_HINT_NORMAL);
	gtk_window_set_focus(GTK_WINDOW(popup_win->popup), close_btn);
	gtk_dialog_add_button(GTK_DIALOG(popup_win->popup),
			      "Close All Popups", GTK_RESPONSE_CANCEL);


	popup_win->show_grid = 1;
	popup_win->toggled = 0;
	popup_win->force_refresh = 0;
	popup_win->type = dest_type;
	popup_win->not_found = false;
	/*
	  for(i=0;; i++) {
	  if (main_popup_positioner[i].width == -1)
	  break;
	  if (strstr(title,main_popup_positioner[i].name)) {
	  width = main_popup_positioner[i].width;
	  height = main_popup_positioner[i].height;
	  break;
	  }
	  }
	*/
	gtk_window_set_default_size(GTK_WINDOW(popup_win->popup),
				    working_sview_config.fi_popup_width,
				    working_sview_config.fi_popup_height);
	gtk_window_set_transient_for(GTK_WINDOW(popup_win->popup), NULL);

	popup_win->event_box = gtk_event_box_new();
	label = gtk_label_new(popup_win->spec_info->title);
	gtk_container_add(GTK_CONTAINER(popup_win->event_box), label);

	g_signal_connect(G_OBJECT(popup_win->event_box),
			 "button-press-event",
			 G_CALLBACK(redo_popup),
			 popup_win);

	gtk_event_box_set_above_child(
		GTK_EVENT_BOX(popup_win->event_box),
		false);

	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(popup_win->popup)->vbox),
			   popup_win->event_box, false, false, 0);

	grid_window = create_scrolled_window();
	gtk_scrolled_window_set_policy(grid_window,
				       GTK_POLICY_NEVER,
				       GTK_POLICY_AUTOMATIC);
	bin = GTK_BIN(&grid_window->container);
	view = GTK_VIEWPORT(bin->child);
	bin = GTK_BIN(&view->bin);
	popup_win->grid_table = GTK_TABLE(bin->child);
	popup_win->grid_button_list = NULL;

	table = gtk_table_new(1, 2, false);

	gtk_table_attach(GTK_TABLE(table), GTK_WIDGET(grid_window), 0, 1, 0, 1,
			 GTK_SHRINK, GTK_EXPAND | GTK_FILL,
			 0, 0);

	window = create_scrolled_window();
	bin = GTK_BIN(&window->container);
	view = GTK_VIEWPORT(bin->child);
	bin = GTK_BIN(&view->bin);
	popup_win->table = GTK_TABLE(bin->child);

	gtk_table_attach_defaults(GTK_TABLE(table), GTK_WIDGET(window),
				  1, 2, 0, 1);

	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(popup_win->popup)->vbox),
			   table, true, true, 0);

	g_signal_connect(G_OBJECT(popup_win->popup), "delete_event",
			 G_CALLBACK(delete_popup),
			 popup_win->spec_info->title);
	g_signal_connect(G_OBJECT(popup_win->popup), "response",
			 G_CALLBACK(_handle_response),
			 popup_win);
	g_signal_connect(G_OBJECT(popup_win->popup), "configure-event",
			 G_CALLBACK(_frame_callback),
			 popup_win);
	gtk_window_move(GTK_WINDOW(popup_win->popup),
			popup_pos.x, popup_pos.y);
	gtk_widget_show_all(popup_win->popup);

	if (cluster_flags & CLUSTER_FLAG_FED)
		gtk_widget_hide(GTK_WIDGET(grid_window));

	return popup_win;
}

extern void setup_popup_info(popup_info_t *popup_win,
			     display_data_t *display_data,
			     int cnt)
{
	int i = 0;

	popup_win->display_data = xmalloc(sizeof(display_data_t)*(cnt+2));
	for(i=0; i<cnt+1; i++) {
		memcpy(&popup_win->display_data[i],
		       &display_data[i],
		       sizeof(display_data_t));
	}
}

extern void redo_popup(GtkWidget *widget, GdkEventButton *event,
		       popup_info_t *popup_win)
{
	if (event && (event->button == 3)) {
		GtkMenu *menu = GTK_MENU(gtk_menu_new());

		(popup_win->display_data->set_menu)(popup_win, menu,
						    NULL,
						    POPUP_CLICKED);

		gtk_widget_show_all(GTK_WIDGET(menu));
		gtk_menu_popup(menu, NULL, NULL, NULL, NULL, event->button,
			       gdk_event_get_time((GdkEvent*)event));
	}
}

extern void destroy_search_info(void *arg)
{
	sview_search_info_t *search_info = (sview_search_info_t *)arg;
	if (search_info) {
		g_free(search_info->cluster_name);
		if (search_info->gchar_data)
			g_free(search_info->gchar_data);
		search_info->gchar_data = NULL;
		xfree(search_info);
		search_info = NULL;
	}
}

extern void destroy_specific_info(void *arg)
{
	specific_info_t *spec_info = (specific_info_t *)arg;
	if (spec_info) {
		xfree(spec_info->title);

		destroy_search_info(spec_info->search_info);

		if (spec_info->display_widget) {
			gtk_widget_destroy(spec_info->display_widget);
			spec_info->display_widget = NULL;
		}
		xfree(spec_info);
	}
}

extern void destroy_popup_info(void *arg)
{
	popup_info_t *popup_win = (popup_info_t *)arg;

	if (popup_win) {
		*popup_win->running = 0;
		g_mutex_lock(sview_mutex);
		/* these are all children of each other so must
		   be freed in this order */
		FREE_NULL_LIST(popup_win->grid_button_list);
		if (popup_win->table) {
			gtk_widget_destroy(GTK_WIDGET(popup_win->table));
			popup_win->table = NULL;
		}
		if (popup_win->grid_table) {
			gtk_widget_destroy(GTK_WIDGET(popup_win->grid_table));
			popup_win->grid_table = NULL;
		}
		if (popup_win->event_box) {
			gtk_widget_destroy(popup_win->event_box);
			popup_win->event_box = NULL;
		}
		if (popup_win->popup) {
			gtk_widget_destroy(popup_win->popup);
			popup_win->popup = NULL;
		}

		destroy_specific_info(popup_win->spec_info);
		xfree(popup_win->display_data);
		xfree(popup_win);
		g_mutex_unlock(sview_mutex);
	}

}

extern void destroy_signal_params(void *arg)
{
	signal_params_t *signal_params = (signal_params_t *)arg;

	if (signal_params) {
		xfree(signal_params);
	}
}

extern gboolean delete_popup(GtkWidget *widget, GtkWidget *event, char *title)
{
	ListIterator itr = list_iterator_create(popup_list);
	popup_info_t *popup_win = NULL;

	while ((popup_win = list_next(itr))) {
		if (popup_win->spec_info) {
			if (!xstrcmp(popup_win->spec_info->title, title)) {
				//g_print("removing %s\n", title);
				list_remove(itr);
				destroy_popup_info(popup_win);
				break;
			}
		}
	}
	list_iterator_destroy(itr);


	return false;
}

extern gboolean delete_popups(void)
{
	ListIterator itr = list_iterator_create(popup_list);
	popup_info_t *popup_win = NULL;

	while ((popup_win = list_next(itr))) {
		list_remove(itr);
		destroy_popup_info(popup_win);
	}
	list_iterator_destroy(itr);

	return false;
}

extern void *popup_thr(popup_info_t *popup_win)
{
	void (*specifc_info) (popup_info_t *popup_win) = NULL;
	int running = 1;
	if (_DEBUG)
		g_print("popup_thr:global_row_count = %d \n",
			global_row_count);
	switch(popup_win->type) {
	case PART_PAGE:
		specifc_info = specific_info_part;
		break;
	case JOB_PAGE:
		specifc_info = specific_info_job;
		break;
	case NODE_PAGE:
		specifc_info = specific_info_node;
		break;
	case RESV_PAGE:
		specifc_info = specific_info_resv;
		break;
	case FRONT_END_PAGE:
		specifc_info = specific_info_front_end;
		break;
	case BB_PAGE:
		specifc_info = specific_info_bb;
		break;
	case SUBMIT_PAGE:
	default:
		g_print("thread got unknown type %d\n", popup_win->type);
		return NULL;
	}
	/* this will switch to 0 when popup is closed. */
	popup_win->running = &running;
	/* when popup is killed running will be set to 0 */
	while (running) {
		gdk_threads_enter();
		(specifc_info)(popup_win);
		gdk_threads_leave();
		sleep(working_sview_config.refresh_delay);
	}
	return NULL;
}

extern void set_for_update(GtkTreeModel *model, int updated)
{
	GtkTreePath *path = gtk_tree_path_new_first();
	GtkTreeIter iter;

	/* mark all current rows as in need of an update. */
	if (path && gtk_tree_model_get_iter(model, &iter, path)) {
		/* This process will make sure all iter's in the
		 * tree_store will be mark as needing to be updated.
		 * If it is still 0 after the update then it is old
		 * data and will be removed with remove_old()
		 */
		while (1) {
			gtk_tree_store_set(GTK_TREE_STORE(model), &iter,
					   updated, 0, -1);
			if (!gtk_tree_model_iter_next(model, &iter)) {
				break;
			}
		}
	}

	if (path)
		gtk_tree_path_free(path);
}

extern void remove_old(GtkTreeModel *model, int updated)
{
	GtkTreePath *path = gtk_tree_path_new_first();
	GtkTreeIter iter;
	int i;

	/* remove all old objects */
	if (gtk_tree_model_get_iter(model, &iter, path)) {
		while (1) {
			gtk_tree_model_get(model, &iter, updated, &i, -1);
			if (!i) {
				if (!gtk_tree_store_remove(
					    GTK_TREE_STORE(model),
					    &iter))
					break;
				else
					continue;
			}
			if (!gtk_tree_model_iter_next(model, &iter)) {
				break;
			}
		}
	}
	gtk_tree_path_free(path);
}

extern GtkWidget *create_pulldown_combo(display_data_t *display_data)
{
	GtkListStore *store = NULL;
	GtkWidget *combo = NULL;
	GtkTreeIter iter;
	GtkCellRenderer *renderer = NULL;
	int i=0;

	store = gtk_list_store_new(2, G_TYPE_INT, G_TYPE_STRING);
	while (display_data[i].id != -1) {
		gtk_list_store_append(store, &iter);
		gtk_list_store_set(store, &iter, 0, display_data[i].id,
				   1, display_data[i].name, -1);
		i++;
	}
	combo = gtk_combo_box_new_with_model(GTK_TREE_MODEL(store));

	g_object_unref(store);
	renderer = gtk_cell_renderer_text_new();
	gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(combo), renderer, true);
	gtk_cell_layout_add_attribute(GTK_CELL_LAYOUT(combo), renderer,
				      "text", 1);

	gtk_combo_box_set_active (GTK_COMBO_BOX (combo), 0);
	return combo;
}

/*
 * str_tolower - convert string to all lower case
 * upper_str IN - upper case input string
 * RET - lower case version of upper_str, caller must be xfree
 */
extern char *str_tolower(char *upper_str)
{
	int i = strlen(upper_str) + 1;
	char *lower_str = xmalloc(i);

	for (i=0; upper_str[i]; i++)
		lower_str[i] = tolower((int) upper_str[i]);

	return lower_str;
}

extern char *get_reason(void)
{
	char *reason_str = NULL;
	int len = 0;
	GtkWidget *table = gtk_table_new(1, 2, false);
	GtkWidget *label = gtk_label_new("Reason ");
	GtkWidget *entry = gtk_entry_new();
	GtkWidget *popup = gtk_dialog_new_with_buttons(
		"State change reason",
		GTK_WINDOW(main_window),
		GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
		GTK_STOCK_OK,
		GTK_RESPONSE_OK,
		GTK_STOCK_CANCEL,
		GTK_RESPONSE_CANCEL,
		NULL);
	int response = 0;
	char *user_name = NULL;
	char time_str[32];
	time_t now = time(NULL);

	gtk_window_set_type_hint(GTK_WINDOW(popup),
				 GDK_WINDOW_TYPE_HINT_NORMAL);

	gtk_container_set_border_width(GTK_CONTAINER(table), 10);

	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(popup)->vbox),
			   table, false, false, 0);

	gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 1, 0, 1);
	gtk_table_attach_defaults(GTK_TABLE(table), entry, 1, 2, 0, 1);

	gtk_widget_show_all(popup);
	response = gtk_dialog_run (GTK_DIALOG(popup));

	if (response == GTK_RESPONSE_OK)
	{
		reason_str = xstrdup(gtk_entry_get_text(GTK_ENTRY(entry)));
		len = strlen(reason_str) - 1;
		if (len == -1) {
			xfree(reason_str);
			reason_str = NULL;
			goto end_it;
		}
		/* Append user, date and time */
		xstrcat(reason_str, " [");
		user_name = getlogin();
		if (user_name)
			xstrcat(reason_str, user_name);
		else
			xstrfmtcat(reason_str, "%d", getuid());
		slurm_make_time_str(&now, time_str, sizeof(time_str));
		xstrfmtcat(reason_str, "@%s]", time_str);
	} else
		reason_str = xstrdup("cancelled");
end_it:
	gtk_widget_destroy(popup);

	return reason_str;
}

extern void display_admin_edit(GtkTable *table, void *type_msg, int *row,
			       GtkTreeModel *model, GtkTreeIter *iter,
			       display_data_t *display_data,
			       GCallback changed_callback,
			       GCallback focus_callback,
			       void (*set_active)(
				       GtkComboBox *combo,
				       GtkTreeModel *model, GtkTreeIter *iter,
				       int type))
{
	GtkWidget *label = NULL;
	GtkWidget *entry = NULL;

	if (display_data->extra == EDIT_MODEL) {
		/* edittable items that can only be known
		   values */
		GtkCellRenderer *renderer = NULL;
		GtkTreeModel *model2 = GTK_TREE_MODEL(
			(display_data->create_model)(display_data->id));
		if (!model2) {
			g_print("no model set up for %d(%s)\n",
				display_data->id,
				display_data->name);
			return;
		}
		entry = gtk_combo_box_new_with_model(model2);
		g_object_unref(model2);

/*		(callback)_set_active_combo_part(GTK_COMBO_BOX(entry), model, */
/*				       iter, display_data->id); */
		(set_active)(GTK_COMBO_BOX(entry), model,
			     iter, display_data->id);

		g_signal_connect(entry, "changed",
				 changed_callback,
				 type_msg);

		renderer = gtk_cell_renderer_text_new();
		gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(entry),
					   renderer, true);
		gtk_cell_layout_add_attribute(GTK_CELL_LAYOUT(entry),
					      renderer, "text", 0);
	} else if (display_data->extra == EDIT_TEXTBOX) {
		char *temp_char = NULL;
		/* other edittable items that are unknown */
		entry = create_entry();
		if (model) {
			gtk_tree_model_get(model, iter,
					   display_data->id,
					   &temp_char, -1);
		}
		gtk_entry_set_max_length(GTK_ENTRY(entry),
					 (DEFAULT_ENTRY_LENGTH +
					  display_data->id));

		if (temp_char) {
			gtk_entry_set_text(GTK_ENTRY(entry),
					   temp_char);
			g_free(temp_char);
		}
		g_signal_connect(entry, "focus-out-event",
				 focus_callback,
				 type_msg);

		/* set global variable so we know something changed */
		g_signal_connect(entry, "changed",
				 G_CALLBACK(entry_changed),
				 NULL);
	} else /* others can't be altered by the user */
		return;
	label = gtk_label_new(display_data->name);
	/* left justify */
	gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
	gtk_table_attach(table, label, 0, 1, *row, (*row)+1,
			 GTK_FILL | GTK_EXPAND, GTK_SHRINK,
			 0, 0);
	gtk_table_attach(table, entry, 1, 2, *row, (*row)+1,
			 GTK_FILL, GTK_SHRINK,
			 0, 0);
	(*row)++;
}

extern void display_edit_note(char *edit_note)
{
	GError *error = NULL;
	int msg_id = 0;

	if (!edit_note)
		return;

	gtk_statusbar_pop(GTK_STATUSBAR(main_statusbar), STATUS_ADMIN_EDIT);
	msg_id = gtk_statusbar_push(GTK_STATUSBAR(main_statusbar),
				    STATUS_ADMIN_EDIT,
				    edit_note);
	if (!sview_thread_new(_editing_thr, GINT_TO_POINTER(msg_id),
			      false, &error))
		g_printerr("Failed to create edit thread: %s\n",
			   error->message);

	return;
}

extern void add_display_treestore_line(int update,
				       GtkTreeStore *treestore,
				       GtkTreeIter *iter,
				       const char *name,
				       const char *value)
{
	if (!name) {
/*		g_print("error, name = %s and value = %s\n", */
/*			name, value); */
		return;
	}
	if (update) {
		char *display_name = NULL;
		GtkTreePath *path = gtk_tree_path_new_first();
		gtk_tree_model_get_iter(GTK_TREE_MODEL(treestore), iter, path);

		while (1) {
			/* search for the jobid and check to see if
			   it is in the list */
			gtk_tree_model_get(GTK_TREE_MODEL(treestore), iter,
					   DISPLAY_NAME,
					   &display_name, -1);
			if (!xstrcmp(display_name, name)) {
				/* update with new info */
				g_free(display_name);
				goto found;
			}
			g_free(display_name);

			if (!gtk_tree_model_iter_next(GTK_TREE_MODEL(treestore),
						      iter)) {
				return;
			}
		}

	} else {
		gtk_tree_store_append(treestore, iter, NULL);
	}
found:
	gtk_tree_store_set(treestore, iter,
			   DISPLAY_NAME,  name,
			   DISPLAY_VALUE, value,
			   -1);

	return;
}

extern void add_display_treestore_line_with_font(
	int update,
	GtkTreeStore *treestore,
	GtkTreeIter *iter,
	const char *name, char *value,
	char *font)
{
	if (!name) {
/*		g_print("error, name = %s and value = %s\n", */
/*			name, value); */
		return;
	}
	if (update) {
		char *display_name = NULL;
		GtkTreePath *path = gtk_tree_path_new_first();
		gtk_tree_model_get_iter(GTK_TREE_MODEL(treestore), iter, path);

		while (1) {
			/* search for the jobid and check to see if
			   it is in the list */
			gtk_tree_model_get(GTK_TREE_MODEL(treestore), iter,
					   DISPLAY_NAME,
					   &display_name, -1);
			if (!xstrcmp(display_name, name)) {
				/* update with new info */
				g_free(display_name);
				goto found;
			}
			g_free(display_name);

			if (!gtk_tree_model_iter_next(GTK_TREE_MODEL(treestore),
						      iter)) {
				return;
			}
		}

	} else {
		gtk_tree_store_append(treestore, iter, NULL);
	}
found:
	gtk_tree_store_set(treestore, iter,
			   DISPLAY_NAME,  name,
			   DISPLAY_VALUE, value,
			   DISPLAY_FONT,  font,
			   -1);

	return;
}

extern void sview_widget_modify_bg(GtkWidget *widget, GtkStateType state,
				   const GdkColor color)
{
	gtk_widget_modify_bg(widget, state, &color);
}

extern void sview_radio_action_set_current_value(GtkRadioAction *action,
						 gint current_value)
{
#ifdef GTK2_USE_RADIO_SET
	gtk_radio_action_set_current_value(action, current_value);
#else
	GSList *slist, *group;
	int i=0;
	/* gtk_radio_action_set_current_value wasn't added to
	   GTK until 2.10, it turns out this is what is required to
	   set the correct value.
	*/
	g_return_if_fail(GTK_IS_RADIO_ACTION(action));

	if ((group = gtk_radio_action_get_group(action))) {
		/* for some reason groups are set backwards like a
		   stack, g_slist_reverse will fix this but takes twice
		   as long so just figure out the length, they add 1
		   to it sense 0 isn't a number and then subtract the
		   value to get the augmented in the stack.
		*/
		current_value = g_slist_length(group) - 1 - current_value;
		if (current_value < 0) {
			g_warning("Radio group does not contain an action "
				  "with value '%d'\n", current_value);
			return;
		}

		for (slist = group; slist; slist = slist->next) {
			if (i == current_value) {
				gtk_toggle_action_set_active(
					GTK_TOGGLE_ACTION(slist->data), true);
				return;
			}
			i++;
		}
	}
#endif
}

extern char *page_to_str(int page)
{
	switch (page) {
	case JOB_PAGE:
		return "Job";
	case PART_PAGE:
		return "Partition";
	case RESV_PAGE:
		return "Reservation";
	case BB_PAGE:
		return "BurstBuffer";
	case NODE_PAGE:
		return "Node";
	case FRONT_END_PAGE:
		return "Frontend";
	default:
		return NULL;
	}
}

extern char *tab_pos_to_str(int pos)
{
	switch (pos) {
	case GTK_POS_TOP:
		return "Top";
	case GTK_POS_BOTTOM:
		return "Bottom";
	case GTK_POS_LEFT:
		return "Left";
	case GTK_POS_RIGHT:
		return "Right";
	default:
		return "Unknown";
	}
}

extern char *visible_to_str(sview_config_t *sview_config)
{
	char *ret = NULL;
	int i;

	for (i = 0; i < PAGE_CNT; i++) {
		if (sview_config->page_visible[i] && (i != TAB_PAGE)) {
			if (ret)
				xstrcat(ret, ",");
			xstrcat(ret, page_to_str(i));
		}
	}

	return ret;
}

extern gboolean entry_changed(GtkWidget *widget, void *msg)
{
	global_entry_changed = 1;
	return false;
}

extern void select_admin_common(GtkTreeModel *model, GtkTreeIter *iter,
				display_data_t *display_data,
				GtkTreeView *treeview,
				uint32_t node_col,
				void (*process_each)(GtkTreeModel *model,
						     GtkTreePath *path,
						     GtkTreeIter *iter,
						     gpointer userdata))
{
	GtkTreePath *path;
	GtkTreeRowReference *ref;
	GtkTreeSelection *selection;
	GList *list = NULL, *selected_rows = NULL;

	if (!treeview)
		return;

	if (display_data->extra & EXTRA_NODES) {
		select_admin_nodes(model, iter, display_data,
				   node_col, treeview);
		return;
	}

	global_multi_error = false;

	selection = gtk_tree_view_get_selection(treeview);
	selected_rows =	gtk_tree_selection_get_selected_rows(selection, &model);

	for (list = selected_rows; list; list = g_list_next(list)) {
		ref = gtk_tree_row_reference_new(model, list->data);
		path = gtk_tree_row_reference_get_path(ref);
		(*(process_each))(model, path, iter, display_data->name);
		gtk_tree_path_free(path);
		gtk_tree_row_reference_free(ref);
	}

	g_list_foreach(selected_rows, (GFunc)gtk_tree_path_free, NULL);
	g_list_free(selected_rows);

	return;
}
