/*****************************************************************************\
 *  other_select.h - Define other select plugin needed for cray since
 *                   it can leverage other plugins.
 *****************************************************************************
 *  Copyright (C) 2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
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

#ifndef _CRAY_OTHER_SELECT_H
#define _CRAY_OTHER_SELECT_H

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/slurm_xlator.h"	/* Must be first */
#include "src/common/list.h"
#include "src/slurmctld/slurmctld.h"

extern uint16_t other_select_type_param;

/*
 * Initialize context for node selection plugin
 */
extern int other_select_init(void);

/*
 * Terminate plugin and free all associated memory
 */
extern int other_select_fini(void);

/*
 * Save any global state information
 * IN dir_name - directory into which the data can be stored
 */
extern int other_state_save(char *dir_name);

/*
 * Initialize context for node selection plugin and
 * restore any global state information
 * IN dir_name - directory from which the data can be restored
 */
extern int other_state_restore(char *dir_name);

/*
 * Note the initialization of job records, issued upon restart of
 * slurmctld and used to synchronize any job state.
 */
extern int other_job_init(List job_list);

/*
 * Note re/initialization of node record data structure
 * IN node_ptr - current node data
 * IN node_count - number of node entries
 */
extern int other_node_init();

/*
 * Get select data from a plugin
 * IN node_pts  - current node record
 * IN dinfo   - type of data to get from the node record
 *               (see enum select_plugindata_info)
 * IN job_ptr   - pointer to the job that's related to this query(may be NULL)
 * IN/OUT data  - the data to get from node record
 */
extern int other_get_info_from_plugin(enum select_plugindata_info dinfo,
				      job_record_t *job_ptr, void *data);

/*
 * Select the "best" nodes for given job from those available
 * IN/OUT job_ptr - pointer to job being considered for initiation,
 *                  set's start_time when job expected to start
 * IN/OUT bitmap - map of nodes being considered for allocation on input,
 *                 map of nodes actually to be assigned on output
 * IN min_nodes - minimum number of nodes to allocate to job
 * IN max_nodes - maximum number of nodes to allocate to job
 * IN req_nodes - requested(or desired) count of nodes
 * IN mode - SELECT_MODE_RUN_NOW: try to schedule job now
 *           SELECT_MODE_TEST_ONLY: test if job can ever run
 *           SELECT_MODE_WILL_RUN: determine when and where job can run
 * IN preemptee_candidates - List of pointers to jobs which can bee preempted
 * IN/OUT preemptee_job_list - Pointer to list of job pointers. These are the
 *		jobs to be preempted to initiate the pending job. Not set
 *		if mode=SELECT_MODE_TEST_ONLY or input pointer is NULL.
 *		Existing list is appended to.
 * IN exc_core_bitmap - bitmap of cores being reserved.
 * RET zero on success, EINVAL otherwise
 */
extern int other_job_test(job_record_t *job_ptr, bitstr_t *bitmap,
			  uint32_t min_nodes, uint32_t max_nodes,
			  uint32_t req_nodes, uint16_t mode,
			  List preemptee_candidates, List *preemptee_job_list,
			  bitstr_t *exc_core_bitmap);

/*
 * Note initiation of job is about to begin. Called immediately
 * after other_job_test(). Executed from slurmctld.
 * IN job_ptr - pointer to job being initiated
 */
extern int other_job_begin(job_record_t *job_ptr);

/*
 * determine if job is ready to execute per the node select plugin
 * IN job_ptr - pointer to job being tested
 * RET -1 on error, 1 if ready to execute, 0 otherwise
 */
extern int other_job_ready(job_record_t *job_ptr);

/*
 * Move the resource allocated to one job into that of another job.
 *	All resources are removed from "from_job_ptr" and moved into
 *	"to_job_ptr". Also see other_job_resized().
 * RET: 0 or an error code
 */
extern int other_job_expand(job_record_t *from_job_ptr,
			    job_record_t *to_job_ptr);

/*
 * Modify internal data structures for a job that has decreased job size.
 *	Only support jobs shrinking. Also see other_job_expand();
 * RET: 0 or an error code
 */
extern int other_job_resized(job_record_t *job_ptr, node_record_t *node_ptr);

/*
 * Note termination of job is starting. Executed from slurmctld.
 * IN job_ptr - pointer to job being terminated
 */
extern int other_job_fini(job_record_t *job_ptr);

/*
 * Suspend a job. Executed from slurmctld.
 * IN job_ptr - pointer to job being suspended
 * indf_susp IN - set if job is being suspended indefinitely by user
 *                or admin, otherwise suspended for gang scheduling
 * RET SLURM_SUCCESS or error code
 */
extern int other_job_suspend(job_record_t *job_ptr, bool indf_susp);

/*
 * Resume a job. Executed from slurmctld.
 * IN job_ptr - pointer to job being resumed
 * indf_susp IN - set if job is being resumed from indefinite suspend by user
 *                or admin, otherwise resume from gang scheduling
 * RET SLURM_SUCCESS or error code
 */
extern int other_job_resume(job_record_t *job_ptr, bool indf_susp);

/*
 * Select the "best" nodes for given job from those available
 * IN/OUT job_ptr - pointer to job already allocated and running in a
 *                  block where the step is to run.
 *                  set's start_time when job expected to start
 * OUT step_jobinfo - Fill in the resources to be used if not
 *                    full size of job.
 * IN node_count  - How many nodes we are looking for.
 * OUT avail_nodes - bitmap of available nodes according to the plugin.
 * RET map of slurm nodes to be used for step, NULL on failure
 */
extern bitstr_t * other_step_pick_nodes(job_record_t *job_ptr,
					select_jobinfo_t *jobinfo,
					uint32_t node_count,
					bitstr_t **avail_nodes);

extern int other_step_start(step_record_t *step_ptr);

/*
 * clear what happened in select_g_step_pick_nodes
 * IN/OUT step_ptr - Flush the resources from the job and step.
 * IN killing_step - if true then we are just starting to kill the step
 *                   if false, the step is completely terminated
 */
extern int other_step_finish(step_record_t *step_ptr, bool killing_step);

/* allocate storage for a select job credential
 * RET jobinfo - storage for a select job credential
 * NOTE: storage must be freed using other_free_jobinfo
 */
extern select_jobinfo_t *other_select_jobinfo_alloc(void);

/* free storage previously allocated for a select job credential
 * IN jobinfo  - the select job credential to be freed
 * RET         - slurm error code
 */
extern int other_select_jobinfo_free(select_jobinfo_t *jobinfo);

/* fill in a previously allocated select job credential
 * IN/OUT jobinfo  - updated select job credential
 * IN data_type - type of data to enter into job credential
 * IN data - the data to enter into job credential
 */
extern int other_select_jobinfo_set(select_jobinfo_t *jobinfo,
				    enum select_jobdata_type data_type,
				    void *data);

/* get data from a select job credential
 * IN jobinfo  - updated select job credential
 * IN data_type - type of data to enter into job credential
 * OUT data - the data to get from job credential, caller must xfree
 */
extern int other_select_jobinfo_get(select_jobinfo_t *jobinfo,
				    enum select_jobdata_type data_type,
				    void *data);

/* copy a select job credential
 * IN jobinfo - the select job credential to be copied
 * RET        - the copy or NULL on failure
 * NOTE: returned value must be freed using other_select_jobinfo_free
 */
extern select_jobinfo_t *other_select_jobinfo_copy(
	select_jobinfo_t *jobinfo);

/* pack a select job credential into a buffer in machine independent form
 * IN jobinfo  - the select job credential to be saved
 * OUT buffer  - buffer with select credential appended
 * IN protocol_version - slurm protocol version of client
 * RET         - slurm error code
 */
extern int other_select_jobinfo_pack(select_jobinfo_t *jobinfo,
				     buf_t *buffer,
				     uint16_t protocol_version);

/* unpack a select job credential from a buffer
 * OUT jobinfo - the select job credential read
 * IN  buffer  - buffer with select credential read from current pointer loc
 * IN protocol_version - slurm protocol version of client
 * RET         - slurm error code
 * NOTE: returned value must be freed using other_select_jobinfo_free
 */
extern int other_select_jobinfo_unpack(select_jobinfo_t **jobinfo,
				       buf_t *buffer,
				       uint16_t protocol_version);

/*******************************************************\
 * NODE-SPECIFIC SELECT CREDENTIAL MANAGEMENT FUNCIONS *
\*******************************************************/

extern int other_select_nodeinfo_pack(select_nodeinfo_t *nodeinfo,
				      buf_t *buffer,
				      uint16_t protocol_version);

extern int other_select_nodeinfo_unpack(select_nodeinfo_t **nodeinfo,
					buf_t *buffer,
					uint16_t protocol_version);

extern select_nodeinfo_t *other_select_nodeinfo_alloc(void);

extern int other_select_nodeinfo_free(select_nodeinfo_t *nodeinfo);

extern int other_select_nodeinfo_set_all(void);

extern int other_select_nodeinfo_set(job_record_t *job_ptr);

extern int other_select_nodeinfo_get(select_nodeinfo_t *nodeinfo,
				     enum select_nodedata_type dinfo,
				     enum node_states state,
				     void *data);


/******************************************************\
 * NODE-SELECT PLUGIN SPECIFIC INFORMATION FUNCTIONS  *
\******************************************************/

/* Note reconfiguration or change in partition configuration */
extern int other_reconfigure(void);

extern bitstr_t * other_resv_test(resv_desc_msg_t *resv_desc_ptr,
				  uint32_t node_cnt,
				  bitstr_t *avail_bitmap,
				  bitstr_t **core_bitmap);

#endif /* _CRAY_OTHER_SELECT_H */
