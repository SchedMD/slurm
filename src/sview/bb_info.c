/*****************************************************************************\
 *  bb_info.c - Functions related to Burst Buffer display mode of sview.
 *****************************************************************************
 *  Copyright (C) 2014-2015 SchedMD LLC.
 *  Written by Nathan Yee <nyee32@shedmd.com>
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
#include "src/common/proc_args.h"
#include "src/common/strlcpy.h"

#define _DEBUG 0

/* Collection of data for printing reports. Like data is combined here */
typedef struct {
	char *		bb_name;
	burst_buffer_resv_t *bb_ptr;
	int		color_inx;
	GtkTreeIter	iter_ptr;
	bool		iter_set;
	char *		plugin;
	int		pos;
} sview_bb_info_t;

enum {
	EDIT_REMOVE = 1,
	EDIT_EDIT
};

/* These need to be in alpha order (except POS and CNT) */
enum {
	SORTID_POS = POS_LOC,
	SORTID_ACCOUNT,
	SORTID_COLOR,
	SORTID_COLOR_INX,
	SORTID_CREATE_TIME,
	SORTID_NAME,
	SORTID_PARTITION,
	SORTID_PLUGIN,
	SORTID_POOL,
	SORTID_QOS,
	SORTID_SIZE,
	SORTID_STATE,
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
static char *_initial_page_opts = "Name/JobID,Pool,Size,State,StateTime,UserID";

static display_data_t display_data_bb[] = {
	{G_TYPE_INT, SORTID_POS, NULL, false, EDIT_NONE,
	 refresh_bb, create_model_bb, admin_edit_bb},
	{G_TYPE_STRING, SORTID_PLUGIN, "Plugin", false, EDIT_NONE,
	 refresh_bb, create_model_bb, admin_edit_bb},
	{G_TYPE_STRING, SORTID_NAME, "Name/JobID", false, EDIT_NONE,
	 refresh_bb, create_model_bb, admin_edit_bb},
	{G_TYPE_STRING, SORTID_COLOR, NULL, true, EDIT_COLOR,
	 refresh_bb, create_model_bb, admin_edit_bb},
	{G_TYPE_INT, SORTID_COLOR_INX, NULL, false, EDIT_NONE,
	 refresh_bb, create_model_bb, admin_edit_bb},
	{G_TYPE_STRING, SORTID_ACCOUNT, "Account", false, EDIT_NONE,
	 refresh_bb, create_model_bb, admin_edit_bb},
	{G_TYPE_STRING, SORTID_CREATE_TIME, "CreateTime", false, EDIT_NONE,
	 refresh_bb, create_model_bb, admin_edit_bb},
	{G_TYPE_STRING, SORTID_PARTITION, "Partition", false, EDIT_NONE,
	 refresh_bb, create_model_bb, admin_edit_bb},
	{G_TYPE_STRING, SORTID_POOL, "Pool", false, EDIT_NONE,
	 refresh_bb, create_model_bb, admin_edit_bb},
	{G_TYPE_STRING, SORTID_QOS, "QOS", false, EDIT_NONE,
	 refresh_bb, create_model_bb, admin_edit_bb},
	{G_TYPE_STRING, SORTID_SIZE, "Size", false, EDIT_NONE,
	 refresh_bb, create_model_bb, admin_edit_bb},
	{G_TYPE_STRING, SORTID_STATE, "State", false, EDIT_NONE,
	 refresh_bb, create_model_bb, admin_edit_bb},
	{G_TYPE_INT, SORTID_UPDATED, NULL, false, EDIT_NONE, refresh_bb,
	 create_model_bb, admin_edit_bb},
	{G_TYPE_STRING, SORTID_USERID, "UserID", false, EDIT_NONE,
	 refresh_bb, create_model_bb, admin_edit_bb},
	{G_TYPE_NONE, -1, NULL, false, EDIT_NONE}
};

/*Burst buffer options list*/
static display_data_t options_data_bb[] = {
	{G_TYPE_INT, SORTID_POS, NULL, false, EDIT_NONE},
	{G_TYPE_STRING, INFO_PAGE, "Full Info", true, BB_PAGE},
	{G_TYPE_NONE, -1, NULL, false, EDIT_NONE}
};

static display_data_t *local_display_data = NULL;
//Variable for Admin edit if needed
/* static char *got_edit_signal = NULL; */
static GtkTreeModel *last_model = NULL;

static void _get_size_str(char *buf, size_t buf_size, uint64_t num);

/*Functions for admin edit*/
/* static void _admin_bb(GtkTreeModel *model, GtkTreeIter *iter, char *type); */
/* static void _process_each_bb(GtkTreeModel *model, GtkTreePath *path, */
/*			       GtkTreeIter*iter, gpointer userdata); */

/* static void _set_active_combo_bb(GtkComboBox *combo, */
/*				 GtkTreeModel *model, GtkTreeIter *iter, */
/*				 int type) */
/* { */
/* NOP */
/* Function for admin edit */
/* } */

// Function for admin edit
//don't free this char
/* static const char *_set_bb_msg(burst_buffer_info_msg_t *bb_msg, */
/*				 const char *new_text, */
/*				 int column) */
/* { */
/* NOP */
/* Function for admin edit */
/* } */

/* Free the burst buffer information */
static void _bb_info_free(sview_bb_info_t *sview_bb_info)
{
	if (sview_bb_info) {
		xfree(sview_bb_info->bb_name);
		xfree(sview_bb_info->plugin);
	}
}

/* Free the Burst Buffer information list */
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
/* NOP */
/* Function for admin edit */
/* } */



/* static gboolean _admin_focus_out_bb(GtkEntry *entry, */
/*				      GdkEventFocus *event, */
/*				      resv_desc_msg_t *resv_msg) */
/* { */
/* NOP */
/* Function for admin edit */
/* } */

/* static GtkWidget *_admin_full_edit_bb(resv_desc_msg_t *resv_msg, */
/*					GtkTreeModel *model, GtkTreeIter *iter) */
/* { */
/* NOP */
/* Function for admin edit */
/* } */

/* Function creates the record menu when you double click on a record */
static void _layout_bb_record(GtkTreeView *treeview,
			      sview_bb_info_t *sview_bb_info, int update)
{
	GtkTreeIter iter;
	char time_buf[20], tmp_user_id[60], tmp_size[20];
	char bb_name_id[32];
	char *tmp_state, *tmp_user_name;
	burst_buffer_resv_t *bb_ptr = sview_bb_info->bb_ptr;
	GtkTreeStore *treestore;

	treestore = GTK_TREE_STORE(gtk_tree_view_get_model(treeview));

	if (bb_ptr->name) {
		strlcpy(bb_name_id, bb_ptr->name, sizeof(bb_name_id));
	} else if (bb_ptr->array_task_id == NO_VAL) {
		convert_num_unit(bb_ptr->job_id, bb_name_id, sizeof(bb_name_id),
				 UNIT_NONE, NO_VAL,
				 working_sview_config.convert_flags);
	} else {
		snprintf(bb_name_id, sizeof(bb_name_id),
			 "%u_%u(%u)",
			 bb_ptr->array_job_id,
			 bb_ptr->array_task_id,
			 bb_ptr->job_id);
	}
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_bb,
						 SORTID_NAME),
				   bb_name_id);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_bb,
						 SORTID_PLUGIN),
				   sview_bb_info->plugin);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_bb,
						 SORTID_ACCOUNT),
				   bb_ptr->account);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_bb,
						 SORTID_PARTITION),
				   bb_ptr->partition);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_bb,
						 SORTID_POOL),
				   bb_ptr->pool);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_bb,
						 SORTID_QOS),
				   bb_ptr->qos);

	tmp_state = bb_state_string(bb_ptr->state);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_bb,
						 SORTID_STATE),
				   tmp_state);

	_get_size_str(tmp_size, sizeof(tmp_size), bb_ptr->size);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_bb,
						 SORTID_SIZE),
				   tmp_size);

	if (bb_ptr->create_time) {
		slurm_make_time_str((time_t *)&bb_ptr->create_time, time_buf,
				    sizeof(time_buf));
	} else {
		time_t now = time(NULL);
		slurm_make_time_str(&now, time_buf, sizeof(time_buf));
	}
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_bb,
						 SORTID_CREATE_TIME),
				   time_buf);

	tmp_user_name = uid_to_string(bb_ptr->user_id);
	snprintf(tmp_user_id, sizeof(tmp_user_id), "%s(%u)", tmp_user_name,
		 bb_ptr->user_id);
	xfree(tmp_user_name);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_bb,
						 SORTID_USERID),
				   tmp_user_id);
}

/* Reformat a numeric value with an appropriate suffix.
 * The input units are GB */
static void _get_size_str(char *buf, size_t buf_size, uint64_t num)
{
	uint64_t tmp64;

	if ((num == NO_VAL64) || (num == INFINITE64)) {
		snprintf(buf, buf_size, "INFINITE");
	} else if (num == 0) {
		snprintf(buf, buf_size, "0GB");
	} else if ((num % ((uint64_t) 1024 * 1024 * 1024 * 1024 * 1024)) == 0) {
		tmp64 = num / ((uint64_t) 1024 * 1024 * 1024 * 1024 * 1024);
		snprintf(buf, buf_size, "%"PRIu64"PB", tmp64);
	} else if ((num % ((uint64_t) 1024 * 1024 * 1024 * 1024)) == 0) {
		tmp64 = num / ((uint64_t) 1024 * 1024 * 1024 * 1024);
		snprintf(buf, buf_size, "%"PRIu64"TB", tmp64);
	} else if ((num % ((uint64_t) 1024 * 1024 * 1024)) == 0) {
		tmp64 = num / ((uint64_t) 1024 * 1024 * 1024);
		snprintf(buf, buf_size, "%"PRIu64"GB", tmp64);
	} else if ((num % ((uint64_t) 1024 * 1024)) == 0) {
		tmp64 = num / ((uint64_t) 1024 * 1024);
		snprintf(buf, buf_size, "%"PRIu64"MB", tmp64);
	} else if ((num % 1024) == 0) {
		tmp64 = num / 1024;
		snprintf(buf, buf_size, "%"PRIu64"KB", tmp64);
	} else {
		tmp64 = num;
		snprintf(buf, buf_size, "%"PRIu64"B", tmp64);
	}
}


/* updates the burst buffer record on sview */
static void _update_bb_record(sview_bb_info_t *sview_bb_info_ptr,
			      GtkTreeStore *treestore)
{
	char tmp_create_time[40];
	char tmp_size[20], tmp_user_id[60], bb_name_id[32];
	char *tmp_state, *tmp_user_name;
	burst_buffer_resv_t *bb_ptr = sview_bb_info_ptr->bb_ptr;

	if (bb_ptr->name) {
		strlcpy(bb_name_id, bb_ptr->name, sizeof(bb_name_id));
	} else if (bb_ptr->array_task_id == NO_VAL) {
		convert_num_unit(bb_ptr->job_id, bb_name_id, sizeof(bb_name_id),
				 UNIT_NONE, NO_VAL,
				 working_sview_config.convert_flags);
	} else {
		snprintf(bb_name_id, sizeof(bb_name_id),
			 "%u_%u(%u)",
			 bb_ptr->array_job_id,
			 bb_ptr->array_task_id,
			 bb_ptr->job_id);
	}

	if (bb_ptr->create_time) {
		slurm_make_time_str((time_t *)&bb_ptr->create_time,
				    tmp_create_time, sizeof(tmp_create_time));
	} else {
		time_t now = time(NULL);
		slurm_make_time_str(&now, tmp_create_time,
				    sizeof(tmp_create_time));
	}

	_get_size_str(tmp_size, sizeof(tmp_size), bb_ptr->size);

	tmp_state = bb_state_string(bb_ptr->state);

	tmp_user_name = uid_to_string(bb_ptr->user_id);
	snprintf(tmp_user_id, sizeof(tmp_user_id), "%s(%u)", tmp_user_name,
		 bb_ptr->user_id);
	xfree(tmp_user_name);

	/* Combining these records provides a slight performance improvement */
	gtk_tree_store_set(treestore, &sview_bb_info_ptr->iter_ptr,
			   SORTID_COLOR,
			   sview_colors[sview_bb_info_ptr->color_inx],
			   SORTID_COLOR_INX,     sview_bb_info_ptr->color_inx,
			   SORTID_PLUGIN,        sview_bb_info_ptr->plugin,
			   SORTID_ACCOUNT,       bb_ptr->account,
			   SORTID_CREATE_TIME,   tmp_create_time,
			   SORTID_NAME,          bb_name_id,
			   SORTID_PARTITION,     bb_ptr->partition,
			   SORTID_POOL,          bb_ptr->pool,
			   SORTID_QOS,           bb_ptr->qos,
			   SORTID_SIZE,          tmp_size,
			   SORTID_STATE,         tmp_state,
			   SORTID_UPDATED,       1,
			   SORTID_USERID,        tmp_user_id,
			   -1);

	return;
}

/* Append the give Burst Record to the list */
static void _append_bb_record(sview_bb_info_t *sview_bb_info_ptr,
				GtkTreeStore *treestore)
{
	gtk_tree_store_append(treestore, &sview_bb_info_ptr->iter_ptr, NULL);
	gtk_tree_store_set(treestore, &sview_bb_info_ptr->iter_ptr,
			   SORTID_POS, sview_bb_info_ptr->pos, -1);
	_update_bb_record(sview_bb_info_ptr, treestore);
}

/* Update the Burst Buffer inforamtion record */
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
		 * or something). */
		if (last_model != model)
			sview_bb_info->iter_set = false;

		if (sview_bb_info->iter_set) {
			gtk_tree_model_get(model, &sview_bb_info->iter_ptr,
					   SORTID_NAME, &name, -1);
			if (xstrcmp(name, sview_bb_info->bb_name)) {
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

static List _create_bb_info_list(burst_buffer_info_msg_t *bb_info_ptr)
{
	static List info_list = NULL;
	List last_list = NULL;
	ListIterator last_list_itr = NULL;
	int i, j, pos = 0;
	static burst_buffer_info_msg_t *last_bb_info_ptr = NULL;
	sview_bb_info_t *sview_bb_info_ptr = NULL;
	burst_buffer_info_t *bb_ptr;
	burst_buffer_resv_t *bb_resv_ptr = NULL;
	char bb_name_id[32] = "";

	if (info_list && (bb_info_ptr == last_bb_info_ptr))
		return info_list;

	last_bb_info_ptr = bb_info_ptr;
	if (info_list)
		last_list = info_list;
	info_list = list_create(_bb_info_list_del);

	for (i = 0, bb_ptr = bb_info_ptr->burst_buffer_array;
	     i < bb_info_ptr->record_count; i++, bb_ptr++) {

		for (j = 0, bb_resv_ptr = bb_ptr->burst_buffer_resv_ptr;
		     j < bb_ptr->buffer_count; j++, bb_resv_ptr++) {

			/* Find any existing record for this burst buffer */
			if (last_list) {
				last_list_itr = list_iterator_create(last_list);
				while ((sview_bb_info_ptr =
					list_next(last_list_itr))) {
					if (bb_resv_ptr->job_id &&
					    (bb_resv_ptr->job_id != 
					     sview_bb_info_ptr->bb_ptr->job_id))
						continue;
					if (bb_resv_ptr->name &&
					    xstrcmp(sview_bb_info_ptr->bb_name,
						    bb_resv_ptr->name))
						continue;
					if (xstrcmp(sview_bb_info_ptr->plugin,
						    bb_ptr->name))
						continue;
					list_remove(last_list_itr);
					_bb_info_free(sview_bb_info_ptr);
					break;
				}
				list_iterator_destroy(last_list_itr);
			} else {
				sview_bb_info_ptr = NULL;
			}

			if (bb_resv_ptr->name) {
				strlcpy(bb_name_id, bb_resv_ptr->name,
					sizeof(bb_name_id));
			} else if (bb_resv_ptr->array_task_id == NO_VAL) {
				convert_num_unit(bb_resv_ptr->job_id,
						 bb_name_id,
						 sizeof(bb_name_id),
						 UNIT_NONE, NO_VAL,
						 working_sview_config.
						 convert_flags);
			} else {
				snprintf(bb_name_id, sizeof(bb_name_id),
					 "%u_%u(%u)",
					 bb_resv_ptr->array_job_id,
					 bb_resv_ptr->array_task_id,
					 bb_resv_ptr->job_id);
			}

			if (!sview_bb_info_ptr) {	/* Need new record */
				sview_bb_info_ptr =
					xmalloc(sizeof(sview_bb_info_t));
			}
			sview_bb_info_ptr->bb_ptr = bb_resv_ptr;
			sview_bb_info_ptr->bb_name = xstrdup(bb_name_id);
			strcpy(bb_name_id, "");	/* Clear bb_name_id */
			sview_bb_info_ptr->color_inx = pos % sview_colors_cnt;
			sview_bb_info_ptr->plugin = xstrdup(bb_ptr->name);
			sview_bb_info_ptr->pos = pos++;
			list_append(info_list, sview_bb_info_ptr);
		}
	}

	FREE_NULL_LIST(last_list);
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
	char bb_name_id[32];

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

	itr = list_iterator_create(info_list);
	while ((sview_bb_info = (sview_bb_info_t*) list_next(itr))) {
		bb_ptr = sview_bb_info->bb_ptr;

		if (bb_ptr->name) {
			strlcpy(bb_name_id, bb_ptr->name, sizeof(bb_name_id));
		} else if (bb_ptr->array_task_id == NO_VAL) {
			convert_num_unit(bb_ptr->job_id,
					 bb_name_id,
					 sizeof(bb_name_id),
					 UNIT_NONE, NO_VAL,
					 working_sview_config.convert_flags);
		} else {
			snprintf(bb_name_id, sizeof(bb_name_id),
				 "%u_%u(%u)",
				 bb_ptr->array_job_id,
				 bb_ptr->array_task_id,
				 bb_ptr->job_id);
		}

		if (!xstrcmp(bb_name_id, name)) {
			_layout_bb_record(treeview, sview_bb_info, update);
			break;
		}
	}
	list_iterator_destroy(itr);
	gtk_widget_show(spec_info->display_widget);

finished:

	return;
}

/* extern GtkWidget *create_bb_entry(resv_desc_msg_t *resv_msg, */
/*				    GtkTreeModel *model, GtkTreeIter *iter) */
/* { */
/* NOP */
/* Function to add new burst buffer */
/* Admin edit function */
/* } */

/* Fresh the Burst Buffer information */
extern void refresh_bb(GtkAction *action, gpointer user_data)
{
	popup_info_t *popup_win = (popup_info_t *)user_data;
	xassert(popup_win);
	xassert(popup_win->spec_info);
	xassert(popup_win->spec_info->title);
	popup_win->force_refresh = 1;
	specific_info_bb(popup_win);
}

/* Get the Burst buffer information */
extern int get_new_info_bb(burst_buffer_info_msg_t **info_ptr, int force)
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

/* Create the model with types with known values */
extern GtkListStore *create_model_bb(int type)
{
/* Since none of the values can be editted this is left blank */
/* NOP */
	return NULL;
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
	static bool set_opts = false;

	if (!set_opts) {
		set_page_opts(BB_PAGE, display_data_bb,
			      SORTID_CNT, _initial_page_opts);
	}
	set_opts = true;

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

	if (!info_list) {
		goto reset_curs;
	}

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
	toggled = false;
	force_refresh = false;
reset_curs:
	if (main_window && main_window->window)
		gdk_window_set_cursor(main_window->window, NULL);

	return;
}

/* Function for full information about a Burst Buffer */
extern void specific_info_bb(popup_info_t *popup_win)
{
	int bb_error_code = SLURM_SUCCESS;
	static burst_buffer_info_msg_t *bb_info_ptr = NULL;
	specific_info_t *spec_info = popup_win->spec_info;
	char error_char[100];
	GtkWidget *label = NULL;
	GtkTreeView *tree_view = NULL;
	List bb_list = NULL;
	List send_bb_list = NULL;
	sview_bb_info_t *sview_bb_info_ptr = NULL;
	int i = -1;
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

	if ((spec_info->view == ERROR_VIEW) && spec_info->display_widget) {
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
		/*
		 * since this function sets the model of the tree_view
		 * to the treestore we don't really care about
		 * the return value
		 */
		create_treestore(tree_view, popup_win->display_data,
				 SORTID_CNT, SORTID_NAME, SORTID_COLOR);
	}

	setup_popup_grid_list(popup_win);

	spec_info->view = INFO_VIEW;
	if (spec_info->type == INFO_PAGE) {
		_display_info_bb(bb_list, popup_win);
		goto end_it;
	}

	/*
	 * just linking to another list, don't free the inside, just the list
	 */
	send_bb_list = list_create(NULL);
	itr = list_iterator_create(bb_list);
	i = -1;
	/*
	 * Set up additional menu options(ie the right click menu stuff)
	 */
	while ((sview_bb_info_ptr = list_next(itr))) {
		i++;
		switch (spec_info->type) {
		case BB_PAGE:
			list_push(send_bb_list, sview_bb_info_ptr);
			break;
		case JOB_PAGE:
		case NODE_PAGE:
		case PART_PAGE:
		case RESV_PAGE:
		default:
			/*
			 * Since we will not use any of these pages we will
			 * leave them blank
			 */
			g_print("Unknown type %d\n", spec_info->type);
			break;
		}
	}
	list_iterator_destroy(itr);
	post_setup_popup_grid_list(popup_win);

	_update_info_bb(send_bb_list,
			  GTK_TREE_VIEW(spec_info->display_widget));
	FREE_NULL_LIST(send_bb_list);
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

	switch (type) {
	case TAB_CLICKED:
		make_fields_menu(NULL, menu, display_data_bb, SORTID_CNT);
		break;
	case ROW_CLICKED:
		make_options_menu(tree_view, path, menu, options_data_bb);
		break;
	case ROW_LEFT_CLICKED:
		/* Highlights the node in th node grid */
		/* since we are not using this we will keep it empty */
		/* NOP */
		break;
	case FULL_CLICKED:
	{
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
		make_fields_menu(popup_win, menu,
				 popup_win->display_data, SORTID_CNT);
		break;
	default:
		g_error("UNKNOWN type %d given to set_fields\n", type);
	}
}

/* Function to setup popup windows for Burst Buffer */
extern void popup_all_bb(GtkTreeModel *model, GtkTreeIter *iter, int id)
{
	char *name = NULL;
	char title[100] = {0};
	ListIterator itr = NULL;
	popup_info_t *popup_win = NULL;
	GError *error = NULL;

	gtk_tree_model_get(model, iter, SORTID_NAME, &name, -1);

	switch (id) {
	case INFO_PAGE:
		snprintf(title, 100, "Full info for Burst Buffer %s", name);
		break;
	default:
		g_print("Burst Buffer got %d\n", id);
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
		if (id == INFO_PAGE) {
			popup_win = create_popup_info(id, BB_PAGE, title);
		} else {
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

	/* Sets up right click information */
	switch (id) {
	case JOB_PAGE:
	case INFO_PAGE:
		popup_win->spec_info->search_info->gchar_data = name;
		specific_info_bb(popup_win);
		break;
	case NODE_PAGE:
	case PART_PAGE:
	case SUBMIT_PAGE:
		break;
	default:
		g_print("Burst Buffer got unknown type %d\n", id);
	}
	if (!sview_thread_new((gpointer)popup_thr, popup_win, false, &error)) {
		g_printerr ("Failed to create burst buffer popup thread: %s\n",
			    error->message);
		return;
	}
}

/* static void _process_each_bb(GtkTreeModel *model, GtkTreePath *path, */
/*			       GtkTreeIter*iter, gpointer userdata) */
/* { */
/* Function for admin edit */
/* NOP */
/* } */

extern void select_admin_bb(GtkTreeModel *model, GtkTreeIter *iter,
			      display_data_t *display_data,
			      GtkTreeView *treeview)
{
/* NOP */
/* Function for admin edit */
}

/* static void _admin_bb(GtkTreeModel *model, GtkTreeIter *iter, char *type) */
/* { */
/* NOP */
/* Function for admin edit */
/* } */

extern void cluster_change_bb(void)
{
	get_info_bb(NULL, NULL);
}
