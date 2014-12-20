/*****************************************************************************\
 *  bb_info.c - Functions related to Burst Buffer display mode of sview.
 *****************************************************************************
 *  Copyright (C) 2014 SchedMD LLC.
 *  Written by Nathan Yee <nyee32@shedmd.com>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
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
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include "src/common/uid.h"
#include "src/sview/sview.h"
#include "src/common/parse_time.h"
#include "src/common/proc_args.h"

#define _DEBUG 0

/* Collection of data for printing reports. Like data is combined here */
typedef struct {
	int		color_inx;
	GtkTreeIter	iter_ptr;
	bool		iter_set;
	int		pos;
	char *		bb_name;
	burst_buffer_resv_t *bb_ptr;
} sview_bb_info_t;

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
	SORTID_SIZE,
	SORTID_STATE,
	SORTID_STATE_TIME,
	SORTID_UPDATED,
	SORTID_USERID,
	SORTID_CNT
};

/* extra field here is for choosing the type of edit you that will
 * take place.  If you choose EDIT_MODEL (means only display a set of
 * known options) create it in function create_model_*.
 */

/*these are the settings to apply for the user
 * on the first startup after a fresh slurm install.
 * s/b a const probably*/
static char *_initial_page_opts = "Name,Size,State,StateTime,UserID";

static display_data_t display_data_bb[] = {
	{G_TYPE_INT, SORTID_POS, NULL, FALSE, EDIT_NONE,
	 refresh_bb, create_model_bb, admin_edit_bb},
	{G_TYPE_STRING, SORTID_NAME, "Name", FALSE, EDIT_NONE,
	 refresh_bb, create_model_bb, admin_edit_bb},
	{G_TYPE_STRING, SORTID_COLOR, NULL, TRUE, EDIT_COLOR,
	 refresh_bb, create_model_bb, admin_edit_bb},
	{G_TYPE_INT, SORTID_COLOR_INX, NULL, FALSE, EDIT_NONE,
	 refresh_bb, create_model_bb, admin_edit_bb},
	{G_TYPE_STRING, SORTID_SIZE, "Size", FALSE, EDIT_NONE,
	 refresh_bb, create_model_bb, admin_edit_bb},
	{G_TYPE_STRING, SORTID_STATE, "State", FALSE, EDIT_NONE,
	 refresh_bb, create_model_bb, admin_edit_bb},
	{G_TYPE_STRING, SORTID_STATE_TIME, "StateTime", FALSE, EDIT_NONE,
	 refresh_bb, create_model_bb, admin_edit_bb},
	{G_TYPE_INT, SORTID_UPDATED, NULL, FALSE, EDIT_NONE, refresh_bb,
	 create_model_bb, admin_edit_bb},
	{G_TYPE_STRING, SORTID_USERID, "UserID", FALSE, EDIT_NONE,
	 refresh_bb, create_model_bb, admin_edit_bb},
	{G_TYPE_NONE, -1, NULL, FALSE, EDIT_NONE}
};

/*Burst buffer options list*/
static display_data_t options_data_bb[] = {
	{G_TYPE_INT, SORTID_POS, NULL, FALSE, EDIT_NONE},
	{G_TYPE_STRING, INFO_PAGE, "Full Info", TRUE, BB_PAGE},
	{G_TYPE_NONE, -1, NULL, FALSE, EDIT_NONE}
};

static display_data_t *local_display_data = NULL;
//Variable for Admin edit if needed
/* static char *got_edit_signal = NULL; */
static GtkTreeModel *last_model = NULL;

/* static void _admin_bb(GtkTreeModel *model, GtkTreeIter *iter, char *type); */
/* static void _process_each_bb(GtkTreeModel *model, GtkTreePath *path, */
/*			       GtkTreeIter*iter, gpointer userdata); */

/* static void _set_active_combo_bb(GtkComboBox *combo, */
/*				 GtkTreeModel *model, GtkTreeIter *iter, */
/*				 int type) */
/* { */
/*	char *temp_char = NULL; */
/*	int action = 0; */

/*	gtk_tree_model_get(model, iter, type, &temp_char, -1); */
/*	if (!temp_char) */
/*		goto end_it; */
/*	switch(type) { */
/*	case SORTID_ACTION: */
/*		if (!strcmp(temp_char, "none")) */
/*			action = 0; */
/*		else if (!strcmp(temp_char, "remove")) */
/*			action = 1; */
/*		else */
/*			action = 0; */

/*		break; */
/*	default: */
/*		break; */
/*	} */
/*	g_free(temp_char); */
/* end_it: */
/*	gtk_combo_box_set_active(combo, action); */

/* } */

// Function for admin edit
//don't free this char
/* static const char *_set_bb_msg(burst_buffer_info_msg_t *bb_msg, */
/*				 const char *new_text, */
/*				 int column) */
/* { */
/* NOP */
/* } */

static void _bb_info_free(sview_bb_info_t *sview_bb_info)
{
	if (sview_bb_info) {
		xfree(sview_bb_info->bb_name);
	}
}

static void _bb_info_list_del(void *object)
{
	sview_bb_info_t *sview_bb_info = (sview_bb_info_t *)object;

	if (sview_bb_info) {
		_bb_info_free(sview_bb_info);
		xfree(sview_bb_info);
	}
}

/* static void _admin_edit_combo_box_bb(GtkComboBox *combo, */
/*				     resv_desc_msg_t *resv_msg) */
/* { */

/* } */



/* static gboolean _admin_focus_out_bb(GtkEntry *entry, */
/*				      GdkEventFocus *event, */
/*				      resv_desc_msg_t *resv_msg) */
/* { */
/*	if (global_entry_changed) { */
/*		const char *col_name = NULL; */
/*		int type = gtk_entry_get_max_length(entry); */
/*		const char *name = gtk_entry_get_text(entry); */
/*		type -= DEFAULT_ENTRY_LENGTH; */
/*		col_name = _set_resv_msg(resv_msg, name, type); */
/*		if (global_edit_error) { */
/*			if (global_edit_error_msg) */
/*				g_free(global_edit_error_msg); */
/*			global_edit_error_msg = g_strdup_printf( */
/*				"Reservation %s %s can't be set to %s", */
/*				resv_msg->name, */
/*				col_name, */
/*				name); */
/*		} */
/*		global_entry_changed = 0; */
/*	} */
/*	return false; */
/* } */

/* static GtkWidget *_admin_full_edit_bb(resv_desc_msg_t *resv_msg, */
/*					GtkTreeModel *model, GtkTreeIter *iter) */
/* { */
/*	GtkScrolledWindow *window = create_scrolled_window(); */
/*	GtkBin *bin = NULL; */
/*	GtkViewport *view = NULL; */
/*	GtkTable *table = NULL; */
/*	int i = 0, row = 0; */
/*	display_data_t *display_data = display_data_resv; */

/*	gtk_scrolled_window_set_policy(window, */
/*				       GTK_POLICY_NEVER, */
/*				       GTK_POLICY_AUTOMATIC); */
/*	bin = GTK_BIN(&window->container); */
/*	view = GTK_VIEWPORT(bin->child); */
/*	bin = GTK_BIN(&view->bin); */
/*	table = GTK_TABLE(bin->child); */
/*	gtk_table_resize(table, SORTID_CNT, 2); */

/*	gtk_table_set_homogeneous(table, FALSE); */

/*	for(i = 0; i < SORTID_CNT; i++) { */
/*		while (display_data++) { */
/*			if (display_data->id == -1) */
/*				break; */
/*			if (!display_data->name) */
/*				continue; */
/*			if (display_data->id != i) */
/*				continue; */
/*			display_admin_edit( */
/*				table, resv_msg, &row, model, iter, */
/*				display_data, */
/*				G_CALLBACK(_admin_edit_combo_box_resv), */
/*				G_CALLBACK(_admin_focus_out_resv), */
/*				_set_active_combo_resv); */
/*			break; */
/*		} */
/*		display_data = display_data_resv; */
/*	} */
/*	gtk_table_resize(table, row, 2); */

/*	return GTK_WIDGET(window); */
/* } */

/* Function creates the record menu when you double click on a record */
static void _layout_bb_record(GtkTreeView *treeview,
			      sview_bb_info_t *sview_bb_info, int update)
{
	GtkTreeIter iter;
	char time_buf[20], tmp_user_id[20];
	char bb_name_id[20];
	char *tmp_state;
	//char tmp_user_id[20];
	burst_buffer_resv_t *bb_ptr =
		sview_bb_info->bb_ptr;

	GtkTreeStore *treestore =
		GTK_TREE_STORE(gtk_tree_view_get_model(treeview));

	if (bb_ptr->name) {
		strncpy(bb_name_id, bb_ptr->name, sizeof(bb_name_id));
	} else if (bb_ptr->array_task_id == NO_VAL) {
		convert_num_unit(bb_ptr->job_id, bb_name_id,
				 sizeof(bb_name_id),
				 UNIT_NONE);
	} else {
		snprintf(bb_name_id, sizeof(bb_name_id),
			 "%u.%u(%u)",
			 bb_ptr->array_job_id,
			 bb_ptr->array_task_id,
			 bb_ptr->job_id);
	}

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_bb,
						 SORTID_NAME),
				   bb_name_id);

	tmp_state = bb_state_string(bb_ptr->state);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_bb,
						 SORTID_STATE),
				   tmp_state);

	slurm_make_time_str((time_t *)&bb_ptr->state_time, time_buf,
			    sizeof(time_buf));
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_bb,
						 SORTID_STATE_TIME),
				   time_buf);

	convert_num_unit(bb_ptr->user_id, tmp_user_id, sizeof(tmp_user_id),
			 UNIT_NONE);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_bb,
						 SORTID_USERID),
				   tmp_user_id);
}

/* updates the burst buffer record on sview */
static void _update_bb_record(sview_bb_info_t *sview_bb_info_ptr,
			      GtkTreeStore *treestore)
{
	char tmp_state_time[40];
	char tmp_size[20], tmp_user_id[20], bb_name_id[20];
	char *tmp_state;
	burst_buffer_resv_t *bb_ptr = sview_bb_info_ptr->bb_ptr;

	if (bb_ptr->name) {
		strncpy(bb_name_id, bb_ptr->name, sizeof(bb_name_id));
	} else if (bb_ptr->array_task_id == NO_VAL) {
		convert_num_unit(bb_ptr->job_id, bb_name_id,
				 sizeof(bb_name_id),
				 UNIT_NONE);
	} else {
		snprintf(bb_name_id, sizeof(bb_name_id),
			 "%u.%u(%u)",
			 bb_ptr->array_job_id,
			 bb_ptr->array_task_id,
			 bb_ptr->job_id);
	}


	slurm_make_time_str((time_t *)&bb_ptr->state_time, tmp_state_time,
			    sizeof(tmp_state_time));


	convert_num_unit(bb_ptr->size,
			 tmp_size, sizeof(tmp_size), UNIT_NONE);

	tmp_state = bb_state_string(bb_ptr->state);

	convert_num_unit(bb_ptr->user_id,
			 tmp_user_id, sizeof(tmp_user_id), UNIT_NONE);

	/* Combining these records provides a slight performance improvement */
	gtk_tree_store_set(treestore, &sview_bb_info_ptr->iter_ptr,
			   SORTID_COLOR,
			   sview_colors[sview_bb_info_ptr->color_inx],
			   SORTID_COLOR_INX,  sview_bb_info_ptr->color_inx,
			   SORTID_NAME,   bb_name_id,
			   SORTID_SIZE,   tmp_size,
			   SORTID_STATE,  tmp_state,
			   SORTID_STATE_TIME,   tmp_state_time,
			   SORTID_UPDATED,    1,
			   SORTID_USERID,      tmp_user_id,
			   -1);
	return;
}

static void _append_bb_record(sview_bb_info_t *sview_bb_info_ptr,
				GtkTreeStore *treestore)
{
	gtk_tree_store_append(treestore, &sview_bb_info_ptr->iter_ptr, NULL);
	gtk_tree_store_set(treestore, &sview_bb_info_ptr->iter_ptr,
			   SORTID_POS, sview_bb_info_ptr->pos, -1);
	_update_bb_record(sview_bb_info_ptr, treestore);
}

static void _update_info_bb(List info_list, GtkTreeView *tree_view)
{
	GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
	char *name = NULL;
	ListIterator itr = NULL;
	sview_bb_info_t *sview_bb_info = NULL;

	set_for_update(model, SORTID_UPDATED);

	itr = list_iterator_create(info_list);
	while ((sview_bb_info = (sview_bb_info_t*) list_next(itr))) {
		/* This means the tree_store changed (added new column
		   or something). */
		if (last_model != model)
			sview_bb_info->iter_set = false;

		if (sview_bb_info->iter_set) {
			gtk_tree_model_get(model, &sview_bb_info->iter_ptr,
					   SORTID_NAME, &name, -1);
			if (strcmp(name, sview_bb_info->bb_name)) {
				/* Bad pointer */
				sview_bb_info->iter_set = false;
				//g_print("bad resv iter pointer\n");
			}
			g_free(name);
		}
		if (sview_bb_info->iter_set) {
			_update_bb_record(sview_bb_info,
					    GTK_TREE_STORE(model));
		} else {
			_append_bb_record(sview_bb_info,
					    GTK_TREE_STORE(model));
			sview_bb_info->iter_set = true;
		}
	}
	list_iterator_destroy(itr);

	/* remove all old bb */
	remove_old(model, SORTID_UPDATED);
	last_model = model;
}

// may not need
/* static int _sview_bb_sort_aval_dec(void *s1, void *s2) */
/* { */
/*	sview_resv_info_t *rec_a = *(sview_resv_info_t **)s1; */
/*	sview_resv_info_t *rec_b = *(sview_resv_info_t **)s2; */
/*	int size_a; */
/*	int size_b; */

/*	size_a = rec_a->resv_ptr->node_cnt; */
/*	size_b = rec_b->resv_ptr->node_cnt; */

/*	if (size_a < size_b) */
/*		return -1; */
/*	else if (size_a > size_b) */
/*		return 1; */

/*	if (rec_a->resv_ptr->node_list && rec_b->resv_ptr->node_list) { */
/*		size_a = strcmp(rec_a->resv_ptr->node_list, */
/*				rec_b->resv_ptr->node_list); */
/*		if (size_a < 0) */
/*			return -1; */
/*		else if (size_a > 0) */
/*			return 1; */
/*	} */
/*	return 0; */
/* } */

static List _create_bb_info_list(burst_buffer_info_msg_t *bb_info_ptr)
{
	static List info_list = NULL;
	List last_list = NULL;
	ListIterator last_list_itr = NULL;
	int i = 0;
	static burst_buffer_info_msg_t *last_bb_info_ptr = NULL;
	sview_bb_info_t *sview_bb_info_ptr = NULL;
	burst_buffer_info_t *bb_ptr = bb_info_ptr->burst_buffer_array;
	burst_buffer_resv_t *bb_resv_ptr = NULL;

	if (info_list && (bb_info_ptr == last_bb_info_ptr))
		goto update_color;

	last_bb_info_ptr = bb_info_ptr;

	if (info_list)
		last_list = info_list;

	info_list = list_create(_bb_info_list_del);
	if (!info_list) {
		g_print("malloc error\n");
		return NULL;
	}

	if (last_list)
		last_list_itr = list_iterator_create(last_list);
	for (i = 0; i < bb_ptr->record_count; i++) {
		bb_resv_ptr = &(bb_ptr->burst_buffer_resv_ptr[i]);

		sview_bb_info_ptr = NULL;
		if (last_list_itr) {
			while ((sview_bb_info_ptr =
				list_next(last_list_itr))) {
				if (bb_resv_ptr->name &&
				    !strcmp(sview_bb_info_ptr->bb_name,
					    bb_resv_ptr->name)) {
					list_remove(last_list_itr);
					_bb_info_free(sview_bb_info_ptr);
					break;
				} else if (bb_resv_ptr->name) {
					continue;
				} else if (bb_resv_ptr->job_id ==
					   sview_bb_info_ptr->bb_ptr->job_id) {
					list_remove(last_list_itr);
					_bb_info_free(sview_bb_info_ptr);
					break;
				}
			}
			list_iterator_reset(last_list_itr);
		}
		if (!sview_bb_info_ptr)
			sview_bb_info_ptr = xmalloc(sizeof(sview_bb_info_t));
		sview_bb_info_ptr->bb_name = xstrdup(bb_resv_ptr->name);
		sview_bb_info_ptr->pos = i;
		sview_bb_info_ptr->bb_ptr = bb_resv_ptr;
		sview_bb_info_ptr->color_inx = i % sview_colors_cnt;
		list_append(info_list, sview_bb_info_ptr);
	}

	// sorts list by node, since there are no nodes we done use it

	/* list_sort(info_list, */
	/*	  (ListCmpF)_sview_resv_sort_aval_dec); */

	if (last_list) {
		list_iterator_destroy(last_list_itr);
		list_destroy(last_list);
	}

update_color:

	return info_list;
}

static void _display_info_bb(List info_list, popup_info_t *popup_win)
{
	specific_info_t *spec_info = popup_win->spec_info;
	char *name = (char *)spec_info->search_info->gchar_data;
	//int found = 0;
	burst_buffer_resv_t *bb_ptr = NULL;
	GtkTreeView *treeview = NULL;
	ListIterator itr = NULL;
	sview_bb_info_t *sview_bb_info = NULL;
	int update = 0;

	if (!spec_info->search_info->gchar_data) {
		//info = xstrdup("No pointer given!");
		goto finished;
	}

	if (!spec_info->display_widget) {
		treeview = create_treeview_2cols_attach_to_table(
			popup_win->table);
		spec_info->display_widget =
			gtk_widget_ref(GTK_WIDGET(treeview));
	} else {
		treeview = GTK_TREE_VIEW(spec_info->display_widget);
		update = 1;
	}

	// node grid ???

	itr = list_iterator_create(info_list);
	while ((sview_bb_info = (sview_bb_info_t*) list_next(itr))) {
		bb_ptr = sview_bb_info->bb_ptr;
		if (!strcmp(bb_ptr->name, name)) {
			/* j=0; */
			/* while (resv_ptr->node_inx[j] >= 0) { */
			/*	change_grid_color( */
			/*		popup_win->grid_button_list, */
			/*		resv_ptr->node_inx[j], */
			/*		resv_ptr->node_inx[j+1], */
			/*		sview_resv_info->color_inx, */
			/*		true, 0); */
			/*	j += 2; */
			/* } */
			_layout_bb_record(treeview, sview_bb_info, update);
			//found = 1;
			break;
		}
	}
	list_iterator_destroy(itr);

	//post_setup_popup_grid_list(popup_win);

	/* if (!found) { */

	/*	printf ("found is false\n"); */

	/*	if (!popup_win->not_found) { */
	/*		char *temp = "RESERVATION DOESN'T EXSIST\n"; */
	/*		GtkTreeIter iter; */
	/*		GtkTreeModel *model = NULL; */

	/*		/\* only time this will be run so no update *\/ */
	/*		model = gtk_tree_view_get_model(treeview); */
	/*		add_display_treestore_line(0, */
	/*					   GTK_TREE_STORE(model), */
	/*					   &iter, */
	/*					   temp, ""); */
	/*	} */
	/*	popup_win->not_found = true; */
	/* } else { */

	/*	printf ("found is true\n"); */

	/*	if (popup_win->not_found) { */
	/*		printf ("-----need fresh------\n"); */
	/*		popup_win->not_found = false; */
	/*		gtk_widget_destroy(spec_info->display_widget); */

	/*		goto need_refresh; */
	/*	} */
	/* } */
	gtk_widget_show(spec_info->display_widget);

finished:

	return;
}


// may not need this
/* extern GtkWidget *create_resv_entry(resv_desc_msg_t *resv_msg, */
/*				    GtkTreeModel *model, GtkTreeIter *iter) */
/* { */
/*	GtkScrolledWindow *window = create_scrolled_window(); */
/*	GtkBin *bin = NULL; */
/*	GtkViewport *view = NULL; */
/*	GtkTable *table = NULL; */
/*	int i = 0, row = 0; */
/*	display_data_t *display_data = create_data_resv; */

/*	gtk_scrolled_window_set_policy(window, */
/*				       GTK_POLICY_NEVER, */
/*				       GTK_POLICY_AUTOMATIC); */
/*	bin = GTK_BIN(&window->container); */
/*	view = GTK_VIEWPORT(bin->child); */
/*	bin = GTK_BIN(&view->bin); */
/*	table = GTK_TABLE(bin->child); */
/*	gtk_table_resize(table, SORTID_CNT, 2); */

/*	gtk_table_set_homogeneous(table, FALSE); */

/*	for (i = 0; i < SORTID_CNT; i++) { */
/*		while (display_data++) { */
/*			if (display_data->id == -1) */
/*				break; */
/*			if (!display_data->name) */
/*				continue; */
/*			if (display_data->id != i) */
/*				continue; */
/*			display_admin_edit( */
/*				table, resv_msg, &row, model, iter, */
/*				display_data, */
/*				G_CALLBACK(_admin_edit_combo_box_resv), */
/*				G_CALLBACK(_admin_focus_out_resv), */
/*				_set_active_combo_resv); */
/*			break; */
/*		} */
/*		display_data = create_data_resv; */
/*	} */
/*	gtk_table_resize(table, row, 2); */

/*	return GTK_WIDGET(window); */
/* } */

extern void refresh_bb(GtkAction *action, gpointer user_data)
{
	popup_info_t *popup_win = (popup_info_t *)user_data;
	xassert(popup_win);
	xassert(popup_win->spec_info);
	xassert(popup_win->spec_info->title);
	popup_win->force_refresh = 1;
	specific_info_bb(popup_win);
}

extern int get_new_info_bb(burst_buffer_info_msg_t **info_ptr,
			     int force)
{
	static burst_buffer_info_msg_t *new_bb_ptr = NULL;
	int error_code = SLURM_NO_CHANGE_IN_DATA;
	time_t now = time(NULL);
	static time_t last;
	static bool changed = 0;

	if (g_bb_info_ptr && !force
	    && ((now - last) < working_sview_config.refresh_delay)) {
		if (*info_ptr != g_bb_info_ptr)
			error_code = SLURM_SUCCESS;
		*info_ptr = g_bb_info_ptr;
		if (changed)
			error_code = SLURM_SUCCESS;
		goto end_it;
	}
	last = now;
	if (g_bb_info_ptr) {
		error_code = slurm_load_burst_buffer_info(&new_bb_ptr);
		if (error_code == SLURM_SUCCESS) {
			slurm_free_burst_buffer_info_msg(g_bb_info_ptr);
			changed = 1;
		} else if (slurm_get_errno() == SLURM_NO_CHANGE_IN_DATA) {
			error_code = SLURM_NO_CHANGE_IN_DATA;
			new_bb_ptr = g_bb_info_ptr;
			changed = 0;
		}
	} else {
		new_bb_ptr = NULL;
		error_code = slurm_load_burst_buffer_info(&new_bb_ptr);
		changed = 1;
	}

	g_bb_info_ptr = new_bb_ptr;

	if (g_bb_info_ptr && (*info_ptr != g_bb_info_ptr))
		error_code = SLURM_SUCCESS;

	*info_ptr = g_bb_info_ptr;
end_it:
	return error_code;
}

// not sure if we need this
extern GtkListStore *create_model_bb(int type)
{

	GtkListStore *model = NULL;
	//GtkTreeIter iter;

	last_model = NULL;	/* Reformat display */
	switch(type) {
	/* case SORTID_ACTION: */
	/*	model = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT); */
	/*	gtk_list_store_append(model, &iter); */
	/*	gtk_list_store_set(model, &iter, */
	/*			   1, SORTID_ACTION, */
	/*			   0, "None", */
	/*			   -1); */
	/*	gtk_list_store_append(model, &iter); */
	/*	gtk_list_store_set(model, &iter, */
	/*			   1, SORTID_ACTION, */
	/*			   0, "Remove Reservation", */
	/*			   -1); */
		break;
	default:
		break;
	}
	return model;
}

/* If Burst buffer wants to be edited it goes here */
extern void admin_edit_bb(GtkCellRendererText *cell,
			    const char *path_string,
			    const char *new_text,
			    gpointer data)
{
	/* NOP */
}

extern void get_info_bb(GtkTable *table, display_data_t *display_data)
{
	int error_code = SLURM_SUCCESS;
	List info_list = NULL;
	static int view = -1;
	static burst_buffer_info_msg_t *bb_info_ptr = NULL;
	char error_char[100];
	GtkWidget *label = NULL;
	GtkTreeView *tree_view = NULL;
	static GtkWidget *display_widget = NULL;
	GtkTreePath *path = NULL;
	static bool set_opts = FALSE;

	if (!set_opts) {
		set_page_opts(BB_PAGE, display_data_bb,
			      SORTID_CNT, _initial_page_opts);
	}
	set_opts = TRUE;

	/* reset */
	if (!table && !display_data) {
		if (display_widget)
			gtk_widget_destroy(display_widget);
		display_widget = NULL;
		bb_info_ptr = NULL;
		goto reset_curs;
	}

	if (display_data)
		local_display_data = display_data;
	if (!table) {
		display_data_bb->set_menu = local_display_data->set_menu;
		goto reset_curs;
	}
	if (display_widget && toggled) {
		gtk_widget_destroy(display_widget);
		display_widget = NULL;
		goto display_it;
	}

	error_code = get_new_info_bb(&bb_info_ptr, force_refresh);
	if (error_code == SLURM_NO_CHANGE_IN_DATA) {
	} else if (error_code != SLURM_SUCCESS) {
		if (view == ERROR_VIEW)
			goto end_it;
		if (display_widget)
			gtk_widget_destroy(display_widget);
		view = ERROR_VIEW;
		sprintf(error_char, "slurm_load_reservations: %s",
			slurm_strerror(slurm_get_errno()));
		label = gtk_label_new(error_char);
		gtk_table_attach_defaults(table, label, 0, 1, 0, 1);
		gtk_widget_show(label);
		display_widget = gtk_widget_ref(GTK_WIDGET(label));
		goto end_it;
	}

display_it:
	info_list = _create_bb_info_list(bb_info_ptr);
	if (!info_list)
		goto reset_curs;
	/* set up the grid */
	if (display_widget && GTK_IS_TREE_VIEW(display_widget)
	    && gtk_tree_selection_count_selected_rows(
		    gtk_tree_view_get_selection(
			    GTK_TREE_VIEW(display_widget)))) {
		GtkTreeViewColumn *focus_column = NULL;
		/* highlight the correct nodes from the last selection */
		gtk_tree_view_get_cursor(GTK_TREE_VIEW(display_widget),
					 &path, &focus_column);
	}

	change_grid_color(grid_button_list, -1, -1,
			  MAKE_WHITE, true, 0);

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
		create_treestore(tree_view, display_data_bb,
				 SORTID_CNT, SORTID_NAME, SORTID_COLOR);
	}

	view = INFO_VIEW;
	_update_info_bb(info_list, GTK_TREE_VIEW(display_widget));
end_it:
	toggled = FALSE;
	force_refresh = FALSE;
reset_curs:
	if (main_window && main_window->window)
		gdk_window_set_cursor(main_window->window, NULL);

	printf ("done with get info bb\n");

	return;
}

extern void specific_info_bb(popup_info_t *popup_win)
{
	int bb_error_code = SLURM_SUCCESS;
	static burst_buffer_info_msg_t *bb_info_ptr = NULL;
//	static burst_buffer_resv_t *bb_ptr = NULL;
	specific_info_t *spec_info = popup_win->spec_info;
//	sview_search_info_t *search_info = spec_info->search_info;
	char error_char[100];
	GtkWidget *label = NULL;
	GtkTreeView *tree_view = NULL;
	List bb_list = NULL;
	List send_bb_list = NULL;
	sview_bb_info_t *sview_bb_info_ptr = NULL;
	int i=-1;
//	hostset_t hostset = NULL;
	ListIterator itr = NULL;

	if (!spec_info->display_widget) {
		setup_popup_info(popup_win, display_data_bb, SORTID_CNT);
	}

	if (spec_info->display_widget && popup_win->toggled) {
		gtk_widget_destroy(spec_info->display_widget);
		spec_info->display_widget = NULL;
		goto display_it;
	}

	if ((bb_error_code =
	     get_new_info_bb(&bb_info_ptr, popup_win->force_refresh))
	    == SLURM_NO_CHANGE_IN_DATA) {
		if (!spec_info->display_widget || spec_info->view == ERROR_VIEW)
			goto display_it;
	} else if (bb_error_code != SLURM_SUCCESS) {
		if (spec_info->view == ERROR_VIEW)
			goto end_it;
		spec_info->view = ERROR_VIEW;
		if (spec_info->display_widget)
			gtk_widget_destroy(spec_info->display_widget);
		sprintf(error_char, "get_new_info_bb: %s",
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

	bb_list = _create_bb_info_list(bb_info_ptr);

	if (!bb_list)
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
		 * to the treestore we don't really care about
		 * the return value */
		create_treestore(tree_view, popup_win->display_data,
				 SORTID_CNT, SORTID_NAME, SORTID_COLOR);
	}

	setup_popup_grid_list(popup_win);

	spec_info->view = INFO_VIEW;
	if (spec_info->type == INFO_PAGE) {
		_display_info_bb(bb_list, popup_win);
		goto end_it;
	}

	/* just linking to another list, don't free the inside, just the list */
	send_bb_list = list_create(NULL);
	itr = list_iterator_create(bb_list);
	i = -1;
	while ((sview_bb_info_ptr = list_next(itr))) {
		i++;
//		bb_ptr = sview_bb_info_ptr->bb_ptr;
		switch(spec_info->type) {
		case PART_PAGE:
		case BLOCK_PAGE:
		case NODE_PAGE:
			printf ("node page in specific info\n");

			/* if (!bb_ptr->node_list) */
			/*	continue; */

			/* if (!(hostset = hostset_create( */
			/*	      search_info->gchar_data))) */
			/*	continue; */
			/* if (!hostset_intersects(hostset, resv_ptr->node_list)) { */
			/*	hostset_destroy(hostset); */
			/*	continue; */
			/* } */
			/* hostset_destroy(hostset); */
			break;
		case JOB_PAGE:
			printf ("job page in specific info\n");

			/* if (strcmp(resv_ptr->name, */
			/*	   search_info->gchar_data)) */
			/*	continue; */
			break;
		case RESV_PAGE:

			printf ("resv page in specific info\n");

			/* switch(search_info->search_type) { */
			/* case SEARCH_RESERVATION_NAME: */
			/*	if (!search_info->gchar_data) */
			/*		continue; */

			/*	if (strcmp(resv_ptr->name, */
			/*		   search_info->gchar_data)) */
			/*		continue; */
			/*	break; */
			/* default: */
			/*	continue; */
			/* } */
			break;
		default:
			g_print("Unknown type %d\n", spec_info->type);
			continue;
		}
		list_push(send_bb_list, sview_bb_info_ptr);
		/* j=0; */
		/* while (resv_ptr->node_inx[j] >= 0) { */
		/*	change_grid_color( */
		/*		popup_win->grid_button_list, */
		/*		resv_ptr->node_inx[j], */
		/*		resv_ptr->node_inx[j+1], */
		/*		sview_resv_info_ptr->color_inx, */
		/*		true, 0); */
		/*	j += 2; */
		/* } */
	}
	list_iterator_destroy(itr);
	post_setup_popup_grid_list(popup_win);

	_update_info_bb(send_bb_list,
			  GTK_TREE_VIEW(spec_info->display_widget));
	list_destroy(send_bb_list);
end_it:
	popup_win->toggled = 0;
	popup_win->force_refresh = 0;

	return;
}

/* creates a popup windo depending on what is clicked */
extern void set_menus_bb(void *arg, void *arg2, GtkTreePath *path, int type)
{
	GtkTreeView *tree_view = (GtkTreeView *)arg;
	popup_info_t *popup_win = (popup_info_t *)arg;
	GtkMenu *menu = (GtkMenu *)arg2;
//	List button_list = (List)arg2;

	switch(type) {
	case TAB_CLICKED:
		make_fields_menu(NULL, menu, display_data_bb, SORTID_CNT);
		break;
	case ROW_CLICKED:
		printf ("-----YOU RIGHT CLICKED-----\n");


		make_options_menu(tree_view, path, menu, options_data_bb);
		break;
	case ROW_LEFT_CLICKED:
		/* Highlights the node in th node grid */
		/* since we are not using this we will keep it empty */
		/* NOP */
		break;
	case FULL_CLICKED:
	{
		printf ("----FULL CLICKED----\n");
		GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
		GtkTreeIter iter;
		if (!gtk_tree_model_get_iter(model, &iter, path)) {
			g_error("error getting iter from model\n");
			break;
		}

		popup_all_bb(model, &iter, INFO_PAGE);

		break;
	}
	case POPUP_CLICKED:
		printf ("---POPUP CLICK---\n");

		make_fields_menu(popup_win, menu,
				 popup_win->display_data, SORTID_CNT);
		break;
	default:
		g_error("UNKNOWN type %d given to set_fields\n", type);
	}
	printf ("\t\tDONE WITH SET MENUS BB\n");

}

extern void popup_all_bb(GtkTreeModel *model, GtkTreeIter *iter, int id)
{
	char *name = NULL;
	char title[100];
	ListIterator itr = NULL;
	popup_info_t *popup_win = NULL;
	GError *error = NULL;

	gtk_tree_model_get(model, iter, SORTID_NAME, &name, -1);

	switch(id) {
	case INFO_PAGE:
		snprintf(title, 100, "Full info for Burst Buffer %s", name);
		break;
	default:
		g_print("Burst Buffer got %d\n", id);
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
		printf ("in popup all bb\n");
		if (id == INFO_PAGE) {
			printf ("\t\tinfo page passed\n");
			popup_win = create_popup_info(id, BB_PAGE, title);
		} else {
			printf ("\t\tinfo page FAILED\n");
			popup_win = create_popup_info(BB_PAGE, id, title);
		}
	} else {
		g_free(name);
		gtk_window_present(GTK_WINDOW(popup_win->popup));
		return;
	}

	/* Pass the model and the structs from the iter so we can always get
	   the current node_inx.
	*/
	popup_win->model = model;
	popup_win->iter = *iter;
//	popup_win->node_inx_id = SORTID_NODE_INX;

	switch(id) {
	case JOB_PAGE:
	case INFO_PAGE:
		printf ("in popup all bb switch\n");

		popup_win->spec_info->search_info->gchar_data = name;
		specific_info_bb(popup_win);	
		printf ("done with switch\n");

		break;
	case BLOCK_PAGE:
	case NODE_PAGE:
	case PART_PAGE:
		g_free(name);
		//gtk_tree_model_get(model, iter, SORTID_NODELIST, &name, -1);
		popup_win->spec_info->search_info->gchar_data = name;
		popup_win->spec_info->search_info->search_type =
			SEARCH_NODE_NAME;
		//specific_info_node(popup_win);
		break;
	case SUBMIT_PAGE:
		break;
	default:
		g_print("Burst Buffer got unknown type %d\n", id);
	}
	if (!sview_thread_new((gpointer)popup_thr, popup_win, FALSE, &error)) {
		g_printerr ("Failed to create burst buffer popup thread: %s\n",
			    error->message);
		return;
	}
}

/* static void _process_each_bb(GtkTreeModel *model, GtkTreePath *path, */
/*			       GtkTreeIter*iter, gpointer userdata) */
/* { */
/*	//char *type = userdata; */
/*	if (_DEBUG) */
/*		g_print("process_each_resv: global_multi_error = %d\n", */
/*			global_multi_error); */

/*	if (!global_multi_error) { */
/* //		_admin_resv(model, iter, type); */
/*	} */
/* } */

extern void select_admin_bb(GtkTreeModel *model, GtkTreeIter *iter,
			      display_data_t *display_data,
			      GtkTreeView *treeview)
{
	/* if (treeview) { */
	/*	if (display_data->extra & EXTRA_NODES) { */
	/*		/\* select_admin_nodes(model, iter, display_data, *\/ */
	/*		/\*		   SORTID_NODELIST, treeview); *\/ */
	/*		return; */
	/*	} */
	/*	global_multi_error = FALSE; */
	/*	gtk_tree_selection_selected_foreach( */
	/*		gtk_tree_view_get_selection(treeview), */
	/*		_process_each_bb, display_data->name); */
	/* } */
}

/* static void _admin_bb(GtkTreeModel *model, GtkTreeIter *iter, char *type) */
/* { */
/*	resv_desc_msg_t *resv_msg = xmalloc(sizeof(resv_desc_msg_t)); */
/*	reservation_name_msg_t resv_name_msg; */
/*	char *resvid = NULL; */
/*	char tmp_char[100]; */
/*	char *temp = NULL; */
/*	int edit_type = 0; */
/*	int response = 0; */
/*	GtkWidget *label = NULL; */
/*	GtkWidget *entry = NULL; */
/*	GtkWidget *popup = gtk_dialog_new_with_buttons( */
/*		type, */
/*		GTK_WINDOW(main_window), */
/*		GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, */
/*		NULL); */
/*	gtk_window_set_transient_for(GTK_WINDOW(popup), NULL); */

/*	gtk_tree_model_get(model, iter, SORTID_NAME, &resvid, -1); */

/*	slurm_init_resv_desc_msg(resv_msg); */
/*	memset(&resv_name_msg, 0, sizeof(reservation_name_msg_t)); */

/*	resv_msg->name = xstrdup(resvid); */

/*	if (!strcasecmp("Remove Reservation", type)) { */
/*		resv_name_msg.name = resvid; */

/*		label = gtk_dialog_add_button(GTK_DIALOG(popup), */
/*					      GTK_STOCK_YES, GTK_RESPONSE_OK); */
/*		gtk_window_set_default(GTK_WINDOW(popup), label); */
/*		gtk_dialog_add_button(GTK_DIALOG(popup), */
/*				      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL); */

/*		snprintf(tmp_char, sizeof(tmp_char), */
/*			 "Are you sure you want to remove " */
/*			 "reservation %s?", */
/*			 resvid); */
/*		label = gtk_label_new(tmp_char); */
/*		edit_type = EDIT_REMOVE; */
/*	} else { */
/*		label = gtk_dialog_add_button(GTK_DIALOG(popup), */
/*					      GTK_STOCK_OK, GTK_RESPONSE_OK); */
/*		gtk_window_set_default(GTK_WINDOW(popup), label); */
/*		gtk_dialog_add_button(GTK_DIALOG(popup), */
/*				      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL); */

/*		gtk_window_set_default_size(GTK_WINDOW(popup), 200, 400); */
/*		snprintf(tmp_char, sizeof(tmp_char), */
/*			 "Editing reservation %s think before you type", */
/*			 resvid); */
/*		label = gtk_label_new(tmp_char); */
/*		edit_type = EDIT_EDIT; */
/* //		entry = _admin_full_edit_resv(resv_msg, model, iter); */
/*	} */

/*	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(popup)->vbox), */
/*			   label, FALSE, FALSE, 0); */
/*	if (entry) */
/*		gtk_box_pack_start(GTK_BOX(GTK_DIALOG(popup)->vbox), */
/*				   entry, TRUE, TRUE, 0); */
/*	gtk_widget_show_all(popup); */
/*	response = gtk_dialog_run (GTK_DIALOG(popup)); */

/*	if (response == GTK_RESPONSE_OK) { */
/*		switch(edit_type) { */
/*		case EDIT_REMOVE: */
/*			if (slurm_delete_reservation(&resv_name_msg) */
/*			    == SLURM_SUCCESS) { */
/*				temp = g_strdup_printf( */
/*					"Reservation %s removed successfully", */
/*					resvid); */
/*			} else { */
/*				temp = g_strdup_printf( */
/*					"Problem removing reservation %s.", */
/*					resvid); */
/*			} */
/*			display_edit_note(temp); */
/*			g_free(temp); */
/*			break; */
/*		case EDIT_EDIT: */
/*			if (got_edit_signal) */
/*				goto end_it; */

/*			if (!global_send_update_msg) { */
/*				temp = g_strdup_printf("No change detected."); */
/*			} else if (slurm_update_reservation(resv_msg) */
/*				   == SLURM_SUCCESS) { */
/*				temp = g_strdup_printf( */
/*					"Reservation %s updated successfully", */
/*					resvid); */
/*			} else { */
/*				temp = g_strdup_printf( */
/*					"Problem updating reservation %s.", */
/*					resvid); */
/*			} */
/*			display_edit_note(temp); */
/*			g_free(temp); */
/*			break; */
/*		default: */
/*			break; */
/*		} */
/*	} */
/* end_it: */

/*	g_free(resvid); */
/*	global_entry_changed = 0; */
/*	slurm_free_resv_desc_msg(resv_msg); */
/*	gtk_widget_destroy(popup); */
/*	if (got_edit_signal) { */
/*		type = got_edit_signal; */
/*		got_edit_signal = NULL; */
/* //		_admin_resv(model, iter, type); */
/*		xfree(type); */
/*	} */
/*	return; */
/* } */

extern void cluster_change_bb(void)
{
	/* display_data_t *display_data = display_data_resv; */
	/* while (display_data++) { */
	/*	if (display_data->id == -1) */
	/*		break; */
	/*	if (cluster_flags & CLUSTER_FLAG_BG) { */
	/*		switch(display_data->id) { */
	/*		case SORTID_NODELIST: */
	/*			display_data->name = "MidplaneList"; */
	/*			break; */
	/*		default: */
	/*			break; */
	/*		} */
	/*	} else { */
	/*		switch(display_data->id) { */
	/*		case SORTID_NODELIST: */
	/*			display_data->name = "NodeList"; */
	/*			break; */
	/*		default: */
	/*			break; */
	/*		} */
	/*	} */
	/* } */
	/* display_data = options_data_resv; */
	/* while (display_data++) { */
	/*	if (display_data->id == -1) */
	/*		break; */

	/*	if (cluster_flags & CLUSTER_FLAG_BG) { */
	/*		switch(display_data->id) { */
	/*		case BLOCK_PAGE: */
	/*			display_data->name = "Blocks"; */
	/*			break; */
	/*		case NODE_PAGE: */
	/*			display_data->name = "Midplanes"; */
	/*			break; */
	/*		} */
	/*	} else { */
	/*		switch(display_data->id) { */
	/*		case BLOCK_PAGE: */
	/*			display_data->name = NULL; */
	/*			break; */
	/*		case NODE_PAGE: */
	/*			display_data->name = "Nodes"; */
	/*			break; */
	/*		} */
	/*	} */
	/* } */
	get_info_bb(NULL, NULL);
}
