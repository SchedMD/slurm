/*****************************************************************************\
 *  part_info.c - Functions related to partition display
 *  mode of sview.
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Portions Copyright (C) 2010-2016 SchedMD <https://www.schedmd.com>.
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

#include "src/sview/sview.h"
#include "src/common/parse_time.h"
#include <grp.h>

#define _DEBUG 0

static GtkListStore *_create_model_part2(int type);

typedef struct {
	uint32_t cpu_cnt;
	uint32_t cpu_alloc_cnt;
	uint32_t cpu_idle_cnt;
	uint32_t disk_total;
	char *features;
	char *features_act;
	hostlist_t hl;
	uint64_t mem_total;
	uint32_t node_cnt;
	uint32_t node_alloc_cnt;
	uint32_t node_error_cnt;
	uint32_t node_idle_cnt;
	List node_ptr_list;
	uint32_t node_state;
	partition_info_t* part_ptr;
	char *reason;
} sview_part_sub_t;

/* Collection of data for printing reports. Like data is combined here */
typedef struct {
	int color_inx;
	GtkTreeIter iter_ptr;
	bool iter_set;
	char *part_name;
	char *cluster_name;
	/* part_info contains partition, avail, max_time, job_size,
	 * root, share, groups */
	partition_info_t* part_ptr;
	int pos;
	List sub_list;
	sview_part_sub_t sub_part_total;
} sview_part_info_t;

enum {
	EDIT_PART_STATE = 1,
	EDIT_REMOVE_PART,
	EDIT_EDIT
};

/* These need to be in alpha order (except POS and CNT) */
enum {
	SORTID_POS = POS_LOC,
	SORTID_ALLOW_ACCOUNTS,
	SORTID_ALLOW_GROUPS,
	SORTID_ALLOW_QOS,
	SORTID_ALTERNATE,
	SORTID_CLUSTER_NAME,
	SORTID_COLOR,
	SORTID_COLOR_INX,
	SORTID_CPUS,
	SORTID_DEFAULT,
	SORTID_DENY_ACCOUNTS,
	SORTID_DENY_QOS,
	SORTID_EXCLUSIVE_USER,
	SORTID_FEATURES,
	SORTID_FEATURES_ACT,
	SORTID_GRACE_TIME,
	SORTID_HIDDEN,
	SORTID_JOB_DEFAULTS,
	SORTID_JOB_SIZE,
	SORTID_MAX_CPUS_PER_NODE,
	SORTID_MEM,
	SORTID_NAME,
	SORTID_NODELIST,
	SORTID_NODES_ALLOWED,
	SORTID_NODE_INX,
	SORTID_NODE_STATE,
	SORTID_NODE_STATE_NUM,
	SORTID_NODES,
	SORTID_NODES_MAX,
	SORTID_NODES_MIN,
	SORTID_ONLY_LINE,
	SORTID_OVER_SUBSCRIBE,
	SORTID_OVER_TIME_LIMIT,
	SORTID_PART_STATE,
	SORTID_PREEMPT_MODE,
	SORTID_PRIORITY_JOB_FACTOR,
	SORTID_PRIORITY_TIER,
	SORTID_QOS_CHAR,
	SORTID_REASON,
	SORTID_ROOT,
	SORTID_TMP_DISK,
	SORTID_TIMELIMIT,
	SORTID_UPDATED,
	SORTID_CNT
};

/*
 * These are the settings to apply for the user
 * on the first startup after a fresh slurm install.
 */
static char *_initial_page_opts = "Partition,Default,Part_State,Time_Limit,"
	"Node_Count,Node_State,NodeList";

static display_data_t display_data_part[] = {
	{G_TYPE_INT, SORTID_POS, NULL, false, EDIT_NONE, refresh_part},
	{G_TYPE_STRING, SORTID_CLUSTER_NAME, "ClusterName", false,
	 EDIT_NONE, refresh_part, create_model_part, admin_edit_part},
	{G_TYPE_STRING, SORTID_NAME, "Partition", false,
	 EDIT_NONE, refresh_part, create_model_part, admin_edit_part},
	{G_TYPE_STRING, SORTID_COLOR, NULL, true, EDIT_COLOR, refresh_part,
	 create_model_part, admin_edit_part},
	{G_TYPE_STRING, SORTID_ALTERNATE, "Alternate", false,
	 EDIT_TEXTBOX, refresh_part, create_model_part, admin_edit_part},
	{G_TYPE_STRING, SORTID_DEFAULT, "Default", false,
	 EDIT_MODEL, refresh_part, create_model_part, admin_edit_part},
	{G_TYPE_STRING, SORTID_EXCLUSIVE_USER, "ExclusiveUser", false,
	 EDIT_MODEL, refresh_part, create_model_part, admin_edit_part},
	{G_TYPE_STRING, SORTID_GRACE_TIME, "GraceTime", false,
	 EDIT_TEXTBOX, refresh_part, create_model_part, admin_edit_part},
	{G_TYPE_STRING, SORTID_HIDDEN, "Hidden", false,
	 EDIT_MODEL, refresh_part, create_model_part, admin_edit_part},
	{G_TYPE_STRING, SORTID_PART_STATE, "Part State", false,
	 EDIT_MODEL, refresh_part, create_model_part, admin_edit_part},
	{G_TYPE_STRING, SORTID_TIMELIMIT, "Time Limit", false,
	 EDIT_TEXTBOX, refresh_part, create_model_part, admin_edit_part},
	{G_TYPE_STRING, SORTID_NODES, "Node Count", false,
	 EDIT_NONE, refresh_part, create_model_part, admin_edit_part},
	{G_TYPE_STRING, SORTID_CPUS, "CPU Count", false,
	 EDIT_NONE, refresh_part, create_model_part, admin_edit_part},
	{G_TYPE_STRING, SORTID_NODE_STATE, "Node State", false,
	 EDIT_MODEL, refresh_part,
	 create_model_part, admin_edit_part},
	{G_TYPE_STRING, SORTID_JOB_DEFAULTS, "Job Defaults", false,
	 EDIT_TEXTBOX, refresh_part, create_model_part, admin_edit_part},
	{G_TYPE_STRING, SORTID_JOB_SIZE, "Job Size", false,
	 EDIT_NONE, refresh_part, create_model_part, admin_edit_part},
	{G_TYPE_STRING, SORTID_PREEMPT_MODE, "PreemptMode", false,
	 EDIT_MODEL, refresh_part, create_model_part, admin_edit_part},
	{G_TYPE_STRING, SORTID_PRIORITY_JOB_FACTOR, "PriorityJobFactor", false,
	 EDIT_TEXTBOX, refresh_part, create_model_part, admin_edit_part},
	{G_TYPE_STRING, SORTID_PRIORITY_TIER, "PriorityTier", false,
	 EDIT_TEXTBOX, refresh_part, create_model_part, admin_edit_part},
	{G_TYPE_STRING, SORTID_NODES_MIN, "Nodes (minimum)", false,
	 EDIT_TEXTBOX, refresh_part, create_model_part, admin_edit_part},
	{G_TYPE_STRING, SORTID_NODES_MAX, "Nodes Max", false,
	 EDIT_TEXTBOX, refresh_part, create_model_part, admin_edit_part},
	{G_TYPE_STRING, SORTID_MAX_CPUS_PER_NODE, "Max CPUs Per Node", false,
	 EDIT_TEXTBOX, refresh_part, create_model_part, admin_edit_part},
	{G_TYPE_STRING, SORTID_OVER_SUBSCRIBE, "OverSubscribe", false,
	 EDIT_MODEL, refresh_part, create_model_part, admin_edit_part},
	{G_TYPE_STRING, SORTID_OVER_TIME_LIMIT, "OverTimeLimit", false,
	 EDIT_TEXTBOX, refresh_part, create_model_part, admin_edit_part},
	{G_TYPE_STRING, SORTID_ROOT, "Root", false, EDIT_MODEL, refresh_part,
	 create_model_part, admin_edit_part},
	{G_TYPE_STRING, SORTID_QOS_CHAR, "Qos", false,
	 EDIT_TEXTBOX, refresh_part, create_model_part, admin_edit_part},
	{G_TYPE_STRING, SORTID_ALLOW_ACCOUNTS, "Allowed Accounts", false,
	 EDIT_TEXTBOX, refresh_part, create_model_part, admin_edit_part},
	{G_TYPE_STRING, SORTID_ALLOW_GROUPS, "Allowed Groups", false,
	 EDIT_TEXTBOX, refresh_part, create_model_part, admin_edit_part},
	{G_TYPE_STRING, SORTID_ALLOW_QOS, "Allowed Qos", false,
	 EDIT_TEXTBOX, refresh_part, create_model_part, admin_edit_part},
	{G_TYPE_STRING, SORTID_DENY_ACCOUNTS, "Denied Accounts", false,
	 EDIT_TEXTBOX, refresh_part, create_model_part, admin_edit_part},
	{G_TYPE_STRING, SORTID_DENY_QOS, "Denied Qos", false,
	 EDIT_TEXTBOX, refresh_part, create_model_part, admin_edit_part},
	{G_TYPE_STRING, SORTID_NODES_ALLOWED, "Nodes Allowed Allocating", false,
	 EDIT_TEXTBOX, refresh_part, create_model_part, admin_edit_part},
	{G_TYPE_STRING, SORTID_TMP_DISK, "Temp Disk", false,
	 EDIT_NONE, refresh_part, create_model_part, admin_edit_part},
	{G_TYPE_STRING, SORTID_MEM, "Memory", false, EDIT_NONE, refresh_part,
	 create_model_part, admin_edit_part},
	{G_TYPE_STRING, SORTID_FEATURES, "Available Features", false,
	 EDIT_TEXTBOX, refresh_part, create_model_part, admin_edit_part},
	{G_TYPE_STRING, SORTID_FEATURES_ACT, "Active Features", false,
	 EDIT_TEXTBOX, refresh_part, create_model_part, admin_edit_part},
	{G_TYPE_STRING, SORTID_REASON, "Reason", false,
	 EDIT_NONE, refresh_part, create_model_part, admin_edit_part},
	{G_TYPE_STRING, SORTID_NODELIST, "NodeList", false,
	 EDIT_TEXTBOX, refresh_part, create_model_part, admin_edit_part},
	{G_TYPE_INT, SORTID_NODE_STATE_NUM, NULL, false,
	 EDIT_NONE, refresh_part, create_model_part, admin_edit_part},
	{G_TYPE_INT, SORTID_ONLY_LINE, NULL, false, EDIT_NONE, refresh_part,
	 create_model_part, admin_edit_part},
	{G_TYPE_INT, SORTID_COLOR_INX, NULL, false, EDIT_NONE, refresh_part,
	 create_model_part, admin_edit_part},
	{G_TYPE_POINTER, SORTID_NODE_INX, NULL, false, EDIT_NONE,
	 refresh_part, create_model_part, admin_edit_part},
	{G_TYPE_INT, SORTID_UPDATED, NULL, false, EDIT_NONE, refresh_part,
	 create_model_part, admin_edit_part},
	{G_TYPE_NONE, -1, NULL, false, EDIT_NONE}
};

static display_data_t create_data_part[] = {
	{G_TYPE_INT, SORTID_POS, NULL, false, EDIT_NONE, refresh_part},
	{G_TYPE_STRING, SORTID_CLUSTER_NAME, "ClusterName", false,
	 EDIT_TEXTBOX, refresh_part, _create_model_part2, admin_edit_part},
	{G_TYPE_STRING, SORTID_NAME, "Name", false,
	 EDIT_TEXTBOX, refresh_part, _create_model_part2, admin_edit_part},
	{G_TYPE_STRING, SORTID_ALTERNATE, "Alternate", false,
	 EDIT_TEXTBOX, refresh_part, _create_model_part2, admin_edit_part},
	{G_TYPE_STRING, SORTID_DEFAULT, "Default", false,
	 EDIT_MODEL, refresh_part, _create_model_part2, admin_edit_part},
	{G_TYPE_STRING, SORTID_EXCLUSIVE_USER, "ExclusiveUser", false,
	 EDIT_MODEL, refresh_part, _create_model_part2, admin_edit_part},
	{G_TYPE_STRING, SORTID_GRACE_TIME, "GraceTime", false,
	 EDIT_TEXTBOX, refresh_part, _create_model_part2, admin_edit_part},
	{G_TYPE_STRING, SORTID_HIDDEN, "Hidden", false,
	 EDIT_MODEL, refresh_part, _create_model_part2, admin_edit_part},
	{G_TYPE_STRING, SORTID_JOB_DEFAULTS, "Job Defaults", false,
	 EDIT_TEXTBOX, refresh_part, create_model_part, admin_edit_part},
	{G_TYPE_STRING, SORTID_PART_STATE, "State", false,
	 EDIT_MODEL, refresh_part, _create_model_part2, admin_edit_part},
	{G_TYPE_STRING, SORTID_TIMELIMIT, "Time Limit", false,
	 EDIT_TEXTBOX, refresh_part, _create_model_part2, admin_edit_part},
	{G_TYPE_STRING, SORTID_PREEMPT_MODE, "PreemptMode", false,
	 EDIT_MODEL, refresh_part, _create_model_part2, admin_edit_part},
	{G_TYPE_STRING, SORTID_PRIORITY_JOB_FACTOR, "PriorityJobFactor", false,
	 EDIT_TEXTBOX, refresh_part, _create_model_part2, admin_edit_part},
	{G_TYPE_STRING, SORTID_PRIORITY_TIER, "PriorityTier", false,
	 EDIT_TEXTBOX, refresh_part, _create_model_part2, admin_edit_part},
	{G_TYPE_STRING, SORTID_NODES_MIN, "Nodes (minimum)", false,
	 EDIT_TEXTBOX, refresh_part, _create_model_part2, admin_edit_part},
	{G_TYPE_STRING, SORTID_NODES_MAX, "Nodes Max", false,
	 EDIT_TEXTBOX, refresh_part, _create_model_part2, admin_edit_part},
	{G_TYPE_STRING, SORTID_MAX_CPUS_PER_NODE, "Max CPUs Per Node", false,
	 EDIT_TEXTBOX, refresh_part, _create_model_part2, admin_edit_part},
	{G_TYPE_STRING, SORTID_ROOT, "Root", false,
	 EDIT_MODEL, refresh_part, _create_model_part2, admin_edit_part},
	{G_TYPE_STRING, SORTID_OVER_SUBSCRIBE, "OverSubscribe", false,
	 EDIT_MODEL, refresh_part, _create_model_part2, admin_edit_part},
	{G_TYPE_STRING, SORTID_OVER_TIME_LIMIT, "OverTimeLimit", false,
	 EDIT_TEXTBOX, refresh_part, _create_model_part2, admin_edit_part},
	{G_TYPE_STRING, SORTID_ALLOW_ACCOUNTS, "Accounts Allowed", false,
	 EDIT_TEXTBOX, refresh_part, _create_model_part2, admin_edit_part},
	{G_TYPE_STRING, SORTID_ALLOW_GROUPS, "Groups Allowed", false,
	 EDIT_TEXTBOX, refresh_part, _create_model_part2, admin_edit_part},
	{G_TYPE_STRING, SORTID_ALLOW_QOS, "Qos Allowed", false,
	 EDIT_TEXTBOX, refresh_part, _create_model_part2, admin_edit_part},
	{G_TYPE_STRING, SORTID_DENY_ACCOUNTS, "Accounts Denied", false,
	 EDIT_TEXTBOX, refresh_part, _create_model_part2, admin_edit_part},
	{G_TYPE_STRING, SORTID_DENY_QOS, "Qos Denied", false,
	 EDIT_TEXTBOX, refresh_part, _create_model_part2, admin_edit_part},
	{G_TYPE_STRING, SORTID_NODES_ALLOWED, "Nodes Allowed Allocating", false,
	 EDIT_TEXTBOX, refresh_part, _create_model_part2, admin_edit_part},
	{G_TYPE_STRING, SORTID_FEATURES, "Available Features", false,
	 EDIT_TEXTBOX, refresh_part, _create_model_part2, admin_edit_part},
	{G_TYPE_STRING, SORTID_FEATURES_ACT, "Active Features", false,
	 EDIT_TEXTBOX, refresh_part, _create_model_part2, admin_edit_part},
	{G_TYPE_NONE, SORTID_REASON, "Reason", false,
	 EDIT_TEXTBOX, refresh_part, _create_model_part2, admin_edit_part},
	{G_TYPE_STRING, SORTID_NODELIST, "NodeList", false,
	 EDIT_TEXTBOX, refresh_part, _create_model_part2, admin_edit_part},
	{G_TYPE_NONE, -1, NULL, false, EDIT_NONE}
};

static display_data_t options_data_part[] = {
	{G_TYPE_INT, SORTID_POS, NULL, false, EDIT_NONE},
	{G_TYPE_STRING, INFO_PAGE, "Full Info", true, PART_PAGE},
	{G_TYPE_STRING, PART_PAGE, "Edit Partition", true, ADMIN_PAGE},
	{G_TYPE_STRING, PART_PAGE, "Remove Partition", true, ADMIN_PAGE},
	{G_TYPE_STRING, PART_PAGE, "Drain Nodes",
	 true, ADMIN_PAGE | EXTRA_NODES},
	{G_TYPE_STRING, PART_PAGE, "Resume Nodes",
	 true, ADMIN_PAGE | EXTRA_NODES},
	{G_TYPE_STRING, PART_PAGE, "Put Nodes Down",
	 true, ADMIN_PAGE | EXTRA_NODES},
	{G_TYPE_STRING, PART_PAGE, "Make Nodes Idle",
	 true, ADMIN_PAGE | EXTRA_NODES},
	{G_TYPE_STRING, PART_PAGE, "Update Node Features",
	 true, ADMIN_PAGE | EXTRA_NODES},
	{G_TYPE_STRING, PART_PAGE, "Change Partition State",
	 true, ADMIN_PAGE},
	{G_TYPE_STRING, JOB_PAGE, "Jobs", true, PART_PAGE},
	{G_TYPE_STRING, NODE_PAGE, "Nodes", true, PART_PAGE},
	//{G_TYPE_STRING, SUBMIT_PAGE, "Job Submit", false, PART_PAGE},
	{G_TYPE_STRING, RESV_PAGE, "Reservations", true, PART_PAGE},
	{G_TYPE_NONE, -1, NULL, false, EDIT_NONE}
};

static display_data_t *local_display_data = NULL;
static char *got_edit_signal = NULL;
static char *got_features_edit_signal = NULL;
static GtkTreeModel *last_model = NULL;

static void _append_part_sub_record(sview_part_sub_t *sview_part_sub,
				    GtkTreeStore *treestore, GtkTreeIter *iter,
				    int line);
static void _update_part_sub_record(sview_part_sub_t *sview_part_sub,
				    GtkTreeStore *treestore,
				    GtkTreeIter *iter);

static int _build_min_max_32_string(char *buffer, int buf_size,
				    uint32_t min, uint32_t max, bool range)
{
	char tmp_min[8];
	char tmp_max[8];
	convert_num_unit((float)min, tmp_min, sizeof(tmp_min), UNIT_NONE,
			 NO_VAL, working_sview_config.convert_flags);
	convert_num_unit((float)max, tmp_max, sizeof(tmp_max), UNIT_NONE,
			 NO_VAL, working_sview_config.convert_flags);

	if (max == min)
		return snprintf(buffer, buf_size, "%s", tmp_max);
	else if (range) {
		if (max == INFINITE)
			return snprintf(buffer, buf_size, "%s-infinite",
					tmp_min);
		else
			return snprintf(buffer, buf_size, "%s-%s",
					tmp_min, tmp_max);
	} else
		return snprintf(buffer, buf_size, "%s+", tmp_min);
}

static void _set_active_combo_part(GtkComboBox *combo,
				   GtkTreeModel *model, GtkTreeIter *iter,
				   int type)
{
	char *temp_char = NULL;
	int action = 0;
	int i = 0, unknown_found = 0;
	char *upper = NULL;

	if (model)
		gtk_tree_model_get(model, iter, type, &temp_char, -1);
	if (!temp_char)
		goto end_it;
	switch(type) {
	case SORTID_DEFAULT:
	case SORTID_EXCLUSIVE_USER:
	case SORTID_HIDDEN:
	case SORTID_ROOT:
		if (!xstrcmp(temp_char, "yes"))
			action = 0;
		else if (!xstrcmp(temp_char, "no"))
			action = 1;
		else
			action = 0;

		break;
	case SORTID_OVER_SUBSCRIBE:
		if (!xstrncmp(temp_char, "force", 5))
			action = 0;
		else if (!xstrcmp(temp_char, "no"))
			action = 1;
		else if (!xstrncmp(temp_char, "yes", 3))
			action = 2;
		else if (!xstrcmp(temp_char, "exclusive"))
			action = 3;
		else
			action = 0;
		break;
	case SORTID_PART_STATE:
		if (!xstrcmp(temp_char, "up"))
			action = 0;
		else if (!xstrcmp(temp_char, "down"))
			action = 1;
		else if (!xstrcmp(temp_char, "inactive"))
			action = 2;
		else if (!xstrcmp(temp_char, "drain"))
			action = 3;
		else
			action = 0;
		break;
	case SORTID_NODE_STATE:
		if (!xstrcasecmp(temp_char, "drain"))
			action = 0;
		else if (!xstrcasecmp(temp_char, "resume"))
			action = 1;
		else
			for(i = 0; i < NODE_STATE_END; i++) {
				upper = node_state_string(i);
				if (!xstrcmp(upper, "UNKNOWN")) {
					unknown_found++;
					continue;
				}

				if (!xstrcasecmp(temp_char, upper)) {
					action = i + 2 - unknown_found;
					break;
				}
			}

		break;
	case SORTID_PREEMPT_MODE:
		if (!xstrcasecmp(temp_char, "cancel"))
			action = 0;
		else if (!xstrcasecmp(temp_char, "checkpoint"))
			action = 1;
		else if (!xstrcasecmp(temp_char, "off"))
			action = 2;
		else if (!xstrcasecmp(temp_char, "requeue"))
			action = 3;
		else if (!xstrcasecmp(temp_char, "suspend"))
			action = 4;
		else
			action = 2;	/* off */
		break;
	default:
		break;
	}
	g_free(temp_char);
end_it:
	gtk_combo_box_set_active(combo, action);

}

static uint16_t _set_part_over_subscribe_popup(void)
{
	GtkWidget *table = gtk_table_new(1, 2, false);
	GtkWidget *label = NULL;
	GtkObject *adjustment = gtk_adjustment_new(4,
						   1, 1000,
						   1, 60,
						   0);
	GtkWidget *spin_button =
		gtk_spin_button_new(GTK_ADJUSTMENT(adjustment), 1, 0);
	GtkWidget *popup = gtk_dialog_new_with_buttons(
		"Count",
		GTK_WINDOW (main_window),
		GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
		NULL);
	int response = 0;
	uint16_t count = 4;

	gtk_window_set_type_hint(GTK_WINDOW(popup),
				 GDK_WINDOW_TYPE_HINT_NORMAL);

	label = gtk_dialog_add_button(GTK_DIALOG(popup),
				      GTK_STOCK_OK, GTK_RESPONSE_OK);
	gtk_window_set_default(GTK_WINDOW(popup), label);

	label = gtk_label_new("OverSubscribe Job Count ");

	gtk_container_set_border_width(GTK_CONTAINER(table), 10);

	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(popup)->vbox),
			   table, false, false, 0);

	gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 1, 0, 1);
	gtk_table_attach_defaults(GTK_TABLE(table), spin_button, 1, 2, 0, 1);

	gtk_widget_show_all(popup);
	response = gtk_dialog_run (GTK_DIALOG(popup));

	if (response == GTK_RESPONSE_OK) {
		count = gtk_spin_button_get_value_as_int(
			GTK_SPIN_BUTTON(spin_button));
	}

	gtk_widget_destroy(popup);

	return count;
}

/* don't free this char */
static const char *_set_part_msg(update_part_msg_t *part_msg,
				 const char *new_text,
				 int column)
{
	char *type = "", *temp_char;
	int temp_int = 0;

	global_edit_error = 0;

	if (!part_msg)
		return NULL;

	switch(column) {
	case SORTID_ALTERNATE:
		type = "alternate";
		part_msg->alternate = xstrdup(new_text);
		break;
	case SORTID_DEFAULT:
		if (!xstrcasecmp(new_text, "yes")) {
			part_msg->flags |= PART_FLAG_DEFAULT;
			part_msg->flags &= (~PART_FLAG_DEFAULT_CLR);
		} else if (!xstrcasecmp(new_text, "no")) {
			part_msg->flags &= (~PART_FLAG_DEFAULT);
			part_msg->flags |= PART_FLAG_DEFAULT_CLR;
		}
		type = "default";
		break;
	case SORTID_EXCLUSIVE_USER:
		if (!xstrcasecmp(new_text, "yes")) {
			part_msg->flags |= PART_FLAG_EXCLUSIVE_USER;
			part_msg->flags &= (~PART_FLAG_EXC_USER_CLR);
		} else if (!xstrcasecmp(new_text, "no")) {
			part_msg->flags &= (~PART_FLAG_EXCLUSIVE_USER);
			part_msg->flags |= PART_FLAG_EXC_USER_CLR;
		}
		type = "hidden";
		break;
	case SORTID_GRACE_TIME:
		temp_int = time_str2mins((char *)new_text);
		type = "grace_time";
		if (temp_int <= 0)
			goto return_error;
		/* convert to seconds */
		part_msg->grace_time = (uint32_t)(temp_int * 60);
		break;
	case SORTID_HIDDEN:
		if (!xstrcasecmp(new_text, "yes")) {
			part_msg->flags |= PART_FLAG_HIDDEN;
			part_msg->flags &= (~PART_FLAG_HIDDEN_CLR);
		} else if (!xstrcasecmp(new_text, "no")) {
			part_msg->flags &= (~PART_FLAG_HIDDEN);
			part_msg->flags |= PART_FLAG_HIDDEN_CLR;
		}
		type = "hidden";
		break;
	case SORTID_JOB_DEFAULTS:
		type = "job_defaults";
		part_msg->job_defaults_str = xstrdup(new_text);
		break;
	case SORTID_TIMELIMIT:
		if ((xstrcasecmp(new_text, "infinite") == 0))
			temp_int = INFINITE;
		else
			temp_int = time_str2mins((char *)new_text);

		type = "timelimit";
		if ((temp_int <= 0) && (temp_int != INFINITE))
			goto return_error;
		part_msg->max_time = (uint32_t)temp_int;
		break;
	case SORTID_PREEMPT_MODE:
		if (!xstrcasecmp(new_text, "cancel"))
			part_msg->preempt_mode = PREEMPT_MODE_CANCEL;
		else if (!xstrcasecmp(new_text, "checkpoint"))
			part_msg->preempt_mode = PREEMPT_MODE_CHECKPOINT;
		else if (!xstrcasecmp(new_text, "off"))
			part_msg->preempt_mode = PREEMPT_MODE_OFF;
		else if (!xstrcasecmp(new_text, "requeue"))
			part_msg->preempt_mode = PREEMPT_MODE_REQUEUE;
		else if (!xstrcasecmp(new_text, "suspend"))
			part_msg->preempt_mode = PREEMPT_MODE_SUSPEND;
		type = "preempt_mode";
		break;
	case SORTID_PRIORITY_JOB_FACTOR:
		temp_int = strtol(new_text, (char **)NULL, 10);
		type = "priority_job_factor";
		part_msg->priority_job_factor = (uint16_t)temp_int;
		break;
	case SORTID_PRIORITY_TIER:
		temp_int = strtol(new_text, (char **)NULL, 10);
		type = "priority_tier";
		part_msg->priority_tier = (uint16_t)temp_int;
		break;
	case SORTID_NAME:
		type = "name";
		part_msg->name = xstrdup(new_text);
		break;
	case SORTID_MAX_CPUS_PER_NODE:
		temp_int = strtol(new_text, (char **)NULL, 10);
		type = "max_cpus_per_node";

		if (temp_int <= 0)
			goto return_error;
		part_msg->max_cpus_per_node = temp_int;
		break;
	case SORTID_NODES_MIN:
		temp_int = strtol(new_text, (char **)NULL, 10);
		type = "min_nodes";

		if (temp_int <= 0)
			goto return_error;
		part_msg->min_nodes = (uint32_t)temp_int;
		break;
	case SORTID_NODES_MAX:
		if (!xstrcasecmp(new_text, "infinite")) {
			temp_int = INFINITE;
		} else {
			temp_int = strtol(new_text, &temp_char, 10);
			if ((temp_char[0] == 'k') || (temp_char[0] == 'K'))
				temp_int *= 1024;
			if ((temp_char[0] == 'm') || (temp_char[0] == 'M'))
				temp_int *= (1024 * 1024);
		}

		type = "max_nodes";
		if ((temp_int <= 0) && (temp_int != INFINITE))
			goto return_error;
		part_msg->max_nodes = (uint32_t)temp_int;
		break;
	case SORTID_ROOT:
		if (!xstrcasecmp(new_text, "yes")) {
			part_msg->flags |= PART_FLAG_ROOT_ONLY;
			part_msg->flags &= (~PART_FLAG_ROOT_ONLY_CLR);
		} else if (!xstrcasecmp(new_text, "no")) {
			part_msg->flags &= (~PART_FLAG_ROOT_ONLY);
			part_msg->flags |= PART_FLAG_ROOT_ONLY_CLR;
		}

		type = "root";
		break;
	case SORTID_OVER_SUBSCRIBE:
		if (!xstrcasecmp(new_text, "yes")) {
			part_msg->max_share = _set_part_over_subscribe_popup();
		} else if (!xstrcasecmp(new_text, "exclusive")) {
			part_msg->max_share = 0;
		} else if (!xstrcasecmp(new_text, "force")) {
			part_msg->max_share = _set_part_over_subscribe_popup()
					      | SHARED_FORCE;
		} else if (!xstrcasecmp(new_text, "no"))
			part_msg->max_share = 1;
		else
			goto return_error;
		type = "oversubscribe";
		break;
	case SORTID_OVER_TIME_LIMIT:
		if (!xstrcasecmp(new_text, "INFINITE") ||
		    !xstrcasecmp(new_text, "UNLIMITED")) {
			part_msg->over_time_limit = INFINITE16;
		} else if (new_text[0] >= '0' && new_text[0] <= '9')
			part_msg->over_time_limit = atoi(new_text);
		else
			goto return_error;
		type = "overtimelimit";
		break;
	case SORTID_ALLOW_ACCOUNTS:
		type = "accounts";
		part_msg->allow_accounts = xstrdup(new_text);
		break;
	case SORTID_ALLOW_GROUPS:
		type = "groups";
		part_msg->allow_groups = xstrdup(new_text);
		break;
	case SORTID_ALLOW_QOS:
		type = "qos";
		part_msg->allow_qos = xstrdup(new_text);
		break;
	case SORTID_DENY_ACCOUNTS:
		type = "deny account";
		part_msg->deny_accounts = xstrdup(new_text);
		break;
	case SORTID_DENY_QOS:
		type = "deny qos";
		part_msg->deny_qos = xstrdup(new_text);
		break;
	case SORTID_NODES_ALLOWED:
		type = "allowed alloc nodes";
		part_msg->allow_alloc_nodes = xstrdup(new_text);
		break;
	case SORTID_NODELIST:
		part_msg->nodes = xstrdup(new_text);
		type = "nodelist";
		break;
	case SORTID_PART_STATE:
		if (!xstrcasecmp(new_text, "up"))
			part_msg->state_up = PARTITION_UP;
		else if (!xstrcasecmp(new_text, "down"))
			part_msg->state_up = PARTITION_DOWN;
		else if (!xstrcasecmp(new_text, "inactive"))
			part_msg->state_up = PARTITION_INACTIVE;
		else if (!xstrcasecmp(new_text, "drain"))
			part_msg->state_up = PARTITION_DRAIN;
		else
			goto return_error;
		type = "availability";

		break;
	case SORTID_NODE_STATE:
		type = (char *)new_text;
		got_edit_signal = xstrdup(new_text);
		break;
	case SORTID_FEATURES:
		type = "Update Features";
		got_features_edit_signal = xstrdup(new_text);
		break;
	case SORTID_QOS_CHAR:
		type = "QOS Char";
		part_msg->qos_char = xstrdup(new_text);
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

static void _admin_edit_combo_box_part(GtkComboBox *combo,
				       update_part_msg_t *part_msg)
{
	GtkTreeModel *model = NULL;
	GtkTreeIter iter;
	int column = 0;
	char *name = NULL;

	if (!part_msg)
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

	(void) _set_part_msg(part_msg, name, column);
	if (name)
		g_free(name);
}

static gboolean _admin_focus_out_part(GtkEntry *entry,
				      GdkEventFocus *event,
				      update_part_msg_t *part_msg)
{
	if (global_entry_changed) {
		const char *col_name = NULL;
		int type = gtk_entry_get_max_length(entry);
		const char *name = gtk_entry_get_text(entry);
		type -= DEFAULT_ENTRY_LENGTH;
		col_name = _set_part_msg(part_msg, name, type);
		if (global_edit_error) {
			if (global_edit_error_msg)
				g_free(global_edit_error_msg);
			global_edit_error_msg = g_strdup_printf(
				"Partition %s %s can't be set to %s",
				part_msg->name,
				col_name,
				name);
		}

		global_entry_changed = 0;
	}
	return false;
}

static GtkWidget *_admin_full_edit_part(update_part_msg_t *part_msg,
					GtkTreeModel *model, GtkTreeIter *iter)
{
	GtkScrolledWindow *window = create_scrolled_window();
	GtkBin *bin = NULL;
	GtkViewport *view = NULL;
	GtkTable *table = NULL;
	int i = 0, row = 0;
	display_data_t *display_data = display_data_part;

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
				table, part_msg, &row, model, iter,
				display_data,
				G_CALLBACK(_admin_edit_combo_box_part),
				G_CALLBACK(_admin_focus_out_part),
				_set_active_combo_part);
			break;
		}
		display_data = display_data_part;
	}
	gtk_table_resize(table, row, 2);

	return GTK_WIDGET(window);
}

static void _subdivide_part(sview_part_info_t *sview_part_info,
			    GtkTreeModel *model,
			    GtkTreeIter *sub_iter,
			    GtkTreeIter *iter)
{
	GtkTreeIter first_sub_iter;
	ListIterator itr = NULL;
	int i = 0, line = 0;
	sview_part_sub_t *sview_part_sub = NULL;
	int set = 0;

	memset(&first_sub_iter, 0, sizeof(GtkTreeIter));

	/* make sure all the steps are still here */
	if (sub_iter) {
		first_sub_iter = *sub_iter;
		while (1) {
			gtk_tree_store_set(GTK_TREE_STORE(model), sub_iter,
					   SORTID_UPDATED, 0, -1);
			if (!gtk_tree_model_iter_next(model, sub_iter)) {
				break;
			}
		}
		memcpy(sub_iter, &first_sub_iter, sizeof(GtkTreeIter));
		set = 1;
	}
	itr = list_iterator_create(sview_part_info->sub_list);
	if ((list_count(sview_part_info->sub_list) == 1) &&
	    (sview_part_sub = list_next(itr))) {
		gtk_tree_store_set(GTK_TREE_STORE(model), iter,
				   SORTID_ONLY_LINE, 1, -1);
		_update_part_sub_record(sview_part_sub,
					GTK_TREE_STORE(model),
					iter);
	} else {
		while ((sview_part_sub = list_next(itr))) {
			if (!sub_iter) {
				i = NO_VAL;
				goto adding;
			} else {
				memcpy(sub_iter, &first_sub_iter,
				       sizeof(GtkTreeIter));
			}
			line = 0;
			while (1) {
				int state;
				/* Search for the state number and
				   check to see if it is in the
				   list.  Here we need to pass an int
				   for system where endian is an
				   issue passing a uint16_t may seg
				   fault. */
				gtk_tree_model_get(model, sub_iter,
						   SORTID_NODE_STATE_NUM,
						   &state, -1);
				if ((uint16_t)state
				    == sview_part_sub->node_state) {
					/* update with new info */
					_update_part_sub_record(
						sview_part_sub,
						GTK_TREE_STORE(model),
						sub_iter);
					goto found;
				}

				line++;
				if (!gtk_tree_model_iter_next(model,
							      sub_iter)) {
					break;
				}
			}
		adding:
			_append_part_sub_record(sview_part_sub,
						GTK_TREE_STORE(model),
						iter, line);
/* 			if (i == NO_VAL) */
/* 				line++; */
		found:
			;
		}
	}
	list_iterator_destroy(itr);

	if (set) {
		sub_iter = &first_sub_iter;
		/* clear all steps that aren't active */
		while (1) {
			gtk_tree_model_get(model, sub_iter,
					   SORTID_UPDATED, &i, -1);
			if (!i) {
				if (!gtk_tree_store_remove(
					    GTK_TREE_STORE(model),
					    sub_iter))
					break;
				else
					continue;
			}
			if (!gtk_tree_model_iter_next(model, sub_iter)) {
				break;
			}
		}
	}
	return;
}

static void _layout_part_record(GtkTreeView *treeview,
				sview_part_info_t *sview_part_info,
				int update)
{
	GtkTreeIter iter;
	char time_buf[20], tmp_buf[20];
	char tmp_cnt[8];
	char tmp_cnt1[8];
	char tmp_cnt2[8];
	partition_info_t *part_ptr = sview_part_info->part_ptr;
	sview_part_sub_t *sview_part_sub = NULL;
	char ind_cnt[1024];
	const char *job_def_str = NULL, *temp_char = NULL;
	uint16_t temp_uint16 = 0;
	int i;
	int yes_no = -1;
	uint32_t limit_set = NO_VAL;
	GtkTreeStore *treestore =
		GTK_TREE_STORE(gtk_tree_view_get_model(treeview));

	convert_num_unit((float)sview_part_info->sub_part_total.node_alloc_cnt,
			 tmp_cnt, sizeof(tmp_cnt), UNIT_NONE, NO_VAL,
			 working_sview_config.convert_flags);
	convert_num_unit((float)sview_part_info->sub_part_total.node_idle_cnt,
			 tmp_cnt1, sizeof(tmp_cnt1), UNIT_NONE, NO_VAL,
			 working_sview_config.convert_flags);
	convert_num_unit((float)sview_part_info->sub_part_total.node_error_cnt,
			 tmp_cnt2, sizeof(tmp_cnt2), UNIT_NONE, NO_VAL,
			 working_sview_config.convert_flags);
	snprintf(ind_cnt, sizeof(ind_cnt), "%s/%s/%s",
		 tmp_cnt, tmp_cnt1, tmp_cnt2);

	for (i = 0; i < SORTID_CNT; i++) {
		switch (i) {
		case SORTID_PART_STATE:
			switch (part_ptr->state_up) {
			case PARTITION_UP:
				temp_char = "up";
				break;
			case PARTITION_DOWN:
				temp_char = "down";
				break;
			case PARTITION_INACTIVE:
				temp_char = "inactive";
				break;
			case PARTITION_DRAIN:
				temp_char = "drain";
				break;
			default:
				temp_char = "unknown";
				break;
			}
			break;
		case SORTID_ALTERNATE:
			if (part_ptr->alternate)
				temp_char = part_ptr->alternate;
			else
				temp_char = "";
			break;
		case SORTID_CPUS:
			convert_num_unit((float)part_ptr->total_cpus,
					 tmp_cnt, sizeof(tmp_cnt),
					 UNIT_NONE, NO_VAL,
					 working_sview_config.convert_flags);
			temp_char = tmp_cnt;
			break;
		case SORTID_DEFAULT:
			if (part_ptr->flags & PART_FLAG_DEFAULT)
				yes_no = 1;
			else
				yes_no = 0;
			break;
		case SORTID_FEATURES:
			if (sview_part_sub)
				temp_char = sview_part_sub->features;
			else
				temp_char = "";
			break;
		case SORTID_GRACE_TIME:
			limit_set = part_ptr->grace_time;
			break;
		case SORTID_ALLOW_ACCOUNTS:
			if (part_ptr->allow_accounts)
				temp_char = part_ptr->allow_accounts;
			else
				temp_char = "all";
			break;
		case SORTID_ALLOW_GROUPS:
			if (part_ptr->allow_groups)
				temp_char = part_ptr->allow_groups;
			else
				temp_char = "all";
			break;
		case SORTID_ALLOW_QOS:
			if (part_ptr->allow_qos)
				temp_char = part_ptr->allow_qos;
			else
				temp_char = "all";
			break;
		case SORTID_CLUSTER_NAME:
			if (part_ptr->cluster_name)
				temp_char = part_ptr->cluster_name;
			break;
		case SORTID_DENY_ACCOUNTS:
			if (part_ptr->deny_accounts)
				temp_char = part_ptr->deny_accounts;
			else
				temp_char = "none";
			break;
		case SORTID_DENY_QOS:
			if (part_ptr->deny_qos)
				temp_char = part_ptr->deny_qos;
			else
				temp_char = "none";
			break;
		case SORTID_EXCLUSIVE_USER:
			if (part_ptr->flags & PART_FLAG_EXCLUSIVE_USER)
				yes_no = 1;
			else
				yes_no = 0;
			break;
		case SORTID_HIDDEN:
			if (part_ptr->flags & PART_FLAG_HIDDEN)
				yes_no = 1;
			else
				yes_no = 0;
			break;
		case SORTID_JOB_DEFAULTS:
			job_def_str =
				job_defaults_str(part_ptr->job_defaults_list);
			temp_char = job_def_str;
			break;
		case SORTID_JOB_SIZE:
			_build_min_max_32_string(time_buf, sizeof(time_buf),
						 part_ptr->min_nodes,
						 part_ptr->max_nodes, true);
			temp_char = time_buf;
			break;
		case SORTID_MEM:
			convert_num_unit((float)sview_part_info->
					 sub_part_total.mem_total,
					 tmp_cnt, sizeof(tmp_cnt),
					 UNIT_MEGA, NO_VAL,
					 working_sview_config.convert_flags);
			temp_char = tmp_cnt;
			break;
		case SORTID_NODELIST:
			temp_char = part_ptr->nodes;
			break;
		case SORTID_NODES_ALLOWED:
			temp_char = part_ptr->allow_alloc_nodes;
			break;
		case SORTID_NODES:
			sprintf(tmp_cnt, "%u", part_ptr->total_nodes);
			temp_char = tmp_cnt;
			break;
		case SORTID_NODES_MAX:
			limit_set = part_ptr->max_nodes;
			break;
		case SORTID_NODES_MIN:
			limit_set = part_ptr->min_nodes;
			break;
		case SORTID_MAX_CPUS_PER_NODE:
			limit_set = part_ptr->max_cpus_per_node;
			break;
		case SORTID_NODE_INX:
			break;
		case SORTID_ONLY_LINE:
			break;
		case SORTID_PREEMPT_MODE:
			temp_uint16 = part_ptr->preempt_mode;
			if (temp_uint16 == NO_VAL16)
				temp_uint16 =  slurm_get_preempt_mode();
			temp_char = preempt_mode_string(temp_uint16);
			break;
		case SORTID_PRIORITY_JOB_FACTOR:
			convert_num_unit((float)part_ptr->priority_job_factor,
					 time_buf, sizeof(time_buf), UNIT_NONE,
					 NO_VAL,
					 working_sview_config.convert_flags);
			temp_char = time_buf;
			break;
		case SORTID_PRIORITY_TIER:
			convert_num_unit((float)part_ptr->priority_tier,
					 time_buf, sizeof(time_buf), UNIT_NONE,
					 NO_VAL,
					 working_sview_config.convert_flags);
			temp_char = time_buf;
			break;
		case SORTID_REASON:
			sview_part_sub = list_peek(sview_part_info->sub_list);
			if (sview_part_sub)
				temp_char = sview_part_sub->reason;
			else
				temp_char = "";
			break;
		case SORTID_ROOT:
			if (part_ptr->flags & PART_FLAG_ROOT_ONLY)
				yes_no = 1;
			else
				yes_no = 0;
			break;
		case SORTID_OVER_SUBSCRIBE:
			if (part_ptr->max_share & SHARED_FORCE) {
				snprintf(tmp_buf, sizeof(tmp_buf), "force:%u",
					 (part_ptr->max_share
					  & ~(SHARED_FORCE)));
				temp_char = tmp_buf;
			} else if (part_ptr->max_share == 0)
				temp_char = "exclusive";
			else if (part_ptr->max_share > 1) {
				snprintf(tmp_buf, sizeof(tmp_buf), "yes:%u",
					 part_ptr->max_share);
				temp_char = tmp_buf;
			} else
				temp_char = "no";
			break;
		case SORTID_OVER_TIME_LIMIT:
			if (part_ptr->over_time_limit == INFINITE16) {
				temp_char = "UNLIMITED";
			} else if (part_ptr->over_time_limit == NO_VAL16) {
				temp_char = "N/A";
			} else {
				snprintf(tmp_buf, sizeof(tmp_buf), "%u",
					 part_ptr->over_time_limit);
				temp_char = tmp_buf;
			}
			break;
		case SORTID_TMP_DISK:
			convert_num_unit(
				(float)sview_part_info->sub_part_total.
				disk_total,
				time_buf, sizeof(time_buf), UNIT_NONE, NO_VAL,
				working_sview_config.convert_flags);
			temp_char = time_buf;
			break;
		case SORTID_TIMELIMIT:
			limit_set = part_ptr->max_time;
			break;
		case SORTID_QOS_CHAR:
			if (part_ptr->qos_char)
				temp_char = part_ptr->qos_char;
			else
				temp_char = "N/A";
			break;
		default:
			break;
		}

		if (yes_no != -1) {
			if (yes_no)
				temp_char = "yes";
			else
				temp_char = "no";
			yes_no = -1;
		} else if (limit_set != NO_VAL) {
			if (limit_set == INFINITE)
				temp_char = "infinite";
			else {
				convert_num_unit(
					(float)limit_set, time_buf,
					sizeof(time_buf), UNIT_NONE, NO_VAL,
					working_sview_config.convert_flags);
				temp_char = time_buf;
			}
			limit_set = NO_VAL;
		}

		if (temp_char) {
			add_display_treestore_line(
				update, treestore, &iter,
				find_col_name(display_data_part,
					      i),
				temp_char);
			if (i == SORTID_NODES) {
				add_display_treestore_line(
					update, treestore, &iter,
					"Nodes (Allocated/Idle/Other)",
					ind_cnt);
			}
			temp_char = NULL;
		}
		xfree(job_def_str);
	}
}

static void _update_part_record(sview_part_info_t *sview_part_info,
				GtkTreeStore *treestore)
{
	char tmp_prio_job_factor[40], tmp_prio_tier[40];
	char tmp_size[40], tmp_over_subscribe_buf[40], tmp_time[40];
	char tmp_max_nodes[40], tmp_min_nodes[40], tmp_grace[40];
	char tmp_over_time_limit_buf[40];
	char tmp_cpu_cnt[40], tmp_node_cnt[40], tmp_max_cpus_per_node[40];
	char *tmp_alt, *tmp_default, *tmp_accounts, *tmp_groups, *tmp_hidden;
	char *tmp_deny_accounts, *tmp_qos_char, *tmp_exc_user;
	char *tmp_qos, *tmp_deny_qos, *job_def_str = NULL;
	char *tmp_root, *tmp_over_subscribe, *tmp_over_time_limit, *tmp_state;
	uint16_t tmp_preempt;
	partition_info_t *part_ptr = sview_part_info->part_ptr;
	GtkTreeIter sub_iter;

	if (part_ptr->alternate)
		tmp_alt = part_ptr->alternate;
	else
		tmp_alt = "";

	sprintf(tmp_cpu_cnt, "%u", part_ptr->total_cpus);

	if (part_ptr->flags & PART_FLAG_DEFAULT)
		tmp_default = "yes";
	else
		tmp_default = "no";

	if (part_ptr->allow_accounts)
		tmp_accounts = part_ptr->allow_accounts;
	else
		tmp_accounts = "all";

	if (part_ptr->allow_groups)
		tmp_groups = part_ptr->allow_groups;
	else
		tmp_groups = "all";

	if (part_ptr->allow_qos)
		tmp_qos = part_ptr->allow_qos;
	else
		tmp_qos = "all";

	if (part_ptr->deny_accounts)
		tmp_deny_accounts = part_ptr->deny_accounts;
	else
		tmp_deny_accounts = "none";

	if (part_ptr->deny_qos)
		tmp_deny_qos = part_ptr->deny_qos;
	else
		tmp_deny_qos = "none";

	if (part_ptr->flags & PART_FLAG_EXCLUSIVE_USER)
		tmp_exc_user = "yes";
	else
		tmp_exc_user = "no";

	if (part_ptr->flags & PART_FLAG_HIDDEN)
		tmp_hidden = "yes";
	else
		tmp_hidden = "no";

	if (part_ptr->grace_time == NO_VAL)
		snprintf(tmp_grace, sizeof(tmp_grace), "none");
	else {
		secs2time_str(part_ptr->grace_time,
			      tmp_grace, sizeof(tmp_grace));
	}

	job_def_str = job_defaults_str(part_ptr->job_defaults_list);

	if (part_ptr->max_nodes == INFINITE)
		snprintf(tmp_max_nodes, sizeof(tmp_max_nodes), "infinite");
	else {
		convert_num_unit((float)part_ptr->max_nodes, tmp_max_nodes,
				 sizeof(tmp_max_nodes), UNIT_NONE, NO_VAL,
				 working_sview_config.convert_flags);
	}

	if (part_ptr->min_nodes == INFINITE)
		snprintf(tmp_min_nodes, sizeof(tmp_min_nodes), "infinite");
	else {
		convert_num_unit((float)part_ptr->min_nodes, tmp_min_nodes,
				 sizeof(tmp_min_nodes), UNIT_NONE, NO_VAL,
				 working_sview_config.convert_flags);
	}

	if (part_ptr->max_cpus_per_node == INFINITE) {
		sprintf(tmp_max_cpus_per_node, "UNLIMITED");
	} else {
		sprintf(tmp_max_cpus_per_node, "%u",
			part_ptr->max_cpus_per_node);
	}

	sprintf(tmp_node_cnt, "%u", part_ptr->total_nodes);

	if (part_ptr->flags & PART_FLAG_ROOT_ONLY)
		tmp_root = "yes";
	else
		tmp_root = "no";

	if (part_ptr->state_up == PARTITION_UP)
		tmp_state = "up";
	else if (part_ptr->state_up == PARTITION_DOWN)
		tmp_state = "down";
	else if (part_ptr->state_up == PARTITION_INACTIVE)
		tmp_state = "inact";
	else if (part_ptr->state_up == PARTITION_DRAIN)
		tmp_state = "drain";
	else
		tmp_state = "unk";

	_build_min_max_32_string(tmp_size, sizeof(tmp_size),
				 part_ptr->min_nodes,
				 part_ptr->max_nodes, true);

	tmp_preempt = part_ptr->preempt_mode;
	if (tmp_preempt == NO_VAL16)
		tmp_preempt = slurm_get_preempt_mode();	/* use cluster param */

	convert_num_unit((float)part_ptr->priority_job_factor,
			 tmp_prio_job_factor, sizeof(tmp_prio_job_factor),
			 UNIT_NONE, NO_VAL, working_sview_config.convert_flags);

	convert_num_unit((float)part_ptr->priority_tier, tmp_prio_tier,
			 sizeof(tmp_prio_tier), UNIT_NONE, NO_VAL,
			 working_sview_config.convert_flags);

	if (part_ptr->max_share & SHARED_FORCE) {
		snprintf(tmp_over_subscribe_buf, sizeof(tmp_over_subscribe_buf),
			 "force:%u", (part_ptr->max_share & ~(SHARED_FORCE)));
		tmp_over_subscribe = tmp_over_subscribe_buf;
	} else if (part_ptr->max_share == 0) {
		tmp_over_subscribe = "exclusive";
	} else if (part_ptr->max_share > 1) {
		snprintf(tmp_over_subscribe_buf,
			 sizeof(tmp_over_subscribe_buf), "yes:%u",
			 part_ptr->max_share);
		tmp_over_subscribe = tmp_over_subscribe_buf;
	} else
		tmp_over_subscribe = "no";

	if (part_ptr->over_time_limit == INFINITE16) {
		tmp_over_time_limit = "UNLIMITED";
	} else if (part_ptr->over_time_limit == NO_VAL16) {
		tmp_over_time_limit = "N/A";
	} else {
		snprintf(tmp_over_time_limit_buf,
			 sizeof(tmp_over_time_limit_buf), "%u",
			 part_ptr->over_time_limit);
		tmp_over_time_limit = tmp_over_time_limit_buf;
	}

	if (part_ptr->max_time == INFINITE)
		snprintf(tmp_time, sizeof(tmp_time), "infinite");
	else {
		secs2time_str((part_ptr->max_time * 60),
			      tmp_time, sizeof(tmp_time));
	}

	if (part_ptr->qos_char)
		tmp_qos_char = part_ptr->qos_char;
	else
		tmp_qos_char = "N/A";

	/* Combining these records provides a slight performance improvement
	 * NOTE: Some of these fields are cleared here and filled in based upon
	 * the configuration of nodes within this partition. */
	gtk_tree_store_set(treestore, &sview_part_info->iter_ptr,
			   SORTID_ALTERNATE,  tmp_alt,
			   SORTID_CLUSTER_NAME, part_ptr->cluster_name,
			   SORTID_COLOR,
				sview_colors[sview_part_info->color_inx],
			   SORTID_COLOR_INX,  sview_part_info->color_inx,
			   SORTID_CPUS,       tmp_cpu_cnt,
			   SORTID_DEFAULT,    tmp_default,
			   SORTID_FEATURES,   "",
			   SORTID_FEATURES_ACT, "",
			   SORTID_GRACE_TIME, tmp_grace,
			   SORTID_QOS_CHAR,   tmp_qos_char,
			   SORTID_ALLOW_ACCOUNTS, tmp_accounts,
			   SORTID_ALLOW_GROUPS, tmp_groups,
			   SORTID_ALLOW_QOS,  tmp_qos,
			   SORTID_DENY_ACCOUNTS, tmp_deny_accounts,
			   SORTID_DENY_QOS,   tmp_deny_qos,
			   SORTID_EXCLUSIVE_USER, tmp_exc_user,
			   SORTID_HIDDEN,     tmp_hidden,
			   SORTID_JOB_DEFAULTS, job_def_str,
			   SORTID_JOB_SIZE,   tmp_size,
			   SORTID_MAX_CPUS_PER_NODE, tmp_max_cpus_per_node,
			   SORTID_MEM,        "",
			   SORTID_NAME,       part_ptr->name,
			   SORTID_NODE_INX,   part_ptr->node_inx,
			   SORTID_NODE_STATE, "",
			   SORTID_NODE_STATE_NUM, -1,
			   SORTID_NODES,      tmp_node_cnt,
			   SORTID_NODES_MAX,  tmp_max_nodes,
			   SORTID_NODES_MIN,  tmp_min_nodes,
			   SORTID_NODELIST,   part_ptr->nodes,
			   SORTID_ONLY_LINE,  0,
			   SORTID_OVER_SUBSCRIBE, tmp_over_subscribe,
			   SORTID_OVER_TIME_LIMIT, tmp_over_time_limit,
			   SORTID_PART_STATE, tmp_state,
			   SORTID_PREEMPT_MODE,
				preempt_mode_string(tmp_preempt),
			   SORTID_PRIORITY_JOB_FACTOR, tmp_prio_job_factor,
			   SORTID_PRIORITY_TIER, tmp_prio_tier,
			   SORTID_REASON,     "",
			   SORTID_ROOT,       tmp_root,
			   SORTID_TIMELIMIT,  tmp_time,
			   SORTID_TMP_DISK,   "",
			   SORTID_UPDATED,    1,
			   -1);
	xfree(job_def_str);

	if (gtk_tree_model_iter_children(GTK_TREE_MODEL(treestore),
					 &sub_iter,
					 &sview_part_info->iter_ptr))
		_subdivide_part(sview_part_info,
				GTK_TREE_MODEL(treestore), &sub_iter,
				&sview_part_info->iter_ptr);
	else
		_subdivide_part(sview_part_info,
				GTK_TREE_MODEL(treestore), NULL,
				&sview_part_info->iter_ptr);

	return;
}

static void _update_part_sub_record(sview_part_sub_t *sview_part_sub,
				    GtkTreeStore *treestore, GtkTreeIter *iter)
{
	partition_info_t *part_ptr = sview_part_sub->part_ptr;
	char *tmp_cpus = NULL, *tmp_nodes = NULL, *tmp_nodelist;
	char *tmp_state_lower, *tmp_state_upper;
	char tmp_cnt[40], tmp_disk[40], tmp_mem[40];

	tmp_state_upper = node_state_string(sview_part_sub->node_state);
	tmp_state_lower = str_tolower(tmp_state_upper);

	if ((sview_part_sub->node_state & NODE_STATE_BASE)
	    == NODE_STATE_MIXED) {
		if (sview_part_sub->cpu_alloc_cnt) {
			convert_num_unit((float)sview_part_sub->cpu_alloc_cnt,
					 tmp_cnt,
					 sizeof(tmp_cnt), UNIT_NONE, NO_VAL,
					 working_sview_config.convert_flags);
			xstrfmtcat(tmp_cpus, "Alloc:%s", tmp_cnt);
		}
		if (sview_part_sub->cpu_idle_cnt) {
			convert_num_unit((float)sview_part_sub->cpu_idle_cnt,
					 tmp_cnt,
					 sizeof(tmp_cnt), UNIT_NONE, NO_VAL,
					 working_sview_config.convert_flags);
			if (tmp_cpus)
				xstrcat(tmp_cpus, " ");
			xstrfmtcat(tmp_cpus, "Idle:%s", tmp_cnt);
		}
	} else {
		tmp_cpus = xmalloc(20);
		convert_num_unit((float)sview_part_sub->cpu_cnt,
				 tmp_cpus, 20, UNIT_NONE, NO_VAL,
				 working_sview_config.convert_flags);
	}

	if (!tmp_nodes) {
		convert_num_unit((float)sview_part_sub->node_cnt, tmp_cnt,
				 sizeof(tmp_cnt), UNIT_NONE, NO_VAL,
				 working_sview_config.convert_flags);
		tmp_nodes = xstrdup(tmp_cnt);
	}

	convert_num_unit((float)sview_part_sub->disk_total, tmp_disk,
			 sizeof(tmp_disk), UNIT_NONE, NO_VAL,
			 working_sview_config.convert_flags);

	convert_num_unit((float)sview_part_sub->mem_total, tmp_mem,
			 sizeof(tmp_mem), UNIT_MEGA, NO_VAL,
			 working_sview_config.convert_flags);

	tmp_nodelist = hostlist_ranged_string_xmalloc(sview_part_sub->hl);

	gtk_tree_store_set(treestore, iter,
			   SORTID_CPUS,           tmp_cpus,
			   SORTID_FEATURES,       sview_part_sub->features,
			   SORTID_FEATURES_ACT,   sview_part_sub->features_act,
			   SORTID_MEM,            tmp_mem,
			   SORTID_NAME,           part_ptr->name,
			   SORTID_NODE_STATE_NUM, sview_part_sub->node_state,
			   SORTID_NODELIST,       tmp_nodelist,
			   SORTID_NODES,          tmp_nodes,
			   SORTID_NODE_STATE,     tmp_state_lower,
			   SORTID_REASON,         sview_part_sub->reason,
			   SORTID_TMP_DISK,       tmp_disk,
			   SORTID_UPDATED,        1,
			   -1);

	xfree(tmp_cpus);
	xfree(tmp_nodelist);
	xfree(tmp_nodes);
	xfree(tmp_state_lower);

	return;
}

static void _append_part_record(sview_part_info_t *sview_part_info,
				GtkTreeStore *treestore)
{
	gtk_tree_store_append(treestore, &sview_part_info->iter_ptr, NULL);
	gtk_tree_store_set(treestore, &sview_part_info->iter_ptr,
			   SORTID_POS, sview_part_info->pos, -1);
	_update_part_record(sview_part_info, treestore);
}

static void _append_part_sub_record(sview_part_sub_t *sview_part_sub,
				    GtkTreeStore *treestore, GtkTreeIter *iter,
				    int line)
{
	GtkTreeIter sub_iter;

	gtk_tree_store_append(treestore, &sub_iter, iter);
	gtk_tree_store_set(treestore, &sub_iter, SORTID_POS, line, -1);
	_update_part_sub_record(sview_part_sub, treestore, &sub_iter);
}

static void _update_info_part(List info_list,
			      GtkTreeView *tree_view)
{
	GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
	char *name = NULL;
	ListIterator itr = NULL;
	sview_part_info_t *sview_part_info = NULL;

	set_for_update(model, SORTID_UPDATED);

	itr = list_iterator_create(info_list);
	while ((sview_part_info = (sview_part_info_t*) list_next(itr))) {
		/* This means the tree_store changed (added new column
		   or something). */
		if (last_model != model)
			sview_part_info->iter_set = false;

		if (sview_part_info->iter_set) {
			gtk_tree_model_get(model, &sview_part_info->iter_ptr,
					   SORTID_NAME, &name, -1);
			if (xstrcmp(name, sview_part_info->part_name))
				/* Bad pointer */
				sview_part_info->iter_set = false;
			g_free(name);
		}
		if (sview_part_info->iter_set)
			_update_part_record(sview_part_info,
					    GTK_TREE_STORE(model));
		else {
			_append_part_record(sview_part_info,
					    GTK_TREE_STORE(model));
			sview_part_info->iter_set = true;
		}
	}
	list_iterator_destroy(itr);
	/* remove all old partitions */
	remove_old(model, SORTID_UPDATED);
	last_model = model;
	return;
}

static void _part_info_free(sview_part_info_t *sview_part_info)
{
	if (sview_part_info) {
		xfree(sview_part_info->cluster_name);
		xfree(sview_part_info->part_name);
		memset(&sview_part_info->sub_part_total, 0,
		       sizeof(sview_part_sub_t));
		FREE_NULL_LIST(sview_part_info->sub_list);
	}
}

static void _part_info_list_del(void *object)
{
	sview_part_info_t *sview_part_info = (sview_part_info_t *)object;

	if (sview_part_info) {
		_part_info_free(sview_part_info);
		xfree(sview_part_info);
	}
}

static void _destroy_part_sub(void *object)
{
	sview_part_sub_t *sview_part_sub = (sview_part_sub_t *)object;

	if (sview_part_sub) {
		xfree(sview_part_sub->features);
		xfree(sview_part_sub->features_act);
		xfree(sview_part_sub->reason);
		if (sview_part_sub->hl)
			hostlist_destroy(sview_part_sub->hl);
		FREE_NULL_LIST(sview_part_sub->node_ptr_list);
		xfree(sview_part_sub);
	}
}

static void _update_sview_part_sub(sview_part_sub_t *sview_part_sub,
				   node_info_t *node_ptr)
{
	int idle_cpus = node_ptr->cpus;
	uint16_t alloc_cpus = 0;

	xassert(sview_part_sub);
	xassert(sview_part_sub->node_ptr_list);
	xassert(sview_part_sub->hl);

	if (sview_part_sub->node_cnt == 0) {	/* first node added */
		sview_part_sub->node_state   = node_ptr->node_state;
		sview_part_sub->features     = xstrdup(node_ptr->features);
		sview_part_sub->features_act = xstrdup(node_ptr->features_act);
		sview_part_sub->reason       = xstrdup(node_ptr->reason);
	} else if (hostlist_find(sview_part_sub->hl, node_ptr->name) != -1) {
		/* we already have this node in this record,
		 * just return, don't duplicate */
		g_print("already been here\n");
		return;
	}

	if ((sview_part_sub->node_state & NODE_STATE_BASE)
	    == NODE_STATE_MIXED) {
		slurm_get_select_nodeinfo(node_ptr->select_nodeinfo,
					  SELECT_NODEDATA_SUBCNT,
					  NODE_STATE_ALLOCATED,
					  &alloc_cpus);
		idle_cpus -= alloc_cpus;
	} else if (sview_part_sub->node_state == NODE_STATE_ALLOCATED) {
		alloc_cpus = idle_cpus;
		idle_cpus = 0;
	}

	sview_part_sub->cpu_cnt += alloc_cpus + idle_cpus;
	sview_part_sub->cpu_alloc_cnt += alloc_cpus;
	sview_part_sub->cpu_idle_cnt += idle_cpus;
	sview_part_sub->disk_total += node_ptr->tmp_disk;
	sview_part_sub->mem_total  += node_ptr->real_memory;
	sview_part_sub->node_cnt++;

	list_append(sview_part_sub->node_ptr_list, node_ptr);
	hostlist_push_host(sview_part_sub->hl, node_ptr->name);
}

/*
 * _create_sview_part_sub - create an sview_part_sub record for
 *                          the given partition
 * sview_part_sub OUT     - ptr to an inited sview_part_sub_t
 */
static sview_part_sub_t *_create_sview_part_sub(partition_info_t *part_ptr,
						node_info_t *node_ptr)
{
	sview_part_sub_t *sview_part_sub_ptr =
		xmalloc(sizeof(sview_part_sub_t));

	if (!part_ptr) {
		g_print("got no part_ptr!\n");
		xfree(sview_part_sub_ptr);
		return NULL;
	}
	if (!node_ptr) {
		g_print("got no node_ptr!\n");
		xfree(sview_part_sub_ptr);
		return NULL;
	}
	sview_part_sub_ptr->part_ptr = part_ptr;
	sview_part_sub_ptr->hl = hostlist_create(NULL);
	sview_part_sub_ptr->node_ptr_list = list_create(NULL);
	_update_sview_part_sub(sview_part_sub_ptr, node_ptr);

	return sview_part_sub_ptr;
}

static int _insert_sview_part_sub(sview_part_info_t *sview_part_info,
				  partition_info_t *part_ptr,
				  node_info_t *node_ptr)
{
	sview_part_sub_t *sview_part_sub = NULL;
	ListIterator itr = list_iterator_create(sview_part_info->sub_list);

	while ((sview_part_sub = list_next(itr))) {
		if (sview_part_sub->node_state
		    == node_ptr->node_state) {
			_update_sview_part_sub(sview_part_sub,
					       node_ptr);
			break;
		}
	}
	list_iterator_destroy(itr);

	if (!sview_part_sub) {
		if ((sview_part_sub = _create_sview_part_sub(
			     part_ptr, node_ptr)))
			list_push(sview_part_info->sub_list,
				  sview_part_sub);
	}
	return SLURM_SUCCESS;
}

static int _sview_part_sort_aval_dec(void *a, void *b)
{
	sview_part_info_t *rec_a = *(sview_part_info_t **)a;
	sview_part_info_t *rec_b = *(sview_part_info_t **)b;
	int size_a;
	int size_b;

	size_a = rec_a->part_ptr->total_nodes;
	size_b = rec_b->part_ptr->total_nodes;

	if (size_a < size_b)
		return -1;
	else if (size_a > size_b)
		return 1;

	if (rec_a->part_ptr->nodes && rec_b->part_ptr->nodes) {
		size_a = xstrcmp(rec_a->part_ptr->nodes,
				 rec_b->part_ptr->nodes);
		if (size_a < 0)
			return -1;
		else if (size_a > 0)
			return 1;
	}
	return 0;
}

static int _sview_sub_part_sort(void *a, void *b)
{
	sview_part_sub_t *rec_a = *(sview_part_sub_t **)a;
	sview_part_sub_t *rec_b = *(sview_part_sub_t **)b;
	int size_a;
	int size_b;

	size_a = rec_a->node_state & NODE_STATE_BASE;
	size_b = rec_b->node_state & NODE_STATE_BASE;

	if (size_a < size_b)
		return -1;
	else if (size_a > size_b)
		return 1;
	return 0;
}

static List _create_part_info_list(partition_info_msg_t *part_info_ptr,
				   node_info_msg_t *node_info_ptr)
{
	sview_part_info_t *sview_part_info = NULL;
	partition_info_t *part_ptr = NULL;
	static node_info_msg_t *last_node_info_ptr = NULL;
	static partition_info_msg_t *last_part_info_ptr = NULL;
	List last_list = NULL;
	ListIterator last_list_itr = NULL;
	node_info_t *node_ptr = NULL;
	static List info_list = NULL;
	int i, j2;
	sview_part_sub_t *sview_part_sub = NULL;
	ListIterator itr;

	if (info_list && (node_info_ptr == last_node_info_ptr)
	    && (part_info_ptr == last_part_info_ptr))
		return info_list;

	last_node_info_ptr = node_info_ptr;
	last_part_info_ptr = part_info_ptr;

	if (info_list)
		last_list = info_list;

	info_list = list_create(_part_info_list_del);

	if (last_list)
		last_list_itr = list_iterator_create(last_list);
	for (i = 0; i < part_info_ptr->record_count; i++) {
		int c_offset = 0;
		part_ptr = &(part_info_ptr->partition_array[i]);

		/* don't include configured excludes */
		if (!working_sview_config.show_hidden &&
		    part_ptr->flags & PART_FLAG_HIDDEN)
			continue;

		sview_part_info = NULL;

		if (last_list_itr) {
			while ((sview_part_info =
				list_next(last_list_itr))) {
				if (sview_part_info->cluster_name &&
				    xstrcmp(sview_part_info->cluster_name,
					    part_ptr->cluster_name))
					continue;
				if (!xstrcmp(sview_part_info->part_name,
					     part_ptr->name)) {
					list_remove(last_list_itr);
					_part_info_free(sview_part_info);
					break;
				}
			}
			list_iterator_reset(last_list_itr);
		}

		if (!sview_part_info)
			sview_part_info = xmalloc(sizeof(sview_part_info_t));
		sview_part_info->part_name = xstrdup(part_ptr->name);
		sview_part_info->cluster_name = xstrdup(part_ptr->cluster_name);
		sview_part_info->part_ptr = part_ptr;
		sview_part_info->sub_list = list_create(_destroy_part_sub);
		sview_part_info->pos = i;
		list_append(info_list, sview_part_info);
		sview_part_info->color_inx = i % sview_colors_cnt;

		if (cluster_flags & CLUSTER_FLAG_FED)
			c_offset =
				get_cluster_node_offset(part_ptr->cluster_name,
							node_info_ptr);

		j2 = 0;
		while (part_ptr->node_inx[j2] >= 0) {
			int i2 = 0;
			for ((i2 = part_ptr->node_inx[j2]);
			     (i2 <= part_ptr->node_inx[j2+1]);
			     i2++) {
				node_info_t *node_array;
				node_array = node_info_ptr->node_array;
				node_ptr   = &(node_array[i2 + c_offset]);

				_insert_sview_part_sub(sview_part_info,
						       part_ptr,
						       node_ptr);
			}
			j2 += 2;
		}
		list_sort(sview_part_info->sub_list,
			  (ListCmpF)_sview_sub_part_sort);

		/* Need to do this after the fact so we deal with
		   complete sub parts.
		*/

		itr = list_iterator_create(sview_part_info->sub_list);
		while ((sview_part_sub = list_next(itr))) {
			sview_part_info->sub_part_total.node_cnt +=
				sview_part_sub->node_cnt;
			sview_part_info->sub_part_total.cpu_cnt +=
				sview_part_sub->cpu_cnt;

			if (((sview_part_sub->node_state
				     & NODE_STATE_BASE) == NODE_STATE_MIXED) ||
				   (sview_part_sub->node_state
				    == NODE_STATE_ALLOCATED))
				sview_part_info->sub_part_total.node_alloc_cnt
					+= sview_part_sub->node_cnt;
			else if (sview_part_sub->node_state
				   != NODE_STATE_IDLE)
				sview_part_info->sub_part_total.node_error_cnt
					+= sview_part_sub->node_cnt;
			else
				sview_part_info->sub_part_total.node_idle_cnt
					+= sview_part_sub->node_cnt;

			sview_part_info->sub_part_total.cpu_alloc_cnt +=
				sview_part_sub->cpu_alloc_cnt;
			sview_part_info->sub_part_total.cpu_idle_cnt +=
				sview_part_sub->cpu_idle_cnt;
			sview_part_info->sub_part_total.disk_total +=
				sview_part_sub->disk_total;
			sview_part_info->sub_part_total.mem_total +=
				sview_part_sub->mem_total;
			if (!sview_part_info->sub_part_total.features) {
				/* store features and reasons
				   in the others group */
				sview_part_info->sub_part_total.features
					= sview_part_sub->features;
				sview_part_info->sub_part_total.features_act
					= sview_part_sub->features_act;
				sview_part_info->sub_part_total.reason
					= sview_part_sub->reason;
			}
			hostlist_sort(sview_part_sub->hl);
		}
		list_iterator_destroy(itr);
	}
	list_sort(info_list, (ListCmpF)_sview_part_sort_aval_dec);

	if (last_list) {
		list_iterator_destroy(last_list_itr);
		FREE_NULL_LIST(last_list);
	}

	return info_list;
}

static void _display_info_part(List info_list,	popup_info_t *popup_win)
{
	specific_info_t *spec_info = popup_win->spec_info;
	char *name = (char *)spec_info->search_info->gchar_data;
	char *cluster_name = (char *)spec_info->search_info->cluster_name;
	int found = 0;
	partition_info_t *part_ptr = NULL;
	GtkTreeView *treeview = NULL;
	ListIterator itr = NULL;
	sview_part_info_t *sview_part_info = NULL;
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
	while ((sview_part_info = (sview_part_info_t*) list_next(itr))) {
		part_ptr = sview_part_info->part_ptr;

		if (cluster_name &&
		    xstrcmp(part_ptr->cluster_name, cluster_name))
			continue;

		if (!xstrcmp(part_ptr->name, name)) {
			j = 0;
			while (part_ptr->node_inx[j] >= 0) {
				change_grid_color(
					popup_win->grid_button_list,
					part_ptr->node_inx[j],
					part_ptr->node_inx[j+1],
					sview_part_info->color_inx,
					true, 0);
				j += 2;
			}
			_layout_part_record(treeview, sview_part_info, update);
			found = 1;
			break;
		}
	}
	list_iterator_destroy(itr);
	post_setup_popup_grid_list(popup_win);

	if (!found) {
		if (!popup_win->not_found) {
			char *temp = "PARTITION DOESN'T EXSIST\n";
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

static void _process_each_partition(GtkTreeModel  *model,
				    GtkTreePath   *path,
				    GtkTreeIter   *iter,
				    gpointer       userdata)
{
	char *type = userdata;
	if (_DEBUG)
		g_print("process_each_partition: global_multi_error = %d\n",
			global_multi_error);

	if (!global_multi_error) {
		admin_part(model, iter, type);
	}
}
/*process_each_partition ^^^*/

extern GtkWidget *create_part_entry(update_part_msg_t *part_msg,
				    GtkTreeModel *model, GtkTreeIter *iter)
{
	GtkScrolledWindow *window = create_scrolled_window();
	GtkBin *bin = NULL;
	GtkViewport *view = NULL;
	GtkTable *table = NULL;
	int i = 0, row = 0;
	display_data_t *display_data = create_data_part;

	gtk_scrolled_window_set_policy(window,
				       GTK_POLICY_NEVER,
				       GTK_POLICY_AUTOMATIC);
	bin = GTK_BIN(&window->container);
	view = GTK_VIEWPORT(bin->child);
	bin = GTK_BIN(&view->bin);
	table = GTK_TABLE(bin->child);
	gtk_table_resize(table, SORTID_CNT, 2);

	gtk_table_set_homogeneous(table, false);

	for(i = 0; i < SORTID_CNT; i++) {
		while (display_data++) {
			if (display_data->id == -1)
				break;
			if (!display_data->name)
				continue;
			if (display_data->id != i)
				continue;
			display_admin_edit(
				table, part_msg, &row, model, iter,
				display_data,
				G_CALLBACK(_admin_edit_combo_box_part),
				G_CALLBACK(_admin_focus_out_part),
				_set_active_combo_part);
			break;
		}
		display_data = create_data_part;
	}
	gtk_table_resize(table, row, 2);

	return GTK_WIDGET(window);
}

extern bool check_part_includes_node(int node_dx)
{
	partition_info_t *part_ptr = NULL;
	bool rc = false;
	int i;
	static partition_info_msg_t *part_info_ptr = NULL;

	if (working_sview_config.show_hidden)
		return true;

	if (!g_part_info_ptr) {
		i = get_new_info_part(&part_info_ptr, true);
		if (!g_part_info_ptr || (i && (i != SLURM_NO_CHANGE_IN_DATA))) {
			if (_DEBUG)
				g_print("%s : error %d ", __func__, i);
			return false;
		}
	}

	for (i = 0; i < g_part_info_ptr->record_count; i++) {
		/* don't include allow group or hidden excludes */
		part_ptr = &(g_part_info_ptr->partition_array[i]);
		if (part_ptr->flags & PART_FLAG_HIDDEN)
			continue;
		if (part_ptr->node_inx[0] >= 0) {
			if (_DEBUG) {
				g_print("node_dx = %d ", node_dx);
				g_print("part_node_inx[0] = %d ",
					part_ptr->node_inx[0]);
				g_print("part_node_inx[1] = %d \n",
					part_ptr->node_inx[1]);
			}
			if (node_dx >= part_ptr->node_inx[0] &&
			    node_dx <= part_ptr->node_inx[1]) {
				rc = true;
				if (_DEBUG)
					g_print("hit!!\n");
			}
		}
		if (rc)
			break;
	}
	return rc;
}


extern void refresh_part(GtkAction *action, gpointer user_data)
{
	popup_info_t *popup_win = (popup_info_t *)user_data;
	xassert(popup_win);
	xassert(popup_win->spec_info);
	xassert(popup_win->spec_info->title);
	popup_win->force_refresh = 1;
	specific_info_part(popup_win);
}


extern int get_new_info_part(partition_info_msg_t **part_ptr, int force)
{
	static partition_info_msg_t *new_part_ptr = NULL;
	uint16_t show_flags = 0;
	int error_code = SLURM_NO_CHANGE_IN_DATA;
	time_t now = time(NULL);
	static time_t last;
	static bool changed = 0;
	static uint16_t last_flags = 0;

	if (g_part_info_ptr && !force
	    && ((now - last) < working_sview_config.refresh_delay)) {
		if (*part_ptr != g_part_info_ptr)
			error_code = SLURM_SUCCESS;
		*part_ptr = g_part_info_ptr;
		if (changed)
			error_code = SLURM_SUCCESS;
		goto end_it;
	}
	last = now;

	if (cluster_flags & CLUSTER_FLAG_FED)
		show_flags |= SHOW_FEDERATION;
	if (working_sview_config.show_hidden)
		/*ignore 'AllowGroups, Hidden settings*/
		show_flags |= SHOW_ALL;

	if (g_part_info_ptr) {
		if (show_flags != last_flags)
			g_part_info_ptr->last_update = 0;
		error_code = slurm_load_partitions(g_part_info_ptr->last_update,
						   &new_part_ptr, show_flags);
		if (error_code == SLURM_SUCCESS) {
			slurm_free_partition_info_msg(g_part_info_ptr);
			changed = 1;
		} else if (slurm_get_errno() == SLURM_NO_CHANGE_IN_DATA) {
			error_code = SLURM_NO_CHANGE_IN_DATA;
			new_part_ptr = g_part_info_ptr;
			changed = 0;
		}
	} else {
		new_part_ptr = NULL;
		error_code = slurm_load_partitions((time_t) NULL, &new_part_ptr,
						   show_flags);
		changed = 1;
	}

	last_flags = show_flags;
	g_part_info_ptr = new_part_ptr;

	if (g_part_info_ptr && (*part_ptr != g_part_info_ptr))
		error_code = SLURM_SUCCESS;

	*part_ptr = new_part_ptr;
end_it:

	return error_code;
}

static GtkListStore *_create_model_part2(int type)
{
	GtkListStore *model = NULL;
	GtkTreeIter iter;

	last_model = NULL;	/* Reformat display */
	switch (type) {
	case SORTID_DEFAULT:
	case SORTID_EXCLUSIVE_USER:
	case SORTID_HIDDEN:
		model = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "no (default)", 1, SORTID_DEFAULT, -1);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "yes", 1, SORTID_DEFAULT, -1);
		break;
	case SORTID_ROOT:
		model = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "yes (default)", 1, SORTID_DEFAULT, -1);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "no", 1, SORTID_DEFAULT, -1);
		break;
	case SORTID_OVER_SUBSCRIBE:
		model = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter, 0, "no (default)",
				   1, SORTID_OVER_SUBSCRIBE, -1);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,  0, "yes",
				  1, SORTID_OVER_SUBSCRIBE, -1);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter, 0, "force",
				   1, SORTID_OVER_SUBSCRIBE, -1);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter, 0, "exclusive",
				   1, SORTID_OVER_SUBSCRIBE, -1);
		break;
	case SORTID_PART_STATE:
		model = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "up (default)", 1, SORTID_PART_STATE, -1);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "down", 1, SORTID_PART_STATE, -1);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "inactive", 1, SORTID_PART_STATE, -1);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "drain", 1, SORTID_PART_STATE, -1);
		break;
	case SORTID_PREEMPT_MODE:
		model = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter, 0,
				preempt_mode_string(slurm_get_preempt_mode()),
				1, SORTID_PREEMPT_MODE, -1);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "cancel", 1, SORTID_PREEMPT_MODE, -1);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "checkpoint", 1, SORTID_PREEMPT_MODE,-1);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "off", 1, SORTID_PREEMPT_MODE, -1);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "requeue", 1, SORTID_PREEMPT_MODE, -1);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "suspend", 1, SORTID_PREEMPT_MODE, -1);
		break;
	}

	return model;
}

extern GtkListStore *create_model_part(int type)
{
	GtkListStore *model = NULL;
	GtkTreeIter iter;
	char *upper = NULL, *lower = NULL;
	int i = 0;

	last_model = NULL;	/* Reformat display */
	switch (type) {
	case SORTID_DEFAULT:
	case SORTID_EXCLUSIVE_USER:
	case SORTID_HIDDEN:
	case SORTID_ROOT:
		model = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "yes", 1, type, -1);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "no", 1, type, -1);
		break;
	case SORTID_PREEMPT_MODE:
		model = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "cancel", 1, SORTID_PREEMPT_MODE, -1);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "checkpoint", 1, SORTID_PREEMPT_MODE,-1);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "off", 1, SORTID_PREEMPT_MODE, -1);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "requeue", 1, SORTID_PREEMPT_MODE, -1);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "suspend", 1, SORTID_PREEMPT_MODE, -1);
		break;
	case SORTID_GRACE_TIME:
	case SORTID_JOB_DEFAULTS:
	case SORTID_PRIORITY_JOB_FACTOR:
	case SORTID_PRIORITY_TIER:
	case SORTID_TIMELIMIT:
	case SORTID_NODES_MIN:
	case SORTID_NODES_MAX:
	case SORTID_MAX_CPUS_PER_NODE:
	case SORTID_OVER_TIME_LIMIT:
		break;
	case SORTID_OVER_SUBSCRIBE:
		model = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter, 0, "force",
				   1, SORTID_OVER_SUBSCRIBE, -1);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter, 0, "no",
				   1, SORTID_OVER_SUBSCRIBE, -1);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter, 0, "yes",
				   1, SORTID_OVER_SUBSCRIBE, -1);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter, 0, "exclusive",
				   1, SORTID_OVER_SUBSCRIBE, -1);
		break;
	case SORTID_ALLOW_ACCOUNTS:
		break;
	case SORTID_ALLOW_GROUPS:
		break;
	case SORTID_ALLOW_QOS:
		break;
	case SORTID_DENY_ACCOUNTS:
		break;
	case SORTID_DENY_QOS:
		break;
	case SORTID_NODELIST:
		break;
	case SORTID_PART_STATE:
		model = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "up", 1, SORTID_PART_STATE, -1);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "down", 1, SORTID_PART_STATE, -1);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "inactive", 1, SORTID_PART_STATE, -1);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "drain", 1, SORTID_PART_STATE, -1);
		break;
	case SORTID_NODE_STATE:
		model = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "drain", 1, SORTID_NODE_STATE, -1);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "resume", 1, SORTID_NODE_STATE, -1);
		for (i = 0; i < NODE_STATE_END; i++) {
			upper = node_state_string(i);
			if (!xstrcmp(upper, "UNKNOWN"))
				continue;

			gtk_list_store_append(model, &iter);
			lower = str_tolower(upper);
			gtk_list_store_set(model, &iter,
					   0, lower, 1, SORTID_NODE_STATE, -1);
			xfree(lower);
		}

		break;
	}

	return model;
}

extern void admin_edit_part(GtkCellRendererText *cell,
			    const char *path_string,
			    const char *new_text,
			    gpointer data)
{
	GtkTreeStore *treestore = NULL;
	GtkTreePath *path = NULL;
	GtkTreeIter iter;
	update_part_msg_t *part_msg = NULL;

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

	part_msg = xmalloc(sizeof(update_part_msg_t));

	treestore = GTK_TREE_STORE(data);
	path = gtk_tree_path_new_from_string(path_string);
	gtk_tree_model_get_iter(GTK_TREE_MODEL(treestore), &iter, path);

	column = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(cell), "column"));
	if (column != SORTID_NODE_STATE) {
		slurm_init_part_desc_msg(part_msg);
		gtk_tree_model_get(GTK_TREE_MODEL(treestore), &iter,
				   SORTID_NAME, &temp,
				   column, &old_text,
				   -1);
		part_msg->name = xstrdup(temp);
		g_free(temp);
	}

	type = _set_part_msg(part_msg, new_text, column);
	if (global_edit_error)
		goto print_error;
	if (got_edit_signal) {
		temp = got_edit_signal;
		got_edit_signal = NULL;
		admin_part(GTK_TREE_MODEL(treestore), &iter, temp);
		xfree(temp);
		goto no_input;
	}

	if (got_features_edit_signal) {
		admin_part(GTK_TREE_MODEL(treestore), &iter, (char *)type);
		goto no_input;
	}

	if ((column != SORTID_NODE_STATE) && (column != SORTID_FEATURES)) {
		if (old_text && !xstrcmp(old_text, new_text)) {
			temp = g_strdup_printf("No change in value.");
		} else if (slurm_update_partition(part_msg)
			   == SLURM_SUCCESS) {
			gtk_tree_store_set(treestore, &iter, column,
					   new_text, -1);
			temp = g_strdup_printf("Partition %s %s changed to %s",
					       part_msg->name,
					       type,
					       new_text);
		} else {
		print_error:
			temp = g_strdup_printf("Partition %s %s can't be "
					       "set to %s",
					       part_msg->name,
					       type,
					       new_text);
		}
		display_edit_note(temp);
		g_free(temp);
	}
no_input:
	slurm_free_update_part_msg(part_msg);
	gtk_tree_path_free(path);
	g_free(old_text);
	g_mutex_unlock(sview_mutex);
}

extern void get_info_part(GtkTable *table, display_data_t *display_data)
{
	int part_error_code = SLURM_SUCCESS;
	int node_error_code = SLURM_SUCCESS;
	static int view = -1;
	static partition_info_msg_t *part_info_ptr = NULL;
	static node_info_msg_t *node_info_ptr = NULL;
	char error_char[100];
	GtkWidget *label = NULL;
	GtkTreeView *tree_view = NULL;
	static GtkWidget *display_widget = NULL;
	List info_list = NULL;
	int j, k;
	sview_part_info_t *sview_part_info = NULL;
	partition_info_t *part_ptr = NULL;
	ListIterator itr = NULL;
	GtkTreePath *path = NULL;
	static bool set_opts = false;

	if (!set_opts)
		set_page_opts(PART_PAGE, display_data_part,
			      SORTID_CNT, _initial_page_opts);
	set_opts = true;

	/* reset */
	if (!table && !display_data) {
		if (display_widget)
			gtk_widget_destroy(display_widget);
		display_widget = NULL;
		part_info_ptr = NULL;
		node_info_ptr = NULL;
		goto reset_curs;
	}

	if (display_data)
		local_display_data = display_data;
	if (!table) {
		display_data_part->set_menu = local_display_data->set_menu;
		goto reset_curs;
	}
	if (display_widget && toggled) {
		gtk_widget_destroy(display_widget);
		display_widget = NULL;
		goto display_it;
	}

	if ((part_error_code = get_new_info_part(&part_info_ptr, force_refresh))
	    == SLURM_NO_CHANGE_IN_DATA) {
		// just goto the new info node
	} else 	if (part_error_code != SLURM_SUCCESS) {
		if (view == ERROR_VIEW)
			goto end_it;
		if (display_widget)
			gtk_widget_destroy(display_widget);
		view = ERROR_VIEW;
		snprintf(error_char, 100, "slurm_load_partitions: %s",
			 slurm_strerror(slurm_get_errno()));
		label = gtk_label_new(error_char);
		display_widget = gtk_widget_ref(GTK_WIDGET(label));
		gtk_table_attach_defaults(table, label, 0, 1, 0, 1);
		gtk_widget_show(label);
		goto end_it;
	}

	if ((node_error_code = get_new_info_node(&node_info_ptr, force_refresh))
	    == SLURM_NO_CHANGE_IN_DATA) {
		if ((!display_widget || view == ERROR_VIEW)
		    || (part_error_code != SLURM_NO_CHANGE_IN_DATA))
			goto display_it;
	} else if (node_error_code != SLURM_SUCCESS) {
		if (view == ERROR_VIEW)
			goto end_it;
		if (display_widget)
			gtk_widget_destroy(display_widget);
		view = ERROR_VIEW;
		snprintf(error_char, 100, "slurm_load_node: %s",
			 slurm_strerror(slurm_get_errno()));
		label = gtk_label_new(error_char);
		display_widget = gtk_widget_ref(GTK_WIDGET(label));
		gtk_table_attach_defaults(table, label, 0, 1, 0, 1);
		gtk_widget_show(label);
		goto end_it;
	}

display_it:

	info_list = _create_part_info_list(part_info_ptr, node_info_ptr);
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
		int array_size = node_info_ptr->record_count;
		int  *color_inx = xmalloc(sizeof(int) * array_size);
		bool *color_set_flag = xmalloc(sizeof(bool) * array_size);
		itr = list_iterator_create(info_list);
		while ((sview_part_info = list_next(itr))) {
			part_ptr = sview_part_info->part_ptr;
			j = 0;
			while (part_ptr->node_inx[j] >= 0) {
				for (k = part_ptr->node_inx[j];
				     k <= part_ptr->node_inx[j+1]; k++) {
					color_set_flag[k] = true;
					color_inx[k] = sview_part_info->
						       color_inx;
				}
				j += 2;
			}
		}
		list_iterator_destroy(itr);
		change_grid_color_array(grid_button_list, array_size,
					color_inx, color_set_flag, true, 0);
		change_grid_color(grid_button_list, -1, -1,
				  MAKE_WHITE, true, 0);
		xfree(color_inx);
		xfree(color_set_flag);
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
		int def_sort_col = SORTID_NAME;
		tree_view = create_treeview(local_display_data,
					    &grid_button_list);
		/*set multiple capability here*/
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
		if (cluster_flags & CLUSTER_FLAG_FED)
			def_sort_col = SORTID_CLUSTER_NAME;
		create_treestore(tree_view, display_data_part, SORTID_CNT,
				 def_sort_col, SORTID_COLOR);
	}

	view = INFO_VIEW;
	_update_info_part(info_list, GTK_TREE_VIEW(display_widget));
end_it:
	toggled = false;
	force_refresh = false;
reset_curs:
	if (main_window && main_window->window)
		gdk_window_set_cursor(main_window->window, NULL);
	return;
}

extern void specific_info_part(popup_info_t *popup_win)
{
	int part_error_code = SLURM_SUCCESS;
	int node_error_code = SLURM_SUCCESS;
	static partition_info_msg_t *part_info_ptr = NULL;
	static node_info_msg_t *node_info_ptr = NULL;
	specific_info_t *spec_info = popup_win->spec_info;
	char error_char[100];
	GtkWidget *label = NULL;
	GtkTreeView *tree_view = NULL;
	List info_list = NULL;
	List send_info_list = NULL;
	int j=0, i=-1;
	sview_part_info_t *sview_part_info_ptr = NULL;
	partition_info_t *part_ptr = NULL;
	ListIterator itr = NULL;
	hostset_t hostset = NULL;

	if (!spec_info->display_widget)
		setup_popup_info(popup_win, display_data_part, SORTID_CNT);

	if (spec_info->display_widget && popup_win->toggled) {
		gtk_widget_destroy(spec_info->display_widget);
		spec_info->display_widget = NULL;
		goto display_it;
	}

	if ((part_error_code = get_new_info_part(&part_info_ptr,
						 popup_win->force_refresh))
	    == SLURM_NO_CHANGE_IN_DATA)  {

	} else if (part_error_code != SLURM_SUCCESS) {
		if (spec_info->view == ERROR_VIEW)
			goto end_it;
		if (spec_info->display_widget) {
			gtk_widget_destroy(spec_info->display_widget);
			spec_info->display_widget = NULL;
		}
		spec_info->view = ERROR_VIEW;
		snprintf(error_char, 100, "slurm_load_partitions: %s",
			 slurm_strerror(slurm_get_errno()));
		label = gtk_label_new(error_char);
		spec_info->display_widget = gtk_widget_ref(GTK_WIDGET(label));
		gtk_table_attach_defaults(popup_win->table, label, 0, 1, 0, 1);
		gtk_widget_show(label);
		goto end_it;
	}

	if ((node_error_code = get_new_info_node(&node_info_ptr,
						 popup_win->force_refresh))
	    == SLURM_NO_CHANGE_IN_DATA) {
		if ((!spec_info->display_widget
		     || spec_info->view == ERROR_VIEW)
		    || (part_error_code != SLURM_NO_CHANGE_IN_DATA))
			goto display_it;
	} else if (node_error_code != SLURM_SUCCESS) {
		if (spec_info->view == ERROR_VIEW)
			goto end_it;
		if (spec_info->display_widget)
			gtk_widget_destroy(spec_info->display_widget);
		spec_info->view = ERROR_VIEW;
		snprintf(error_char, 100, "slurm_load_node: %s",
			 slurm_strerror(slurm_get_errno()));
		label = gtk_label_new(error_char);
		spec_info->display_widget = gtk_widget_ref(GTK_WIDGET(label));
		gtk_table_attach_defaults(popup_win->table, label, 0, 1, 0, 1);
		gtk_widget_show(label);
		goto end_it;
	}

display_it:

	info_list = _create_part_info_list(part_info_ptr, node_info_ptr);

	if (!info_list)
		return;

	if (spec_info->view == ERROR_VIEW && spec_info->display_widget) {
		gtk_widget_destroy(spec_info->display_widget);
		spec_info->display_widget = NULL;
	}

	if (spec_info->type != INFO_PAGE && !spec_info->display_widget) {
		tree_view = create_treeview(local_display_data,
					    &popup_win->grid_button_list);
		/*set multiple capability here*/
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
		_display_info_part(info_list, popup_win);
		goto end_it;
	}

	/* just linking to another list, don't free the inside, just
	   the list */
	send_info_list = list_create(NULL);

	itr = list_iterator_create(info_list);
	i = -1;
	while ((sview_part_info_ptr = list_next(itr))) {
		i++;
		part_ptr = sview_part_info_ptr->part_ptr;
		switch (spec_info->type) {
		case RESV_PAGE:
		case NODE_PAGE:
			if (!part_ptr->nodes)
				continue;

			if (!(hostset = hostset_create(
				      spec_info->search_info->gchar_data)))
				continue;
			if (!hostset_intersects(hostset, part_ptr->nodes)) {
				hostset_destroy(hostset);
				continue;
			}
			hostset_destroy(hostset);
			break;
		case PART_PAGE:
			switch(spec_info->search_info->search_type) {
			case SEARCH_PARTITION_NAME:
				if (!spec_info->search_info->gchar_data)
					continue;

				if (xstrcmp(part_ptr->name,
					    spec_info->search_info->gchar_data))
					continue;

				if (spec_info->search_info->cluster_name &&
				    xstrcmp(
					part_ptr->cluster_name,
					spec_info->search_info->cluster_name))
					continue;
				break;
			case SEARCH_PARTITION_STATE:
				if (spec_info->search_info->int_data == NO_VAL)
					continue;
				if (part_ptr->state_up !=
				    spec_info->search_info->int_data)
					continue;
				break;
			default:
				continue;
				break;
			}
			break;
		case JOB_PAGE:
			if (!spec_info->search_info->gchar_data)
				continue;

			if (xstrcmp(part_ptr->name,
				    spec_info->search_info->gchar_data))
				continue;

			if (spec_info->search_info->cluster_name &&
			    xstrcmp(part_ptr->cluster_name,
				    spec_info->search_info->cluster_name))
				continue;
			break;
		default:
			g_print("Unknown type %d\n", spec_info->type);
			list_iterator_destroy(itr);
			goto end_it;
		}
		list_push(send_info_list, sview_part_info_ptr);
		j=0;
		while (part_ptr->node_inx[j] >= 0) {
			change_grid_color(
				popup_win->grid_button_list,
				part_ptr->node_inx[j],
				part_ptr->node_inx[j+1],
				sview_part_info_ptr->color_inx, true, 0);
			j += 2;
		}
	}
	list_iterator_destroy(itr);
	post_setup_popup_grid_list(popup_win);

	_update_info_part(send_info_list,
			  GTK_TREE_VIEW(spec_info->display_widget));
	FREE_NULL_LIST(send_info_list);
end_it:
	popup_win->toggled = 0;
	popup_win->force_refresh = 0;

	return;
}

extern void set_menus_part(void *arg, void *arg2, GtkTreePath *path, int type)
{
	GtkTreeView *tree_view = (GtkTreeView *)arg;
	popup_info_t *popup_win = (popup_info_t *)arg;
	GtkMenu *menu = (GtkMenu *)arg2;
	List button_list = (List)arg2;

	switch(type) {
	case TAB_CLICKED:
		make_fields_menu(NULL, menu, display_data_part, SORTID_CNT);
		break;
	case ROW_CLICKED:
		make_options_menu(tree_view, path, menu, options_data_part);
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

		popup_all_part(model, &iter, INFO_PAGE);

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

extern void popup_all_part(GtkTreeModel *model, GtkTreeIter *iter, int id)
{
	char *name = NULL;
	char *cluster_name = NULL;
	char *state = NULL;
	char title[100] = {0};
	int only_line = 0;
	ListIterator itr = NULL;
	popup_info_t *popup_win = NULL;
	GError *error = NULL;
	GtkTreeIter par_iter;

	gtk_tree_model_get(model, iter, SORTID_NAME, &name, -1);
	gtk_tree_model_get(model, iter, SORTID_CLUSTER_NAME, &cluster_name, -1);

	switch(id) {
	case JOB_PAGE:
		snprintf(title, 100, "Job(s) in partition %s", name);
		break;
	case RESV_PAGE:
		snprintf(title, 100, "Reservation(s) in partition %s", name);
		break;
	case NODE_PAGE:
		gtk_tree_model_get(model, iter, SORTID_ONLY_LINE,
				   &only_line, -1);
		if (!only_line)
			gtk_tree_model_get(model, iter,
					   SORTID_NODE_STATE, &state, -1);
		if (!state || !strlen(state))
			snprintf(title, 100, "Node(s) in partition %s ",
				 name);
		else
			snprintf(title, 100,
				 "Node(s) in partition %s that are in "
				 "'%s' state",
				 name, state);
		break;
	case SUBMIT_PAGE:
		snprintf(title, 100, "Submit job in partition %s", name);
		break;
	case INFO_PAGE:
		snprintf(title, 100, "Full info for partition %s", name);
		break;
	default:
		g_print("part got %d\n", id);
	}

	if (cluster_name && federation_name &&
	    (cluster_flags & CLUSTER_FLAG_FED)) {
		char *tmp_cname = xstrdup_printf(" (%s:%s)",
						 federation_name, cluster_name);
		strncat(title, tmp_cname, sizeof(title) - strlen(title) - 1);
		xfree(tmp_cname);
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
			popup_win = create_popup_info(id, PART_PAGE, title);
		else
			popup_win = create_popup_info(PART_PAGE, id, title);
	} else {
		g_free(name);
		g_free(cluster_name);
		g_free(state);
		gtk_window_present(GTK_WINDOW(popup_win->popup));
		return;
	}

	/* Pass the model and the structs from the iter so we can always get
	   the current node_inx.
	*/
	popup_win->model = model;
	popup_win->iter = *iter;
	popup_win->node_inx_id = SORTID_NODE_INX;

	if (cluster_flags & CLUSTER_FLAG_FED) {
		popup_win->spec_info->search_info->cluster_name = cluster_name;
		cluster_name = NULL;
	}
	g_free(cluster_name);

	switch (id) {
	case JOB_PAGE:
	case INFO_PAGE:
		popup_win->spec_info->search_info->gchar_data = name;
		//specific_info_job(popup_win);
		break;
	case RESV_PAGE:
	case NODE_PAGE:
		g_free(name);
		/* we want to include the parent's nodes here not just
		   the subset */
		if (gtk_tree_model_iter_parent(model, &par_iter, iter))
			gtk_tree_model_get(model, &par_iter,
					   SORTID_NODELIST, &name, -1);
		else
			gtk_tree_model_get(model, iter,
					   SORTID_NODELIST, &name, -1);
		popup_win->spec_info->search_info->gchar_data = name;
		if (state && strlen(state)) {
			popup_win->spec_info->search_info->search_type =
				SEARCH_NODE_STATE;
			gtk_tree_model_get(
				model, iter, SORTID_NODE_STATE_NUM,
				&popup_win->spec_info->search_info->int_data,
				-1);
		} else {
			popup_win->spec_info->search_info->search_type =
				SEARCH_NODE_NAME;
		}
		g_free(state);

		//specific_info_node(popup_win);
		break;
	case SUBMIT_PAGE:
		break;
	default:
		g_print("part got unknown type %d\n", id);
	}
	if (!sview_thread_new((gpointer)popup_thr, popup_win, false, &error)) {
		g_printerr ("Failed to create part popup thread: %s\n",
			    error->message);
		return;
	}
}

extern void select_admin_partitions(GtkTreeModel *model,
				    GtkTreeIter *iter,
				    display_data_t *display_data,
				    GtkTreeView *treeview)
{
	select_admin_common(model, iter, display_data, treeview,
			    SORTID_NODELIST, _process_each_partition);
} /*select_admin_partitions ^^^*/


extern void admin_part(GtkTreeModel *model, GtkTreeIter *iter, char *type)
{
	char *nodelist = NULL;
	char *partid = NULL;
	update_part_msg_t *part_msg = NULL;
	int edit_type = 0;
	int response = 0;
	char tmp_char[100];
	char *temp = NULL;
	GtkWidget *label = NULL;
	GtkWidget *entry = NULL;
	GtkWidget *popup = NULL;

	if (cluster_flags & CLUSTER_FLAG_FED) {
		display_fed_disabled_popup(type);
		return;
	}

	popup = gtk_dialog_new_with_buttons(
		type,
		GTK_WINDOW(main_window),
		GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
		NULL);
	gtk_window_set_transient_for(GTK_WINDOW(popup), NULL);

	gtk_window_set_type_hint(GTK_WINDOW(popup),
				 GDK_WINDOW_TYPE_HINT_NORMAL);

	gtk_tree_model_get(model, iter, SORTID_NAME, &partid, -1);
	gtk_tree_model_get(model, iter, SORTID_NODELIST, &nodelist, -1);

	part_msg = xmalloc(sizeof(update_part_msg_t));
	slurm_init_part_desc_msg(part_msg);
	part_msg->name = xstrdup(partid);

	if (!xstrcasecmp("Change Partition State", type)) {
		GtkCellRenderer *renderer = NULL;
		GtkTreeModel *model2 = GTK_TREE_MODEL(
			create_model_part(SORTID_PART_STATE));
		if (!model2) {
			g_print("In change part, no model set up for %d(%s)\n",
				SORTID_PART_STATE, partid);
			xfree(part_msg);
			return;
		}
		entry = gtk_combo_box_new_with_model(model2);
		g_object_unref(model2);

		_set_active_combo_part(GTK_COMBO_BOX(entry),
				       model, iter, SORTID_PART_STATE);

		g_signal_connect(entry, "changed",
				 G_CALLBACK(_admin_edit_combo_box_part),
				 part_msg);

		renderer = gtk_cell_renderer_text_new();
		gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(entry),
					   renderer, true);
		gtk_cell_layout_add_attribute(GTK_CELL_LAYOUT(entry),
					      renderer, "text", 0);

		label = gtk_dialog_add_button(GTK_DIALOG(popup),
					      GTK_STOCK_YES, GTK_RESPONSE_OK);
		gtk_window_set_default(GTK_WINDOW(popup), label);
		gtk_dialog_add_button(GTK_DIALOG(popup),
				      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);

		snprintf(tmp_char, sizeof(tmp_char),
			 "Which state would you like to set partition '%s' to?",
			 partid);
		label = gtk_label_new(tmp_char);
		edit_type = EDIT_PART_STATE;
	} else if (!xstrcasecmp("Remove Partition", type)) {
		label = gtk_dialog_add_button(GTK_DIALOG(popup),
					      GTK_STOCK_YES, GTK_RESPONSE_OK);
		gtk_window_set_default(GTK_WINDOW(popup), label);
		gtk_dialog_add_button(GTK_DIALOG(popup),
				      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);

		snprintf(tmp_char, sizeof(tmp_char),
			 "Are you sure you want to remove partition %s?",
			 partid);
		label = gtk_label_new(tmp_char);
		edit_type = EDIT_REMOVE_PART;
	} else if (!xstrcasecmp("Edit Partition", type)) {
		label = gtk_dialog_add_button(GTK_DIALOG(popup),
					      GTK_STOCK_OK, GTK_RESPONSE_OK);
		gtk_window_set_default(GTK_WINDOW(popup), label);
		gtk_dialog_add_button(GTK_DIALOG(popup),
				      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);

		gtk_window_set_default_size(GTK_WINDOW(popup), 200, 400);
		snprintf(tmp_char, sizeof(tmp_char),
			 "Editing partition %s think before you type",
			 partid);
		label = gtk_label_new(tmp_char);
		edit_type = EDIT_EDIT;
		entry = _admin_full_edit_part(part_msg, model, iter);
	} else if (!xstrncasecmp("Update", type, 6)) {
		char *old_features = NULL;
		if (got_features_edit_signal)
			old_features = got_features_edit_signal;
		else
			gtk_tree_model_get(model, iter, SORTID_FEATURES,
					   &old_features, -1);
		update_avail_features_node(GTK_DIALOG(popup),
					   nodelist, old_features);
		if (got_features_edit_signal) {
			got_features_edit_signal = NULL;
			xfree(old_features);
		} else
			g_free(old_features);
		goto end_it;
	} else {
		/* something that has to deal with a node state change */
		update_state_node(GTK_DIALOG(popup), nodelist, type);
		goto end_it;
	}

	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(popup)->vbox),
			   label, false, false, 0);
	if (entry)
		gtk_box_pack_start(GTK_BOX(GTK_DIALOG(popup)->vbox),
				   entry, true, true, 0);
	gtk_widget_show_all(popup);
	response = gtk_dialog_run (GTK_DIALOG(popup));

	if (response == GTK_RESPONSE_OK) {
		int rc;
		if (global_edit_error)
			temp = global_edit_error_msg;
		else if (edit_type == EDIT_REMOVE_PART) {
			delete_part_msg_t part_del_msg;
			part_del_msg.name = partid;
			rc = slurm_delete_partition(&part_del_msg);
			if (rc == SLURM_SUCCESS) {
				temp = g_strdup_printf(
					"Partition %s removed successfully",
					partid);
			} else {
				temp = g_strdup_printf(
					"Problem removing partition %s: %s",
					partid, slurm_strerror(rc));
				global_multi_error = true;
			}
		} else if (!global_send_update_msg) {
			temp = g_strdup_printf("No change detected.");
		} else if ((rc = slurm_update_partition(part_msg))
			   == SLURM_SUCCESS) {
			temp = g_strdup_printf(
				"Partition %s updated successfully",
				partid);
		} else {
			temp = g_strdup_printf(
				"Problem updating partition %s: %s",
				partid, slurm_strerror(rc));
			global_multi_error = true;
		}
		display_edit_note(temp);
		g_free(temp);
	}
end_it:

	g_free(partid);
	g_free(nodelist);
	global_entry_changed = 0;
	slurm_free_update_part_msg(part_msg);
	gtk_widget_destroy(popup);
	if (got_edit_signal) {
		type = got_edit_signal;
		got_edit_signal = NULL;
		admin_part(model, iter, type);
		xfree(type);
	}
	if (got_features_edit_signal) {
		type = "Update Features";
		admin_part(model, iter, type);
	}
	return;
}

extern void cluster_change_part(void)
{
	display_data_t *display_data = display_data_part;
	while (display_data++) {
		if (display_data->id == -1)
			break;
		if (cluster_flags & CLUSTER_FLAG_FED) {
			switch(display_data->id) {
			case SORTID_CLUSTER_NAME:
				display_data->show = true;
				break;
			}
		} else {
			switch (display_data->id) {
			case SORTID_CLUSTER_NAME:
				display_data->show = false;
				break;
			}
		}
	}

	get_info_part(NULL, NULL);
}
