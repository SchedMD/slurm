/*****************************************************************************\
 *  node_info.c - Functions related to node display
 *  mode of sview.
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *
 *  CODE-OCEC-09-009. All rights reserved.
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

#include "src/sview/sview.h"

#define _DEBUG 0
#define ECLIPSE_RT 0

int cpus_per_node = 1;
int g_node_scaling = 1;
//static int _l_topo_color_ndx = MAKE_TOPO_1;
//static int _l_sw_color_ndx = 0;

/* These need to be in alpha order (except POS and CNT) */
enum {
	SORTID_POS = POS_LOC,
	SORTID_ARCH,
	SORTID_BASE_WATTS,
	SORTID_BOARDS,
	SORTID_BOOT_TIME,
	SORTID_COLOR,
	SORTID_CPUS,
	SORTID_CPU_LOAD,
	SORTID_CONSUMED_ENERGY,
	SORTID_CORES,
	SORTID_CURRENT_WATTS,
	SORTID_ERR_CPUS,
	SORTID_FEATURES,
	SORTID_GRES,
	SORTID_IDLE_CPUS,
	SORTID_NAME,
	SORTID_NODE_ADDR,
	SORTID_NODE_HOSTNAME,
	SORTID_MEMORY,	/* RealMemory */
	SORTID_REASON,
	SORTID_RACK_MP,
	SORTID_SLURMD_START_TIME,
	SORTID_SOCKETS,
	SORTID_STATE,
	SORTID_STATE_NUM,
	SORTID_THREADS,
	SORTID_DISK,	/* TmpDisk */
	SORTID_UPDATED,
	SORTID_USED_CPUS,
	SORTID_USED_MEMORY,
	SORTID_VERSION,
	SORTID_WEIGHT,
	SORTID_CNT
};



typedef struct {
	int node_col;
	char *nodelist;
} process_node_t;

/*these are the settings to apply for the user
 * on the first startup after a fresh slurm install.*/
static char *_initial_page_opts = "Name,RackMidplane,State,CPU_Count,"
	"Used_CPU_Count,Error_CPU_Count,CoresPerSocket,Sockets,ThreadsPerCore,"
	"Real_Memory,Tmp_Disk";

static display_data_t display_data_node[] = {
	{G_TYPE_INT, SORTID_POS, NULL, FALSE, EDIT_NONE, refresh_node,
	 create_model_node, admin_edit_node},
	{G_TYPE_STRING, SORTID_NAME, "Name", FALSE, EDIT_NONE, refresh_node,
	 create_model_node, admin_edit_node},
	{G_TYPE_STRING, SORTID_COLOR, NULL, TRUE, EDIT_COLOR, refresh_node,
	 create_model_node, admin_edit_node},
#ifdef HAVE_BG
	{G_TYPE_STRING, SORTID_RACK_MP, "RackMidplane", FALSE, EDIT_NONE,
	 refresh_node, create_model_node, admin_edit_node},
#else
	{G_TYPE_STRING, SORTID_RACK_MP, NULL, TRUE, EDIT_NONE, refresh_node,
	 create_model_node, admin_edit_node},
#endif
	{G_TYPE_STRING, SORTID_NODE_ADDR, "NodeAddr", FALSE, EDIT_NONE,
	 refresh_node, create_model_node, admin_edit_node},
	{G_TYPE_STRING, SORTID_NODE_HOSTNAME, "NodeHostName", FALSE, EDIT_NONE,
	 refresh_node, create_model_node, admin_edit_node},
	{G_TYPE_STRING, SORTID_STATE, "State", FALSE, EDIT_MODEL, refresh_node,
	 create_model_node, admin_edit_node},
	{G_TYPE_INT, SORTID_STATE_NUM, NULL, FALSE, EDIT_NONE, refresh_node,
	 create_model_node, admin_edit_node},
	{G_TYPE_STRING, SORTID_CPUS, "CPU Count", FALSE,
	 EDIT_NONE, refresh_node, create_model_node, admin_edit_node},
	{G_TYPE_STRING, SORTID_USED_CPUS, "Used CPU Count", FALSE,
	 EDIT_NONE, refresh_node, create_model_node, admin_edit_node},
	{G_TYPE_STRING, SORTID_ERR_CPUS, "Error CPU Count", FALSE,
	 EDIT_NONE, refresh_node, create_model_node, admin_edit_node},
	{G_TYPE_STRING, SORTID_IDLE_CPUS, "Idle CPU Count", FALSE,
	 EDIT_NONE, refresh_node, create_model_node, admin_edit_node},
	{G_TYPE_INT, SORTID_BOARDS, "Boards", FALSE,
	 EDIT_NONE, refresh_node, create_model_node, admin_edit_node},
	{G_TYPE_INT, SORTID_SOCKETS, "Sockets", FALSE,
	 EDIT_NONE, refresh_node, create_model_node, admin_edit_node},
	{G_TYPE_INT, SORTID_CORES, "CoresPerSocket", FALSE,
	 EDIT_NONE, refresh_node, create_model_node, admin_edit_node},
	{G_TYPE_INT, SORTID_THREADS, "ThreadsPerCore", FALSE,
	 EDIT_NONE, refresh_node, create_model_node, admin_edit_node},
	{G_TYPE_STRING, SORTID_MEMORY, "Real Memory", FALSE,
	 EDIT_NONE, refresh_node, create_model_node, admin_edit_node},
	{G_TYPE_STRING, SORTID_USED_MEMORY, "Used Memory", FALSE,
	 EDIT_NONE, refresh_node, create_model_node, admin_edit_node},
	{G_TYPE_STRING, SORTID_DISK, "Tmp Disk", FALSE, EDIT_NONE, refresh_node,
	 create_model_node, admin_edit_node},
	{G_TYPE_INT, SORTID_WEIGHT,"Weight", FALSE, EDIT_NONE, refresh_node,
	 create_model_node, admin_edit_node},
	{G_TYPE_STRING, SORTID_CPU_LOAD, "CPU Load", FALSE, EDIT_NONE,
	 refresh_node, create_model_node, admin_edit_node},
	{G_TYPE_STRING, SORTID_ARCH, "Arch", FALSE,
	 EDIT_NONE, refresh_node, create_model_node, admin_edit_node},
	{G_TYPE_STRING, SORTID_FEATURES, "Features", FALSE,
	 EDIT_TEXTBOX, refresh_node, create_model_node, admin_edit_node},
	{G_TYPE_STRING, SORTID_GRES, "Gres", FALSE,
	 EDIT_TEXTBOX, refresh_node, create_model_node, admin_edit_node},
	{G_TYPE_STRING, SORTID_BOOT_TIME, "BootTime", FALSE,
	 EDIT_NONE, refresh_node, create_model_node, admin_edit_node},
	{G_TYPE_STRING, SORTID_SLURMD_START_TIME, "SlurmdStartTime", FALSE,
	 EDIT_NONE, refresh_node, create_model_node, admin_edit_node},
	{G_TYPE_STRING, SORTID_REASON, "Reason", FALSE,
	 EDIT_NONE, refresh_node, create_model_node, admin_edit_node},
	{G_TYPE_STRING, SORTID_BASE_WATTS, "Lowest Joules", FALSE,
	 EDIT_NONE, refresh_node, create_model_node, admin_edit_node},
	{G_TYPE_STRING, SORTID_CONSUMED_ENERGY,"Consumed Joules", FALSE,
	 EDIT_NONE, refresh_node, create_model_node, admin_edit_node},
	{G_TYPE_STRING, SORTID_CURRENT_WATTS, "Current Watts", FALSE,
	 EDIT_NONE, refresh_node, create_model_node, admin_edit_node},
	{G_TYPE_STRING, SORTID_VERSION, "Version", FALSE,
	 EDIT_NONE, refresh_node, create_model_node, admin_edit_node},
	{G_TYPE_INT, SORTID_UPDATED, NULL, FALSE, EDIT_NONE, refresh_node,
	 create_model_node, admin_edit_node},
	{G_TYPE_NONE, -1, NULL, FALSE, EDIT_NONE}
};

static display_data_t options_data_node[] = {
	{G_TYPE_INT, SORTID_POS, NULL, FALSE, EDIT_NONE},
	{G_TYPE_STRING, INFO_PAGE, "Full Info", TRUE, NODE_PAGE},
#ifdef HAVE_BG
	{G_TYPE_STRING, NODE_PAGE, "Drain Midplane", TRUE, ADMIN_PAGE},
	{G_TYPE_STRING, NODE_PAGE, "Undrain Midplane", TRUE, ADMIN_PAGE},
	{G_TYPE_STRING, NODE_PAGE, "Resume Midplane", TRUE, ADMIN_PAGE},
	{G_TYPE_STRING, NODE_PAGE, "Set Midplane Down",
	 TRUE, ADMIN_PAGE},
	{G_TYPE_STRING, NODE_PAGE, "Make Midplane Idle",
	 TRUE, ADMIN_PAGE},
#else
	{G_TYPE_STRING, NODE_PAGE, "Drain Node", TRUE, ADMIN_PAGE},
	{G_TYPE_STRING, NODE_PAGE, "Undrain Node", TRUE, ADMIN_PAGE},
	{G_TYPE_STRING, NODE_PAGE, "Resume Node", TRUE, ADMIN_PAGE},
	{G_TYPE_STRING, NODE_PAGE, "Set Node(s) Down", TRUE, ADMIN_PAGE},
	{G_TYPE_STRING, NODE_PAGE, "Make Node(s) Idle", TRUE, ADMIN_PAGE},
#endif
	{G_TYPE_STRING, NODE_PAGE, "Update Features", TRUE, ADMIN_PAGE},
	{G_TYPE_STRING, NODE_PAGE, "Update Gres", TRUE, ADMIN_PAGE},
	{G_TYPE_STRING, JOB_PAGE,  "Jobs", TRUE, NODE_PAGE},
#ifdef HAVE_BG
	{G_TYPE_STRING, BLOCK_PAGE, "Blocks", TRUE, NODE_PAGE},
#else
	{G_TYPE_STRING, BLOCK_PAGE, NULL, TRUE, NODE_PAGE},
#endif
	{G_TYPE_STRING, PART_PAGE, "Partitions", TRUE, NODE_PAGE},
	{G_TYPE_STRING, RESV_PAGE, "Reservations", TRUE, NODE_PAGE},
	//{G_TYPE_STRING, SUBMIT_PAGE, "Job Submit", FALSE, NODE_PAGE},
	{G_TYPE_NONE, -1, NULL, FALSE, EDIT_NONE}
};

static display_data_t *local_display_data = NULL;
static GtkTreeModel *last_model = NULL;

static void _layout_node_record(GtkTreeView *treeview,
				sview_node_info_t *sview_node_info_ptr,
				int update)
{
	char tmp_cnt[50];
	char tmp_current_watts[50];
	char tmp_base_watts[50];
	char tmp_consumed_energy[50];
	char tmp_version[50];
	char *upper = NULL, *lower = NULL;
	GtkTreeIter iter;
	uint16_t err_cpus = 0, alloc_cpus = 0;
	uint32_t alloc_memory = 0;
	node_info_t *node_ptr = sview_node_info_ptr->node_ptr;
	int idle_cpus = node_ptr->cpus;
	GtkTreeStore *treestore =
		GTK_TREE_STORE(gtk_tree_view_get_model(treeview));
	if (!treestore)
		return;

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_node,
						 SORTID_NAME),
				   node_ptr->name);

	if (sview_node_info_ptr->rack_mp)
		add_display_treestore_line(update, treestore, &iter,
					   find_col_name(display_data_node,
							 SORTID_RACK_MP),
					   sview_node_info_ptr->rack_mp);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_node,
						 SORTID_NODE_ADDR),
				   node_ptr->node_addr);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_node,
						 SORTID_NODE_HOSTNAME),
				   node_ptr->node_hostname);

	convert_num_unit((float)node_ptr->cpus, tmp_cnt, sizeof(tmp_cnt),
			 UNIT_NONE);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_node,
						 SORTID_CPUS),
				   tmp_cnt);

	if (node_ptr->cpu_load == NO_VAL) {
		snprintf(tmp_cnt, sizeof(tmp_cnt), "N/A");
	} else {
		snprintf(tmp_cnt, sizeof(tmp_cnt), "%.2f",
			 (node_ptr->cpu_load / 100.0));
	}
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_node,
						 SORTID_CPU_LOAD),
				   tmp_cnt);

	select_g_select_nodeinfo_get(node_ptr->select_nodeinfo,
				     SELECT_NODEDATA_SUBCNT,
				     NODE_STATE_ALLOCATED,
				     &alloc_cpus);
	if (cluster_flags & CLUSTER_FLAG_BG) {
		if (!alloc_cpus
		    && ((node_ptr->node_state & NODE_STATE_ALLOCATED)
			||  (node_ptr->node_state & NODE_STATE_COMPLETING)))
			alloc_cpus = node_ptr->cpus;
		else
			alloc_cpus *= cpus_per_node;
	}
	idle_cpus -= alloc_cpus;
	convert_num_unit((float)alloc_cpus, tmp_cnt,
			 sizeof(tmp_cnt), UNIT_NONE);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_node,
						 SORTID_USED_CPUS),
				   tmp_cnt);

	select_g_select_nodeinfo_get(node_ptr->select_nodeinfo,
				     SELECT_NODEDATA_SUBCNT,
				     NODE_STATE_ERROR,
				     &err_cpus);

	if (cluster_flags & CLUSTER_FLAG_BG)
		err_cpus *= cpus_per_node;

	idle_cpus -= err_cpus;
	convert_num_unit((float)err_cpus, tmp_cnt, sizeof(tmp_cnt), UNIT_NONE);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_node,
						 SORTID_ERR_CPUS),
				   tmp_cnt);

	convert_num_unit((float)idle_cpus, tmp_cnt, sizeof(tmp_cnt), UNIT_NONE);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_node,
						 SORTID_IDLE_CPUS),
				   tmp_cnt);

	upper = node_state_string(node_ptr->node_state);
	lower = str_tolower(upper);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_node,
						 SORTID_STATE),
				   lower);
	xfree(lower);

	convert_num_unit((float)node_ptr->boards, tmp_cnt, sizeof(tmp_cnt),
			 UNIT_NONE);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_node,
						 SORTID_BOARDS),
				   tmp_cnt);

	convert_num_unit((float)node_ptr->sockets, tmp_cnt, sizeof(tmp_cnt),
			 UNIT_NONE);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_node,
						 SORTID_SOCKETS),
				   tmp_cnt);

	convert_num_unit((float)node_ptr->cores, tmp_cnt, sizeof(tmp_cnt),
			 UNIT_NONE);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_node,
						 SORTID_CORES),
				   tmp_cnt);

	convert_num_unit((float)node_ptr->threads, tmp_cnt, sizeof(tmp_cnt),
			 UNIT_NONE);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_node,
						 SORTID_THREADS),
				   tmp_cnt);

	convert_num_unit((float)node_ptr->real_memory, tmp_cnt, sizeof(tmp_cnt),
			 UNIT_MEGA);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_node,
						 SORTID_MEMORY),
				   tmp_cnt);

	select_g_select_nodeinfo_get(node_ptr->select_nodeinfo,
				     SELECT_NODEDATA_MEM_ALLOC,
				     NODE_STATE_ALLOCATED,
				     &alloc_memory);
	snprintf(tmp_cnt, sizeof(tmp_cnt), "%uM", alloc_memory);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_node,
						 SORTID_USED_MEMORY),
				   tmp_cnt);

	convert_num_unit((float)node_ptr->tmp_disk, tmp_cnt, sizeof(tmp_cnt),
			 UNIT_MEGA);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_node,
						 SORTID_DISK),
				   tmp_cnt);
	snprintf(tmp_cnt, sizeof(tmp_cnt), "%u", node_ptr->weight);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_node,
						 SORTID_WEIGHT),
				   tmp_cnt);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_node,
						 SORTID_ARCH),
				   node_ptr->arch);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_node,
						 SORTID_FEATURES),
				   node_ptr->features);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_node,
						 SORTID_GRES),
				   node_ptr->gres);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_node,
						 SORTID_BOOT_TIME),
				   sview_node_info_ptr->boot_time);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_node,
						 SORTID_SLURMD_START_TIME),
				   sview_node_info_ptr->slurmd_start_time);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_node,
						 SORTID_REASON),
				   sview_node_info_ptr->reason);

	if (node_ptr->energy->current_watts == NO_VAL) {
		snprintf(tmp_current_watts, sizeof(tmp_current_watts),
			 "N/A");
		snprintf(tmp_base_watts, sizeof(tmp_base_watts),
			 "N/A");
		snprintf(tmp_consumed_energy, sizeof(tmp_consumed_energy),
			 "N/A");
	} else {
		snprintf(tmp_current_watts, sizeof(tmp_current_watts),
			 "%u", node_ptr->energy->current_watts);
		snprintf(tmp_base_watts, sizeof(tmp_base_watts),
			 "%u", node_ptr->energy->base_watts);
		snprintf(tmp_consumed_energy, sizeof(tmp_consumed_energy),
			 "%u", node_ptr->energy->consumed_energy);
	}
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_node,
						 SORTID_BASE_WATTS),
				   tmp_base_watts);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_node,
						 SORTID_CONSUMED_ENERGY),
				   tmp_consumed_energy);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_node,
						 SORTID_CURRENT_WATTS),
				   tmp_current_watts);

	if (node_ptr->version == NULL) {
		snprintf(tmp_version, sizeof(tmp_version), "N/A");
	} else {
		snprintf(tmp_version, sizeof(tmp_version), "%s",
			 node_ptr->version);
	}
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_node,
						 SORTID_VERSION),
				   tmp_version);
	return;
}

static void _update_node_record(sview_node_info_t *sview_node_info_ptr,
				GtkTreeStore *treestore)
{
	uint16_t alloc_cpus = 0, err_cpus = 0, idle_cpus;
	uint32_t alloc_memory;
	node_info_t *node_ptr = sview_node_info_ptr->node_ptr;
	char tmp_disk[20], tmp_cpus[20], tmp_err_cpus[20], tmp_idle_cpus[20];
	char tmp_mem[20], tmp_used_memory[20];
	char tmp_used_cpus[20], tmp_cpu_load[20];
	char tmp_current_watts[50], tmp_base_watts[50], tmp_consumed_energy[50];
	char tmp_version[50];
	char *tmp_state_lower, *tmp_state_upper;


	if (node_ptr->energy->current_watts == NO_VAL) {
		snprintf(tmp_current_watts, sizeof(tmp_current_watts),
			 "N/A");
		snprintf(tmp_base_watts, sizeof(tmp_base_watts),
			 "N/A");
		snprintf(tmp_consumed_energy, sizeof(tmp_consumed_energy),
			 "N/A");
	} else {
		snprintf(tmp_current_watts, sizeof(tmp_current_watts),
			 "%u ", node_ptr->energy->current_watts);
		snprintf(tmp_base_watts, sizeof(tmp_base_watts),
			 "%u", node_ptr->energy->base_watts);
		snprintf(tmp_consumed_energy, sizeof(tmp_consumed_energy),
			 "%u", node_ptr->energy->consumed_energy);
	}

	if (node_ptr->cpu_load == NO_VAL) {
		strcpy(tmp_cpu_load, "N/A");
	} else {
		snprintf(tmp_cpu_load, sizeof(tmp_cpu_load),
			 "%.2f", (node_ptr->cpu_load / 100.0));
	}

	convert_num_unit((float)node_ptr->cpus, tmp_cpus,
			 sizeof(tmp_cpus), UNIT_NONE);

	select_g_select_nodeinfo_get(node_ptr->select_nodeinfo,
				     SELECT_NODEDATA_SUBCNT,
				     NODE_STATE_ALLOCATED,
				     &alloc_cpus);
	if (cluster_flags & CLUSTER_FLAG_BG) {
		if (!alloc_cpus &&
		    (IS_NODE_ALLOCATED(node_ptr) ||
		     IS_NODE_COMPLETING(node_ptr)))
			alloc_cpus = node_ptr->cpus;
		else
			alloc_cpus *= cpus_per_node;
	}
	idle_cpus = node_ptr->cpus - alloc_cpus;
	convert_num_unit((float)alloc_cpus, tmp_used_cpus,
			 sizeof(tmp_used_cpus), UNIT_NONE);

	select_g_select_nodeinfo_get(node_ptr->select_nodeinfo,
				     SELECT_NODEDATA_MEM_ALLOC,
				     NODE_STATE_ALLOCATED,
				     &alloc_memory);
	snprintf(tmp_used_memory, sizeof(tmp_used_memory), "%uM", alloc_memory);

	convert_num_unit((float)alloc_cpus, tmp_used_cpus,
			 sizeof(tmp_used_cpus), UNIT_NONE);

	select_g_select_nodeinfo_get(node_ptr->select_nodeinfo,
				     SELECT_NODEDATA_SUBCNT,
				     NODE_STATE_ERROR,
				     &err_cpus);
	if (cluster_flags & CLUSTER_FLAG_BG)
		err_cpus *= cpus_per_node;
	idle_cpus -= err_cpus;
	convert_num_unit((float)err_cpus, tmp_err_cpus, sizeof(tmp_err_cpus),
			 UNIT_NONE);

	convert_num_unit((float)idle_cpus, tmp_idle_cpus, sizeof(tmp_idle_cpus),
			 UNIT_NONE);

	if (IS_NODE_DRAIN(node_ptr)) {
		/* don't worry about mixed since the
		 * whole node is being drained. */
	} else if ((alloc_cpus && err_cpus) ||
		   (idle_cpus  && (idle_cpus != node_ptr->cpus))) {
		node_ptr->node_state &= NODE_STATE_FLAGS;
		node_ptr->node_state |= NODE_STATE_MIXED;
	}
	tmp_state_upper = node_state_string(node_ptr->node_state);
	tmp_state_lower = str_tolower(tmp_state_upper);

	convert_num_unit((float)node_ptr->real_memory, tmp_mem, sizeof(tmp_mem),
			 UNIT_MEGA);

	convert_num_unit((float)node_ptr->tmp_disk, tmp_disk, sizeof(tmp_disk),
			 UNIT_MEGA);

	if (node_ptr->version == NULL) {
		snprintf(tmp_version, sizeof(tmp_version), "N/A");
	} else {
		snprintf(tmp_version, sizeof(tmp_version), "%s",
			 node_ptr->version);
	}

	/* Combining these records provides a slight performance improvement */
	gtk_tree_store_set(treestore, &sview_node_info_ptr->iter_ptr,
			   SORTID_ARCH,      node_ptr->arch,
			   SORTID_BASE_WATTS,tmp_base_watts,
			   SORTID_BOARDS,    node_ptr->boards,
			   SORTID_BOOT_TIME, sview_node_info_ptr->boot_time,
			   SORTID_COLOR,
				sview_colors[sview_node_info_ptr->pos
				% sview_colors_cnt],
			   SORTID_CONSUMED_ENERGY, tmp_consumed_energy,
			   SORTID_CORES,     node_ptr->cores,
			   SORTID_CPUS,      tmp_cpus,
			   SORTID_CURRENT_WATTS, tmp_current_watts,
			   SORTID_CPU_LOAD,  tmp_cpu_load,
			   SORTID_DISK,      tmp_disk,
			   SORTID_ERR_CPUS,  tmp_err_cpus,
			   SORTID_IDLE_CPUS, tmp_idle_cpus,
			   SORTID_FEATURES,  node_ptr->features,
			   SORTID_GRES,      node_ptr->gres,
			   SORTID_MEMORY,    tmp_mem,
			   SORTID_NAME,      node_ptr->name,
			   SORTID_NODE_ADDR, node_ptr->node_addr,
			   SORTID_NODE_HOSTNAME, node_ptr->node_hostname,
			   SORTID_RACK_MP,   sview_node_info_ptr->rack_mp,
			   SORTID_REASON,    sview_node_info_ptr->reason,
			   SORTID_SLURMD_START_TIME,
				sview_node_info_ptr->slurmd_start_time,
			   SORTID_SOCKETS,   node_ptr->sockets,
			   SORTID_STATE,     tmp_state_lower,
			   SORTID_STATE_NUM, node_ptr->node_state,
			   SORTID_THREADS,   node_ptr->threads,
			   SORTID_USED_CPUS, tmp_used_cpus,
			   SORTID_USED_MEMORY, tmp_used_memory,
			   SORTID_VERSION,   tmp_version,
			   SORTID_WEIGHT,    node_ptr->weight,
			   SORTID_UPDATED,   1,
			  -1);

	xfree(tmp_state_lower);

	return;
}

static void _append_node_record(sview_node_info_t *sview_node_info,
				GtkTreeStore *treestore)
{
	gtk_tree_store_append(treestore, &sview_node_info->iter_ptr, NULL);
	gtk_tree_store_set(treestore, &sview_node_info->iter_ptr, SORTID_POS,
			   sview_node_info->pos, -1);
	_update_node_record(sview_node_info, treestore);
}

static void _update_info_node(List info_list, GtkTreeView *tree_view)
{
	GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
	char *name = NULL;
	ListIterator itr = NULL;
	sview_node_info_t *sview_node_info = NULL;

	set_for_update(model, SORTID_UPDATED);

	itr = list_iterator_create(info_list);
	while ((sview_node_info = (sview_node_info_t*) list_next(itr))) {
		/* This means the tree_store changed (added new column
		 * or something). */
		if (last_model != model)
			sview_node_info->iter_set = false;

		if (sview_node_info->iter_set) {
			gtk_tree_model_get(model, &sview_node_info->iter_ptr,
					   SORTID_NAME, &name, -1);
			if (strcmp(name, sview_node_info->node_name)) {
				/* Bad pointer */
				sview_node_info->iter_set = false;
				//g_print("bad node iter pointer\n");
			}
			g_free(name);
		}
		if (sview_node_info->iter_set)
			_update_node_record(sview_node_info,
					    GTK_TREE_STORE(model));
		else {
			_append_node_record(sview_node_info,
					    GTK_TREE_STORE(model));
			sview_node_info->iter_set = true;
		}
	}
	list_iterator_destroy(itr);

	/* remove all old nodes */
	remove_old(model, SORTID_UPDATED);
	last_model = model;
}

static void _node_info_free(sview_node_info_t *sview_node_info)
{
	if (sview_node_info) {
		xfree(sview_node_info->slurmd_start_time);
		xfree(sview_node_info->boot_time);
		xfree(sview_node_info->node_name);
		xfree(sview_node_info->rack_mp);
		xfree(sview_node_info->reason);
	}
}

static void _node_info_list_del(void *object)
{
	sview_node_info_t *sview_node_info = (sview_node_info_t *)object;

	if (sview_node_info) {
		_node_info_free(sview_node_info);
		xfree(sview_node_info);
	}
}

static void _display_info_node(List info_list,	popup_info_t *popup_win)
{
	specific_info_t *spec_info = popup_win->spec_info;
	char *name = (char *)spec_info->search_info->gchar_data;
	int found = 0;
	node_info_t *node_ptr = NULL;
	GtkTreeView *treeview = NULL;
	int update = 0;
	ListIterator itr = NULL;
	sview_node_info_t *sview_node_info = NULL;
	int i = -1;

	if (!spec_info->search_info->gchar_data) {
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
	while ((sview_node_info = (sview_node_info_t*) list_next(itr))) {
		node_ptr = sview_node_info->node_ptr;
		i++;
		if (!strcmp(node_ptr->name, name)) {
			change_grid_color(popup_win->grid_button_list,
					  i, i, i, true, 0);
			_layout_node_record(treeview, sview_node_info, update);
			found = 1;
			break;
		}
	}
	list_iterator_destroy(itr);
	if (!found) {
		if (!popup_win->not_found) {
			char *temp;
			GtkTreeIter iter;
			GtkTreeModel *model = NULL;

			if (cluster_flags & CLUSTER_FLAG_BG)
				temp = "MIDPLANE NOT FOUND\n";
			else
				temp = "NODE NOT FOUND\n";
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

static void _selected_page(GtkMenuItem *menuitem,
			   display_data_t *display_data)
{
	switch(display_data->extra) {
	case NODE_PAGE:
		popup_all_node_name(display_data->user_data, display_data->id);
		break;
	case ADMIN_PAGE:
		admin_node_name(display_data->user_data,
				NULL, display_data->name);
		break;
	default:
		g_print("node got %d %d\n", display_data->extra,
			display_data->id);
	}

}

static void _process_each_node(GtkTreeModel *model, GtkTreePath *path,
			       GtkTreeIter *iter, gpointer userdata)
{
	char *name = NULL;
	process_node_t *process_node = userdata;

	gtk_tree_model_get(model, iter, process_node->node_col, &name, -1);
	if (process_node->nodelist)
		xstrfmtcat(process_node->nodelist, ",%s", name);
	else
		process_node->nodelist = xstrdup(name);
	g_free(name);
}
/*process_each_node ^^^*/



extern void refresh_node(GtkAction *action, gpointer user_data)
{
	popup_info_t *popup_win = (popup_info_t *)user_data;
	xassert(popup_win);
	xassert(popup_win->spec_info);
	xassert(popup_win->spec_info->title);
	popup_win->force_refresh = 1;
	specific_info_node(popup_win);
}

/* don't destroy the list from this function */
extern List create_node_info_list(node_info_msg_t *node_info_ptr,
				  bool by_partition)
{
	static List info_list = NULL;
	static node_info_msg_t *last_node_info_ptr = NULL;
	List last_list = NULL;
	ListIterator last_list_itr = NULL;
	int i = 0;
	sview_node_info_t *sview_node_info_ptr = NULL;
	node_info_t *node_ptr = NULL;
	char user[32], time_str[32];

	if (!by_partition) {
		if (!node_info_ptr
		    || (info_list && (node_info_ptr == last_node_info_ptr)))
			goto update_color;
	}

	last_node_info_ptr = node_info_ptr;

	if (info_list)
		last_list = info_list;

	info_list = list_create(_node_info_list_del);
	if (!info_list) {
		g_print("malloc error\n");
		return NULL;
	}

	if (last_list)
		last_list_itr = list_iterator_create(last_list);
	for (i=0; i<node_info_ptr->record_count; i++) {
		char *select_reason_str = NULL;
		node_ptr = &(node_info_ptr->node_array[i]);

		if (!node_ptr->name || (node_ptr->name[0] == '\0'))
			continue;

		sview_node_info_ptr = NULL;

		if (last_list_itr) {
			while ((sview_node_info_ptr =
				list_next(last_list_itr))) {
				if (!strcmp(sview_node_info_ptr->node_name,
					    node_ptr->name)) {
					list_remove(last_list_itr);
					_node_info_free(sview_node_info_ptr);
					break;
				}
			}
			list_iterator_reset(last_list_itr);
		}

		/* constrain list to included partitions' nodes */
		/* and there are excluded values to process */
		/* and user has not requested to show hidden */
		/* if (by_partition && apply_partition_check */
		/*     && !working_sview_config.show_hidden */
		/*     && !check_part_includes_node(i)) */
		/* 	continue; */

		if (!sview_node_info_ptr)
			sview_node_info_ptr =
				xmalloc(sizeof(sview_node_info_t));
		list_append(info_list, sview_node_info_ptr);
		sview_node_info_ptr->node_name = xstrdup(node_ptr->name);
		sview_node_info_ptr->node_ptr = node_ptr;
		sview_node_info_ptr->pos = i;

		slurm_get_select_nodeinfo(node_ptr->select_nodeinfo,
					  SELECT_NODEDATA_RACK_MP,
					  0, &sview_node_info_ptr->rack_mp);

		if (node_ptr->reason &&
		    (node_ptr->reason_uid != NO_VAL) && node_ptr->reason_time) {
			struct passwd *pw = NULL;

			if ((pw=getpwuid(node_ptr->reason_uid)))
				snprintf(user, sizeof(user), "%s", pw->pw_name);
			else
				snprintf(user, sizeof(user), "Unk(%u)",
					 node_ptr->reason_uid);
			slurm_make_time_str(&node_ptr->reason_time,
					    time_str, sizeof(time_str));
			sview_node_info_ptr->reason = xstrdup_printf(
				"%s [%s@%s]", node_ptr->reason, user, time_str);
		} else if (node_ptr->reason)
			sview_node_info_ptr->reason = xstrdup(node_ptr->reason);

		slurm_get_select_nodeinfo(node_ptr->select_nodeinfo,
					  SELECT_NODEDATA_EXTRA_INFO,
					  0, &select_reason_str);
		if (select_reason_str && select_reason_str[0]) {
			if (sview_node_info_ptr->reason)
				xstrfmtcat(sview_node_info_ptr->reason, "\n%s",
					   select_reason_str);
			else {
				sview_node_info_ptr->reason = select_reason_str;
				select_reason_str = NULL;
			}
		}
		xfree(select_reason_str);


		if (node_ptr->boot_time) {
			slurm_make_time_str(&node_ptr->boot_time,
					    time_str, sizeof(time_str));
			sview_node_info_ptr->boot_time = xstrdup(time_str);
		}
		if (node_ptr->slurmd_start_time) {
			slurm_make_time_str(&node_ptr->slurmd_start_time,
					    time_str, sizeof(time_str));
			sview_node_info_ptr->slurmd_start_time =
				xstrdup(time_str);
		}
	}

	if (last_list) {
		list_iterator_destroy(last_list_itr);
		list_destroy(last_list);
	}

update_color:

	return info_list;
}

extern int get_new_info_node(node_info_msg_t **info_ptr, int force)
{
	node_info_msg_t *new_node_ptr = NULL;
	uint16_t show_flags = 0;
	int error_code = SLURM_NO_CHANGE_IN_DATA;
	time_t now = time(NULL), delay;
	static time_t last;
	static bool changed = 0;
	static uint16_t last_flags = 0;

	delay = now - last;
	if (delay < 2) {
		/* Avoid re-loading node information within 2 secs as the data
		 * may still be in use. If we load new node data and free the
		 * old data while it it still in use, the result is likely
		 * invalid memory references. */
		force = 0;
		/* FIXME: Add an "in use" flag, copy the data, or otherwise
		 * permit the timely loading of new node information. */
	}

	if (g_node_info_ptr && !force &&
	    (delay < working_sview_config.refresh_delay)) {
		if (*info_ptr != g_node_info_ptr)
			error_code = SLURM_SUCCESS;
		*info_ptr = g_node_info_ptr;
		return error_code;
	}
	last = now;

	//if (working_sview_config.show_hidden)
	show_flags |= SHOW_ALL;
	if (g_node_info_ptr) {
		if (show_flags != last_flags)
			g_node_info_ptr->last_update = 0;
		error_code = slurm_load_node(g_node_info_ptr->last_update,
					     &new_node_ptr, show_flags);
		if (error_code == SLURM_SUCCESS) {
			slurm_free_node_info_msg(g_node_info_ptr);
			changed = 1;
		} else if (slurm_get_errno() == SLURM_NO_CHANGE_IN_DATA) {
			error_code = SLURM_NO_CHANGE_IN_DATA;
			new_node_ptr = g_node_info_ptr;
			changed = 0;
		}
	} else {
		new_node_ptr = NULL;
		error_code = slurm_load_node((time_t) NULL, &new_node_ptr,
					     show_flags);
		changed = 1;
	}

	last_flags = show_flags;
	g_node_info_ptr = new_node_ptr;

	if (g_node_info_ptr && (*info_ptr != g_node_info_ptr))
		error_code = SLURM_SUCCESS;

 	if (new_node_ptr && new_node_ptr->node_array && changed) {
		int i;
		node_info_t *node_ptr = NULL;
		uint16_t err_cpus = 0, alloc_cpus = 0;
		int idle_cpus;

		g_node_scaling = new_node_ptr->node_scaling;
		cpus_per_node =
			new_node_ptr->node_array[0].cpus / g_node_scaling;
		for (i=0; i<g_node_info_ptr->record_count; i++) {
			node_ptr = &(g_node_info_ptr->node_array[i]);
			if (!node_ptr->name || (node_ptr->name[0] == '\0'))
				continue;	/* bad node */
			idle_cpus = node_ptr->cpus;

			slurm_get_select_nodeinfo(
				node_ptr->select_nodeinfo,
				SELECT_NODEDATA_SUBCNT,
				NODE_STATE_ALLOCATED,
				&alloc_cpus);
			if (cluster_flags & CLUSTER_FLAG_BG) {
				if (!alloc_cpus
				    && (IS_NODE_ALLOCATED(node_ptr)
					|| IS_NODE_COMPLETING(node_ptr)))
					alloc_cpus = node_ptr->cpus;
				else
					alloc_cpus *= cpus_per_node;
			}
			idle_cpus -= alloc_cpus;

			slurm_get_select_nodeinfo(
				node_ptr->select_nodeinfo,
				SELECT_NODEDATA_SUBCNT,
				NODE_STATE_ERROR,
				&err_cpus);
			if (cluster_flags & CLUSTER_FLAG_BG)
				err_cpus *= cpus_per_node;

			idle_cpus -= err_cpus;

			if (IS_NODE_DRAIN(node_ptr)) {
				/* don't worry about mixed since the
				   whole node is being drained. */
			} else if ((alloc_cpus && err_cpus) ||
				   (idle_cpus &&
				    (idle_cpus != node_ptr->cpus))) {
				node_ptr->node_state &= NODE_STATE_FLAGS;
				if (err_cpus)
					node_ptr->node_state |= NODE_STATE_ERROR;
				node_ptr->node_state |= NODE_STATE_MIXED;
			} else if (err_cpus) {
				node_ptr->node_state &= NODE_STATE_FLAGS;
				node_ptr->node_state |= NODE_STATE_ERROR;
			}

/* 			if (alloc_cpus && err_cpus && !idle_cpus) { */
/* 				node_ptr->node_state &= NODE_STATE_FLAGS; */
/* 				node_ptr->node_state |= NODE_STATE_AE; */
/* 			} else if (alloc_cpus && err_cpus && idle_cpus) { */
/* 				node_ptr->node_state &= NODE_STATE_FLAGS; */
/* 				node_ptr->node_state |= NODE_STATE_AEI; */
/* 			} else if (alloc_cpus && !err_cpus && idle_cpus) { */
/* 				node_ptr->node_state &= NODE_STATE_FLAGS; */
/* 				node_ptr->node_state |= NODE_STATE_AI; */
/* 			} else if (!alloc_cpus && err_cpus && idle_cpus) { */
/* 				node_ptr->node_state &= NODE_STATE_FLAGS; */
/* 				node_ptr->node_state |= NODE_STATE_EI; */
/* 			} */
		}
	}

	*info_ptr = g_node_info_ptr;

	if (!g_topo_info_msg_ptr &&
	    default_sview_config.grid_topological) {
		get_topo_conf(); /* pull in topology NOW */
	}

	return error_code;
}

extern int update_features_node(GtkDialog *dialog, const char *nodelist,
				const char *old_features)
{
	char tmp_char[100];
	char *edit = NULL;
	GtkWidget *entry = NULL;
	GtkWidget *label = NULL;
	update_node_msg_t *node_msg = xmalloc(sizeof(update_node_msg_t));
	int response = 0;
	int no_dialog = 0;
	int rc = SLURM_SUCCESS;


	if (_DEBUG)
		g_print("update_features_node:global_row_count: %d "
			"node_names %s\n",
			global_row_count, nodelist);
	if (!dialog) {
		snprintf(tmp_char, sizeof(tmp_char),
			 "Update Features for Node(s) %s?",
			 nodelist);

		dialog = GTK_DIALOG(
			gtk_dialog_new_with_buttons(
				tmp_char,
				GTK_WINDOW(main_window),
				GTK_DIALOG_MODAL
				| GTK_DIALOG_DESTROY_WITH_PARENT,
				NULL));
		no_dialog = 1;
	}
	label = gtk_dialog_add_button(dialog,
				      GTK_STOCK_YES, GTK_RESPONSE_OK);
	gtk_window_set_default(GTK_WINDOW(dialog), label);
	gtk_dialog_add_button(dialog,
			      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);

	slurm_init_update_node_msg(node_msg);
	node_msg->node_names = xstrdup(nodelist);

	snprintf(tmp_char, sizeof(tmp_char),
		 "Features for Node(s) %s?", nodelist);
	label = gtk_label_new(tmp_char);
	gtk_box_pack_start(GTK_BOX(dialog->vbox),
			   label, FALSE, FALSE, 0);

	entry = create_entry();
	if (!entry)
		goto end_it;

	if (old_features)
		gtk_entry_set_text(GTK_ENTRY(entry), old_features);

	gtk_box_pack_start(GTK_BOX(dialog->vbox), entry, TRUE, TRUE, 0);
	gtk_widget_show_all(GTK_WIDGET(dialog));

	response = gtk_dialog_run(dialog);
	if (response == GTK_RESPONSE_OK) {
		node_msg->features =
			xstrdup(gtk_entry_get_text(GTK_ENTRY(entry)));
		if (!node_msg->features) {
			edit = g_strdup_printf("No features given.");
			display_edit_note(edit);
			g_free(edit);
			goto end_it;
		}
		if ((rc = slurm_update_node(node_msg) == SLURM_SUCCESS)) {
			edit = g_strdup_printf(
				"Node(s) %s updated successfully.",
				nodelist);
			display_edit_note(edit);
			g_free(edit);

		} else {
			edit = g_strdup_printf(
				"Problem updating node(s) %s: %s",
				nodelist, slurm_strerror(rc));
			display_edit_note(edit);
			g_free(edit);
		}
	}

end_it:
	slurm_free_update_node_msg(node_msg);
	if (no_dialog)
		gtk_widget_destroy(GTK_WIDGET(dialog));

	return rc;
}

extern int update_gres_node(GtkDialog *dialog, const char *nodelist,
			    const char *old_gres)
{
	char tmp_char[100];
	char *edit = NULL;
	GtkWidget *entry = NULL;
	GtkWidget *label = NULL;
	update_node_msg_t *node_msg = xmalloc(sizeof(update_node_msg_t));
	int response = 0;
	int no_dialog = 0;
	int rc = SLURM_SUCCESS;

	if (_DEBUG)
		g_print("update_gres_node:global_row_count:"
			" %d node_names %s\n",
			global_row_count, nodelist);
	if (!dialog) {
		snprintf(tmp_char, sizeof(tmp_char),
			 "Update Gres for Node(s) %s?",
			 nodelist);

		dialog = GTK_DIALOG(
			gtk_dialog_new_with_buttons(
				tmp_char,
				GTK_WINDOW(main_window),
				GTK_DIALOG_MODAL
				| GTK_DIALOG_DESTROY_WITH_PARENT,
				NULL));
		no_dialog = 1;
	}
	label = gtk_dialog_add_button(dialog, GTK_STOCK_YES, GTK_RESPONSE_OK);
	gtk_window_set_default(GTK_WINDOW(dialog), label);
	gtk_dialog_add_button(dialog, GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);

	slurm_init_update_node_msg(node_msg);
	node_msg->node_names = xstrdup(nodelist);

	snprintf(tmp_char, sizeof(tmp_char), "Gres for Node(s) %s?", nodelist);

	label = gtk_label_new(tmp_char);
	gtk_box_pack_start(GTK_BOX(dialog->vbox), label, FALSE, FALSE, 0);

	entry = create_entry();
	if (!entry)
		goto end_it;

	if (old_gres)
		gtk_entry_set_text(GTK_ENTRY(entry), old_gres);

	gtk_box_pack_start(GTK_BOX(dialog->vbox), entry, TRUE, TRUE, 0);
	gtk_widget_show_all(GTK_WIDGET(dialog));

	response = gtk_dialog_run(dialog);
	if (response == GTK_RESPONSE_OK) {
		node_msg->gres = xstrdup(gtk_entry_get_text(GTK_ENTRY(entry)));
		if (!node_msg->gres) {
			edit = g_strdup_printf("No gres given.");
			display_edit_note(edit);
			g_free(edit);
			goto end_it;
		}
		if ((rc = slurm_update_node(node_msg)) == SLURM_SUCCESS) {
			edit = g_strdup_printf(
				"Nodes %s updated successfully.",
				nodelist);
			display_edit_note(edit);
			g_free(edit);
		} else {
			edit = g_strdup_printf(
				"Problem updating nodes %s: %s",
				nodelist, slurm_strerror(rc));
			display_edit_note(edit);
			g_free(edit);
		}
	}

end_it:
	slurm_free_update_node_msg(node_msg);
	if (no_dialog)
		gtk_widget_destroy(GTK_WIDGET(dialog));

	return rc;
}

extern int update_state_node(GtkDialog *dialog,
			     const char *nodelist, const char *type)
{
	uint16_t state = (uint16_t) NO_VAL;
	char *upper = NULL, *lower = NULL;
	int i = 0;
	int rc = SLURM_SUCCESS;
	char tmp_char[100];
	update_node_msg_t *node_msg = xmalloc(sizeof(update_node_msg_t));
	GtkWidget *label = NULL;
	GtkWidget *entry = NULL;
	int no_dialog = 0;

	if (!dialog) {
		dialog = GTK_DIALOG(
			gtk_dialog_new_with_buttons(
				type,
				GTK_WINDOW(main_window),
				GTK_DIALOG_MODAL
				| GTK_DIALOG_DESTROY_WITH_PARENT,
				NULL));
		no_dialog = 1;
	}
	label = gtk_dialog_add_button(dialog, GTK_STOCK_YES, GTK_RESPONSE_OK);
	gtk_window_set_default(GTK_WINDOW(dialog), label);
	gtk_dialog_add_button(dialog, GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);

	slurm_init_update_node_msg(node_msg);
	node_msg->node_names = xstrdup(nodelist);

	if (!strncasecmp("drain", type, 5)) {
		snprintf(tmp_char, sizeof(tmp_char),
			 "Are you sure you want to drain node(s) %s?\n\n"
			 "Please put reason.",
			 nodelist);
		entry = create_entry();
		label = gtk_label_new(tmp_char);
		state = NODE_STATE_DRAIN;
	} else if (!strncasecmp("resume", type, 5)) {
		snprintf(tmp_char, sizeof(tmp_char),
			 "Are you sure you want to resume node(s) %s?",
			 nodelist);
		label = gtk_label_new(tmp_char);
		state = NODE_RESUME;
	} else if (!strncasecmp("set", type, 3)) {
		snprintf(tmp_char, sizeof(tmp_char),
			 "Are you sure you want to down node(s) %s?\n\n"
			 "Please put reason.",
			 nodelist);
		entry = create_entry();
		label = gtk_label_new(tmp_char);
		state = NODE_STATE_DOWN;
	} else if (!strncasecmp("undrain", type, 5)) {
		snprintf(tmp_char, sizeof(tmp_char),
			 "Are you sure you want to undrain node(s) %s?",
			 nodelist);
		label = gtk_label_new(tmp_char);
		state = NODE_STATE_UNDRAIN;
	} else {

		if (!strncasecmp("make", type, 4))
			type = "idle";
		for(i = 0; i < NODE_STATE_END; i++) {
			upper = node_state_string(i);
			lower = str_tolower(upper);
			if (!strcmp(lower, type)) {
				snprintf(tmp_char, sizeof(tmp_char),
					 "Are you sure you want to set "
					 "node(s) %s to %s?",
					 nodelist, lower);
				label = gtk_label_new(tmp_char);
				state = i;
				xfree(lower);
				break;
			}
			xfree(lower);
		}
	}
	if (!label)
		goto end_it;
	node_msg->node_state = (uint16_t)state;
	gtk_box_pack_start(GTK_BOX(dialog->vbox), label, FALSE, FALSE, 0);
	if (entry)
		gtk_box_pack_start(GTK_BOX(dialog->vbox), entry, TRUE, TRUE, 0);
	gtk_widget_show_all(GTK_WIDGET(dialog));
	i = gtk_dialog_run(dialog);
	if (i == GTK_RESPONSE_OK) {
		if (entry) {
			node_msg->reason = xstrdup(
				gtk_entry_get_text(GTK_ENTRY(entry)));
			if (!node_msg->reason || !strlen(node_msg->reason)) {
				lower = g_strdup_printf(
					"You need a reason to do that.");
				display_edit_note(lower);
				g_free(lower);
				goto end_it;
			}
			if (uid_from_string(getlogin(),
					    &node_msg->reason_uid) < 0)
				node_msg->reason_uid = getuid();

		}
		if ((rc = slurm_update_node(node_msg)) == SLURM_SUCCESS) {
			lower = g_strdup_printf(
				"Nodes %s updated successfully.",
				nodelist);
			display_edit_note(lower);
			g_free(lower);
		} else {
			lower = g_strdup_printf(
				"Problem updating nodes %s: %s",
				nodelist, slurm_strerror(rc));
			display_edit_note(lower);
			g_free(lower);
		}
	}
end_it:
	slurm_free_update_node_msg(node_msg);
	if (no_dialog)
		gtk_widget_destroy(GTK_WIDGET(dialog));

	return rc;
}

extern GtkListStore *create_model_node(int type)
{
	GtkListStore *model = NULL;
	GtkTreeIter iter;
	char *upper = NULL, *lower = NULL;
	int i=0;

	last_model = NULL;	/* Reformat display */
	switch(type) {
	case SORTID_STATE:
		model = gtk_list_store_new(2, G_TYPE_STRING,
					   G_TYPE_INT);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "drain",
				   1, i,
				   -1);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "NoResp",
				   1, i,
				   -1);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "resume",
				   1, i,
				   -1);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "undrain",
				   1, i,
				   -1);
		for(i = 0; i < NODE_STATE_END; i++) {
			upper = node_state_string(i);
			gtk_list_store_append(model, &iter);
			lower = str_tolower(upper);
			gtk_list_store_set(model, &iter,
					   0, lower,
					   1, i,
					   -1);
			xfree(lower);
		}

		break;

	}
	return model;
}

extern void admin_edit_node(GtkCellRendererText *cell,
			    const char *path_string,
			    const char *new_text,
			    gpointer data)
{
	GtkTreeStore *treestore = GTK_TREE_STORE(data);
	GtkTreePath *path = gtk_tree_path_new_from_string(path_string);
	GtkTreeIter iter;
	char *nodelist = NULL;
	int column = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(cell),
						       "column"));
	if (!new_text || !strcmp(new_text, ""))
		goto no_input;

	gtk_tree_model_get_iter(GTK_TREE_MODEL(treestore), &iter, path);
	switch(column) {
	case SORTID_STATE:
		gtk_tree_model_get(GTK_TREE_MODEL(treestore), &iter,
				   SORTID_NAME,
				   &nodelist, -1);

		update_state_node(NULL, nodelist, new_text);

		g_free(nodelist);
	default:
		break;
	}
no_input:
	gtk_tree_path_free(path);
	g_mutex_unlock(sview_mutex);
}

extern void get_info_node(GtkTable *table, display_data_t *display_data)
{
	int error_code = SLURM_SUCCESS;
	static int view = -1;
	static node_info_msg_t *node_info_ptr = NULL;
	char error_char[100];
	GtkWidget *label = NULL;
	GtkTreeView *tree_view = NULL;
	static GtkWidget *display_widget = NULL;
	List info_list = NULL;
	int i = 0, sort_key;
	sview_node_info_t *sview_node_info_ptr = NULL;
	ListIterator itr = NULL;
	GtkTreePath *path = NULL;
	static bool set_opts = FALSE;

	if (!set_opts)
		set_page_opts(NODE_PAGE, display_data_node,
			      SORTID_CNT, _initial_page_opts);
	set_opts = TRUE;

	/* reset */
	if (!table && !display_data) {
		if (display_widget)
			gtk_widget_destroy(display_widget);
		display_widget = NULL;
		goto reset_curs;
	}

	if (display_data)
		local_display_data = display_data;
	if (!table) {
		display_data_node->set_menu = local_display_data->set_menu;
		goto reset_curs;
	}

	if (display_widget && toggled) {
		gtk_widget_destroy(display_widget);
		display_widget = NULL;
		/* Since the node_info_ptr could change out from under
		 * us we always need to check if it is new or not.
		 */
		/* goto display_it; */
	}
	if ((error_code = get_new_info_node(&node_info_ptr, force_refresh))
	    == SLURM_NO_CHANGE_IN_DATA) {
		if (!display_widget || view == ERROR_VIEW)
			goto display_it;
	} else if (error_code != SLURM_SUCCESS) {
		if (view == ERROR_VIEW)
			goto end_it;
		view = ERROR_VIEW;
		if (display_widget)
			gtk_widget_destroy(display_widget);
		sprintf(error_char, "slurm_load_node: %s",
			slurm_strerror(slurm_get_errno()));
		label = gtk_label_new(error_char);
		display_widget = gtk_widget_ref(label);
		gtk_table_attach_defaults(table, label, 0, 1, 0, 1);
		gtk_widget_show(label);
		goto end_it;
	}

display_it:
	info_list = create_node_info_list(node_info_ptr, FALSE);
	if (!info_list)
		goto reset_curs;
	i = 0;
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
		int array_size = node_info_ptr->record_count;
		int  *color_inx = xmalloc(sizeof(int) * array_size);
		bool *color_set_flag = xmalloc(sizeof(bool) * array_size);
		itr = list_iterator_create(info_list);
		while ((sview_node_info_ptr = list_next(itr))) {
			color_set_flag[i] = true;
			color_inx[i] = i;
			i++;
		}
		list_iterator_destroy(itr);
		change_grid_color_array(grid_button_list, array_size,
					color_inx, color_set_flag, true, 0);
		xfree(color_inx);
		xfree(color_set_flag);
	} else {
		highlight_grid(GTK_TREE_VIEW(display_widget),
			       SORTID_POS, (int)NO_VAL, grid_button_list);
		gtk_tree_path_free(path);
	}

	if (view == ERROR_VIEW && display_widget) {
		gtk_widget_destroy(display_widget);
		display_widget = NULL;
	}
	if (!display_widget) {
		tree_view = create_treeview(local_display_data,
					    &grid_button_list);
		/*set multiple capability here*/
		gtk_tree_selection_set_mode(
			gtk_tree_view_get_selection(tree_view),
			GTK_SELECTION_MULTIPLE);
		display_widget = gtk_widget_ref(GTK_WIDGET(tree_view));
		gtk_table_attach_defaults(GTK_TABLE(table),
					  GTK_WIDGET(tree_view),
					  0, 1, 0, 1);
		/* Since this function sets the model of the tree_view to the
		 * treestore we don't really care about the return value
		 * On large clusters, sorting on the node name slows GTK down
		 * by a large margin. */
		if (node_info_ptr->record_count > 1000)
			sort_key = -1;
		else
			sort_key = SORTID_NAME;
		create_treestore(tree_view, display_data_node,
				 SORTID_CNT, sort_key, SORTID_COLOR);
	}

	view = INFO_VIEW;
	/* If the system has a large number of nodes then not all lines
	 * will be displayed. You can try different values for the third
	 * argument of gtk_widget_set_size_request() in an attempt to
	 * maximumize the data displayed in your environment. These are my
	 * results: Y=1000 good for 43 lines, Y=-1 good for 1151 lines,
	 *  Y=64000 good for 2781 lines, Y=99000 good for 1453 lines */
	/* gtk_widget_set_size_request(display_widget, -1, -1); */
	_update_info_node(info_list, GTK_TREE_VIEW(display_widget));
end_it:
	toggled = FALSE;
	force_refresh = 1;
reset_curs:
	if (main_window && main_window->window)
		gdk_window_set_cursor(main_window->window, NULL);
	return;

}

extern void specific_info_node(popup_info_t *popup_win)
{
	int error_code = SLURM_SUCCESS;
	static node_info_msg_t *node_info_ptr = NULL;
	specific_info_t *spec_info = popup_win->spec_info;
	char error_char[100];
	GtkWidget *label = NULL;
	GtkTreeView *tree_view = NULL;
	List info_list = NULL;
	List send_info_list = NULL;
	ListIterator itr = NULL;
	sview_node_info_t *sview_node_info_ptr = NULL;
	node_info_t *node_ptr = NULL;
	hostlist_t hostlist = NULL;
	hostlist_iterator_t host_itr = NULL;
	int i = -1, sort_key;
	sview_search_info_t *search_info = spec_info->search_info;

	if (!spec_info->display_widget)
		setup_popup_info(popup_win, display_data_node, SORTID_CNT);

	if (node_info_ptr && popup_win->toggled) {
		gtk_widget_destroy(spec_info->display_widget);
		spec_info->display_widget = NULL;
		/* Since the node_info_ptr could change out from under
		 * us we always need to check if it is new or not.
		 */
		/* goto display_it; */
	}

	if ((error_code = get_new_info_node(&node_info_ptr,
					    popup_win->force_refresh))
	    == SLURM_NO_CHANGE_IN_DATA) {
		if (!spec_info->display_widget || spec_info->view == ERROR_VIEW)
			goto display_it;
	} else if (error_code != SLURM_SUCCESS) {
		if (spec_info->view == ERROR_VIEW)
			goto end_it;
		spec_info->view = ERROR_VIEW;
		if (spec_info->display_widget)
			gtk_widget_destroy(spec_info->display_widget);
		sprintf(error_char, "slurm_load_node: %s",
			slurm_strerror(slurm_get_errno()));
		label = gtk_label_new(error_char);
		gtk_table_attach_defaults(popup_win->table,
					  label,
					  0, 1, 0, 1);
		gtk_widget_show(label);
		spec_info->display_widget = gtk_widget_ref(label);
		return;
	}
display_it:

	info_list = create_node_info_list(node_info_ptr, FALSE);

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
		/* Since this function sets the model of the tree_view to the
		 * treestore we don't really care about the return value
		 * On large clusters, sorting on the node name slows GTK down
		 * by a large margin. */
		if (node_info_ptr->record_count > 1000)
			sort_key = -1;
		else
			sort_key = SORTID_NAME;
		create_treestore(tree_view, popup_win->display_data,
				 SORTID_CNT, sort_key, SORTID_COLOR);
	}

	setup_popup_grid_list(popup_win);

	spec_info->view = INFO_VIEW;
	if (spec_info->type == INFO_PAGE) {
		_display_info_node(info_list, popup_win);
		goto end_it;
	}

	setup_popup_grid_list(popup_win);

	/* just linking to another list, don't free the inside, just
	   the list */
	send_info_list = list_create(NULL);
	if (search_info->gchar_data) {
		hostlist = hostlist_create(search_info->gchar_data);
		host_itr = hostlist_iterator_create(hostlist);
	}

	i = -1;

	itr = list_iterator_create(info_list);
	while ((sview_node_info_ptr = list_next(itr))) {
		int found = 0;
		char *host = NULL;
		i++;
		node_ptr = sview_node_info_ptr->node_ptr;

		switch(search_info->search_type) {
		case SEARCH_NODE_STATE:
			if (search_info->int_data == NO_VAL)
				continue;
			else if (search_info->int_data
				 != node_ptr->node_state) {
				if (IS_NODE_MIXED(node_ptr)) {
					uint16_t alloc_cnt = 0, err_cnt = 0;
					uint16_t idle_cnt = node_ptr->cpus;
					select_g_select_nodeinfo_get(
						node_ptr->select_nodeinfo,
						SELECT_NODEDATA_SUBCNT,
						NODE_STATE_ALLOCATED,
						&alloc_cnt);
					select_g_select_nodeinfo_get(
						node_ptr->select_nodeinfo,
						SELECT_NODEDATA_SUBCNT,
						NODE_STATE_ERROR,
						&err_cnt);
					idle_cnt -= (alloc_cnt + err_cnt);
					if ((search_info->int_data
					     & NODE_STATE_BASE)
					    == NODE_STATE_ALLOCATED) {
						if (alloc_cnt)
							break;
					} else if ((search_info->int_data
						    & NODE_STATE_BASE)
						   == NODE_STATE_ERROR) {
						if (err_cnt)
							break;
					} else if ((search_info->int_data
						    & NODE_STATE_BASE)
						   == NODE_STATE_IDLE) {
						if (idle_cnt)
							break;
					}
				}
				continue;
			}
			break;
		case SEARCH_NODE_NAME:
		default:
			if (!search_info->gchar_data)
				continue;
			while ((host = hostlist_next(host_itr))) {
				if (!strcmp(host, node_ptr->name)) {
					free(host);
					found = 1;
					break;
				}
				free(host);
			}
			hostlist_iterator_reset(host_itr);

			if (!found)
				continue;
			break;
		}
		list_push(send_info_list, sview_node_info_ptr);
		change_grid_color(popup_win->grid_button_list,
				  i, i, 0, true, 0);
	}
	list_iterator_destroy(itr);
	post_setup_popup_grid_list(popup_win);

	if (search_info->gchar_data) {
		hostlist_iterator_destroy(host_itr);
		hostlist_destroy(hostlist);
	}

	_update_info_node(send_info_list,
			  GTK_TREE_VIEW(spec_info->display_widget));
	list_destroy(send_info_list);
end_it:
	popup_win->toggled = 0;
	popup_win->force_refresh = 0;

	return;

}

extern void set_menus_node(void *arg, void *arg2, GtkTreePath *path, int type)
{
	GtkTreeView *tree_view = (GtkTreeView *)arg;
	popup_info_t *popup_win = (popup_info_t *)arg;
	GtkMenu *menu = (GtkMenu *)arg2;
	List button_list = (List)arg2;

	switch(type) {
	case TAB_CLICKED:
		make_fields_menu(NULL, menu, display_data_node, SORTID_CNT);
		break;
	case ROW_CLICKED:
		make_options_menu(tree_view, path, menu, options_data_node);
		break;
	case ROW_LEFT_CLICKED:
	{
		GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
		GtkTreeIter iter;
		if (!gtk_tree_model_get_iter(model, &iter, path)) {
			g_error("error getting iter from model\n");
			break;
		}
		highlight_grid(tree_view, SORTID_POS, (int)NO_VAL, button_list);
		break;
	}
	case FULL_CLICKED:
	{
		GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
		GtkTreeIter iter;
		if (!gtk_tree_model_get_iter(model, &iter, path)) {
			g_error("error getting iter from model\n");
			break;
		}

		popup_all_node(model, &iter, INFO_PAGE);

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

extern void popup_all_node(GtkTreeModel *model, GtkTreeIter *iter, int id)
{
	char *name = NULL;

	gtk_tree_model_get(model, iter, SORTID_NAME, &name, -1);
	if (_DEBUG)
		g_print("popup_all_node: name = %s\n", name);
	popup_all_node_name(name, id);
	/* this name gets g_strdup'ed in the previous function */
	g_free(name);
}

extern void popup_all_node_name(char *name, int id)
{
	char title[100];
	ListIterator itr = NULL;
	popup_info_t *popup_win = NULL;
	GError *error = NULL;
	char *node;

	if (cluster_flags & CLUSTER_FLAG_BG)
		node = "Midplane";
	else
		node = "Node";

	switch(id) {
	case JOB_PAGE:
		snprintf(title, 100, "Job(s) with %s %s", node, name);
		break;
	case PART_PAGE:
		snprintf(title, 100, "Partition(s) with %s %s", node, name);
		break;
	case RESV_PAGE:
		snprintf(title, 100, "Reservation(s) with %s %s", node, name);
		break;
	case BLOCK_PAGE:
		snprintf(title, 100, "Blocks(s) with %s %s", node, name);
		break;
	case SUBMIT_PAGE:
		snprintf(title, 100, "Submit job on %s %s", node, name);
		break;
	case INFO_PAGE:
		snprintf(title, 100, "Full Info for %s %s", node, name);
		break;
	default:
		g_print("%s got %d\n", node, id);
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
			popup_win = create_popup_info(id, NODE_PAGE, title);
		else
			popup_win = create_popup_info(NODE_PAGE, id, title);
		popup_win->spec_info->search_info->gchar_data = g_strdup(name);
		if (!sview_thread_new((gpointer)popup_thr, popup_win,
				      FALSE, &error)) {
			g_printerr ("Failed to create node popup thread: "
				    "%s\n",
				    error->message);
			return;
		}
	} else
		gtk_window_present(GTK_WINDOW(popup_win->popup));
}

extern void admin_menu_node_name(char *name, GdkEventButton *event)
{
	GtkMenu *menu = GTK_MENU(gtk_menu_new());
	display_data_t *display_data = options_data_node;
	GtkWidget *menuitem;

	while (display_data++) {
		if (display_data->id == -1)
			break;
		if (!display_data->name)
			continue;

		display_data->user_data = name;
		menuitem = gtk_menu_item_new_with_label(display_data->name);
		g_signal_connect(menuitem, "activate",
				 G_CALLBACK(_selected_page),
				 display_data);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
	}
	gtk_widget_show_all(GTK_WIDGET(menu));
	gtk_menu_popup(menu, NULL, NULL, NULL, NULL,
		       event ? event->button : 0,
		       gdk_event_get_time((GdkEvent*)event));
}

extern void select_admin_nodes(GtkTreeModel *model,
			       GtkTreeIter *iter,
			       display_data_t *display_data,
			       uint32_t node_col,
			       GtkTreeView *treeview)
{
	if (treeview) {
		char *old_value = NULL;
		hostlist_t hl = NULL;
		process_node_t process_node;
		memset(&process_node, 0, sizeof(process_node_t));
		if (node_col == NO_VAL)
			process_node.node_col = SORTID_NAME;
		else
			process_node.node_col = node_col;

		gtk_tree_selection_selected_foreach(
			gtk_tree_view_get_selection(treeview),
			_process_each_node, &process_node);
		hl = hostlist_create(process_node.nodelist);
		hostlist_uniq(hl);
		hostlist_sort(hl);
		xfree(process_node.nodelist);
		process_node.nodelist = hostlist_ranged_string_xmalloc(hl);
		hostlist_destroy(hl);

		if (!strcasecmp("Update Features", display_data->name)) {
			/* get old features */
			gtk_tree_model_get(model, iter, SORTID_FEATURES,
					   &old_value, -1);
		} else if (!strcasecmp("Update Gres", display_data->name)) {
			/* get old gres */
			gtk_tree_model_get(model, iter, SORTID_GRES,
					   &old_value, -1);
		}
		admin_node_name(process_node.nodelist, old_value,
				display_data->name);
		xfree(process_node.nodelist);
		if (old_value)
			g_free(old_value);


	}
} /*select_admin_nodes ^^^*/

extern void admin_node_name(char *name, char *old_value, char *type)
{
	GtkWidget *popup = gtk_dialog_new_with_buttons(
		type,
		GTK_WINDOW(main_window),
		GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
		NULL);
	gtk_window_set_transient_for(GTK_WINDOW(popup), NULL);

	if (!strcasecmp("Update Features", type)
	    || !strcasecmp("Update Node Features", type)
	    || !strcasecmp("Update Midplane Features",
			   type)) { /* update features */
		update_features_node(GTK_DIALOG(popup), name, old_value);
	} else if (!strcasecmp("Update Gres", type)) { /* update gres */
		update_gres_node(GTK_DIALOG(popup), name, old_value);
	} else /* something that has to deal with a node state change */
		update_state_node(GTK_DIALOG(popup), name, type);

	gtk_widget_destroy(popup);

	return;
}

extern void cluster_change_node(void)
{
	display_data_t *display_data = display_data_node;
	while (display_data++) {
		if (display_data->id == -1)
			break;
		if (cluster_flags & CLUSTER_FLAG_BG) {
			switch(display_data->id) {
			case SORTID_RACK_MP:
				display_data->name = "RackMidplane";
				break;
			}
		} else {
			switch(display_data->id) {
			case SORTID_RACK_MP:
				display_data->name = NULL;
				break;
			}
		}
	}

	display_data = options_data_node;
	while (display_data++) {
		if (display_data->id == -1)
			break;
		if (cluster_flags & CLUSTER_FLAG_BG) {
			switch(display_data->id) {
			case BLOCK_PAGE:
				display_data->name = "Blocks";
				break;
			}

			if (!display_data->name) {
			} else if (!strcmp(display_data->name, "Drain Node"))
				display_data->name = "Drain Midplane";
			else if (!strcmp(display_data->name, "Undrain Node"))
				display_data->name = "Undrain Midplane";
			else if (!strcmp(display_data->name, "Resume Node"))
				display_data->name = "Resume Midplane";
			else if (!strcmp(display_data->name, "Put Node Down"))
				display_data->name = "Put Midplane Down";
			else if (!strcmp(display_data->name, "Make Node Idle"))
				display_data->name =
					"Make Midplane Idle";
		} else {
			switch(display_data->id) {
			case BLOCK_PAGE:
				display_data->name = NULL;
				break;
			}

			if (!display_data->name) {
			} else if (!strcmp(display_data->name,
					   "Drain Midplanes"))
				display_data->name = "Drain Nodes";
			else if (!strcmp(display_data->name,
					   "Undrain Midplanes"))
				display_data->name = "Undrain Nodes";
			else if (!strcmp(display_data->name,
					 "Resume Midplanes"))
				display_data->name = "Resume Nodes";
			else if (!strcmp(display_data->name,
					 "Put Midplanes Down"))
				display_data->name = "Put Nodes Down";
			else if (!strcmp(display_data->name,
					 "Make Midplanes Idle"))
				display_data->name = "Make Nodes Idle";
		}
	}
	get_info_node(NULL, NULL);
}
