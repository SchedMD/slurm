/*****************************************************************************\
 *  job_mgr.c - manage the job information of slurm
 *	Note: there is a global job list (job_list), time stamp
 *	(last_job_update), and hash table (job_hash)
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Copyright (C) SchedMD LLC.
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

#include "config.h"
#define _GNU_SOURCE

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "slurm/slurm_errno.h"

#include "src/common/assoc_mgr.h"
#include "src/common/bitstring.h"
#include "src/common/cpu_frequency.h"
#include "src/common/cron.h"
#include "src/common/fd.h"
#include "src/common/forward.h"
#include "src/common/hostlist.h"
#include "src/common/id_util.h"
#include "src/common/node_features.h"
#include "src/common/parse_time.h"
#include "src/common/port_mgr.h"
#include "src/common/slurm_protocol_pack.h"
#include "src/common/state_save.h"
#include "src/common/timers.h"
#include "src/common/track_script.h"
#include "src/common/tres_bind.h"
#include "src/common/tres_frequency.h"
#include "src/common/xassert.h"
#include "src/common/xstring.h"

#include "src/interfaces/accounting_storage.h"
#include "src/interfaces/acct_gather.h"
#include "src/interfaces/auth.h"
#include "src/interfaces/burst_buffer.h"
#include "src/interfaces/cred.h"
#include "src/interfaces/gres.h"
#include "src/interfaces/hash.h"
#include "src/interfaces/job_submit.h"
#include "src/interfaces/jobcomp.h"
#include "src/interfaces/mcs.h"
#include "src/interfaces/node_features.h"
#include "src/interfaces/preempt.h"
#include "src/interfaces/priority.h"
#include "src/interfaces/sched_plugin.h"
#include "src/interfaces/select.h"
#include "src/interfaces/switch.h"
#include "src/interfaces/topology.h"

#include "src/slurmctld/acct_policy.h"
#include "src/slurmctld/agent.h"
#include "src/slurmctld/fed_mgr.h"
#include "src/slurmctld/front_end.h"
#include "src/slurmctld/gang.h"
#include "src/slurmctld/job_scheduler.h"
#include "src/slurmctld/licenses.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/node_scheduler.h"
#include "src/slurmctld/power_save.h"
#include "src/slurmctld/proc_req.h"
#include "src/slurmctld/reservation.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/slurmscriptd.h"
#include "src/slurmctld/state_save.h"
#include "src/slurmctld/trigger_mgr.h"

#include "src/stepmgr/gres_stepmgr.h"
#include "src/stepmgr/srun_comm.h"
#include "src/stepmgr/stepmgr.h"

#define ARRAY_ID_BUF_SIZE 32
#define MAX_EXIT_VAL 255	/* Maximum value returned by WIFEXITED() */
#define SLURM_CREATE_JOB_FLAG_NO_ALLOCATE_0 0
#define TOP_PRIORITY 0xffff0000	/* large, but leave headroom for higher */
#define PURGE_OLD_JOB_IN_SEC 2592000 /* 30 days in seconds */

#define JOB_HASH_INX(_job_id)	(_job_id % hash_table_size)
#define JOB_ARRAY_HASH_INX(_job_id, _task_id)		\
	((_job_id + _task_id) % hash_table_size)

/* No need to change we always pack SLURM_PROTOCOL_VERSION */
#define JOB_STATE_VERSION     "PROTOCOL_VERSION"

typedef enum {
	JOB_HASH_JOB,
	JOB_HASH_ARRAY_JOB,
	JOB_HASH_ARRAY_TASK,
} job_hash_type_t;

typedef struct {
	int resp_array_cnt;
	int resp_array_size;
	uint32_t *resp_array_rc;
	bitstr_t **resp_array_task_id;
	char **err_msg;
} resp_array_struct_t;

typedef struct {
	buf_t *buffer;
	uint32_t  filter_uid;
	bool has_qos_lock;
	uint32_t  jobs_packed;
	uint16_t  protocol_version;
	uint16_t  show_flags;
	uid_t     uid;
	slurmdb_user_rec_t user_rec;
	bool privileged;
	part_record_t **visible_parts;
} _foreach_pack_job_info_t;

typedef struct {
	bitstr_t *node_map;
	list_t *license_list;
	int rc;
} job_overlap_args_t;

typedef struct {
	slurm_selected_step_t *filter_id;
	bool free_array_bitmap;
	job_record_t *job_ptr;
} array_task_filter_t;

typedef struct {
	list_t *array_leader_list; /* list of job_record_t */
	list_t *pending_array_task_list; /* list of array_task_filter_t */
	uid_t auth_uid;
	bool filter_specific_job_ids;
	job_record_t *het_leader;
	kill_jobs_msg_t *kill_msg;
	time_t now;
	list_t *other_job_list; /* list of job_record_t */
	list_t *responses; /* list of kill_jobs_resp_job_t */
} signal_jobs_args_t;

typedef struct {
	int curr_count;
	kill_jobs_resp_msg_t *resp_msg;
} xfer_signal_jobs_responses_args_t;

#define MAGIC_FOREACH_BY_JOBID_ARGS 0x1a0beebe
typedef struct {
	int magic; /* MAGIC_FOREACH_BY_JOBID_ARGS */
	foreach_job_by_id_control_t control;
	uint32_t count;
	JobForEachFunc callback;
	JobNullForEachFunc null_callback; /* If not set, then do nothing when
					   * the job id is not found. */
	JobROForEachFunc ro_callback;
	void *callback_arg;
	job_record_t *job_ptr;
	const slurm_selected_step_t *filter;
} for_each_by_job_id_args_t;

typedef struct {
	uint32_t error_code;
	uint32_t max_nodes;
	uint32_t min_nodes;
	part_record_t *part_ptr;
	uid_t submit_uid;
	uint32_t time_limit;
} qos_part_check_t;

typedef struct {
	job_record_t *job_ptr;
	uint16_t min_part_prio_tier;
	bitstr_t *part_nodes;
	list_itr_t *resv_list_iter;
	bool use_none_resv_nodes;
} top_prio_args_t;

/* Global variables */
list_t *job_list = NULL;	/* job_record list */
time_t last_job_update;		/* time of last update to job records */

list_t *purge_jobs_list = NULL;	/* job_record_t entries to free */

/* Local variables */
static int      bf_min_age_reserve = 0;
static uint32_t delay_boot = 0;
static uint32_t highest_prio = 0;
static uint32_t lowest_prio  = TOP_PRIORITY;
static int      hash_table_size = 0;
static int      job_count = 0;		/* job's in the system */
static uint32_t job_id_sequence = 0;	/* first job_id to assign new job */
static struct   job_record **job_hash = NULL;
static struct   job_record **job_array_hash_j = NULL;
static struct   job_record **job_array_hash_t = NULL;
static bool     kill_invalid_dep;
static time_t   last_file_write_time = (time_t) 0;
static uint32_t max_array_size = NO_VAL;
static bitstr_t *requeue_exit = NULL;
static bitstr_t *requeue_exit_hold = NULL;
static bool     validate_cfgd_licenses = true;

/* Local functions */
static void _signal_pending_job_array_tasks(job_record_t *job_ptr, bitstr_t
					    **array_bitmap, uint16_t signal,
					    uid_t uid, int32_t i_last,
					    time_t now, int *rc);
static void _add_job_hash(job_record_t *job_ptr);
static void _add_job_array_hash(job_record_t *job_ptr);
static void _handle_requeue_limit(job_record_t *job_ptr, const char *caller);
static int  _copy_job_desc_to_file(job_desc_msg_t * job_desc,
				   uint32_t job_id);
static int  _copy_job_desc_to_job_record(job_desc_msg_t * job_desc,
					 job_record_t **job_ptr,
					 bitstr_t ** exc_bitmap,
					 bitstr_t ** req_bitmap);
static char *_copy_nodelist_no_dup(char *node_list);
static job_record_t *_create_job_record(uint32_t num_jobs, bool list_add);
static slurmdb_qos_rec_t *_determine_and_validate_qos(
	char *resv_name, slurmdb_assoc_rec_t *assoc_ptr,
	bool operator, slurmdb_qos_rec_t *qos_rec, int *error_code,
	bool locked, log_level_t log_lvl);
static job_fed_details_t *_dup_job_fed_details(job_fed_details_t *src);
static void _get_batch_job_dir_ids(list_t *batch_dirs);
static bool _get_whole_hetjob(void);
static bool _higher_precedence(job_record_t *job_ptr, job_record_t *job_ptr2);
static void _job_array_comp(job_record_t *job_ptr, bool was_running,
			    bool requeue);
static int  _job_create(job_desc_msg_t *job_desc, int allocate, int will_run,
			bool cron, job_record_t **job_rec_ptr, uid_t submit_uid,
			char **err_msg, uint16_t protocol_version);
static void _job_timed_out(job_record_t *job_ptr, bool preempted);
static void _kill_dependent(job_record_t *job_ptr);
static int  _list_find_job_old(void *job_entry, void *key);
static bitstr_t *_make_requeue_array(char *conf_buf);
static uint32_t _max_switch_wait(uint32_t input_wait);
static void _move_to_purge_jobs_list(void *job_entry);
static void _notify_srun_missing_step(job_record_t *job_ptr, int node_inx,
				      time_t now, time_t node_boot_time);
static buf_t *_open_job_state_file(char **state_file);
static time_t _get_last_job_state_write_time(void);
static void _pack_default_job_details(job_record_t *job_ptr, buf_t *buffer,
				      uint16_t protocol_version);
static void _pack_pending_job_details(job_details_t *detail_ptr, buf_t *buffer,
				      uint16_t protocol_version);
static void _purge_missing_jobs(int node_inx, time_t now);
static int  _read_data_array_from_file(int fd, char *file_name, char ***data,
				       uint32_t *size, job_record_t *job_ptr);
static void _remove_defunct_batch_dirs(list_t *batch_dirs);
static void _remove_job_hash(job_record_t *job_ptr, job_hash_type_t type);
static void _resp_array_add(resp_array_struct_t **resp, job_record_t *job_ptr,
			    uint32_t rc, char *err_msg);
static void _resp_array_add_id(resp_array_struct_t **resp, uint32_t job_id,
			       uint32_t task_id, uint32_t rc);
static void _resp_array_free(resp_array_struct_t *resp);
static job_array_resp_msg_t *_resp_array_xlate(resp_array_struct_t *resp,
					       uint32_t job_id);
static int  _resume_job_nodes(job_record_t *job_ptr, bool indf_susp);
static void _send_job_kill(job_record_t *job_ptr);
static int  _set_job_id(job_record_t *job_ptr);
static void _set_job_requeue_exit_value(job_record_t *job_ptr);
static void _signal_batch_job(job_record_t *job_ptr, uint16_t signal,
			      uint16_t flags);
static void _signal_job(job_record_t *job_ptr, int signal, uint16_t flags);
static void _suspend_job(job_record_t *job_ptr, uint16_t op);
static int  _suspend_job_nodes(job_record_t *job_ptr, bool indf_susp);
static bool _top_priority(job_record_t *job_ptr, uint32_t het_job_offset);
static int _update_job_nodes_str(job_record_t *job_ptr);
static int  _valid_job_part(job_desc_msg_t *job_desc, uid_t submit_uid,
			    bitstr_t *req_bitmap, part_record_t *part_ptr,
			    list_t *part_ptr_list,
			    slurmdb_assoc_rec_t *assoc_ptr,
			    slurmdb_qos_rec_t *qos_ptr,
			    list_t *qos_ptr_list);
static int  _validate_job_desc(job_desc_msg_t *job_desc_msg, int allocate,
			       bool cron, uid_t submit_uid,
			       part_record_t *part_ptr, list_t *part_list);
static void _validate_job_files(list_t *batch_dirs);
static bool _validate_min_mem_partition(job_desc_msg_t *job_desc_msg,
					part_record_t *part_ptr,
					list_t *part_list);
static bool _valid_pn_min_mem(job_desc_msg_t * job_desc_msg,
			      part_record_t *part_ptr);
static int  _write_data_array_to_file(char *file_name, char **data,
				      uint32_t size);

static char *_get_mail_user(const char *user_name, job_record_t *job_ptr)
{
	char *mail_user = NULL;
	if (!user_name || (user_name[0] == '\0')) {
		mail_user = user_from_job(job_ptr);
		/* unqualified sender, append MailDomain if set */
		if (slurm_conf.mail_domain)
			xstrfmtcat(mail_user, "@%s", slurm_conf.mail_domain);
	} else {
		mail_user = xstrdup(user_name);
	}

	return mail_user;
}

static int _job_fail_account(job_record_t *job_ptr, const char *func_name,
			     bool assoc_locked)
{
	int rc = 0; // Return number of pending jobs held

	if (IS_JOB_FINISHED(job_ptr)) {
		/*
		 * The acct_policy has already be cleared for this job.  Just
		 * reset the pointer.
		 */
		job_ptr->assoc_ptr = NULL;
		job_ptr->assoc_id = 0;
		return rc;
	}

	if (IS_JOB_PENDING(job_ptr)) {
		info("%s: %pJ ineligible due to invalid association",
		     func_name, job_ptr);

		xfree(job_ptr->state_desc);
		job_ptr->state_reason = FAIL_ACCOUNT;

		if (job_ptr->details) {
			/* reset the job */
			job_ptr->details->accrue_time = 0;
			job_ptr->bit_flags &= ~JOB_ACCRUE_OVER;
			job_ptr->details->begin_time = 0;
			/* Update job with new begin_time. */
			jobacct_storage_g_job_start(acct_db_conn, job_ptr);
		}
		rc = 1;
	}

	/* This job is no longer eligible, so make it so. */
	if (job_ptr->assoc_ptr) {
		part_record_t *tmp_part = job_ptr->part_ptr;
		list_t *tmp_part_list = job_ptr->part_ptr_list;
		slurmdb_qos_rec_t *tmp_qos = job_ptr->qos_ptr;

		/*
		 * Force a start so the association doesn't get lost.  Since
		 * there could be some delay in the start of the job when
		 * running with the slurmdbd.
		 */
		if (!IS_JOB_IN_DB(job_ptr))
			jobacct_storage_g_job_start(acct_db_conn, job_ptr);

		/*
		 * Don't call acct_policy_remove_accrue_time() here, the cnt on
		 * parent associations will be handled correctly by the removal
		 * of the association.
		 */

		/*
		 * Clear ptrs so that only association usage is removed.
		 * Otherwise qos and partition limits will be double accounted
		 * for when this job finishes. Don't do this for acrrual time,
		 * it has be on both because the job is ineligible and can't
		 * accrue time.
		 */
		job_ptr->part_ptr = NULL;
		job_ptr->part_ptr_list = NULL;
		job_ptr->qos_ptr = NULL;

		acct_policy_remove_job_submit(job_ptr, assoc_locked);

		job_ptr->part_ptr = tmp_part;
		job_ptr->part_ptr_list = tmp_part_list;
		job_ptr->qos_ptr = tmp_qos;

		job_ptr->assoc_ptr = NULL;
		/* Don't clear assoc_id, since that is what the job requests */
	}

	job_ptr->assoc_id = 0;

	return rc;
}

extern int job_fail_qos(job_record_t *job_ptr, const char *func_name,
			bool assoc_locked)
{
	int rc = 0; // Return number of pending jobs held

	if (IS_JOB_FINISHED(job_ptr)) {
		/*
		 * The acct_policy has already be cleared for this job.  Just
		 * reset the pointer.
		 */
		job_ptr->qos_ptr = NULL;
		job_ptr->qos_id = 0;
		return rc;
	}

	if (IS_JOB_PENDING(job_ptr)) {
		info("%s: %pJ ineligible due to invalid qos",
		     func_name, job_ptr);

		xfree(job_ptr->state_desc);
		job_ptr->state_reason = FAIL_QOS;

		if (job_ptr->details) {
			/* reset the job */
			acct_policy_remove_accrue_time(job_ptr, assoc_locked);
			job_ptr->details->begin_time = 0;
			/* Update job with new begin_time. */
			jobacct_storage_g_job_start(acct_db_conn, job_ptr);
		}
		rc = 1;
	}

	/* This job is no longer eligible, so make it so. */
	if (job_ptr->qos_ptr) {
		slurmdb_assoc_rec_t *tmp_assoc = job_ptr->assoc_ptr;

		/*
		 * Force a start so the qos doesn't get lost.  Since
		 * there could be some delay in the start of the job when
		 * running with the slurmdbd.
		 */
		if (!IS_JOB_IN_DB(job_ptr))
			jobacct_storage_g_job_start(acct_db_conn, job_ptr);

		/*
		 * Clear ptrs so that only qos usage is removed. Otherwise
		 * association limits will be double accounted for when this
		 * job finishes. Don't do this for acrrual time, it has be on
		 * both because the job is ineligible and can't accrue time.
		 */
		job_ptr->assoc_ptr = NULL;

		acct_policy_remove_job_submit(job_ptr, assoc_locked);

		job_ptr->assoc_ptr = tmp_assoc;

		job_ptr->qos_ptr = NULL;
		FREE_NULL_LIST(job_ptr->qos_list);
		/*
		 * Don't clear qos_id or details->qos_req, since that is what
		 * the job requests
		 */
	}

	return rc;
}

/*
 * Functions used to manage job array responses with a separate return code
 * possible for each task ID
 */
/* Add job record to resp_array_struct_t, free with _resp_array_free() */
static void _resp_array_add(resp_array_struct_t **resp, job_record_t *job_ptr,
			    uint32_t rc, char *err_msg)
{
	resp_array_struct_t *loc_resp;
	int array_size;
	int i;

	if ((job_ptr->array_task_id == NO_VAL) &&
	    (job_ptr->array_recs == NULL)) {
		error("%s: called for non-job array %pJ",
		      __func__, job_ptr);
		return;
	}

	if (max_array_size == NO_VAL)
		max_array_size = slurm_conf.max_array_sz;

	xassert(resp);
	if (*resp == NULL) {
		/* Initialize the data structure */
		loc_resp = xmalloc(sizeof(resp_array_struct_t));
		loc_resp->resp_array_cnt  = 0;
		loc_resp->resp_array_size = 10;
		xrecalloc(loc_resp->resp_array_rc, loc_resp->resp_array_size,
			  sizeof(uint32_t));
		xrecalloc(loc_resp->resp_array_task_id,
			  loc_resp->resp_array_size,
			  sizeof(bitstr_t *));
		xrecalloc(loc_resp->err_msg, loc_resp->resp_array_size,
			  sizeof(char *));
		*resp = loc_resp;
	} else {
		loc_resp = *resp;
	}

	for (i = 0; i < loc_resp->resp_array_cnt; i++) {
		if (loc_resp->resp_array_rc[i] != rc)
			continue;
		/* Add to existing error code record */
		if (job_ptr->array_task_id != NO_VAL) {
			if (job_ptr->array_task_id <
			    bit_size(loc_resp->resp_array_task_id[i])) {
				bit_set(loc_resp->resp_array_task_id[i],
					job_ptr->array_task_id);
			} else {
				error("%s: found invalid task id %pJ",
				      __func__, job_ptr);
			}
		} else if (job_ptr->array_recs &&
			   job_ptr->array_recs->task_id_bitmap) {
			array_size = bit_size(job_ptr->array_recs->
					      task_id_bitmap);
			if (bit_size(loc_resp->resp_array_task_id[i]) !=
			    array_size) {
				bit_realloc(loc_resp->resp_array_task_id[i],
					    array_size);
			}
			bit_or(loc_resp->resp_array_task_id[i],
			       job_ptr->array_recs->task_id_bitmap);
		} else {
			error("%s: found job %pJ without task ID or bitmap",
			      __func__, job_ptr);
		}
		return;
	}

	/* Need to add a new record for this error code */
	if (loc_resp->resp_array_cnt >= loc_resp->resp_array_size) {
		/* Need to grow the table size */
		loc_resp->resp_array_size += 10;
		xrecalloc(loc_resp->resp_array_rc, loc_resp->resp_array_size,
			  sizeof(uint32_t));
		xrecalloc(loc_resp->resp_array_task_id,
			  loc_resp->resp_array_size,
			  sizeof(bitstr_t *));
		xrecalloc(loc_resp->err_msg, loc_resp->resp_array_size,
			  sizeof(bitstr_t *));
	}

	loc_resp->resp_array_rc[loc_resp->resp_array_cnt] = rc;
	loc_resp->err_msg[loc_resp->resp_array_cnt] = xstrdup(err_msg);
	if (job_ptr->array_task_id != NO_VAL) {
		loc_resp->resp_array_task_id[loc_resp->resp_array_cnt] =
			bit_alloc(max_array_size);
		if (job_ptr->array_task_id <
		    bit_size(loc_resp->resp_array_task_id
			     [loc_resp->resp_array_cnt])) {
			bit_set(loc_resp->resp_array_task_id
				[loc_resp->resp_array_cnt],
				job_ptr->array_task_id);
		}
	} else if (job_ptr->array_recs && job_ptr->array_recs->task_id_bitmap) {
		loc_resp->resp_array_task_id[loc_resp->resp_array_cnt] =
			bit_copy(job_ptr->array_recs->task_id_bitmap);
	} else {
		error("%s: found %pJ without task ID or bitmap",
		      __func__, job_ptr);
		loc_resp->resp_array_task_id[loc_resp->resp_array_cnt] =
			bit_alloc(max_array_size);
	}
	loc_resp->resp_array_cnt++;
}

/* Add record to resp_array_struct_t, free with _resp_array_free().
 * This is a variant of _resp_array_add for the case where a job/task ID
 * is not found, so we use a dummy job record based upon the input IDs. */
static void _resp_array_add_id(resp_array_struct_t **resp, uint32_t job_id,
			       uint32_t task_id, uint32_t rc)
{
	job_record_t job_ptr;

	job_ptr.job_id = job_id;
	job_ptr.array_job_id = job_id;
	job_ptr.array_task_id = task_id;
	job_ptr.array_recs = NULL;
	_resp_array_add(resp, &job_ptr, rc, NULL);
}

/* Free resp_array_struct_t built by _resp_array_add() */
static void _resp_array_free(resp_array_struct_t *resp)
{
	int i;

	if (resp) {
		for (i = 0; i < resp->resp_array_cnt; i++) {
			FREE_NULL_BITMAP(resp->resp_array_task_id[i]);
			xfree(resp->err_msg[i]);
		}
		xfree(resp->err_msg);
		xfree(resp->resp_array_task_id);
		xfree(resp->resp_array_rc);
		xfree(resp);
	}
}

/* Translate internal job array data structure into a response message */
static job_array_resp_msg_t *_resp_array_xlate(resp_array_struct_t *resp,
					       uint32_t job_id)
{
	job_array_resp_msg_t *msg;
	char task_str[ARRAY_ID_BUF_SIZE];
	int *ffs = NULL;
	int i, j, low;

	ffs = xcalloc(resp->resp_array_cnt, sizeof(int));
	for (i = 0; i < resp->resp_array_cnt; i++) {
		ffs[i] = bit_ffs(resp->resp_array_task_id[i]);
	}

	msg = xmalloc(sizeof(job_array_resp_msg_t));
	msg->job_array_count = resp->resp_array_cnt;
	msg->job_array_id = xcalloc(resp->resp_array_cnt, sizeof(char *));
	msg->error_code = xcalloc(resp->resp_array_cnt, sizeof(uint32_t));
	msg->err_msg = xcalloc(resp->resp_array_cnt, sizeof(char *));
	for (i = 0; i < resp->resp_array_cnt; i++) {
		low = -1;
		for (j = 0; j < resp->resp_array_cnt; j++) {
			if ((ffs[j] != -1) &&
			    ((low == -1) || (ffs[j] < ffs[low])))
				low = j;
		}
		if (low == -1)
			break;
		ffs[low] = -1;

		msg->error_code[i] = resp->resp_array_rc[low];
		msg->err_msg[i] = xstrdup(resp->err_msg[low]);
		bit_fmt(task_str, ARRAY_ID_BUF_SIZE,
			resp->resp_array_task_id[low]);
		if (strlen(task_str) >= ARRAY_ID_BUF_SIZE - 2) {
			/* Append "..." to the buffer on overflow */
			task_str[ARRAY_ID_BUF_SIZE - 4] = '.';
			task_str[ARRAY_ID_BUF_SIZE - 3] = '.';
			task_str[ARRAY_ID_BUF_SIZE - 2] = '.';
			task_str[ARRAY_ID_BUF_SIZE - 1] = '\0';
		}
		xstrfmtcat(msg->job_array_id[i], "%u_%s", job_id, task_str);
	}

	xfree(ffs);
	return msg;
}

static int _add_job_record(job_record_t *job_ptr, int num_jobs)
{
	if ((job_count + num_jobs) >= slurm_conf.max_job_cnt) {
		error("%s: MaxJobCount limit from slurm.conf reached (%u)",
		      __func__, slurm_conf.max_job_cnt);
		return SLURM_ERROR;
	}
	job_count += num_jobs;
	last_job_update = time(NULL);
	list_append(job_list, job_ptr);

	return SLURM_SUCCESS;
}

/*
 * _create_job_record - create an empty job_record including job_details.
 *	load its values with defaults (zeros, nulls, and magic cookie)
 * IN num_jobs - number of jobs this record should represent
 *    = 0 - split out a job array record to its own job record
 *    = 1 - simple job OR job array with one task
 *    > 1 - job array create with the task count as num_jobs
 * IN list_add - add to the joblist or not.
 * RET pointer to the record or NULL if error
 * NOTE: allocates memory that should be xfreed with job_record_delete
 */
static job_record_t *_create_job_record(uint32_t num_jobs, bool list_add)
{
	job_record_t *job_ptr = job_record_create();

	if (list_add) {
		_add_job_record(job_ptr, num_jobs);
	}

	return job_ptr;
}

/*
 * delete_job_desc_files - delete job descriptor related files
 *
 * Note that this will be called on all individual job array tasks,
 * even though (as of 17.11) individual directories are no longer created.
 */
extern void delete_job_desc_files(uint32_t job_id)
{
	char *dir_name = NULL, *file_name = NULL;
	int hash = job_id % 10;
	DIR *f_dir;
	struct dirent *dir_ent;

	dir_name = xstrdup_printf("%s/hash.%d/job.%u",
	                          slurm_conf.state_save_location,
	                          hash, job_id);

	f_dir = opendir(dir_name);
	if (f_dir) {
		while ((dir_ent = readdir(f_dir))) {
			if (!xstrcmp(dir_ent->d_name, ".") ||
			    !xstrcmp(dir_ent->d_name, ".."))
				continue;
			xstrfmtcat(file_name, "%s/%s", dir_name,
				   dir_ent->d_name);
			(void) unlink(file_name);
			xfree(file_name);
		}
		closedir(f_dir);
	} else if (errno == ENOENT) {
		xfree(dir_name);
		return;
	} else {
		error("opendir(%s): %m", dir_name);
	}

	(void) rmdir(dir_name);
	xfree(dir_name);
}

static uint32_t _max_switch_wait(uint32_t input_wait)
{
	static time_t sched_update = 0;
	static uint32_t max_wait = 300;	/* default max_switch_wait, seconds */
	int i;

	if (sched_update != slurm_conf.last_update) {
		char *tmp_ptr;
		sched_update = slurm_conf.last_update;
		if ((tmp_ptr = xstrcasestr(slurm_conf.sched_params,
		                           "max_switch_wait="))) {
			/*                  0123456789012345 */
			i = atoi(tmp_ptr + 16);
			if (i < 0) {
				error("ignoring SchedulerParameters: "
				      "max_switch_wait of %d", i);
			} else {
				max_wait = i;
			}
		}
	}

	if (max_wait > input_wait)
		return input_wait;
	return max_wait;
}

static slurmdb_qos_rec_t *_determine_and_validate_qos(
	char *resv_name, slurmdb_assoc_rec_t *assoc_ptr,
	bool operator, slurmdb_qos_rec_t *qos_rec, int *error_code,
	bool locked, log_level_t log_lvl)
{
	slurmdb_qos_rec_t *qos_ptr = NULL;

	/* If enforcing associations make sure this is a valid qos
	   with the association.  If not just fill in the qos and
	   continue. */

	xassert(qos_rec);

	assoc_mgr_get_default_qos_info(assoc_ptr, qos_rec);
	if (assoc_mgr_fill_in_qos(acct_db_conn, qos_rec, accounting_enforce,
				  &qos_ptr, locked) != SLURM_SUCCESS) {
		log_var(log_lvl, "Invalid qos (%s)", qos_rec->name);
		*error_code = ESLURM_INVALID_QOS;
		return NULL;
	}

	if ((accounting_enforce & ACCOUNTING_ENFORCE_QOS)
	    && assoc_ptr
	    && !operator
	    && (!assoc_ptr->usage->valid_qos
		|| !bit_test(assoc_ptr->usage->valid_qos, qos_rec->id))) {
		log_var(log_lvl, "This association %d(account='%s', user='%s', partition='%s') does not have access to qos %s",
		        assoc_ptr->id, assoc_ptr->acct, assoc_ptr->user,
		        assoc_ptr->partition, qos_rec->name);
		*error_code = ESLURM_INVALID_QOS;
		return NULL;
	}

	if (qos_ptr) {
		if ((qos_ptr->flags & QOS_FLAG_RELATIVE) &&
		    (qos_ptr->flags & QOS_FLAG_PART_QOS)) {
			log_var(log_lvl, "QOS %s is relative and used as a Partition QOS. This prohibits it from being used as a job's QOS",
				qos_rec->name);
			*error_code = ESLURM_INVALID_QOS;
			return NULL;
		}

		if ((qos_ptr->flags & QOS_FLAG_REQ_RESV) &&
		    (!resv_name || resv_name[0] == '\0')) {
			log_var(log_lvl, "qos %s can only be used in a reservation",
				qos_rec->name);
			*error_code = ESLURM_INVALID_QOS;
			return NULL;
		}
	}

	*error_code = SLURM_SUCCESS;
	return qos_ptr;
}

static list_t *_get_qos_ptr_list(char *qos_req, char *resv_name,
				 slurmdb_assoc_rec_t *assoc_ptr,
				 bool operator, int *error_code,
				 bool locked, log_level_t log_lvl)
{
	list_t *qos_ptr_list = NULL;
	char *token, *last = NULL, *tmp_qos_req;

	xassert(error_code);

	if (!xstrchr(qos_req, ','))
		return qos_ptr_list;

	tmp_qos_req = xstrdup(qos_req);
	token = strtok_r(tmp_qos_req, ",", &last);
	while (token) {
		slurmdb_qos_rec_t qos_rec = {
			.name = token,
		};
		slurmdb_qos_rec_t *qos_ptr = _determine_and_validate_qos(
			resv_name, assoc_ptr, operator, &qos_rec,
			error_code, locked, log_lvl);

		if (*error_code != SLURM_SUCCESS)
			break;

		/*
		 * This should not happen as the error_code check should catch
		 * issues before we get here.
		 */
		if (!qos_ptr) {
			*error_code = ESLURM_INVALID_QOS;
			break;
		}

		if (!qos_ptr_list)
			qos_ptr_list = list_create(NULL);

		if (!list_find_first_ro(qos_ptr_list,
					slurm_find_ptr_in_list,
					qos_ptr)) {
			list_append(qos_ptr_list, qos_ptr);
		}
		token = strtok_r(NULL, ",", &last);
	}
	xfree(tmp_qos_req);

	/* If we have a trailing comma error out */
	if (qos_ptr_list && (list_count(qos_ptr_list) == 1)) {
		error("%s: Invalid qos (%s), it appears there is a trailing comma",
		      __func__, qos_req);
		*error_code = ESLURM_INVALID_QOS;
	}

	if (*error_code != SLURM_SUCCESS)
		FREE_NULL_LIST(qos_ptr_list);

	if (qos_ptr_list)
		list_sort(qos_ptr_list, priority_sort_qos_desc);

	return qos_ptr_list;
}

static int _get_qos_info(
	char *qos_req, uint32_t qos_id,
	list_t **qos_plist, slurmdb_qos_rec_t **qos_pptr,
	char *resv_name, slurmdb_assoc_rec_t *assoc_ptr,
	bool operator, bool locked, log_level_t log_lvl)
{
	int rc = SLURM_SUCCESS;

	xassert(qos_plist);
	xassert(qos_pptr);
	xassert(!*qos_plist);

	*qos_plist = _get_qos_ptr_list(qos_req, resv_name, assoc_ptr,
				       operator, &rc,
				       locked, log_lvl);

	if (!*qos_plist) {
		slurmdb_qos_rec_t qos_rec = {
			.name = qos_req,
			.id = qos_id,
		};

		*qos_pptr = _determine_and_validate_qos(
			resv_name, assoc_ptr, operator,
			&qos_rec, &rc,
			locked, log_lvl);
	} else {
		*qos_pptr = list_peek(*qos_plist);
	}

	return rc;
}
/*
 * dump_all_job_state - save the state of all jobs to file for checkpoint
 *	Changes here should be reflected in load_last_job_id() and
 *	load_all_job_state().
 * RET 0 or error code
 */
int dump_all_job_state(void)
{
	/* Save high-water mark to avoid buffer growth with copies */
	static uint32_t high_buffer_size = (1024 * 1024);
	int error_code = SLURM_SUCCESS;
	char *reg_file;
	struct stat stat_buf;
	/* Locks: Read config and job */
	slurmctld_lock_t job_read_lock =
		{ READ_LOCK, READ_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };
	buf_t *buffer = init_buf(high_buffer_size);
	time_t now = time(NULL);
	time_t last_state_file_time;
	static time_t last_job_state_size_check = 0;
	uint32_t jobs_start, jobs_end, jobs_count;
	DEF_TIMERS;

	START_TIMER;
	/*
	 * Check that last state file was written at expected time.
	 * This is a check for two slurmctld daemons running at the same
	 * time in primary mode (a split-brain problem).
	 */
	last_state_file_time = _get_last_job_state_write_time();
	if (last_file_write_time && last_state_file_time &&
	    (last_file_write_time != last_state_file_time)) {
		error("Bad job state save file time. We wrote it at time %u, "
		      "but the file contains a time stamp of %u.",
		      (uint32_t) last_file_write_time,
		      (uint32_t) last_state_file_time);
		if (!slurmctld_primary) {
			fatal("Two slurmctld daemons are running as primary. "
			      "Shutting down this daemon to avoid inconsistent "
			      "state due to split brain.");
		}
	}

	/* write header: version, time */
	packstr(JOB_STATE_VERSION, buffer);
	pack16(SLURM_PROTOCOL_VERSION, buffer);
	pack_time(now, buffer);

	/*
	 * write header: job id
	 * This is needed so that the job id remains persistent even after
	 * slurmctld is restarted.
	 */
	pack32( job_id_sequence, buffer);

	debug3("Writing job id %u to header record of job_state file",
	       job_id_sequence);

	/* write individual job records */
	lock_slurmctld(job_read_lock);

	pack_time(slurmctld_diag_stats.bf_when_last_cycle, buffer);

	jobs_start = get_buf_offset(buffer);
	list_for_each_ro(job_list, job_mgr_dump_job_state, buffer);
	jobs_end = get_buf_offset(buffer);
	if ((difftime(now, last_job_state_size_check) > 60) &&
	    (jobs_count = list_count(job_list))) {
		uint64_t ave_job_size = jobs_end - jobs_start;
		uint64_t estimated_job_state_size = ave_job_size *
			slurm_conf.max_job_cnt;
		last_job_state_size_check = time(NULL);
		/*
		 * We assume all jobs were written to buffer, which may not
		 * be true, but in that case we'd already flood the log with
		 * errors.
		 */
		estimated_job_state_size /= jobs_count;
		estimated_job_state_size += jobs_start;
		ave_job_size /= jobs_count;
		if (estimated_job_state_size > MAX_BUF_SIZE)
			error("Configured MaxJobCount may lead to job_state being larger then maximum buffer size and not saved, based on the average job state size(%.2f KiB) we can save state of %"PRIu64" jobs.",
			      (float)ave_job_size / 1024,
			      ((uint64_t)(MAX_BUF_SIZE - jobs_start)) /
			      ave_job_size);
	}

	unlock_slurmctld(job_read_lock);

	reg_file = xstrdup_printf("%s/job_state",
	                          slurm_conf.state_save_location);

	if (stat(reg_file, &stat_buf) == 0) {
		static time_t last_mtime = (time_t) 0;
		int delta_t = difftime(stat_buf.st_mtime, last_mtime);
		if (delta_t < -10) {
			error("The modification time of %s moved backwards "
			      "by %d seconds",
			      reg_file, (0-delta_t));
			error("The clock of the file system and this computer "
			      "appear to not be synchronized");
			/* It could be safest to exit here. We likely mounted
			 * a different file system with the state save files */
		}
		last_mtime = time(NULL);
	}

	error_code = save_buf_to_state("job_state", buffer, &high_buffer_size);
	if (!error_code)
		last_file_write_time = now;

	xfree(reg_file);
	FREE_NULL_BUFFER(buffer);
	END_TIMER2(__func__);
	return error_code;
}

static int _find_resv_part(void *x, void *key)
{
	slurmctld_resv_t *resv_ptr = (slurmctld_resv_t *) x;

	if (resv_ptr->part_ptr != (part_record_t *) key)
		return 0;
	else
		return 1;	/* match */
}

static int _find_part_assoc(void *x, void *key)
{
	part_record_t *part_ptr = (part_record_t *)x;
	slurmdb_assoc_rec_t *assoc_ptr = (slurmdb_assoc_rec_t *) key;
	slurmdb_assoc_rec_t assoc_rec;

	memset(&assoc_rec, 0, sizeof(assoc_rec));
	assoc_rec.acct      = assoc_ptr->acct;
	assoc_rec.partition = part_ptr->name;
	assoc_rec.uid       = assoc_ptr->uid;

	(void) assoc_mgr_fill_in_assoc(acct_db_conn, &assoc_rec,
				       accounting_enforce, NULL, true);

	if (assoc_rec.id != assoc_ptr->id) {
		info("%s: can't check multiple partitions with partition based associations",
		     __func__);
		return 1;
	}
	return 0;
}

static int _check_for_part_assocs(list_t *part_ptr_list,
				  slurmdb_assoc_rec_t *assoc_ptr)
{
	if (assoc_ptr && part_ptr_list &&
	    list_find_first(part_ptr_list, _find_part_assoc, assoc_ptr)) {
		return ESLURM_PARTITION_ASSOC;
	}

	return SLURM_SUCCESS;
}

/* Open the job state save file, or backup if necessary.
 * state_file IN - the name of the state save file used
 * RET the file description to read from or error code
 */
static buf_t *_open_job_state_file(char **state_file)
{
	buf_t *buf;

	xassert(state_file);
	xassert(!*state_file);

	*state_file = xstrdup_printf("%s/job_state",
	                             slurm_conf.state_save_location);

	if (!(buf = create_mmap_buf(*state_file)))
		error("Could not open job state file %s: %m", *state_file);
	else
		return buf;

	error("NOTE: Trying backup state save file. Jobs may be lost!");
	xstrcat(*state_file, ".old");
	return create_mmap_buf(*state_file);
}

extern void set_job_failed_assoc_qos_ptr(job_record_t *job_ptr)
{
	if (!job_ptr->assoc_ptr && (job_ptr->state_reason == FAIL_ACCOUNT)) {
		slurmdb_assoc_rec_t assoc_rec;
		memset(&assoc_rec, 0, sizeof(assoc_rec));
		/*
		 * For speed and accurracy we will first see if we once had an
		 * association record.  If not look for it by
		 * account,partition, user_id.
		 */
		if (job_ptr->assoc_id)
			assoc_rec.id = job_ptr->assoc_id;
		else {
			assoc_rec.acct      = job_ptr->account;
			if (job_ptr->part_ptr)
				assoc_rec.partition = job_ptr->part_ptr->name;
			assoc_rec.uid       = job_ptr->user_id;
		}

		if (assoc_mgr_fill_in_assoc(acct_db_conn, &assoc_rec,
		                            accounting_enforce,
		                            &job_ptr->assoc_ptr, false) ==
		    SLURM_SUCCESS) {
			job_ptr->assoc_id = assoc_rec.id;
			debug("%s: Filling in assoc for %pJ Assoc=%u",
			      __func__, job_ptr, job_ptr->assoc_id);

			job_ptr->state_reason = WAIT_NO_REASON;
			xfree(job_ptr->state_desc);
			last_job_update = time(NULL);
		}
	}

	/*
	 * This shouldn't matter if there is a qos_list as that will get
	 * handled after this is called.
	 */
	if (!job_ptr->qos_ptr && (job_ptr->state_reason == FAIL_QOS)) {
		int qos_error = SLURM_SUCCESS;
		slurmdb_qos_rec_t qos_rec;
		memset(&qos_rec, 0, sizeof(qos_rec));
		qos_rec.id = job_ptr->qos_id;
		job_ptr->qos_ptr = _determine_and_validate_qos(
			job_ptr->resv_name, job_ptr->assoc_ptr,
			job_ptr->limit_set.qos, &qos_rec,
			&qos_error, false, LOG_LEVEL_DEBUG2);

		if ((qos_error == SLURM_SUCCESS) && job_ptr->qos_ptr) {
			/* job_ptr->qos_id should never start at 0 */
			if (job_ptr->qos_id != qos_rec.id) {
				error("%s: Changing job_ptr->qos_id from %u to %u; this should never happen",
				      __func__, job_ptr->qos_id, qos_rec.id);
				job_ptr->qos_id = qos_rec.id;
			}
			debug("%s: Filling in QOS for %pJ QOS=%s(%u)",
			      __func__, job_ptr, qos_rec.name, job_ptr->qos_id);
			job_ptr->state_reason = WAIT_NO_REASON;
			xfree(job_ptr->state_desc);
			last_job_update = time(NULL);
		}
	}
}

extern void set_job_tres_req_str(job_record_t *job_ptr, bool assoc_mgr_locked)
{
	assoc_mgr_lock_t locks = { .tres = READ_LOCK };
	xassert(job_ptr);

	if (!assoc_mgr_locked)
		assoc_mgr_lock(&locks);

	xfree(job_ptr->tres_req_str);
	job_ptr->tres_req_str = assoc_mgr_make_tres_str_from_array(
		job_ptr->tres_req_cnt, TRES_STR_FLAG_SIMPLE, true);

	xfree(job_ptr->tres_fmt_req_str);
	job_ptr->tres_fmt_req_str = assoc_mgr_make_tres_str_from_array(
		job_ptr->tres_req_cnt, TRES_STR_CONVERT_UNITS, true);

	if (!assoc_mgr_locked)
		assoc_mgr_unlock(&locks);
}

/* Note that the backup slurmctld has assumed primary control.
 * This function can be called multiple times. */
extern void backup_slurmctld_restart(void)
{
	last_file_write_time = (time_t) 0;
}

/* Return the time stamp in the current job state save file, 0 is returned on
 * error */
static time_t _get_last_job_state_write_time(void)
{
	int error_code = SLURM_SUCCESS;
	char *state_file = NULL;
	buf_t *buffer;
	time_t buf_time = (time_t) 0;
	char *ver_str = NULL;
	uint16_t protocol_version = NO_VAL16;

	/* read the file */
	if (!(buffer = _open_job_state_file(&state_file))) {
		info("No job state file (%s) found", state_file);
		error_code = ENOENT;
	}
	xfree(state_file);
	if (error_code)
		return buf_time;

	safe_unpackstr(&ver_str, buffer);
	if (ver_str && !xstrcmp(ver_str, JOB_STATE_VERSION))
		safe_unpack16(&protocol_version, buffer);
	safe_unpack_time(&buf_time, buffer);

unpack_error:
	xfree(ver_str);
	FREE_NULL_BUFFER(buffer);
	return buf_time;
}

/*
 * load_all_job_state - load the job state from file, recover from last
 *	checkpoint. Execute this after loading the configuration file data.
 *	Changes here should be reflected in load_last_job_id().
 * RET 0 or error code
 */
extern int load_all_job_state(void)
{
	int error_code = SLURM_SUCCESS;
	int job_cnt = 0;
	char *state_file = NULL;
	buf_t *buffer;
	time_t buf_time;
	uint32_t saved_job_id;
	char *ver_str = NULL;
	uint16_t protocol_version = NO_VAL16;

	/* read the file */
	lock_state_files();
	if (!(buffer = _open_job_state_file(&state_file))) {
		info("No job state file (%s) to recover", state_file);
		xfree(state_file);
		unlock_state_files();
		return ENOENT;
	}
	xfree(state_file);
	unlock_state_files();

	job_id_sequence = MAX(job_id_sequence, slurm_conf.first_job_id);

	safe_unpackstr(&ver_str, buffer);
	debug3("Version string in job_state header is %s", ver_str);
	if (ver_str && !xstrcmp(ver_str, JOB_STATE_VERSION))
		safe_unpack16(&protocol_version, buffer);
	xfree(ver_str);

	if (protocol_version == NO_VAL16) {
		if (!ignore_state_errors)
			fatal("Can not recover job state, incompatible version, start with '-i' to ignore this. Warning: using -i will lose the data that can't be recovered.");
		error("***********************************************");
		error("Can not recover job state, incompatible version");
		error("***********************************************");
		FREE_NULL_BUFFER(buffer);
		return EFAULT;
	}

	safe_unpack_time(&buf_time, buffer);
	safe_unpack32(&saved_job_id, buffer);
	if (saved_job_id <= slurm_conf.max_job_id)
		job_id_sequence = MAX(saved_job_id, job_id_sequence);
	debug3("Job id in job_state header is %u", saved_job_id);

	safe_unpack_time(&buf_time, buffer); /* bf_when_last_cycle */
	if (!slurmctld_diag_stats.bf_when_last_cycle)
		slurmctld_diag_stats.bf_when_last_cycle = buf_time;

	/*
	 * Previously we locked the tres read lock before this loop.  It turned
	 * out that created a double lock when steps were being loaded during
	 * the calls to jobacctinfo_create() which also locks the read lock.
	 * It ended up being much easier to move the locks for the assoc_mgr
	 * into the job_mgr_load_job_state function than any other option.
	 */
	while (remaining_buf(buffer) > 0) {
		error_code = job_mgr_load_job_state(buffer, protocol_version);
		if (error_code != SLURM_SUCCESS)
			goto unpack_error;
		job_cnt++;
	}
	debug3("Set job_id_sequence to %u", job_id_sequence);

	FREE_NULL_BUFFER(buffer);
	info("Recovered information about %d jobs", job_cnt);
	return error_code;

unpack_error:
	if (!ignore_state_errors)
		fatal("Incomplete job state save file, start with '-i' to ignore this. Warning: using -i will lose the data that can't be recovered.");
	error("Incomplete job state save file");
	info("Recovered information about %d jobs", job_cnt);
	FREE_NULL_BUFFER(buffer);
	return SLURM_ERROR;
}

/*
 * load_last_job_id - load only the last job ID from state save file.
 *	Changes here should be reflected in load_all_job_state().
 * RET 0 or error code
 */
extern int load_last_job_id( void )
{
	char *state_file = NULL;
	buf_t *buffer;
	time_t buf_time;
	char *ver_str = NULL;
	uint16_t protocol_version = NO_VAL16;

	/* read the file */
	lock_state_files();
	if (!(buffer = _open_job_state_file(&state_file))) {
		debug("No job state file (%s) to recover", state_file);
		xfree(state_file);
		unlock_state_files();
		return ENOENT;
	}
	xfree(state_file);
	unlock_state_files();

	safe_unpackstr(&ver_str, buffer);
	debug3("Version string in job_state header is %s", ver_str);
	if (ver_str && !xstrcmp(ver_str, JOB_STATE_VERSION))
		safe_unpack16(&protocol_version, buffer);
	xfree(ver_str);

	if (protocol_version == NO_VAL16) {
		if (!ignore_state_errors)
			fatal("Can not recover last job ID, incompatible version, start with '-i' to ignore this. Warning: using -i will lose the data that can't be recovered.");
		debug("*************************************************");
		debug("Can not recover last job ID, incompatible version");
		debug("*************************************************");
		FREE_NULL_BUFFER(buffer);
		return EFAULT;
	}

	safe_unpack_time(&buf_time, buffer);
	safe_unpack32( &job_id_sequence, buffer);
	debug3("Job ID in job_state header is %u", job_id_sequence);

	/* Ignore the state for individual jobs stored here */

	xfree(ver_str);
	FREE_NULL_BUFFER(buffer);
	return SLURM_SUCCESS;

unpack_error:
	if (!ignore_state_errors)
		fatal("Invalid job data checkpoint file, start with '-i' to ignore this. Warning: using -i will lose the data that can't be recovered.");
	error("Invalid job data checkpoint file");
	xfree(ver_str);
	FREE_NULL_BUFFER(buffer);
	return SLURM_ERROR;
}

extern int job_mgr_dump_job_state(void *object, void *arg)
{
	job_record_t *dump_job_ptr = object;
	buf_t *buffer = arg;

	xassert(dump_job_ptr->magic == JOB_MAGIC);

	/* Don't pack "unlinked" job. */
	if (dump_job_ptr->job_id == NO_VAL)
		return 0;

	if (dump_job_ptr->array_recs)
		build_array_str(dump_job_ptr);
	_update_job_nodes_str(dump_job_ptr);

	job_record_pack(dump_job_ptr, slurmctld_tres_cnt, buffer,
			SLURM_PROTOCOL_VERSION);
	return 0;
}

extern int job_mgr_load_job_state(buf_t *buffer,
				  uint16_t protocol_version)
{
	char *err_part = NULL;
	time_t now = time(NULL);
	job_record_t *job_ptr = NULL;
	int rc;
	slurmdb_assoc_rec_t assoc_rec;
	bool job_finished = false;
	assoc_mgr_lock_t locks = {
		.assoc = WRITE_LOCK,
		.qos = WRITE_LOCK,
		.tres = READ_LOCK,
		.user = READ_LOCK
	};

	if (job_record_unpack(&job_ptr, slurmctld_tres_cnt, buffer,
			      protocol_version)) {
		error("failed to load job from state");
		goto unpack_error;
	}

	if (find_job_record(job_ptr->job_id)) {
		error("duplicate job state record found for %pJ", job_ptr);
		goto unpack_error;
	} else if (_add_job_record(job_ptr, 1)) {
		rc = SLURM_SUCCESS;
		job_record_delete(job_ptr);
		job_ptr = NULL;
		goto free_it;
	}

	/* "Don't load "unlinked" job. */
	if (job_ptr->job_id == NO_VAL) {
		debug("skipping unlinked job");
		rc = SLURM_SUCCESS;
		goto free_it;
	}

	if ((job_ptr->job_state & JOB_STATE_BASE) >= JOB_END) {
		error("Invalid data for JobId=%u: job_state=%u",
		      job_ptr->job_id, job_ptr->job_state);
		goto unpack_error;
	}
	if (job_ptr->kill_on_node_fail > 1) {
		error("Invalid data for JobId=%u: kill_on_node_fail=%u",
		      job_ptr->job_id, job_ptr->kill_on_node_fail);
		goto unpack_error;
	}

	if ((job_ptr->priority > 1) && (job_ptr->direct_set_prio == 0)) {
		highest_prio = MAX(highest_prio, job_ptr->priority);
		lowest_prio  = MIN(lowest_prio,  job_ptr->priority);
	}

	get_part_list(job_ptr->partition, &job_ptr->part_ptr_list,
		      &job_ptr->part_ptr, &err_part);
	if (job_ptr->part_ptr == NULL) {
		verbose("Invalid partition (%s) for JobId=%u",
			err_part, job_ptr->job_id);
		xfree(err_part);
		/* not fatal error, partition could have been
		 * removed, reset_job_bitmaps() will clean-up
		 * this job */
	}

#if 0
	/*
	 * This is not necessary since the job_id_sequence is checkpointed and
	 * the jobid will be checked if it's in use in get_next_job_id().
	 */

	/* Base job_id_sequence off of local job id but only if the job
	 * originated from this cluster -- so that the local job id of a
	 * different cluster isn't restored here. */
	if (!job_fed_details ||
	    !xstrcmp(job_fed_details->origin_str, slurm_conf.cluster_name))
		local_job_id = fed_mgr_get_local_id(job_id);
	if (job_id_sequence <= local_job_id)
		job_id_sequence = local_job_id + 1;
#endif

	if (job_ptr->array_recs && (job_ptr->array_recs->task_cnt > 1))
		job_count += (job_ptr->array_recs->task_cnt - 1);

	xstrtolower(job_ptr->account);
	job_state_set(job_ptr, job_ptr->job_state);
	job_ptr->time_last_active = now;

	if (IS_JOB_PENDING(job_ptr))
		job_ptr->node_cnt_wag = job_ptr->total_nodes;

	/*
	 * This needs to always to initialized to "true".  The select
	 * plugin will deal with it every time it goes through the
	 * logic if req_switch or wait4switch are set.
	 */
	job_ptr->best_switch     = true;

	/* If start_protocol_ver is too old, reset to current version. */
	if (job_ptr->start_protocol_ver < SLURM_MIN_PROTOCOL_VERSION)
		job_ptr->start_protocol_ver = SLURM_PROTOCOL_VERSION;

	/* Handle this after user_id and other identity has been filled in */
	if (!job_ptr->mail_user) {
		job_ptr->mail_user = _get_mail_user(NULL, job_ptr);
	}

	_add_job_hash(job_ptr);
	_add_job_array_hash(job_ptr);

	memset(&assoc_rec, 0, sizeof(assoc_rec));

	/*
	 * For speed and accurracy we will first see if we once had an
	 * association record.  If not look for it by
	 * account,partition, user_id.
	 */
	if (job_ptr->assoc_id)
		assoc_rec.id = job_ptr->assoc_id;
	else {
		assoc_rec.acct      = job_ptr->account;
		if (job_ptr->part_ptr)
			assoc_rec.partition = job_ptr->part_ptr->name;
		assoc_rec.uid       = job_ptr->user_id;
	}

	assoc_mgr_lock(&locks);
	if (assoc_mgr_fill_in_assoc(acct_db_conn, &assoc_rec,
				    accounting_enforce,
				    &job_ptr->assoc_ptr, true) &&
	    (accounting_enforce & ACCOUNTING_ENFORCE_ASSOCS)) {
		_job_fail_account(job_ptr, __func__, true);
	} else {
		job_ptr->assoc_id = assoc_rec.id;
		info("Recovered %pJ Assoc=%u", job_ptr, job_ptr->assoc_id);

		if (job_ptr->state_reason == FAIL_ACCOUNT) {
			job_ptr->state_reason = WAIT_NO_REASON;
			xfree(job_ptr->state_desc);
		}

		/* make sure we have started this job in accounting */
		if (!IS_JOB_IN_DB(job_ptr)) {
			debug("starting %pJ in accounting", job_ptr);
			jobacct_storage_g_job_start(acct_db_conn, job_ptr);
			if (slurmctld_init_db
			    && IS_JOB_SUSPENDED(job_ptr)) {
				jobacct_storage_g_job_suspend(acct_db_conn,
							      job_ptr);
			}
		}
		/* make sure we have this job completed in the database */
		if (IS_JOB_FINISHED(job_ptr)) {
			if (slurmctld_init_db &&
			    !(job_ptr->bit_flags & TRES_STR_CALC) &&
			    job_ptr->tres_alloc_cnt &&
			    (job_ptr->tres_alloc_cnt[TRES_ENERGY] != NO_VAL64))
				assoc_mgr_set_job_tres_alloc_str(job_ptr,
								 false);
			jobacct_storage_g_job_complete(
				acct_db_conn, job_ptr);
			job_finished = 1;
		}
	}

	if (!job_finished && (job_ptr->qos_id || job_ptr->details->qos_req) &&
	    (job_ptr->state_reason != FAIL_ACCOUNT)) {
		int qos_error = _get_qos_info(job_ptr->details->qos_req,
					      job_ptr->qos_id,
					      &job_ptr->qos_list,
					      &job_ptr->qos_ptr,
					      job_ptr->resv_name,
					      job_ptr->assoc_ptr,
					      job_ptr->limit_set.qos,
					      true, LOG_LEVEL_ERROR);

		if ((qos_error != SLURM_SUCCESS) &&
		    !job_ptr->limit_set.qos) {
			job_fail_qos(job_ptr, __func__, true);
		} else if (job_ptr->qos_ptr) {
			job_ptr->qos_id = job_ptr->qos_ptr->id;
			if (job_ptr->state_reason == FAIL_QOS) {
				job_ptr->state_reason = WAIT_NO_REASON;
				xfree(job_ptr->state_desc);
			}
		}
	}

	/*
	 * do this after the format string just in case for some
	 * reason the tres_alloc_str is NULL but not the fmt_str
	 */
	if (job_ptr->tres_alloc_str)
		assoc_mgr_set_tres_cnt_array(
			&job_ptr->tres_alloc_cnt, job_ptr->tres_alloc_str,
			0, true, false, NULL);
	else
		job_set_alloc_tres(job_ptr, true);

	if (job_ptr->tres_req_str)
		assoc_mgr_set_tres_cnt_array(
			&job_ptr->tres_req_cnt, job_ptr->tres_req_str, 0, true,
			false, NULL);
	else
		job_set_req_tres(job_ptr, true);
	assoc_mgr_unlock(&locks);

	build_node_details(job_ptr, false);	/* set node_addr */
	gres_stepmgr_job_build_details(
		job_ptr->gres_list_alloc, job_ptr->nodes,
		&job_ptr->gres_detail_cnt,
		&job_ptr->gres_detail_str,
		&job_ptr->gres_used);

	on_job_state_change(job_ptr, job_ptr->job_state);
	last_job_update = now;
	return SLURM_SUCCESS;

unpack_error:
	error("Incomplete job record");
	rc = SLURM_ERROR;

free_it:
	if (job_ptr) {
		if (job_ptr->job_id == 0)
			job_ptr->job_id = NO_VAL;
		purge_job_record(job_ptr->job_id);
	}

	return rc;
}

/* _add_job_hash - add a job hash entry for given job record, job_id must
 *	already be set
 * IN job_ptr - pointer to job record
 * Globals: hash table updated
 */
static void _add_job_hash(job_record_t *job_ptr)
{
	int inx;

	inx = JOB_HASH_INX(job_ptr->job_id);
	job_ptr->job_next = job_hash[inx];
	job_hash[inx] = job_ptr;
}

/* _remove_job_hash - remove a job hash entry for given job record, job_id must
 *	already be set
 * IN job_ptr - pointer to job record
 * IN type - which hash to work with
 * Globals: hash table updated
 */
static void _remove_job_hash(job_record_t *job_entry, job_hash_type_t type)
{
	job_record_t *job_ptr, **job_pptr;

	xassert(job_entry);

	on_job_state_change(job_entry, NO_VAL);

	switch (type) {
	case JOB_HASH_JOB:
		job_pptr = &job_hash[JOB_HASH_INX(job_entry->job_id)];
		break;
	case JOB_HASH_ARRAY_JOB:
		job_pptr = &job_array_hash_j[
			JOB_HASH_INX(job_entry->array_job_id)];
		break;
	case JOB_HASH_ARRAY_TASK:
		job_pptr = &job_array_hash_t[
			JOB_ARRAY_HASH_INX(job_entry->array_job_id,
					   job_entry->array_task_id)];
		break;
	default:
		fatal("%s: unknown job_hash_type_t %d", __func__, type);
		return;
	}

	while ((job_pptr != NULL) && (*job_pptr != NULL) &&
	       ((job_ptr = *job_pptr) != job_entry)) {
		xassert(job_ptr->magic == JOB_MAGIC);
		switch (type) {
		case JOB_HASH_JOB:
			job_pptr = &job_ptr->job_next;
			break;
		case JOB_HASH_ARRAY_JOB:
			job_pptr = &job_ptr->job_array_next_j;
			break;
		case JOB_HASH_ARRAY_TASK:
			job_pptr = &job_ptr->job_array_next_t;
			break;
		}
	}

	if (job_pptr == NULL || *job_pptr == NULL) {
		if (job_entry->job_id == NO_VAL)
			return;

		switch (type) {
		case JOB_HASH_JOB:
			error("%s: Could not find hash entry for JobId=%u",
			      __func__, job_entry->job_id);
			break;
		case JOB_HASH_ARRAY_JOB:
			error("%s: job array hash error %u", __func__,
			      job_entry->array_job_id);
			break;
		case JOB_HASH_ARRAY_TASK:
			error("%s: job array, task ID hash error %u_%u",
			      __func__,
			      job_entry->array_job_id,
			      job_entry->array_task_id);
			break;
		}
		return;
	}

	switch (type) {
	case JOB_HASH_JOB:
		*job_pptr = job_entry->job_next;
		job_entry->job_next = NULL;
		break;
	case JOB_HASH_ARRAY_JOB:
		*job_pptr = job_entry->job_array_next_j;
		job_entry->job_array_next_j = NULL;
		break;
	case JOB_HASH_ARRAY_TASK:
		*job_pptr = job_entry->job_array_next_t;
		job_entry->job_array_next_t = NULL;
		break;
	}
}

/* _add_job_array_hash - add a job hash entry for given job record,
 *	array_job_id and array_task_id must already be set
 * IN job_ptr - pointer to job record
 * Globals: hash table updated
 */
void _add_job_array_hash(job_record_t *job_ptr)
{
	int inx;

	if (job_ptr->array_task_id == NO_VAL)
		return;	/* Not a job array */

	xassert(verify_lock(JOB_LOCK, WRITE_LOCK));

	inx = JOB_HASH_INX(job_ptr->array_job_id);
	job_ptr->job_array_next_j = job_array_hash_j[inx];
	job_array_hash_j[inx] = job_ptr;

	inx = JOB_ARRAY_HASH_INX(job_ptr->array_job_id,job_ptr->array_task_id);
	job_ptr->job_array_next_t = job_array_hash_t[inx];
	job_array_hash_t[inx] = job_ptr;
}

/* For the job array data structure, build the string representation of the
 * bitmap.
 * NOTE: bit_fmt_hexmask() is far more scalable than bit_fmt(). */
extern void build_array_str(job_record_t *job_ptr)
{
	job_array_struct_t *array_recs = job_ptr->array_recs;

	if (!array_recs || array_recs->task_id_str ||
	    !array_recs->task_id_bitmap ||
	    (job_ptr->array_task_id != NO_VAL) ||
	    (bit_ffs(job_ptr->array_recs->task_id_bitmap) == -1))
		return;

	array_recs->task_id_str = bit_fmt_hexmask(array_recs->task_id_bitmap);

	/* Update the job in the database. */
	jobacct_storage_g_job_start(acct_db_conn, job_ptr);
}

/* Return true if ALL tasks of specific array job ID are complete */
extern bool test_job_array_complete(uint32_t array_job_id)
{
	job_record_t *job_ptr;
	int inx;

	job_ptr = find_job_record(array_job_id);
	if (job_ptr) {
		if (!IS_JOB_COMPLETE(job_ptr))
			return false;
		if (job_ptr->array_recs && job_ptr->array_recs->max_exit_code)
			return false;
	}

	/* Need to test individual job array records */
	inx = JOB_HASH_INX(array_job_id);
	job_ptr = job_array_hash_j[inx];
	while (job_ptr) {
		if (job_ptr->array_job_id == array_job_id) {
			if (!IS_JOB_COMPLETE(job_ptr))
				return false;
		}
		job_ptr = job_ptr->job_array_next_j;
	}
	return true;
}

/* Return true if ALL tasks of specific array job ID are completed */
extern bool test_job_array_completed(uint32_t array_job_id)
{
	job_record_t *job_ptr;
	int inx;

	job_ptr = find_job_record(array_job_id);
	if (job_ptr) {
		if (!IS_JOB_COMPLETED(job_ptr))
			return false;
	}

	/* Need to test individual job array records */
	inx = JOB_HASH_INX(array_job_id);
	job_ptr = job_array_hash_j[inx];
	while (job_ptr) {
		if (job_ptr->array_job_id == array_job_id) {
			if (!IS_JOB_COMPLETED(job_ptr))
				return false;
		}
		job_ptr = job_ptr->job_array_next_j;
	}
	return true;
}

/*
 * Return true if ALL tasks of specific array job ID are completed AND
 * all except for the head job have been purged.
 */
static bool _test_job_array_purged(uint32_t array_job_id)
{
	job_record_t *job_ptr, *head_job_ptr;
	int inx;

	head_job_ptr = find_job_record(array_job_id);
	if (head_job_ptr) {
		if (!IS_JOB_COMPLETED(head_job_ptr))
			return false;
	}

	/* Need to test individual job array records */
	inx = JOB_HASH_INX(array_job_id);
	job_ptr = job_array_hash_j[inx];
	while (job_ptr) {
		if ((job_ptr->array_job_id == array_job_id) &&
		    (job_ptr != head_job_ptr)) {
			return false;
		}
		job_ptr = job_ptr->job_array_next_j;
	}
	return true;
}

/* Return true if ALL tasks of specific array job ID are finished */
extern bool test_job_array_finished(uint32_t array_job_id)
{
	job_record_t *job_ptr;
	int inx;

	job_ptr = find_job_record(array_job_id);
	if (job_ptr) {
		if (!IS_JOB_FINISHED(job_ptr))
			return false;
	}

	/* Need to test individual job array records */
	inx = JOB_HASH_INX(array_job_id);
	job_ptr = job_array_hash_j[inx];
	while (job_ptr) {
		if (job_ptr->array_job_id == array_job_id) {
			if (!IS_JOB_FINISHED(job_ptr))
				return false;
		}
		job_ptr = job_ptr->job_array_next_j;
	}

	return true;
}

/* Return true if ANY tasks of specific array job ID are pending */
extern bool test_job_array_pending(uint32_t array_job_id)
{
	job_record_t *job_ptr;
	int inx;

	job_ptr = find_job_record(array_job_id);
	if (job_ptr) {
		if (IS_JOB_PENDING(job_ptr))
			return true;
		if (job_ptr->array_recs && job_ptr->array_recs->task_cnt)
			return true;
	}

	/* Need to test individual job array records */
	inx = JOB_HASH_INX(array_job_id);
	job_ptr = job_array_hash_j[inx];
	while (job_ptr) {
		if (job_ptr->array_job_id == array_job_id) {
			if (IS_JOB_PENDING(job_ptr))
				return true;
		}
		job_ptr = job_ptr->job_array_next_j;
	}
	return false;
}

/* For a given job ID return the number of PENDING tasks which have their
 * own separate job_record (do not count tasks in pending META job record) */
extern int num_pending_job_array_tasks(uint32_t array_job_id)
{
	job_record_t *job_ptr;
	int count = 0, inx;

	inx = JOB_HASH_INX(array_job_id);
	job_ptr = job_array_hash_j[inx];
	while (job_ptr) {
		if ((job_ptr->array_job_id == array_job_id) &&
		    IS_JOB_PENDING(job_ptr))
			count++;
		job_ptr = job_ptr->job_array_next_j;
	}

	return count;
}

static void _foreach_by_job_callback(job_record_t *job_ptr,
				     for_each_by_job_id_args_t *args)
{
	xassert(args->magic == MAGIC_FOREACH_BY_JOBID_ARGS);

	if (!job_ptr || !job_ptr->job_id)
		return;

	xassert(!!args->ro_callback != !!args->callback); /* xor */
	xassert(args->control == FOR_EACH_JOB_BY_ID_EACH_CONT);

	if (args->ro_callback)
		args->control = args->ro_callback(job_ptr, args->filter,
						  args->callback_arg);
	else
		args->control = args->callback(job_ptr, args->filter,
					       args->callback_arg);

	xassert(args->control > FOR_EACH_JOB_BY_ID_EACH_INVALID);
	xassert(args->control < FOR_EACH_JOB_BY_ID_EACH_INVALID_MAX);
}

static int _foreach_job_by_id_single(void *x, void *arg)
{
	job_record_t *job_ptr = x;
	for_each_by_job_id_args_t *args = arg;

	xassert(args->magic == MAGIC_FOREACH_BY_JOBID_ARGS);

	_foreach_by_job_callback(job_ptr, args);

	switch (args->control)
	{
	case FOR_EACH_JOB_BY_ID_EACH_CONT:
		return SLURM_SUCCESS;
	case FOR_EACH_JOB_BY_ID_EACH_STOP:
	case FOR_EACH_JOB_BY_ID_EACH_FAIL:
		/* must return error as only way to stop list foreach */
		return SLURM_ERROR;
	case FOR_EACH_JOB_BY_ID_EACH_INVALID_MAX:
	case FOR_EACH_JOB_BY_ID_EACH_INVALID:
		fatal_abort("should never happen");
	}

	return SLURM_SUCCESS;
}

static int _foreach_by_het_job(void *x, void *arg)
{
	job_record_t *job_ptr = x;
	for_each_by_job_id_args_t *args = arg;

	xassert(args->magic == MAGIC_FOREACH_BY_JOBID_ARGS);

	/* Filter to only this HetJob */

	if (job_ptr->het_job_id != args->job_ptr->het_job_id)
		return SLURM_SUCCESS;

	if ((args->filter->het_job_offset != NO_VAL) &&
	    (job_ptr->het_job_offset != args->filter->het_job_offset))
		return SLURM_SUCCESS;

	return _foreach_job_by_id_single(job_ptr, args);
}

static job_record_t *_find_first_job_array_rec(uint32_t array_job_id)
{
	job_record_t *job_ptr;
	int inx;

	inx = JOB_HASH_INX(array_job_id);
	job_ptr = job_array_hash_j[inx];
	while (job_ptr) {
		if (job_ptr->array_job_id == array_job_id)
			return job_ptr;
		job_ptr = job_ptr->job_array_next_j;
	}

	return NULL;
}

static void _foreach_job_by_id_array(for_each_by_job_id_args_t *args)
{
	job_record_t *meta, *start;
	bool dumped_meta = false, dumped_linked = false;
	const uint32_t array_job_id = args->job_ptr->array_job_id;

	xassert(args->magic == MAGIC_FOREACH_BY_JOBID_ARGS);

	start = _find_first_job_array_rec(array_job_id);

	for (job_record_t *j = start; j; j = j->job_array_next_j) {
		if (j->array_job_id != array_job_id)
			continue;

		if (j->array_recs)
			dumped_meta = true;

		if ((args->filter->array_task_id != NO_VAL) &&
		    (j->array_task_id != args->filter->array_task_id))
			continue;

		debug3("%pJ->array_recs=%"PRIxPTR" linked to %pJ->array_recs=%"PRIxPTR,
		       start, (uintptr_t) (start ? start->array_recs : NULL), j,
		       (uintptr_t) j->array_recs);

		_foreach_by_job_callback(j, args);

		if (args->control != FOR_EACH_JOB_BY_ID_EACH_CONT)
			return;

		dumped_linked = true;
	}

	if (dumped_meta)
		return;

	meta = find_job_record(args->job_ptr->array_job_id);

	if (!meta)
		return;

	if (!meta->array_recs) {
		debug3("%pJ->array_recs = NULL", meta);
		return;
	} else if (!meta->array_recs->task_id_bitmap) {
		debug3("%pJ->array_recs->task_id_bitmap = NULL", meta);
		return;
	}

	xassert(meta->array_task_id == NO_VAL);
	xassert(meta->array_job_id == meta->job_id);

	_foreach_by_job_callback(meta, args);

	if (args->control != FOR_EACH_JOB_BY_ID_EACH_CONT)
		return;

	if (dumped_linked)
		return;

	for (int i = 0; i < bit_size(meta->array_recs->task_id_bitmap); i++) {
		if (!bit_test(meta->array_recs->task_id_bitmap, i)) {
			job_record_t *job_ptr =
				find_job_array_rec(meta->array_job_id, i);

			if (!job_ptr)
				continue;

			if ((args->filter->array_task_id != NO_VAL) &&
			    (job_ptr->array_task_id !=
			     args->filter->array_task_id))
				continue;

			debug3("%pJ resolving bit:%d=%c to %pJ",
			       meta, i,
			       (bit_test(meta->array_recs->task_id_bitmap, i) ?
				'1' : '0'), job_ptr);

			_foreach_by_job_callback(job_ptr, args);

			if (args->control != FOR_EACH_JOB_BY_ID_EACH_CONT)
				return;
		}
	}
}

static void _find_array_expression_jobs(const slurm_selected_step_t *filter,
					for_each_by_job_id_args_t *args,
					list_t *match_job_list,
					slurm_selected_step_t *not_found_tasks)
{
	int32_t i_first, i_last;
	uint32_t job_id = filter->step_id.job_id;
	bitstr_t *array_bitmap = filter->array_bitmap;
	job_record_t *job_ptr;
	job_record_t *meta_job = NULL;

	i_first = bit_ffs(array_bitmap);
	if (i_first >= 0)
		i_last = bit_fls(array_bitmap);
	else
		i_last = -2;
	for (int i = i_first; i <= i_last; i++) {
		if (!bit_test(array_bitmap, i))
			continue;
		job_ptr = find_job_array_rec(job_id, i);
		/* If !job_ptr, the array task does not exist. */
		if (!job_ptr && !not_found_tasks)
			continue;
		if (!job_ptr && not_found_tasks) {
			bit_set(not_found_tasks->array_bitmap, i);
			continue;
		}
		if (IS_JOB_PENDING(job_ptr) && job_ptr->array_recs) {
			/* Found the meta job, or a task in the meta job */
			meta_job = job_ptr;
			continue;
		}
		/*
		 * Found an array task that has been split from the meta record,
		 * or the meta record is not pending and all tasks have already
		 * been split out.
		 */
		list_append(match_job_list, job_ptr);
	}
	if (meta_job)
		list_append(match_job_list, meta_job);
}

static void _foreach_array_bitmap(const slurm_selected_step_t *filter,
				  for_each_by_job_id_args_t *args)
{
	list_t *match_job_list = list_create(NULL); /* list of job_record_t */
	slurm_selected_step_t *not_found_tasks = NULL;
	foreach_job_by_id_control_t tmp_control =
		FOR_EACH_JOB_BY_ID_EACH_INVALID;

	/*
	 * Call the callback once per record that has been split off.
	 * Then call it once for the meta record.
	 */
	if (args->null_callback) {
		not_found_tasks = xmalloc(sizeof(*not_found_tasks));
		memcpy(not_found_tasks, filter, sizeof(*not_found_tasks));
		not_found_tasks->array_bitmap =
			bit_alloc(bit_size(filter->array_bitmap));
	}
	_find_array_expression_jobs(filter, args, match_job_list,
				    not_found_tasks);

	/*
	 * Because this is a single filter, call both callbacks (no-match and
	 * match). Then, set args->control to the max of each callback return
	 * value.
	 */
	if (not_found_tasks) {
		if (bit_ffs(not_found_tasks->array_bitmap) != -1)
			tmp_control = args->null_callback(not_found_tasks,
							  args->callback_arg);
		FREE_NULL_BITMAP(not_found_tasks->array_bitmap);
		xfree(not_found_tasks);
	}

	if (list_count(match_job_list))
		(void) list_for_each(match_job_list, _foreach_job_by_id_single,
				     args);

	FREE_NULL_LIST(match_job_list);
	if (tmp_control != FOR_EACH_JOB_BY_ID_EACH_INVALID)
		args->control = MAX(args->control, tmp_control);
}

static int _walk_jobs_by_selected_step(const slurm_selected_step_t *filter,
				       for_each_by_job_id_args_t *args)
{
	xassert(args->magic == MAGIC_FOREACH_BY_JOBID_ARGS);

	if (!filter->step_id.job_id) {
		/* 0 is never a valid job so just return now */
		goto done;
	} else if (filter->step_id.job_id == NO_VAL) {
		/* walk all jobs */
		(void) list_for_each_ro(job_list, _foreach_job_by_id_single,
					args);
		goto done;
	}

	xassert(!((filter->array_task_id != NO_VAL) &&
		  (filter->het_job_offset != NO_VAL)));

	if (filter->array_bitmap) {
		_foreach_array_bitmap(filter, args);
		goto done;
	}

	if (filter->array_task_id != NO_VAL)
		args->job_ptr = find_job_array_rec(filter->step_id.job_id,
						   filter->array_task_id);
	else if (filter->het_job_offset != NO_VAL)
		args->job_ptr = find_job_record(filter->step_id.job_id +
						filter->het_job_offset);
	else /* not array task or het component */
		args->job_ptr = find_job_record(filter->step_id.job_id);

	if (!args->job_ptr) {
		if (!args->null_callback) {
			args->control = FOR_EACH_JOB_BY_ID_EACH_CONT;
		} else {
			args->control = args->null_callback(filter,
							    args->callback_arg);
		}
		goto done;
	}

	if (args->job_ptr->het_job_list) {
		xassert(args->job_ptr->het_job_id > 0);
		(void) list_for_each(args->job_ptr->het_job_list,
				     _foreach_by_het_job, args);
	} else if (args->job_ptr->array_job_id != args->job_ptr->job_id) {
		/* Pack regular (not array/het) job */
		_foreach_by_job_callback(args->job_ptr, args);
	} else {
		/* array job */
		_foreach_job_by_id_array(args);
	}

done:
	switch (args->control)
	{
	case FOR_EACH_JOB_BY_ID_EACH_STOP:
	case FOR_EACH_JOB_BY_ID_EACH_CONT:
		return args->count;
	case FOR_EACH_JOB_BY_ID_EACH_FAIL:
		return args->count * -1;
	case FOR_EACH_JOB_BY_ID_EACH_INVALID_MAX:
	case FOR_EACH_JOB_BY_ID_EACH_INVALID:
		fatal_abort("should never happen");
	}

	fatal_abort("should never happen");
}

extern int foreach_job_by_id(const slurm_selected_step_t *filter,
			     JobForEachFunc callback,
			     JobNullForEachFunc null_callback, void *arg)
{
	for_each_by_job_id_args_t args = {
		.magic = MAGIC_FOREACH_BY_JOBID_ARGS,
		.control = FOR_EACH_JOB_BY_ID_EACH_CONT,
		.count = 0,
		.callback = callback,
		.callback_arg = arg,
		.null_callback = null_callback,
		.filter = filter,
	};

	xassert(verify_lock(JOB_LOCK, WRITE_LOCK));

	return _walk_jobs_by_selected_step(filter, &args);
}

extern int foreach_job_by_id_ro(const slurm_selected_step_t *filter,
				JobROForEachFunc callback,
				JobNullForEachFunc null_callback, void *arg)
{
	for_each_by_job_id_args_t args = {
		.magic = MAGIC_FOREACH_BY_JOBID_ARGS,
		.control = FOR_EACH_JOB_BY_ID_EACH_CONT,
		.count = 0,
		.ro_callback = callback,
		.callback_arg = arg,
		.null_callback = null_callback,
		.filter = filter,
	};

	xassert(verify_lock(JOB_LOCK, READ_LOCK));

	return _walk_jobs_by_selected_step(filter, &args);
}

/*
 * find_job_array_rec - return a pointer to the job record with the given
 *	array_job_id/array_task_id
 * IN job_id - requested job's id
 * IN array_task_id - requested job's task id,
 *		      NO_VAL if none specified (i.e. not a job array)
 *		      INFINITE return any task for specified job id
 * RET pointer to the job's record, NULL on error
 */
extern job_record_t *find_job_array_rec(uint32_t array_job_id,
					uint32_t array_task_id)
{
	job_record_t *job_ptr, *match_job_ptr = NULL;
	int inx;

	if (array_task_id == NO_VAL)
		return find_job_record(array_job_id);

	if (array_task_id == INFINITE) {	/* find by job ID */
		/* Look for job record with all of the pending tasks */
		job_ptr = find_job_record(array_job_id);
		if (job_ptr && job_ptr->array_recs &&
		    (job_ptr->array_job_id == array_job_id))
			return job_ptr;

		inx = JOB_HASH_INX(array_job_id);
		job_ptr = job_array_hash_j[inx];
		while (job_ptr) {
			if (job_ptr->array_job_id == array_job_id) {
				match_job_ptr = job_ptr;
				if (!IS_JOB_FINISHED(job_ptr)) {
					return job_ptr;
				}
			}
			job_ptr = job_ptr->job_array_next_j;
		}
		return match_job_ptr;
	} else {		/* Find specific task ID */
		inx = JOB_ARRAY_HASH_INX(array_job_id, array_task_id);
		job_ptr = job_array_hash_t[inx];
		while (job_ptr) {
			if ((job_ptr->array_job_id == array_job_id) &&
			    (job_ptr->array_task_id == array_task_id)) {
				return job_ptr;
			}
			job_ptr = job_ptr->job_array_next_t;
		}
		/* Look for job record with all of the pending tasks */
		job_ptr = find_job_record(array_job_id);
		if (job_ptr && job_ptr->array_recs &&
		    job_ptr->array_recs->task_id_bitmap) {
			inx = bit_size(job_ptr->array_recs->task_id_bitmap);
			if ((array_task_id < inx) &&
			    bit_test(job_ptr->array_recs->task_id_bitmap,
				     array_task_id)) {
				return job_ptr;
			}
		}
		return NULL;	/* None found */
	}
}

/*
 * find_het_job_record - return a pointer to the job record with the given ID
 * IN job_id - requested job's ID
 * IN het_job_offset - hetjob component offset
 * RET pointer to the job's record, NULL on error
 */
extern job_record_t *find_het_job_record(uint32_t job_id,
					 uint32_t het_job_offset)
{
	job_record_t *het_job_leader, *het_job;
	list_itr_t *iter;

	het_job_leader = job_hash[JOB_HASH_INX(job_id)];
	while (het_job_leader) {
		if (het_job_leader->job_id == job_id)
			break;
		het_job_leader = het_job_leader->job_next;
	}
	if (!het_job_leader)
		return NULL;
	if (het_job_leader->het_job_offset == het_job_offset)
		return het_job_leader;

	if (!het_job_leader->het_job_list)
		return NULL;
	iter = list_iterator_create(het_job_leader->het_job_list);
	while ((het_job = list_next(iter))) {
		if (het_job_leader->het_job_id != het_job->het_job_id) {
			error("%s: Bad het_job_list for %pJ",
			      __func__, het_job_leader);
			continue;
		}
		if (het_job->het_job_offset == het_job_offset)
			break;
	}
	list_iterator_destroy(iter);

	return het_job;
}

/*
 * find_job_record - return a pointer to the job record with the given job_id
 * IN job_id - requested job's id
 * RET pointer to the job's record, NULL on error
 */
extern job_record_t *find_job_record(uint32_t job_id)
{
	job_record_t *job_ptr;
	xassert(verify_lock(JOB_LOCK, READ_LOCK));

	job_ptr = job_hash[JOB_HASH_INX(job_id)];
	while (job_ptr) {
		if (job_ptr->job_id == job_id)
			return job_ptr;
		job_ptr = job_ptr->job_next;
	}

	return NULL;
}

/*
 * Set a requeued job to PENDING and COMPLETING if all the nodes are completed
 * and the EpilogSlurmctld is not running
 */
static void _set_requeued_job_pending_completing(job_record_t *job_ptr)
{
	/* do this after the epilog complete, setting it here is too early */
	//job_record_set_sluid(job_ptr);
	//job_ptr->details->submit_time = now;

	if (job_ptr->node_cnt || job_ptr->epilog_running)
		job_state_set(job_ptr, (JOB_PENDING | JOB_COMPLETING));
	else
		job_state_set(job_ptr, JOB_PENDING);
}

/*
 * Kill job or job step
 *
 * IN job_step_kill_msg - msg with specs on which job/step to cancel.
 * IN uid               - uid of user requesting job/step cancel.
 */
static int _kill_job_step(job_step_kill_msg_t *job_step_kill_msg, uint32_t uid)
{
	DEF_TIMERS;
	/* Locks: Read config, write job, write node, read fed */
	slurmctld_lock_t job_write_lock = {
		READ_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK, READ_LOCK };
	job_record_t *job_ptr;
	int error_code = SLURM_SUCCESS;

	START_TIMER;
	lock_slurmctld(job_write_lock);
	job_ptr = find_job_record(job_step_kill_msg->step_id.job_id);
	log_flag(TRACE_JOBS, "%s: enter %pJ", __func__, job_ptr);

	/* do RPC call */
	if (job_step_kill_msg->step_id.step_id == NO_VAL) {
		/* NO_VAL means the whole job, not individual steps */
		error_code = job_signal_id(job_step_kill_msg->step_id.job_id,
					   job_step_kill_msg->signal,
					   job_step_kill_msg->flags, uid,
					   false);
		unlock_slurmctld(job_write_lock);
		END_TIMER2(__func__);

		/* return result */
		if (error_code) {
			log_flag(STEPS, "Signal %u %pJ by UID=%u: %s",
				 job_step_kill_msg->signal, job_ptr, uid,
				 slurm_strerror(error_code));
		} else {
			if (job_step_kill_msg->signal == SIGKILL) {
				log_flag(STEPS, "%s: Cancel of %pJ by UID=%u, %s",
					 __func__, job_ptr, uid, TIME_STR);
				slurmctld_diag_stats.jobs_canceled++;
			} else
				log_flag(STEPS, "%s: Signal %u of %pJ by UID=%u, %s",
					 __func__, job_step_kill_msg->signal,
					 job_ptr, uid, TIME_STR);

			/* Below function provides its own locking */
			schedule_job_save();
		}
	} else {
		error_code = job_step_signal(&job_step_kill_msg->step_id,
					     job_step_kill_msg->signal,
					     job_step_kill_msg->flags,
					     uid);
		unlock_slurmctld(job_write_lock);
		END_TIMER2(__func__);

		/* return result */
		if (error_code) {
			log_flag(STEPS, "Signal %u of JobId=%u StepId=%u by UID=%u: %s",
				 job_step_kill_msg->signal,
				 job_step_kill_msg->step_id.job_id,
				 job_step_kill_msg->step_id.step_id, uid,
				 slurm_strerror(error_code));
		} else {
			if (job_step_kill_msg->signal == SIGKILL)
				log_flag(STEPS, "%s: Cancel of JobId=%u StepId=%u by UID=%u %s",
					 __func__,
					 job_step_kill_msg->step_id.job_id,
					 job_step_kill_msg->step_id.step_id,
					 uid,
					 TIME_STR);
			else
				log_flag(STEPS, "%s: Signal %u of JobId=%u StepId=%u by UID=%u %s",
					 __func__, job_step_kill_msg->signal,
					 job_step_kill_msg->step_id.job_id,
					 job_step_kill_msg->step_id.step_id,
					 uid,
					 TIME_STR);

			/* Below function provides its own locking */
			schedule_job_save();
		}
	}

	log_flag(TRACE_JOBS, "%s: return %pJ", __func__, job_ptr);
	return error_code;
}

/*
 * Kill job or job step
 *
 * IN job_step_kill_msg - msg with specs on which job/step to cancel.
 * IN uid               - uid of user requesting job/step cancel.
 */
extern int kill_job_step(job_step_kill_msg_t *job_step_kill_msg, uint32_t uid)
{
	/* Locks: Read job */
	slurmctld_lock_t job_read_lock = {
		NO_LOCK, READ_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };
	job_record_t *job_ptr, *het_job_ptr;
	uint32_t *het_job_ids = NULL;
	int cnt = 0, i, rc;
	int error_code = SLURM_SUCCESS;
	list_itr_t *iter;

	lock_slurmctld(job_read_lock);
	job_ptr = find_job_record(job_step_kill_msg->step_id.job_id);
	if (job_ptr && job_ptr->het_job_list &&
	    (job_step_kill_msg->signal == SIGKILL) &&
	    (job_step_kill_msg->step_id.step_id != NO_VAL)) {
		cnt = list_count(job_ptr->het_job_list);
		het_job_ids = xcalloc(cnt, sizeof(uint32_t));
		i = 0;
		iter = list_iterator_create(job_ptr->het_job_list);
		while ((het_job_ptr = list_next(iter))) {
			het_job_ids[i++] = het_job_ptr->job_id;
		}
		list_iterator_destroy(iter);
	}
	unlock_slurmctld(job_read_lock);

	if (!job_ptr) {
		info("%s: invalid JobId=%u",
		     __func__, job_step_kill_msg->step_id.job_id);
		error_code = ESLURM_INVALID_JOB_ID;
	} else if (het_job_ids) {
		for (i = 0; i < cnt; i++) {
			job_step_kill_msg->step_id.job_id = het_job_ids[i];
			rc = _kill_job_step(job_step_kill_msg, uid);
			if (rc != SLURM_SUCCESS)
				error_code = rc;
		}
		xfree(het_job_ids);
	} else {
		error_code = _kill_job_step(job_step_kill_msg, uid);
	}

	return error_code;
}

/*
 * kill_job_by_part_name - Given a partition name, deallocate resource for
 *	its jobs and kill them. All jobs associated with this partition
 *	will have their partition pointer cleared.
 * IN part_name - name of a partition
 * RET number of jobs associated with this partition
 */
extern int kill_job_by_part_name(char *part_name)
{
	list_itr_t *job_iterator, *part_iterator;
	job_record_t *job_ptr;
	part_record_t *part_ptr, *part2_ptr;
	int kill_job_cnt = 0;
	time_t now = time(NULL);

	part_ptr = find_part_record (part_name);
	if (part_ptr == NULL)	/* No such partition */
		return 0;

	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = list_next(job_iterator))) {
		bool pending = false, suspended = false;

		pending = IS_JOB_PENDING(job_ptr);
		if (job_ptr->part_ptr_list) {
			/* Remove partition if candidate for a job */
			bool rebuild_name_list = false;
			part_iterator = list_iterator_create(job_ptr->
							     part_ptr_list);
			while ((part2_ptr = list_next(part_iterator))) {
				if (part2_ptr != part_ptr)
					continue;
				list_remove(part_iterator);
				rebuild_name_list = true;
			}
			list_iterator_destroy(part_iterator);
			if (rebuild_name_list) {
				if (list_count(job_ptr->part_ptr_list) > 0) {
					rebuild_job_part_list(job_ptr);
					job_ptr->part_ptr =
						list_peek(job_ptr->
							  part_ptr_list);
				} else {
					FREE_NULL_LIST(job_ptr->part_ptr_list);
				}
			}
		}

		if (job_ptr->part_ptr != part_ptr)
			continue;

		if (IS_JOB_SUSPENDED(job_ptr)) {
			uint32_t suspend_job_state = job_ptr->job_state;
			/* we can't have it as suspended when we call the
			 * accounting stuff.
			 */
			job_state_set(job_ptr, JOB_CANCELLED);
			jobacct_storage_g_job_suspend(acct_db_conn, job_ptr);
			job_state_set(job_ptr, suspend_job_state);
			suspended = true;
		}
		if (IS_JOB_RUNNING(job_ptr) || suspended) {
			kill_job_cnt++;
			info("Killing %pJ on defunct partition %s",
			     job_ptr, part_name);
			job_state_set(job_ptr, (JOB_NODE_FAIL | JOB_COMPLETING));
			build_cg_bitmap(job_ptr);
			job_ptr->state_reason = FAIL_DOWN_PARTITION;
			xfree(job_ptr->state_desc);
			if (suspended) {
				job_ptr->end_time = job_ptr->suspend_time;
				job_ptr->tot_sus_time +=
					difftime(now, job_ptr->suspend_time);
			} else
				job_ptr->end_time = now;
			job_ptr->exit_code = 1;
			job_completion_logger(job_ptr, false);
			if (!pending)
				deallocate_nodes(job_ptr, false, suspended,
						 false);
		} else if (pending) {
			kill_job_cnt++;
			info("Killing %pJ on defunct partition %s",
			     job_ptr, part_name);
			job_state_set(job_ptr, JOB_CANCELLED);
			job_ptr->start_time	= now;
			job_ptr->end_time	= now;
			job_ptr->exit_code	= 1;
			job_completion_logger(job_ptr, false);
			fed_mgr_job_complete(job_ptr, 0, now);
		}
		job_ptr->part_ptr = NULL;
		FREE_NULL_LIST(job_ptr->part_ptr_list);
	}
	list_iterator_destroy(job_iterator);

	if (kill_job_cnt)
		last_job_update = now;
	return kill_job_cnt;
}

/*
 * kill_job_by_front_end_name - Given a front end node name, deallocate
 *	resource for its jobs and kill them.
 * IN node_name - name of a front end node
 * RET number of jobs associated with this front end node
 * NOTE: Patterned after kill_running_job_by_node_name()
 */
extern int kill_job_by_front_end_name(char *node_name)
{
#ifdef HAVE_FRONT_END
	list_itr_t *job_iterator;
	job_record_t *job_ptr, *het_job_leader;
	node_record_t *node_ptr;
	time_t now = time(NULL);
	int i, kill_job_cnt = 0;

	if (node_name == NULL)
		fatal("kill_job_by_front_end_name: node_name is NULL");

	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = list_next(job_iterator))) {
		bool suspended = false;

		if (!IS_JOB_RUNNING(job_ptr) && !IS_JOB_SUSPENDED(job_ptr) &&
		    !IS_JOB_COMPLETING(job_ptr))
			continue;
		het_job_leader = NULL;
		if (job_ptr->het_job_id)
			het_job_leader = find_job_record(job_ptr->het_job_id);
		if (!het_job_leader)
			het_job_leader = job_ptr;
		if ((het_job_leader->batch_host == NULL) ||
		    xstrcmp(het_job_leader->batch_host, node_name))
			continue;	/* no match on node name */

		if (IS_JOB_SUSPENDED(job_ptr)) {
			uint32_t suspend_job_state = job_ptr->job_state;
			/*
			 * we can't have it as suspended when we call the
			 * accounting stuff.
			 */
			job_state_set(job_ptr, JOB_CANCELLED);
			jobacct_storage_g_job_suspend(acct_db_conn, job_ptr);
			job_state_set(job_ptr, suspend_job_state);
			suspended = true;
		}
		if (IS_JOB_COMPLETING(job_ptr)) {
			kill_job_cnt++;
			while ((i = bit_ffs(job_ptr->node_bitmap_cg)) >= 0) {
				bit_clear(job_ptr->node_bitmap_cg, i);
				if (job_ptr->node_cnt)
					(job_ptr->node_cnt)--;
				else {
					error("node_cnt underflow on %pJ",
					      job_ptr);
				}
				job_update_tres_cnt(job_ptr, i);
				if (job_ptr->node_cnt == 0) {
					cleanup_completing(job_ptr);
					if (!job_ptr->epilog_running)
						batch_requeue_fini(job_ptr);
				}
				node_ptr = node_record_table_ptr[i];
				if (node_ptr->comp_job_cnt)
					(node_ptr->comp_job_cnt)--;
				else {
					error("Node %s comp_job_cnt underflow, %pJ",
					      node_ptr->name, job_ptr);
				}
			}
		} else if (IS_JOB_RUNNING(job_ptr) || suspended) {
			kill_job_cnt++;
			if (job_ptr->batch_flag && job_ptr->details &&
			    slurm_conf.job_requeue &&
			    (job_ptr->details->requeue > 0)) {
				srun_node_fail(job_ptr, node_name);
				info("requeue %pJ due to failure of node %s",
				     job_ptr, node_name);
				set_job_prio(job_ptr);
				job_ptr->time_last_active  = now;
				if (suspended) {
					job_ptr->end_time =
						job_ptr->suspend_time;
					job_ptr->tot_sus_time +=
						difftime(now,
							 job_ptr->
							 suspend_time);
				} else
					job_ptr->end_time = now;

				/*
				 * We want this job to look like it
				 * was terminated in the accounting logs.
				 * Set a new submit time so the restarted
				 * job looks like a new job.
				 */
				job_state_set(job_ptr, JOB_NODE_FAIL);
				build_cg_bitmap(job_ptr);
				job_ptr->exit_code = 1;
				job_completion_logger(job_ptr, true);
				deallocate_nodes(job_ptr, false, suspended,
						 false);

				_set_requeued_job_pending_completing(job_ptr);

				job_ptr->restart_cnt++;

				/* clear signal sent flag on requeue */
				job_ptr->warn_flags &= ~WARN_SENT;

				job_ptr->exit_code = 0;

				/* Since the job completion logger
				 * removes the submit we need to add it
				 * again. */
				acct_policy_add_job_submit(job_ptr, false);

				if (!job_ptr->node_bitmap_cg ||
				    bit_ffs(job_ptr->node_bitmap_cg) == -1)
					batch_requeue_fini(job_ptr);
			} else {
				info("Killing %pJ on failed node %s",
				     job_ptr, node_name);
				srun_node_fail(job_ptr, node_name);
				job_state_set(job_ptr, (JOB_NODE_FAIL |
							JOB_COMPLETING));
				build_cg_bitmap(job_ptr);
				job_ptr->state_reason = FAIL_DOWN_NODE;
				xfree(job_ptr->state_desc);
				if (suspended) {
					job_ptr->end_time =
						job_ptr->suspend_time;
					job_ptr->tot_sus_time +=
						difftime(now,
							 job_ptr->suspend_time);
				} else
					job_ptr->end_time = now;
				job_ptr->exit_code = 1;
				job_completion_logger(job_ptr, false);
				deallocate_nodes(job_ptr, false, suspended,
						 false);
			}
		}
	}
	list_iterator_destroy(job_iterator);

	if (kill_job_cnt)
		last_job_update = now;
	return kill_job_cnt;
#else
	return 0;
#endif
}

/*
 * partition_in_use - determine whether a partition is in use by a RUNNING
 *	PENDING or SUSPENDED job or reservations
 * IN part_name - name of a partition
 * RET true if the partition is in use, else false
 */
extern bool partition_in_use(char *part_name)
{
	list_itr_t *job_iterator;
	job_record_t *job_ptr;
	part_record_t *part_ptr;

	part_ptr = find_part_record (part_name);
	if (part_ptr == NULL)	/* No such partition */
		return false;

	/* check jobs */
	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = list_next(job_iterator))) {
		if (job_ptr->part_ptr == part_ptr) {
			if (!IS_JOB_FINISHED(job_ptr)) {
				list_iterator_destroy(job_iterator);
				return true;
			}
		}
	}
	list_iterator_destroy(job_iterator);

	/* check reservations */
	if (list_find_first(resv_list, _find_resv_part, part_ptr))
		return true;

	return false;
}

static bool _job_node_test(job_record_t *job_ptr, int node_inx)
{
	if (job_ptr->node_bitmap &&
	    bit_test(job_ptr->node_bitmap, node_inx))
		return true;
	return false;
}

static bool _het_job_on_node(job_record_t *job_ptr, int node_inx)
{
	job_record_t *het_job_leader, *het_job;
	list_itr_t *iter;
	static bool result = false;

	if (!job_ptr->het_job_id)
		return _job_node_test(job_ptr, node_inx);

	het_job_leader = find_job_record(job_ptr->het_job_id);
	if (!het_job_leader) {
		error("%s: Hetjob leader %pJ not found",
		      __func__, job_ptr);
		return _job_node_test(job_ptr, node_inx);
	}
	if (!het_job_leader->het_job_list) {
		error("%s: Hetjob leader %pJ job list is NULL",
		      __func__, job_ptr);
		return _job_node_test(job_ptr, node_inx);
	}

	iter = list_iterator_create(het_job_leader->het_job_list);
	while ((het_job = list_next(iter))) {
		if ((result = _job_node_test(het_job, node_inx)))
			break;
		/*
		 * After a DOWN node is removed from another job component,
		 * we have no way to identify other hetjob components with
		 * the same node, so assume if one component is in NODE_FAILED
		 * state, they all should be.
		 */
		if (IS_JOB_NODE_FAILED(het_job)) {
			result = true;
			break;
		}
	}
	list_iterator_destroy(iter);

	return result;
}

/*
 * kill_running_job_by_node_name - Given a node name, deallocate RUNNING
 *	or COMPLETING jobs from the node or kill them
 * IN node_name - name of a node
 * RET number of killed jobs
 */
extern int kill_running_job_by_node_name(char *node_name)
{
	list_itr_t *job_iterator;
	job_record_t *job_ptr;
	node_record_t *node_ptr;
	bitstr_t *orig_job_node_bitmap;
	int kill_job_cnt = 0;
	time_t now = time(NULL);

	xassert(verify_lock(JOB_LOCK, WRITE_LOCK));
	xassert(verify_lock(NODE_LOCK, WRITE_LOCK));

	node_ptr = find_node_record(node_name);
	if (node_ptr == NULL)	/* No such node */
		return 0;

	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = list_next(job_iterator))) {
		bool suspended = false;
		job_resources_t *job_resrcs_ptr = job_ptr->job_resrcs;
		if (!_het_job_on_node(job_ptr, node_ptr->index))
			continue;	/* job not on this node */
		if (IS_JOB_SUSPENDED(job_ptr)) {
			uint32_t suspend_job_state = job_ptr->job_state;
			/*
			 * we can't have it as suspended when we call the
			 * accounting stuff.
			 */
			job_state_set(job_ptr, JOB_CANCELLED);
			jobacct_storage_g_job_suspend(acct_db_conn, job_ptr);
			job_state_set(job_ptr, suspend_job_state);
			suspended = true;
		}

		if (IS_JOB_COMPLETING(job_ptr)) {
			if (!bit_test(job_ptr->node_bitmap_cg, node_ptr->index))
				continue;
			kill_job_cnt++;
			bit_clear(job_ptr->node_bitmap_cg, node_ptr->index);
			job_update_tres_cnt(job_ptr, node_ptr->index);
			if (job_ptr->node_cnt)
				(job_ptr->node_cnt)--;
			else {
				error("node_cnt underflow on %pJ", job_ptr);
			}
			if (job_ptr->node_cnt == 0) {
				cleanup_completing(job_ptr);
				if (!job_ptr->epilog_running)
					batch_requeue_fini(job_ptr);
			}
			if (node_ptr->comp_job_cnt)
				(node_ptr->comp_job_cnt)--;
			else {
				error("Node %s comp_job_cnt underflow, %pJ",
				      node_ptr->name, job_ptr);
			}
		} else if (IS_JOB_RUNNING(job_ptr) || suspended) {
			kill_job_cnt++;
			if ((job_ptr->details) &&
			    (job_ptr->kill_on_node_fail == 0) &&
			    (job_ptr->node_cnt > 1) &&
			    !IS_JOB_CONFIGURING(job_ptr)) {
				/* keep job running on remaining nodes */
				srun_node_fail(job_ptr, node_name);
				error("Removing failed node %s from %pJ",
				      node_name, job_ptr);
				job_pre_resize_acctg(job_ptr);
				kill_step_on_node(job_ptr, node_ptr, true);
				orig_job_node_bitmap =
					bit_copy(job_resrcs_ptr->node_bitmap);
				excise_node_from_job(job_ptr, node_ptr);
				/* Resize the bitmaps of the job's steps */
				rebuild_step_bitmaps(job_ptr,
						     orig_job_node_bitmap);
				FREE_NULL_BITMAP(orig_job_node_bitmap);
				(void) gs_job_start(job_ptr);
				gres_stepmgr_job_build_details(
					job_ptr->gres_list_alloc,
					job_ptr->nodes,
					&job_ptr->gres_detail_cnt,
					&job_ptr->gres_detail_str,
					&job_ptr->gres_used);
				job_post_resize_acctg(job_ptr);
			} else if (job_ptr->batch_flag && job_ptr->details &&
				   job_ptr->details->requeue) {
				srun_node_fail(job_ptr, node_name);
				info("requeue job %pJ due to failure of node %s",
				     job_ptr, node_name);
				job_ptr->time_last_active  = now;
				if (suspended) {
					job_ptr->end_time =
						job_ptr->suspend_time;
					job_ptr->tot_sus_time +=
						difftime(now,
							 job_ptr->
							 suspend_time);
				} else
					job_ptr->end_time = now;

				/*
				 * We want this job to look like it
				 * was terminated in the accounting logs.
				 * Set a new submit time so the restarted
				 * job looks like a new job.
				 */
				job_state_set(job_ptr, JOB_NODE_FAIL);
				job_ptr->failed_node = xstrdup(node_name);
				build_cg_bitmap(job_ptr);
				job_ptr->exit_code = 1;
				job_completion_logger(job_ptr, true);
				deallocate_nodes(job_ptr, false, suspended,
						 false);

				_set_requeued_job_pending_completing(job_ptr);

				job_ptr->restart_cnt++;

				/* clear signal sent flag on requeue */
				job_ptr->warn_flags &= ~WARN_SENT;

				job_ptr->exit_code = 0;

				/*
				 * Since the job completion logger
				 * removes the submit we need to add it
				 * again.
				 */
				acct_policy_add_job_submit(job_ptr, false);

				if (!job_ptr->node_bitmap_cg ||
				    bit_ffs(job_ptr->node_bitmap_cg) == -1)
					batch_requeue_fini(job_ptr);
			} else {
				info("Killing %pJ on failed node %s",
				     job_ptr, node_name);
				srun_node_fail(job_ptr, node_name);
				job_state_set(job_ptr, (JOB_NODE_FAIL |
							JOB_COMPLETING));
				job_ptr->failed_node = xstrdup(node_name);
				build_cg_bitmap(job_ptr);
				job_ptr->state_reason = FAIL_DOWN_NODE;
				xfree(job_ptr->state_desc);
				if (suspended) {
					job_ptr->end_time =
						job_ptr->suspend_time;
					job_ptr->tot_sus_time +=
						difftime(now,
							 job_ptr->suspend_time);
				} else
					job_ptr->end_time = now;
				job_ptr->exit_code = 1;
				job_completion_logger(job_ptr, false);
				deallocate_nodes(job_ptr, false, suspended,
						 false);
			}
		}

	}
	list_iterator_destroy(job_iterator);
	if (kill_job_cnt)
		last_job_update = now;

	return kill_job_cnt;
}

/* Remove one node from a job's allocation */
extern void excise_node_from_job(job_record_t *job_ptr,
				 node_record_t *node_ptr)
{
	make_node_idle(node_ptr, job_ptr); /* updates bitmap */
	xfree(job_ptr->nodes);
	job_ptr->nodes = bitmap2node_name(job_ptr->node_bitmap);

	job_ptr->total_nodes = job_ptr->node_cnt = bit_set_count(job_ptr->node_bitmap);

	(void) select_g_job_resized(job_ptr, node_ptr);
}

/*
 * dump_job_desc - dump the incoming job submit request message
 * IN job_desc - job specification from RPC
 */
void dump_job_desc(job_desc_msg_t *job_desc)
{
	long pn_min_cpus, pn_min_tmp_disk, min_cpus;
	uint64_t pn_min_memory;
	long time_limit, priority, contiguous, nice, time_min;
	long kill_on_node_fail, shared, immediate, wait_all_nodes;
	long cpus_per_task, requeue, num_tasks, overcommit;
	long ntasks_per_node, ntasks_per_socket, ntasks_per_core;
	long ntasks_per_tres;
	int spec_count;
	char *mem_type, buf[256], *signal_flags, *spec_type, *job_id;

	if (get_log_level() < LOG_LEVEL_DEBUG3)
		return;

	if (job_desc == NULL)
		return;

	if (job_desc->job_id_str)
		job_id = job_desc->job_id_str;
	else if (job_desc->job_id == NO_VAL)
		job_id = "N/A";
	else {
		snprintf(buf, sizeof(buf), "%u", job_desc->job_id);
		job_id = buf;
	}
	debug3("JobDesc: user_id=%u JobId=%s partition=%s name=%s",
	       job_desc->user_id, job_id,
	       job_desc->partition, job_desc->name);

	min_cpus = (job_desc->min_cpus != NO_VAL) ?
		(long) job_desc->min_cpus : -1L;
	pn_min_cpus    = (job_desc->pn_min_cpus != NO_VAL16) ?
		(long) job_desc->pn_min_cpus : -1L;
	if (job_desc->core_spec == NO_VAL16) {
		spec_type  = "core";
		spec_count = -1;
	} else if (job_desc->core_spec & CORE_SPEC_THREAD) {
		spec_type  = "thread";
		spec_count = job_desc->core_spec & (~CORE_SPEC_THREAD);
	} else {
		spec_type  = "core";
		spec_count = job_desc->core_spec;
	}
	debug3("   cpus=%ld-%u pn_min_cpus=%ld %s_spec=%d",
	       min_cpus, job_desc->max_cpus, pn_min_cpus,
	       spec_type, spec_count);

	debug3("   Nodes=%u-[%u] Sock/Node=%u Core/Sock=%u Thread/Core=%u",
	       job_desc->min_nodes, job_desc->max_nodes,
	       job_desc->sockets_per_node, job_desc->cores_per_socket,
	       job_desc->threads_per_core);

	if (job_desc->pn_min_memory == NO_VAL64) {
		pn_min_memory = -1L;
		mem_type = "job";
	} else if (job_desc->pn_min_memory & MEM_PER_CPU) {
		pn_min_memory = job_desc->pn_min_memory & (~MEM_PER_CPU);
		mem_type = "cpu";
	} else {
		pn_min_memory = job_desc->pn_min_memory;
		mem_type = "job";
	}
	pn_min_tmp_disk = (job_desc->pn_min_tmp_disk != NO_VAL) ?
		(long) job_desc->pn_min_tmp_disk : -1L;
	debug3("   pn_min_memory_%s=%"PRIu64" pn_min_tmp_disk=%ld",
	       mem_type, pn_min_memory, pn_min_tmp_disk);
	immediate = (job_desc->immediate == 0) ? 0L : 1L;
	debug3("   immediate=%ld reservation=%s",
	       immediate, job_desc->reservation);
	debug3("   features=%s batch_features=%s cluster_features=%s prefer=%s",
	       job_desc->features, job_desc->batch_features,
	       job_desc->cluster_features, job_desc->prefer);

	debug3("   req_nodes=%s exc_nodes=%s",
	       job_desc->req_nodes, job_desc->exc_nodes);

	time_limit = (job_desc->time_limit != NO_VAL) ?
		(long) job_desc->time_limit : -1L;
	time_min = (job_desc->time_min != NO_VAL) ?
		(long) job_desc->time_min : time_limit;
	priority   = (job_desc->priority != NO_VAL) ?
		(long) job_desc->priority : -1L;
	contiguous = (job_desc->contiguous != NO_VAL16) ?
		(long) job_desc->contiguous : -1L;
	shared = (job_desc->shared != NO_VAL16) ?
		(long) job_desc->shared : -1L;
	debug3("   time_limit=%ld-%ld priority=%ld contiguous=%ld shared=%ld",
	       time_min, time_limit, priority, contiguous, shared);

	kill_on_node_fail = (job_desc->kill_on_node_fail !=
			     NO_VAL16) ?
		(long) job_desc->kill_on_node_fail : -1L;
	if (job_desc->script)	/* log has problem with string len & null */
		debug3("   kill_on_node_fail=%ld script=%.40s...",
		       kill_on_node_fail, job_desc->script);
	else
		debug3("   kill_on_node_fail=%ld script=(null)",
		       kill_on_node_fail);

	if (job_desc->argc == 1)
		debug3("   argv=\"%s\"",
		       job_desc->argv[0]);
	else if (job_desc->argc == 2)
		debug3("   argv=%s,%s",
		       job_desc->argv[0],
		       job_desc->argv[1]);
	else if (job_desc->argc > 2)
		debug3("   argv=%s,%s,%s,...",
		       job_desc->argv[0],
		       job_desc->argv[1],
		       job_desc->argv[2]);

	if (job_desc->env_size == 1)
		debug3("   environment=\"%s\"",
		       job_desc->environment[0]);
	else if (job_desc->env_size == 2)
		debug3("   environment=%s,%s",
		       job_desc->environment[0],
		       job_desc->environment[1]);
	else if (job_desc->env_size > 2)
		debug3("   environment=%s,%s,%s,...",
		       job_desc->environment[0],
		       job_desc->environment[1],
		       job_desc->environment[2]);

	if (job_desc->spank_job_env_size == 1)
		debug3("   spank_job_env=\"%s\"",
		       job_desc->spank_job_env[0]);
	else if (job_desc->spank_job_env_size == 2)
		debug3("   spank_job_env=%s,%s",
		       job_desc->spank_job_env[0],
		       job_desc->spank_job_env[1]);
	else if (job_desc->spank_job_env_size > 2)
		debug3("   spank_job_env=%s,%s,%s,...",
		       job_desc->spank_job_env[0],
		       job_desc->spank_job_env[1],
		       job_desc->spank_job_env[2]);

	debug3("   stdin=%s stdout=%s stderr=%s",
	       job_desc->std_in, job_desc->std_out, job_desc->std_err);

	debug3("   work_dir=%s alloc_node:sid=%s:%u",
	       job_desc->work_dir,
	       job_desc->alloc_node, job_desc->alloc_sid);

	debug3("   resp_host=%s alloc_resp_port=%u other_port=%u",
	       job_desc->resp_host,
	       job_desc->alloc_resp_port, job_desc->other_port);
	debug3("   dependency=%s account=%s qos=%s comment=%s",
	       job_desc->dependency, job_desc->account,
	       job_desc->qos, job_desc->comment);

	num_tasks = (job_desc->num_tasks != NO_VAL) ?
		(long) job_desc->num_tasks : -1L;
	overcommit = (job_desc->overcommit != NO_VAL8) ?
		(long) job_desc->overcommit : -1L;
	nice = (job_desc->nice != NO_VAL) ?
		((int64_t)job_desc->nice - NICE_OFFSET) : 0;
	debug3("   mail_type=%u mail_user=%s nice=%ld num_tasks=%ld "
	       "open_mode=%u overcommit=%ld acctg_freq=%s",
	       job_desc->mail_type, job_desc->mail_user, nice, num_tasks,
	       job_desc->open_mode, overcommit, job_desc->acctg_freq);

	slurm_make_time_str(&job_desc->begin_time, buf, sizeof(buf));
	cpus_per_task = (job_desc->cpus_per_task != NO_VAL16) ?
		(long) job_desc->cpus_per_task : -1L;
	requeue = (job_desc->requeue != NO_VAL16) ?
		(long) job_desc->requeue : -1L;
	debug3("   network=%s begin=%s cpus_per_task=%ld requeue=%ld "
	       "licenses=%s",
	       job_desc->network, buf, cpus_per_task, requeue,
	       job_desc->licenses);

	slurm_make_time_str(&job_desc->end_time, buf, sizeof(buf));
	wait_all_nodes = (job_desc->wait_all_nodes != NO_VAL16) ?
		(long) job_desc->wait_all_nodes : -1L;
	if (job_desc->warn_flags & KILL_JOB_BATCH)
		signal_flags = "B:";
	else
		signal_flags = "";
	cpu_freq_debug(NULL, NULL, buf, sizeof(buf), job_desc->cpu_freq_gov,
		       job_desc->cpu_freq_min, job_desc->cpu_freq_max,
		       NO_VAL);
	debug3("   end_time=%s signal=%s%u@%u wait_all_nodes=%ld cpu_freq=%s",
	       buf, signal_flags, job_desc->warn_signal, job_desc->warn_time,
	       wait_all_nodes, buf);

	ntasks_per_node = (job_desc->ntasks_per_node != NO_VAL16) ?
		(long) job_desc->ntasks_per_node : -1L;
	ntasks_per_socket = (job_desc->ntasks_per_socket !=
			     NO_VAL16) ?
		(long) job_desc->ntasks_per_socket : -1L;
	ntasks_per_core = (job_desc->ntasks_per_core != NO_VAL16) ?
		(long) job_desc->ntasks_per_core : -1L;
	ntasks_per_tres = (job_desc->ntasks_per_tres != NO_VAL16) ?
		(long) job_desc->ntasks_per_tres : -1L;
	debug3("   ntasks_per_node=%ld ntasks_per_socket=%ld ntasks_per_core=%ld ntasks_per_tres=%ld",
	       ntasks_per_node, ntasks_per_socket, ntasks_per_core,
	       ntasks_per_tres);

	debug3("   mem_bind=%u:%s plane_size:%u",
	       job_desc->mem_bind_type, job_desc->mem_bind,
	       job_desc->plane_size);
	debug3("   array_inx=%s", job_desc->array_inx);
	debug3("   burst_buffer=%s", job_desc->burst_buffer);
	debug3("   mcs_label=%s", job_desc->mcs_label);
	slurm_make_time_str(&job_desc->deadline, buf, sizeof(buf));
	debug3("   deadline=%s", buf);
	debug3("   bitflags=0x%"PRIx64" delay_boot=%u",
	       job_desc->bitflags, job_desc->delay_boot);

	if (job_desc->cpus_per_tres)
		debug3("   CPUs_per_TRES=%s", job_desc->cpus_per_tres);
	if (job_desc->mem_per_tres)
		debug3("   Mem_per_TRES=%s", job_desc->mem_per_tres);
	if (job_desc->tres_bind)
		debug3("   TRES_bind=%s", job_desc->tres_bind);
	if (job_desc->tres_freq)
		debug3("   TRES_freq=%s", job_desc->tres_freq);
	if (job_desc->tres_per_job)
		debug3("   TRES_per_job=%s", job_desc->tres_per_job);
	if (job_desc->tres_per_node)
		debug3("   TRES_per_node=%s", job_desc->tres_per_node);
	if (job_desc->tres_per_socket)
		debug3("   TRES_per_socket=%s", job_desc->tres_per_socket);
	if (job_desc->tres_per_task)
		debug3("   TRES_per_task=%s", job_desc->tres_per_task);

	if (job_desc->container || job_desc->container_id)
		debug3("   container=%s container-id=%s",
		       job_desc->container, job_desc->container_id);
}

/*
 * init_job_conf - initialize the job configuration tables and values.
 *	this should be called after creating node information, but
 *	before creating any job entries. Pre-existing job entries are
 *	left unchanged.
 *	NOTE: The job hash table size does not change after initial creation.
 * global: last_job_update - time of last job table update
 *	job_list - pointer to global job list
 *	purge_jobs_list - pointer to purge_jobs_list
 */
void init_job_conf(void)
{
	if (job_list == NULL) {
		job_count = 0;
		job_list = list_create(_move_to_purge_jobs_list);
	}

	last_job_update = time(NULL);

	if (!purge_files_list) {
		purge_files_list = list_create(xfree_ptr);
	}

	if (!purge_jobs_list)
		purge_jobs_list = list_create(job_record_delete);
}

/*
 * rehash_jobs - Create or rebuild the job hash table.
 */
extern void rehash_jobs(void)
{
	xassert(verify_lock(CONF_LOCK, READ_LOCK));
	xassert(verify_lock(JOB_LOCK, WRITE_LOCK));

	if (job_hash == NULL) {
		hash_table_size = slurm_conf.max_job_cnt;
		job_hash = xcalloc(hash_table_size, sizeof(job_record_t *));
		job_array_hash_j = xcalloc(hash_table_size,
					   sizeof(job_record_t *));
		job_array_hash_t = xcalloc(hash_table_size,
					   sizeof(job_record_t *));
		if (xstrcasestr(slurm_conf.sched_params,
				"enable_job_state_cache"))
			setup_job_state_hash(hash_table_size);
	} else if (hash_table_size < (slurm_conf.max_job_cnt / 2)) {
		/* If the MaxJobCount grows by too much, the hash table will
		 * be ineffective without rebuilding. We don't presently bother
		 * to rebuild the hash table, but cut MaxJobCount back as
		 * needed. */
		error ("MaxJobCount reset too high, restart slurmctld");
		slurm_conf.max_job_cnt = hash_table_size;
	}
}

/* Create an exact copy of an existing job record for a job array.
 * IN job_ptr - META job record for a job array, which is to become an
 *		individial task of the job array.
 *		Set the job's array_task_id to the task to be split out.
 * RET - The new job record, which is the new META job record. */
extern job_record_t *job_array_split(job_record_t *job_ptr, bool list_add)
{
	job_record_t *job_ptr_pend = NULL;
	job_details_t *job_details, *details_new, *save_details;
	uint32_t save_job_id, save_db_flags = job_ptr->db_flags;
	uint64_t save_db_index = job_ptr->db_index;
	priority_factors_t *save_prio_factors;
	list_t *save_step_list = NULL;
	int i;

	job_ptr_pend = _create_job_record(0, list_add);

	_remove_job_hash(job_ptr, JOB_HASH_JOB);
	job_ptr_pend->job_id = job_ptr->job_id;
	if (_set_job_id(job_ptr) != SLURM_SUCCESS)
		fatal("%s: _set_job_id error", __func__);
	if (!job_ptr->array_recs) {
		fatal_abort("%s: %pJ record lacks array structure",
			    __func__, job_ptr);
	}

	/*
	 * Copy most of original job data.
	 * This could be done in parallel, but performance was worse.
	 */
	save_job_id   = job_ptr_pend->job_id;
	save_details  = job_ptr_pend->details;
	save_prio_factors = job_ptr_pend->prio_factors;
	save_step_list = job_ptr_pend->step_list;
	memcpy(job_ptr_pend, job_ptr, sizeof(job_record_t));

	job_ptr_pend->job_id   = save_job_id;
	job_ptr_pend->details  = save_details;
	job_ptr_pend->db_flags = save_db_flags;
	job_ptr_pend->step_list = save_step_list;
	job_ptr_pend->db_index = save_db_index;

	job_ptr_pend->prio_factors = save_prio_factors;
	slurm_copy_priority_factors(job_ptr_pend->prio_factors,
				    job_ptr->prio_factors);

	job_ptr_pend->account = xstrdup(job_ptr->account);
	job_ptr_pend->admin_comment = xstrdup(job_ptr->admin_comment);
	job_ptr_pend->alias_list = NULL;
	job_ptr_pend->alloc_node = xstrdup(job_ptr->alloc_node);
	job_ptr_pend->node_addrs = NULL;

	job_ptr_pend->array_recs = job_ptr->array_recs;
	job_ptr->array_recs = NULL;

	if (job_ptr_pend->array_recs &&
	    job_ptr_pend->array_recs->task_id_bitmap) {
		bit_clear(job_ptr_pend->array_recs->task_id_bitmap,
			  job_ptr_pend->array_task_id);
	}
	xfree(job_ptr_pend->array_recs->task_id_str);
	if (job_ptr_pend->array_recs->task_cnt) {
		job_ptr_pend->array_recs->task_cnt--;
		if (job_ptr_pend->array_recs->task_cnt <= 1) {
			/*
			 * This is the last task of the job array, so we need to
			 * set array_task_id to a specific task id. We also
			 * need to call job_array_post_sched() to do cleanup
			 * on the array, specifically how job_array_post_sched()
			 * handles adding the job to the array_hash, otherwise
			 * we'll get errors.
			 */
			i = bit_ffs(job_ptr_pend->array_recs->task_id_bitmap);
			if (i < 0) {
				error("%s: No tasks in task_id_bitmap for %pJ",
				      __func__, job_ptr_pend);
				job_ptr_pend->array_task_id = NO_VAL;
			} else {
				job_ptr_pend->array_task_id = i;
				job_array_post_sched(job_ptr_pend, true);
			}
		} else {
			/* Still have tasks left to split off in the array */
			job_ptr_pend->array_task_id = NO_VAL;
		}
	} else {
		error("%pJ array_recs->task_cnt underflow",
		      job_ptr);
		job_ptr_pend->array_task_id = NO_VAL;
	}

	job_ptr_pend->batch_features = xstrdup(job_ptr->batch_features);
	job_ptr_pend->batch_host = NULL;
	job_ptr_pend->burst_buffer = xstrdup(job_ptr->burst_buffer);
	job_ptr_pend->burst_buffer_state = xstrdup(job_ptr->burst_buffer_state);
	job_ptr_pend->clusters = xstrdup(job_ptr->clusters);
	job_ptr_pend->comment = xstrdup(job_ptr->comment);
	job_ptr_pend->container = xstrdup(job_ptr->container);
	job_ptr_pend->container_id = xstrdup(job_ptr->container_id);
	job_ptr_pend->extra = xstrdup(job_ptr->extra);
	if ((extra_constraints_parse(job_ptr_pend->extra,
				     &job_ptr_pend->extra_constraints)) !=
	    SLURM_SUCCESS)
		error("%s: %pJ Invalid extra_constraints %s",
		      __func__, job_ptr, job_ptr_pend->extra);


	job_ptr_pend->fed_details = _dup_job_fed_details(job_ptr->fed_details);

	job_ptr_pend->front_end_ptr = NULL;
	/* job_details_t *details;		*** NOTE: Copied below */

	job_ptr_pend->limit_set.tres = xcalloc(slurmctld_tres_cnt,
					       sizeof(uint16_t));
	memcpy(job_ptr_pend->limit_set.tres, job_ptr->limit_set.tres,
	       sizeof(uint16_t) * slurmctld_tres_cnt);

	_add_job_hash(job_ptr);		/* Sets job_next */
	_add_job_hash(job_ptr_pend);	/* Sets job_next */
	_add_job_array_hash(job_ptr);
	job_ptr_pend->job_resrcs = NULL;

	job_ptr_pend->id = copy_identity(job_ptr->id);
	job_ptr_pend->licenses = xstrdup(job_ptr->licenses);
	job_ptr_pend->license_list = license_copy(job_ptr->license_list);
	job_ptr_pend->licenses_to_preempt = NULL;
	job_ptr_pend->lic_req = xstrdup(job_ptr->lic_req);
	job_ptr_pend->mail_user = xstrdup(job_ptr->mail_user);
	job_ptr_pend->mcs_label = xstrdup(job_ptr->mcs_label);
	job_ptr_pend->name = xstrdup(job_ptr->name);
	job_ptr_pend->network = xstrdup(job_ptr->network);
	job_ptr_pend->node_bitmap = NULL;
	job_ptr_pend->node_bitmap_cg = NULL;
	job_ptr_pend->node_bitmap_pr = NULL;
	job_ptr_pend->node_bitmap_preempt = NULL;
	job_ptr_pend->nodes = NULL;
	job_ptr_pend->nodes_completing = NULL;
	job_ptr_pend->nodes_pr = NULL;
	job_ptr_pend->origin_cluster = xstrdup(job_ptr->origin_cluster);
	job_ptr_pend->partition = xstrdup(job_ptr->partition);
	job_ptr_pend->part_ptr_list = part_list_copy(job_ptr->part_ptr_list);
	/* On jobs that are held the priority_array isn't set up yet,
	 * so check to see if it exists before copying. */
	if (job_ptr->part_ptr_list &&
	    job_ptr->part_prio) {
		job_ptr_pend->part_prio =
			xmalloc(sizeof(*job_ptr_pend->part_prio));

		if (job_ptr->part_prio->priority_array) {
			i = list_count(job_ptr->part_ptr_list);
			job_ptr_pend->part_prio->priority_array =
				xcalloc(i, sizeof(uint32_t));
			memcpy(job_ptr_pend->part_prio->priority_array,
			       job_ptr->part_prio->priority_array,
			       i * sizeof(uint32_t));
		}

		job_ptr_pend->part_prio->priority_array_names =
			xstrdup(job_ptr->part_prio->priority_array_names);
	}
	if (job_ptr->qos_list)
		job_ptr_pend->qos_list = list_shallow_copy(job_ptr->qos_list);
	job_ptr_pend->resv_name = xstrdup(job_ptr->resv_name);
	if (job_ptr->resv_list)
		job_ptr_pend->resv_list = list_shallow_copy(job_ptr->resv_list);
	job_ptr_pend->resv_ports = NULL;
	job_ptr_pend->resv_port_array = NULL;
	job_ptr_pend->resp_host = xstrdup(job_ptr->resp_host);
	if (job_ptr->select_jobinfo) {
		job_ptr_pend->select_jobinfo =
			select_g_select_jobinfo_copy(job_ptr->select_jobinfo);
	}
	job_ptr_pend->selinux_context = xstrdup(job_ptr->selinux_context);
	job_ptr_pend->sched_nodes = NULL;
	if (job_ptr->spank_job_env_size) {
		job_ptr_pend->spank_job_env =
			xcalloc((job_ptr->spank_job_env_size + 1),
				sizeof(char *));
		for (i = 0; i < job_ptr->spank_job_env_size; i++) {
			job_ptr_pend->spank_job_env[i] =
				xstrdup(job_ptr->spank_job_env[i]);
		}
	}
	job_ptr_pend->state_desc = xstrdup(job_ptr->state_desc);

	job_ptr_pend->system_comment = xstrdup(job_ptr->system_comment);

	i = sizeof(uint64_t) * slurmctld_tres_cnt;
	job_ptr_pend->tres_req_cnt = xmalloc(i);
	memcpy(job_ptr_pend->tres_req_cnt, job_ptr->tres_req_cnt, i);
	job_ptr_pend->tres_req_str = xstrdup(job_ptr->tres_req_str);
	job_ptr_pend->tres_fmt_req_str = xstrdup(job_ptr->tres_fmt_req_str);
	job_ptr_pend->tres_alloc_str = NULL;
	job_ptr_pend->tres_fmt_alloc_str = NULL;
	job_ptr_pend->tres_alloc_cnt = NULL;

	job_ptr_pend->cpus_per_tres = xstrdup(job_ptr->cpus_per_tres);
	job_ptr_pend->mem_per_tres = xstrdup(job_ptr->mem_per_tres);
	job_ptr_pend->tres_bind = xstrdup(job_ptr->tres_bind);
	job_ptr_pend->tres_freq = xstrdup(job_ptr->tres_freq);
	job_ptr_pend->tres_per_job = xstrdup(job_ptr->tres_per_job);
	job_ptr_pend->tres_per_node = xstrdup(job_ptr->tres_per_node);
	job_ptr_pend->tres_per_socket = xstrdup(job_ptr->tres_per_socket);
	job_ptr_pend->tres_per_task = xstrdup(job_ptr->tres_per_task);

	job_ptr_pend->user_name = xstrdup(job_ptr->user_name);
	job_ptr_pend->wckey = xstrdup(job_ptr->wckey);
	job_ptr_pend->deadline = job_ptr->deadline;

	job_details = job_ptr->details;
	details_new = job_ptr_pend->details;
	memcpy(details_new, job_details, sizeof(job_details_t));

	/*
	 * Reset the preempt_start_time or high priority array jobs will hang
	 * for a period before preempting more jobs.
	 */
	details_new->preempt_start_time = 0;

	details_new->acctg_freq = xstrdup(job_details->acctg_freq);
	if (job_details->argc) {
		details_new->argv =
			xcalloc((job_details->argc + 1), sizeof(char *));
		for (i = 0; i < job_details->argc; i++) {
			details_new->argv[i] = xstrdup(job_details->argv[i]);
		}
	}
	details_new->cpu_bind = xstrdup(job_details->cpu_bind);
	details_new->cpu_bind_type = job_details->cpu_bind_type;
	details_new->cpu_freq_min = job_details->cpu_freq_min;
	details_new->cpu_freq_max = job_details->cpu_freq_max;
	details_new->cpu_freq_gov = job_details->cpu_freq_gov;
	details_new->depend_list = depended_list_copy(job_details->depend_list);
	details_new->dependency = xstrdup(job_details->dependency);
	details_new->orig_dependency = xstrdup(job_details->orig_dependency);
	if (job_details->env_cnt) {
		details_new->env_sup =
			xcalloc((job_details->env_cnt + 1), sizeof(char *));
		for (i = 0; i < job_details->env_cnt; i++) {
			details_new->env_sup[i] =
				xstrdup(job_details->env_sup[i]);
		}
	}
	if (job_details->exc_node_bitmap) {
		details_new->exc_node_bitmap =
			bit_copy(job_details->exc_node_bitmap);
	}
	details_new->exc_nodes = xstrdup(job_details->exc_nodes);
	details_new->feature_list =
		feature_list_copy(job_details->feature_list);
	details_new->features = xstrdup(job_details->features);
	details_new->cluster_features = xstrdup(job_details->cluster_features);
	if (job_details->job_size_bitmap) {
		details_new->job_size_bitmap =
			bit_copy(job_details->job_size_bitmap);
	}
	details_new->prefer = xstrdup(job_details->prefer);
	details_new->prefer_list =
		feature_list_copy(job_details->prefer_list);
	set_job_features_use(details_new);
	if (job_details->mc_ptr) {
		i = sizeof(multi_core_data_t);
		details_new->mc_ptr = xmalloc(i);
		memcpy(details_new->mc_ptr, job_details->mc_ptr, i);
	}
	details_new->mem_bind = xstrdup(job_details->mem_bind);
	details_new->mem_bind_type = job_details->mem_bind_type;
	details_new->qos_req = xstrdup(job_details->qos_req);
	if (job_details->req_node_bitmap) {
		details_new->req_node_bitmap =
			bit_copy(job_details->req_node_bitmap);
	}
	details_new->req_context = xstrdup(job_details->req_context);
	details_new->req_nodes = xstrdup(job_details->req_nodes);
	details_new->std_err = xstrdup(job_details->std_err);
	details_new->std_in = xstrdup(job_details->std_in);
	details_new->std_out = xstrdup(job_details->std_out);
	details_new->submit_line = xstrdup(job_details->submit_line);
	details_new->work_dir = xstrdup(job_details->work_dir);
	details_new->x11_magic_cookie = xstrdup(job_details->x11_magic_cookie);
	details_new->env_hash = xstrdup(job_details->env_hash);
	details_new->script_hash = xstrdup(job_details->script_hash);

	if (job_ptr->gres_list_req) {
		if (details_new->whole_node & WHOLE_NODE_REQUIRED) {
			multi_core_data_t *mc_ptr = details_new->mc_ptr;
			gres_job_state_validate_t gres_js_val = {
				.cpus_per_tres = job_ptr_pend->cpus_per_tres,
				.mem_per_tres = job_ptr_pend->mem_per_tres,
				.tres_freq = job_ptr_pend->tres_freq,
				.tres_per_job = job_ptr_pend->tres_per_job,
				.tres_per_node = job_ptr_pend->tres_per_node,
				.tres_per_socket = job_ptr->tres_per_socket,
				.tres_per_task = job_ptr->tres_per_task,

				.cpus_per_task =
				&details_new->orig_cpus_per_task,
				.max_nodes = &details_new->max_nodes,
				.min_cpus = &details_new->min_cpus,
				.min_nodes = &details_new->min_nodes,
				.ntasks_per_node =
				&details_new->ntasks_per_node,
				.ntasks_per_socket = &mc_ptr->ntasks_per_socket,
				.ntasks_per_tres =
				&details_new->ntasks_per_tres,
				.num_tasks = &details_new->num_tasks,
				.sockets_per_node = &mc_ptr->sockets_per_node,

				.gres_list = &job_ptr_pend->gres_list_req,
			};

			/*
			 * We need to reset the gres_list to what was requested
			 * instead of what was given exclusively.
			 */
			job_ptr_pend->gres_list_req = NULL;
			(void)gres_job_state_validate(&gres_js_val);
		} else
			job_ptr_pend->gres_list_req =
				gres_job_state_list_dup(job_ptr->gres_list_req);
	}
	job_ptr_pend->gres_list_req_accum = NULL;
	job_ptr_pend->gres_list_alloc = NULL;
	job_ptr_pend->gres_detail_cnt = 0;
	job_ptr_pend->gres_detail_str = NULL;
	job_ptr_pend->gres_used = NULL;

	if (job_ptr->fed_details) {
		add_fed_job_info(job_ptr);
		/*
		 * The new (split) job needs its remote dependencies tested
		 * separately from just the meta job, so send remote
		 * dependencies to siblings if needed.
		 */
		if (job_ptr->details->dependency &&
		    job_ptr->details->depend_list)
			fed_mgr_submit_remote_dependencies(job_ptr, false,
							   false);
	}

	on_job_state_change(job_ptr, job_ptr->job_state);
	on_job_state_change(job_ptr_pend, job_ptr_pend->job_state);

	return job_ptr_pend;
}

/* Add job array data stucture to the job record */
static void _create_job_array(job_record_t *job_ptr, job_desc_msg_t *job_desc)
{
	job_details_t *details;
	char *sep = NULL;
	int max_run_tasks, min_task_id, max_task_id, step_task_id = 1, task_cnt;

	if (!job_desc->array_bitmap)
		return;

	if ((min_task_id = bit_ffs(job_desc->array_bitmap)) == -1) {
		info("%s: %pJ array_bitmap is empty", __func__, job_ptr);
		return;
	}

	job_ptr->array_job_id = job_ptr->job_id;
	job_ptr->array_recs = xmalloc(sizeof(job_array_struct_t));
	max_task_id = bit_fls(job_desc->array_bitmap);
	task_cnt = bit_set_count(job_desc->array_bitmap);
	bit_realloc(job_desc->array_bitmap, max_task_id + 1);
	job_ptr->array_recs->task_id_bitmap = job_desc->array_bitmap;
	job_desc->array_bitmap = NULL;
	job_ptr->array_recs->task_cnt =
		bit_set_count(job_ptr->array_recs->task_id_bitmap);
	if (job_ptr->array_recs->task_cnt > 1)
		job_count += (job_ptr->array_recs->task_cnt - 1);

	if (job_desc->array_inx)
		sep = strchr(job_desc->array_inx, '%');
	if (sep) {
		max_run_tasks = atoi(sep + 1);
		if (max_run_tasks > 0)
			job_ptr->array_recs->max_run_tasks = max_run_tasks;
	}

	details = job_ptr->details;
	if (details) {
		if (job_desc->array_inx) {
			sep = strchr(job_desc->array_inx, ':');
			if (sep)
				step_task_id = atoi(sep + 1);
		}
		xrecalloc(details->env_sup,
			  MAX(job_ptr->details->env_cnt, 1) + 4,
			  sizeof(char *));
		xstrfmtcat(details->env_sup[details->env_cnt++],
			   "SLURM_ARRAY_TASK_COUNT=%d", task_cnt);
		xstrfmtcat(details->env_sup[details->env_cnt++],
			   "SLURM_ARRAY_TASK_MIN=%d", min_task_id);
		xstrfmtcat(details->env_sup[details->env_cnt++],
			   "SLURM_ARRAY_TASK_MAX=%d", max_task_id);
		xstrfmtcat(details->env_sup[details->env_cnt++],
			   "SLURM_ARRAY_TASK_STEP=%d", step_task_id);
	}

	on_job_state_change(job_ptr, job_ptr->job_state);
}

static int _select_nodes_base(job_node_select_t *job_node_select)
{
	job_node_select->rc_part_limits =
		job_limits_check(&job_node_select->job_ptr, false);

	if ((job_node_select->rc_part_limits != WAIT_NO_REASON) &&
	    (slurm_conf.enforce_part_limits == PARTITION_ENFORCE_ANY))
		return SLURM_ERROR;

	if ((job_node_select->rc_part_limits != WAIT_NO_REASON) &&
	    (slurm_conf.enforce_part_limits == PARTITION_ENFORCE_ALL)) {
		if (job_node_select->rc_part_limits != WAIT_PART_DOWN) {
			job_node_select->rc_best =
				ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE;
			return SLURM_SUCCESS;
		} else {
			job_node_select->rc_best = ESLURM_PARTITION_DOWN;
		}
	}

	if (job_node_select->rc_part_limits == WAIT_NO_REASON) {
		job_node_select->rc = select_nodes(job_node_select,
						   job_node_select->test_only,
						   true,
						   SLURMDB_JOB_FLAG_SUBMIT);
	} else {
		job_node_select->rc = select_nodes(job_node_select,
						   true,
						   true,
						   SLURMDB_JOB_FLAG_SUBMIT);
		if ((job_node_select->rc == SLURM_SUCCESS) &&
		    (job_node_select->rc_part_limits == WAIT_PART_DOWN))
			job_node_select->rc = ESLURM_PARTITION_DOWN;
	}
	if ((job_node_select->rc == ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE) &&
	    (slurm_conf.enforce_part_limits == PARTITION_ENFORCE_ALL)) {
		/* Job can not run */
		job_node_select->rc_best = job_node_select->rc;
		return SLURM_SUCCESS;
	}
	if ((job_node_select->rc != ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE) &&
	    (job_node_select->rc != ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE) &&
	    (job_node_select->rc != ESLURM_REQUESTED_TOPO_CONFIG_UNAVAILABLE) &&
	    (job_node_select->rc != ESLURM_RESERVATION_BUSY) &&
	    (job_node_select->rc != ESLURM_NODES_BUSY)) {
		/* Job can run now */
		job_node_select->rc_best = job_node_select->rc;
		if ((slurm_conf.enforce_part_limits ==
		     PARTITION_ENFORCE_ANY) ||
		    (slurm_conf.enforce_part_limits ==
		     PARTITION_ENFORCE_NONE) ||
		    (!job_node_select->test_only &&
		     (job_node_select->rc_part_limits == WAIT_NO_REASON)))
			return SLURM_SUCCESS;
	}
	if (((job_node_select->rc == ESLURM_NODES_BUSY) ||
	     (job_node_select->rc == ESLURM_RESERVATION_BUSY) ||
	     (job_node_select->rc == ESLURM_PORTS_BUSY)) &&
	    (job_node_select->rc_best == -1)) {
		if (job_node_select->test_only)
			return SLURM_SUCCESS;

		/* Keep looking for partition where job can start now */
		job_node_select->rc_best = job_node_select->rc;
	}
	if ((job_node_select->job_ptr->preempt_in_progress) &&
	    (job_node_select->rc != ESLURM_NODES_BUSY)) {
		/* Already started preempting jobs, don't
		 * consider starting this job in another
		 * partition as we iterator over others. */
		job_node_select->test_only = true;
	}

	return SLURM_ERROR;
}

static int _foreach_select_nodes_resvs(void *object, void *args)
{
	slurmctld_resv_t *resv_ptr = object;
	job_node_select_t *job_node_select = args;
	job_record_t *job_ptr = job_node_select->job_ptr;

	job_ptr->resv_ptr = resv_ptr;
	job_ptr->resv_id = resv_ptr->resv_id;

	if ((job_ptr->bit_flags & JOB_PART_ASSIGNED) && resv_ptr->part_ptr)
		job_ptr->part_ptr = resv_ptr->part_ptr;

	debug2("Try %pJ on next reservation %s", job_ptr, resv_ptr->name);

	if ((job_node_select->rc_resv =
	     _select_nodes_base(job_node_select)) == SLURM_SUCCESS) {
		/* break if success */
		if ((job_node_select->rc != ESLURM_RESERVATION_NOT_USABLE) &&
		    (job_node_select->rc != ESLURM_RESERVATION_BUSY)) {
			return -1;
		}
	}

	return 0;
}

static int _select_nodes_resvs(job_node_select_t *job_node_select)
{
	job_record_t *job_ptr = job_node_select->job_ptr;

	if (!job_ptr->resv_list)
		return _select_nodes_base(job_node_select);

	job_node_select->rc_resv = SLURM_ERROR;
	(void) list_for_each(job_ptr->resv_list,
			     _foreach_select_nodes_resvs,
			     job_node_select);

	return job_node_select->rc_resv;
}

static int _foreach_select_nodes_qos(void *object, void *args)
{
	slurmdb_qos_rec_t *qos_ptr = object;
	job_node_select_t *job_node_select = args;
	job_record_t *job_ptr = job_node_select->job_ptr;

	job_ptr->qos_ptr = qos_ptr;

	debug2("Try %pJ on next QOS %s", job_ptr, qos_ptr->name);

	/* break if success */
	if ((job_node_select->rc_qos =
	     _select_nodes_resvs(job_node_select)) == SLURM_SUCCESS)
		return -1;

	return 0;
}

static int _select_nodes_qos(job_node_select_t *job_node_select)
{
	job_record_t *job_ptr = job_node_select->job_ptr;

	if (!job_ptr->qos_list)
		return _select_nodes_resvs(job_node_select);

	job_node_select->rc_qos = SLURM_ERROR;
	(void) list_for_each(job_ptr->qos_list,
			     _foreach_select_nodes_qos,
			     job_node_select);

	return job_node_select->rc_qos;
}

/*
 * Wrapper for select_nodes() function that will test all valid partitions
 * for a new job
 * IN job_ptr - pointer to the job record
 * IN test_only - if set do not allocate nodes, just confirm they
 *	could be allocated now
 * OUT err_msg - error message for job, caller must xfree
 */
static int _select_nodes_parts(job_record_t *job_ptr, bool test_only,
			       char **err_msg)
{
	part_record_t *part_ptr;
	list_itr_t *iter;
	job_node_select_t job_node_select = {
		.err_msg = err_msg,
		.job_ptr = job_ptr,
		.rc_part_limits = WAIT_NO_REASON,
		.rc = ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE,
		.rc_best = -1,
		.test_only = test_only,
	};
	int rc, best_rc, part_limits_rc;

	if (job_ptr->part_ptr_list) {
		/* part_ptr_list is already sorted */
		/*
		 * This iter can not be a list_for_each() we eventually will
		 * call select_nodes() which will then call
		 * rebuild_job_part_list() which will need to access this list
		 * again.
		 */
		iter = list_iterator_create(job_ptr->part_ptr_list);
		while ((part_ptr = list_next(iter))) {
			job_ptr->part_ptr = part_ptr;
			debug2("Try %pJ on next partition %s",
			       job_ptr, part_ptr->name);

			if (_select_nodes_qos(&job_node_select) ==
			    SLURM_SUCCESS)
				break;
		}
		list_iterator_destroy(iter);
	} else {
		/*
		 * We don't need to check the return code of this as the rc we
		 * are sending in is the rc we care about.
		 */
		(void)_select_nodes_qos(&job_node_select);
	}

	rc = job_node_select.rc;
	best_rc = job_node_select.rc_best;
	part_limits_rc = job_node_select.rc_part_limits;
	if (best_rc != -1)
		rc = best_rc;
	else if (part_limits_rc == WAIT_PART_DOWN)
		rc = ESLURM_PARTITION_DOWN;
	if (rc == ESLURM_NODES_BUSY)
		job_ptr->state_reason = WAIT_RESOURCES;
	else if ((rc == ESLURM_RESERVATION_BUSY) ||
		 (rc == ESLURM_RESERVATION_NOT_USABLE))
		job_ptr->state_reason = WAIT_RESERVATION;
	else if (rc == ESLURM_JOB_HELD)
		/* Do not reset the state_reason field here. select_nodes()
		 * already set the state_reason field, and this error code
		 * does not distinguish between user and admin holds. */
		;
	else if (rc == ESLURM_NODE_NOT_AVAIL)
		job_ptr->state_reason = WAIT_NODE_NOT_AVAIL;
	else if (rc == ESLURM_QOS_THRES)
		job_ptr->state_reason = WAIT_QOS_THRES;
	else if (rc == ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE)
		job_ptr->state_reason = WAIT_PART_CONFIG;
	else if (rc == ESLURM_BURST_BUFFER_WAIT)
		job_ptr->state_reason = WAIT_BURST_BUFFER_RESOURCE;
	else if (rc == ESLURM_PARTITION_DOWN)
		job_ptr->state_reason = WAIT_PART_DOWN;
	else if (rc == ESLURM_INVALID_QOS)
		job_ptr->state_reason = FAIL_QOS;
	else if (rc == ESLURM_INVALID_ACCOUNT)
		job_ptr->state_reason = FAIL_ACCOUNT;

	return rc;
}

static inline bool _has_deadline(job_record_t *job_ptr)
{
	if ((job_ptr->deadline) && (job_ptr->deadline != NO_VAL)) {
		queue_job_scheduler();
		return true;
	}
	return false;
}

/*
 * job_allocate - create job_records for the supplied job specification and
 *	allocate nodes for it.
 * IN job_desc - job specifications
 * IN immediate - if set then either initiate the job immediately or fail
 * IN will_run - don't initiate the job if set, just test if it could run
 *	now or later
 * OUT resp - will run response (includes start location, time, etc.)
 * IN allocate - resource allocation request only if set, batch job if zero
 * IN submit_uid -uid of user issuing the request
 * OUT job_pptr - set to pointer to job record
 * OUT err_msg - Custom error message to the user, caller to xfree results
 * IN protocol_version - version of the code the caller is using
 * RET 0 or an error code. If the job would only be able to execute with
 *	some change in partition configuration then
 *	ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE is returned
 * globals: job_list - pointer to global job list
 *	list_part - global list of partition info
 *	default_part_loc - pointer to default partition
 */
extern int job_allocate(job_desc_msg_t *job_desc, int immediate,
			int will_run, will_run_response_msg_t **resp,
			int allocate, uid_t submit_uid, bool cron,
			job_record_t **job_pptr, char **err_msg,
			uint16_t protocol_version)
{
	static time_t sched_update = 0;
	static bool defer_batch = false, defer_sched = false;
	static bool ignore_prefer_val = false, ignore_constraint_val = false;
	int error_code, i;
	bool no_alloc, top_prio, test_only, too_fragmented, independent;
	job_record_t *job_ptr;
	time_t now = time(NULL);
	bool held_user = false;
	bool defer_this = false;

	xassert(verify_lock(CONF_LOCK, READ_LOCK));
	xassert(verify_lock(JOB_LOCK, WRITE_LOCK));
	xassert(verify_lock(NODE_LOCK, WRITE_LOCK));
	xassert(verify_lock(PART_LOCK, READ_LOCK));

	if (sched_update != slurm_conf.last_update) {
		char *tmp_ptr;
		sched_update = slurm_conf.last_update;
		defer_batch = defer_sched = false;
		if (xstrcasestr(slurm_conf.sched_params, "defer_batch"))
			defer_batch = true;
		else if (xstrcasestr(slurm_conf.sched_params, "defer"))
			defer_sched = true;
		if ((tmp_ptr = xstrcasestr(slurm_conf.sched_params,
		                           "delay_boot="))) {
			char *tmp_comma;
			if ((tmp_comma = xstrstr(tmp_ptr, ",")))
				*tmp_comma = '\0';
			i = time_str2secs(tmp_ptr + 11);
			if (i != NO_VAL)
				delay_boot = i;
			if (tmp_comma)
				*tmp_comma = ',';
		}
		bf_min_age_reserve = 0;
		if ((tmp_ptr = xstrcasestr(slurm_conf.sched_params,
					   "bf_min_age_reserve="))) {
			int min_age = atoi(tmp_ptr + 19);
			if (min_age > 0)
				bf_min_age_reserve = min_age;
		}

		if (xstrcasestr(slurm_conf.sched_params, "allow_zero_lic"))
			validate_cfgd_licenses = false;

		if (xstrcasestr(slurm_conf.sched_params,
				"ignore_prefer_validation"))
			ignore_prefer_val = true;
		else
			ignore_prefer_val = false;
		if (xstrcasestr(slurm_conf.sched_params,
				"ignore_constraint_validation"))
			ignore_constraint_val = true;
		else
			ignore_constraint_val = false;
	}

	if (job_desc->array_bitmap)
		i = bit_set_count(job_desc->array_bitmap);
	else
		i = 1;

	if ((job_count + i) >= slurm_conf.max_job_cnt) {
		error("%s: MaxJobCount limit from slurm.conf reached (%u)",
		      __func__, slurm_conf.max_job_cnt);
		return EAGAIN;
	}

	error_code = _job_create(job_desc, allocate, will_run, cron,
				 &job_ptr, submit_uid, err_msg,
				 protocol_version);
	*job_pptr = job_ptr;
	if (error_code) {
		if (job_ptr && (immediate || will_run)) {
			/* this should never really happen here */
			job_state_set(job_ptr, JOB_FAILED);
			job_ptr->exit_code = 1;
			job_ptr->state_reason = FAIL_BAD_CONSTRAINTS;
			xfree(job_ptr->state_desc);
			job_ptr->start_time = job_ptr->end_time = now;
			job_completion_logger(job_ptr, false);
			error("%s: setting %pJ to \"%s\"",
			      __func__, job_ptr,
			      job_state_reason_string(job_ptr->state_reason));
		}
		return error_code;
	}
	xassert(job_ptr);
	if (job_desc->array_bitmap)
		independent = false;
	else
		independent = job_independent(job_ptr);
	/*
	 * priority needs to be calculated after this since we set a
	 * begin time in job_independent and that lets us know if the
	 * job is eligible.
	 */
	if (job_ptr->priority == NO_VAL)
		set_job_prio(job_ptr);

	if (job_ptr->state_reason == WAIT_HELD_USER)
		held_user = true;

	/* Avoid resource fragmentation if important */
	if ((submit_uid || (job_desc->req_nodes == NULL)) &&
	    independent && job_is_completing(NULL))
		too_fragmented = true;	/* Don't pick nodes for job now */
	/*
	 * FIXME: Ideally we only want to refuse the request if the
	 * required node list is insufficient to satisfy the job's
	 * processor or node count requirements, but the overhead is
	 * rather high to do that right here. We let requests from
	 * user root proceed if a node list is specified, for
	 * meta-schedulers (e.g. Maui, Moab, etc.).
	 */
	else
		too_fragmented = false;

	defer_this = defer_sched || (defer_batch && job_ptr->batch_flag);

	if (independent && (!too_fragmented) && !defer_this)
		top_prio = _top_priority(job_ptr, job_desc->het_job_offset);
	else
		top_prio = true;	/* don't bother testing,
					 * it is not runable anyway */

	if (immediate &&
	    (too_fragmented || (!top_prio) || (!independent) || defer_this)) {
		job_state_set(job_ptr, JOB_FAILED);
		job_ptr->exit_code  = 1;
		job_ptr->state_reason = FAIL_BAD_CONSTRAINTS;
		xfree(job_ptr->state_desc);
		job_ptr->start_time = job_ptr->end_time = now;
		job_completion_logger(job_ptr, false);
		if (!independent) {
			debug2("%s: setting %pJ to \"%s\" due to dependency (%s)",
			       __func__, job_ptr,
			       job_state_reason_string(job_ptr->state_reason),
			       slurm_strerror(ESLURM_DEPENDENCY));
			return ESLURM_DEPENDENCY;
		}
		else if (too_fragmented) {
			debug2("%s: setting %pJ to \"%s\" due to fragmentation (%s)",
			       __func__, job_ptr,
			       job_state_reason_string(job_ptr->state_reason),
			       slurm_strerror(ESLURM_FRAGMENTATION));
			return ESLURM_FRAGMENTATION;
		}
		else if (!top_prio) {
			debug2("%s: setting %pJ to \"%s\" because it's not top priority (%s)",
			       __func__, job_ptr,
			       job_state_reason_string(job_ptr->state_reason),
			       slurm_strerror(ESLURM_NOT_TOP_PRIORITY));
			return ESLURM_NOT_TOP_PRIORITY;
		} else {
			job_ptr->state_reason = FAIL_DEFER;
			debug2("%s: setting %pJ to \"%s\" due to SchedulerParameters=defer (%s)",
			       __func__, job_ptr,
			       job_state_reason_string(job_ptr->state_reason),
			       slurm_strerror(ESLURM_DEFER));
			return ESLURM_DEFER;
		}
	}

	if (will_run && resp) {
		int rc;
		rc = job_start_data(job_ptr, resp);
		job_state_set(job_ptr, JOB_FAILED);
		job_ptr->exit_code  = 1;
		job_ptr->start_time = job_ptr->end_time = now;
		purge_job_record(job_ptr->job_id);
		return rc;
	}

	/*
	 * We should have a job_ptr->details here if not something is really
	 * wrong.
	 */
	xassert(job_ptr->details);

	/*
	 * fed jobs need to go to the siblings first so don't attempt to
	 * schedule the job now.
	 */
	test_only = will_run || job_ptr->deadline || (allocate == 0) ||
		job_ptr->fed_details;

	no_alloc = test_only || too_fragmented || _has_deadline(job_ptr) ||
		(!top_prio) || (!independent) || !avail_front_end(job_ptr) ||
		(job_desc->het_job_offset != NO_VAL) || defer_this ||
		(job_ptr->details->prefer && ignore_prefer_val) ||
		(job_ptr->details->features && ignore_constraint_val);

	no_alloc = no_alloc || (bb_g_job_test_stage_in(job_ptr, no_alloc) != 1);

	no_alloc = no_alloc || (!job_ptr->resv_name &&
				get_magnetic_resv_count());

	/*
	 * If we have a prefer feature list check that, if not check the
	 * normal features.
	 */
	if (job_ptr->details->prefer && !ignore_prefer_val) {
		job_ptr->details->features_use = job_ptr->details->prefer;
		job_ptr->details->feature_list_use =
			job_ptr->details->prefer_list;
	} else if (!ignore_constraint_val) {
		job_ptr->details->features_use = job_ptr->details->features;
		job_ptr->details->feature_list_use =
			job_ptr->details->feature_list;
	} else {
		/*
		 * Set features_use to "" because ignore_constraint_val is set.
		 * We also set no_alloc to true to avoid actually allocating
		 * with this setup.
		 * We are using an empty string rather than NULL because
		 * valid_feature_counts() will use features rather than
		 * features_use if it is NULL.
		 */
		job_ptr->details->features_use = "";
		job_ptr->details->feature_list_use = NULL;
	}

	error_code = _select_nodes_parts(job_ptr, no_alloc, err_msg);

	set_job_features_use(job_ptr->details);

	if (!test_only) {
		last_job_update = now;
	}

	if (held_user)
		job_ptr->state_reason = WAIT_HELD_USER;
	/*
	 * Moved this (_create_job_array) here to handle when a job
	 * array is submitted since we
	 * want to know the array task count when we check the job against
	 * QOS/Assoc limits
	 */
	_create_job_array(job_ptr, job_desc);

	slurmctld_diag_stats.jobs_submitted +=
		(job_ptr->array_recs && job_ptr->array_recs->task_cnt) ?
		job_ptr->array_recs->task_cnt : 1;

	acct_policy_add_job_submit(job_ptr, false);

	/*
	 * This only needs to happen if the job didn't schedule immediately.
	 * select_nodes() can start it if there are nodes available, but if
	 * that didn't happened send the start record now.
	 */
	if (!IS_JOB_IN_DB(job_ptr))
		jobacct_storage_g_job_start(acct_db_conn, job_ptr);

	if ((error_code == ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE) &&
	    (slurm_conf.enforce_part_limits != PARTITION_ENFORCE_NONE))
		;	/* Reject job submission */
	else if ((error_code == ESLURM_NODES_BUSY) ||
		 (error_code == ESLURM_RESERVATION_BUSY) ||
		 (error_code == ESLURM_JOB_HELD) ||
		 (error_code == ESLURM_NODE_NOT_AVAIL) ||
		 (error_code == ESLURM_QOS_THRES) ||
		 (error_code == ESLURM_ACCOUNTING_POLICY) ||
		 (error_code == ESLURM_RESERVATION_NOT_USABLE) ||
		 (error_code == ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE) ||
		 (error_code == ESLURM_BURST_BUFFER_WAIT) ||
		 (error_code == ESLURM_PARTITION_DOWN) ||
		 (error_code == ESLURM_LICENSES_UNAVAILABLE) ||
		 (error_code == ESLURM_PORTS_BUSY) ||
		 ((error_code == ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE) &&
		  (job_ptr->state_reason == FAIL_CONSTRAINTS))) {
		/*
		 * Non-fatal error, but job can't be scheduled right now.
		 *
		 * Note: Keep list in sync with nonfatal_errors[] in
		 * openapi/slurmctld.
		 */
		if (immediate) {
			job_state_set(job_ptr, JOB_FAILED);
			job_ptr->exit_code  = 1;
			job_ptr->state_reason = FAIL_BAD_CONSTRAINTS;
			xfree(job_ptr->state_desc);
			job_ptr->start_time = job_ptr->end_time = now;
			job_completion_logger(job_ptr, false);
			debug2("%s: setting %pJ to \"%s\" because it cannot be immediately allocated (%s)",
			       __func__, job_ptr,
			       job_state_reason_string(job_ptr->state_reason),
			       slurm_strerror(error_code));
		} else {	/* job remains queued */
			if ((error_code == ESLURM_NODES_BUSY) ||
			    (error_code == ESLURM_BURST_BUFFER_WAIT) ||
			    (error_code == ESLURM_RESERVATION_BUSY) ||
			    (error_code == ESLURM_ACCOUNTING_POLICY) ||
			    (error_code == ESLURM_PORTS_BUSY) ||
			    ((error_code == ESLURM_PARTITION_DOWN) &&
			     (job_ptr->batch_flag))) {
				job_ptr->details->features_use = NULL;
				job_ptr->details->feature_list_use = NULL;
				error_code = SLURM_SUCCESS;
			}
		}
		return error_code;
	}

	if (error_code) {	/* fundamental flaw in job request */
		job_state_set(job_ptr, JOB_FAILED);
		job_ptr->exit_code  = 1;
		job_ptr->state_reason = FAIL_BAD_CONSTRAINTS;
		xfree(job_ptr->state_desc);
		job_ptr->start_time = job_ptr->end_time = now;
		job_completion_logger(job_ptr, false);
		debug2("%s: setting %pJ to \"%s\" due to a flaw in the job request (%s)",
		       __func__, job_ptr,
		       job_state_reason_string(job_ptr->state_reason),
		       slurm_strerror(error_code));
		return error_code;
	}

	if (will_run) {		/* job would run, flag job destruction */
		job_state_set(job_ptr, JOB_FAILED);
		job_ptr->exit_code  = 1;
		job_ptr->start_time = job_ptr->end_time = now;
		purge_job_record(job_ptr->job_id);
	}

	if (!will_run) {
		sched_debug2("%pJ allocated resources: NodeList=%s",
			     job_ptr, job_ptr->nodes);
		rebuild_job_part_list(job_ptr);
	}

	return SLURM_SUCCESS;
}

/*
 * job_fail - terminate a job due to initiation failure
 * IN job_ptr - Pointer to job to be killed
 * IN job_state - desired job state (JOB_BOOT_FAIL, JOB_NODE_FAIL, etc.)
 * RET 0 on success, otherwise ESLURM error code
 */
static int _job_fail(job_record_t *job_ptr, uint32_t job_state)
{
	time_t now = time(NULL);
	bool suspended = false;

	if (IS_JOB_FINISHED(job_ptr))
		return ESLURM_ALREADY_DONE;
	if (IS_JOB_SUSPENDED(job_ptr)) {
		uint32_t suspend_job_state = job_ptr->job_state;
		/*
		 * we can't have it as suspended when we call the
		 * accounting stuff.
		 */
		job_state_set(job_ptr, JOB_CANCELLED);
		jobacct_storage_g_job_suspend(acct_db_conn, job_ptr);
		job_state_set(job_ptr, suspend_job_state);
		suspended = true;
	}

	if (IS_JOB_CONFIGURING(job_ptr) || IS_JOB_RUNNING(job_ptr) ||
	    suspended) {
		/* No need to signal steps, deallocate kills them */
		job_ptr->time_last_active       = now;
		if (suspended) {
			job_ptr->end_time       = job_ptr->suspend_time;
			job_ptr->tot_sus_time  +=
				difftime(now, job_ptr->suspend_time);
		} else
			job_ptr->end_time       = now;
		last_job_update                 = now;
		job_state_set(job_ptr, (job_state | JOB_COMPLETING));
		job_ptr->exit_code = 1;
		job_ptr->state_reason = FAIL_LAUNCH;
		xfree(job_ptr->state_desc);
		job_completion_logger(job_ptr, false);
		if (job_ptr->node_bitmap) {
			build_cg_bitmap(job_ptr);
			deallocate_nodes(job_ptr, false, suspended, false);
		}
		return SLURM_SUCCESS;
	}
	/* All other states */
	verbose("job_fail: %pJ can't be killed from state=%s",
		job_ptr, job_state_string(job_ptr->job_state));

	return ESLURM_TRANSITION_STATE_NO_UPDATE;

}

/*
 * IN signal_args - Append the response to signal_args->responses.
 * IN cluster_id - If set, then this identifies the sibling cluster that the
 *                 job is running on or originated from.
 * IN eror_code - Error code to use in the response.
 * IN err_msg - If set, use this as the response error message.
 * IN id - Identifier for the job. Job id is different than the actual job id
 *         if the job is an array task or a het job component that is not the
 *         het job leader.
 * IN real_job_id - The real job id or NO_VAL
 */
static void _add_signal_job_resp(signal_jobs_args_t *signal_args,
				 char *sibling_name, int error_code,
				 char *err_msg, slurm_selected_step_t *id,
				 uint32_t real_job_id)
{
	kill_jobs_resp_job_t *job_resp = xmalloc(sizeof(*job_resp));

	job_resp->error_code = error_code;
	if (err_msg)
		job_resp->error_msg = err_msg;
	else if (error_code != SLURM_SUCCESS)
		job_resp->error_msg = xstrdup(slurm_strerror(error_code));
	job_resp->id = xmalloc(sizeof(*job_resp->id));
	memcpy(job_resp->id, id, sizeof(*id));
	/* Full copy job_resp->id->array_bitmap */
	if (id->array_bitmap)
		job_resp->id->array_bitmap = bit_copy(id->array_bitmap);

	job_resp->real_job_id = real_job_id;
	job_resp->sibling_name = sibling_name;

	list_append(signal_args->responses, job_resp);
}

static int _match_part_name(void *x, void *key)
{
	part_record_t *part_ptr = x;
	char *part_name = key;

	if (!xstrcmp(part_ptr->name, part_name))
		return 1;
	return 0;
}

static int _match_resv_name(void *x, void *key)
{
	slurmctld_resv_t *resv_ptr = x;
	char *resv_name = key;

	if (!xstrcmp(resv_ptr->name, resv_name))
		return 1;
	return 0;
}

static void _slurm_selected_step_init(job_record_t *job_ptr,
				      slurm_selected_step_t *id)
{
	xassert(job_ptr);

	id->array_bitmap = NULL;
	id->array_task_id = job_ptr->array_task_id;
	if (job_ptr->array_task_id != NO_VAL)
		id->step_id.job_id = job_ptr->array_job_id;
	else if (job_ptr->het_job_offset)
		id->step_id.job_id = job_ptr->het_job_id;
	else
		id->step_id.job_id = job_ptr->job_id;

	if (job_ptr->het_job_offset)
		id->het_job_offset = job_ptr->het_job_offset;
	else
		id->het_job_offset = NO_VAL;

	id->step_id.step_het_comp = NO_VAL;
	id->step_id.step_id = NO_VAL;
}

static void _handle_signal_filter_mismatch(job_record_t *job_ptr,
					   signal_jobs_args_t *signal_args,
					   uint32_t error_code,
					   char *filter_err_msg)
{
	slurm_selected_step_t id;
	char *err_msg = NULL;

	/*
	 * If the job is revoked on this cluster and started on a sibling, the
	 * revoked job's state, reservation, and partition will not necessarily
	 * match the other cluster, and the other cluster has the cluster lock
	 * for this job. For example, this job's state is 0+REVOKED and the job
	 * state on the other cluster could be suspended, running, etc.
	 * In that case, always send a response back to the client that we
	 * could not signal the job.
	 */
	if (fed_mgr_fed_rec && fed_mgr_job_started_on_sib(job_ptr)) {
		char *sib_name;

		sib_name = fed_mgr_get_cluster_name(
			job_ptr->fed_details->cluster_lock);
		err_msg = xstrdup_printf("Job started on sibling cluster %s: %s",
					 sib_name, slurm_strerror(error_code));
		_slurm_selected_step_init(job_ptr, &id);
		_add_signal_job_resp(signal_args, sib_name, error_code,
				     err_msg, &id, job_ptr->job_id);
		/* sib_name is added to the job_resp, do not free */
		return;
	}

	if (!signal_args->filter_specific_job_ids)
		return;

	if (filter_err_msg)
		err_msg = xstrdup_printf("%s: %s",
					 filter_err_msg,
					 slurm_strerror(error_code));
	else
		err_msg = xstrdup_printf("%s", slurm_strerror(error_code));

	_slurm_selected_step_init(job_ptr, &id);
	_add_signal_job_resp(signal_args, NULL, error_code,
			     err_msg, &id, job_ptr->job_id);
}

static bool _signal_job_matches_filter(job_record_t *job_ptr,
				       signal_jobs_args_t *signal_args)
{
	bool matches_filter = true;
	int error_code = ESLURM_JOB_SIGNAL_FAILED;
	uint32_t job_base_state = job_ptr->job_state & JOB_STATE_BASE;
	char *filter_err_msg = NULL;
	kill_jobs_msg_t *kill_msg = signal_args->kill_msg;

	if (IS_JOB_FINISHED(job_ptr)) {
		error_code = ESLURM_ALREADY_DONE;
		matches_filter = false;
		goto fini;
	}

	if (kill_msg->account && xstrcmp(job_ptr->account, kill_msg->account)) {
		if (signal_args->filter_specific_job_ids) {
			filter_err_msg = xstrdup_printf("Job account %s != filter account %s",
							job_ptr->account,
							kill_msg->account);
		}
		matches_filter = false;
		goto fini;
	}

	if (kill_msg->job_name && xstrcmp(job_ptr->name, kill_msg->job_name)) {
		if (signal_args->filter_specific_job_ids) {
			filter_err_msg = xstrdup_printf("Job name %s != filter name %s",
							job_ptr->name,
							kill_msg->job_name);
		}
		matches_filter = false;
		goto fini;
	}

	/*
	 * If the job is submitted to multiple partitions, then its partition
	 * string is all the partitions. We need to find if the requested
	 * partition matches any of the partitions that the job was submitted
	 * to if the job is still pending. If the job is running, only check
	 * the partition the job is running in.
	 */
	if (kill_msg->partition) {
		if (IS_JOB_PENDING(job_ptr) && job_ptr->part_ptr_list) {
			if (!list_find_first(job_ptr->part_ptr_list,
					     _match_part_name,
					     kill_msg->partition))
				matches_filter = false;
		} else if (job_ptr->part_ptr) {
			if (xstrcmp(job_ptr->part_ptr->name,
				    kill_msg->partition))
				matches_filter = false;
		} else {
			if (xstrcmp(job_ptr->partition, kill_msg->partition))
				matches_filter = false;
		}

		if (!matches_filter) {
			if (signal_args->filter_specific_job_ids) {
				filter_err_msg =
					xstrdup_printf("Job partition %s does not include filter partition %s",
						       job_ptr->partition,
						       kill_msg->partition);
			}
			goto fini;
		}
	}

	if (kill_msg->qos) {
		char *qos_name = "NULL";

		if (!job_ptr->qos_ptr)
			matches_filter = false;
		else if (xstrcmp(job_ptr->qos_ptr->name, kill_msg->qos)) {
			matches_filter = false;
			qos_name = job_ptr->qos_ptr->name;
		}

		if (!matches_filter) {
			if (signal_args->filter_specific_job_ids) {
				filter_err_msg = xstrdup_printf("Job qos %s != filter qos %s",
								qos_name,
								kill_msg->qos);
			}
			goto fini;
		}
	}

	if (kill_msg->reservation) {
		if (IS_JOB_RUNNING(job_ptr) || IS_JOB_SUSPENDED(job_ptr)) {
			slurmctld_resv_t *resv_ptr =
				find_resv_name(kill_msg->reservation);

			if (!(resv_ptr &&
			      (resv_ptr->resv_id == job_ptr->resv_id)))
				matches_filter = false;
		} else if (job_ptr->resv_list) {
			if (!list_find_first(job_ptr->resv_list,
					     _match_resv_name,
					     kill_msg->reservation))
				matches_filter = false;
		} else if (job_ptr->resv_ptr) {
			if (xstrcmp(job_ptr->resv_ptr->name,
				    kill_msg->reservation))
				matches_filter = false;
		} else {
			if (xstrcmp(job_ptr->resv_name, kill_msg->reservation))
				matches_filter = false;
		}

		if (!matches_filter) {
			if (signal_args->filter_specific_job_ids) {
				filter_err_msg =
					xstrdup_printf("Job reservation %s does not include filter reservation %s",
						       job_ptr->resv_name,
						       kill_msg->reservation);
			}
			goto fini;
		}
	}

	if ((kill_msg->state != JOB_END) &&
	    (job_base_state != kill_msg->state)) {
		if (signal_args->filter_specific_job_ids) {
			char *msg_state_str = job_state_string(kill_msg->state);
			char *job_state_str = job_state_string(job_base_state);

			filter_err_msg = xstrdup_printf("Job state %s != filter state %s",
							job_state_str,
							msg_state_str);
		}
		matches_filter = false;
		goto fini;
	}

	if (kill_msg->user_name && (job_ptr->user_id != kill_msg->user_id)) {
		if (signal_args->filter_specific_job_ids) {
			filter_err_msg = xstrdup_printf("Job user id %u != filter user id %u",
							job_ptr->user_id,
							kill_msg->user_id);
		}
		matches_filter = false;
		goto fini;
	}

	if (kill_msg->nodelist) {
		hostset_t *hs;
		bool intersects;

		if (!job_ptr->nodes) {
			if (signal_args->filter_specific_job_ids) {
				filter_err_msg =
					xstrdup_printf("Job does not have nodes but filter has nodes %s",
						       kill_msg->nodelist);
			}
			matches_filter = false;
			goto fini;
		}

		hs = hostset_create(job_ptr->nodes);
		intersects = hostset_intersects(hs, kill_msg->nodelist);
		hostset_destroy(hs);
		if (!intersects) {
			if (signal_args->filter_specific_job_ids) {
				filter_err_msg =
					xstrdup_printf("Job nodes %s does not intersect with filter nodes %s",
						       job_ptr->nodes,
						       kill_msg->nodelist);
			}
			matches_filter = false;
			goto fini;
		}
	}

	if (kill_msg->wckey) {
		char *job_key = job_ptr->wckey;

		/*
		 * A wckey that begins with '*' indicates that the wckey
		 * was applied by default.  When the --wckey option does
		 * not begin with a '*', act on all wckeys with the same
		 * name, default or not.
		 */
		if ((kill_msg->wckey[0] != '*') && job_key &&
		    (job_key[0] == '*'))
			job_key++;

		if (xstrcmp(job_key, kill_msg->wckey)) {
			if (signal_args->filter_specific_job_ids) {
				filter_err_msg =
					xstrdup_printf("Job wckey %s != filter wckey %s",
						       job_ptr->wckey,
						       kill_msg->wckey);
			}
			matches_filter = false;
			goto fini;
		}
	}

	if (job_ptr->het_job_offset) {
		if (signal_args->het_leader &&
		    signal_args->het_leader->job_id &&
		    (job_ptr->het_job_id ==
		     signal_args->het_leader->het_job_id)) {
			/*
			 * Filter out HetJob non-leader component as its leader
			 * should have already been evaluated and hasn't been
			 * filtered out.
			 *
			 * The leader RPC signal handler will affect all the
			 * components, so this avoids extra unneeded RPCs, races
			 * and issues interpreting multiple error codes.
			 *
			 * This can be done assuming the walking of the loaded
			 * jobs is guaranteed to evaluate in an order such that
			 * HetJob leaders are evaluated before their matching
			 * non-leaders and the whole HetJob is evaluated
			 * contiguously. The slurmctld job_list is ordered by
			 * job creation time (always leader first) and HetJobs
			 * are created in a row.
			 */
			return false;
		}

		/*
		 * Het job components may not be signalled individually if they
		 * are pending or if whole_hetjob is set.
		 */
		if (IS_JOB_PENDING(job_ptr)) {
			error_code = ESLURM_NOT_WHOLE_HET_JOB;
			if (signal_args->filter_specific_job_ids)
				filter_err_msg = xstrdup("Het job component cannot be signalled while pending");
			goto fini;
		}
		if (_get_whole_hetjob()) {
			error_code = ESLURM_NOT_WHOLE_HET_JOB;
			if (signal_args->filter_specific_job_ids)
				filter_err_msg = xstrdup("slurm.conf whole_hetjob is set");
			goto fini;
		}
	}

fini:
	if (!matches_filter)
		_handle_signal_filter_mismatch(job_ptr, signal_args,
					       error_code, filter_err_msg);
	else {
		/* Track most recent het leader. */
		if (job_ptr->het_job_id && !job_ptr->het_job_offset)
			signal_args->het_leader = job_ptr;
	}

	xfree(filter_err_msg);

	return matches_filter;
}

/*
 * Figure out if the job (job_ptr) matches the specified filters:
 * - filter_id describes a job or set of jobs if it is an array expression.
 * - signal_args->kill_msg has filters requested by the client.
 *
 * If the job does not match the specified filters in signal_args, then
 * _signal_job_matches_filter() adds a response message for the job and we
 * return.
 *
 * If the job matches the specified filters, but the user is not authorized to
 * signal the job, add a response message and return.
 *
 * If the job matches the specified filters and the user is authorized to signal
 * the job, place the job into the appropriate list of jobs which will later be
 * signaled. The lists are in signal_args.
 * - pending_array_task_list: A meta record with pending array tasks that are
 *   requested to be signaled, or a single pending array task that has not yet
 *   been split from the meta record.
 * - array_leader_list - A meta record for an array where that entire array has
 *   been requested to be signaled.
 * - other_job_list - All other jobs to be signaled.
 */
static void _apply_signal_jobs_filter(job_record_t *job_ptr,
				      slurm_selected_step_t *filter_id,
				      signal_jobs_args_t *signal_args)
{
	bool is_pending_meta_record_with_tasks;
	uid_t auth_uid = signal_args->auth_uid;

	if (!_signal_job_matches_filter(job_ptr, signal_args))
		return;

	/* Verify that the user can kill the requested job */
	if ((job_ptr->user_id != auth_uid) &&
	    !validate_operator_locked(auth_uid) &&
	    !assoc_mgr_is_user_acct_coord(acct_db_conn, auth_uid,
					  job_ptr->account, true)) {
		slurm_selected_step_t *use_id;
		slurm_selected_step_t id;

		if (filter_id)
			use_id = filter_id;
		else {
			_slurm_selected_step_init(job_ptr, &id);
			use_id = &id;
		}
		_add_signal_job_resp(signal_args, NULL, ESLURM_ACCESS_DENIED,
				     NULL, use_id, job_ptr->job_id);
		return;
	}

	is_pending_meta_record_with_tasks = (IS_JOB_PENDING(job_ptr) &&
					     job_ptr->array_recs &&
					     job_ptr->array_recs->task_cnt);

	if (filter_id && !filter_id->array_bitmap &&
	    (filter_id->array_task_id != NO_VAL) &&
	    is_pending_meta_record_with_tasks) {
		/*
		 * A pending job array task that has not been split from the
		 * meta array record.
		 */
		array_task_filter_t *atf = xmalloc(sizeof(*atf));

		/* Copy filter_id, but use a new array_bitmap */
		atf->filter_id = xmalloc(sizeof(*atf->filter_id));
		memcpy(atf->filter_id, filter_id, sizeof(*filter_id));

		atf->filter_id->array_bitmap = bit_alloc(max_array_size);
		bit_set(atf->filter_id->array_bitmap, filter_id->array_task_id);
		atf->free_array_bitmap = true;
		atf->job_ptr = job_ptr;

		list_append(signal_args->pending_array_task_list, atf);
	} else if (filter_id && filter_id->array_bitmap &&
		   is_pending_meta_record_with_tasks) {
		/* A job array expression with pending array tasks */
		array_task_filter_t *atf = xmalloc(sizeof(*atf));

		atf->filter_id = xmalloc(sizeof(*atf->filter_id));
		memcpy(atf->filter_id, filter_id, sizeof(*filter_id));
		atf->job_ptr = job_ptr;

		list_append(signal_args->pending_array_task_list, atf);
	} else if (job_ptr->array_recs)
		list_append(signal_args->array_leader_list, job_ptr);
	else
		list_append(signal_args->other_job_list, job_ptr);
}

static int _foreach_filter_job_list(void *x, void *arg)
{
	job_record_t *job_ptr = x;
	signal_jobs_args_t *signal_args = arg;

	_apply_signal_jobs_filter(job_ptr, NULL, signal_args);

	return SLURM_SUCCESS;
}

static int _foreach_signal_job(void *x, void *arg)
{
	int error_code;
	job_record_t *job_ptr = x;
	signal_jobs_args_t *signal_args = arg;
	kill_jobs_msg_t *kill_msg = signal_args->kill_msg;

	if (job_ptr->het_job_list)
		error_code = het_job_signal(job_ptr, kill_msg->signal,
					    kill_msg->flags,
					    signal_args->auth_uid, 0);
	else
		error_code = job_signal(job_ptr, kill_msg->signal,
					kill_msg->flags,
					signal_args->auth_uid, 0);

	if (error_code || (kill_msg->flags & KILL_JOBS_VERBOSE)) {
		slurm_selected_step_t id;

		_slurm_selected_step_init(job_ptr, &id);
		_add_signal_job_resp(signal_args, NULL, error_code, NULL, &id,
				     job_ptr->job_id);
	}

	return SLURM_SUCCESS;
}

static int _foreach_signal_job_array_tasks(void *x, void *arg)
{
	array_task_filter_t *atf = x;
	signal_jobs_args_t *signal_args = arg;
	kill_jobs_msg_t *kill_msg = signal_args->kill_msg;
	int32_t i_last;
	int error_code = SLURM_SUCCESS;

	/*
	 * Signal the pending array tasks in the array job. The tasks that
	 * have already been split out are not part of the meta job's array
	 * bitmap and are handled elsewhere.
	 *
	 * _signal_pending_job_array_tasks() removes the pending tasks from
	 * array_bitmap. For the response to the client, we want to the pending
	 * tasks that were signalled. To get that, operate on a copy of
	 * array_bitmap which will be returned with the running tasks. Then
	 * remove the running tasks from the original bitmap (bit_and_not).
	 */
	i_last = bit_fls(atf->filter_id->array_bitmap);
	if (i_last >= 0) {
		bitstr_t *array_bitmap_running =
			bit_copy(atf->filter_id->array_bitmap);

		_signal_pending_job_array_tasks(atf->job_ptr,
						&array_bitmap_running,
						kill_msg->signal,
						signal_args->auth_uid,
						i_last, signal_args->now,
						&error_code);
		bit_and_not(atf->filter_id->array_bitmap, array_bitmap_running);
		FREE_NULL_BITMAP(array_bitmap_running);
	}

	if (error_code || (kill_msg->flags & KILL_JOBS_VERBOSE))
		_add_signal_job_resp(signal_args, NULL, error_code, NULL,
				     atf->filter_id, atf->job_ptr->job_id);

	return 0;
}

static foreach_job_by_id_control_t _job_not_found(const slurm_selected_step_t
						  	*id,
						  void *arg)
{
	signal_jobs_args_t *signal_args = arg;
	uint32_t job_id = id->step_id.job_id;

	if (fed_mgr_fed_rec && !fed_mgr_is_origin_job_id(job_id)) {
		int error_code = ESLURM_JOB_SIGNAL_FAILED;
		char *err_msg = NULL;

		err_msg = xstrdup_printf("Job id not in federation: %s",
					 slurm_strerror(error_code));
		_add_signal_job_resp(signal_args, NULL, error_code,
				     err_msg, (slurm_selected_step_t *) id,
				     NO_VAL);
	} else {
		_add_signal_job_resp(signal_args, NULL, ESLURM_INVALID_JOB_ID,
				     NULL, (slurm_selected_step_t *) id,
				     NO_VAL);
	}
	return FOR_EACH_JOB_BY_ID_EACH_CONT;
}

static foreach_job_by_id_control_t _filter_job(job_record_t *job_ptr,
					       const slurm_selected_step_t *id,
					       void *arg)
{
	_apply_signal_jobs_filter(job_ptr, (slurm_selected_step_t *) id, arg);

	return FOR_EACH_JOB_BY_ID_EACH_CONT;
}

static void _filter_jobs_ids(slurm_selected_step_t **job_ids, uint32_t cnt,
			     signal_jobs_args_t *signal_args)
{
	signal_args->filter_specific_job_ids = true;
	for (int i = 0; i < cnt; i++) {
		slurm_selected_step_t *filter = job_ids[i];
		uint32_t job_id = filter->step_id.job_id;
		int rc;

		if (fed_mgr_cluster_rec && !fed_mgr_is_job_id_in_fed(job_id)) {
			rc = ESLURM_JOB_NOT_FEDERATED;
			_add_signal_job_resp(signal_args, NULL, rc, NULL,
					     filter, NO_VAL);
			continue;
		}

		(void) foreach_job_by_id(filter, _filter_job, _job_not_found,
					 signal_args);
	}
}

static int _foreach_xfer_responses(void *x, void *arg)
{
	kill_jobs_resp_job_t *job_resp = x;
	xfer_signal_jobs_responses_args_t *args = arg;

	memcpy(&args->resp_msg->job_responses[args->curr_count], job_resp,
	       sizeof(*job_resp));

	/*
	 * Pointers in job_resp were transferred and will be free'd with
	 * job_responses
	 */
	xfree(job_resp);
	args->curr_count++;

	return SLURM_SUCCESS;
}

static void _build_kill_jobs_resp_msg(signal_jobs_args_t *signal_args,
				      kill_jobs_resp_msg_t **resp_msg_p)
{
	kill_jobs_resp_msg_t *resp_msg = xmalloc(sizeof(*resp_msg));
	xfer_signal_jobs_responses_args_t foreach_args = {
		.resp_msg = resp_msg,
	};

	*resp_msg_p = resp_msg;
	resp_msg->jobs_cnt = list_count(signal_args->responses);

	if (!resp_msg->jobs_cnt)
		return;

	resp_msg->job_responses = xcalloc(resp_msg->jobs_cnt,
					  sizeof(*resp_msg->job_responses));
	list_for_each(signal_args->responses, _foreach_xfer_responses,
		      &foreach_args);
}

/*
 * Signal a job based upon job pointer.
 * Authentication and authorization checks must be performed before calling.
 */
extern int job_signal(job_record_t *job_ptr, uint16_t signal,
		      uint16_t flags, uid_t uid, bool preempt)
{
	uint16_t job_term_state;
	time_t now = time(NULL);

	log_flag(TRACE_JOBS, "%s: enter %pJ", __func__, job_ptr);

	if (IS_JOB_STAGE_OUT(job_ptr) && (flags & KILL_HURRY)) {
		job_ptr->bit_flags |= JOB_KILL_HURRY;
		return bb_g_job_cancel(job_ptr);
	}

	if (IS_JOB_FINISHED(job_ptr))
		return ESLURM_ALREADY_DONE;

	/*
	 * If is origin job then cancel siblings -- if they exist.
	 * origin job = because it knows where the siblings are
	 * If the job is running locally then just do the normal signaling
	 */
	if (!(flags & KILL_NO_SIBS) && !IS_JOB_RUNNING(job_ptr) &&
	    job_ptr->fed_details && fed_mgr_fed_rec) {
		uint32_t origin_id = fed_mgr_get_cluster_id(job_ptr->job_id);
		slurmdb_cluster_rec_t *origin =
			fed_mgr_get_cluster_by_id(origin_id);

		if (origin && (origin == fed_mgr_cluster_rec) &&
		    fed_mgr_job_started_on_sib(job_ptr)) {
			/*
			 * If the job is running on a remote cluster then wait
			 * for the job to report back that it's completed,
			 * otherwise just signal the pending siblings and itself
			 * (by not returning).
			 */
			return fed_mgr_job_cancel(job_ptr, signal, flags, uid,
						  false);
		} else if (origin && (origin == fed_mgr_cluster_rec)) {
			/* cancel origin job and revoke sibling jobs */
			fed_mgr_job_revoke_sibs(job_ptr);
			fed_mgr_remove_remote_dependencies(job_ptr);
		} else if (!origin ||
			   !origin->fed.send ||
			   (((persist_conn_t *) origin->fed.send)->fd == -1)) {
			/*
			 * The origin is down just signal all of the viable
			 * sibling jobs
			 */
			fed_mgr_job_cancel(job_ptr, signal, flags, uid, true);
		}
	}

	last_job_update = now;

	/*
	 * Handle jobs submitted through scrontab.
	 */
	if (job_ptr->bit_flags & CRON_JOB) {
		cron_entry_t *entry =
			(cron_entry_t *) job_ptr->details->crontab_entry;
		/*
		 * The KILL_CRON flag being set here is indicating that the
		 * user has specifically requested killing scrontab jobs. To
		 * avoid interfering with other possible ways of killing jobs,
		 * the KILL_CRON flag being set must mean that killing cron
		 * jobs is permitted.
		 */
		if (xstrcasestr(slurm_conf.scron_params, "explicit_scancel") &&
		    !(flags & KILL_CRON))
			return ESLURM_CANNOT_CANCEL_CRON_JOB;
		job_ptr->bit_flags &= ~CRON_JOB;
		error("cancelling cron job, lines %u %u",
		      entry->line_start, entry->line_end);
		crontab_add_disabled_lines(job_ptr->user_id, entry->line_start,
					   entry->line_end);
	}

	/* save user ID of the one who requested the job be cancelled */
	if (signal == SIGKILL)
		job_ptr->requid = uid;
	if (IS_JOB_PENDING(job_ptr) && IS_JOB_COMPLETING(job_ptr) &&
	    (signal == SIGKILL)) {
		/* Prevent job requeue, otherwise preserve state */
		job_state_set(job_ptr, (JOB_CANCELLED | JOB_COMPLETING));

		/* build_cg_bitmap() not needed, job already completing */
		verbose("%s: %u of requeuing %pJ successful",
			__func__, signal, job_ptr);
		return SLURM_SUCCESS;
	}

	if (flags & KILL_HURRY)
		job_ptr->bit_flags |= JOB_KILL_HURRY;

	if (IS_JOB_CONFIGURING(job_ptr) && (signal == SIGKILL)) {
		last_job_update         = now;
		job_ptr->end_time       = now;
		job_state_set(job_ptr, (JOB_CANCELLED | JOB_COMPLETING));
		if (flags & KILL_FED_REQUEUE)
			job_state_set_flag(job_ptr, JOB_REQUEUE);
		slurmscriptd_flush_job(job_ptr->job_id);
		track_script_flush_job(job_ptr->job_id);
		build_cg_bitmap(job_ptr);
		job_completion_logger(job_ptr, false);
		deallocate_nodes(job_ptr, false, false, false);
		if (flags & KILL_FED_REQUEUE)
			job_state_unset_flag(job_ptr, JOB_REQUEUE);

		verbose("%s: %u of configuring %pJ successful",
			__func__, signal, job_ptr);
		return SLURM_SUCCESS;
	}

	if (IS_JOB_PENDING(job_ptr) && (signal == SIGKILL)) {
		job_state_set(job_ptr, JOB_CANCELLED);
		if (flags & KILL_FED_REQUEUE)
			job_state_set_flag(job_ptr, JOB_REQUEUE);
		job_ptr->start_time	= now;
		job_ptr->end_time	= now;
		srun_allocate_abort(job_ptr);
		slurmscriptd_flush_job(job_ptr->job_id);
		track_script_flush_job(job_ptr->job_id);
		job_completion_logger(job_ptr, false);
		if (flags & KILL_FED_REQUEUE)
			job_state_unset_flag(job_ptr, JOB_REQUEUE);

		/*
		 * Send back a response to the origin cluster, in other cases
		 * where the job is running the job will send back a response
		 * after the job is is completed. This can happen when the
		 * pending origin job is put into a hold state and the siblings
		 * are removed or when the job is canceled from the origin.
		 */
		fed_mgr_job_complete(job_ptr, 0, now);
		verbose("%s: %u of pending %pJ successful",
			__func__, signal, job_ptr);
		return SLURM_SUCCESS;
	}

	if (preempt)
		job_term_state = JOB_PREEMPTED;
	else
		job_term_state = JOB_CANCELLED;
	if (IS_JOB_SUSPENDED(job_ptr) && (signal == SIGKILL)) {
		last_job_update         = now;
		job_ptr->end_time       = job_ptr->suspend_time;
		job_ptr->tot_sus_time  += difftime(now, job_ptr->suspend_time);
		job_state_set(job_ptr, (job_term_state | JOB_COMPLETING));
		if (flags & KILL_FED_REQUEUE)
			job_state_set_flag(job_ptr, JOB_REQUEUE);
		build_cg_bitmap(job_ptr);
		jobacct_storage_g_job_suspend(acct_db_conn, job_ptr);
		job_completion_logger(job_ptr, false);
		if (flags & KILL_FED_REQUEUE)
			job_state_unset_flag(job_ptr, JOB_REQUEUE);
		deallocate_nodes(job_ptr, false, true, preempt);
		verbose("%s: %u of suspended %pJ successful",
			__func__, signal, job_ptr);
		return SLURM_SUCCESS;
	}

	if (IS_JOB_RUNNING(job_ptr)) {

		if ((signal == SIGSTOP) || (signal == SIGCONT)) {
			if (IS_JOB_SIGNALING(job_ptr)) {
				verbose("%s: %u not send to %pJ 0x%x",
					__func__, signal, job_ptr,
					job_ptr->job_state);
				return ESLURM_TRANSITION_STATE_NO_UPDATE;
			}
			job_state_set_flag(job_ptr, JOB_SIGNALING);
		}

		if ((signal == SIGKILL)
		    && !(flags & KILL_STEPS_ONLY)
		    && !(flags & KILL_JOB_BATCH)) {
			/* No need to signal steps, deallocate kills them
			 */
			job_ptr->time_last_active	= now;
			job_ptr->end_time		= now;
			last_job_update			= now;
			job_state_set(job_ptr, (job_term_state |
						JOB_COMPLETING));
			if (flags & KILL_FED_REQUEUE)
				job_state_set_flag(job_ptr, JOB_REQUEUE);
			build_cg_bitmap(job_ptr);
			job_completion_logger(job_ptr, false);
			deallocate_nodes(job_ptr, false, false, preempt);
			if (flags & KILL_FED_REQUEUE)
				job_state_unset_flag(job_ptr, JOB_REQUEUE);
		} else if (job_ptr->batch_flag && (flags & KILL_JOB_BATCH)) {
			_signal_batch_job(job_ptr, signal, flags);
		} else if ((flags & KILL_JOB_BATCH) && !job_ptr->batch_flag) {
			if ((signal == SIGSTOP) || (signal == SIGCONT))
				job_state_unset_flag(job_ptr, JOB_SIGNALING);
			return ESLURM_JOB_SCRIPT_MISSING;
		} else {
			_signal_job(job_ptr, signal, flags);
		}
		verbose("%s: %u of running %pJ successful 0x%x",
			__func__, signal, job_ptr, job_ptr->job_state);
		return SLURM_SUCCESS;
	}

	verbose("%s: %pJ can't be sent signal %u from state=%s",
		__func__, job_ptr, signal,
		job_state_string(job_ptr->job_state));

	log_flag(TRACE_JOBS, "%s: return %pJ", __func__, job_ptr);

	return ESLURM_TRANSITION_STATE_NO_UPDATE;
}

/*
 * job_signal_id - signal the specified job
 * IN job_id - id of the job to be signaled
 * IN signal - signal to send, SIGKILL == cancel the job
 * IN flags  - see KILL_JOB_* flags in slurm.h
 * IN uid - uid of requesting user
 * IN preempt - true if job being preempted
 * RET 0 on success, otherwise ESLURM error code
 */
extern int job_signal_id(uint32_t job_id, uint16_t signal, uint16_t flags,
			 uid_t uid, bool preempt)
{
	job_record_t *job_ptr;

	job_ptr = find_job_record(job_id);
	if (job_ptr == NULL) {
		info("%s: invalid JobId=%u", __func__, job_id);
		return ESLURM_INVALID_JOB_ID;
	}

	if ((job_ptr->user_id != uid) && !validate_operator(uid) &&
	    !assoc_mgr_is_user_acct_coord(acct_db_conn, uid,
					  job_ptr->account, false)) {
		error("Security violation, JOB_CANCEL RPC for %pJ from uid %u",
		      job_ptr, uid);
		return ESLURM_ACCESS_DENIED;
	}

	return job_signal(job_ptr, signal, flags, uid, preempt);
}

/* Signal all components of a hetjob */
extern int het_job_signal(job_record_t *het_job_leader, uint16_t signal,
			  uint16_t flags, uid_t uid, bool preempt)
{
	list_itr_t *iter;
	int rc = SLURM_SUCCESS, rc1;
	job_record_t *het_job;

	if (!het_job_leader->het_job_id)
		return ESLURM_NOT_HET_JOB;
	else if (!het_job_leader->het_job_list)
		return ESLURM_NOT_HET_JOB_LEADER;

	iter = list_iterator_create(het_job_leader->het_job_list);
	while ((het_job = list_next(iter))) {
		if (het_job_leader->het_job_id != het_job->het_job_id) {
			error("%s: Bad het_job_list for %pJ",
			      __func__, het_job_leader);
			continue;
		}
		rc1 = job_signal(het_job, signal, flags, uid, preempt);
		if (rc1 != SLURM_SUCCESS)
			rc = rc1;
	}
	list_iterator_destroy(iter);

	return rc;
}

static bool _get_whole_hetjob(void)
{
	static time_t sched_update = 0;
	static bool whole_hetjob = false;

	if (sched_update != slurm_conf.last_update) {
		sched_update = slurm_conf.last_update;
		if (xstrcasestr(slurm_conf.sched_params, "whole_hetjob") ||
		    xstrcasestr(slurm_conf.sched_params, "whole_pack"))
			whole_hetjob = true;
		else
			whole_hetjob = false;
	}

	return whole_hetjob;
}

static job_record_t *_find_meta_job_record(uint32_t job_id)
{
	job_record_t *job_ptr;

	job_ptr = find_job_record(job_id);
	if (job_ptr == NULL) {
		job_ptr = job_array_hash_j[JOB_HASH_INX(job_id)];
		while (job_ptr) {
			if (job_ptr->array_job_id == job_id)
				break;
			job_ptr = job_ptr->job_array_next_j;
		}
	}
	if ((job_ptr == NULL) ||
	    ((job_ptr->array_task_id == NO_VAL) &&
	     (job_ptr->array_recs == NULL)))
		return NULL;

	return job_ptr;
}

static void _signal_pending_job_array_tasks(job_record_t *job_ptr,
					    bitstr_t **array_bitmap,
					    uint16_t signal,
					    uid_t uid,
					    int32_t i_last,
					    time_t now,
					    int *rc)
{
	int len;

	xassert(job_ptr);

	if (!(IS_JOB_PENDING(job_ptr) && job_ptr->array_recs &&
	      job_ptr->array_recs->task_id_bitmap))
		return; /* No tasks to signal */

	/* Ensure bitmap sizes match for AND operations */
	len = bit_size(job_ptr->array_recs->task_id_bitmap);
	i_last++;
	if (i_last < len) {
		bit_realloc(*array_bitmap, len);
	} else {
		bit_realloc(*array_bitmap, i_last);
		bit_realloc(job_ptr->array_recs->task_id_bitmap, i_last);
	}
	if (signal == SIGKILL) {
		uint32_t orig_task_cnt, new_task_count;
		/* task_id_bitmap changes, so we need a copy of it */
		bitstr_t *task_id_bitmap_orig =
			bit_copy(job_ptr->array_recs->task_id_bitmap);

		bit_and_not(job_ptr->array_recs->task_id_bitmap,
			    *array_bitmap);
		xfree(job_ptr->array_recs->task_id_str);
		orig_task_cnt = job_ptr->array_recs->task_cnt;
		new_task_count = bit_set_count(job_ptr->array_recs->
					       task_id_bitmap);
		if (!new_task_count) {
			last_job_update		= now;
			job_state_set(job_ptr, JOB_CANCELLED);
			job_ptr->start_time	= now;
			job_ptr->end_time	= now;
			job_ptr->requid		= uid;
			srun_allocate_abort(job_ptr);
			job_completion_logger(job_ptr, false);
			/*
			 * Master job record, even wihtout tasks,
			 * counts as one job record
			 */
			job_count -= (orig_task_cnt - 1);
		} else {
			_job_array_comp(job_ptr, false, false);
			job_count -= (orig_task_cnt - new_task_count);
			/*
			 * Since we are altering the job array's
			 * task_cnt we must go alter this count in the
			 * acct_policy code as if they are finishing
			 * (accrue_cnt/job_submit etc...).
			 */
			if (job_ptr->array_recs->task_cnt >
			    new_task_count) {
				uint32_t tmp_state = job_ptr->job_state;
				job_state_set(job_ptr, JOB_CANCELLED);

				job_ptr->array_recs->task_cnt -=
					new_task_count;
				acct_policy_remove_job_submit(job_ptr,
							      false);
				job_ptr->bit_flags &= ~JOB_ACCRUE_OVER;
				job_state_set(job_ptr, tmp_state);
			}
		}

		/*
		 * Set the task_cnt here since
		 * job_completion_logger needs the total
		 * pending count to handle the acct_policy
		 * limit for submitted jobs correctly.
		 */
		job_ptr->array_recs->task_cnt = new_task_count;
		bit_and_not(*array_bitmap, task_id_bitmap_orig);
		FREE_NULL_BITMAP(task_id_bitmap_orig);
	} else {
		bit_and_not(*array_bitmap,
			    job_ptr->array_recs->task_id_bitmap);
		*rc = ESLURM_TRANSITION_STATE_NO_UPDATE;
	}
}

/*
 * job_str_signal - signal the specified job
 * IN job_id_str - id of the job to be signaled, valid formats include "#"
 *	"#_#" and "#_[expr]"
 * IN signal - signal to send, SIGKILL == cancel the job
 * IN flags  - see KILL_JOB_* flags in slurm.h
 * IN uid - uid of requesting user
 * IN preempt - true if job being preempted
 * RET 0 on success, otherwise ESLURM error code
 */
extern int job_str_signal(char *job_id_str, uint16_t signal, uint16_t flags,
			  uid_t uid, bool preempt)
{
	job_record_t *job_ptr;
	uint32_t job_id;
	time_t now = time(NULL);
	char *end_ptr = NULL;
	long int long_id;
	bitstr_t *array_bitmap = NULL;
	int32_t i, i_first, i_last;
	int rc = SLURM_SUCCESS, rc2;

	if (max_array_size == NO_VAL) {
		max_array_size = slurm_conf.max_array_sz;
	}

	long_id = strtol(job_id_str, &end_ptr, 10);
	if ((long_id <= 0) || (long_id == LONG_MAX) ||
	    ((end_ptr[0] != '\0') && (end_ptr[0] != '_') &&
	     (end_ptr[0] != '+'))) {
		info("%s(1): invalid JobId=%s", __func__, job_id_str);
		return ESLURM_INVALID_JOB_ID;
	}
	if ((end_ptr[0] == '_') && (end_ptr[1] == '*'))
		end_ptr += 2;	/* Defaults to full job array */

	if (end_ptr[0] == '+') {	/* Signal hetjob element */
		job_id = (uint32_t) long_id;
		long_id = strtol(end_ptr + 1, &end_ptr, 10);
		if ((long_id < 0) || (long_id == LONG_MAX) ||
		    (end_ptr[0] != '\0')) {
			info("%s(2): invalid JobId=%s", __func__, job_id_str);
			return ESLURM_INVALID_JOB_ID;
		}
		job_ptr = find_het_job_record(job_id, (uint32_t) long_id);
		if (!job_ptr)
			return ESLURM_INVALID_JOB_ID;
		if ((job_ptr->user_id != uid) && !validate_operator(uid) &&
		    !assoc_mgr_is_user_acct_coord(acct_db_conn, uid,
						  job_ptr->account, false)) {
			error("Security violation, REQUEST_KILL_JOB RPC for %pJ from uid %u",
			      job_ptr, uid);
			return ESLURM_ACCESS_DENIED;
		}

		if (!job_ptr->het_job_id)
			return ESLURM_NOT_HET_JOB;

		if (!job_ptr->het_job_offset)
			/*
			 * HetJob leader. Attempt to signal all components no
			 * matter what. If we cared about state or whole_hetjob
			 * for the leader, we would be being inconsistent with
			 * direct format '#' below. But even if we made an
			 * exception here for leader R and no whole_hetjob,
			 * job_complete() would end all the components anyways.
			 */
			return het_job_signal(job_ptr, signal, flags, uid,
					      preempt);

		/* HetJob non-leader component. */
		if (_get_whole_hetjob()) {
			/* Attempt to signal all components no matter state. */
			job_record_t *het_leader = NULL;
			if (!(het_leader = find_het_job_record(job_id, 0))) {
				/* Leader not found. Attempt individual. */
				error("%s: can't find HetJob leader for HetJob component %pJ",
				      __func__, job_ptr);
				return job_signal(job_ptr, signal,
						  flags, uid, preempt);
			} else {
				/* Got the leader, signal all. */
				return het_job_signal(het_leader,
						      signal, flags,
						      uid, preempt);
			}
		}

		if (IS_JOB_PENDING(job_ptr))
			return ESLURM_NOT_WHOLE_HET_JOB;
		else
			return job_signal(job_ptr, signal, flags, uid, preempt);
	}

	last_job_update = now;
	job_id = (uint32_t) long_id;
	if (end_ptr[0] == '\0') {	/* Single job (or full job array) */
		int jobs_done = 0, jobs_signaled = 0;
		job_record_t *job_ptr_done = NULL;
		job_ptr = find_job_record(job_id);
		if (job_ptr && (job_ptr->user_id != uid) &&
		    !validate_operator(uid) &&
		    !assoc_mgr_is_user_acct_coord(acct_db_conn, uid,
						  job_ptr->account, false)) {
			error("Security violation, REQUEST_KILL_JOB RPC for %pJ from uid %u",
			      job_ptr, uid);
			return ESLURM_ACCESS_DENIED;
		}
		if (job_ptr && job_ptr->het_job_list) {   /* Hetjob leader */
			return het_job_signal(job_ptr, signal, flags, uid,
					      preempt);
		}
		if (job_ptr && job_ptr->het_job_id && _get_whole_hetjob()) {
			job_record_t *het_job_leader;
			het_job_leader = find_job_record(job_ptr->het_job_id);
			if (het_job_leader && het_job_leader->het_job_list) {
				return het_job_signal(het_job_leader, signal,
						      flags, uid, preempt);
			}
			error("%s: Hetjob leader %pJ not found",
			      __func__, job_ptr);
		}
		if (job_ptr && job_ptr->het_job_id && IS_JOB_PENDING(job_ptr))
			return ESLURM_NOT_WHOLE_HET_JOB;/* Hetjob child */

		if (job_ptr &&
		    (((job_ptr->array_task_id == NO_VAL) &&
		      (job_ptr->array_recs == NULL)) ||
		     ((job_ptr->array_task_id != NO_VAL) &&
		      ((job_ptr->array_job_id != job_id) ||
		       (flags & KILL_ARRAY_TASK))))) {
			/*
			 * This is a regular job or a single task of a job
			 * array. KILL_ARRAY_TASK indicates that the meta job
			 * should be treated as a single task.
			 */
			return job_signal_id(job_id, signal, flags, uid, preempt);
		}

		/*
		 * This will kill the meta record that holds all
		 * pending jobs.  We want to kill this first so we
		 * don't start jobs just to kill them as we are
		 * killing other elements of the array.
		 */
		if (job_ptr && job_ptr->array_recs) {
			/* This is a job array */
			job_ptr_done = job_ptr;
			rc = job_signal(job_ptr, signal, flags, uid, preempt);
			if (rc == ESLURM_ACCESS_DENIED)
				return rc;
			jobs_signaled++;
			if (rc == ESLURM_ALREADY_DONE) {
				jobs_done++;
				rc = SLURM_SUCCESS;
			}
		}

		/* Signal all tasks of this job array */
		job_ptr = job_array_hash_j[JOB_HASH_INX(job_id)];
		if (!job_ptr && !job_ptr_done) {
			info("%s(3): invalid JobId=%u", __func__, job_id);
			return ESLURM_INVALID_JOB_ID;
		}
		while (job_ptr) {
			if (job_ptr->array_job_id == job_id)
				break;
			job_ptr = job_ptr->job_array_next_j;
		}
		while (job_ptr) {
			if ((job_ptr->array_job_id == job_id) &&
			    (job_ptr != job_ptr_done)) {
				rc2 = job_signal(job_ptr, signal, flags, uid,
						 preempt);
				jobs_signaled++;
				if (rc2 == ESLURM_ALREADY_DONE) {
					jobs_done++;
				} else {
					rc = MAX(rc, rc2);
				}
			}
			job_ptr = job_ptr->job_array_next_j;
		}
		if ((rc == SLURM_SUCCESS) && (jobs_done == jobs_signaled))
			return ESLURM_ALREADY_DONE;
		return rc;

	}

	array_bitmap = slurm_array_str2bitmap(end_ptr + 1, max_array_size,
					      &i_last);
	if (!array_bitmap) {
		info("%s(4): invalid JobId=%s", __func__, job_id_str);
		rc = ESLURM_INVALID_JOB_ID;
		goto endit;
	}

	/* Find some job record and validate the user signaling the job */
	if (!(job_ptr = _find_meta_job_record(job_id))) {
		info("%s(5): invalid JobId=%s", __func__, job_id_str);
		rc = ESLURM_INVALID_JOB_ID;
		goto endit;
	}

	if ((job_ptr->user_id != uid) && !validate_operator(uid) &&
	    !assoc_mgr_is_user_acct_coord(acct_db_conn, uid,
					  job_ptr->account, false)) {
		error("%s: Security violation JOB_CANCEL RPC for %pJ from uid %u",
		      __func__, job_ptr, uid);
		rc = ESLURM_ACCESS_DENIED;
		goto endit;
	}

	_signal_pending_job_array_tasks(job_ptr, &array_bitmap, signal, uid,
					i_last, now, &rc);

	i_first = bit_ffs(array_bitmap);
	if (i_first >= 0)
		i_last = bit_fls(array_bitmap);
	else
		i_last = -2;
	for (i = i_first; i <= i_last; i++) {
		if (!bit_test(array_bitmap, i))
			continue;
		job_ptr = find_job_array_rec(job_id, i);
		if (job_ptr == NULL) {
			info("%s(6): invalid JobId=%u_%d",
			     __func__, job_id, i);
			rc = ESLURM_INVALID_JOB_ID;
			continue;
		}

		rc2 = job_signal(job_ptr, signal, flags, uid, preempt);
		rc = MAX(rc, rc2);
	}
endit:
	FREE_NULL_BITMAP(array_bitmap);

	return rc;
}

static void _free_selected_step_array(slurm_selected_step_t ***jobs_p,
				      uint32_t cnt)
{
	slurm_selected_step_t **jobs = *jobs_p;

	for (int i = 0; i < cnt; i++)
		slurm_destroy_selected_step(jobs[i]);
	xfree(jobs);
	*jobs_p = NULL;
}

static void _free_array_task_filter(void *x)
{
	array_task_filter_t *rec = x;

	if (!rec)
		return;

	/*
	 * Do not use slurm_destroy_selected_step() as that will
	 * unconditionally free the bitmap.
	 */
	if (rec->free_array_bitmap)
		FREE_NULL_BITMAP(rec->filter_id->array_bitmap);
	xfree(rec->filter_id);
	/* Do not free rec->job_ptr */
	xfree(rec);
}

static int _parse_jobs_array(char **jobs_array, uint32_t jobs_cnt,
			     slurm_selected_step_t ***jobs_p)
{
	slurm_selected_step_t **jobs = NULL;

	if (!jobs_array)
		return SLURM_SUCCESS;
	if (max_array_size == NO_VAL)
		max_array_size = slurm_conf.max_array_sz;

	jobs = xcalloc(jobs_cnt, sizeof(*jobs));
	for (int i = 0; i < jobs_cnt; i++) {
		int rc;

		jobs[i] = xmalloc(sizeof(*jobs[i]));
		rc = unfmt_job_id_string(jobs_array[i], jobs[i],
					 max_array_size);
		if (rc != SLURM_SUCCESS) {
			_free_selected_step_array(&jobs, i + 1);
			return rc;
		}
	}

	*jobs_p = jobs;
	return SLURM_SUCCESS;
}

static bool _verify_kill_jobs_msg(kill_jobs_msg_t *kill_msg)
{
	/* At least one job id or filter must be specified */
	if (!kill_msg->account && !kill_msg->job_name &&
	    !kill_msg->jobs_cnt && !kill_msg->partition && !kill_msg->qos &&
	    !kill_msg->reservation &&
	    ((kill_msg->state & JOB_STATE_BASE) == JOB_END) &&
	    !kill_msg->user_name && !kill_msg->wckey && !kill_msg->nodelist)
		return false;

	return true;
}

extern int job_mgr_signal_jobs(kill_jobs_msg_t *kill_msg, uid_t auth_uid,
                               kill_jobs_resp_msg_t **resp_msg_p)
{
	int rc = 0;
	signal_jobs_args_t signal_args = {
		.auth_uid = auth_uid,
		.kill_msg = kill_msg,
	};
	slurm_selected_step_t **jobs = NULL;
	assoc_mgr_lock_t assoc_lock = {
		.user = READ_LOCK,
	};

	xassert(verify_lock(CONF_LOCK, READ_LOCK));
	xassert(verify_lock(JOB_LOCK, WRITE_LOCK));
	xassert(verify_lock(NODE_LOCK, WRITE_LOCK));
	xassert(verify_lock(FED_LOCK, READ_LOCK));

	if (!_verify_kill_jobs_msg(kill_msg))
		return ESLURM_SIGNAL_JOBS_INVALID;

	/*
	 * Items in the signal_args.responses list are free'd in
	 * _foreach_xfer_responses
	 */
	signal_args.responses = list_create(NULL);
	signal_args.array_leader_list = list_create(NULL);
	signal_args.other_job_list = list_create(NULL);

	if (kill_msg->jobs_cnt) {
		rc = _parse_jobs_array(kill_msg->jobs_array,
				       kill_msg->jobs_cnt, &jobs);
		if (rc != SLURM_SUCCESS)
			return rc;
		signal_args.pending_array_task_list =
			list_create(_free_array_task_filter);
	}

	if (max_array_size == NO_VAL)
		max_array_size = slurm_conf.max_array_sz;

	/*
	 * Get a list of jobs to signal first, then signal the jobs outside of
	 * the job_list lock. Array job leaders need to be signalled before
	 * the tasks in their array. Try to signal each job; add each failure
	 * to signal_args.responses.
	 *
	 * We check if the auth_uid is able to signal the job on every possible
	 * job that matches the filter. Lock the assoc lock once here rather
	 * than every time we check.
	 */
	assoc_mgr_lock(&assoc_lock);
	if (jobs)
		_filter_jobs_ids(jobs, kill_msg->jobs_cnt, &signal_args);
	else
		list_for_each_ro(job_list, _foreach_filter_job_list,
				 &signal_args);
	/*
	 * het_leader is only used during filtering; explicitly NULL it out
	 * so it cannot accidentally be used later.
	 */
	signal_args.het_leader = NULL;
	assoc_mgr_unlock(&assoc_lock);

	list_for_each(signal_args.array_leader_list, _foreach_signal_job,
		      &signal_args);
	if (signal_args.pending_array_task_list) {
		signal_args.now = time(NULL);
		list_for_each(signal_args.pending_array_task_list,
			      _foreach_signal_job_array_tasks, &signal_args);
	}
	list_for_each(signal_args.other_job_list, _foreach_signal_job,
		      &signal_args);

	_build_kill_jobs_resp_msg(&signal_args, resp_msg_p);

	/* Cleanup */
	_free_selected_step_array(&jobs, kill_msg->jobs_cnt);
	FREE_NULL_LIST(signal_args.array_leader_list);
	FREE_NULL_LIST(signal_args.pending_array_task_list);
	FREE_NULL_LIST(signal_args.other_job_list);
	FREE_NULL_LIST(signal_args.responses);

	return SLURM_SUCCESS;
}

static void _signal_batch_job(job_record_t *job_ptr, uint16_t signal,
			      uint16_t flags)
{
	bitoff_t i;
	signal_tasks_msg_t *signal_tasks_msg = NULL;
	agent_arg_t *agent_args = NULL;

	xassert(job_ptr);
	xassert(job_ptr->batch_host);
	i = bit_ffs(job_ptr->node_bitmap);
	if (i < 0) {
		error("%s: %pJ lacks assigned nodes", __func__, job_ptr);
		return;
	}

	agent_args = xmalloc(sizeof(agent_arg_t));
	agent_args->msg_type	= REQUEST_SIGNAL_TASKS;
	agent_args->retry	= 1;
	agent_args->node_count  = 1;
#ifdef HAVE_FRONT_END
	if (job_ptr->front_end_ptr)
		agent_args->protocol_version =
			job_ptr->front_end_ptr->protocol_version;
#else
	node_record_t *node_ptr;
	if ((node_ptr = find_node_record(job_ptr->batch_host)))
		agent_args->protocol_version = node_ptr->protocol_version;
#endif
	agent_args->hostlist	= hostlist_create(job_ptr->batch_host);
	signal_tasks_msg = xmalloc(sizeof(signal_tasks_msg_t));
	signal_tasks_msg->step_id.job_id      = job_ptr->job_id;
	signal_tasks_msg->step_id.step_id = SLURM_BATCH_SCRIPT;
	signal_tasks_msg->step_id.step_het_comp = NO_VAL;

	signal_tasks_msg->flags = flags;
	signal_tasks_msg->signal = signal;

	agent_args->msg_args = signal_tasks_msg;
	set_agent_arg_r_uid(agent_args, SLURM_AUTH_UID_ANY);
	agent_queue_request(agent_args);
}

/*
 * prolog_complete - note the normal termination of the prolog
 * IN job_id - id of the job which completed
 * IN prolog_return_code - prolog's return code,
 *    if set then set job state to FAILED
 * RET - 0 on success, otherwise ESLURM error code
 * global: job_list - pointer global job list
 *	last_job_update - time of last job table update
 */
extern int prolog_complete(uint32_t job_id, uint32_t prolog_return_code,
			   char *node_name)
{
	job_record_t *job_ptr;

	job_ptr = find_job_record(job_id);
	if (job_ptr == NULL) {
		info("prolog_complete: invalid JobId=%u", job_id);
		return ESLURM_INVALID_JOB_ID;
	}

	if (IS_JOB_COMPLETING(job_ptr))
		return SLURM_SUCCESS;

	if (prolog_return_code) {
		error("Prolog launch failure, %pJ", job_ptr);
		job_ptr->exit_code = prolog_return_code;
	}
	/*
	 * job_ptr->node_bitmap_pr is always NULL for front end systems
	 */
	if (job_ptr->node_bitmap_pr) {
		node_record_t *node_ptr = NULL;

		if (node_name)
			node_ptr = find_node_record(node_name);

		if (node_ptr) {
			bit_clear(job_ptr->node_bitmap_pr, node_ptr->index);
		} else {
			if (node_name)
				error("%s: can't find node:%s",
				      __func__, node_name);
			bit_clear_all(job_ptr->node_bitmap_pr);
		}
	}
	if (!job_ptr->node_bitmap_pr ||
	    (bit_ffs(job_ptr->node_bitmap_pr) == -1))
	{
		job_ptr->state_reason = WAIT_NO_REASON;
		agent_trigger(999, false, true);
	}
	last_job_update = time(NULL);

	return SLURM_SUCCESS;
}

static void _handle_requeue_limit(job_record_t *job_ptr, const char *caller)
{
	if (job_ptr->batch_flag <= slurm_conf.max_batch_requeue)
		return;

	debug("%s: Holding %pJ, repeated requeue failures",
	      caller, job_ptr);

	job_state_set_flag(job_ptr, JOB_REQUEUE_HOLD);
	job_ptr->state_reason = WAIT_MAX_REQUEUE;
	xfree(job_ptr->state_desc);
	job_ptr->state_desc =
		xstrdup("launch failure limit exceeded requeued held");
	job_ptr->batch_flag = 1;
	job_ptr->priority = 0;
}

static int _job_complete(job_record_t *job_ptr, uid_t uid, bool requeue,
			 bool node_fail, uint32_t job_return_code)
{
	node_record_t *node_ptr;
	time_t now = time(NULL);
	uint32_t job_comp_flag = 0;
	bool suspended = false;
	int i;
	int use_cloud = false;
	uint16_t over_time_limit;

	xassert(verify_lock(JOB_LOCK, WRITE_LOCK));
	xassert(verify_lock(FED_LOCK, READ_LOCK));

	if (IS_JOB_FINISHED(job_ptr)) {
		if (job_ptr->exit_code == 0)
			job_ptr->exit_code = job_return_code;
		return ESLURM_ALREADY_DONE;
	}

	if (IS_JOB_COMPLETING(job_ptr))
		return SLURM_SUCCESS;	/* avoid replay */

	if ((job_return_code & 0xff) == SIG_OOM) {
		info("%s: %pJ OOM failure",  __func__, job_ptr);
	} else if (WIFSIGNALED(job_return_code)) {
		info("%s: %pJ WTERMSIG %d",
		     __func__, job_ptr, WTERMSIG(job_return_code));
	} else if (WIFEXITED(job_return_code)) {
		info("%s: %pJ WEXITSTATUS %d",
		     __func__, job_ptr, WEXITSTATUS(job_return_code));
	}

	if (IS_JOB_RUNNING(job_ptr))
		job_comp_flag = JOB_COMPLETING;
	else if (IS_JOB_PENDING(job_ptr)) {
		job_return_code = NO_VAL;
		fed_mgr_job_revoke_sibs(job_ptr);
	}

	if ((job_return_code == NO_VAL) &&
	    (IS_JOB_RUNNING(job_ptr) || IS_JOB_PENDING(job_ptr))) {
		if (node_fail) {
			info("%s: %pJ cancelled by node failure",
			     __func__, job_ptr);
		} else {
			info("%s: %pJ cancelled by interactive user",
			     __func__, job_ptr);
		}
	}

	if (IS_JOB_SUSPENDED(job_ptr)) {
		uint32_t suspend_job_state = job_ptr->job_state;
		/*
		 * we can't have it as suspended when we call the
		 * accounting stuff.
		 */
		job_state_set(job_ptr, JOB_CANCELLED);
		jobacct_storage_g_job_suspend(acct_db_conn, job_ptr);
		job_state_set(job_ptr, suspend_job_state);
		job_comp_flag = JOB_COMPLETING;
		suspended = true;
	}

	if (job_comp_flag && (job_ptr->node_cnt == 0)) {
		/*
		 * Job has no resources left (used to expand another job).
		 * Avoid duplicate run of epilog and underflow in CPU count.
		 */
		job_comp_flag = 0;
	}

	if (requeue && job_ptr->details && job_ptr->batch_flag) {
		/*
		 * We want this job to look like it was terminated in the
		 * accounting logs. Set a new submit time so the restarted
		 * job looks like a new job.
		 */
		job_ptr->end_time = now;
		if (job_ptr->bit_flags & GRACE_PREEMPT) {
			job_state_set(job_ptr, (JOB_PREEMPTED | job_comp_flag));

			/* clear signal sent on GracePeriod start */
			job_ptr->bit_flags &= (~GRACE_PREEMPT);
		} else {
			job_state_set(job_ptr, JOB_NODE_FAIL);
			job_ptr->exit_code = job_return_code;
		}

		job_completion_logger(job_ptr, true);
		/*
		 * Do this after the epilog complete.
		 * Setting it here is too early.
		 */
		//job_record_set_sluid(job_ptr);
		//job_ptr->details->submit_time = now + 1;
		if (job_ptr->node_bitmap) {
			i = bit_ffs(job_ptr->node_bitmap);
			if (i >= 0) {
				node_ptr = node_record_table_ptr[i];
				if (IS_NODE_CLOUD(node_ptr))
					use_cloud = true;
			}
		}
		if (!use_cloud)
			job_ptr->batch_flag++;	/* only one retry */
		job_ptr->restart_cnt++;

		/* clear signal sent flag on requeue */
		job_ptr->warn_flags &= ~WARN_SENT;


		job_state_set(job_ptr, (JOB_PENDING | job_comp_flag));
		job_ptr->exit_code = 0;
		/*
		 * Since the job completion logger removes the job submit
		 * information, we need to add it again.
		 */
		acct_policy_add_job_submit(job_ptr, false);
		if (node_fail) {
			info("%s: requeue %pJ due to node failure",
			     __func__, job_ptr);
		} else {
			info("%s: requeue %pJ per user/system request",
			     __func__, job_ptr);
		}
		/* hold job if over requeue limit */
		_handle_requeue_limit(job_ptr, __func__);
	} else if (IS_JOB_PENDING(job_ptr) && job_ptr->details &&
		   job_ptr->batch_flag) {
		/*
		 * Possible failure mode with DOWN node and job requeue.
		 * The DOWN node might actually respond to the cancel and
		 * take us here.  Don't run job_completion_logger here since
		 * this is here to catch duplicate cancels from slowly
		 * responding slurmds
		 */
		return SLURM_SUCCESS;
	} else {
		if (job_ptr->part_ptr &&
		    (job_ptr->part_ptr->over_time_limit != NO_VAL16)) {
			over_time_limit = job_ptr->part_ptr->over_time_limit;
		} else {
			over_time_limit = slurm_conf.over_time_limit;
		}

		if (node_fail) {
			job_state_set(job_ptr, (JOB_NODE_FAIL | job_comp_flag));
			job_ptr->exit_code = job_return_code;
			job_ptr->requid = uid;
		} else if (job_ptr->bit_flags & GRACE_PREEMPT) {
			job_state_set(job_ptr, (JOB_PREEMPTED | job_comp_flag));
		} else if (job_return_code == NO_VAL) {
			job_state_set(job_ptr, (JOB_CANCELLED | job_comp_flag));
			job_ptr->requid = uid;
		} else if ((job_return_code & 0xff) == SIG_OOM) {
			job_state_set(job_ptr, (JOB_OOM | job_comp_flag));
			job_ptr->exit_code = job_return_code;
			job_ptr->state_reason = FAIL_OOM;
			xfree(job_ptr->state_desc);
		} else if (WIFEXITED(job_return_code) &&
			   WEXITSTATUS(job_return_code)) {
			job_state_set(job_ptr, (JOB_FAILED | job_comp_flag));
			job_ptr->exit_code = job_return_code;
			job_ptr->state_reason = FAIL_EXIT_CODE;
			xfree(job_ptr->state_desc);
		} else if (WIFSIGNALED(job_return_code)) {
			job_state_set(job_ptr, (JOB_FAILED | job_comp_flag));
			job_ptr->exit_code = job_return_code;
			job_ptr->state_reason = FAIL_SIGNAL;
			xfree(job_ptr->state_desc);
			xstrfmtcat(job_ptr->state_desc,
				   "RaisedSignal:%d(%s)",
				   WTERMSIG(job_return_code),
				   strsignal(WTERMSIG(job_return_code)));
		} else if (job_comp_flag
			   && ((job_ptr->end_time
				+ over_time_limit * 60) < now)) {
			/*
			 * Test if the job has finished before its allowed
			 * over time has expired.
			 */
			job_state_set(job_ptr, (JOB_TIMEOUT | job_comp_flag));
			job_ptr->state_reason = FAIL_TIMEOUT;
			xfree(job_ptr->state_desc);
		} else {
			job_state_set(job_ptr, (JOB_COMPLETE | job_comp_flag));
			job_ptr->exit_code = job_return_code;
		}

		if (suspended) {
			job_ptr->end_time = job_ptr->suspend_time;
			job_ptr->tot_sus_time +=
				difftime(now, job_ptr->suspend_time);
		} else
			job_ptr->end_time = now;
		job_completion_logger(job_ptr, false);
	}

	last_job_update = now;
	job_ptr->time_last_active = now;   /* Timer for resending kill RPC */
	if (job_comp_flag) {	/* job was running */
		build_cg_bitmap(job_ptr);
		deallocate_nodes(job_ptr, false, suspended, false);
	}

	/* Check for and cleanup stuck scripts */
	if (IS_JOB_PENDING(job_ptr) || IS_JOB_CONFIGURING(job_ptr) ||
	    (job_ptr->details && job_ptr->details->prolog_running)) {
		slurmscriptd_flush_job(job_ptr->job_id);
		track_script_flush_job(job_ptr->job_id);
	}

	info("%s: %pJ done", __func__, job_ptr);
	return SLURM_SUCCESS;
}


/*
 * job_complete - note the normal termination the specified job
 * IN job_id - id of the job which completed
 * IN uid - user id of user issuing the RPC
 * IN requeue - job should be run again if possible
 * IN node_fail - true if job terminated due to node failure
 * IN job_return_code - job's return code, if set then set state to FAILED
 * RET - 0 on success, otherwise ESLURM error code
 * global: job_list - pointer global job list
 *	last_job_update - time of last job table update
 */
extern int job_complete(uint32_t job_id, uid_t uid, bool requeue,
			bool node_fail, uint32_t job_return_code)
{
	job_record_t *job_ptr, *het_job_ptr;
	list_itr_t *iter;
	int rc, rc1;

	xassert(verify_lock(JOB_LOCK, WRITE_LOCK));
	xassert(verify_lock(FED_LOCK, READ_LOCK));

	job_ptr = find_job_record(job_id);
	if (job_ptr == NULL) {
		info("%s: invalid JobId=%u", __func__, job_id);
		return ESLURM_INVALID_JOB_ID;
	}

	if ((job_ptr->user_id != uid) && !validate_slurm_user(uid)) {
		error("%s: Security violation, JOB_COMPLETE RPC for %pJ from uid %u",
		      __func__, job_ptr, uid);
		return ESLURM_USER_ID_MISSING;
	}

	if (job_ptr->het_job_list) {
		rc = SLURM_SUCCESS;
		iter = list_iterator_create(job_ptr->het_job_list);
		while ((het_job_ptr = list_next(iter))) {
			if (job_ptr->het_job_id != het_job_ptr->het_job_id) {
				error("%s: Bad het_job_list for %pJ",
				      __func__, job_ptr);
				continue;
			}
			rc1 = _job_complete(het_job_ptr, uid, requeue,
					    node_fail, job_return_code);
			if (rc1 != SLURM_SUCCESS)
				rc = rc1;
		}
		list_iterator_destroy(iter);
	} else {
		rc = _job_complete(job_ptr, uid, requeue, node_fail,
				   job_return_code);
	}

	return rc;
}

static int _alt_part_test(part_record_t *part_ptr, part_record_t **part_ptr_new)
{
	part_record_t *alt_part_ptr = NULL;
	char *alt_name;

	*part_ptr_new = NULL;
	if ((part_ptr->state_up & PARTITION_SUBMIT) == 0) {
		info("_alt_part_test: original partition is not available "
		     "(drain or inactive): %s", part_ptr->name);
		alt_name = part_ptr->alternate;
		while (alt_name) {
			alt_part_ptr = find_part_record(alt_name);
			if (alt_part_ptr == NULL) {
				info("_alt_part_test: invalid alternate "
				     "partition name specified: %s", alt_name);
				return ESLURM_INVALID_PARTITION_NAME;
			}
			if (alt_part_ptr == part_ptr) {
				info("_alt_part_test: no valid alternate "
				     "partition is available");
				return ESLURM_PARTITION_NOT_AVAIL;
			}
			if (alt_part_ptr->state_up & PARTITION_SUBMIT)
				break;
			/* Try next alternate in the sequence */
			alt_name = alt_part_ptr->alternate;
		}
		if (alt_name == NULL) {
			info("_alt_part_test: no valid alternate partition is "
			     "available");
			return ESLURM_PARTITION_NOT_AVAIL;
		}
		*part_ptr_new = alt_part_ptr;
	}
	return SLURM_SUCCESS;
}

static int _qos_part_check(void *object, void *arg)
{
	slurmdb_qos_rec_t *qos_ptr = object;
	qos_part_check_t *qos_part_check = arg;
	part_record_t *part_ptr = qos_part_check->part_ptr;

	if ((part_ptr->state_up & PARTITION_SCHED) &&
	    (qos_part_check->min_nodes != NO_VAL) &&
	    (qos_part_check->min_nodes < part_ptr->min_nodes) &&
	    (!qos_ptr || !(qos_ptr->flags & QOS_FLAG_PART_MIN_NODE))) {
		debug2("%s: Job requested for nodes (%u) smaller than partition %s(%u) min nodes",
		       __func__, qos_part_check->min_nodes,
		       part_ptr->name, part_ptr->min_nodes);
		qos_part_check->error_code = ESLURM_INVALID_NODE_COUNT;
		return -1;
	}

	if ((part_ptr->state_up & PARTITION_SCHED) &&
	    (qos_part_check->max_nodes != NO_VAL) &&
	    (qos_part_check->max_nodes > part_ptr->max_nodes) &&
	    (!qos_ptr || !(qos_ptr->flags & QOS_FLAG_PART_MAX_NODE))) {
		debug2("%s: Job requested for nodes (%u) greater than partition %s(%u) max nodes",
		       __func__, qos_part_check->max_nodes,
		       part_ptr->name, part_ptr->max_nodes);
		qos_part_check->error_code = ESLURM_INVALID_NODE_COUNT;
		return -1;
	}

	if ((part_ptr->state_up & PARTITION_SCHED) &&
	    (qos_part_check->time_limit != NO_VAL) &&
	    (qos_part_check->time_limit > part_ptr->max_time) &&
	    (!qos_ptr || !(qos_ptr->flags & QOS_FLAG_PART_TIME_LIMIT))) {
		debug2("%s: Job time limit (%u) exceeds limit of partition %s(%u)",
		       __func__, qos_part_check->time_limit,
		       part_ptr->name, part_ptr->max_time);
		qos_part_check->error_code = ESLURM_INVALID_TIME_LIMIT;
		return -1;
	}

	if (slurm_conf.enforce_part_limits) {
		if ((qos_part_check->error_code =
		     part_policy_valid_qos(part_ptr, qos_ptr,
					   qos_part_check->submit_uid,
					   NULL)) != SLURM_SUCCESS)
			return -1;
	}

	return 0;
}

/*
 * Test if this job can use this partition
 *
 * NOTE: This function is also called with a dummy job_desc_msg_t from
 * job_limits_check() if there is any new check added here you may also have to
 * add that parameter to the job_desc_msg_t in that function.
 */
static int _part_access_check(part_record_t *part_ptr, job_desc_msg_t *job_desc,
			      bitstr_t *req_bitmap, uid_t submit_uid,
			      slurmdb_qos_rec_t *qos_ptr,
			      list_t *qos_ptr_list, char *acct)
{
	uint32_t total_nodes;
	qos_part_check_t qos_part_check = {
		.error_code = SLURM_SUCCESS,
		.max_nodes = job_desc->max_nodes,
		.min_nodes = job_desc->min_nodes,
		.part_ptr = part_ptr,
		.submit_uid = submit_uid,
		.time_limit = job_desc->time_limit,
	};
	int rc = SLURM_SUCCESS;

	xassert(verify_assoc_lock(ASSOC_LOCK, READ_LOCK));
	xassert(verify_assoc_lock(QOS_LOCK, READ_LOCK));

	if ((part_ptr->flags & PART_FLAG_REQ_RESV) &&
	    (!job_desc->reservation || job_desc->reservation[0] == '\0')) {
		debug2("%s: uid %u access to partition %s "
		       "denied, requires reservation", __func__,
		       (unsigned int) submit_uid, part_ptr->name);
		return ESLURM_ACCESS_DENIED;
	}

	if ((part_ptr->flags & PART_FLAG_ROOT_ONLY) && (submit_uid != 0) &&
	    (submit_uid != slurm_conf.slurm_user_id)) {
		debug2("%s: uid %u access to partition %s "
		       "denied, not root", __func__,
		       (unsigned int) submit_uid, part_ptr->name);
		return ESLURM_ACCESS_DENIED;
	}

	if ((job_desc->user_id == 0) && (part_ptr->flags & PART_FLAG_NO_ROOT)) {
		error("%s: Security violation, SUBMIT_JOB for "
		      "user root disabled", __func__);
		return ESLURM_USER_ID_MISSING;
	}

	if (validate_alloc_node(part_ptr, job_desc->alloc_node) == 0) {
		debug2("%s: uid %u access to partition %s "
		       "denied, bad allocating node: %s", __func__,
		       (unsigned int) job_desc->user_id, part_ptr->name,
		       job_desc->alloc_node);
		return ESLURM_ACCESS_DENIED;
	}

	if ((part_ptr->state_up & PARTITION_SCHED) &&
	    (job_desc->min_cpus != NO_VAL)) {
		if (job_desc->min_cpus > part_ptr->total_cpus) {
			debug2("%s: Job requested too many CPUs (%u) of partition %s(%u)",
			       __func__, job_desc->min_cpus, part_ptr->name,
			       part_ptr->total_cpus);
			return ESLURM_TOO_MANY_REQUESTED_CPUS;
		} else if (job_desc->min_cpus >
			   (part_ptr->max_cpus_per_node *
			    part_ptr->total_nodes)) {
			debug2("%s: Job requested too many CPUs (%u) of partition %s(%u)",
			       __func__, job_desc->min_cpus, part_ptr->name,
			       (part_ptr->max_cpus_per_node *
				part_ptr->total_nodes));
			return ESLURM_TOO_MANY_REQUESTED_CPUS;
		}
	}

	/* Check against total nodes on the partition */
	total_nodes = part_ptr->total_nodes;
	if ((part_ptr->state_up & PARTITION_SCHED) &&
	    (job_desc->min_nodes != NO_VAL) &&
	    (job_desc->min_nodes > total_nodes)) {
		debug2("%s: Job requested too many nodes (%u) "
		       "of partition %s(%u)", __func__,
		       job_desc->min_nodes, part_ptr->name, total_nodes);
		return ESLURM_INVALID_NODE_COUNT;
	}

	if (req_bitmap && !bit_super_set(req_bitmap, part_ptr->node_bitmap)) {
		debug2("%s: requested nodes %s not in partition %s", __func__,
		       job_desc->req_nodes, part_ptr->name);
		return ESLURM_REQUESTED_NODES_NOT_IN_PARTITION;
	}

	/* Check against min/max node limits in the partition */
	if (qos_ptr_list)
		(void) list_for_each(qos_ptr_list,
				     _qos_part_check,
				     &qos_part_check);
	else
		(void) _qos_part_check(qos_ptr, &qos_part_check);
	if (qos_part_check.error_code != SLURM_SUCCESS)
		return qos_part_check.error_code;

	if (slurm_conf.enforce_part_limits) {
		if (!validate_group(part_ptr, job_desc->user_id)) {
			debug2("%s: uid %u not in group permitted to use this partition (%s). groups allowed: %s",
			     __func__, job_desc->user_id, part_ptr->name,
			     part_ptr->allow_groups);
			rc = ESLURM_JOB_MISSING_REQUIRED_PARTITION_GROUP;
			goto fini;
		}

		if ((rc = part_policy_valid_acct(part_ptr, acct, NULL))
		    != SLURM_SUCCESS)
			goto fini;
	}

fini:
	return rc;
}

static int _get_job_parts(job_desc_msg_t *job_desc, part_record_t **part_pptr,
			  list_t **part_pptr_list, char **err_msg)
{
	part_record_t *part_ptr = NULL, *part_ptr_new = NULL;
	list_t *part_ptr_list = NULL;
	int rc = SLURM_SUCCESS;

	/* Identify partition(s) and set pointer(s) to their struct */
	if (job_desc->partition) {
		char *err_part = NULL;
		get_part_list(job_desc->partition, &part_ptr_list, &part_ptr,
			      &err_part);
		if (part_ptr == NULL) {
			info("%s: invalid partition specified: %s",
			     __func__, job_desc->partition);
			if (err_msg) {
				xfree(*err_msg);
				xstrfmtcat(*err_msg,
					   "invalid partition specified: %s",
					   err_part);
				xfree(err_part);
			}
			FREE_NULL_LIST(part_ptr_list);
			return ESLURM_INVALID_PARTITION_NAME;
		}
	} else if (job_desc->reservation && job_desc->reservation[0] != '\0' ) {
		slurmctld_resv_t *resv_ptr = NULL;
		resv_ptr = find_resv_name(job_desc->reservation);
		if (resv_ptr)
			part_ptr = resv_ptr->part_ptr;
		if (part_ptr)
			job_desc->partition = xstrdup(part_ptr->name);
	}

	if (!part_ptr) {
		if (default_part_loc == NULL) {
			error("%s: default partition not set", __func__);
			return ESLURM_DEFAULT_PARTITION_NOT_SET;
		}
		part_ptr = default_part_loc;
		job_desc->partition = xstrdup(part_ptr->name);
		job_desc->bitflags |= JOB_PART_ASSIGNED;
	}

	/* Change partition pointer(s) to alternates as needed */
	if (part_ptr_list) {
		int fail_rc = SLURM_SUCCESS;
		part_record_t *part_ptr_tmp;
		bool rebuild_name_list = false;
		list_itr_t *iter = list_iterator_create(part_ptr_list);

		while ((part_ptr_tmp = list_next(iter))) {
			rc = _alt_part_test(part_ptr_tmp, &part_ptr_new);
			if (rc != SLURM_SUCCESS) {
				fail_rc = rc;
				list_remove(iter);
				rebuild_name_list = true;
				continue;
			}
			if (part_ptr_new) {
				list_insert(iter, part_ptr_new);
				list_remove(iter);
				rebuild_name_list = true;
			}
		}
		list_iterator_destroy(iter);
		if (list_is_empty(part_ptr_list)) {
			if (fail_rc != SLURM_SUCCESS)
				rc = fail_rc;
			else
				rc = ESLURM_PARTITION_NOT_AVAIL;
			goto fini;
		}
		rc = SLURM_SUCCESS;	/* At least some partition usable */
		if (rebuild_name_list) {
			part_ptr = NULL;
			xfree(job_desc->partition);
			iter = list_iterator_create(part_ptr_list);
			while ((part_ptr_tmp = list_next(iter))) {
				if (job_desc->partition)
					xstrcat(job_desc->partition, ",");
				else
					part_ptr = part_ptr_tmp;
				xstrcat(job_desc->partition,
					part_ptr_tmp->name);
			}
			list_iterator_destroy(iter);
			if (!part_ptr) {
				rc = ESLURM_PARTITION_NOT_AVAIL;
				goto fini;
			}
		}
	} else {
		rc = _alt_part_test(part_ptr, &part_ptr_new);
		if (rc != SLURM_SUCCESS)
			goto fini;
		if (part_ptr_new) {
			part_ptr = part_ptr_new;
			xfree(job_desc->partition);
			job_desc->partition = xstrdup(part_ptr->name);
		}
	}

	*part_pptr = part_ptr;
	if (part_pptr_list) {
		*part_pptr_list = part_ptr_list;
		part_ptr_list = NULL;
	} else
		FREE_NULL_LIST(part_ptr_list);

fini:
	return rc;
}

static int _valid_job_part(job_desc_msg_t *job_desc, uid_t submit_uid,
			   bitstr_t *req_bitmap, part_record_t *part_ptr,
			   list_t *part_ptr_list,
			   slurmdb_assoc_rec_t *assoc_ptr,
			   slurmdb_qos_rec_t *qos_ptr,
			   list_t *qos_ptr_list)
{
	int rc = SLURM_SUCCESS;
	part_record_t *part_ptr_tmp;
	uint32_t min_nodes_orig = INFINITE, max_nodes_orig = 1;
	uint32_t max_time = 0;
	bool any_check = false;

	xassert(verify_assoc_lock(ASSOC_LOCK, READ_LOCK));
	xassert(verify_assoc_lock(QOS_LOCK, READ_LOCK));

	/* Change partition pointer(s) to alternates as needed */
	if (part_ptr_list) {
		int fail_rc = SLURM_SUCCESS;
		list_itr_t *iter = list_iterator_create(part_ptr_list);

		while ((part_ptr_tmp = list_next(iter))) {
			/*
			 * Associations should have already be checked before
			 * this. It is not allowed to have a multiple partition
			 * request with partition based associations.
			 */
			rc = _part_access_check(part_ptr_tmp, job_desc,
						req_bitmap, submit_uid,
						qos_ptr, qos_ptr_list,
						assoc_ptr ?
						assoc_ptr->acct : NULL);

			if ((rc != SLURM_SUCCESS) &&
			    ((rc == ESLURM_ACCESS_DENIED) ||
			     (rc == ESLURM_USER_ID_MISSING) ||
			     (slurm_conf.enforce_part_limits ==
			      PARTITION_ENFORCE_ALL))) {
				fail_rc = rc;
				break;
			} else if (rc != SLURM_SUCCESS) {
				fail_rc = rc;
			} else {
				any_check = true;
			}

			/* Set to success since we found a usable partition */
			if (any_check && slurm_conf.enforce_part_limits ==
			    PARTITION_ENFORCE_ANY)
				fail_rc = SLURM_SUCCESS;

			min_nodes_orig = MIN(min_nodes_orig,
					     part_ptr_tmp->min_nodes_orig);
			max_nodes_orig = MAX(max_nodes_orig,
					     part_ptr_tmp->max_nodes_orig);
			max_time = MAX(max_time, part_ptr_tmp->max_time);
		}
		list_iterator_destroy(iter);

		if (list_is_empty(part_ptr_list) ||
		    (slurm_conf.enforce_part_limits &&
		     (fail_rc != SLURM_SUCCESS))) {
			if (slurm_conf.enforce_part_limits ==
			    PARTITION_ENFORCE_ALL)
				rc = fail_rc;
			else if (slurm_conf.enforce_part_limits ==
				 PARTITION_ENFORCE_ANY && !any_check)
				rc = fail_rc;
			else {
				rc = ESLURM_PARTITION_NOT_AVAIL;
			}
			goto fini;
		}
		rc = SLURM_SUCCESS;	/* At least some partition usable */
	} else {
		min_nodes_orig = part_ptr->min_nodes_orig;
		max_nodes_orig = part_ptr->max_nodes_orig;
		max_time = part_ptr->max_time;
		rc = _part_access_check(part_ptr, job_desc, req_bitmap,
					submit_uid, qos_ptr, qos_ptr_list,
					assoc_ptr ? assoc_ptr->acct : NULL);
		if ((rc != SLURM_SUCCESS) &&
		    ((rc == ESLURM_ACCESS_DENIED) ||
		     (rc == ESLURM_USER_ID_MISSING) ||
		     slurm_conf.enforce_part_limits))
			goto fini;
		/* Enforce Part Limit = no */
		rc = SLURM_SUCCESS;
	}

	/* Validate job limits against partition limits */

	/* Check Partition with the highest limits when there are muliple */
	if (job_desc->min_nodes == NO_VAL) {
		/* Avoid setting the job request to 0 nodes unless requested */
		if (!min_nodes_orig)
			job_desc->min_nodes = 1;
		else
			job_desc->min_nodes = min_nodes_orig;
	} else if ((job_desc->min_nodes > max_nodes_orig) &&
	           slurm_conf.enforce_part_limits &&
	           (!qos_ptr || (qos_ptr && !(qos_ptr->flags &
	                                      QOS_FLAG_PART_MAX_NODE)))) {
		info("%s: job's min nodes greater than "
		     "partition's max nodes (%u > %u)",
		     __func__, job_desc->min_nodes, max_nodes_orig);
		rc = ESLURM_INVALID_NODE_COUNT;
		goto fini;
	} else if ((job_desc->min_nodes < min_nodes_orig) &&
		   ((job_desc->max_nodes == NO_VAL) ||
		    (job_desc->max_nodes >= min_nodes_orig))) {
		job_desc->min_nodes = min_nodes_orig;
	}

	if ((job_desc->max_nodes != NO_VAL) &&
	    slurm_conf.enforce_part_limits &&
	    (job_desc->max_nodes < min_nodes_orig) &&
	    (!qos_ptr || (qos_ptr && !(qos_ptr->flags
	                               & QOS_FLAG_PART_MIN_NODE)))) {
		info("%s: job's max nodes less than partition's "
		     "min nodes (%u < %u)",
		     __func__, job_desc->max_nodes, min_nodes_orig);
		rc = ESLURM_INVALID_NODE_COUNT;
		goto fini;
	}
#ifndef HAVE_FRONT_END
	/* Zero node count OK for persistent burst buffer create or destroy */
	if ((job_desc->min_nodes == 0) &&
	    (job_desc->array_inx || (job_desc->het_job_offset != NO_VAL) ||
	     (!job_desc->burst_buffer && !job_desc->script))) {
		info("%s: min_nodes is zero", __func__);
		rc = ESLURM_INVALID_NODE_COUNT;
		goto fini;
	}
#endif

	if ((job_desc->time_limit   == NO_VAL) &&
	    (part_ptr->default_time == 0)) {
		info("%s: job's default time is 0", __func__);
		rc = ESLURM_INVALID_TIME_LIMIT;
		goto fini;
	}

	if ((job_desc->time_limit   == NO_VAL) &&
	    (part_ptr->default_time != NO_VAL))
		job_desc->time_limit = part_ptr->default_time;

	if ((job_desc->time_min != NO_VAL) &&
	    (job_desc->time_min >  max_time) &&
	    (!qos_ptr || (qos_ptr && !(qos_ptr->flags &
				       QOS_FLAG_PART_TIME_LIMIT)))) {
		info("%s: job's min time greater than "
		     "partition's (%u > %u)",
		     __func__, job_desc->time_min, max_time);
		rc = ESLURM_INVALID_TIME_MIN_LIMIT;
		goto fini;
	}
	if ((job_desc->time_limit != NO_VAL) &&
	    (job_desc->time_limit >  max_time) &&
	    (job_desc->time_min   == NO_VAL) &&
	    slurm_conf.enforce_part_limits &&
	    (!qos_ptr || (qos_ptr && !(qos_ptr->flags &
	                               QOS_FLAG_PART_TIME_LIMIT)))) {
		info("%s: job's time limit greater than "
		     "partition's (%u > %u)",
		     __func__, job_desc->time_limit, max_time);
		rc = ESLURM_INVALID_TIME_LIMIT;
		goto fini;
	}
	if ((job_desc->time_min != NO_VAL) &&
	    (job_desc->time_min >  job_desc->time_limit) &&
	    (!qos_ptr || (qos_ptr && !(qos_ptr->flags &
				       QOS_FLAG_PART_TIME_LIMIT)))) {
		info("%s: job's min_time greater time limit "
		     "(%u > %u)",
		     __func__, job_desc->time_min, job_desc->time_limit);
		rc = ESLURM_INVALID_TIME_MIN_LIMIT;
		goto fini;
	}
	if ((job_desc->deadline) && (job_desc->deadline != NO_VAL)) {
		char time_str_earliest[256];
		char time_str_deadline[256];
		time_t now = time(NULL);
		time_t begin_time = job_desc->begin_time;
		time_t earliest_start = MAX(begin_time, now);
		time_t limit_in_sec = job_desc->time_limit * 60;
		time_t min_in_sec = job_desc->time_min * 60;

		slurm_make_time_str(&job_desc->deadline, time_str_deadline,
				    sizeof(time_str_deadline));
		slurm_make_time_str(&earliest_start, time_str_earliest,
				    sizeof(time_str_earliest));

		if (job_desc->deadline < earliest_start) {
			info("%s: job's deadline is before its earliest start time (%s < %s)",
			     __func__, time_str_deadline, time_str_earliest);
			rc = ESLURM_INVALID_TIME_LIMIT;
			goto fini;
		}
		if ((job_desc->time_min) && (job_desc->time_min != NO_VAL) &&
		    (job_desc->deadline < (earliest_start + min_in_sec))) {
			info("%s: job's min_time exceeds the deadline (%s + %lu > %s)",
			     __func__, time_str_earliest, min_in_sec,
			     time_str_deadline);
			rc = ESLURM_INVALID_TIME_MIN_LIMIT;
			goto fini;
		}
		if ((!job_desc->time_min || job_desc->time_min == NO_VAL) &&
		    (job_desc->time_limit) &&
		    (job_desc->time_limit != NO_VAL) &&
		    (job_desc->deadline < (earliest_start + limit_in_sec))) {
			info("%s: job's time_limit exceeds the deadline (%s + %lu > %s)",
			     __func__, time_str_earliest, limit_in_sec,
			     time_str_deadline);
			rc = ESLURM_INVALID_TIME_LIMIT;
			goto fini;
		}
	}

fini:
	return rc;
}

/*
 * job_limits_check - check the limits specified for the job.
 * IN job_ptr - pointer to job table entry.
 * IN check_min_time - if true test job's minimum time limit,
 *		otherwise test maximum time limit
 * RET WAIT_NO_REASON on success, fail status otherwise.
 */
extern int job_limits_check(job_record_t **job_pptr, bool check_min_time)
{
	job_details_t *detail_ptr;
	enum job_state_reason fail_reason;
	part_record_t *part_ptr = NULL;
	job_record_t *job_ptr = NULL;
	slurmdb_qos_rec_t  *qos_ptr;
	slurmdb_assoc_rec_t *assoc_ptr;
	job_desc_msg_t job_desc;
	int rc;

	assoc_mgr_lock_t assoc_mgr_read_lock = {
		.assoc = READ_LOCK,
		.qos = READ_LOCK,
		.user = READ_LOCK,
	};

	assoc_mgr_lock(&assoc_mgr_read_lock);

	job_ptr = *job_pptr;
	detail_ptr = job_ptr->details;
	part_ptr = job_ptr->part_ptr;
	qos_ptr = job_ptr->qos_ptr;
	assoc_ptr = job_ptr->assoc_ptr;
	if (!detail_ptr || !part_ptr) {
		fatal_abort("%pJ has NULL details_ptr and/or part_ptr",
			    job_ptr);
		assoc_mgr_unlock(&assoc_mgr_read_lock);
		return WAIT_NO_REASON;	/* To prevent CLANG error */
	}

	fail_reason = WAIT_NO_REASON;

	/*
	 * Here we need to pretend we are just submitting the job so we can
	 * utilize the already existing function _part_access_check. If any
	 * additional fields in that function are ever checked, the fields set
	 * below will need to be modified.
	 */
	slurm_init_job_desc_msg(&job_desc);
	job_desc.reservation = job_ptr->resv_name;
	job_desc.user_id = job_ptr->user_id;
	job_desc.alloc_node = job_ptr->alloc_node;
	job_desc.min_cpus = detail_ptr->orig_min_cpus;
	job_desc.min_nodes = detail_ptr->min_nodes;
	/* _part_access_check looks for NO_VAL instead of 0 */
	job_desc.max_nodes = detail_ptr->max_nodes ?
		detail_ptr->max_nodes : NO_VAL;;
	if (check_min_time && job_ptr->time_min)
		job_desc.time_limit = job_ptr->time_min;
	else
		job_desc.time_limit = job_ptr->time_limit;

	/* For qos_ptr_list we are checking that now, so send in NULL */
	if ((rc = _part_access_check(part_ptr, &job_desc, NULL,
				     job_ptr->user_id, qos_ptr,
				     NULL,
				     job_ptr->account))) {
		debug2("%pJ can't run in partition %s: %s",
		       job_ptr, part_ptr->name, slurm_strerror(rc));
		switch (rc) {
		case ESLURM_INVALID_TIME_LIMIT:
		case ESLURM_INVALID_TIME_MIN_LIMIT:
			if (job_ptr->limit_set.time != ADMIN_SET_LIMIT)
				fail_reason = WAIT_PART_TIME_LIMIT;
			break;
		case ESLURM_INVALID_NODE_COUNT:
			fail_reason = WAIT_PART_NODE_LIMIT;
			break;
		/* FIXME */
		/* case ESLURM_TOO_MANY_REQUESTED_CPUS: */
		/* 	failt_reason = NON_EXISTANT_WAIT_PART_CPU_LIMIT; */
		/* 	break; */
		default:
			fail_reason = WAIT_PART_CONFIG;
			break;
		}
	} else if (part_ptr->state_up == PARTITION_DOWN) {
		debug2("%pJ requested down partition %s",
		       job_ptr, part_ptr->name);
		fail_reason = WAIT_PART_DOWN;
	} else if (part_ptr->state_up == PARTITION_INACTIVE) {
		debug2("%pJ requested inactive partition %s",
		       job_ptr, part_ptr->name);
		fail_reason = WAIT_PART_INACTIVE;
	} else if (qos_ptr && assoc_ptr &&
		   (qos_ptr->flags & QOS_FLAG_ENFORCE_USAGE_THRES) &&
		   (!fuzzy_equal(qos_ptr->usage_thres, NO_VAL))) {
		if (!job_ptr->prio_factors) {
			job_ptr->prio_factors =
				xmalloc(sizeof(priority_factors_t));
		}
		if (!job_ptr->prio_factors->priority_fs) {
			if (fuzzy_equal(assoc_ptr->usage->usage_efctv, NO_VAL))
				priority_g_set_assoc_usage(assoc_ptr);
			job_ptr->prio_factors->priority_fs =
				priority_g_calc_fs_factor(
					assoc_ptr->usage->usage_efctv,
					(long double)assoc_ptr->usage->
					shares_norm);
		}
		if (job_ptr->prio_factors->priority_fs < qos_ptr->usage_thres){
			debug2("%pJ exceeds usage threshold", job_ptr);
			fail_reason = WAIT_QOS_THRES;
		}
	} else if (fail_reason == WAIT_NO_REASON) {
		/*
		 * Here we need to pretend we are just submitting the job so we
		 * can utilize the already existing function _valid_pn_min_mem.
		 * If anything else is ever checked in that function this will
		 * most likely have to be updated. Some of the needed members
		 * were already initialized above to call _part_access_check, as
		 * well as the memset for job_desc.
		 */
		if (job_ptr->bit_flags & JOB_MEM_SET)
			job_desc.pn_min_memory = detail_ptr->orig_pn_min_memory;
		else if (part_ptr->def_mem_per_cpu)
			job_desc.pn_min_memory = part_ptr->def_mem_per_cpu;
		else
			job_desc.pn_min_memory = slurm_conf.def_mem_per_cpu;
		if (detail_ptr->orig_cpus_per_task == NO_VAL16)
			job_desc.cpus_per_task = 1;
		else
			job_desc.cpus_per_task = detail_ptr->orig_cpus_per_task;
		/*
		 * Passing the value directly since detail_ptr->num_tasks
		 * already set correctly. If it is zero _valid_pn_min_mem()
		 * already handles it.
		 */
		job_desc.num_tasks = detail_ptr->num_tasks;
		//job_desc.min_cpus = detail_ptr->min_cpus; /* init'ed above */
		job_desc.max_cpus = detail_ptr->orig_max_cpus;
		job_desc.shared = (uint16_t)detail_ptr->share_res;
		/*
		 * At this point detail_ptr->ntasks_per_node is expected to
		 * hold 0 (not set) or a regular value, but never NO_VAL16.
		 * _valid_pn_min_mem will check for job_desc.ntasks_per_node
		 * being different than NO_VAL16, which is its initial value.
		 */
		if (detail_ptr->ntasks_per_node)
			job_desc.ntasks_per_node = detail_ptr->ntasks_per_node;
		job_desc.ntasks_per_tres = detail_ptr->ntasks_per_tres;
		job_desc.pn_min_cpus = detail_ptr->orig_pn_min_cpus;
		job_desc.job_id = job_ptr->job_id;
		job_desc.bitflags = job_ptr->bit_flags;
		job_desc.tres_per_task = xstrdup(job_ptr->tres_per_task);
		if (!_valid_pn_min_mem(&job_desc, part_ptr)) {
			/* debug2 message already logged inside the function. */
			fail_reason = WAIT_PN_MEM_LIMIT;
		} else {
			/* Copy back to job_record adjusted members */
			detail_ptr->pn_min_memory = job_desc.pn_min_memory;
			detail_ptr->cpus_per_task = job_desc.cpus_per_task;
			detail_ptr->min_cpus = job_desc.min_cpus;
			detail_ptr->max_cpus = job_desc.max_cpus;
			detail_ptr->pn_min_cpus = job_desc.pn_min_cpus;
			SWAP(job_ptr->tres_per_task, job_desc.tres_per_task);
		}

		xfree(job_desc.tres_per_task);
	}
	assoc_mgr_unlock(&assoc_mgr_read_lock);

	return (fail_reason);
}

static void _set_tot_license_req(job_desc_msg_t *job_desc,
				 job_record_t *job_ptr)
{
	char *lic_req = NULL, *lic_req_pos = NULL;
	uint32_t num_tasks = job_desc->num_tasks;
	char *tres_per_task = job_desc->tres_per_task;

	/*
	 * If !tres_per_task we check to see if num_tasks has changed.
	 * If it has then use the current tres.
	 */
	if (job_ptr && !tres_per_task && (job_desc->bitflags & TASKS_CHANGED)) {
		tres_per_task = job_ptr->tres_per_task;
	}

	/*
	 * Here we are seeing we we are setting something explicit. If we are
	 * set it. If we are changing tasks we need what was already on the job.
	 */
	if (job_desc->licenses && (job_desc->licenses[0] ||
				   (job_desc->bitflags & RESET_LIC_JOB)))
		xstrfmtcatat(lic_req, &lic_req_pos, "%s", job_desc->licenses);
	else if (tres_per_task &&
		 !(job_desc->bitflags & RESET_LIC_JOB) &&
		 job_ptr &&
		 job_ptr->lic_req)
		xstrfmtcatat(lic_req, &lic_req_pos, "%s", job_ptr->lic_req);

	if (job_desc->bitflags & RESET_LIC_TASK) {
		/* removed tres */
		if (!lic_req)
			lic_req = xstrdup("");
	} else if (tres_per_task) {
		char *lic_tmp = slurm_get_tres_sub_string(
			tres_per_task, "license", num_tasks, false, false);
		if (lic_tmp) {
			if (lic_req) {
				xstrfmtcatat(lic_req, &lic_req_pos,
					     ",%s", lic_tmp);
				xfree(lic_tmp);
			} else {
				lic_req = lic_tmp;
				lic_tmp = NULL;
			}
		}
	}

	xfree(job_desc->licenses_tot);
	job_desc->licenses_tot = lic_req;
	lic_req = NULL;
}

static void _enable_stepmgr(job_record_t *job_ptr, job_desc_msg_t *job_desc)
{
#ifndef HAVE_FRONT_END
	static bool first_time = true;
	static bool stepmgr_enabled = false;

	if (first_time) {
		first_time = false;
		stepmgr_enabled = xstrstr(slurm_conf.slurmctld_params,
					  "enable_stepmgr");
	}

	if ((stepmgr_enabled || (job_desc->bitflags & STEPMGR_ENABLED)) &&
	    (job_desc->het_job_offset == NO_VAL) &&
	    (job_ptr->start_protocol_ver >= SLURM_24_05_PROTOCOL_VERSION)) {
		job_ptr->bit_flags |= STEPMGR_ENABLED;
	} else {
		job_ptr->bit_flags &= ~STEPMGR_ENABLED;
	}

	if ((job_ptr->bit_flags & STEPMGR_ENABLED) &&
	    !(slurm_conf.prolog_flags & PROLOG_FLAG_CONTAIN)) {
		error("STEP_MGR not supported without PrologFlags=contain");
		job_ptr->bit_flags &= ~STEPMGR_ENABLED;
	}
#endif
}

/*
 * _job_create - create a job table record for the supplied specifications.
 *	This performs only basic tests for request validity (access to
 *	partition, nodes count in partition, and sufficient processors in
 *	partition).
 * IN job_desc - job specifications
 * IN allocate - resource allocation request if set rather than job submit
 * IN will_run - job is not to be created, test of validity only
 * OUT job_pptr - pointer to the job (NULL on error)
 * OUT err_msg - Error message for user
 * RET 0 on success, otherwise ESLURM error code. If the job would only be
 *	able to execute with some change in partition configuration then
 *	ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE is returned
 */

static int _job_create(job_desc_msg_t *job_desc, int allocate, int will_run,
		       bool cron, job_record_t **job_pptr, uid_t submit_uid,
		       char **err_msg, uint16_t protocol_version)
{
	int error_code = SLURM_SUCCESS;
	part_record_t *part_ptr = NULL;
	list_t *part_ptr_list = NULL, *qos_ptr_list = NULL;
	bitstr_t *req_bitmap = NULL, *exc_bitmap = NULL;
	job_record_t *job_ptr = NULL;
	slurmdb_assoc_rec_t assoc_rec, *assoc_ptr = NULL;
	list_t *license_list = NULL, *gres_list = NULL;
	bool valid;
	slurmdb_qos_rec_t *qos_ptr;
	uint32_t user_submit_priority, acct_reason = 0;
	uint32_t qos_id = 0;
	acct_policy_limit_set_t acct_policy_limit_set;
	assoc_mgr_lock_t assoc_mgr_read_lock = {
		.assoc = READ_LOCK,
		.qos = READ_LOCK,
		.user = READ_LOCK,
	};
	gres_job_state_validate_t gres_js_val = {
		.cpus_per_tres = job_desc->cpus_per_tres,
		.mem_per_tres = job_desc->mem_per_tres,
		.tres_freq = job_desc->tres_freq,
		.tres_per_job = job_desc->tres_per_job,
		.tres_per_node = job_desc->tres_per_node,
		.tres_per_socket = job_desc->tres_per_socket,
		.tres_per_task = job_desc->tres_per_task,

		.cpus_per_task = &job_desc->cpus_per_task,
		.max_nodes = &job_desc->max_nodes,
		.min_cpus = &job_desc->min_cpus,
		.min_nodes = &job_desc->min_nodes,
		.ntasks_per_node = &job_desc->ntasks_per_node,
		.ntasks_per_socket = &job_desc->ntasks_per_socket,
		.ntasks_per_tres = &job_desc->ntasks_per_tres,
		.num_tasks = &job_desc->num_tasks,
		.sockets_per_node = &job_desc->sockets_per_node,

		.gres_list = &gres_list,
	};

	memset(&acct_policy_limit_set, 0, sizeof(acct_policy_limit_set));
	acct_policy_limit_set.tres = xcalloc(slurmctld_tres_cnt,
					     sizeof(uint16_t));

	*job_pptr = NULL;

	user_submit_priority = job_desc->priority;

	/* ensure that selected nodes are in this partition */
	if (job_desc->req_nodes) {
		error_code = node_name2bitmap(job_desc->req_nodes, false,
					      &req_bitmap, NULL);
		if (error_code) {
			error_code = ESLURM_INVALID_NODE_NAME;
			goto cleanup_fail;
		}
		if ((job_desc->contiguous != NO_VAL16) &&
		    (job_desc->contiguous))
			bit_fill_gaps(req_bitmap);
		if (bit_set_count(req_bitmap) > job_desc->min_nodes) {
			/*
			 * If a nodelist has been provided with more nodes than
			 * are required for the job, translate this into an
			 * exclusion of all nodes except those requested.
			 */
			exc_bitmap = bit_alloc(node_record_count);
			bit_or_not(exc_bitmap, req_bitmap);
			FREE_NULL_BITMAP(req_bitmap);
		}
	}

	/* Zero node count OK for persistent burst buffer create or destroy */
	if ((job_desc->max_nodes == 0) &&
	    (job_desc->array_inx || (job_desc->het_job_offset != NO_VAL) ||
	     (!job_desc->burst_buffer && !job_desc->script))) {
		info("%s: max_nodes is zero", __func__);
		error_code = ESLURM_INVALID_NODE_COUNT;
		goto cleanup_fail;
	}

	error_code = _get_job_parts(job_desc, &part_ptr, &part_ptr_list,
				    err_msg);
	if (error_code != SLURM_SUCCESS)
		goto cleanup_fail;

	memset(&assoc_rec, 0, sizeof(assoc_rec));
	assoc_rec.acct      = job_desc->account;
	assoc_rec.partition = part_ptr->name;
	assoc_rec.uid       = job_desc->user_id;
	/*
	 * Checks are done later to validate assoc_ptr, so we don't
	 * need to lock outside of fill_in_assoc.
	 */
	assoc_mgr_lock(&assoc_mgr_read_lock);
	if (assoc_mgr_fill_in_assoc(acct_db_conn, &assoc_rec,
				    accounting_enforce, &assoc_ptr, true)) {
		info("%s: invalid account or partition for user %u, "
		     "account '%s', and partition '%s'", __func__,
		     job_desc->user_id, assoc_rec.acct, assoc_rec.partition);
		error_code = ESLURM_INVALID_ACCOUNT;
		assoc_mgr_unlock(&assoc_mgr_read_lock);
		goto cleanup_fail;
	} else if (slurm_with_slurmdbd() &&
		   !assoc_ptr &&
		   !(accounting_enforce & ACCOUNTING_ENFORCE_ASSOCS)) {
		/*
		 * If not enforcing associations we want to look for the
		 * default account and use it to avoid getting trash in the
		 * accounting records.
		 */
		assoc_rec.acct = NULL;
		(void) assoc_mgr_fill_in_assoc(acct_db_conn, &assoc_rec,
					       accounting_enforce, &assoc_ptr,
					       true);
		if (assoc_ptr) {
			info("%s: account '%s' has no association for user %u "
			     "using default account '%s'",
			     __func__, job_desc->account, job_desc->user_id,
			     assoc_rec.acct);
			xfree(job_desc->account);
		}
	}

	if ((error_code = _check_for_part_assocs(
		     part_ptr_list, assoc_ptr)) != SLURM_SUCCESS) {
		assoc_mgr_unlock(&assoc_mgr_read_lock);
		goto cleanup_fail;
	}

	if (job_desc->account == NULL)
		job_desc->account = xstrdup(assoc_rec.acct);

	/* This must be done after we have the assoc_ptr set */
	error_code = _get_qos_info(job_desc->qos, 0,
				   &qos_ptr_list,
				   &qos_ptr,
				   job_desc->reservation,
				   assoc_ptr,
				   false, true, LOG_LEVEL_ERROR);
	if (error_code != SLURM_SUCCESS) {
		assoc_mgr_unlock(&assoc_mgr_read_lock);
		goto cleanup_fail;
	}

	error_code = _valid_job_part(job_desc, submit_uid, req_bitmap,
				     part_ptr, part_ptr_list,
				     assoc_ptr, qos_ptr, qos_ptr_list);
	if (qos_ptr)
		qos_id = qos_ptr->id;
	assoc_mgr_unlock(&assoc_mgr_read_lock);
	if (error_code != SLURM_SUCCESS)
		goto cleanup_fail;

	if ((error_code = _validate_job_desc(job_desc, allocate, cron,
					     submit_uid, part_ptr,
					     part_ptr_list))) {
		goto cleanup_fail;
	}

	job_desc->tres_req_cnt = xcalloc(slurmctld_tres_cnt, sizeof(uint64_t));

	_set_tot_license_req(job_desc, NULL);

	license_list = license_validate(job_desc->licenses_tot,
					validate_cfgd_licenses, true,
					job_desc->tres_req_cnt, &valid);

	if (!valid) {
		info("Job's requested licenses are invalid: %s",
		     job_desc->licenses_tot);
		error_code = ESLURM_INVALID_LICENSES;
		goto cleanup_fail;
	}

	if ((job_desc->bitflags & GRES_ONE_TASK_PER_SHARING) &&
	     (!(slurm_conf.select_type_param & MULTIPLE_SHARING_GRES_PJ))){
		info("%s: one-task-per-sharing requires MULTIPLE_SHARING_GRES_PJ",
		     __func__);
		error_code = ESLURM_INVALID_GRES;
		goto cleanup_fail;
	}

	if ((error_code = gres_job_state_validate(&gres_js_val)))
		goto cleanup_fail;

	if (!assoc_mgr_valid_tres_cnt(job_desc->cpus_per_tres, 0) ||
	    !assoc_mgr_valid_tres_cnt(job_desc->mem_per_tres, 0) ||
	    tres_bind_verify_cmdline(job_desc->tres_bind) ||
	    tres_freq_verify_cmdline(job_desc->tres_freq) ||
	    !assoc_mgr_valid_tres_cnt(job_desc->mem_per_tres, 0) ||
	    !assoc_mgr_valid_tres_cnt(job_desc->tres_per_job, 0) ||
	    !assoc_mgr_valid_tres_cnt(job_desc->tres_per_node, 0) ||
	    !assoc_mgr_valid_tres_cnt(job_desc->tres_per_socket, 0) ||
	    !assoc_mgr_valid_tres_cnt(job_desc->tres_per_task, 0)) {
		error_code = ESLURM_INVALID_TRES;
		goto cleanup_fail;
	}

	gres_stepmgr_set_job_tres_cnt(
		gres_list,
		job_desc->min_nodes,
		job_desc->tres_req_cnt,
		false);

	/* gres_job_state_validate() can update min_nodes and min_cpus. */
	job_desc->tres_req_cnt[TRES_ARRAY_NODE] = job_desc->min_nodes;
	job_desc->tres_req_cnt[TRES_ARRAY_CPU]  = job_desc->min_cpus;

	/* Get GRES before mem so we can pass gres_list to job_get_tres_mem() */
	job_desc->tres_req_cnt[TRES_ARRAY_MEM]  =
		job_get_tres_mem(NULL,
				 job_desc->pn_min_memory,
				 job_desc->tres_req_cnt[TRES_ARRAY_CPU],
				 job_desc->min_nodes, part_ptr,
				 gres_list,
				 job_desc->bitflags & JOB_MEM_SET,
				 job_desc->sockets_per_node,
				 job_desc->num_tasks);

	/*
	 * Do this last,after other TRES' have been set as it uses the other
	 * values to calculate the billing value.
	 */
	job_desc->tres_req_cnt[TRES_ARRAY_BILLING] =
		assoc_mgr_tres_weighted(job_desc->tres_req_cnt,
		                        part_ptr->billing_weights,
		                        slurm_conf.priority_flags, false);

	if ((error_code = bb_g_job_validate(job_desc, submit_uid, err_msg))
	    != SLURM_SUCCESS)
		goto cleanup_fail;

	if (job_desc->deadline && (job_desc->time_limit == NO_VAL) &&
	    (job_desc->time_min == NO_VAL))
		job_desc->time_min = 1;
	if ((accounting_enforce & ACCOUNTING_ENFORCE_LIMITS) &&
	    (!acct_policy_validate(job_desc, part_ptr, part_ptr_list,
				   assoc_ptr, qos_ptr, &acct_reason,
				   &acct_policy_limit_set, 0))) {
		if (err_msg) {
			xfree(*err_msg);
			*err_msg =
				xstrdup(job_state_reason_string(acct_reason));
		}
		info("%s: exceeded association/QOS limit for user %u: %s",
		     __func__, job_desc->user_id,
		     err_msg ? *err_msg : job_state_reason_string(acct_reason));
		error_code = ESLURM_ACCOUNTING_POLICY;
		goto cleanup_fail;
	}

	if (job_desc->exc_nodes) {
		bitstr_t *old_exc_bitmap = exc_bitmap;

		error_code = node_name2bitmap(job_desc->exc_nodes, false,
					      &exc_bitmap, NULL);
		if (error_code) {
			error_code = ESLURM_INVALID_NODE_NAME;
			goto cleanup_fail;
		}

		if (old_exc_bitmap)
			bit_or(exc_bitmap, old_exc_bitmap);
		FREE_NULL_BITMAP(old_exc_bitmap);
	}
	if (exc_bitmap && req_bitmap) {
		bitstr_t *tmp_bitmap = NULL;
		bitoff_t first_set;
		tmp_bitmap = bit_copy(exc_bitmap);
		bit_and(tmp_bitmap, req_bitmap);
		first_set = bit_ffs(tmp_bitmap);
		FREE_NULL_BITMAP(tmp_bitmap);
		if (first_set != -1) {
			info("Job's required and excluded node lists overlap");
			error_code = ESLURM_INVALID_NODE_NAME;
			goto cleanup_fail;
		}
	}

	if (job_desc->min_nodes == NO_VAL)
		job_desc->min_nodes = 1;

	if (job_desc->max_nodes == NO_VAL)
		job_desc->max_nodes = 0;

	if (job_desc->max_nodes &&
	    (job_desc->max_nodes < job_desc->min_nodes)) {
		info("%s: Job's max_nodes(%u) < min_nodes(%u)",
		     __func__, job_desc->max_nodes, job_desc->min_nodes);
		error_code = ESLURM_INVALID_NODE_COUNT;
		goto cleanup_fail;
	}

	if ((error_code = _copy_job_desc_to_job_record(job_desc,
						       job_pptr,
						       &req_bitmap,
						       &exc_bitmap))) {
		if (error_code == SLURM_ERROR)
			error_code = ESLURM_ERROR_ON_DESC_TO_RECORD_COPY;
		job_ptr = *job_pptr;
		goto cleanup_fail;
	}

	job_ptr = *job_pptr;
	job_ptr->start_protocol_ver = protocol_version;
	job_ptr->part_ptr = part_ptr;
	job_ptr->part_ptr_list = part_ptr_list;
	job_ptr->qos_list = qos_ptr_list;
	job_ptr->bit_flags |= JOB_DEPENDENT;
	job_ptr->last_sched_eval = time(NULL);

	part_ptr_list = NULL;
	qos_ptr_list = NULL;

	memcpy(&job_ptr->limit_set, &acct_policy_limit_set,
	       sizeof(acct_policy_limit_set_t));
	acct_policy_limit_set.tres = NULL;

	job_ptr->assoc_id = assoc_rec.id;
	job_ptr->assoc_ptr = (void *) assoc_ptr;
	job_ptr->qos_ptr = (void *) qos_ptr;
	job_ptr->qos_id = qos_id;

	if (mcs_g_set_mcs_label(job_ptr, job_desc->mcs_label) != 0 ) {
		if (job_desc->mcs_label == NULL) {
			error("Failed to create job: No valid mcs_label found");
		} else {
			error("Failed to create job: Invalid mcs-label: %s",
			      job_desc->mcs_label);
		}
		error_code = ESLURM_INVALID_MCS_LABEL;
		goto cleanup_fail;
	}

	/*
	 * Permission for altering priority was confirmed above. The job_submit
	 * plugin may have set the priority directly or put the job on hold. If
	 * the priority is not given, we will figure it out later after we see
	 * if the job is eligible or not. So we want NO_VAL if not set.
	 */
	job_ptr->priority = job_desc->priority;
	if (job_ptr->priority == 0) {
		if (user_submit_priority == 0)
			job_ptr->state_reason = WAIT_HELD_USER;
		else
			job_ptr->state_reason = WAIT_HELD;
	} else if ((job_ptr->priority != NO_VAL) &&
		   (job_ptr->priority != INFINITE)) {
		job_ptr->direct_set_prio = 1;
	} else if ((job_ptr->priority == INFINITE) &&
		   (user_submit_priority == INFINITE)) {
		/* This happens when "hold": false is specified to slurmrestd */
		job_ptr->priority = NO_VAL;
	}

	/*
	 * The job submit plugin sets site_factor to NO_VAL so that it can
	 * only be set the by the job submit plugin at submission.
	 */
	if (job_desc->site_factor != NO_VAL)
		job_ptr->site_factor = job_desc->site_factor;

	error_code = update_job_dependency(job_ptr, job_desc->dependency);
	if (error_code != SLURM_SUCCESS)
		goto cleanup_fail;
	job_ptr->details->orig_dependency = xstrdup(job_ptr->details->
						    dependency);

	if ((error_code = build_feature_list(job_ptr, false, false)))
		goto cleanup_fail;

	if ((error_code = build_feature_list(job_ptr, true, false)))
		goto cleanup_fail;

	error_code = extra_constraints_parse(job_ptr->extra,
					     &job_ptr->extra_constraints);
	if (error_code != SLURM_SUCCESS)
		goto cleanup_fail;

	/*
	 * NOTE: If this job is being used to expand another job, this job's
	 * gres_list has already been filled in with a copy of gres_list job
	 * to be expanded by update_job_dependency()
	 */
	if (!job_ptr->details->expanding_jobid) {
		job_ptr->gres_list_req = gres_list;
		gres_list = NULL;
	}

	job_ptr->gres_detail_cnt = 0;
	job_ptr->gres_detail_str = NULL;
	gres_job_state_log(job_ptr->gres_list_req, job_ptr->job_id);

	if ((error_code = validate_job_resv(job_ptr)))
		goto cleanup_fail;

	if (job_desc->script
	    &&  (!will_run)) {	/* don't bother with copy if just a test */
		char *tmp;
		if ((error_code = _copy_job_desc_to_file(job_desc,
							 job_ptr->job_id))) {
			error_code = ESLURM_WRITING_TO_FILE;
			goto cleanup_fail;
		}
		job_ptr->batch_flag = 1;

		if (slurm_conf.conf_flags & CONF_FLAG_SJE) {
			tmp = xstring_bytes2hex(job_desc->env_hash.hash,
						sizeof(job_desc->env_hash.hash),
						NULL);
			job_ptr->details->env_hash =
				xstrdup_printf("%d:%s",
					       job_desc->env_hash.type,
					       tmp);
			xfree(tmp);
		}

		if (slurm_conf.conf_flags & CONF_FLAG_SJS) {
			tmp = xstring_bytes2hex(
				job_desc->script_hash.hash,
				sizeof(job_desc->script_hash.hash), NULL);

			job_ptr->details->script_hash =
				xstrdup_printf("%d:%s",
					       job_desc->script_hash.type,
					       tmp);
			xfree(tmp);
		}
	} else
		job_ptr->batch_flag = 0;
	if (!will_run &&
	    (error_code = bb_g_job_validate2(job_ptr, err_msg)))
		goto cleanup_fail;

	job_ptr->license_list = license_list;
	license_list = NULL;

	if (job_desc->req_switch != NO_VAL) {	/* Max # of switches */
		job_ptr->req_switch = job_desc->req_switch;
		if (job_desc->wait4switch != NO_VAL) {
			job_ptr->wait4switch =
				_max_switch_wait(job_desc->wait4switch);
		} else
			job_ptr->wait4switch = _max_switch_wait(INFINITE);
	}
	job_ptr->best_switch = true;

	_enable_stepmgr(job_ptr, job_desc);

	FREE_NULL_LIST(license_list);
	FREE_NULL_LIST(gres_list);
	FREE_NULL_BITMAP(req_bitmap);
	FREE_NULL_BITMAP(exc_bitmap);
	return error_code;

cleanup_fail:
	if (job_ptr) {
		job_state_set(job_ptr, JOB_FAILED);
		job_ptr->exit_code = 1;
		job_ptr->state_reason = FAIL_SYSTEM;
		xfree(job_ptr->state_desc);
		job_ptr->start_time = job_ptr->end_time = time(NULL);
		purge_job_record(job_ptr->job_id);
		*job_pptr = NULL;
	}
	FREE_NULL_LIST(license_list);
	xfree(acct_policy_limit_set.tres);
	FREE_NULL_LIST(gres_list);
	FREE_NULL_LIST(part_ptr_list);
	FREE_NULL_LIST(qos_ptr_list);
	FREE_NULL_BITMAP(req_bitmap);
	FREE_NULL_BITMAP(exc_bitmap);
	return error_code;
}

static int _test_strlen(char *test_str, char *str_name, int max_str_len)
{
	int i = 0;

	if (test_str)
		i = strlen(test_str);
	if (i > max_str_len) {
		info("job_create_request: strlen(%s) too big (%d > %d)",
		     str_name, i, max_str_len);
		return ESLURM_PATHNAME_TOO_LONG;
	}
	return SLURM_SUCCESS;
}

/* Translate a job array expression into the equivalent bitmap */
static bool _valid_array_inx(job_desc_msg_t *job_desc)
{
	static time_t sched_update = 0;
	static uint32_t max_task_cnt = NO_VAL;
	uint32_t task_cnt;
	bool valid = true;
	char *tmp, *tok, *last = NULL;

	FREE_NULL_BITMAP(job_desc->array_bitmap);
	if (!job_desc->array_inx || !job_desc->array_inx[0])
		return true;
	if (!job_desc->script || !job_desc->script[0])
		return false;

	if (max_array_size == NO_VAL) {
		max_array_size = slurm_conf.max_array_sz;
	}
	if (max_array_size == 0) {
		verbose("Job arrays disabled, MaxArraySize=0");
		return false;
	}

	if (sched_update != slurm_conf.last_update) {
		char *key;
		max_task_cnt = max_array_size;
		sched_update = slurm_conf.last_update;
		if ((key = xstrcasestr(slurm_conf.sched_params,
		                       "max_array_tasks="))) {
			key += 16;
			max_task_cnt = atoi(key);
		}
	}

	/* We have a job array request */
	job_desc->immediate = 0;	/* Disable immediate option */
	job_desc->array_bitmap = bit_alloc(max_array_size);

	tmp = xstrdup(job_desc->array_inx);
	tok = strtok_r(tmp, ",", &last);
	while (tok && valid) {
		valid = slurm_parse_array_tok(tok, job_desc->array_bitmap,
					      max_array_size);
		tok = strtok_r(NULL, ",", &last);
	}
	xfree(tmp);

	if (valid && (max_task_cnt < max_array_size)) {
		task_cnt = bit_set_count(job_desc->array_bitmap);
		if (task_cnt > max_task_cnt) {
			debug("max_array_tasks exceeded (%u > %u)",
			      task_cnt, max_task_cnt);
			valid = false;
		}
	}

	return valid;
}

/* Make sure a job descriptor's strings are not huge, which could result in
 * a denial of service attack due to memory demands by the slurmctld */
static int _test_job_desc_fields(job_desc_msg_t * job_desc)
{
	static time_t sched_update = 0;
	static int max_script = DEFAULT_BATCH_SCRIPT_LIMIT;
	static int max_submit_line = DEFAULT_MAX_SUBMIT_LINE_SIZE;

	if (sched_update != slurm_conf.last_update) {
		char *tmp_ptr;
		sched_update = slurm_conf.last_update;

		if ((tmp_ptr = xstrcasestr(slurm_conf.sched_params,
		                           "max_script_size="))) {
			max_script = atoi(tmp_ptr + 16);
		} else {
			max_script = DEFAULT_BATCH_SCRIPT_LIMIT;
		}

		if ((tmp_ptr = xstrcasestr(slurm_conf.sched_params,
		                           "max_submit_line_size="))) {
			max_submit_line = atoi(tmp_ptr + 21);
		} else {
			max_submit_line = DEFAULT_MAX_SUBMIT_LINE_SIZE;
		}
	}

	if (_test_strlen(job_desc->account, "account", 1024)		||
	    _test_strlen(job_desc->alloc_node, "alloc_node", 1024)	||
	    _test_strlen(job_desc->array_inx, "array_inx", 1024 * 4)	||
	    _test_strlen(job_desc->burst_buffer, "burst_buffer",1024*8) ||
	    _test_strlen(job_desc->comment, "comment", 1024)		||
	    _test_strlen(job_desc->cpu_bind, "cpu-bind", 1024 * 128)	||
	    _test_strlen(job_desc->cpus_per_tres, "cpus_per_tres", 1024)||
	    _test_strlen(job_desc->dependency, "dependency", 1024*128)	||
	    _test_strlen(job_desc->extra, "extra", 1024)		||
	    _test_strlen(job_desc->features, "features", 1024)		||
	    _test_strlen(
		    job_desc->cluster_features, "cluster_features", 1024)   ||
	    _test_strlen(job_desc->licenses_tot, "licenses", 1024)	||
	    _test_strlen(job_desc->mail_user, "mail_user", 1024)	||
	    _test_strlen(job_desc->mcs_label, "mcs_label", 1024)	||
	    _test_strlen(job_desc->mem_bind, "mem-bind", 1024 * 128)	||
	    _test_strlen(job_desc->mem_per_tres, "mem_per_tres", 1024)	||
	    _test_strlen(job_desc->name, "name", 1024)			||
	    _test_strlen(job_desc->network, "network", 1024)		||
	    _test_strlen(job_desc->partition, "partition", 1024)	||
	    _test_strlen(job_desc->prefer, "prefer", 1024)		||
	    _test_strlen(job_desc->qos, "qos", 1024)			||
	    _test_strlen(job_desc->reservation, "reservation", 1024)	||
	    _test_strlen(job_desc->script, "script", max_script)	||
	    _test_strlen(job_desc->std_err, "std_err", PATH_MAX)	||
	    _test_strlen(job_desc->std_in, "std_in", PATH_MAX)		||
	    _test_strlen(job_desc->std_out, "std_out", PATH_MAX)	||
	    _test_strlen(job_desc->submit_line, "submit_line",
			 max_submit_line) ||
	    _test_strlen(job_desc->tres_bind, "tres_bind", 1024)	||
	    _test_strlen(job_desc->tres_freq, "tres_freq", 1024)	||
	    _test_strlen(job_desc->tres_per_job, "tres_per_job", 1024)	||
	    _test_strlen(job_desc->tres_per_node, "tres_per_node", 1024)||
	    _test_strlen(job_desc->tres_per_socket, "tres_per_socket", 1024) ||
	    _test_strlen(job_desc->tres_per_task, "tres_per_task", 1024)||
	    _test_strlen(job_desc->wckey, "wckey", 1024)		||
	    _test_strlen(job_desc->work_dir, "work_dir", PATH_MAX))
		return ESLURM_PATHNAME_TOO_LONG;

	return SLURM_SUCCESS;
}

static void _figure_out_num_tasks(
	job_desc_msg_t *job_desc, job_record_t *job_ptr)
{
	uint32_t num_tasks = job_desc->num_tasks;
	uint32_t min_nodes = job_desc->min_nodes;
	uint32_t max_nodes = job_desc->max_nodes;
	uint16_t ntasks_per_node = job_desc->ntasks_per_node;
	uint16_t ntasks_per_tres = job_desc->ntasks_per_tres;

	if (num_tasks != NO_VAL) {
		job_desc->bitflags |= JOB_NTASKS_SET;
	}

	if (job_ptr) {
		if (min_nodes == NO_VAL)
			min_nodes = job_ptr->details->min_nodes;
		if (max_nodes == NO_VAL)
			max_nodes = job_ptr->details->max_nodes;
		if (max_nodes == 0)
			max_nodes = min_nodes;

		if ((ntasks_per_node == NO_VAL16) &&
		    job_ptr->details->ntasks_per_node)
			ntasks_per_node = job_ptr->details->ntasks_per_node;
		else if ((ntasks_per_tres == NO_VAL16) &&
			 job_ptr->details->ntasks_per_tres)
			ntasks_per_tres = job_ptr->details->ntasks_per_tres;

	} else if (job_desc->min_nodes == NO_VAL) {
		min_nodes = job_desc->min_nodes = 1;
	}

	/* If we are creating the job we want the tasks to be set every time. */
	if ((num_tasks == NO_VAL) &&
	    (min_nodes != NO_VAL) &&
	    (!job_ptr || (job_ptr && (min_nodes == max_nodes)))) {
		/* Implicitly set task count */
		if (ntasks_per_tres != NO_VAL16)
			num_tasks = min_nodes * ntasks_per_tres;
		else if (ntasks_per_node != NO_VAL16)
			num_tasks = min_nodes * ntasks_per_node;
	}

	if (job_ptr) {
		if ((num_tasks != NO_VAL) &&
		    (num_tasks != job_ptr->details->num_tasks)) {
			job_desc->num_tasks = num_tasks;
			job_desc->bitflags |= TASKS_CHANGED;
		}
	} else if (num_tasks != job_desc->num_tasks) {
		job_desc->num_tasks = num_tasks;
		job_desc->bitflags |= TASKS_CHANGED;
	}
}

/* Perform some size checks on strings we store to prevent
 * malicious user filling slurmctld's memory
 * IN job_desc   - user job submit request
 * IN submit_uid - UID making job submit request
 * OUT err_msg   - custom error message to return
 * RET 0 or error code */
extern int validate_job_create_req(job_desc_msg_t * job_desc, uid_t submit_uid,
				   char **err_msg)
{
	job_record_t *job_ptr = NULL;
	int rc;

	/*
	 * Check user permission for negative 'nice' and non-0 priority values
	 * (restricted to root, SlurmUser, or SLURMDB_ADMIN_OPERATOR) _before_
	 * running the job_submit plugin.
	 */
	if (!validate_operator(submit_uid)) {
		if (job_desc->priority != 0)
			job_desc->priority = NO_VAL;
		if (job_desc->nice < NICE_OFFSET)
			return ESLURM_INVALID_NICE;
	}

	if (!validate_super_user(submit_uid)) {
		/* AdminComment can only be set by an Admin. */
		if (job_desc->admin_comment)
			return ESLURM_ACCESS_DENIED;

		if (job_desc->reboot && (job_desc->reboot != NO_VAL16)) {
			*err_msg = xstrdup("rebooting of nodes is only allowed for admins");
			return ESLURM_ACCESS_DENIED;
		}
	}

	rc = job_submit_g_submit(job_desc, submit_uid, err_msg);
	if (rc != SLURM_SUCCESS)
		return rc;

	/* Reject jobs requesting arbitrary distribution without a task count */
	if (((job_desc->task_dist & SLURM_DIST_STATE_BASE) ==
	     SLURM_DIST_ARBITRARY) && (job_desc->num_tasks == NO_VAL)) {
		*err_msg = xstrdup("task count required for arbitrary distribution");
		return ESLURM_BAD_TASK_COUNT;
	}

	/* Add a temporary job_ptr for node_features_g_job_valid */
	job_ptr = xmalloc(sizeof(job_record_t));
	job_ptr->details = xmalloc(sizeof(job_details_t));
	/* Point, don't dup, so don't free */
	job_ptr->details->features = job_desc->features;
	job_ptr->details->prefer = job_desc->prefer;
	/* job_ptr->job_id = 0; */
	job_ptr->user_id = job_desc->user_id;
	if ((rc = build_feature_list(job_ptr, false, false)) != SLURM_SUCCESS)
		goto fini;
	rc = node_features_g_job_valid(job_desc->features,
				       job_ptr->details->feature_list);
	if (rc != SLURM_SUCCESS)
		goto fini;

	if (build_feature_list(job_ptr, true, false) != SLURM_SUCCESS) {
		rc = ESLURM_INVALID_PREFER;
		goto fini;
	}
	rc = node_features_g_job_valid(job_desc->prefer,
				       job_ptr->details->prefer_list);
	if (rc == ESLURM_INVALID_FEATURE)
		rc = ESLURM_INVALID_PREFER;
	if (rc != SLURM_SUCCESS) {
		goto fini;
	}

	rc = _test_job_desc_fields(job_desc);
	if (rc != SLURM_SUCCESS)
		goto fini;

	if (!_valid_array_inx(job_desc)) {
		rc = ESLURM_INVALID_ARRAY;
		goto fini;
	}

	if (job_desc->x11 && !(slurm_conf.prolog_flags & PROLOG_FLAG_X11)) {
		rc = ESLURM_X11_NOT_AVAIL;
		goto fini;
	}

	/* Make sure anything that may be put in the database will be
	 * lower case */
	xstrtolower(job_desc->account);
	xstrtolower(job_desc->wckey);

	/* Basic validation of some parameters */
	if (job_desc->req_nodes && (job_desc->min_nodes == NO_VAL)) {
		bitstr_t *node_bitmap = NULL;
		if (node_name2bitmap(job_desc->req_nodes, false,
				     &node_bitmap, NULL)) {
			/* likely a badly formatted hostlist */
			error("validate_job_create_req: bad hostlist");
			rc = ESLURM_INVALID_NODE_NAME;
			goto fini;
		}
		job_desc->min_nodes = bit_set_count(node_bitmap);
		FREE_NULL_BITMAP(node_bitmap);
	}

	_figure_out_num_tasks(job_desc, NULL);

	/* Only set min and max cpus if overcommit isn't set */
	if ((job_desc->overcommit == NO_VAL8) &&
	    ((job_desc->min_cpus == NO_VAL) ||
	     ((job_desc->min_cpus != NO_VAL) &&
	      (job_desc->num_tasks != NO_VAL) &&
	      (job_desc->num_tasks > job_desc->min_cpus)))) {
		if (job_desc->num_tasks != NO_VAL)
			job_desc->min_cpus = job_desc->num_tasks;
		else if (job_desc->min_nodes != NO_VAL)
			job_desc->min_cpus = job_desc->min_nodes;
		else
			job_desc->min_cpus = 1;

		if (job_desc->cpus_per_task != NO_VAL16)
			job_desc->min_cpus *= job_desc->cpus_per_task;
		/* This is just a sanity check as we wouldn't ever have a
		 * max_cpus if we didn't have a min_cpus.
		 */
		if ((job_desc->max_cpus != NO_VAL) &&
		    (job_desc->max_cpus < job_desc->min_cpus))
			job_desc->max_cpus = job_desc->min_cpus;
	}

	if (job_desc->reboot && (job_desc->reboot != NO_VAL16))
		job_desc->shared = 0;

fini:
	on_job_state_change(job_ptr, NO_VAL);
	FREE_NULL_LIST(job_ptr->details->feature_list);
	FREE_NULL_LIST(job_ptr->details->prefer_list);
	xfree(job_ptr->details);
	xfree(job_ptr);

	return rc;
}

/* _copy_job_desc_to_file - copy the job script and environment from the RPC
 *	structure into a file */
static int
_copy_job_desc_to_file(job_desc_msg_t * job_desc, uint32_t job_id)
{
	int error_code = 0, hash;
	char *dir_name, *file_name;
	DEF_TIMERS;

	START_TIMER;

	if (!job_desc->container &&
	    (!job_desc->environment || job_desc->env_size == 0)) {
		error("%s: batch job cannot run without an environment",
		      __func__);
		return ESLURM_ENVIRONMENT_MISSING;
	}

	/* Create directory based upon job ID due to limitations on the number
	 * of files possible in a directory on some file system types (e.g.
	 * up to 64k files on a FAT32 file system). */
	hash = job_id % 10;
	dir_name = xstrdup_printf("%s/hash.%d",
	                          slurm_conf.state_save_location, hash);
	(void) mkdir(dir_name, 0700);

	/* Create job_id specific directory */
	xstrfmtcat(dir_name, "/job.%u", job_id);
	if (mkdir(dir_name, 0700)) {
		if (!slurmctld_primary && (errno == EEXIST)) {
			error("Apparent duplicate JobId=%u. Two primary slurmctld daemons might currently be active",
			      job_id);
		}
		error("mkdir(%s) error %m", dir_name);
		xfree(dir_name);
		return ESLURM_WRITING_TO_FILE;
	}

	/* Create environment file, and write data to it */
	file_name = xstrdup_printf("%s/environment", dir_name);
	error_code = _write_data_array_to_file(file_name,
					       job_desc->environment,
					       job_desc->env_size);
	xfree(file_name);

	if (error_code == 0) {
		/* Create script file */
		file_name = xstrdup_printf("%s/script", dir_name);
		error_code = write_data_to_file(file_name, job_desc->script);
		xfree(file_name);
	}

	xfree(dir_name);
	END_TIMER2(__func__);
	return error_code;
}

/* Return true of the specified job ID already has a batch directory so
 * that a different job ID can be created. This is to help limit damage from
 * split-brain, where two slurmctld daemons are running as primary. */
static bool _dup_job_file_test(uint32_t job_id)
{
	char *dir_name_src;
	struct stat buf;
	int rc, hash = job_id % 10;

	dir_name_src = xstrdup_printf("%s/hash.%d/job.%u",
	                              slurm_conf.state_save_location,
	                              hash, job_id);
	rc = stat(dir_name_src, &buf);
	xfree(dir_name_src);
	if (rc == 0) {
		error("Vestigial state files for JobId=%u, but no job record. This may be the result of two slurmctld running in primary mode",
		      job_id);
		return true;
	}
	errno = 0; /* don't care about errno */
	return false;
}

/*
 * Create file with specified name and write the supplied data array to it
 * IN file_name - file to create and write to
 * IN data - array of pointers to strings (e.g. env)
 * IN size - number of elements in data
 */
static int
_write_data_array_to_file(char *file_name, char **data, uint32_t size)
{
	int fd, i, pos, nwrite, amount;

	fd = creat(file_name, 0600);
	if (fd < 0) {
		error("Error creating file %s, %m", file_name);
		return ESLURM_WRITING_TO_FILE;
	}

	amount = write(fd, &size, sizeof(uint32_t));
	if (amount < sizeof(uint32_t)) {
		error("Error writing file %s, %m", file_name);
		close(fd);
		return ESLURM_WRITING_TO_FILE;
	}

	if (data == NULL) {
		close(fd);
		return SLURM_SUCCESS;
	}

	for (i = 0; i < size; i++) {
		nwrite = strlen(data[i]) + 1;
		pos = 0;
		while (nwrite > 0) {
			amount = write(fd, &data[i][pos], nwrite);
			if ((amount < 0) && (errno != EINTR)) {
				error("Error writing file %s, %m",
				      file_name);
				close(fd);
				return ESLURM_WRITING_TO_FILE;
			}
			nwrite -= amount;
			pos    += amount;
		}
	}

	close(fd);
	return SLURM_SUCCESS;
}

/*
 * Create file with specified name and write the supplied data array to it
 * IN file_name - file to create and write to
 * IN data - pointer to string
 */
extern int write_data_to_file(char *file_name, char *data)
{
	int fd, pos, nwrite, amount;

	if (data == NULL) {
		(void) unlink(file_name);
		return SLURM_SUCCESS;
	}

	fd = creat(file_name, 0700);
	if (fd < 0) {
		error("Error creating file %s, %m", file_name);
		return ESLURM_WRITING_TO_FILE;
	}

	nwrite = strlen(data) + 1;
	pos = 0;
	while (nwrite > 0) {
		amount = write(fd, &data[pos], nwrite);
		if ((amount < 0) && (errno != EINTR)) {
			error("Error writing file %s, %m", file_name);
			close(fd);
			return ESLURM_WRITING_TO_FILE;
		}
		nwrite -= amount;
		pos    += amount;
	}
	close(fd);
	return SLURM_SUCCESS;
}

/*
 * get_job_env - return the environment variables and their count for a
 *	given job
 * IN job_ptr - pointer to job for which data is required
 * OUT env_size - number of elements to read
 * RET point to array of string pointers containing environment variables
 */
char **get_job_env(job_record_t *job_ptr, uint32_t *env_size)
{
	char *file_name = NULL, **environment = NULL;
	int cc, fd = -1, hash;
	uint32_t use_id;

	use_id = (job_ptr->array_task_id != NO_VAL) ?
		job_ptr->array_job_id : job_ptr->job_id;
	hash = use_id % 10;
	file_name = xstrdup_printf("%s/hash.%d/job.%u/environment",
	                           slurm_conf.state_save_location,
	                           hash, use_id);
	fd = open(file_name, 0);

	if (fd >= 0) {
		cc = _read_data_array_from_file(fd, file_name, &environment,
						env_size, job_ptr);
		if (cc < 0)
			environment = NULL;
		close(fd);
	} else {
		error("Could not open environment file for %pJ", job_ptr);
	}

	xfree(file_name);
	return environment;
}

/*
 * get_job_script - return the script for a given job
 * IN job_ptr - pointer to job for which data is required
 * RET buf_t *containing job script
 */
buf_t *get_job_script(const job_record_t *job_ptr)
{
	char *file_name = NULL;
	int hash;
	uint32_t use_id;
	buf_t *buf;

	if (!job_ptr->batch_flag)
		return NULL;

	use_id = (job_ptr->array_task_id != NO_VAL) ?
		job_ptr->array_job_id : job_ptr->job_id;
	hash = use_id % 10;
	file_name = xstrdup_printf("%s/hash.%d/job.%u/script",
	                           slurm_conf.state_save_location,
	                           hash, use_id);

	if (!(buf = create_mmap_buf(file_name)))
		error("Could not open script file for %pJ", job_ptr);
	xfree(file_name);

	return buf;
}

extern uint16_t job_get_sockets_per_node(job_record_t *job_ptr)
{
	xassert(job_ptr);

	if (job_ptr->details && job_ptr->details->mc_ptr &&
	    job_ptr->details->mc_ptr->sockets_per_node &&
	    (job_ptr->details->mc_ptr->sockets_per_node != NO_VAL16))
		return job_ptr->details->mc_ptr->sockets_per_node;
	return 1;
}

/*
 * Read a collection of strings from a file
 * IN fd - file descriptor
 * IN file_name - file to read from
 * OUT data - pointer to array of pointers to strings (e.g. env),
 *	must be xfreed when no longer needed
 * OUT size - number of elements in data
 * IN job_ptr - job
 * RET 0 on success, -1 on error
 * NOTE: The output format of this must be identical with _xduparray2()
 */
static int _read_data_array_from_file(int fd, char *file_name, char ***data,
				      uint32_t *size, job_record_t *job_ptr)
{
	int pos, buf_size, amount, i, j;
	char *buffer, **array_ptr;
	uint32_t rec_cnt;

	xassert(file_name);
	xassert(data);
	xassert(size);
	*data = NULL;
	*size = 0;

	amount = read(fd, &rec_cnt, sizeof(uint32_t));
	if (amount < sizeof(uint32_t)) {
		if (amount != 0)	/* incomplete write */
			error("Error reading file %s, %m", file_name);
		else
			verbose("File %s has zero size", file_name);
		return -1;
	}

	if (rec_cnt >= INT_MAX) {
		error("%s: unreasonable record counter %d in file %s",
		      __func__, rec_cnt, file_name);
		return -1;
	}

	if (rec_cnt == 0) {
		*data = NULL;
		*size = 0;
		return 0;
	}

	pos = 0;
	buf_size = BUF_SIZE;
	buffer = xmalloc(buf_size + 1);
	while (1) {
		amount = read(fd, &buffer[pos], BUF_SIZE);
		if (amount < 0) {
			error("Error reading file %s, %m", file_name);
			xfree(buffer);
			return -1;
		}
		buffer[pos + amount] = '\0';
		pos += amount;
		if (amount < BUF_SIZE)	/* end of file */
			break;
		buf_size += amount;
		xrealloc(buffer, buf_size + 1);
	}

	/* Allocate extra space for supplemental environment variables */
	if (job_ptr->details->env_cnt) {
		for (j = 0; j < job_ptr->details->env_cnt; j++)
			pos += (strlen(job_ptr->details->env_sup[j]) + 1);
		xrealloc(buffer, pos);
	}

	/* We have all the data, now let's compute the pointers */
	array_ptr = xcalloc((rec_cnt + job_ptr->details->env_cnt) + 1,
			    sizeof(char *));
	for (i = 0, pos = 0; i < rec_cnt; i++) {
		array_ptr[i] = &buffer[pos];
		pos += strlen(&buffer[pos]) + 1;
		if ((pos > buf_size) && ((i + 1) < rec_cnt)) {
			error("Bad environment file %s", file_name);
			rec_cnt = i;
			break;
		}
	}

	/* Add supplemental environment variables */
	if (job_ptr->details->env_cnt) {
		char *tmp_chr;
		int env_len, name_len;
		for (j = 0; j < job_ptr->details->env_cnt; j++) {
			tmp_chr = strchr(job_ptr->details->env_sup[j], '=');
			if (tmp_chr == NULL) {
				error("Invalid supplemental environment "
				      "variable: %s",
				      job_ptr->details->env_sup[j]);
				continue;
			}
			env_len  = strlen(job_ptr->details->env_sup[j]) + 1;
			name_len = tmp_chr - job_ptr->details->env_sup[j] + 1;
			/* search for duplicate */
			for (i = 0; i < rec_cnt; i++) {
				if (xstrncmp(array_ptr[i],
					     job_ptr->details->env_sup[j],
					     name_len)) {
					continue;
				}

				/*
				 * If we are are the front we can not overwrite
				 * that spot, we can clear it an then add to the
				 * end of the array.
				 */
				if (i == 0) {
					array_ptr[0][0] = '\0';
					i = rec_cnt;
					break;
				}
				/* over-write duplicate */
				memcpy(&buffer[pos],
				       job_ptr->details->env_sup[j], env_len);
				array_ptr[i] = &buffer[pos];
				pos += env_len;
				break;
			}
			if (i >= rec_cnt) {	/* add env to array end */
				memcpy(&buffer[pos],
				       job_ptr->details->env_sup[j], env_len);
				array_ptr[rec_cnt++] = &buffer[pos];
				pos += env_len;
			}
		}
	}

	*size = rec_cnt;
	*data = array_ptr;
	return 0;
}

/* Given a job request, return a multi_core_data struct.
 * Returns NULL if no values set in the job/step request */
static multi_core_data_t *
_set_multi_core_data(job_desc_msg_t * job_desc)
{
	multi_core_data_t * mc_ptr;

	if ((job_desc->sockets_per_node  == NO_VAL16)	&&
	    (job_desc->cores_per_socket  == NO_VAL16)	&&
	    (job_desc->threads_per_core  == NO_VAL16)	&&
	    (job_desc->ntasks_per_socket == NO_VAL16)	&&
	    (job_desc->ntasks_per_core   == NO_VAL16)	&&
	    (job_desc->plane_size        == NO_VAL16))
		return NULL;

	mc_ptr = xmalloc(sizeof(multi_core_data_t));
	mc_ptr->sockets_per_node = job_desc->sockets_per_node;
	mc_ptr->cores_per_socket = job_desc->cores_per_socket;
	mc_ptr->threads_per_core = job_desc->threads_per_core;
	if (job_desc->ntasks_per_socket != NO_VAL16)
		mc_ptr->ntasks_per_socket  = job_desc->ntasks_per_socket;
	else
		mc_ptr->ntasks_per_socket  = INFINITE16;
	if (job_desc->ntasks_per_core != NO_VAL16)
		mc_ptr->ntasks_per_core    = job_desc->ntasks_per_core;
	else if (slurm_conf.select_type_param & CR_ONE_TASK_PER_CORE)
		mc_ptr->ntasks_per_core    = 1;
	else
		mc_ptr->ntasks_per_core    = INFINITE16;
	if (job_desc->plane_size != NO_VAL16)
		mc_ptr->plane_size         = job_desc->plane_size;
	else
		mc_ptr->plane_size         = 0;

	return mc_ptr;
}

/* Return default "wait_all_nodes" option for a new job */
static uint16_t _default_wait_all_nodes(job_desc_msg_t *job_desc)
{
	static uint16_t default_batch_wait = NO_VAL16;
	static time_t sched_update = 0;

	if (!job_desc->script)
		return 0;

	if ((default_batch_wait != NO_VAL16) &&
	    (sched_update == slurm_conf.last_update))
		return default_batch_wait;

	if (xstrcasestr(slurm_conf.sched_params, "sbatch_wait_nodes"))
		default_batch_wait = 1;
	else
		default_batch_wait = 0;
	sched_update = slurm_conf.last_update;

	return default_batch_wait;
}

static int _unroll_min_max_node(job_record_t *job_ptr)
{
	static int max_unroll = -1;
	static time_t topo_update = 0;
	job_details_t *detail_ptr = job_ptr->details;
	int i;

	if (topo_update != slurm_conf.last_update) {
		char *tmp_ptr;
		topo_update = slurm_conf.last_update;
		char *unroll_opt_str = "TopoMaxSizeUnroll=";

		if ((topology_get_plugin_id() == TOPOLOGY_PLUGIN_BLOCK) &&
		    (tmp_ptr = xstrcasestr(slurm_conf.topology_param,
					   unroll_opt_str))) {
			i = atoi(tmp_ptr + strlen(unroll_opt_str));
			if (i < 0) {
				error("ignoring TopologyParam: TopoMaxSizeUnroll %d",
				      i);
			} else {
				max_unroll = i;
			}
		}
	}

	if (max_unroll < 0)
		return SLURM_SUCCESS;

	if (detail_ptr->job_size_bitmap)
		return SLURM_SUCCESS;

	if (!detail_ptr->max_nodes ||
	    (detail_ptr->max_nodes == detail_ptr->min_nodes))
		return SLURM_SUCCESS;

	if ((detail_ptr->max_nodes < MAX_JOB_SIZE_BITMAP) &&
	    ((detail_ptr->max_nodes - detail_ptr->min_nodes) < max_unroll)) {
		bitstr_t *size_bitmap;
		size_bitmap = bit_alloc(detail_ptr->max_nodes + 1);
		bit_nset(size_bitmap, detail_ptr->min_nodes,
			 detail_ptr->max_nodes);
		detail_ptr->job_size_bitmap = size_bitmap;
	} else {
		return ESLURM_INVALID_NODE_COUNT;
	}

	return SLURM_SUCCESS;
}

/* _copy_job_desc_to_job_record - copy the job descriptor from the RPC
 *	structure into the actual slurmctld job record */
static int _copy_job_desc_to_job_record(job_desc_msg_t *job_desc,
					job_record_t **job_rec_ptr,
					bitstr_t **req_bitmap,
					bitstr_t **exc_bitmap)
{
	int error_code;
	job_details_t *detail_ptr;
	job_record_t *job_ptr;

	if (slurm_conf.conf_flags & CONF_FLAG_WCKEY) {
		if (!job_desc->wckey) {
			/* get the default wckey for this user since none was
			 * given */
			slurmdb_user_rec_t user_rec;
			memset(&user_rec, 0, sizeof(user_rec));
			user_rec.uid = job_desc->user_id;
			assoc_mgr_fill_in_user(acct_db_conn, &user_rec,
					       accounting_enforce, NULL, false);
			if (user_rec.default_wckey)
				job_desc->wckey = xstrdup_printf(
					"*%s", user_rec.default_wckey);
			else if (!(accounting_enforce &
				   ACCOUNTING_ENFORCE_WCKEYS))
				job_desc->wckey = xstrdup("*");
			else {
				error("Job didn't specify wckey and user "
				      "%d has no default.", job_desc->user_id);
				return ESLURM_INVALID_WCKEY;
			}
		} else if (job_desc->wckey) {
			slurmdb_wckey_rec_t wckey_rec, *wckey_ptr = NULL;

			memset(&wckey_rec, 0, sizeof(wckey_rec));
			wckey_rec.uid       = job_desc->user_id;
			wckey_rec.name      = job_desc->wckey;

			if (assoc_mgr_fill_in_wckey(acct_db_conn, &wckey_rec,
						    accounting_enforce,
						    &wckey_ptr, false)) {
				if (accounting_enforce &
				    ACCOUNTING_ENFORCE_WCKEYS) {
					error("%s: invalid wckey '%s' for "
					      "user %u.",
					      __func__, wckey_rec.name,
					      job_desc->user_id);
					return ESLURM_INVALID_WCKEY;
				}
			}
		} else if (accounting_enforce & ACCOUNTING_ENFORCE_WCKEYS) {
			/* This should never happen */
			info("%s: no wckey was given for job submit", __func__);
			return ESLURM_INVALID_WCKEY;
		}
	}

	job_ptr = _create_job_record(1, true);

	*job_rec_ptr = job_ptr;
	job_ptr->partition = xstrdup(job_desc->partition);
	if (job_desc->profile != ACCT_GATHER_PROFILE_NOT_SET)
		job_ptr->profile = job_desc->profile;

	if (job_desc->job_id != NO_VAL) {	/* already confirmed unique */
		job_ptr->job_id = job_desc->job_id;
	} else {
		error_code = _set_job_id(job_ptr);
		if (error_code)
			return error_code;
	}

	job_ptr->name = xstrdup(job_desc->name);
	job_ptr->wckey = xstrdup(job_desc->wckey);

	/* Since this is only used in the slurmctld, copy it now. */
	job_ptr->tres_req_cnt = job_desc->tres_req_cnt;
	job_desc->tres_req_cnt = NULL;
	set_job_tres_req_str(job_ptr, false);
	_add_job_hash(job_ptr);

	job_ptr->user_id    = (uid_t) job_desc->user_id;
	job_ptr->group_id   = (gid_t) job_desc->group_id;
	/* skip copy, just take ownership */
	job_ptr->id = job_desc->id;
	job_desc->id = NULL;

	job_state_set(job_ptr, JOB_PENDING);
	job_ptr->time_limit = job_desc->time_limit;
	job_ptr->deadline   = job_desc->deadline;
	if (job_desc->delay_boot == NO_VAL)
		job_ptr->delay_boot   = delay_boot;
	else
		job_ptr->delay_boot   = job_desc->delay_boot;
	if (job_desc->time_min != NO_VAL)
		job_ptr->time_min = job_desc->time_min;
	job_ptr->alloc_sid  = job_desc->alloc_sid;
	job_ptr->alloc_node = xstrdup(job_desc->alloc_node);
	job_ptr->account    = xstrdup(job_desc->account);
	job_ptr->batch_features = xstrdup(job_desc->batch_features);
	job_ptr->burst_buffer = xstrdup(job_desc->burst_buffer);
	job_ptr->network    = xstrdup(job_desc->network);
	job_ptr->resv_name  = xstrdup(job_desc->reservation);
	job_ptr->restart_cnt = job_desc->restart_cnt;
	job_ptr->comment    = xstrdup(job_desc->comment);
	job_ptr->extra = xstrdup(job_desc->extra);
	job_ptr->container = xstrdup(job_desc->container);
	job_ptr->container_id = xstrdup(job_desc->container_id);
	job_ptr->admin_comment = xstrdup(job_desc->admin_comment);

	if (job_desc->kill_on_node_fail != NO_VAL16)
		job_ptr->kill_on_node_fail = job_desc->kill_on_node_fail;

	job_ptr->resp_host = xstrdup(job_desc->resp_host);
	job_ptr->alloc_resp_port = job_desc->alloc_resp_port;
	job_ptr->other_port = job_desc->other_port;
	job_ptr->time_last_active = time(NULL);
	job_ptr->derived_ec = 0;

	job_ptr->licenses  = xstrdup(job_desc->licenses_tot);
	job_ptr->lic_req  = xstrdup(job_desc->licenses);
	job_ptr->mail_user = _get_mail_user(job_desc->mail_user,
					    job_ptr);
	if (job_desc->mail_type &&
	    (job_desc->mail_type != NO_VAL16)) {
		job_ptr->mail_type = job_desc->mail_type;
	}

	job_ptr->bit_flags = job_desc->bitflags;
	job_ptr->bit_flags &= ~TASKS_CHANGED;
	job_ptr->bit_flags &= ~BACKFILL_TEST;
	job_ptr->bit_flags &= ~BF_WHOLE_NODE_TEST;

	job_ptr->resv_port_cnt = job_desc->resv_port_cnt;
	if (job_desc->resv_port_cnt != NO_VAL16) {
		error_code = resv_port_check_job_request_cnt(job_ptr);
		if (error_code)
			return error_code;
	}

	job_ptr->spank_job_env = job_desc->spank_job_env;
	job_ptr->spank_job_env_size = job_desc->spank_job_env_size;
	job_desc->spank_job_env = (char **) NULL; /* nothing left to free */
	job_desc->spank_job_env_size = 0;         /* nothing left to free */
	job_ptr->mcs_label = xstrdup(job_desc->mcs_label);
	job_ptr->origin_cluster = xstrdup(job_desc->origin_cluster);

	job_ptr->cpus_per_tres = xstrdup(job_desc->cpus_per_tres);
	job_ptr->mem_per_tres = xstrdup(job_desc->mem_per_tres);
	job_ptr->tres_bind = xstrdup(job_desc->tres_bind);
	job_ptr->tres_freq = xstrdup(job_desc->tres_freq);
	job_ptr->tres_per_job = xstrdup(job_desc->tres_per_job);
	job_ptr->tres_per_node = xstrdup(job_desc->tres_per_node);
	job_ptr->tres_per_socket = xstrdup(job_desc->tres_per_socket);
	job_ptr->tres_per_task = xstrdup(job_desc->tres_per_task);

	if (job_desc->wait_all_nodes == NO_VAL16)
		job_ptr->wait_all_nodes = _default_wait_all_nodes(job_desc);
	else
		job_ptr->wait_all_nodes = job_desc->wait_all_nodes;
	job_ptr->warn_flags  = job_desc->warn_flags;
	job_ptr->warn_signal = job_desc->warn_signal;
	job_ptr->warn_time   = job_desc->warn_time;

	detail_ptr = job_ptr->details;
	detail_ptr->argc = job_desc->argc;
	detail_ptr->argv = job_desc->argv;
	job_desc->argv   = (char **) NULL; /* nothing left to free */
	job_desc->argc   = 0;		   /* nothing left to free */
	detail_ptr->acctg_freq = xstrdup(job_desc->acctg_freq);
	detail_ptr->cpu_bind_type = job_desc->cpu_bind_type;
	detail_ptr->cpu_bind   = xstrdup(job_desc->cpu_bind);
	detail_ptr->cpu_freq_gov = job_desc->cpu_freq_gov;
	detail_ptr->cpu_freq_max = job_desc->cpu_freq_max;
	detail_ptr->cpu_freq_min = job_desc->cpu_freq_min;
	detail_ptr->nice       = job_desc->nice;
	detail_ptr->open_mode  = job_desc->open_mode;
	detail_ptr->min_cpus   = job_desc->min_cpus;
	detail_ptr->orig_min_cpus   = job_desc->min_cpus;
	detail_ptr->max_cpus   = job_desc->max_cpus;
	detail_ptr->orig_max_cpus   = job_desc->max_cpus;
	detail_ptr->min_nodes  = job_desc->min_nodes;
	detail_ptr->max_nodes  = job_desc->max_nodes;
	detail_ptr->qos_req = xstrdup(job_desc->qos);
	if (job_desc->job_size_str && detail_ptr->max_nodes) {
		if (detail_ptr->max_nodes >= MAX_JOB_SIZE_BITMAP)
			return ESLURM_INVALID_NODE_COUNT;
		detail_ptr->job_size_bitmap =
			bit_alloc(detail_ptr->max_nodes + 1);
		if (bit_unfmt(detail_ptr->job_size_bitmap,
			      job_desc->job_size_str))
			FREE_NULL_BITMAP(detail_ptr->job_size_bitmap);
	} else {
		error_code = _unroll_min_max_node(job_ptr);
		if (error_code)
			return error_code;
	}
	detail_ptr->req_context = xstrdup(job_desc->req_context);
	detail_ptr->x11        = job_desc->x11;
	detail_ptr->x11_magic_cookie = xstrdup(job_desc->x11_magic_cookie);
	detail_ptr->x11_target = xstrdup(job_desc->x11_target);
	detail_ptr->x11_target_port = job_desc->x11_target_port;
	if (job_desc->req_nodes) {
		if ((job_desc->task_dist & SLURM_DIST_STATE_BASE) ==
		    SLURM_DIST_ARBITRARY) {
			detail_ptr->req_nodes = xstrdup(job_desc->req_nodes);
			if ((error_code =
			     job_record_calc_arbitrary_tpn(job_ptr)))
				return error_code;
		} else {
			detail_ptr->req_nodes =
				_copy_nodelist_no_dup(job_desc->req_nodes);
		}
		detail_ptr->req_node_bitmap = *req_bitmap;
		*req_bitmap = NULL;	/* Reused nothing left to free */
		detail_ptr->exc_node_bitmap = *exc_bitmap;
	}
	if (job_desc->exc_nodes) {
		detail_ptr->exc_nodes =
			_copy_nodelist_no_dup(job_desc->exc_nodes);
		detail_ptr->exc_node_bitmap = *exc_bitmap;
	}
	if (job_desc->exc_nodes || job_desc->req_nodes)
		*exc_bitmap = NULL;	/* Reused nothing left to free */
	detail_ptr->features = xstrdup(job_desc->features);
	detail_ptr->cluster_features = xstrdup(job_desc->cluster_features);
	detail_ptr->prefer = xstrdup(job_desc->prefer);
	if (job_desc->fed_siblings_viable) {
		job_ptr->fed_details = xmalloc(sizeof(job_fed_details_t));
		job_ptr->fed_details->siblings_viable =
			job_desc->fed_siblings_viable;
		update_job_fed_details(job_ptr);
	}
	if (job_desc->shared == JOB_SHARED_NONE) {
		detail_ptr->share_res  = 0;
		detail_ptr->whole_node = WHOLE_NODE_REQUIRED;
	} else if (job_desc->shared == JOB_SHARED_OK) {
		detail_ptr->share_res  = 1;
		detail_ptr->whole_node = 0;
	} else if (job_desc->shared == JOB_SHARED_USER) {
		detail_ptr->share_res  = NO_VAL8;
		detail_ptr->whole_node = WHOLE_NODE_USER;
	} else if (job_desc->shared == JOB_SHARED_MCS) {
		detail_ptr->share_res  = NO_VAL8;
		detail_ptr->whole_node = WHOLE_NODE_MCS;
	} else if (job_desc->shared == JOB_SHARED_TOPO) {
		detail_ptr->share_res  = NO_VAL8;
		detail_ptr->whole_node = WHOLE_TOPO;
	} else {
		detail_ptr->share_res  = NO_VAL8;
		detail_ptr->whole_node = 0;
	}
	if (job_desc->contiguous != NO_VAL16)
		detail_ptr->contiguous = job_desc->contiguous;
	if (slurm_conf.conf_flags & CONF_FLAG_ASRU)
		detail_ptr->core_spec = job_desc->core_spec;
	else
		detail_ptr->core_spec = NO_VAL16;
	if (detail_ptr->core_spec != NO_VAL16)
		detail_ptr->whole_node = WHOLE_NODE_REQUIRED;
	if (job_desc->task_dist != NO_VAL)
		detail_ptr->task_dist = job_desc->task_dist;
	if (job_desc->cpus_per_task == NO_VAL16) {
		detail_ptr->cpus_per_task = 1;
		detail_ptr->orig_cpus_per_task = NO_VAL16;
	} else {
		detail_ptr->cpus_per_task = MAX(job_desc->cpus_per_task, 1);
		detail_ptr->orig_cpus_per_task = detail_ptr->cpus_per_task;
	}
	if (job_desc->pn_min_cpus != NO_VAL16)
		detail_ptr->pn_min_cpus = job_desc->pn_min_cpus;
	if (job_desc->overcommit != NO_VAL8)
		detail_ptr->overcommit = job_desc->overcommit;
	if (job_desc->num_tasks != NO_VAL)
		detail_ptr->num_tasks = job_desc->num_tasks;
	if (job_desc->ntasks_per_node != NO_VAL16) {
		detail_ptr->ntasks_per_node = job_desc->ntasks_per_node;
		if ((detail_ptr->overcommit == 0) &&
		    (detail_ptr->num_tasks > 1)) {
			detail_ptr->pn_min_cpus =
				MAX(detail_ptr->pn_min_cpus,
				    (detail_ptr->cpus_per_task *
				     detail_ptr->ntasks_per_node));
		}
	}
	if (job_desc->ntasks_per_tres != NO_VAL16)
		detail_ptr->ntasks_per_tres = job_desc->ntasks_per_tres;
	detail_ptr->pn_min_cpus = MAX(detail_ptr->pn_min_cpus,
				      detail_ptr->cpus_per_task);
	detail_ptr->orig_pn_min_cpus = detail_ptr->pn_min_cpus;
	if (job_desc->reboot != NO_VAL16)
		job_ptr->reboot = MIN(job_desc->reboot, 1);
	else
		job_ptr->reboot = 0;
	if (job_desc->requeue != NO_VAL16)
		detail_ptr->requeue = MIN(job_desc->requeue, 1);
	else
		detail_ptr->requeue = slurm_conf.job_requeue;
	if (job_desc->pn_min_memory != NO_VAL64)
		detail_ptr->pn_min_memory = job_desc->pn_min_memory;
	detail_ptr->orig_pn_min_memory = detail_ptr->pn_min_memory;
	if (job_desc->pn_min_tmp_disk != NO_VAL)
		detail_ptr->pn_min_tmp_disk = job_desc->pn_min_tmp_disk;

	detail_ptr->oom_kill_step = job_desc->oom_kill_step;

	detail_ptr->segment_size = job_desc->segment_size;
	detail_ptr->std_err = xstrdup(job_desc->std_err);
	detail_ptr->std_in = xstrdup(job_desc->std_in);
	detail_ptr->std_out = xstrdup(job_desc->std_out);
	detail_ptr->submit_line = xstrdup(job_desc->submit_line);
	detail_ptr->work_dir = xstrdup(job_desc->work_dir);
	if (job_desc->begin_time > time(NULL))
		detail_ptr->begin_time = job_desc->begin_time;
	job_ptr->select_jobinfo = select_g_select_jobinfo_alloc();

	select_g_select_jobinfo_set(job_ptr->select_jobinfo,
				    SELECT_JOBDATA_NETWORK,
				    job_ptr->network);

	job_ptr->clusters = xstrdup(job_desc->clusters);

	/*
	 * The priority needs to be set after this since we don't have
	 * an association rec yet
	 */
	detail_ptr->mc_ptr = _set_multi_core_data(job_desc);

	if ((job_ptr->bit_flags & SPREAD_JOB) && (detail_ptr->max_nodes == 0) &&
	    (detail_ptr->num_tasks != 0)) {
		if (detail_ptr->min_nodes == 0)
			detail_ptr->min_nodes = 1;
		detail_ptr->max_nodes = MIN(active_node_record_count,
					    detail_ptr->num_tasks);
	}

	job_ptr->selinux_context = xstrdup(job_desc->selinux_context);

	return SLURM_SUCCESS;
}

/*
 * _copy_nodelist_no_dup - Take a node_list string and convert it to an
 *	expression without duplicate names. For example, we want to convert
 *	a users request for nodes "lx1,lx2,lx1,lx3" to "lx[1-3]"
 * node_list IN - string describing a list of nodes
 * RET a compact node expression, must be xfreed by the user
 */
static char *_copy_nodelist_no_dup(char *node_list)
{
	char *buf;

	hostlist_t *hl = hostlist_create(node_list);
	if (hl == NULL)
		return NULL;
	hostlist_uniq(hl);
	buf = hostlist_ranged_string_xmalloc(hl);
	hostlist_destroy(hl);

	return buf;
}

/* Return memory on the first node in the identified partition */
static uint64_t _mem_per_node_part(part_record_t *part_ptr)
{
	int node_inx = -1;
	node_record_t *node_ptr;

	if (!part_ptr)
		return 0;

	if (part_ptr->node_bitmap)
		node_inx = bit_ffs(part_ptr->node_bitmap);
	if (node_inx >= 0) {
		node_ptr = node_record_table_ptr[node_inx];
		return (node_ptr->config_ptr->real_memory -
			node_ptr->mem_spec_limit);
	}
	return 0;
}

/*
 * Test if this job exceeds any of MaxMemPer[CPU|Node] limits and potentially
 * adjust mem / cpu ratios.
 *
 * NOTE: This function is also called with a dummy job_desc_msg_t from
 * job_limits_check(), if there is any new check added here you may also have to
 * add that parameter to the job_desc_msg_t in that function.
 */
static bool _valid_pn_min_mem(job_desc_msg_t *job_desc_msg,
			      part_record_t *part_ptr)
{
	uint64_t job_mem_limit = job_desc_msg->pn_min_memory;
	uint64_t sys_mem_limit;
	uint16_t cpus_per_node;

	if (part_ptr && part_ptr->max_mem_per_cpu)
		sys_mem_limit = part_ptr->max_mem_per_cpu;
	else
		sys_mem_limit = slurm_conf.max_mem_per_cpu;

	if ((sys_mem_limit == 0) || (sys_mem_limit == MEM_PER_CPU))
		return true;

	if ((job_mem_limit & MEM_PER_CPU) && (sys_mem_limit & MEM_PER_CPU)) {
		uint64_t mem_ratio;
		job_mem_limit &= (~MEM_PER_CPU);
		sys_mem_limit &= (~MEM_PER_CPU);
		if (job_mem_limit <= sys_mem_limit)
			return true;
		mem_ratio = ROUNDUP(job_mem_limit, sys_mem_limit);
		debug("JobId=%u: increasing cpus_per_task and decreasing mem_per_cpu by factor of %"PRIu64" based upon mem_per_cpu limits",
		      job_desc_msg->job_id, mem_ratio);
		if (job_desc_msg->cpus_per_task == NO_VAL16)
			job_desc_msg->cpus_per_task = mem_ratio;
		else
			job_desc_msg->cpus_per_task *= mem_ratio;

		/* Update tres_per_task, but not if it wasn't set before */
		if (job_desc_msg->bitflags & JOB_CPUS_SET)
			slurm_option_update_tres_per_task(
				job_desc_msg->cpus_per_task, "cpu",
				&job_desc_msg->tres_per_task);

		job_desc_msg->pn_min_memory = ((job_mem_limit + mem_ratio - 1) /
					       mem_ratio) | MEM_PER_CPU;
		if ((job_desc_msg->num_tasks != NO_VAL) &&
		    (job_desc_msg->num_tasks != 0) &&
		    (job_desc_msg->min_cpus  != NO_VAL)) {
			job_desc_msg->min_cpus =
				job_desc_msg->num_tasks *
				job_desc_msg->cpus_per_task;

			if ((job_desc_msg->max_cpus != NO_VAL) &&
			    (job_desc_msg->max_cpus < job_desc_msg->min_cpus)) {
				job_desc_msg->max_cpus = job_desc_msg->min_cpus;
			}
		} else {
			job_desc_msg->pn_min_cpus = job_desc_msg->cpus_per_task;
		}
		return true;
	}

	if (job_mem_limit == 0)
		job_mem_limit = _mem_per_node_part(part_ptr);

	if (((job_mem_limit & MEM_PER_CPU) == 0) &&
	    ((sys_mem_limit & MEM_PER_CPU) == 0)) {
		if (job_mem_limit <= sys_mem_limit)
			return true;
		debug2("JobId=%u mem=%"PRIu64"M > MaxMemPerNode=%"PRIu64"M in partition %s",
		       job_desc_msg->job_id, job_mem_limit, sys_mem_limit,
		       (part_ptr && part_ptr->name) ? part_ptr->name : "N/A");
		return false;
	}

	/* Job and system have different memory limit forms (i.e. one is a
	 * per-job and the other is per-node). Covert them both to per-node
	 * values for comparison. */
	if (part_ptr && (!part_ptr->max_share || !job_desc_msg->shared)) {
		/* Whole node allocation */
		cpus_per_node = part_ptr->max_cpu_cnt;
	} else {
		if ((job_desc_msg->ntasks_per_node != NO_VAL16) &&
		    (job_desc_msg->ntasks_per_node != 0))
			cpus_per_node = job_desc_msg->ntasks_per_node;
		else
			cpus_per_node = 1;

		if ((job_desc_msg->num_tasks != NO_VAL) &&
		    (job_desc_msg->num_tasks != 0)     &&
		    (job_desc_msg->max_nodes != NO_VAL) &&
		    (job_desc_msg->max_nodes != 0)) {
			cpus_per_node = MAX(cpus_per_node,
					    ((job_desc_msg->num_tasks +
					      job_desc_msg->max_nodes - 1) /
					     job_desc_msg->max_nodes));
		}

		if ((job_desc_msg->cpus_per_task != NO_VAL16) &&
		    (job_desc_msg->cpus_per_task != 0))
			cpus_per_node *= job_desc_msg->cpus_per_task;

		if ((job_desc_msg->pn_min_cpus != NO_VAL16) &&
		    (job_desc_msg->pn_min_cpus > cpus_per_node))
			cpus_per_node = job_desc_msg->pn_min_cpus;
	}

	if (job_mem_limit & MEM_PER_CPU) {
		/* Job has per-CPU memory limit, system has per-node limit */
		job_mem_limit &= (~MEM_PER_CPU);
		job_mem_limit *= cpus_per_node;
	} else {
		/* Job has per-node memory limit, system has per-CPU limit */
		uint32_t min_cpus;
		sys_mem_limit &= (~MEM_PER_CPU);
		min_cpus = (job_mem_limit + sys_mem_limit - 1) / sys_mem_limit;

		if ((job_desc_msg->pn_min_cpus == NO_VAL16) ||
		    (job_desc_msg->pn_min_cpus < min_cpus)) {
			job_desc_msg->pn_min_cpus = min_cpus;
			if (min_cpus > job_desc_msg->min_cpus) {
				job_desc_msg->min_cpus = min_cpus;
				job_desc_msg->max_cpus =
					MAX(min_cpus, job_desc_msg->max_cpus);
			}
			cpus_per_node = MAX(cpus_per_node, min_cpus);
			if (job_desc_msg->ntasks_per_node != NO_VAL16) {
				job_desc_msg->cpus_per_task =
					(job_desc_msg->pn_min_cpus +
					 job_desc_msg->ntasks_per_node - 1) /
					job_desc_msg->ntasks_per_node;
				job_desc_msg->pn_min_cpus =
					MAX(job_desc_msg->cpus_per_task *
					    job_desc_msg->ntasks_per_node,
					    job_desc_msg->pn_min_cpus);
			} else if (job_desc_msg->num_tasks &&
				   (job_desc_msg->num_tasks != NO_VAL) &&
				   job_desc_msg->min_nodes &&
				   (job_desc_msg->min_nodes != NO_VAL)) {
				/*
				 * Calculate a new value of cpus/task given the
				 * current nodes and tasks values:
				 * CPUs/Task = (min_cpus_per_node * min_nodes) / num_tasks
				 */
				uint32_t cpus =
					min_cpus * job_desc_msg->min_nodes;
				job_desc_msg->cpus_per_task =
					ROUNDUP(cpus, job_desc_msg->num_tasks);
				/*
				 * Recalculate pn_min_cpus based on the new
				 * CPUs/task. This formula aims to get
				 * an allocation with the least amount of
				 * CPUs combining all the nodes from the job.
				 */
				min_cpus = (job_desc_msg->cpus_per_task *
					    job_desc_msg->num_tasks) /
					   job_desc_msg->min_nodes;
				job_desc_msg->pn_min_cpus = min_cpus;
				job_desc_msg->min_cpus =
					MAX(min_cpus,
					    job_desc_msg->pn_min_cpus);
			} else if (!job_desc_msg->num_tasks) {
				/*
				 * The job did not request any amount of tasks
				 * explicitly. Assuming 1 per node.
				 */
				job_desc_msg->cpus_per_task =
					MAX(job_desc_msg->pn_min_cpus,
					    job_desc_msg->cpus_per_task);
			}
			debug("JobId=%u: Setting job's pn_min_cpus to %u due to memory limit",
			      job_desc_msg->job_id,
			      job_desc_msg->pn_min_cpus);
		}
		sys_mem_limit *= cpus_per_node;
	}

	if (job_mem_limit <= sys_mem_limit)
		return true;

	debug2("JobId=%u mem=%"PRIu64"M > MaxMemPer%s=%"PRIu64"M in partition:%s",
	       job_desc_msg->job_id, job_mem_limit,
	       (job_mem_limit & MEM_PER_CPU) ? "CPU" : "Node", sys_mem_limit,
	       (part_ptr && part_ptr->name) ? part_ptr->name : "N/A");

	return false;
}

/*
 * Increment time limit of one job record for node configuration.
 */
static void _job_time_limit_incr(job_record_t *job_ptr, uint32_t boot_job_id)
{
	time_t delta_t, now = time(NULL);

	delta_t = difftime(now, job_ptr->start_time);
	if ((job_ptr->job_id != boot_job_id) && !IS_JOB_CONFIGURING(job_ptr))
		job_ptr->tot_sus_time = delta_t;

	if ((job_ptr->time_limit != INFINITE) &&
	    ((job_ptr->job_id == boot_job_id) || (delta_t != 0))) {
		if (delta_t && !IS_JOB_CONFIGURING(job_ptr)) {
			verbose("Extending %pJ time limit by %u secs for configuration",
				job_ptr, (uint32_t) delta_t);
		}
		job_ptr->end_time = now + (job_ptr->time_limit * 60);
		job_ptr->end_time_exp = job_ptr->end_time;
	}
}

/*
 * Increment time limit for all components of a hetjob for node configuration.
 * job_ptr IN - pointer to job record for which configuration is complete
 * boot_job_id - job ID of record with newly powered up node or 0
 */
static void _het_job_time_limit_incr(job_record_t *job_ptr,
				     uint32_t boot_job_id)
{
	job_record_t *het_job_leader, *het_job;
	list_itr_t *iter;

	if (!job_ptr->het_job_id) {
		_job_time_limit_incr(job_ptr, boot_job_id);
		return;
	}

	het_job_leader = find_job_record(job_ptr->het_job_id);
	if (!het_job_leader) {
		error("%s: Hetjob leader %pJ not found",
		      __func__, job_ptr);
		_job_time_limit_incr(job_ptr, boot_job_id);
		return;
	}
	if (!het_job_leader->het_job_list) {
		error("%s: Hetjob leader %pJ job list is NULL",
		      __func__, job_ptr);
		_job_time_limit_incr(job_ptr, boot_job_id);
		return;
	}

	iter = list_iterator_create(het_job_leader->het_job_list);
	while ((het_job = list_next(iter))) {
		_job_time_limit_incr(het_job, boot_job_id);
	}
	list_iterator_destroy(iter);
}

/* Clear job's CONFIGURING flag and advance end time as needed */
extern void job_config_fini(job_record_t *job_ptr)
{
	time_t now = time(NULL);

	last_job_update = now;
	job_state_unset_flag(job_ptr, JOB_CONFIGURING);
	if (IS_JOB_POWER_UP_NODE(job_ptr)) {
		info("Resetting %pJ start time for node power up", job_ptr);
		job_state_unset_flag(job_ptr, JOB_POWER_UP_NODE);
		job_ptr->start_time = now;
		_het_job_time_limit_incr(job_ptr, job_ptr->job_id);
		jobacct_storage_g_job_start(acct_db_conn, job_ptr);
	} else {
		_het_job_time_limit_incr(job_ptr, 0);
	}

	if (job_ptr->alias_list && !xstrcmp(job_ptr->alias_list, "TBD"))
		set_job_alias_list(job_ptr);

	/*
	 * Request asynchronous launch of a prolog for a non-batch job.
	 * PROLOG_FLAG_CONTAIN also turns on PROLOG_FLAG_ALLOC.
	 */
	if (slurm_conf.prolog_flags & PROLOG_FLAG_ALLOC)
		launch_prolog(job_ptr);
}

/*
 * Determine of the nodes are ready to run a job
 * RET true if ready
 */
extern bool test_job_nodes_ready(job_record_t *job_ptr)
{
	if (IS_JOB_PENDING(job_ptr))
		return false;
	if (!job_ptr->node_bitmap)	/* Revoked allocation */
		return true;
	if (bit_overlap_any(job_ptr->node_bitmap, power_down_node_bitmap))
		return false;

	if (!job_ptr->batch_flag ||
	    job_ptr->batch_features ||
	    job_ptr->wait_all_nodes || job_ptr->burst_buffer) {
		/* Make sure all nodes ready to start job */
		if ((select_g_job_ready(job_ptr) & READY_NODE_STATE) == 0)
			return false;
	} else if (job_ptr->batch_flag) {

#ifdef HAVE_FRONT_END
		/* Make sure frontend node is ready to start batch job */
		front_end_record_t *front_end_ptr =
			find_front_end_record(job_ptr->batch_host);
		if (!front_end_ptr ||
		    IS_NODE_POWERED_DOWN(front_end_ptr) ||
		    IS_NODE_POWERING_UP(front_end_ptr)) {
			return false;
		}
#else
		/* Make sure first node is ready to start batch job */
		node_record_t *node_ptr =
			find_node_record(job_ptr->batch_host);
		if (!node_ptr ||
		    IS_NODE_POWERED_DOWN(node_ptr) ||
		    IS_NODE_POWERING_UP(node_ptr)) {
			return false;
		}
#endif
	}

	return true;
}

/*
 * For non-hetjob, return true if this job is configuring.
 * For hetjob, return true if any component of the job is configuring.
 */
static bool _het_job_configuring_test(job_record_t *job_ptr)
{
	job_record_t *het_job_leader, *het_job;
	list_itr_t *iter;
	bool result = false;

	if (IS_JOB_CONFIGURING(job_ptr))
		return true;
	if (!job_ptr->het_job_id)
		return false;

	het_job_leader = find_job_record(job_ptr->het_job_id);
	if (!het_job_leader) {
		error("%s: Hetjob leader %pJ not found", __func__, job_ptr);
		return false;
	}
	if (!het_job_leader->het_job_list) {
		error("%s: Hetjob leader %pJ job list is NULL",
		      __func__, job_ptr);
		return false;
	}

	iter = list_iterator_create(het_job_leader->het_job_list);
	while ((het_job = list_next(iter))) {
		if (IS_JOB_CONFIGURING(het_job)) {
			result = true;
			break;
		}
	}
	list_iterator_destroy(iter);

	return result;
}

/*
 * job_time_limit - terminate jobs which have exceeded their time limit
 * global: job_list - pointer global job list
 *	last_job_update - time of last job table update
 */
void job_time_limit(void)
{
	list_itr_t *job_iterator;
	job_record_t *job_ptr;
	time_t now = time(NULL);
	time_t old = now - ((slurm_conf.inactive_limit * 4 / 3) +
	                    slurm_conf.msg_timeout + 1);
	time_t over_run;
	uint16_t over_time_limit;
	uint8_t prolog;
	int job_test_count = 0;
	uint32_t resv_over_run = slurm_conf.resv_over_run;

	xassert(verify_lock(JOB_LOCK, WRITE_LOCK));

	if (resv_over_run == INFINITE16)
		resv_over_run = YEAR_SECONDS;
	else
		resv_over_run *= 60;

	/*
	 * locks same as in _slurmctld_background() (The only current place this
	 * is called).
	 */
	slurmctld_lock_t job_write_lock = {
		READ_LOCK, WRITE_LOCK, WRITE_LOCK, READ_LOCK, READ_LOCK };
	DEF_TIMERS;

	job_iterator = list_iterator_create(job_list);
	START_TIMER;
	while ((job_ptr = list_next(job_iterator))) {
		xassert (job_ptr->magic == JOB_MAGIC);
		job_test_count++;

		if (job_ptr->details)
			prolog = job_ptr->details->prolog_running;
		else
			prolog = 0;
		if ((prolog == 0) && IS_JOB_CONFIGURING(job_ptr) &&
		    test_job_nodes_ready(job_ptr)) {
			info("%s: Configuration for %pJ complete",
			     __func__, job_ptr);
			job_config_fini(job_ptr);
			if (job_ptr->batch_flag)
				launch_job(job_ptr);
		}

		/*
		 * Features have been changed on some node, make job eligiable
		 * to run and test to see if it can run now
		 */
		if (node_features_updated &&
		    (job_ptr->state_reason == FAIL_BAD_CONSTRAINTS) &&
		    IS_JOB_PENDING(job_ptr) && (job_ptr->priority == 0)) {
			job_ptr->state_reason = WAIT_NO_REASON;
			set_job_prio(job_ptr);
			last_job_update = now;
		}

		/* Don't enforce time limits for configuring hetjobs */
		if (_het_job_configuring_test(job_ptr))
			continue;

		/*
		 * Only running jobs can be killed due to timeout. Do not kill
		 * suspended jobs due to timeout.
		 */
		if (!IS_JOB_RUNNING(job_ptr))
			continue;

		/*
		 * everything above here is considered "quick", and skips the
		 * timeout at the bottom of the loop by using a continue.
		 * everything below is considered "slow", and needs to jump to
		 * time_check before the next job is tested
		 */
		if (job_ptr->preempt_time) {
			(void)slurm_job_preempt(job_ptr, NULL,
						slurm_job_preempt_mode(job_ptr),
						false);
			goto time_check;
		}

		if (slurm_conf.inactive_limit && (job_ptr->batch_flag == 0) &&
		    (job_ptr->time_last_active <= old) &&
		    (job_ptr->other_port) &&
		    (job_ptr->part_ptr) &&
		    (!(job_ptr->part_ptr->flags & PART_FLAG_ROOT_ONLY))) {
			/* job inactive, kill it */
			info("%s: inactivity time limit reached for %pJ",
			     __func__, job_ptr);
			_job_timed_out(job_ptr, false);
			job_ptr->state_reason = FAIL_INACTIVE_LIMIT;
			xfree(job_ptr->state_desc);
			goto time_check;
		}
		if (job_ptr->time_limit != INFINITE) {
			send_job_warn_signal(job_ptr, false);
			if ((job_ptr->mail_type & MAIL_JOB_TIME100) &&
			    (now >= job_ptr->end_time)) {
				job_ptr->mail_type &= (~MAIL_JOB_TIME100);
				mail_job_info(job_ptr, MAIL_JOB_TIME100);
			}
			if ((job_ptr->mail_type & MAIL_JOB_TIME90) &&
			    (now + (job_ptr->time_limit * 60 * 0.1) >=
			     job_ptr->end_time)) {
				job_ptr->mail_type &= (~MAIL_JOB_TIME90);
				mail_job_info(job_ptr, MAIL_JOB_TIME90);
			}
			if ((job_ptr->mail_type & MAIL_JOB_TIME80) &&
			    (now + (job_ptr->time_limit * 60 * 0.2) >=
			     job_ptr->end_time)) {
				job_ptr->mail_type &= (~MAIL_JOB_TIME80);
				mail_job_info(job_ptr, MAIL_JOB_TIME80);
			}
			if ((job_ptr->mail_type & MAIL_JOB_TIME50) &&
			    (now + (job_ptr->time_limit * 60 * 0.5) >=
			     job_ptr->end_time)) {
				job_ptr->mail_type &= (~MAIL_JOB_TIME50);
				mail_job_info(job_ptr, MAIL_JOB_TIME50);
			}

			if (job_ptr->part_ptr &&
			    (job_ptr->part_ptr->over_time_limit != NO_VAL16)) {
				over_time_limit =
					job_ptr->part_ptr->over_time_limit;
			} else {
				over_time_limit = slurm_conf.over_time_limit;
			}
			if (over_time_limit == INFINITE16)
				over_run = now - YEAR_SECONDS;
			else
				over_run = now - (over_time_limit  * 60);
			if (job_ptr->end_time <= over_run) {
				last_job_update = now;
				info("Time limit exhausted for %pJ", job_ptr);
				_job_timed_out(job_ptr, false);
				job_ptr->state_reason = FAIL_TIMEOUT;
				xfree(job_ptr->state_desc);
				goto time_check;
			}
		}

		if (job_ptr->resv_ptr &&
		    !(job_ptr->resv_ptr->flags & RESERVE_FLAG_FLEX) &&
		    (job_ptr->resv_ptr->end_time + resv_over_run) < time(NULL)){
			last_job_update = now;
			info("Reservation ended for %pJ", job_ptr);
			xfree(job_ptr->state_desc);
			xstrfmtcat(job_ptr->state_desc, "Reservation %s, which this job was running under, has ended",
				   job_ptr->resv_ptr->name);
			_job_timed_out(job_ptr, false);
			job_ptr->state_reason = FAIL_TIMEOUT;
			xfree(job_ptr->state_desc);
			goto time_check;
		}

		/*
		 * check if any individual job steps have exceeded
		 * their time limit
		 */
		list_for_each(job_ptr->step_list, check_job_step_time_limit,
			      &now);

		acct_policy_job_time_out(job_ptr);

		if (job_ptr->state_reason == FAIL_TIMEOUT) {
			last_job_update = now;
			_job_timed_out(job_ptr, false);
			xfree(job_ptr->state_desc);
			goto time_check;
		}

		/* Give srun command warning message about pending timeout */
		if (job_ptr->end_time <= (now + PERIODIC_TIMEOUT * 2))
			srun_timeout (job_ptr);

		/*
		 * _job_timed_out() and other calls can take a long time on
		 * some platforms. This loop is holding the job_write lock;
		 * if a lot of jobs need to be timed out within the same cycle
		 * this stalls other threads from running and causes
		 * communication issues within the cluster.
		 *
		 * This test happens last, as job_ptr may be pointing to a job
		 * that would be deleted by a separate thread when the job_write
		 * lock is released. However, list_next itself is thread safe,
		 * and can be used again once the locks are reacquired.
		 * list_peek_next is used in the unlikely event the timer has
		 * expired just as the end of the job_list is reached.
		 */
	time_check:
		/* Use a hard-coded 3 second timeout, with a 1 second sleep. */
		if (slurm_delta_tv(&tv1) >= 3000000 &&
		    list_peek_next(job_iterator)) {
			END_TIMER;
			debug("%s: yielding locks after testing %d jobs, %s",
			      __func__, job_test_count, TIME_STR);
			unlock_slurmctld(job_write_lock);
			usleep(1000000);
			lock_slurmctld(job_write_lock);
			START_TIMER;
			job_test_count = 0;
		}
	}
	list_iterator_destroy(job_iterator);
	node_features_updated = false;
}

extern void job_set_req_tres(job_record_t *job_ptr, bool assoc_mgr_locked)
{
	uint32_t cpu_cnt = 0, node_cnt = 0;
	uint64_t mem_cnt = 0;
	uint16_t sockets_per_node;
	uint32_t num_tasks = 1; /* Default to 1 if it's not set */
	assoc_mgr_lock_t locks = { .tres = READ_LOCK };

	xassert(verify_lock(JOB_LOCK, WRITE_LOCK));

	xfree(job_ptr->tres_req_str);
	xfree(job_ptr->tres_fmt_req_str);
	xfree(job_ptr->tres_req_cnt);

	if (!assoc_mgr_locked)
		assoc_mgr_lock(&locks);

	job_ptr->tres_req_cnt = xcalloc(g_tres_count, sizeof(uint64_t));

	if (job_ptr->details) {
		node_cnt = job_ptr->details->min_nodes;
		cpu_cnt = job_ptr->details->min_cpus;
		if (job_ptr->details->pn_min_memory)
			mem_cnt = job_ptr->details->pn_min_memory;
		num_tasks = job_ptr->details->num_tasks;
	}

	/* if this is set just override */
	if (job_ptr->total_cpus)
		cpu_cnt = job_ptr->total_cpus;

	if (job_ptr->node_cnt)
		node_cnt = job_ptr->node_cnt;

	job_ptr->tres_req_cnt[TRES_ARRAY_NODE] = (uint64_t)node_cnt;
	job_ptr->tres_req_cnt[TRES_ARRAY_CPU] = (uint64_t)cpu_cnt;
	sockets_per_node = job_get_sockets_per_node(job_ptr);
	job_ptr->tres_req_cnt[TRES_ARRAY_MEM] =
		job_get_tres_mem(job_ptr->job_resrcs,
				 mem_cnt, cpu_cnt,
				 node_cnt,
				 job_ptr->part_ptr,
				 job_ptr->gres_list_req,
				 (job_ptr->bit_flags & JOB_MEM_SET),
				 sockets_per_node,
				 num_tasks);

	license_set_job_tres_cnt(job_ptr->license_list,
				 job_ptr->tres_req_cnt,
				 true);

	/* FIXME: this assumes that all nodes have equal TRES */
	gres_stepmgr_set_job_tres_cnt(
		job_ptr->gres_list_req,
		node_cnt,
		job_ptr->tres_req_cnt,
		true);

	bb_g_job_set_tres_cnt(job_ptr,
			      job_ptr->tres_req_cnt,
			      true);

	/*
	 * Do this last as it calculates off of everything else.
	 * Don't use calc_job_billable_tres() as it relies on allocated tres
	 * If the partition was destroyed the part_ptr will be NULL.  As this
	 * could be run on already finished jobs running in the assoc mgr
	 * cache.
	 */
	if (job_ptr->part_ptr)
		job_ptr->tres_req_cnt[TRES_ARRAY_BILLING] =
			assoc_mgr_tres_weighted(
				job_ptr->tres_req_cnt,
				job_ptr->part_ptr->billing_weights,
				slurm_conf.priority_flags, true);

	/* now that the array is filled lets make the string from it */
	set_job_tres_req_str(job_ptr, true);

	if (!assoc_mgr_locked)
		assoc_mgr_unlock(&locks);
}

extern void job_set_alloc_tres(job_record_t *job_ptr, bool assoc_mgr_locked)
{
	uint32_t alloc_nodes = 0;
	assoc_mgr_lock_t locks = { .tres = READ_LOCK };

	xfree(job_ptr->tres_alloc_str);
	xfree(job_ptr->tres_alloc_cnt);
	xfree(job_ptr->tres_fmt_alloc_str);

	/*
	 * We only need to do this on non-pending jobs.
	 * Requeued jobs are marked as PENDING|COMPLETING until the epilog is
	 * finished so we still need the alloc tres until then.
	 */
	if (IS_JOB_PENDING(job_ptr) && !IS_JOB_COMPLETING(job_ptr))
		return;

	if (!assoc_mgr_locked)
		assoc_mgr_lock(&locks);

	job_ptr->tres_alloc_cnt = xcalloc(slurmctld_tres_cnt, sizeof(uint64_t));

	job_ptr->tres_alloc_cnt[TRES_ARRAY_CPU] = (uint64_t)job_ptr->total_cpus;

	alloc_nodes = job_ptr->node_cnt;
	job_ptr->tres_alloc_cnt[TRES_ARRAY_NODE] = (uint64_t)alloc_nodes;
	job_ptr->tres_alloc_cnt[TRES_ARRAY_MEM] =
		job_get_tres_mem(job_ptr->job_resrcs,
				 job_ptr->details->pn_min_memory,
				 job_ptr->tres_alloc_cnt[TRES_ARRAY_CPU],
				 job_ptr->tres_alloc_cnt[TRES_ARRAY_NODE],
				 job_ptr->part_ptr,
				 job_ptr->gres_list_req,
				 job_ptr->bit_flags & JOB_MEM_SET,
				 job_get_sockets_per_node(job_ptr),
				 job_ptr->details->num_tasks);

	job_ptr->tres_alloc_cnt[TRES_ARRAY_ENERGY] = NO_VAL64;

	license_set_job_tres_cnt(job_ptr->license_list,
				 job_ptr->tres_alloc_cnt,
				 true);
	gres_stepmgr_set_job_tres_cnt(
		job_ptr->gres_list_alloc,
		alloc_nodes,
		job_ptr->tres_alloc_cnt,
		true);

	bb_g_job_set_tres_cnt(job_ptr,
			      job_ptr->tres_alloc_cnt,
			      true);

	/* Do this last as it calculates off of everything else. */
	job_ptr->tres_alloc_cnt[TRES_ARRAY_BILLING] =
		calc_job_billable_tres(job_ptr, job_ptr->start_time, true);

	/* now that the array is filled lets make the string from it */
	assoc_mgr_set_job_tres_alloc_str(job_ptr, true);

	if (!assoc_mgr_locked)
		assoc_mgr_unlock(&locks);
}

/*
 * job_update_tres_cnt - when job is completing remove allocated tres
 *                      from count.
 * IN/OUT job_ptr - job structure to be updated
 * IN node_inx    - node bit that is finished with job.
 * RET SLURM_SUCCES on success SLURM_ERROR on cpu_cnt underflow
 */
extern int job_update_tres_cnt(job_record_t *job_ptr, int node_inx)
{
	int cpu_cnt, offset = -1, rc = SLURM_SUCCESS;

	xassert(job_ptr);

	if (job_ptr->details->whole_node & WHOLE_NODE_REQUIRED) {
		/*
		 * Since we are allocating whole nodes don't rely on
		 * the job_resrcs since it could be less because the
		 * node could of only used 1 thread per core.
		 */
		node_record_t *node_ptr =
			node_record_table_ptr[node_inx];
		cpu_cnt = node_ptr->config_ptr->cpus;
	} else {
		if ((offset = job_resources_node_inx_to_cpu_inx(
			     job_ptr->job_resrcs, node_inx)) < 0) {
			error("%s: problem getting offset of %pJ",
			      __func__, job_ptr);
			job_ptr->cpu_cnt = 0;
			return SLURM_ERROR;
		}

		cpu_cnt = job_ptr->job_resrcs->cpus[offset];
	}
	if (cpu_cnt > job_ptr->cpu_cnt) {
		error("%s: cpu_cnt underflow (%d > %u) on %pJ", __func__,
		      cpu_cnt, job_ptr->cpu_cnt, job_ptr);
		job_ptr->cpu_cnt = 0;
		rc = SLURM_ERROR;
	} else
		job_ptr->cpu_cnt -= cpu_cnt;

	if (IS_JOB_RESIZING(job_ptr)) {
		if (cpu_cnt > job_ptr->total_cpus) {
			error("%s: total_cpus underflow on %pJ",
			      __func__, job_ptr);
			job_ptr->total_cpus = 0;
			rc = SLURM_ERROR;
		} else
			job_ptr->total_cpus -= cpu_cnt;

		job_set_alloc_tres(job_ptr, false);
	}
	return rc;
}

/* Terminate a job that has exhausted its time limit */
static void _job_timed_out(job_record_t *job_ptr, bool preempted)
{
	xassert(job_ptr);

	srun_timeout(job_ptr);
	if (job_ptr->details) {
		time_t now      = time(NULL);
		job_ptr->end_time           = now;
		job_ptr->time_last_active   = now;
		if (!job_ptr->preempt_time)
			job_state_set(job_ptr, (JOB_TIMEOUT | JOB_COMPLETING));
		build_cg_bitmap(job_ptr);
		job_completion_logger(job_ptr, false);
		deallocate_nodes(job_ptr, !preempted, false, preempted);
	} else
		job_signal(job_ptr, SIGKILL, 0, 0, false);
}

/* _validate_job_desc - validate that a job descriptor for job submit or
 *	allocate has valid data, set values to defaults as required
 * IN/OUT job_desc_msg - pointer to job descriptor, modified as needed
 * IN allocate - if clear job to be queued, if set allocate for user now
 * IN submit_uid - who request originated
 */
static int _validate_job_desc(job_desc_msg_t *job_desc_msg, int allocate,
			      bool cron, uid_t submit_uid,
			      part_record_t *part_ptr, list_t *part_list)
{
	if ((job_desc_msg->min_cpus  == NO_VAL) &&
	    (job_desc_msg->min_nodes == NO_VAL) &&
	    (job_desc_msg->req_nodes == NULL)) {
		info("%s: job specified no min_cpus, min_nodes or req_nodes",
		     __func__);
		return ESLURM_JOB_MISSING_SIZE_SPECIFICATION;
	}
	if ((allocate == SLURM_CREATE_JOB_FLAG_NO_ALLOCATE_0) &&
	    (job_desc_msg->script == NULL)) {
		info("%s: job failed to specify Script", __func__);
		return ESLURM_JOB_SCRIPT_MISSING;
	}
	if (job_desc_msg->script && job_desc_msg->x11) {
		info("%s: batch job cannot use X11 forwarding", __func__);
		return ESLURM_X11_NOT_AVAIL;
	}
	if (job_desc_msg->user_id == NO_VAL) {
		info("%s: job failed to specify User", __func__);
		return ESLURM_USER_ID_MISSING;
	}
	if ( job_desc_msg->group_id == NO_VAL ) {
		debug("%s: job failed to specify group", __func__);
		return ESLURM_GROUP_ID_MISSING;
	}
	if (!job_desc_msg->container_id && !job_desc_msg->container &&
	    (!job_desc_msg->work_dir || !job_desc_msg->work_dir[0])) {
		debug("%s: job working directory has to be set", __func__);
		return ESLURM_MISSING_WORK_DIR;
	}
	if ((job_desc_msg->warn_flags & KILL_JOB_RESV) &&
	    (slurm_conf.preempt_mode == PREEMPT_MODE_OFF)) {
		debug("%s: job specified \"R:\" option of --signal, which is incompatible with PreemptMode=OFF",
		     __func__);
		return ESLURM_PREEMPTION_REQUIRED;
	}
	if (job_desc_msg->contiguous == NO_VAL16)
		job_desc_msg->contiguous = 0;

	if (job_desc_msg->task_dist == NO_VAL) {
		/* not typically set by salloc or sbatch */
		job_desc_msg->task_dist = SLURM_DIST_CYCLIC;
	}
	if (job_desc_msg->plane_size == NO_VAL16)
		job_desc_msg->plane_size = 0;

	if (job_desc_msg->segment_size == NO_VAL16)
		job_desc_msg->segment_size = 0;

	if (job_desc_msg->kill_on_node_fail == NO_VAL16)
		job_desc_msg->kill_on_node_fail = 1;

	if (job_desc_msg->job_id != NO_VAL) {
		job_record_t *dup_job_ptr;
		if (!fed_mgr_fed_rec &&
		    (submit_uid != 0) &&
		    (submit_uid != slurm_conf.slurm_user_id)) {
			info("attempt by uid %u to set JobId=%u",
			     submit_uid, job_desc_msg->job_id);
			return ESLURM_INVALID_JOB_ID;
		}
		if (job_desc_msg->job_id == 0) {
			info("attempt by uid %u to set JobId=0",
			     submit_uid);
			return ESLURM_INVALID_JOB_ID;
		}
		dup_job_ptr = find_job_record(job_desc_msg->job_id);
		if (dup_job_ptr) {
			info("attempt to re-use active %pJ", dup_job_ptr);
			return ESLURM_DUPLICATE_JOB_ID;
		}
	}

	if (job_desc_msg->nice == NO_VAL)
		job_desc_msg->nice = NICE_OFFSET;

	if (job_desc_msg->pn_min_memory == NO_VAL64) {
		/* Default memory limit is DefMemPerCPU (if set) or no limit */
		if (part_ptr && part_ptr->def_mem_per_cpu) {
			job_desc_msg->pn_min_memory =
				part_ptr->def_mem_per_cpu;
		} else {
			job_desc_msg->pn_min_memory =
				slurm_conf.def_mem_per_cpu;
		}
	} else if (!_validate_min_mem_partition(job_desc_msg, part_ptr,
						part_list)) {
		return ESLURM_INVALID_TASK_MEMORY;
	} else {
		/* Memory limit explicitly set by user */
		job_desc_msg->bitflags |= JOB_MEM_SET;
	}

	job_desc_msg->bitflags &= ~BACKFILL_TEST;
	job_desc_msg->bitflags &= ~BF_WHOLE_NODE_TEST;
	job_desc_msg->bitflags &= ~JOB_ACCRUE_OVER;
	job_desc_msg->bitflags &= ~JOB_KILL_HURRY;
	job_desc_msg->bitflags &= ~SIB_JOB_FLUSH;
	job_desc_msg->bitflags &= ~TRES_STR_CALC;
	job_desc_msg->bitflags &= ~JOB_WAS_RUNNING;
	if (!cron)
		job_desc_msg->bitflags &= ~CRON_JOB;

	if (job_desc_msg->pn_min_memory == MEM_PER_CPU) {
		/* Map --mem-per-cpu=0 to --mem=0 for simpler logic */
		job_desc_msg->pn_min_memory = 0;
	}

	/* Validate a job's accounting frequency, if specified */
	if (acct_gather_check_acct_freq_task(
		    job_desc_msg->pn_min_memory, job_desc_msg->acctg_freq))
		return ESLURMD_INVALID_ACCT_FREQ;

	if (job_desc_msg->min_nodes == NO_VAL)
		job_desc_msg->min_nodes = 1;	/* default node count of 1 */
	if (job_desc_msg->min_cpus == NO_VAL)
		job_desc_msg->min_cpus = job_desc_msg->min_nodes;

	if ((job_desc_msg->pn_min_cpus == NO_VAL16) ||
	    (job_desc_msg->pn_min_cpus == 0))
		job_desc_msg->pn_min_cpus = 1;   /* default 1 cpu per node */
	if (job_desc_msg->pn_min_tmp_disk == NO_VAL)
		job_desc_msg->pn_min_tmp_disk = 0;/* default 0MB disk per node */

	return SLURM_SUCCESS;
}

/*
 * Traverse the list of partitions and invoke the
 * function validating the job memory specification.
 */
static bool _validate_min_mem_partition(job_desc_msg_t *job_desc_msg,
					part_record_t *part_ptr,
					list_t *part_list)
{
	list_itr_t *iter;
	part_record_t *part;
	uint64_t tmp_pn_min_memory;
	uint16_t tmp_cpus_per_task;
	uint32_t tmp_min_cpus;
	uint32_t tmp_max_cpus;
	uint32_t tmp_pn_min_cpus;
	bool cc = false;

	/* no reason to check them here as we aren't enforcing them */
	if (!slurm_conf.enforce_part_limits)
		return true;

	tmp_pn_min_memory = job_desc_msg->pn_min_memory;
	tmp_cpus_per_task = job_desc_msg->cpus_per_task;
	tmp_min_cpus = job_desc_msg->min_cpus;
	tmp_max_cpus = job_desc_msg->max_cpus;
	tmp_pn_min_cpus = job_desc_msg->pn_min_cpus;

	if (part_list == NULL) {
		cc = _valid_pn_min_mem(job_desc_msg, part_ptr);
	} else {
		iter = list_iterator_create(part_list);
		while ((part = list_next(iter))) {
			cc = _valid_pn_min_mem(job_desc_msg, part);

			/* for ALL we have to test them all */
			if (slurm_conf.enforce_part_limits ==
			    PARTITION_ENFORCE_ALL) {
				if (!cc)
					break;
			} else if (cc) /* break, we found one! */
				break;
			else if (slurm_conf.enforce_part_limits ==
				 PARTITION_ENFORCE_ANY) {
				debug("%s: Job requested for (%"PRIu64")MB is invalid for partition %s",
				      __func__, job_desc_msg->pn_min_memory,
				      part->name);
			}

			job_desc_msg->pn_min_memory = tmp_pn_min_memory;
			job_desc_msg->cpus_per_task = tmp_cpus_per_task;
			job_desc_msg->min_cpus = tmp_min_cpus;
			job_desc_msg->max_cpus = tmp_max_cpus;
			job_desc_msg->pn_min_cpus = tmp_pn_min_cpus;
		}
		list_iterator_destroy(iter);
	}

	/*
	 * Restoring original values, if it is necessary,
	 * these will be modified in job_limits_check()
	 */
	job_desc_msg->pn_min_memory = tmp_pn_min_memory;
	job_desc_msg->cpus_per_task = tmp_cpus_per_task;
	job_desc_msg->min_cpus = tmp_min_cpus;
	job_desc_msg->max_cpus = tmp_max_cpus;
	job_desc_msg->pn_min_cpus = tmp_pn_min_cpus;

	return cc;
}

static void _delete_job_common(job_record_t *job_ptr)
{
	if (!job_ptr->job_id)
		return;

	/* Remove record from fed_job_list */
	fed_mgr_remove_fed_job_info(job_ptr->job_id);

	/* Remove the record from job hash table */
	_remove_job_hash(job_ptr, JOB_HASH_JOB);

	/* Remove the record from job array hash tables, if applicable */
	if (job_ptr->array_task_id != NO_VAL) {
		_remove_job_hash(job_ptr, JOB_HASH_ARRAY_JOB);
		_remove_job_hash(job_ptr, JOB_HASH_ARRAY_TASK);
	}
}

/*
 * Remove the job record from hash tables and append to purge_jobs_list.
 */
static void _move_to_purge_jobs_list(void *job_entry)
{
	job_record_t *job_ptr = job_entry;
	int job_array_size;

	if (!job_entry)
		return;

	xassert(job_ptr->magic == JOB_MAGIC);

	_delete_job_common(job_ptr);

	if (job_ptr->array_recs) {
		job_array_size = MAX(1, job_ptr->array_recs->task_cnt);
	} else if (!job_ptr->job_id) { /* reservation */
		job_array_size = 0;
	} else {
		job_array_size = 1;
	}

	if (job_array_size > job_count) {
		error("job_count underflow");
		job_count = 0;
	} else {
		job_count -= job_array_size;
	}

	list_append(purge_jobs_list, job_ptr);
}

/*
 * find specific job_id entry in the job list, key is job_id_ptr
 */
static int _list_find_job_id(void *job_entry, void *key)
{
	job_record_t *job_ptr = (job_record_t *) job_entry;
	uint32_t *job_id_ptr = (uint32_t *) key;

	if (job_ptr->job_id == *job_id_ptr)
		return 1;

	return 0;
}

/*
 * _list_find_job_old - find old entries in the job list,
 *	see common/list.h for documentation, key is ignored
 * job_entry IN - job pointer
 * key IN - if not NULL, then skip hetjobs
 */
static int _list_find_job_old(void *job_entry, void *key)
{
	time_t kill_age, min_age, now = time(NULL);
	job_record_t *job_ptr = (job_record_t *) job_entry;

	if ((job_ptr->job_id == NO_VAL) && IS_JOB_REVOKED(job_ptr))
		return 1;

	if (key && job_ptr->het_job_id)
		return 0;

	if (IS_JOB_COMPLETING(job_ptr) && !LOTS_OF_AGENTS) {
		kill_age = now - (slurm_conf.kill_wait +
		                  2 * slurm_conf.msg_timeout);
		if (job_ptr->time_last_active < kill_age) {
			job_ptr->time_last_active = now;
			re_kill_job(job_ptr);
		}
		return 0;       /* Job still completing */
	}

	if (job_ptr->epilog_running)
		return 0;       /* EpilogSlurmctld still running */

	if (slurm_conf.min_job_age == 0)
		return 0;	/* No job record purging */

	if (fed_mgr_fed_rec && job_ptr->fed_details &&
	    !fed_mgr_is_origin_job(job_ptr)) {
		uint32_t origin_id = fed_mgr_get_cluster_id(job_ptr->job_id);
		slurmdb_cluster_rec_t *origin =
			fed_mgr_get_cluster_by_id(origin_id);

		/* keep job around until origin comes back and is synced */
		if (origin &&
		    (!origin->fed.send ||
		     (((persist_conn_t *) origin->fed.send)->fd == -1) ||
		     !origin->fed.sync_sent))
			return 0;
	}

	min_age = now - slurm_conf.min_job_age;
	if (job_ptr->end_time > min_age)
		return 0;	/* Too new to purge */

	if (!(IS_JOB_COMPLETED(job_ptr)))
		return 0;	/* Job still active */

	if (job_ptr->step_list && list_count(job_ptr->step_list)) {
		debug("%pJ still has %d active steps",
		      job_ptr, list_count(job_ptr->step_list));
		/*
		 * If the job has been around more than 30 days the steps are
		 * bogus.  Blow the job away.  This was witnessed <= 16.05 but
		 * hasn't be seen since.  This is here just to clear them out if
		 * this ever shows up again.
		 */
		min_age = now - PURGE_OLD_JOB_IN_SEC;
		if (job_ptr->end_time <= min_age) {
			info("Force purge of %pJ. It ended over 30 days ago, the slurmctld thinks there are still steps running but they are most likely bogus. In any case you might want to check nodes %s to make sure nothing remains of the job.",
			     job_ptr, job_ptr->nodes);
			goto end_it;
		} else
			return 0;	/* steps are still active */
	}

	if (job_ptr->array_recs) {
		if (job_ptr->array_recs->tot_run_tasks ||
		    !_test_job_array_purged(job_ptr->array_job_id)) {
			/* Some tasks from this job array still active */
			return 0;
		}
	}

	if (bb_g_job_test_stage_out(job_ptr) != 1)
		return 0;      /* Stage out in progress */

end_it:

	return 1;		/* Purge the job */
}

/* Determine if ALL partitions associated with a job are hidden */
static bool _all_parts_hidden(job_record_t *job_ptr,
			      part_record_t **visible_parts)
{
	bool rc;
	list_itr_t *part_iterator;
	part_record_t *part_ptr;

	if (job_ptr->part_ptr_list) {
		rc = true;
		part_iterator = list_iterator_create(part_list);
		while (rc && (part_ptr = list_next(part_iterator))) {
			for (int i = 0; visible_parts[i]; i++) {
				if (visible_parts[i] == part_ptr) {
					rc = false;
					break;
				}
			}
		}
		list_iterator_destroy(part_iterator);
		return rc;
	}

	if (job_ptr->part_ptr) {
		for (int i = 0; visible_parts[i]; i++) {
			if (visible_parts[i] == job_ptr->part_ptr)
				return false;
		}
	}

	return true;
}

/* Determine if a given job should be seen by a specific user */
static bool _hide_job_user_rec(job_record_t *job_ptr, slurmdb_user_rec_t *user,
			       uint16_t show_flags)
{
	if (!job_ptr)
		return true;

	if ((slurm_conf.private_data & PRIVATE_DATA_JOBS) &&
	    (job_ptr->user_id != user->uid) &&
	    (((slurm_mcs_get_privatedata() == 0) &&
	      !assoc_mgr_is_user_acct_coord_user_rec(user, job_ptr->account)) ||
	     ((slurm_mcs_get_privatedata() == 1) &&
	      (mcs_g_check_mcs_label(user->uid, job_ptr->mcs_label,
				     true) != 0))))
		return true;
	return false;
}

static int _pack_job(void *object, void *arg)
{
	job_record_t *job_ptr = (job_record_t *)object;
	_foreach_pack_job_info_t *pack_info = (_foreach_pack_job_info_t *)arg;

	xassert (job_ptr->magic == JOB_MAGIC);

	if ((pack_info->filter_uid != NO_VAL) &&
	    (pack_info->filter_uid != job_ptr->user_id))
		return SLURM_SUCCESS;

	if (!(pack_info->show_flags & SHOW_ALL) && IS_JOB_REVOKED(job_ptr))
		return SLURM_SUCCESS;

	if (!pack_info->privileged) {
		if (((pack_info->show_flags & SHOW_ALL) == 0) &&
		    _all_parts_hidden(job_ptr, pack_info->visible_parts))
			return SLURM_SUCCESS;

		if (_hide_job_user_rec(job_ptr, &pack_info->user_rec,
				       pack_info->show_flags))
			return SLURM_SUCCESS;
	}

	pack_job(job_ptr, pack_info->show_flags, pack_info->buffer,
		 pack_info->protocol_version, pack_info->uid,
		 pack_info->has_qos_lock);

	pack_info->jobs_packed++;

	return SLURM_SUCCESS;
}

static int _foreach_pack_jobid(void *object, void *arg)
{
	job_record_t *job_ptr;
	uint32_t job_id = *(uint32_t *)object;
	_foreach_pack_job_info_t *info = (_foreach_pack_job_info_t *)arg;

	if (!(job_ptr = find_job_record(job_id)))
		return SLURM_SUCCESS;

	return _pack_job(job_ptr, info);
}

/*
 * _pack_init_job_info - create buffer with header packed for a job_info_msg_t
 *
 * NOTE: change _unpack_job_info_msg() in common/slurm_protocol_pack.c
 *	whenever the data format changes
 */
static buf_t *_pack_init_job_info(uint16_t protocol_version)
{
	buf_t *buffer = init_buf(BUF_SIZE);

	/* write message body header : size and time */
	/* put in a place holder job record count of 0 for now */
	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(0, buffer);
		pack_time(time(NULL), buffer);
		pack_time(slurmctld_diag_stats.bf_when_last_cycle, buffer);
	}

	return buffer;
}

/*
 * pack_all_jobs - dump all job information for all jobs in
 *	machine independent form (for network transmission)
 * IN show_flags - job filtering options
 * IN uid - uid of user making request (for partition filtering)
 * IN filter_uid - pack only jobs belonging to this user if not NO_VAL
 * OUT buffer
 * global: job_list - global list of job records
 * NOTE: the buffer at *buffer_ptr must be xfreed by the caller
 */
extern buf_t *pack_all_jobs(uint16_t show_flags, uid_t uid, uint32_t filter_uid,
			    uint16_t protocol_version)
{
	uint32_t tmp_offset;
	_foreach_pack_job_info_t pack_info = {
		.buffer = _pack_init_job_info(protocol_version),
		.filter_uid = filter_uid,
		.jobs_packed = 0,
		.protocol_version = protocol_version,
		.show_flags = show_flags,
		.uid = uid,
		.has_qos_lock = true,
		.user_rec.uid = uid,
	};
	assoc_mgr_lock_t locks = { .assoc = READ_LOCK, .user = READ_LOCK,
				   .qos = READ_LOCK };

	assoc_mgr_lock(&locks);
	assoc_mgr_fill_in_user(acct_db_conn, &pack_info.user_rec,
			       accounting_enforce, NULL, true);
	pack_info.privileged = validate_operator_user_rec(&pack_info.user_rec);
	pack_info.visible_parts = build_visible_parts(
		uid, (pack_info.privileged || (show_flags & SHOW_ALL)));
	list_for_each_ro(job_list, _pack_job, &pack_info);
	assoc_mgr_unlock(&locks);

	/* put the real record count in the message body header */
	tmp_offset = get_buf_offset(pack_info.buffer);
	set_buf_offset(pack_info.buffer, 0);
	pack32(pack_info.jobs_packed, pack_info.buffer);
	set_buf_offset(pack_info.buffer, tmp_offset);

	xfree(pack_info.visible_parts);

	return pack_info.buffer;
}

/*
 * pack_spec_jobs - dump job information for specified jobs in
 *	machine independent form (for network transmission)
 * IN show_flags - job filtering options
 * IN job_ids - list of job_ids to pack
 * IN uid - uid of user making request (for partition filtering)
 * IN filter_uid - pack only jobs belonging to this user if not NO_VAL
 * OUT buffer
 * global: job_list - global list of job records
 * NOTE: the buffer at *buffer_ptr must be xfreed by the caller
 */
extern buf_t *pack_spec_jobs(list_t *job_ids, uint16_t show_flags, uid_t uid,
			     uint32_t filter_uid, uint16_t protocol_version)
{
	uint32_t tmp_offset;
	_foreach_pack_job_info_t pack_info = {
		.buffer = _pack_init_job_info(protocol_version),
		.filter_uid = filter_uid,
		.jobs_packed = 0,
		.protocol_version = protocol_version,
		.show_flags = show_flags,
		.uid = uid,
		.has_qos_lock = true,
		.user_rec.uid = uid,
	};
	assoc_mgr_lock_t locks = { .assoc = READ_LOCK, .user = READ_LOCK,
				   .qos = READ_LOCK };

	xassert(job_ids);

	assoc_mgr_lock(&locks);
	assoc_mgr_fill_in_user(acct_db_conn, &pack_info.user_rec,
			       accounting_enforce, NULL, true);
	pack_info.privileged = validate_operator_user_rec(&pack_info.user_rec);
	pack_info.visible_parts = build_visible_parts(
		uid, (pack_info.privileged || (show_flags & SHOW_ALL)));
	list_for_each_ro(job_ids, _foreach_pack_jobid, &pack_info);
	assoc_mgr_unlock(&locks);

	/* put the real record count in the message body header */
	tmp_offset = get_buf_offset(pack_info.buffer);
	set_buf_offset(pack_info.buffer, 0);
	pack32(pack_info.jobs_packed, pack_info.buffer);
	set_buf_offset(pack_info.buffer, tmp_offset);

	xfree(pack_info.visible_parts);

	return pack_info.buffer;
}

static int _pack_het_job(job_record_t *job_ptr, uint16_t show_flags,
			 buf_t *buffer, uint16_t protocol_version, uid_t uid)
{
	job_record_t *het_job_ptr;
	int job_cnt = 0;
	list_itr_t *iter;

	xassert(verify_assoc_lock(QOS_LOCK, READ_LOCK));

	iter = list_iterator_create(job_ptr->het_job_list);
	while ((het_job_ptr = list_next(iter))) {
		if (het_job_ptr->het_job_id == job_ptr->het_job_id) {
			pack_job(het_job_ptr, show_flags, buffer,
				 protocol_version, uid, true);
			job_cnt++;
		} else {
			error("%s: Bad het_job_list for %pJ",
			      __func__, job_ptr);
		}
	}
	list_iterator_destroy(iter);

	return job_cnt;
}

/*
 * pack_one_job - dump information for one jobs in
 *	machine independent form (for network transmission)
 * IN job_id - ID of job that we want info for
 * IN show_flags - job filtering options
 * IN uid - uid of user making request (for partition filtering)
 * OUT buffer
 */
extern buf_t *pack_one_job(uint32_t job_id, uint16_t show_flags, uid_t uid,
			   uint16_t protocol_version)
{
	job_record_t *job_ptr;
	uint32_t jobs_packed = 0, tmp_offset;
	buf_t *buffer;
	assoc_mgr_lock_t locks = { .qos = READ_LOCK, .user = READ_LOCK };
	slurmdb_user_rec_t user_rec = { 0 };
	bool hide_job = false;
	bool valid_operator;

	buffer = _pack_init_job_info(protocol_version);

	assoc_mgr_lock(&locks);
	user_rec.uid = uid;
	assoc_mgr_fill_in_user(acct_db_conn, &user_rec,
			       accounting_enforce, NULL, true);

	job_ptr = find_job_record(job_id);

	if (!(valid_operator = validate_operator_user_rec(&user_rec)))
		hide_job = _hide_job_user_rec(job_ptr, &user_rec, show_flags);

	if (!(show_flags & SHOW_ALL) && job_ptr && IS_JOB_REVOKED(job_ptr))
		hide_job = true;

	if (job_ptr && job_ptr->het_job_list) {
		/* Pack heterogeneous job components */
		if (!hide_job) {
			jobs_packed = _pack_het_job(job_ptr, show_flags,
						    buffer, protocol_version,
						    uid);
		}
	} else if (job_ptr && (job_ptr->array_task_id == NO_VAL) &&
		   !job_ptr->array_recs) {
		/* Pack regular (not array) job */
		if (!hide_job) {
			pack_job(job_ptr, show_flags, buffer, protocol_version,
				 uid, true);
			jobs_packed++;
		}
	} else {
		bool packed_head = false;

		/* Either the job is not found or it is a job array */
		if (job_ptr) {
			packed_head = true;
			if (!hide_job) {
				pack_job(job_ptr, show_flags, buffer,
					 protocol_version, uid, true);
				jobs_packed++;
			}
		}

		job_ptr = job_array_hash_j[JOB_HASH_INX(job_id)];
		while (job_ptr) {
			if ((job_ptr->job_id == job_id) && packed_head) {
				;	/* Already packed */
			} else if (!(show_flags & SHOW_ALL) &&
				   IS_JOB_REVOKED(job_ptr)) {
				/*
				 * Array jobs can't be federated but to be
				 * consistent and future proof, don't pack
				 * revoked array jobs.
				 */
			} else if (job_ptr->array_job_id == job_id) {
				if (valid_operator ||
				    !_hide_job_user_rec(job_ptr, &user_rec,
							show_flags)) {
					pack_job(job_ptr, show_flags, buffer,
						 protocol_version, uid, true);
					jobs_packed++;
				}
			}
			job_ptr = job_ptr->job_array_next_j;
		}
	}

	assoc_mgr_unlock(&locks);

	if (jobs_packed == 0) {
		FREE_NULL_BUFFER(buffer);
		return NULL;
	}

	/* put the real record count in the message body header */
	tmp_offset = get_buf_offset(buffer);
	set_buf_offset(buffer, 0);
	pack32(jobs_packed, buffer);
	set_buf_offset(buffer, tmp_offset);

	return buffer;
}

static void _pack_job_gres(job_record_t *dump_job_ptr, buf_t *buffer,
			   uint16_t protocol_version)
{
	if (!IS_JOB_STARTED(dump_job_ptr) || IS_JOB_FINISHED(dump_job_ptr) ||
	    (dump_job_ptr->gres_list_req == NULL)) {
		packstr_array(NULL, 0, buffer);
		return;
	}

	packstr_array(dump_job_ptr->gres_detail_str,
		      dump_job_ptr->gres_detail_cnt, buffer);
}

/*
 * pack_job - dump all configuration information about a specific job in
 *	machine independent form (for network transmission)
 * IN dump_job_ptr - pointer to job for which information is requested
 * IN show_flags - job filtering options
 * IN/OUT buffer - buffer in which data is placed, pointers automatically
 *	updated
 * IN uid - user requesting the data
 * NOTE: change _unpack_job_info_members() in common/slurm_protocol_pack.c
 *	  whenever the data format changes
 */
void pack_job(job_record_t *dump_job_ptr, uint16_t show_flags, buf_t *buffer,
	      uint16_t protocol_version, uid_t uid, bool has_qos_lock)
{
	job_details_t *detail_ptr;
	time_t accrue_time = 0, begin_time = 0, start_time = 0, end_time = 0;
	uint32_t time_limit;
	char *nodelist = NULL;
	assoc_mgr_lock_t locks = { .qos = READ_LOCK };
	xassert(!has_qos_lock || verify_assoc_lock(QOS_LOCK, READ_LOCK));

	if (protocol_version >= SLURM_24_11_PROTOCOL_VERSION) {
		job_record_pack_common(dump_job_ptr, false, buffer,
				       protocol_version);

		if (dump_job_ptr->array_recs) {
			build_array_str(dump_job_ptr);
			packstr(dump_job_ptr->array_recs->task_id_str, buffer);
			pack32(dump_job_ptr->array_recs->max_run_tasks, buffer);
		} else {
			job_record_t *array_head = NULL;
			packnull(buffer);
			if (dump_job_ptr->array_job_id) {
				array_head = find_job_record(
					dump_job_ptr->array_job_id);
			}
			if (array_head && array_head->array_recs) {
				pack32(array_head->array_recs->max_run_tasks,
				       buffer);
			} else {
				pack32(0, buffer);
			}
		}
		if ((dump_job_ptr->time_limit == NO_VAL) &&
		    dump_job_ptr->part_ptr)
			time_limit = dump_job_ptr->part_ptr->max_time;
		else
			time_limit = dump_job_ptr->time_limit;

		pack32(time_limit, buffer);

		if (IS_JOB_STARTED(dump_job_ptr)) {
			/* Report actual start time, in past */
			start_time = dump_job_ptr->start_time;
			end_time = dump_job_ptr->end_time;
		} else if (dump_job_ptr->start_time != 0) {
			/*
			 * Report expected start time,
			 * making sure that time is not in the past
			 */
			start_time = MAX(dump_job_ptr->start_time, time(NULL));
			if (time_limit != NO_VAL) {
				end_time = MAX(dump_job_ptr->end_time,
					       (start_time + time_limit * 60));
			}
		} else if (dump_job_ptr->details->begin_time > time(NULL)) {
			/* earliest start time in the future */
			start_time = dump_job_ptr->details->begin_time;
			if (time_limit != NO_VAL) {
				end_time = MAX(dump_job_ptr->end_time,
					       (start_time + time_limit * 60));
			}
		}
		pack_time(start_time, buffer);
		pack_time(end_time, buffer);

		if (dump_job_ptr->part_prio) {
			pack32_array(dump_job_ptr->part_prio->priority_array,
				     (dump_job_ptr->part_prio->priority_array) ?
				     list_count(dump_job_ptr->part_ptr_list) :
				     0, buffer);
			packstr(dump_job_ptr->part_prio->priority_array_names,
				buffer);
		} else {
			packnull(buffer);
			packnull(buffer);
		}

		packstr(slurm_conf.cluster_name, buffer);

		/*
		 * Only send the allocated nodelist since we are only sending
		 * the number of cpus and nodes that are currently allocated.
		 */
		if (!IS_JOB_COMPLETING(dump_job_ptr))
			packstr(dump_job_ptr->nodes, buffer);
		else {
			nodelist = bitmap2node_name(
				dump_job_ptr->node_bitmap_cg);
			packstr(nodelist, buffer);
			xfree(nodelist);
		}
		packstr(dump_job_ptr->sched_nodes, buffer);

		if (!IS_JOB_PENDING(dump_job_ptr) && dump_job_ptr->part_ptr)
			packstr(dump_job_ptr->part_ptr->name, buffer);
		else
			packstr(dump_job_ptr->partition, buffer);

		if (IS_JOB_PENDING(dump_job_ptr) &&
		    dump_job_ptr->details->qos_req)
			packstr(dump_job_ptr->details->qos_req, buffer);
		else {
			if (!has_qos_lock)
				assoc_mgr_lock(&locks);
			if (dump_job_ptr->qos_ptr)
				packstr(dump_job_ptr->qos_ptr->name, buffer);
			else {
				if (assoc_mgr_qos_list) {
					packstr(slurmdb_qos_str(
							assoc_mgr_qos_list,
							dump_job_ptr->qos_id),
						buffer);
				} else
					packnull(buffer);
			}
		}

		if (IS_JOB_STARTED(dump_job_ptr) &&
		    (slurm_conf.preempt_mode != PREEMPT_MODE_OFF) &&
		    (slurm_job_preempt_mode(dump_job_ptr) !=
		     PREEMPT_MODE_OFF)) {
			time_t preemptable = acct_policy_get_preemptable_time(
				dump_job_ptr);
			pack_time(preemptable, buffer);
		} else {
			pack_time(0, buffer);
		}
		if (!has_qos_lock)
			assoc_mgr_unlock(&locks);

		if (show_flags & SHOW_DETAIL) {
			pack_job_resources(dump_job_ptr->job_resrcs, buffer,
					   protocol_version);
			_pack_job_gres(dump_job_ptr, buffer, protocol_version);
		} else {
			pack32(NO_VAL, buffer);
			pack32((uint32_t)0, buffer);
		}

		if (!IS_JOB_COMPLETING(dump_job_ptr))
			pack_bit_str_hex(dump_job_ptr->node_bitmap, buffer);
		else
			pack_bit_str_hex(dump_job_ptr->node_bitmap_cg, buffer);

		/* A few details are always dumped here */
		_pack_default_job_details(dump_job_ptr, buffer,
					  protocol_version);

		/*
		 * other job details are only dumped until the job starts
		 * running (at which time they become meaningless)
		 */
		_pack_pending_job_details(dump_job_ptr->details,
					  buffer, protocol_version);
	} else if (protocol_version >= SLURM_24_05_PROTOCOL_VERSION) {
		detail_ptr = dump_job_ptr->details;
		pack32(dump_job_ptr->array_job_id, buffer);
		pack32(dump_job_ptr->array_task_id, buffer);
		if (dump_job_ptr->array_recs) {
			build_array_str(dump_job_ptr);
			packstr(dump_job_ptr->array_recs->task_id_str, buffer);
			pack32(dump_job_ptr->array_recs->max_run_tasks, buffer);
		} else {
			job_record_t *array_head = NULL;
			packnull(buffer);
			if (dump_job_ptr->array_job_id) {
				array_head = find_job_record(
					dump_job_ptr->array_job_id);
			}
			if (array_head && array_head->array_recs) {
				pack32(array_head->array_recs->max_run_tasks,
				       buffer);
			} else {
				pack32(0, buffer);
			}
		}

		pack32(dump_job_ptr->assoc_id, buffer);
		packstr(dump_job_ptr->container, buffer);
		packstr(dump_job_ptr->container_id, buffer);
		pack32(dump_job_ptr->delay_boot, buffer);
		packstr(dump_job_ptr->failed_node, buffer);
		pack32(dump_job_ptr->job_id, buffer);
		pack32(dump_job_ptr->user_id, buffer);
		pack32(dump_job_ptr->group_id, buffer);
		pack32(dump_job_ptr->het_job_id, buffer);
		packstr(dump_job_ptr->het_job_id_set, buffer);
		pack32(dump_job_ptr->het_job_offset, buffer);
		pack32(dump_job_ptr->profile, buffer);

		pack32(dump_job_ptr->job_state, buffer);
		pack16(dump_job_ptr->batch_flag, buffer);
		pack32(dump_job_ptr->state_reason, buffer);
		pack8(0, buffer); /* was power_flags */
		pack8(dump_job_ptr->reboot, buffer);
		pack16(dump_job_ptr->restart_cnt, buffer);
		pack16(show_flags, buffer);
		pack_time(dump_job_ptr->deadline, buffer);

		pack32(dump_job_ptr->alloc_sid, buffer);
		if ((dump_job_ptr->time_limit == NO_VAL) &&
		    dump_job_ptr->part_ptr)
			time_limit = dump_job_ptr->part_ptr->max_time;
		else
			time_limit = dump_job_ptr->time_limit;

		pack32(time_limit, buffer);
		pack32(dump_job_ptr->time_min, buffer);

		if (dump_job_ptr->details) {
			pack32(dump_job_ptr->details->nice, buffer);
			pack_time(dump_job_ptr->details->submit_time, buffer);
			/* Earliest possible begin time */
			begin_time = dump_job_ptr->details->begin_time;
			/* When we started accruing time for priority */
			accrue_time = dump_job_ptr->details->accrue_time;
		} else { /* Some job details may be purged after completion */
			pack32(NICE_OFFSET, buffer); /* Best guess */
			pack_time((time_t)0, buffer);
		}

		pack_time(begin_time, buffer);
		pack_time(accrue_time, buffer);

		if (IS_JOB_STARTED(dump_job_ptr)) {
			/* Report actual start time, in past */
			start_time = dump_job_ptr->start_time;
			end_time = dump_job_ptr->end_time;
		} else if (dump_job_ptr->start_time != 0) {
			/*
			 * Report expected start time,
			 * making sure that time is not in the past
			 */
			start_time = MAX(dump_job_ptr->start_time, time(NULL));
			if (time_limit != NO_VAL) {
				end_time = MAX(dump_job_ptr->end_time,
					       (start_time + time_limit * 60));
			}
		} else if (begin_time > time(NULL)) {
			/* earliest start time in the future */
			start_time = begin_time;
			if (time_limit != NO_VAL) {
				end_time = MAX(dump_job_ptr->end_time,
					       (start_time + time_limit * 60));
			}
		}
		pack_time(start_time, buffer);
		pack_time(end_time, buffer);

		pack_time(dump_job_ptr->suspend_time, buffer);
		pack_time(dump_job_ptr->pre_sus_time, buffer);
		pack_time(dump_job_ptr->resize_time, buffer);
		pack_time(dump_job_ptr->last_sched_eval, buffer);
		pack_time(dump_job_ptr->preempt_time, buffer);
		pack32(dump_job_ptr->priority, buffer);
		if (dump_job_ptr->part_prio) {
			pack32_array(dump_job_ptr->part_prio->priority_array,
				     (dump_job_ptr->part_prio->priority_array) ?
				     list_count(dump_job_ptr->part_ptr_list) :
				     0, buffer);
			packstr(dump_job_ptr->part_prio->priority_array_names,
				buffer);
		} else {
			packnull(buffer);
			packnull(buffer);
		}
		packdouble(dump_job_ptr->billable_tres, buffer);

		packstr(slurm_conf.cluster_name, buffer);
		/*
		 * Only send the allocated nodelist since we are only sending
		 * the number of cpus and nodes that are currently allocated.
		 */
		if (!IS_JOB_COMPLETING(dump_job_ptr))
			packstr(dump_job_ptr->nodes, buffer);
		else {
			nodelist = bitmap2node_name(
				dump_job_ptr->node_bitmap_cg);
			packstr(nodelist, buffer);
			xfree(nodelist);
		}

		packstr(dump_job_ptr->sched_nodes, buffer);

		if (!IS_JOB_PENDING(dump_job_ptr) && dump_job_ptr->part_ptr)
			packstr(dump_job_ptr->part_ptr->name, buffer);
		else
			packstr(dump_job_ptr->partition, buffer);
		packstr(dump_job_ptr->account, buffer);
		packstr(dump_job_ptr->admin_comment, buffer);
		pack32(dump_job_ptr->site_factor, buffer);
		packstr(dump_job_ptr->network, buffer);
		packstr(dump_job_ptr->comment, buffer);
		packstr(dump_job_ptr->extra, buffer);
		packstr(dump_job_ptr->container, buffer);
		packstr(dump_job_ptr->batch_features, buffer);
		packstr(dump_job_ptr->batch_host, buffer);
		packstr(dump_job_ptr->burst_buffer, buffer);
		packstr(dump_job_ptr->burst_buffer_state, buffer);
		packstr(dump_job_ptr->system_comment, buffer);

		if (!has_qos_lock)
			assoc_mgr_lock(&locks);
		if (dump_job_ptr->qos_ptr)
			packstr(dump_job_ptr->qos_ptr->name, buffer);
		else {
			if (assoc_mgr_qos_list) {
				packstr(slurmdb_qos_str(assoc_mgr_qos_list,
							dump_job_ptr->qos_id),
					buffer);
			} else
				packnull(buffer);
		}

		if (IS_JOB_STARTED(dump_job_ptr) &&
		    (slurm_conf.preempt_mode != PREEMPT_MODE_OFF) &&
		    (slurm_job_preempt_mode(dump_job_ptr) !=
		     PREEMPT_MODE_OFF)) {
			time_t preemptable = acct_policy_get_preemptable_time(
				dump_job_ptr);
			pack_time(preemptable, buffer);
		} else {
			pack_time(0, buffer);
		}
		if (!has_qos_lock)
			assoc_mgr_unlock(&locks);

		packstr(dump_job_ptr->licenses, buffer);
		packstr(dump_job_ptr->state_desc, buffer);
		packstr(dump_job_ptr->resv_name, buffer);
		packstr(dump_job_ptr->resv_ports, buffer);
		packstr(dump_job_ptr->mcs_label, buffer);

		pack32(dump_job_ptr->exit_code, buffer);
		pack32(dump_job_ptr->derived_ec, buffer);

		packstr(dump_job_ptr->gres_used, buffer);
		if (show_flags & SHOW_DETAIL) {
			pack_job_resources(dump_job_ptr->job_resrcs, buffer,
					   protocol_version);
			_pack_job_gres(dump_job_ptr, buffer, protocol_version);
		} else {
			pack32(NO_VAL, buffer);
			pack32((uint32_t)0, buffer);
		}

		packstr(dump_job_ptr->name, buffer);
		packstr(dump_job_ptr->user_name, buffer);
		packstr(dump_job_ptr->wckey, buffer);
		pack32(dump_job_ptr->req_switch, buffer);
		pack32(dump_job_ptr->wait4switch, buffer);

		packstr(dump_job_ptr->alloc_node, buffer);
		if (!IS_JOB_COMPLETING(dump_job_ptr))
			pack_bit_str_hex(dump_job_ptr->node_bitmap, buffer);
		else
			pack_bit_str_hex(dump_job_ptr->node_bitmap_cg, buffer);

		/* A few details are always dumped here */
		_pack_default_job_details(dump_job_ptr, buffer,
					  protocol_version);

		/*
		 * other job details are only dumped until the job starts
		 * running (at which time they become meaningless)
		 */
		if (detail_ptr)
			_pack_pending_job_details(detail_ptr, buffer,
						  protocol_version);
		else
			_pack_pending_job_details(NULL, buffer,
						  protocol_version);
		pack64(dump_job_ptr->bit_flags, buffer);
		packstr(dump_job_ptr->tres_fmt_alloc_str, buffer);
		packstr(dump_job_ptr->tres_fmt_req_str, buffer);
		pack16(dump_job_ptr->start_protocol_ver, buffer);

		if (dump_job_ptr->fed_details) {
			packstr(dump_job_ptr->fed_details->origin_str, buffer);
			pack64(dump_job_ptr->fed_details->siblings_active,
			       buffer);
			packstr(dump_job_ptr->fed_details->siblings_active_str,
				buffer);
			pack64(dump_job_ptr->fed_details->siblings_viable,
			       buffer);
			packstr(dump_job_ptr->fed_details->siblings_viable_str,
				buffer);
		} else {
			packnull(buffer);
			pack64((uint64_t)0, buffer);
			packnull(buffer);
			pack64((uint64_t)0, buffer);
			packnull(buffer);
		}

		packstr(dump_job_ptr->cpus_per_tres, buffer);
		packstr(dump_job_ptr->mem_per_tres, buffer);
		packstr(dump_job_ptr->tres_bind, buffer);
		packstr(dump_job_ptr->tres_freq, buffer);
		packstr(dump_job_ptr->tres_per_job, buffer);
		packstr(dump_job_ptr->tres_per_node, buffer);
		packstr(dump_job_ptr->tres_per_socket, buffer);
		packstr(dump_job_ptr->tres_per_task, buffer);

		pack16(dump_job_ptr->mail_type, buffer);
		packstr(dump_job_ptr->mail_user, buffer);

		packstr(dump_job_ptr->selinux_context, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		detail_ptr = dump_job_ptr->details;
		pack32(dump_job_ptr->array_job_id, buffer);
		pack32(dump_job_ptr->array_task_id, buffer);
		if (dump_job_ptr->array_recs) {
			build_array_str(dump_job_ptr);
			packstr(dump_job_ptr->array_recs->task_id_str, buffer);
			pack32(dump_job_ptr->array_recs->max_run_tasks, buffer);
		} else {
			job_record_t *array_head = NULL;
			packnull(buffer);
			if (dump_job_ptr->array_job_id) {
				array_head = find_job_record(
					dump_job_ptr->array_job_id);
			}
			if (array_head && array_head->array_recs) {
				pack32(array_head->array_recs->max_run_tasks,
				       buffer);
			} else {
				pack32(0, buffer);
			}
		}

		pack32(dump_job_ptr->assoc_id, buffer);
		packstr(dump_job_ptr->container, buffer);
		packstr(dump_job_ptr->container_id, buffer);
		pack32(dump_job_ptr->delay_boot, buffer);
		packstr(dump_job_ptr->failed_node, buffer);
		pack32(dump_job_ptr->job_id, buffer);
		pack32(dump_job_ptr->user_id, buffer);
		pack32(dump_job_ptr->group_id, buffer);
		pack32(dump_job_ptr->het_job_id, buffer);
		packstr(dump_job_ptr->het_job_id_set, buffer);
		pack32(dump_job_ptr->het_job_offset, buffer);
		pack32(dump_job_ptr->profile, buffer);

		pack32(dump_job_ptr->job_state, buffer);
		pack16(dump_job_ptr->batch_flag, buffer);
		pack32(dump_job_ptr->state_reason, buffer);
		pack8(0, buffer); /* was power_flags */
		pack8(dump_job_ptr->reboot, buffer);
		pack16(dump_job_ptr->restart_cnt, buffer);
		pack16(show_flags, buffer);
		pack_time(dump_job_ptr->deadline, buffer);

		pack32(dump_job_ptr->alloc_sid, buffer);
		if ((dump_job_ptr->time_limit == NO_VAL) &&
		    dump_job_ptr->part_ptr)
			time_limit = dump_job_ptr->part_ptr->max_time;
		else
			time_limit = dump_job_ptr->time_limit;

		pack32(time_limit, buffer);
		pack32(dump_job_ptr->time_min, buffer);

		if (dump_job_ptr->details) {
			pack32(dump_job_ptr->details->nice, buffer);
			pack_time(dump_job_ptr->details->submit_time, buffer);
			/* Earliest possible begin time */
			begin_time = dump_job_ptr->details->begin_time;
			/* When we started accruing time for priority */
			accrue_time = dump_job_ptr->details->accrue_time;
		} else { /* Some job details may be purged after completion */
			pack32(NICE_OFFSET, buffer); /* Best guess */
			pack_time((time_t)0, buffer);
		}

		pack_time(begin_time, buffer);
		pack_time(accrue_time, buffer);

		if (IS_JOB_STARTED(dump_job_ptr)) {
			/* Report actual start time, in past */
			start_time = dump_job_ptr->start_time;
			end_time = dump_job_ptr->end_time;
		} else if (dump_job_ptr->start_time != 0) {
			/*
			 * Report expected start time,
			 * making sure that time is not in the past
			 */
			start_time = MAX(dump_job_ptr->start_time, time(NULL));
			if (time_limit != NO_VAL) {
				end_time = MAX(dump_job_ptr->end_time,
					       (start_time + time_limit * 60));
			}
		} else if (begin_time > time(NULL)) {
			/* earliest start time in the future */
			start_time = begin_time;
			if (time_limit != NO_VAL) {
				end_time = MAX(dump_job_ptr->end_time,
					       (start_time + time_limit * 60));
			}
		}
		pack_time(start_time, buffer);
		pack_time(end_time, buffer);

		pack_time(dump_job_ptr->suspend_time, buffer);
		pack_time(dump_job_ptr->pre_sus_time, buffer);
		pack_time(dump_job_ptr->resize_time, buffer);
		pack_time(dump_job_ptr->last_sched_eval, buffer);
		pack_time(dump_job_ptr->preempt_time, buffer);
		pack32(dump_job_ptr->priority, buffer);
		packdouble(dump_job_ptr->billable_tres, buffer);

		packstr(slurm_conf.cluster_name, buffer);
		/*
		 * Only send the allocated nodelist since we are only sending
		 * the number of cpus and nodes that are currently allocated.
		 */
		if (!IS_JOB_COMPLETING(dump_job_ptr))
			packstr(dump_job_ptr->nodes, buffer);
		else {
			nodelist = bitmap2node_name(
				dump_job_ptr->node_bitmap_cg);
			packstr(nodelist, buffer);
			xfree(nodelist);
		}

		packstr(dump_job_ptr->sched_nodes, buffer);

		if (!IS_JOB_PENDING(dump_job_ptr) && dump_job_ptr->part_ptr)
			packstr(dump_job_ptr->part_ptr->name, buffer);
		else
			packstr(dump_job_ptr->partition, buffer);
		packstr(dump_job_ptr->account, buffer);
		packstr(dump_job_ptr->admin_comment, buffer);
		pack32(dump_job_ptr->site_factor, buffer);
		packstr(dump_job_ptr->network, buffer);
		packstr(dump_job_ptr->comment, buffer);
		packstr(dump_job_ptr->extra, buffer);
		packstr(dump_job_ptr->container, buffer);
		packstr(dump_job_ptr->batch_features, buffer);
		packstr(dump_job_ptr->batch_host, buffer);
		packstr(dump_job_ptr->burst_buffer, buffer);
		packstr(dump_job_ptr->burst_buffer_state, buffer);
		packstr(dump_job_ptr->system_comment, buffer);

		if (!has_qos_lock)
			assoc_mgr_lock(&locks);
		if (dump_job_ptr->qos_ptr)
			packstr(dump_job_ptr->qos_ptr->name, buffer);
		else {
			if (assoc_mgr_qos_list) {
				packstr(slurmdb_qos_str(assoc_mgr_qos_list,
							dump_job_ptr->qos_id),
					buffer);
			} else
				packnull(buffer);
		}

		if (IS_JOB_STARTED(dump_job_ptr) &&
		    (slurm_conf.preempt_mode != PREEMPT_MODE_OFF) &&
		    (slurm_job_preempt_mode(dump_job_ptr) !=
		     PREEMPT_MODE_OFF)) {
			time_t preemptable = acct_policy_get_preemptable_time(
				dump_job_ptr);
			pack_time(preemptable, buffer);
		} else {
			pack_time(0, buffer);
		}
		if (!has_qos_lock)
			assoc_mgr_unlock(&locks);

		packstr(dump_job_ptr->licenses, buffer);
		packstr(dump_job_ptr->state_desc, buffer);
		packstr(dump_job_ptr->resv_name, buffer);
		packstr(dump_job_ptr->mcs_label, buffer);

		pack32(dump_job_ptr->exit_code, buffer);
		pack32(dump_job_ptr->derived_ec, buffer);

		packstr(dump_job_ptr->gres_used, buffer);
		if (show_flags & SHOW_DETAIL) {
			pack_job_resources(dump_job_ptr->job_resrcs, buffer,
					   protocol_version);
			_pack_job_gres(dump_job_ptr, buffer, protocol_version);
		} else {
			pack32(NO_VAL, buffer);
			pack32((uint32_t)0, buffer);
		}

		packstr(dump_job_ptr->name, buffer);
		packstr(dump_job_ptr->user_name, buffer);
		packstr(dump_job_ptr->wckey, buffer);
		pack32(dump_job_ptr->req_switch, buffer);
		pack32(dump_job_ptr->wait4switch, buffer);

		packstr(dump_job_ptr->alloc_node, buffer);
		if (!IS_JOB_COMPLETING(dump_job_ptr))
			pack_bit_str_hex(dump_job_ptr->node_bitmap, buffer);
		else
			pack_bit_str_hex(dump_job_ptr->node_bitmap_cg, buffer);

		/* A few details are always dumped here */
		_pack_default_job_details(dump_job_ptr, buffer,
					  protocol_version);

		/*
		 * other job details are only dumped until the job starts
		 * running (at which time they become meaningless)
		 */
		if (detail_ptr)
			_pack_pending_job_details(detail_ptr, buffer,
						  protocol_version);
		else
			_pack_pending_job_details(NULL, buffer,
						  protocol_version);
		pack64(dump_job_ptr->bit_flags, buffer);
		packstr(dump_job_ptr->tres_fmt_alloc_str, buffer);
		packstr(dump_job_ptr->tres_fmt_req_str, buffer);
		pack16(dump_job_ptr->start_protocol_ver, buffer);

		if (dump_job_ptr->fed_details) {
			packstr(dump_job_ptr->fed_details->origin_str, buffer);
			pack64(dump_job_ptr->fed_details->siblings_active,
			       buffer);
			packstr(dump_job_ptr->fed_details->siblings_active_str,
				buffer);
			pack64(dump_job_ptr->fed_details->siblings_viable,
			       buffer);
			packstr(dump_job_ptr->fed_details->siblings_viable_str,
				buffer);
		} else {
			packnull(buffer);
			pack64((uint64_t)0, buffer);
			packnull(buffer);
			pack64((uint64_t)0, buffer);
			packnull(buffer);
		}

		packstr(dump_job_ptr->cpus_per_tres, buffer);
		packstr(dump_job_ptr->mem_per_tres, buffer);
		packstr(dump_job_ptr->tres_bind, buffer);
		packstr(dump_job_ptr->tres_freq, buffer);
		packstr(dump_job_ptr->tres_per_job, buffer);
		packstr(dump_job_ptr->tres_per_node, buffer);
		packstr(dump_job_ptr->tres_per_socket, buffer);
		packstr(dump_job_ptr->tres_per_task, buffer);

		pack16(dump_job_ptr->mail_type, buffer);
		packstr(dump_job_ptr->mail_user, buffer);

		packstr(dump_job_ptr->selinux_context, buffer);
	} else {
		error("pack_job: protocol_version "
		      "%hu not supported", protocol_version);
	}
}

static void _find_node_config(int *cpu_cnt_ptr, int *core_cnt_ptr)
{
	static int max_cpu_cnt = -1, max_core_cnt = -1;
	static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
	int i;
	node_record_t *node_ptr;

	slurm_mutex_lock(&lock);
	if (max_cpu_cnt == -1) {
		for (i = 0; (node_ptr = next_node(&i)); i++) {
			/* Only data from config_record used for scheduling */
			max_cpu_cnt = MAX(max_cpu_cnt,
					  node_ptr->config_ptr->cpus);
			max_core_cnt = MAX(max_core_cnt,
					   node_ptr->config_ptr->cores);
		}
	}
	slurm_mutex_unlock(&lock);

	*cpu_cnt_ptr  = max_cpu_cnt;
	*core_cnt_ptr = max_core_cnt;
}

/* pack default job details for "get_job_info" RPC */
static void _pack_default_job_details(job_record_t *job_ptr, buf_t *buffer,
				      uint16_t protocol_version)
{
	int max_cpu_cnt = -1, max_core_cnt = -1;
	job_details_t *detail_ptr = job_ptr->details;
	uint16_t shared = 0;

	shared = get_job_share_value(job_ptr);

	if (job_ptr->part_ptr && job_ptr->part_ptr->max_cpu_cnt) {
		max_cpu_cnt  = job_ptr->part_ptr->max_cpu_cnt;
		max_core_cnt = job_ptr->part_ptr->max_core_cnt;
	} else
		_find_node_config(&max_cpu_cnt, &max_core_cnt);

	if (protocol_version >= SLURM_24_11_PROTOCOL_VERSION) {
		if (!detail_ptr) {
			packbool(false, buffer);

			if (job_ptr->total_cpus)
				pack32(job_ptr->total_cpus, buffer);
			else
				pack32(job_ptr->cpu_cnt, buffer);

			pack32(job_ptr->node_cnt, buffer);
			pack32(NICE_OFFSET, buffer); /* Best guess */
			return;
		}
		packbool(true, buffer);
		job_record_pack_details_common(detail_ptr, buffer,
					       protocol_version);

		if (!IS_JOB_PENDING(job_ptr)) {
			packstr(detail_ptr->features_use, buffer);
			packnull(buffer);
		} else {
			packstr(detail_ptr->features, buffer);
			packstr(detail_ptr->prefer, buffer);
		}

		if (detail_ptr->argv)
			packstr(detail_ptr->argv[0], buffer);
		else
			packnull(buffer);

		if (IS_JOB_COMPLETING(job_ptr) && job_ptr->cpu_cnt) {
			pack32(job_ptr->cpu_cnt, buffer);
			pack32((uint32_t) 0, buffer);
		} else if (job_ptr->total_cpus &&
			   !IS_JOB_PENDING(job_ptr)) {
			/* If job is PENDING ignore total_cpus,
			 * which may have been set by previous run
			 * followed by job requeue. */
			pack32(job_ptr->total_cpus, buffer);
			pack32((uint32_t) 0, buffer);
		} else {
			pack32(detail_ptr->min_cpus, buffer);
			if (detail_ptr->max_cpus != NO_VAL)
				pack32(detail_ptr->max_cpus, buffer);
			else
				pack32((uint32_t) 0, buffer);
		}

		if (IS_JOB_COMPLETING(job_ptr) && job_ptr->node_cnt) {
			pack32(job_ptr->node_cnt, buffer);
			pack32((uint32_t) 0, buffer);
		} else if (job_ptr->total_nodes) {
			pack32(job_ptr->total_nodes, buffer);
			pack32((uint32_t) 0, buffer);
		} else if (job_ptr->node_cnt_wag) {
			/* This should catch everything else, but
			 * just in case this is 0 (startup or
			 * whatever) we will keep the rest of
			 * this if statement around.
			 */
			pack32(job_ptr->node_cnt_wag, buffer);
			pack32((uint32_t) detail_ptr->max_nodes,
			       buffer);
		} else if (detail_ptr->ntasks_per_node) {
			/* min_nodes based upon task count and ntasks
			 * per node */
			uint32_t min_nodes;
			min_nodes = detail_ptr->num_tasks /
				detail_ptr->ntasks_per_node;
			min_nodes = MAX(min_nodes,
					detail_ptr->min_nodes);
			pack32(min_nodes, buffer);
			pack32(detail_ptr->max_nodes, buffer);
		} else if (detail_ptr->cpus_per_task > 1) {
			/* min_nodes based upon task count and cpus
			 * per task */
			uint32_t ntasks_per_node, min_nodes;
			ntasks_per_node = max_cpu_cnt /
				detail_ptr->cpus_per_task;
			ntasks_per_node = MAX(ntasks_per_node, 1);
			min_nodes = detail_ptr->num_tasks /
				ntasks_per_node;
			min_nodes = MAX(min_nodes,
					detail_ptr->min_nodes);
			pack32(min_nodes, buffer);
			pack32(detail_ptr->max_nodes, buffer);
		} else if (detail_ptr->mc_ptr &&
			   detail_ptr->mc_ptr->ntasks_per_core &&
			   (detail_ptr->mc_ptr->ntasks_per_core
			    != INFINITE16)) {
			/* min_nodes based upon task count and ntasks
			 * per core */
			uint32_t min_cores, min_nodes;
			min_cores = ROUNDUP(detail_ptr->num_tasks,
					    detail_ptr->mc_ptr->
					    ntasks_per_core);
			min_nodes = ROUNDUP(min_cores, max_core_cnt);
			min_nodes = MAX(min_nodes,
					detail_ptr->min_nodes);
			pack32(min_nodes, buffer);
			pack32(detail_ptr->max_nodes, buffer);
		} else {
			/* min_nodes based upon task count only */
			uint32_t min_nodes;
			uint32_t max_nodes;

			min_nodes = ROUNDUP(detail_ptr->num_tasks,
					    max_cpu_cnt);
			min_nodes = MAX(min_nodes,
					detail_ptr->min_nodes);
			max_nodes = MAX(min_nodes,
					detail_ptr->max_nodes);
			pack32(min_nodes, buffer);
			pack32(max_nodes, buffer);
		}
		if (detail_ptr->num_tasks)
			pack32(detail_ptr->num_tasks, buffer);
		else if (IS_JOB_PENDING(job_ptr))
			pack32(detail_ptr->min_nodes, buffer);
		else if (job_ptr->tres_alloc_cnt)
			pack32((uint32_t)
			       job_ptr->tres_alloc_cnt[TRES_ARRAY_NODE],
			       buffer);
		else
			pack32(NO_VAL, buffer);

		pack16(shared, buffer);

		if (detail_ptr->crontab_entry)
			packstr(detail_ptr->crontab_entry->cronspec,
				buffer);
		else
			packnull(buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		if (detail_ptr) {
			if (!IS_JOB_PENDING(job_ptr)) {
				packstr(detail_ptr->features_use, buffer);
				packnull(buffer);
			} else {
				packstr(detail_ptr->features, buffer);
				packstr(detail_ptr->prefer, buffer);
			}
			packstr(detail_ptr->cluster_features, buffer);
			packstr(detail_ptr->work_dir, buffer);
			packstr(detail_ptr->dependency, buffer);

			if (detail_ptr->argv)
				packstr(detail_ptr->argv[0], buffer);
			else
				packnull(buffer);

			if (IS_JOB_COMPLETING(job_ptr) && job_ptr->cpu_cnt) {
				pack32(job_ptr->cpu_cnt, buffer);
				pack32((uint32_t) 0, buffer);
			} else if (job_ptr->total_cpus &&
				   !IS_JOB_PENDING(job_ptr)) {
				/* If job is PENDING ignore total_cpus,
				 * which may have been set by previous run
				 * followed by job requeue. */
				pack32(job_ptr->total_cpus, buffer);
				pack32((uint32_t) 0, buffer);
			} else {
				pack32(detail_ptr->min_cpus, buffer);
				if (detail_ptr->max_cpus != NO_VAL)
					pack32(detail_ptr->max_cpus, buffer);
				else
					pack32((uint32_t) 0, buffer);
			}

			if (IS_JOB_COMPLETING(job_ptr) && job_ptr->node_cnt) {
				pack32(job_ptr->node_cnt, buffer);
				pack32((uint32_t) 0, buffer);
			} else if (job_ptr->total_nodes) {
				pack32(job_ptr->total_nodes, buffer);
				pack32((uint32_t) 0, buffer);
			} else if (job_ptr->node_cnt_wag) {
				/* This should catch everything else, but
				 * just in case this is 0 (startup or
				 * whatever) we will keep the rest of
				 * this if statement around.
				 */
				pack32(job_ptr->node_cnt_wag, buffer);
				pack32((uint32_t) detail_ptr->max_nodes,
				       buffer);
			} else if (detail_ptr->ntasks_per_node) {
				/* min_nodes based upon task count and ntasks
				 * per node */
				uint32_t min_nodes;
				min_nodes = detail_ptr->num_tasks /
					detail_ptr->ntasks_per_node;
				min_nodes = MAX(min_nodes,
						detail_ptr->min_nodes);
				pack32(min_nodes, buffer);
				pack32(detail_ptr->max_nodes, buffer);
			} else if (detail_ptr->cpus_per_task > 1) {
				/* min_nodes based upon task count and cpus
				 * per task */
				uint32_t ntasks_per_node, min_nodes;
				ntasks_per_node = max_cpu_cnt /
					detail_ptr->cpus_per_task;
				ntasks_per_node = MAX(ntasks_per_node, 1);
				min_nodes = detail_ptr->num_tasks /
					ntasks_per_node;
				min_nodes = MAX(min_nodes,
						detail_ptr->min_nodes);
				pack32(min_nodes, buffer);
				pack32(detail_ptr->max_nodes, buffer);
			} else if (detail_ptr->mc_ptr &&
				   detail_ptr->mc_ptr->ntasks_per_core &&
				   (detail_ptr->mc_ptr->ntasks_per_core
				    != INFINITE16)) {
				/* min_nodes based upon task count and ntasks
				 * per core */
				uint32_t min_cores, min_nodes;
				min_cores = ROUNDUP(detail_ptr->num_tasks,
						    detail_ptr->mc_ptr->
						    ntasks_per_core);
				min_nodes = ROUNDUP(min_cores, max_core_cnt);
				min_nodes = MAX(min_nodes,
						detail_ptr->min_nodes);
				pack32(min_nodes, buffer);
				pack32(detail_ptr->max_nodes, buffer);
			} else {
				/* min_nodes based upon task count only */
				uint32_t min_nodes;
				uint32_t max_nodes;

				min_nodes = ROUNDUP(detail_ptr->num_tasks,
						    max_cpu_cnt);
				min_nodes = MAX(min_nodes,
						detail_ptr->min_nodes);
				max_nodes = MAX(min_nodes,
						detail_ptr->max_nodes);
				pack32(min_nodes, buffer);
				pack32(max_nodes, buffer);
			}
			pack_bit_str_hex(detail_ptr->job_size_bitmap, buffer);

			pack16(detail_ptr->requeue,   buffer);
			pack16(detail_ptr->ntasks_per_node, buffer);
			pack16(detail_ptr->ntasks_per_tres, buffer);
			if (detail_ptr->num_tasks)
				pack32(detail_ptr->num_tasks, buffer);
			else if (IS_JOB_PENDING(job_ptr))
				pack32(detail_ptr->min_nodes, buffer);
			else if (job_ptr->tres_alloc_cnt)
				pack32((uint32_t)
				       job_ptr->tres_alloc_cnt[TRES_ARRAY_NODE],
				       buffer);
			else
				pack32(NO_VAL, buffer);

			pack16(shared, buffer);
			pack32(detail_ptr->cpu_freq_min, buffer);
			pack32(detail_ptr->cpu_freq_max, buffer);
			pack32(detail_ptr->cpu_freq_gov, buffer);

			if (detail_ptr->crontab_entry)
				packstr(detail_ptr->crontab_entry->cronspec,
					buffer);
			else
				packnull(buffer);
		} else {
			packnull(buffer);
			packnull(buffer);
			packnull(buffer);
			packnull(buffer);

			if (job_ptr->total_cpus)
				pack32(job_ptr->total_cpus, buffer);
			else
				pack32(job_ptr->cpu_cnt, buffer);
			pack32((uint32_t) 0, buffer);

			pack32(job_ptr->node_cnt, buffer);
			pack32((uint32_t) 0, buffer);
			pack16((uint16_t) 0, buffer);
			pack16((uint16_t) 0, buffer);
			pack16((uint16_t) 0, buffer);
			pack32((uint32_t) 0, buffer);
			pack32((uint32_t) 0, buffer);
			pack32((uint32_t) 0, buffer);

			packnull(buffer);
		}
	} else {
		error("_pack_default_job_details: protocol_version "
		      "%hu not supported", protocol_version);
	}
}

/* pack pending job details for "get_job_info" RPC */
static void _pack_pending_job_details(job_details_t *detail_ptr, buf_t *buffer,
				      uint16_t protocol_version)
{
	if (protocol_version >= SLURM_24_11_PROTOCOL_VERSION) {
		if (detail_ptr) {
			pack16(detail_ptr->contiguous, buffer);
			pack16(detail_ptr->core_spec, buffer);
			pack16(detail_ptr->cpus_per_task, buffer);
			pack16(detail_ptr->pn_min_cpus, buffer);

			pack64(detail_ptr->pn_min_memory, buffer);
			pack32(detail_ptr->pn_min_tmp_disk, buffer);

			pack16(detail_ptr->oom_kill_step, buffer);

			packstr(detail_ptr->req_nodes, buffer);
			pack_bit_str_hex(detail_ptr->req_node_bitmap, buffer);
			packstr(detail_ptr->exc_nodes, buffer);
			pack_bit_str_hex(detail_ptr->exc_node_bitmap, buffer);

			packstr(detail_ptr->std_err, buffer);
			packstr(detail_ptr->std_in, buffer);
			packstr(detail_ptr->std_out, buffer);

			pack_multi_core_data(detail_ptr->mc_ptr, buffer,
					     protocol_version);
		} else {
			pack16((uint16_t) 0, buffer);
			pack16((uint16_t) 0, buffer);
			pack16((uint16_t) 0, buffer);
			pack16((uint16_t) 0, buffer);

			pack64((uint64_t) 0, buffer);
			pack32((uint32_t) 0, buffer);

			pack16((uint16_t) 0, buffer);

			packnull(buffer);
			packnull(buffer);
			packnull(buffer);
			packnull(buffer);

			packnull(buffer);
			packnull(buffer);
			packnull(buffer);

			pack_multi_core_data(NULL, buffer, protocol_version);
		}
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		if (detail_ptr) {
			pack16(detail_ptr->contiguous, buffer);
			pack16(detail_ptr->core_spec, buffer);
			pack16(detail_ptr->cpus_per_task, buffer);
			pack16(detail_ptr->pn_min_cpus, buffer);

			pack64(detail_ptr->pn_min_memory, buffer);
			pack32(detail_ptr->pn_min_tmp_disk, buffer);

			packstr(detail_ptr->req_nodes, buffer);
			pack_bit_str_hex(detail_ptr->req_node_bitmap, buffer);
			packstr(detail_ptr->exc_nodes, buffer);
			pack_bit_str_hex(detail_ptr->exc_node_bitmap, buffer);

			packstr(detail_ptr->std_err, buffer);
			packstr(detail_ptr->std_in, buffer);
			packstr(detail_ptr->std_out, buffer);

			pack_multi_core_data(detail_ptr->mc_ptr, buffer,
					     protocol_version);
		} else {
			pack16((uint16_t) 0, buffer);
			pack16((uint16_t) 0, buffer);
			pack16((uint16_t) 0, buffer);
			pack16((uint16_t) 0, buffer);

			pack64((uint64_t) 0, buffer);
			pack32((uint32_t) 0, buffer);

			packnull(buffer);
			packnull(buffer);
			packnull(buffer);
			packnull(buffer);

			packnull(buffer);
			packnull(buffer);
			packnull(buffer);

			pack_multi_core_data(NULL, buffer, protocol_version);
		}
	} else {
		error("%s: protocol_version %hu not supported", __func__,
		      protocol_version);
	}
}

static int _purge_het_job_filter(void *x, void *key)
{
	job_record_t *job_ptr = (job_record_t *) x;
	job_record_t *job_filter = (job_record_t *) key;
	if (job_ptr->het_job_id == job_filter->het_job_id)
		return 1;
	return 0;
}

/* If this is a hetjob leader and all components are complete,
 * then purge all job of its hetjob records
 * RET true if this record purged */
static inline bool _purge_complete_het_job(job_record_t *het_job_leader)
{
	job_record_t purge_job_rec;
	job_record_t *het_job;
	list_itr_t *iter;
	bool incomplete_job = false;
	int i;

	if (!het_job_leader->het_job_list)
		return false;		/* Not hetjob leader */
	if (!IS_JOB_FINISHED(het_job_leader))
		return false;		/* Hetjob leader incomplete */

	iter = list_iterator_create(het_job_leader->het_job_list);
	while ((het_job = list_next(iter))) {
		if (het_job_leader->het_job_id != het_job->het_job_id) {
			error("%s: Bad het_job_list for %pJ",
			      __func__, het_job_leader);
			continue;
		}
		if (!_list_find_job_old(het_job, NULL)) {
			incomplete_job = true;
			break;
		}
	}
	list_iterator_destroy(iter);

	if (incomplete_job)
		return false;

	purge_job_rec.het_job_id = het_job_leader->het_job_id;
	i = list_delete_all(job_list, &_purge_het_job_filter, &purge_job_rec);
	if (i) {
		debug2("%s: purged %d old job records", __func__, i);
		last_job_update = time(NULL);
		slurm_mutex_lock(&purge_thread_lock);
		slurm_cond_signal(&purge_thread_cond);
		slurm_mutex_unlock(&purge_thread_lock);
	}
	return true;
}

/*
 * If the job or slurm.conf requests to not kill on invalid dependency,
 * then set the job state reason to WAIT_DEP_INVALID. Otherwise, kill the
 * job.
 */
void handle_invalid_dependency(job_record_t *job_ptr)
{
	job_ptr->state_reason = WAIT_DEP_INVALID;
	xfree(job_ptr->state_desc);

	if (job_ptr->mail_type & MAIL_INVALID_DEPEND)
		mail_job_info(job_ptr, MAIL_INVALID_DEPEND);

	if (job_ptr->bit_flags & KILL_INV_DEP) {
		_kill_dependent(job_ptr);
	} else if (job_ptr->bit_flags & NO_KILL_INV_DEP) {
		debug("%s: %pJ job dependency never satisfied",
		      __func__, job_ptr);
	} else if (kill_invalid_dep) {
		_kill_dependent(job_ptr);
	} else {
		debug("%s: %pJ job dependency never satisfied",
		      __func__, job_ptr);
		job_ptr->state_reason = WAIT_DEP_INVALID;
	}
	fed_mgr_remove_remote_dependencies(job_ptr);
}

/*
 * purge_old_job - purge old job records.
 *	The jobs must have completed at least MIN_JOB_AGE minutes ago.
 *	Test job dependencies, handle after_ok, after_not_ok before
 *	purging any jobs.
 */
void purge_old_job(void)
{
	list_itr_t *job_iterator;
	job_record_t *job_ptr;
	int i, purge_job_count;

	xassert(verify_lock(CONF_LOCK, READ_LOCK));
	xassert(verify_lock(JOB_LOCK, WRITE_LOCK));
	xassert(verify_lock(NODE_LOCK, WRITE_LOCK));
	xassert(verify_lock(FED_LOCK, READ_LOCK));

	if ((purge_job_count = list_count(purge_files_list)))
		debug("%s: job file deletion is falling behind, "
		      "%d left to remove", __func__, purge_job_count);

	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = list_next(job_iterator))) {
		if (_purge_complete_het_job(job_ptr))
			continue;
		if (!IS_JOB_PENDING(job_ptr))
			continue;

		if ((job_ptr->deadline) && (job_ptr->deadline != NO_VAL) &&
		    !deadline_ok(job_ptr, __func__))
			continue;

		/*
		 * If the dependency is already invalid there's no reason to
		 * keep checking it.
		 */
		if (job_ptr->state_reason == WAIT_DEP_INVALID)
			continue;
		if (test_job_dependency(job_ptr, NULL) == FAIL_DEPEND) {
			/* Check what are the job disposition
			 * to deal with invalid dependecies
			 */
			handle_invalid_dependency(job_ptr);
		}
	}
	list_iterator_destroy(job_iterator);
	fed_mgr_test_remote_dependencies();

	i = list_delete_all(job_list, &_list_find_job_old, "");
	if (i) {
		debug2("purge_old_job: purged %d old job records", i);
		last_job_update = time(NULL);
		slurm_mutex_lock(&purge_thread_lock);
		slurm_cond_signal(&purge_thread_cond);
		slurm_mutex_unlock(&purge_thread_lock);
	}
}

extern void free_old_jobs(void)
{
	job_record_t *job_ptr;
	/*
	 * Delete records one-by-one to avoid blocking purge_job_record().
	 */
	while ((job_ptr = list_pop(purge_jobs_list)))
		job_record_delete(job_ptr);
}

/*
 * purge_job_record - purge specific job record. No testing is performed to
 *	ensure the job records has no active references. Use only for job
 *	records that were never fully operational (e.g. WILL_RUN test, failed
 *	job load, failed job create, etc.).
 * IN job_id - job_id of job record to be purged
 * RET int - count of job's purged
 * global: job_list - global job table
 */
extern int purge_job_record(uint32_t job_id)
{
	int count = 0;
	count = list_delete_all(job_list, _list_find_job_id, (void *)&job_id);
	if (count) {
		last_job_update = time(NULL);
		slurm_mutex_lock(&purge_thread_lock);
		slurm_cond_signal(&purge_thread_cond);
		slurm_mutex_unlock(&purge_thread_lock);
	}

	return count;
}

extern void unlink_job_record(job_record_t *job_ptr)
{
	uint32_t *job_id;

	xassert(job_ptr->magic == JOB_MAGIC);

	_delete_job_common(job_ptr);

	job_id = xmalloc(sizeof(uint32_t));
	*job_id = job_ptr->job_id;
	list_enqueue(purge_files_list, job_id);

	job_ptr->job_id = NO_VAL;

	last_job_update = time(NULL);
	slurm_mutex_lock(&purge_thread_lock);
	slurm_cond_signal(&purge_thread_cond);
	slurm_mutex_unlock(&purge_thread_lock);
}

/* update first assigned job id as needed on reconfigure */
void reset_first_job_id(void)
{
	xassert(verify_lock(CONF_LOCK, READ_LOCK));
	job_id_sequence = MAX(job_id_sequence, slurm_conf.first_job_id);
}

/*
 * Return the next available job_id to be used.
 *
 * IN test_only - if true, doesn't advance the job_id sequence, just returns
 * 	what the next job id will be.
 * RET a valid job_id or SLURM_ERROR if all job_ids are exhausted.
 */
extern uint32_t get_next_job_id(bool test_only)
{
	int i;
	uint32_t new_id, max_jobs, tmp_id_sequence;

	xassert(verify_lock(JOB_LOCK, READ_LOCK));
	xassert(test_only || verify_lock(JOB_LOCK, WRITE_LOCK));
	xassert(verify_lock(FED_LOCK, READ_LOCK));

	max_jobs = slurm_conf.max_job_id - slurm_conf.first_job_id;
	tmp_id_sequence = MAX(job_id_sequence, slurm_conf.first_job_id);

	/* Ensure no conflict in job id if we roll over 32 bits */
	for (i = 0; i < max_jobs; i++) {
		if (tmp_id_sequence >= slurm_conf.max_job_id)
			tmp_id_sequence = slurm_conf.first_job_id;

		new_id = fed_mgr_get_job_id(tmp_id_sequence);

		if (find_job_record(new_id)) {
			tmp_id_sequence++;
			continue;
		}
		if (_dup_job_file_test(new_id)) {
			tmp_id_sequence++;
			continue;
		}

		if (!test_only)
			job_id_sequence = tmp_id_sequence + 1;

		return new_id;
	}

	error("We have exhausted our supply of valid job id values. FirstJobId=%u MaxJobId=%u",
	      slurm_conf.first_job_id, slurm_conf.max_job_id);
	return SLURM_ERROR;
}

/*
 * _set_job_id - set a default job_id, ensure that it is unique
 * IN job_ptr - pointer to the job_record
 */
static int _set_job_id(job_record_t *job_ptr)
{
	uint32_t new_id;

	xassert(job_ptr);
	xassert (job_ptr->magic == JOB_MAGIC);

	if ((new_id = get_next_job_id(false)) != SLURM_ERROR) {
		job_ptr->job_id = new_id;
		/* When we get a new job id might as well make sure
		 * the db_index is set since there is no way it will be
		 * correct otherwise :). */
		job_record_set_sluid(job_ptr);
		return SLURM_SUCCESS;
	}

	job_ptr->job_id = NO_VAL;
	return EAGAIN;
}


/*
 * set_job_prio - set a default job priority
 * IN job_ptr - pointer to the job_record
 */
extern void set_job_prio(job_record_t *job_ptr)
{
	uint32_t relative_prio;

	xassert(job_ptr);
	xassert (job_ptr->magic == JOB_MAGIC);

	if (IS_JOB_FINISHED(job_ptr))
		return;
	job_ptr->priority = priority_g_set(lowest_prio, job_ptr);
	if ((job_ptr->priority == 0) || (job_ptr->direct_set_prio))
		return;

	relative_prio = job_ptr->priority;
	if (job_ptr->details && (job_ptr->details->nice != NICE_OFFSET)) {
		int64_t offset = job_ptr->details->nice;
		offset -= NICE_OFFSET;
		relative_prio += offset;
	}
	lowest_prio = MIN(relative_prio, lowest_prio);
}

/* After recovering job state, if using priority/basic then we increment the
 * priorities of all jobs to avoid decrementing the base down to zero */
extern void sync_job_priorities(void)
{
	uint32_t prio_boost = 0;

	if ((highest_prio != 0) && (highest_prio < TOP_PRIORITY))
		prio_boost = TOP_PRIORITY - highest_prio;

	prio_boost = priority_g_recover(prio_boost);
	lowest_prio += prio_boost;
}

/*
 * _higher_precedence - determine if job_ptr should be considered before
 *	job_ptr2 when scheduling jobs at submission time.
 *	This compares priority, submit time, and job id (in this order).
 *
 * IN job_ptr - pointer to first job
 * IN job_ptr2 - pointer to second job
 * RET true if job_ptr has higher scheduling precedence over job_ptr2
 */
static bool _higher_precedence(job_record_t *job_ptr, job_record_t *job_ptr2)
{
	xassert(job_ptr);
	xassert(job_ptr2);

	/* Compare priority */
	if (job_ptr->priority > job_ptr2->priority)
		return true;
	if (job_ptr2->priority > job_ptr->priority)
		return false;

	/* Compare submit time */
	if (job_ptr->details->submit_time && job_ptr2->details->submit_time) {
		if (job_ptr->details->submit_time <
		    job_ptr2->details->submit_time)
			return true;
		if (job_ptr2->details->submit_time <
		    job_ptr->details->submit_time)
			return false;
	}

	/* Compare job id */
	return job_ptr->job_id < job_ptr2->job_id;
}

static int _is_flex_or_any_nodes(void *x, void *none)
{
	slurmctld_resv_t *resv_ptr = x;
	xassert(resv_ptr);
	if (resv_ptr->flags & (RESERVE_FLAG_FLEX | RESERVE_FLAG_ANY_NODES))
		return true;
	return false;
}

static bool _use_none_resv_nodes(job_record_t *job_ptr)
{
	if (!job_ptr->resv_name)
		return true; /* no reservation is used */
	if (!job_ptr->resv_list)
		return _is_flex_or_any_nodes(job_ptr->resv_ptr, NULL);
	return list_find_first(job_ptr->resv_list, _is_flex_or_any_nodes, NULL);
}

static int _match_resv_id(void *x, void *key)
{
	slurmctld_resv_t *resv_ptr = (slurmctld_resv_t *) x;
	uint32_t *resv_id = (uint32_t *) key;

	xassert(resv_ptr);

	if (resv_ptr->resv_id != *resv_id)
		return 0;
	else
		return 1; /* match */
}

static int _will_resv_allow_warn_time(void *x, void *arg)
{
	slurmctld_resv_t *resv_ptr = x;
	uint16_t *warn_time = arg;

	xassert(resv_ptr);
	xassert(warn_time);

	if (resv_ptr->max_start_delay &&
	    (*warn_time <= resv_ptr->max_start_delay))
		return true;

	return false;
}

static bool _can_resv_overlap(top_prio_args_t *job_args, job_record_t *job_ptr2)
{
	job_record_t *job_ptr1 = job_args->job_ptr;
	slurmctld_resv_t* cur_resv1;
	slurmctld_resv_t* cur_resv2;
	list_itr_t *resv_iter2;

	if (job_args->use_none_resv_nodes && _use_none_resv_nodes(job_ptr2))
		return true;

	/*
	 * If job_ptr1 does not have a resv but uses --signal=R, check if any of
	 * job_ptr2's resv will allow overlap.
	 */
	if (!job_ptr1->resv_ptr && job_ptr2->resv_ptr &&
	    (job_ptr1->warn_flags & KILL_JOB_RESV)) {
		if (!job_ptr2->resv_list)
			return _will_resv_allow_warn_time(job_ptr2->resv_ptr,
							  &job_ptr1->warn_time);
		return list_find_first(job_ptr2->resv_list,
				       _will_resv_allow_warn_time,
				       &job_ptr1->warn_time);
	}

	/* If 0-1 resv is used per job see if they match */
	if (!job_ptr1->resv_list && !job_ptr2->resv_list)
		return !xstrcmp(job_ptr1->resv_name, job_ptr2->resv_name);

	/* If one doesn't use resv at this point they can't overlap */
	if (!job_ptr1->resv_ptr || !job_ptr2->resv_ptr)
		return false;

	/* If one has a list of resv and the other has one resv */
	if (job_ptr1->resv_list && !job_ptr2->resv_list)
		return list_find_first(job_ptr1->resv_list, _match_resv_id,
				       &job_ptr2->resv_ptr->resv_id);
	if (job_ptr2->resv_list && !job_ptr1->resv_list)
		return list_find_first(job_ptr2->resv_list, _match_resv_id,
				       &job_ptr1->resv_ptr->resv_id);

	/* Both jobs have resv lists - Note resv_list is sorted by id */
	xassert(job_args->resv_list_iter);

	list_iterator_reset(job_args->resv_list_iter);
	resv_iter2 = list_iterator_create(job_ptr2->resv_list);
	while ((cur_resv1 = list_next(job_args->resv_list_iter))) {
		while ((cur_resv2 = list_next(resv_iter2))) {
			if (cur_resv1->resv_id == cur_resv2->resv_id) {
				list_iterator_destroy(resv_iter2);
				return true;
			} else if (cur_resv1->resv_id < cur_resv2->resv_id)
				break;
		}
		if (!cur_resv2)
			break;
	}
	list_iterator_destroy(resv_iter2);
	return false;
}

static int _union_part_nodes(void *x, void *args)
{
	part_record_t *part_ptr = x;
	bitstr_t *node_bitmap = args;

	xassert(part_ptr);
	xassert(node_bitmap);

	bit_or(node_bitmap, part_ptr->node_bitmap);
	return SLURM_SUCCESS;
}

static bitstr_t *_get_all_part_nodes(job_record_t *job_ptr)
{
	bitstr_t *node_bitmap = NULL;

	if (!job_ptr->part_ptr_list)
		return bit_copy(job_ptr->part_ptr->node_bitmap);

	node_bitmap = bit_alloc(bit_size(job_ptr->part_ptr->node_bitmap));
	list_for_each(job_ptr->part_ptr_list, _union_part_nodes, node_bitmap);
	return node_bitmap;
}

/* Return 1 if higher, 0 if the same, and -1 if lower */
static int _cmp_part_prio_tier(top_prio_args_t *job_args,
			       job_record_t *job_ptr2)
{
	uint16_t max_prio_tier2 = job_ptr2->part_ptr->priority_tier;
	if (job_ptr2->part_ptr_list) {
		/* part_ptr_list is sorted by priority tier */
		part_record_t *part_ptr = list_peek(job_ptr2->part_ptr_list);
		max_prio_tier2 = part_ptr->priority_tier;
	}

	/*
	 * Comparing the min partition priority tier of job_ptr1
	 * (the job in job_args) to the max of job_ptr2 is an optimization. It
	 * will prevent job_ptr1 from being considered top priority if it is
	 * possible for it to start in a lower priority tier partition than what
	 * job_ptr2 could start in, even if job_ptr1 could also potentially
	 * start in a higher priority tier partition.
	 */
	if (job_args->min_part_prio_tier > max_prio_tier2)
		return 1;
	if (job_args->min_part_prio_tier == max_prio_tier2)
		return 0;
	return -1;
}

static int _set_min_prio_tier(void *x, void *arg)
{
	part_record_t * part_ptr = x;
	uint16_t *min_prio_tier = arg;

	xassert(part_ptr);
	xassert(min_prio_tier);

	if (part_ptr->priority_tier < *min_prio_tier)
		*min_prio_tier = part_ptr->priority_tier;

	return SLURM_SUCCESS;
}

static void _init_top_prio_args(job_record_t *job_ptr, top_prio_args_t *args)
{
	args->job_ptr = job_ptr;
	args->min_part_prio_tier = job_ptr->part_ptr->priority_tier;
	if (job_ptr->part_ptr_list)
		list_for_each(job_ptr->part_ptr_list, _set_min_prio_tier,
			      &args->min_part_prio_tier);
	args->part_nodes = NULL; /* This is set later if necessary */
	if (job_ptr->resv_list)
		args->resv_list_iter = list_iterator_create(job_ptr->resv_list);
	args->use_none_resv_nodes = _use_none_resv_nodes(job_ptr);
}

static void _destroy_top_prio_args(top_prio_args_t *args)
{
	if (!args || !args->job_ptr)
		return;

	/* Intentionaly not freeing the job_ptr */
	FREE_NULL_BITMAP(args->part_nodes);
	if (args->resv_list_iter)
		list_iterator_destroy(args->resv_list_iter);
}

/*
 * _top_priority - determine if any other job has a higher priority than the
 *	specified job
 * IN job_ptr - pointer to selected job
 * RET true if selected job has highest priority
 */
static bool _top_priority(job_record_t *job_ptr, uint32_t het_job_offset)
{
	job_details_t *detail_ptr = job_ptr->details;
	time_t now = time(NULL);
	int pend_time;
	bool top;

	if (job_ptr->priority == 0)	/* user held */
		top = false;
	else {
		list_itr_t *job_iterator;
		job_record_t *job_ptr2;
		top_prio_args_t job_args = {0};
		bool parts_overlap = false;

		top = true;	/* assume top priority until found otherwise */
		job_iterator = list_iterator_create(job_list);
		while ((job_ptr2 = list_next(job_iterator))) {
			bool overlap_with_resv = false;
			int part_prio_cmp;
			bitstr_t *node_bitmap2 = NULL;

			if (!job_args.job_ptr)
				_init_top_prio_args(job_ptr, &job_args);

			if (job_ptr2 == job_ptr)
				continue;
			if ((het_job_offset != NO_VAL) && (job_ptr->job_id ==
							   (job_ptr2->job_id + het_job_offset)))
				continue;
			if (!IS_JOB_PENDING(job_ptr2))
				continue;
			if (IS_JOB_COMPLETING(job_ptr2)) {
				/* Job is hung in pending & completing state,
				 * indicative of job requeue */
				continue;
			}

			if (bf_min_age_reserve) {
				if (job_ptr2->details->begin_time == 0)
					continue;
				pend_time = difftime(now, job_ptr2->
						     details->begin_time);
				if (pend_time < bf_min_age_reserve)
					continue;
			}
			if (job_state_reason_check(job_ptr2->state_reason,
				    JSR_QOS_ASSOC | JSR_MISC | JSR_PART) ||
			    !job_independent(job_ptr2))
				continue;

			if (job_ptr->resv_name && !job_ptr2->resv_name)
				continue; /* job's with resv have priority */
			if (!_can_resv_overlap(&job_args, job_ptr2))
				continue; /* job can't overlap nodes */
			if (!job_ptr->resv_name && job_ptr2->resv_name)
				overlap_with_resv = true;

			if (bb_g_job_test_stage_in(job_ptr2, true) != 1)
				continue;	/* Waiting for buffer */

			/*
			 * Priority tiers doesn't matter if job_ptr2 uses a resv
			 * and job_ptr does not since resv take precedence
			 */
			part_prio_cmp = overlap_with_resv ?
				-1 : _cmp_part_prio_tier(&job_args, job_ptr2);
			if ((part_prio_cmp == 1) ||
			    ((part_prio_cmp == 0) &&
			     _higher_precedence(job_ptr, job_ptr2)))
				continue;

			/*
			 * Here job_ptr2 is either in a higher priority tier
			 * partition or is using a resv while job_ptr is not.
			 * If partions overlap job_ptr is not top priority.
			 */
			if (!job_args.part_nodes) {
				job_args.part_nodes =
					_get_all_part_nodes(job_ptr);
			}
			node_bitmap2 = _get_all_part_nodes(job_ptr2);
			parts_overlap = bit_overlap_any(job_args.part_nodes,
							node_bitmap2);
			FREE_NULL_BITMAP(node_bitmap2);

			if(!parts_overlap)
				continue;   /* no nodes overlap in partitions */

			top = false;
			break;
		}
		list_iterator_destroy(job_iterator);
		_destroy_top_prio_args(&job_args);
	}

	if ((!top) && detail_ptr) {	/* not top prio */
		if (job_ptr->priority == 0) {		/* user/admin hold */
			if (job_ptr->state_reason != FAIL_BAD_CONSTRAINTS
			    && (job_ptr->state_reason != WAIT_RESV_DELETED)
			    && (job_ptr->state_reason != FAIL_BURST_BUFFER_OP)
			    && (job_ptr->state_reason != FAIL_ACCOUNT)
			    && (job_ptr->state_reason != FAIL_QOS)
			    && (job_ptr->state_reason != WAIT_HELD)
			    && (job_ptr->state_reason != WAIT_HELD_USER)
			    && job_ptr->state_reason != WAIT_MAX_REQUEUE) {
				job_ptr->state_reason = WAIT_HELD;
				xfree(job_ptr->state_desc);
			}
		} else if (job_ptr->state_reason == WAIT_NO_REASON &&
			   het_job_offset == NO_VAL) {
			job_ptr->state_reason = WAIT_PRIORITY;
			xfree(job_ptr->state_desc);
		}
	}
	return top;
}

static void _merge_job_licenses(job_record_t *shrink_job_ptr,
				job_record_t *expand_job_ptr)
{
	xassert(shrink_job_ptr);
	xassert(expand_job_ptr);

	/* FIXME: do we really need to update accounting here?  It
	 * might already happen */

	if (!shrink_job_ptr->licenses)		/* No licenses to add */
		return;

	if (!expand_job_ptr->licenses) {	/* Just transfer licenses */
		expand_job_ptr->licenses = shrink_job_ptr->licenses;
		shrink_job_ptr->licenses = NULL;
		FREE_NULL_LIST(expand_job_ptr->license_list);
		expand_job_ptr->license_list = shrink_job_ptr->license_list;
		shrink_job_ptr->license_list = NULL;
		return;
	}

	/* Merge the license information into expanding job */
	xstrcat(expand_job_ptr->licenses, ",");
	xstrcat(expand_job_ptr->licenses, shrink_job_ptr->licenses);
	xfree(shrink_job_ptr->licenses);
	FREE_NULL_LIST(expand_job_ptr->license_list);
	FREE_NULL_LIST(shrink_job_ptr->license_list);
	license_job_merge(expand_job_ptr);
}

static void _hold_job_rec(job_record_t *job_ptr, uid_t uid)
{
	int i, j;
	time_t now = time(NULL);

	job_ptr->direct_set_prio = 1;
	job_ptr->priority = 0;

	if (job_ptr->details && (job_ptr->details->begin_time < now))
		job_ptr->details->begin_time = 0;

	/* Update job with new begin_time. */
	jobacct_storage_g_job_start(acct_db_conn, job_ptr);

	if (IS_JOB_PENDING(job_ptr))
		acct_policy_remove_accrue_time(job_ptr, false);

	if (job_ptr->part_ptr_list &&
	    job_ptr->part_prio &&
	    job_ptr->part_prio->priority_array) {
		j = list_count(job_ptr->part_ptr_list);
		for (i = 0; i < j; i++) {
			job_ptr->part_prio->priority_array[i] = 0;
		}
	}
	sched_info("%s: hold on %pJ by uid %u", __func__, job_ptr, uid);
}

static void _hold_job(job_record_t *job_ptr, uid_t uid)
{
	job_record_t *het_job_leader = NULL, *het_job;
	list_itr_t *iter;

	if (job_ptr->het_job_id && _get_whole_hetjob())
		het_job_leader = find_job_record(job_ptr->het_job_id);
	if (het_job_leader && het_job_leader->het_job_list) {
		iter = list_iterator_create(het_job_leader->het_job_list);
		while ((het_job = list_next(iter)))
			_hold_job_rec(het_job, uid);
		list_iterator_destroy(iter);
		return;
	}
	_hold_job_rec(job_ptr, uid);
}

static void _release_job_rec(job_record_t *job_ptr, uid_t uid)
{
	time_t now = time(NULL);
	if (job_ptr->details && (job_ptr->details->begin_time < now))
		job_ptr->details->begin_time = 0;
	job_ptr->direct_set_prio = 0;
	set_job_prio(job_ptr);
	job_ptr->state_reason = WAIT_NO_REASON;
	job_state_unset_flag(job_ptr, JOB_SPECIAL_EXIT);
	xfree(job_ptr->state_desc);
	job_ptr->exit_code = 0;
	fed_mgr_job_requeue(job_ptr); /* submit sibling jobs */
	sched_info("%s: release hold on %pJ by uid %u",
		   __func__, job_ptr, uid);
}

static void _release_job(job_record_t *job_ptr, uid_t uid)
{
	job_record_t *het_job_leader = NULL, *het_job;
	list_itr_t *iter;

	if (job_ptr->het_job_id && _get_whole_hetjob())
		het_job_leader = find_job_record(job_ptr->het_job_id);
	if (het_job_leader && het_job_leader->het_job_list) {
		iter = list_iterator_create(het_job_leader->het_job_list);
		while ((het_job = list_next(iter)))
			_release_job_rec(het_job, uid);
		list_iterator_destroy(iter);
		return;
	}
	_release_job_rec(job_ptr, uid);
}

/*
 * Gets a new association giving priority to the given parameters in job_desc,
 * and if not possible using the job_ptr ones.
 * IN job_desc: The new job description to use for getting the assoc_ptr.
 * IN job_ptr: The original job_ptr to use when parameters are not in job_desc.
 * RET assoc_rec, the new association combining the most updated information
 * from job_desc.
 */
static slurmdb_assoc_rec_t *_retrieve_new_assoc(job_desc_msg_t *job_desc,
						job_record_t *job_ptr)
{
	slurmdb_assoc_rec_t assoc_rec, *assoc_ptr = NULL;

	memset(&assoc_rec, 0, sizeof(assoc_rec));

	if (job_desc->partition) {
		part_record_t *part_ptr = NULL;
		int error_code =
			_get_job_parts(job_desc, &part_ptr, NULL, NULL);
		/* We don't need this we only care about part_ptr */
		if (error_code != SLURM_SUCCESS) {
			errno = error_code;
			return NULL;
		} else if (!(part_ptr->state_up & PARTITION_SUBMIT)) {
			errno = ESLURM_PARTITION_NOT_AVAIL;
			return NULL;
		}

		assoc_rec.partition = part_ptr->name;
	} else if (job_ptr->part_ptr)
		assoc_rec.partition = job_ptr->part_ptr->name;

	if (job_desc->account)
		assoc_rec.acct = job_desc->account;
	else
		assoc_rec.acct = job_ptr->account;

	assoc_rec.uid = job_ptr->user_id;

	if (assoc_mgr_fill_in_assoc(acct_db_conn, &assoc_rec,
				    accounting_enforce,
				    &assoc_ptr, false)) {
		info("%s: invalid account %s for %pJ",
		     __func__, assoc_rec.acct, job_ptr);
		errno = ESLURM_INVALID_ACCOUNT;
		return NULL;
	} else if (slurm_with_slurmdbd() &&
		   !assoc_ptr &&
		   !(accounting_enforce & ACCOUNTING_ENFORCE_ASSOCS) &&
		   assoc_rec.acct) {
		/* if not enforcing associations we want to look for
		 * the default account and use it to avoid getting
		 * trash in the accounting records.
		 */
		assoc_rec.acct = NULL;
		(void) assoc_mgr_fill_in_assoc(acct_db_conn, &assoc_rec,
					       accounting_enforce,
					       &assoc_ptr, false);
	}

	return assoc_ptr;
}

/* Allocate nodes to new job. Old job info will be cleared at epilog complete */
static void _realloc_nodes(job_record_t *job_ptr, bitstr_t *orig_node_bitmap)
{
	bitstr_t *node_bitmap;
	node_record_t *node_ptr;

	xassert(job_ptr);
	xassert(orig_node_bitmap);
	if (!job_ptr->job_resrcs || !job_ptr->job_resrcs->node_bitmap)
		return;

	node_bitmap = job_ptr->job_resrcs->node_bitmap;
	for (int i = 0; (node_ptr = next_node_bitmap(node_bitmap, &i)); i++) {
		if (bit_test(orig_node_bitmap, i))
			continue;
		make_node_alloc(node_ptr, job_ptr);
	}
	node_mgr_make_node_blocked(job_ptr, true);
}

extern bool permit_job_expansion(void)
{
	static time_t sched_update = 0;
	static bool permit_job_expansion = false;

	if (sched_update != slurm_conf.last_update) {
		sched_update = slurm_conf.last_update;
		if (xstrcasestr(slurm_conf.sched_params,
		                "permit_job_expansion"))
			permit_job_expansion = true;
		else
			permit_job_expansion = false;
	}

	return permit_job_expansion;
}

extern bool permit_job_shrink(void)
{
	static time_t sched_update = 0;
	static bool permit_job_shrink = false;

	if (sched_update != slurm_conf.last_update) {
		sched_update = slurm_conf.last_update;
		if (xstrcasestr(slurm_conf.sched_params, "disable_job_shrink"))
			permit_job_shrink = false;
		else
			permit_job_shrink = true;
	}

	return permit_job_shrink;
}

static int _update_job(job_record_t *job_ptr, job_desc_msg_t *job_desc,
		       uid_t uid, char **err_msg)
{
	int error_code = SLURM_SUCCESS;
	enum job_state_reason fail_reason;
	bool operator = false;
	bool is_coord_oldacc = false, is_coord_newacc = false;
	uint32_t save_min_nodes = 0, save_max_nodes = 0;
	uint32_t save_min_cpus = 0, save_max_cpus = 0;
	job_details_t *detail_ptr;
	part_record_t *new_part_ptr = NULL, *use_part_ptr = NULL;
	bitstr_t *exc_bitmap = NULL, *new_req_bitmap = NULL;
	bitstr_t *orig_job_node_bitmap = NULL;
	time_t now = time(NULL);
	multi_core_data_t *mc_ptr = NULL;
	bool update_accounting = false, new_req_bitmap_given = false;
	acct_policy_limit_set_t acct_policy_limit_set;
	uint16_t tres[slurmctld_tres_cnt];
	bool acct_limit_already_exceeded;
	bool tres_changed = false;
	int tres_pos;
	uint64_t tres_req_cnt[slurmctld_tres_cnt];
	bool tres_req_cnt_set = false, valid_licenses = false;
	list_t *gres_list = NULL, *license_list = NULL;
	list_t *part_ptr_list = NULL;
	uint32_t orig_time_limit;
	bool gres_update = false;
	slurmdb_assoc_rec_t *new_assoc_ptr = NULL, *use_assoc_ptr = NULL;
	slurmdb_qos_rec_t *new_qos_ptr = NULL, *use_qos_ptr = NULL;
	slurmctld_resv_t *new_resv_ptr = NULL;
	list_t *new_resv_list = NULL;
	list_t *new_qos_list = NULL;
	uint32_t user_site_factor;
	uint32_t new_qos_id = 0;
	uint64_t mem_req;

	assoc_mgr_lock_t locks = { .tres = READ_LOCK };
	assoc_mgr_lock_t assoc_mgr_read_lock = {
		.assoc = READ_LOCK,
		.qos = READ_LOCK,
		.user = READ_LOCK,
	};

	/*
	 * Block scontrol updates of scrontab jobs.
	 */
	if (job_ptr->bit_flags & CRON_JOB)
		return ESLURM_CANNOT_MODIFY_CRON_JOB;

	operator = validate_operator(uid);

	/* Check authorization for modifying this job */
	is_coord_oldacc = assoc_mgr_is_user_acct_coord(acct_db_conn, uid,
						       job_ptr->account,
						       false);
	is_coord_newacc = assoc_mgr_is_user_acct_coord(acct_db_conn, uid,
						       job_desc->account,
						       false);
	if ((job_ptr->user_id != uid) && !operator) {
		/*
		 * Fail if we are not coordinators of the current account or
		 * if we are changing an account and  we are not coordinators
		 * of both src and dest accounts.
		 */
		if (!is_coord_oldacc ||
		    (!is_coord_newacc && job_desc->account)) {
			error("Security violation, JOB_UPDATE RPC from uid %u",
			      uid);
			return ESLURM_USER_ID_MISSING;
		}
	}

	if (job_desc->burst_buffer) {
		/*
		 * burst_buffer contents are validated at job submit time and
		 * data is possibly being staged at later times. It can not
		 * be changed except to clear the value on a completed job and
		 * purge the record in order to recover from a failure mode
		 */
		if (IS_JOB_COMPLETED(job_ptr) && operator &&
		    (job_desc->burst_buffer[0] == '\0')) {
			xfree(job_ptr->burst_buffer);
			last_job_update = now;
		} else {
			error_code = ESLURM_NOT_SUPPORTED;
		}
	}
	if (error_code != SLURM_SUCCESS)
		goto fini;

	if (job_desc->array_inx && job_ptr->array_recs) {
		int throttle;
		throttle = strtoll(job_desc->array_inx, (char **) NULL, 10);
		if (throttle >= 0) {
			info("%s: set max_run_tasks to %d for job array %pJ",
			     __func__, throttle, job_ptr);
			job_ptr->array_recs->max_run_tasks = throttle;
		} else {
			info("%s: invalid max_run_tasks of %d for job array %pJ, ignored",
			     __func__, throttle, job_ptr);
			error_code = ESLURM_BAD_TASK_COUNT;
		}
		/*
		 * Even if the job is complete, permit changing
		 * ArrayTaskThrottle for other elements of the task array
		 */
		if (IS_JOB_FINISHED(job_ptr))
			goto fini;
	}

	if (IS_JOB_FINISHED(job_ptr)) {
		error_code = ESLURM_JOB_FINISHED;
		goto fini;
	}

	/*
	 * Validate before job_submit_g_modify() so that the job_submit
	 * plugin can make changes to the field without triggering an auth
	 * issue.
	 */
	if (job_desc->admin_comment && !validate_super_user(uid)) {
		error("Attempt to change admin_comment for %pJ", job_ptr);
		error_code = ESLURM_ACCESS_DENIED;
		goto fini;
	}

	/* Save before submit plugin potentially modifies it. */
	user_site_factor = job_desc->site_factor;

	if (job_desc->user_id == SLURM_AUTH_NOBODY) {
		/*
		 * Used by job_submit/lua to find default partition and
		 * access control logic below to validate partition change
		 */
		job_desc->user_id = job_ptr->user_id;
	}
	error_code = job_submit_g_modify(job_desc, job_ptr, uid, err_msg);
	if (error_code != SLURM_SUCCESS)
		return error_code;

	error_code = _test_job_desc_fields(job_desc);
	if (error_code != SLURM_SUCCESS)
		return error_code;

	memset(&acct_policy_limit_set, 0, sizeof(acct_policy_limit_set));
	acct_policy_limit_set.tres = tres;

	if (operator) {
		/* set up the acct_policy if we are at least an operator */
		for (tres_pos = 0; tres_pos < slurmctld_tres_cnt; tres_pos++)
			acct_policy_limit_set.tres[tres_pos] = ADMIN_SET_LIMIT;
		acct_policy_limit_set.time = ADMIN_SET_LIMIT;
		acct_policy_limit_set.qos = ADMIN_SET_LIMIT;
	} else
		memset(tres, 0, sizeof(tres));

	detail_ptr = job_ptr->details;
	if (detail_ptr)
		mc_ptr = detail_ptr->mc_ptr;
	last_job_update = now;

	/*
	 * Check to see if the new requested job_desc exceeds any
	 * existing limit. If it passes, cool, we will check the new
	 * association/qos/part later in the code and fail if it is wrong.
	 *
	 * If it doesn't pass this mean some limit was exceededed before the
	 * update request so let's keep the user continue screwing up herself
	 * with the limit if it is what she wants. We do this by not exiting
	 * on the later call to acct_policy_validate() if it fails.
	 *
	 * We will also prevent the update to return an error code that is
	 * confusing since many things could successfully update and we are now
	 * just already violating a limit. The job won't be allowed to run,
	 * but it will allow the update to happen which is most likely what
	 * was desired.
	 *
	 * Changes in between this check and the next acct_policy_validate()
	 * will not be constrained to accounting enforce limits.
	 */
	orig_time_limit = job_desc->time_limit;


	/*
	 * We need to figure out if we changed task cnt.
	 */
	_figure_out_num_tasks(job_desc, job_ptr);

	memcpy(tres_req_cnt, job_ptr->tres_req_cnt, sizeof(tres_req_cnt));
	job_desc->tres_req_cnt = tres_req_cnt;
	tres_req_cnt_set = true;

	acct_limit_already_exceeded = false;

	if (!operator && (accounting_enforce & ACCOUNTING_ENFORCE_LIMITS)) {
		if (!acct_policy_validate(job_desc, job_ptr->part_ptr,
					  job_ptr->part_ptr_list,
					  job_ptr->assoc_ptr, job_ptr->qos_ptr,
					  NULL, &acct_policy_limit_set,
					  true)) {
			debug("%s: already exceeded association's cpu, node, "
			      "memory or time limit for user %u",
			      __func__, job_desc->user_id);
			acct_limit_already_exceeded = true;
		}
		job_desc->time_limit = orig_time_limit;
	}

	/*
	 * The partition, assoc, qos, reservation, and req_node_bitmap all have
	 * to be set before checking later.  So here we set them into temporary
	 * variables set in the job way later.
	 */
	if (job_desc->partition &&
	    !xstrcmp(job_desc->partition, job_ptr->partition)) {
		sched_debug("%s: new partition identical to old partition %pJ",
			    __func__, job_ptr);
	} else if (job_desc->partition) {
		if (!IS_JOB_PENDING(job_ptr)) {
			error_code = ESLURM_JOB_NOT_PENDING;
			goto fini;
		}

		error_code = _get_job_parts(job_desc,
					    &new_part_ptr,
					    &part_ptr_list, NULL);

		if (error_code != SLURM_SUCCESS)
			;
		else if ((new_part_ptr->state_up & PARTITION_SUBMIT) == 0)
			error_code = ESLURM_PARTITION_NOT_AVAIL;
		else if (!part_ptr_list &&
			 !xstrcmp(new_part_ptr->name, job_ptr->partition)) {
			sched_debug("%s: 2 new partition identical to old partition %pJ",
				    __func__, job_ptr);
			new_part_ptr = NULL;
		}
		if (error_code != SLURM_SUCCESS)
			goto fini;
	}

	use_part_ptr = new_part_ptr ? new_part_ptr : job_ptr->part_ptr;

	/* Check the account and the partition as both affect the association */
	if (job_desc->account || new_part_ptr) {
		if (!IS_JOB_PENDING(job_ptr))
			error_code = ESLURM_JOB_NOT_PENDING;
		else {
			new_assoc_ptr = _retrieve_new_assoc(job_desc, job_ptr);

			if (!new_assoc_ptr)
				error_code = errno;
			else if (new_assoc_ptr == job_ptr->assoc_ptr) {
				new_assoc_ptr = NULL;
				sched_debug("%s: new association identical to old association %u",
					    __func__, job_ptr->job_id);
			}

			/*
			 * Clear errno that may have been set by
			 * _retrieve_new_assoc.
			 */
			errno = 0;
		}

		if (error_code != SLURM_SUCCESS)
			goto fini;
	}

	use_assoc_ptr = new_assoc_ptr ?	new_assoc_ptr : job_ptr->assoc_ptr;

	if (job_desc->qos) {
		char *resv_name;
		assoc_mgr_lock_t qos_read_lock = {
			.qos = READ_LOCK,
		};

		if (job_desc->reservation
		    && job_desc->reservation[0] != '\0')
			resv_name = job_desc->reservation;
		else
			resv_name = job_ptr->resv_name;

		assoc_mgr_lock(&qos_read_lock);

		error_code = _get_qos_info(job_desc->qos, 0,
					   &new_qos_list,
					   &new_qos_ptr,
					   resv_name,
					   use_assoc_ptr,
					   operator, true, LOG_LEVEL_ERROR);
		if ((error_code == SLURM_SUCCESS) && new_qos_ptr) {
			if (!new_qos_list &&
			    (job_ptr->qos_ptr == new_qos_ptr)) {
				sched_debug("%s: new QOS identical to old QOS %pJ",
					    __func__, job_ptr);
				new_qos_ptr = NULL;
			} else if (!IS_JOB_PENDING(job_ptr)) {
				error_code = ESLURM_JOB_NOT_PENDING;
				new_qos_ptr = NULL;
			}
		}

		if (new_qos_ptr)
			new_qos_id = new_qos_ptr->id;

		assoc_mgr_unlock(&qos_read_lock);

		if (error_code != SLURM_SUCCESS)
			goto fini;
	}

	use_qos_ptr = new_qos_ptr ? new_qos_ptr : job_ptr->qos_ptr;

	if (job_desc->bitflags & RESET_ACCRUE_TIME) {
		if (!IS_JOB_PENDING(job_ptr) || !detail_ptr) {
			error_code = ESLURM_JOB_NOT_PENDING;
			goto fini;
		} else if (detail_ptr->accrue_time) {
			uint64_t bit_flags = job_ptr->bit_flags;
			acct_policy_remove_accrue_time(job_ptr, false);
			/*
			 * Set the accrue_time to 'now' since we are not
			 * removing this job, but resetting the time
			 * instead. Since acct_policy_remove_accrue_time()
			 * will set this to 0 which will cause the next time
			 * through acct_policy_handle_accrue_time() to set
			 * things back to the original time thus making it as if
			 * nothing happened here.
			 *
			 * We also reset the bit_flags to be the same as it was
			 * before so we don't loose JOB_ACCRUE_OVER if set
			 * beforehand.
			 */
			job_ptr->bit_flags = bit_flags;
			detail_ptr->accrue_time = now;
		}
	}

	/*
	 * Before any action over excluded or required nodes, we are going to
	 * reset them to their original values.
	 *
	 * We will decide later if those values need update, or even if we need
	 * to merge the negated required list into the excluded one (when
	 * -N < size required list).
	 */
	FREE_NULL_BITMAP(detail_ptr->exc_node_bitmap);
	if (detail_ptr->exc_nodes) {
		/* This error should never happen */
		if (node_name2bitmap(detail_ptr->exc_nodes,
				     false, &exc_bitmap, NULL)) {
			sched_info("%s: Invalid excluded nodes list in job records: %s",
				   __func__, detail_ptr->exc_nodes);
			FREE_NULL_BITMAP(exc_bitmap);
			error_code = ESLURM_INVALID_NODE_NAME;
			goto fini;
		}
		detail_ptr->exc_node_bitmap = exc_bitmap;
		exc_bitmap = NULL;
	}
	FREE_NULL_BITMAP(detail_ptr->req_node_bitmap);
	if (detail_ptr->req_nodes) {
		/* This error should never happen */
		if (node_name2bitmap(detail_ptr->req_nodes,
				     false, &new_req_bitmap, NULL)) {
			sched_info("%s: Invalid required nodes list in job records: %s",
				   __func__, detail_ptr->req_nodes);
			FREE_NULL_BITMAP(new_req_bitmap);
			error_code = ESLURM_INVALID_NODE_NAME;
			goto fini;
		}
		detail_ptr->req_node_bitmap = new_req_bitmap;
		new_req_bitmap = NULL;
	}

	if (job_desc->exc_nodes && detail_ptr &&
	    !xstrcmp(job_desc->exc_nodes, detail_ptr->exc_nodes)) {
		sched_debug("%s: new exc_nodes identical to old exc_nodes %s",
			    __func__, job_desc->exc_nodes);
	} else if (job_desc->exc_nodes) {
		if ((!IS_JOB_PENDING(job_ptr)) || (detail_ptr == NULL))
			error_code = ESLURM_JOB_NOT_PENDING;
		else if (job_desc->exc_nodes[0] == '\0') {
			xfree(detail_ptr->exc_nodes);
			FREE_NULL_BITMAP(detail_ptr->exc_node_bitmap);
		} else {
			if (node_name2bitmap(job_desc->exc_nodes, false,
					     &exc_bitmap, NULL)) {
				sched_error("%s: Invalid node list for update of %pJ: %s",
					    __func__, job_ptr,
					    job_desc->exc_nodes);
				FREE_NULL_BITMAP(exc_bitmap);
				error_code = ESLURM_INVALID_NODE_NAME;
			}
			if (exc_bitmap) {
				xfree(detail_ptr->exc_nodes);
				detail_ptr->exc_nodes =
					xstrdup(job_desc->exc_nodes);
				FREE_NULL_BITMAP(detail_ptr->exc_node_bitmap);
				detail_ptr->exc_node_bitmap = exc_bitmap;
				sched_info("%s: setting exc_nodes to %s for %pJ",
					   __func__, job_desc->exc_nodes, job_ptr);
			}
		}
	}
	if (error_code != SLURM_SUCCESS)
		goto fini;

	/*
	 * Must check req_nodes to set the job_ptr->details->req_node_bitmap
	 * before we validate it later.
	 */
	if (job_desc->req_nodes && detail_ptr &&
	    !xstrcmp(job_desc->req_nodes, detail_ptr->req_nodes)) {
		sched_debug("%s: new req_nodes identical to old req_nodes %s",
			    __func__, job_desc->req_nodes);
	} else if (job_desc->req_nodes && detail_ptr &&
		   (detail_ptr->task_dist & SLURM_DIST_STATE_BASE) ==
		   SLURM_DIST_ARBITRARY) {
		sched_info("%s: Cannot update node list of %pJ. Not compatible with arbitrary distribution",
		      __func__, job_ptr);
		error_code = ESLURM_NOT_SUPPORTED;
		goto fini;
	} else if (job_desc->req_nodes &&
		   (IS_JOB_RUNNING(job_ptr) || IS_JOB_SUSPENDED(job_ptr))) {
		/*
		 * Use req_nodes to change the nodes associated with a running
		 * for lack of other field in the job request to use
		 */
		if (!permit_job_shrink()) {
			error("%s: request to shrink %pJ denied by configuration",
			      __func__, job_ptr);
			error_code = ESLURM_NOT_SUPPORTED;
			goto fini;
		}

		if ((job_desc->req_nodes[0] == '\0') ||
		    node_name2bitmap(job_desc->req_nodes, false,
				     &new_req_bitmap, NULL) ||
		    !bit_super_set(new_req_bitmap, job_ptr->node_bitmap) ||
		    (job_ptr->details && job_ptr->details->expanding_jobid)) {
			sched_info("%s: Invalid node list (%s) for %pJ update",
				   __func__, job_desc->req_nodes, job_ptr);
			error_code = ESLURM_INVALID_NODE_NAME;
			goto fini;
		}

		if (new_req_bitmap) {
			node_record_t *node_ptr;
			bitstr_t *rem_nodes;

			/*
			 * They requested a new list of nodes for the job. If
			 * the batch host isn't in this list, then deny this
			 * request.
			 */
			if (job_ptr->batch_flag) {
				int batch_inx = node_name_get_inx(
					job_ptr->batch_host);

				if (batch_inx == -1)
					error("%s: Invalid batch host %s for %pJ; this should never happen",
					      __func__, job_ptr->batch_host,
					      job_ptr);
				else if (!bit_test(new_req_bitmap, batch_inx)) {
					error("%s: Batch host %s for %pJ is not in the requested node list %s. You cannot remove the batch host from a job when resizing.",
					      __func__, job_ptr->batch_host,
					      job_ptr, job_desc->req_nodes);
					error_code = ESLURM_INVALID_NODE_NAME;
					goto fini;
				}
			}

			sched_info("%s: setting nodes to %s for %pJ",
				   __func__, job_desc->req_nodes, job_ptr);
			job_pre_resize_acctg(job_ptr);
			rem_nodes = bit_copy(job_ptr->node_bitmap);
			bit_and_not(rem_nodes, new_req_bitmap);
#ifndef HAVE_FRONT_END
			abort_job_on_nodes(job_ptr, rem_nodes);
#endif
			orig_job_node_bitmap =
				bit_copy(job_ptr->job_resrcs->node_bitmap);
			for (int i = 0;
			     (node_ptr = next_node_bitmap(rem_nodes, &i));
			     i++) {
				kill_step_on_node(job_ptr, node_ptr, false);
				excise_node_from_job(job_ptr, node_ptr);
			}
			/* Resize the core bitmaps of the job's steps */
			rebuild_step_bitmaps(job_ptr, orig_job_node_bitmap);

			FREE_NULL_BITMAP(orig_job_node_bitmap);
			FREE_NULL_BITMAP(rem_nodes);
			(void) gs_job_start(job_ptr);
			gres_stepmgr_job_build_details(
				job_ptr->gres_list_alloc,
				job_ptr->nodes,
				&job_ptr->gres_detail_cnt,
				&job_ptr->gres_detail_str,
				&job_ptr->gres_used);
			job_post_resize_acctg(job_ptr);
			/*
			 * Since job_post_resize_acctg will restart
			 * things, don't do it again.
			 */
			update_accounting = false;
		} else {
			update_accounting = true;
		}
		FREE_NULL_BITMAP(new_req_bitmap);
	} else if (job_desc->req_nodes) {
		if ((!IS_JOB_PENDING(job_ptr)) || (detail_ptr == NULL))
			error_code = ESLURM_JOB_NOT_PENDING;
		else if (job_desc->req_nodes[0] == '\0')
			new_req_bitmap_given = true;
		else {
			if (node_name2bitmap(job_desc->req_nodes, false,
					     &new_req_bitmap, NULL)) {
				sched_info("%s: Invalid node list for job_update: %s",
					   __func__, job_desc->req_nodes);
				FREE_NULL_BITMAP(new_req_bitmap);
				error_code = ESLURM_INVALID_NODE_NAME;
			} else
				new_req_bitmap_given = true;
		}
	}

	if (error_code != SLURM_SUCCESS)
		goto fini;

	if (new_req_bitmap_given) {
		xfree(detail_ptr->req_nodes);
		if (job_desc->req_nodes[0] != '\0')
			detail_ptr->req_nodes =	xstrdup(job_desc->req_nodes);
		FREE_NULL_BITMAP(detail_ptr->req_node_bitmap);
		detail_ptr->req_node_bitmap = new_req_bitmap;
		new_req_bitmap = NULL;
		sched_info("%s: setting req_nodes to %s for %pJ",
			   __func__, job_desc->req_nodes, job_ptr);
	}

	/* this needs to be after partition and QOS checks */
	if (job_desc->reservation
	    && (!xstrcmp(job_desc->reservation, job_ptr->resv_name) ||
		(!job_ptr->resv_name && job_desc->reservation[0] == '\0'))) {
		sched_debug("%s: new reservation identical to old reservation %pJ",
			    __func__, job_ptr);
	} else if (job_desc->reservation) {
		if (!IS_JOB_PENDING(job_ptr) && !IS_JOB_RUNNING(job_ptr)) {
			error_code = ESLURM_JOB_NOT_PENDING_NOR_RUNNING;
		} else {
			job_record_t tmp_job_rec;

			memcpy(&tmp_job_rec, job_ptr, sizeof(job_record_t));
			tmp_job_rec.resv_name = xstrdup(job_desc->reservation);
			tmp_job_rec.resv_ptr = NULL;
			tmp_job_rec.resv_list = NULL;
			tmp_job_rec.part_ptr = use_part_ptr;
			tmp_job_rec.qos_ptr = use_qos_ptr;
			tmp_job_rec.assoc_ptr = use_assoc_ptr;

			error_code = validate_job_resv(&tmp_job_rec);

			/*
			 * It doesn't matter what this is, just set it as
			 * failure will be NULL.
			 */
			new_resv_ptr = tmp_job_rec.resv_ptr;
			new_resv_list = tmp_job_rec.resv_list;

			/*
			 * Make sure this job isn't using a partition or QOS
			 * that requires it to be in a reservation.
			 */
			if ((error_code == SLURM_SUCCESS) && !new_resv_ptr) {
				if (use_part_ptr
				    && use_part_ptr->flags & PART_FLAG_REQ_RESV)
					error_code = ESLURM_ACCESS_DENIED;

				if (use_qos_ptr
				    && use_qos_ptr->flags & QOS_FLAG_REQ_RESV)
					error_code = ESLURM_INVALID_QOS;
			}

			if (job_ptr->state_reason == WAIT_RESV_INVALID)
				_release_job(job_ptr, uid);

			xfree(tmp_job_rec.resv_name);
		}
		if (error_code != SLURM_SUCCESS)
			goto fini;
	}

	if (job_desc->cpus_per_tres   || job_desc->tres_per_job    ||
	    job_desc->tres_per_node   || job_desc->tres_per_socket ||
	    job_desc->tres_per_task   || job_desc->mem_per_tres ||
	    (job_desc->bitflags & TASKS_CHANGED))
		gres_update = true;
	if (gres_update) {
		uint16_t orig_ntasks_per_socket = NO_VAL16;
		gres_job_state_validate_t gres_js_val = {
			.cpus_per_task = &job_desc->cpus_per_task,
			.max_nodes = &job_desc->max_nodes,
			.min_cpus = &job_desc->min_cpus,
			.min_nodes = &job_desc->min_nodes,
			.ntasks_per_node = &job_desc->ntasks_per_node,
			.ntasks_per_socket = &job_desc->ntasks_per_socket,
			.ntasks_per_tres = &job_desc->ntasks_per_tres,
			.num_tasks = &job_desc->num_tasks,
			.sockets_per_node = &job_desc->sockets_per_node,

			.gres_list = &gres_list,
		};

		if ((!IS_JOB_PENDING(job_ptr)) || (detail_ptr == NULL) ||
		    (detail_ptr->expanding_jobid != 0)) {
			error_code = ESLURM_JOB_NOT_PENDING;
			goto fini;
		}
		if (!job_desc->cpus_per_tres)
			job_desc->cpus_per_tres =
				xstrdup(job_ptr->cpus_per_tres);
		if (!job_desc->tres_freq)
			job_desc->tres_freq = xstrdup(job_ptr->tres_freq);
		if (!job_desc->tres_per_job)
			job_desc->tres_per_job = xstrdup(job_ptr->tres_per_job);
		if (!job_desc->tres_per_node)
			job_desc->tres_per_node =
				xstrdup(job_ptr->tres_per_node);
		if (!job_desc->tres_per_socket)
			job_desc->tres_per_socket =
				xstrdup(job_ptr->tres_per_socket);
		if (!job_desc->tres_per_task)
			job_desc->tres_per_task =
				xstrdup(job_ptr->tres_per_task);
		if (!job_desc->mem_per_tres)
			job_desc->mem_per_tres = xstrdup(job_ptr->mem_per_tres);
		if (job_desc->num_tasks == NO_VAL)
			job_desc->num_tasks = detail_ptr->num_tasks;
		if (job_desc->min_cpus == NO_VAL)
			job_desc->min_cpus = 0; /* min_cpus could decrease */
		if (job_desc->min_nodes == NO_VAL)
			job_desc->min_nodes = detail_ptr->min_nodes;
		if (job_desc->max_nodes == NO_VAL)
			job_desc->max_nodes = detail_ptr->max_nodes;
		if (job_desc->ntasks_per_node == NO_VAL16)
			job_desc->ntasks_per_node = detail_ptr->ntasks_per_node;
		if ((job_desc->ntasks_per_socket == NO_VAL16) &&
		    (detail_ptr->mc_ptr) &&
		    (detail_ptr->mc_ptr->ntasks_per_socket != INFINITE16)) {
			job_desc->ntasks_per_socket =
				mc_ptr->ntasks_per_socket;
			orig_ntasks_per_socket = job_desc->ntasks_per_socket;
		}
		if (job_desc->sockets_per_node == NO_VAL16)
			job_desc->sockets_per_node =
				detail_ptr->mc_ptr->sockets_per_node;
		if (job_desc->cpus_per_task == NO_VAL16)
			job_desc->cpus_per_task =
				detail_ptr->orig_cpus_per_task;
		if (!job_desc->ntasks_per_tres)
			job_desc->ntasks_per_tres = detail_ptr->ntasks_per_tres;

		gres_js_val.cpus_per_tres = job_desc->cpus_per_tres;
		gres_js_val.mem_per_tres = job_desc->mem_per_tres;
		gres_js_val.tres_freq = job_desc->tres_freq;
		gres_js_val.tres_per_job = job_desc->tres_per_job;
		gres_js_val.tres_per_node = job_desc->tres_per_node;
		gres_js_val.tres_per_socket = job_desc->tres_per_socket;
		gres_js_val.tres_per_task = job_desc->tres_per_task;

		if ((error_code = gres_job_state_validate(&gres_js_val))) {
			sched_info("%s: invalid GRES for %pJ",
				   __func__, job_ptr);
			goto fini;
		}
		if (job_desc->num_tasks == detail_ptr->num_tasks)
			job_desc->num_tasks = NO_VAL;	/* Unchanged */
		if ((job_desc->min_cpus == detail_ptr->min_cpus) ||
		    (job_desc->min_cpus == 0)) /* Unchanged */
			job_desc->min_cpus = NO_VAL;
		if (job_desc->min_nodes == detail_ptr->min_nodes)
			job_desc->min_nodes = NO_VAL;	/* Unchanged */
		if (job_desc->max_nodes == detail_ptr->max_nodes)
			job_desc->max_nodes = NO_VAL;	/* Unchanged */
		if (job_desc->ntasks_per_node == detail_ptr->ntasks_per_node)
			job_desc->ntasks_per_node = NO_VAL16;	/* Unchanged */
		if (job_desc->ntasks_per_socket == orig_ntasks_per_socket)
			job_desc->ntasks_per_socket = NO_VAL16; /* Unchanged */
		if (job_desc->sockets_per_node ==
		    detail_ptr->mc_ptr->sockets_per_node)
			job_desc->sockets_per_node = NO_VAL16;
		if (job_desc->cpus_per_task == detail_ptr->cpus_per_task)
			job_desc->cpus_per_task = NO_VAL16;	/* Unchanged */
		if (job_desc->ntasks_per_tres == detail_ptr->ntasks_per_tres)
			job_desc->ntasks_per_tres = 0;
		if (!xstrcmp(job_desc->cpus_per_tres, job_ptr->cpus_per_tres))
			xfree(job_desc->cpus_per_tres);
		if (!xstrcmp(job_desc->tres_freq, job_ptr->tres_freq))
			xfree(job_desc->tres_freq);
		if (!xstrcmp(job_desc->tres_per_job, job_ptr->tres_per_job))
			xfree(job_desc->tres_per_job);
		if (!xstrcmp(job_desc->tres_per_node, job_ptr->tres_per_node))
			xfree(job_desc->tres_per_node);
		if (!xstrcmp(job_desc->tres_per_socket,
			     job_ptr->tres_per_socket))
			xfree(job_desc->tres_per_socket);
		if (!xstrcmp(job_desc->tres_per_task, job_ptr->tres_per_task))
			xfree(job_desc->tres_per_task);
		if (!xstrcmp(job_desc->mem_per_tres, job_ptr->mem_per_tres))
			xfree(job_desc->mem_per_tres);

	}

	if ((job_desc->min_nodes != NO_VAL) &&
	    (job_desc->min_nodes != INFINITE)) {
		uint32_t min_cpus = (job_desc->pn_min_cpus != NO_VAL16 ?
				     job_desc->pn_min_cpus : detail_ptr->pn_min_cpus) *
			job_desc->min_nodes;
		uint32_t num_cpus = job_desc->min_cpus != NO_VAL ?
			job_desc->min_cpus :
			IS_JOB_PENDING(job_ptr) ?
			job_ptr->tres_req_cnt[TRES_ARRAY_CPU] :
			job_ptr->tres_alloc_cnt[TRES_ARRAY_CPU];
		uint32_t num_tasks = job_desc->num_tasks != NO_VAL ?
			job_desc->num_tasks : detail_ptr->num_tasks;

		if (!num_tasks) {
			num_tasks = job_desc->min_nodes;
		} else if (num_tasks < job_desc->min_nodes) {
			info("%s: adjusting num_tasks (prev: %u) to be at least min_nodes: %u",
			     __func__, num_tasks, job_desc->min_nodes);
			num_tasks = job_desc->min_nodes;
			if (IS_JOB_PENDING(job_ptr))
				job_desc->num_tasks = num_tasks;
		}

		num_tasks *= job_desc->cpus_per_task != NO_VAL16 ?
			job_desc->cpus_per_task : detail_ptr->cpus_per_task;
		num_tasks = MAX(num_tasks, min_cpus);
		if (num_tasks > num_cpus) {
			info("%s: adjusting min_cpus (prev: %u) to be at least : %u",
			     __func__, num_cpus, num_tasks);
			job_desc->min_cpus = num_tasks;

			job_desc->pn_min_memory =
				job_desc->pn_min_memory != NO_VAL64 ?
				job_desc->pn_min_memory :
				detail_ptr->pn_min_memory;
		}

		assoc_mgr_lock(&locks);

		if (!job_desc->licenses) {
			license_set_job_tres_cnt(job_ptr->license_list,
						 job_desc->tres_req_cnt,
						 true);
		}
		assoc_mgr_unlock(&locks);


		job_desc->tres_req_cnt[TRES_ARRAY_NODE] = job_desc->min_nodes;
	}

	if (job_desc->min_cpus != NO_VAL)
		job_desc->tres_req_cnt[TRES_ARRAY_CPU] = job_desc->min_cpus;
	else if ((job_desc->pn_min_cpus != NO_VAL16) &&
		 (job_desc->pn_min_cpus != 0)) {
		job_desc->tres_req_cnt[TRES_ARRAY_CPU] =
			job_desc->pn_min_cpus *
			(job_desc->min_nodes != NO_VAL ?
			 job_desc->min_nodes :
			 detail_ptr ? detail_ptr->min_nodes : 1);
		job_desc->min_cpus = job_desc->tres_req_cnt[TRES_ARRAY_CPU];
	} else if (job_desc->bitflags & TASKS_CHANGED) {
		job_desc->tres_req_cnt[TRES_ARRAY_CPU] = job_desc->min_cpus =
			job_desc->num_tasks;
	}

	mem_req =
		job_get_tres_mem(NULL,
				 job_desc->pn_min_memory,
				 job_desc->tres_req_cnt[TRES_ARRAY_CPU] ?
				 job_desc->tres_req_cnt[TRES_ARRAY_CPU] :
				 job_ptr->tres_req_cnt[TRES_ARRAY_CPU],
				 job_desc->min_nodes != NO_VAL ?
				 job_desc->min_nodes :
				 detail_ptr ? detail_ptr->min_nodes : 1,
				 use_part_ptr,
				 gres_list ? gres_list : job_ptr->gres_list_req,
				 (job_desc->pn_min_memory != NO_VAL64),
				 job_desc->sockets_per_node,
				 job_desc->num_tasks);
	if (mem_req)
		job_desc->tres_req_cnt[TRES_ARRAY_MEM] = mem_req;

	if (gres_update) {
		gres_stepmgr_set_job_tres_cnt(
			gres_list,
			job_desc->tres_req_cnt[TRES_ARRAY_NODE],
			job_desc->tres_req_cnt, false);
	}

	/* Check if we are clearing licenses */
	if (job_desc->licenses && !job_desc->licenses[0])
		job_desc->bitflags |= RESET_LIC_JOB;
	if (job_desc->tres_per_task &&
	    !xstrcasestr(job_desc->tres_per_task, "license/"))
		job_desc->bitflags |= RESET_LIC_TASK;

	_set_tot_license_req(job_desc, job_ptr);

	if (job_desc->licenses_tot && !xstrcmp(job_desc->licenses_tot,
						job_ptr->licenses)) {
		sched_debug("%s: new licenses identical to old licenses \"%s\"",
			    __func__, job_ptr->licenses);
	} else if (job_desc->licenses_tot) {
		bool pending = IS_JOB_PENDING(job_ptr);
		license_list = license_validate(job_desc->licenses_tot,
						true, true,
						pending ?
						job_desc->tres_req_cnt : NULL,
						&valid_licenses);

		if (!valid_licenses) {
			sched_info("%s: invalid licenses: %s",
				   __func__, job_desc->licenses_tot);
			error_code = ESLURM_INVALID_LICENSES;
		} else if (!license_list)
			xfree(job_desc->licenses_tot);
	}

	if (error_code != SLURM_SUCCESS)
		goto fini;


	if (job_desc->min_nodes == INFINITE) {
		/* Used by scontrol just to get current configuration info */
		job_desc->min_nodes = NO_VAL;
	}
	if ((job_desc->min_nodes != NO_VAL) &&
	    (job_desc->min_nodes > job_ptr->node_cnt) &&
	    !permit_job_expansion() &&
	    (IS_JOB_RUNNING(job_ptr) || IS_JOB_SUSPENDED(job_ptr))) {
		info("%s: Change of size for %pJ not supported",  __func__,
		     job_ptr);
		error_code = ESLURM_NOT_SUPPORTED;
		goto fini;
	}

	if (job_desc->req_switch != NO_VAL) {
		job_ptr->req_switch = job_desc->req_switch;
		info("%s: Change of switches to %u %pJ",
		     __func__, job_desc->req_switch, job_ptr);
	}
	if (job_desc->wait4switch != NO_VAL) {
		job_ptr->wait4switch = _max_switch_wait(job_desc->wait4switch);
		info("%s: Change of switch wait to %u secs %pJ",
		     __func__, job_ptr->wait4switch, job_ptr);
	}

	if (job_desc->admin_comment) {
		if (!validate_super_user(uid)) {
			error("%s: Attempt to change admin_comment for %pJ",
			      __func__, job_ptr);
			error_code = ESLURM_ACCESS_DENIED;
		} else {
			xfree(job_ptr->admin_comment);
			job_ptr->admin_comment =
				xstrdup(job_desc->admin_comment);
			info("%s: setting admin_comment to %s for %pJ",
			     __func__, job_ptr->admin_comment, job_ptr);
		}
	}

	if (job_desc->comment) {
		xfree(job_ptr->comment);
		job_ptr->comment = xstrdup(job_desc->comment);
		info("%s: setting comment to %s for %pJ",
		     __func__, job_ptr->comment, job_ptr);
	}

	if (job_desc->extra) {
		elem_t *head = NULL;

		error_code = extra_constraints_parse(job_desc->extra, &head);
		if (error_code != SLURM_SUCCESS) {
			error("%s: Invalid extra constraints", __func__);
		} else {
			xfree(job_ptr->extra);
			job_ptr->extra = xstrdup(job_desc->extra);
			FREE_NULL_EXTRA_CONSTRAINTS(job_ptr->extra_constraints);
			job_ptr->extra_constraints = head;
			info("%s: setting extra to %s for %pJ",
			     __func__, job_ptr->extra, job_ptr);
		}
	}

	if (error_code != SLURM_SUCCESS)
		goto fini;

        /*
	 * Now that we know what the new part, qos, and association are going
	 * to be lets check the limits.
	 * If a limit was already exceeded before this update
	 * request, let's assume it is expected and allow the change to happen.
	 */
	if (new_qos_ptr || new_assoc_ptr || new_part_ptr) {
		list_t *use_part_list = new_part_ptr ?
			part_ptr_list : job_ptr->part_ptr_list;
		assoc_mgr_lock(&assoc_mgr_read_lock);
		if ((error_code = _check_for_part_assocs(
			     use_part_list, use_assoc_ptr)) != SLURM_SUCCESS) {
			assoc_mgr_unlock(&assoc_mgr_read_lock);
			goto fini;
		}
		assoc_mgr_unlock(&assoc_mgr_read_lock);

		if (!operator &&
		    (accounting_enforce & ACCOUNTING_ENFORCE_LIMITS)) {
			uint32_t acct_reason = 0;
			char *resv_orig = NULL;
			bool resv_reset = false, min_reset = false,
				max_reset = false,
				time_min_reset = false;
			if (!acct_policy_validate(job_desc, use_part_ptr,
						  use_part_list,
						  use_assoc_ptr, use_qos_ptr,
						  &acct_reason,
						  &acct_policy_limit_set,
						  true)
			    && !acct_limit_already_exceeded) {
				info("%s: exceeded association/QOS limit for user %u: %s",
				     __func__, job_desc->user_id,
				     job_state_reason_string(acct_reason));
				error_code = ESLURM_ACCOUNTING_POLICY;
				goto fini;
			}
			/*
			 * We need to set the various parts of job_desc below
			 * to something since _valid_job_part() will validate
			 * them.  Note the reservation part is validated in the
			 * sub call to _part_access_check().
			 */
			if (job_desc->min_nodes == NO_VAL) {
				job_desc->min_nodes = detail_ptr->min_nodes;
				min_reset = true;
			}
			if ((job_desc->max_nodes == NO_VAL) &&
			    (detail_ptr->max_nodes != 0)) {
				job_desc->max_nodes = detail_ptr->max_nodes;
				max_reset = true;
			}

			if ((job_desc->time_min == NO_VAL) &&
			    (job_ptr->time_min != 0)) {
				job_desc->time_min = job_ptr->time_min;
				time_min_reset = true;
			}

			/*
			 * This always gets reset, so don't worry about tracking
			 * it.
			 */
			if (job_desc->time_limit == NO_VAL)
				job_desc->time_limit = job_ptr->time_limit;

			if (!job_desc->reservation
			    || job_desc->reservation[0] == '\0') {
				resv_reset = true;
				resv_orig = job_desc->reservation;
				job_desc->reservation = job_ptr->resv_name;
			}

			assoc_mgr_lock(&assoc_mgr_read_lock);
			if ((error_code = _valid_job_part(
				     job_desc, uid,
				     new_req_bitmap_given ?
				     new_req_bitmap :
				     job_ptr->details->req_node_bitmap,
				     use_part_ptr,
				     new_part_ptr ?
				     part_ptr_list : job_ptr->part_ptr_list,
				     use_assoc_ptr, use_qos_ptr, NULL))) {
				assoc_mgr_unlock(&assoc_mgr_read_lock);
				goto fini;
			}
			assoc_mgr_unlock(&assoc_mgr_read_lock);

			if (min_reset)
				job_desc->min_nodes = NO_VAL;
			if (max_reset)
				job_desc->max_nodes = NO_VAL;
			if (time_min_reset)
				job_desc->time_min = NO_VAL;
			if (resv_reset)
				job_desc->reservation = resv_orig;

			job_desc->time_limit = orig_time_limit;
		}

		/*
		 * Since we are successful to this point remove the job from the
		 * old qos/assoc's
		 */
		acct_policy_remove_job_submit(job_ptr, false);
		acct_policy_remove_accrue_time(job_ptr, false);
	}

	if (new_qos_ptr) {
		/* Change QOS */
		job_ptr->qos_id = new_qos_id;
		job_ptr->qos_ptr = new_qos_ptr;
		FREE_NULL_LIST(job_ptr->qos_list);
		job_ptr->qos_list = new_qos_list;
		new_qos_list = NULL;
		xfree(detail_ptr->qos_req);
		detail_ptr->qos_req = job_desc->qos;
		job_desc->qos = NULL;

		job_ptr->limit_set.qos = acct_policy_limit_set.qos;

		if (job_ptr->state_reason == FAIL_QOS) {
			job_ptr->state_reason = WAIT_NO_REASON;
			xfree(job_ptr->state_desc);
		}

		info("%s: setting QOS to %s for %pJ",
		     __func__, detail_ptr->qos_req, job_ptr);
	}

	if (new_assoc_ptr) {
		/* Change account/association */
		xfree(job_ptr->account);
		job_ptr->account = xstrdup(new_assoc_ptr->acct);
		job_ptr->assoc_id = new_assoc_ptr->id;
		job_ptr->assoc_ptr = new_assoc_ptr;

		if (job_ptr->state_reason == FAIL_ACCOUNT) {
			job_ptr->state_reason = WAIT_NO_REASON;
			xfree(job_ptr->state_desc);
		}

		info("%s: setting account to %s for %pJ",
		     __func__, job_ptr->account, job_ptr);
	}

	if (new_part_ptr) {
		/* Change partition */
		job_ptr->part_ptr = new_part_ptr;
		job_ptr->bit_flags &= ~JOB_PART_ASSIGNED;

		FREE_NULL_LIST(job_ptr->part_ptr_list);
		job_ptr->part_ptr_list = part_ptr_list;
		part_ptr_list = NULL;	/* nothing to free */

		rebuild_job_part_list(job_ptr);

		/* Rebuilt in priority/multifactor plugin */
		if (job_ptr->part_prio)
			xfree(job_ptr->part_prio->priority_array);

		info("%s: setting partition to %s for %pJ",
		     __func__, job_desc->partition, job_ptr);
	}

	/* Now add the job to the new qos/assoc's */
	if (new_qos_ptr || new_assoc_ptr || new_part_ptr) {
		update_accounting = true;
		acct_policy_add_job_submit(job_ptr, false);
	}

	if (new_resv_ptr) {
		FREE_NULL_LIST(job_ptr->resv_list);
		xfree(job_ptr->resv_name);
		job_ptr->resv_name = xstrdup(job_desc->reservation);
		job_ptr->resv_list = new_resv_list;
		job_ptr->resv_id = new_resv_ptr->resv_id;
		job_ptr->resv_ptr = new_resv_ptr;

		sched_info("%s: setting reservation to %s for %pJ", __func__,
			   job_ptr->resv_name, job_ptr);
		update_accounting = true;
	} else if (job_desc->reservation &&
		   job_desc->reservation[0] == '\0' &&
		   job_ptr->resv_name) {
		FREE_NULL_LIST(job_ptr->resv_list);
		xfree(job_ptr->resv_name);
		job_ptr->resv_id    = 0;
		job_ptr->resv_ptr   = NULL;
		sched_info("%s: setting reservation to '' for %pJ",
			   __func__, job_ptr);
		update_accounting = true;
	}

	/* Reset min and max cpu counts as needed, ensure consistency */
	if (job_desc->min_cpus != NO_VAL) {
		if ((!IS_JOB_PENDING(job_ptr)) || (detail_ptr == NULL))
			error_code = ESLURM_JOB_NOT_PENDING;
		else if (job_desc->min_cpus < 1)
			error_code = ESLURM_INVALID_CPU_COUNT;
		else {
			save_min_cpus = detail_ptr->min_cpus;
			detail_ptr->min_cpus = job_desc->min_cpus;
		}
	}
	if (job_desc->max_cpus != NO_VAL) {
		if ((!IS_JOB_PENDING(job_ptr)) || (detail_ptr == NULL))
			error_code = ESLURM_JOB_NOT_PENDING;
		else {
			save_max_cpus = detail_ptr->max_cpus;
			detail_ptr->max_cpus = job_desc->max_cpus;
		}
	}
	if ((save_min_cpus || save_max_cpus) && detail_ptr->max_cpus &&
	    (detail_ptr->max_cpus < detail_ptr->min_cpus)) {
		error_code = ESLURM_INVALID_CPU_COUNT;
		if (save_min_cpus) {
			detail_ptr->min_cpus = save_min_cpus;
			save_min_cpus = 0;
		}
		if (save_max_cpus) {
			detail_ptr->max_cpus = save_max_cpus;
			save_max_cpus = 0;
		}
	}

	if (error_code != SLURM_SUCCESS)
		goto fini;

	if (save_min_cpus && (detail_ptr->min_cpus != save_min_cpus)) {
		info("%s: setting min_cpus from %u to %u for %pJ",
		     __func__, save_min_cpus, detail_ptr->min_cpus, job_ptr);
		job_ptr->limit_set.tres[TRES_ARRAY_CPU] =
			acct_policy_limit_set.tres[TRES_ARRAY_CPU];
		detail_ptr->orig_min_cpus = job_desc->min_cpus;
		update_accounting = true;
	}
	if (save_max_cpus && (detail_ptr->max_cpus != save_max_cpus)) {
		info("%s: setting max_cpus from %u to %u for %pJ",
		     __func__, save_max_cpus, detail_ptr->max_cpus, job_ptr);
		/*
		 * Always use the acct_policy_limit_set.* since if set by a
		 * super user it be set correctly
		 */
		job_ptr->limit_set.tres[TRES_ARRAY_CPU] =
			acct_policy_limit_set.tres[TRES_ARRAY_CPU];
		detail_ptr->orig_max_cpus = job_desc->max_cpus;
		update_accounting = true;
	}

	if ((job_desc->pn_min_cpus != NO_VAL16) &&
	    (job_desc->pn_min_cpus != 0)) {

		if ((!IS_JOB_PENDING(job_ptr)) || (detail_ptr == NULL)) {
			error_code = ESLURM_JOB_NOT_PENDING;
		} else {
			detail_ptr->pn_min_cpus = job_desc->pn_min_cpus;
			detail_ptr->orig_pn_min_cpus = job_desc->pn_min_cpus;
			info("%s: setting pn_min_cpus to %u for %pJ",
			     __func__, job_desc->pn_min_cpus, job_ptr);
		}
	}

	if (error_code != SLURM_SUCCESS)
		goto fini;

	if (job_desc->cpus_per_task != NO_VAL16) {
		if ((!IS_JOB_PENDING(job_ptr)) || (detail_ptr == NULL)) {
			error_code = ESLURM_JOB_NOT_PENDING;
		} else if (job_desc->cpus_per_task == 0) {
			error("%s: trying to set cpus_per_task to an erroneous value: %u",
			      __func__, job_desc->cpus_per_task);
			error_code = ESLURM_INVALID_CPU_COUNT;
		} else if (detail_ptr->cpus_per_task !=
			   job_desc->cpus_per_task) {
			info("%s: setting cpus_per_task from %u to %u for %pJ",
			     __func__, detail_ptr->cpus_per_task,
			     job_desc->cpus_per_task, job_ptr);
			detail_ptr->cpus_per_task = job_desc->cpus_per_task;
			detail_ptr->orig_cpus_per_task =
				job_desc->cpus_per_task;
		}
	}

	if (error_code != SLURM_SUCCESS)
		goto fini;

	/* Reset min and max node counts as needed, ensure consistency */
	if (job_desc->min_nodes != NO_VAL) {
		if (job_ptr->details &&
		    (job_ptr->details->task_dist & SLURM_DIST_STATE_BASE) ==
		    SLURM_DIST_ARBITRARY) {
			info("%s: Cannot update node count of %pJ. Not compatible with arbitrary distribution",
			     __func__, job_ptr);
			error_code = ESLURM_NOT_SUPPORTED;
		} else if (IS_JOB_RUNNING(job_ptr) || IS_JOB_SUSPENDED(job_ptr))
			;	/* shrink running job, processed later */
		else if ((!IS_JOB_PENDING(job_ptr)) || (detail_ptr == NULL))
			error_code = ESLURM_JOB_NOT_PENDING;
		else if (job_desc->min_nodes < 1) {
			info("%s: min_nodes < 1 for %pJ", __func__, job_ptr);
			error_code = ESLURM_INVALID_NODE_COUNT;
		} else {
			/* Resize of pending job */
			save_min_nodes = detail_ptr->min_nodes;
			detail_ptr->min_nodes = job_desc->min_nodes;
		}
	}
	if (job_desc->max_nodes != NO_VAL) {
		if ((IS_JOB_RUNNING(job_ptr) || IS_JOB_SUSPENDED(job_ptr)) &&
		    (job_desc->max_nodes == job_desc->min_nodes))
			;	/* shrink running job, processed later */
		else if ((!IS_JOB_PENDING(job_ptr)) || (detail_ptr == NULL))
			error_code = ESLURM_JOB_NOT_PENDING;
		else {
			save_max_nodes = detail_ptr->max_nodes;
			detail_ptr->max_nodes = job_desc->max_nodes;
		}
	}
	if ((save_min_nodes || save_max_nodes) && detail_ptr->max_nodes &&
	    (detail_ptr->max_nodes < detail_ptr->min_nodes)) {
		info("%s: max_nodes < min_nodes (%u < %u) for %pJ", __func__,
		     detail_ptr->max_nodes, detail_ptr->min_nodes,
		     job_ptr);
		error_code = ESLURM_INVALID_NODE_COUNT;
		if (save_min_nodes) {
			detail_ptr->min_nodes = save_min_nodes;
			save_min_nodes = 0;
		}
		if (save_max_nodes) {
			detail_ptr->max_nodes = save_max_nodes;
			save_max_nodes = 0;
		}
	}
	if (error_code != SLURM_SUCCESS)
		goto fini;

	if (save_min_nodes && (save_min_nodes!= detail_ptr->min_nodes)) {
		info("%s: setting min_nodes from %u to %u for %pJ", __func__,
		     save_min_nodes, detail_ptr->min_nodes, job_ptr);
		job_ptr->limit_set.tres[TRES_ARRAY_NODE] =
			acct_policy_limit_set.tres[TRES_ARRAY_NODE];
		update_accounting = true;
		FREE_NULL_BITMAP(detail_ptr->job_size_bitmap);
	}
	if (save_max_nodes && (save_max_nodes != detail_ptr->max_nodes)) {
		info("%s: setting max_nodes from %u to %u for %pJ", __func__,
		     save_max_nodes, detail_ptr->max_nodes, job_ptr);
		/*
		 * Always use the acct_policy_limit_set.* since if set by a
		 * super user it be set correctly
		 */
		job_ptr->limit_set.tres[TRES_ARRAY_NODE] =
			acct_policy_limit_set.tres[TRES_ARRAY_NODE];
		update_accounting = true;
		FREE_NULL_BITMAP(detail_ptr->job_size_bitmap);
	}
	if (job_desc->job_size_str) {
		if ((!IS_JOB_PENDING(job_ptr)) || !detail_ptr)
			error_code = ESLURM_JOB_NOT_PENDING;
		else if (detail_ptr->min_nodes && detail_ptr->max_nodes &&
			 (detail_ptr->max_nodes != NO_VAL) &&
			 (detail_ptr->max_nodes < MAX_JOB_SIZE_BITMAP)) {
			bitstr_t  *new_size_bitmap;
			new_size_bitmap = bit_alloc(detail_ptr->max_nodes + 1);
			if (bit_unfmt(new_size_bitmap,
				      job_desc->job_size_str)) {
				FREE_NULL_BITMAP(new_size_bitmap);
				info("%s: %pJ: invalid job_size_str:%s",
				     __func__, job_ptr, job_desc->job_size_str);
				error_code = ESLURM_INVALID_NODE_COUNT;
			} else {
				FREE_NULL_BITMAP(detail_ptr->job_size_bitmap);
				detail_ptr->job_size_bitmap = new_size_bitmap;
			}
		} else {
			info("%s: %pJ: invalid job_size_str:%s", __func__,
			     job_ptr, job_desc->job_size_str);
			error_code = ESLURM_INVALID_NODE_COUNT;
		}

	} else {
		error_code = _unroll_min_max_node(job_ptr);
	}

	if (error_code != SLURM_SUCCESS)
		goto fini;

	if ((job_desc->num_tasks != NO_VAL) &&
	    (job_desc->bitflags & TASKS_CHANGED)) {
		if (!IS_JOB_PENDING(job_ptr))
			error_code = ESLURM_JOB_NOT_PENDING;
		else if (job_desc->num_tasks < 1)
			error_code = ESLURM_BAD_TASK_COUNT;
		else {
			detail_ptr->num_tasks = job_desc->num_tasks;
			/*
			 * Once you actually requested ntasks you will get
			 * SLURM_NTASKS in your environment. There is no way to
			 * remove that.
			 */
			if (job_desc->bitflags & JOB_NTASKS_SET)
				job_ptr->bit_flags |= JOB_NTASKS_SET;
			info("%s: setting num_tasks to %u for %pJ",
			     __func__, job_desc->num_tasks, job_ptr);
		}
	}
	if (error_code != SLURM_SUCCESS)
		goto fini;

	/*
	 * If the job records now holds a required nodelist with more nodes than
	 * are required, translate this list into an exclusion of all nodes
	 * except those requested.
	 *
	 * Merge the resulting negated version into the excluded nodelist of the
	 * job.
	 */
	if (detail_ptr->req_node_bitmap &&
	    (bit_set_count(detail_ptr->req_node_bitmap) >
	     detail_ptr->min_nodes)) {
		if (!detail_ptr->exc_node_bitmap)
			detail_ptr->exc_node_bitmap =
				bit_alloc(node_record_count);
		bit_or_not(detail_ptr->exc_node_bitmap,
			   detail_ptr->req_node_bitmap);
		FREE_NULL_BITMAP(detail_ptr->req_node_bitmap);
	}

	if (job_desc->time_limit != NO_VAL) {
		if (IS_JOB_FINISHED(job_ptr) || job_ptr->preempt_time)
			error_code = ESLURM_JOB_FINISHED;
		else if (job_ptr->time_limit == job_desc->time_limit) {
			sched_debug("%s: new time limit identical to old time limit %pJ",
				    __func__, job_ptr);
		} else if (operator ||
			   (job_ptr->time_limit > job_desc->time_limit)) {
			time_t old_time =  job_ptr->time_limit;
			uint32_t use_time_min = job_desc->time_min != NO_VAL ?
				job_desc->time_min : job_ptr->time_min;
			if (old_time == INFINITE)	/* one year in mins */
				old_time = (365 * 24 * 60);
			if (job_desc->time_limit < use_time_min) {
				sched_info("%s: attempt to set time_limit < time_min (%u < %u)",
					   __func__,
					   job_desc->time_limit,
					   use_time_min);
				error_code = ESLURM_INVALID_TIME_MIN_LIMIT;
				goto fini;
			}
			acct_policy_alter_job(job_ptr, job_desc->time_limit);
			job_ptr->time_limit = job_desc->time_limit;
			if (IS_JOB_RUNNING(job_ptr) ||
			    IS_JOB_SUSPENDED(job_ptr)) {
				if (job_ptr->preempt_time) {
					;	/* Preemption in progress */
				} else if (job_ptr->time_limit == INFINITE) {
					/* Set end time in one year */
					job_ptr->end_time = now +
						(365 * 24 * 60 * 60);
				} else {
					/*
					 * Update end_time based upon change
					 * to preserve suspend time info
					 */
					job_ptr->end_time = job_ptr->end_time +
						((job_ptr->time_limit -
						  old_time) * 60);
				}
				if (job_ptr->end_time < now)
					job_ptr->end_time = now;
				job_ptr->end_time_exp = job_ptr->end_time;
			}
			sched_info("%s: setting time_limit to %u for %pJ",
				   __func__, job_desc->time_limit, job_ptr);
			/*
			 * Always use the acct_policy_limit_set.*
			 * since if set by a super user it be set correctly
			 */
			job_ptr->limit_set.time = acct_policy_limit_set.time;
			update_accounting = true;
		} else if (IS_JOB_PENDING(job_ptr) && job_ptr->part_ptr &&
			   (job_ptr->part_ptr->max_time >=
			    job_desc->time_limit)) {
			job_ptr->time_limit = job_desc->time_limit;
			sched_info("%s: setting time_limit to %u for %pJ",
				   __func__, job_desc->time_limit, job_ptr);
			/*
			 * Always use the acct_policy_limit_set.*
			 * since if set by a super user it be set correctly
			 */
			job_ptr->limit_set.time = acct_policy_limit_set.time;
			update_accounting = true;
		} else {
			sched_info("%s: Attempt to increase time limit for %pJ",
				   __func__, job_ptr);
			error_code = ESLURM_ACCESS_DENIED;
		}
	}
	if (error_code != SLURM_SUCCESS)
		goto fini;

	if ((job_desc->time_min != NO_VAL) && IS_JOB_PENDING(job_ptr)) {
		if (job_desc->time_min > job_ptr->time_limit) {
			info("%s: attempt to set TimeMin > TimeLimit (%u > %u)",
			     __func__, job_desc->time_min, job_ptr->time_limit);
			error_code = ESLURM_INVALID_TIME_MIN_LIMIT;
		} else if (job_ptr->time_min != job_desc->time_min) {
			job_ptr->time_min = job_desc->time_min;
			info("%s: setting TimeMin to %u for %pJ",
			     __func__, job_desc->time_min, job_ptr);
		}
	}
	if (error_code != SLURM_SUCCESS)
		goto fini;

	if (job_desc->end_time) {
		if (!IS_JOB_RUNNING(job_ptr) || job_ptr->preempt_time) {
			/*
			 * We may want to use this for deadline scheduling
			 * at some point in the future. For now only reset
			 * the time limit of running jobs.
			 */
			error_code = ESLURM_JOB_NOT_RUNNING;
		} else if (job_desc->end_time < now) {
			error_code = ESLURM_INVALID_TIME_VALUE;
		} else if (operator ||
			   (job_ptr->end_time > job_desc->end_time)) {
			int delta_t  = job_desc->end_time - job_ptr->end_time;
			job_ptr->end_time = job_desc->end_time;
			job_ptr->time_limit += (delta_t+30)/60; /* Sec->min */
			sched_info("%s: setting time_limit to %u for %pJ",
				   __func__, job_ptr->time_limit, job_ptr);
			/* Always use the acct_policy_limit_set.*
			 * since if set by a super user it be set correctly */
			job_ptr->limit_set.time = acct_policy_limit_set.time;
			update_accounting = true;
		} else {
			sched_info("%s: Attempt to extend end time for %pJ",
				   __func__, job_ptr);
			error_code = ESLURM_ACCESS_DENIED;
		}
	}

	if ((job_desc->deadline) && (!IS_JOB_RUNNING(job_ptr))) {
		char time_str[256];
		slurm_make_time_str(&job_ptr->deadline, time_str,
				    sizeof(time_str));
		if (job_desc->deadline < now) {
			error_code = ESLURM_INVALID_TIME_VALUE;
		} else if (operator) {
			/* update deadline */
			job_ptr->deadline = job_desc->deadline;
			sched_info("%s: setting deadline to %s for %pJ",
				   __func__, time_str, job_ptr);
			/*
			 * Always use the acct_policy_limit_set.*
			 * since if set by a super user it be set correctly
			 */
			job_ptr->limit_set.time = acct_policy_limit_set.time;
			update_accounting = true;
		} else {
			sched_info("%s: Attempt to extend end time for %pJ",
				   __func__, job_ptr);
			error_code = ESLURM_ACCESS_DENIED;
		}
	}
	if (error_code != SLURM_SUCCESS)
		goto fini;

	if (job_desc->delay_boot != NO_VAL) {
		job_ptr->delay_boot = job_desc->delay_boot;
		sched_info("%s: setting delay_boot to %u for %pJ",
			   __func__, job_desc->delay_boot, job_ptr);
	}

	if ((job_desc->requeue != NO_VAL16) && detail_ptr) {
		detail_ptr->requeue = MIN(job_desc->requeue, 1);
		sched_info("%s: setting requeue to %u for %pJ",
			   __func__, job_desc->requeue, job_ptr);
	}

	if (job_desc->priority != NO_VAL) {
		/*
		 * If we are doing time slicing we could update the
		 * priority of the job while running to give better
		 * position (larger time slices) than competing jobs
		 */
		if (IS_JOB_FINISHED(job_ptr) || (detail_ptr == NULL))
			error_code = ESLURM_JOB_FINISHED;
		else if (job_ptr->priority == job_desc->priority) {
			debug("%s: setting priority to current value",__func__);
			if ((job_ptr->priority == 0) && operator) {
				/*
				 * Authorized user can change from user hold
				 * to admin hold or admin hold to user hold
				 */
				if (job_desc->alloc_sid == ALLOC_SID_USER_HOLD)
					job_ptr->state_reason = WAIT_HELD_USER;
				else
					job_ptr->state_reason = WAIT_HELD;
			}
		} else if ((job_ptr->priority == 0) &&
			   (job_desc->priority == INFINITE) &&
			   (operator ||
			    (job_ptr->state_reason == WAIT_RESV_DELETED) ||
			    (job_ptr->state_reason == WAIT_HELD_USER))) {
			_release_job(job_ptr, uid);
		} else if ((job_ptr->priority == 0) &&
			   (job_desc->priority != INFINITE)) {
			info("%s: ignore priority reset request on held %pJ",
			     __func__, job_ptr);
			error_code = ESLURM_JOB_HELD;
		} else if (operator ||
			   (job_ptr->priority > job_desc->priority)) {
			if (job_desc->priority != 0)
				job_ptr->details->nice = NICE_OFFSET;
			if (job_desc->priority == INFINITE) {
				job_ptr->direct_set_prio = 0;
				set_job_prio(job_ptr);
			} else if (job_desc->priority == 0) {
				_hold_job(job_ptr, uid);
			} else {
				if (operator) {
					/*
					 * Only administrator can make
					 * persistent change to a job's
					 * priority, except holding a job
					 */
					job_ptr->direct_set_prio = 1;
				} else
					error_code = ESLURM_PRIO_RESET_FAIL;
				job_ptr->priority = job_desc->priority;
				if (job_ptr->part_ptr_list &&
				    job_ptr->part_prio &&
				    job_ptr->part_prio->priority_array) {
					int i, j = list_count(
						job_ptr->part_ptr_list);
					for (i = 0; i < j; i++) {
						job_ptr->part_prio->
							priority_array[i] =
							job_desc->priority;
					}
				}
			}
			sched_info("%s: set priority to %u for %pJ",
				   __func__, job_ptr->priority, job_ptr);
			update_accounting = true;
			if (job_ptr->priority == 0) {
				if (!operator ||
				    (job_desc->alloc_sid ==
				     ALLOC_SID_USER_HOLD)) {
					job_ptr->state_reason = WAIT_HELD_USER;
				} else
					job_ptr->state_reason = WAIT_HELD;
				xfree(job_ptr->state_desc);

				/* remove pending remote sibling jobs */
				if (IS_JOB_PENDING(job_ptr) &&
				    !IS_JOB_REVOKED(job_ptr)) {
					fed_mgr_job_revoke_sibs(job_ptr);
				}
			}
		} else if ((job_ptr->priority != 0) &&
			   (job_desc->priority == INFINITE)) {
			/*
			 * If the job was already released, ignore another
			 * release request.
			 */
			debug("%s: %pJ already released, ignoring request",
			      __func__, job_ptr);
		} else {
			sched_error("Attempt to modify priority for %pJ",
				    job_ptr);
			error_code = ESLURM_ACCESS_DENIED;
		}
	} else if (job_ptr->state_reason == FAIL_BAD_CONSTRAINTS) {
		/*
		 * We need to check if the state is BadConstraints here since we
		 * are altering the job the bad constraint might have gone
		 * away.  If it did the priority (0) wouldn't get reset so the
		 * job would just go into JobAdminHeld otherwise.
		 */
		job_ptr->direct_set_prio = 0;
		set_job_prio(job_ptr);
		sched_debug("%s: job request changed somehow, removing the bad constraints to reevaluate %pJ uid %u",
			    __func__, job_ptr, uid);
		job_ptr->state_reason = WAIT_NO_REASON;
	}

	if (error_code != SLURM_SUCCESS)
		goto fini;

	if (job_desc->nice != NO_VAL) {
		if (IS_JOB_FINISHED(job_ptr) || (job_ptr->details == NULL))
			error_code = ESLURM_JOB_FINISHED;
		else if (job_ptr->details &&
			 (job_ptr->details->nice == job_desc->nice))
			sched_debug("%s: new nice identical to old nice %pJ",
				    __func__, job_ptr);
		else if (job_ptr->direct_set_prio && job_ptr->priority != 0)
			info("%s: ignore nice set request on %pJ",
			     __func__, job_ptr);
		else if (operator || (job_desc->nice >= NICE_OFFSET)) {
			if (!xstrcmp(slurm_conf.priority_type,
			             "priority/basic")) {
				int64_t new_prio = job_ptr->priority;
				new_prio += job_ptr->details->nice;
				new_prio -= job_desc->nice;
				job_ptr->priority = MAX(new_prio, 2);
				sched_info("%s: nice changed from %u to %u, setting priority to %u for %pJ",
					   __func__, job_ptr->details->nice,
					   job_desc->nice,
					   job_ptr->priority, job_ptr);
			}
			job_ptr->details->nice = job_desc->nice;
			update_accounting = true;
		} else {
			sched_error("%s: Attempt to modify nice for %pJ",
				    __func__, job_ptr);
			error_code = ESLURM_ACCESS_DENIED;
		}
	}
	if (error_code != SLURM_SUCCESS)
		goto fini;

	if (job_desc->pn_min_memory != NO_VAL64) {
		if ((!IS_JOB_PENDING(job_ptr)) || (detail_ptr == NULL)) {
			error_code = ESLURM_JOB_NOT_PENDING;
		} else if (job_desc->pn_min_memory
			   == detail_ptr->pn_min_memory) {
			sched_debug("%s: new memory limit identical to old limit for %pJ",
				    __func__, job_ptr);
		} else {
			char *entity;
			if (job_desc->pn_min_memory == MEM_PER_CPU) {
				/* Map --mem-per-cpu=0 to --mem=0 */
				job_desc->pn_min_memory = 0;
			}
			if (job_desc->pn_min_memory & MEM_PER_CPU)
				entity = "cpu";
			else
				entity = "job";

			detail_ptr->pn_min_memory = job_desc->pn_min_memory;
			detail_ptr->orig_pn_min_memory =
				job_desc->pn_min_memory;
			job_ptr->bit_flags |= JOB_MEM_SET;
			sched_info("%s: setting min_memory_%s to %"PRIu64" for %pJ",
				   __func__, entity,
				   (job_desc->pn_min_memory & (~MEM_PER_CPU)),
				   job_ptr);
			/*
			 * Always use the acct_policy_limit_set.*
			 * since if set by a super user it be set correctly
			 */
			job_ptr->limit_set.tres[TRES_ARRAY_MEM] =
				acct_policy_limit_set.tres[TRES_ARRAY_MEM];
		}
	}
	if (error_code != SLURM_SUCCESS)
		goto fini;

	if (job_desc->pn_min_tmp_disk != NO_VAL) {
		if ((!IS_JOB_PENDING(job_ptr)) || (detail_ptr == NULL)) {
			error_code = ESLURM_JOB_NOT_PENDING;
		} else {
			detail_ptr->pn_min_tmp_disk =
				job_desc->pn_min_tmp_disk;

			sched_info("%s: setting job_min_tmp_disk to %u for %pJ",
				   __func__, job_desc->pn_min_tmp_disk,
				   job_ptr);
		}
	}
	if (error_code != SLURM_SUCCESS)
		goto fini;

	if (job_desc->sockets_per_node != NO_VAL16) {
		if ((!IS_JOB_PENDING(job_ptr)) || (mc_ptr == NULL)) {
			error_code = ESLURM_JOB_NOT_PENDING;
			goto fini;
		} else {
			mc_ptr->sockets_per_node = job_desc->sockets_per_node;
			sched_info("%s: setting sockets_per_node to %u for %pJ",
				   __func__, job_desc->sockets_per_node,
				   job_ptr);
		}
	}

	if (job_desc->cores_per_socket != NO_VAL16) {
		if ((!IS_JOB_PENDING(job_ptr)) || (mc_ptr == NULL)) {
			error_code = ESLURM_JOB_NOT_PENDING;
			goto fini;
		} else {
			mc_ptr->cores_per_socket = job_desc->cores_per_socket;
			sched_info("%s: setting cores_per_socket to %u for %pJ",
				   __func__, job_desc->cores_per_socket,
				   job_ptr);
		}
	}

	if ((job_desc->threads_per_core != NO_VAL16)) {
		if ((!IS_JOB_PENDING(job_ptr)) || (mc_ptr == NULL)) {
			error_code = ESLURM_JOB_NOT_PENDING;
			goto fini;
		} else {
			mc_ptr->threads_per_core = job_desc->threads_per_core;
			sched_info("%s: setting threads_per_core to %u for %pJ",
				   __func__, job_desc->threads_per_core,
				   job_ptr);
		}
	}

	if (job_desc->shared != NO_VAL16) {
		if ((!IS_JOB_PENDING(job_ptr)) || (detail_ptr == NULL)) {
			error_code = ESLURM_JOB_NOT_PENDING;
		} else if (!operator) {
			sched_error("%s: Attempt to change sharing for %pJ",
				    __func__, job_ptr);
			error_code = ESLURM_ACCESS_DENIED;
		} else {
			if (job_desc->shared) {
				detail_ptr->share_res = 1;
				detail_ptr->whole_node = 0;
			} else {
				detail_ptr->share_res = 0;
			}
			sched_info("%s: setting shared to %u for %pJ",
				   __func__, job_desc->shared, job_ptr);
		}
	}
	if (error_code != SLURM_SUCCESS)
		goto fini;

	if (job_desc->contiguous != NO_VAL16) {
		if ((!IS_JOB_PENDING(job_ptr)) || (detail_ptr == NULL))
			error_code = ESLURM_JOB_NOT_PENDING;
		else if (operator
			 || (detail_ptr->contiguous > job_desc->contiguous)) {
			detail_ptr->contiguous = job_desc->contiguous;
			sched_info("%s: setting contiguous to %u for %pJ",
				   __func__, job_desc->contiguous, job_ptr);
		} else {
			sched_error("%s: Attempt to add contiguous for %pJ",
				    __func__, job_ptr);
			error_code = ESLURM_ACCESS_DENIED;
		}
	}
	if (error_code != SLURM_SUCCESS)
		goto fini;

	if (job_desc->core_spec != NO_VAL16) {
		if ((!IS_JOB_PENDING(job_ptr)) || (detail_ptr == NULL))
			error_code = ESLURM_JOB_NOT_PENDING;
		else if (operator && (slurm_conf.conf_flags & CONF_FLAG_ASRU)) {
			if (job_desc->core_spec == INFINITE16)
				detail_ptr->core_spec = NO_VAL16;
			else
				detail_ptr->core_spec = job_desc->core_spec;
			sched_info("%s: setting core_spec to %u for %pJ",
				   __func__, detail_ptr->core_spec, job_ptr);
			if (detail_ptr->core_spec != NO_VAL16)
				detail_ptr->whole_node |= WHOLE_NODE_REQUIRED;
		} else {
			sched_error("%s Attempt to modify core_spec for %pJ",
				    __func__, job_ptr);
			error_code = ESLURM_ACCESS_DENIED;
		}
	}
	if (error_code != SLURM_SUCCESS)
		goto fini;

	if (job_desc->features && detail_ptr &&
	    !xstrcmp(job_desc->features, detail_ptr->features)) {
		sched_debug("%s: new features identical to old features %s",
			    __func__, job_desc->features);
	} else if (job_desc->features) {
		if ((!IS_JOB_PENDING(job_ptr)) || (detail_ptr == NULL))
			error_code = ESLURM_JOB_NOT_PENDING;
		else if (job_desc->features[0] != '\0') {
			char *old_features = detail_ptr->features;
			list_t *old_list = detail_ptr->feature_list;
			detail_ptr->features = xstrdup(job_desc->features);
			detail_ptr->feature_list = NULL;
			if (build_feature_list(job_ptr, false, false)) {
				sched_info("%s: invalid features(%s) for %pJ",
					   __func__, job_desc->features,
					   job_ptr);
				FREE_NULL_LIST(detail_ptr->feature_list);
				xfree(detail_ptr->features);
				detail_ptr->features = old_features;
				detail_ptr->feature_list = old_list;
				error_code = ESLURM_INVALID_FEATURE;
			} else if (node_features_g_job_valid(
						detail_ptr->features,
						detail_ptr->feature_list) !=
				   SLURM_SUCCESS) {
				FREE_NULL_LIST(detail_ptr->feature_list);
				xfree(detail_ptr->features);
				detail_ptr->features = old_features;
				detail_ptr->feature_list = old_list;
				error_code = ESLURM_INVALID_FEATURE;
			} else {
				sched_info("%s: setting features to %s for %pJ",
					   __func__, job_desc->features,
					   job_ptr);
				xfree(old_features);
				FREE_NULL_LIST(old_list);
				detail_ptr->features_use = detail_ptr->features;
				detail_ptr->feature_list_use =
					detail_ptr->feature_list;
			}
		} else {
			sched_info("%s: cleared features for %pJ", __func__,
				   job_ptr);
			xfree(detail_ptr->features);
			FREE_NULL_LIST(detail_ptr->feature_list);
			detail_ptr->features_use = NULL;
			detail_ptr->feature_list_use = NULL;
		}
	}
	if (error_code != SLURM_SUCCESS)
		goto fini;

	if (job_desc->prefer && detail_ptr &&
	    !xstrcmp(job_desc->prefer, detail_ptr->prefer)) {
		sched_debug("%s: new prefer identical to old prefer %s",
			    __func__, job_desc->prefer);
	} else if (job_desc->prefer) {
		if ((!IS_JOB_PENDING(job_ptr)) || (detail_ptr == NULL))
			error_code = ESLURM_JOB_NOT_PENDING;
		else if (job_desc->prefer[0] != '\0') {
			char *old_prefer = detail_ptr->prefer;
			list_t *old_list = detail_ptr->prefer_list;
			detail_ptr->prefer = xstrdup(job_desc->prefer);
			detail_ptr->prefer_list = NULL;
			if (build_feature_list(job_ptr, true, false)) {
				sched_info("%s: invalid prefer(%s) for %pJ",
					   __func__, job_desc->prefer,
					   job_ptr);
				FREE_NULL_LIST(detail_ptr->prefer_list);
				xfree(detail_ptr->prefer);
				detail_ptr->prefer = old_prefer;
				detail_ptr->prefer_list = old_list;
				error_code = ESLURM_INVALID_PREFER;
			} else if (node_features_g_job_valid(
						detail_ptr->prefer,
						detail_ptr->prefer_list) !=
				   SLURM_SUCCESS) {
				FREE_NULL_LIST(detail_ptr->prefer_list);
				xfree(detail_ptr->prefer);
				detail_ptr->features = old_prefer;
				detail_ptr->feature_list = old_list;
				error_code = ESLURM_INVALID_PREFER;
			} else {
				sched_info("%s: setting prefer to %s for %pJ",
					   __func__, job_desc->prefer,
					   job_ptr);
				xfree(old_prefer);
				FREE_NULL_LIST(old_list);
				detail_ptr->features_use = detail_ptr->prefer;
				detail_ptr->feature_list_use =
					detail_ptr->prefer_list;
			}
		} else {
			sched_info("%s: cleared prefer for %pJ", __func__,
				   job_ptr);
			xfree(detail_ptr->prefer);
			FREE_NULL_LIST(detail_ptr->prefer_list);
			detail_ptr->features_use = NULL;
			detail_ptr->feature_list_use = NULL;
		}
	}
	if (error_code != SLURM_SUCCESS)
		goto fini;

	if (job_desc->cluster_features &&
	    (error_code = fed_mgr_update_job_cluster_features(
		    job_ptr, job_desc->cluster_features)))
		goto fini;

	if (job_desc->clusters &&
	    (error_code = fed_mgr_update_job_clusters(job_ptr,
						      job_desc->clusters)))
		goto fini;

	if (gres_list) {
		char *tmp = NULL;
		if (job_desc->cpus_per_tres) {
			xstrfmtcat(tmp, "cpus_per_tres:%s ",
				   job_desc->cpus_per_tres);
			xfree(job_ptr->cpus_per_tres);
			job_ptr->cpus_per_tres = job_desc->cpus_per_tres;
			job_desc->cpus_per_tres = NULL;
		}
		if (job_desc->tres_per_job) {
			xstrfmtcat(tmp, "tres_per_job:%s ",
				   job_desc->tres_per_job);
			xfree(job_ptr->tres_per_job);
			job_ptr->tres_per_job = job_desc->tres_per_job;
			job_desc->tres_per_job = NULL;
		}
		if (job_desc->tres_per_node) {
			xstrfmtcat(tmp, "tres_per_node:%s ",
				   job_desc->tres_per_node);
			xfree(job_ptr->tres_per_node);
			job_ptr->tres_per_node = job_desc->tres_per_node;
			job_desc->tres_per_node = NULL;
		}
		if (job_desc->tres_per_socket) {
			xstrfmtcat(tmp, "tres_per_socket:%s ",
				   job_desc->tres_per_socket);
			xfree(job_ptr->tres_per_socket);
			job_ptr->tres_per_socket = job_desc->tres_per_socket;
			job_desc->tres_per_socket = NULL;
		}
		if (job_desc->tres_per_task) {
			xstrfmtcat(tmp, "tres_per_task:%s ",
				   job_desc->tres_per_task);
			xfree(job_ptr->tres_per_task);
			job_ptr->tres_per_task = job_desc->tres_per_task;
			job_desc->tres_per_task = NULL;
		}
		if (job_desc->mem_per_tres) {
			xstrfmtcat(tmp, "mem_per_tres:%s ",
				   job_desc->mem_per_tres);
			xfree(job_ptr->mem_per_tres);
			job_ptr->mem_per_tres = job_desc->mem_per_tres;
			job_desc->mem_per_tres = NULL;
		}
		if (tmp) {
			sched_info("%s: setting %sfor %pJ",
				   __func__, tmp, job_ptr);
			xfree(tmp);
		}
		FREE_NULL_LIST(job_ptr->gres_list_req);
		job_ptr->gres_list_req = gres_list;

		gres_list = NULL;
	}

	if (job_desc->name) {
		if (IS_JOB_FINISHED(job_ptr)) {
			error_code = ESLURM_JOB_FINISHED;
			goto fini;
		} else if (!xstrcmp(job_desc->name, job_ptr->name)) {
			sched_debug("%s: new name identical to old name %pJ",
				    __func__, job_ptr);
		} else {
			xfree(job_ptr->name);
			job_ptr->name = xstrdup(job_desc->name);

			sched_info("%s: setting name to %s for %pJ",
				   __func__, job_ptr->name, job_ptr);
			update_accounting = true;
		}
	}

	if (job_desc->work_dir && detail_ptr &&
	    !xstrcmp(job_desc->work_dir, detail_ptr->work_dir)) {
		sched_debug("%s: new work_dir identical to old work_dir %s",
			    __func__, job_desc->work_dir);
	} else if (job_desc->work_dir) {
		if (!IS_JOB_PENDING(job_ptr)) {
			error_code = ESLURM_JOB_NOT_PENDING;
			goto fini;
		} else if (detail_ptr) {
			xfree(detail_ptr->work_dir);
			detail_ptr->work_dir = xstrdup(job_desc->work_dir);
			sched_info("%s: setting work_dir to %s for %pJ",
				   __func__, detail_ptr->work_dir, job_ptr);
			update_accounting = true;
		}
	}

	if (job_desc->std_err && detail_ptr &&
	    !xstrcmp(job_desc->std_err, detail_ptr->std_err)) {
		sched_debug("%s: new std_err identical to old std_err %s",
			    __func__, job_desc->std_err);
	} else if (job_desc->std_err) {
		if (!IS_JOB_PENDING(job_ptr))
			error_code = ESLURM_JOB_NOT_PENDING;
		else if (detail_ptr && job_desc->std_err[0] == '\0')
			xfree(detail_ptr->std_err);
		else if (detail_ptr) {
			xfree(detail_ptr->std_err);
			detail_ptr->std_err = xstrdup(job_desc->std_err);
		}
	}
	if (error_code != SLURM_SUCCESS)
		goto fini;

	if (job_desc->std_in && detail_ptr &&
	    !xstrcmp(job_desc->std_in, detail_ptr->std_in)) {
		sched_debug("%s: new std_in identical to old std_in %s",
			    __func__, job_desc->std_in);
	} else if (job_desc->std_in) {
		if (!IS_JOB_PENDING(job_ptr))
			error_code = ESLURM_JOB_NOT_PENDING;
		else if (detail_ptr && job_desc->std_in[0] == '\0')
			xfree(detail_ptr->std_in);
		else if (detail_ptr) {
			xfree(detail_ptr->std_in);
			detail_ptr->std_in = xstrdup(job_desc->std_in);
		}
	}
	if (error_code != SLURM_SUCCESS)
		goto fini;

	if (job_desc->std_out && detail_ptr &&
	    !xstrcmp(job_desc->std_out, detail_ptr->std_out)) {
		sched_debug("%s: new std_out identical to old std_out %s",
			    __func__, job_desc->std_out);
	} else if (job_desc->std_out) {
		if (!IS_JOB_PENDING(job_ptr))
			error_code = ESLURM_JOB_NOT_PENDING;
		else if (detail_ptr && job_desc->std_out[0] == '\0')
			xfree(detail_ptr->std_out);
		else if (detail_ptr) {
			xfree(detail_ptr->std_out);
			detail_ptr->std_out = xstrdup(job_desc->std_out);
		}
	}
	if (error_code != SLURM_SUCCESS)
		goto fini;

	if (job_desc->wckey
	    && !xstrcmp(job_desc->wckey, job_ptr->wckey)) {
		sched_debug("%s: new wckey identical to old wckey %pJ",
			    __func__, job_ptr);
	} else if (job_desc->wckey) {
		if (!IS_JOB_PENDING(job_ptr))
			error_code = ESLURM_JOB_NOT_PENDING;
		else {
			int rc = update_job_wckey((char *) __func__,
						  job_ptr, job_desc->wckey);
			if (rc != SLURM_SUCCESS)
				error_code = rc;
			else
				update_accounting = true;
		}
	}
	if (error_code != SLURM_SUCCESS)
		goto fini;

	if ((job_desc->min_nodes != NO_VAL) &&
	    (IS_JOB_RUNNING(job_ptr) || IS_JOB_SUSPENDED(job_ptr))) {
		uint32_t new_min_task_cnt;
		/*
		 * Use req_nodes to change the nodes associated with a running
		 * for lack of other field in the job request to use
		 */
		if ((job_desc->min_nodes == 0) && (job_ptr->node_cnt > 0) &&
		    job_ptr->details && job_ptr->details->expanding_jobid) {
			job_record_t *expand_job_ptr;
			bitstr_t *orig_job_node_bitmap, *orig_jobx_node_bitmap;

			expand_job_ptr = find_job_record(job_ptr->details->
							 expanding_jobid);
			if (expand_job_ptr == NULL) {
				info("%s: Invalid node count (%u) for %pJ update, JobId=%u to expand not found",
				     __func__, job_desc->min_nodes, job_ptr,
				     job_ptr->details->expanding_jobid);
				error_code = ESLURM_INVALID_JOB_ID;
				goto fini;
			}
			if (IS_JOB_SUSPENDED(job_ptr) ||
			    IS_JOB_SUSPENDED(expand_job_ptr)) {
				info("%s: Can not expand %pJ from %pJ, job is suspended",
				     __func__, expand_job_ptr, job_ptr);
				error_code = ESLURM_JOB_SUSPENDED;
				goto fini;
			}
			if ((job_ptr->step_list != NULL) &&
			    (list_count(job_ptr->step_list) != 0)) {
				info("%s: Attempt to merge %pJ with active steps into %pJ",
				     __func__, job_ptr, expand_job_ptr);
				error_code = ESLURMD_STEP_EXISTS;
				goto fini;
			}
			sched_info("%s: killing %pJ and moving all resources to %pJ",
				   __func__, job_ptr, expand_job_ptr);
			job_pre_resize_acctg(job_ptr);
			job_pre_resize_acctg(expand_job_ptr);
			_send_job_kill(job_ptr);

			xassert(job_ptr->job_resrcs);
			xassert(job_ptr->job_resrcs->node_bitmap);
			xassert(expand_job_ptr->job_resrcs->node_bitmap);
			orig_job_node_bitmap = bit_copy(job_ptr->node_bitmap);
			orig_jobx_node_bitmap = bit_copy(expand_job_ptr->
							 job_resrcs->
							 node_bitmap);
			error_code = select_g_job_expand(job_ptr,
							 expand_job_ptr);
			if (error_code == SLURM_SUCCESS) {
				_merge_job_licenses(job_ptr, expand_job_ptr);
				FREE_NULL_BITMAP(job_ptr->node_bitmap);
				job_ptr->node_bitmap = orig_job_node_bitmap;
				orig_job_node_bitmap = NULL;
				deallocate_nodes(job_ptr, false, false, false);
				bit_clear_all(job_ptr->node_bitmap);
				job_state_set(job_ptr, (JOB_COMPLETE |
							(job_ptr->job_state &
							 JOB_STATE_FLAGS)));
				_realloc_nodes(expand_job_ptr,
					       orig_jobx_node_bitmap);
				rebuild_step_bitmaps(expand_job_ptr,
						     orig_jobx_node_bitmap);
				(void) gs_job_fini(job_ptr);
				(void) gs_job_start(expand_job_ptr);
			}
			FREE_NULL_BITMAP(orig_job_node_bitmap);
			FREE_NULL_BITMAP(orig_jobx_node_bitmap);
			job_post_resize_acctg(job_ptr);
			job_post_resize_acctg(expand_job_ptr);
			/*
			 * Since job_post_resize_acctg will restart things,
			 * don't do it again.
			 */
			update_accounting = false;
			if (error_code)
				goto fini;
		} else if ((job_desc->min_nodes == 0) ||
			   (job_desc->min_nodes > job_ptr->node_cnt) ||
			   job_ptr->details->expanding_jobid) {
			sched_info("%s: Invalid node count (%u) for %pJ update",
				   __func__, job_desc->min_nodes, job_ptr);
			error_code = ESLURM_INVALID_NODE_COUNT;
			goto fini;
		} else if (job_desc->min_nodes == job_ptr->node_cnt) {
			debug2("%s: No change in node count update for %pJ",
			       __func__, job_ptr);
		} else if (!permit_job_shrink()) {
			error("%s: request to shrink %pJ denied by configuration",
			      __func__, job_ptr);
			error_code = ESLURM_NOT_SUPPORTED;
			goto fini;
		} else {
			int total = 0;
			node_record_t *node_ptr;
			bitstr_t *rem_nodes, *tmp_nodes;
			sched_info("%s: set node count to %u for %pJ", __func__,
				   job_desc->min_nodes, job_ptr);
			job_pre_resize_acctg(job_ptr);

			/*
			 * Don't remove the batch host from the job. The batch
			 * host isn't guaranteed to be the first bit set in
			 * job_ptr->node_bitmap because the batch host can be
			 * selected with the --batch and --constraint sbatch
			 * flags.
			 */
			tmp_nodes = bit_copy(job_ptr->node_bitmap);
			if (job_ptr->batch_host) {
				bitstr_t *batch_host_bitmap;
				if (node_name2bitmap(job_ptr->batch_host, false,
						     &batch_host_bitmap, NULL))
					error("%s: Invalid batch host %s for %pJ; this should never happen",
					      __func__, job_ptr->batch_host,
					      job_ptr);
				else {
					bit_and_not(tmp_nodes,
						    batch_host_bitmap);
					FREE_NULL_BITMAP(batch_host_bitmap);
					/*
					 * Set total to 1 since we're
					 * guaranteeing that we won't remove the
					 * batch host.
					 */
					total = 1;
				}
			}

			rem_nodes = bit_alloc(bit_size(tmp_nodes));
			for (int i = 0; next_node_bitmap(tmp_nodes, &i); i++) {
				if (++total <= job_desc->min_nodes)
					continue;
				bit_set(rem_nodes, i);
			}
#ifndef HAVE_FRONT_END
			abort_job_on_nodes(job_ptr, rem_nodes);
#endif
			orig_job_node_bitmap =
				bit_copy(job_ptr->job_resrcs->node_bitmap);
			for (int i = 0;
			     (node_ptr = next_node_bitmap(rem_nodes, &i));
			     i++) {
				kill_step_on_node(job_ptr, node_ptr, false);
				excise_node_from_job(job_ptr, node_ptr);
			}
			/* Resize the core bitmaps of the job's steps */
			rebuild_step_bitmaps(job_ptr, orig_job_node_bitmap);

			FREE_NULL_BITMAP(orig_job_node_bitmap);
			FREE_NULL_BITMAP(rem_nodes);
			FREE_NULL_BITMAP(tmp_nodes);
			(void) gs_job_start(job_ptr);
			job_post_resize_acctg(job_ptr);
			sched_info("%s: set nodes to %s for %pJ",
				   __func__, job_ptr->nodes, job_ptr);
			/*
			 * Since job_post_resize_acctg() will restart
			 * things don't do it again.
			 */
			update_accounting = false;
		}
		gres_stepmgr_job_build_details(
			job_ptr->gres_list_alloc,
			job_ptr->nodes,
			&job_ptr->gres_detail_cnt,
			&job_ptr->gres_detail_str,
			&job_ptr->gres_used);

		/*
		 * Ensure that the num_tasks is less than
		 * the number of cpus now that tasks can be changed
		 * for a running job.
		 */
		new_min_task_cnt = job_ptr->cpu_cnt / detail_ptr->cpus_per_task;
		if (detail_ptr->num_tasks > new_min_task_cnt)
			detail_ptr->num_tasks = new_min_task_cnt;

		tres_req_cnt_set = false;
	}

	if (job_desc->ntasks_per_node != NO_VAL16) {
		if ((!IS_JOB_PENDING(job_ptr)) || (detail_ptr == NULL))
			error_code = ESLURM_JOB_NOT_PENDING;
		else if (operator) {
			detail_ptr->ntasks_per_node =
				job_desc->ntasks_per_node;
			if (detail_ptr->pn_min_cpus <
			    detail_ptr->ntasks_per_node) {
				detail_ptr->pn_min_cpus =
					detail_ptr->orig_pn_min_cpus =
					job_desc->ntasks_per_node;
			}
			sched_info("%s: setting ntasks_per_node to %u for %pJ",
				   __func__, job_desc->ntasks_per_node, job_ptr);
		} else {
			sched_error("%s: Not super user: ignore ntasks_per_node change for job %pJ",
				    __func__, job_ptr);
			error_code = ESLURM_ACCESS_DENIED;
		}
	}
	if (error_code != SLURM_SUCCESS)
		goto fini;

	if (job_desc->ntasks_per_socket != NO_VAL16) {
		if ((!IS_JOB_PENDING(job_ptr)) || (detail_ptr == NULL) ||
		    (detail_ptr->mc_ptr == NULL)) {
			error_code = ESLURM_JOB_NOT_PENDING;
		} else if (operator) {
			detail_ptr->mc_ptr->ntasks_per_socket =
				job_desc->ntasks_per_socket;
			sched_info("%s: setting ntasks_per_socket to %u for %pJ",
				   __func__, job_desc->ntasks_per_socket,
				   job_ptr);
		} else {
			sched_error("%s: Not super user: ignore ntasks_per_socket change for %pJ",
				    __func__, job_ptr);
			error_code = ESLURM_ACCESS_DENIED;
		}
	}
	if (error_code != SLURM_SUCCESS)
		goto fini;

	if (job_desc->dependency) {
		/* Can't update dependency of revoked job */
		if ((!IS_JOB_PENDING(job_ptr)) || (job_ptr->details == NULL) ||
		    IS_JOB_REVOKED(job_ptr))
			error_code = ESLURM_JOB_NOT_PENDING;
		else if (!fed_mgr_is_origin_job(job_ptr)) {
			/*
			 * If the job became independent because of a dependency
			 * update, that job gets requeued on siblings and then
			 * the dependency update gets sent to siblings. So we
			 * silently ignore this update on the sibling.
			 */
		} else {
			int rc;
			rc = update_job_dependency(job_ptr,
						   job_desc->dependency);
			if (rc != SLURM_SUCCESS)
				error_code = rc;
			/*
			 * Because dependencies updated and we don't know where
			 * they used to be, send dependencies to all siblings
			 * so the siblings can update their dependency list.
			 */
			else {
				rc = fed_mgr_submit_remote_dependencies(job_ptr,
									true,
									false);
				if (rc) {
					error("%s: %pJ Failed to send remote dependencies to some or all siblings.",
					      __func__, job_ptr);
					error_code = rc;
				}
				/*
				 * Even if we fail to send remote dependencies,
				 * we already succeeded in updating the job's
				 * dependency locally, so we still need to
				 * do these things.
				 */
				xfree(job_ptr->details->orig_dependency);
				job_ptr->details->orig_dependency =
					xstrdup(job_ptr->details->dependency);
				sched_info("%s: setting dependency to %s for %pJ",
					   __func__,
					   job_ptr->details->dependency,
					   job_ptr);
				/*
				 * If the job isn't independent, remove pending
				 * remote sibling jobs
				 */
				if (!job_independent(job_ptr))
					fed_mgr_job_revoke_sibs(job_ptr);
			}
		}
	}
	if (error_code != SLURM_SUCCESS)
		goto fini;

	if (job_desc->begin_time) {
		if (IS_JOB_PENDING(job_ptr) && detail_ptr) {
			char time_str[256];
			/*
			 * Make sure this time is current, it does no good for
			 * accounting to say this job could have started before
			 * now
			 */
			if (job_desc->begin_time < now)
				job_desc->begin_time = now;

			if (detail_ptr->begin_time != job_desc->begin_time) {
				detail_ptr->begin_time = job_desc->begin_time;
				update_accounting = true;
				slurm_make_time_str(&detail_ptr->begin_time,
						    time_str, sizeof(time_str));
				sched_info("%s: setting begin to %s for %pJ",
					   __func__, time_str, job_ptr);
				acct_policy_remove_accrue_time(job_ptr, false);
			} else
				sched_debug("%s: new begin time identical to old begin time %pJ",
					    __func__, job_ptr);
		} else {
			error_code = ESLURM_JOB_NOT_PENDING;
			goto fini;
		}
	}

	if (valid_licenses) {
		if (IS_JOB_PENDING(job_ptr)) {
			FREE_NULL_LIST(job_ptr->license_list);
			job_ptr->license_list = license_list;
			license_list = NULL;
			sched_info("%s: changing licenses from '%s' to '%s' for pending %pJ",
				   __func__, job_ptr->licenses,
				   job_desc->licenses_tot, job_ptr);
			xfree(job_ptr->licenses);
			job_ptr->licenses = xstrdup(job_desc->licenses_tot);
			if (job_desc->bitflags & RESET_LIC_JOB)
				xfree(job_ptr->lic_req);
			else if (job_desc->licenses) {
				xfree(job_ptr->lic_req);
				job_ptr->lic_req = xstrdup(job_desc->licenses);
			}
		} else if (IS_JOB_RUNNING(job_ptr)) {
			/*
			 * Operators can modify license counts on running jobs,
			 * regular users can only completely remove license
			 * counts on running jobs.
			 */
			if (!operator && license_list) {
				sched_error("%s: Not operator user: ignore licenses change for %pJ",
					    __func__, job_ptr);
				error_code = ESLURM_ACCESS_DENIED;
				goto fini;
			}

			/*
			 * NOTE: This can result in oversubscription of
			 * licenses
			 */
			license_job_return(job_ptr);
			FREE_NULL_LIST(job_ptr->license_list);
			job_ptr->license_list = license_list;
			license_list = NULL;
			sched_info("%s: changing licenses from '%s' to '%s' for running %pJ",
				   __func__, job_ptr->licenses,
				   job_desc->licenses, job_ptr);
			xfree(job_ptr->licenses);
			job_ptr->licenses = xstrdup(job_desc->licenses);
			license_job_get(job_ptr, false);
		} else {
			/*
			 * licenses are valid, but job state or user not
			 * allowed to make changes
			 */
			sched_info("%s: could not change licenses for %pJ",
				   __func__, job_ptr);
			error_code = ESLURM_JOB_NOT_PENDING_NOR_RUNNING;
			FREE_NULL_LIST(license_list);
		}

		update_accounting = true;
	}
	if (error_code != SLURM_SUCCESS)
		goto fini;

	fail_reason = job_limits_check(&job_ptr, false);
	if (fail_reason != WAIT_NO_REASON) {
		if (fail_reason == WAIT_QOS_THRES)
			error_code = ESLURM_QOS_THRES;
		else if ((fail_reason == WAIT_PART_TIME_LIMIT) ||
			 (fail_reason == WAIT_PART_NODE_LIMIT) ||
			 (fail_reason == WAIT_PART_DOWN) ||
			 (fail_reason == WAIT_HELD))
			error_code = SLURM_SUCCESS;
		else
			error_code = ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE;

		if (error_code != SLURM_SUCCESS) {
			if ((job_ptr->state_reason != WAIT_HELD) &&
			    (job_ptr->state_reason != WAIT_HELD_USER) &&
			    (job_ptr->state_reason != WAIT_RESV_DELETED)) {
				job_ptr->state_reason = fail_reason;
				xfree(job_ptr->state_desc);
			}
			goto fini;
		}
	} else if ((job_ptr->state_reason != WAIT_HELD)
		   && (job_ptr->state_reason != WAIT_HELD_USER)
		   && (job_ptr->state_reason != WAIT_RESV_DELETED)
		   /*
		    * A job update can come while the prolog is running.
		    * Don't change state_reason if the prolog is running.
		    * _is_prolog_finished() relies on state_reason==WAIT_PROLOG
		    * to know if the prolog is running. If we change it here,
		    * then slurmctld will think that the prolog isn't running
		    * anymore and _slurm_rpc_job_ready will tell srun that the
		    * prolog is done even if it isn't. Then srun can launch a
		    * job step before the prolog is done, which breaks the
		    * behavior of PrologFlags=alloc and means that the job step
		    * could launch before the extern step sets up x11.
		    */
		   && (job_ptr->state_reason != WAIT_PROLOG)
		   && (job_ptr->state_reason != WAIT_MAX_REQUEUE)) {
		job_ptr->state_reason = WAIT_NO_REASON;
		xfree(job_ptr->state_desc);
	}

	if (job_desc->reboot != NO_VAL16) {
		if (!validate_super_user(uid)) {
			error("%s: Attempt to change reboot for %pJ",
			      __func__, job_ptr);
			error_code = ESLURM_ACCESS_DENIED;
		} else if (!IS_JOB_PENDING(job_ptr)) {
			error_code = ESLURM_JOB_NOT_PENDING;
			goto fini;
		} else {
			sched_info("%s: setting reboot to %u for %pJ",
				   __func__, job_desc->reboot, job_ptr);
			if (job_desc->reboot == 0)
				job_ptr->reboot = 0;
			else
				job_ptr->reboot = MAX(1, job_desc->reboot);
		}
	}

	if (job_desc->network && !xstrcmp(job_desc->network,
					   job_ptr->network)) {
		sched_debug("%s: new network identical to old network %s",
			    __func__, job_ptr->network);
	} else if (job_desc->network) {
		xfree(job_ptr->network);
		if (!strlen(job_desc->network)
		    || !xstrcmp(job_desc->network, "none")) {
			sched_info("%s: clearing Network option for %pJ",
				   __func__, job_ptr);
		} else {
			job_ptr->network = xstrdup(job_desc->network);
			sched_info("%s: setting Network to %s for %pJ",
				   __func__, job_ptr->network, job_ptr);
			select_g_select_jobinfo_set(
				job_ptr->select_jobinfo,
				SELECT_JOBDATA_NETWORK,
				job_ptr->network);
		}
	}

	if (job_desc->fed_siblings_viable) {
		if (!job_ptr->fed_details) {
			error_code = ESLURM_JOB_NOT_FEDERATED;
			goto fini;
		}

		info("%s: setting fed_siblings from %"PRIu64" to %"PRIu64" for %pJ",
		     __func__, job_ptr->fed_details->siblings_viable,
		     job_desc->fed_siblings_viable, job_ptr);

		job_ptr->fed_details->siblings_viable =
			job_desc->fed_siblings_viable;
		update_job_fed_details(job_ptr);
	}

	if (job_desc->cpus_per_tres) {
		if (!assoc_mgr_valid_tres_cnt(job_desc->cpus_per_tres, 0)) {
			error_code = ESLURM_INVALID_TRES;
			goto fini;
		}
		xfree(job_ptr->cpus_per_tres);
		if (!strlen(job_desc->cpus_per_tres)) {
			sched_info("%s: clearing CpusPerTres option for %pJ",
				   __func__, job_ptr);
		} else {
			job_ptr->cpus_per_tres =
				xstrdup(job_desc->cpus_per_tres);
			sched_info("%s: setting CpusPerTres to %s for %pJ",
				   __func__, job_ptr->cpus_per_tres, job_ptr);
		}
	}

	if (job_desc->mem_per_tres) {
		if (!assoc_mgr_valid_tres_cnt(job_desc->mem_per_tres, 0)) {
			error_code = ESLURM_INVALID_TRES;
			goto fini;
		}
		xfree(job_ptr->mem_per_tres);
		if (!strlen(job_desc->mem_per_tres)) {
			sched_info("%s: clearing MemPerTres option for %pJ",
				   __func__, job_ptr);
		} else {
			job_ptr->mem_per_tres =
				xstrdup(job_desc->mem_per_tres);
			sched_info("%s: setting MemPerTres to %s for %pJ",
				   __func__, job_ptr->mem_per_tres, job_ptr);
		}
	}

	if (job_desc->tres_bind) {
		if (tres_bind_verify_cmdline(job_desc->tres_bind)) {
			error_code = ESLURM_INVALID_TRES;
			goto fini;
		}
		xfree(job_ptr->tres_bind);
		if (!strlen(job_desc->tres_bind)) {
			sched_info("%s: clearing TresBind option for %pJ",
				   __func__, job_ptr);
		} else {
			job_ptr->tres_bind = xstrdup(job_desc->tres_bind);
			sched_info("%s: setting TresBind to %s for %pJ",
				   __func__, job_ptr->tres_bind, job_ptr);
		}
	}

	if (job_desc->tres_freq) {
		if (tres_freq_verify_cmdline(job_desc->tres_freq)) {
			error_code = ESLURM_INVALID_TRES;
			goto fini;
		}
		xfree(job_ptr->tres_freq);
		if (!strlen(job_desc->tres_freq)) {
			sched_info("%s: clearing TresFreq option for %pJ",
				   __func__, job_ptr);
		} else {
			job_ptr->tres_freq = xstrdup(job_desc->tres_freq);
			sched_info("%s: setting TresFreq to %s for %pJ",
				   __func__, job_ptr->tres_freq, job_ptr);
		}
	}

	if (job_desc->tres_per_job) {
		if (!assoc_mgr_valid_tres_cnt(job_desc->tres_per_job, 0)) {
			error_code = ESLURM_INVALID_TRES;
			goto fini;
		}
		xfree(job_ptr->tres_per_job);
		if (!strlen(job_desc->tres_per_job)) {
			sched_info("%s: clearing TresPerJob option for %pJ",
				   __func__, job_ptr);
		} else {
			job_ptr->tres_per_job =
				xstrdup(job_desc->tres_per_job);
			sched_info("%s: setting TresPerJob to %s for %pJ",
				   __func__, job_ptr->tres_per_job, job_ptr);
		}
	}
	if (job_desc->tres_per_node) {
		if (!assoc_mgr_valid_tres_cnt(job_desc->tres_per_node, 0)) {
			error_code = ESLURM_INVALID_TRES;
			goto fini;
		}
		xfree(job_ptr->tres_per_node);
		if (!strlen(job_desc->tres_per_node)) {
			sched_info("%s: clearing TresPerNode option for %pJ",
				   __func__, job_ptr);
		} else {
			job_ptr->tres_per_node =
				xstrdup(job_desc->tres_per_node);
			sched_info("%s: setting TresPerNode to %s for %pJ",
				   __func__, job_ptr->tres_per_node, job_ptr);
		}
	}

	if (job_desc->tres_per_socket) {
		if (!assoc_mgr_valid_tres_cnt(job_desc->tres_per_socket, 0)) {
			error_code = ESLURM_INVALID_TRES;
			goto fini;
		}
		xfree(job_ptr->tres_per_socket);
		if (!strlen(job_desc->tres_per_socket)) {
			sched_info("%s: clearing TresPerSocket option for %pJ",
				   __func__, job_ptr);
		} else {
			job_ptr->tres_per_socket =
				xstrdup(job_desc->tres_per_socket);
			sched_info("%s: setting TresPerSocket to %s for %pJ",
				   __func__, job_ptr->tres_per_socket, job_ptr);
		}
	}

	if (job_desc->tres_per_task) {
		if (!assoc_mgr_valid_tres_cnt(job_desc->tres_per_task, 0)) {
			error_code = ESLURM_INVALID_TRES;
			goto fini;
		}
		xfree(job_ptr->tres_per_task);
		if (!strlen(job_desc->tres_per_task)) {
			sched_info("%s: clearing TresPerTask option for %pJ",
				   __func__, job_ptr);
		} else {
			job_ptr->tres_per_task =
				xstrdup(job_desc->tres_per_task);
			sched_info("%s: setting TresPerTask to %s for %pJ",
				   __func__, job_ptr->tres_per_task, job_ptr);
		}
	}

	if (job_desc->mail_type != NO_VAL16) {
		job_ptr->mail_type = job_desc->mail_type;
		sched_info("%s: setting mail_type to %u for %pJ",
			   __func__, job_ptr->mail_type, job_ptr);
	}

	if (job_desc->mail_user) {
		xfree(job_ptr->mail_user);
		job_ptr->mail_user = _get_mail_user(job_desc->mail_user,
						    job_ptr);
		sched_info("%s: setting mail_user to %s for %pJ",
			   __func__, job_ptr->mail_user, job_ptr);
	}

	/*
	 * The job submit plugin sets site_factor to NO_VAL before calling
	 * the plugin to prevent the user from specifying it.
	 */
	if (user_site_factor != NO_VAL) {
		if (!operator) {
			error("%s: Attempt to change SiteFactor for %pJ",
			      __func__, job_ptr);
			error_code = ESLURM_ACCESS_DENIED;
			job_desc->site_factor = NO_VAL;
		} else
			job_desc->site_factor = user_site_factor;
	}
	if (job_desc->site_factor != NO_VAL) {
		sched_info("%s: setting AdinPrioFactor to %u for %pJ",
			   __func__, job_desc->site_factor, job_ptr);
		job_ptr->site_factor = job_desc->site_factor;
	}

fini:
	FREE_NULL_BITMAP(new_req_bitmap);
	FREE_NULL_LIST(part_ptr_list);

	if ((error_code == SLURM_SUCCESS) && tres_req_cnt_set) {
		for (tres_pos = 0; tres_pos < slurmctld_tres_cnt; tres_pos++) {
			if (tres_req_cnt[tres_pos] ==
			    job_ptr->tres_req_cnt[tres_pos])
				continue;

			job_ptr->tres_req_cnt[tres_pos] =
				tres_req_cnt[tres_pos];
			tres_changed = true;
		}
		if (tres_changed) {
			job_ptr->tres_req_cnt[TRES_ARRAY_BILLING] =
				assoc_mgr_tres_weighted(
					job_ptr->tres_req_cnt,
					job_ptr->part_ptr->billing_weights,
					slurm_conf.priority_flags, false);
			set_job_tres_req_str(job_ptr, false);
			update_accounting = true;
			job_ptr->node_cnt_wag = 0;
		}
	}

	/* This was a local variable, so set it back to NULL */
	job_desc->tres_req_cnt = NULL;

	if (!list_count(job_ptr->gres_list_req))
		FREE_NULL_LIST(job_ptr->gres_list_req);

	FREE_NULL_LIST(gres_list);
	FREE_NULL_LIST(license_list);
	if (update_accounting) {
		info("%s: updating accounting",  __func__);
		/* Update job record in accounting to reflect changes */
		jobacct_storage_g_job_start(acct_db_conn, job_ptr);
	}

	/*
	 * If job isn't held recalculate the priority when not using
	 * priority/basic. Since many factors of an update may affect priority
	 * considerations. Do this whether or not the update was successful or
	 * not.
	 */
	if ((job_ptr->priority != 0) &&
	    xstrcmp(slurm_conf.priority_type, "priority/basic"))
		set_job_prio(job_ptr);

	if ((error_code == SLURM_SUCCESS) &&
	    fed_mgr_fed_rec &&
	    job_ptr->fed_details && fed_mgr_is_origin_job(job_ptr)) {
		/* Send updates to sibling jobs */
		/* Add the siblings_active to be updated. They could have been
		 * updated if the job's ClusterFeatures were updated. */
		job_desc->fed_siblings_viable =
			job_ptr->fed_details->siblings_viable;
		fed_mgr_update_job(job_ptr->job_id, job_desc,
				   job_ptr->fed_details->siblings_active, uid);
	}

	return error_code;
}

/*
 * update_job - update a job's parameters per the supplied specifications
 * IN msg - RPC to update job, including change specification
 * IN uid - uid of user issuing RPC
 * IN send_msg - whether to send msg back or not
 * RET returns an error code from slurm_errno.h
 * global: job_list - global list of job entries
 *	last_job_update - time of last job table update
 */
extern int update_job(slurm_msg_t *msg, uid_t uid, bool send_msg)
{
	job_desc_msg_t *job_desc = msg->data;
	job_record_t *job_ptr;
	char *hostname = auth_g_get_host(msg);
	char *err_msg = NULL;
	int rc;

	xfree(job_desc->job_id_str);
	xstrfmtcat(job_desc->job_id_str, "%u", job_desc->job_id);

	if (hostname) {
		xfree(job_desc->alloc_node);
		job_desc->alloc_node = hostname;
	}

	job_ptr = find_job_record(job_desc->job_id);
	if (job_ptr == NULL) {
		info("%s: JobId=%u does not exist",
		     __func__, job_desc->job_id);
		rc = ESLURM_INVALID_JOB_ID;
	} else {
		if (job_ptr->array_recs && job_ptr->array_recs->task_id_bitmap)
			job_desc->array_bitmap =
				bit_copy(job_ptr->array_recs->task_id_bitmap);

		rc = _update_job(job_ptr, job_desc, uid, &err_msg);
	}
	if (send_msg)
		slurm_send_rc_err_msg(msg, rc, err_msg);
	xfree(job_desc->job_id_str);

	return rc;
}

/*
 * IN msg - RPC to update job, including change specification
 * IN job_desc - a job's specification
 * IN uid - uid of user issuing RPC
 * RET returns an error code from slurm_errno.h
 * global: job_list - global list of job entries
 *	last_job_update - time of last job table update
 */
extern int update_job_str(slurm_msg_t *msg, uid_t uid)
{
	job_desc_msg_t *job_desc = msg->data;
	job_record_t *job_ptr, *new_job_ptr, *het_job;
	char *hostname = auth_g_get_host(msg);
	list_itr_t *iter;
	long int long_id;
	uint32_t job_id = 0, het_job_offset;
	bitstr_t *array_bitmap = NULL, *tmp_bitmap;
	int32_t i, i_first, i_last;
	int len, rc = SLURM_SUCCESS, rc2;
	char *end_ptr, *tmp = NULL;
	char *job_id_str;
	char *err_msg = NULL;
	resp_array_struct_t *resp_array = NULL;

	job_id_str = job_desc->job_id_str;

	if (hostname) {
		xfree(job_desc->alloc_node);
		job_desc->alloc_node = hostname;

	}

	if (max_array_size == NO_VAL)
		max_array_size = slurm_conf.max_array_sz;

	long_id = strtol(job_id_str, &end_ptr, 10);
	if ((long_id <= 0) || (long_id == LONG_MAX) ||
	    ((end_ptr[0] != '\0') && (end_ptr[0] != '_') &&
	     (end_ptr[0] != '+'))) {
		info("%s: invalid JobId=%s", __func__, job_id_str);
		rc = ESLURM_INVALID_JOB_ID;
		goto reply;
	}
	job_id = (uint32_t) long_id;
	if (end_ptr[0] == '\0') {	/* Single job (or full job array) */
		job_record_t *job_ptr_done = NULL;
		job_ptr = find_job_record(job_id);
		if (job_ptr && job_ptr->het_job_list) {
			iter = list_iterator_create(job_ptr->het_job_list);
			while ((het_job = list_next(iter))) {
				if (job_ptr->het_job_id !=
				    het_job->het_job_id) {
					error("%s: Bad het_job_list for %pJ",
					      __func__, job_ptr);
					continue;
				}
				if (job_desc->array_inx) {
					err_msg = xstrdup("Update of ArrayTaskThrottle is only allowed on ArrayJobId");
					rc = ESLURM_NOT_SUPPORTED;
					break;
				} else {
					rc = _update_job(het_job, job_desc, uid,
							 &err_msg);
				}
			}
			list_iterator_destroy(iter);
			goto reply;
		}
		if (job_ptr &&
		    (((job_ptr->array_task_id == NO_VAL) &&
		      (job_ptr->array_recs == NULL)) ||
		     ((job_ptr->array_task_id != NO_VAL) &&
		      (job_ptr->array_job_id  != job_id)))) {
			/* This is a regular job or single task of job array */
			if (job_desc->array_inx) {
				err_msg = xstrdup("Update of ArrayTaskThrottle is only allowed on ArrayJobId");
				rc = ESLURM_NOT_SUPPORTED;
			} else
				rc = _update_job(job_ptr, job_desc, uid,
						 &err_msg);
			goto reply;
		}

		if (job_ptr && job_ptr->array_recs) {
			/* This is a job array */
			job_ptr_done = job_ptr;
			if (job_ptr->array_recs->task_id_bitmap)
				job_desc->array_bitmap = bit_copy(
					job_ptr->array_recs->task_id_bitmap);
			rc2 = _update_job(job_ptr, job_desc, uid, &err_msg);
			_resp_array_add(&resp_array, job_ptr, rc2, err_msg);
			xfree(err_msg);
		}

		/* Update all tasks of this job array */
		job_ptr = job_array_hash_j[JOB_HASH_INX(job_id)];
		if (!job_ptr && !job_ptr_done) {
			info("%s: invalid JobId=%u", __func__, job_id);
			rc = ESLURM_INVALID_JOB_ID;
			goto reply;
		}
		while (job_ptr) {
			if ((job_ptr->array_job_id == job_id) &&
			    (job_ptr != job_ptr_done)) {
				rc2 = _update_job(job_ptr, job_desc, uid,
						  &err_msg);
				_resp_array_add(&resp_array, job_ptr, rc2,
						err_msg);
				xfree(err_msg);
			}
			job_ptr = job_ptr->job_array_next_j;
		}
		goto reply;
	} else if (end_ptr[0] == '+') {	/* Hetjob element */
		long_id = strtol(end_ptr+1, &tmp, 10);
		if ((long_id < 0) || (long_id == LONG_MAX) ||
		    (tmp[0] != '\0')) {
			info("%s: invalid JobId=%s", __func__, job_id_str);
			rc = ESLURM_INVALID_JOB_ID;
			goto reply;
		}
		het_job_offset = (uint32_t) long_id;
		job_ptr = find_het_job_record(job_id, het_job_offset);
		if (!job_ptr) {
			info("%s: invalid JobId=%u", __func__, job_id);
			rc = ESLURM_INVALID_JOB_ID;
			goto reply;
		}
		if (job_desc->array_inx) {
			err_msg = xstrdup("Update of ArrayTaskThrottle is only allowed on ArrayJobId");
			rc = ESLURM_NOT_SUPPORTED;
		} else {
			rc = _update_job(job_ptr, job_desc, uid, &err_msg);
		}
		goto reply;
	}

	array_bitmap = slurm_array_str2bitmap(end_ptr + 1, max_array_size,
					      &i_last);
	if (!array_bitmap) {
		info("%s: invalid JobId=%s", __func__, job_id_str);
		rc = ESLURM_INVALID_JOB_ID;
		goto reply;
	}

	job_ptr = find_job_record(job_id);
	if (job_ptr && IS_JOB_PENDING(job_ptr) &&
	    job_ptr->array_recs && job_ptr->array_recs->task_id_bitmap) {
		/* Ensure bitmap sizes match for AND operations */
		len = bit_size(job_ptr->array_recs->task_id_bitmap);
		i_last++;
		if (i_last < len) {
			bit_realloc(array_bitmap, len);
		} else {
			bit_realloc(array_bitmap, i_last);
			bit_realloc(job_ptr->array_recs->task_id_bitmap,
				    i_last);
		}
		if (!bit_overlap_any(job_ptr->array_recs->task_id_bitmap,
				     array_bitmap)) {
			/* Nothing to do with this job record */
		} else if (bit_super_set(job_ptr->array_recs->task_id_bitmap,
					 array_bitmap)) {
			/* Update the record with all pending tasks */
			job_desc->array_bitmap =
				bit_copy(job_ptr->array_recs->task_id_bitmap);
			if (job_desc->array_inx) {
				err_msg = xstrdup("Update of ArrayTaskThrottle is only allowed on ArrayJobId");
				rc2 = ESLURM_NOT_SUPPORTED;
			} else
				rc2 = _update_job(job_ptr, job_desc, uid,
						  &err_msg);
			_resp_array_add(&resp_array, job_ptr, rc2, err_msg);
			xfree(err_msg);
			bit_and_not(array_bitmap, job_desc->array_bitmap);
		} else {
			/* Need to split out tasks to separate job records */
			tmp_bitmap = bit_copy(job_ptr->array_recs->
					      task_id_bitmap);
			bit_and(tmp_bitmap, array_bitmap);
			i_first = bit_ffs(tmp_bitmap);
			if (i_first >= 0)
				i_last = bit_fls(tmp_bitmap);
			else
				i_last = -2;
			for (i = i_first; i <= i_last; i++) {
				if (!bit_test(tmp_bitmap, i))
					continue;
				job_ptr->array_task_id = i;
				new_job_ptr = job_array_split(job_ptr, true);

				/*
				 * The array_recs structure is moved to the
				 * new job record copy.
				 */
				bb_g_job_validate2(job_ptr, NULL);
				job_ptr = new_job_ptr;
			}
			FREE_NULL_BITMAP(tmp_bitmap);
		}
	}

	i_first = bit_ffs(array_bitmap);
	if (i_first >= 0)
		i_last = bit_fls(array_bitmap);
	else
		i_last = -2;
	for (i = i_first; i <= i_last; i++) {
		if (!bit_test(array_bitmap, i))
			continue;
		job_ptr = find_job_array_rec(job_id, i);
		if (job_ptr == NULL) {
			info("%s: invalid JobId=%u_%d", __func__, job_id, i);
			_resp_array_add_id(&resp_array, job_id, i,
					   ESLURM_INVALID_JOB_ID);
			continue;
		}

		if (job_desc->array_inx) {
			err_msg = xstrdup("Update of ArrayTaskThrottle is only allowed on ArrayJobId");
			rc2 = ESLURM_NOT_SUPPORTED;
		} else
			rc2 = _update_job(job_ptr, job_desc, uid, &err_msg);
		_resp_array_add(&resp_array, job_ptr, rc2, err_msg);
		xfree(err_msg);
	}

reply:
	if (msg->conn_fd >= 0) {
		if (resp_array) {
			job_array_resp_msg_t *resp_array_msg =
				_resp_array_xlate(resp_array, job_id);
			(void) send_msg_response(msg, RESPONSE_JOB_ARRAY_ERRORS,
						 resp_array_msg);
			slurm_free_job_array_resp(resp_array_msg);
		} else {
			slurm_send_rc_err_msg(msg, rc, err_msg);
		}
	}
	xfree(err_msg);
	_resp_array_free(resp_array);

	FREE_NULL_BITMAP(array_bitmap);

	return rc;
}

extern kill_job_msg_t *create_kill_job_msg(job_record_t *job_ptr,
					   uint16_t protocol_version)
{
	slurm_cred_arg_t cred_arg;
	kill_job_msg_t *msg = xmalloc(sizeof(*msg));

	xassert(job_ptr);
	xassert(job_ptr->details);

	setup_cred_arg(&cred_arg, job_ptr);

	cred_arg.step_id.job_id = job_ptr->job_id;
	cred_arg.step_id.step_het_comp = NO_VAL;
	cred_arg.step_id.step_id = NO_VAL;

	msg->cred = slurm_cred_create(&cred_arg, false, protocol_version);

	msg->derived_ec = job_ptr->derived_ec;
	msg->details = xstrdup(job_ptr->state_desc);
	msg->exit_code = job_ptr->exit_code;
	msg->het_job_id = job_ptr->het_job_id;
	msg->job_gres_prep = gres_g_prep_build_env(job_ptr->gres_list_alloc,
						   job_ptr->nodes);
	msg->job_state = job_ptr->job_state;
	msg->job_uid = job_ptr->user_id;
	msg->job_gid = job_ptr->group_id;
	msg->start_time = job_ptr->start_time;
	msg->step_id.job_id = job_ptr->job_id;
	msg->step_id.step_het_comp = NO_VAL;
	msg->step_id.step_id = NO_VAL;
	msg->spank_job_env = xduparray(job_ptr->spank_job_env_size,
				       job_ptr->spank_job_env);
	msg->spank_job_env_size = job_ptr->spank_job_env_size;
	msg->time = time(NULL);
	msg->work_dir = xstrdup(job_ptr->details->work_dir);

	return msg;
}

static void _send_job_kill(job_record_t *job_ptr)
{
	agent_arg_t *agent_args = NULL;
#ifdef HAVE_FRONT_END
	front_end_record_t *front_end_ptr;
#else
	node_record_t *node_ptr;
#endif
	kill_job_msg_t *kill_job;

	agent_args = xmalloc(sizeof(agent_arg_t));
	agent_args->msg_type = REQUEST_TERMINATE_JOB;
	agent_args->retry = 0;	/* re_kill_job() resends as needed */
	agent_args->hostlist = hostlist_create(NULL);

	last_node_update    = time(NULL);

#ifdef HAVE_FRONT_END
	if (job_ptr->batch_host &&
	    (front_end_ptr = job_ptr->front_end_ptr)) {
		agent_args->protocol_version = front_end_ptr->protocol_version;
		hostlist_push_host(agent_args->hostlist, job_ptr->batch_host);
		agent_args->node_count++;
	}
#else
	if (!job_ptr->node_bitmap_cg)
		build_cg_bitmap(job_ptr);
	agent_args->protocol_version = SLURM_PROTOCOL_VERSION;
	for (int i = 0;
	     (node_ptr = next_node_bitmap(job_ptr->node_bitmap_cg, &i)); i++) {
		if (agent_args->protocol_version > node_ptr->protocol_version)
			agent_args->protocol_version =
				node_ptr->protocol_version;
		hostlist_push_host(agent_args->hostlist, node_ptr->name);
		agent_args->node_count++;
		if (PACK_FANOUT_ADDRS(node_ptr))
			agent_args->msg_flags |= SLURM_PACK_ADDRS;
	}
#endif
	if (agent_args->node_count == 0) {
		if (job_ptr->details->expanding_jobid == 0) {
			error("%s: %pJ allocated no nodes to be killed on",
			      __func__, job_ptr);
		}
		hostlist_destroy(agent_args->hostlist);
		xfree(agent_args);
		return;
	}

	kill_job = create_kill_job_msg(job_ptr, agent_args->protocol_version);
	kill_job->nodes = xstrdup(job_ptr->nodes);

	agent_args->msg_args = kill_job;
	set_agent_arg_r_uid(agent_args, SLURM_AUTH_UID_ANY);
	agent_queue_request(agent_args);
}

/* Record accounting information for a job immediately before changing size */
extern void job_pre_resize_acctg(job_record_t *job_ptr)
{
	job_state_set_flag(job_ptr, JOB_RESIZING);
	job_ptr->resize_time = time(NULL);
	/* NOTE: job_completion_logger() calls
	 *	 acct_policy_remove_job_submit() */
	job_completion_logger(job_ptr, false);

	/* This doesn't happen in job_completion_logger, but gets
	 * added back in with job_post_resize_acctg so remove it here. */
	acct_policy_job_fini(job_ptr, false);

	/* NOTE: The RESIZING FLAG needed to be cleared with
	   job_post_resize_acctg */
}

/* Record accounting information for a job immediately after changing size */
extern void job_post_resize_acctg(job_record_t *job_ptr)
{
	/*
	 * NOTE: The RESIZING FLAG needed to be set with job_pre_resize_acctg()
	 * the assert is here to make sure we code it that way.
	 */
	xassert(IS_JOB_RESIZING(job_ptr));
	acct_policy_add_job_submit(job_ptr, false);
	/* job_set_alloc_tres() must be called before acct_policy_job_begin() */
	job_set_alloc_tres(job_ptr, false);

	/*
	 * Clear out the old request and replace it with the new alloc.
	 * This probably isn't totally perfect in all situations, but it will
	 * make it tres_req_* correct enough to the user. The tres_req_* isn't
	 * used to make any decisions. It is stored in the database, but only
	 * as a reference for non-pending jobs, which in this case will always
	 * be the case.
	 */
	memcpy(job_ptr->tres_req_cnt, job_ptr->tres_alloc_cnt,
	       slurmctld_tres_cnt * sizeof(uint64_t));
	xfree(job_ptr->tres_req_str);
	job_ptr->tres_req_str = xstrdup(job_ptr->tres_alloc_str);
	xfree(job_ptr->tres_fmt_req_str);
	job_ptr->tres_fmt_req_str = xstrdup(job_ptr->tres_fmt_alloc_str);

	acct_policy_job_begin(job_ptr, false);
	resv_replace_update(job_ptr);

	/*
	 * Get new sluid now that we are basically a new job.
	 */
	job_record_set_sluid(job_ptr);
	jobacct_storage_g_job_start(acct_db_conn, job_ptr);

	job_state_unset_flag(job_ptr, JOB_RESIZING);

	/*
	 * Reset the end_time_exp that was probably set to NO_VAL when
	 * ending the job on the resize.  If using the
	 * priority/multifactor plugin if the end_time_exp is NO_VAL
	 * it will not run again for the job.
	 */
	job_ptr->end_time_exp = job_ptr->end_time;
}

/*
 * validate_jobs_on_node - validate that any jobs that should be on the node
 *	are actually running, if not clean up the job records and/or node
 *	records.
 * IN slurm_msg - contains the node registration message
 */
extern void validate_jobs_on_node(slurm_msg_t *slurm_msg)
{
	int i, jobs_on_node;
	node_record_t *node_ptr;
	job_record_t *job_ptr;
	step_record_t *step_ptr;
	time_t now = time(NULL);

	slurm_node_registration_status_msg_t *reg_msg = slurm_msg->data;

	node_ptr = find_node_record(reg_msg->node_name);
	if (node_ptr == NULL) {
		error("slurmd registered on unknown node %s",
		      reg_msg->node_name);
		return;
	}

	/*
	 * Set protocol_version now because abort_job_on_node() needs to know
	 * the node's correct version. validate_node_specs() sets it but that's
	 * too late.
	 */
	node_ptr->protocol_version = slurm_msg->protocol_version;

	if (reg_msg->energy)
		memcpy(node_ptr->energy, reg_msg->energy,
		       sizeof(acct_gather_energy_t));

	if (node_ptr->up_time > reg_msg->up_time) {
		verbose("Node %s rebooted %u secs ago",
			reg_msg->node_name, reg_msg->up_time);
	}

	if (reg_msg->up_time <= now) {
		node_ptr->up_time = reg_msg->up_time;
		node_ptr->boot_time = now - reg_msg->up_time;
		node_ptr->slurmd_start_time = reg_msg->slurmd_start_time;
	} else {
		error("Node up_time is invalid: %u>%u", reg_msg->up_time,
		      (uint32_t) now);
	}

	if (waiting_for_node_boot(node_ptr) ||
	    waiting_for_node_power_down(node_ptr))
		return;

	/* Check that jobs running are really supposed to be there */
	for (i = 0; i < reg_msg->job_count; i++) {
		if ( (reg_msg->step_id[i].job_id >= MIN_NOALLOC_JOBID) &&
		     (reg_msg->step_id[i].job_id <= MAX_NOALLOC_JOBID) ) {
			info("NoAllocate %ps reported on node %s",
			     &reg_msg->step_id[i], reg_msg->node_name);
			continue;
		}

		job_ptr = find_job_record(reg_msg->step_id[i].job_id);
		if (job_ptr == NULL) {
			error("Orphan %ps reported on node %s",
			      &reg_msg->step_id[i],
			      reg_msg->node_name);
			abort_job_on_node(reg_msg->step_id[i].job_id,
					  job_ptr, node_ptr->name);
		}

		else if (IS_JOB_RUNNING(job_ptr) ||
			 IS_JOB_SUSPENDED(job_ptr)) {
			if (bit_test(job_ptr->node_bitmap, node_ptr->index)) {
				if ((job_ptr->batch_flag) &&
				    (node_ptr->index == bit_ffs(
					    job_ptr->node_bitmap))) {
					/* NOTE: Used for purging defunct
					 * batch jobs */
					job_ptr->time_last_active = now;
				}
				step_ptr = find_step_record(job_ptr,
							    &reg_msg->
							    step_id[i]);
				if (step_ptr)
					step_ptr->time_last_active = now;
				debug3("Registered %pS on node %s",
				       step_ptr, reg_msg->node_name);
			} else {
				/* Typically indicates a job requeue and
				 * restart on another nodes. A node from the
				 * original allocation just responded here. */
				error("Registered %pJ %ps on wrong node %s",
				      job_ptr,
				      &reg_msg->step_id[i],
				      reg_msg->node_name);
				info("%s: job nodes %s count %d inx %d",
				     __func__, job_ptr->nodes,
				     job_ptr->node_cnt, node_ptr->index);
				abort_job_on_node(reg_msg->step_id[i].job_id,
						  job_ptr,
						  node_ptr->name);
			}
		}

		else if (IS_JOB_COMPLETING(job_ptr)) {
			/*
			 * Re-send kill request as needed,
			 * not necessarily an error
			 */
			kill_job_on_node(job_ptr, node_ptr);
		}


		else if (IS_JOB_PENDING(job_ptr)) {
			/* Typically indicates a job requeue and the hung
			 * slurmd that went DOWN is now responding */
			error("Registered PENDING %pJ %ps on node %s",
			      job_ptr,
			      &reg_msg->step_id[i],
			      reg_msg->node_name);
			abort_job_on_node(reg_msg->step_id[i].job_id,
					  job_ptr, node_ptr->name);
		} else if (difftime(now, job_ptr->end_time) <
		           slurm_conf.msg_timeout) {
			/* Race condition */
			debug("Registered newly completed %pJ %ps on %s",
			      job_ptr,
			      &reg_msg->step_id[i],
			      node_ptr->name);
		}

		else {		/* else job is supposed to be done */
			error("Registered %pJ %ps in state %s on node %s",
			      job_ptr,
			      &reg_msg->step_id[i],
			      job_state_string(job_ptr->job_state),
			      reg_msg->node_name);
			kill_job_on_node(job_ptr, node_ptr);
		}
	}

	jobs_on_node = node_ptr->run_job_cnt + node_ptr->comp_job_cnt;
	if (jobs_on_node)
		_purge_missing_jobs(node_ptr->index, now);

	if (jobs_on_node != reg_msg->job_count) {
		/* slurmd will not know of a job unless the job has
		 * steps active at registration time, so this is not
		 * an error condition, slurmd is also reporting steps
		 * rather than jobs */
		debug3("resetting job_count on node %s from %u to %d",
		       reg_msg->node_name, reg_msg->job_count, jobs_on_node);
		reg_msg->job_count = jobs_on_node;
	}
}

/* Purge any batch job that should have its script running on node
 * node_inx, but is not. Allow BatchStartTimeout + ResumeTimeout seconds
 * for startup.
 *
 * Purge all job steps that were started before the node was last booted.
 *
 * Also notify srun if any job steps should be active on this node
 * but are not found. */
static void _purge_missing_jobs(int node_inx, time_t now)
{
	list_itr_t *job_iterator;
	job_record_t *job_ptr;
	node_record_t *node_ptr = node_record_table_ptr[node_inx];
	time_t batch_startup_time, node_boot_time = (time_t) 0, startup_time;
	static bool power_save_on = false;
	static time_t sched_update = 0;

	if (sched_update != slurm_conf.last_update) {
		power_save_on = power_save_test();
		sched_update = slurm_conf.last_update;
	}

	if (node_ptr->boot_time > (slurm_conf.msg_timeout + 5)) {
		/* allow for message timeout and other delays */
		node_boot_time = node_ptr->boot_time -
			(slurm_conf.msg_timeout + 5);
	}
	batch_startup_time  = now - slurm_conf.batch_start_timeout;
	batch_startup_time -= MIN(DEFAULT_MSG_TIMEOUT, slurm_conf.msg_timeout);

	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = list_next(job_iterator))) {
		if ((IS_JOB_CONFIGURING(job_ptr) ||
		     (!IS_JOB_RUNNING(job_ptr) &&
		      !IS_JOB_SUSPENDED(job_ptr))) ||
		    (!bit_test(job_ptr->node_bitmap, node_inx)))
			continue;
		if ((job_ptr->batch_flag != 0) && power_save_on &&
		    (job_ptr->start_time < node_boot_time)) {
			startup_time = batch_startup_time -
				slurm_conf.resume_timeout;
		} else
			startup_time = batch_startup_time;

		if ((job_ptr->batch_flag != 0)			&&
		    (job_ptr->het_job_offset == 0)		&&
		    (job_ptr->time_last_active < startup_time)	&&
		    (job_ptr->start_time       < startup_time)	&&
		    (node_ptr == find_node_record(job_ptr->batch_host))) {
			bool requeue = false;
			char *requeue_msg = "";
			if (job_ptr->details && job_ptr->details->requeue) {
				requeue = true;
				requeue_msg = ", Requeuing job";
			}
			info("Batch %pJ missing from batch node %s (not found BatchStartTime after startup)%s",
			     job_ptr, job_ptr->batch_host, requeue_msg);
			xfree(job_ptr->failed_node);
			job_ptr->failed_node = xstrdup(job_ptr->batch_host);
			job_complete(job_ptr->job_id, slurm_conf.slurm_user_id,
			             requeue, true, 1);
		} else {
			_notify_srun_missing_step(job_ptr, node_inx,
						  now, node_boot_time);
		}
	}
	list_iterator_destroy(job_iterator);
}

static void _notify_srun_missing_step(job_record_t *job_ptr, int node_inx,
				      time_t now, time_t node_boot_time)
{
	list_itr_t *step_iterator;
	step_record_t *step_ptr;
	char *node_name = node_record_table_ptr[node_inx]->name;

	xassert(job_ptr);
	step_iterator = list_iterator_create (job_ptr->step_list);
	while ((step_ptr = list_next(step_iterator))) {
		if ((step_ptr->step_id.step_id == SLURM_EXTERN_CONT) ||
		    (step_ptr->step_id.step_id == SLURM_BATCH_SCRIPT) ||
		    (step_ptr->state != JOB_RUNNING))
			continue;
		if (!bit_test(step_ptr->step_node_bitmap, node_inx))
			continue;
		if (step_ptr->time_last_active >= now) {
			/* Back up timer in case more than one node
			 * registration happens at this same time.
			 * We don't want this node's registration
			 * to count toward a different node's
			 * registration message. */
			step_ptr->time_last_active = now - 1;
		} else if (step_ptr->host && step_ptr->port) {
			/* srun may be able to verify step exists on
			 * this node using I/O sockets and kill the
			 * job as needed */
			srun_step_missing(step_ptr, node_name);
		} else if ((step_ptr->start_time < node_boot_time) &&
			   !(step_ptr->flags & SSF_NO_KILL)) {
			/* There is a risk that the job step's tasks completed
			 * on this node before its reboot, but that should be
			 * very rare and there is no srun to work with (POE) */
			info("Node %s rebooted, killing missing step %u.%u",
			     node_name, job_ptr->job_id, step_ptr->step_id.step_id);
			signal_step_tasks_on_node(node_name, step_ptr, SIGKILL,
						  REQUEST_TERMINATE_TASKS);
		}
	}
	list_iterator_destroy (step_iterator);
}

/*
 * abort_job_on_node - Kill the specific job_id on a specific node,
 *	the request is not processed immediately, but queued.
 *	This is to prevent a flood of pthreads if slurmctld restarts
 *	without saved state and slurmd daemons register with a
 *	multitude of running jobs. Slurmctld will not recognize
 *	these jobs and use this function to kill them - one
 *	agent request per node as they register.
 * IN job_id - id of the job to be killed
 * IN job_ptr - pointer to terminating job (NULL if unknown, e.g. job reported
 *		by slurmd on some node, but job records already purged from
 *		slurmctld)
 * IN node_name - name of the node on which the job resides
 */
extern void abort_job_on_node(uint32_t job_id, job_record_t *job_ptr,
			      char *node_name)
{
	agent_arg_t *agent_info;
	kill_job_msg_t *kill_req;

	agent_info = xmalloc(sizeof(agent_arg_t));
	agent_info->node_count	= 1;
	agent_info->retry	= 0;
	agent_info->hostlist	= hostlist_create(node_name);
#ifdef HAVE_FRONT_END
	if (job_ptr && job_ptr->front_end_ptr)
		agent_info->protocol_version =
			job_ptr->front_end_ptr->protocol_version;
	if (job_ptr) {
		debug("Aborting %pJ on front end node %s", job_ptr, node_name);
	} else {
		debug("Aborting JobId=%u on front end node %s", job_id,
		      node_name);
	}
#else
	node_record_t *node_ptr;
	if ((node_ptr = find_node_record(node_name)))
		agent_info->protocol_version = node_ptr->protocol_version;
	if (job_ptr)
		debug("Aborting %pJ on node %s", job_ptr, node_name);
	else
		debug("Aborting JobId=%u on node %s", job_id, node_name);
#endif

	if (job_ptr) {  /* NULL if unknown */
		kill_req = create_kill_job_msg(job_ptr,
					       agent_info->protocol_version);
	} else {
		kill_req = xmalloc(sizeof(*kill_req));
		kill_req->step_id.job_id = job_id;
		kill_req->step_id.step_id = NO_VAL;
		kill_req->step_id.step_het_comp = NO_VAL;
		kill_req->time = time(NULL);
		/* kill_req->start_time = 0;  Default value */
	}

	kill_req->nodes = xstrdup(node_name);

	agent_info->msg_type	= REQUEST_ABORT_JOB;
	agent_info->msg_args	= kill_req;

	set_agent_arg_r_uid(agent_info, SLURM_AUTH_UID_ANY);
	agent_queue_request(agent_info);
}

/*
 * abort_job_on_nodes - Kill the specific job_on the specific nodes,
 *	the request is not processed immediately, but queued.
 *	This is to prevent a flood of pthreads if slurmctld restarts
 *	without saved state and slurmd daemons register with a
 *	multitude of running jobs. Slurmctld will not recognize
 *	these jobs and use this function to kill them - one
 *	agent request per node as they register.
 * IN job_ptr - pointer to terminating job
 * IN node_name - name of the node on which the job resides
 */
extern void abort_job_on_nodes(job_record_t *job_ptr,
			       bitstr_t *node_bitmap)
{
	bitstr_t *full_node_bitmap, *tmp_node_bitmap;
	node_record_t *node_ptr;
	int zero = 0;
	agent_arg_t *agent_info;
	kill_job_msg_t *kill_req;
	uint16_t protocol_version;

#ifdef HAVE_FRONT_END
	fatal("%s: front-end mode not supported", __func__);
#endif
	xassert(node_bitmap);
	/* Send a separate message for nodes at different protocol_versions */
	full_node_bitmap = bit_copy(node_bitmap);
	while ((node_ptr = next_node_bitmap(full_node_bitmap, &zero))) {
		protocol_version = node_ptr->protocol_version;
		tmp_node_bitmap = bit_alloc(bit_size(node_bitmap));
		for (int i = 0;
		     (node_ptr = next_node_bitmap(full_node_bitmap, &i)); i++) {
			if (node_ptr->protocol_version != protocol_version)
				continue;
			bit_clear(full_node_bitmap, i);
			bit_set(tmp_node_bitmap, i);
		}
		kill_req = create_kill_job_msg(job_ptr, protocol_version);
		kill_req->nodes = bitmap2node_name_sortable(tmp_node_bitmap,
							    false);
		agent_info = xmalloc(sizeof(agent_arg_t));
		agent_info->node_count	= bit_set_count(tmp_node_bitmap);
		agent_info->retry	= 1;
		agent_info->hostlist	= hostlist_create(kill_req->nodes);
		debug("Aborting %pJ on nodes %s", job_ptr, kill_req->nodes);
		agent_info->msg_type	= REQUEST_ABORT_JOB;
		agent_info->msg_args	= kill_req;
		agent_info->protocol_version = protocol_version;
		set_agent_arg_r_uid(agent_info, SLURM_AUTH_UID_ANY);
		agent_queue_request(agent_info);
		FREE_NULL_BITMAP(tmp_node_bitmap);
	}
	FREE_NULL_BITMAP(full_node_bitmap);
}

/*
 * kill_job_on_node - Kill the specific job on a specific node.
 * IN job_ptr - pointer to terminating job (NULL if unknown, e.g. orphaned)
 * IN node_ptr - pointer to the node on which the job resides
 */
extern void kill_job_on_node(job_record_t *job_ptr,
			     node_record_t *node_ptr)
{
	agent_arg_t *agent_info;
	kill_job_msg_t *kill_req;

	agent_info = xmalloc(sizeof(agent_arg_t));
	agent_info->node_count	= 1;
	agent_info->retry	= 0;
#ifdef HAVE_FRONT_END
	xassert(job_ptr->batch_host);
	if (job_ptr->front_end_ptr)
		agent_info->protocol_version =
			job_ptr->front_end_ptr->protocol_version;
	agent_info->hostlist	= hostlist_create(job_ptr->batch_host);
	debug("Killing %pJ on front end node %s",
	      job_ptr, job_ptr->batch_host);
#else
	agent_info->protocol_version = node_ptr->protocol_version;
	agent_info->hostlist	= hostlist_create(node_ptr->name);
	debug("Killing %pJ on node %s", job_ptr, node_ptr->name);
#endif

	kill_req = create_kill_job_msg(job_ptr, agent_info->protocol_version);
	kill_req->nodes	= xstrdup(node_ptr->name);

	agent_info->msg_type	= REQUEST_TERMINATE_JOB;
	agent_info->msg_args	= kill_req;

	set_agent_arg_r_uid(agent_info, SLURM_AUTH_UID_ANY);
	agent_queue_request(agent_info);
}

/*
 * Return true if this job is complete (including all elements of a hetjob)
 */
static bool _job_all_finished(job_record_t *job_ptr)
{
	job_record_t *het_job;
	list_itr_t *iter;
	bool finished = true;

	if (!IS_JOB_FINISHED(job_ptr))
		return false;

	if (!job_ptr->het_job_list)
		return true;

	iter = list_iterator_create(job_ptr->het_job_list);
	while ((het_job = list_next(iter))) {
		if (!IS_JOB_FINISHED(het_job)) {
			finished = false;
			break;
		}
	}
	list_iterator_destroy(iter);

	return finished;
}

/*
 * job_alloc_info_ptr - get details about an existing job allocation
 * IN uid - job issuing the code
 * IN job_ptr - pointer to job record
 * NOTE: See job_alloc_info() if job pointer not known
 */
extern int job_alloc_info_ptr(uint32_t uid, job_record_t *job_ptr)
{
	uint8_t prolog = 0;

	if ((slurm_conf.private_data & PRIVATE_DATA_JOBS) &&
	    (job_ptr->user_id != uid) && !validate_operator(uid) &&
	    (((slurm_mcs_get_privatedata() == 0) &&
	      !assoc_mgr_is_user_acct_coord(acct_db_conn, uid,
					    job_ptr->account, false)) ||
	     ((slurm_mcs_get_privatedata() == 1) &&
	      (mcs_g_check_mcs_label(uid, job_ptr->mcs_label, false) != 0))))
		return ESLURM_ACCESS_DENIED;
	if (IS_JOB_PENDING(job_ptr))
		return ESLURM_JOB_PENDING;
	if (_job_all_finished(job_ptr))
		return ESLURM_ALREADY_DONE;
	if (job_ptr->details)
		prolog = job_ptr->details->prolog_running;

	if (job_ptr->alias_list && !xstrcmp(job_ptr->alias_list, "TBD") &&
	    (prolog == 0) && job_ptr->node_bitmap &&
	    (bit_overlap_any(power_down_node_bitmap,
	                     job_ptr->node_bitmap) == 0)) {
		last_job_update = time(NULL);
		set_job_alias_list(job_ptr);
	}

	return SLURM_SUCCESS;
}

/*
 * job_alloc_info - get details about an existing job allocation
 * IN uid - job issuing the code
 * IN job_id - ID of job for which info is requested
 * OUT job_pptr - set to pointer to job record
 * NOTE: See job_alloc_info_ptr() if job pointer is known
 */
extern int job_alloc_info(uint32_t uid, uint32_t job_id,
			  job_record_t **job_pptr)
{
	job_record_t *job_ptr;

	job_ptr = find_job_record(job_id);
	if (job_ptr == NULL)
		return ESLURM_INVALID_JOB_ID;
	if (job_pptr)
		*job_pptr = job_ptr;
	return job_alloc_info_ptr(uid, job_ptr);
}

/*
 * Synchronize the batch job in the system with their files.
 * All pending batch jobs must have script and environment files
 * No other jobs should have such files
 */
int sync_job_files(void)
{
	list_t *batch_dirs = NULL;

	xassert(verify_lock(CONF_LOCK, READ_LOCK));
	xassert(verify_lock(JOB_LOCK, WRITE_LOCK));

	if (!slurmctld_primary)	/* Don't purge files from backup slurmctld */
		return SLURM_SUCCESS;

	batch_dirs = list_create(xfree_ptr);
	_get_batch_job_dir_ids(batch_dirs);
	_validate_job_files(batch_dirs);
	_remove_defunct_batch_dirs(batch_dirs);
	FREE_NULL_LIST(batch_dirs);
	return SLURM_SUCCESS;
}

/* Append to the batch_dirs list the job_id's associated with
 *	every batch job directory in existence
 */
static void _get_batch_job_dir_ids(list_t *batch_dirs)
{
	DIR *f_dir, *h_dir;
	struct dirent *dir_ent, *hash_ent;
	long long_job_id;
	uint32_t *job_id_ptr;
	char *endptr;

	xassert(verify_lock(CONF_LOCK, READ_LOCK));

	xassert(slurm_conf.state_save_location);
	f_dir = opendir(slurm_conf.state_save_location);
	if (!f_dir) {
		error("opendir(%s): %m", slurm_conf.state_save_location);
		return;
	}

	while ((dir_ent = readdir(f_dir))) {
		if (!xstrncmp("hash.#", dir_ent->d_name, 5)) {
			char *h_path = NULL;
			xstrfmtcat(h_path, "%s/%s",
			           slurm_conf.state_save_location,
			           dir_ent->d_name);
			h_dir = opendir(h_path);
			xfree(h_path);
			if (!h_dir)
				continue;
			while ((hash_ent = readdir(h_dir))) {
				if (xstrncmp("job.#", hash_ent->d_name, 4))
					continue;
				long_job_id = strtol(&hash_ent->d_name[4],
						     &endptr, 10);
				if ((long_job_id == 0) || (endptr[0] != '\0'))
					continue;
				debug3("Found batch directory for JobId=%ld",
				       long_job_id);
				job_id_ptr = xmalloc(sizeof(uint32_t));
				*job_id_ptr = long_job_id;
				list_append(batch_dirs, job_id_ptr);
			}
			closedir(h_dir);
		}
	}

	closedir(f_dir);
}

static int _clear_state_dir_flag(void *x, void *arg)
{
	job_record_t *job_ptr = (job_record_t *) x;
	job_ptr->bit_flags &= ~HAS_STATE_DIR;
	return 0;
}

static int _test_state_dir_flag(void *x, void *arg)
{
	job_record_t *job_ptr = (job_record_t *) x;

	if (job_ptr->bit_flags & HAS_STATE_DIR) {
		job_ptr->bit_flags &= ~HAS_STATE_DIR;
		return 0;
	}

	if (!job_ptr->batch_flag || !IS_JOB_PENDING(job_ptr) ||
	    (job_ptr->het_job_offset > 0))
		return 0;	/* No files expected */

	error("Script for %pJ lost, state set to FAILED", job_ptr);
	job_state_set(job_ptr, JOB_FAILED);
	job_ptr->exit_code = 1;
	job_ptr->state_reason = FAIL_SYSTEM;
	xfree(job_ptr->state_desc);
	job_ptr->start_time = job_ptr->end_time = time(NULL);
	job_completion_logger(job_ptr, false);
	return 0;
}

/* All pending batch jobs must have a batch_dir entry,
 *	otherwise we flag it as FAILED and don't schedule
 * If the batch_dir entry exists for a PENDING or RUNNING batch job,
 *	remove it the list (of directories to be deleted) */
static void _validate_job_files(list_t *batch_dirs)
{
	job_record_t *job_ptr;
	list_itr_t *batch_dir_iter;
	uint32_t *job_id_ptr, array_job_id;

	list_for_each(job_list, _clear_state_dir_flag, NULL);

	batch_dir_iter = list_iterator_create(batch_dirs);
	while ((job_id_ptr = list_next(batch_dir_iter))) {
		job_ptr = find_job_record(*job_id_ptr);
		if (job_ptr) {
			job_ptr->bit_flags |= HAS_STATE_DIR;
			list_delete_item(batch_dir_iter);
		}
		if (job_ptr && job_ptr->array_recs) { /* Update all tasks */
			array_job_id = job_ptr->array_job_id;
			job_ptr = job_array_hash_j[JOB_HASH_INX(array_job_id)];
			while (job_ptr) {
				if (job_ptr->array_job_id == array_job_id)
					job_ptr->bit_flags |= HAS_STATE_DIR;
				job_ptr = job_ptr->job_array_next_j;
			}
		}
	}
	list_iterator_destroy(batch_dir_iter);

	list_for_each(job_list, _test_state_dir_flag, NULL);
}

/* Remove all batch_dir entries in the list */
static void _remove_defunct_batch_dirs(list_t *batch_dirs)
{
	list_itr_t *batch_dir_inx;
	uint32_t *job_id_ptr;

	xassert(verify_lock(CONF_LOCK, READ_LOCK));

	batch_dir_inx = list_iterator_create(batch_dirs);
	while ((job_id_ptr = list_next(batch_dir_inx))) {
		info("Purged files for defunct batch JobId=%u",
		     *job_id_ptr);
		delete_job_desc_files(*job_id_ptr);
	}
	list_iterator_destroy(batch_dir_inx);
}

/* Get requested gres but only if mem_per_gres was set for that gres */
static int _get_req_gres(void *x, void *arg)
{
	gres_state_t *gres_state_job = x;
	gres_job_state_t *gres_js_out = arg;
	gres_job_state_t *gres_js = gres_state_job->gres_data;

	/*
	 * This assumes that only one gres name has mem_per_gres in the job.
	 * This won't work if two different gres names (for example, "gpu" and
	 * "license") both have mem_per_gres. Right now we only allow
	 * mem_per_gres for GPU so this works.
	 */
	if (!gres_js->mem_per_gres)
		return SLURM_SUCCESS;

	/*
	 * In theory MAX(mem_per_gres) shouldn't matter because we should only
	 * allow one gres name to have mem_per_gres and it should be the same
	 * for all types (e.g., gpu:k80 vs gpu:tesla) of that same gres (gpu).
	 */
	gres_js_out->mem_per_gres = MAX(gres_js_out->mem_per_gres,
					gres_js->mem_per_gres);

	gres_js_out->gres_per_job += gres_js->gres_per_job;
	gres_js_out->gres_per_node += gres_js->gres_per_node;
	gres_js_out->gres_per_socket += gres_js->gres_per_socket;
	gres_js_out->gres_per_task += gres_js->gres_per_task;

	return SLURM_SUCCESS;
}

extern uint64_t job_get_tres_mem(struct job_resources *job_res,
				 uint64_t pn_min_memory, uint32_t cpu_cnt,
				 uint32_t node_cnt, part_record_t *part_ptr,
				 list_t *gres_list, bool user_set_mem,
				 uint16_t min_sockets_per_node,
				 uint32_t num_tasks)
{
	uint64_t mem_total = 0;
	int i;

	if (job_res) {
		for (i = 0; i < job_res->nhosts; i++) {
			mem_total += job_res->memory_allocated[i];
		}
		return mem_total;
	}

	if (pn_min_memory == NO_VAL64)
		return mem_total;

	if (!user_set_mem && gres_list &&
	    (slurm_select_cr_type() == SELECT_TYPE_CONS_TRES)) {
		/* mem_per_[cpu|node] not set, check if mem_per_gres was set */
		gres_job_state_t gres_js;
		memset(&gres_js, 0, sizeof(gres_js));
		list_for_each(gres_list, _get_req_gres, &gres_js);
		if (gres_js.mem_per_gres) {
			/* Requested node_cnt == 1 if not given */
			if (node_cnt == NO_VAL)
				node_cnt = 1;

			/* Estimate requested gres per job */
			if (gres_js.gres_per_job)
				return gres_js.mem_per_gres *
					gres_js.gres_per_job;
			if (gres_js.gres_per_node)
				return gres_js.mem_per_gres *
					gres_js.gres_per_node * node_cnt;
			if (gres_js.gres_per_socket) {
				if (min_sockets_per_node &&
				    (min_sockets_per_node != NO_VAL16))
					return gres_js.mem_per_gres *
						gres_js.gres_per_socket *
						node_cnt * min_sockets_per_node;
				else
					return gres_js.mem_per_gres *
						gres_js.gres_per_socket *
						node_cnt;
			}
			if (gres_js.gres_per_task) {
				if (num_tasks && (num_tasks != NO_VAL))
					return gres_js.mem_per_gres *
						gres_js.gres_per_task *
						num_tasks;
				else
					return gres_js.mem_per_gres *
						gres_js.gres_per_task;
			}
			/*
			 * mem_per_gres set but no gres requested.
			 * We shouldn't get here.
			 */
			return 0;
		}
	}

	if (pn_min_memory == 0)
		pn_min_memory = _mem_per_node_part(part_ptr);

	if (pn_min_memory & MEM_PER_CPU) {
		if (cpu_cnt != NO_VAL) {
			mem_total = pn_min_memory & (~MEM_PER_CPU);
			mem_total *= cpu_cnt;
		}
	} else if (node_cnt != NO_VAL)
		mem_total = pn_min_memory * node_cnt;

	return mem_total;
}

/*
 * job_epilog_complete - Note the completion of the epilog script for a
 *	given job
 * IN job_id      - id of the job for which the epilog was executed
 * IN node_name   - name of the node on which the epilog was executed
 * IN return_code - return code from epilog script
 * RET true if job is COMPLETED, otherwise false
 */
extern bool job_epilog_complete(uint32_t job_id, char *node_name,
				uint32_t return_code)
{
	job_record_t *job_ptr = find_job_record(job_id);
	node_record_t *node_ptr;

	if (job_ptr == NULL) {
		debug("%s: unable to find JobId=%u for node=%s with return_code=%u.",
		      __func__, job_id, node_name, return_code);
		return true;
	}

	log_flag(TRACE_JOBS, "%s: enter %pJ", __func__, job_ptr);

	/*
	 * There is a potential race condition this handles.
	 * If slurmctld cold-starts while slurmd keeps running, slurmd could
	 * notify slurmctld of a job epilog completion before getting synced
	 * up with slurmctld state. If a new job arrives and the job_id is
	 * reused, we could try to note the termination of a job that hasn't
	 * really started. Very rare obviously.
	 */
	if ((IS_JOB_PENDING(job_ptr) && (!IS_JOB_COMPLETING(job_ptr))) ||
	    (job_ptr->node_bitmap == NULL)) {
#ifndef HAVE_FRONT_END
		uint32_t base_state = NODE_STATE_UNKNOWN;
		node_ptr = find_node_record(node_name);
		if (node_ptr)
			base_state = node_ptr->node_state & NODE_STATE_BASE;
		if (base_state == NODE_STATE_DOWN) {
			debug("%s: %pJ complete response from DOWN node %s",
			      __func__, job_ptr, node_name);
		} else if (job_ptr->restart_cnt) {
			/*
			 * Duplicate epilog complete can be due to race
			 */
			debug("%s: %pJ duplicate epilog complete response",
			      __func__, job_ptr);
		} else {
			error("%s: %pJ is non-running slurmctld and slurmd out of sync",
			      __func__, job_ptr);
		}
#endif
		return false;
	}

#ifdef HAVE_FRONT_END
	xassert(job_ptr->batch_host);
	/*
	 * If there is a bad epilog error don't down the frontend node.
	 * If needed the nodes in use by the job will be downed below.
	 */
	if (return_code)
		error("%s: %pJ epilog error on %s",
		      __func__, job_ptr, job_ptr->batch_host);

	if (job_ptr->front_end_ptr && IS_JOB_COMPLETING(job_ptr)) {
		front_end_record_t *front_end_ptr = job_ptr->front_end_ptr;
		if (front_end_ptr->job_cnt_comp)
			front_end_ptr->job_cnt_comp--;
		else {
			error("%s: %pJ job_cnt_comp underflow on front end %s",
			      __func__, job_ptr, front_end_ptr->name);
		}
		if (front_end_ptr->job_cnt_comp == 0)
			front_end_ptr->node_state &= (~NODE_STATE_COMPLETING);
	}

	if ((job_ptr->total_nodes == 0) && IS_JOB_COMPLETING(job_ptr)) {
		/*
		 * Job resources moved into another job and
		 * tasks already killed
		 */
		front_end_record_t *front_end_ptr = job_ptr->front_end_ptr;
		if (front_end_ptr)
			front_end_ptr->node_state &= (~NODE_STATE_COMPLETING);
	} else {
		for (int i = 0;
		     (node_ptr = next_node_bitmap(job_ptr->node_bitmap, &i));
		     i++) {
			if (return_code) {
				drain_nodes(node_ptr->name, "Epilog error",
				            slurm_conf.slurm_user_id);
			}
			/* Change job from completing to completed */
			make_node_idle(node_ptr, job_ptr);
		}
	}
#else
	if (return_code) {
		error("%s: %pJ epilog error on %s, draining the node",
		      __func__, job_ptr, node_name);
		drain_nodes(node_name, "Epilog error",
		            slurm_conf.slurm_user_id);
	}
	/* Change job from completing to completed */
	node_ptr = find_node_record(node_name);
	if (node_ptr)
		make_node_idle(node_ptr, job_ptr);
#endif

	/* nodes_completing is out of date, rebuild when next saved */
	xfree(job_ptr->nodes_completing);
	if (!IS_JOB_COMPLETING(job_ptr)) {	/* COMPLETED */
		batch_requeue_fini(job_ptr);
		return true;
	} else
		return false;
}

/* Complete a batch job requeue logic after all steps complete so that
 * subsequent jobs appear in a separate accounting record. */
void batch_requeue_fini(job_record_t *job_ptr)
{
	if (IS_JOB_COMPLETING(job_ptr) ||
	    !IS_JOB_PENDING(job_ptr) || !job_ptr->batch_flag)
		return;

	info("Requeuing %pJ", job_ptr);

	/* Clear everything so this appears to be a new job and then restart
	 * it in accounting. */
	job_ptr->start_time = 0;
	job_ptr->end_time_exp = job_ptr->end_time = 0;
	job_ptr->total_cpus = 0;
	job_ptr->pre_sus_time = 0;
	job_ptr->preempt_time = 0;
	job_ptr->suspend_time = 0;
	job_ptr->tot_sus_time = 0;
	job_ptr->next_step_id = 0;
	job_ptr->state_reason_prev_db = 0;

	job_ptr->node_cnt = 0;
	job_ptr->total_nodes = 0;
	xfree(job_ptr->alias_list);
	xfree(job_ptr->batch_host);
	free_job_resources(&job_ptr->job_resrcs);
	xfree(job_ptr->nodes);
	xfree(job_ptr->node_addrs);
	xfree(job_ptr->nodes_completing);
	xfree(job_ptr->failed_node);
	FREE_NULL_BITMAP(job_ptr->node_bitmap);
	FREE_NULL_BITMAP(job_ptr->node_bitmap_cg);
	FREE_NULL_LIST(job_ptr->gres_list_alloc);

	job_resv_clear_magnetic_flag(job_ptr);

	if (job_ptr->details) {
		time_t now = time(NULL);
		/* The time stamp on the new batch launch credential must be
		 * larger than the time stamp on the revoke request. Also the
		 * I/O must be all cleared out, the named socket purged and
		 * the job credential purged by slurmd. */
		if (job_ptr->details->begin_time <= now) {
			int cred_lifetime = DEFAULT_EXPIRATION_WINDOW;
			time_t begin_time;
			cred_lifetime = cred_expiration();
			begin_time = now + cred_lifetime + 1;
			if ((job_ptr->bit_flags & CRON_JOB) &&
			    job_ptr->details->crontab_entry) {
				begin_time = calc_next_cron_start(
					job_ptr->details->crontab_entry,
					begin_time);
			} else if (job_ptr->bit_flags & CRON_JOB) {
				/*
				 * Skip requeuing this instead of crashing.
				 */
				error("Missing cron details for %pJ. This should never happen. Clearing CRON_JOB flag and skipping requeue.",
				      job_ptr);
				job_ptr->bit_flags &= ~CRON_JOB;
			}
			job_ptr->details->begin_time = begin_time;
		}

		/* Since this could happen on a launch we need to make sure the
		 * submit isn't the same as the last submit so put now + 1 so
		 * we get different records in the database */
		if (now == job_ptr->details->submit_time)
			now++;
		job_ptr->details->submit_time = now;

		/* clear the accrue flag */
		job_ptr->bit_flags &= ~JOB_ACCRUE_OVER;
		job_ptr->details->accrue_time = 0;

		if ((job_ptr->details->whole_node & WHOLE_NODE_REQUIRED) &&
		    job_ptr->gres_list_req) {
			job_details_t *detail_ptr = job_ptr->details;
			multi_core_data_t *mc_ptr = detail_ptr->mc_ptr;
			gres_job_state_validate_t gres_js_val = {
				.cpus_per_tres = job_ptr->cpus_per_tres,
				.mem_per_tres = job_ptr->mem_per_tres,
				.tres_freq = job_ptr->tres_freq,
				.tres_per_job = job_ptr->tres_per_job,
				.tres_per_node = job_ptr->tres_per_node,
				.tres_per_socket = job_ptr->tres_per_socket,
				.tres_per_task = job_ptr->tres_per_task,

				.cpus_per_task =
				&detail_ptr->orig_cpus_per_task,
				.max_nodes = &detail_ptr->max_nodes,
				.min_cpus = &detail_ptr->min_cpus,
				.min_nodes = &detail_ptr->min_nodes,
				.ntasks_per_node = &detail_ptr->ntasks_per_node,
				.ntasks_per_socket = &mc_ptr->ntasks_per_socket,
				.ntasks_per_tres = &detail_ptr->ntasks_per_tres,
				.num_tasks = &detail_ptr->num_tasks,
				.sockets_per_node = &mc_ptr->sockets_per_node,

				.gres_list = &job_ptr->gres_list_req,
			};

			/*
			 * We need to reset the gres_list to what was requested
			 * instead of what was given exclusively.
			 */
			FREE_NULL_LIST(job_ptr->gres_list_req);
			(void)gres_job_state_validate(&gres_js_val);
		}
	}

	/* Reset the priority (begin and accrue times were reset) */
	if (job_ptr->priority != 0)
		set_job_prio(job_ptr);

	/*
	 * If a reservation ended and was a repeated (e.g., daily, weekly)
	 * reservation, its ID will be different; make sure
	 * job->resv_id matches the reservation id.
	 */
	if (job_ptr->resv_ptr)
		job_ptr->resv_id = job_ptr->resv_ptr->resv_id;

	/* Reset this after the batch step has finished or the batch step
	 * information will be attributed to the next run of the job. */
	job_record_set_sluid(job_ptr);
	jobacct_storage_g_job_start(acct_db_conn, job_ptr);

	/* Submit new sibling jobs for fed jobs */
	if (fed_mgr_is_origin_job(job_ptr)) {
		if (fed_mgr_job_requeue(job_ptr)) {
			error("failed to submit requeued sibling jobs for fed %pJ",
			      job_ptr);
		}
	}
}


/* job_fini - free all memory associated with job records */
void job_fini (void)
{
	FREE_NULL_LIST(job_list);
	xfree(job_hash);
	xfree(job_array_hash_j);
	xfree(job_array_hash_t);
	FREE_NULL_LIST(purge_jobs_list);
	FREE_NULL_LIST(purge_files_list);
	FREE_NULL_BITMAP(requeue_exit);
	FREE_NULL_BITMAP(requeue_exit_hold);
}

/* Record the start of one job array task */
extern void job_array_start(job_record_t *job_ptr)
{
	job_record_t *base_job_ptr;

	if ((job_ptr->array_task_id != NO_VAL) || job_ptr->array_recs) {
		base_job_ptr = find_job_record(job_ptr->array_job_id);
		if (base_job_ptr && base_job_ptr->array_recs) {
			base_job_ptr->array_recs->tot_run_tasks++;
		}
	}
}

/* Return true if a job array task can be started */
extern bool job_array_start_test(job_record_t *job_ptr)
{
	job_record_t *base_job_ptr;
	time_t now = time(NULL);

	if ((job_ptr->array_task_id != NO_VAL) || job_ptr->array_recs) {
		base_job_ptr = find_job_record(job_ptr->array_job_id);
		if (base_job_ptr && base_job_ptr->array_recs &&
		    (base_job_ptr->array_recs->max_run_tasks != 0) &&
		    (base_job_ptr->array_recs->tot_run_tasks >=
		     base_job_ptr->array_recs->max_run_tasks)) {
			if (job_ptr->details &&
			    (job_ptr->details->begin_time <= now))
				job_ptr->details->begin_time = (time_t) 0;
			xfree(job_ptr->state_desc);
			job_ptr->state_reason = WAIT_ARRAY_TASK_LIMIT;
			return false;
		}
	}

	return true;
}

static void _job_array_comp(job_record_t *job_ptr, bool was_running,
			    bool requeue)
{
	job_record_t *base_job_ptr;
	uint32_t status;

	if ((job_ptr->array_task_id != NO_VAL) || job_ptr->array_recs) {
		status = job_ptr->exit_code;
		if ((status == 0) && !IS_JOB_COMPLETE(job_ptr)) {
			/* Avoid max_exit_code == 0 if task did not run to
			 * successful completion (e.g. Cancelled, NodeFail) */
			status = 9;
		}
		base_job_ptr = find_job_record(job_ptr->array_job_id);
		if (base_job_ptr && base_job_ptr->array_recs) {
			if (requeue) {
				base_job_ptr->array_recs->array_flags |=
					ARRAY_TASK_REQUEUED;
			} else if (!base_job_ptr->array_recs->tot_comp_tasks) {
				base_job_ptr->array_recs->min_exit_code =
					status;
				base_job_ptr->array_recs->max_exit_code =
					status;
			} else {
				base_job_ptr->array_recs->min_exit_code =
					MIN(status, base_job_ptr->
					    array_recs->min_exit_code);
				base_job_ptr->array_recs->max_exit_code =
					MAX(status, base_job_ptr->
					    array_recs->max_exit_code);
			}
			if (was_running &&
			    base_job_ptr->array_recs->tot_run_tasks)
				base_job_ptr->array_recs->tot_run_tasks--;
			base_job_ptr->array_recs->tot_comp_tasks++;
		}
	}
}

/* log the completion of the specified job */
extern void job_completion_logger(job_record_t *job_ptr, bool requeue)
{
	int base_state;
	bool arr_finished = false, task_failed = false, task_requeued = false;
	bool was_running = false;
	job_record_t *master_job = NULL;
	uint32_t max_exit_code = 0;

	xassert(job_ptr);

	if (job_ptr->resv_ports)
		resv_port_job_free(job_ptr);

	acct_policy_remove_job_submit(job_ptr, false);
	if (job_ptr->nodes && ((job_ptr->bit_flags & JOB_KILL_HURRY) == 0)
	    && !IS_JOB_RESIZING(job_ptr)) {
		(void) bb_g_job_start_stage_out(job_ptr);
	} else if (job_ptr->nodes && IS_JOB_RESIZING(job_ptr)){
		debug("%s: %pJ resizing, skipping bb stage_out",
		      __func__, job_ptr);
	} else {
		/*
		 * Never allocated compute nodes.
		 * Unless job ran, there is no data to stage-out
		 */
		(void) bb_g_job_cancel(job_ptr);
	}
	if (job_ptr->bit_flags & JOB_WAS_RUNNING) {
		job_ptr->bit_flags &= ~JOB_WAS_RUNNING;
		was_running = true;
	}

	_job_array_comp(job_ptr, was_running, requeue);

	if (!IS_JOB_RESIZING(job_ptr) &&
	    (!IS_JOB_PENDING(job_ptr) || requeue) &&
	    !IS_JOB_REVOKED(job_ptr)  &&
	    ((job_ptr->array_task_id == NO_VAL) ||
	     (job_ptr->mail_type & MAIL_ARRAY_TASKS) ||
	     (arr_finished = test_job_array_finished(job_ptr->array_job_id)))) {
		/* Remove configuring state just to make sure it isn't there
		 * since it will throw off displays of the job. */
		job_state_unset_flag(job_ptr, JOB_CONFIGURING);

		/* make sure all parts of the job are notified
		 * Fed Jobs: only signal the srun from where the job is running
		 * or from the origin if the job wasn't running. */
		if (!job_ptr->fed_details ||
		    fed_mgr_job_is_self_owned(job_ptr) ||
		    (fed_mgr_is_origin_job(job_ptr) &&
		     !fed_mgr_job_is_locked(job_ptr)))
			srun_job_complete(job_ptr);

		/* mail out notifications of completion */
		if (arr_finished) {
			/* We need to summarize different tasks states. */
			master_job = find_job_record(job_ptr->array_job_id);
			if (master_job && master_job->array_recs) {
				task_requeued =
					(master_job->array_recs->array_flags &
					 ARRAY_TASK_REQUEUED);
				if (task_requeued &&
				    (job_ptr->mail_type & MAIL_JOB_REQUEUE)) {
					/*
					 * At least 1 task requeued and job
					 * req. to be notified on requeues.
					 */
					mail_job_info(master_job,
						      MAIL_JOB_REQUEUE);
				}

				max_exit_code =
					master_job->array_recs->max_exit_code;
				task_failed = (WIFEXITED(max_exit_code) &&
					       WEXITSTATUS(max_exit_code));
				if (task_failed &&
				    (job_ptr->mail_type & MAIL_JOB_FAIL)) {
					/*
					 * At least 1 task failed and job
					 * req. to be notified on failures.
					 */
					mail_job_info(master_job,
						      MAIL_JOB_FAIL);
				} else if (job_ptr->mail_type & MAIL_JOB_END) {
					/*
					 * Job req. to be notified on END.
					 */
					mail_job_info(job_ptr, MAIL_JOB_END);
				}
			}
		} else {
			base_state = job_ptr->job_state & JOB_STATE_BASE;
			if ((job_ptr->mail_type & MAIL_JOB_FAIL) &&
			    (base_state >= JOB_FAILED) &&
			    ((base_state != JOB_PREEMPTED) || !requeue))
				mail_job_info(job_ptr, MAIL_JOB_FAIL);
			else if ((job_ptr->mail_type & MAIL_JOB_END) &&
				 (base_state >= JOB_COMPLETE))
				mail_job_info(job_ptr, MAIL_JOB_END);

			if (requeue &&
			    (job_ptr->mail_type & MAIL_JOB_REQUEUE))
				mail_job_info(job_ptr,
					      MAIL_JOB_REQUEUE);

		}
	}

	if (!(job_ptr->bit_flags & TRES_STR_CALC) &&
	    job_ptr->tres_alloc_cnt &&
	    (job_ptr->tres_alloc_cnt[TRES_ENERGY] != NO_VAL64))
		assoc_mgr_set_job_tres_alloc_str(job_ptr, false);

	jobcomp_g_write(job_ptr);

	jobacct_storage_g_job_complete(acct_db_conn, job_ptr);
}

/*
 * job_independent - determine if this job has a dependent job pending
 *	or if the job's scheduled begin time is in the future
 * IN job_ptr - pointer to job being tested
 * RET - true if job no longer must be deferred for another job
 */
extern bool job_independent(job_record_t *job_ptr)
{
	job_details_t *detail_ptr = job_ptr->details;
	time_t now = time(NULL);
	int depend_rc;

	if ((job_ptr->state_reason == FAIL_BURST_BUFFER_OP) ||
	    (job_ptr->state_reason == FAIL_ACCOUNT) ||
	    (job_ptr->state_reason == FAIL_QOS) ||
	    (job_ptr->state_reason == WAIT_HELD) ||
	    (job_ptr->state_reason == WAIT_HELD_USER) ||
	    (job_ptr->state_reason == WAIT_MAX_REQUEUE) ||
	    (job_ptr->state_reason == WAIT_RESV_DELETED) ||
	    (job_ptr->state_reason == WAIT_RESV_INVALID) ||
	    (job_ptr->state_reason == WAIT_DEP_INVALID))
		return false;

	/* Test dependencies first so we can cancel jobs before dependent
	 * job records get purged (e.g. afterok, afternotok) */
	depend_rc = test_job_dependency(job_ptr, NULL);
	if ((depend_rc == LOCAL_DEPEND) || (depend_rc == REMOTE_DEPEND)) {
		/* start_time has passed but still has dependency which
		 * makes it ineligible */
		if (detail_ptr->begin_time < now)
			detail_ptr->begin_time = 0;
		job_ptr->state_reason = WAIT_DEPENDENCY;
		xfree(job_ptr->state_desc);
		return false;
	} else if (depend_rc == FAIL_DEPEND) {
		handle_invalid_dependency(job_ptr);
		return false;
	}
	/* Job is eligible to start now */
	if (job_ptr->state_reason == WAIT_DEPENDENCY) {
		job_ptr->state_reason = WAIT_NO_REASON;
		xfree(job_ptr->state_desc);
		/* Submit the job to its siblings. */
		if (job_ptr->details) {
			fed_mgr_job_requeue(job_ptr);
		}
	}

	/* Check for maximum number of running tasks in a job array */
	if (!job_array_start_test(job_ptr))
		return false;

	if (detail_ptr && (detail_ptr->begin_time > now)) {
		job_ptr->state_reason = WAIT_TIME;
		xfree(job_ptr->state_desc);
		return false;	/* not yet time */
	}

	if (job_test_resv_now(job_ptr) != SLURM_SUCCESS) {
		job_ptr->state_reason = WAIT_RESERVATION;
		xfree(job_ptr->state_desc);
		return false;	/* not yet time */
	}

	if ((detail_ptr && (detail_ptr->begin_time == 0) &&
	     (job_ptr->priority != 0))) {
		detail_ptr->begin_time = now;
		/*
		 * Send begin time to the database if it is already there, or it
		 * won't get there until the job starts.
		 */
		if (IS_JOB_IN_DB(job_ptr))
			jobacct_storage_g_job_start(acct_db_conn, job_ptr);
	} else if (job_ptr->state_reason == WAIT_TIME) {
		job_ptr->state_reason = WAIT_NO_REASON;
		xfree(job_ptr->state_desc);
	}
	return true;
}

/*
 * determine if job is ready to execute per the node select plugin
 * IN job_id - job to test
 * OUT ready - 1 if job is ready to execute 0 otherwise
 * RET Slurm error code
 */
extern int job_node_ready(uint32_t job_id, int *ready)
{
	int rc;
	job_record_t *job_ptr;
	xassert(ready);

	*ready = 0;
	job_ptr = find_job_record(job_id);
	if (job_ptr == NULL)
		return ESLURM_INVALID_JOB_ID;

	/*
	 * If the job is configuring, the node might be booting, or a script
	 * such as PrologSlurmctld is running; delay job launch until these
	 * are finished.
	 */
	if (IS_JOB_CONFIGURING(job_ptr))
		return EAGAIN;

	/* Always call select_g_job_ready() so that select/bluegene can
	 * test and update block state information. */
	rc = select_g_job_ready(job_ptr);
	if (rc == READY_JOB_FATAL)
		return ESLURM_INVALID_PARTITION_NAME;
	if (rc == READY_JOB_ERROR)
		return EAGAIN;
	if (rc)
		rc = READY_NODE_STATE;

	if (job_ptr->details && !job_ptr->details->prolog_running)
		rc |= READY_PROLOG_STATE;
	if (IS_JOB_RUNNING(job_ptr) || IS_JOB_SUSPENDED(job_ptr))
		rc |= READY_JOB_STATE;
	if ((rc == (READY_NODE_STATE | READY_JOB_STATE | READY_PROLOG_STATE)) &&
	    job_ptr->alias_list && !xstrcmp(job_ptr->alias_list, "TBD") &&
	    job_ptr->node_bitmap &&
	    (bit_overlap_any(power_down_node_bitmap,
	                     job_ptr->node_bitmap) == 0)) {
		last_job_update = time(NULL);
		set_job_alias_list(job_ptr);
	}

	*ready = rc;
	return SLURM_SUCCESS;
}

/* Send specified signal to all steps associated with a job */
static void _signal_job(job_record_t *job_ptr, int signal, uint16_t flags)
{
#ifndef HAVE_FRONT_END
	node_record_t *node_ptr;
#endif
	agent_arg_t *agent_args = NULL;
	signal_tasks_msg_t *signal_job_msg = NULL;

	agent_args = xmalloc(sizeof(agent_arg_t));
	agent_args->msg_type = REQUEST_SIGNAL_TASKS;
	agent_args->retry = 1;
	agent_args->hostlist = hostlist_create(NULL);
	signal_job_msg = xmalloc(sizeof(signal_tasks_msg_t));
	signal_job_msg->step_id.job_id = job_ptr->job_id;

	/*
	 * We don't ever want to kill a step with this message.  The flags below
	 * will make sure that does happen.  Just in case though, set the
	 * step_id to an impossible number.
	 */
	signal_job_msg->step_id.step_id = slurm_conf.max_step_cnt + 1;
	signal_job_msg->step_id.step_het_comp = NO_VAL;

	/*
	 * Encode the flags for slurm stepd to know what steps get signaled
	 * Here if we aren't signaling the full job we always only want to
	 * signal all other steps.
	 */
	if ((flags & KILL_FULL_JOB) ||
	    (flags & KILL_JOB_BATCH) ||
	    (flags & KILL_STEPS_ONLY))
		signal_job_msg->flags = flags;
	else
		signal_job_msg->flags = KILL_STEPS_ONLY;

	signal_job_msg->signal = signal;

#ifdef HAVE_FRONT_END
	xassert(job_ptr->batch_host);
	if (job_ptr->front_end_ptr)
		agent_args->protocol_version =
			job_ptr->front_end_ptr->protocol_version;
	hostlist_push_host(agent_args->hostlist, job_ptr->batch_host);
	agent_args->node_count = 1;
#else
	agent_args->protocol_version = SLURM_PROTOCOL_VERSION;
	for (int i = 0; (node_ptr = next_node_bitmap(job_ptr->node_bitmap, &i));
	     i++) {
		if (agent_args->protocol_version > node_ptr->protocol_version)
			agent_args->protocol_version =
				node_ptr->protocol_version;
		hostlist_push_host(agent_args->hostlist, node_ptr->name);
		agent_args->node_count++;
		if (PACK_FANOUT_ADDRS(node_ptr))
			agent_args->msg_flags |= SLURM_PACK_ADDRS;
	}
#endif

	if (agent_args->node_count == 0) {
		xfree(signal_job_msg);
		xfree(agent_args);
		return;
	}

	agent_args->msg_args = signal_job_msg;
	set_agent_arg_r_uid(agent_args, SLURM_AUTH_UID_ANY);
	agent_queue_request(agent_args);
}

/* Send suspend request to slumrd of all nodes associated with a job
 * job_ptr IN - job to be suspended or resumed
 * op IN - SUSPEND_JOB or RESUME_JOB
 * indf_susp IN - set if job is being suspended indefinitely by user
 *                or admin, otherwise suspended for gang scheduling
 */
static void _suspend_job(job_record_t *job_ptr, uint16_t op)
{
#ifndef HAVE_FRONT_END
	node_record_t *node_ptr;
#endif
	agent_arg_t *agent_args;
	suspend_int_msg_t *sus_ptr;

	agent_args = xmalloc(sizeof(agent_arg_t));
	agent_args->msg_type = REQUEST_SUSPEND_INT;
	agent_args->retry = 0;	/* don't resend, gang scheduler can
				 * quickly induce huge backlog
				 * of agent.c RPCs */
	agent_args->hostlist = hostlist_create(NULL);
	sus_ptr = xmalloc(sizeof(suspend_int_msg_t));
	sus_ptr->job_id = job_ptr->job_id;
	sus_ptr->op = op;

#ifdef HAVE_FRONT_END
	xassert(job_ptr->batch_host);
	if (job_ptr->front_end_ptr) {
		agent_args->protocol_version =
			job_ptr->front_end_ptr->protocol_version;
	}
	hostlist_push_host(agent_args->hostlist, job_ptr->batch_host);
	agent_args->node_count = 1;
#else
	agent_args->protocol_version = SLURM_PROTOCOL_VERSION;
	for (int i = 0; (node_ptr = next_node_bitmap(job_ptr->node_bitmap, &i));
	     i++) {
		if (agent_args->protocol_version > node_ptr->protocol_version)
			agent_args->protocol_version =
				node_ptr->protocol_version;
		hostlist_push_host(agent_args->hostlist, node_ptr->name);
		agent_args->node_count++;
		if (PACK_FANOUT_ADDRS(node_ptr))
			agent_args->msg_flags |= SLURM_PACK_ADDRS;
	}
#endif

	if (agent_args->node_count == 0) {
		slurm_free_suspend_int_msg(sus_ptr);
		xfree(agent_args);
		return;
	}

	agent_args->msg_args = sus_ptr;
	set_agent_arg_r_uid(agent_args, SLURM_AUTH_UID_ANY);
	agent_queue_request(agent_args);
}

/*
 * Specified job is being suspended, release allocated nodes
 * job_ptr IN - job to be suspended
 * indf_susp IN - set if job is being suspended indefinitely by user
 *                or admin, otherwise suspended for gang scheduling
 */
static int _suspend_job_nodes(job_record_t *job_ptr, bool indf_susp)
{
	int rc = SLURM_SUCCESS;
	node_record_t *node_ptr;
	uint32_t node_flags;
	time_t now = time(NULL);

	if ((rc = select_g_job_suspend(job_ptr, indf_susp)) != SLURM_SUCCESS)
		return rc;

	for (int i = 0; (node_ptr = next_node_bitmap(job_ptr->node_bitmap, &i));
	     i++) {
		node_ptr->sus_job_cnt++;
		if (node_ptr->run_job_cnt)
			(node_ptr->run_job_cnt)--;
		else {
			error("%s: %pJ node %s run_job_cnt underflow",
			      __func__, job_ptr, node_ptr->name);
		}
		if (job_ptr->details && (job_ptr->details->share_res == 0)) {
			if (node_ptr->no_share_job_cnt)
				(node_ptr->no_share_job_cnt)--;
			else {
				error("%s: %pJ node %s no_share_job_cnt underflow",
				      __func__, job_ptr, node_ptr->name);
			}
			if (node_ptr->no_share_job_cnt == 0)
				bit_set(share_node_bitmap, i);
		}
		node_flags = node_ptr->node_state & NODE_STATE_FLAGS;
		if ((node_ptr->run_job_cnt  == 0) &&
		    (node_ptr->comp_job_cnt == 0)) {
			bit_set(idle_node_bitmap, i);
		}
		if (IS_NODE_DOWN(node_ptr)) {
			debug3("%s: %pJ node %s left DOWN",
			       __func__, job_ptr, node_ptr->name);
		} else if (node_ptr->run_job_cnt) {
			node_ptr->node_state =
				NODE_STATE_ALLOCATED | node_flags;
		} else {
			node_ptr->node_state = NODE_STATE_IDLE | node_flags;
			node_ptr->last_busy  = now;
		}
	}
	last_job_update = last_node_update = now;
	return rc;
}

/*
 * Specified job is being resumed, re-allocate the nodes
 * job_ptr IN - job to be resumed
 * indf_susp IN - set i f job is being resumed from indefinite suspend by user
 *                or admin, otherwise resume from gang scheduling
 */
static int _resume_job_nodes(job_record_t *job_ptr, bool indf_susp)
{
	int rc = SLURM_SUCCESS;
	node_record_t *node_ptr;
	uint32_t node_flags;

	if ((rc = select_g_job_resume(job_ptr, indf_susp)) != SLURM_SUCCESS)
		return rc;

	for (int i = 0; (node_ptr = next_node_bitmap(job_ptr->node_bitmap, &i));
	     i++) {
		if (IS_NODE_DOWN(node_ptr))
			return SLURM_ERROR;
	}

	for (int i = 0; (node_ptr = next_node_bitmap(job_ptr->node_bitmap, &i));
	     i++) {
		if (node_ptr->sus_job_cnt)
			(node_ptr->sus_job_cnt)--;
		else {
			error("Node %s sus_job_cnt underflow",
			      node_ptr->name);
		}
		node_ptr->run_job_cnt++;
		if (job_ptr->details &&
		    (job_ptr->details->share_res == 0)) {
			node_ptr->no_share_job_cnt++;
			if (node_ptr->no_share_job_cnt)
				bit_clear(share_node_bitmap, i);
		}

		if (slurm_mcs_get_select(job_ptr) == 1) {
			xfree(node_ptr->mcs_label);
			node_ptr->mcs_label = xstrdup(job_ptr->mcs_label);
		}

		bit_clear(idle_node_bitmap, i);
		node_flags = node_ptr->node_state & NODE_STATE_FLAGS;
		node_ptr->node_state = NODE_STATE_ALLOCATED | node_flags;
	}
	last_job_update = last_node_update = time(NULL);
	return rc;
}

/*
 * Determine if a job can be resumed.
 * Check for multiple jobs on the same nodes with core specialization.
 * RET 0 on success, otherwise ESLURM error code
 */
static int _job_resume_test(job_record_t *job_ptr)
{
	int rc = SLURM_SUCCESS;
	list_itr_t *job_iterator;
	job_record_t *test_job_ptr;

	if ((job_ptr->details == NULL) ||
	    (job_ptr->details->core_spec == NO_VAL16) ||
	    (job_ptr->node_bitmap == NULL))
		return rc;

	job_iterator = list_iterator_create(job_list);
	while ((test_job_ptr = list_next(job_iterator))) {
		if (test_job_ptr->details &&
		    (test_job_ptr->details->core_spec != NO_VAL16) &&
		    IS_JOB_RUNNING(test_job_ptr) &&
		    test_job_ptr->node_bitmap &&
		    bit_overlap_any(test_job_ptr->node_bitmap,
				    job_ptr->node_bitmap)) {
			rc = ESLURM_NODES_BUSY;
			break;
		}
/* FIXME: Also test for ESLURM_INTERCONNECT_BUSY */
	}
	list_iterator_destroy(job_iterator);

	return rc;
}

/*
 * _job_suspend_op - perform some suspend/resume operation on a job
 * op IN - operation: suspend/resume
 * indf_susp IN - set if job is being suspended indefinitely by user or admin
 *                and we should clear it's priority, otherwise suspended
 *		  temporarily for gang scheduling
 * RET 0 on success, otherwise ESLURM error code
 */
static int _job_suspend_op(job_record_t *job_ptr, uint16_t op, bool indf_susp)
{
	int rc = SLURM_SUCCESS;
	time_t now = time(NULL);

	if (IS_JOB_PENDING(job_ptr))
		return ESLURM_JOB_PENDING;
	if (IS_JOB_FINISHED(job_ptr))
		return ESLURM_ALREADY_DONE;
	if ((op == RESUME_JOB) && (rc = _job_resume_test(job_ptr)))
		return rc;

	/* perform the operation */
	if (op == SUSPEND_JOB) {
		if (IS_JOB_SUSPENDED(job_ptr) && indf_susp) {
			debug("%s: Holding %pJ, re-suspend operation",
			      __func__, job_ptr);
			job_ptr->priority = 0;	/* Prevent gang sched resume */
			return SLURM_SUCCESS;
		}
		if (!IS_JOB_RUNNING(job_ptr))
			return ESLURM_JOB_NOT_RUNNING;
		rc = _suspend_job_nodes(job_ptr, indf_susp);
		if (rc != SLURM_SUCCESS)
			return rc;
		_suspend_job(job_ptr, op);
		job_state_set(job_ptr, JOB_SUSPENDED);
		if (indf_susp) {    /* Job being manually suspended, not gang */
			debug("%s: Holding %pJ, suspend operation",
			      __func__, job_ptr);
			job_ptr->priority = 0;
			(void) gs_job_fini(job_ptr);
		}
		if (job_ptr->suspend_time) {
			job_ptr->pre_sus_time +=
				difftime(now, job_ptr->suspend_time);
		} else {
			job_ptr->pre_sus_time +=
				difftime(now, job_ptr->start_time);
		}
		suspend_job_step(job_ptr);
	} else if (op == RESUME_JOB) {
		if (!IS_JOB_SUSPENDED(job_ptr))
			return ESLURM_JOB_NOT_SUSPENDED;
		rc = _resume_job_nodes(job_ptr, indf_susp);
		if (rc != SLURM_SUCCESS)
			return rc;
		_suspend_job(job_ptr, op);
		if (job_ptr->priority == 0) {
			/* Job was manually suspended, not gang */
			set_job_prio(job_ptr);
			(void) gs_job_start(job_ptr);
		}
		job_state_set(job_ptr, JOB_RUNNING);
		job_ptr->tot_sus_time +=
			difftime(now, job_ptr->suspend_time);

		if ((job_ptr->time_limit != INFINITE) &&
		    (!job_ptr->preempt_time)) {
			debug3("%pJ resumed, updating end_time", job_ptr);
			job_ptr->end_time_exp = job_ptr->end_time =
				now + (job_ptr->time_limit * 60)
				- job_ptr->pre_sus_time;
		}
		resume_job_step(job_ptr);
	}

	job_ptr->time_last_active = now;
	job_ptr->suspend_time = now;
	jobacct_storage_g_job_suspend(acct_db_conn, job_ptr);

	return rc;
}


/*
 * _job_suspend - perform some suspend/resume operation, if the specified
 *                job records is a hetjob leader, perform the operation on all
 *                components of the hetjob
 * job_ptr - job to operate upon
 * op IN - operation: suspend/resume
 * indf_susp IN - set if job is being suspended indefinitely by user or admin
 *                and we should clear it's priority, otherwise suspended
 *		  temporarily for gang scheduling
 * RET 0 on success, otherwise ESLURM error code
 */
static int _job_suspend(job_record_t *job_ptr, uint16_t op, bool indf_susp)
{
	job_record_t *het_job;
	int rc = SLURM_SUCCESS, rc1;
	list_itr_t *iter;

	if (job_ptr->het_job_id && !job_ptr->het_job_list)
		return ESLURM_NOT_WHOLE_HET_JOB;

	/* Notify salloc/srun of suspend/resume */
	srun_job_suspend(job_ptr, op);

	if (job_ptr->het_job_list) {
		iter = list_iterator_create(job_ptr->het_job_list);
		while ((het_job = list_next(iter))) {
			if (job_ptr->het_job_id != het_job->het_job_id) {
				error("%s: Bad het_job_list for %pJ",
				      __func__, job_ptr);
				continue;
			}
			rc1 = _job_suspend_op(het_job, op, indf_susp);
			if (rc1 != SLURM_SUCCESS)
				rc = rc1;
		}
		list_iterator_destroy(iter);
	} else {
		rc = _job_suspend_op(job_ptr, op, indf_susp);
	}

	return rc;
}

/*
 * job_suspend - perform some suspend/resume operation
 * NOTE: job_suspend  - Uses the job_id field and ignores job_id_str
 *
 * IN msg - original msg
 * IN sus_ptr - suspend/resume request message
 * IN uid - user id of the user issuing the RPC
 * indf_susp IN - set if job is being suspended indefinitely by user or admin
 *                and we should clear it's priority, otherwise suspended
 *		  temporarily for gang scheduling
 * IN protocol_version - slurm protocol version of client
 * RET 0 on success, otherwise ESLURM error code
 */
extern int job_suspend(slurm_msg_t *msg, suspend_msg_t *sus_ptr, uid_t uid,
		       bool indf_susp, uint16_t protocol_version)
{
	int rc = SLURM_SUCCESS;
	job_record_t *job_ptr = NULL;

	xfree(sus_ptr->job_id_str);
	xstrfmtcat(sus_ptr->job_id_str, "%u", sus_ptr->job_id);

	/* find the job */
	job_ptr = find_job_record (sus_ptr->job_id);
	if (job_ptr == NULL) {
		rc = ESLURM_INVALID_JOB_ID;
		goto reply;
	}

	/* validate the request */
	if (!validate_operator(uid) &&
	    !assoc_mgr_is_user_acct_coord(acct_db_conn, uid,
					  job_ptr->account, false)) {
		error("SECURITY VIOLATION: Attempt to suspend job from user %u",
		       uid);
		rc = ESLURM_ACCESS_DENIED;
		goto reply;
	}

	rc = _job_suspend(job_ptr, sus_ptr->op, indf_susp);

reply:

	/* Since we have already used it lets make sure we don't leak
	   memory */
	xfree(sus_ptr->job_id_str);

	if (msg)
		slurm_send_rc_msg(msg, rc);

	return rc;
}

/*
 * job_suspend2 - perform some suspend/resume operation
 * NB job_suspend2 - Ignores the job_id field and uses job_id_str
 *
 * IN msg - original msg
 * IN sus_ptr - suspend/resume request message
 * IN uid - user id of the user issuing the RPC
 * indf_susp IN - set if job is being suspended indefinitely by user or admin
 *                and we should clear it's priority, otherwise suspended
 *		  temporarily for gang scheduling
 * IN protocol_version - slurm protocol version of client
 * RET 0 on success, otherwise ESLURM error code
 */
extern int job_suspend2(slurm_msg_t *msg, suspend_msg_t *sus_ptr, uid_t uid,
			bool indf_susp, uint16_t protocol_version)
{
	int rc = SLURM_SUCCESS, rc2;
	job_record_t *job_ptr = NULL;
	long int long_id;
	uint32_t job_id = 0;
	char *end_ptr = NULL;
	bitstr_t *array_bitmap = NULL;
	resp_array_struct_t *resp_array = NULL;

	if (max_array_size == NO_VAL) {
		max_array_size = slurm_conf.max_array_sz;
	}

	long_id = strtol(sus_ptr->job_id_str, &end_ptr, 10);
	if (end_ptr[0] == '+')
		rc = ESLURM_NOT_WHOLE_HET_JOB;
	else if ((long_id <= 0) || (long_id == LONG_MAX) ||
		 ((end_ptr[0] != '\0') && (end_ptr[0] != '_')))
		rc = ESLURM_INVALID_JOB_ID;
	else {
		job_id = (uint32_t) long_id;
		job_ptr = find_job_record(job_id);
		if (job_ptr == NULL)
			rc = ESLURM_INVALID_JOB_ID;
	}
	if (rc != SLURM_SUCCESS) {
		info("%s: invalid JobId=%s", __func__, sus_ptr->job_id_str);
		goto reply;
	}

	/* validate the request */
	if (!validate_operator(uid) &&
	    !assoc_mgr_is_user_acct_coord(acct_db_conn, uid,
					  job_ptr->account, false)) {
		error("SECURITY VIOLATION: Attempt to suspend job from user %u",
		      uid);
		rc = ESLURM_ACCESS_DENIED;
		goto reply;
	}

	if (end_ptr[0] == '\0') {	/* Single job (or full job array) */
		job_record_t *job_ptr_done = NULL;
		if (job_ptr &&
		    (((job_ptr->array_task_id == NO_VAL) &&
		      (job_ptr->array_recs == NULL)) ||
		     ((job_ptr->array_task_id != NO_VAL) &&
		      (job_ptr->array_job_id  != job_id)))) {
			/* This is a regular job or single task of job array */
			rc = _job_suspend(job_ptr, sus_ptr->op, indf_susp);
			goto reply;
		}

		if (job_ptr && job_ptr->array_recs) {
			/* This is a job array */
			job_ptr_done = job_ptr;
			rc2 = _job_suspend(job_ptr, sus_ptr->op, indf_susp);
			_resp_array_add(&resp_array, job_ptr, rc2, NULL);
		}

		/* Suspend all tasks of this job array */
		job_ptr = job_array_hash_j[JOB_HASH_INX(job_id)];
		if (!job_ptr && !job_ptr_done) {
			rc = ESLURM_INVALID_JOB_ID;
			goto reply;
		}
		while (job_ptr) {
			if ((job_ptr->array_job_id == job_id) &&
			    (job_ptr != job_ptr_done)) {
				rc2 = _job_suspend(job_ptr, sus_ptr->op,
						   indf_susp);
				_resp_array_add(&resp_array, job_ptr, rc2,
						NULL);
			}
			job_ptr = job_ptr->job_array_next_j;
		}
		goto reply;
	}

	array_bitmap = slurm_array_str2bitmap(end_ptr + 1, max_array_size,
					      NULL);
	if (!array_bitmap) {
		info("%s: invalid JobId=%s", __func__, sus_ptr->job_id_str);
		rc = ESLURM_INVALID_JOB_ID;
		goto reply;
	}

	for (int i = 0; (i = bit_ffs_from_bit(array_bitmap, i)) >= 0; i++) {
		job_ptr = find_job_array_rec(job_id, i);
		if (job_ptr == NULL) {
			info("%s: invalid JobId=%u_%d", __func__, job_id, i);
			_resp_array_add_id(&resp_array, job_id, i,
					   ESLURM_INVALID_JOB_ID);
			continue;
		}
		rc2 = _job_suspend(job_ptr, sus_ptr->op, indf_susp);
		_resp_array_add(&resp_array, job_ptr, rc2, NULL);
	}

reply:
	if (resp_array) {
		job_array_resp_msg_t *resp_array_msg =
			_resp_array_xlate(resp_array, job_id);
		(void) send_msg_response(msg, RESPONSE_JOB_ARRAY_ERRORS,
					 resp_array_msg);
		slurm_free_job_array_resp(resp_array_msg);
	} else
		slurm_send_rc_msg(msg, rc);

	_resp_array_free(resp_array);

	FREE_NULL_BITMAP(array_bitmap);

	return rc;
}

/*
 * _job_requeue_op - Requeue a running or pending batch job
 * IN uid - user id of user issuing the RPC
 * IN job_ptr - job to be requeued
 * IN preempt - true if job being preempted
 * RET 0 on success, otherwise ESLURM error code
 */
static int _job_requeue_op(uid_t uid, job_record_t *job_ptr, bool preempt,
			   uint32_t flags)
{
	static time_t config_update = 0;
	static bool requeue_nohold_prolog = true;
	bool is_running = false, is_suspended = false, is_completed = false;
	bool is_completing = false;
	bool force_requeue = false;
	time_t now = time(NULL);
	uint32_t completing_flags = 0;

	if (config_update != slurm_conf.last_update) {
		requeue_nohold_prolog = (xstrcasestr(slurm_conf.sched_params,
						     "nohold_on_prolog_fail"));
		config_update = slurm_conf.last_update;
	}

	/* validate the request */
	if ((uid != job_ptr->user_id) && !validate_operator(uid) &&
	    !assoc_mgr_is_user_acct_coord(acct_db_conn, uid,
					  job_ptr->account, false)) {
		return ESLURM_ACCESS_DENIED;
	}

	if (((flags & JOB_STATE_BASE) == JOB_RUNNING) &&
	    !IS_JOB_RUNNING(job_ptr) && !IS_JOB_SUSPENDED(job_ptr)) {
		return SLURM_SUCCESS;
	}

	if (flags & JOB_RECONFIG_FAIL)
		node_features_g_get_node(job_ptr->nodes);

	/*
	 * If the partition was removed don't allow the job to be
	 * requeued.  If it doesn't have details then something is very
	 * wrong and if the job doesn't want to be requeued don't unless
	 * it's being forced to do so after a launch failure.
	 */
	if ((flags & JOB_LAUNCH_FAILED) &&
	    (slurm_conf.prolog_flags & PROLOG_FLAG_FORCE_REQUEUE_ON_FAIL))
		force_requeue = true;
	if (!job_ptr->part_ptr || !job_ptr->details
	    || (!job_ptr->details->requeue && !force_requeue)) {
		if (flags & JOB_RECONFIG_FAIL)
			(void) _job_fail(job_ptr, JOB_BOOT_FAIL);
		return ESLURM_DISABLED;
	}

	if (job_ptr->batch_flag == 0) {
		debug("Job-requeue can only be done for batch jobs");
		if (flags & JOB_RECONFIG_FAIL)
			(void) _job_fail(job_ptr, JOB_BOOT_FAIL);
		return ESLURM_BATCH_ONLY;
	}

	/*
	 * If the job is already pending, just return an error.
	 * A federated origin job can be pending and revoked with a sibling job
	 * on another cluster.
	 */
	if (IS_JOB_PENDING(job_ptr) &&
	    (!job_ptr->fed_details || !job_ptr->fed_details->cluster_lock))
		return ESLURM_JOB_PENDING;

	if ((flags & JOB_RECONFIG_FAIL) && IS_JOB_CANCELLED(job_ptr)) {
		/*
		 * Job was cancelled (likely be the user) while node
		 * reconfiguration was in progress, so don't requeue it
		 * if the node reconfiguration failed.
		 */
		return ESLURM_DISABLED;
	}

	if (job_ptr->fed_details) {
		int rc;
		if ((rc = fed_mgr_job_requeue_test(job_ptr, flags)))
			return rc;

		/* Sent requeue request to origin cluster */
		if (job_ptr->job_state & JOB_REQUEUE_FED)
			return SLURM_SUCCESS;
	}

	last_job_update = now;

	/*
	 * In the job is in the process of completing
	 * return SLURM_SUCCESS and set the status
	 * to JOB_PENDING since we support requeue
	 * of done/exit/exiting jobs.
	 */
	if (IS_JOB_COMPLETING(job_ptr)) {
		completing_flags = job_ptr->job_state & JOB_STATE_FLAGS;
		is_completing = true;
	}

	if (IS_JOB_SUSPENDED(job_ptr)) {
		uint32_t suspend_job_state = job_ptr->job_state;
		/*
		 * we can't have it as suspended when we call the
		 * accounting stuff.
		 */
		job_state_set(job_ptr, JOB_REQUEUE);
		jobacct_storage_g_job_suspend(acct_db_conn, job_ptr);
		job_state_set(job_ptr, suspend_job_state);
		is_suspended = true;
	}

	job_ptr->time_last_active  = now;
	if (is_suspended)
		job_ptr->end_time = job_ptr->suspend_time;
	else if (!is_completing)
		job_ptr->end_time = now;

	/*
	 * Save the state of the job so that
	 * we deallocate the nodes if is in
	 * running state.
	 */
	if (IS_JOB_SUSPENDED(job_ptr) || IS_JOB_RUNNING(job_ptr))
		is_running = true;
	else if (IS_JOB_COMPLETED(job_ptr))
		is_completed = true;

	/* Only change state to requeue for local jobs */
	if (fed_mgr_is_origin_job(job_ptr) &&
	    !fed_mgr_is_tracker_only_job(job_ptr)) {
		/*
		 * We want this job to have the requeued/preempted state in the
		 * accounting logs. Set a new submit time so the restarted
		 * job looks like a new job.
		 */
		if (preempt) {
			job_state_set(job_ptr, JOB_PREEMPTED);
			build_cg_bitmap(job_ptr);
			job_completion_logger(job_ptr, true);
			job_state_set(job_ptr, JOB_REQUEUE);
		} else {
			job_state_set(job_ptr, JOB_REQUEUE);
			build_cg_bitmap(job_ptr);
			job_completion_logger(job_ptr, true);
		}
	}

	/*
	 * Increment restart counter before completing reply so that completing
	 * jobs get counted and so that fed jobs get counted before submitting
	 * new siblings in batch_requeue_fini()
	 */
	job_ptr->restart_cnt++;

	if (is_completing) {
		job_state_set(job_ptr, (JOB_PENDING | completing_flags));
		goto reply;
	}

	/*
	 * Deallocate resources only if the job has some.
	 * JOB_COMPLETING is needed to properly clean up steps.
	 */
	if (is_running) {
		job_state_set_flag(job_ptr, JOB_COMPLETING);
		deallocate_nodes(job_ptr, false, is_suspended, preempt);
		if (!IS_JOB_COMPLETING(job_ptr) && !job_ptr->fed_details)
			is_completed = true;
		else
			job_state_unset_flag(job_ptr, JOB_COMPLETING);
	}

	_set_requeued_job_pending_completing(job_ptr);

	/*
	 * Mark the origin job as requeuing. Will finish requeuing fed job
	 * after job has completed.
	 * If it's completed, batch_requeue_fini is called below and will call
	 * fed_mgr_job_requeue() to submit new siblings.
	 * If it's not completed, batch_requeue_fini will either be called when
	 * the running origin job finishes or the running remote sibling job
	 * reports that the job is finished.
	 */
	if (job_ptr->fed_details && !is_completed) {
		job_state_set_flag(job_ptr, (JOB_COMPLETING | JOB_REQUEUE_FED));
	}

	/*
	 * If we set the time limit it means the user didn't so reset
	 * it here or we could bust some limit when we try again
	 */
	if (job_ptr->limit_set.time == 1) {
		job_ptr->time_limit = NO_VAL;
		job_ptr->limit_set.time = 0;
	}

reply:
	job_ptr->pre_sus_time = (time_t) 0;
	job_ptr->suspend_time = (time_t) 0;
	job_ptr->tot_sus_time = (time_t) 0;

	job_ptr->db_flags = 0;

	/* clear signal sent flag on requeue */
	job_ptr->warn_flags &= ~WARN_SENT;

	/*
	 * Since the job completion logger removes the submit we need
	 * to add it again.
	 */
	acct_policy_add_job_submit(job_ptr, false);

	acct_policy_update_pending_job(job_ptr);

	if (flags & JOB_SPECIAL_EXIT) {
		job_state_set_flag(job_ptr, JOB_SPECIAL_EXIT);
		job_ptr->state_reason = WAIT_HELD_USER;
		xfree(job_ptr->state_desc);
		job_ptr->state_desc =
			xstrdup("job requeued in special exit state");
		debug("%s: Holding %pJ, special exit", __func__, job_ptr);
		job_ptr->priority = 0;
	}
	if (flags & JOB_REQUEUE_HOLD) {
		job_ptr->state_reason = WAIT_HELD_USER;
		xfree(job_ptr->state_desc);
		job_ptr->state_desc = xstrdup("job requeued in held state");
		debug("%s: Holding %pJ, requeue-hold exit", __func__, job_ptr);
		job_ptr->priority = 0;
	}
	if (flags & JOB_LAUNCH_FAILED) {
		job_ptr->batch_flag++;
		_handle_requeue_limit(job_ptr, __func__);

		/* If job not already held, make it so if needed. */
		if (!(job_ptr->job_state & JOB_REQUEUE_HOLD) &&
		    !requeue_nohold_prolog) {
			job_ptr->state_reason = WAIT_HELD_USER;
			xfree(job_ptr->state_desc);
			job_ptr->state_desc =
				xstrdup("launch failed requeued held");
			debug("%s: Holding %pJ due to prolog failure",
			      __func__, job_ptr);
			job_ptr->priority = 0;
		}
	}

	/*
	 * When jobs are requeued while running/completing batch_requeue_fini is
	 * called after the job is completely finished.  If the job is already
	 * finished it needs to be called to clear out states (especially the
	 * db_index or we will just write over the last job in the database).
	 * Call batch_requeue_fini after setting priority to 0 for requeue_hold
	 * and special_exit so federation doesn't submit siblings for held job.
	 */
	if (is_completed)
		batch_requeue_fini(job_ptr);

	debug("%s: %pJ state 0x%x reason %u priority %d",
	      __func__, job_ptr, job_ptr->job_state,
	      job_ptr->state_reason, job_ptr->priority);

	return SLURM_SUCCESS;
}

/*
 * _job_requeue - Requeue a running or pending batch job, if the specified
 *		  job records is a hetjob leader, perform the operation on all
 *		  components of the hetjob
 * IN uid - user id of user issuing the RPC
 * IN job_ptr - job to be requeued
 * IN preempt - true if job being preempted
 * RET 0 on success, otherwise ESLURM error code
 */
static int _job_requeue(uid_t uid, job_record_t *job_ptr, bool preempt,
			uint32_t flags)
{
	job_record_t *het_job;
	int rc = SLURM_SUCCESS, rc1;
	list_itr_t *iter;

	if (job_ptr->het_job_id && !job_ptr->het_job_list)
		return ESLURM_NOT_HET_JOB_LEADER;

	if (job_ptr->het_job_list) {
		iter = list_iterator_create(job_ptr->het_job_list);
		while ((het_job = list_next(iter))) {
			if (job_ptr->het_job_id != het_job->het_job_id) {
				error("%s: Bad het_job_list for %pJ",
				      __func__, job_ptr);
				continue;
			}
			rc1 = _job_requeue_op(uid, het_job, preempt, flags);
			if (rc1 != SLURM_SUCCESS)
				rc = rc1;
		}
		list_iterator_destroy(iter);
	} else {
		rc = _job_requeue_op(uid, job_ptr, preempt, flags);
	}

	return rc;
}

/*
 * job_requeue - Requeue a running or pending batch job
 * IN uid - user id of user issuing the RPC
 * IN job_id - id of the job to be requeued
 * IN msg - slurm_msg to send response back on
 * IN preempt - true if job being preempted
 * IN flags - JobExitRequeue | Hold | JobFailed | etc.
 * RET 0 on success, otherwise ESLURM error code
 */
extern int job_requeue(uid_t uid, uint32_t job_id, slurm_msg_t *msg,
		       bool preempt, uint32_t flags)
{
	int rc = SLURM_SUCCESS;
	job_record_t *job_ptr = NULL;

	/* find the job */
	job_ptr = find_job_record(job_id);
	if (job_ptr == NULL) {
		rc = ESLURM_INVALID_JOB_ID;
	} else {
		/* _job_requeue already handles het jobs */
		rc = _job_requeue(uid, job_ptr, preempt, flags);
	}

	if (msg) {
		slurm_send_rc_msg(msg, rc);
	}

	return rc;
}

/*
 * job_requeue2 - Requeue a running or pending batch job
 * IN uid - user id of user issuing the RPC
 * IN req_ptr - request including ID of the job to be requeued
 * IN msg - slurm_msg to send response back on
 * IN preempt - true if job being preempted
 * RET 0 on success, otherwise ESLURM error code
 */
extern int job_requeue2(uid_t uid, requeue_msg_t *req_ptr, slurm_msg_t *msg,
			bool preempt)
{
	int rc = SLURM_SUCCESS, rc2;
	job_record_t *job_ptr = NULL;
	long int long_id;
	uint32_t job_id = 0;
	char *end_ptr = NULL;
	bitstr_t *array_bitmap = NULL;
	uint32_t flags = req_ptr->flags;
	char *job_id_str = req_ptr->job_id_str;
	resp_array_struct_t *resp_array = NULL;
	job_array_resp_msg_t *resp_array_msg = NULL;

	if (max_array_size == NO_VAL) {
		max_array_size = slurm_conf.max_array_sz;
	}

	long_id = strtol(job_id_str, &end_ptr, 10);
	if ((long_id <= 0) || (long_id == LONG_MAX) ||
	    ((end_ptr[0] != '\0') && (end_ptr[0] != '_'))) {
		info("%s: invalid JobId=%s", __func__, job_id_str);
		rc = ESLURM_INVALID_JOB_ID;
		goto reply;
	}
	if ((end_ptr[0] == '_') && (end_ptr[1] == '*'))
		end_ptr += 2;	/* Defaults to full job array */

	job_id = (uint32_t) long_id;
	if (end_ptr[0] == '\0') {	/* Single job (or full job array) */
		job_record_t *job_ptr_done = NULL;
		job_ptr = find_job_record(job_id);
		if (job_ptr &&
		    (((job_ptr->array_task_id == NO_VAL) &&
		      (job_ptr->array_recs == NULL)) ||
		     ((job_ptr->array_task_id != NO_VAL) &&
		      (job_ptr->array_job_id  != job_id)))) {
			/* This is a regular job or single task of job array */
			rc = _job_requeue(uid, job_ptr, preempt, flags);
			goto reply;
		}

		if (job_ptr && job_ptr->array_recs) {
			/* This is a job array */
			job_ptr_done = job_ptr;
			rc2 = _job_requeue(uid, job_ptr, preempt, flags);
			_resp_array_add(&resp_array, job_ptr, rc2, NULL);
		}

		/* Requeue all tasks of this job array */
		job_ptr = job_array_hash_j[JOB_HASH_INX(job_id)];
		if (!job_ptr && !job_ptr_done) {
			rc = ESLURM_INVALID_JOB_ID;
			goto reply;
		}
		while (job_ptr) {
			if ((job_ptr->array_job_id == job_id) &&
			    (job_ptr != job_ptr_done)) {
				rc2 = _job_requeue(uid, job_ptr, preempt,flags);
				_resp_array_add(&resp_array, job_ptr, rc2,
						NULL);
			}
			job_ptr = job_ptr->job_array_next_j;
		}
		goto reply;
	}

	array_bitmap = slurm_array_str2bitmap(end_ptr + 1, max_array_size,
					      NULL);
	if (!array_bitmap) {
		info("%s: invalid JobId=%s", __func__, job_id_str);
		rc = ESLURM_INVALID_JOB_ID;
		goto reply;
	}

	for (int i = 0; (i = bit_ffs_from_bit(array_bitmap, i)) >= 0; i++) {
		job_ptr = find_job_array_rec(job_id, i);
		if (job_ptr == NULL) {
			info("%s: invalid JobId=%u_%d", __func__, job_id, i);
			_resp_array_add_id(&resp_array, job_id, i,
					   ESLURM_INVALID_JOB_ID);
			continue;
		}

		rc2 = _job_requeue(uid, job_ptr, preempt, flags);
		_resp_array_add(&resp_array, job_ptr, rc2, NULL);
	}

reply:
	if (msg) {
		if (resp_array) {
			resp_array_msg = _resp_array_xlate(resp_array, job_id);
			(void) send_msg_response(msg, RESPONSE_JOB_ARRAY_ERRORS,
						 resp_array_msg);
			slurm_free_job_array_resp(resp_array_msg);
		} else {
			slurm_send_rc_msg(msg, rc);
		}
	}
	_resp_array_free(resp_array);

	FREE_NULL_BITMAP(array_bitmap);

	return rc;
}

static int _top_job_flag_clear(void *x, void *arg)
{
	job_record_t *job_ptr = (job_record_t *) x;
	job_ptr->bit_flags &= (~TOP_PRIO_TMP);
	return 0;
}

/* This sorts so the highest priorities come off the list first */
static int _top_job_prio_sort(void *x, void *y)
{
	uint32_t *prio1, *prio2;
	prio1 = *(uint32_t **) x;
	prio2 = *(uint32_t **) y;
	if (*prio1 < *prio2)
		return 1;
	if (*prio1 > *prio2)
		return -1;
	return 0;
}

static int _set_top(list_t *top_job_list, uid_t uid)
{
	list_t *prio_list, *other_job_list;
	list_itr_t *iter;
	job_record_t *job_ptr, *first_job_ptr = NULL;
	int rc = SLURM_SUCCESS, rc2 = SLURM_SUCCESS;
	uint32_t last_prio = NO_VAL, next_prio;
	int64_t delta_prio, delta_nice, total_delta = 0;
	int other_job_cnt = 0;
	uint32_t *prio_elem;

	xassert(job_list);
	xassert(top_job_list);
	prio_list = list_create(xfree_ptr);
	(void) list_for_each(job_list, _top_job_flag_clear, NULL);

	/* Validate the jobs in our "top" list */
	iter = list_iterator_create(top_job_list);
	while ((job_ptr = list_next(iter))) {
		if ((job_ptr->user_id != uid) && (uid != 0)) {
			error("Security violation: REQUEST_TOP_JOB for %pJ from uid=%u",
			      job_ptr, uid);
			rc = ESLURM_ACCESS_DENIED;
			break;
		}
		if (!IS_JOB_PENDING(job_ptr) || (job_ptr->details == NULL)) {
			debug("%s: %pJ not pending",  __func__, job_ptr);
			list_remove(iter);
			rc2 = ESLURM_JOB_NOT_PENDING;
			continue;
		}
		if (job_ptr->part_ptr_list) {
			debug("%s: %pJ in partition list", __func__, job_ptr);
			list_remove(iter);
			rc = ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE;
			break;
		}
		if (job_ptr->priority == 0) {
			debug("%s: %pJ is held", __func__, job_ptr);
			list_remove(iter);
			rc2 = ESLURM_JOB_HELD;
			continue;
		}
		if (job_ptr->bit_flags & TOP_PRIO_TMP) {
			/* Duplicate job ID */
			list_remove(iter);
			continue;
		}
		if (!first_job_ptr)
			first_job_ptr = job_ptr;
		job_ptr->bit_flags |= TOP_PRIO_TMP;
		prio_elem = xmalloc(sizeof(uint32_t));
		*prio_elem = job_ptr->priority;
		list_append(prio_list, prio_elem);
	}
	list_iterator_destroy(iter);
	if (rc != SLURM_SUCCESS) {
		FREE_NULL_LIST(prio_list);
		return rc;
	}
	if (!first_job_ptr) {
		FREE_NULL_LIST(prio_list);
		return rc2;
	}

	/* Identify other jobs which we can adjust the nice value of */
	other_job_list = list_create(NULL);
	iter = list_iterator_create(job_list);
	while ((job_ptr = list_next(iter))) {
		/*
		 * Do not select jobs with priority 0 (held), or
		 * priority 1 (would be held if we lowered the priority).
		 */
		if ((job_ptr->bit_flags & TOP_PRIO_TMP) ||
		    (job_ptr->details == NULL) ||
		    (job_ptr->part_ptr_list)   ||
		    (job_ptr->priority <= 1)   ||
		    (job_ptr->assoc_ptr != first_job_ptr->assoc_ptr) ||
		    (job_ptr->part_ptr  != first_job_ptr->part_ptr)  ||
		    (job_ptr->qos_ptr   != first_job_ptr->qos_ptr)   ||
		    (job_ptr->user_id   != first_job_ptr->user_id)   ||
		    (!IS_JOB_PENDING(job_ptr)))
			continue;
		other_job_cnt++;
		job_ptr->bit_flags |= TOP_PRIO_TMP;
		prio_elem = xmalloc(sizeof(uint32_t));
		*prio_elem = job_ptr->priority;
		list_append(prio_list, prio_elem);
		list_append(other_job_list, job_ptr);
	}
	list_iterator_destroy(iter);

	/* Now adjust nice values and priorities of the listed "top" jobs */
	list_sort(prio_list, _top_job_prio_sort);
	iter = list_iterator_create(top_job_list);
	while ((job_ptr = list_next(iter))) {
		prio_elem = list_pop(prio_list);
		next_prio = *prio_elem;
		xfree(prio_elem);
		if ((last_prio != NO_VAL) && (next_prio == last_prio) &&
		    (last_prio > 2))
			/*
			 * We don't want to set job priority lower than 1, so
			 * last_prio cannot be smaller than 2, since we will
			 * later use last_prio - 1 for the new job priority.
			 */
			next_prio = last_prio - 1;
		last_prio = next_prio;
		delta_prio = (int64_t) next_prio - job_ptr->priority;
		delta_nice = MIN(job_ptr->details->nice, delta_prio);
		total_delta += delta_nice;
		job_ptr->priority = next_prio;
		job_ptr->details->nice -= delta_nice;
		job_ptr->bit_flags &= (~TOP_PRIO_TMP);
	}
	list_iterator_destroy(iter);
	FREE_NULL_LIST(prio_list);

	/* Now adjust nice values and priorities of remaining effected jobs */
	if (other_job_cnt) {
		iter = list_iterator_create(other_job_list);
		while ((job_ptr = list_next(iter))) {
			delta_prio = total_delta / other_job_cnt;
			next_prio = job_ptr->priority - delta_prio;
			if (next_prio >= last_prio) {
				next_prio = last_prio - 1;
				delta_prio = job_ptr->priority - next_prio;
			}
			delta_nice = delta_prio;
			job_ptr->priority = next_prio;
			job_ptr->details->nice += delta_nice;
			job_ptr->bit_flags &= (~TOP_PRIO_TMP);
			total_delta -= delta_nice;
			if (--other_job_cnt == 0)
				break;	/* Count will match list size anyway */
		}
		list_iterator_destroy(iter);
	}
	FREE_NULL_LIST(other_job_list);

	last_job_update = time(NULL);

	return rc;
}

/*
 * job_set_top - Move the specified jobs to the top of the queue (at least
 *	for that user ID, partition, account, and QOS).
 *
 * IN msg - original request msg
 * IN top_ptr - user request
 * IN uid - user id of the user issuing the RPC
 * IN protocol_version - slurm protocol version of client
 * RET 0 on success, otherwise ESLURM error code
 */
extern int job_set_top(slurm_msg_t *msg, top_job_msg_t *top_ptr, uid_t uid,
		       uint16_t protocol_version)
{
	int rc = SLURM_SUCCESS;
	list_t *top_job_list = NULL;
	char *job_str_tmp = NULL, *tok, *save_ptr = NULL, *end_ptr = NULL;
	job_record_t *job_ptr = NULL;
	long int long_id;
	uint32_t job_id = 0, task_id = 0;
	uid_t job_uid = uid;

	if (validate_operator(uid)) {
		job_uid = 0;
	} else {
		bool disable_user_top = true;
		if (xstrcasestr(slurm_conf.sched_params, "enable_user_top"))
			disable_user_top = false;
		if (disable_user_top) {
			rc = ESLURM_ACCESS_DENIED;
			goto reply;
		}
	}

	top_job_list = list_create(NULL);
	job_str_tmp = xstrdup(top_ptr->job_id_str);
	tok = strtok_r(job_str_tmp, ",", &save_ptr);
	while (tok) {
		long_id = strtol(tok, &end_ptr, 10);
		if ((long_id <= 0) || (long_id == LONG_MAX) ||
		    ((end_ptr[0] != '\0') && (end_ptr[0] != '_'))) {
			info("%s: invalid job id %s", __func__, tok);
			rc = ESLURM_INVALID_JOB_ID;
			goto reply;
		}
		job_id = (uint32_t) long_id;
		if ((end_ptr[0] == '\0') || /* Single job (or full job array) */
		    ((end_ptr[0] == '_') && (end_ptr[1] == '*') &&
		     (end_ptr[2] == '\0'))) {
			job_ptr = find_job_record(job_id);
			if (!job_ptr) {
				rc = ESLURM_INVALID_JOB_ID;
				goto reply;
			}
			list_append(top_job_list, job_ptr);
		} else if (end_ptr[0] != '_') {        /* Invalid job ID spec */
			rc = ESLURM_INVALID_JOB_ID;
			goto reply;
		} else {		/* Single task of a job array */
			task_id = strtol(end_ptr + 1, &end_ptr, 10);
			if (end_ptr[0] != '\0') {      /* Invalid job ID spec */
				rc = ESLURM_INVALID_JOB_ID;
				goto reply;
			}
			job_ptr = find_job_array_rec(job_id, task_id);
			if (!job_ptr) {
				rc = ESLURM_INVALID_JOB_ID;
				goto reply;
			}
			list_append(top_job_list, job_ptr);
		}
		tok = strtok_r(NULL, ",", &save_ptr);
	}

	if (list_count(top_job_list) == 0) {
		rc = ESLURM_INVALID_JOB_ID;
		goto reply;
	}
	rc = _set_top(top_job_list, job_uid);

reply:	FREE_NULL_LIST(top_job_list);
	xfree(job_str_tmp);
	slurm_send_rc_msg(msg, rc);
	return rc;
}

/*
 * job_end_time - Process JOB_END_TIME
 * IN time_req_msg - job end time request
 * OUT timeout_msg - job timeout response to be sent
 * RET SLURM_SUCCESS or an error code
 */
extern int job_end_time(job_alloc_info_msg_t *time_req_msg,
			srun_timeout_msg_t *timeout_msg)
{
	job_record_t *job_ptr;
	xassert(timeout_msg);

	job_ptr = find_job_record(time_req_msg->job_id);
	if (!job_ptr)
		return ESLURM_INVALID_JOB_ID;

	memset(timeout_msg, 0, sizeof(srun_timeout_msg_t));
	timeout_msg->step_id.job_id = time_req_msg->job_id;
	timeout_msg->step_id.step_id = NO_VAL;
	timeout_msg->step_id.step_het_comp = NO_VAL;
	timeout_msg->timeout = job_ptr->end_time;
	return SLURM_SUCCESS;
}

static int _update_job_nodes_str(job_record_t *job_ptr)
{
	xfree(job_ptr->nodes_completing);
	xfree(job_ptr->nodes_pr);

	if (!job_ptr->node_bitmap)
		return 0;

	if (IS_JOB_COMPLETING(job_ptr)) {
		if (job_ptr->node_bitmap_cg) {
			job_ptr->nodes_completing =
				bitmap2node_name(job_ptr->node_bitmap_cg);
		} else {
			job_ptr->nodes_completing =
				bitmap2node_name(job_ptr->node_bitmap);
		}
	}
	if (job_ptr->state_reason == WAIT_PROLOG) {
		if (job_ptr->node_bitmap_pr) {
			job_ptr->nodes_pr =
				bitmap2node_name(job_ptr->node_bitmap_pr);
		} else {
			job_ptr->nodes_pr =
				bitmap2node_name(job_ptr->node_bitmap);
		}
	}

	return 0;
}

/*
 * job_hold_by_assoc_id - Hold all pending jobs with a given
 *	association ID. This happens when an association is deleted (e.g. when
 *	a user is removed from the association database).
 * RET count of held jobs
 */
extern int job_hold_by_assoc_id(uint32_t assoc_id)
{
	int cnt = 0;
	list_itr_t *job_iterator;
	job_record_t *job_ptr;
	/* Write lock on jobs */
	slurmctld_lock_t job_write_lock =
		{ NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };

	if (!job_list)
		return cnt;

	lock_slurmctld(job_write_lock);
	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = list_next(job_iterator))) {
		if (job_ptr->assoc_id != assoc_id)
			continue;

		cnt += _job_fail_account(job_ptr, __func__, false);
	}
	list_iterator_destroy(job_iterator);
	unlock_slurmctld(job_write_lock);
	return cnt;
}

/*
 * job_hold_by_qos_id - Hold all pending jobs with a given
 *	QOS ID. This happens when a QOS is deleted (e.g. when
 *	a QOS is removed from the association database).
 * RET count of held jobs
 */
extern int job_hold_by_qos_id(uint32_t qos_id)
{
	int cnt = 0;
	list_itr_t *job_iterator;
	job_record_t *job_ptr;
	/* Write lock on jobs */
	slurmctld_lock_t job_write_lock =
		{ NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };

	if (!job_list)
		return cnt;

	lock_slurmctld(job_write_lock);
	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = list_next(job_iterator))) {
		if (job_ptr->qos_blocking_ptr &&
		    ((slurmdb_qos_rec_t *)job_ptr->qos_blocking_ptr)->id
		    == qos_id)
			job_ptr->qos_blocking_ptr = NULL;
		if (job_ptr->qos_list) {
			if (!list_find_first(job_ptr->qos_list,
					     slurmdb_find_qos_in_list,
					     &qos_id))
				continue;
		} else if (job_ptr->qos_id != qos_id)
			continue;

		cnt += job_fail_qos(job_ptr, __func__, false);
	}
	list_iterator_destroy(job_iterator);
	unlock_slurmctld(job_write_lock);
	return cnt;
}

/*
 * Modify the account associated with a pending job
 * IN module - where this is called from
 * IN job_ptr - pointer to job which should be modified
 * IN new_wckey - desired wckey name
 * RET SLURM_SUCCESS or error code
 */
extern int update_job_wckey(char *module, job_record_t *job_ptr,
			    char *new_wckey)
{
	slurmdb_wckey_rec_t wckey_rec, *wckey_ptr;

	if ((!IS_JOB_PENDING(job_ptr)) || (job_ptr->details == NULL)) {
		info("%s: attempt to modify account for non-pending %pJ",
		     module, job_ptr);
		return ESLURM_JOB_NOT_PENDING;
	}

	memset(&wckey_rec, 0, sizeof(wckey_rec));
	wckey_rec.uid       = job_ptr->user_id;
	wckey_rec.name      = new_wckey;
	if (assoc_mgr_fill_in_wckey(acct_db_conn, &wckey_rec,
				    accounting_enforce, &wckey_ptr, false)) {
		info("%s: invalid wckey %s for %pJ",
		     module, new_wckey, job_ptr);
		return ESLURM_INVALID_WCKEY;
	} else if (slurm_with_slurmdbd() &&
		   !wckey_ptr &&
		   !(accounting_enforce & ACCOUNTING_ENFORCE_WCKEYS)) {
		/* if not enforcing associations we want to look for
		   the default account and use it to avoid getting
		   trash in the accounting records.
		*/
		wckey_rec.name = NULL;
		assoc_mgr_fill_in_wckey(acct_db_conn, &wckey_rec,
					accounting_enforce, &wckey_ptr, false);
		if (!wckey_ptr) {
			debug("%s: we didn't have a wckey record for wckey "
			      "'%s' and user '%u', and we can't seem to find "
			      "a default one either.  Setting it anyway. "
			      "This will produce trash in accounting.  "
			      "If this is not what you desire please put "
			      "AccountStorageEnforce=wckeys in your slurm.conf "
			      "file.", module, new_wckey,
			      job_ptr->user_id);
			wckey_rec.name = new_wckey;
		}
	}

	xfree(job_ptr->wckey);
	if (wckey_rec.name && wckey_rec.name[0] != '\0') {
		job_ptr->wckey = xstrdup(wckey_rec.name);
		info("%s: setting wckey to %s for %pJ",
		     module, wckey_rec.name, job_ptr);
	} else {
		info("%s: cleared wckey for %pJ", module, job_ptr);
	}

	last_job_update = time(NULL);

	return SLURM_SUCCESS;
}

/*
 * Currently only sends active and suspsended jobs not already in the datbase.
 *
 * On node changes, we opt not to send updated node_inx's due to the heavy cost
 * of doing so. If we were to update the job's node_inx's, this could be done by
 * resizing the job which will create a new db record for the job with the
 * changed node_inx's -- like how reservations are done.
 * e.g.
 * job_pre_resize_acctg(job_ptr);
 * job_post_resize_acctg(job_ptr);
 */
extern int send_jobs_to_accounting(void)
{
	list_itr_t *itr = NULL;
	job_record_t *job_ptr;
	slurmctld_lock_t job_write_lock = {
		NO_LOCK, WRITE_LOCK, READ_LOCK, READ_LOCK, NO_LOCK };

	/* send jobs in pending or running state */
	lock_slurmctld(job_write_lock);
	itr = list_iterator_create(job_list);
	while ((job_ptr = list_next(itr))) {
		if (!job_ptr->assoc_id) {
			slurmdb_assoc_rec_t assoc_rec;
			memset(&assoc_rec, 0,
			       sizeof(assoc_rec));
			assoc_rec.acct      = job_ptr->account;
			if (job_ptr->part_ptr)
				assoc_rec.partition = job_ptr->part_ptr->name;
			assoc_rec.uid       = job_ptr->user_id;

			if (assoc_mgr_fill_in_assoc(
				    acct_db_conn, &assoc_rec,
				    accounting_enforce,
				    &job_ptr->assoc_ptr, false) &&
			    (accounting_enforce & ACCOUNTING_ENFORCE_ASSOCS)) {
				_job_fail_account(job_ptr, __func__, false);
				continue;
			} else
				job_ptr->assoc_id = assoc_rec.id;
		}

		/* we only want active, un accounted for jobs */
		if (IS_JOB_IN_DB(job_ptr) || IS_JOB_FINISHED(job_ptr))
			continue;

		debug("first reg: starting %pJ in accounting", job_ptr);
		jobacct_storage_g_job_start(acct_db_conn, job_ptr);

		if (IS_JOB_SUSPENDED(job_ptr))
			jobacct_storage_g_job_suspend(acct_db_conn, job_ptr);
	}
	list_iterator_destroy(itr);
	unlock_slurmctld(job_write_lock);

	return SLURM_SUCCESS;
}

/*
 * copy_job_record_to_job_desc - construct a job_desc_msg_t for a job.
 * IN job_ptr - the job record
 * RET the job_desc_msg_t, NULL on error
 */
extern job_desc_msg_t *copy_job_record_to_job_desc(job_record_t *job_ptr)
{
	job_desc_msg_t *job_desc;
	job_details_t *details = job_ptr->details;
	multi_core_data_t *mc_ptr = details->mc_ptr;
	int i;

	/* construct a job_desc_msg_t from job */
	job_desc = xmalloc(sizeof(job_desc_msg_t));

	job_desc->account           = xstrdup(job_ptr->account);
	job_desc->acctg_freq        = xstrdup(details->acctg_freq);
	job_desc->alloc_node        = xstrdup(job_ptr->alloc_node);
	/* Since the allocating salloc or srun is not expected to exist
	 * when this checkpointed job is restarted, do not save these:
	 *
	 * job_desc->alloc_resp_port   = job_ptr->alloc_resp_port;
	 * job_desc->alloc_sid         = job_ptr->alloc_sid;
	 */
	job_desc->argc              = details->argc;
	job_desc->argv              = xcalloc(job_desc->argc, sizeof(char *));
	for (i = 0; i < job_desc->argc; i ++)
		job_desc->argv[i]   = xstrdup(details->argv[i]);
	job_desc->begin_time        = details->begin_time;
	job_desc->bitflags 	    = job_ptr->bit_flags;
	job_desc->clusters          = xstrdup(job_ptr->clusters);
	job_desc->comment           = xstrdup(job_ptr->comment);
	job_desc->container = xstrdup(job_ptr->container);
	job_desc->container_id = xstrdup(job_ptr->container_id);
	job_desc->contiguous        = details->contiguous;
	job_desc->core_spec         = details->core_spec;
	job_desc->cpu_bind          = xstrdup(details->cpu_bind);
	job_desc->cpu_bind_type     = details->cpu_bind_type;
	job_desc->cpu_freq_min      = details->cpu_freq_min;
	job_desc->cpu_freq_max      = details->cpu_freq_max;
	job_desc->cpu_freq_gov      = details->cpu_freq_gov;
	job_desc->deadline          = job_ptr->deadline;
	job_desc->dependency        = xstrdup(details->dependency);
	job_desc->end_time          = 0; /* Unused today */
	job_desc->environment       = get_job_env(job_ptr,
						  &job_desc->env_size);
	job_desc->exc_nodes         = xstrdup(details->exc_nodes);
	job_desc->extra = xstrdup(job_ptr->extra);
	job_desc->features          = xstrdup(details->features);
	job_desc->cluster_features  = xstrdup(details->cluster_features);
	job_desc->group_id          = job_ptr->group_id;
	job_desc->immediate         = 0; /* nowhere to get this value */
	job_desc->job_id            = job_ptr->job_id;
	job_desc->kill_on_node_fail = job_ptr->kill_on_node_fail;
	job_desc->licenses          = xstrdup(job_ptr->lic_req);
	job_desc->mail_type         = job_ptr->mail_type;
	job_desc->mail_user         = xstrdup(job_ptr->mail_user);
	job_desc->mcs_label	    = xstrdup(job_ptr->mcs_label);
	job_desc->mem_bind          = xstrdup(details->mem_bind);
	job_desc->mem_bind_type     = details->mem_bind_type;
	job_desc->name              = xstrdup(job_ptr->name);
	job_desc->network           = xstrdup(job_ptr->network);
	job_desc->nice              = details->nice;
	job_desc->num_tasks         = details->num_tasks;
	job_desc->open_mode         = details->open_mode;
	job_desc->origin_cluster    = xstrdup(job_ptr->origin_cluster);
	job_desc->other_port        = job_ptr->other_port;
	job_desc->overcommit        = details->overcommit;
	job_desc->partition         = xstrdup(job_ptr->partition);
	job_desc->plane_size = mc_ptr->plane_size;
	job_desc->prefer            = xstrdup(details->prefer);
	job_desc->priority          = job_ptr->priority;
	if (job_ptr->qos_ptr)
		job_desc->qos       = xstrdup(job_ptr->qos_ptr->name);
	job_desc->resp_host         = xstrdup(job_ptr->resp_host);
	job_desc->req_nodes         = xstrdup(details->req_nodes);
	job_desc->requeue           = details->requeue;
	job_desc->reservation       = xstrdup(job_ptr->resv_name);
	job_desc->restart_cnt       = job_ptr->restart_cnt;
	job_desc->segment_size = details->segment_size;
	job_desc->script_buf        = get_job_script(job_ptr);
	if (details->share_res == 1)
		job_desc->shared     = JOB_SHARED_OK;
	else if (details->whole_node & WHOLE_NODE_REQUIRED)
		job_desc->shared     =  JOB_SHARED_NONE;
	else if (details->whole_node & WHOLE_NODE_USER)
		job_desc->shared     =  JOB_SHARED_USER;
	else if (details->whole_node & WHOLE_NODE_MCS)
		job_desc->shared     =  JOB_SHARED_MCS;
	else
		job_desc->shared     = NO_VAL16;
	job_desc->spank_job_env_size = job_ptr->spank_job_env_size;
	job_desc->spank_job_env      = xcalloc(job_desc->spank_job_env_size,
					       sizeof(char *));
	for (i = 0; i < job_desc->spank_job_env_size; i ++)
		job_desc->spank_job_env[i]= xstrdup(job_ptr->spank_job_env[i]);
	job_desc->std_err           = xstrdup(details->std_err);
	job_desc->std_in            = xstrdup(details->std_in);
	job_desc->std_out           = xstrdup(details->std_out);
	job_desc->submit_line       = xstrdup(details->submit_line);
	job_desc->task_dist         = details->task_dist;
	job_desc->time_limit        = job_ptr->time_limit;
	job_desc->time_min          = job_ptr->time_min;
	job_desc->user_id           = job_ptr->user_id;
	job_desc->wait_all_nodes    = job_ptr->wait_all_nodes;
	job_desc->warn_flags        = job_ptr->warn_flags;
	job_desc->warn_signal       = job_ptr->warn_signal;
	job_desc->warn_time         = job_ptr->warn_time;
	job_desc->wckey             = xstrdup(job_ptr->wckey);
	job_desc->work_dir          = xstrdup(details->work_dir);
	job_desc->pn_min_cpus       = details->pn_min_cpus;
	job_desc->pn_min_memory     = details->pn_min_memory;
	job_desc->oom_kill_step     = details->oom_kill_step;
	job_desc->pn_min_tmp_disk   = details->pn_min_tmp_disk;
	job_desc->min_cpus          = details->min_cpus;
	job_desc->max_cpus          = details->max_cpus;
	job_desc->min_nodes         = details->min_nodes;
	job_desc->max_nodes         = details->max_nodes;
	if (job_desc->max_nodes == 0) /* set 0 in _job_create() */
		job_desc->max_nodes = NO_VAL;
	job_desc->sockets_per_node  = mc_ptr->sockets_per_node;
	job_desc->cores_per_socket  = mc_ptr->cores_per_socket;
	job_desc->threads_per_core  = mc_ptr->threads_per_core;
	job_desc->cpus_per_task     = details->cpus_per_task;
	job_desc->ntasks_per_node   = details->ntasks_per_node;
	job_desc->ntasks_per_socket = mc_ptr->ntasks_per_socket;
	job_desc->ntasks_per_core   = mc_ptr->ntasks_per_core;

	job_desc->cpus_per_tres     = xstrdup(job_ptr->cpus_per_tres);
	job_desc->mem_per_tres      = xstrdup(job_ptr->mem_per_tres);
	job_desc->tres_bind         = xstrdup(job_ptr->tres_bind);
	job_desc->tres_freq         = xstrdup(job_ptr->tres_freq);
	job_desc->tres_per_job      = xstrdup(job_ptr->tres_per_job);
	job_desc->tres_per_node     = xstrdup(job_ptr->tres_per_node);
	job_desc->tres_per_socket   = xstrdup(job_ptr->tres_per_socket);
	job_desc->tres_per_task     = xstrdup(job_ptr->tres_per_task);

	if (job_ptr->fed_details) {
		job_desc->fed_siblings_active =
			job_ptr->fed_details->siblings_active;
		job_desc->fed_siblings_viable =
			job_ptr->fed_details->siblings_viable;
	}

	return job_desc;
}

/* Build a bitmap of nodes completing this job */
extern void build_cg_bitmap(job_record_t *job_ptr)
{
	FREE_NULL_BITMAP(job_ptr->node_bitmap_cg);
	if (job_ptr->node_bitmap) {
		job_ptr->node_bitmap_cg = bit_copy(job_ptr->node_bitmap);
		if (bit_ffs(job_ptr->node_bitmap_cg) == -1)
			job_state_unset_flag(job_ptr, JOB_COMPLETING);
	} else {
		error("build_cg_bitmap: node_bitmap is NULL");
		job_ptr->node_bitmap_cg = bit_alloc(node_record_count);
		job_state_unset_flag(job_ptr, JOB_COMPLETING);
	}
}

/* job_hold_requeue()
 *
 * Requeue the job based upon its current state.
 * If JOB_SPECIAL_EXIT then requeue and hold with JOB_SPECIAL_EXIT state.
 * If JOB_REQUEUE_HOLD then requeue and hold.
 * If JOB_REQUEUE then requeue and let it run again.
 * The requeue can happen directly from job_requeue() or from
 * job_epilog_complete() after the last component has finished.
 *
 * RET returns true if the job was requeued
 */
extern bool job_hold_requeue(job_record_t *job_ptr)
{
	uint32_t state;
	uint32_t flags;
	job_record_t *base_job_ptr = NULL;

	xassert(job_ptr);

	/* If the job is already pending it was
	 * eventually requeued somewhere else.
	 */
	if (IS_JOB_PENDING(job_ptr) && !IS_JOB_REVOKED(job_ptr))
		return false;

	/* If the job is not on the origin cluster, then don't worry about
	 * requeuing the job here. The exit code will be sent the origin
	 * cluster and the origin cluster will decide if the job should be
	 * requeued or not. */
	if (!fed_mgr_is_origin_job(job_ptr))
		return false;

	/*
	 * A job may be canceled during its epilog in which case we need to
	 * check that the job (or base job in the case of an array) was not
	 * canceled before attemping to requeue.
	 */
	if (IS_JOB_CANCELLED(job_ptr) ||
	    (((job_ptr->array_task_id != NO_VAL) || job_ptr->array_recs) &&
	     (base_job_ptr = find_job_record(job_ptr->array_job_id)) &&
	     base_job_ptr->array_recs && IS_JOB_CANCELLED(base_job_ptr)))
		return false;

	/* Check if the job exit with one of the
	 * configured requeue values. */
	_set_job_requeue_exit_value(job_ptr);

	/* handle crontab jobs */
	if ((job_ptr->bit_flags & CRON_JOB) &&
	    job_ptr->details->crontab_entry) {
		job_state_set_flag(job_ptr, JOB_REQUEUE);
		job_ptr->details->begin_time =
			calc_next_cron_start(job_ptr->details->crontab_entry,
					     0);
	} else if (job_ptr->bit_flags & CRON_JOB) {
		/*
		 * Skip requeuing this instead of crashing.
		 */
		error("Missing cron details for %pJ. This should never happen. Clearing CRON_JOB flag and skipping requeue.",
		      job_ptr);
		job_ptr->bit_flags &= ~CRON_JOB;
	}

	state = job_ptr->job_state;

	if (! (state & JOB_REQUEUE))
		return false;

	/* Sent event requeue to the database.  */
	if (!(job_ptr->bit_flags & TRES_STR_CALC) &&
	    job_ptr->tres_alloc_cnt &&
	    (job_ptr->tres_alloc_cnt[TRES_ENERGY] != NO_VAL64))
		assoc_mgr_set_job_tres_alloc_str(job_ptr, false);
	jobacct_storage_g_job_complete(acct_db_conn, job_ptr);

	debug("%s: %pJ state 0x%x", __func__, job_ptr, state);

	/* Set the job pending */
	flags = job_ptr->job_state & JOB_STATE_FLAGS;
	job_state_set(job_ptr, (JOB_PENDING | flags));

	job_ptr->restart_cnt++;

	/* clear signal sent flag on requeue */
	job_ptr->warn_flags &= ~WARN_SENT;

	/*
	 * Test if user wants to requeue the job
	 * in hold or with a special exit value.
	 */
	if (state & JOB_SPECIAL_EXIT) {
		/*
		 * JOB_SPECIAL_EXIT means requeue the job,
		 * put it on hold and display state as JOB_SPECIAL_EXIT.
		 */
		job_state_set_flag(job_ptr, JOB_SPECIAL_EXIT);
		job_ptr->state_reason = WAIT_HELD_USER;
		debug("%s: Holding %pJ, special exit", __func__, job_ptr);
		job_ptr->priority = 0;
	}

	job_state_unset_flag(job_ptr, JOB_REQUEUE);

	/*
	 * Mark array as requeued. Exit codes have already been handled in
	 * _job_array_comp()
	 */
	if (((job_ptr->array_task_id != NO_VAL) || job_ptr->array_recs) &&
	    (base_job_ptr = find_job_record(job_ptr->array_job_id)) &&
	    base_job_ptr->array_recs) {
		base_job_ptr->array_recs->array_flags |= ARRAY_TASK_REQUEUED;
	}

	debug("%s: %pJ state 0x%x reason %u priority %d",
	      __func__, job_ptr, job_ptr->job_state,
	      job_ptr->state_reason, job_ptr->priority);

	return true;
}

static void _parse_max_depend_depth(char *str)
{
	int i = atoi(str);
	if (i < 0)
		error("ignoring max_depend_depth value of %d", i);
	else
		max_depend_depth = i;
}

extern void init_depend_policy(void)
{
	char *tmp_ptr;

	disable_remote_singleton =
		(xstrcasestr(slurm_conf.dependency_params,
		             "disable_remote_singleton")) ?
		true : false;

	kill_invalid_dep =
		(xstrcasestr(slurm_conf.dependency_params,
			     "kill_invalid_depend")) ?
		true : false;

	/* 			    01234567890123456 */
	if ((tmp_ptr = xstrcasestr(slurm_conf.dependency_params,
	                           "max_depend_depth=")))
		_parse_max_depend_depth(tmp_ptr + 17);
	else
		max_depend_depth = 10;

	log_flag(DEPENDENCY, "%s: kill_invalid_depend is set to %d; disable_remote_singleton is set to %d; max_depend_depth is set to %d",
	         __func__, kill_invalid_dep, disable_remote_singleton,
	         max_depend_depth);
}

/* init_requeue_policy()
 * Initialize the requeue exit/hold bitmaps.
 */
extern void init_requeue_policy(void)
{
	/* clean first as we can be reconfiguring */
	FREE_NULL_BITMAP(requeue_exit);
	FREE_NULL_BITMAP(requeue_exit_hold);

	requeue_exit = _make_requeue_array(slurm_conf.requeue_exit);
	requeue_exit_hold = _make_requeue_array(slurm_conf.requeue_exit_hold);
}

/* _make_requeue_array()
 *
 * Process the RequeueExit|RequeueExitHold configuration
 * parameters creating two bitmaps holding the exit values
 * of jobs for which they have to be requeued.
 */
static bitstr_t *_make_requeue_array(char *conf_buf)
{
	hostset_t *hs;
	bitstr_t *bs = NULL;
	char *tok = NULL, *end_ptr = NULL;
	long val;

	if (conf_buf == NULL)
		return bs;

	xstrfmtcat(tok, "[%s]", conf_buf);
	hs = hostset_create(tok);
	xfree(tok);
	if (!hs) {
		error("%s: exit values: %s", __func__, conf_buf);
		return bs;
	}

	debug("%s: exit values: %s", __func__, conf_buf);

	bs = bit_alloc(MAX_EXIT_VAL + 1);
	while ((tok = hostset_shift(hs))) {
		val = strtol(tok, &end_ptr, 10);
		if ((end_ptr[0] == '\0') &&
		    (val >= 0) && (val <= MAX_EXIT_VAL)) {
			bit_set(bs, val);
		} else {
			error("%s: exit values: %s (%s)",
			      __func__, conf_buf, tok);
		}
		free(tok);
	}
	hostset_destroy(hs);

	return bs;
}

/* _set_job_requeue_exit_value()
 *
 * Compared the job exit values with the configured
 * RequeueExit and RequeueHoldExit and a match is
 * found, set the appropriate state for job_hold_requeue()
 */
static void _set_job_requeue_exit_value(job_record_t *job_ptr)
{
	int exit_code;

	/* --no-requeue option supercedes config for RequeueExit &
	 * RequeueExitHold
	 */
	if (job_ptr->details && !job_ptr->details->requeue)
		return;

	exit_code = WEXITSTATUS(job_ptr->exit_code);

	if (requeue_exit && bit_test(requeue_exit, exit_code)) {
		debug2("%s: %pJ exit code %d state JOB_REQUEUE",
		       __func__, job_ptr, exit_code);
		job_state_set_flag(job_ptr, JOB_REQUEUE);
		return;
	}

	if (requeue_exit_hold && bit_test(requeue_exit_hold, exit_code)) {
		/* Not sure if want to set special exit state in this case */
		debug2("%s: %pJ exit code %d state JOB_SPECIAL_EXIT",
		       __func__, job_ptr, exit_code);
		job_state_set_flag(job_ptr, (JOB_REQUEUE | JOB_SPECIAL_EXIT));
		return;
	}
}

/*
 * Reset a job's end_time based upon it's start_time and time_limit.
 * NOTE: Do not reset the end_time if already being preempted
 */
extern void job_end_time_reset(job_record_t *job_ptr)
{
	if (job_ptr->preempt_time)
		return; /* Preemption in progress */
	if (job_ptr->time_limit == INFINITE) {
		job_ptr->end_time = job_ptr->start_time +
			(365 * 24 * 60 * 60); /* secs in year */
	} else {
		job_ptr->end_time = job_ptr->start_time +
			(job_ptr->time_limit * 60);	/* secs */
	}
	job_ptr->end_time_exp = job_ptr->end_time;
}

/* If this is a job array meta-job, prepare it for being scheduled */
extern void job_array_pre_sched(job_record_t *job_ptr)
{
	int32_t i;

	if (!job_ptr->array_recs || !job_ptr->array_recs->task_id_bitmap)
		return;

	i = bit_ffs(job_ptr->array_recs->task_id_bitmap);
	if (i < 0) {
		/* This happens if the final task in a meta-job is requeued */
		if (job_ptr->restart_cnt == 0) {
			error("%pJ has empty task_id_bitmap", job_ptr);
		}
		FREE_NULL_BITMAP(job_ptr->array_recs->task_id_bitmap);
		return;
	}

	job_ptr->array_job_id  = job_ptr->job_id;
	job_ptr->array_task_id = i;
}

/* If this is a job array meta-job, clean up after scheduling attempt */
extern job_record_t *job_array_post_sched(job_record_t *job_ptr, bool list_add)
{
	job_record_t *new_job_ptr = NULL;

	if (!job_ptr->array_recs || !job_ptr->array_recs->task_id_bitmap)
		return job_ptr;

	if (job_ptr->array_recs->task_cnt <= 1) {
		/* Preserve array_recs for min/max exit codes for job array */
		if (job_ptr->array_recs->task_cnt) {
			job_ptr->array_recs->task_cnt--;
		} else if (job_ptr->restart_cnt) {
			/* Last task of a job array has been requeued */
		} else {
			error("job %pJ array_recs task count underflow",
			      job_ptr);
		}
		xfree(job_ptr->array_recs->task_id_str);
		if (job_ptr->array_recs->task_cnt == 0)
			FREE_NULL_BITMAP(job_ptr->array_recs->task_id_bitmap);


		/* Update the job in the database. */
		jobacct_storage_g_job_start(acct_db_conn, job_ptr);

		/* If job is requeued, it will already be in the hash table */
		if (!find_job_array_rec(job_ptr->array_job_id,
					job_ptr->array_task_id)) {
			_add_job_array_hash(job_ptr);
		}
		new_job_ptr = job_ptr;
	} else {
		new_job_ptr = job_array_split(job_ptr, list_add);
		job_state_set(new_job_ptr, JOB_PENDING);
		new_job_ptr->start_time = (time_t) 0;
	}

	return new_job_ptr;
}

/* _kill_dependent()
 *
 * Exterminate the job that has invalid dependency
 * condition.
 */
static void _kill_dependent(job_record_t *job_ptr)
{
	time_t now = time(NULL);

	info("%s: Job dependency can't be satisfied, cancelling %pJ",
	     __func__, job_ptr);
	job_state_set(job_ptr, JOB_CANCELLED);
	job_ptr->start_time = now;
	job_ptr->end_time = now;
	job_completion_logger(job_ptr, false);
	last_job_update = now;
	srun_allocate_abort(job_ptr);
}

static job_fed_details_t *_dup_job_fed_details(job_fed_details_t *src)
{
	job_fed_details_t *dst = NULL;

	if (!src)
		return NULL;

	dst = xmalloc(sizeof(job_fed_details_t));
	memcpy(dst, src, sizeof(job_fed_details_t));
	dst->origin_str          = xstrdup(src->origin_str);
	dst->siblings_active_str = xstrdup(src->siblings_active_str);
	dst->siblings_viable_str = xstrdup(src->siblings_viable_str);

	return dst;
}

/* Set federated job's sibling strings. */
extern void update_job_fed_details(job_record_t *job_ptr)
{
	xassert(job_ptr);
	xassert(job_ptr->fed_details);

	xfree(job_ptr->fed_details->siblings_active_str);
	xfree(job_ptr->fed_details->siblings_viable_str);

	job_ptr->fed_details->siblings_active_str =
		fed_mgr_cluster_ids_to_names(
			job_ptr->fed_details->siblings_active);
	job_ptr->fed_details->siblings_viable_str =
		fed_mgr_cluster_ids_to_names(
			job_ptr->fed_details->siblings_viable);

	/* only set once */
	if (!job_ptr->fed_details->origin_str)
		job_ptr->fed_details->origin_str =
			fed_mgr_get_cluster_name(
				fed_mgr_get_cluster_id(job_ptr->job_id));
}

/*
 * Set the allocation response with the current cluster's information and the
 * job's allocated node's addr's if the allocation is being filled by a cluster
 * other than the cluster that submitted the job
 *
 * Note: make sure that the resp's working_cluster_rec is NULL'ed out before the
 * resp is free'd since it points to global memory.
 *
 * IN resp - allocation response being sent back to client.
 * IN job_ptr - allocated job
 * IN req_cluster - the cluster requsting the allocation info.
 */
extern void set_remote_working_response(
	resource_allocation_response_msg_t *resp,
	job_record_t *job_ptr, const char *req_cluster)
{
	xassert(resp);
	xassert(job_ptr);

	if (job_ptr->node_cnt && req_cluster &&
	    xstrcmp(slurm_conf.cluster_name, req_cluster)) {
		if (job_ptr->fed_details &&
		    fed_mgr_cluster_rec) {
			resp->working_cluster_rec = fed_mgr_cluster_rec;
		} else {
			resp->working_cluster_rec = response_cluster_rec;
		}

		if (!job_ptr->node_addrs) {
			/*
			 * The job may be owned by the local cluster but a
			 * remote srun might be trying to launch a job in the
			 * allocation.
			 */
			set_job_node_addrs(job_ptr, req_cluster);
		}
		if (job_ptr->node_addrs) {
			resp->node_addr = xcalloc(job_ptr->node_cnt,
						  sizeof(slurm_addr_t));
			memcpy(resp->node_addr, job_ptr->node_addrs,
			       job_ptr->node_cnt * sizeof(slurm_addr_t));
		}
	}
}

/*
 * Calculate billable TRES based on partition's defined BillingWeights. If none
 * is defined, return total_cpus. This is cached on job_ptr->billable_tres and
 * is updated if the job was resized since the last iteration.
 *
 * IN job_ptr          - job to calc billable tres on
 * IN start_time       - time the has started or been resized
 * IN assoc_mgr_locked - whether the tres assoc lock is set or not
 */
extern double calc_job_billable_tres(job_record_t *job_ptr, time_t start_time,
				     bool assoc_mgr_locked)
{
	xassert(job_ptr);

	part_record_t *part_ptr = job_ptr->part_ptr;

	/* We don't have any resources allocated, just return 0. */
	if (!job_ptr->tres_alloc_cnt)
		return 0;

	/* Don't recalculate unless the job is new or resized */
	if ((!fuzzy_equal(job_ptr->billable_tres, NO_VAL)) &&
	    difftime(job_ptr->resize_time, start_time) < 0.0)
		return job_ptr->billable_tres;

	log_flag(PRIO, "BillingWeight: %pJ is either new or it was resized",
		 job_ptr);

	/* No billing weights defined. Return CPU count */
	if (!part_ptr || !part_ptr->billing_weights) {
		job_ptr->billable_tres = job_ptr->total_cpus;
		return job_ptr->billable_tres;
	}

	log_flag(PRIO, "BillingWeight: %pJ using \"%s\" from partition %s",
		 job_ptr, part_ptr->billing_weights_str,
		 job_ptr->part_ptr->name);

	job_ptr->billable_tres =
		assoc_mgr_tres_weighted(job_ptr->tres_alloc_cnt,
		                        part_ptr->billing_weights,
		                        slurm_conf.priority_flags,
		                        assoc_mgr_locked);

	log_flag(PRIO, "BillingWeight: %pJ %s = %f",
	         job_ptr,
	         (slurm_conf.priority_flags & PRIORITY_FLAGS_MAX_TRES) ?
	         "MAX(node TRES) + SUM(Global TRES)" : "SUM(TRES)",
	         job_ptr->billable_tres);

	return job_ptr->billable_tres;
}

/*
 * Send warning signal to job before end time.
 *
 * IN job_ptr - job to send warn signal to.
 * IN ignore_time - If set, ignore the warn time and just send it.
 */
extern void send_job_warn_signal(job_record_t *job_ptr, bool ignore_time)
{
	if (job_ptr->warn_signal &&
	    !(job_ptr->warn_flags & WARN_SENT) &&
	    (ignore_time ||
	     (job_ptr->warn_time &&
	      ((job_ptr->warn_time + PERIODIC_TIMEOUT + time(NULL)) >=
	       job_ptr->end_time)))) {
		/*
		 * If --signal B option was not specified,
		 * signal only the steps but not the batch step.
		 */
		if (!(job_ptr->warn_flags & KILL_JOB_BATCH))
			job_ptr->warn_flags |= KILL_STEPS_ONLY;

		/* send SIGCONT first */
		job_signal(job_ptr, SIGCONT, job_ptr->warn_flags, 0, false);

		debug("%s: warning signal %u to %pJ",
		      __func__, job_ptr->warn_signal, job_ptr);

		job_signal(job_ptr, job_ptr->warn_signal,
			   job_ptr->warn_flags, 0, false);

		/* mark job as signaled */
		job_ptr->warn_flags |= WARN_SENT;
	}
}

static int _overlap_and_running_internal(void *x, void *arg)
{
	job_record_t *job_ptr = (job_record_t *)x;
	job_overlap_args_t *overlap_args = (job_overlap_args_t *)arg;

	/* We always break if we find something not running */
	if ((!IS_JOB_RUNNING(job_ptr) && !IS_JOB_SUSPENDED(job_ptr))) {
		overlap_args->rc = 0;
		return -1;
	}

	/*
	 * We are just looking for something overlapping.  On a hetjob we need
	 * to check everything.
	 */
	if (license_list_overlap(overlap_args->license_list,
				 job_ptr->license_list) ||
	    (job_ptr->node_bitmap &&
	    bit_overlap_any(overlap_args->node_map, job_ptr->node_bitmap)))
		overlap_args->rc = 1;

	return 0;
}

extern bool job_overlap_and_running(bitstr_t *node_map, list_t *license_list,
				    job_record_t *job_ptr)
{
	job_overlap_args_t overlap_args = {
		.node_map = node_map,
		.license_list = license_list,
	};

	if (!job_ptr->het_job_list)
		(void)_overlap_and_running_internal(job_ptr, &overlap_args);
	else
		(void)list_for_each(job_ptr->het_job_list,
				    _overlap_and_running_internal,
				    &overlap_args);

	return overlap_args.rc;
}

extern char **job_common_env_vars(job_record_t *job_ptr, bool is_complete)
{
	char **my_env, *name, *eq, buf[32];
	int exit_code, i, signal;

	my_env = xmalloc(sizeof(char *));
	my_env[0] = NULL;

	/* Set SPANK env vars first so that we can overwrite as needed
	 * below. Prevent user hacking from setting SLURM_JOB_ID etc. */
	if (job_ptr->spank_job_env_size) {
		env_array_merge(&my_env,
				(const char **) job_ptr->spank_job_env);
		valid_spank_job_env(my_env, job_ptr->spank_job_env_size,
				    job_ptr->user_id);
	}

	setenvf(&my_env, "SLURM_JOB_ACCOUNT", "%s", job_ptr->account);

	if (is_complete) {
		exit_code = signal = 0;
		if (WIFEXITED(job_ptr->exit_code)) {
			exit_code = WEXITSTATUS(job_ptr->exit_code);
		}
		if (WIFSIGNALED(job_ptr->exit_code)) {
			signal = WTERMSIG(job_ptr->exit_code);
		}
		snprintf(buf, sizeof(buf), "%d:%d", exit_code, signal);
		setenvf(&my_env, "SLURM_JOB_DERIVED_EC", "%u",
			job_ptr->derived_ec);
		setenvf(&my_env, "SLURM_JOB_EXIT_CODE2", "%s", buf);
		setenvf(&my_env, "SLURM_JOB_EXIT_CODE", "%u",
			job_ptr->exit_code);
	}

	if (job_ptr->array_task_id != NO_VAL) {
		setenvf(&my_env, "SLURM_ARRAY_JOB_ID", "%u",
			job_ptr->array_job_id);
		setenvf(&my_env, "SLURM_ARRAY_TASK_ID", "%u",
			job_ptr->array_task_id);
		if (job_ptr->details && job_ptr->details->env_sup &&
		    job_ptr->details->env_cnt) {
			for (i = 0; i < job_ptr->details->env_cnt; i++) {
				if (xstrncmp(job_ptr->details->env_sup[i],
					     "SLURM_ARRAY_TASK", 16))
					continue;
				eq = strchr(job_ptr->details->env_sup[i], '=');
				if (!eq)
					continue;
				eq[0] = '\0';
				setenvf(&my_env,
					job_ptr->details->env_sup[i],
					"%s", eq + 1);
				eq[0] = '=';
			}
		}
	}

	if (slurm_conf.cluster_name) {
		setenvf(&my_env, "SLURM_CLUSTER_NAME", "%s",
		        slurm_conf.cluster_name);
	}

	if (job_ptr->comment)
		setenvf(&my_env, "SLURM_JOB_COMMENT", "%s", job_ptr->comment);

	setenvf(&my_env, "SLURM_JOB_END_TIME", "%lu", job_ptr->end_time);

	if (job_ptr->extra)
		setenvf(&my_env, "SLURM_JOB_EXTRA", "%s", job_ptr->extra);

	if (job_ptr->het_job_id) {
		/* Continue support for old hetjob terminology. */
		setenvf(&my_env, "SLURM_PACK_JOB_ID", "%u",
			job_ptr->het_job_id);
		setenvf(&my_env, "SLURM_PACK_JOB_OFFSET", "%u",
			job_ptr->het_job_offset);
		setenvf(&my_env, "SLURM_HET_JOB_ID", "%u",
			job_ptr->het_job_id);
		setenvf(&my_env, "SLURM_HET_JOB_OFFSET", "%u",
			job_ptr->het_job_offset);
		if ((job_ptr->het_job_offset == 0) && job_ptr->het_job_list) {
			job_record_t *het_job = NULL;
			list_itr_t *iter;
			hostset_t *hs = NULL;
			iter = list_iterator_create(job_ptr->het_job_list);
			while ((het_job = list_next(iter))) {
				if (job_ptr->het_job_id !=
				    het_job->het_job_id) {
					error("%s: Bad het_job_list for %pJ",
					      __func__, job_ptr);
					continue;
				}

				if (!het_job->nodes) {
					debug("%s: %pJ het_job->nodes == NULL.  Usually this means the job was canceled while it was starting and shouldn't be a real issue.",
					      __func__, job_ptr);
					continue;
				}

				if (hs) {
					(void) hostset_insert(hs,
							      het_job->nodes);
				} else {
					hs = hostset_create(het_job->nodes);
				}
			}
			list_iterator_destroy(iter);
			if (hs) {
				char *buf = hostset_ranged_string_xmalloc(hs);
				/* Support for old hetjob terminology. */
				setenvf(&my_env, "SLURM_PACK_JOB_NODELIST",
					"%s", buf);
				setenvf(&my_env, "SLURM_HET_JOB_NODELIST",
					"%s", buf);
				xfree(buf);
				hostset_destroy(hs);
			}
		}
	}
	setenvf(&my_env, "SLURM_JOB_GID", "%u", job_ptr->group_id);
	name = group_from_job(job_ptr);
	setenvf(&my_env, "SLURM_JOB_GROUP", "%s", name);
	xfree(name);
	setenvf(&my_env, "SLURM_JOBID", "%u", job_ptr->job_id);
	setenvf(&my_env, "SLURM_JOB_ID", "%u", job_ptr->job_id);
	if (job_ptr->licenses)
		setenvf(&my_env, "SLURM_JOB_LICENSES", "%s", job_ptr->licenses);
	setenvf(&my_env, "SLURM_JOB_NAME", "%s", job_ptr->name);
	setenvf(&my_env, "SLURM_JOB_NODELIST", "%s", job_ptr->nodes);
	if (job_ptr->job_resrcs) {
		char *tmp;

		tmp = uint32_compressed_to_str(
			job_ptr->job_resrcs->cpu_array_cnt,
			job_ptr->job_resrcs->cpu_array_value,
			job_ptr->job_resrcs->cpu_array_reps);
		setenvf(&my_env, "SLURM_JOB_CPUS_PER_NODE", "%s", tmp);
		xfree(tmp);

		setenvf(&my_env, "SLURM_JOB_NUM_NODES", "%u",
			job_ptr->job_resrcs->nhosts);
	}
	if (job_ptr->part_ptr) {
		setenvf(&my_env, "SLURM_JOB_PARTITION", "%s",
			job_ptr->part_ptr->name);
	} else {
		setenvf(&my_env, "SLURM_JOB_PARTITION", "%s",
			job_ptr->partition);
	}

	if (job_ptr->resv_ptr)
		setenvf(&my_env, "SLURM_JOB_RESERVATION", "%s",
			job_ptr->resv_ptr->name);

	setenvf(&my_env, "SLURM_JOB_RESTART_COUNT", "%d", job_ptr->restart_cnt);

	setenvf(&my_env, "SLURM_JOB_START_TIME", "%lu", job_ptr->start_time);

	setenvf(&my_env, "SLURM_JOB_UID", "%u", job_ptr->user_id);
	name = user_from_job(job_ptr);
	setenvf(&my_env, "SLURM_JOB_USER", "%s", name);
	xfree(name);
	if (job_ptr->wckey) {
		setenvf(&my_env, "SLURM_WCKEY", "%s", job_ptr->wckey);
	}

	if (job_ptr->details) {
		if (job_ptr->details->features_use)
			setenvf(&my_env, "SLURM_JOB_CONSTRAINTS", "%s",
				job_ptr->details->features_use);

		setenvf(&my_env, "SLURM_JOB_OVERSUBSCRIBE", "%s",
			job_share_string(get_job_share_value(job_ptr)));

		if (job_ptr->details->std_err)
			setenvf(&my_env, "SLURM_JOB_STDERR", "%s",
				job_ptr->details->std_err);
		if (job_ptr->details->std_in)
			setenvf(&my_env, "SLURM_JOB_STDIN", "%s",
				job_ptr->details->std_in);
		if (job_ptr->details->std_out)
			setenvf(&my_env, "SLURM_JOB_STDOUT", "%s",
				job_ptr->details->std_out);
		if (job_ptr->details->work_dir)
			setenvf(&my_env, "SLURM_JOB_WORK_DIR", "%s",
				job_ptr->details->work_dir);
	}

	return my_env;
}

extern job_record_t *job_mgr_copy_resv_desc_to_job_record(
	resv_desc_msg_t *resv_desc_ptr)
{
	job_record_t *job_ptr;
	job_details_t *detail_ptr;
	part_record_t *part_ptr = NULL;

	job_ptr = _create_job_record(1, false);
	detail_ptr = job_ptr->details;

	job_ptr->partition = xstrdup(resv_desc_ptr->partition);

	if (job_ptr->partition)
		part_ptr = find_part_record(job_ptr->partition);
	if (part_ptr && part_ptr->def_mem_per_cpu)
		detail_ptr->pn_min_memory = part_ptr->def_mem_per_cpu;
	else
		detail_ptr->pn_min_memory = slurm_conf.def_mem_per_cpu;

	job_ptr->time_limit = resv_desc_ptr->duration;

	detail_ptr->begin_time = resv_desc_ptr->start_time;
	if (resv_desc_ptr->node_cnt != NO_VAL) {
		detail_ptr->max_nodes = detail_ptr->min_nodes =
			resv_desc_ptr->node_cnt;
	} else {
		detail_ptr->min_nodes = 1;
		/* 500000 comes from job_scheduler.c job_start_data() */
		detail_ptr->max_nodes = 500000;
	}

	if (resv_desc_ptr->node_list) {
		hostlist_t *hl = hostlist_create(resv_desc_ptr->node_list);
		hostlist_uniq(hl);
		detail_ptr->req_nodes = hostlist_ranged_string_xmalloc(hl);
		detail_ptr->max_nodes = detail_ptr->min_nodes =
			hostlist_count(hl);
		hostlist_destroy(hl);

		(void) node_name2bitmap(detail_ptr->req_nodes, true,
					&detail_ptr->req_node_bitmap, NULL);
	}

	if (resv_desc_ptr->tres_str || resv_desc_ptr->core_cnt != NO_VAL) {
		detail_ptr->mc_ptr = xmalloc(sizeof(*detail_ptr->mc_ptr));
		detail_ptr->mc_ptr->cores_per_socket = NO_VAL16;
		detail_ptr->mc_ptr->ntasks_per_core = 1;
		detail_ptr->mc_ptr->ntasks_per_socket = INFINITE16;
		detail_ptr->mc_ptr->plane_size = 0;
		detail_ptr->mc_ptr->sockets_per_node = NO_VAL16;
		detail_ptr->mc_ptr->threads_per_core = 1;

		detail_ptr->num_tasks = detail_ptr->min_cpus =
			resv_desc_ptr->core_cnt;
		if (detail_ptr->min_cpus == NO_VAL)
			detail_ptr->min_cpus = detail_ptr->min_nodes;
	} else {
		detail_ptr->num_tasks = detail_ptr->min_cpus =
			detail_ptr->min_nodes;
		detail_ptr->whole_node = WHOLE_NODE_REQUIRED;
	}
	detail_ptr->core_spec = NO_VAL16;
	detail_ptr->cpus_per_task = 1;
	detail_ptr->orig_min_cpus = detail_ptr->min_cpus;
	detail_ptr->orig_max_cpus = detail_ptr->max_cpus = NO_VAL;
	if ((resv_desc_ptr->flags & RESERVE_TRES_PER_NODE) &&
	    (resv_desc_ptr->core_cnt != NO_VAL) &&
	    (resv_desc_ptr->node_cnt != NO_VAL)) {
		detail_ptr->orig_pn_min_cpus = detail_ptr->pn_min_cpus =
			resv_desc_ptr->core_cnt / resv_desc_ptr->node_cnt;
	} else
		detail_ptr->orig_pn_min_cpus = detail_ptr->pn_min_cpus = 1;
	detail_ptr->features = xstrdup(resv_desc_ptr->features);

	if (build_feature_list(job_ptr, false, true)) {
		error("%s: invalid features(%s) for reservation given",
		      __func__, detail_ptr->features);
	}

	detail_ptr->task_dist = SLURM_DIST_BLOCK;
	job_ptr->best_switch = true;

	if (resv_desc_ptr->tres_str) {
		gres_job_state_validate_t gres_js_val = {
			.cpus_per_tres = NULL,
			.mem_per_tres = NULL,
			.tres_freq = NULL,
			.tres_per_socket = NULL,
			.tres_per_task = NULL,

			.cpus_per_task = &detail_ptr->orig_cpus_per_task,
			.max_nodes = &detail_ptr->max_nodes,
			.min_cpus = &detail_ptr->min_cpus,
			.min_nodes = &detail_ptr->min_nodes,
			.ntasks_per_node = &detail_ptr->ntasks_per_node,
			.ntasks_per_socket =
			&detail_ptr->mc_ptr->ntasks_per_socket,
			.ntasks_per_tres = &detail_ptr->ntasks_per_tres,
			.num_tasks = &detail_ptr->num_tasks,
			.sockets_per_node =
			&detail_ptr->mc_ptr->sockets_per_node,

			.gres_list = &job_ptr->gres_list_req,
		};

		detail_ptr->mc_ptr->ntasks_per_socket = NO_VAL16;
		detail_ptr->mc_ptr->sockets_per_node = NO_VAL16;
		detail_ptr->orig_cpus_per_task = NO_VAL16;
		detail_ptr->ntasks_per_tres = NO_VAL16;

		job_ptr->tres_req_str = xstrdup(resv_desc_ptr->tres_str);

		if (resv_desc_ptr->flags & RESERVE_TRES_PER_NODE)
			job_ptr->tres_per_node = xstrdup(job_ptr->tres_req_str);
		else
			job_ptr->tres_per_job = xstrdup(job_ptr->tres_req_str);

		gres_js_val.tres_per_job = job_ptr->tres_per_job;
		gres_js_val.tres_per_node = job_ptr->tres_per_node;

		(void)gres_job_state_validate(&gres_js_val);

		if (detail_ptr->num_tasks == NO_VAL)
			detail_ptr->num_tasks = 0;
		if (detail_ptr->min_cpus == NO_VAL)
			detail_ptr->min_cpus = 1;

		if (resv_desc_ptr->flags & RESERVE_TRES_PER_NODE)
			detail_ptr->ntasks_per_node = detail_ptr->pn_min_cpus;
		else if (detail_ptr->ntasks_per_node == NO_VAL16)
			detail_ptr->ntasks_per_node = 0;

		if (detail_ptr->mc_ptr->ntasks_per_socket == NO_VAL16)
			detail_ptr->mc_ptr->ntasks_per_socket = INFINITE16;
		if (job_ptr->gres_list_req)
			job_ptr->bit_flags |= GRES_ENFORCE_BIND;
		gres_job_state_log(job_ptr->gres_list_req, job_ptr->job_id);
	}
	return job_ptr;
}

extern uint16_t job_mgr_determine_cpus_per_core(
	job_details_t *details, int node_inx)
{
	uint16_t ncpus_per_core = INFINITE16;	/* Usable CPUs per core */
	uint16_t threads_per_core = node_record_table_ptr[node_inx]->tpc;

	if ((slurm_conf.select_type_param & CR_ONE_TASK_PER_CORE) &&
	    (details->min_gres_cpu > 0)) {
		/* May override default of 1 CPU per core */
		return node_record_table_ptr[node_inx]->tpc;
	}

	if (details && details->mc_ptr) {
		multi_core_data_t *mc_ptr = details->mc_ptr;
		if ((mc_ptr->ntasks_per_core != INFINITE16) &&
		    (mc_ptr->ntasks_per_core)) {
			ncpus_per_core = MIN(threads_per_core,
					     (mc_ptr->ntasks_per_core *
					      details->cpus_per_task));
		}
		if ((mc_ptr->threads_per_core != NO_VAL16) &&
		    (mc_ptr->threads_per_core <  ncpus_per_core)) {
			ncpus_per_core = mc_ptr->threads_per_core;
		}
	}

	threads_per_core = MIN(threads_per_core, ncpus_per_core);

	return threads_per_core;
}

static int _sort_part_lists(void *x, void *none)
{
	job_record_t *job_ptr = x;
	if (job_ptr && job_ptr->part_ptr_list)
		list_sort(job_ptr->part_ptr_list, priority_sort_part_tier);
	return SLURM_SUCCESS;
}

extern void sort_all_jobs_partition_lists()
{
	list_for_each(job_list, _sort_part_lists, NULL);
}

extern void job_mgr_handle_cred_failure(job_record_t *job_ptr)
{
	job_ptr->priority = 0; /* Hold job */
	xfree(job_ptr->system_comment);
	job_ptr->system_comment =
		xstrdup("slurm_cred_create failure, holding job.");
	job_complete(job_ptr->job_id, slurm_conf.slurm_user_id, true, false, 0);
}
