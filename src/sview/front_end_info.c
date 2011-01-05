/*****************************************************************************\
 *  front_end_info.c - Functions related to front end node display
 *  mode of sview.
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2011 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#include "src/common/uid.h"
#include "src/sview/sview.h"
#include "src/common/parse_time.h"

#define _DEBUG 0

/* Collection of data for printing reports. Like data is combined here */
typedef struct {
	int color_inx;
	front_end_info_t *front_end_ptr;
} sview_front_end_info_t;

enum {
	EDIT_REMOVE = 1,
	EDIT_EDIT
};

/* These need to be in alpha order (except POS and CNT) */
enum {
	SORTID_POS = POS_LOC,
	SORTID_COLOR,
	SORTID_COLOR_INX,
	SORTID_NAME,
	SORTID_STATE,
	SORTID_CNT
};

/* extra field here is for choosing the type of edit you that will
 * take place.  If you choose EDIT_MODEL (means only display a set of
 * known options) create it in function create_model_*.
 */

/*these are the settings to apply for the user
 * on the first startup after a fresh slurm install.
 * s/b a const probably*/
static char *_initial_page_opts = "Name,State";

static display_data_t display_data_resv[] = {
	{G_TYPE_INT, SORTID_POS, NULL, FALSE, EDIT_NONE,
	 refresh_front_end, create_model_front_end, admin_edit_front_end},
	{G_TYPE_STRING, SORTID_NAME, "Name", FALSE, EDIT_NONE,
	 refresh_front_end, create_model_front_end, admin_edit_front_end},
	{G_TYPE_STRING, SORTID_STATE, "State", FALSE, EDIT_MODEL,
	 refresh_front_end, create_model_front_end, admin_edit_front_end},
	{G_TYPE_STRING, SORTID_COLOR,  NULL, TRUE, EDIT_COLOR,
	 refresh_front_end, create_model_front_end, admin_edit_front_end},
	{G_TYPE_INT, SORTID_COLOR_INX,  NULL, FALSE, EDIT_NONE,
	 refresh_front_end, create_model_front_end, admin_edit_front_end},
	{G_TYPE_NONE, -1, NULL, FALSE, EDIT_NONE}
};

static display_data_t options_data_resv[] = {
	{G_TYPE_INT, SORTID_POS, NULL, FALSE, EDIT_NONE},
	{G_TYPE_STRING, INFO_PAGE, "Full Info", TRUE, RESV_PAGE},
	{G_TYPE_STRING, RESV_PAGE, "Remove", TRUE, ADMIN_PAGE},
	{G_TYPE_STRING, RESV_PAGE, "Edit Reservation", TRUE, ADMIN_PAGE},
	{G_TYPE_STRING, JOB_PAGE, "Jobs", TRUE, RESV_PAGE},
#ifdef HAVE_BG
	{G_TYPE_STRING, BLOCK_PAGE, "Blocks", TRUE, RESV_PAGE},
	{G_TYPE_STRING, NODE_PAGE, "Base Partitions", TRUE, RESV_PAGE},
#else
	{G_TYPE_STRING, BLOCK_PAGE, NULL, TRUE, RESV_PAGE},
	{G_TYPE_STRING, NODE_PAGE, "Nodes", TRUE, RESV_PAGE},
#endif
	{G_TYPE_NONE, -1, NULL, FALSE, EDIT_NONE}
};


static display_data_t *local_display_data = NULL;

static char *got_edit_signal = NULL;

static void _admin_resv(GtkTreeModel *model, GtkTreeIter *iter, char *type);
static void _process_each_resv(GtkTreeModel *model, GtkTreePath *path,
			       GtkTreeIter*iter, gpointer userdata);

static void _set_active_combo_resv(GtkComboBox *combo,
				   GtkTreeModel *model, GtkTreeIter *iter,
				   int type)
{
	char *temp_char = NULL;
	int action = 0;

	gtk_tree_model_get(model, iter, type, &temp_char, -1);
	if (!temp_char)
		goto end_it;
	g_free(temp_char);
end_it:
	gtk_combo_box_set_active(combo, action);

}

/* don't free this char */
static const char *_set_resv_msg(resv_desc_msg_t *resv_msg,
				 const char *new_text,
				 int column)
{
	char *type = "";

	/* need to clear global_edit_error here (just in case) */
	global_edit_error = 0;

	if (!resv_msg)
		return NULL;

	switch (column) {
	case SORTID_NAME:
		resv_msg->name = xstrdup(new_text);
		type = "name";
		break;
	default:
		type = "unknown";
		break;
	}

	if (strcmp(type, "unknown"))
		global_send_update_msg = 1;

	return type;
}

static void _front_end_info_list_del(void *object)
{
	xfree(object);
}

static void _admin_edit_combo_box_resv(GtkComboBox *combo,
				       resv_desc_msg_t *resv_msg)
{
	GtkTreeModel *model = NULL;
	GtkTreeIter iter;
	int column = 0;
	char *name = NULL;

	if (!resv_msg)
		return;

	if (!gtk_combo_box_get_active_iter(combo, &iter)) {
		g_print("nothing selected\n");
		return;
	}
	model = gtk_combo_box_get_model(combo);
	if (!model) {
		g_print("nothing selected\n");
		return;
	}

	gtk_tree_model_get(model, &iter, 0, &name, -1);
	gtk_tree_model_get(model, &iter, 1, &column, -1);

	_set_resv_msg(resv_msg, name, column);

	g_free(name);
}



static gboolean _admin_focus_out_resv(GtkEntry *entry,
				      GdkEventFocus *event,
				      resv_desc_msg_t *resv_msg)
{
	if (global_entry_changed) {
		const char *col_name = NULL;
		int type = gtk_entry_get_max_length(entry);
		const char *name = gtk_entry_get_text(entry);
		type -= DEFAULT_ENTRY_LENGTH;
		col_name = _set_resv_msg(resv_msg, name, type);
		if (global_edit_error) {
			if (global_edit_error_msg)
				g_free(global_edit_error_msg);
			global_edit_error_msg = g_strdup_printf(
				"Reservation %s %s can't be set to %s",
				resv_msg->name,
				col_name,
				name);
		}
		global_entry_changed = 0;
	}
	return false;
}

static GtkWidget *_admin_full_edit_resv(resv_desc_msg_t *resv_msg,
					GtkTreeModel *model, GtkTreeIter *iter)
{
	GtkScrolledWindow *window = create_scrolled_window();
	GtkBin *bin = NULL;
	GtkViewport *view = NULL;
	GtkTable *table = NULL;
	int i = 0, row = 0;
	display_data_t *display_data = display_data_resv;

	gtk_scrolled_window_set_policy(window,
				       GTK_POLICY_NEVER,
				       GTK_POLICY_AUTOMATIC);
	bin = GTK_BIN(&window->container);
	view = GTK_VIEWPORT(bin->child);
	bin = GTK_BIN(&view->bin);
	table = GTK_TABLE(bin->child);
	gtk_table_resize(table, SORTID_CNT, 2);

	gtk_table_set_homogeneous(table, FALSE);

	for(i = 0; i < SORTID_CNT; i++) {
		while (display_data++) {
			if (display_data->id == -1)
				break;
			if (!display_data->name)
				continue;
			if (display_data->id != i)
				continue;
			display_admin_edit(
				table, resv_msg, &row, model, iter,
				display_data,
				G_CALLBACK(_admin_edit_combo_box_resv),
				G_CALLBACK(_admin_focus_out_resv),
				_set_active_combo_resv);
			break;
		}
		display_data = display_data_resv;
	}
	gtk_table_resize(table, row, 2);

	return GTK_WIDGET(window);
}

static void _layout_resv_record(GtkTreeView *treeview,
				sview_front_end_info_t *sview_front_end_info,
				int update)
{
	GtkTreeStore *treestore =
		GTK_TREE_STORE(gtk_tree_view_get_model(treeview));
}

static void _update_resv_record(sview_front_end_info_t *sview_front_end_info_ptr,
				GtkTreeStore *treestore,
				GtkTreeIter *iter)
{
	front_end_info_t *front_end_ptr;
	char *upper = NULL, *lower = NULL;

	front_end_ptr = sview_front_end_info_ptr->front_end_ptr;
	gtk_tree_store_set(treestore, iter, SORTID_COLOR,
			   sview_colors[sview_front_end_info_ptr->color_inx],
			   -1);
	gtk_tree_store_set(treestore, iter, SORTID_COLOR_INX,
			   sview_front_end_info_ptr->color_inx, -1);

	gtk_tree_store_set(treestore, iter, SORTID_NAME, front_end_ptr->name,
			   -1);

	upper = node_state_string(front_end_ptr->node_state);
	lower = str_tolower(upper);
	gtk_tree_store_set(treestore, iter, SORTID_STATE, lower, -1);
	xfree(lower);

	return;
}

static void _append_resv_record(sview_front_end_info_t *sview_front_end_info_ptr,
				GtkTreeStore *treestore, GtkTreeIter *iter,
				int line)
{
	gtk_tree_store_append(treestore, iter, NULL);
	gtk_tree_store_set(treestore, iter, SORTID_POS, line, -1);
	_update_resv_record(sview_front_end_info_ptr, treestore, iter);
}

static void _update_info_resv(List info_list,
			      GtkTreeView *tree_view)
{
	GtkTreePath *path = gtk_tree_path_new_first();
	GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
	GtkTreeIter iter;
	front_end_info_t *front_end_ptr = NULL;
	int line = 0;
	char *host = NULL, *front_end_name = NULL;
	ListIterator itr = NULL;
	sview_front_end_info_t *sview_front_end_info = NULL;

	/* get the iter, or find out the list is empty goto add */
	if (gtk_tree_model_get_iter(model, &iter, path)) {
		/* make sure all the reserves are still here */
		while (1) {
			if (!gtk_tree_model_iter_next(model, &iter)) {
				break;
			}
		}
	}

	itr = list_iterator_create(info_list);
	while ((sview_front_end_info = (sview_front_end_info_t*) list_next(itr))) {
		front_end_ptr = sview_front_end_info->front_end_ptr;
		/* get the iter, or find out the list is empty goto add */
		if (!gtk_tree_model_get_iter(model, &iter, path)) {
			goto adding;
		}
		line = 0;
		while (1) {
			/* search for the jobid and check to see if
			   it is in the list */
			gtk_tree_model_get(model, &iter, SORTID_NAME,
					   &front_end_name, -1);
			if (!strcmp(front_end_name, front_end_ptr->name)) {
				/* update with new info */
				g_free(front_end_name);
				_update_resv_record(sview_front_end_info,
						    GTK_TREE_STORE(model),
						    &iter);
				goto found;
			}
			g_free(front_end_name);

			line++;
			if (!gtk_tree_model_iter_next(model, &iter)) {
				break;
			}
		}
	adding:
		_append_resv_record(sview_front_end_info, GTK_TREE_STORE(model),
				    &iter, line);
	found:
		;
	}
	list_iterator_destroy(itr);
	if (host)
		free(host);

	gtk_tree_path_free(path);

	return;
}

static List _create_front_end_info_list(front_end_info_msg_t *front_end_info_ptr,
					int changed)
{
	static List info_list = NULL;
	int i = 0;
	sview_front_end_info_t *sview_front_end_info_ptr = NULL;
	front_end_info_t *front_end_ptr = NULL;

	if (!changed && info_list)
		goto update_color;

	if (info_list)
		list_flush(info_list);
	else
		info_list = list_create(_front_end_info_list_del);

	if (!info_list) {
		g_print("malloc error\n");
		return NULL;
	}

	for (i=0; i<front_end_info_ptr->record_count; i++) {
		front_end_ptr = &(front_end_info_ptr->front_end_array[i]);
		sview_front_end_info_ptr =
			xmalloc(sizeof(sview_front_end_info_t));
		sview_front_end_info_ptr->front_end_ptr = front_end_ptr;
		sview_front_end_info_ptr->color_inx = i % sview_colors_cnt;
		list_append(info_list, sview_front_end_info_ptr);
	}

update_color:
	return info_list;
}

static void _display_info_front_end(List info_list, popup_info_t *popup_win)
{
	specific_info_t *spec_info = popup_win->spec_info;
	char *name = (char *)spec_info->search_info->gchar_data;
	int found = 0;
	front_end_info_t *front_end_ptr = NULL;
	GtkTreeView *treeview = NULL;
	ListIterator itr = NULL;
	sview_front_end_info_t *sview_front_end_info = NULL;
	int update = 0;

	if (!spec_info->search_info->gchar_data) {
		//info = xstrdup("No pointer given!");
		goto finished;
	}

need_refresh:
	if (!spec_info->display_widget) {
		treeview = create_treeview_2cols_attach_to_table(
			popup_win->table);
		spec_info->display_widget =
			gtk_widget_ref(GTK_WIDGET(treeview));
	} else {
		treeview = GTK_TREE_VIEW(spec_info->display_widget);
		update = 1;
	}

	itr = list_iterator_create(info_list);
	while ((sview_front_end_info =
	       (sview_front_end_info_t*) list_next(itr))) {
		front_end_ptr = sview_front_end_info->front_end_ptr;
		if (!strcmp(front_end_ptr->name, name)) {
			_layout_resv_record(treeview, sview_front_end_info,
					    update);
			found = 1;
			break;
		}
	}
	list_iterator_destroy(itr);
	post_setup_popup_grid_list(popup_win);

	if (!found) {
		if (!popup_win->not_found) {
			char *temp = "RESERVATION DOESN'T EXSIST\n";
			GtkTreeIter iter;
			GtkTreeModel *model = NULL;

			/* only time this will be run so no update */
			model = gtk_tree_view_get_model(treeview);
			add_display_treestore_line(0,
						   GTK_TREE_STORE(model),
						   &iter,
						   temp, "");
		}
		popup_win->not_found = true;
	} else {
		if (popup_win->not_found) {
			popup_win->not_found = false;
			gtk_widget_destroy(spec_info->display_widget);

			goto need_refresh;
		}
	}
	gtk_widget_show(spec_info->display_widget);

finished:

	return;
}

extern void refresh_front_end(GtkAction *action, gpointer user_data)
{
	popup_info_t *popup_win = (popup_info_t *)user_data;
	xassert(popup_win);
	xassert(popup_win->spec_info);
	xassert(popup_win->spec_info->title);
	popup_win->force_refresh = 1;
	specific_info_front_end(popup_win);
}

extern int get_new_info_front_end(front_end_info_msg_t **info_ptr, int force)
{
	static front_end_info_msg_t *new_front_end_ptr = NULL;
	int error_code = SLURM_NO_CHANGE_IN_DATA;
	time_t now = time(NULL);
	static time_t last;
	static bool changed = 0;

	if (g_front_end_info_ptr && !force &&
	    ((now - last) < working_sview_config.refresh_delay)) {
		if (*info_ptr != g_front_end_info_ptr)
			error_code = SLURM_SUCCESS;
		*info_ptr = g_front_end_info_ptr;
		if (changed)
			error_code = SLURM_SUCCESS;
		goto end_it;
	}
	last = now;
	if (g_front_end_info_ptr) {
		error_code = slurm_load_front_end(
			g_front_end_info_ptr->last_update, &new_front_end_ptr);
		if (error_code == SLURM_SUCCESS) {
			slurm_free_front_end_info_msg(g_front_end_info_ptr);
			changed = 1;
		} else if (slurm_get_errno() == SLURM_NO_CHANGE_IN_DATA) {
			error_code = SLURM_NO_CHANGE_IN_DATA;
			new_front_end_ptr = g_front_end_info_ptr;
			changed = 0;
		}
	} else {
		new_front_end_ptr = NULL;
		error_code = slurm_load_front_end((time_t) NULL,
						  &new_front_end_ptr);
		changed = 1;
	}

	g_front_end_info_ptr = new_front_end_ptr;

	if (g_front_end_info_ptr && (*info_ptr != g_front_end_info_ptr))
		error_code = SLURM_SUCCESS;

	*info_ptr = g_front_end_info_ptr;
end_it:
	return error_code;
}

extern GtkListStore *create_model_front_end(int type)
{
	GtkListStore *model = NULL;

	switch (type) {
	default:
		break;
	}
	return model;
}

extern void admin_edit_front_end(GtkCellRendererText *cell,
				 const char *path_string,
				 const char *new_text, gpointer data)
{
#if 0
	GtkTreeStore *treestore = GTK_TREE_STORE(data);
	GtkTreePath *path = gtk_tree_path_new_from_string(path_string);
	GtkTreeIter iter;
	front_end_desc_msg_t *front_end_msg = xmalloc(sizeof(front_end_desc_msg_t));

	char *temp = NULL;
	char *old_text = NULL;
	const char *type = NULL;

	int column = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(cell),
						       "column"));

	if (!new_text || !strcmp(new_text, ""))
		goto no_input;

	gtk_tree_model_get_iter(GTK_TREE_MODEL(treestore), &iter, path);

	slurm_init_resv_desc_msg(resv_msg);
	gtk_tree_model_get(GTK_TREE_MODEL(treestore), &iter,
			   SORTID_NAME, &temp,
			   column, &old_text,
			   -1);
	resv_msg->name = xstrdup(temp);
	g_free(temp);

	type = _set_resv_msg(resv_msg, new_text, column);
	if (global_edit_error)
		goto print_error;

	if (got_edit_signal) {
		temp = got_edit_signal;
		got_edit_signal = NULL;
		_admin_resv(GTK_TREE_MODEL(treestore), &iter, temp);
		xfree(temp);
		goto no_input;
	}

	if (old_text && !strcmp(old_text, new_text)) {
		temp = g_strdup_printf("No change in value.");
	} else if (slurm_update_reservation(resv_msg)
		  == SLURM_SUCCESS) {
		gtk_tree_store_set(treestore, &iter, column, new_text, -1);
		temp = g_strdup_printf("Reservation %s %s changed to %s",
				       resv_msg->name,
				       type,
				       new_text);
	} else if (errno == ESLURM_DISABLED) {
		temp = g_strdup_printf(
			"Can only edit %s on reservations not yet started.",
			type);
	} else {
	print_error:
		temp = g_strdup_printf("Reservation %s %s can't be "
				       "set to %s",
				       resv_msg->name,
				       type,
				       new_text);
	}

	display_edit_note(temp);
	g_free(temp);

no_input:
	slurm_free_front_end_desc_msg(front_end_msg);

	gtk_tree_path_free (path);
	g_free(old_text);
	g_static_mutex_unlock(&sview_mutex);
#endif
}

extern void get_info_front_end(GtkTable *table, display_data_t *display_data)
{
	int error_code = SLURM_SUCCESS;
	List info_list = NULL;
	static int view = -1;
	static front_end_info_msg_t *front_end_info_ptr = NULL;
	char error_char[100];
	GtkWidget *label = NULL;
	GtkTreeView *tree_view = NULL;
	static GtkWidget *display_widget = NULL;
	int changed = 1;
	GtkTreePath *path = NULL;
	static bool set_opts = FALSE;

	if (!set_opts)
		set_page_opts(FRONT_END_PAGE, display_data_resv,
			      SORTID_CNT, _initial_page_opts);
	set_opts = TRUE;

	/* reset */
	if (!table && !display_data) {
		if (display_widget)
			gtk_widget_destroy(display_widget);
		display_widget = NULL;
		front_end_info_ptr = NULL;
		goto reset_curs;
	}

	if (display_data)
		local_display_data = display_data;
	if (!table) {
		display_data_resv->set_menu = local_display_data->set_menu;
		goto reset_curs;
	}
	if (display_widget && toggled) {
		gtk_widget_destroy(display_widget);
		display_widget = NULL;
		goto display_it;
	}

	error_code = get_new_info_front_end(&front_end_info_ptr, force_refresh);
	if (error_code == SLURM_NO_CHANGE_IN_DATA) {
		changed = 0;
	} else if (error_code != SLURM_SUCCESS) {
		if (view == ERROR_VIEW)
			goto end_it;
		if (display_widget)
			gtk_widget_destroy(display_widget);
		view = ERROR_VIEW;
		sprintf(error_char, "slurm_load_front_end: %s",
			slurm_strerror(slurm_get_errno()));
		label = gtk_label_new(error_char);
		gtk_table_attach_defaults(table, label, 0, 1, 0, 1);
		gtk_widget_show(label);
		display_widget = gtk_widget_ref(GTK_WIDGET(label));
		goto end_it;
	}

display_it:
	info_list = _create_front_end_info_list(front_end_info_ptr, changed);
	if (!info_list)
		goto reset_curs;
	/* set up the grid */
	if (display_widget && GTK_IS_TREE_VIEW(display_widget) &&
	    gtk_tree_selection_count_selected_rows(
		   gtk_tree_view_get_selection(
			   GTK_TREE_VIEW(display_widget)))) {
		GtkTreeViewColumn *focus_column = NULL;
		/* highlight the correct nodes from the last selection */
		gtk_tree_view_get_cursor(GTK_TREE_VIEW(display_widget),
					 &path, &focus_column);
	}
	if (!path) {
		change_grid_color(grid_button_list, -1, -1,
				  MAKE_WHITE, true, 0);
	} else
		highlight_grid(GTK_TREE_VIEW(display_widget),
			       SORTID_NAME, SORTID_COLOR_INX,
			       grid_button_list);

	if (working_sview_config.grid_speedup) {
		gtk_widget_set_sensitive(GTK_WIDGET(main_grid_table), 0);
		gtk_widget_set_sensitive(GTK_WIDGET(main_grid_table), 1);
	}

	if (view == ERROR_VIEW && display_widget) {
		gtk_widget_destroy(display_widget);
		display_widget = NULL;
	}
	if (!display_widget) {
		tree_view = create_treeview(local_display_data,
					    &grid_button_list);
		gtk_tree_selection_set_mode(
			gtk_tree_view_get_selection(tree_view),
			GTK_SELECTION_MULTIPLE);
		display_widget = gtk_widget_ref(GTK_WIDGET(tree_view));
		gtk_table_attach_defaults(table,
					  GTK_WIDGET(tree_view),
					  0, 1, 0, 1);
		/* since this function sets the model of the tree_view
		   to the treestore we don't really care about
		   the return value */
		create_treestore(tree_view, display_data_resv,
				 SORTID_CNT, SORTID_NAME, SORTID_COLOR);
	}

	view = INFO_VIEW;
	_update_info_resv(info_list, GTK_TREE_VIEW(display_widget));
end_it:
	toggled = FALSE;
	force_refresh = FALSE;
reset_curs:
	if (main_window && main_window->window)
		gdk_window_set_cursor(main_window->window, NULL);
	return;
}

extern void specific_info_front_end(popup_info_t *popup_win)
{
	int resv_error_code = SLURM_SUCCESS;
	static front_end_info_msg_t *front_end_info_ptr = NULL;
	static front_end_info_t *front_end_ptr = NULL;
	specific_info_t *spec_info = popup_win->spec_info;
	sview_search_info_t *search_info = spec_info->search_info;
	char error_char[100];
	GtkWidget *label = NULL;
	GtkTreeView *tree_view = NULL;
	List resv_list = NULL;
	List send_resv_list = NULL;
	int changed = 1;
	sview_front_end_info_t *sview_front_end_info_ptr = NULL;
	int i = -1;
	ListIterator itr = NULL;

	if (!spec_info->display_widget) {
		setup_popup_info(popup_win, display_data_resv, SORTID_CNT);
	}

	if (spec_info->display_widget && popup_win->toggled) {
		gtk_widget_destroy(spec_info->display_widget);
		spec_info->display_widget = NULL;
		goto display_it;
	}

	resv_error_code = get_new_info_front_end(&front_end_info_ptr,
						 popup_win->force_refresh);
	if (resv_error_code == SLURM_NO_CHANGE_IN_DATA) {
		if (!spec_info->display_widget || spec_info->view == ERROR_VIEW)
			goto display_it;
		changed = 0;
	} else if (resv_error_code != SLURM_SUCCESS) {
		if (spec_info->view == ERROR_VIEW)
			goto end_it;
		spec_info->view = ERROR_VIEW;
		if (spec_info->display_widget)
			gtk_widget_destroy(spec_info->display_widget);
		sprintf(error_char, "get_new_info_front_end: %s",
			slurm_strerror(slurm_get_errno()));
		label = gtk_label_new(error_char);
		gtk_table_attach_defaults(popup_win->table,
					  label,
					  0, 1, 0, 1);
		gtk_widget_show(label);
		spec_info->display_widget = gtk_widget_ref(label);
		goto end_it;
	}

display_it:

	resv_list = _create_front_end_info_list(front_end_info_ptr, changed);

	if (!resv_list)
		return;

	if (spec_info->view == ERROR_VIEW && spec_info->display_widget) {
		gtk_widget_destroy(spec_info->display_widget);
		spec_info->display_widget = NULL;
	}
	if (spec_info->type != INFO_PAGE && !spec_info->display_widget) {
		tree_view = create_treeview(local_display_data,
					    &popup_win->grid_button_list);
		gtk_tree_selection_set_mode(
			gtk_tree_view_get_selection(tree_view),
			GTK_SELECTION_MULTIPLE);
		spec_info->display_widget =
			gtk_widget_ref(GTK_WIDGET(tree_view));
		gtk_table_attach_defaults(popup_win->table,
					  GTK_WIDGET(tree_view),
					  0, 1, 0, 1);
		/* since this function sets the model of the tree_view
		   to the treestore we don't really care about
		   the return value */
		create_treestore(tree_view, popup_win->display_data,
				 SORTID_CNT, SORTID_NAME, SORTID_COLOR);
	}

	setup_popup_grid_list(popup_win);

	spec_info->view = INFO_VIEW;
	if (spec_info->type == INFO_PAGE) {
		_display_info_front_end(resv_list, popup_win);
		goto end_it;
	}

	/* just linking to another list, don't free the inside, just
	   the list */
	send_resv_list = list_create(NULL);
	itr = list_iterator_create(resv_list);
	i = -1;
	while ((sview_front_end_info_ptr = list_next(itr))) {
		i++;
		front_end_ptr = sview_front_end_info_ptr->front_end_ptr;
		switch (spec_info->type) {
		case PART_PAGE:
		case BLOCK_PAGE:
		case NODE_PAGE:
			break;
		case JOB_PAGE:
			if (strcmp(front_end_ptr->name,
				   search_info->gchar_data))
				continue;
			break;
		case RESV_PAGE:
			switch (search_info->search_type) {
			case SEARCH_RESERVATION_NAME:
				if (!search_info->gchar_data)
					continue;

				if (strcmp(front_end_ptr->name,
					   search_info->gchar_data))
					continue;
				break;
			default:
				continue;
			}
			break;
		default:
			g_print("Unknown type %d\n", spec_info->type);
			continue;
		}
		list_push(send_resv_list, sview_front_end_info_ptr);
	}
	list_iterator_destroy(itr);
	post_setup_popup_grid_list(popup_win);

	_update_info_resv(send_resv_list,
			  GTK_TREE_VIEW(spec_info->display_widget));
	list_destroy(send_resv_list);
end_it:
	popup_win->toggled = 0;
	popup_win->force_refresh = 0;

	return;
}

extern void set_menus_front_end(void *arg, void *arg2, GtkTreePath *path,
				int type)
{
	GtkTreeView *tree_view = (GtkTreeView *)arg;
	popup_info_t *popup_win = (popup_info_t *)arg;
	GtkMenu *menu = (GtkMenu *)arg2;
	List button_list = (List)arg2;

	switch (type) {
	case TAB_CLICKED:
		make_fields_menu(NULL, menu, display_data_resv, SORTID_CNT);
		break;
	case ROW_CLICKED:
		make_options_menu(tree_view, path, menu, options_data_resv);
		break;
	case ROW_LEFT_CLICKED:
		highlight_grid(tree_view, SORTID_NAME,
			       SORTID_COLOR_INX, button_list);
		break;
	case FULL_CLICKED:
	{
		GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
		GtkTreeIter iter;
		if (!gtk_tree_model_get_iter(model, &iter, path)) {
			g_error("error getting iter from model\n");
			break;
		}

		popup_all_front_end(model, &iter, INFO_PAGE);

		break;
	}
	case POPUP_CLICKED:
		make_fields_menu(popup_win, menu,
				 popup_win->display_data, SORTID_CNT);
		break;
	default:
		g_error("UNKNOWN type %d given to set_fields\n", type);
	}
}

extern void popup_all_front_end(GtkTreeModel *model, GtkTreeIter *iter, int id)
{
	char *name = NULL;
	char title[100];
	ListIterator itr = NULL;
	popup_info_t *popup_win = NULL;
	GError *error = NULL;

	gtk_tree_model_get(model, iter, SORTID_NAME, &name, -1);

	switch (id) {
	case INFO_PAGE:
		snprintf(title, 100, "Full info for front end node %s", name);
		break;
	default:
		g_print("front end got %d\n", id);
	}

	itr = list_iterator_create(popup_list);
	while ((popup_win = list_next(itr))) {
		if (popup_win->spec_info)
			if (!strcmp(popup_win->spec_info->title, title)) {
				break;
			}
	}
	list_iterator_destroy(itr);

	if (!popup_win) {
		if (id == INFO_PAGE)
			popup_win = create_popup_info(id, RESV_PAGE, title);
		else
			popup_win = create_popup_info(RESV_PAGE, id, title);
	} else {
		g_free(name);
		gtk_window_present(GTK_WINDOW(popup_win->popup));
		return;
	}

	/* Pass the model and the structs from the iter so we can always get
	 * the current node_inx.
	 */
	popup_win->model = model;
	popup_win->iter = *iter;
	popup_win->node_inx_id = SORTID_NAME;	/* Should be NODE_INX */

	switch (id) {
	case INFO_PAGE:
		popup_win->spec_info->search_info->gchar_data = name;
		//specific_info_job(popup_win);
		break;
	default:
		g_print("resv got unknown type %d\n", id);
	}
	if (!g_thread_create((gpointer)popup_thr, popup_win, FALSE, &error)) {
		g_printerr ("Failed to create resv popup thread: %s\n",
			    error->message);
		return;
	}
}

static void _process_each_resv(GtkTreeModel *model, GtkTreePath *path,
			       GtkTreeIter*iter, gpointer userdata)
{
	char *type = userdata;
	if (_DEBUG)
		g_print("process_each_resv: global_multi_error = %d\n",
			global_multi_error);

	if (!global_multi_error) {
		_admin_resv(model, iter, type);
	}
}

extern void select_admin_front_end(GtkTreeModel *model, GtkTreeIter *iter,
				   display_data_t *display_data,
				   GtkTreeView *treeview)
{
	if (treeview) {
		global_multi_error = FALSE;
		gtk_tree_selection_selected_foreach(
			gtk_tree_view_get_selection(treeview),
			_process_each_resv, display_data->name);
	}
}

static void _admin_resv(GtkTreeModel *model, GtkTreeIter *iter, char *type)
{
	resv_desc_msg_t *resv_msg = xmalloc(sizeof(resv_desc_msg_t));
	reservation_name_msg_t resv_name_msg;
	char *resvid = NULL;
	char tmp_char[100];
	char *temp = NULL;
	int edit_type = 0;
	int response = 0;
	GtkWidget *label = NULL;
	GtkWidget *entry = NULL;
	GtkWidget *popup = gtk_dialog_new_with_buttons(
		type,
		GTK_WINDOW(main_window),
		GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
		NULL);
	gtk_window_set_transient_for(GTK_WINDOW(popup), NULL);

	gtk_tree_model_get(model, iter, SORTID_NAME, &resvid, -1);

	slurm_init_resv_desc_msg(resv_msg);
	memset(&resv_name_msg, 0, sizeof(reservation_name_msg_t));

	resv_msg->name = xstrdup(resvid);

	if (!strcasecmp("Remove", type)) {
		resv_name_msg.name = resvid;

		label = gtk_dialog_add_button(GTK_DIALOG(popup),
					      GTK_STOCK_YES, GTK_RESPONSE_OK);
		gtk_window_set_default(GTK_WINDOW(popup), label);
		gtk_dialog_add_button(GTK_DIALOG(popup),
				      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);

		snprintf(tmp_char, sizeof(tmp_char),
			 "Are you sure you want to remove "
			 "reservation %s?",
			 resvid);
		label = gtk_label_new(tmp_char);
		edit_type = EDIT_REMOVE;
	} else {
		label = gtk_dialog_add_button(GTK_DIALOG(popup),
					      GTK_STOCK_OK, GTK_RESPONSE_OK);
		gtk_window_set_default(GTK_WINDOW(popup), label);
		gtk_dialog_add_button(GTK_DIALOG(popup),
				      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);

		gtk_window_set_default_size(GTK_WINDOW(popup), 200, 400);
		snprintf(tmp_char, sizeof(tmp_char),
			 "Editing reservation %s think before you type",
			 resvid);
		label = gtk_label_new(tmp_char);
		edit_type = EDIT_EDIT;
		entry = _admin_full_edit_resv(resv_msg, model, iter);
	}

	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(popup)->vbox),
			   label, FALSE, FALSE, 0);
	if (entry)
		gtk_box_pack_start(GTK_BOX(GTK_DIALOG(popup)->vbox),
				   entry, TRUE, TRUE, 0);
	gtk_widget_show_all(popup);
	response = gtk_dialog_run (GTK_DIALOG(popup));

	if (response == GTK_RESPONSE_OK) {
		switch (edit_type) {
		case EDIT_REMOVE:
			if (slurm_delete_reservation(&resv_name_msg)
			   == SLURM_SUCCESS) {
				temp = g_strdup_printf(
					"Reservation %s removed successfully",
					resvid);
			} else {
				temp = g_strdup_printf(
					"Problem removing reservation %s.",
					resvid);
			}
			display_edit_note(temp);
			g_free(temp);
			break;
		case EDIT_EDIT:
			if (got_edit_signal)
				goto end_it;

			if (!global_send_update_msg) {
				temp = g_strdup_printf("No change detected.");
			} else if (slurm_update_reservation(resv_msg)
				  == SLURM_SUCCESS) {
				temp = g_strdup_printf(
					"Reservation %s updated successfully",
					resvid);
			} else {
				temp = g_strdup_printf(
					"Problem updating reservation %s.",
					resvid);
			}
			display_edit_note(temp);
			g_free(temp);
			break;
		default:
			break;
		}
	}
end_it:

	g_free(resvid);
	global_entry_changed = 0;
	slurm_free_resv_desc_msg(resv_msg);
	gtk_widget_destroy(popup);
	if (got_edit_signal) {
		type = got_edit_signal;
		got_edit_signal = NULL;
		_admin_resv(model, iter, type);
		xfree(type);
	}
	return;
}

extern void cluster_change_front_end(void)
{
	display_data_t *display_data = display_data_resv;

	display_data = options_data_resv;
	while (display_data++) {
		if (display_data->id == -1)
			break;

		if (cluster_flags & CLUSTER_FLAG_BG) {
			switch (display_data->id) {
			case BLOCK_PAGE:
				display_data->name = "Blocks";
				break;
			case NODE_PAGE:
				display_data->name = "Base Partitions";
				break;
			}
		} else {
			switch (display_data->id) {
			case BLOCK_PAGE:
				display_data->name = NULL;
				break;
			case NODE_PAGE:
				display_data->name = "Nodes";
				break;
			}
		}
	}
	get_info_front_end(NULL, NULL);
}
