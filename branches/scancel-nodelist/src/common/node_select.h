/*****************************************************************************\
 *  node_select.h - Define node selection plugin functions.
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  LLNL-CODE-402394.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
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

#ifndef _NODE_SELECT_H 
#define _NODE_SELECT_H

#include "src/api/node_select_info.h"
#include "src/common/list.h"
#include "src/slurmctld/slurmctld.h"
#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>

typedef struct {
	bitstr_t *avail_nodes;      /* usable nodes are set on input, nodes
				     * not required to satisfy the request
				     * are cleared, other left set */
	struct job_record *job_ptr; /* pointer to job being scheduled
				     * start_time is set when we can
				     * possibly start job. Or must not
				     * increase for success of running
				     * other jobs.
				     */
	uint32_t max_nodes;         /* maximum count of nodes (0==don't care) */
	uint32_t min_nodes;         /* minimum count of nodes */
	uint32_t req_nodes;         /* requested (or desired) count of nodes */
} select_will_run_t;

/*****************************************\
 * GLOBAL SELECT STATE MANGEMENT FUNCIONS *
\*****************************************/

/*
 * Initialize context for node selection plugin
 */
extern int slurm_select_init(void);

/*
 * Terminate plugin and free all associated memory
 */
extern int slurm_select_fini(void);

/*
 * Save any global state information
 * IN dir_name - directory into which the data can be stored
 */
extern int select_g_state_save(char *dir_name);

/*
 * Initialize context for node selection plugin and
 * restore any global state information
 * IN dir_name - directory from which the data can be restored
 */
extern int select_g_state_restore(char *dir_name);

/*
 * Note re/initialization of node record data structure
 * IN node_ptr - current node data
 * IN node_count - number of node entries
 */
extern int select_g_node_init(struct node_record *node_ptr, int node_cnt);

/* 
 * Get select data from a specific node record
 * IN node_pts  - current node record
 * IN cr_info   - type of data to get from the node record
 * IN/OUT data  - the data to get from node record
 */
extern int select_g_get_select_nodeinfo (struct node_record *node_ptr, 
                                         enum select_data_info cr_info, 
					 void *data);

/* 
 * Update select data for a specific node record for a specific job 
 * IN job_ptr - current job record
 */
extern int select_g_update_nodeinfo (struct job_record *job_ptr);

/* 
 * Update specific block (usually something has gone wrong)  
 * IN part_desc_ptr - information about the block
 */
extern int select_g_update_block (update_part_msg_t *part_desc_ptr);

/* 
 * Update specific sub nodes (usually something has gone wrong)  
 * IN part_desc_ptr - information about the block
 */
extern int select_g_update_sub_node (update_part_msg_t *part_desc_ptr);

/* 
 * Get select data from a plugin
 * IN node_pts  - current node record
 * IN cr_info   - type of data to get from the node record 
 *                (see enum select_data_info)
 * IN job_ptr   - pointer to the job that's related to this query (may be NULL)
 * IN/OUT data  - the data to get from node record
 */
extern int select_g_get_info_from_plugin (enum select_data_info cr_info, 
					  struct job_record *job_ptr,
					  void *data);

/* 
 * Updated a node state in the plugin, this should happen when a node is
 * drained or put into a down state then changed back.
 * IN index  - index into the node record list
 * IN state  - state to update to
 * RETURN SLURM_SUCCESS on success || SLURM_ERROR else wise
 */
extern int select_g_update_node_state (int index, uint16_t state);

/* 
 * Alter the node count for a job given the type of system we are on
 * IN/OUT job_desc  - current job desc
 */
extern int select_g_alter_node_cnt (enum select_node_cnt type, void *data);

/*
 * Note re/initialization of partition record data structure
 * IN part_list - list of partition records
 */
extern int select_g_block_init(List part_list);

/* 
 * Note the initialization of job records, issued upon restart of 
 * slurmctld and used to synchronize any job state.
 */
extern int select_g_job_init(List job_list);


extern int select_g_block_init(List part_list);

/******************************************************\
 * JOB-SPECIFIC SELECT CREDENTIAL MANAGEMENT FUNCIONS *
\******************************************************/

#define SELECT_MODE_RUN_NOW	0
#define SELECT_MODE_TEST_ONLY	1
#define SELECT_MODE_WILL_RUN	2

/*
 * Select the "best" nodes for given job from those available
 * IN/OUT job_ptr - pointer to job being considered for initiation,
 *                  set's start_time when job expected to start
 * IN/OUT bitmap - map of nodes being considered for allocation on input,
 *                 map of nodes actually to be assigned on output
 * IN min_nodes - minimum number of nodes to allocate to job
 * IN max_nodes - maximum number of nodes to allocate to job
 * IN req_nodes - requested (or desired) count of nodes
 * IN mode - SELECT_MODE_RUN_NOW: try to schedule job now
 *           SELECT_MODE_TEST_ONLY: test if job can ever run
 *           SELECT_MODE_WILL_RUN: determine when and where job can run
 * RET zero on success, EINVAL otherwise
 */
extern int select_g_job_test(struct job_record *job_ptr, bitstr_t *bitmap,
			uint32_t min_nodes, uint32_t max_nodes, 
			uint32_t req_nodes, int mode);


/*
 * Given a list of select_will_run_t's in
 * accending priority order we will see if we can start and
 * finish all the jobs without increasing the start times of the
 * jobs specified and fill in the est_start of requests with no
 * est_start.  If you are looking to see if one job will ever run
 * then use select_p_job_test instead.
 * IN/OUT req_list - list of select_will_run_t's in asscending
 *	             priority order on success of placement fill in
 *	             est_start of request with time.
 * RET zero on success, EINVAL otherwise
 */
extern int select_g_job_list_test(List req_list);

/*
 * Note initiation of job is about to begin. Called immediately 
 * after select_g_job_test(). Executed from slurmctld.
 * IN job_ptr - pointer to job being initiated
 */
extern int select_g_job_begin(struct job_record *job_ptr);

/*
 * determine if job is ready to execute per the node select plugin
 * IN job_ptr - pointer to job being tested
 * RET -1 on error, 1 if ready to execute, 0 otherwise
 */
extern int select_g_job_ready(struct job_record *job_ptr);

/*
 * Note termination of job is starting. Executed from slurmctld.
 * IN job_ptr - pointer to job being terminated
 */
extern int select_g_job_fini(struct job_record *job_ptr);

/*
 * Suspend a job. Executed from slurmctld.
 * IN job_ptr - pointer to job being suspended
 * RET SLURM_SUCCESS or error code
 */
extern int select_g_job_suspend(struct job_record *job_ptr);

/*
 * Resume a job. Executed from slurmctld.
 * IN job_ptr - pointer to job being resumed
 * RET SLURM_SUCCESS or error code
 */
extern int select_g_job_resume(struct job_record *job_ptr);

/* allocate storage for a select job credential
 * OUT jobinfo - storage for a select job credential
 * RET         - slurm error code
 * NOTE: storage must be freed using select_g_free_jobinfo
 */
extern int select_g_alloc_jobinfo (select_jobinfo_t *jobinfo);

/* fill in a previously allocated select job credential
 * IN/OUT jobinfo  - updated select job credential
 * IN data_type - type of data to enter into job credential
 * IN data - the data to enter into job credential
 */
extern int select_g_set_jobinfo (select_jobinfo_t jobinfo,
				 enum select_data_type data_type, void *data);

/* get data from a select job credential
 * IN jobinfo  - updated select job credential
 * IN data_type - type of data to enter into job credential
 * OUT data - the data to get from job credential, caller must xfree
 *	data for data_tyep == SELECT_DATA_PART_ID
 */
extern int select_g_get_jobinfo (select_jobinfo_t jobinfo,
				 enum select_data_type data_type, void *data);

/* copy a select job credential
 * IN jobinfo - the select job credential to be copied
 * RET        - the copy or NULL on failure
 * NOTE: returned value must be freed using select_g_free_jobinfo
 */
extern select_jobinfo_t select_g_copy_jobinfo(select_jobinfo_t jobinfo);

/* free storage previously allocated for a select job credential
 * IN jobinfo  - the select job credential to be freed
 * RET         - slurm error code
 */
extern int select_g_free_jobinfo  (select_jobinfo_t *jobinfo);

/* pack a select job credential into a buffer in machine independent form
 * IN jobinfo  - the select job credential to be saved
 * OUT buffer  - buffer with select credential appended
 * RET         - slurm error code
 */
extern int  select_g_pack_jobinfo  (select_jobinfo_t jobinfo, Buf buffer);

/* unpack a select job credential from a buffer
 * OUT jobinfo - the select job credential read
 * IN  buffer  - buffer with select credential read from current pointer loc
 * RET         - slurm error code
 * NOTE: returned value must be freed using select_g_free_jobinfo
 */
extern int  select_g_unpack_jobinfo(select_jobinfo_t jobinfo, Buf buffer);

/* write select job info to a string
 * IN jobinfo - a select job credential
 * OUT buf    - location to write job info contents
 * IN size    - byte size of buf
 * IN mode    - print mode, see enum select_print_mode
 * RET        - the string, same as buf
 */
extern char *select_g_sprint_jobinfo(select_jobinfo_t jobinfo,
				     char *buf, size_t size, int mode);

/* write select job info to a string
 * IN jobinfo - a select job credential
 * OUT buf    - location to write job info contents
 * IN mode    - print mode, see enum select_print_mode
 * RET        - the string, same as buf
 */
extern char *select_g_xstrdup_jobinfo(select_jobinfo_t jobinfo, int mode);

/* Prepare to start a job step, allocate memory as needed
 * RET - slurm error code
 */
extern int select_g_step_begin(struct step_record *step_ptr);

/* Prepare to terminate a job step, release memory as needed
 * RET - slurm error code
 */
extern int select_g_step_fini(struct step_record *step_ptr);

/******************************************************\
 * NODE-SELECT PLUGIN SPECIFIC INFORMATION FUNCTIONS  *
\******************************************************/

/* pack node-select plugin specific information into a buffer in 
 *	machine independent form
 * IN last_update_time - time of latest information consumer has
 * OUT buffer - location to hold the data, consumer must free
 * RET - slurm error code
 */
extern int select_g_pack_node_info(time_t last_query_time, Buf *buffer);
 
/* Unpack node select info from a buffer */
extern int select_g_unpack_node_info(node_select_info_msg_t **
				     node_select_info_msg_pptr, Buf buffer);

/* Free a node select information buffer */
extern int select_g_free_node_info(node_select_info_msg_t **
				   node_select_info_msg_pptr);

/* Note reconfiguration or change in partition configuration */
extern int select_g_reconfigure(void);

#endif /*__SELECT_PLUGIN_API_H__*/
