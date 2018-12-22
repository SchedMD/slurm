/*****************************************************************************\
 *  node_select.h - Define node selection plugin functions.
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#ifndef _NODE_SELECT_H
#define _NODE_SELECT_H

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"
#include "src/common/list.h"
#include "src/common/plugrack.h"
#include "src/slurmctld/slurmctld.h"

/* NO_JOB_RUNNING is used by select/blugene, select/bgq, smap and sview */
#define NO_JOB_RUNNING -1
#define NOT_FROM_CONTROLLER -2

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

/*
 * Local data
 */
typedef struct slurm_select_ops {
	uint32_t	(*plugin_id);
	int		(*state_save)		(char *dir_name);
	int		(*state_restore)	(char *dir_name);
	int		(*job_init)		(List job_list);
	int		(*node_ranking)		(struct node_record *node_ptr,
						 int node_cnt);
	int		(*node_init)		(struct node_record *node_ptr,
						 int node_cnt);
	int		(*block_init)		(List block_list);
	int		(*job_test)		(struct job_record *job_ptr,
						 bitstr_t *bitmap,
						 uint32_t min_nodes,
						 uint32_t max_nodes,
						 uint32_t req_nodes,
						 uint16_t mode,
						 List preeemptee_candidates,
						 List *preemptee_job_list,
						 bitstr_t *exc_core_bitmap);
	int		(*job_begin)		(struct job_record *job_ptr);
	int		(*job_ready)		(struct job_record *job_ptr);
	int		(*job_expand)		(struct job_record *from_job_ptr,
						 struct job_record *to_job_ptr);
	int		(*job_resized)		(struct job_record *job_ptr,
						 struct node_record *node_ptr);
	int		(*job_signal)		(struct job_record *job_ptr,
						 int signal);
	int		(*job_mem_confirm)	(struct job_record *job_ptr);
	int		(*job_fini)		(struct job_record *job_ptr);
	int		(*job_suspend)		(struct job_record *job_ptr,
						 bool indf_susp);
	int		(*job_resume)		(struct job_record *job_ptr,
						 bool indf_susp);
	bitstr_t *      (*step_pick_nodes)      (struct job_record *job_ptr,
						 select_jobinfo_t *step_jobinfo,
						 uint32_t node_count,
						 bitstr_t **avail_nodes);
	int             (*step_start)           (struct step_record *step_ptr);
	int             (*step_finish)          (struct step_record *step_ptr,
						 bool killing_step);
	int		(*nodeinfo_pack)	(select_nodeinfo_t *nodeinfo,
						 Buf buffer,
						 uint16_t protocol_version);
	int		(*nodeinfo_unpack)	(select_nodeinfo_t **nodeinfo,
						 Buf buffer,
						 uint16_t protocol_version);
	select_nodeinfo_t *(*nodeinfo_alloc)	(void);
	int		(*nodeinfo_free)	(select_nodeinfo_t *nodeinfo);
	int		(*nodeinfo_set_all)	(void);
	int		(*nodeinfo_set)		(struct job_record *job_ptr);
	int		(*nodeinfo_get)		(select_nodeinfo_t *nodeinfo,
						 enum
						 select_nodedata_type dinfo,
						 enum node_states state,
						 void *data);
	select_jobinfo_t *(*jobinfo_alloc)	(void);
	int		(*jobinfo_free)		(select_jobinfo_t *jobinfo);
	int		(*jobinfo_set)		(select_jobinfo_t *jobinfo,
						 enum
						 select_jobdata_type data_type,
						 void *data);
	int		(*jobinfo_get)		(select_jobinfo_t *jobinfo,
						 enum
						 select_jobdata_type data_type,
						 void *data);
	select_jobinfo_t *(*jobinfo_copy)	(select_jobinfo_t *jobinfo);
	int		(*jobinfo_pack)		(select_jobinfo_t *jobinfo,
						 Buf buffer,
						 uint16_t protocol_version);
	int		(*jobinfo_unpack)	(select_jobinfo_t **jobinfo_pptr,
						 Buf buffer,
						 uint16_t protocol_version);
	char *		(*jobinfo_sprint)	(select_jobinfo_t *jobinfo,
						 char *buf, size_t size,
						 int mode);
	char *		(*jobinfo_xstrdup)	(select_jobinfo_t *jobinfo,
						 int mode);
	int		(*get_info_from_plugin)	(enum
						 select_plugindata_info dinfo,
						 struct job_record *job_ptr,
						 void *data);
	int		(*update_node_config)	(int index);
	int		(*update_node_state)	(struct node_record *node_ptr);
	int		(*reconfigure)		(void);
	bitstr_t *      (*resv_test)            (resv_desc_msg_t *resv_desc_ptr,
						 uint32_t node_cnt,
						 bitstr_t *avail_bitmap,
						 bitstr_t **core_bitmap);
} slurm_select_ops_t;

/*
 * Defined in node_select.c Must be synchronized with slurm_select_ops_t above.
 * Also must be synchronized with the other_select.c in
 * the select/other lib.
 */
extern const char *node_select_syms[];

/* Convert a node coordinate character into its equivalent number:
 * '0' = 0; '9' = 9; 'A' = 10; etc. */
extern int select_char2coord(char coord);

/*******************************************\
 * GLOBAL SELECT STATE MANAGEMENT FUNCIONS *
\*******************************************/

/*
 * Initialize context for node selection plugin
 */
extern int slurm_select_init(bool only_default);

/*
 * Terminate plugin and free all associated memory
 */
extern int slurm_select_fini(void);

/* Get this plugin's sequence number in Slurm's internal tables */
extern int select_get_plugin_id_pos(uint32_t plugin_id);

/* Get the plugin ID number. Unique for each select plugin type */
extern int select_get_plugin_id(void);

/* If the slurmctld is running a linear based select plugin return 1
 * else 0. */
extern int select_running_linear_based(void);

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

/*********************************\
 * STATE INITIALIZATION FUNCIONS *
\*********************************/

/*
 * Note re/initialization of node record data structure
 * IN node_ptr - current node data
 * IN node_count - number of node entries
 */
extern int select_g_node_init(struct node_record *node_ptr, int node_cnt);

/*
 * Note re/initialization of partition record data structure
 * IN part_list - list of partition records
 */
extern int select_g_block_init(List part_list);

/*
 * Note the initialization of job records, issued upon restart of
 * slurmctld and used to synchronize any job state.
 * IN job_list - List of Slurm jobs from slurmctld
 */
extern int select_g_job_init(List job_list);

/* Note reconfiguration or change in partition configuration */
extern int select_g_reconfigure(void);

/**************************\
 * NODE SPECIFIC FUNCIONS *
\**************************/

/*
 * Allocate a select plugin node record.
 *
 * NOTE: Call select_g_select_nodeinfo_free() to release the memory in the
 * returned value
 */
extern dynamic_plugin_data_t *select_g_select_nodeinfo_alloc(void);

/*
 * Pack a select plugin node record into a buffer.
 * IN nodeinfo - The node record to pack
 * IN/OUT buffer - The buffer to pack the record into
 * IN protocol_version - Version used for packing the record
 */
extern int select_g_select_nodeinfo_pack(dynamic_plugin_data_t *nodeinfo,
					 Buf buffer,
					 uint16_t protocol_version);

/*
 * Unpack a select plugin node record from a buffer.
 * OUT nodeinfo - The unpacked node record
 * IN/OUT buffer - The buffer to unpack the record from
 * IN protocol_version - Version used for unpacking the record
 *
 * NOTE: Call select_g_select_nodeinfo_free() to release the memory in the
 * returned value
 */
extern int select_g_select_nodeinfo_unpack(dynamic_plugin_data_t **nodeinfo,
					   Buf buffer,
					   uint16_t protocol_version);

/* Free the memory allocated for a select plugin node record */
extern int select_g_select_nodeinfo_free(dynamic_plugin_data_t *nodeinfo);

/* Reset select plugin specific information about a job
 * IN job_ptr - The updated job */
extern int select_g_select_nodeinfo_set(struct job_record *job_ptr);

/* Update slect plugin information about every node as needed (if changed since
 * previous query) */
extern int select_g_select_nodeinfo_set_all(void);

/*
 * Get information from a slect plugin node record
 * IN nodeinfo - The record to get information from
 * IN dinfo - The data type to be retrieved
 * IN state - Node state filter to be applied (ie. only get information about
 *            ALLOCATED nodes
 * OUT data - The retrieved data
 */
extern int select_g_select_nodeinfo_get(dynamic_plugin_data_t *nodeinfo,
					enum select_nodedata_type dinfo,
					enum node_states state,
					void *data);

/*
 * Updated a node configuration. This happens when a node registers with
 *	more resources than originally configured (e.g. memory).
 * IN index  - index into the node record list
 * RETURN SLURM_SUCCESS on success || SLURM_ERROR else wise
 */
extern int select_g_update_node_config (int index);

/*
 * Assign a 'node_rank' value to each of the node_ptr entries.
 * IN node_ptr - current node data
 * IN node_count - number of node entries
 * Return true if node ranking was performed, false if not.
 */
extern bool select_g_node_ranking(struct node_record *node_ptr, int node_cnt);

/*
 * Updated a node state in the plugin, this should happen when a node is
 * drained or put into a down state then changed back.
 * IN node_ptr - Pointer to the node that has been updated
 * RETURN SLURM_SUCCESS on success || SLURM_ERROR else wise
 */
extern int select_g_update_node_state (struct node_record *node_ptr);

/******************************************************\
 * JOB SPECIFIC SELECT CREDENTIAL MANAGEMENT FUNCIONS *
\******************************************************/

#define SELECT_MODE_BASE         0x00ff
#define SELECT_MODE_FLAGS        0xff00

#define SELECT_MODE_RUN_NOW	 0x0000
#define SELECT_MODE_TEST_ONLY	 0x0001
#define SELECT_MODE_WILL_RUN	 0x0002
#define SELECT_MODE_RESV	 0x0004

#define SELECT_MODE_PREEMPT_FLAG 0x0100
#define SELECT_MODE_CHECK_FULL   0x0200
#define SELECT_MODE_IGN_ERR      0x0400

#define SELECT_IS_MODE_RUN_NOW(_X) \
	(((_X & SELECT_MODE_BASE) == SELECT_MODE_RUN_NOW) \
	 && !SELECT_IS_PREEMPT_ON_FULL_TEST(_X))

#define SELECT_IS_MODE_TEST_ONLY(_X) \
	(_X & SELECT_MODE_TEST_ONLY)

#define SELECT_IS_MODE_WILL_RUN(_X) \
	(_X & SELECT_MODE_WILL_RUN || SELECT_IS_MODE_RESV(_X))

#define SELECT_IS_MODE_RESV(_X) \
	(_X & SELECT_MODE_RESV)

#define SELECT_IGN_ERR(_X) \
	(_X & SELECT_MODE_IGN_ERR)

#define SELECT_IS_PREEMPT_SET(_X) \
	(_X & SELECT_MODE_PREEMPT_FLAG)

#define SELECT_IS_CHECK_FULL_SET(_X) \
	(_X & SELECT_MODE_CHECK_FULL)

#define SELECT_IS_TEST(_X) \
	(SELECT_IS_MODE_TEST_ONLY(_X) || SELECT_IS_MODE_WILL_RUN(_X))

#define SELECT_IS_PREEMPT_ON_FULL_TEST(_X) \
	(SELECT_IS_CHECK_FULL_SET(_X) && SELECT_IS_PREEMPT_SET(_X))

#define SELECT_IS_PREEMPTABLE_TEST(_X) \
	((SELECT_IS_MODE_TEST_ONLY(_X) || SELECT_IS_MODE_WILL_RUN(_X))	\
	 && SELECT_IS_PREEMPT_SET(_X))

/* allocate storage for a select job credential
 * RET jobinfo - storage for a select job credential
 * NOTE: storage must be freed using select_g_free_jobinfo
 */
extern dynamic_plugin_data_t *select_g_select_jobinfo_alloc(void);

/* copy a select job credential
 * IN jobinfo - the select job credential to be copied
 * RET        - the copy or NULL on failure
 * NOTE: returned value must be freed using select_g_select_jobinfo_free
 */
extern dynamic_plugin_data_t *select_g_select_jobinfo_copy(
	dynamic_plugin_data_t *jobinfo);

/* free storage previously allocated for a select job credential
 * IN jobinfo  - the select job credential to be freed
 * RET         - slurm error code
 */
extern int select_g_select_jobinfo_free(dynamic_plugin_data_t *jobinfo);

/* pack a select job credential into a buffer in machine independent form
 * IN jobinfo  - the select job credential to be saved
 * OUT buffer  - buffer with select credential appended
 * IN protocol_version - slurm protocol version of client
 * RET         - slurm error code
 */
extern int select_g_select_jobinfo_pack(dynamic_plugin_data_t *jobinfo,
					Buf buffer,
					uint16_t protocol_version);

/* unpack a select job credential from a buffer
 * OUT jobinfo - the select job credential read
 * IN  buffer  - buffer with select credential read from current pointer loc
 * IN protocol_version - slurm protocol version of client
 * RET         - slurm error code
 * NOTE: returned value must be freed using select_g_select_jobinfo_free
 */
extern int select_g_select_jobinfo_unpack(dynamic_plugin_data_t **jobinfo,
					  Buf buffer,
					  uint16_t protocol_version);

/* fill in a previously allocated select job credential
 * IN/OUT jobinfo  - updated select job credential
 * IN data_type - type of data to enter into job credential
 * IN data - the data to enter into job credential
 */
extern int select_g_select_jobinfo_set(dynamic_plugin_data_t *jobinfo,
				       enum select_jobdata_type data_type,
				       void *data);

/* get data from a select job credential
 * IN jobinfo  - updated select job credential
 * IN data_type - type of data to enter into job credential
 * OUT data - the data to get from job credential, caller must xfree
 *	data for data_type == SELECT_JOBDATA_PART_ID
 */
extern int select_g_select_jobinfo_get(dynamic_plugin_data_t *jobinfo,
				       enum select_jobdata_type data_type,
				       void *data);

/* write select job info to a string
 * IN jobinfo - a select job credential
 * OUT buf    - location to write job info contents
 * IN size    - byte size of buf
 * IN mode    - print mode, see enum select_print_mode
 * RET        - the string, same as buf
 */
extern char *select_g_select_jobinfo_sprint(dynamic_plugin_data_t *jobinfo,
					    char *buf, size_t size, int mode);

/* write select job info to a string
 * IN jobinfo - a select job credential
 * OUT buf    - location to write job info contents
 * IN mode    - print mode, see enum select_print_mode
 * RET        - the string, same as buf
 */
extern char *select_g_select_jobinfo_xstrdup(dynamic_plugin_data_t *jobinfo,
					     int mode);

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
 * IN preemptee_candidates - List of pointers to jobs which can bee preempted
 * IN/OUT preemptee_job_list - Pointer to list of job pointers. These are the
 *		jobs to be preempted to initiate the pending job. Not set
 *		if mode=SELECT_MODE_TEST_ONLY or input pointer is NULL.
 *		Existing list is appended to.
 * IN exc_core_bitmap - cores reserved and not usable
 * RET zero on success, EINVAL otherwise
 */
extern int select_g_job_test(struct job_record *job_ptr, bitstr_t *bitmap,
			     uint32_t min_nodes, uint32_t max_nodes,
			     uint32_t req_nodes, uint16_t mode,
			     List preemptee_candidates,
			     List *preemptee_job_list,
			     bitstr_t *exc_core_bitmap);

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
 * Pass job-step signal to plugin before signaling any job steps, so that
 * any signal-dependent actions can be taken.
 * IN job_ptr - job to be signaled
 * IN signal  - signal(7) number
 */
extern int select_g_job_signal(struct job_record *job_ptr, int signal);

/*
 * Confirm that a job's memory allocation is still valid after a node is
 * restarted. This is an issue if the job is allocated all of the memory on a
 * node and that node is restarted with a different memory size than at the time
 * it is allocated to the job. This would mostly be an issue on an Intel KNL
 * node where the memory size would vary with the MCDRAM cache mode.
 */
extern int select_g_job_mem_confirm(struct job_record *job_ptr);

/*
 * Suspend a job. Executed from slurmctld.
 * IN job_ptr - pointer to job being suspended
 * IN indf_susp - set if job is being suspended indefinitely by user
 *                or admin, otherwise suspended for gang scheduling
 * RET SLURM_SUCCESS or error code
 */
extern int select_g_job_suspend(struct job_record *job_ptr, bool indf_susp);

/*
 * Resume a job. Executed from slurmctld.
 * IN job_ptr - pointer to job being resumed
 * IN indf_susp - set if job is being resumed from indefinite suspend by user
 *                or admin, otherwise resume from gang scheduling
 * RET SLURM_SUCCESS or error code
 */
extern int select_g_job_resume(struct job_record *job_ptr, bool indf_susp);

/*
 * Move the resource allocated to one job into that of another job.
 *	All resources are removed from "from_job_ptr" and moved into
 *	"to_job_ptr". Also see other_job_resized().
 * RET: 0 or an error code
 */
extern int select_g_job_expand(struct job_record *from_job_ptr,
			       struct job_record *to_job_ptr);

/*
 * Modify internal data structures for a job that has changed size
 *	Only support jobs shrinking now.
 * RET: 0 or an error code
 */
extern int select_g_job_resized(struct job_record *job_ptr,
				struct node_record *node_ptr);

/*******************************************************\
 * STEP SPECIFIC SELECT CREDENTIAL MANAGEMENT FUNCIONS *
\*******************************************************/

/*
 * Select the "best" nodes for given job from those available
 * IN/OUT job_ptr - pointer to job already allocated and running in a
 *                  block where the step is to run.
 *                  set's start_time when job expected to start
 * OUT step_jobinfo - Fill in the resources to be used if not
 *                    full size of job.
 * IN node_count  - How many nodes we are looking for.
 * OUT avail_nodes - bitmap of available nodes according to the plugin
 *                  (not always set).
 * RET map of slurm nodes to be used for step, NULL if resources not selected
 *
 * NOTE: Most select plugins return NULL and use common code slurmctld to
 * select resources for a job step. Only on IBM Bluegene systems does the
 * select plugin need to select resources and take system topology into
 * consideration.
 */
extern bitstr_t * select_g_step_pick_nodes(struct job_record *job_ptr,
					   dynamic_plugin_data_t *step_jobinfo,
					   uint32_t node_count,
					   bitstr_t **avail_nodes);
/*
 * Post pick_nodes operations for the step.
 * IN/OUT step_ptr - step pointer to operate on.
 */
extern int select_g_step_start(struct step_record *step_ptr);

/*
 * clear what happened in select_g_step_pick_nodes and/or select_g_step_start
 * IN/OUT step_ptr - step pointer to operate on.
 * IN killing_step - if true then we are just starting to kill the step
 *                   if false, the step is completely terminated
 */
extern int select_g_step_finish(struct step_record *step_ptr, bool killing_step);

/*********************************\
 * ADVANCE RESERVATION FUNCTIONS *
\*********************************/

/*
 * select_g_resv_test - Identify the nodes which "best" satisfy a reservation
 *	request. "best" is defined as either single set of consecutive nodes
 *	satisfying the request and leaving the minimum number of unused nodes
 *	OR the fewest number of consecutive node sets
 * IN/OUT resv_desc_ptr - reservation request - select_jobinfo can be
 *	updated in the plugin
 * IN node_cnt - count of required nodes
 * IN/OUT avail_bitmap - nodes available for the reservation
 * IN/OUT core_bitmap - cores which can not be used for this
 *	reservation IN, and cores to be used in the reservation OUT
 *	(flush bitstr then apply only used cores)
 * RET - nodes selected for use by the reservation
 */
extern bitstr_t * select_g_resv_test(resv_desc_msg_t *resv_desc_ptr,
				     uint32_t node_cnt,
				     bitstr_t *avail_bitmap,
				     bitstr_t **core_bitmap);

/*****************************\
 * GET INFORMATION FUNCTIONS *
\*****************************/

/*
 * Get select data from a plugin
 * IN node_pts  - current node record
 * IN dinfo     - type of data to get from the node record
 *                (see enum select_plugindata_info)
 * IN job_ptr   - pointer to the job that's related to this query (may be NULL)
 * IN/OUT data  - the data to get from node record
 */
extern int select_g_get_info_from_plugin (enum select_plugindata_info dinfo,
					  struct job_record *job_ptr,
					  void *data);

#endif /*__SELECT_PLUGIN_API_H__*/
