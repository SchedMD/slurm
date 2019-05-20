/*****************************************************************************\
 *  resv_info.c - Functions related to advanced reservation display
 *  mode of sview.
 *****************************************************************************
 *  Copyright (C) 2009-2011 Lawrence Livermore National Security.
 *  Portions Copyright (C) 2012-2015 SchedMD LLC <https://www.schedmd.com>
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
#include "src/common/proc_args.h"
#include "src/common/state_control.h"
#include "src/common/xstring.h"

#define _DEBUG 0

/* Collection of data for printing reports. Like data is combined here */
typedef struct {
	int color_inx;
	GtkTreeIter iter_ptr;
	bool iter_set;
	int pos;
	char *resv_name;
	reserve_info_t *resv_ptr;
} sview_resv_info_t;

enum {
	EDIT_REMOVE = 1,
	EDIT_EDIT
};

/* These need to be in alpha order (except POS and CNT) */
enum {
	SORTID_POS = POS_LOC,
	SORTID_ACCOUNTS,
	SORTID_ACTION,
	SORTID_BURST_BUFFER,
	SORTID_COLOR,
	SORTID_COLOR_INX,
	SORTID_CORE_CNT,
	SORTID_DURATION,
	SORTID_FEATURES,
	SORTID_FLAGS,
	SORTID_LICENSES,
	SORTID_NAME,
	SORTID_NODE_CNT,
	SORTID_NODELIST,
	SORTID_NODE_INX,
	SORTID_PARTITION,
	SORTID_TIME_END,
	SORTID_TIME_START,
	SORTID_TRES,
	SORTID_UPDATED,
	SORTID_USERS,
	SORTID_WATTS,
	SORTID_CNT
};

/* extra field here is for choosing the type of edit you that will
 * take place.  If you choose EDIT_MODEL (means only display a set of
 * known options) create it in function create_model_*.
 */

/* these are the settings to apply for the user
 * on the first startup after a fresh slurm install.
 * s/b a const probably */
static char *_initial_page_opts = "Name,Node_Count,Core_Count,NodeList,"
	"Time_Start,Time_End";

static display_data_t display_data_resv[] = {
	{G_TYPE_INT, SORTID_POS, NULL, false, EDIT_NONE,
	 refresh_resv, create_model_resv, admin_edit_resv},
	{G_TYPE_STRING, SORTID_NAME, "Name", false, EDIT_NONE,
	 refresh_resv, create_model_resv, admin_edit_resv},
	{G_TYPE_STRING, SORTID_COLOR, NULL, true, EDIT_COLOR,
	 refresh_resv, create_model_resv, admin_edit_resv},
	{G_TYPE_STRING, SORTID_ACTION, "Action", false, EDIT_MODEL,
	 refresh_resv, create_model_resv, admin_edit_resv},
	{G_TYPE_STRING, SORTID_NODE_CNT, "Node Count", false, EDIT_TEXTBOX,
	 refresh_resv, create_model_resv, admin_edit_resv},
	{G_TYPE_STRING, SORTID_CORE_CNT, "Core Count", false, EDIT_TEXTBOX,
	 refresh_resv, create_model_resv, admin_edit_resv},
	{G_TYPE_STRING, SORTID_NODELIST, "Node List", false, EDIT_TEXTBOX,
	 refresh_resv, create_model_resv, admin_edit_resv},
	{G_TYPE_STRING, SORTID_TIME_START, "Time Start", false, EDIT_TEXTBOX,
	 refresh_resv, create_model_resv, admin_edit_resv},
	{G_TYPE_STRING, SORTID_TIME_END, "Time End", false, EDIT_TEXTBOX,
	 refresh_resv, create_model_resv, admin_edit_resv},
	{G_TYPE_STRING, SORTID_DURATION, "Duration", false, EDIT_TEXTBOX,
	 refresh_resv, create_model_resv, admin_edit_resv},
	{G_TYPE_STRING, SORTID_ACCOUNTS, "Accounts", false, EDIT_TEXTBOX,
	 refresh_resv, create_model_resv, admin_edit_resv},
	{G_TYPE_STRING, SORTID_BURST_BUFFER, "BurstBuffer", false,
	 EDIT_TEXTBOX, refresh_resv, create_model_resv, admin_edit_resv},
	{G_TYPE_STRING, SORTID_LICENSES, "Licenses", true, EDIT_TEXTBOX,
	 refresh_resv, create_model_resv, admin_edit_resv},
	{G_TYPE_STRING, SORTID_USERS, "Users", false, EDIT_TEXTBOX,
	 refresh_resv, create_model_resv, admin_edit_resv},
	{G_TYPE_STRING, SORTID_PARTITION, "Partition", false, EDIT_TEXTBOX,
	 refresh_resv, create_model_resv, admin_edit_resv},
	{G_TYPE_STRING, SORTID_FEATURES, "Features", false, EDIT_TEXTBOX,
	 refresh_resv, create_model_resv, admin_edit_resv},
	{G_TYPE_STRING, SORTID_FLAGS, "Flags", false, EDIT_TEXTBOX,
	 refresh_resv, create_model_resv, admin_edit_resv},
	{G_TYPE_POINTER, SORTID_NODE_INX, NULL, false, EDIT_NONE,
	 refresh_resv, create_model_resv, admin_edit_resv},
	{G_TYPE_INT, SORTID_COLOR_INX, NULL, false, EDIT_NONE,
	 refresh_resv, create_model_resv, admin_edit_resv},
	{G_TYPE_STRING, SORTID_TRES, "TRES", false, EDIT_TEXTBOX,
	 refresh_resv, create_model_resv, admin_edit_resv},
	{G_TYPE_INT, SORTID_UPDATED, NULL, false, EDIT_NONE,
	 refresh_resv, create_model_resv, admin_edit_resv},
	{G_TYPE_STRING, SORTID_WATTS, "Watts", false, EDIT_TEXTBOX,
	 refresh_resv, create_model_resv, admin_edit_resv},
	{G_TYPE_NONE, -1, NULL, false, EDIT_NONE}
};

static display_data_t create_data_resv[] = {
	{G_TYPE_INT, SORTID_POS, NULL, false, EDIT_NONE,
	 refresh_resv, create_model_resv, admin_edit_resv},
	{G_TYPE_STRING, SORTID_NAME, "Name", false, EDIT_TEXTBOX,
	 refresh_resv, create_model_resv, admin_edit_resv},
	{G_TYPE_STRING, SORTID_NODE_CNT, "Node_Count", false, EDIT_TEXTBOX,
	 refresh_resv, create_model_resv, admin_edit_resv},
	{G_TYPE_STRING, SORTID_CORE_CNT, "Core_Count", false, EDIT_TEXTBOX,
	 refresh_resv, create_model_resv, admin_edit_resv},
	{G_TYPE_STRING, SORTID_NODELIST, "Node_List", false, EDIT_TEXTBOX,
	 refresh_resv, create_model_resv, admin_edit_resv},
	{G_TYPE_STRING, SORTID_TIME_START, "Time_Start", false, EDIT_TEXTBOX,
	 refresh_resv, create_model_resv, admin_edit_resv},
	{G_TYPE_STRING, SORTID_TIME_END, "Time_End", false, EDIT_TEXTBOX,
	 refresh_resv, create_model_resv, admin_edit_resv},
	{G_TYPE_STRING, SORTID_DURATION, "Duration", false, EDIT_TEXTBOX,
	 refresh_resv, create_model_resv, admin_edit_resv},
	{G_TYPE_STRING, SORTID_ACCOUNTS, "Accounts", false, EDIT_TEXTBOX,
	 refresh_resv, create_model_resv, admin_edit_resv},
	{G_TYPE_STRING, SORTID_BURST_BUFFER, "BurstBuffer", false,
	 EDIT_TEXTBOX, refresh_resv, create_model_resv, admin_edit_resv},
	{G_TYPE_STRING, SORTID_USERS, "Users", false, EDIT_TEXTBOX,
	 refresh_resv, create_model_resv, admin_edit_resv},
	{G_TYPE_STRING, SORTID_PARTITION, "Partition", false, EDIT_TEXTBOX,
	 refresh_resv, create_model_resv, admin_edit_resv},
	{G_TYPE_STRING, SORTID_FEATURES, "Features", false, EDIT_TEXTBOX,
	 refresh_resv, create_model_resv, admin_edit_resv},
	{G_TYPE_STRING, SORTID_FLAGS, "Flags", false, EDIT_TEXTBOX,
	 refresh_resv, create_model_resv, admin_edit_resv},
	{G_TYPE_STRING, SORTID_TRES, "TRES", false, EDIT_TEXTBOX,
	 refresh_resv, create_model_resv, admin_edit_resv},
	{G_TYPE_STRING, SORTID_WATTS, "Watts", false, EDIT_TEXTBOX,
	 refresh_resv, create_model_resv, admin_edit_resv},
	{G_TYPE_NONE, -1, NULL, false, EDIT_NONE}
};

static display_data_t options_data_resv[] = {
	{G_TYPE_INT, SORTID_POS, NULL, false, EDIT_NONE},
	{G_TYPE_STRING, INFO_PAGE, "Full Info", true, RESV_PAGE},
	{G_TYPE_STRING, RESV_PAGE, "Remove Reservation", true, ADMIN_PAGE},
	{G_TYPE_STRING, RESV_PAGE, "Edit Reservation", true, ADMIN_PAGE},
	{G_TYPE_STRING, JOB_PAGE, "Jobs", true, RESV_PAGE},
	{G_TYPE_STRING, PART_PAGE, "Partitions", true, RESV_PAGE},
	{G_TYPE_STRING, NODE_PAGE, "Nodes", true, RESV_PAGE},
	{G_TYPE_NONE, -1, NULL, false, EDIT_NONE}
};

static display_data_t *local_display_data = NULL;
static char *got_edit_signal = NULL;
static GtkTreeModel *last_model = NULL;

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
	switch (type) {
	case SORTID_ACTION:
		if (!xstrcmp(temp_char, "none"))
			action = 0;
		else if (!xstrcmp(temp_char, "remove"))
			action = 1;
		else
			action = 0;

		break;
	default:
		break;
	}
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
	char *err_msg = NULL;
	int free_tres_license = 0;
	int free_tres_bb = 0;
	int free_tres_corecnt = 0;
	int free_tres_nodecnt = 0;
	int temp_int = 0;
	uint64_t f;

	/* need to clear global_edit_error here (just in case) */
	global_edit_error = 0;

	if (!resv_msg)
		return NULL;

	switch (column) {
	case SORTID_ACCOUNTS:
		resv_msg->accounts = xstrdup(new_text);
		type = "accounts";
		break;
	case SORTID_ACTION:
		xfree(got_edit_signal);
		if (!xstrcasecmp(new_text, "None"))
			got_edit_signal = NULL;
		else
			got_edit_signal = xstrdup(new_text);
		break;
	case SORTID_BURST_BUFFER:
		resv_msg->burst_buffer = xstrdup(new_text);
		type = "burst_buffer";
		break;
	case SORTID_CORE_CNT:
		type = "Core Count";
		if (state_control_corecnt_supported() != SLURM_SUCCESS) {
			if (global_edit_error_msg)
				g_free(global_edit_error_msg);
			global_edit_error_msg = g_strdup("CoreCnt or CPUCnt is only supported when SelectType includes select/cons_res or SelectTypeParameters includes OTHER_CONS_RES on a Cray.");
			goto return_error;
		}
		if (state_control_parse_resv_corecnt(resv_msg, (char *)new_text,
						     &free_tres_corecnt, false,
						     &err_msg) == SLURM_ERROR) {
			if (global_edit_error_msg)
				g_free(global_edit_error_msg);
			global_edit_error_msg = g_strdup(err_msg);
			xfree(err_msg);
			goto return_error;
		}
		break;
	case SORTID_DURATION:
		temp_int = time_str2mins((char *)new_text);
		if (temp_int <= 0)
			goto return_error;
		resv_msg->duration = temp_int;
		type = "duration";
		break;
	case SORTID_TIME_END:
		resv_msg->end_time = parse_time((char *)new_text, 0);
		type = "end time";
		break;
	case SORTID_FEATURES:
		resv_msg->features = xstrdup(new_text);
		type = "features";
		break;
	case SORTID_FLAGS:
		f = parse_resv_flags(new_text, __func__);
		type = "flags";
		if (f == INFINITE64)
			goto return_error;
		resv_msg->flags = f;
		break;
	case SORTID_LICENSES:
		resv_msg->licenses = xstrdup(new_text);
		type = "licenses";
		break;
	case SORTID_NAME:
		resv_msg->name = xstrdup(new_text);
		type = "name";
		break;
	case SORTID_NODE_CNT:
		type = "Node Count";
		if (parse_resv_nodecnt(resv_msg, (char *)new_text,
				       &free_tres_nodecnt, false,
				       &err_msg) == SLURM_ERROR) {
			if (global_edit_error_msg)
				g_free(global_edit_error_msg);
			global_edit_error_msg = g_strdup(err_msg);
			xfree(err_msg);
			goto return_error;
		}
		break;
	case SORTID_NODELIST:
		resv_msg->node_list = xstrdup(new_text);
		type = "Node List";
		break;
	case SORTID_PARTITION:
		resv_msg->partition = xstrdup(new_text);
		type = "partition";
		break;
	case SORTID_TIME_START:
		resv_msg->start_time = parse_time((char *)new_text, 0);
		type = "start time";
		break;
	case SORTID_USERS:
		resv_msg->users = xstrdup(new_text);
		type = "users";
		break;
	case SORTID_TRES:
		if (state_control_parse_resv_tres((char *)new_text, resv_msg,
						  &free_tres_license,
						  &free_tres_bb,
						  &free_tres_corecnt,
						  &free_tres_nodecnt, &err_msg)
		    == SLURM_ERROR) {
			if (global_edit_error_msg)
				g_free(global_edit_error_msg);
			global_edit_error_msg = g_strdup(err_msg);
			xfree(err_msg);
			goto return_error;
		}
		break;
	case SORTID_WATTS:
		if (state_control_parse_resv_watts((char *) new_text, resv_msg,
						   &err_msg) == SLURM_ERROR) {
			if (global_edit_error_msg)
				g_free(global_edit_error_msg);
			global_edit_error_msg = g_strdup(err_msg);
			xfree(err_msg);
			goto return_error;
		}
		type = "watts";
		break;
	default:
		type = "unknown";
		break;
	}

	if (xstrcmp(type, "unknown"))
		global_send_update_msg = 1;

	return type;

return_error:
	global_edit_error = 1;
	return type;
}

static void _resv_info_free(sview_resv_info_t *sview_resv_info)
{
	if (sview_resv_info) {
		xfree(sview_resv_info->resv_name);
	}
}

static void _resv_info_list_del(void *object)
{
	sview_resv_info_t *sview_resv_info = (sview_resv_info_t *)object;

	if (sview_resv_info) {
		_resv_info_free(sview_resv_info);
		xfree(sview_resv_info);
	}
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
		if (global_edit_error && !global_edit_error_msg) {
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

	gtk_table_set_homogeneous(table, false);

	for (i = 0; i < SORTID_CNT; i++) {
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
				sview_resv_info_t *sview_resv_info,
				int update)
{
	GtkTreeIter iter;
	char time_buf[20];
	reserve_info_t *resv_ptr = sview_resv_info->resv_ptr;
	char *temp_char = NULL;

	GtkTreeStore *treestore =
		GTK_TREE_STORE(gtk_tree_view_get_model(treeview));

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_resv,
						 SORTID_ACCOUNTS),
				   resv_ptr->accounts);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_resv,
						 SORTID_BURST_BUFFER),
				   resv_ptr->burst_buffer);

	convert_num_unit((float)resv_ptr->core_cnt,
			 time_buf, sizeof(time_buf), UNIT_NONE, NO_VAL,
			 working_sview_config.convert_flags);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_resv,
						 SORTID_CORE_CNT),
				   time_buf);
	secs2time_str((uint32_t)difftime(resv_ptr->end_time,
					 resv_ptr->start_time),
		      time_buf, sizeof(time_buf));
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_resv,
						 SORTID_DURATION),
				   time_buf);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_resv,
						 SORTID_FEATURES),
				   resv_ptr->features);

	temp_char = reservation_flags_string(resv_ptr->flags);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_resv,
						 SORTID_FLAGS),
				   temp_char);
	xfree(temp_char);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_resv,
						 SORTID_LICENSES),
				   resv_ptr->licenses);

	/* NOTE: node_cnt in reservation info from slurmctld ONE number */
	convert_num_unit((float)resv_ptr->node_cnt,
			 time_buf, sizeof(time_buf), UNIT_NONE, NO_VAL,
			 working_sview_config.convert_flags);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_resv,
						 SORTID_NODE_CNT),
				   time_buf);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_resv,
						 SORTID_NODELIST),
				   resv_ptr->node_list);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_resv,
						 SORTID_PARTITION),
				   resv_ptr->partition);

	slurm_make_time_str((time_t *)&resv_ptr->end_time, time_buf,
			    sizeof(time_buf));
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_resv,
						 SORTID_TIME_END),
				   time_buf);
	slurm_make_time_str((time_t *)&resv_ptr->start_time, time_buf,
			    sizeof(time_buf));
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_resv,
						 SORTID_TIME_START),
				   time_buf);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_resv,
						 SORTID_TRES),
				   resv_ptr->tres_str);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_resv,
						 SORTID_USERS),
				   resv_ptr->users);

	temp_char = state_control_watts_to_str(resv_ptr->resv_watts);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_resv,
						 SORTID_WATTS),
				   temp_char);
	xfree(temp_char);
}

static void _update_resv_record(sview_resv_info_t *sview_resv_info_ptr,
				GtkTreeStore *treestore)
{
	char tmp_duration[40], tmp_end[40], tmp_nodes[40], tmp_start[40];
	char tmp_cores[40];
	char *tmp_flags;
	char *tmp_watts = NULL;
	reserve_info_t *resv_ptr = sview_resv_info_ptr->resv_ptr;

	secs2time_str((uint32_t)difftime(resv_ptr->end_time,
					 resv_ptr->start_time),
		      tmp_duration, sizeof(tmp_duration));

	slurm_make_time_str((time_t *)&resv_ptr->end_time, tmp_end,
			    sizeof(tmp_end));

	tmp_flags = reservation_flags_string(resv_ptr->flags);

	convert_num_unit((float)resv_ptr->core_cnt,
			 tmp_cores, sizeof(tmp_cores), UNIT_NONE, NO_VAL,
			 working_sview_config.convert_flags);

	convert_num_unit((float)resv_ptr->node_cnt,
			 tmp_nodes, sizeof(tmp_nodes), UNIT_NONE, NO_VAL,
			 working_sview_config.convert_flags);

	slurm_make_time_str((time_t *)&resv_ptr->start_time, tmp_start,
			    sizeof(tmp_start));

	tmp_watts = state_control_watts_to_str(resv_ptr->resv_watts);

	/* Combining these records provides a slight performance improvement */
	gtk_tree_store_set(treestore, &sview_resv_info_ptr->iter_ptr,
			   SORTID_ACCOUNTS,   resv_ptr->accounts,
			   SORTID_BURST_BUFFER, resv_ptr->burst_buffer,
			   SORTID_COLOR,
				sview_colors[sview_resv_info_ptr->color_inx],
			   SORTID_COLOR_INX,  sview_resv_info_ptr->color_inx,
			   SORTID_CORE_CNT,   tmp_cores,
			   SORTID_DURATION,   tmp_duration,
			   SORTID_FEATURES,   resv_ptr->features,
			   SORTID_FLAGS,      tmp_flags,
			   SORTID_LICENSES,   resv_ptr->licenses,
			   SORTID_NAME,       resv_ptr->name,
			   SORTID_NODE_CNT,   tmp_nodes,
			   SORTID_NODE_INX,   resv_ptr->node_inx,
			   SORTID_NODELIST,   resv_ptr->node_list,
			   SORTID_PARTITION,  resv_ptr->partition,
			   SORTID_TIME_START, tmp_start,
			   SORTID_TIME_END,   tmp_end,
			   SORTID_TRES,       resv_ptr->tres_str,
			   SORTID_UPDATED,    1,
			   SORTID_USERS,      resv_ptr->users,
			   SORTID_WATTS,      tmp_watts,
			   -1);

	xfree(tmp_flags);
	xfree(tmp_watts);

	return;
}

static void _append_resv_record(sview_resv_info_t *sview_resv_info_ptr,
				GtkTreeStore *treestore)
{
	gtk_tree_store_append(treestore, &sview_resv_info_ptr->iter_ptr, NULL);
	gtk_tree_store_set(treestore, &sview_resv_info_ptr->iter_ptr,
			   SORTID_POS, sview_resv_info_ptr->pos, -1);
	_update_resv_record(sview_resv_info_ptr, treestore);
}

static void _update_info_resv(List info_list,
			      GtkTreeView *tree_view)
{
	GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
	char *name = NULL;
	ListIterator itr = NULL;
	sview_resv_info_t *sview_resv_info = NULL;

	set_for_update(model, SORTID_UPDATED);

	itr = list_iterator_create(info_list);
	while ((sview_resv_info = list_next(itr))) {
		/* This means the tree_store changed (added new column
		   or something). */
		if (last_model != model)
			sview_resv_info->iter_set = false;

		if (sview_resv_info->iter_set) {
			gtk_tree_model_get(model, &sview_resv_info->iter_ptr,
					   SORTID_NAME, &name, -1);
			if (xstrcmp(name, sview_resv_info->resv_name)) {
				/* Bad pointer */
				sview_resv_info->iter_set = false;
				//g_print("bad resv iter pointer\n");
			}
			g_free(name);
		}
		if (sview_resv_info->iter_set) {
			_update_resv_record(sview_resv_info,
					    GTK_TREE_STORE(model));
		} else {
			_append_resv_record(sview_resv_info,
					    GTK_TREE_STORE(model));
			sview_resv_info->iter_set = true;
		}
	}
	list_iterator_destroy(itr);

	/* remove all old reservations */
	remove_old(model, SORTID_UPDATED);
	last_model = model;
}

static int _sview_resv_sort_aval_dec(void *s1, void *s2)
{
	sview_resv_info_t *rec_a = *(sview_resv_info_t **)s1;
	sview_resv_info_t *rec_b = *(sview_resv_info_t **)s2;
	int size_a;
	int size_b;

	size_a = rec_a->resv_ptr->node_cnt;
	size_b = rec_b->resv_ptr->node_cnt;

	if (size_a < size_b)
		return -1;
	else if (size_a > size_b)
		return 1;

	if (rec_a->resv_ptr->node_list && rec_b->resv_ptr->node_list) {
		size_a = xstrcmp(rec_a->resv_ptr->node_list,
				 rec_b->resv_ptr->node_list);
		if (size_a < 0)
			return -1;
		else if (size_a > 0)
			return 1;
	}
	return 0;
}

static List _create_resv_info_list(reserve_info_msg_t *resv_info_ptr)
{
	static List info_list = NULL;
	List last_list = NULL;
	ListIterator last_list_itr = NULL;
	int i = 0;
	static reserve_info_msg_t *last_resv_info_ptr = NULL;
	sview_resv_info_t *sview_resv_info_ptr = NULL;
	reserve_info_t *resv_ptr = NULL;

	if (info_list && (resv_info_ptr == last_resv_info_ptr))
		goto update_color;

	last_resv_info_ptr = resv_info_ptr;

	if (info_list)
		last_list = info_list;

	info_list = list_create(_resv_info_list_del);

	if (last_list)
		last_list_itr = list_iterator_create(last_list);
	for(i=0; i<resv_info_ptr->record_count; i++) {
		resv_ptr = &(resv_info_ptr->reservation_array[i]);

		sview_resv_info_ptr = NULL;

		if (last_list_itr) {
			while ((sview_resv_info_ptr =
				list_next(last_list_itr))) {
				if (!xstrcmp(sview_resv_info_ptr->resv_name,
					     resv_ptr->name)) {
					list_remove(last_list_itr);
					_resv_info_free(sview_resv_info_ptr);
					break;
				}
			}
			list_iterator_reset(last_list_itr);
		}
		if (!sview_resv_info_ptr)
			sview_resv_info_ptr =
				xmalloc(sizeof(sview_resv_info_t));
		sview_resv_info_ptr->resv_name = xstrdup(resv_ptr->name);
		sview_resv_info_ptr->pos = i;
		sview_resv_info_ptr->resv_ptr = resv_ptr;
		sview_resv_info_ptr->color_inx = i % sview_colors_cnt;
		list_append(info_list, sview_resv_info_ptr);
	}

	list_sort(info_list,
		  (ListCmpF)_sview_resv_sort_aval_dec);

	if (last_list) {
		list_iterator_destroy(last_list_itr);
		FREE_NULL_LIST(last_list);
	}

update_color:
	return info_list;
}

static void _display_info_resv(List info_list, popup_info_t *popup_win)
{
	specific_info_t *spec_info = popup_win->spec_info;
	char *name = (char *)spec_info->search_info->gchar_data;
	int found = 0;
	reserve_info_t *resv_ptr = NULL;
	GtkTreeView *treeview = NULL;
	ListIterator itr = NULL;
	sview_resv_info_t *sview_resv_info = NULL;
	int update = 0;
	int j = 0;

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
	while ((sview_resv_info = list_next(itr))) {
		resv_ptr = sview_resv_info->resv_ptr;
		if (!xstrcmp(resv_ptr->name, name)) {
			j=0;
			while (resv_ptr->node_inx[j] >= 0) {
				change_grid_color(
					popup_win->grid_button_list,
					resv_ptr->node_inx[j],
					resv_ptr->node_inx[j+1],
					sview_resv_info->color_inx,
					true, 0);
				j += 2;
			}
			_layout_resv_record(treeview, sview_resv_info, update);
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

extern GtkWidget *create_resv_entry(resv_desc_msg_t *resv_msg,
				    GtkTreeModel *model, GtkTreeIter *iter)
{
	GtkScrolledWindow *window = create_scrolled_window();
	GtkBin *bin = NULL;
	GtkViewport *view = NULL;
	GtkTable *table = NULL;
	int i = 0, row = 0;
	display_data_t *display_data = create_data_resv;

	gtk_scrolled_window_set_policy(window,
				       GTK_POLICY_NEVER,
				       GTK_POLICY_AUTOMATIC);
	bin = GTK_BIN(&window->container);
	view = GTK_VIEWPORT(bin->child);
	bin = GTK_BIN(&view->bin);
	table = GTK_TABLE(bin->child);
	gtk_table_resize(table, SORTID_CNT, 2);

	gtk_table_set_homogeneous(table, false);

	for (i = 0; i < SORTID_CNT; i++) {
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
		display_data = create_data_resv;
	}
	gtk_table_resize(table, row, 2);

	return GTK_WIDGET(window);
}

extern void refresh_resv(GtkAction *action, gpointer user_data)
{
	popup_info_t *popup_win = (popup_info_t *)user_data;
	xassert(popup_win);
	xassert(popup_win->spec_info);
	xassert(popup_win->spec_info->title);
	popup_win->force_refresh = 1;
	specific_info_resv(popup_win);
}

extern int get_new_info_resv(reserve_info_msg_t **info_ptr,
			     int force)
{
	static reserve_info_msg_t *new_resv_ptr = NULL;
	int error_code = SLURM_NO_CHANGE_IN_DATA;
	time_t now = time(NULL);
	static time_t last;
	static bool changed = 0;

	if (g_resv_info_ptr && !force
	    && ((now - last) < working_sview_config.refresh_delay)) {
		if (*info_ptr != g_resv_info_ptr)
			error_code = SLURM_SUCCESS;
		*info_ptr = g_resv_info_ptr;
		if (changed)
			error_code = SLURM_SUCCESS;
		goto end_it;
	}
	last = now;
	if (g_resv_info_ptr) {
		error_code = slurm_load_reservations(
			g_resv_info_ptr->last_update, &new_resv_ptr);
		if (error_code == SLURM_SUCCESS) {
			slurm_free_reservation_info_msg(g_resv_info_ptr);
			changed = 1;
		} else if (slurm_get_errno() == SLURM_NO_CHANGE_IN_DATA) {
			error_code = SLURM_NO_CHANGE_IN_DATA;
			new_resv_ptr = g_resv_info_ptr;
			changed = 0;
		}
	} else {
		new_resv_ptr = NULL;
		error_code = slurm_load_reservations((time_t) NULL,
						     &new_resv_ptr);
		changed = 1;
	}

	g_resv_info_ptr = new_resv_ptr;

	if (g_resv_info_ptr && (*info_ptr != g_resv_info_ptr))
		error_code = SLURM_SUCCESS;

	*info_ptr = g_resv_info_ptr;
end_it:
	return error_code;
}

extern GtkListStore *create_model_resv(int type)
{
	GtkListStore *model = NULL;
	GtkTreeIter iter;

	last_model = NULL;	/* Reformat display */
	switch(type) {
	case SORTID_ACTION:
		model = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   1, SORTID_ACTION,
				   0, "None",
				   -1);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   1, SORTID_ACTION,
				   0, "Remove Reservation",
				   -1);
		break;
	default:
		break;
	}
	return model;
}

extern void admin_edit_resv(GtkCellRendererText *cell,
			    const char *path_string,
			    const char *new_text,
			    gpointer data)
{
	GtkTreeStore *treestore = NULL;
	GtkTreePath *path = NULL;
	GtkTreeIter iter;
	resv_desc_msg_t *resv_msg = NULL;

	char *temp = NULL;
	char *old_text = NULL;
	const char *type = NULL;

	int column;

	if (!new_text || !xstrcmp(new_text, ""))
		goto no_input;

	if (cluster_flags & CLUSTER_FLAG_FED) {
		display_fed_disabled_popup(type);
		goto no_input;
	}

	column = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(cell), "column"));
	path = gtk_tree_path_new_from_string(path_string);
	treestore = GTK_TREE_STORE(data);
	gtk_tree_model_get_iter(GTK_TREE_MODEL(treestore), &iter, path);

	gtk_tree_model_get(GTK_TREE_MODEL(treestore), &iter,
			   SORTID_NAME, &temp,
			   column, &old_text,
			   -1);

	resv_msg = xmalloc(sizeof(resv_desc_msg_t));
	slurm_init_resv_desc_msg(resv_msg);
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

	if (old_text && !xstrcmp(old_text, new_text)) {
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
	slurm_free_resv_desc_msg(resv_msg);

	gtk_tree_path_free(path);
	g_free(old_text);
	g_mutex_unlock(sview_mutex);
}

extern void get_info_resv(GtkTable *table, display_data_t *display_data)
{
	int error_code = SLURM_SUCCESS;
	List info_list = NULL;
	static int view = -1;
	static reserve_info_msg_t *resv_info_ptr = NULL;
	char error_char[100];
	GtkWidget *label = NULL;
	GtkTreeView *tree_view = NULL;
	static GtkWidget *display_widget = NULL;
	int j=0;
	ListIterator itr = NULL;
	sview_resv_info_t *sview_resv_info_ptr = NULL;
	reserve_info_t *resv_ptr = NULL;
	time_t now = time(NULL);
	GtkTreePath *path = NULL;
	static bool set_opts = false;

	if (!set_opts)
		set_page_opts(RESV_PAGE, display_data_resv,
			      SORTID_CNT, _initial_page_opts);
	set_opts = true;

	/* reset */
	if (!table && !display_data) {
		if (display_widget)
			gtk_widget_destroy(display_widget);
		display_widget = NULL;
		resv_info_ptr = NULL;
		goto reset_curs;
	}

	if (display_data)
		local_display_data = display_data;
	if (!table) {
		display_data_resv->set_menu = local_display_data->set_menu;
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

	error_code = get_new_info_resv(&resv_info_ptr, force_refresh);
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
	info_list = _create_resv_info_list(resv_info_ptr);
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
	if (!path) {
		itr = list_iterator_create(info_list);
		while ((sview_resv_info_ptr = list_next(itr))) {
			resv_ptr = sview_resv_info_ptr->resv_ptr;
			if ((resv_ptr->start_time > now) ||
			    (resv_ptr->end_time   < now))
				continue;/* only map current reservations */
			j=0;
			while (resv_ptr->node_inx[j] >= 0) {
				change_grid_color(grid_button_list,
						  resv_ptr->node_inx[j],
						  resv_ptr->node_inx[j+1],
						  sview_resv_info_ptr->
						  color_inx,
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
		create_treestore(tree_view, display_data_resv,
				 SORTID_CNT, SORTID_TIME_START, SORTID_COLOR);
	}

	view = INFO_VIEW;
	_update_info_resv(info_list, GTK_TREE_VIEW(display_widget));
end_it:
	toggled = false;
	force_refresh = false;
reset_curs:
	if (main_window && main_window->window)
		gdk_window_set_cursor(main_window->window, NULL);
	return;
}

extern void specific_info_resv(popup_info_t *popup_win)
{
	int resv_error_code = SLURM_SUCCESS;
	static reserve_info_msg_t *resv_info_ptr = NULL;
	static reserve_info_t *resv_ptr = NULL;
	specific_info_t *spec_info = popup_win->spec_info;
	sview_search_info_t *search_info = spec_info->search_info;
	char error_char[100];
	GtkWidget *label = NULL;
	GtkTreeView *tree_view = NULL;
	List resv_list = NULL;
	List send_resv_list = NULL;
	sview_resv_info_t *sview_resv_info_ptr = NULL;
	int j=0, i=-1;
	hostset_t hostset = NULL;
	ListIterator itr = NULL;

	if (!spec_info->display_widget) {
		setup_popup_info(popup_win, display_data_resv, SORTID_CNT);
	}

	if (spec_info->display_widget && popup_win->toggled) {
		gtk_widget_destroy(spec_info->display_widget);
		spec_info->display_widget = NULL;
		goto display_it;
	}

	if ((resv_error_code =
	     get_new_info_resv(&resv_info_ptr, popup_win->force_refresh))
	    == SLURM_NO_CHANGE_IN_DATA) {
		if (!spec_info->display_widget || spec_info->view == ERROR_VIEW)
			goto display_it;
	} else if (resv_error_code != SLURM_SUCCESS) {
		if (spec_info->view == ERROR_VIEW)
			goto end_it;
		spec_info->view = ERROR_VIEW;
		if (spec_info->display_widget)
			gtk_widget_destroy(spec_info->display_widget);
		sprintf(error_char, "get_new_info_resv: %s",
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

	resv_list = _create_resv_info_list(resv_info_ptr);

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
				 SORTID_CNT, SORTID_TIME_START, SORTID_COLOR);
	}

	setup_popup_grid_list(popup_win);

	spec_info->view = INFO_VIEW;
	if (spec_info->type == INFO_PAGE) {
		_display_info_resv(resv_list, popup_win);
		goto end_it;
	}

	/* just linking to another list, don't free the inside, just
	   the list */
	send_resv_list = list_create(NULL);
	itr = list_iterator_create(resv_list);
	i = -1;
	while ((sview_resv_info_ptr = list_next(itr))) {
		i++;
		resv_ptr = sview_resv_info_ptr->resv_ptr;
		switch(spec_info->type) {
		case PART_PAGE:
		case NODE_PAGE:
			if (!resv_ptr->node_list)
				continue;

			if (!(hostset = hostset_create(
				      search_info->gchar_data)))
				continue;
			if (!hostset_intersects(hostset, resv_ptr->node_list)) {
				hostset_destroy(hostset);
				continue;
			}
			hostset_destroy(hostset);
			break;
		case JOB_PAGE:
			if (xstrcmp(resv_ptr->name, search_info->gchar_data))
				continue;
			break;
		case RESV_PAGE:
			switch(search_info->search_type) {
			case SEARCH_RESERVATION_NAME:
				if (!search_info->gchar_data)
					continue;

				if (xstrcmp(resv_ptr->name,
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
		list_push(send_resv_list, sview_resv_info_ptr);
		j=0;
		while (resv_ptr->node_inx[j] >= 0) {
			change_grid_color(
				popup_win->grid_button_list,
				resv_ptr->node_inx[j],
				resv_ptr->node_inx[j+1],
				sview_resv_info_ptr->color_inx,
				true, 0);
			j += 2;
		}
	}
	list_iterator_destroy(itr);
	post_setup_popup_grid_list(popup_win);

	_update_info_resv(send_resv_list,
			  GTK_TREE_VIEW(spec_info->display_widget));
	FREE_NULL_LIST(send_resv_list);
end_it:
	popup_win->toggled = 0;
	popup_win->force_refresh = 0;

	return;
}

extern void set_menus_resv(void *arg, void *arg2, GtkTreePath *path, int type)
{
	GtkTreeView *tree_view = (GtkTreeView *)arg;
	popup_info_t *popup_win = (popup_info_t *)arg;
	GtkMenu *menu = (GtkMenu *)arg2;
	List button_list = (List)arg2;

	switch(type) {
	case TAB_CLICKED:
		make_fields_menu(NULL, menu, display_data_resv, SORTID_CNT);
		break;
	case ROW_CLICKED:
		make_options_menu(tree_view, path, menu, options_data_resv);
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

		popup_all_resv(model, &iter, INFO_PAGE);

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

extern void popup_all_resv(GtkTreeModel *model, GtkTreeIter *iter, int id)
{
	char *name = NULL;
	char title[100] = {0};
	ListIterator itr = NULL;
	popup_info_t *popup_win = NULL;
	GError *error = NULL;

	gtk_tree_model_get(model, iter, SORTID_NAME, &name, -1);

	switch(id) {
	case PART_PAGE:
		snprintf(title, 100, "Partition(s) with reservation %s", name);
		break;
	case JOB_PAGE:
		snprintf(title, 100, "Job(s) in reservation %s", name);
		break;
	case NODE_PAGE:
		snprintf(title, 100, "Node(s) in reservation %s ", name);
		break;
	case SUBMIT_PAGE:
		snprintf(title, 100, "Submit job in reservation %s", name);
		break;
	case INFO_PAGE:
		snprintf(title, 100, "Full info for reservation %s", name);
		break;
	default:
		g_print("resv got %d\n", id);
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
			popup_win = create_popup_info(id, RESV_PAGE, title);
		else
			popup_win = create_popup_info(RESV_PAGE, id, title);
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
	popup_win->node_inx_id = SORTID_NODE_INX;

	switch(id) {
	case JOB_PAGE:
	case INFO_PAGE:
		popup_win->spec_info->search_info->gchar_data = name;
		//specific_info_job(popup_win);
		break;
	case NODE_PAGE:
	case PART_PAGE:
		g_free(name);
		gtk_tree_model_get(model, iter, SORTID_NODELIST, &name, -1);
		popup_win->spec_info->search_info->gchar_data = name;
		popup_win->spec_info->search_info->search_type =
			SEARCH_NODE_NAME;
		//specific_info_node(popup_win);
		break;
	case SUBMIT_PAGE:
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

extern void select_admin_resv(GtkTreeModel *model, GtkTreeIter *iter,
			      display_data_t *display_data,
			      GtkTreeView *treeview)
{
	select_admin_common(model, iter, display_data, treeview,
			    SORTID_NODELIST, _process_each_resv);
}

static void _admin_resv(GtkTreeModel *model, GtkTreeIter *iter, char *type)
{
	resv_desc_msg_t *resv_msg = NULL;
	reservation_name_msg_t resv_name_msg;
	char *resvid = NULL;
	char tmp_char[100];
	char *temp = NULL;
	int edit_type = 0;
	int response = 0;
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

	gtk_tree_model_get(model, iter, SORTID_NAME, &resvid, -1);

	resv_msg = xmalloc(sizeof(resv_desc_msg_t));
	slurm_init_resv_desc_msg(resv_msg);
	memset(&resv_name_msg, 0, sizeof(reservation_name_msg_t));

	resv_msg->name = xstrdup(resvid);

	if (!xstrcasecmp("Remove Reservation", type)) {
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
			   label, false, false, 0);
	if (entry)
		gtk_box_pack_start(GTK_BOX(GTK_DIALOG(popup)->vbox),
				   entry, true, true, 0);
	gtk_widget_show_all(popup);
	response = gtk_dialog_run (GTK_DIALOG(popup));

	if (response == GTK_RESPONSE_OK) {
		switch(edit_type) {
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

			if (global_edit_error) {
				temp = g_strdup_printf(
					"Something was wrong with the "
					"values you wanted to change: %s",
					global_edit_error_msg ?
					global_edit_error_msg : "unknown");
				if (global_edit_error_msg)
					g_free(global_edit_error_msg);
			} else if (!global_send_update_msg) {
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

extern void cluster_change_resv(void)
{
	get_info_resv(NULL, NULL);
}
