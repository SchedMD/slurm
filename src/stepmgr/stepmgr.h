/*****************************************************************************\
 *  stepmgr.h - manage the job step information of slurm
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
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

#ifndef _SLURM_STEP_MGR_H
#define _SLURM_STEP_MGR_H

#include "src/common/front_end.h"
#include "src/common/id_util.h"
#include "src/common/job_record.h"
#include "src/common/node_conf.h"
#include "src/common/slurm_step_layout.h"

typedef struct {
	void *acct_db_conn;
	list_t *job_list;
	time_t *last_job_update;
	bitstr_t *up_node_bitmap;

	void (*job_config_fini)(job_record_t *job_ptr);
	job_record_t *(*find_job_record)(uint32_t job_id);
	job_record_t *(*find_job_array_rec)(uint32_t array_job_id,
					    uint32_t array_task_id);
	void (*agent_queue_request)(agent_arg_t *agent_arg_ptr);

	front_end_record_t *(*find_front_end_record)(char *name);
} stepmgr_ops_t;

extern stepmgr_ops_t *stepmgr_ops;

extern void stepmgr_init(stepmgr_ops_t *ops);

/*
 * delete_step_records - delete step record for specified job_ptr
 * IN job_ptr - pointer to job table entry to have step records removed
 */
extern void delete_step_records(job_record_t *job_ptr);

/*
 * job_step_signal - signal the specified job step
 * IN step_id - filled in slurm_step_id_t
 * IN signal - user id of user issuing the RPC
 * IN flags - RPC flags
 * IN uid - user id of user issuing the RPC
 * RET 0 on success, otherwise ESLURM error code
 * global: job_list - pointer global job list
 *	last_job_update - time of last job table update
 */
extern int job_step_signal(slurm_step_id_t *step_id,
			   uint16_t signal, uint16_t flags, uid_t uid);

/*
 * step_create - creates a step_record in step_specs->job_id, sets up the
 *	according to the step_specs.
 * IN job_ptr - job_ptr to create step in
 * IN step_specs - job step specifications
 * OUT new_step_record - pointer to the new step_record (NULL on error)
 * IN protocol_version - slurm protocol version of client
 * OUT err_msg - Custom error message to the user, caller to xfree results
  * RET - 0 or error code
 * NOTE: don't free the returned step_record because that is managed through
 * 	the job.
 */
extern int step_create(job_record_t *job_ptr,
		       job_step_create_request_msg_t *step_specs,
		       step_record_t **new_step_record,
		       uint16_t protocol_version, char **err_msg);

/*
 * step_layout_create - creates a step_layout according to the inputs.
 * IN step_ptr - step having tasks layed out
 * IN step_node_list - node list of hosts in step
 * IN node_count - count of nodes in step allocation
 * IN num_tasks - number of tasks in step
 * IN cpus_per_task - number of cpus per task
 * IN task_dist - type of task distribution
 * IN plane_size - size of plane (only needed for the plane distribution)
 * RET - NULL or slurm_step_layout_t *
 * NOTE: you need to free the returned step_layout usually when the
 *       step is freed.
 */
extern slurm_step_layout_t *step_layout_create(step_record_t *step_ptr,
					       char *step_node_list,
					       uint32_t node_count,
					       uint32_t num_tasks,
					       uint16_t cpus_per_task,
					       uint32_t task_dist,
					       uint16_t plane_size);

/*
 * kill_step_on_node - determine if the specified job has any job steps
 *	allocated to the specified node and kill them unless no_kill flag
 *	is set on the step
 * IN job_ptr - pointer to an active job record
 * IN node_ptr - pointer to a node record
 * IN node_fail - true of removed node has failed
 */
extern void kill_step_on_node(job_record_t *job_ptr, node_record_t *node_ptr,
			      bool node_fail);

/*
 * step_partial_comp - Note the completion of a job step on at least
 *	some of its nodes
 * IN req     - step_completion_msg RPC from slurmstepd
 * IN uid     - UID issuing the request
 * IN finish  - If true, no error, and no rem is 0 finish the step.
 * OUT rem    - count of nodes for which responses are still pending
 * OUT max_rc - highest return code for any step thus far
 * RET 0 on success, otherwise ESLURM error code
 */
extern int step_partial_comp(step_complete_msg_t *req, uid_t uid, bool finish,
			     int *rem, uint32_t *max_rc);

/*
 * step_set_alloc_tres - set the tres up when allocating the step.
 * Only set when job is running.
 * NOTE: job write lock must be locked before calling this */
extern void step_set_alloc_tres(step_record_t *step_ptr, uint32_t node_count,
				bool assoc_mgr_locked, bool make_formatted);

/*
 * Create the batch step and add it to the job.
 */
extern step_record_t *build_batch_step(job_record_t *job_ptr_in);

/* Update time stamps for job step suspend */
extern void suspend_job_step(job_record_t *job_ptr);

/* Update time stamps for job step resume */
extern void resume_job_step(job_record_t *job_ptr);

/* Process job step update request from specified user,
 * RET - 0 or error code */
extern int update_step(step_update_request_msg_t *req, uid_t uid);

/*
 * Rebuild a job step's core_bitmap_job after a job has just changed size
 * job_ptr IN - job that was just re-sized
 * orig_job_node_bitmap IN - The job's original node bitmap
 */
extern void rebuild_step_bitmaps(job_record_t *job_ptr,
				 bitstr_t *orig_job_node_bitmap);

/*
 * Create the extern step and add it to the job.
 */
extern step_record_t *build_extern_step(job_record_t *job_ptr);

/*
 * build_alias_addrs - build alias_addrs for step_layout
 */
extern slurm_node_alias_addrs_t *build_alias_addrs(job_record_t *job_ptr);

/*
 * Given a full system bitmap return the nth bit set where node_name is in it
 * IN - node_name - name of node
 * IN - node_bitmap - full system bitmap
 *
 * Used when you have a job/step specific array and you want to find the index
 * where that node is represented in that array.
 */
extern int job_get_node_inx(char *node_name, bitstr_t *node_bitmap);

extern int step_create_from_msg(slurm_msg_t *msg,
				void (*lock_func)(bool lock),
				void (*fail_lock_func)(bool lock));

/*
 * pack_job_step_info_response_msg - packs job step info
 * IN step_id - specific id or NO_VAL/NO_VAL for all
 * IN uid - user issuing request
 * IN show_flags - job step filtering options
 * OUT buffer - location to store data, pointers automatically advanced
 * IN protocol_version - slurm protocol version of client
 * RET - 0 or error code
 * NOTE: MUST free_buf buffer
 */
extern int pack_job_step_info_response_msg(pack_step_args_t *args);

/*
 * Return combined layouts of all job_ptr steps
 */
extern int stepmgr_get_step_layouts(job_record_t *job_ptr,
				    slurm_step_id_t *step_id,
				    slurm_step_layout_t **out_step_layout);

/*
 * Create a job_sbcast_cred_msg_t with a sbcast_cred to send back to client
 */
extern int stepmgr_get_job_sbcast_cred_msg(
	job_record_t *job_ptr,
	slurm_step_id_t *step_id,
	char *hetjob_nodelist,
	uint16_t protocol_version,
	job_sbcast_cred_msg_t **out_sbcast_cred_msg);

extern resource_allocation_response_msg_t *build_job_info_resp(
	job_record_t *job_ptr);

#endif /* _SLURM_STEP_MGR_H */
