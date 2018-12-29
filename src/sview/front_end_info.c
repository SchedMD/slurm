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

#include "src/common/uid.h"
#include "src/sview/sview.h"
#include "src/common/parse_time.h"

#define _DEBUG 0

/* Collection of data for printing reports. Like data is combined here */
typedef struct {
	int color_inx;
	char *front_end_name;
	front_end_info_t *front_end_ptr;
	GtkTreeIter iter_ptr;
	bool iter_set;
	char *boot_time;
	int node_inx[3];
	int pos;
	char *reason;
	char *slurmd_start_time;
	char *state;
} sview_front_end_info_t;

typedef struct {
	char *node_list;
} front_end_user_data_t;

enum {
	EDIT_REMOVE = 1,
	EDIT_EDIT
};

/* These need to be in alpha order (except POS and CNT) */
enum {
	SORTID_POS = POS_LOC,
	SORTID_ALLOW_GROUPS,
	SORTID_ALLOW_USERS,
	SORTID_BOOT_TIME,
	SORTID_COLOR,
	SORTID_COLOR_INX,
	SORTID_DENY_GROUPS,
	SORTID_DENY_USERS,
	SORTID_NAME,
	SORTID_NODE_INX,
	SORTID_REASON,
	SORTID_SLURMD_START_TIME,
	SORTID_STATE,
	SORTID_UPDATED,
	SORTID_VERSION,
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

static display_data_t display_data_front_end[] = {
	{G_TYPE_INT, SORTID_POS, NULL, false, EDIT_NONE,
	 refresh_front_end, create_model_front_end, admin_edit_front_end},
	{G_TYPE_STRING, SORTID_NAME, "Name", false, EDIT_NONE,
	 refresh_front_end, create_model_front_end, admin_edit_front_end},
	{G_TYPE_STRING, SORTID_COLOR,  NULL, true, EDIT_COLOR,
	 refresh_front_end, create_model_front_end, admin_edit_front_end},
	{G_TYPE_STRING, SORTID_STATE, "State", false, EDIT_MODEL,
	 refresh_front_end, create_model_front_end, admin_edit_front_end},
	{G_TYPE_STRING, SORTID_BOOT_TIME, "BootTime", false, EDIT_NONE,
	 refresh_front_end, create_model_front_end, admin_edit_front_end},
	{G_TYPE_STRING, SORTID_SLURMD_START_TIME, "SlurmdStartTime",
	 false, EDIT_NONE,
	 refresh_front_end, create_model_front_end, admin_edit_front_end},
	{G_TYPE_STRING, SORTID_REASON, "Reason", false, EDIT_NONE,
	 refresh_front_end, create_model_front_end, admin_edit_front_end},
	{G_TYPE_STRING, SORTID_ALLOW_GROUPS, "Allow Groups", false, EDIT_NONE,
	 refresh_front_end, create_model_front_end, admin_edit_front_end},
	{G_TYPE_STRING, SORTID_ALLOW_USERS, "Allow Users", false, EDIT_NONE,
	 refresh_front_end, create_model_front_end, admin_edit_front_end},
	{G_TYPE_STRING, SORTID_DENY_GROUPS, "Deny Groups", false, EDIT_NONE,
	 refresh_front_end, create_model_front_end, admin_edit_front_end},
	{G_TYPE_STRING, SORTID_DENY_USERS, "Deny Users", false, EDIT_NONE,
	 refresh_front_end, create_model_front_end, admin_edit_front_end},
	{G_TYPE_STRING, SORTID_VERSION, "Version", false, EDIT_NONE,
	 refresh_front_end, create_model_front_end, admin_edit_front_end},
	{G_TYPE_INT, SORTID_COLOR_INX,  NULL, false, EDIT_NONE,
	 refresh_front_end, create_model_front_end, admin_edit_front_end},
	{G_TYPE_POINTER, SORTID_NODE_INX,  NULL, false, EDIT_NONE,
	 refresh_front_end, create_model_front_end, admin_edit_front_end},
	{G_TYPE_INT,    SORTID_UPDATED,    NULL, false, EDIT_NONE,
	 refresh_resv, create_model_resv, admin_edit_resv},
	{G_TYPE_NONE, -1, NULL, false, EDIT_NONE}
};

static display_data_t options_data_front_end[] = {
	{G_TYPE_INT, SORTID_POS, NULL, false, EDIT_NONE},
	{G_TYPE_STRING, INFO_PAGE, "Full Info", true, FRONT_END_PAGE},
	{G_TYPE_STRING, FRONT_END_PAGE, "Drain Front End Node", true,
	 ADMIN_PAGE},
	{G_TYPE_STRING, FRONT_END_PAGE, "Resume Front End Node", true,
	 ADMIN_PAGE},
	{G_TYPE_NONE, -1, NULL, false, EDIT_NONE}
};


static display_data_t *local_display_data = NULL;
static char *got_edit_signal = NULL;
static GtkTreeModel *last_model = NULL;

static void _admin_front_end(GtkTreeModel *model, GtkTreeIter *iter, char *type,
			     char *node_list);
static void _process_each_front_end(GtkTreeModel *model, GtkTreePath *path,
				    GtkTreeIter*iter, gpointer userdata);

static void _front_end_info_free(sview_front_end_info_t *sview_front_end_info)
{
	if (sview_front_end_info) {
		xfree(sview_front_end_info->boot_time);
		xfree(sview_front_end_info->reason);
		xfree(sview_front_end_info->slurmd_start_time);
		xfree(sview_front_end_info->state);
	}
}

static void _front_end_info_list_del(void *object)
{
	sview_front_end_info_t *sview_front_end_info;

	sview_front_end_info = (sview_front_end_info_t *)object;
	if (sview_front_end_info) {
		_front_end_info_free(sview_front_end_info);
		xfree(sview_front_end_info);
	}
}

static void _layout_front_end_record(GtkTreeView *treeview,
				     sview_front_end_info_t *
				     sview_front_end_info,
				     int update)
{

	GtkTreeIter iter;
	front_end_info_t *front_end_ptr =
		sview_front_end_info->front_end_ptr;
	GtkTreeStore *treestore =
		GTK_TREE_STORE(gtk_tree_view_get_model(treeview));

	if (!treestore)
		return;

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_front_end,
						 SORTID_NAME),
				   front_end_ptr->name);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_front_end,
						 SORTID_STATE),
				   sview_front_end_info->state);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_front_end,
						 SORTID_BOOT_TIME),
				   sview_front_end_info->boot_time);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_front_end,
						 SORTID_SLURMD_START_TIME),
				   sview_front_end_info->slurmd_start_time);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_front_end,
						 SORTID_REASON),
				   sview_front_end_info->reason);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_front_end,
						 SORTID_ALLOW_GROUPS),
				   front_end_ptr->allow_groups);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_front_end,
						 SORTID_ALLOW_USERS),
				   front_end_ptr->allow_users);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_front_end,
						 SORTID_DENY_GROUPS),
				   front_end_ptr->deny_groups);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_front_end,
						 SORTID_DENY_USERS),
				   front_end_ptr->deny_users);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_front_end,
						 SORTID_VERSION),
				   front_end_ptr->version);
}

static void _update_front_end_record(
			sview_front_end_info_t *sview_front_end_info_ptr,
			GtkTreeStore *treestore)
{
	front_end_info_t *front_end_ptr;

	front_end_ptr = sview_front_end_info_ptr->front_end_ptr;

	/* Combining these records provides a slight performance improvement */
	gtk_tree_store_set(treestore, &sview_front_end_info_ptr->iter_ptr,
			   SORTID_ALLOW_GROUPS, front_end_ptr->allow_groups,
			   SORTID_ALLOW_USERS,  front_end_ptr->allow_users,
			   SORTID_BOOT_TIME,
				sview_front_end_info_ptr->boot_time,
			   SORTID_COLOR, sview_colors[
				   sview_front_end_info_ptr->color_inx],
			   SORTID_COLOR_INX,
				sview_front_end_info_ptr->color_inx,
			   SORTID_DENY_GROUPS, front_end_ptr->deny_groups,
			   SORTID_DENY_USERS, front_end_ptr->deny_users,
			   SORTID_NODE_INX,
				sview_front_end_info_ptr->node_inx,
			   SORTID_NAME,    front_end_ptr->name,
			   SORTID_REASON,  sview_front_end_info_ptr->reason,
			   SORTID_SLURMD_START_TIME,
				sview_front_end_info_ptr->slurmd_start_time,
			   SORTID_STATE,   sview_front_end_info_ptr->state,
			   SORTID_UPDATED,    1,
			   SORTID_VERSION, front_end_ptr->version,
			   -1);

	return;
}

static void _append_front_end_record(
			sview_front_end_info_t *sview_front_end_info_ptr,
			GtkTreeStore *treestore)
{
	gtk_tree_store_append(treestore, &sview_front_end_info_ptr->iter_ptr,
			      NULL);
	gtk_tree_store_set(treestore, &sview_front_end_info_ptr->iter_ptr,
			   SORTID_POS, sview_front_end_info_ptr->pos, -1);
	_update_front_end_record(sview_front_end_info_ptr, treestore);
}

static void _update_info_front_end(List info_list, GtkTreeView *tree_view)
{
	GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
	char *name;
	ListIterator itr = NULL;
	sview_front_end_info_t *sview_front_end_info = NULL;

	set_for_update(model, SORTID_UPDATED);

	itr = list_iterator_create(info_list);
	while ((sview_front_end_info = list_next(itr))) {
		/* This means the tree_store changed (added new column
		   or something). */
		if (last_model != model)
			sview_front_end_info->iter_set = false;

		if (sview_front_end_info->iter_set) {
			gtk_tree_model_get(model,
					   &sview_front_end_info->iter_ptr,
					   SORTID_NAME, &name, -1);
			if (xstrcmp(name,
				    sview_front_end_info->front_end_name)) {
				/* Bad pointer */
				sview_front_end_info->iter_set = false;
				//g_print("bad front_end iter pointer\n");
			}
			g_free(name);
		}
		if (sview_front_end_info->iter_set)
			_update_front_end_record(sview_front_end_info,
						 GTK_TREE_STORE(model));
		else {
			_append_front_end_record(sview_front_end_info,
						 GTK_TREE_STORE(model));
			sview_front_end_info->iter_set = true;
		}
	}
	list_iterator_destroy(itr);

	/* remove all old front_ends */
	remove_old(model, SORTID_UPDATED);
	last_model = model;
}

static List _create_front_end_info_list(
	front_end_info_msg_t *front_end_info_ptr, int changed)
{
	char *upper = NULL;
	char user[32], time_str[32];
	static List info_list = NULL;
	List last_list = NULL;
	ListIterator last_list_itr = NULL;
	int i = 0;
	sview_front_end_info_t *sview_front_end_info_ptr = NULL;
	front_end_info_t *front_end_ptr = NULL;

	if (!changed && info_list)
		goto update_color;

	if (info_list)
		last_list = info_list;

	info_list = list_create(_front_end_info_list_del);

	if (last_list)
		last_list_itr = list_iterator_create(last_list);
	for (i = 0; i < front_end_info_ptr->record_count; i++) {
		front_end_ptr = &(front_end_info_ptr->front_end_array[i]);

		sview_front_end_info_ptr = NULL;

		if (last_list_itr) {
			while ((sview_front_end_info_ptr =
				list_next(last_list_itr))) {
				if (!xstrcmp(sview_front_end_info_ptr->
					     front_end_name,
					     front_end_ptr->name)) {
					list_remove(last_list_itr);
					_front_end_info_free(
						sview_front_end_info_ptr);
					break;
				}
			}
			list_iterator_reset(last_list_itr);
		}
		if (!sview_front_end_info_ptr)
			sview_front_end_info_ptr =
				xmalloc(sizeof(sview_front_end_info_t));
		sview_front_end_info_ptr->pos = i;
		sview_front_end_info_ptr->front_end_name = front_end_ptr->name;
		sview_front_end_info_ptr->front_end_ptr = front_end_ptr;
		sview_front_end_info_ptr->color_inx = i % sview_colors_cnt;
		if (g_node_info_ptr) {
			sview_front_end_info_ptr->node_inx[0] = 0;
			sview_front_end_info_ptr->node_inx[1] =
				g_node_info_ptr->record_count - 1;
			sview_front_end_info_ptr->node_inx[2] = -1;
		} else
			sview_front_end_info_ptr->node_inx[0] = -1;
		if (front_end_ptr->boot_time) {
			slurm_make_time_str(&front_end_ptr->boot_time,
					    time_str, sizeof(time_str));
			sview_front_end_info_ptr->boot_time =
				xstrdup(time_str);
		}
		if (front_end_ptr->slurmd_start_time) {
			slurm_make_time_str(&front_end_ptr->slurmd_start_time,
					    time_str, sizeof(time_str));
			sview_front_end_info_ptr->slurmd_start_time =
				xstrdup(time_str);
		}
		upper = node_state_string(front_end_ptr->node_state);
		sview_front_end_info_ptr->state = str_tolower(upper);

		if (front_end_ptr->reason && front_end_ptr->reason_time &&
		    (front_end_ptr->reason_uid != NO_VAL)) {
			struct passwd *pw = NULL;

			if ((pw=getpwuid(front_end_ptr->reason_uid)))
				snprintf(user, sizeof(user), "%s", pw->pw_name);
			else
				snprintf(user, sizeof(user), "Unk(%u)",
					 front_end_ptr->reason_uid);
			slurm_make_time_str(&front_end_ptr->reason_time,
					    time_str, sizeof(time_str));
			sview_front_end_info_ptr->reason =
				xstrdup_printf("%s [%s@%s]",
					       front_end_ptr->reason, user,
					       time_str);
		} else {
			sview_front_end_info_ptr->reason =
				xstrdup(front_end_ptr->reason);
		}

		list_append(info_list, sview_front_end_info_ptr);
	}

	if (last_list) {
		list_iterator_destroy(last_list_itr);
		FREE_NULL_LIST(last_list);
	}

update_color:
	return info_list;
}

static void _display_info_front_end(List info_list, popup_info_t *popup_win)
{
	specific_info_t *spec_info = popup_win->spec_info;
	char *name = (char *)spec_info->search_info->gchar_data;
	int found = 0, j;
	front_end_info_t *front_end_ptr = NULL;
	GtkTreeView *treeview = NULL;
	ListIterator itr = NULL;
	sview_front_end_info_t *sview_fe_info = NULL;
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
	while ((sview_fe_info = list_next(itr))) {
		front_end_ptr = sview_fe_info->front_end_ptr;
		if (xstrcmp(front_end_ptr->name, name) == 0) {
			j = 0;
			while (sview_fe_info->node_inx[j] >= 0) {
				change_grid_color(popup_win->grid_button_list,
						  sview_fe_info->node_inx[j],
						  sview_fe_info->node_inx[j+1],
						  sview_fe_info->color_inx,
						  true, 0);
				j += 2;
			}
			_layout_front_end_record(treeview, sview_fe_info,
						 update);
			found = 1;
			break;
		}
	}
	list_iterator_destroy(itr);
	post_setup_popup_grid_list(popup_win);

	if (!found) {
		if (!popup_win->not_found) {
			char *temp = "FRONT END DOESN'T EXSIST\n";
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
	GtkTreeIter iter;
	int i = 0;

	last_model = NULL;	/* Reformat display */
	switch(type) {
	case SORTID_STATE:
		model = gtk_list_store_new(2, G_TYPE_STRING,
					   G_TYPE_INT);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "Drain",
				   1, i,
				   -1);
		gtk_list_store_append(model, &iter);

		gtk_list_store_set(model, &iter,
				   0, "Resume",
				   1, i,
				   -1);
		break;

	}
	return model;
}

extern void admin_edit_front_end(GtkCellRendererText *cell,
				 const char *path_string,
				 const char *new_text, gpointer data)
{
	GtkTreeStore *treestore = GTK_TREE_STORE(data);
	GtkTreePath *path = gtk_tree_path_new_from_string(path_string);
	GtkTreeIter iter;
	char *node_list = NULL;
	int column = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(cell),
						       "column"));
	if (!new_text || !xstrcmp(new_text, ""))
		goto no_input;

	gtk_tree_model_get_iter(GTK_TREE_MODEL(treestore), &iter, path);
	switch(column) {
	case SORTID_STATE:
		gtk_tree_model_get(GTK_TREE_MODEL(treestore), &iter,
				   SORTID_NAME,
				   &node_list, -1);
		_admin_front_end(GTK_TREE_MODEL(treestore), &iter,
				 (char *)new_text, node_list);
		g_free(node_list);
	default:
		break;
	}
no_input:
	gtk_tree_path_free(path);
	g_mutex_unlock(sview_mutex);
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
	int changed = 1, j;
	ListIterator itr = NULL;
	GtkTreePath *path = NULL;
	static bool set_opts = false;

	if (!set_opts)
		set_page_opts(FRONT_END_PAGE, display_data_front_end,
			      SORTID_CNT, _initial_page_opts);
	set_opts = true;

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
		display_data_front_end->set_menu = local_display_data->set_menu;
		goto reset_curs;
	}
	if (cluster_flags & CLUSTER_FLAG_FED) {
		view = ERROR_VIEW;
		if (display_widget)
			gtk_widget_destroy(display_widget);
		label = gtk_label_new("Not available in a federated view");
		gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 1, 0, 1);
		gtk_widget_show(label);
		display_widget = gtk_widget_ref(label);
		goto end_it;
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
		sview_front_end_info_t *fe_ptr;
		itr = list_iterator_create(info_list);
		while ((fe_ptr = list_next(itr))) {
			j = 0;
			while (fe_ptr->node_inx[j] >= 0) {
				change_grid_color(grid_button_list,
						  fe_ptr->node_inx[j],
						  fe_ptr->node_inx[j+1],
						  fe_ptr->color_inx,
						  true, 0);
				j += 2;
			}
		}
		list_iterator_destroy(itr);
		change_grid_color(grid_button_list, -1, -1,
				  MAKE_WHITE, true, 0);
	} else {
		highlight_grid(GTK_TREE_VIEW(display_widget),
			       SORTID_NODE_INX, SORTID_COLOR_INX,
			       grid_button_list);
		gtk_tree_path_free(path);
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
		create_treestore(tree_view, display_data_front_end,
				 SORTID_CNT, SORTID_NAME, SORTID_COLOR);
	}

	view = INFO_VIEW;
	_update_info_front_end(info_list, GTK_TREE_VIEW(display_widget));
end_it:
	toggled = false;
	force_refresh = false;
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
		setup_popup_info(popup_win, display_data_front_end, SORTID_CNT);
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
		case NODE_PAGE:
			break;
		case JOB_PAGE:
			if (xstrcmp(front_end_ptr->name,
				    search_info->gchar_data))
				continue;
			break;
		case RESV_PAGE:
			switch (search_info->search_type) {
			case SEARCH_RESERVATION_NAME:
				if (!search_info->gchar_data)
					continue;

				if (xstrcmp(front_end_ptr->name,
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

	_update_info_front_end(send_resv_list,
			  GTK_TREE_VIEW(spec_info->display_widget));
	FREE_NULL_LIST(send_resv_list);
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
		make_fields_menu(NULL, menu, display_data_front_end,
				 SORTID_CNT);
		break;
	case ROW_CLICKED:
		make_options_menu(tree_view, path, menu,
				  options_data_front_end);
		break;
	case ROW_LEFT_CLICKED:
		highlight_grid(tree_view, SORTID_NODE_INX,
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
	char title[100] = {0};
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
			if (!xstrcmp(popup_win->spec_info->title, title)) {
				break;
			}
	}
	list_iterator_destroy(itr);

	if (!popup_win) {
		if (id == INFO_PAGE)
			popup_win = create_popup_info(id, FRONT_END_PAGE,
						      title);
		else {
			popup_win = create_popup_info(FRONT_END_PAGE, id,
						      title);
		}
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
	popup_win->node_inx_id = SORTID_NODE_INX;

	switch (id) {
	case INFO_PAGE:
		popup_win->spec_info->search_info->gchar_data = name;
		break;
	default:
		g_print("resv got unknown type %d\n", id);
	}
	if (!sview_thread_new((gpointer)popup_thr, popup_win, false, &error)) {
		g_printerr ("Failed to create resv popup thread: %s\n",
			    error->message);
		return;
	}
}

static void _process_each_front_end(GtkTreeModel *model, GtkTreePath *path,
				    GtkTreeIter*iter, gpointer user_data)
{
	char *name = NULL;
	front_end_user_data_t *fe_data = user_data;

	gtk_tree_model_get(model, iter, SORTID_NAME, &name, -1);
	if (fe_data->node_list)
		xstrfmtcat(fe_data->node_list, ",%s", name);
	else
		fe_data->node_list = xstrdup(name);
	g_free(name);
}

extern void select_admin_front_end(GtkTreeModel *model, GtkTreeIter *iter,
				   display_data_t *display_data,
				   GtkTreeView *treeview)
{
	if (treeview) {
		char *node_list;
		hostlist_t hl = NULL;
		front_end_user_data_t user_data;

		memset(&user_data, 0, sizeof(front_end_user_data_t));
		gtk_tree_selection_selected_foreach(
			gtk_tree_view_get_selection(treeview),
			_process_each_front_end, &user_data);

		hl = hostlist_create(user_data.node_list);
		hostlist_uniq(hl);
		hostlist_sort(hl);
		xfree(user_data.node_list);
		node_list = hostlist_ranged_string_xmalloc(hl);
		hostlist_destroy(hl);

		_admin_front_end(model, iter, display_data->name, node_list);
		xfree(node_list);
	}
}

static void _admin_front_end(GtkTreeModel *model, GtkTreeIter *iter, char *type,
			     char *node_list)
{
	uint16_t state = NO_VAL16;
	update_front_end_msg_t front_end_update_msg;
	char *new_type = NULL, *reason = NULL;
	char tmp_char[100];
	char *lower;
	int rc;
	GtkWidget *label = NULL;
	GtkWidget *entry = NULL;
	GtkWidget *popup = NULL;

	if (cluster_flags & CLUSTER_FLAG_FED) {
		display_fed_disabled_popup(type);
		global_entry_changed = 0;
		return;
	}

	popup = gtk_dialog_new_with_buttons(
		type,
		GTK_WINDOW(main_window),
		GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
		NULL);

	gtk_window_set_type_hint(GTK_WINDOW(popup),
				 GDK_WINDOW_TYPE_HINT_NORMAL);

	gtk_window_set_transient_for(GTK_WINDOW(popup), NULL);

	label = gtk_dialog_add_button(GTK_DIALOG(popup),
				      GTK_STOCK_YES, GTK_RESPONSE_OK);
	gtk_window_set_default(GTK_WINDOW(popup), label);
	gtk_dialog_add_button(GTK_DIALOG(popup),
			      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);

	if (!xstrncasecmp("Drain", type, 5)) {
		new_type = "DRAIN";
		reason = "\n\nPlease enter reason.";
		state = NODE_STATE_DRAIN;
		entry = create_entry();
	} else if (!xstrncasecmp("Resume", type, 6)) {
		new_type = "RESUME";
		reason = "";
		state = NODE_RESUME;
	}
	snprintf(tmp_char, sizeof(tmp_char),
		 "Are you sure you want to set state of front end node %s "
		 "to %s?%s", node_list, new_type, reason);
	label = gtk_label_new(tmp_char);

	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(popup)->vbox),
			   label, false, false, 0);
	if (entry)
		gtk_box_pack_start(GTK_BOX(GTK_DIALOG(popup)->vbox),
				   entry, true, true, 0);
	gtk_widget_show_all(popup);
	rc = gtk_dialog_run (GTK_DIALOG(popup));

	slurm_init_update_front_end_msg(&front_end_update_msg);

	if (rc == GTK_RESPONSE_OK) {
		front_end_update_msg.name = node_list;
		front_end_update_msg.node_state = state;
		if (entry) {
			front_end_update_msg.reason = xstrdup(
				gtk_entry_get_text(GTK_ENTRY(entry)));
			if (!front_end_update_msg.reason ||
			    !strlen(front_end_update_msg.reason)) {
				lower = g_strdup_printf(
					"You need a reason to do that.");
				display_edit_note(lower);
				g_free(lower);
				goto end_it;
			}
			rc = uid_from_string(getlogin(),
					     &front_end_update_msg.reason_uid);
			if (rc < 0)
				front_end_update_msg.reason_uid = getuid();
		}

		rc = slurm_update_front_end(&front_end_update_msg);
		if (rc == SLURM_SUCCESS) {
			lower = g_strdup_printf(
				"Nodes %s updated successfully.",
				node_list);
			display_edit_note(lower);
			g_free(lower);
		} else {
			lower = g_strdup_printf(
				"Problem updating nodes %s: %s",
				node_list, slurm_strerror(rc));
			display_edit_note(lower);
			g_free(lower);
		}
	}

end_it:
	global_entry_changed = 0;
	xfree(front_end_update_msg.reason);
	gtk_widget_destroy(popup);
	if (got_edit_signal) {
		type = got_edit_signal;
		got_edit_signal = NULL;
		_admin_front_end(model, iter, type, node_list);
		xfree(type);
	}
	return;
}

extern void cluster_change_front_end(void)
{
	get_info_front_end(NULL, NULL);
}
