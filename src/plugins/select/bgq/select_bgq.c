/*****************************************************************************\
 *  select_bgq.cc - node selection plugin for Blue Gene/Q system.
 *****************************************************************************
 *  Copyright (C) 2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
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


#include "bgq.h"

#define HUGE_BUF_SIZE (1024*16)
#define NOT_FROM_CONTROLLER -2

/* These are defined here so when we link with something other than
 * the slurmctld we will have these symbols defined.  They will get
 * overwritten when linking with the slurmctld.
 */
#if defined (__APPLE__)
slurm_ctl_conf_t slurmctld_conf __attribute__((weak_import));
struct node_record *node_record_table_ptr  __attribute__((weak_import)) = NULL;
int bg_recover __attribute__((weak_import)) = NOT_FROM_CONTROLLER;
List part_list  __attribute__((weak_import)) = NULL;
int node_record_count __attribute__((weak_import));
time_t last_node_update __attribute__((weak_import));
time_t last_job_update __attribute__((weak_import));
char *alpha_num  __attribute__((weak_import)) =
	"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
void *acct_db_conn  __attribute__((weak_import)) = NULL;
char *slurmctld_cluster_name  __attribute__((weak_import)) = NULL;
slurmdb_cluster_rec_t *working_cluster_rec  __attribute__((weak_import)) = NULL;
#else
slurm_ctl_conf_t slurmctld_conf;
struct node_record *node_record_table_ptr = NULL;
int bg_recover = NOT_FROM_CONTROLLER;
List part_list = NULL;
int node_record_count;
time_t last_node_update;
time_t last_job_update;
char *alpha_num = (char *)"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
void *acct_db_conn = NULL;
char *slurmctld_cluster_name = NULL;
slurmdb_cluster_rec_t *working_cluster_rec = NULL;
#endif

const char plugin_name[]       	= "BG/Q node selection plugin";
const char plugin_type[]       	= "select/bgq";
const uint32_t plugin_id     	= 103;
const uint32_t plugin_version	= 100;

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init ( void )
{

#ifdef HAVE_BGQ
	if(bg_recover != NOT_FROM_CONTROLLER) {
#if (SYSTEM_DIMENSIONS != 4)
		fatal("SYSTEM_DIMENSIONS value (%d) invalid for BGQ",
		      SYSTEM_DIMENSIONS);
#endif

		verbose("%s loading...", plugin_name);
		/* if this is coming from something other than the controller
		   we don't want to read the config or anything like that. */
	}
	verbose("%s loaded", plugin_name);
#else
	if (bg_recover != NOT_FROM_CONTROLLER)
		fatal("select/bgq is incompatible with a "
		      "non BlueGene/Q system");
#endif
	return SLURM_SUCCESS;
}

extern int fini ( void )
{
	int rc = SLURM_SUCCESS;

	return rc;
}

/*
 * The remainder of this file implements the standard SLURM
 * node selection API.
 */

/* We rely upon DB2 to save and restore BlueGene state */
extern int select_p_state_save(char *dir_name)
{
#ifdef HAVE_BGQ
	return SLURM_SUCCESS;
#else
	return SLURM_ERROR;
#endif
}

extern int select_p_state_restore(char *dir_name)
{
#ifdef HAVE_BGQ
	debug("bgq: select_p_state_restore");

	return SLURM_SUCCESS;
#else
	return SLURM_ERROR;
#endif
}

/* Sync BG blocks to currently active jobs */
extern int select_p_job_init(List job_list)
{
#ifdef HAVE_BGQ
	int rc = SLURM_SUCCESS;
	return rc;
#else
	return SLURM_ERROR;
#endif
}

/* All initialization is performed by init() */
extern int select_p_node_init(struct node_record *node_ptr, int node_cnt)
{
#ifdef HAVE_BGQ
	return SLURM_SUCCESS;
#else
	return SLURM_ERROR;
#endif
}

/*
 * Called by slurmctld when a new configuration file is loaded
 * or scontrol is used to change block configuration
 */
 extern int select_p_block_init(List part_list)
{
#ifdef HAVE_BGQ
	return SLURM_SUCCESS;
#else
	return SLURM_ERROR;
#endif
}


/*
 * select_p_job_test - Given a specification of scheduling requirements,
 *	identify the nodes which "best" satify the request. The specified
 *	nodes may be DOWN or BUSY at the time of this test as may be used
 *	to deterime if a job could ever run.
 * IN/OUT job_ptr - pointer to job being scheduled start_time is set
 *	when we can possibly start job.
 * IN/OUT bitmap - usable nodes are set on input, nodes not required to
 *	satisfy the request are cleared, other left set
 * IN min_nodes - minimum count of nodes
 * IN max_nodes - maximum count of nodes (0==don't care)
 * IN req_nodes - requested (or desired) count of nodes
 * IN mode - SELECT_MODE_RUN_NOW: try to schedule job now
 *           SELECT_MODE_TEST_ONLY: test if job can ever run
 *           SELECT_MODE_WILL_RUN: determine when and where job can run
 * IN preemptee_candidates - List of pointers to jobs which can be preempted.
 * IN/OUT preemptee_job_list - Pointer to list of job pointers. These are the
 *		jobs to be preempted to initiate the pending job. Not set
 *		if mode=SELECT_MODE_TEST_ONLY or input pointer is NULL.
 * RET zero on success, EINVAL otherwise
 * NOTE: bitmap must be a superset of req_nodes at the time that
 *	select_p_job_test is called
 */
extern int select_p_job_test(struct job_record *job_ptr, bitstr_t *bitmap,
			     uint32_t min_nodes, uint32_t max_nodes,
			     uint32_t req_nodes, uint16_t mode,
			     List preemptee_candidates,
			     List *preemptee_job_list)
{
#ifdef HAVE_BGQ
	return SLURM_SUCCESS;
#else
	return SLURM_ERROR;
#endif
}

extern int select_p_job_begin(struct job_record *job_ptr)
{
#ifdef HAVE_BGQ
	return SLURM_SUCCESS;
#else
	return SLURM_ERROR;
#endif
}

extern int select_p_job_ready(struct job_record *job_ptr)
{
#ifdef HAVE_BGQ
	return SLURM_SUCCESS;
#else
	return SLURM_ERROR;
#endif
}

extern int select_p_job_resized(struct job_record *job_ptr,
				struct node_record *node_ptr)
{
	return ESLURM_NOT_SUPPORTED;
}

extern int select_p_job_fini(struct job_record *job_ptr)
{
#ifdef HAVE_BGQ
	return SLURM_SUCCESS;
#else
	return SLURM_ERROR;
#endif
}

extern int select_p_job_suspend(struct job_record *job_ptr)
{
	return ESLURM_NOT_SUPPORTED;
}

extern int select_p_job_resume(struct job_record *job_ptr)
{
	return ESLURM_NOT_SUPPORTED;
}

extern int select_p_pack_select_info(time_t last_query_time,
				     uint16_t show_flags, Buf *buffer_ptr,
				     uint16_t protocol_version)
{
#ifdef HAVE_BGQ
	return ESLURM_NOT_SUPPORTED;
#else
	return SLURM_ERROR;
#endif
}

extern int select_p_select_nodeinfo_pack(select_nodeinfo_t *nodeinfo,
					 Buf buffer,
					 uint16_t protocol_version)
{
	return ESLURM_NOT_SUPPORTED;
}

extern int select_p_select_nodeinfo_unpack(select_nodeinfo_t **nodeinfo,
					   Buf buffer,
					   uint16_t protocol_version)
{
	return ESLURM_NOT_SUPPORTED;
}

extern select_nodeinfo_t *select_p_select_nodeinfo_alloc(uint32_t size)
{
	return SLURM_SUCCESS;
}

extern int select_p_select_nodeinfo_free(select_nodeinfo_t *nodeinfo)
{
	return SLURM_SUCCESS;
}

extern int select_p_select_nodeinfo_set_all(time_t last_query_time)
{
	return SLURM_SUCCESS;
}

extern int select_p_select_nodeinfo_set(struct job_record *job_ptr)
{
	return SLURM_SUCCESS;
}

extern int select_p_select_nodeinfo_get(select_nodeinfo_t *nodeinfo,
					enum select_nodedata_type dinfo,
					enum node_states state,
					void *data)
{
	return SLURM_SUCCESS;
}

select_jobinfo_t *select_p_select_jobinfo_alloc(void)
{
	return SLURM_SUCCESS;
}

extern int select_p_select_jobinfo_set(select_jobinfo_t *jobinfo,
				       enum select_jobdata_type data_type,
				       void *data)
{
	return SLURM_SUCCESS;
}

extern int select_p_select_jobinfo_get (select_jobinfo_t *jobinfo,
				 enum select_jobdata_type data_type, void *data)
{
	return SLURM_SUCCESS;
}

extern select_jobinfo_t *select_p_select_jobinfo_copy(select_jobinfo_t *jobinfo)
{
	return SLURM_SUCCESS;
}

extern int select_p_select_jobinfo_free  (select_jobinfo_t *jobinfo)
{
	return SLURM_SUCCESS;
}

extern int  select_p_select_jobinfo_pack(select_jobinfo_t *jobinfo, Buf buffer,
					 uint16_t protocol_version)
{
	return SLURM_SUCCESS;
}

extern int  select_p_select_jobinfo_unpack(select_jobinfo_t **jobinfo,
					   Buf buffer,
					   uint16_t protocol_version)
{
	return SLURM_SUCCESS;
}

extern char *select_p_select_jobinfo_sprint(select_jobinfo_t *jobinfo,
				     char *buf, size_t size, int mode)
{
	return SLURM_SUCCESS;
}

extern char *select_p_select_jobinfo_xstrdup(select_jobinfo_t *jobinfo,
					     int mode)
{
	return SLURM_SUCCESS;
}

extern int select_p_update_block (update_block_msg_t *block_desc_ptr)
{
#ifdef HAVE_BGQ
	int rc = SLURM_SUCCESS;
	return rc;
#else
	return SLURM_ERROR;
#endif
}

extern int select_p_update_sub_node (update_block_msg_t *block_desc_ptr)
{
#ifdef HAVE_BGQ
	return SLURM_SUCCESS;
#else
	return SLURM_ERROR;
#endif
}

extern int select_p_get_info_from_plugin (enum select_plugindata_info dinfo,
					  struct job_record *job_ptr,
					  void *data)
{
#ifdef HAVE_BGQ
	return SLURM_SUCCESS;
#else
	return SLURM_ERROR;
#endif
}

extern int select_p_update_node_config (int index)
{
	return SLURM_SUCCESS;
}

extern int select_p_update_node_state (int index, uint16_t state)
{
#ifdef HAVE_BGQ
	return SLURM_SUCCESS;
#endif
	return SLURM_ERROR;
}

extern int select_p_alter_node_cnt(enum select_node_cnt type, void *data)
{
#ifdef HAVE_BGQ
	return SLURM_SUCCESS;
#else
	return SLURM_ERROR;
#endif
}

extern int select_p_reconfigure(void)
{
#ifdef HAVE_BGQ
	return SLURM_SUCCESS;
#else
	return SLURM_ERROR;
#endif
}
