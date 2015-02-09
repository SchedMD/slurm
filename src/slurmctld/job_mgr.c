/*****************************************************************************\
 *  job_mgr.c - manage the job information of slurm
 *	Note: there is a global job list (job_list), time stamp
 *	(last_job_update), and hash table (job_hash)
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Portions Copyright (C) 2010-2015 SchedMD <http://www.schedmd.com>.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/param.h>
#include <unistd.h>

#include "slurm/slurm_errno.h"

#include "src/common/slurm_acct_gather.h"
#include "src/common/assoc_mgr.h"
#include "src/common/bitstring.h"
#include "src/common/fd.h"
#include "src/common/forward.h"
#include "src/common/gres.h"
#include "src/common/hostlist.h"
#include "src/common/node_select.h"
#include "src/common/parse_time.h"
#include "src/common/slurm_accounting_storage.h"
#include "src/common/slurm_jobcomp.h"
#include "src/common/slurm_priority.h"
#include "src/common/slurm_protocol_pack.h"
#include "src/common/switch.h"
#include "src/common/timers.h"
#include "src/common/xassert.h"
#include "src/common/xstring.h"

#include "src/slurmctld/acct_policy.h"
#include "src/slurmctld/agent.h"
#include "src/slurmctld/front_end.h"
#include "src/slurmctld/job_scheduler.h"
#include "src/slurmctld/job_submit.h"
#include "src/slurmctld/licenses.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/node_scheduler.h"
#include "src/slurmctld/preempt.h"
#include "src/slurmctld/proc_req.h"
#include "src/slurmctld/reservation.h"
#include "src/slurmctld/sched_plugin.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/slurmctld_plugstack.h"
#include "src/slurmctld/srun_comm.h"
#include "src/slurmctld/state_save.h"
#include "src/slurmctld/trigger_mgr.h"

#define ARRAY_ID_BUF_SIZE 32
#define DETAILS_FLAG 0xdddd
#define SLURM_CREATE_JOB_FLAG_NO_ALLOCATE_0 0
#define STEP_FLAG 0xbbbb
#define TOP_PRIORITY 0xffff0000	/* large, but leave headroom for higher */

#define JOB_HASH_INX(_job_id)	(_job_id % hash_table_size)
#define JOB_ARRAY_HASH_INX(_job_id, _task_id) \
	((_job_id + _task_id) % hash_table_size)

/* Change JOB_STATE_VERSION value when changing the state save format */
#define JOB_STATE_VERSION       "PROTOCOL_VERSION"
#define JOB_2_6_STATE_VERSION   "VER014"	/* SLURM version 2.6 */

#define JOB_CKPT_VERSION      "PROTOCOL_VERSION"

typedef struct {
	int resp_array_cnt;
	int resp_array_size;
	uint32_t *resp_array_rc;
	bitstr_t **resp_array_task_id;
} resp_array_struct_t;

/* Global variables */
List   job_list = NULL;		/* job_record list */
time_t last_job_update;		/* time of last update to job records */

/* Local variables */
static uint32_t highest_prio = 0;
static uint32_t lowest_prio  = TOP_PRIORITY;
static int      hash_table_size = 0;
static int      job_count = 0;		/* job's in the system */
static uint32_t job_id_sequence = 0;	/* first job_id to assign new job */
static struct   job_record **job_hash = NULL;
static struct   job_record **job_array_hash_j = NULL;
static struct   job_record **job_array_hash_t = NULL;
static time_t   last_file_write_time = (time_t) 0;
static uint32_t max_array_size = NO_VAL;
static int	select_serial = -1;
static bool     wiki_sched = false;
static bool     wiki2_sched = false;
static bool     wiki_sched_test = false;
static uint32_t num_exit;
static int32_t  *requeue_exit;
static uint32_t num_hold;
static int32_t  *requeue_exit_hold;
static bool     kill_invalid_dep;

/* Local functions */
static void _add_job_hash(struct job_record *job_ptr);
static void _add_job_array_hash(struct job_record *job_ptr);
static int  _checkpoint_job_record (struct job_record *job_ptr,
				    char *image_dir);
static int  _copy_job_desc_files(uint32_t job_id_src, uint32_t job_id_dest);
static int  _copy_job_desc_to_file(job_desc_msg_t * job_desc,
				   uint32_t job_id);
static int  _copy_job_desc_to_job_record(job_desc_msg_t * job_desc,
					 struct job_record **job_ptr,
					 bitstr_t ** exc_bitmap,
					 bitstr_t ** req_bitmap);
static int _copy_job_file(const char *src, const char *dst);
static job_desc_msg_t * _copy_job_record_to_job_desc(
				struct job_record *job_ptr);
static char *_copy_nodelist_no_dup(char *node_list);
static struct job_record *_create_job_record(int *error_code,
					     uint32_t num_jobs);
static void _del_batch_list_rec(void *x);
static void _delete_job_desc_files(uint32_t job_id);
static slurmdb_qos_rec_t *_determine_and_validate_qos(
	char *resv_name, slurmdb_association_rec_t *assoc_ptr,
	bool admin, slurmdb_qos_rec_t *qos_rec,	int *error_code);
static void _dump_job_details(struct job_details *detail_ptr,
			      Buf buffer);
static void _dump_job_state(struct job_record *dump_job_ptr, Buf buffer);
static int  _find_batch_dir(void *x, void *key);
static void _get_batch_job_dir_ids(List batch_dirs);
static time_t _get_last_state_write_time(void);
static struct job_record *_job_rec_copy(struct job_record *job_ptr);
static void _job_timed_out(struct job_record *job_ptr);
static int  _job_create(job_desc_msg_t * job_specs, int allocate, int will_run,
			struct job_record **job_rec_ptr, uid_t submit_uid,
			char **err_msg, uint16_t protocol_version);
static void _list_delete_job(void *job_entry);
static int  _list_find_job_id(void *job_entry, void *key);
static int  _list_find_job_old(void *job_entry, void *key);
static int  _load_job_details(struct job_record *job_ptr, Buf buffer,
			      uint16_t protocol_version);
static int  _load_job_state(Buf buffer,	uint16_t protocol_version);
static int32_t *_make_requeue_array(char *conf_buf, uint32_t *num);
static uint32_t _max_switch_wait(uint32_t input_wait);
static void _notify_srun_missing_step(struct job_record *job_ptr, int node_inx,
				      time_t now, time_t node_boot_time);
static int  _open_job_state_file(char **state_file);
static void _pack_job_for_ckpt (struct job_record *job_ptr, Buf buffer);
static void _pack_default_job_details(struct job_record *job_ptr,
				      Buf buffer,
				      uint16_t protocol_version);
static void _pack_pending_job_details(struct job_details *detail_ptr,
				      Buf buffer,
				      uint16_t protocol_version);
static bool _parse_array_tok(char *tok, bitstr_t *array_bitmap, uint32_t max);
static int  _purge_job_record(uint32_t job_id);
static void _purge_missing_jobs(int node_inx, time_t now);
static int  _read_data_array_from_file(char *file_name, char ***data,
				       uint32_t * size,
				       struct job_record *job_ptr);
static int  _read_data_from_file(char *file_name, char **data);
static char *_read_job_ckpt_file(char *ckpt_file, int *size_ptr);
static void _remove_defunct_batch_dirs(List batch_dirs);
static void _remove_job_hash(struct job_record *job_ptr);
static int  _reset_detail_bitmaps(struct job_record *job_ptr);
static void _reset_step_bitmaps(struct job_record *job_ptr);
static void _resp_array_add(resp_array_struct_t **resp,
			    struct job_record *job_ptr, uint32_t rc);
static void _resp_array_add_id(resp_array_struct_t **resp, uint32_t job_id,
			       uint32_t task_id, uint32_t rc);
static void _resp_array_free(resp_array_struct_t *resp);
static job_array_resp_msg_t *_resp_array_xlate(resp_array_struct_t *resp,
					       uint32_t job_id);
static int  _resume_job_nodes(struct job_record *job_ptr, bool indf_susp);
static void _send_job_kill(struct job_record *job_ptr);
static int  _set_job_id(struct job_record *job_ptr);
static void  _set_job_requeue_exit_value(struct job_record *job_ptr);
static void _signal_batch_job(struct job_record *job_ptr,
			      uint16_t signal,
			      uint16_t flags);
static void _signal_job(struct job_record *job_ptr, int signal);
static void _suspend_job(struct job_record *job_ptr, uint16_t op,
			 bool indf_susp);
static int  _suspend_job_nodes(struct job_record *job_ptr, bool indf_susp);
static bool _top_priority(struct job_record *job_ptr);
static int  _valid_job_part(job_desc_msg_t * job_desc,
			    uid_t submit_uid, bitstr_t *req_bitmap,
			    struct part_record **part_pptr,
			    List part_ptr_list,
			    slurmdb_association_rec_t *assoc_ptr,
			    slurmdb_qos_rec_t *qos_ptr);
static int  _validate_job_desc(job_desc_msg_t * job_desc_msg, int allocate,
                               uid_t submit_uid, struct part_record *part_ptr,
                               List part_list);
static void _validate_job_files(List batch_dirs);
static bool _validate_min_mem_partition(job_desc_msg_t *job_desc_msg,
                                        struct part_record *,
                                        List part_list);
static int  _write_data_to_file(char *file_name, char *data);
static int  _write_data_array_to_file(char *file_name, char **data,
				      uint32_t size);
static void _xmit_new_end_time(struct job_record *job_ptr);
static void _kill_dependent(struct job_record *);

/*
 * Functions used to manage job array responses with a separate return code
 * possible for each task ID
 */
/* Add job record to resp_array_struct_t, free with _resp_array_free() */
static void _resp_array_add(resp_array_struct_t **resp,
			    struct job_record *job_ptr, uint32_t rc)
{
	slurm_ctl_conf_t *conf;
	resp_array_struct_t *loc_resp;
	int array_size;
	int i;

	if ((job_ptr->array_task_id == NO_VAL) &&
	    (job_ptr->array_recs == NULL)) {
		error("_resp_array_add called for non-job array %u",
		      job_ptr->job_id);
		return;
	}

	if (max_array_size == NO_VAL) {
		conf = slurm_conf_lock();
		max_array_size = conf->max_array_sz;
		slurm_conf_unlock();
	}

	xassert(resp);
	if (*resp == NULL) {
		/* Initialize the data structure */
		loc_resp = xmalloc(sizeof(resp_array_struct_t));
		loc_resp->resp_array_cnt  = 0;
		loc_resp->resp_array_size = 10;
		xrealloc(loc_resp->resp_array_rc,
			 (sizeof(uint32_t) * loc_resp->resp_array_size));
		xrealloc(loc_resp->resp_array_task_id,
			 (sizeof(bitstr_t *) * loc_resp->resp_array_size));
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
				error("_resp_array_add found invalid "
				      "task id %u_%u",
				      job_ptr->array_job_id,
				      job_ptr->array_task_id);
			}
		} else if (job_ptr->array_recs &&
			   job_ptr->array_recs->task_id_bitmap) {
			array_size = bit_size(job_ptr->array_recs->
					      task_id_bitmap);
			if (bit_size(loc_resp->resp_array_task_id[i]) !=
			    array_size) {
				loc_resp->resp_array_task_id[i] = bit_realloc(
					loc_resp->resp_array_task_id[i],
					array_size);
			}
			bit_or(loc_resp->resp_array_task_id[i],
			       job_ptr->array_recs->task_id_bitmap);
		} else {
			error("_resp_array_add found job %u without task ID "
			      "or bitmap", job_ptr->job_id);
		}
		return;
	}

	/* Need to add a new record for this error code */
	if (loc_resp->resp_array_cnt >= loc_resp->resp_array_size) {
		/* Need to grow the table size */
		loc_resp->resp_array_size += 10;
		xrealloc(loc_resp->resp_array_rc,
			 (sizeof(uint32_t) * loc_resp->resp_array_size));
		xrealloc(loc_resp->resp_array_task_id,
			 (sizeof(bitstr_t *) * loc_resp->resp_array_size));
	}

	loc_resp->resp_array_rc[loc_resp->resp_array_cnt] = rc;
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
		error("_resp_array_add found job %u without task ID or bitmap",
		      job_ptr->job_id);
		loc_resp->resp_array_task_id[loc_resp->resp_array_cnt] =
				bit_alloc(max_array_size);
	}
	loc_resp->resp_array_cnt++;
	return;
}
/* Add record to resp_array_struct_t, free with _resp_array_free().
 * This is a variant of _resp_array_add for the case where a job/task ID
 * is not found, so we use a dummy job record based upon the input IDs. */
static void _resp_array_add_id(resp_array_struct_t **resp, uint32_t job_id,
			       uint32_t task_id, uint32_t rc)
{
	struct job_record job_ptr;

	job_ptr.job_id = job_id;
	job_ptr.array_job_id = job_id;
	job_ptr.array_task_id = task_id;
	job_ptr.array_recs = NULL;
	_resp_array_add(resp, &job_ptr, rc);
}

/* Free resp_array_struct_t built by _resp_array_add() */
static void _resp_array_free(resp_array_struct_t *resp)
{
	int i;

	if (resp) {
		for (i = 0; i < resp->resp_array_cnt; i++)
			FREE_NULL_BITMAP(resp->resp_array_task_id[i]);
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

	ffs   = xmalloc(sizeof(int) * resp->resp_array_cnt);
	for (i = 0; i < resp->resp_array_cnt; i++) {
		ffs[i] = bit_ffs(resp->resp_array_task_id[i]);
	}

	msg = xmalloc(sizeof(job_array_resp_msg_t));
	msg->job_array_count = resp->resp_array_cnt;
	msg->job_array_id    = xmalloc(sizeof(char *)   * resp->resp_array_cnt);
	msg->error_code      = xmalloc(sizeof(uint32_t) * resp->resp_array_cnt);
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

/*
 * _create_job_record - create an empty job_record including job_details.
 *	load its values with defaults (zeros, nulls, and magic cookie)
 * OUT error_code - set to zero if no error, errno otherwise
 * IN num_jobs - number of jobs this record should represent
 *    = 0 - split out a job array record to its own job record
 *    = 1 - simple job OR job array with one task
 *    > 1 - job array create with the task count as num_jobs
 * RET pointer to the record or NULL if error
 * NOTE: allocates memory that should be xfreed with _list_delete_job
 */
static struct job_record *_create_job_record(int *error_code, uint32_t num_jobs)
{
	struct job_record  *job_ptr;
	struct job_details *detail_ptr;

	if ((job_count + num_jobs) >= slurmctld_conf.max_job_cnt) {
		error("_create_job_record: MaxJobCount reached (%u)",
		      slurmctld_conf.max_job_cnt);
	}

	job_count += num_jobs;
	*error_code = 0;
	last_job_update = time(NULL);

	job_ptr    = (struct job_record *) xmalloc(sizeof(struct job_record));
	detail_ptr = (struct job_details *)xmalloc(sizeof(struct job_details));

	job_ptr->magic = JOB_MAGIC;
	job_ptr->array_task_id = NO_VAL;
	job_ptr->details = detail_ptr;
	job_ptr->prio_factors = xmalloc(sizeof(priority_factors_object_t));
	job_ptr->step_list = list_create(NULL);

	xassert (detail_ptr->magic = DETAILS_MAGIC); /* set value */
	detail_ptr->submit_time = time(NULL);
	job_ptr->requid = -1; /* force to -1 for sacct to know this
			       * hasn't been set yet  */
	(void) list_append(job_list, job_ptr);

	return job_ptr;
}


/*
 * delete_job_details - delete a job's detail record and clear it's pointer
 *	this information can be deleted as soon as the job is allocated
 *	resources and running (could need to restart batch job)
 * IN job_entry - pointer to job_record to clear the record of
 */
void delete_job_details(struct job_record *job_entry)
{
	int i;

	if (job_entry->details == NULL)
		return;

	xassert (job_entry->details->magic == DETAILS_MAGIC);
	if (IS_JOB_FINISHED(job_entry))
		_delete_job_desc_files(job_entry->job_id);

	xfree(job_entry->details->acctg_freq);
	for (i=0; i<job_entry->details->argc; i++)
		xfree(job_entry->details->argv[i]);
	xfree(job_entry->details->argv);
	xfree(job_entry->details->ckpt_dir);
	xfree(job_entry->details->cpu_bind);
	if (job_entry->details->depend_list)
		list_destroy(job_entry->details->depend_list);
	xfree(job_entry->details->dependency);
	xfree(job_entry->details->orig_dependency);
	for (i=0; i<job_entry->details->env_cnt; i++)
		xfree(job_entry->details->env_sup[i]);
	xfree(job_entry->details->env_sup);
	xfree(job_entry->details->std_err);
	FREE_NULL_BITMAP(job_entry->details->exc_node_bitmap);
	xfree(job_entry->details->exc_nodes);
	if (job_entry->details->feature_list)
		list_destroy(job_entry->details->feature_list);
	xfree(job_entry->details->features);
	xfree(job_entry->details->std_in);
	xfree(job_entry->details->mc_ptr);
	xfree(job_entry->details->mem_bind);
	xfree(job_entry->details->std_out);
	FREE_NULL_BITMAP(job_entry->details->req_node_bitmap);
	xfree(job_entry->details->req_node_layout);
	xfree(job_entry->details->req_nodes);
	xfree(job_entry->details->restart_dir);
	xfree(job_entry->details->work_dir);
	xfree(job_entry->details);	/* Must be last */
}

/* _delete_job_desc_files - delete job descriptor related files */
static void _delete_job_desc_files(uint32_t job_id)
{
	char *dir_name = NULL, *file_name;
	struct stat sbuf;
	int hash = job_id % 10, stat_rc;

	dir_name = slurm_get_state_save_location();
	xstrfmtcat(dir_name, "/hash.%d/job.%u", hash, job_id);
	stat_rc = stat(dir_name, &sbuf);
	if (stat_rc != 0) {
		/* Read version 14.03 or earlier state format */
		xfree(dir_name);
		dir_name = slurm_get_state_save_location();
		xstrfmtcat(dir_name, "/job.%u", job_id);
		stat_rc = stat(dir_name, &sbuf);
		if (stat_rc != 0) {
			xfree(dir_name);
			return;
		}
	}

	file_name = xstrdup(dir_name);
	xstrcat(file_name, "/environment");
	(void) unlink(file_name);
	xfree(file_name);

	file_name = xstrdup(dir_name);
	xstrcat(file_name, "/script");
	(void) unlink(file_name);
	xfree(file_name);

	(void) rmdir(dir_name);
	xfree(dir_name);
}

static uint32_t _max_switch_wait(uint32_t input_wait)
{
	static time_t sched_update = 0;
	static uint32_t max_wait = 300;	/* default max_switch_wait, seconds */
	char *sched_params, *tmp_ptr;
	int i;

	if (sched_update != slurmctld_conf.last_update) {
		sched_params = slurm_get_sched_params();
		if (sched_params &&
		    (tmp_ptr = strstr(sched_params, "max_switch_wait="))) {
		/*                                   0123456789012345 */
			i = atoi(tmp_ptr + 16);
			if (i < 0) {
				error("ignoring SchedulerParameters: "
				      "max_switch_wait of %d", i);
			} else {
				max_wait = i;
			}
		}
		xfree(sched_params);
	}

	if (max_wait > input_wait)
		return input_wait;
	return max_wait;
}

static slurmdb_qos_rec_t *_determine_and_validate_qos(
	char *resv_name, slurmdb_association_rec_t *assoc_ptr,
	bool admin, slurmdb_qos_rec_t *qos_rec, int *error_code)
{
	slurmdb_qos_rec_t *qos_ptr = NULL;

	/* If enforcing associations make sure this is a valid qos
	   with the association.  If not just fill in the qos and
	   continue. */

	xassert(qos_rec);

	if (!qos_rec->name && !qos_rec->id) {
		if (assoc_ptr && assoc_ptr->usage->valid_qos) {
			if (assoc_ptr->def_qos_id)
				qos_rec->id = assoc_ptr->def_qos_id;
			else if (bit_set_count(assoc_ptr->usage->valid_qos)
				 == 1)
				qos_rec->id =
					bit_ffs(assoc_ptr->usage->valid_qos);
			else if (assoc_mgr_root_assoc
				 && assoc_mgr_root_assoc->def_qos_id)
				qos_rec->id = assoc_mgr_root_assoc->def_qos_id;
			else
				qos_rec->name = "normal";
		} else if (assoc_mgr_root_assoc
			   && assoc_mgr_root_assoc->def_qos_id)
			qos_rec->id = assoc_mgr_root_assoc->def_qos_id;
		else
			qos_rec->name = "normal";
	}

	if (assoc_mgr_fill_in_qos(acct_db_conn, qos_rec, accounting_enforce,
				  &qos_ptr, 0) != SLURM_SUCCESS) {
		error("Invalid qos (%s)", qos_rec->name);
		*error_code = ESLURM_INVALID_QOS;
		return NULL;
	}

	if ((accounting_enforce & ACCOUNTING_ENFORCE_QOS)
	    && assoc_ptr
	    && !admin
	    && (!assoc_ptr->usage->valid_qos
		|| !bit_test(assoc_ptr->usage->valid_qos, qos_rec->id))) {
		error("This association %d(account='%s', "
		      "user='%s', partition='%s') does not have "
		      "access to qos %s",
		      assoc_ptr->id, assoc_ptr->acct, assoc_ptr->user,
		      assoc_ptr->partition, qos_rec->name);
		*error_code = ESLURM_INVALID_QOS;
		return NULL;
	}

	if (qos_ptr && (qos_ptr->flags & QOS_FLAG_REQ_RESV)
	    && (!resv_name || resv_name[0] == '\0')) {
		error("qos %s can only be used in a reservation",
		      qos_rec->name);
		*error_code = ESLURM_INVALID_QOS;
		return NULL;
	}

	*error_code = SLURM_SUCCESS;
	return qos_ptr;
}

/*
 * dump_all_job_state - save the state of all jobs to file for checkpoint
 *	Changes here should be reflected in load_last_job_id() and
 *	load_all_job_state().
 * RET 0 or error code */
int dump_all_job_state(void)
{
	/* Save high-water mark to avoid buffer growth with copies */
	static int high_buffer_size = (1024 * 1024);
	int error_code = SLURM_SUCCESS, log_fd;
	char *old_file, *new_file, *reg_file;
	struct stat stat_buf;
	/* Locks: Read config and job */
	slurmctld_lock_t job_read_lock =
		{ READ_LOCK, READ_LOCK, NO_LOCK, NO_LOCK };
	ListIterator job_iterator;
	struct job_record *job_ptr;
	Buf buffer = init_buf(high_buffer_size);
	time_t min_age = 0, now = time(NULL);
	time_t last_state_file_time;
	DEF_TIMERS;

	START_TIMER;
	/* Check that last state file was written at expected time.
	 * This is a check for two slurmctld daemons running at the same
	 * time in primary mode (a split-brain problem). */
	last_state_file_time = _get_last_state_write_time();
	if (last_file_write_time && last_state_file_time &&
	    (last_file_write_time != last_state_file_time)) {
		error("Bad job state save file time. We wrote it at time %u, "
		      "but the file contains a time stamp of %u.",
		      (uint32_t) last_file_write_time,
		      (uint32_t) last_state_file_time);
		if (slurmctld_primary == 0) {
			fatal("Two slurmctld daemons are running as primary. "
			      "Shutting down this daemon to avoid inconsistent "
			      "state due to split brain.");
		}
	}

	/* write header: version, time */
	packstr(JOB_STATE_VERSION, buffer);
	pack16(SLURM_PROTOCOL_VERSION, buffer);
	pack_time(now, buffer);

	if (slurmctld_conf.min_job_age > 0)
		min_age = now  - slurmctld_conf.min_job_age;

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
	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		xassert (job_ptr->magic == JOB_MAGIC);
		if ((min_age > 0) && (job_ptr->end_time < min_age) &&
		    (! IS_JOB_COMPLETING(job_ptr)) && IS_JOB_FINISHED(job_ptr))
			continue;	/* job ready for purging, don't dump */

		_dump_job_state(job_ptr, buffer);
	}
	list_iterator_destroy(job_iterator);

	/* write the buffer to file */
	old_file = xstrdup(slurmctld_conf.state_save_location);
	xstrcat(old_file, "/job_state.old");
	reg_file = xstrdup(slurmctld_conf.state_save_location);
	xstrcat(reg_file, "/job_state");
	new_file = xstrdup(slurmctld_conf.state_save_location);
	xstrcat(new_file, "/job_state.new");
	unlock_slurmctld(job_read_lock);

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

	lock_state_files();
	log_fd = creat(new_file, 0600);
	if (log_fd < 0) {
		error("Can't save state, create file %s error %m",
		      new_file);
		error_code = errno;
	} else {
		int pos = 0, nwrite, amount, rc;
		char *data;

		fd_set_close_on_exec(log_fd);
		nwrite = get_buf_offset(buffer);
		data = (char *)get_buf_data(buffer);
		high_buffer_size = MAX(nwrite, high_buffer_size);
		while (nwrite > 0) {
			amount = write(log_fd, &data[pos], nwrite);
			if ((amount < 0) && (errno != EINTR)) {
				error("Error writing file %s, %m", new_file);
				error_code = errno;
				break;
			}
			nwrite -= amount;
			pos    += amount;
		}

		rc = fsync_and_close(log_fd, "job");
		if (rc && !error_code)
			error_code = rc;
	}
	if (error_code)
		(void) unlink(new_file);
	else {			/* file shuffle */
		(void) unlink(old_file);
		if (link(reg_file, old_file))
			debug4("unable to create link for %s -> %s: %m",
			       reg_file, old_file);
		(void) unlink(reg_file);
		if (link(new_file, reg_file))
			debug4("unable to create link for %s -> %s: %m",
			       new_file, reg_file);
		(void) unlink(new_file);
		last_file_write_time = now;
	}
	xfree(old_file);
	xfree(reg_file);
	xfree(new_file);
	unlock_state_files();

	free_buf(buffer);
	END_TIMER2("dump_all_job_state");
	return error_code;
}

/* Open the job state save file, or backup if necessary.
 * state_file IN - the name of the state save file used
 * RET the file description to read from or error code
 */
static int _open_job_state_file(char **state_file)
{
	int state_fd;
	struct stat stat_buf;

	*state_file = slurm_get_state_save_location();
	xstrcat(*state_file, "/job_state");
	state_fd = open(*state_file, O_RDONLY);
	if (state_fd < 0) {
		error("Could not open job state file %s: %m", *state_file);
	} else if (fstat(state_fd, &stat_buf) < 0) {
		error("Could not stat job state file %s: %m", *state_file);
		(void) close(state_fd);
	} else if (stat_buf.st_size < 10) {
		error("Job state file %s too small", *state_file);
		(void) close(state_fd);
	} else 	/* Success */
		return state_fd;

	error("NOTE: Trying backup state save file. Jobs may be lost!");
	xstrcat(*state_file, ".old");
	state_fd = open(*state_file, O_RDONLY);
	return state_fd;
}

/* Note that the backup slurmctld has assumed primary control.
 * This function can be called multiple times. */
extern void backup_slurmctld_restart(void)
{
	last_file_write_time = (time_t) 0;
}

/* Return the time stamp in the current job state save file */
static time_t _get_last_state_write_time(void)
{
	int data_allocated, data_read = 0, error_code = SLURM_SUCCESS;
	uint32_t data_size = 0;
	int state_fd;
	char *data, *state_file;
	Buf buffer;
	time_t buf_time = (time_t) 0;
	char *ver_str = NULL;
	uint32_t ver_str_len;
	uint16_t protocol_version = (uint16_t)NO_VAL;

	/* read the file */
	state_fd = _open_job_state_file(&state_file);
	if (state_fd < 0) {
		info("No job state file (%s) found", state_file);
		error_code = ENOENT;
	} else {
		data_allocated = 128;
		data = xmalloc(data_allocated);
		while (1) {
			data_read = read(state_fd, &data[data_size],
					 (data_allocated - data_size));
			if (data_read < 0) {
				if (errno == EINTR)
					continue;
				else {
					error("Read error on %s: %m",
					      state_file);
					break;
				}
			} else if (data_read == 0)	/* eof */
				break;
			data_size += data_read;
			if (data_size >= 128)
				break;
		}
		close(state_fd);
	}
	xfree(state_file);
	if (error_code)
		return error_code;

	buffer = create_buf(data, data_size);
	safe_unpackstr_xmalloc(&ver_str, &ver_str_len, buffer);
	if (ver_str) {
		if (!strcmp(ver_str, JOB_STATE_VERSION))
			safe_unpack16(&protocol_version, buffer);
		else if (!strcmp(ver_str, JOB_2_6_STATE_VERSION))
			protocol_version = SLURM_2_6_PROTOCOL_VERSION;
	}
	safe_unpack_time(&buf_time, buffer);

unpack_error:
	xfree(ver_str);
	free_buf(buffer);
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
	int data_allocated, data_read = 0, error_code = SLURM_SUCCESS;
	uint32_t data_size = 0;
	int state_fd, job_cnt = 0;
	char *data = NULL, *state_file;
	Buf buffer;
	time_t buf_time;
	uint32_t saved_job_id;
	char *ver_str = NULL;
	uint32_t ver_str_len;
	uint16_t protocol_version = (uint16_t)NO_VAL;

	/* read the file */
	lock_state_files();
	state_fd = _open_job_state_file(&state_file);
	if (state_fd < 0) {
		info("No job state file (%s) to recover", state_file);
		error_code = ENOENT;
	} else {
		data_allocated = BUF_SIZE;
		data = xmalloc(data_allocated);
		while (1) {
			data_read = read(state_fd, &data[data_size],
					 BUF_SIZE);
			if (data_read < 0) {
				if (errno == EINTR)
					continue;
				else {
					error("Read error on %s: %m",
					      state_file);
					break;
				}
			} else if (data_read == 0)	/* eof */
				break;
			data_size      += data_read;
			data_allocated += data_read;
			xrealloc(data, data_allocated);
		}
		close(state_fd);
	}
	xfree(state_file);
	unlock_state_files();

	job_id_sequence = MAX(job_id_sequence, slurmctld_conf.first_job_id);
	if (error_code)
		return error_code;

	buffer = create_buf(data, data_size);
	safe_unpackstr_xmalloc(&ver_str, &ver_str_len, buffer);
	debug3("Version string in job_state header is %s", ver_str);
	if (ver_str) {
		if (!strcmp(ver_str, JOB_STATE_VERSION))
			safe_unpack16(&protocol_version, buffer);
		else if (!strcmp(ver_str, JOB_2_6_STATE_VERSION))
			protocol_version = SLURM_2_6_PROTOCOL_VERSION;
	}

	if (protocol_version == (uint16_t)NO_VAL) {
		error("***********************************************");
		error("Can not recover job state, incompatible version");
		error("***********************************************");
		xfree(ver_str);
		free_buf(buffer);
		return EFAULT;
	}
	xfree(ver_str);

	safe_unpack_time(&buf_time, buffer);
	safe_unpack32( &saved_job_id, buffer);
	job_id_sequence = MAX(saved_job_id, job_id_sequence);
	debug3("Job id in job_state header is %u", saved_job_id);

	while (remaining_buf(buffer) > 0) {
		error_code = _load_job_state(buffer, protocol_version);
		if (error_code != SLURM_SUCCESS)
			goto unpack_error;
		job_cnt++;
	}
	debug3("Set job_id_sequence to %u", job_id_sequence);

	free_buf(buffer);
	info("Recovered information about %d jobs", job_cnt);
	return error_code;

unpack_error:
	error("Incomplete job data checkpoint file");
	info("Recovered information about %d jobs", job_cnt);
	free_buf(buffer);
	return SLURM_FAILURE;
}

/*
 * load_last_job_id - load only the last job ID from state save file.
 *	Changes here should be reflected in load_all_job_state().
 * RET 0 or error code
 */
extern int load_last_job_id( void )
{
	int data_allocated, data_read = 0, error_code = SLURM_SUCCESS;
	uint32_t data_size = 0;
	int state_fd;
	char *data = NULL, *state_file;
	Buf buffer;
	time_t buf_time;
	char *ver_str = NULL;
	uint32_t ver_str_len;
	uint16_t protocol_version = (uint16_t)NO_VAL;

	/* read the file */
	state_file = slurm_get_state_save_location();
	xstrcat(state_file, "/job_state");
	lock_state_files();
	state_fd = open(state_file, O_RDONLY);
	if (state_fd < 0) {
		debug("No job state file (%s) to recover", state_file);
		error_code = ENOENT;
	} else {
		data_allocated = BUF_SIZE;
		data = xmalloc(data_allocated);
		while (1) {
			data_read = read(state_fd, &data[data_size],
					 BUF_SIZE);
			if (data_read < 0) {
				if (errno == EINTR)
					continue;
				else {
					error("Read error on %s: %m",
					      state_file);
					break;
				}
			} else if (data_read == 0)	/* eof */
				break;
			data_size      += data_read;
			data_allocated += data_read;
			xrealloc(data, data_allocated);
		}
		close(state_fd);
	}
	xfree(state_file);
	unlock_state_files();

	if (error_code)
		return error_code;

	buffer = create_buf(data, data_size);
	safe_unpackstr_xmalloc(&ver_str, &ver_str_len, buffer);
	debug3("Version string in job_state header is %s", ver_str);
	if (ver_str) {
		if (!strcmp(ver_str, JOB_STATE_VERSION))
			safe_unpack16(&protocol_version, buffer);
		else if (!strcmp(ver_str, JOB_2_6_STATE_VERSION))
			protocol_version = SLURM_2_6_PROTOCOL_VERSION;
	}
	xfree(ver_str);

	if (protocol_version == (uint16_t)NO_VAL) {
		debug("*************************************************");
		debug("Can not recover last job ID, incompatible version");
		debug("*************************************************");
		free_buf(buffer);
		return EFAULT;
	}

	safe_unpack_time(&buf_time, buffer);
	safe_unpack32( &job_id_sequence, buffer);
	debug3("Job ID in job_state header is %u", job_id_sequence);

	/* Ignore the state for individual jobs stored here */

	free_buf(buffer);
	return error_code;

unpack_error:
	debug("Invalid job data checkpoint file");
	free_buf(buffer);
	return SLURM_FAILURE;
}

/*
 * _dump_job_state - dump the state of a specific job, its details, and
 *	steps to a buffer
 * IN dump_job_ptr - pointer to job for which information is requested
 * IN/OUT buffer - location to store data, pointers automatically advanced
 */
static void _dump_job_state(struct job_record *dump_job_ptr, Buf buffer)
{
	struct job_details *detail_ptr;
	ListIterator step_iterator;
	struct step_record *step_ptr;
	uint32_t tmp_32;

	/* Dump basic job info */
	pack32(dump_job_ptr->array_job_id, buffer);
	pack32(dump_job_ptr->array_task_id, buffer);
	if (dump_job_ptr->array_recs) {
		build_array_str(dump_job_ptr);
		if (dump_job_ptr->array_recs->task_id_bitmap) {
			tmp_32 = bit_size(dump_job_ptr->array_recs->
					  task_id_bitmap);
		} else
			tmp_32 = 0;
		pack32(tmp_32, buffer);
		if (tmp_32)
			packstr(dump_job_ptr->array_recs->task_id_str, buffer);
		pack32(dump_job_ptr->array_recs->array_flags,    buffer);
		pack32(dump_job_ptr->array_recs->max_run_tasks,  buffer);
		pack32(dump_job_ptr->array_recs->tot_run_tasks,  buffer);
		pack32(dump_job_ptr->array_recs->min_exit_code,  buffer);
		pack32(dump_job_ptr->array_recs->max_exit_code,  buffer);
		pack32(dump_job_ptr->array_recs->tot_comp_tasks, buffer);
	} else {
		tmp_32 = NO_VAL;
		pack32(tmp_32, buffer);
	}
	pack32(dump_job_ptr->assoc_id, buffer);
	pack32(dump_job_ptr->job_id, buffer);
	pack32(dump_job_ptr->user_id, buffer);
	pack32(dump_job_ptr->group_id, buffer);
	pack32(dump_job_ptr->time_limit, buffer);
	pack32(dump_job_ptr->time_min, buffer);
	pack32(dump_job_ptr->priority, buffer);
	pack32(dump_job_ptr->alloc_sid, buffer);
	pack32(dump_job_ptr->total_cpus, buffer);
	if (dump_job_ptr->total_nodes)
		pack32(dump_job_ptr->total_nodes, buffer);
	else
		pack32(dump_job_ptr->node_cnt_wag, buffer);
	pack32(dump_job_ptr->cpu_cnt, buffer);
	pack32(dump_job_ptr->exit_code, buffer);
	pack32(dump_job_ptr->derived_ec, buffer);
	pack32(dump_job_ptr->db_index, buffer);
	pack32(dump_job_ptr->resv_id, buffer);
	pack32(dump_job_ptr->next_step_id, buffer);
	pack32(dump_job_ptr->qos_id, buffer);
	pack32(dump_job_ptr->req_switch, buffer);
	pack32(dump_job_ptr->wait4switch, buffer);
	pack32(dump_job_ptr->profile, buffer);

	pack_time(dump_job_ptr->preempt_time, buffer);
	pack_time(dump_job_ptr->start_time, buffer);
	pack_time(dump_job_ptr->end_time, buffer);
	pack_time(dump_job_ptr->suspend_time, buffer);
	pack_time(dump_job_ptr->pre_sus_time, buffer);
	pack_time(dump_job_ptr->resize_time, buffer);
	pack_time(dump_job_ptr->tot_sus_time, buffer);

	pack16(dump_job_ptr->direct_set_prio, buffer);
	pack16(dump_job_ptr->job_state, buffer);
	pack16(dump_job_ptr->kill_on_node_fail, buffer);
	pack16(dump_job_ptr->batch_flag, buffer);
	pack16(dump_job_ptr->mail_type, buffer);
	pack16(dump_job_ptr->state_reason, buffer);
	pack8(dump_job_ptr->reboot, buffer);
	pack16(dump_job_ptr->restart_cnt, buffer);
	pack16(dump_job_ptr->wait_all_nodes, buffer);
	pack16(dump_job_ptr->warn_flags, buffer);
	pack16(dump_job_ptr->warn_signal, buffer);
	pack16(dump_job_ptr->warn_time, buffer);
	pack16(dump_job_ptr->limit_set_max_cpus, buffer);
	pack16(dump_job_ptr->limit_set_max_nodes, buffer);
	pack16(dump_job_ptr->limit_set_min_cpus, buffer);
	pack16(dump_job_ptr->limit_set_min_nodes, buffer);
	pack16(dump_job_ptr->limit_set_pn_min_memory, buffer);
	pack16(dump_job_ptr->limit_set_time, buffer);
	pack16(dump_job_ptr->limit_set_qos, buffer);

	packstr(dump_job_ptr->state_desc, buffer);
	packstr(dump_job_ptr->resp_host, buffer);

	pack16(dump_job_ptr->alloc_resp_port, buffer);
	pack16(dump_job_ptr->other_port, buffer);
	pack16(dump_job_ptr->start_protocol_ver, buffer);

	if (IS_JOB_COMPLETING(dump_job_ptr)) {
		if (dump_job_ptr->nodes_completing == NULL) {
			dump_job_ptr->nodes_completing =
				bitmap2node_name(dump_job_ptr->node_bitmap);
		}
		packstr(dump_job_ptr->nodes_completing, buffer);
	}
	packstr(dump_job_ptr->nodes, buffer);
	packstr(dump_job_ptr->partition, buffer);
	packstr(dump_job_ptr->name, buffer);
	packstr(dump_job_ptr->wckey, buffer);
	packstr(dump_job_ptr->alloc_node, buffer);
	packstr(dump_job_ptr->account, buffer);
	packstr(dump_job_ptr->comment, buffer);
	packstr(dump_job_ptr->gres, buffer);
	packstr(dump_job_ptr->gres_alloc, buffer);
	packstr(dump_job_ptr->gres_req, buffer);
	packstr(dump_job_ptr->gres_used, buffer);
	packstr(dump_job_ptr->network, buffer);
	packstr(dump_job_ptr->licenses, buffer);
	packstr(dump_job_ptr->mail_user, buffer);
	packstr(dump_job_ptr->resv_name, buffer);
	packstr(dump_job_ptr->batch_host, buffer);

	select_g_select_jobinfo_pack(dump_job_ptr->select_jobinfo,
				     buffer, SLURM_PROTOCOL_VERSION);
	pack_job_resources(dump_job_ptr->job_resrcs, buffer,
			   SLURM_PROTOCOL_VERSION);

	pack16(dump_job_ptr->ckpt_interval, buffer);
	checkpoint_pack_jobinfo(dump_job_ptr->check_job, buffer,
				SLURM_PROTOCOL_VERSION);
	packstr_array(dump_job_ptr->spank_job_env,
		      dump_job_ptr->spank_job_env_size, buffer);

	(void) gres_plugin_job_state_pack(dump_job_ptr->gres_list, buffer,
					  dump_job_ptr->job_id, true,
					  SLURM_PROTOCOL_VERSION);

	/* Dump job details, if available */
	detail_ptr = dump_job_ptr->details;
	if (detail_ptr) {
		xassert (detail_ptr->magic == DETAILS_MAGIC);
		pack16((uint16_t) DETAILS_FLAG, buffer);
		_dump_job_details(detail_ptr, buffer);
	} else
		pack16((uint16_t) 0, buffer);	/* no details flag */

	/* Dump job steps */
	step_iterator = list_iterator_create(dump_job_ptr->step_list);
	while ((step_ptr = (struct step_record *)
		list_next(step_iterator))) {
		if (step_ptr->state < JOB_RUNNING)
			continue;
		pack16((uint16_t) STEP_FLAG, buffer);
		dump_job_step_state(dump_job_ptr, step_ptr, buffer);
	}
	list_iterator_destroy(step_iterator);
	pack16((uint16_t) 0, buffer);	/* no step flag */
}

/* Unpack a job's state information from a buffer */
static int _load_job_state(Buf buffer, uint16_t protocol_version)
{
	uint32_t job_id, user_id, group_id, time_limit, priority, alloc_sid;
	uint32_t exit_code, assoc_id, db_index, name_len, time_min;
	uint32_t next_step_id, total_cpus, total_nodes = 0, cpu_cnt;
	uint32_t resv_id, spank_job_env_size = 0, qos_id, derived_ec = 0;
	uint32_t array_job_id = 0, req_switch = 0, wait4switch = 0;
	uint32_t profile = ACCT_GATHER_PROFILE_NOT_SET;
	time_t start_time, end_time, suspend_time, pre_sus_time, tot_sus_time;
	time_t preempt_time = 0;
	time_t resize_time = 0, now = time(NULL);
	uint8_t reboot = 0;
	uint32_t array_task_id = NO_VAL;
	uint32_t array_flags = 0, max_run_tasks = 0, tot_run_tasks = 0;
	uint32_t min_exit_code = 0, max_exit_code = 0, tot_comp_tasks = 0;
	uint16_t job_state, details, batch_flag, step_flag;
	uint16_t kill_on_node_fail, direct_set_prio;
	uint16_t alloc_resp_port, other_port, mail_type, state_reason;
	uint16_t restart_cnt, ckpt_interval;
	uint16_t wait_all_nodes, warn_flags = 0, warn_signal, warn_time;
	uint16_t limit_set_max_cpus = 0, limit_set_max_nodes = 0;
	uint16_t limit_set_min_cpus = 0, limit_set_min_nodes = 0;
	uint16_t limit_set_pn_min_memory = 0;
	uint16_t limit_set_time = 0, limit_set_qos = 0;
	uint16_t uint16_tmp;
	uint16_t start_protocol_ver = SLURM_MIN_PROTOCOL_VERSION;
	char *nodes = NULL, *partition = NULL, *name = NULL, *resp_host = NULL;
	char *account = NULL, *network = NULL, *mail_user = NULL;
	char *comment = NULL, *nodes_completing = NULL, *alloc_node = NULL;
	char *licenses = NULL, *state_desc = NULL, *wckey = NULL;
	char *resv_name = NULL, *gres = NULL, *batch_host = NULL;
	char *gres_alloc = NULL, *gres_req = NULL, *gres_used = NULL;
	char *task_id_str = NULL;
	uint32_t task_id_size = NO_VAL;
	char **spank_job_env = (char **) NULL;
	List gres_list = NULL, part_ptr_list = NULL;
	struct job_record *job_ptr = NULL;
	struct part_record *part_ptr;
	int error_code, i, qos_error;
	dynamic_plugin_data_t *select_jobinfo = NULL;
	job_resources_t *job_resources = NULL;
	check_jobinfo_t check_job = NULL;
	slurmdb_association_rec_t assoc_rec;
	slurmdb_qos_rec_t qos_rec;
	bool job_finished = false;
	char jbuf[JBUFSIZ];

	if (protocol_version >= SLURM_14_11_PROTOCOL_VERSION) {
		safe_unpack32(&array_job_id, buffer);
		safe_unpack32(&array_task_id, buffer);

		/* Job Array record */
		safe_unpack32(&task_id_size, buffer);
		if (task_id_size != NO_VAL) {
			if (task_id_size) {
				safe_unpackstr_xmalloc(&task_id_str, &name_len,
						       buffer);
			}
			safe_unpack32(&array_flags,    buffer);
			safe_unpack32(&max_run_tasks,  buffer);
			safe_unpack32(&tot_run_tasks,  buffer);
			safe_unpack32(&min_exit_code,  buffer);
			safe_unpack32(&max_exit_code,  buffer);
			safe_unpack32(&tot_comp_tasks, buffer);
		}

		safe_unpack32(&assoc_id, buffer);
		safe_unpack32(&job_id, buffer);

		/* validity test as possible */
		if (job_id == 0) {
			verbose("Invalid job_id %u", job_id);
			goto unpack_error;
		}

		job_ptr = find_job_record(job_id);
		if (job_ptr == NULL) {
			job_ptr = _create_job_record(&error_code, 1);
			if (error_code) {
				error("Create job entry failed for job_id %u",
				      job_id);
				goto unpack_error;
			}
			job_ptr->job_id = job_id;
			job_ptr->array_job_id = array_job_id;
			job_ptr->array_task_id = array_task_id;
		}

		safe_unpack32(&user_id, buffer);
		safe_unpack32(&group_id, buffer);
		safe_unpack32(&time_limit, buffer);
		safe_unpack32(&time_min, buffer);
		safe_unpack32(&priority, buffer);
		safe_unpack32(&alloc_sid, buffer);
		safe_unpack32(&total_cpus, buffer);
		safe_unpack32(&total_nodes, buffer);
		safe_unpack32(&cpu_cnt, buffer);
		safe_unpack32(&exit_code, buffer);
		safe_unpack32(&derived_ec, buffer);
		safe_unpack32(&db_index, buffer);
		safe_unpack32(&resv_id, buffer);
		safe_unpack32(&next_step_id, buffer);
		safe_unpack32(&qos_id, buffer);
		safe_unpack32(&req_switch, buffer);
		safe_unpack32(&wait4switch, buffer);
		safe_unpack32(&profile, buffer);

		safe_unpack_time(&preempt_time, buffer);
		safe_unpack_time(&start_time, buffer);
		safe_unpack_time(&end_time, buffer);
		safe_unpack_time(&suspend_time, buffer);
		safe_unpack_time(&pre_sus_time, buffer);
		safe_unpack_time(&resize_time, buffer);
		safe_unpack_time(&tot_sus_time, buffer);

		safe_unpack16(&direct_set_prio, buffer);
		safe_unpack16(&job_state, buffer);
		safe_unpack16(&kill_on_node_fail, buffer);
		safe_unpack16(&batch_flag, buffer);
		safe_unpack16(&mail_type, buffer);
		safe_unpack16(&state_reason, buffer);
		safe_unpack8 (&reboot, buffer);
		safe_unpack16(&restart_cnt, buffer);
		safe_unpack16(&wait_all_nodes, buffer);
		safe_unpack16(&warn_flags, buffer);
		safe_unpack16(&warn_signal, buffer);
		safe_unpack16(&warn_time, buffer);
		safe_unpack16(&limit_set_max_cpus, buffer);
		safe_unpack16(&limit_set_max_nodes, buffer);
		safe_unpack16(&limit_set_min_cpus, buffer);
		safe_unpack16(&limit_set_min_nodes, buffer);
		safe_unpack16(&limit_set_pn_min_memory, buffer);
		safe_unpack16(&limit_set_time, buffer);
		safe_unpack16(&limit_set_qos, buffer);

		safe_unpackstr_xmalloc(&state_desc, &name_len, buffer);
		safe_unpackstr_xmalloc(&resp_host, &name_len, buffer);

		safe_unpack16(&alloc_resp_port, buffer);
		safe_unpack16(&other_port, buffer);
		safe_unpack16(&start_protocol_ver, buffer);

		if (job_state & JOB_COMPLETING) {
			safe_unpackstr_xmalloc(&nodes_completing,
					       &name_len, buffer);
		}
		safe_unpackstr_xmalloc(&nodes, &name_len, buffer);
		safe_unpackstr_xmalloc(&partition, &name_len, buffer);
		if (partition == NULL) {
			error("No partition for job %u", job_id);
			goto unpack_error;
		}
		part_ptr = find_part_record (partition);
		if (part_ptr == NULL) {
			part_ptr_list = get_part_list(partition);
			if (part_ptr_list) {
				part_ptr = list_peek(part_ptr_list);
			} else {
				verbose("Invalid partition (%s) for job_id %u",
					partition, job_id);
				/* not fatal error, partition could have been
				 * removed, reset_job_bitmaps() will clean-up
				 * this job */
			}
		}

		safe_unpackstr_xmalloc(&name, &name_len, buffer);
		safe_unpackstr_xmalloc(&wckey, &name_len, buffer);
		safe_unpackstr_xmalloc(&alloc_node, &name_len, buffer);
		safe_unpackstr_xmalloc(&account, &name_len, buffer);
		safe_unpackstr_xmalloc(&comment, &name_len, buffer);
		safe_unpackstr_xmalloc(&gres, &name_len, buffer);
		safe_unpackstr_xmalloc(&gres_alloc, &name_len, buffer);
		safe_unpackstr_xmalloc(&gres_req, &name_len, buffer);
		safe_unpackstr_xmalloc(&gres_used, &name_len, buffer);
		safe_unpackstr_xmalloc(&network, &name_len, buffer);
		safe_unpackstr_xmalloc(&licenses, &name_len, buffer);
		safe_unpackstr_xmalloc(&mail_user, &name_len, buffer);
		safe_unpackstr_xmalloc(&resv_name, &name_len, buffer);
		safe_unpackstr_xmalloc(&batch_host, &name_len, buffer);

		if (select_g_select_jobinfo_unpack(&select_jobinfo, buffer,
						   protocol_version))
			goto unpack_error;
		if (unpack_job_resources(&job_resources, buffer,
					 protocol_version))
			goto unpack_error;

		safe_unpack16(&ckpt_interval, buffer);
		if (checkpoint_alloc_jobinfo(&check_job) ||
		    checkpoint_unpack_jobinfo(check_job, buffer,
					      protocol_version))
			goto unpack_error;

		safe_unpackstr_array(&spank_job_env, &spank_job_env_size,
				     buffer);

		if (gres_plugin_job_state_unpack(&gres_list, buffer, job_id,
						 protocol_version) !=
		    SLURM_SUCCESS)
			goto unpack_error;
		gres_plugin_job_state_log(gres_list, job_id);

		safe_unpack16(&details, buffer);
		if ((details == DETAILS_FLAG) &&
		    (_load_job_details(job_ptr, buffer, protocol_version))) {
			job_ptr->job_state = JOB_FAILED;
			job_ptr->exit_code = 1;
			job_ptr->state_reason = FAIL_SYSTEM;
			xfree(job_ptr->state_desc);
			job_ptr->end_time = now;
			goto unpack_error;
		}
		safe_unpack16(&step_flag, buffer);

		while (step_flag == STEP_FLAG) {
			/* No need to put these into accounting if they
			 * haven't been since all information will be
			 * put in when the job is finished.
			 */
			if ((error_code = load_step_state(job_ptr, buffer,
							  protocol_version)))
				goto unpack_error;
			safe_unpack16(&step_flag, buffer);
		}
	} else if (protocol_version >= SLURM_14_03_PROTOCOL_VERSION) {
		safe_unpack32(&array_job_id, buffer);
		safe_unpack32(&array_task_id, buffer);
		safe_unpack32(&assoc_id, buffer);
		safe_unpack32(&job_id, buffer);

		/* validity test as possible */
		if (job_id == 0) {
			verbose("Invalid job_id %u", job_id);
			goto unpack_error;
		}

		job_ptr = find_job_record(job_id);
		if (job_ptr == NULL) {
			job_ptr = _create_job_record(&error_code, 1);
			if (error_code) {
				error("Create job entry failed for job_id %u",
				      job_id);
				goto unpack_error;
			}
			job_ptr->job_id = job_id;
			job_ptr->array_job_id = array_job_id;
			job_ptr->array_task_id = array_task_id;
		}

		safe_unpack32(&user_id, buffer);
		safe_unpack32(&group_id, buffer);
		safe_unpack32(&time_limit, buffer);
		safe_unpack32(&time_min, buffer);
		safe_unpack32(&priority, buffer);
		safe_unpack32(&alloc_sid, buffer);
		safe_unpack32(&total_cpus, buffer);
		safe_unpack32(&total_nodes, buffer);
		safe_unpack32(&cpu_cnt, buffer);
		safe_unpack32(&exit_code, buffer);
		safe_unpack32(&derived_ec, buffer);
		safe_unpack32(&db_index, buffer);
		safe_unpack32(&resv_id, buffer);
		safe_unpack32(&next_step_id, buffer);
		safe_unpack32(&qos_id, buffer);
		safe_unpack32(&req_switch, buffer);
		safe_unpack32(&wait4switch, buffer);
		safe_unpack32(&profile, buffer);

		safe_unpack_time(&preempt_time, buffer);
		safe_unpack_time(&start_time, buffer);
		safe_unpack_time(&end_time, buffer);
		safe_unpack_time(&suspend_time, buffer);
		safe_unpack_time(&pre_sus_time, buffer);
		safe_unpack_time(&resize_time, buffer);
		safe_unpack_time(&tot_sus_time, buffer);

		safe_unpack16(&direct_set_prio, buffer);
		safe_unpack16(&job_state, buffer);
		safe_unpack16(&kill_on_node_fail, buffer);
		safe_unpack16(&batch_flag, buffer);
		safe_unpack16(&mail_type, buffer);
		safe_unpack16(&state_reason, buffer);
		safe_unpack16(&restart_cnt, buffer);
		safe_unpack16(&wait_all_nodes, buffer);
		safe_unpack16(&warn_flags, buffer);
		safe_unpack16(&warn_signal, buffer);
		safe_unpack16(&warn_time, buffer);
		safe_unpack16(&limit_set_max_cpus, buffer);
		safe_unpack16(&limit_set_max_nodes, buffer);
		safe_unpack16(&limit_set_min_cpus, buffer);
		safe_unpack16(&limit_set_min_nodes, buffer);
		safe_unpack16(&limit_set_pn_min_memory, buffer);
		safe_unpack16(&limit_set_time, buffer);
		safe_unpack16(&limit_set_qos, buffer);

		safe_unpackstr_xmalloc(&state_desc, &name_len, buffer);
		safe_unpackstr_xmalloc(&resp_host, &name_len, buffer);

		safe_unpack16(&alloc_resp_port, buffer);
		safe_unpack16(&other_port, buffer);

		if (job_state & JOB_COMPLETING) {
			safe_unpackstr_xmalloc(&nodes_completing,
					       &name_len, buffer);
		}
		safe_unpackstr_xmalloc(&nodes, &name_len, buffer);
		safe_unpackstr_xmalloc(&partition, &name_len, buffer);
		if (partition == NULL) {
			error("No partition for job %u", job_id);
			goto unpack_error;
		}
		part_ptr = find_part_record (partition);
		if (part_ptr == NULL) {
			part_ptr_list = get_part_list(partition);
			if (part_ptr_list) {
				part_ptr = list_peek(part_ptr_list);
			} else {
				verbose("Invalid partition (%s) for job_id %u",
					partition, job_id);
				/* not fatal error, partition could have been
				 * removed, reset_job_bitmaps() will clean-up
				 * this job */
			}
		}

		safe_unpackstr_xmalloc(&name, &name_len, buffer);
		safe_unpackstr_xmalloc(&wckey, &name_len, buffer);
		safe_unpackstr_xmalloc(&alloc_node, &name_len, buffer);
		safe_unpackstr_xmalloc(&account, &name_len, buffer);
		safe_unpackstr_xmalloc(&comment, &name_len, buffer);
		safe_unpackstr_xmalloc(&gres, &name_len, buffer);
		safe_unpackstr_xmalloc(&gres_alloc, &name_len, buffer);
		safe_unpackstr_xmalloc(&gres_req, &name_len, buffer);
		safe_unpackstr_xmalloc(&gres_used, &name_len, buffer);
		safe_unpackstr_xmalloc(&network, &name_len, buffer);
		safe_unpackstr_xmalloc(&licenses, &name_len, buffer);
		safe_unpackstr_xmalloc(&mail_user, &name_len, buffer);
		safe_unpackstr_xmalloc(&resv_name, &name_len, buffer);
		safe_unpackstr_xmalloc(&batch_host, &name_len, buffer);

		if (select_g_select_jobinfo_unpack(&select_jobinfo, buffer,
						   protocol_version))
			goto unpack_error;
		if (unpack_job_resources(&job_resources, buffer,
					 protocol_version))
			goto unpack_error;

		safe_unpack16(&ckpt_interval, buffer);
		if (checkpoint_alloc_jobinfo(&check_job) ||
		    checkpoint_unpack_jobinfo(check_job, buffer,
					      protocol_version))
			goto unpack_error;

		safe_unpackstr_array(&spank_job_env, &spank_job_env_size,
				     buffer);

		if (gres_plugin_job_state_unpack(&gres_list, buffer, job_id,
						 protocol_version) !=
		    SLURM_SUCCESS)
			goto unpack_error;
		gres_plugin_job_state_log(gres_list, job_id);

		safe_unpack16(&details, buffer);
		if ((details == DETAILS_FLAG) &&
		    (_load_job_details(job_ptr, buffer, protocol_version))) {
			job_ptr->job_state = JOB_FAILED;
			job_ptr->exit_code = 1;
			job_ptr->state_reason = FAIL_SYSTEM;
			xfree(job_ptr->state_desc);
			job_ptr->end_time = now;
			goto unpack_error;
		}
		safe_unpack16(&step_flag, buffer);

		while (step_flag == STEP_FLAG) {
			/* No need to put these into accounting if they
			 * haven't been since all information will be
			 * put in when the job is finished.
			 */
			if ((error_code = load_step_state(job_ptr, buffer,
							  protocol_version)))
				goto unpack_error;
			safe_unpack16(&step_flag, buffer);
		}
	} else if (protocol_version >= SLURM_2_6_PROTOCOL_VERSION) {
		safe_unpack32(&array_job_id, buffer);
		safe_unpack16(&uint16_tmp, buffer);
		if (uint16_tmp == (uint16_t) NO_VAL)
			array_task_id = NO_VAL;
		else
			array_task_id = (uint32_t) uint16_tmp;
		safe_unpack32(&assoc_id, buffer);
		safe_unpack32(&job_id, buffer);

		/* validity test as possible */
		if (job_id == 0) {
			verbose("Invalid job_id %u", job_id);
			goto unpack_error;
		}

		job_ptr = find_job_record(job_id);
		if (job_ptr == NULL) {
			job_ptr = _create_job_record(&error_code, 1);
			if (error_code) {
				error("Create job entry failed for job_id %u",
				      job_id);
				goto unpack_error;
			}
			job_ptr->job_id = job_id;
			job_ptr->array_job_id = array_job_id;
			job_ptr->array_task_id = array_task_id;
		}

		safe_unpack32(&user_id, buffer);
		safe_unpack32(&group_id, buffer);
		safe_unpack32(&time_limit, buffer);
		safe_unpack32(&time_min, buffer);
		safe_unpack32(&priority, buffer);
		safe_unpack32(&alloc_sid, buffer);
		safe_unpack32(&total_cpus, buffer);
		safe_unpack32(&total_nodes, buffer);
		safe_unpack32(&cpu_cnt, buffer);
		safe_unpack32(&exit_code, buffer);
		safe_unpack32(&derived_ec, buffer);
		safe_unpack32(&db_index, buffer);
		safe_unpack32(&resv_id, buffer);
		safe_unpack32(&next_step_id, buffer);
		safe_unpack32(&qos_id, buffer);
		safe_unpack32(&req_switch, buffer);
		safe_unpack32(&wait4switch, buffer);
		safe_unpack32(&profile, buffer);

		safe_unpack_time(&preempt_time, buffer);
		safe_unpack_time(&start_time, buffer);
		safe_unpack_time(&end_time, buffer);
		safe_unpack_time(&suspend_time, buffer);
		safe_unpack_time(&pre_sus_time, buffer);
		safe_unpack_time(&resize_time, buffer);
		safe_unpack_time(&tot_sus_time, buffer);

		safe_unpack16(&direct_set_prio, buffer);
		safe_unpack16(&job_state, buffer);
		safe_unpack16(&kill_on_node_fail, buffer);
		safe_unpack16(&batch_flag, buffer);
		safe_unpack16(&mail_type, buffer);
		safe_unpack16(&state_reason, buffer);
		safe_unpack16(&restart_cnt, buffer);
		safe_unpack16(&uint16_tmp, buffer);	/* Was resv_flags */
		safe_unpack16(&wait_all_nodes, buffer);
		safe_unpack16(&warn_signal, buffer);
		safe_unpack16(&warn_time, buffer);
		safe_unpack16(&limit_set_max_cpus, buffer);
		safe_unpack16(&limit_set_max_nodes, buffer);
		safe_unpack16(&limit_set_min_cpus, buffer);
		safe_unpack16(&limit_set_min_nodes, buffer);
		safe_unpack16(&limit_set_pn_min_memory, buffer);
		safe_unpack16(&limit_set_time, buffer);
		safe_unpack16(&limit_set_qos, buffer);

		safe_unpackstr_xmalloc(&state_desc, &name_len, buffer);
		safe_unpackstr_xmalloc(&resp_host, &name_len, buffer);

		safe_unpack16(&alloc_resp_port, buffer);
		safe_unpack16(&other_port, buffer);

		if (job_state & JOB_COMPLETING) {
			safe_unpackstr_xmalloc(&nodes_completing,
					       &name_len, buffer);
		}
		safe_unpackstr_xmalloc(&nodes, &name_len, buffer);
		safe_unpackstr_xmalloc(&partition, &name_len, buffer);
		if (partition == NULL) {
			error("No partition for job %u", job_id);
			goto unpack_error;
		}
		part_ptr = find_part_record (partition);
		if (part_ptr == NULL) {
			part_ptr_list = get_part_list(partition);
			if (part_ptr_list) {
				part_ptr = list_peek(part_ptr_list);
			} else {
				verbose("Invalid partition (%s) for job_id %u",
					partition, job_id);
				/* not fatal error, partition could have been
				 * removed, reset_job_bitmaps() will clean-up
				 * this job */
			}
		}

		safe_unpackstr_xmalloc(&name, &name_len, buffer);
		safe_unpackstr_xmalloc(&wckey, &name_len, buffer);
		safe_unpackstr_xmalloc(&alloc_node, &name_len, buffer);
		safe_unpackstr_xmalloc(&account, &name_len, buffer);
		safe_unpackstr_xmalloc(&comment, &name_len, buffer);
		safe_unpackstr_xmalloc(&gres, &name_len, buffer);
		safe_unpackstr_xmalloc(&gres_alloc, &name_len, buffer);
		safe_unpackstr_xmalloc(&gres_req, &name_len, buffer);
		safe_unpackstr_xmalloc(&gres_used, &name_len, buffer);
		safe_unpackstr_xmalloc(&network, &name_len, buffer);
		safe_unpackstr_xmalloc(&licenses, &name_len, buffer);
		safe_unpackstr_xmalloc(&mail_user, &name_len, buffer);
		safe_unpackstr_xmalloc(&resv_name, &name_len, buffer);
		safe_unpackstr_xmalloc(&batch_host, &name_len, buffer);

		if (select_g_select_jobinfo_unpack(&select_jobinfo, buffer,
						   protocol_version))
			goto unpack_error;
		if (unpack_job_resources(&job_resources, buffer,
					 protocol_version))
			goto unpack_error;

		safe_unpack16(&ckpt_interval, buffer);
		if (checkpoint_alloc_jobinfo(&check_job) ||
		    checkpoint_unpack_jobinfo(check_job, buffer,
					      protocol_version))
			goto unpack_error;

		safe_unpackstr_array(&spank_job_env, &spank_job_env_size,
				     buffer);

		if (gres_plugin_job_state_unpack(&gres_list, buffer, job_id,
						 protocol_version) !=
		    SLURM_SUCCESS)
			goto unpack_error;
		gres_plugin_job_state_log(gres_list, job_id);

		safe_unpack16(&details, buffer);
		if ((details == DETAILS_FLAG) &&
		    (_load_job_details(job_ptr, buffer, protocol_version))) {
			job_ptr->job_state = JOB_FAILED;
			job_ptr->exit_code = 1;
			job_ptr->state_reason = FAIL_SYSTEM;
			xfree(job_ptr->state_desc);
			job_ptr->end_time = now;
			goto unpack_error;
		}
		safe_unpack16(&step_flag, buffer);

		while (step_flag == STEP_FLAG) {
			/* No need to put these into accounting if they
			 * haven't been since all information will be
			 * put in when the job is finished.
			 */
			if ((error_code = load_step_state(job_ptr, buffer,
							  protocol_version)))
				goto unpack_error;
			safe_unpack16(&step_flag, buffer);
		}
	} else {
		error("_load_job_state: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}

	if (((job_state & JOB_STATE_BASE) >= JOB_END) ||
	    (batch_flag > MAX_BATCH_REQUEUE)) {
		error("Invalid data for job %u: "
		      "job_state=%u batch_flag=%u",
		      job_id, job_state, batch_flag);
		goto unpack_error;
	}
	if (kill_on_node_fail > 1) {
		error("Invalid data for job %u: kill_on_node_fail=%u",
		      job_id, kill_on_node_fail);
		goto unpack_error;
	}

	if ((priority > 1) && (direct_set_prio == 0)) {
		highest_prio = MAX(highest_prio, priority);
		lowest_prio  = MIN(lowest_prio,  priority);
	}
	if (job_id_sequence <= job_id)
		job_id_sequence = job_id + 1;

	xfree(job_ptr->account);
	job_ptr->account = account;
	xstrtolower(job_ptr->account);
	account          = NULL;  /* reused, nothing left to free */
	xfree(job_ptr->alloc_node);
	job_ptr->alloc_node   = alloc_node;
	alloc_node             = NULL;	/* reused, nothing left to free */
	job_ptr->alloc_resp_port = alloc_resp_port;
	job_ptr->alloc_sid    = alloc_sid;
	job_ptr->assoc_id     = assoc_id;
	job_ptr->batch_flag   = batch_flag;
	xfree(job_ptr->batch_host);
	job_ptr->batch_host   = batch_host;
	batch_host            = NULL;  /* reused, nothing left to free */
	xfree(job_ptr->comment);
	job_ptr->comment      = comment;
	comment               = NULL;  /* reused, nothing left to free */
	xfree(job_ptr->gres);
	job_ptr->gres         = gres;
	gres                  = NULL;  /* reused, nothing left to free */
	xfree(job_ptr->gres_alloc);
	job_ptr->gres_alloc   = gres_alloc;
	gres_alloc            = NULL;  /* reused, nothing left to free */
	xfree(job_ptr->gres_req);
	job_ptr->gres_req    = gres_req;
	gres_req              = NULL;  /* reused, nothing left to free */
	xfree(job_ptr->gres_used);
	job_ptr->gres_used    = gres_used;
	gres_used             = NULL;  /* reused, nothing left to free */
	job_ptr->gres_list    = gres_list;
	job_ptr->direct_set_prio = direct_set_prio;
	job_ptr->db_index     = db_index;
	job_ptr->derived_ec   = derived_ec;
	job_ptr->end_time     = end_time;
	job_ptr->exit_code    = exit_code;
	job_ptr->group_id     = group_id;
	job_ptr->job_state    = job_state;
	job_ptr->kill_on_node_fail = kill_on_node_fail;
	xfree(job_ptr->licenses);
	job_ptr->licenses     = licenses;
	licenses              = NULL;	/* reused, nothing left to free */
	job_ptr->mail_type    = mail_type;
	xfree(job_ptr->mail_user);
	job_ptr->mail_user    = mail_user;
	mail_user             = NULL;	/* reused, nothing left to free */
	xfree(job_ptr->name);		/* in case duplicate record */
	job_ptr->name         = name;
	name                  = NULL;	/* reused, nothing left to free */
	xfree(job_ptr->wckey);		/* in case duplicate record */
	job_ptr->wckey        = wckey;
	xstrtolower(job_ptr->wckey);
	wckey                 = NULL;	/* reused, nothing left to free */
	xfree(job_ptr->network);
	job_ptr->network      = network;
	network               = NULL;  /* reused, nothing left to free */
	job_ptr->next_step_id = next_step_id;
	xfree(job_ptr->nodes);		/* in case duplicate record */
	job_ptr->nodes        = nodes;
	nodes                 = NULL;	/* reused, nothing left to free */
	if (nodes_completing) {
		xfree(job_ptr->nodes_completing);
		job_ptr->nodes_completing = nodes_completing;
		nodes_completing = NULL;  /* reused, nothing left to free */
	}
	job_ptr->other_port   = other_port;
	xfree(job_ptr->partition);
	job_ptr->partition    = partition;
	partition             = NULL;	/* reused, nothing left to free */
	job_ptr->part_ptr = part_ptr;
	job_ptr->part_ptr_list = part_ptr_list;
	job_ptr->pre_sus_time = pre_sus_time;
	job_ptr->priority     = priority;
	job_ptr->qos_id       = qos_id;
	job_ptr->reboot       = reboot;
	xfree(job_ptr->resp_host);
	job_ptr->resp_host    = resp_host;
	resp_host             = NULL;	/* reused, nothing left to free */
	job_ptr->resize_time  = resize_time;
	job_ptr->restart_cnt  = restart_cnt;
	job_ptr->resv_id      = resv_id;
	job_ptr->resv_name    = resv_name;
	resv_name             = NULL;	/* reused, nothing left to free */
	job_ptr->select_jobinfo = select_jobinfo;
	job_ptr->job_resrcs   = job_resources;
	job_ptr->spank_job_env = spank_job_env;
	job_ptr->spank_job_env_size = spank_job_env_size;
	job_ptr->ckpt_interval = ckpt_interval;
	job_ptr->check_job    = check_job;
	job_ptr->start_time   = start_time;
	job_ptr->state_reason = state_reason;
	job_ptr->state_desc   = state_desc;
	state_desc            = NULL;	/* reused, nothing left to free */
	job_ptr->suspend_time = suspend_time;
	if (task_id_size != NO_VAL) {
		if (!job_ptr->array_recs)
			job_ptr->array_recs=xmalloc(sizeof(job_array_struct_t));
		FREE_NULL_BITMAP(job_ptr->array_recs->task_id_bitmap);
		job_ptr->array_recs->task_id_bitmap = bit_alloc(task_id_size);
		xfree(job_ptr->array_recs->task_id_str);
		if (task_id_str) {
			bit_unfmt_hexmask(job_ptr->array_recs->task_id_bitmap,
					  task_id_str);
			job_ptr->array_recs->task_id_str = task_id_str;
			task_id_str = NULL;
		}
		job_ptr->array_recs->task_cnt =
			bit_set_count(job_ptr->array_recs->task_id_bitmap);

		if (job_ptr->array_recs->task_cnt > 1)
			job_count += (job_ptr->array_recs->task_cnt - 1);

		job_ptr->array_recs->array_flags    = array_flags;
		job_ptr->array_recs->max_run_tasks  = max_run_tasks;
		job_ptr->array_recs->tot_run_tasks  = tot_run_tasks;
		job_ptr->array_recs->min_exit_code  = min_exit_code;
		job_ptr->array_recs->max_exit_code  = max_exit_code;
		job_ptr->array_recs->tot_comp_tasks = tot_comp_tasks;
	}
	job_ptr->time_last_active = now;
	job_ptr->time_limit   = time_limit;
	job_ptr->time_min     = time_min;
	job_ptr->total_cpus   = total_cpus;

	if (IS_JOB_PENDING(job_ptr))
		job_ptr->node_cnt_wag = total_nodes;
	else
		job_ptr->total_nodes  = total_nodes;

	job_ptr->cpu_cnt      = cpu_cnt;
	job_ptr->tot_sus_time = tot_sus_time;
	job_ptr->preempt_time = preempt_time;
	job_ptr->user_id      = user_id;
	select_g_select_jobinfo_set(job_ptr->select_jobinfo,
				    SELECT_JOBDATA_USER_NAME, &user_id);
	job_ptr->wait_all_nodes = wait_all_nodes;
	job_ptr->warn_flags   = warn_flags;
	job_ptr->warn_signal  = warn_signal;
	job_ptr->warn_time    = warn_time;
	job_ptr->limit_set_max_cpus  = limit_set_max_cpus;
	job_ptr->limit_set_max_nodes = limit_set_max_nodes;
	job_ptr->limit_set_min_cpus  = limit_set_min_cpus;
	job_ptr->limit_set_min_nodes = limit_set_min_nodes;
	job_ptr->limit_set_pn_min_memory = limit_set_pn_min_memory;
	job_ptr->limit_set_time      = limit_set_time;
	job_ptr->limit_set_qos       = limit_set_qos;
	job_ptr->req_switch      = req_switch;
	job_ptr->wait4switch     = wait4switch;
	job_ptr->profile         = profile;
	/* This needs to always to initialized to "true".  The select
	   plugin will deal with it every time it goes through the
	   logic if req_switch or wait4switch are set.
	*/
	job_ptr->best_switch     = true;
	job_ptr->start_protocol_ver = start_protocol_ver;

	_add_job_hash(job_ptr);
	_add_job_array_hash(job_ptr);

	memset(&assoc_rec, 0, sizeof(slurmdb_association_rec_t));

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
				    (slurmdb_association_rec_t **)
				    &job_ptr->assoc_ptr, false) &&
	    (accounting_enforce & ACCOUNTING_ENFORCE_ASSOCS)
	    && (!IS_JOB_FINISHED(job_ptr))) {
		info("Holding job %u with invalid association", job_id);
		xfree(job_ptr->state_desc);
		job_ptr->state_reason = FAIL_ACCOUNT;
	} else {
		job_ptr->assoc_id = assoc_rec.id;
		info("Recovered %s Assoc=%u",
		     jobid2str(job_ptr, jbuf), job_ptr->assoc_id);

		/* make sure we have started this job in accounting */
		if (!job_ptr->db_index) {
			debug("starting job %u in accounting",
			      job_ptr->job_id);
			if (!with_slurmdbd)
				jobacct_storage_g_job_start(
					acct_db_conn, job_ptr);
			if (slurmctld_init_db
			    && IS_JOB_SUSPENDED(job_ptr)) {
				jobacct_storage_g_job_suspend(acct_db_conn,
							      job_ptr);
			}
		}
		/* make sure we have this job completed in the
		 * database */
		if (IS_JOB_FINISHED(job_ptr)) {
			if (slurmctld_init_db)
				jobacct_storage_g_job_complete(
					acct_db_conn, job_ptr);
			job_finished = 1;
		}
	}

	if (!job_finished && job_ptr->qos_id &&
	    (job_ptr->state_reason != FAIL_ACCOUNT)) {
		memset(&qos_rec, 0, sizeof(slurmdb_qos_rec_t));
		qos_rec.id = job_ptr->qos_id;
		job_ptr->qos_ptr = _determine_and_validate_qos(
			job_ptr->resv_name, job_ptr->assoc_ptr,
			job_ptr->limit_set_qos, &qos_rec,
			&qos_error);
		if ((qos_error != SLURM_SUCCESS) && !job_ptr->limit_set_qos) {
			info("Holding job %u with invalid qos", job_id);
			xfree(job_ptr->state_desc);
			job_ptr->state_reason = FAIL_QOS;
			job_ptr->qos_id = 0;
		} else
			job_ptr->qos_id = qos_rec.id;
	}
	build_node_details(job_ptr, false);	/* set node_addr */
	return SLURM_SUCCESS;

unpack_error:
	error("Incomplete job record");
	xfree(alloc_node);
	xfree(account);
	xfree(batch_host);
	xfree(comment);
	xfree(gres);
	xfree(gres_alloc);
	xfree(gres_req);
	xfree(gres_used);
	xfree(resp_host);
	xfree(licenses);
	xfree(mail_user);
	xfree(name);
	xfree(nodes);
	xfree(nodes_completing);
	xfree(partition);
	FREE_NULL_LIST(part_ptr_list);
	xfree(resv_name);
	for (i=0; i<spank_job_env_size; i++)
		xfree(spank_job_env[i]);
	xfree(spank_job_env);
	xfree(state_desc);
	xfree(task_id_str);
	xfree(wckey);
	select_g_select_jobinfo_free(select_jobinfo);
	checkpoint_free_jobinfo(check_job);
	if (job_ptr) {
		if (job_ptr->job_id == 0)
			job_ptr->job_id = NO_VAL;
		_purge_job_record(job_ptr->job_id);
	}
	return SLURM_FAILURE;
}

/*
 * _dump_job_details - dump the state of a specific job details to
 *	a buffer
 * IN detail_ptr - pointer to job details for which information is requested
 * IN/OUT buffer - location to store data, pointers automatically advanced
 */
void _dump_job_details(struct job_details *detail_ptr, Buf buffer)
{
	pack32(detail_ptr->min_cpus, buffer);
	pack32(detail_ptr->max_cpus, buffer);
	pack32(detail_ptr->min_nodes, buffer);
	pack32(detail_ptr->max_nodes, buffer);
	pack32(detail_ptr->num_tasks, buffer);

	packstr(detail_ptr->acctg_freq, buffer);
	pack16(detail_ptr->contiguous, buffer);
	pack16(detail_ptr->core_spec, buffer);
	pack16(detail_ptr->cpus_per_task, buffer);
	pack16(detail_ptr->nice, buffer);
	pack16(detail_ptr->ntasks_per_node, buffer);
	pack16(detail_ptr->requeue, buffer);
	pack16(detail_ptr->task_dist, buffer);

	pack8(detail_ptr->share_res, buffer);
	pack8(detail_ptr->whole_node, buffer);

	packstr(detail_ptr->cpu_bind,     buffer);
	pack16(detail_ptr->cpu_bind_type, buffer);
	packstr(detail_ptr->mem_bind,     buffer);
	pack16(detail_ptr->mem_bind_type, buffer);
	pack16(detail_ptr->plane_size, buffer);

	pack8(detail_ptr->open_mode, buffer);
	pack8(detail_ptr->overcommit, buffer);
	pack8(detail_ptr->prolog_running, buffer);

	pack32(detail_ptr->pn_min_cpus, buffer);
	pack32(detail_ptr->pn_min_memory, buffer);
	pack32(detail_ptr->pn_min_tmp_disk, buffer);
	pack_time(detail_ptr->begin_time, buffer);
	pack_time(detail_ptr->submit_time, buffer);

	packstr(detail_ptr->req_nodes,  buffer);
	packstr(detail_ptr->exc_nodes,  buffer);
	packstr(detail_ptr->features,   buffer);
	packstr(detail_ptr->dependency, buffer);
	packstr(detail_ptr->orig_dependency, buffer);

	packstr(detail_ptr->std_err,       buffer);
	packstr(detail_ptr->std_in,        buffer);
	packstr(detail_ptr->std_out,       buffer);
	packstr(detail_ptr->work_dir,  buffer);
	packstr(detail_ptr->ckpt_dir,  buffer);
	packstr(detail_ptr->restart_dir, buffer);

	pack_multi_core_data(detail_ptr->mc_ptr, buffer,
			     SLURM_PROTOCOL_VERSION);
	packstr_array(detail_ptr->argv, detail_ptr->argc, buffer);
	packstr_array(detail_ptr->env_sup, detail_ptr->env_cnt, buffer);
}

/* _load_job_details - Unpack a job details information from buffer */
static int _load_job_details(struct job_record *job_ptr, Buf buffer,
			     uint16_t protocol_version)
{
	char *acctg_freq = NULL, *req_nodes = NULL, *exc_nodes = NULL;
	char *features = NULL, *cpu_bind = NULL, *dependency = NULL;
	char *orig_dependency = NULL, *mem_bind;
	char *err = NULL, *in = NULL, *out = NULL, *work_dir = NULL;
	char *ckpt_dir = NULL, *restart_dir = NULL;
	char **argv = (char **) NULL, **env_sup = (char **) NULL;
	uint32_t min_nodes, max_nodes;
	uint32_t min_cpus = 1, max_cpus = NO_VAL;
	uint32_t pn_min_cpus, pn_min_memory, pn_min_tmp_disk;
	uint32_t num_tasks, name_len, argc = 0, env_cnt = 0;
	uint16_t contiguous, core_spec = (uint16_t) NO_VAL, nice;
	uint16_t ntasks_per_node, cpus_per_task, requeue, task_dist;
	uint16_t cpu_bind_type, mem_bind_type, plane_size;
	uint8_t open_mode, overcommit, prolog_running;
	uint8_t share_res, whole_node;
	time_t begin_time, submit_time;
	int i;
	multi_core_data_t *mc_ptr;

	/* unpack the job's details from the buffer */
	if (protocol_version >= SLURM_14_03_PROTOCOL_VERSION) {
		safe_unpack32(&min_cpus, buffer);
		safe_unpack32(&max_cpus, buffer);
		safe_unpack32(&min_nodes, buffer);
		safe_unpack32(&max_nodes, buffer);
		safe_unpack32(&num_tasks, buffer);

		safe_unpackstr_xmalloc(&acctg_freq, &name_len, buffer);
		if (acctg_freq && !strcmp(acctg_freq, "65534")) {
			/* This fixes job state generated by version 2.6.0,
			 * in which a version 2.5 value of NO_VAL was converted
			 * from uint16_t to a string. */
			xfree(acctg_freq);
		}
		safe_unpack16(&contiguous, buffer);
		safe_unpack16(&core_spec, buffer);
		safe_unpack16(&cpus_per_task, buffer);
		safe_unpack16(&nice, buffer);
		safe_unpack16(&ntasks_per_node, buffer);
		safe_unpack16(&requeue, buffer);
		safe_unpack16(&task_dist, buffer);

		safe_unpack8(&share_res, buffer);
		safe_unpack8(&whole_node, buffer);

		safe_unpackstr_xmalloc(&cpu_bind, &name_len, buffer);
		safe_unpack16(&cpu_bind_type, buffer);
		safe_unpackstr_xmalloc(&mem_bind, &name_len, buffer);
		safe_unpack16(&mem_bind_type, buffer);
		safe_unpack16(&plane_size, buffer);

		safe_unpack8(&open_mode, buffer);
		safe_unpack8(&overcommit, buffer);
		safe_unpack8(&prolog_running, buffer);

		safe_unpack32(&pn_min_cpus, buffer);
		safe_unpack32(&pn_min_memory, buffer);
		safe_unpack32(&pn_min_tmp_disk, buffer);
		safe_unpack_time(&begin_time, buffer);
		safe_unpack_time(&submit_time, buffer);

		safe_unpackstr_xmalloc(&req_nodes,  &name_len, buffer);
		safe_unpackstr_xmalloc(&exc_nodes,  &name_len, buffer);
		safe_unpackstr_xmalloc(&features,   &name_len, buffer);
		safe_unpackstr_xmalloc(&dependency, &name_len, buffer);
		safe_unpackstr_xmalloc(&orig_dependency, &name_len, buffer);

		safe_unpackstr_xmalloc(&err, &name_len, buffer);
		safe_unpackstr_xmalloc(&in,  &name_len, buffer);
		safe_unpackstr_xmalloc(&out, &name_len, buffer);
		safe_unpackstr_xmalloc(&work_dir, &name_len, buffer);
		safe_unpackstr_xmalloc(&ckpt_dir, &name_len, buffer);
		safe_unpackstr_xmalloc(&restart_dir, &name_len, buffer);

		if (unpack_multi_core_data(&mc_ptr, buffer, protocol_version))
			goto unpack_error;
		safe_unpackstr_array(&argv, &argc, buffer);
		safe_unpackstr_array(&env_sup, &env_cnt, buffer);
	} else if (protocol_version >= SLURM_2_6_PROTOCOL_VERSION) {
		uint16_t tmp_uint16;
		safe_unpack32(&min_cpus, buffer);
		safe_unpack32(&max_cpus, buffer);
		safe_unpack32(&min_nodes, buffer);
		safe_unpack32(&max_nodes, buffer);
		safe_unpack32(&num_tasks, buffer);

		safe_unpackstr_xmalloc(&acctg_freq, &name_len, buffer);
		if (acctg_freq && !strcmp(acctg_freq, "65534")) {
			/* This fixes job state generated by version 2.6.0,
			 * in which a version 2.5 value of NO_VAL was converted
			 * from uint16_t to a string. */
			xfree(acctg_freq);
		}
		safe_unpack16(&contiguous, buffer);
		safe_unpack16(&cpus_per_task, buffer);
		safe_unpack16(&nice, buffer);
		safe_unpack16(&ntasks_per_node, buffer);
		safe_unpack16(&requeue, buffer);
		safe_unpack16(&tmp_uint16, buffer);
		if (tmp_uint16 == 0) {
			share_res = 0;
			whole_node = 1;
		} else if ((tmp_uint16 == 1) || (tmp_uint16 == 2)) {
			share_res = 1;
			whole_node = 0;
		} else {
			share_res = (uint8_t) NO_VAL;
			whole_node = 0;
		}
		safe_unpack16(&task_dist, buffer);

		safe_unpackstr_xmalloc(&cpu_bind, &name_len, buffer);
		safe_unpack16(&cpu_bind_type, buffer);
		safe_unpackstr_xmalloc(&mem_bind, &name_len, buffer);
		safe_unpack16(&mem_bind_type, buffer);
		safe_unpack16(&plane_size, buffer);

		safe_unpack8(&open_mode, buffer);
		safe_unpack8(&overcommit, buffer);
		safe_unpack8(&prolog_running, buffer);

		safe_unpack32(&pn_min_cpus, buffer);
		safe_unpack32(&pn_min_memory, buffer);
		safe_unpack32(&pn_min_tmp_disk, buffer);
		safe_unpack_time(&begin_time, buffer);
		safe_unpack_time(&submit_time, buffer);

		safe_unpackstr_xmalloc(&req_nodes,  &name_len, buffer);
		safe_unpackstr_xmalloc(&exc_nodes,  &name_len, buffer);
		safe_unpackstr_xmalloc(&features,   &name_len, buffer);
		safe_unpackstr_xmalloc(&dependency, &name_len, buffer);
		safe_unpackstr_xmalloc(&orig_dependency, &name_len, buffer);

		safe_unpackstr_xmalloc(&err, &name_len, buffer);
		safe_unpackstr_xmalloc(&in,  &name_len, buffer);
		safe_unpackstr_xmalloc(&out, &name_len, buffer);
		safe_unpackstr_xmalloc(&work_dir, &name_len, buffer);
		safe_unpackstr_xmalloc(&ckpt_dir, &name_len, buffer);
		safe_unpackstr_xmalloc(&restart_dir, &name_len, buffer);

		if (unpack_multi_core_data(&mc_ptr, buffer, protocol_version))
			goto unpack_error;
		safe_unpackstr_array(&argv, &argc, buffer);
		safe_unpackstr_array(&env_sup, &env_cnt, buffer);
	} else {
		error("_load_job_details: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}

	/* validity test as possible */
	if (contiguous > 1) {
		error("Invalid data for job %u: contiguous=%u",
		      job_ptr->job_id, contiguous);
		goto unpack_error;
	}
	if ((requeue > 1) || (overcommit > 1)) {
		error("Invalid data for job %u: requeue=%u overcommit=%u",
		      job_ptr->job_id, requeue, overcommit);
		goto unpack_error;
	}
	if (prolog_running > 1) {
		error("Invalid data for job %u: prolog_running=%u",
		      job_ptr->job_id, prolog_running);
		goto unpack_error;
	}

	/* free any left-over detail data */
	xfree(job_ptr->details->acctg_freq);
	for (i=0; i<job_ptr->details->argc; i++)
		xfree(job_ptr->details->argv[i]);
	xfree(job_ptr->details->argv);
	xfree(job_ptr->details->cpu_bind);
	xfree(job_ptr->details->dependency);
	xfree(job_ptr->details->orig_dependency);
	xfree(job_ptr->details->std_err);
	for (i=0; i<job_ptr->details->env_cnt; i++)
		xfree(job_ptr->details->env_sup[i]);
	xfree(job_ptr->details->env_sup);
	xfree(job_ptr->details->exc_nodes);
	xfree(job_ptr->details->features);
	xfree(job_ptr->details->std_in);
	xfree(job_ptr->details->mem_bind);
	xfree(job_ptr->details->std_out);
	xfree(job_ptr->details->req_nodes);
	xfree(job_ptr->details->work_dir);
	xfree(job_ptr->details->ckpt_dir);
	xfree(job_ptr->details->restart_dir);

	/* now put the details into the job record */
	job_ptr->details->acctg_freq = acctg_freq;
	job_ptr->details->argc = argc;
	job_ptr->details->argv = argv;
	job_ptr->details->begin_time = begin_time;
	job_ptr->details->contiguous = contiguous;
	job_ptr->details->core_spec = core_spec;
	job_ptr->details->cpu_bind = cpu_bind;
	job_ptr->details->cpu_bind_type = cpu_bind_type;
	job_ptr->details->cpus_per_task = cpus_per_task;
	job_ptr->details->dependency = dependency;
	job_ptr->details->orig_dependency = orig_dependency;
	job_ptr->details->env_cnt = env_cnt;
	job_ptr->details->env_sup = env_sup;
	job_ptr->details->std_err = err;
	job_ptr->details->exc_nodes = exc_nodes;
	job_ptr->details->features = features;
	job_ptr->details->std_in = in;
	job_ptr->details->pn_min_cpus = pn_min_cpus;
	job_ptr->details->pn_min_memory = pn_min_memory;
	job_ptr->details->pn_min_tmp_disk = pn_min_tmp_disk;
	job_ptr->details->max_cpus = max_cpus;
	job_ptr->details->max_nodes = max_nodes;
	job_ptr->details->mc_ptr = mc_ptr;
	job_ptr->details->mem_bind = mem_bind;
	job_ptr->details->mem_bind_type = mem_bind_type;
	job_ptr->details->min_cpus = min_cpus;
	job_ptr->details->min_nodes = min_nodes;
	job_ptr->details->nice = nice;
	job_ptr->details->ntasks_per_node = ntasks_per_node;
	job_ptr->details->num_tasks = num_tasks;
	job_ptr->details->open_mode = open_mode;
	job_ptr->details->std_out = out;
	job_ptr->details->overcommit = overcommit;
	job_ptr->details->plane_size = plane_size;
	job_ptr->details->prolog_running = prolog_running;
	job_ptr->details->req_nodes = req_nodes;
	job_ptr->details->requeue = requeue;
	job_ptr->details->share_res = share_res;
	job_ptr->details->submit_time = submit_time;
	job_ptr->details->task_dist = task_dist;
	job_ptr->details->whole_node = whole_node;
	job_ptr->details->work_dir = work_dir;
	job_ptr->details->ckpt_dir = ckpt_dir;
	job_ptr->details->restart_dir = restart_dir;

	return SLURM_SUCCESS;

unpack_error:

/*	for (i=0; i<argc; i++)
	xfree(argv[i]);  Don't trust this on unpack error */
	xfree(acctg_freq);
	xfree(argv);
	xfree(cpu_bind);
	xfree(dependency);
	xfree(orig_dependency);
/*	for (i=0; i<env_cnt; i++)
	xfree(env_sup[i]);  Don't trust this on unpack error */
	xfree(env_sup);
	xfree(err);
	xfree(exc_nodes);
	xfree(features);
	xfree(in);
	xfree(mem_bind);
	xfree(out);
	xfree(req_nodes);
	xfree(work_dir);
	xfree(ckpt_dir);
	xfree(restart_dir);
	return SLURM_FAILURE;
}

/* _add_job_hash - add a job hash entry for given job record, job_id must
 *	already be set
 * IN job_ptr - pointer to job record
 * Globals: hash table updated
 */
static void _add_job_hash(struct job_record *job_ptr)
{
	int inx;

	inx = JOB_HASH_INX(job_ptr->job_id);
	job_ptr->job_next = job_hash[inx];
	job_hash[inx] = job_ptr;
}

/* _remove_job_hash - remove a job hash entry for given job record, job_id must
 *	already be set
 * IN job_ptr - pointer to job record
 * Globals: hash table updated
 */
static void _remove_job_hash(struct job_record *job_entry)
{
	struct job_record *job_ptr, **job_pptr;

        job_pptr = &job_hash[JOB_HASH_INX(job_entry->job_id)];
        while ((job_pptr != NULL) &&
               ((job_ptr = *job_pptr) != job_entry)) {
                job_pptr = &job_ptr->job_next;
        }
        if (job_pptr == NULL) {
                fatal("job hash error");
                return; /* Fix CLANG false positive error */
        }
	*job_pptr = job_entry->job_next;
	job_entry->job_next = NULL;
}

/* _add_job_array_hash - add a job hash entry for given job record,
 *	array_job_id and array_task_id must already be set
 * IN job_ptr - pointer to job record
 * Globals: hash table updated
 */
void _add_job_array_hash(struct job_record *job_ptr)
{
	int inx;

	if (job_ptr->array_task_id == NO_VAL)
		return;	/* Not a job array */

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
extern void build_array_str(struct job_record *job_ptr)
{
	job_array_struct_t *array_recs = job_ptr->array_recs;

	if (!array_recs || array_recs->task_id_str || !array_recs->task_cnt ||
	    !array_recs->task_id_bitmap)
		return;

	array_recs->task_id_str = bit_fmt_hexmask(array_recs->task_id_bitmap);
	/* Here we set the db_index to 0 so we resend the start of the
	 * job updating the array task string and count of pending
	 * jobs.  This is faster than sending the start again since
	 * this could happen many times instead of just ever so often.
	 */
	job_ptr->db_index = 0;
}

/* Return true if ALL tasks of specific array job ID are complete */
extern bool test_job_array_complete(uint32_t array_job_id)
{
	struct job_record *job_ptr;
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
	struct job_record *job_ptr;
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

/* Return true if ANY tasks of specific array job ID are pending */
extern bool test_job_array_pending(uint32_t array_job_id)
{
	struct job_record *job_ptr;
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

/*
 * find_job_array_rec - return a pointer to the job record with the given
 *	array_job_id/array_task_id
 * IN job_id - requested job's id
 * IN array_task_id - requested job's task id,
 *		      NO_VAL if none specified (i.e. not a job array)
 *		      INFINITE return any task for specified job id
 * RET pointer to the job's record, NULL on error
 */
extern struct job_record *find_job_array_rec(uint32_t array_job_id,
					     uint32_t array_task_id)
{
	struct job_record *job_ptr, *match_job_ptr = NULL;
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
 * find_job_record - return a pointer to the job record with the given job_id
 * IN job_id - requested job's id
 * RET pointer to the job's record, NULL on error
 */
struct job_record *find_job_record(uint32_t job_id)
{
	struct job_record *job_ptr;

	job_ptr = job_hash[JOB_HASH_INX(job_id)];
	while (job_ptr) {
		if (job_ptr->job_id == job_id)
			return job_ptr;
		job_ptr = job_ptr->job_next;
	}

	return NULL;
}

/* rebuild a job's partition name list based upon the contents of its
 *	part_ptr_list */
static void _rebuild_part_name_list(struct job_record  *job_ptr)
{
	bool job_active = false, job_pending = false;
	struct part_record *part_ptr;
	ListIterator part_iterator;

	xfree(job_ptr->partition);
	if (IS_JOB_RUNNING(job_ptr) || IS_JOB_SUSPENDED(job_ptr)) {
		job_active = true;
		xfree(job_ptr->partition);
		job_ptr->partition = xstrdup(job_ptr->part_ptr->name);
	} else if (IS_JOB_PENDING(job_ptr))
		job_pending = true;

	part_iterator = list_iterator_create(job_ptr->part_ptr_list);
	while ((part_ptr = (struct part_record *) list_next(part_iterator))) {
		if (job_pending) {
			/* Reset job's one partition to a valid one */
			job_ptr->part_ptr = part_ptr;
			job_pending = false;
		}
		if (job_active && (part_ptr == job_ptr->part_ptr))
			continue;	/* already added */
		if (job_ptr->partition)
			xstrcat(job_ptr->partition, ",");
		xstrcat(job_ptr->partition, part_ptr->name);
	}
	list_iterator_destroy(part_iterator);
	last_job_update = time(NULL);
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
	ListIterator job_iterator, part_iterator;
	struct job_record  *job_ptr;
	struct part_record *part_ptr, *part2_ptr;
	int kill_job_cnt = 0;
	time_t now = time(NULL);

	part_ptr = find_part_record (part_name);
	if (part_ptr == NULL)	/* No such partition */
		return 0;

	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		bool pending = false, suspended = false;

		pending = IS_JOB_PENDING(job_ptr);
		if (job_ptr->part_ptr_list) {
			/* Remove partition if candidate for a job */
			bool rebuild_name_list = false;
			part_iterator = list_iterator_create(job_ptr->
							     part_ptr_list);
			while ((part2_ptr = (struct part_record *)
					list_next(part_iterator))) {
				if (part2_ptr != part_ptr)
					continue;
				list_remove(part_iterator);
				rebuild_name_list = true;
			}
			list_iterator_destroy(part_iterator);
			if (rebuild_name_list) {
				if (list_count(job_ptr->part_ptr_list) > 0) {
					_rebuild_part_name_list(job_ptr);
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
			enum job_states suspend_job_state = job_ptr->job_state;
			/* we can't have it as suspended when we call the
			 * accounting stuff.
			 */
			job_ptr->job_state = JOB_CANCELLED;
			jobacct_storage_g_job_suspend(acct_db_conn, job_ptr);
			job_ptr->job_state = suspend_job_state;
			suspended = true;
		}
		if (IS_JOB_RUNNING(job_ptr) || suspended) {
			kill_job_cnt++;
			info("Killing job_id %u on defunct partition %s",
			     job_ptr->job_id, part_name);
			job_ptr->job_state = JOB_NODE_FAIL | JOB_COMPLETING;
			build_cg_bitmap(job_ptr);
			job_ptr->exit_code = MAX(job_ptr->exit_code, 1);
			job_ptr->state_reason = FAIL_DOWN_PARTITION;
			xfree(job_ptr->state_desc);
			if (suspended) {
				job_ptr->end_time = job_ptr->suspend_time;
				job_ptr->tot_sus_time +=
					difftime(now, job_ptr->suspend_time);
			} else
				job_ptr->end_time = now;
			job_completion_logger(job_ptr, false);
			if (!pending)
				deallocate_nodes(job_ptr, false, suspended,
						 false);
		} else if (pending) {
			kill_job_cnt++;
			info("Killing job_id %u on defunct partition %s",
			     job_ptr->job_id, part_name);
			job_ptr->job_state	= JOB_CANCELLED;
			job_ptr->start_time	= now;
			job_ptr->end_time	= now;
			job_ptr->exit_code	= 1;
			job_completion_logger(job_ptr, false);
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
	ListIterator job_iterator;
	struct job_record  *job_ptr;
	struct node_record *node_ptr;
	time_t now = time(NULL);
	int i, kill_job_cnt = 0;

	if (node_name == NULL)
		fatal("kill_job_by_front_end_name: node_name is NULL");

	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		bool suspended = false;

		if (!IS_JOB_RUNNING(job_ptr) && !IS_JOB_SUSPENDED(job_ptr) &&
		    !IS_JOB_COMPLETING(job_ptr))
			continue;
		if ((job_ptr->batch_host == NULL) ||
		    strcmp(job_ptr->batch_host, node_name))
			continue;	/* no match on node name */

		if (IS_JOB_SUSPENDED(job_ptr)) {
			enum job_states suspend_job_state = job_ptr->job_state;
			/* we can't have it as suspended when we call the
			 * accounting stuff.
			 */
			job_ptr->job_state = JOB_CANCELLED;
			jobacct_storage_g_job_suspend(acct_db_conn, job_ptr);
			job_ptr->job_state = suspend_job_state;
			suspended = true;
		}
		if (IS_JOB_COMPLETING(job_ptr)) {
			kill_job_cnt++;
			while ((i = bit_ffs(job_ptr->node_bitmap_cg)) >= 0) {
				bit_clear(job_ptr->node_bitmap_cg, i);
				job_update_cpu_cnt(job_ptr, i);
				if (job_ptr->node_cnt)
					(job_ptr->node_cnt)--;
				else {
					error("node_cnt underflow on JobId=%u",
					      job_ptr->job_id);
				}
				if (job_ptr->node_cnt == 0) {
					delete_step_records(job_ptr);
					job_ptr->job_state &= (~JOB_COMPLETING);
					slurm_sched_g_schedule();
				}
				node_ptr = &node_record_table_ptr[i];
				if (node_ptr->comp_job_cnt)
					(node_ptr->comp_job_cnt)--;
				else {
					error("Node %s comp_job_cnt underflow, "
					      "JobId=%u",
					      node_ptr->name, job_ptr->job_id);
				}
			}
		} else if (IS_JOB_RUNNING(job_ptr) || suspended) {
			kill_job_cnt++;
			if (job_ptr->batch_flag && job_ptr->details &&
			    slurmctld_conf.job_requeue &&
			    (job_ptr->details->requeue > 0)) {
				char requeue_msg[128];

				srun_node_fail(job_ptr->job_id, node_name);

				info("requeue job %u due to failure of node %s",
				     job_ptr->job_id, node_name);
				set_job_prio(job_ptr);
				snprintf(requeue_msg, sizeof(requeue_msg),
					 "Job requeued due to failure "
					 "of node %s",
					 node_name);
				slurm_sched_g_requeue(job_ptr, requeue_msg);
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

				/* We want this job to look like it
				 * was terminated in the accounting logs.
				 * Set a new submit time so the restarted
				 * job looks like a new job. */
				job_ptr->job_state  = JOB_NODE_FAIL;
				build_cg_bitmap(job_ptr);
				job_completion_logger(job_ptr, true);
				deallocate_nodes(job_ptr, false, suspended,
						 false);

				/* do this after the epilog complete,
				 * setting it here is too early */
				//job_ptr->db_index = 0;
				//job_ptr->details->submit_time = now;

				job_ptr->job_state = JOB_PENDING;
				if (job_ptr->node_cnt)
					job_ptr->job_state |= JOB_COMPLETING;

				/* restart from periodic checkpoint */
				if (job_ptr->ckpt_interval &&
				    job_ptr->ckpt_time &&
				    job_ptr->details->ckpt_dir) {
					xfree(job_ptr->details->restart_dir);
					job_ptr->details->restart_dir =
						xstrdup (job_ptr->details->
							 ckpt_dir);
					xstrfmtcat(job_ptr->details->
						   restart_dir,
						   "/%u", job_ptr->job_id);
				}
				job_ptr->restart_cnt++;
				/* Since the job completion logger
				 * removes the submit we need to add it
				 * again. */
				acct_policy_add_job_submit(job_ptr);

				if (!job_ptr->node_bitmap_cg ||
				    bit_set_count(job_ptr->node_bitmap_cg) == 0)
					batch_requeue_fini(job_ptr);
			} else {
				info("Killing job_id %u on failed node %s",
				     job_ptr->job_id, node_name);
				srun_node_fail(job_ptr->job_id, node_name);
				job_ptr->job_state = JOB_NODE_FAIL |
						     JOB_COMPLETING;
				build_cg_bitmap(job_ptr);
				job_ptr->exit_code = MAX(job_ptr->exit_code, 1);
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
 *	PENDING or SUSPENDED job
 * IN part_name - name of a partition
 * RET true if the partition is in use, else false
 */
extern bool partition_in_use(char *part_name)
{
	ListIterator job_iterator;
	struct job_record *job_ptr;
	struct part_record *part_ptr;

	part_ptr = find_part_record (part_name);
	if (part_ptr == NULL)	/* No such partition */
		return false;

	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		if (job_ptr->part_ptr == part_ptr) {
			if (!IS_JOB_FINISHED(job_ptr)) {
				list_iterator_destroy(job_iterator);
				return true;
			}
		}
	}
	list_iterator_destroy(job_iterator);
	return false;
}

/*
 * allocated_session_in_use - check if an interactive session is already running
 * IN new_alloc - allocation (alloc_node:alloc_sid) to test for
 * Returns true if an interactive session of the same node:sid already is in use
 * by a RUNNING, PENDING, or SUSPENDED job. Provides its own locking.
 */
extern bool allocated_session_in_use(job_desc_msg_t *new_alloc)
{
	ListIterator job_iter;
	struct job_record *job_ptr;
	/* Locks: Read job */
	slurmctld_lock_t job_read_lock = {
		NO_LOCK, READ_LOCK, NO_LOCK, NO_LOCK };

	if ((new_alloc->script != NULL) || (new_alloc->alloc_node == NULL))
		return false;

	lock_slurmctld(job_read_lock);
	job_iter = list_iterator_create(job_list);

	while ((job_ptr = (struct job_record *)list_next(job_iter))) {
		if (job_ptr->batch_flag || IS_JOB_FINISHED(job_ptr))
			continue;
		if (job_ptr->alloc_node &&
		    (strcmp(job_ptr->alloc_node, new_alloc->alloc_node) == 0) &&
		    (job_ptr->alloc_sid == new_alloc->alloc_sid))
			break;
	}
	list_iterator_destroy(job_iter);
	unlock_slurmctld(job_read_lock);

	return job_ptr != NULL;
}

/*
 * kill_running_job_by_node_name - Given a node name, deallocate RUNNING
 *	or COMPLETING jobs from the node or kill them
 * IN node_name - name of a node
 * RET number of killed jobs
 */
extern int kill_running_job_by_node_name(char *node_name)
{
	ListIterator job_iterator;
	struct job_record *job_ptr;
	struct node_record *node_ptr;
	int bit_position;
	int kill_job_cnt = 0;
	time_t now = time(NULL);

	node_ptr = find_node_record(node_name);
	if (node_ptr == NULL)	/* No such node */
		return 0;
	bit_position = node_ptr - node_record_table_ptr;

	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		bool suspended = false;
		if ((job_ptr->node_bitmap == NULL) ||
		    (!bit_test(job_ptr->node_bitmap, bit_position)))
			continue;	/* job not on this node */
		if (nonstop_ops.node_fail)
			(nonstop_ops.node_fail)(job_ptr, node_ptr);
		if (IS_JOB_SUSPENDED(job_ptr)) {
			enum job_states suspend_job_state = job_ptr->job_state;
			/* we can't have it as suspended when we call the
			 * accounting stuff.
			 */
			job_ptr->job_state = JOB_CANCELLED;
			jobacct_storage_g_job_suspend(acct_db_conn, job_ptr);
			job_ptr->job_state = suspend_job_state;
			suspended = true;
		}

		if (IS_JOB_COMPLETING(job_ptr)) {
			if (!bit_test(job_ptr->node_bitmap_cg, bit_position))
				continue;
			kill_job_cnt++;
			bit_clear(job_ptr->node_bitmap_cg, bit_position);
			job_update_cpu_cnt(job_ptr, bit_position);
			if (job_ptr->node_cnt)
				(job_ptr->node_cnt)--;
			else {
				error("node_cnt underflow on JobId=%u",
				      job_ptr->job_id);
			}
			if (job_ptr->node_cnt == 0) {
				delete_step_records(job_ptr);
				job_ptr->job_state &= (~JOB_COMPLETING);
				slurm_sched_g_schedule();
			}
			if (node_ptr->comp_job_cnt)
				(node_ptr->comp_job_cnt)--;
			else {
				error("Node %s comp_job_cnt underflow, "
				      "JobId=%u",
				      node_ptr->name, job_ptr->job_id);
			}
		} else if (IS_JOB_RUNNING(job_ptr) || suspended) {
			kill_job_cnt++;
			if ((job_ptr->details) &&
			    (job_ptr->kill_on_node_fail == 0) &&
			    (job_ptr->node_cnt > 1)) {
				/* keep job running on remaining nodes */
				srun_node_fail(job_ptr->job_id, node_name);
				error("Removing failed node %s from job_id %u",
				      node_name, job_ptr->job_id);
				job_pre_resize_acctg(job_ptr);
				kill_step_on_node(job_ptr, node_ptr, true);
				excise_node_from_job(job_ptr, node_ptr);
				job_post_resize_acctg(job_ptr);
			} else if (job_ptr->batch_flag && job_ptr->details &&
				   job_ptr->details->requeue) {
				char requeue_msg[128];

				srun_node_fail(job_ptr->job_id, node_name);

				info("requeue job %u due to failure of node %s",
				     job_ptr->job_id, node_name);
				snprintf(requeue_msg, sizeof(requeue_msg),
					 "Job requeued due to failure "
					 "of node %s",
					 node_name);
				slurm_sched_g_requeue(job_ptr, requeue_msg);
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

				/* We want this job to look like it
				 * was terminated in the accounting logs.
				 * Set a new submit time so the restarted
				 * job looks like a new job. */
				job_ptr->job_state  = JOB_NODE_FAIL;
				build_cg_bitmap(job_ptr);
				job_completion_logger(job_ptr, true);
				deallocate_nodes(job_ptr, false, suspended,
						 false);

				/* do this after the epilog complete,
				 * setting it here is too early */
				//job_ptr->db_index = 0;
				//job_ptr->details->submit_time = now;

				job_ptr->job_state = JOB_PENDING;
				if (job_ptr->node_cnt)
					job_ptr->job_state |= JOB_COMPLETING;

				/* restart from periodic checkpoint */
				if (job_ptr->ckpt_interval &&
				    job_ptr->ckpt_time &&
				    job_ptr->details->ckpt_dir) {
					xfree(job_ptr->details->restart_dir);
					job_ptr->details->restart_dir =
						xstrdup (job_ptr->details->
							 ckpt_dir);
					xstrfmtcat(job_ptr->details->
						   restart_dir,
						   "/%u", job_ptr->job_id);
				}
				job_ptr->restart_cnt++;
				/* Since the job completion logger
				 * removes the submit we need to add it
				 * again. */
				acct_policy_add_job_submit(job_ptr);

				if (!job_ptr->node_bitmap_cg ||
				    bit_set_count(job_ptr->node_bitmap_cg) == 0)
					batch_requeue_fini(job_ptr);
			} else {
				info("Killing job_id %u on failed node %s",
				     job_ptr->job_id, node_name);
				srun_node_fail(job_ptr->job_id, node_name);
				job_ptr->job_state = JOB_NODE_FAIL |
						     JOB_COMPLETING;
				build_cg_bitmap(job_ptr);
				job_ptr->exit_code = MAX(job_ptr->exit_code, 1);
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
extern void excise_node_from_job(struct job_record *job_ptr,
				 struct node_record *node_ptr)
{
	int i, orig_pos = -1, new_pos = -1;
	bitstr_t *orig_bitmap;

	orig_bitmap = bit_copy(job_ptr->node_bitmap);
	make_node_idle(node_ptr, job_ptr); /* updates bitmap */
	xfree(job_ptr->nodes);
	job_ptr->nodes = bitmap2node_name(job_ptr->node_bitmap);
	for (i=bit_ffs(orig_bitmap); i<node_record_count; i++) {
		if (!bit_test(orig_bitmap,i))
			continue;
		orig_pos++;
		if (!bit_test(job_ptr->node_bitmap, i))
			continue;
		new_pos++;
		if (orig_pos == new_pos)
			continue;
		memcpy(&job_ptr->node_addr[new_pos],
		       &job_ptr->node_addr[orig_pos], sizeof(slurm_addr_t));
		/* NOTE: The job's allocation in the job_ptr->job_resrcs
		 * data structure is unchanged  even after a node allocated
		 * to the job goes DOWN. */
	}

	job_ptr->total_nodes = job_ptr->node_cnt = new_pos + 1;

	FREE_NULL_BITMAP(orig_bitmap);
	(void) select_g_job_resized(job_ptr, node_ptr);
}

/*
 * dump_job_desc - dump the incoming job submit request message
 * IN job_specs - job specification from RPC
 */
void dump_job_desc(job_desc_msg_t * job_specs)
{
	long pn_min_cpus, pn_min_memory, pn_min_tmp_disk, min_cpus;
	long time_limit, priority, contiguous, nice, time_min;
	long kill_on_node_fail, shared, immediate, wait_all_nodes;
	long cpus_per_task, requeue, num_tasks, overcommit;
	long ntasks_per_node, ntasks_per_socket, ntasks_per_core;
	int core_spec;
	char *mem_type, buf[100], *signal_flags, *job_id;

	if (job_specs == NULL)
		return;

	if (job_specs->job_id_str)
		job_id = job_specs->job_id_str;
	else if (job_specs->job_id == NO_VAL)
		job_id = "N/A";
	else {
		snprintf(buf, sizeof(buf), "%u", job_specs->job_id);
		job_id = buf;
	}
	debug3("JobDesc: user_id=%u job_id=%s partition=%s name=%s",
	       job_specs->user_id, job_id,
	       job_specs->partition, job_specs->name);

	min_cpus = (job_specs->min_cpus != NO_VAL) ?
		(long) job_specs->min_cpus : -1L;
	pn_min_cpus    = (job_specs->pn_min_cpus != (uint16_t) NO_VAL) ?
		(long) job_specs->pn_min_cpus : -1L;
	core_spec = (job_specs->core_spec != (uint16_t) NO_VAL) ?
		    job_specs->core_spec : -1;
	debug3("   cpus=%ld-%u pn_min_cpus=%ld core_spec=%d",
	       min_cpus, job_specs->max_cpus, pn_min_cpus, core_spec);

	debug3("   -N min-[max]: %u-[%u]:%u:%u:%u",
	       job_specs->min_nodes,   job_specs->max_nodes,
	       job_specs->sockets_per_node, job_specs->cores_per_socket,
	       job_specs->threads_per_core);

	if (job_specs->pn_min_memory == NO_VAL) {
		pn_min_memory = -1L;
		mem_type = "job";
	} else if (job_specs->pn_min_memory & MEM_PER_CPU) {
		pn_min_memory = (long) (job_specs->pn_min_memory &
					 (~MEM_PER_CPU));
		mem_type = "cpu";
	} else {
		pn_min_memory = (long) job_specs->pn_min_memory;
		mem_type = "job";
	}
	pn_min_tmp_disk = (job_specs->pn_min_tmp_disk != NO_VAL) ?
		(long) job_specs->pn_min_tmp_disk : -1L;
	debug3("   pn_min_memory_%s=%ld pn_min_tmp_disk=%ld",
	       mem_type, pn_min_memory, pn_min_tmp_disk);
	immediate = (job_specs->immediate == 0) ? 0L : 1L;
	debug3("   immediate=%ld features=%s reservation=%s",
	       immediate, job_specs->features, job_specs->reservation);

	debug3("   req_nodes=%s exc_nodes=%s gres=%s",
	       job_specs->req_nodes, job_specs->exc_nodes, job_specs->gres);

	time_limit = (job_specs->time_limit != NO_VAL) ?
		(long) job_specs->time_limit : -1L;
	time_min = (job_specs->time_min != NO_VAL) ?
		(long) job_specs->time_min : time_limit;
	priority   = (job_specs->priority != NO_VAL) ?
		(long) job_specs->priority : -1L;
	contiguous = (job_specs->contiguous != (uint16_t) NO_VAL) ?
		(long) job_specs->contiguous : -1L;
	shared = (job_specs->shared != (uint16_t) NO_VAL) ?
		(long) job_specs->shared : -1L;
	debug3("   time_limit=%ld-%ld priority=%ld contiguous=%ld shared=%ld",
	       time_min, time_limit, priority, contiguous, shared);

	kill_on_node_fail = (job_specs->kill_on_node_fail !=
			     (uint16_t) NO_VAL) ?
		(long) job_specs->kill_on_node_fail : -1L;
	if (job_specs->script)	/* log has problem with string len & null */
		debug3("   kill_on_node_fail=%ld script=%.40s...",
		       kill_on_node_fail, job_specs->script);
	else
		debug3("   kill_on_node_fail=%ld script=%s",
		       kill_on_node_fail, job_specs->script);

	if (job_specs->argc == 1)
		debug3("   argv=\"%s\"",
		       job_specs->argv[0]);
	else if (job_specs->argc == 2)
		debug3("   argv=%s,%s",
		       job_specs->argv[0],
		       job_specs->argv[1]);
	else if (job_specs->argc > 2)
		debug3("   argv=%s,%s,%s,...",
		       job_specs->argv[0],
		       job_specs->argv[1],
		       job_specs->argv[2]);

	if (job_specs->env_size == 1)
		debug3("   environment=\"%s\"",
		       job_specs->environment[0]);
	else if (job_specs->env_size == 2)
		debug3("   environment=%s,%s",
		       job_specs->environment[0],
		       job_specs->environment[1]);
	else if (job_specs->env_size > 2)
		debug3("   environment=%s,%s,%s,...",
		       job_specs->environment[0],
		       job_specs->environment[1],
		       job_specs->environment[2]);

	if (job_specs->spank_job_env_size == 1)
		debug3("   spank_job_env=\"%s\"",
		       job_specs->spank_job_env[0]);
	else if (job_specs->spank_job_env_size == 2)
		debug3("   spank_job_env=%s,%s",
		       job_specs->spank_job_env[0],
		       job_specs->spank_job_env[1]);
	else if (job_specs->spank_job_env_size > 2)
		debug3("   spank_job_env=%s,%s,%s,...",
		       job_specs->spank_job_env[0],
		       job_specs->spank_job_env[1],
		       job_specs->spank_job_env[2]);

	debug3("   stdin=%s stdout=%s stderr=%s",
	       job_specs->std_in, job_specs->std_out, job_specs->std_err);

	debug3("   work_dir=%s alloc_node:sid=%s:%u",
	       job_specs->work_dir,
	       job_specs->alloc_node, job_specs->alloc_sid);

	debug3("   resp_host=%s alloc_resp_port=%u  other_port=%u",
	       job_specs->resp_host,
	       job_specs->alloc_resp_port, job_specs->other_port);
	debug3("   dependency=%s account=%s qos=%s comment=%s",
	       job_specs->dependency, job_specs->account,
	       job_specs->qos, job_specs->comment);

	num_tasks = (job_specs->num_tasks != NO_VAL) ?
		(long) job_specs->num_tasks : -1L;
	overcommit = (job_specs->overcommit != (uint8_t) NO_VAL) ?
		(long) job_specs->overcommit : -1L;
	nice = (job_specs->nice != (uint16_t) NO_VAL) ?
		(job_specs->nice - NICE_OFFSET) : 0;
	debug3("   mail_type=%u mail_user=%s nice=%ld num_tasks=%ld "
	       "open_mode=%u overcommit=%ld acctg_freq=%s",
	       job_specs->mail_type, job_specs->mail_user, nice, num_tasks,
	       job_specs->open_mode, overcommit, job_specs->acctg_freq);

	slurm_make_time_str(&job_specs->begin_time, buf, sizeof(buf));
	cpus_per_task = (job_specs->cpus_per_task != (uint16_t) NO_VAL) ?
		(long) job_specs->cpus_per_task : -1L;
	requeue = (job_specs->requeue != (uint16_t) NO_VAL) ?
		(long) job_specs->requeue : -1L;
	debug3("   network=%s begin=%s cpus_per_task=%ld requeue=%ld "
	       "licenses=%s",
	       job_specs->network, buf, cpus_per_task, requeue,
	       job_specs->licenses);

	slurm_make_time_str(&job_specs->end_time, buf, sizeof(buf));
	wait_all_nodes = (job_specs->wait_all_nodes != (uint16_t) NO_VAL) ?
			 (long) job_specs->wait_all_nodes : -1L;
	if (job_specs->warn_flags & KILL_JOB_BATCH)
		signal_flags = "B:";
	else
		signal_flags = "";
	debug3("   end_time=%s signal=%s%u@%u wait_all_nodes=%ld",
	       buf, signal_flags, job_specs->warn_signal, job_specs->warn_time,
	       wait_all_nodes);

	ntasks_per_node = (job_specs->ntasks_per_node != (uint16_t) NO_VAL) ?
		(long) job_specs->ntasks_per_node : -1L;
	ntasks_per_socket = (job_specs->ntasks_per_socket !=
			     (uint16_t) NO_VAL) ?
		(long) job_specs->ntasks_per_socket : -1L;
	ntasks_per_core = (job_specs->ntasks_per_core != (uint16_t) NO_VAL) ?
		(long) job_specs->ntasks_per_core : -1L;
	debug3("   ntasks_per_node=%ld ntasks_per_socket=%ld "
	       "ntasks_per_core=%ld",
	       ntasks_per_node, ntasks_per_socket, ntasks_per_core);

	debug3("   mem_bind=%u:%s plane_size:%u",
	       job_specs->mem_bind_type, job_specs->mem_bind,
	       job_specs->plane_size);
	debug3("   array_inx=%s", job_specs->array_inx);

	select_g_select_jobinfo_sprint(job_specs->select_jobinfo,
				       buf, sizeof(buf), SELECT_PRINT_MIXED);
	if (buf[0] != '\0')
		debug3("   %s", buf);
}


/*
 * init_job_conf - initialize the job configuration tables and values.
 *	this should be called after creating node information, but
 *	before creating any job entries. Pre-existing job entries are
 *	left unchanged.
 *	NOTE: The job hash table size does not change after initial creation.
 * RET 0 if no error, otherwise an error code
 * global: last_job_update - time of last job table update
 *	job_list - pointer to global job list
 */
int init_job_conf(void)
{
	if (job_list == NULL) {
		job_count = 0;
		job_list = list_create(_list_delete_job);
	}

	last_job_update = time(NULL);
	return SLURM_SUCCESS;
}

/*
 * rehash_jobs - Create or rebuild the job hash table.
 * NOTE: run lock_slurmctld before entry: Read config, write job
 */
extern void rehash_jobs(void)
{
	if (job_hash == NULL) {
		hash_table_size = slurmctld_conf.max_job_cnt;
		job_hash = (struct job_record **)
			xmalloc(hash_table_size * sizeof(struct job_record *));
		job_array_hash_j = (struct job_record **)
			xmalloc(hash_table_size * sizeof(struct job_record *));
		job_array_hash_t = (struct job_record **)
			xmalloc(hash_table_size * sizeof(struct job_record *));
	} else if (hash_table_size < (slurmctld_conf.max_job_cnt / 2)) {
		/* If the MaxJobCount grows by too much, the hash table will
		 * be ineffective without rebuilding. We don't presently bother
		 * to rebuild the hash table, but cut MaxJobCount back as
		 * needed. */
		error ("MaxJobCount reset too high, restart slurmctld");
		slurmctld_conf.max_job_cnt = hash_table_size;
	}
}

/* Create an exact copy of an existing job record for a job array.
 * The array_recs structure is moved to the new job record copy */
struct job_record *_job_rec_copy(struct job_record *job_ptr)
{
	struct job_record *job_ptr_pend = NULL, *save_job_next;
	struct job_details *job_details, *details_new, *save_details;
	uint32_t save_job_id, save_db_index = job_ptr->db_index;
	priority_factors_object_t *save_prio_factors;
	List save_step_list;
	int error_code = SLURM_SUCCESS;
	int i;

	job_ptr_pend = _create_job_record(&error_code, 0);
	if (!job_ptr_pend)     /* MaxJobCount checked when job array submitted */
		fatal("%s: _create_job_record error", __func__);
	if (error_code != SLURM_SUCCESS)
		return NULL;

	_remove_job_hash(job_ptr);
	job_ptr_pend->job_id = job_ptr->job_id;
	if (_set_job_id(job_ptr) != SLURM_SUCCESS)
		fatal("%s: _set_job_id error", __func__);
	if (!job_ptr->array_recs) {
		fatal("%s: job %u record lacks array structure",
		      __func__, job_ptr->job_id);
	}
	if (_copy_job_desc_files(job_ptr_pend->job_id, job_ptr->job_id)) {
		/* Need to quit here to preserve job record integrity */
		fatal("%s: failed to copy enviroment/script files for job %u",
		      __func__, job_ptr->job_id);
	}

	/* Copy most of original job data.
	 * This could be done in parallel, but performance was worse. */
	save_job_id   = job_ptr_pend->job_id;
	save_job_next = job_ptr_pend->job_next;
	save_details  = job_ptr_pend->details;
	save_prio_factors = job_ptr_pend->prio_factors;
	save_step_list = job_ptr_pend->step_list;
	memcpy(job_ptr_pend, job_ptr, sizeof(struct job_record));

	job_ptr_pend->job_id   = save_job_id;
	job_ptr_pend->job_next = save_job_next;
	job_ptr_pend->details  = save_details;
	job_ptr_pend->prio_factors = save_prio_factors;
	job_ptr_pend->step_list = save_step_list;
	job_ptr_pend->db_index = save_db_index;

	job_ptr_pend->account = xstrdup(job_ptr->account);
	job_ptr_pend->alias_list = xstrdup(job_ptr->alias_list);
	job_ptr_pend->alloc_node = xstrdup(job_ptr->alloc_node);

	job_ptr_pend->array_recs = job_ptr->array_recs;
	job_ptr->array_recs = NULL;

	if (job_ptr_pend->array_recs &&
	    job_ptr_pend->array_recs->task_id_bitmap) {
		bit_clear(job_ptr_pend->array_recs->task_id_bitmap,
			  job_ptr_pend->array_task_id);
	}
	xfree(job_ptr_pend->array_recs->task_id_str);
	job_ptr_pend->array_recs->task_cnt--;
	job_ptr_pend->array_task_id = NO_VAL;

	job_ptr_pend->batch_host = NULL;
	if (job_ptr->check_job) {
		job_ptr_pend->check_job =
			checkpoint_copy_jobinfo(job_ptr->check_job);
	}
	job_ptr_pend->comment = xstrdup(job_ptr->comment);

	job_ptr_pend->front_end_ptr = NULL;
	/* struct job_details *details;		*** NOTE: Copied below */
	job_ptr_pend->gres = xstrdup(job_ptr->gres);
	if (job_ptr->gres_list) {
		job_ptr_pend->gres_list =
			gres_plugin_job_state_dup(job_ptr->gres_list);
	}
	job_ptr_pend->gres_alloc = NULL;
	job_ptr_pend->gres_req = NULL;
	job_ptr_pend->gres_used = NULL;

	_add_job_hash(job_ptr);		/* Sets job_next */
	_add_job_hash(job_ptr_pend);	/* Sets job_next */
	_add_job_array_hash(job_ptr);
	job_ptr_pend->job_resrcs = NULL;

	job_ptr_pend->licenses = xstrdup(job_ptr->licenses);
	job_ptr_pend->license_list = license_job_copy(job_ptr->license_list);
	job_ptr_pend->mail_user = xstrdup(job_ptr->mail_user);
	job_ptr_pend->name = xstrdup(job_ptr->name);
	job_ptr_pend->network = xstrdup(job_ptr->network);
	job_ptr_pend->node_addr = NULL;
	job_ptr_pend->node_bitmap = NULL;
	job_ptr_pend->node_bitmap_cg = NULL;
	job_ptr_pend->nodes = NULL;
	job_ptr_pend->nodes_completing = NULL;
	job_ptr_pend->partition = xstrdup(job_ptr->partition);
	job_ptr_pend->part_ptr_list = part_list_copy(job_ptr->part_ptr_list);
	/* On jobs that are held the priority_array isn't set up yet,
	 * so check to see if it exists before copying. */
	if (job_ptr->part_ptr_list && job_ptr->priority_array) {
		i = list_count(job_ptr->part_ptr_list) * sizeof(uint32_t);
		job_ptr_pend->priority_array = xmalloc(i);
		memcpy(job_ptr_pend->priority_array,
		       job_ptr->priority_array, i);
	}
	job_ptr_pend->resv_name = xstrdup(job_ptr->resv_name);
	job_ptr_pend->resp_host = xstrdup(job_ptr->resp_host);
	if (job_ptr->select_jobinfo) {
		job_ptr_pend->select_jobinfo =
			select_g_select_jobinfo_copy(job_ptr->select_jobinfo);
	}
	job_ptr_pend->sched_nodes = NULL;
	if (job_ptr->spank_job_env_size) {
		job_ptr_pend->spank_job_env =
			xmalloc(sizeof(char *) *
			(job_ptr->spank_job_env_size + 1));
		for (i = 0; i < job_ptr->spank_job_env_size; i++) {
			job_ptr_pend->spank_job_env[i] =
				xstrdup(job_ptr->spank_job_env[i]);
		}
	}
	job_ptr_pend->state_desc = xstrdup(job_ptr->state_desc);
	job_ptr_pend->wckey = xstrdup(job_ptr->wckey);

	job_details = job_ptr->details;
	details_new = job_ptr_pend->details;
	memcpy(details_new, job_details, sizeof(struct job_details));
	details_new->acctg_freq = xstrdup(job_details->acctg_freq);
	if (job_details->argc) {
		details_new->argv =
			xmalloc(sizeof(char *) * (job_details->argc + 1));
		for (i = 0; i < job_details->argc; i++) {
			details_new->argv[i] = xstrdup(job_details->argv[i]);
		}
	}
	details_new->ckpt_dir = xstrdup(job_details->ckpt_dir);
	details_new->cpu_bind = xstrdup(job_details->cpu_bind);
	details_new->cpu_bind_type = job_details->cpu_bind_type;
	details_new->depend_list = depended_list_copy(job_details->depend_list);
	details_new->dependency = xstrdup(job_details->dependency);
	details_new->orig_dependency = xstrdup(job_details->orig_dependency);
	if (job_details->env_cnt) {
		details_new->env_sup =
			xmalloc(sizeof(char *) * (job_details->env_cnt + 1));
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
	if (job_details->mc_ptr) {
		i = sizeof(multi_core_data_t);
		details_new->mc_ptr = xmalloc(i);
		memcpy(details_new->mc_ptr, job_details->mc_ptr, i);
	}
	details_new->mem_bind = xstrdup(job_details->mem_bind);
	details_new->mem_bind_type = job_details->mem_bind_type;
	if (job_details->req_node_bitmap) {
		details_new->req_node_bitmap =
			bit_copy(job_details->req_node_bitmap);
	}
	if (job_details->req_node_layout && job_details->req_node_bitmap) {
		i = bit_set_count(job_details->req_node_bitmap) *
		    sizeof(uint16_t);
		details_new->req_node_layout = xmalloc(i);
		memcpy(details_new->req_node_layout,
		       job_details->req_node_layout, i);
	}
	details_new->req_nodes = xstrdup(job_details->req_nodes);
	details_new->restart_dir = xstrdup(job_details->restart_dir);
	details_new->std_err = xstrdup(job_details->std_err);
	details_new->std_in = xstrdup(job_details->std_in);
	details_new->std_out = xstrdup(job_details->std_out);
	details_new->work_dir = xstrdup(job_details->work_dir);

	return job_ptr_pend;
}

/* Add job array data stucture to the job record */
static void _create_job_array(struct job_record *job_ptr,
			      job_desc_msg_t *job_specs)
{
	char *sep = NULL;
	int max_run_tasks;
	uint32_t i_cnt;

	if (!job_specs->array_bitmap)
		return;

	i_cnt = bit_set_count(job_specs->array_bitmap);
	if (i_cnt == 0) {
		info("_create_job_array: job %u array_bitmap is empty",
		     job_ptr->job_id);
		return;
	}

	job_ptr->array_job_id = job_ptr->job_id;
	job_ptr->array_recs = xmalloc(sizeof(job_array_struct_t));
	i_cnt = bit_fls(job_specs->array_bitmap) + 1;
	job_specs->array_bitmap = bit_realloc(job_specs->array_bitmap, i_cnt);
	job_ptr->array_recs->task_id_bitmap = job_specs->array_bitmap;
	job_specs->array_bitmap = NULL;
	job_ptr->array_recs->task_cnt =
		bit_set_count(job_ptr->array_recs->task_id_bitmap);
	if (job_ptr->array_recs->task_cnt > 1)
		job_count += (job_ptr->array_recs->task_cnt - 1);

	if (job_specs->array_inx)
		sep = strchr(job_specs->array_inx, '%');
	if (sep) {
		max_run_tasks = atoi(sep + 1);
		if (max_run_tasks > 0)
			job_ptr->array_recs->max_run_tasks = max_run_tasks;
	}
}

/*
 * Wrapper for select_nodes() function that will test all valid partitions
 * for a new job
 * IN job_ptr - pointer to the job record
 * IN test_only - if set do not allocate nodes, just confirm they
 *	could be allocated now
 * IN select_node_bitmap - bitmap of nodes to be used for the
 *	job's resource allocation (not returned if NULL), caller
 *	must free
 * OUT err_msg - error message for job, caller must xfree
 */
static int _select_nodes_parts(struct job_record *job_ptr, bool test_only,
			       bitstr_t **select_node_bitmap, char **err_msg)
{
	struct part_record *part_ptr;
	ListIterator iter;
	int rc = ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE;

	if (job_ptr->part_ptr_list) {
		iter = list_iterator_create(job_ptr->part_ptr_list);
		while ((part_ptr = list_next(iter))) {
			job_ptr->part_ptr = part_ptr;
			debug2("Try job %u on next partition %s",
			       job_ptr->job_id, part_ptr->name);
			if (job_limits_check(&job_ptr, false) != WAIT_NO_REASON)
				continue;
			rc = select_nodes(job_ptr, test_only,
					  select_node_bitmap, err_msg);
			if ((rc != ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE) &&
			    (rc != ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE) &&
			    (rc != ESLURM_RESERVATION_BUSY) &&
			    (rc != ESLURM_NODES_BUSY))
				break;
			if ((job_ptr->preempt_in_progress) &&
			    (rc != ESLURM_NODES_BUSY))
				break;
		}
		list_iterator_destroy(iter);
	} else {
		if (job_limits_check(&job_ptr, false) != WAIT_NO_REASON)
			test_only = true;
		rc = select_nodes(job_ptr, test_only, select_node_bitmap,
				  err_msg);
	}

	return rc;
}

/*
 * job_allocate - create job_records for the supplied job specification and
 *	allocate nodes for it.
 * IN job_specs - job specifications
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
 * NOTE: lock_slurmctld on entry: Read config Write job, Write node, Read part
 */
extern int job_allocate(job_desc_msg_t * job_specs, int immediate,
			int will_run, will_run_response_msg_t **resp,
			int allocate, uid_t submit_uid,
			struct job_record **job_pptr, char **err_msg,
			uint16_t protocol_version)
{
	static int defer_sched = -1;
	int error_code, i;
	bool no_alloc, top_prio, test_only, too_fragmented, independent;
	struct job_record *job_ptr;
	time_t now = time(NULL);

	if (job_specs->array_bitmap) {
		i = bit_set_count(job_specs->array_bitmap);
		if ((job_count + i) >= slurmctld_conf.max_job_cnt) {
			info("%s: MaxJobCount limit reached (%d + %d >= %u)",
			     __func__, job_count, i,
			     slurmctld_conf.max_job_cnt);
			return EAGAIN;
		}
	} else if (job_count >= slurmctld_conf.max_job_cnt) {
		info("%s: MaxJobCount limit reached (%u)",
		     __func__, slurmctld_conf.max_job_cnt);
		return EAGAIN;
	}

	error_code = _job_create(job_specs, allocate, will_run,
				 &job_ptr, submit_uid, err_msg,
				 protocol_version);
	*job_pptr = job_ptr;

	if (error_code) {
		if (job_ptr && (immediate || will_run)) {
			/* this should never really happen here */
			job_ptr->job_state = JOB_FAILED;
			job_ptr->exit_code = 1;
			job_ptr->state_reason = FAIL_BAD_CONSTRAINTS;
			xfree(job_ptr->state_desc);
			job_ptr->start_time = job_ptr->end_time = now;
			job_completion_logger(job_ptr, false);
		}
		return error_code;
	}
	xassert(job_ptr);
	if (job_specs->array_bitmap)
		independent = false;
	else
		independent = job_independent(job_ptr, will_run);
	/* priority needs to be calculated after this since we set a
	 * begin time in job_independent and that lets us know if the
	 * job is eligible.
	 */
	if (job_ptr->priority == NO_VAL)
		set_job_prio(job_ptr);

	if (independent &&
	    (license_job_test(job_ptr, time(NULL)) != SLURM_SUCCESS))
		independent = false;

	/* Avoid resource fragmentation if important */
	if ((submit_uid || (job_specs->req_nodes == NULL)) &&
	    independent && job_is_completing())
		too_fragmented = true;	/* Don't pick nodes for job now */
	/* FIXME: Ideally we only want to refuse the request if the
	 * required node list is insufficient to satisfy the job's
	 * processor or node count requirements, but the overhead is
	 * rather high to do that right here. We let requests from
	 * user root proceed if a node list is specified, for
	 * meta-schedulers (e.g. LCRM). */
	else
		too_fragmented = false;

	if (defer_sched == -1) {
		char *sched_params = slurm_get_sched_params();
		if (sched_params && strstr(sched_params, "defer"))
			defer_sched = 1;
		else
			defer_sched = 0;
		xfree(sched_params);
	}
	if (defer_sched == 1)
		too_fragmented = true;

	if (independent && (!too_fragmented))
		top_prio = _top_priority(job_ptr);
	else
		top_prio = true;	/* don't bother testing,
					 * it is not runable anyway */
	if (immediate && (too_fragmented || (!top_prio) || (!independent))) {
		job_ptr->job_state  = JOB_FAILED;
		job_ptr->exit_code  = 1;
		job_ptr->state_reason = FAIL_BAD_CONSTRAINTS;
		xfree(job_ptr->state_desc);
		job_ptr->start_time = job_ptr->end_time = now;
		job_completion_logger(job_ptr, false);
		if (!independent)
			return ESLURM_DEPENDENCY;
		else if (too_fragmented)
			return ESLURM_FRAGMENTATION;
		else
			return ESLURM_NOT_TOP_PRIORITY;
	}

	if (will_run && resp) {
		job_desc_msg_t job_desc_msg;
		int rc;
		memset(&job_desc_msg, 0, sizeof(job_desc_msg_t));
		job_desc_msg.job_id = job_ptr->job_id;
		rc = job_start_data(&job_desc_msg, resp);
		job_ptr->job_state  = JOB_FAILED;
		job_ptr->exit_code  = 1;
		job_ptr->start_time = job_ptr->end_time = now;
		_purge_job_record(job_ptr->job_id);
		return rc;
	}

	test_only = will_run || (allocate == 0);

	no_alloc = test_only || too_fragmented ||
		   (!top_prio) || (!independent) || !avail_front_end(job_ptr);
	error_code = _select_nodes_parts(job_ptr, no_alloc, NULL, err_msg);
	if (!test_only) {
		last_job_update = now;
		slurm_sched_g_schedule();	/* work for external scheduler */
	}

	slurmctld_diag_stats.jobs_submitted++;
	acct_policy_add_job_submit(job_ptr);

	if ((error_code == ESLURM_NODES_BUSY) ||
	    (error_code == ESLURM_RESERVATION_BUSY) ||
	    (error_code == ESLURM_JOB_HELD) ||
	    (error_code == ESLURM_NODE_NOT_AVAIL) ||
	    (error_code == ESLURM_QOS_THRES) ||
	    (error_code == ESLURM_ACCOUNTING_POLICY) ||
	    (error_code == ESLURM_RESERVATION_NOT_USABLE) ||
	    (error_code == ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE)) {
		/* Not fatal error, but job can't be scheduled right now */
		if (immediate) {
			job_ptr->job_state  = JOB_FAILED;
			job_ptr->exit_code  = 1;
			job_ptr->state_reason = FAIL_BAD_CONSTRAINTS;
			xfree(job_ptr->state_desc);
			job_ptr->start_time = job_ptr->end_time = now;
			job_completion_logger(job_ptr, false);
		} else {	/* job remains queued */
			_create_job_array(job_ptr, job_specs);
			if ((error_code == ESLURM_NODES_BUSY) ||
			    (error_code == ESLURM_RESERVATION_BUSY) ||
			    (error_code == ESLURM_ACCOUNTING_POLICY)) {
				error_code = SLURM_SUCCESS;
			}
		}
		return error_code;
	}

	if (error_code) {	/* fundamental flaw in job request */
		job_ptr->job_state  = JOB_FAILED;
		job_ptr->exit_code  = 1;
		job_ptr->state_reason = FAIL_BAD_CONSTRAINTS;
		xfree(job_ptr->state_desc);
		job_ptr->start_time = job_ptr->end_time = now;
		job_completion_logger(job_ptr, false);
		return error_code;
	}

	if (will_run) {		/* job would run, flag job destruction */
		job_ptr->job_state  = JOB_FAILED;
		job_ptr->exit_code  = 1;
		job_ptr->start_time = job_ptr->end_time = now;
		_purge_job_record(job_ptr->job_id);
	} else if (!with_slurmdbd && !job_ptr->db_index)
		jobacct_storage_g_job_start(acct_db_conn, job_ptr);

	if (!will_run) {
		_create_job_array(job_ptr, job_specs);
		debug2("sched: JobId=%u allocated resources: NodeList=%s",
		       job_ptr->job_id, job_ptr->nodes);
		rebuild_job_part_list(job_ptr);
	}

	return SLURM_SUCCESS;
}

/*
 * job_fail - terminate a job due to initiation failure
 * IN job_id - id of the job to be killed
 * IN job_state - desired job state (JOB_BOOT_FAIL, JOB_NODE_FAIL, etc.)
 * RET 0 on success, otherwise ESLURM error code
 */
extern int job_fail(uint32_t job_id, uint16_t job_state)
{
	struct job_record *job_ptr;
	time_t now = time(NULL);
	bool suspended = false;

	job_ptr = find_job_record(job_id);
	if (job_ptr == NULL) {
		error("job_fail: invalid job id %u", job_id);
		return ESLURM_INVALID_JOB_ID;
	}

	if (IS_JOB_FINISHED(job_ptr))
		return ESLURM_ALREADY_DONE;
	if (IS_JOB_SUSPENDED(job_ptr)) {
		enum job_states suspend_job_state = job_ptr->job_state;
		/* we can't have it as suspended when we call the
		 * accounting stuff.
		 */
		job_ptr->job_state = JOB_CANCELLED;
		jobacct_storage_g_job_suspend(acct_db_conn, job_ptr);
		job_ptr->job_state = suspend_job_state;
		suspended = true;
	}

	if (IS_JOB_RUNNING(job_ptr) || suspended) {
		/* No need to signal steps, deallocate kills them */
		job_ptr->time_last_active       = now;
		if (suspended) {
			job_ptr->end_time       = job_ptr->suspend_time;
			job_ptr->tot_sus_time  +=
				difftime(now, job_ptr->suspend_time);
		} else
			job_ptr->end_time       = now;
		last_job_update                 = now;
		job_ptr->job_state = job_state | JOB_COMPLETING;
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
	verbose("job_fail: job %u can't be killed from state=%s",
		job_id, job_state_string(job_ptr->job_state));
	return ESLURM_TRANSITION_STATE_NO_UPDATE;

}

/* Signal a job based upon job pointer.
 * Authentication and authorization checks must be performed before calling. */
static int _job_signal(struct job_record *job_ptr, uint16_t signal,
		       uint16_t flags, uid_t uid, bool preempt)
{
	uint16_t job_term_state;
	char jbuf[JBUFSIZ];
	time_t now = time(NULL);

	trace_job(job_ptr, __func__, "enter");

	if (IS_JOB_FINISHED(job_ptr))
		return ESLURM_ALREADY_DONE;

	/* let node select plugin do any state-dependent signalling actions */
	select_g_job_signal(job_ptr, signal);

	/* save user ID of the one who requested the job be cancelled */
	if (signal == SIGKILL)
		job_ptr->requid = uid;
	if (IS_JOB_PENDING(job_ptr) && IS_JOB_COMPLETING(job_ptr) &&
	    (signal == SIGKILL)) {
		if ((job_ptr->job_state & JOB_STATE_BASE) == JOB_PENDING) {
			/* Prevent job requeue, otherwise preserve state */
			job_ptr->job_state = JOB_CANCELLED | JOB_COMPLETING;
		}
		/* build_cg_bitmap() not needed, job already completing */
		verbose("%s: of requeuing %s successful",
			__func__, jobid2str(job_ptr, jbuf));
		return SLURM_SUCCESS;
	}

	if (IS_JOB_PENDING(job_ptr) && (signal == SIGKILL)) {
		last_job_update		= now;
		job_ptr->job_state	= JOB_CANCELLED;
		job_ptr->start_time	= now;
		job_ptr->end_time	= now;
		srun_allocate_abort(job_ptr);
		job_completion_logger(job_ptr, false);
		verbose("%s: of pending %s successful",
			__func__, jobid2str(job_ptr, jbuf));
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
		job_ptr->job_state      = job_term_state | JOB_COMPLETING;
		build_cg_bitmap(job_ptr);
		jobacct_storage_g_job_suspend(acct_db_conn, job_ptr);
		job_completion_logger(job_ptr, false);
		deallocate_nodes(job_ptr, false, true, preempt);
		verbose("%s: %u of suspended %s successful",
			__func__, signal, jobid2str(job_ptr, jbuf));
		return SLURM_SUCCESS;
	}

	if (IS_JOB_RUNNING(job_ptr)) {
		if ((signal == SIGKILL)
		    && !(flags & KILL_STEPS_ONLY)
		    && !(flags & KILL_JOB_BATCH)) {
			/* No need to signal steps, deallocate kills them
			 */
			job_ptr->time_last_active	= now;
			job_ptr->end_time		= now;
			last_job_update			= now;
			job_ptr->job_state = job_term_state | JOB_COMPLETING;
			build_cg_bitmap(job_ptr);
			job_completion_logger(job_ptr, false);
			deallocate_nodes(job_ptr, false, false, preempt);
		} else if (job_ptr->batch_flag
			   && (flags & KILL_STEPS_ONLY
			       || flags & KILL_JOB_BATCH)) {
			_signal_batch_job(job_ptr, signal, flags);
		} else if ((flags & KILL_JOB_BATCH) && !job_ptr->batch_flag) {
			return ESLURM_JOB_SCRIPT_MISSING;
		} else {
			_signal_job(job_ptr, signal);
		}
		verbose("%s: %u of running %s successful 0x%x",
			__func__, signal, jobid2str(job_ptr, jbuf),
			job_ptr->job_state);
		return SLURM_SUCCESS;
	}

	verbose("%s: %s can't be sent signal %u from state=%s",
		__func__, jobid2str(job_ptr, jbuf), signal,
		job_state_string(job_ptr->job_state));

	trace_job(job_ptr, __func__, "return");

	return ESLURM_TRANSITION_STATE_NO_UPDATE;
}

/*
 * job_signal - signal the specified job
 * IN job_id - id of the job to be signaled
 * IN signal - signal to send, SIGKILL == cancel the job
 * IN flags  - see KILL_JOB_* flags in slurm.h
 * IN uid - uid of requesting user
 * IN preempt - true if job being preempted
 * RET 0 on success, otherwise ESLURM error code
 */
extern int job_signal(uint32_t job_id, uint16_t signal, uint16_t flags,
		      uid_t uid, bool preempt)
{
	struct job_record *job_ptr;

	/* Jobs submitted using Moab command should be cancelled using
	 * Moab command for accurate job records */
	if (!wiki_sched_test) {
		char *sched_type = slurm_get_sched_type();
		if (strcmp(sched_type, "sched/wiki") == 0)
			wiki_sched  = true;
		if (strcmp(sched_type, "sched/wiki2") == 0) {
			wiki_sched  = true;
			wiki2_sched = true;
		}
		xfree(sched_type);
		wiki_sched_test = true;
	}

	job_ptr = find_job_record(job_id);
	if (job_ptr == NULL) {
		info("job_signal: invalid job id %u", job_id);
		return ESLURM_INVALID_JOB_ID;
	}

	if ((job_ptr->user_id != uid) && !validate_operator(uid) &&
	    !assoc_mgr_is_user_acct_coord(acct_db_conn, uid,
					  job_ptr->account)) {
		error("Security violation, JOB_CANCEL RPC for jobID %u from "
		      "uid %d", job_ptr->job_id, uid);
		return ESLURM_ACCESS_DENIED;
	}
	if (!validate_slurm_user(uid) && (signal == SIGKILL) &&
	    job_ptr->part_ptr &&
	    (job_ptr->part_ptr->flags & PART_FLAG_ROOT_ONLY) && wiki2_sched) {
		info("Attempt to cancel Moab job using Slurm command from "
		     "uid %d", uid);
		return ESLURM_ACCESS_DENIED;
	}

	return _job_signal(job_ptr, signal, flags, uid, preempt);
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
	slurm_ctl_conf_t *conf;
	struct job_record *job_ptr;
	uint32_t job_id;
	time_t now = time(NULL);
	char *end_ptr = NULL, *tok, *tmp;
	long int long_id;
	bitstr_t *array_bitmap = NULL, *tmp_bitmap;
	bool valid = true;
	int32_t i, i_first, i_last;
	int rc = SLURM_SUCCESS, rc2, len;

	/* Jobs submitted using Moab command should be cancelled using
	 * Moab command for accurate job records */
	if (!wiki_sched_test) {
		char *sched_type = slurm_get_sched_type();
		if (strcmp(sched_type, "sched/wiki") == 0)
			wiki_sched  = true;
		if (strcmp(sched_type, "sched/wiki2") == 0) {
			wiki_sched  = true;
			wiki2_sched = true;
		}
		xfree(sched_type);
		wiki_sched_test = true;
	}
	if (max_array_size == NO_VAL) {
		conf = slurm_conf_lock();
		max_array_size = conf->max_array_sz;
		slurm_conf_unlock();
	}

	long_id = strtol(job_id_str, &end_ptr, 10);
	if ((long_id <= 0) || (long_id == LONG_MAX) ||
	    ((end_ptr[0] != '\0') && (end_ptr[0] != '_'))) {
		info("%s: 1 invalid job id %s", __func__, job_id_str);
		return ESLURM_INVALID_JOB_ID;
	}
	job_id = (uint32_t) long_id;
	if (end_ptr[0] == '\0') {	/* Single job (or full job array) */
		int jobs_done = 0, jobs_signalled = 0;
		struct job_record *job_ptr_done = NULL;
		job_ptr = find_job_record(job_id);
		if (job_ptr && (job_ptr->array_task_id == NO_VAL) &&
		    (job_ptr->array_recs == NULL)) {
			/* This is a regular job, not a job array */
			return job_signal(job_id, signal, flags, uid, preempt);
		}

		if (job_ptr && job_ptr->array_recs) {
			/* This is a job array */
			job_ptr_done = job_ptr;
			rc = _job_signal(job_ptr, signal, flags, uid, preempt);
			jobs_signalled++;
			if (rc == ESLURM_ALREADY_DONE) {
				jobs_done++;
				rc = SLURM_SUCCESS;
			}
		}

		/* Signal all tasks of this job array */
		job_ptr = job_array_hash_j[JOB_HASH_INX(job_id)];
		if (!job_ptr && !job_ptr_done) {
			info("%s: 2 invalid job id %u", __func__, job_id);
			return ESLURM_INVALID_JOB_ID;
		}
		while (job_ptr) {
			if ((job_ptr->array_job_id == job_id) &&
			    (job_ptr != job_ptr_done)) {
				rc2 = _job_signal(job_ptr, signal, flags, uid,
						  preempt);
				jobs_signalled++;
				if (rc2 == ESLURM_ALREADY_DONE) {
					jobs_done++;
				} else {
					rc = MAX(rc, rc2);
				}
			}
			job_ptr = job_ptr->job_array_next_j;
		}
		if ((rc == SLURM_SUCCESS) && (jobs_done == jobs_signalled))
			return ESLURM_ALREADY_DONE;
		return rc;

	}

	array_bitmap = bit_alloc(max_array_size);
	tmp = xstrdup(end_ptr + 1);
	tok = strtok_r(tmp, ",", &end_ptr);
	while (tok && valid) {
		valid = _parse_array_tok(tok, array_bitmap,
					 max_array_size);
		tok = strtok_r(NULL, ",", &end_ptr);
	}
	xfree(tmp);
	if (valid) {
		i_last = bit_fls(array_bitmap);
		if (i_last < 0)
			valid = false;
	}
	if (!valid) {
		info("%s: 3 invalid job id %s", __func__, job_id_str);
		rc = ESLURM_INVALID_JOB_ID;
		goto endit;
	}

	/* Find some job record and validate the user cancelling the job */
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
	     (job_ptr->array_recs == NULL))) {
		info("%s: 4 invalid job id %s", __func__, job_id_str);
		rc = ESLURM_INVALID_JOB_ID;
		goto endit;
	}

	if ((job_ptr->user_id != uid) && !validate_operator(uid) &&
	    !assoc_mgr_is_user_acct_coord(acct_db_conn, uid,
					  job_ptr->account)) {
		error("%s: Security violation JOB_CANCEL RPC for jobID %s from "
		      "uid %d", __func__, job_id_str, uid);
		rc = ESLURM_ACCESS_DENIED;
		goto endit;
	}
	if (!validate_slurm_user(uid) && (signal == SIGKILL) &&
	    job_ptr->part_ptr &&
	    (job_ptr->part_ptr->flags & PART_FLAG_ROOT_ONLY) && wiki2_sched) {
		info("%s: Attempt to cancel Moab job using Slurm command from "
		     "uid %d", __func__, uid);
		rc = ESLURM_ACCESS_DENIED;
		goto endit;
	}

	if (IS_JOB_PENDING(job_ptr) &&
	    job_ptr->array_recs && job_ptr->array_recs->task_id_bitmap) {
		/* Ensure bitmap sizes match for AND operations */
		len = bit_size(job_ptr->array_recs->task_id_bitmap);
		i_last++;
		if (i_last < len) {
			array_bitmap = bit_realloc(array_bitmap, len);
		} else {
			array_bitmap = bit_realloc(array_bitmap, i_last);
			job_ptr->array_recs->task_id_bitmap = bit_realloc(
				job_ptr->array_recs->task_id_bitmap, i_last);
		}
		tmp_bitmap = bit_copy(job_ptr->array_recs->task_id_bitmap);
		if (signal == SIGKILL) {
			uint32_t orig_task_cnt, new_task_count;
			bit_not(array_bitmap);
			bit_and(job_ptr->array_recs->task_id_bitmap,
				array_bitmap);
			xfree(job_ptr->array_recs->task_id_str);
			bit_not(array_bitmap);
			orig_task_cnt = job_ptr->array_recs->task_cnt;
			new_task_count = bit_set_count(job_ptr->array_recs->
						       task_id_bitmap);
			job_ptr->array_recs->task_cnt = new_task_count;
			job_count -= (orig_task_cnt - new_task_count);
			if (job_ptr->array_recs->task_cnt == 0) {
				last_job_update		= now;
				job_ptr->job_state	= JOB_CANCELLED;
				job_ptr->start_time	= now;
				job_ptr->end_time	= now;
				job_ptr->requid		= uid;
				srun_allocate_abort(job_ptr);
				job_completion_logger(job_ptr, false);
			}
			bit_not(tmp_bitmap);
			bit_and(array_bitmap, tmp_bitmap);
			FREE_NULL_BITMAP(tmp_bitmap);
		} else {
			bit_not(tmp_bitmap);
			bit_and(array_bitmap, tmp_bitmap);
			FREE_NULL_BITMAP(tmp_bitmap);
			rc = ESLURM_TRANSITION_STATE_NO_UPDATE;
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
			info("%s: 5 invalid job id %u_%d", __func__, job_id, i);
			rc = ESLURM_INVALID_JOB_ID;
			continue;
		}

		rc2 = _job_signal(job_ptr, signal, flags, uid, preempt);
		rc = MAX(rc, rc2);
	}
endit:
	FREE_NULL_BITMAP(array_bitmap);

	return rc;
}

static void
_signal_batch_job(struct job_record *job_ptr, uint16_t signal, uint16_t flags)
{
	bitoff_t i;
	kill_tasks_msg_t *kill_tasks_msg = NULL;
	agent_arg_t *agent_args = NULL;
	uint32_t z = 0;

	xassert(job_ptr);
	xassert(job_ptr->batch_host);
	i = bit_ffs(job_ptr->node_bitmap);
	if (i < 0) {
		error("%s: JobId=%u lacks assigned nodes",
		      __func__, job_ptr->job_id);
		return;
	}
	if (flags > 0xf) {	/* Top 4 bits used for KILL_* flags */
		error("%s: signal flags %u for job %u exceed limit",
		      __func__, flags, job_ptr->job_id);
		return;
	}
	if (signal > 0xfff) {	/* Top 4 bits used for KILL_* flags */
		error("%s: signal value %u for job %u exceed limit",
		      __func__, signal, job_ptr->job_id);
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
	struct node_record *node_ptr;
	if ((node_ptr = find_node_record(job_ptr->batch_host)))
		agent_args->protocol_version = node_ptr->protocol_version;
#endif
	agent_args->hostlist	= hostlist_create(job_ptr->batch_host);
	kill_tasks_msg = xmalloc(sizeof(kill_tasks_msg_t));
	kill_tasks_msg->job_id      = job_ptr->job_id;
	kill_tasks_msg->job_step_id = NO_VAL;

	/* Encode the KILL_JOB_BATCH|KILL_STEPS_ONLY flags for stepd to know if
	 * has to signal only the batch script or only the steps.
	 * The job was submitted using the --signal=B:sig
	 * or without B sbatch option.
	 */
	if (flags == KILL_JOB_BATCH)
		z = KILL_JOB_BATCH << 24;
	else if (flags == KILL_STEPS_ONLY)
		z = KILL_STEPS_ONLY << 24;

	kill_tasks_msg->signal = z | signal;

	agent_args->msg_args = kill_tasks_msg;
	agent_args->node_count = 1;/* slurm/477 be sure to update node_count */
	agent_queue_request(agent_args);
	return;
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
extern int prolog_complete(uint32_t job_id,
			   uint32_t prolog_return_code)
{
	struct job_record *job_ptr;

	debug("completing prolog for job %u", job_id);
	job_ptr = find_job_record(job_id);
	if (job_ptr == NULL) {
		info("prolog_complete: invalid JobId=%u", job_id);
		return ESLURM_INVALID_JOB_ID;
	}

	if (IS_JOB_COMPLETING(job_ptr))
		return SLURM_SUCCESS;

	if (prolog_return_code)
		error("Prolog launch failure, JobId=%u", job_ptr->job_id);

	job_ptr->state_reason = WAIT_NO_REASON;

	return SLURM_SUCCESS;
}

/*
 * job_complete - note the normal termination the specified job
 * IN job_id - id of the job which completed
 * IN uid - user id of user issuing the RPC
 * IN requeue - job should be run again if possible
 * IN node_fail - true of job terminated due to node failure
 * IN job_return_code - job's return code, if set then set state to FAILED
 * RET - 0 on success, otherwise ESLURM error code
 * global: job_list - pointer global job list
 *	last_job_update - time of last job table update
 */
extern int job_complete(uint32_t job_id, uid_t uid, bool requeue,
			bool node_fail, uint32_t job_return_code)
{
	struct node_record *node_ptr;
	struct job_record *job_ptr;
	time_t now = time(NULL);
	uint32_t job_comp_flag = 0;
	bool suspended = false;
	char jbuf[JBUFSIZ];
	int i;
	int use_cloud = false;

	job_ptr = find_job_record(job_id);
	if (job_ptr == NULL) {
		info("%s: invalid JobId=%u", __func__, job_id);
		return ESLURM_INVALID_JOB_ID;
	}

	info("%s: %s WIFEXITED %d WEXITSTATUS %d",
	     __func__, jobid2str(job_ptr, jbuf),
	     WIFEXITED(job_return_code), WEXITSTATUS(job_return_code));

	if (IS_JOB_FINISHED(job_ptr)) {
		if (job_ptr->exit_code == 0)
			job_ptr->exit_code = job_return_code;
		return ESLURM_ALREADY_DONE;
	}

	if ((job_ptr->user_id != uid) && !validate_slurm_user(uid)) {
		error("%s: Security violation, JOB_COMPLETE RPC for job %u "
		      "from uid %u", __func__,
		      job_ptr->job_id, (unsigned int) uid);
		return ESLURM_USER_ID_MISSING;
	}

	if (IS_JOB_COMPLETING(job_ptr))
		return SLURM_SUCCESS;	/* avoid replay */

	if (IS_JOB_RUNNING(job_ptr))
		job_comp_flag = JOB_COMPLETING;
	else if (IS_JOB_PENDING(job_ptr)) {
		job_return_code = NO_VAL;
		job_ptr->start_time = now;
	}

	if ((job_return_code == NO_VAL) &&
	    (IS_JOB_RUNNING(job_ptr) || IS_JOB_PENDING(job_ptr)))
		info("%s: %s cancelled from interactive user or node failure",
		     __func__, jobid2str(job_ptr, jbuf));

	if (IS_JOB_SUSPENDED(job_ptr)) {
		enum job_states suspend_job_state = job_ptr->job_state;
		/* we can't have it as suspended when we call the
		 * accounting stuff.
		 */
		job_ptr->job_state = JOB_CANCELLED;
		jobacct_storage_g_job_suspend(acct_db_conn, job_ptr);
		job_ptr->job_state = suspend_job_state;
		job_comp_flag = JOB_COMPLETING;
		suspended = true;
	}

	if (requeue && job_ptr->details && job_ptr->batch_flag) {
		/* We want this job to look like it
		 * was terminated in the accounting logs.
		 * Set a new submit time so the restarted
		 * job looks like a new job. */
		job_ptr->end_time = now;
		job_ptr->job_state  = JOB_NODE_FAIL;
		job_completion_logger(job_ptr, true);
		/* do this after the epilog complete, setting it here
		 * is too early */
		//job_ptr->db_index = 0;
		//job_ptr->details->submit_time = now + 1;
		if (job_ptr->node_bitmap) {
			i = bit_ffs(job_ptr->node_bitmap);
			if (i >= 0) {
				node_ptr = node_record_table_ptr + i;
				if (IS_NODE_CLOUD(node_ptr))
					use_cloud = true;
			}
		}
		if (!use_cloud)
			job_ptr->batch_flag++;	/* only one retry */
		job_ptr->restart_cnt++;
		job_ptr->job_state = JOB_PENDING | job_comp_flag;
		/* Since the job completion logger removes the job submit
		 * information, we need to add it again. */
		acct_policy_add_job_submit(job_ptr);
		if (node_fail) {
			info("%s: requeue %s due to node failure",
			     __func__, jobid2str(job_ptr, jbuf));
		} else {
			info("%s: requeue %s per user/system request",
			     __func__, jobid2str(job_ptr, jbuf));
		}
		/* We have reached the maximum number of requeue
		 * attempts hold the job with HoldMaxRequeue reason.
		 */
		if (job_ptr->batch_flag > MAX_BATCH_REQUEUE) {
			job_ptr->job_state |= JOB_REQUEUE_HOLD;
			job_ptr->state_reason = WAIT_MAX_REQUEUE;
			job_ptr->batch_flag = 1;
			job_ptr->priority = 0;
		}
	} else if (IS_JOB_PENDING(job_ptr) && job_ptr->details &&
		   job_ptr->batch_flag) {
		/* Possible failure mode with DOWN node and job requeue.
		 * The DOWN node might actually respond to the cancel and
		 * take us here.  Don't run job_completion_logger
		 * here since this is here to catch duplicate cancels
		 * from slow responding slurmds */
		return SLURM_SUCCESS;
	} else {
		if (node_fail) {
			job_ptr->job_state = JOB_NODE_FAIL | job_comp_flag;
			job_ptr->requid = uid;
		} else if (job_return_code == NO_VAL) {
			job_ptr->job_state = JOB_CANCELLED | job_comp_flag;
			job_ptr->requid = uid;
		} else if (WIFEXITED(job_return_code) &&
			   WEXITSTATUS(job_return_code)) {
			job_ptr->job_state = JOB_FAILED   | job_comp_flag;
			job_ptr->exit_code = job_return_code;
			job_ptr->state_reason = FAIL_EXIT_CODE;
			xfree(job_ptr->state_desc);
		} else if (job_comp_flag
		           && ((job_ptr->end_time
		                + slurmctld_conf.over_time_limit * 60) < now)) {
			/* Test if the job has finished before its allowed
			 * over time has expired.
			 */
			job_ptr->job_state = JOB_TIMEOUT  | job_comp_flag;
			job_ptr->exit_code = MAX(job_ptr->exit_code, 1);
			job_ptr->state_reason = FAIL_TIMEOUT;
			xfree(job_ptr->state_desc);
		} else {
			job_ptr->job_state = JOB_COMPLETE | job_comp_flag;
			job_ptr->exit_code = job_return_code;
			if (nonstop_ops.job_fini)
				(nonstop_ops.job_fini)(job_ptr);
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

	info("%s: %s done", __func__, jobid2str(job_ptr, jbuf));

	return SLURM_SUCCESS;
}

static int _alt_part_test(struct part_record *part_ptr,
			  struct part_record **part_ptr_new)
{
	struct part_record *alt_part_ptr = NULL;
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

/* Test if this job can use this partition */
static int _part_access_check(struct part_record *part_ptr,
			      job_desc_msg_t * job_desc, bitstr_t *req_bitmap,
			      uid_t submit_uid, slurmdb_qos_rec_t *qos_ptr,
			      char *acct)
{
	uint32_t total_nodes;
	size_t resv_name_leng = 0;
	int rc = SLURM_SUCCESS;

	if (job_desc->reservation != NULL) {
		resv_name_leng = strlen(job_desc->reservation);
	}

	if ((part_ptr->flags & PART_FLAG_REQ_RESV) &&
		((job_desc->reservation == NULL) ||
		(resv_name_leng == 0))) {
		info("_part_access_check: uid %u access to partition %s "
		     "denied, requires reservation",
		     (unsigned int) submit_uid, part_ptr->name);
		return ESLURM_ACCESS_DENIED;
	}


	if ((part_ptr->flags & PART_FLAG_REQ_RESV) &&
	    (!job_desc->reservation || !strlen(job_desc->reservation))) {
		info("_part_access_check: uid %u access to partition %s "
		     "denied, requires reservation",
		     (unsigned int) submit_uid, part_ptr->name);
		return ESLURM_ACCESS_DENIED;
	}

	if ((part_ptr->flags & PART_FLAG_ROOT_ONLY) && (submit_uid != 0) &&
	    (submit_uid != slurmctld_conf.slurm_user_id)) {
		info("_part_access_check: uid %u access to partition %s "
		     "denied, not root",
		     (unsigned int) submit_uid, part_ptr->name);
		return ESLURM_ACCESS_DENIED;
	}

	if ((job_desc->user_id == 0) && (part_ptr->flags & PART_FLAG_NO_ROOT)) {
		error("_part_access_check: Security violation, SUBMIT_JOB for "
		      "user root disabled");
		return ESLURM_USER_ID_MISSING;
	}

	if (validate_group(part_ptr, job_desc->user_id) == 0) {
		info("_part_access_check: uid %u access to partition %s "
		     "denied, bad group",
		     (unsigned int) job_desc->user_id, part_ptr->name);
		return ESLURM_JOB_MISSING_REQUIRED_PARTITION_GROUP;
	}

	if (validate_alloc_node(part_ptr, job_desc->alloc_node) == 0) {
		info("_part_access_check: uid %u access to partition %s "
		     "denied, bad allocating node: %s",
		     (unsigned int) job_desc->user_id, part_ptr->name,
		     job_desc->alloc_node);
		return ESLURM_ACCESS_DENIED;
	}

	if ((part_ptr->state_up & PARTITION_SCHED) &&
	    (job_desc->min_cpus != NO_VAL) &&
	    (job_desc->min_cpus >  part_ptr->total_cpus)) {
		info("_part_access_check: Job requested too many cpus (%u) of "
		     "partition %s(%u)",
		     job_desc->min_cpus, part_ptr->name,
		     part_ptr->total_cpus);
		return ESLURM_TOO_MANY_REQUESTED_CPUS;
	}

	total_nodes = part_ptr->total_nodes;
	select_g_alter_node_cnt(SELECT_APPLY_NODE_MAX_OFFSET, &total_nodes);
	if ((part_ptr->state_up & PARTITION_SCHED) &&
	    (job_desc->min_nodes != NO_VAL) &&
	    (job_desc->min_nodes > total_nodes)) {
		info("_part_access_check: Job requested too many nodes (%u) "
		     "of partition %s(%u)",
		     job_desc->min_nodes, part_ptr->name, total_nodes);
		return ESLURM_INVALID_NODE_COUNT;
	}

	if (req_bitmap && !bit_super_set(req_bitmap, part_ptr->node_bitmap)) {
		info("_part_access_check: requested nodes %s not in "
		     "partition %s", job_desc->req_nodes, part_ptr->name);
		return ESLURM_REQUESTED_NODES_NOT_IN_PARTITION;
	}

	if (slurmctld_conf.enforce_part_limits) {
		if ((rc = part_policy_valid_acct(part_ptr, acct))
		    != SLURM_SUCCESS)
			goto fini;

		if ((rc = part_policy_valid_qos(part_ptr, qos_ptr))
		    != SLURM_SUCCESS)
			goto fini;
	}

fini:
	return rc;
}

static int _get_job_parts(job_desc_msg_t * job_desc,
			  struct part_record **part_pptr,
			  List *part_pptr_list)
{
	struct part_record *part_ptr = NULL, *part_ptr_new = NULL;
	List part_ptr_list = NULL;
	int rc = SLURM_SUCCESS;

	/* Identify partition(s) and set pointer(s) to their struct */
	if (job_desc->partition) {
		part_ptr = find_part_record(job_desc->partition);
		if (part_ptr == NULL) {
			part_ptr_list = get_part_list(job_desc->partition);
			if (part_ptr_list)
				part_ptr = list_peek(part_ptr_list);
		}
		if (part_ptr == NULL) {
			info("_valid_job_part: invalid partition specified: %s",
			     job_desc->partition);
			return ESLURM_INVALID_PARTITION_NAME;
		}
	} else {
		if (default_part_loc == NULL) {
			error("_valid_job_part: default partition not set");
			return ESLURM_DEFAULT_PARTITION_NOT_SET;
		}
		part_ptr = default_part_loc;
		job_desc->partition = xstrdup(part_ptr->name);
	}

	/* Change partition pointer(s) to alternates as needed */
	if (part_ptr_list) {
		int fail_rc = SLURM_SUCCESS;
		struct part_record *part_ptr_tmp;
		bool rebuild_name_list = false;
		ListIterator iter = list_iterator_create(part_ptr_list);

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
	*part_pptr_list = part_ptr_list;
	part_ptr_list = NULL;
fini:
	return rc;
}

static int _valid_job_part(job_desc_msg_t * job_desc,
			   uid_t submit_uid, bitstr_t *req_bitmap,
			   struct part_record **part_pptr,
			   List part_ptr_list,
			   slurmdb_association_rec_t *assoc_ptr,
			   slurmdb_qos_rec_t *qos_ptr)
{
	int rc = SLURM_SUCCESS;
	struct part_record *part_ptr = *part_pptr, *part_ptr_tmp;
	slurmdb_association_rec_t assoc_rec;
	uint32_t min_nodes_orig = INFINITE, max_nodes_orig = 1;
	uint32_t max_time = 0;

	/* Change partition pointer(s) to alternates as needed */
	if (part_ptr_list) {
		int fail_rc = SLURM_SUCCESS;
		bool rebuild_name_list = false;
		ListIterator iter = list_iterator_create(part_ptr_list);

		while ((part_ptr_tmp = (struct part_record *)list_next(iter))) {
			/* FIXME: When dealing with multiple partitions we
			 * currently can't deal with partition based
			 * associations.
			 */
			memset(&assoc_rec, 0,
			       sizeof(slurmdb_association_rec_t));
			if (assoc_ptr) {
				assoc_rec.acct      = assoc_ptr->acct;
				assoc_rec.partition = part_ptr_tmp->name;
				assoc_rec.uid       = job_desc->user_id;

				assoc_mgr_fill_in_assoc(
					acct_db_conn, &assoc_rec,
					accounting_enforce, NULL, false);
			}

			if (assoc_ptr && assoc_rec.id != assoc_ptr->id) {
				info("_valid_job_part: can't check multiple "
				     "partitions with partition based "
				     "associations");
				rc = SLURM_ERROR;
			} else
				rc = _part_access_check(part_ptr_tmp, job_desc,
							req_bitmap, submit_uid,
							qos_ptr, assoc_ptr ?
							assoc_ptr->acct : NULL);

			if (rc != SLURM_SUCCESS) {
				fail_rc = rc;
				list_remove(iter);
				rebuild_name_list = true;
				continue;
			}

			min_nodes_orig = MIN(min_nodes_orig,
					     part_ptr_tmp->min_nodes_orig);
			max_nodes_orig = MAX(max_nodes_orig,
					     part_ptr_tmp->max_nodes_orig);
			max_time = MAX(max_time, part_ptr_tmp->max_time);
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
			*part_pptr = part_ptr = NULL;
			xfree(job_desc->partition);
			iter = list_iterator_create(part_ptr_list);
			while ((part_ptr_tmp = list_next(iter))) {
				if (job_desc->partition)
					xstrcat(job_desc->partition, ",");
				else
					*part_pptr = part_ptr = part_ptr_tmp;
				xstrcat(job_desc->partition,
					part_ptr_tmp->name);
			}
			list_iterator_destroy(iter);
		}
	} else {
		min_nodes_orig = part_ptr->min_nodes_orig;
		max_nodes_orig = part_ptr->max_nodes_orig;
		max_time = part_ptr->max_time;
		rc = _part_access_check(part_ptr, job_desc, req_bitmap,
					submit_uid, qos_ptr,
					assoc_ptr ? assoc_ptr->acct : NULL);
		if (rc != SLURM_SUCCESS)
			goto fini;
	}

	/* Validate job limits against partition limits */
	if (job_desc->min_nodes == NO_VAL) {
		/* Avoid setting the job request to 0 nodes if the
		   user didn't ask for 0.
		*/
		if (!min_nodes_orig)
			job_desc->min_nodes = 1;
		else
			job_desc->min_nodes = min_nodes_orig;
	} else if ((job_desc->min_nodes > max_nodes_orig) &&
		   slurmctld_conf.enforce_part_limits &&
		   (!qos_ptr || (qos_ptr && !(qos_ptr->flags &
					      QOS_FLAG_PART_MIN_NODE)))) {
		info("_valid_job_part: job's min nodes greater than "
		     "partition's max nodes (%u > %u)",
		     job_desc->min_nodes, max_nodes_orig);
		rc = ESLURM_INVALID_NODE_COUNT;
		goto fini;
	} else if ((job_desc->min_nodes < min_nodes_orig) &&
		   ((job_desc->max_nodes == NO_VAL) ||
		    (job_desc->max_nodes >= min_nodes_orig))) {
		job_desc->min_nodes = min_nodes_orig;
	}

	if ((job_desc->max_nodes != NO_VAL) &&
	    slurmctld_conf.enforce_part_limits &&
	    (job_desc->max_nodes < min_nodes_orig) &&
	    (!qos_ptr || (qos_ptr && !(qos_ptr->flags
				       & QOS_FLAG_PART_MAX_NODE)))) {
		info("_valid_job_part: job's max nodes less than partition's "
		     "min nodes (%u < %u)",
		     job_desc->max_nodes, min_nodes_orig);
		rc = ESLURM_INVALID_NODE_COUNT;
		goto fini;
	}
#ifndef HAVE_FRONT_END
	if ((job_desc->min_nodes == 0) && (job_desc->script == NULL)) {
		info("_valid_job_part: min_nodes==0 for non-batch job");
		rc = ESLURM_INVALID_NODE_COUNT;
		goto fini;
	}
#endif

	if ((job_desc->time_limit   == NO_VAL) &&
	    (part_ptr->default_time == 0)) {
		info("_valid_job_part: job's default time is 0");
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
		info("_valid_job_part: job's min time greater than "
		     "partition's (%u > %u)",
		     job_desc->time_min, max_time);
		rc = ESLURM_INVALID_TIME_LIMIT;
		goto fini;
	}
	if ((job_desc->time_limit != NO_VAL) &&
	    (job_desc->time_limit >  max_time) &&
	    (job_desc->time_min   == NO_VAL) &&
	    slurmctld_conf.enforce_part_limits &&
	    (!qos_ptr || (qos_ptr && !(qos_ptr->flags &
				       QOS_FLAG_PART_TIME_LIMIT)))) {
		info("_valid_job_part: job's time limit greater than "
		     "partition's (%u > %u)",
		     job_desc->time_limit, max_time);
		rc = ESLURM_INVALID_TIME_LIMIT;
		goto fini;
	}
	if ((job_desc->time_min != NO_VAL) &&
	    (job_desc->time_min >  job_desc->time_limit) &&
	    (!qos_ptr || (qos_ptr && !(qos_ptr->flags &
				       QOS_FLAG_PART_TIME_LIMIT)))) {
		info("_valid_job_part: job's min_time greater time limit "
		     "(%u > %u)",
		     job_desc->time_min, job_desc->time_limit);
		rc = ESLURM_INVALID_TIME_LIMIT;
		goto fini;
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
extern int job_limits_check(struct job_record **job_pptr, bool check_min_time)
{
	struct job_details *detail_ptr;
	enum job_state_reason fail_reason;
	struct part_record *part_ptr = NULL;
	struct job_record *job_ptr = NULL;
	slurmdb_qos_rec_t  *qos_ptr;
	slurmdb_association_rec_t *assoc_ptr;
	uint32_t job_min_nodes, job_max_nodes;
	uint32_t part_min_nodes, part_max_nodes;
	uint32_t time_check;
#ifdef HAVE_BG
	static uint16_t cpus_per_node = 0;
	if (!cpus_per_node)
		select_g_alter_node_cnt(SELECT_GET_NODE_CPU_CNT,
					&cpus_per_node);
#endif
	job_ptr = *job_pptr;
	detail_ptr = job_ptr->details;
	part_ptr = job_ptr->part_ptr;
	qos_ptr = job_ptr->qos_ptr;
	assoc_ptr = job_ptr->assoc_ptr;
	if (!detail_ptr) {	/* To prevent CLANG error */
		fatal("job %u has NULL details_ptr", job_ptr->job_id);
		return WAIT_NO_REASON;
	}

#ifdef HAVE_BG
	job_min_nodes = detail_ptr->min_cpus / cpus_per_node;
	job_max_nodes = detail_ptr->max_cpus / cpus_per_node;
	part_min_nodes = part_ptr->min_nodes_orig;
	part_max_nodes = part_ptr->max_nodes_orig;
#else
	job_min_nodes = detail_ptr->min_nodes;
	job_max_nodes = detail_ptr->max_nodes;
	part_min_nodes = part_ptr->min_nodes;
	part_max_nodes = part_ptr->max_nodes;
#endif

	fail_reason = WAIT_NO_REASON;

	if (check_min_time && job_ptr->time_min)
		time_check = job_ptr->time_min;
	else
		time_check = job_ptr->time_limit;
	if ((job_min_nodes > part_max_nodes) &&
	    (!qos_ptr || (qos_ptr && !(qos_ptr->flags
				       & QOS_FLAG_PART_MAX_NODE)))) {
		debug2("Job %u requested too many nodes (%u) of "
		       "partition %s(MaxNodes %u)",
		       job_ptr->job_id, job_min_nodes,
		       part_ptr->name, part_max_nodes);
		fail_reason = WAIT_PART_NODE_LIMIT;
	} else if ((job_max_nodes != 0) &&  /* no max_nodes for job */
		   ((job_max_nodes < part_min_nodes) &&
		    (!qos_ptr || (qos_ptr && !(qos_ptr->flags &
					       QOS_FLAG_PART_MIN_NODE))))) {
		debug2("Job %u requested too few nodes (%u) of "
		       "partition %s(MinNodes %u)",
		       job_ptr->job_id, job_max_nodes,
		       part_ptr->name, part_min_nodes);
		fail_reason = WAIT_PART_NODE_LIMIT;
	} else if (part_ptr->state_up == PARTITION_DOWN) {
		debug2("Job %u requested down partition %s",
		       job_ptr->job_id, part_ptr->name);
		fail_reason = WAIT_PART_DOWN;
	} else if (part_ptr->state_up == PARTITION_INACTIVE) {
		debug2("Job %u requested inactive partition %s",
		       job_ptr->job_id, part_ptr->name);
		fail_reason = WAIT_PART_INACTIVE;
	} else if ((time_check != NO_VAL) &&
		   (time_check > part_ptr->max_time) &&
		   (!qos_ptr || (qos_ptr && !(qos_ptr->flags &
					     QOS_FLAG_PART_TIME_LIMIT)))) {
		info("Job %u exceeds partition time limit (%u > %u)",
		       job_ptr->job_id, time_check, part_ptr->max_time);
		fail_reason = WAIT_PART_TIME_LIMIT;
	} else if (qos_ptr && assoc_ptr &&
		   (qos_ptr->flags & QOS_FLAG_ENFORCE_USAGE_THRES) &&
		   (!fuzzy_equal(qos_ptr->usage_thres, NO_VAL))) {
		if (!job_ptr->prio_factors) {
			job_ptr->prio_factors =
				xmalloc(sizeof(priority_factors_object_t));
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
			debug2("Job %u exceeds usage threashold",
			       job_ptr->job_id);
			fail_reason = WAIT_QOS_THRES;
		}
	}

	return (fail_reason);
}

/*
 * _job_create - create a job table record for the supplied specifications.
 *	This performs only basic tests for request validity (access to
 *	partition, nodes count in partition, and sufficient processors in
 *	partition).
 * IN job_specs - job specifications
 * IN allocate - resource allocation request if set rather than job submit
 * IN will_run - job is not to be created, test of validity only
 * OUT job_pptr - pointer to the job (NULL on error)
 * OUT err_msg - Error message for user
 * RET 0 on success, otherwise ESLURM error code. If the job would only be
 *	able to execute with some change in partition configuration then
 *	ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE is returned
 */

static int _job_create(job_desc_msg_t * job_desc, int allocate, int will_run,
		       struct job_record **job_pptr, uid_t submit_uid,
		       char **err_msg, uint16_t protocol_version)
{
	static int launch_type_poe = -1;
	int error_code = SLURM_SUCCESS, i, qos_error;
	struct part_record *part_ptr = NULL;
	List part_ptr_list = NULL;
	bitstr_t *req_bitmap = NULL, *exc_bitmap = NULL;
	struct job_record *job_ptr = NULL;
	slurmdb_association_rec_t assoc_rec, *assoc_ptr = NULL;
	List license_list = NULL;
	bool valid;
	slurmdb_qos_rec_t qos_rec, *qos_ptr;
	uint32_t user_submit_priority;
	static uint32_t node_scaling = 1;
	static uint32_t cpus_per_mp = 1;
	acct_policy_limit_set_t acct_policy_limit_set;

#ifdef HAVE_BG
	uint16_t geo[SYSTEM_DIMENSIONS];
	uint16_t reboot;
	uint16_t rotate;
	uint16_t conn_type[SYSTEM_DIMENSIONS];
	static bool sub_mp_system = 0;

	if (node_scaling == 1) {
		select_g_alter_node_cnt(SELECT_GET_NODE_SCALING,
					&node_scaling);
		select_g_alter_node_cnt(SELECT_GET_MP_CPU_CNT,
					&cpus_per_mp);
		if (node_scaling < 512)
			sub_mp_system = 1;
	}
#endif

	if (select_serial == -1) {
		if (strcmp(slurmctld_conf.select_type, "select/serial"))
			select_serial = 0;
		else
			select_serial = 1;
	}

	memset(&acct_policy_limit_set, 0, sizeof(acct_policy_limit_set_t));

	*job_pptr = (struct job_record *) NULL;
	/*
	 * Check user permission for negative 'nice' and non-0 priority values
	 * (both restricted to SlurmUser) before running the job_submit plugin.
	 */
	if ((submit_uid != 0) && (submit_uid != slurmctld_conf.slurm_user_id)) {
		if (job_desc->priority != 0)
			job_desc->priority = NO_VAL;
		if (job_desc->nice < NICE_OFFSET)
			job_desc->nice = NICE_OFFSET;
	}
	user_submit_priority = job_desc->priority;

	/* insure that selected nodes are in this partition */
	if (job_desc->req_nodes) {
		error_code = node_name2bitmap(job_desc->req_nodes, false,
					      &req_bitmap);
		if (error_code) {
			error_code = ESLURM_INVALID_NODE_NAME;
			goto cleanup_fail;
		}
		if ((job_desc->contiguous != (uint16_t) NO_VAL) &&
		    (job_desc->contiguous))
			bit_fill_gaps(req_bitmap);
		i = bit_set_count(req_bitmap);
		if (i > job_desc->min_nodes)
			job_desc->min_nodes = i * node_scaling;
		if (i > job_desc->min_cpus)
			job_desc->min_cpus = i * cpus_per_mp;
		if (job_desc->max_nodes &&
		    (job_desc->min_nodes > job_desc->max_nodes)) {
#if 0
			info("_job_create: max node count less than required "
			     "hostlist size for user %u", job_desc->user_id);
			job_desc->max_nodes = job_desc->min_nodes;
#else
			error_code = ESLURM_INVALID_NODE_COUNT;
			goto cleanup_fail;
#endif
		}
	}
#ifdef HAVE_ALPS_CRAY
	if ((job_desc->max_nodes == 0) && (job_desc->script == NULL)) {
#else
	if (job_desc->max_nodes == 0) {
#endif
		info("_job_create: max_nodes == 0");
		error_code = ESLURM_INVALID_NODE_COUNT;
		goto cleanup_fail;
	}

	error_code = _get_job_parts(job_desc, &part_ptr, &part_ptr_list);
	if (error_code != SLURM_SUCCESS)
		goto cleanup_fail;


	memset(&assoc_rec, 0, sizeof(slurmdb_association_rec_t));
	assoc_rec.acct      = job_desc->account;
	assoc_rec.partition = part_ptr->name;
	assoc_rec.uid       = job_desc->user_id;
	/* Checks are done later to validate assoc_ptr, so we don't
	   need to lock outside of fill_in_assoc.
	*/
	if (assoc_mgr_fill_in_assoc(acct_db_conn, &assoc_rec,
				    accounting_enforce, &assoc_ptr, false)) {
		info("_job_create: invalid account or partition for user %u, "
		     "account '%s', and partition '%s'",
		     job_desc->user_id, assoc_rec.acct, assoc_rec.partition);
		error_code = ESLURM_INVALID_ACCOUNT;
		goto cleanup_fail;
	} else if (association_based_accounting &&
		   !assoc_ptr &&
		   !(accounting_enforce & ACCOUNTING_ENFORCE_ASSOCS)) {
		/* If not enforcing associations we want to look for the
		 * default account and use it to avoid getting trash in the
		 * accounting records. */
		assoc_rec.acct = NULL;
		assoc_mgr_fill_in_assoc(acct_db_conn, &assoc_rec,
					accounting_enforce, &assoc_ptr, false);
		if (assoc_ptr) {
			info("_job_create: account '%s' has no association "
			     "for user %u using default account '%s'",
			     job_desc->account, job_desc->user_id,
			     assoc_rec.acct);
			xfree(job_desc->account);
		}
	}

	if (job_desc->account == NULL)
		job_desc->account = xstrdup(assoc_rec.acct);

	/* This must be done after we have the assoc_ptr set */
	memset(&qos_rec, 0, sizeof(slurmdb_qos_rec_t));
	qos_rec.name = job_desc->qos;
	if (wiki_sched && job_desc->comment &&
	    strstr(job_desc->comment, "QOS:")) {
		if (strstr(job_desc->comment, "FLAGS:PREEMPTOR"))
			qos_rec.name = "expedite";
		else if (strstr(job_desc->comment, "FLAGS:PREEMPTEE"))
			qos_rec.name = "standby";
	}

	qos_ptr = _determine_and_validate_qos(
		job_desc->reservation, assoc_ptr, false, &qos_rec, &qos_error);

	if (qos_error != SLURM_SUCCESS) {
		error_code = qos_error;
		goto cleanup_fail;
	}

	error_code = _valid_job_part(job_desc, submit_uid, req_bitmap,
				     &part_ptr, part_ptr_list,
				     assoc_ptr, qos_ptr);

	if (error_code != SLURM_SUCCESS)
		goto cleanup_fail;

	if ((error_code = _validate_job_desc(job_desc, allocate, submit_uid,
	                                     part_ptr, part_ptr_list))) {
		goto cleanup_fail;
	}

	if ((accounting_enforce & ACCOUNTING_ENFORCE_LIMITS) &&
	    (!acct_policy_validate(job_desc, part_ptr,
				   assoc_ptr, qos_ptr, NULL,
				   &acct_policy_limit_set, 0))) {
		info("_job_create: exceeded association/qos's limit "
		     "for user %u", job_desc->user_id);
		error_code = ESLURM_ACCOUNTING_POLICY;
		goto cleanup_fail;
	}

	/* This needs to be done after the association acct policy check since
	 * it looks at unaltered nodes for bluegene systems
	 */
	debug3("before alteration asking for nodes %u-%u cpus %u-%u",
	       job_desc->min_nodes, job_desc->max_nodes,
	       job_desc->min_cpus, job_desc->max_cpus);
	if (select_g_alter_node_cnt(SELECT_SET_NODE_CNT, job_desc)
	    != SLURM_SUCCESS) {
		error_code = ESLURM_INVALID_NODE_COUNT;
		goto cleanup_fail;
	}

	debug3("after alteration asking for nodes %u-%u cpus %u-%u",
	       job_desc->min_nodes, job_desc->max_nodes,
	       job_desc->min_cpus, job_desc->max_cpus);

	if (job_desc->exc_nodes) {
		error_code = node_name2bitmap(job_desc->exc_nodes, false,
					      &exc_bitmap);
		if (error_code) {
			error_code = ESLURM_INVALID_NODE_NAME;
			goto cleanup_fail;
		}
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

#ifdef HAVE_BG
	select_g_select_jobinfo_get(job_desc->select_jobinfo,
				    SELECT_JOBDATA_GEOMETRY, &geo);
	if (geo[0] == (uint16_t) NO_VAL) {
		for (i=0; i<SYSTEM_DIMENSIONS; i++)
			geo[i] = 0;
		select_g_select_jobinfo_set(job_desc->select_jobinfo,
					    SELECT_JOBDATA_GEOMETRY, &geo);
	} else if (geo[0] != 0) {
		uint32_t i, tot = 1;
		for (i=0; i<SYSTEM_DIMENSIONS; i++)
			tot *= geo[i];
		if (job_desc->min_nodes > tot) {
			info("MinNodes(%d) > GeometryNodes(%d)",
			     job_desc->min_nodes, tot);
			error_code = ESLURM_TOO_MANY_REQUESTED_CPUS;
			goto cleanup_fail;
		}
		job_desc->min_nodes = tot;
	}
	select_g_select_jobinfo_get(job_desc->select_jobinfo,
				    SELECT_JOBDATA_REBOOT, &reboot);
	if (reboot == (uint16_t) NO_VAL) {
		reboot = 0;	/* default is no reboot */
		select_g_select_jobinfo_set(job_desc->select_jobinfo,
					    SELECT_JOBDATA_REBOOT, &reboot);
	}
	select_g_select_jobinfo_get(job_desc->select_jobinfo,
				    SELECT_JOBDATA_ROTATE, &rotate);
	if (rotate == (uint16_t) NO_VAL) {
		rotate = 1;	/* refault is to rotate */
		select_g_select_jobinfo_set(job_desc->select_jobinfo,
					    SELECT_JOBDATA_ROTATE, &rotate);
	}
	select_g_select_jobinfo_get(job_desc->select_jobinfo,
				    SELECT_JOBDATA_CONN_TYPE, &conn_type);

	if ((conn_type[0] != (uint16_t) NO_VAL)
	    && (((conn_type[0] >= SELECT_SMALL)
		 && ((job_desc->min_cpus >= cpus_per_mp) && !sub_mp_system))
		|| (!sub_mp_system
		    && ((conn_type[0] == SELECT_TORUS)
			|| (conn_type[0] == SELECT_MESH))
		    && (job_desc->min_cpus < cpus_per_mp)))) {
		/* check to make sure we have a valid conn_type with
		 * the cpu count */
		info("Job's cpu count at %u makes our conn_type "
		     "of '%s' invalid.",
		     job_desc->min_cpus, conn_type_string(conn_type[0]));
		error_code = ESLURM_INVALID_NODE_COUNT;
		goto cleanup_fail;
	}

	/* make sure we reset all the NO_VAL's to NAV's */
	for (i=0; i<SYSTEM_DIMENSIONS; i++) {
		if (conn_type[i] == (uint16_t)NO_VAL)
			conn_type[i] = SELECT_NAV;
	}
	select_g_select_jobinfo_set(job_desc->select_jobinfo,
				    SELECT_JOBDATA_CONN_TYPE,
				    &conn_type);
#endif

	if (job_desc->max_nodes == NO_VAL)
		job_desc->max_nodes = 0;

	if (job_desc->max_nodes &&
	    (job_desc->max_nodes < job_desc->min_nodes)) {
		info("_job_create: Job's max_nodes(%u) < min_nodes(%u)",
		     job_desc->max_nodes, job_desc->min_nodes);
		error_code = ESLURM_INVALID_NODE_COUNT;
		goto cleanup_fail;
	}

	license_list = license_validate(job_desc->licenses, &valid);
	if (!valid) {
		info("Job's requested licenses are invalid: %s",
		     job_desc->licenses);
		error_code = ESLURM_INVALID_LICENSES;
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

	part_ptr_list = NULL;
	if ((error_code = checkpoint_alloc_jobinfo(&(job_ptr->check_job)))) {
		error("Failed to allocate checkpoint info for job");
		goto cleanup_fail;
	}

	job_ptr->limit_set_max_cpus = acct_policy_limit_set.max_cpus;
	job_ptr->limit_set_max_nodes = acct_policy_limit_set.max_nodes;
	job_ptr->limit_set_min_cpus = acct_policy_limit_set.min_cpus;
	job_ptr->limit_set_min_nodes = acct_policy_limit_set.min_nodes;
	job_ptr->limit_set_pn_min_memory = acct_policy_limit_set.pn_min_memory;
	job_ptr->limit_set_time = acct_policy_limit_set.time;
	job_ptr->limit_set_qos = acct_policy_limit_set.qos;

	job_ptr->assoc_id = assoc_rec.id;
	job_ptr->assoc_ptr = (void *) assoc_ptr;
	job_ptr->qos_ptr = (void *) qos_ptr;
	job_ptr->qos_id = qos_rec.id;

	if (launch_type_poe == -1) {
		char *launch_type = slurm_get_launch_type();
		if (!strcmp(launch_type, "launch/poe"))
			launch_type_poe = 1;
		else
			launch_type_poe = 0;
		xfree(launch_type);
	}
	if (launch_type_poe == 1)
		job_ptr->next_step_id = 1;

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
	} else if (job_ptr->priority != NO_VAL) {
		job_ptr->direct_set_prio = 1;
	}

	error_code = update_job_dependency(job_ptr, job_desc->dependency);
	if (error_code != SLURM_SUCCESS)
		goto cleanup_fail;
	job_ptr->details->orig_dependency = xstrdup(job_ptr->details->
						    dependency);

	if (build_feature_list(job_ptr)) {
		error_code = ESLURM_INVALID_FEATURE;
		goto cleanup_fail;
	}
	/* NOTE: If this job is being used to expand another job, this job's
	 * gres_list has already been filled in with a copy of gres_list job
	 * to be expanded by update_job_dependency() */
	if ((job_ptr->details->expanding_jobid == 0) &&
	    gres_plugin_job_state_validate(job_ptr->gres, &job_ptr->gres_list)){
		error_code = ESLURM_INVALID_GRES;
		goto cleanup_fail;
	}
	gres_plugin_job_state_log(job_ptr->gres_list, job_ptr->job_id);

	if ((error_code = validate_job_resv(job_ptr)))
		goto cleanup_fail;

	if (job_desc->script
	    &&  (!will_run)) {	/* don't bother with copy if just a test */
		if ((error_code = _copy_job_desc_to_file(job_desc,
							 job_ptr->job_id))) {
			error_code = ESLURM_WRITING_TO_FILE;
			goto cleanup_fail;
		}
		job_ptr->batch_flag = 1;
	} else
		job_ptr->batch_flag = 0;

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

	FREE_NULL_LIST(license_list);
	FREE_NULL_BITMAP(req_bitmap);
	FREE_NULL_BITMAP(exc_bitmap);
	return error_code;

cleanup_fail:
	if (job_ptr) {
		job_ptr->job_state = JOB_FAILED;
		job_ptr->exit_code = 1;
		job_ptr->state_reason = FAIL_SYSTEM;
		xfree(job_ptr->state_desc);
		job_ptr->start_time = job_ptr->end_time = time(NULL);
		_purge_job_record(job_ptr->job_id);
		*job_pptr = (struct job_record *) NULL;
	}
	FREE_NULL_LIST(license_list);
	FREE_NULL_LIST(part_ptr_list);
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

/* For each token in a comma delimited job array expression set the matching
 * bitmap entry */
static bool _parse_array_tok(char *tok, bitstr_t *array_bitmap, uint32_t max)
{
	char *end_ptr = NULL;
	int i, first, last, step = 1;

	first = strtol(tok, &end_ptr, 10);
	if (first < 0)
		return false;
	if (end_ptr[0] == '-') {
		last = strtol(end_ptr + 1, &end_ptr, 10);
		if (end_ptr[0] == ':') {
			step = strtol(end_ptr + 1, &end_ptr, 10);
			if ((end_ptr[0] != '\0') && (end_ptr[0] != '%'))
				return false;
			if (step <= 0)
				return false;
		} else if ((end_ptr[0] != '\0') && (end_ptr[0] != '%')) {
			return false;
		}
		if (last < first)
			return false;
	} else if ((end_ptr[0] != '\0') && (end_ptr[0] != '%')) {
		return false;
	} else {
		last = first;
	}

	if (last >= max)
		return false;

	for (i = first; i <= last; i += step) {
		bit_set(array_bitmap, i);
	}

	return true;
}

/* Translate a job array expression into the equivalent bitmap */
static bool _valid_array_inx(job_desc_msg_t *job_desc)
{
	slurm_ctl_conf_t *conf;
	bool valid = true;
	char *tmp, *tok, *last = NULL;

	FREE_NULL_BITMAP(job_desc->array_bitmap);
	if (!job_desc->array_inx || !job_desc->array_inx[0])
		return true;
	if (!job_desc->script || !job_desc->script[0])
		return false;

	if (max_array_size == NO_VAL) {
		conf = slurm_conf_lock();
		max_array_size = conf->max_array_sz;
		slurm_conf_unlock();
	}
	if (max_array_size == 0) {
		verbose("Job arrays disabled, MaxArraySize=0");
		return false;
	}

	/* We have a job array request */
	job_desc->immediate = 0;	/* Disable immediate option */
	job_desc->array_bitmap = bit_alloc(max_array_size);

	tmp = xstrdup(job_desc->array_inx);
	tok = strtok_r(tmp, ",", &last);
	while (tok && valid) {
		valid = _parse_array_tok(tok, job_desc->array_bitmap,
					 max_array_size);
		tok = strtok_r(NULL, ",", &last);
	}
	xfree(tmp);

	return valid;
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
	int rc;

	rc = job_submit_plugin_submit(job_desc, (uint32_t) submit_uid, err_msg);
	if (rc != SLURM_SUCCESS)
		return rc;

	if (_test_strlen(job_desc->account, "account", 1024)		||
	    _test_strlen(job_desc->alloc_node, "alloc_node", 1024)	||
	    _test_strlen(job_desc->array_inx, "array_inx", 1024 * 4)	||
	    _test_strlen(job_desc->blrtsimage, "blrtsimage", 1024)	||
	    _test_strlen(job_desc->ckpt_dir, "ckpt_dir", 1024)		||
	    _test_strlen(job_desc->comment, "comment", 1024)		||
	    _test_strlen(job_desc->cpu_bind, "cpu_bind", 1024)		||
	    _test_strlen(job_desc->dependency, "dependency", 1024*128)	||
	    _test_strlen(job_desc->exc_nodes, "exc_nodes", 1024*64)	||
	    _test_strlen(job_desc->features, "features", 1024)		||
	    _test_strlen(job_desc->gres, "gres", 1024)			||
	    _test_strlen(job_desc->licenses, "licenses", 1024)		||
	    _test_strlen(job_desc->linuximage, "linuximage", 1024)	||
	    _test_strlen(job_desc->mail_user, "mail_user", 1024)	||
	    _test_strlen(job_desc->mem_bind, "mem_bind", 1024)		||
	    _test_strlen(job_desc->mloaderimage, "mloaderimage", 1024)	||
	    _test_strlen(job_desc->name, "name", 1024)			||
	    _test_strlen(job_desc->network, "network", 1024)		||
	    _test_strlen(job_desc->partition, "partition", 1024)	||
	    _test_strlen(job_desc->qos, "qos", 1024)			||
	    _test_strlen(job_desc->ramdiskimage, "ramdiskimage", 1024)	||
	    _test_strlen(job_desc->req_nodes, "req_nodes", 1024*64)	||
	    _test_strlen(job_desc->reservation, "reservation", 1024)	||
	    _test_strlen(job_desc->script, "script", 1024 * 1024 * 4)	||
	    _test_strlen(job_desc->std_err, "std_err", MAXPATHLEN)	||
	    _test_strlen(job_desc->std_in, "std_in", MAXPATHLEN)	||
	    _test_strlen(job_desc->std_out, "std_out", MAXPATHLEN)	||
	    _test_strlen(job_desc->wckey, "wckey", 1024)		||
	    _test_strlen(job_desc->work_dir, "work_dir", MAXPATHLEN))
		return ESLURM_PATHNAME_TOO_LONG;

	if (!_valid_array_inx(job_desc))
		return ESLURM_INVALID_ARRAY;

	/* Make sure anything that may be put in the database will be
	 * lower case */
	xstrtolower(job_desc->account);
	xstrtolower(job_desc->wckey);

	/* Basic validation of some parameters */
	if (job_desc->req_nodes) {
		hostlist_t hl;
		uint32_t host_cnt;
		hl = hostlist_create(job_desc->req_nodes);
		if (hl == NULL) {
			/* likely a badly formatted hostlist */
			error("validate_job_create_req: bad hostlist");
			return ESLURM_INVALID_NODE_NAME;
		}
		host_cnt = hostlist_count(hl);
		hostlist_destroy(hl);
		if ((job_desc->min_nodes == NO_VAL) ||
		    (job_desc->min_nodes <  host_cnt))
			job_desc->min_nodes = host_cnt;
	}
	if ((job_desc->ntasks_per_node != (uint16_t) NO_VAL) &&
	    (job_desc->min_nodes       != NO_VAL) &&
	    (job_desc->num_tasks       != NO_VAL)) {
		uint32_t ntasks = job_desc->ntasks_per_node *
				  job_desc->min_nodes;
		job_desc->num_tasks = MAX(job_desc->num_tasks, ntasks);
	}
	if ((job_desc->min_cpus  != NO_VAL) &&
	    (job_desc->min_nodes != NO_VAL) &&
	    (job_desc->min_cpus  <  job_desc->min_nodes) &&
	    (job_desc->max_cpus  >= job_desc->min_nodes))
		job_desc->min_cpus = job_desc->min_nodes;

	return SLURM_SUCCESS;
}

/* _copy_job_desc_to_file - copy the job script and environment from the RPC
 *	structure into a file */
static int
_copy_job_desc_to_file(job_desc_msg_t * job_desc, uint32_t job_id)
{
	int error_code = 0, hash;
	char *dir_name, job_dir[32], *file_name;
	DEF_TIMERS;

	START_TIMER;
	/* Create state_save_location directory */
	dir_name = slurm_get_state_save_location();

	/* Create directory based upon job ID due to limitations on the number
	 * of files possible in a directory on some file system types (e.g.
	 * up to 64k files on a FAT32 file system). */
	hash = job_id % 10;
	sprintf(job_dir, "/hash.%d", hash);
	xstrcat(dir_name, job_dir);
	(void) mkdir(dir_name, 0700);

	/* Create job_id specific directory */
	sprintf(job_dir, "/job.%u", job_id);
	xstrcat(dir_name, job_dir);
	if (mkdir(dir_name, 0700)) {
		if (!slurmctld_primary && (errno == EEXIST)) {
			error("Apparent duplicate job ID %u. Two primary "
			      "slurmctld daemons might currently be active",
			      job_id);
		}
		error("mkdir(%s) error %m", dir_name);
		xfree(dir_name);
		return ESLURM_WRITING_TO_FILE;
	}

	/* Create environment file, and write data to it */
	file_name = xstrdup(dir_name);
	xstrcat(file_name, "/environment");
	error_code = _write_data_array_to_file(file_name,
					       job_desc->environment,
					       job_desc->env_size);
	xfree(file_name);

	if (error_code == 0) {
		/* Create script file */
		file_name = xstrdup(dir_name);
		xstrcat(file_name, "/script");
		error_code = _write_data_to_file(file_name, job_desc->script);
		xfree(file_name);
	}

	xfree(dir_name);
	END_TIMER2("_copy_job_desc_to_file");
	return error_code;
}

/* Return true of the specified job ID already has a batch directory so
 * that a different job ID can be created. This is to help limit damage from
 * split-brain, where two slurmctld daemons are running as primary. */
static bool _dup_job_file_test(uint32_t job_id)
{
	char *dir_name_src, job_dir[40];
	struct stat buf;
	int rc, hash;

	dir_name_src  = slurm_get_state_save_location();
	hash = job_id % 10;
	sprintf(job_dir, "/hash.%d", hash);
	xstrcat(dir_name_src, job_dir);
	sprintf(job_dir, "/job.%u", job_id);
	xstrcat(dir_name_src, job_dir);
	rc = stat(dir_name_src, &buf);
	xfree(dir_name_src);
	if (rc == 0) {
		error("Vestigial state files for job %u, but no job record. "
		      "this may be the result of two slurmctld running in "
		      "primary mode", job_id);
		return true;
	}
	return false;
}

/* _copy_job_desc_files - create copies of a job script and environment files */
static int
_copy_job_desc_files(uint32_t job_id_src, uint32_t job_id_dest)
{
	int error_code = SLURM_SUCCESS, hash;
	char *dir_name_src, *dir_name_dest, job_dir[40];
	char *file_name_src, *file_name_dest;

	/* Create state_save_location directory */
	dir_name_src  = slurm_get_state_save_location();
	dir_name_dest = xstrdup(dir_name_src);

	/* Create directory based upon job ID due to limitations on the number
	 * of files possible in a directory on some file system types (e.g.
	 * up to 64k files on a FAT32 file system). */
	hash = job_id_dest % 10;
	sprintf(job_dir, "/hash.%d", hash);
	xstrcat(dir_name_dest, job_dir);
	(void) mkdir(dir_name_dest, 0700);

	/* Create job_id_dest specific directory */
	sprintf(job_dir, "/job.%u", job_id_dest);
	xstrcat(dir_name_dest, job_dir);
	if (mkdir(dir_name_dest, 0700)) {
		if (!slurmctld_primary && (errno == EEXIST)) {
			error("Apparent duplicate job ID %u. Two primary "
			      "slurmctld daemons might currently be active",
			      job_id_dest);
		}
		error("mkdir(%s) error %m", dir_name_dest);
		xfree(dir_name_src);
		xfree(dir_name_dest);
		return ESLURM_WRITING_TO_FILE;
	}

	/* Identify job_id_src specific directory */
	hash = job_id_src % 10;
	sprintf(job_dir, "/hash.%d", hash);
	xstrcat(dir_name_src, job_dir);
	(void) mkdir(dir_name_src, 0700);
	sprintf(job_dir, "/job.%u", job_id_src);
	xstrcat(dir_name_src, job_dir);

	file_name_src  = xstrdup(dir_name_src);
	file_name_dest = xstrdup(dir_name_dest);
	xstrcat(file_name_src,  "/environment");
	xstrcat(file_name_dest, "/environment");
	error_code = link(file_name_src, file_name_dest);
	if (error_code < 0) {
		error("%s: link() failed %m copy files src %s dest %s",
		      __func__, file_name_src, file_name_dest);
		error_code = _copy_job_file(file_name_src, file_name_dest);
		if (error_code < 0) {
			error("%s: failed copy files %m src %s dst %s",
			      __func__, file_name_src, file_name_dest);
		}
	}
	xfree(file_name_src);
	xfree(file_name_dest);

	if (error_code == 0) {
		file_name_src  = xstrdup(dir_name_src);
		file_name_dest = xstrdup(dir_name_dest);
		xstrcat(file_name_src,  "/script");
		xstrcat(file_name_dest, "/script");
		error_code = link(file_name_src, file_name_dest);
		if (error_code < 0) {
			error("%s: link() failed %m copy files src %s dest %s",
			      __func__, file_name_src, file_name_dest);
			error_code = _copy_job_file(file_name_src,
						    file_name_dest);
			if (error_code < 0) {
				error("%s: failed copy files %m src %s dst %s",
				      __func__, file_name_src, file_name_dest);
			}
		}
		xfree(file_name_src);
		xfree(file_name_dest);
	}

	xfree(dir_name_src);
	xfree(dir_name_dest);
	return error_code;
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
static int _write_data_to_file(char *file_name, char *data)
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
char **get_job_env(struct job_record *job_ptr, uint32_t * env_size)
{
	char *file_name, **environment = NULL;
	int hash = job_ptr->job_id % 10;
	int cc;

	file_name = slurm_get_state_save_location();
	xstrfmtcat(file_name, "/hash.%d/job.%u/environment",
		   hash, job_ptr->job_id);

	if (_read_data_array_from_file(file_name, &environment, env_size,
				       job_ptr)) {
		/* Read state from version 14.03 or earlier */
		xfree(file_name);
		file_name = slurm_get_state_save_location();
		xstrfmtcat(file_name, "/job.%u/environment", job_ptr->job_id);
		cc = _read_data_array_from_file(file_name,
						&environment,
						env_size,
						job_ptr);
		if (cc < 0) {
			xfree(file_name);
			return NULL;
		}
	}

	xfree(file_name);
	return environment;
}

/*
 * get_job_script - return the script for a given job
 * IN job_ptr - pointer to job for which data is required
 * RET point to string containing job script
 */
char *get_job_script(struct job_record *job_ptr)
{
	char *file_name, job_dir[40], *script = NULL;
	int hash;

	if (!job_ptr->batch_flag)
		return NULL;

	hash = job_ptr->job_id % 10;
	file_name = slurm_get_state_save_location();
	sprintf(job_dir, "/hash.%d/job.%u/script", hash, job_ptr->job_id);
	xstrcat(file_name, job_dir);

	if (_read_data_from_file(file_name, &script)) {
		/* Read version 14.03 or earlier state format */
		xfree(file_name);
		file_name = slurm_get_state_save_location();
		sprintf(job_dir, "/job.%u/script", job_ptr->job_id);
		xstrcat(file_name, job_dir);
		(void) _read_data_from_file(file_name, &script);
	}

	xfree(file_name);
	return script;
}

/*
 * Read a collection of strings from a file
 * IN file_name - file to read from
 * OUT data - pointer to array of pointers to strings (e.g. env),
 *	must be xfreed when no longer needed
 * OUT size - number of elements in data
 * IN job_ptr - job
 * RET 0 on success, -1 on error
 * NOTE: The output format of this must be identical with _xduparray2()
 */
static int
_read_data_array_from_file(char *file_name, char ***data, uint32_t * size,
			   struct job_record *job_ptr)
{
	int fd, pos, buf_size, amount, i, j;
	char *buffer, **array_ptr;
	uint32_t rec_cnt;

	xassert(file_name);
	xassert(data);
	xassert(size);
	*data = NULL;
	*size = 0;

	fd = open(file_name, 0);
	if (fd < 0) {
		error("Error opening file %s, %m", file_name);
		return -1;
	}

	amount = read(fd, &rec_cnt, sizeof(uint32_t));
	if (amount < sizeof(uint32_t)) {
		if (amount != 0)	/* incomplete write */
			error("Error reading file %s, %m", file_name);
		else
			verbose("File %s has zero size", file_name);
		close(fd);
		return -1;
	}

	if (rec_cnt >= INT_MAX) {
		error("%s: unreasonable record counter %d in file %s",
		      __func__, rec_cnt, file_name);
		close(fd);
		return -1;
	}

	if (rec_cnt == 0) {
		*data = NULL;
		*size = 0;
		close(fd);
		return 0;
	}

	pos = 0;
	buf_size = BUF_SIZE;
	buffer = xmalloc(buf_size);
	while (1) {
		amount = read(fd, &buffer[pos], BUF_SIZE);
		if (amount < 0) {
			error("Error reading file %s, %m", file_name);
			xfree(buffer);
			close(fd);
			return -1;
		}
		pos += amount;
		if (amount < BUF_SIZE)	/* end of file */
			break;
		buf_size += amount;
		xrealloc(buffer, buf_size);
	}
	close(fd);

	/* Allocate extra space for supplemental environment variables
	 * as set by Moab */
	if (job_ptr->details->env_cnt) {
		for (j = 0; j < job_ptr->details->env_cnt; j++)
			pos += (strlen(job_ptr->details->env_sup[j]) + 1);
		xrealloc(buffer, pos);
	}

	/* We have all the data, now let's compute the pointers */
	array_ptr = xmalloc(sizeof(char *) *
			    (rec_cnt + job_ptr->details->env_cnt));
	for (i = 0, pos = 0; i < rec_cnt; i++) {
		array_ptr[i] = &buffer[pos];
		pos += strlen(&buffer[pos]) + 1;
		if ((pos > buf_size) && ((i + 1) < rec_cnt)) {
			error("Bad environment file %s", file_name);
			rec_cnt = i;
			break;
		}
	}

	/* Add supplemental environment variables for Moab */
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
				if (strncmp(array_ptr[i],
					    job_ptr->details->env_sup[j],
					    name_len)) {
					continue;
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

/*
 * Read a string from a file
 * IN file_name - file to read from
 * OUT data - pointer to  string
 *	must be xfreed when no longer needed
 * RET - 0 on success, -1 on error
 */
static int _read_data_from_file(char *file_name, char **data)
{
	int fd, pos, buf_size, amount;
	char *buffer;

	xassert(file_name);
	xassert(data);
	*data = NULL;

	fd = open(file_name, 0);
	if (fd < 0) {
		error("Error opening file %s, %m", file_name);
		return -1;
	}

	pos = 0;
	buf_size = BUF_SIZE;
	buffer = xmalloc(buf_size);
	while (1) {
		amount = read(fd, &buffer[pos], BUF_SIZE);
		if (amount < 0) {
			error("Error reading file %s, %m", file_name);
			xfree(buffer);
			close(fd);
			return -1;
		}
		if (amount < BUF_SIZE)	/* end of file */
			break;
		pos += amount;
		buf_size += amount;
		xrealloc(buffer, buf_size);
	}

	*data = buffer;
	close(fd);
	return 0;
}

/* Given a job request, return a multi_core_data struct.
 * Returns NULL if no values set in the job/step request */
static multi_core_data_t *
_set_multi_core_data(job_desc_msg_t * job_desc)
{
	multi_core_data_t * mc_ptr;

	if ((job_desc->sockets_per_node  == (uint16_t) NO_VAL)	&&
	    (job_desc->cores_per_socket  == (uint16_t) NO_VAL)	&&
	    (job_desc->threads_per_core  == (uint16_t) NO_VAL)	&&
	    (job_desc->ntasks_per_socket == (uint16_t) NO_VAL)	&&
	    (job_desc->ntasks_per_core   == (uint16_t) NO_VAL)	&&
	    (job_desc->plane_size        == (uint16_t) NO_VAL))
		return NULL;

	mc_ptr = xmalloc(sizeof(multi_core_data_t));
	mc_ptr->sockets_per_node = job_desc->sockets_per_node;
	mc_ptr->cores_per_socket = job_desc->cores_per_socket;
	mc_ptr->threads_per_core = job_desc->threads_per_core;
	if (job_desc->ntasks_per_socket != (uint16_t) NO_VAL)
		mc_ptr->ntasks_per_socket  = job_desc->ntasks_per_socket;
	else
		mc_ptr->ntasks_per_socket  = (uint16_t) INFINITE;
	if (job_desc->ntasks_per_core != (uint16_t) NO_VAL)
		mc_ptr->ntasks_per_core    = job_desc->ntasks_per_core;
	else if (slurmctld_conf.select_type_param & CR_ONE_TASK_PER_CORE)
		mc_ptr->ntasks_per_core    = 1;
	else
		mc_ptr->ntasks_per_core    = (uint16_t) INFINITE;
	if (job_desc->plane_size != (uint16_t) NO_VAL)
		mc_ptr->plane_size         = job_desc->plane_size;
	else
		mc_ptr->plane_size         = 0;

	return mc_ptr;
}

/* _copy_job_desc_to_job_record - copy the job descriptor from the RPC
 *	structure into the actual slurmctld job record */
static int
_copy_job_desc_to_job_record(job_desc_msg_t * job_desc,
			     struct job_record **job_rec_ptr,
			     bitstr_t ** req_bitmap,
			     bitstr_t ** exc_bitmap)
{
	int error_code;
	struct job_details *detail_ptr;
	struct job_record *job_ptr;

	if (slurm_get_track_wckey()) {
		if (!job_desc->wckey) {
			/* get the default wckey for this user since none was
			 * given */
			slurmdb_user_rec_t user_rec;
			memset(&user_rec, 0, sizeof(slurmdb_user_rec_t));
			user_rec.uid = job_desc->user_id;
			assoc_mgr_fill_in_user(acct_db_conn, &user_rec,
					       accounting_enforce, NULL);
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

			memset(&wckey_rec, 0, sizeof(slurmdb_wckey_rec_t));
			wckey_rec.uid       = job_desc->user_id;
			wckey_rec.name      = job_desc->wckey;

			if (assoc_mgr_fill_in_wckey(acct_db_conn, &wckey_rec,
						    accounting_enforce,
						    &wckey_ptr)) {
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

	job_ptr = _create_job_record(&error_code, 1);
	if (error_code)
		return error_code;

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

	if (job_desc->name)
		job_ptr->name = xstrdup(job_desc->name);
	if (job_desc->wckey)
		job_ptr->wckey = xstrdup(job_desc->wckey);

	_add_job_hash(job_ptr);

	job_ptr->user_id    = (uid_t) job_desc->user_id;
	job_ptr->group_id   = (gid_t) job_desc->group_id;
	job_ptr->job_state  = JOB_PENDING;
	job_ptr->time_limit = job_desc->time_limit;
	if (job_desc->time_min != NO_VAL)
		job_ptr->time_min = job_desc->time_min;
	job_ptr->alloc_sid  = job_desc->alloc_sid;
	job_ptr->alloc_node = xstrdup(job_desc->alloc_node);
	job_ptr->account    = xstrdup(job_desc->account);
	job_ptr->gres       = xstrdup(job_desc->gres);
	job_ptr->network    = xstrdup(job_desc->network);
	job_ptr->resv_name  = xstrdup(job_desc->reservation);
	job_ptr->comment    = xstrdup(job_desc->comment);
	if (!wiki_sched_test) {
		char *sched_type = slurm_get_sched_type();
		if (strcmp(sched_type, "sched/wiki") == 0)
			wiki_sched  = true;
		if (strcmp(sched_type, "sched/wiki2") == 0) {
			wiki_sched  = true;
			wiki2_sched = true;
		}
		xfree(sched_type);
		wiki_sched_test = true;
	}

	if (job_desc->kill_on_node_fail != (uint16_t) NO_VAL)
		job_ptr->kill_on_node_fail = job_desc->kill_on_node_fail;

	job_ptr->resp_host = xstrdup(job_desc->resp_host);
	job_ptr->alloc_resp_port = job_desc->alloc_resp_port;
	job_ptr->other_port = job_desc->other_port;
	job_ptr->time_last_active = time(NULL);
	job_ptr->cr_enabled = 0;
	job_ptr->derived_ec = 0;

	job_ptr->licenses  = xstrdup(job_desc->licenses);
	job_ptr->mail_type = job_desc->mail_type;
	job_ptr->mail_user = xstrdup(job_desc->mail_user);

	job_ptr->ckpt_interval = job_desc->ckpt_interval;
	job_ptr->spank_job_env = job_desc->spank_job_env;
	job_ptr->spank_job_env_size = job_desc->spank_job_env_size;
	job_desc->spank_job_env = (char **) NULL; /* nothing left to free */
	job_desc->spank_job_env_size = 0;         /* nothing left to free */

	if (job_desc->wait_all_nodes == (uint16_t) NO_VAL)
		job_ptr->wait_all_nodes = DEFAULT_WAIT_ALL_NODES;
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
	detail_ptr->nice       = job_desc->nice;
	detail_ptr->open_mode  = job_desc->open_mode;
	detail_ptr->min_cpus   = job_desc->min_cpus;
	detail_ptr->max_cpus   = job_desc->max_cpus;
	detail_ptr->min_nodes  = job_desc->min_nodes;
	detail_ptr->max_nodes  = job_desc->max_nodes;
	if (job_desc->req_nodes) {
		detail_ptr->req_nodes =
			_copy_nodelist_no_dup(job_desc->req_nodes);
		detail_ptr->req_node_bitmap = *req_bitmap;
		detail_ptr->req_node_layout = NULL; /* Layout specified at
						     * start time */
		*req_bitmap = NULL;	/* Reused nothing left to free */
	}
	if (job_desc->exc_nodes) {
		detail_ptr->exc_nodes =
			_copy_nodelist_no_dup(job_desc->exc_nodes);
		detail_ptr->exc_node_bitmap = *exc_bitmap;
		*exc_bitmap = NULL;	/* Reused nothing left to free */
	}
	if (job_desc->features)
		detail_ptr->features = xstrdup(job_desc->features);
	if ((job_desc->shared == 0) && (select_serial == 0)) {
		detail_ptr->share_res  = 0;
		detail_ptr->whole_node = 1;
	} else if (job_desc->shared == 1) {
		detail_ptr->share_res  = 1;
		detail_ptr->whole_node = 0;
	} else {
		detail_ptr->share_res  = (uint8_t) NO_VAL;
		detail_ptr->whole_node = 0;
	}
	if (job_desc->contiguous != (uint16_t) NO_VAL)
		detail_ptr->contiguous = job_desc->contiguous;
	if (slurm_get_use_spec_resources())
		detail_ptr->core_spec = job_desc->core_spec;
	else
		detail_ptr->core_spec = (uint16_t) NO_VAL;
	if (detail_ptr->core_spec != (uint16_t) NO_VAL)
		detail_ptr->whole_node = 1;
	if (job_desc->task_dist != (uint16_t) NO_VAL)
		detail_ptr->task_dist = job_desc->task_dist;
	if (job_desc->cpus_per_task != (uint16_t) NO_VAL)
		detail_ptr->cpus_per_task = MAX(job_desc->cpus_per_task, 1);
	else
		detail_ptr->cpus_per_task = 1;
	if (job_desc->pn_min_cpus != (uint16_t) NO_VAL)
		detail_ptr->pn_min_cpus = job_desc->pn_min_cpus;
	if (job_desc->overcommit != (uint8_t) NO_VAL)
		detail_ptr->overcommit = job_desc->overcommit;
	if (job_desc->ntasks_per_node != (uint16_t) NO_VAL) {
		detail_ptr->ntasks_per_node = job_desc->ntasks_per_node;
		if (detail_ptr->overcommit == 0) {
			detail_ptr->pn_min_cpus =
				MAX(detail_ptr->pn_min_cpus,
				    (detail_ptr->cpus_per_task *
				     detail_ptr->ntasks_per_node));
		}
	} else {
		detail_ptr->pn_min_cpus = MAX(detail_ptr->pn_min_cpus,
					      detail_ptr->cpus_per_task);
	}
	if (job_desc->reboot != (uint16_t) NO_VAL)
		job_ptr->reboot = MIN(job_desc->reboot, 1);
	else
		job_ptr->reboot = 0;
	if (job_desc->requeue != (uint16_t) NO_VAL)
		detail_ptr->requeue = MIN(job_desc->requeue, 1);
	else
		detail_ptr->requeue = slurmctld_conf.job_requeue;
	if (job_desc->pn_min_memory != NO_VAL)
		detail_ptr->pn_min_memory = job_desc->pn_min_memory;
	if (job_desc->pn_min_tmp_disk != NO_VAL)
		detail_ptr->pn_min_tmp_disk = job_desc->pn_min_tmp_disk;
	if (job_desc->num_tasks != NO_VAL)
		detail_ptr->num_tasks = job_desc->num_tasks;
	if (job_desc->std_err)
		detail_ptr->std_err = xstrdup(job_desc->std_err);
	if (job_desc->std_in)
		detail_ptr->std_in = xstrdup(job_desc->std_in);
	if (job_desc->std_out)
		detail_ptr->std_out = xstrdup(job_desc->std_out);
	if (job_desc->work_dir)
		detail_ptr->work_dir = xstrdup(job_desc->work_dir);
	if (job_desc->begin_time > time(NULL))
		detail_ptr->begin_time = job_desc->begin_time;
	job_ptr->select_jobinfo =
		select_g_select_jobinfo_copy(job_desc->select_jobinfo);
	select_g_select_jobinfo_set(job_ptr->select_jobinfo,
				    SELECT_JOBDATA_USER_NAME,
				    &job_ptr->user_id);

	select_g_select_jobinfo_set(job_ptr->select_jobinfo,
				    SELECT_JOBDATA_NETWORK,
				    job_ptr->network);

	if (job_desc->ckpt_dir)
		detail_ptr->ckpt_dir = xstrdup(job_desc->ckpt_dir);
	else
		detail_ptr->ckpt_dir = xstrdup(detail_ptr->work_dir);

	/* The priority needs to be set after this since we don't have
	 * an association rec yet
	 */

	detail_ptr->mc_ptr = _set_multi_core_data(job_desc);
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

	hostlist_t hl = hostlist_create(node_list);
	if (hl == NULL)
		return NULL;
	hostlist_uniq(hl);
	buf = hostlist_ranged_string_xmalloc(hl);
	hostlist_destroy(hl);

	return buf;
}

static bool _valid_pn_min_mem(job_desc_msg_t * job_desc_msg,
			      struct part_record *part_ptr)
{
	uint32_t job_mem_limit = job_desc_msg->pn_min_memory;
	uint32_t sys_mem_limit;
	uint16_t cpus_per_node;

	if (part_ptr && part_ptr->max_mem_per_cpu)
		sys_mem_limit = part_ptr->max_mem_per_cpu;
	else
		sys_mem_limit = slurmctld_conf.max_mem_per_cpu;

	if ((sys_mem_limit == 0) || (sys_mem_limit == MEM_PER_CPU))
		return true;

	if ((job_mem_limit & MEM_PER_CPU) && (sys_mem_limit & MEM_PER_CPU)) {
		uint32_t mem_ratio;
		job_mem_limit &= (~MEM_PER_CPU);
		sys_mem_limit &= (~MEM_PER_CPU);
		if (job_mem_limit <= sys_mem_limit)
			return true;
		mem_ratio = (job_mem_limit + sys_mem_limit - 1);
		mem_ratio /= sys_mem_limit;
		debug("increasing cpus_per_task and decreasing mem_per_cpu by "
		      "factor of %u based upon mem_per_cpu limits", mem_ratio);
		if (job_desc_msg->cpus_per_task == (uint16_t) NO_VAL)
			job_desc_msg->cpus_per_task = mem_ratio;
		else
			job_desc_msg->cpus_per_task *= mem_ratio;
		job_desc_msg->pn_min_memory = ((job_mem_limit + mem_ratio - 1) /
					       mem_ratio) | MEM_PER_CPU;
		return true;
	}

	if (((job_mem_limit & MEM_PER_CPU) == 0) &&
	    ((sys_mem_limit & MEM_PER_CPU) == 0)) {
		if (job_mem_limit <= sys_mem_limit)
			return true;
		return false;
	}

	/* Our size is per CPU and limit per node or vice-versa.
	 * CPU count my vary by node, but we don't have a good
	 * way to identify specific nodes for the job at this
	 * point, so just pick the first node as a basis for enforcing
	 * MaxMemPerCPU and convert both numbers to per-node values. */
	if (slurmctld_conf.fast_schedule)
		cpus_per_node = node_record_table_ptr[0].config_ptr->cpus;
	else
		cpus_per_node = node_record_table_ptr[0].cpus;
	if (job_desc_msg->min_cpus != NO_VAL)
		cpus_per_node = MIN(cpus_per_node, job_desc_msg->min_cpus);
	if (job_mem_limit & MEM_PER_CPU) {
		job_mem_limit &= (~MEM_PER_CPU);
		job_mem_limit *= cpus_per_node;
	} else {
		uint32_t min_cpus;
		sys_mem_limit &= (~MEM_PER_CPU);
		min_cpus = (job_mem_limit + sys_mem_limit - 1) / sys_mem_limit;
		if ((job_desc_msg->pn_min_cpus == (uint16_t) NO_VAL) ||
		    (job_desc_msg->pn_min_cpus < min_cpus)) {
			debug("Setting job's pn_min_cpus to %u due to memory "
			      "limit", min_cpus);
			job_desc_msg->pn_min_cpus = min_cpus;
			sys_mem_limit *= min_cpus;
		} else {
			sys_mem_limit *= cpus_per_node;
		}
	}
	if (job_mem_limit <= sys_mem_limit)
		return true;
	return false;
}

/*
 * job_time_limit - terminate jobs which have exceeded their time limit
 * global: job_list - pointer global job list
 *	last_job_update - time of last job table update
 * NOTE: READ lock_slurmctld config before entry
 */
void job_time_limit(void)
{
	ListIterator job_iterator;
	struct job_record *job_ptr;
	time_t now = time(NULL);
	time_t old = now - ((slurmctld_conf.inactive_limit * 4 / 3) +
			    slurmctld_conf.msg_timeout + 1);
	time_t over_run;
	int resv_status = 0;

	if (slurmctld_conf.over_time_limit == (uint16_t) INFINITE)
		over_run = now - (365 * 24 * 60 * 60);	/* one year */
	else
		over_run = now - (slurmctld_conf.over_time_limit  * 60);

	begin_job_resv_check();
	job_iterator = list_iterator_create(job_list);
	while ((job_ptr =(struct job_record *) list_next(job_iterator))) {
		xassert (job_ptr->magic == JOB_MAGIC);

#ifndef HAVE_BG
		/* If the CONFIGURING flag is removed elsewhere like
		 * on a Bluegene system this check is not needed and
		 * should be avoided.  In the case of BG blocks that
		 * are booting aren't associated with
		 * power_node_bitmap so bit_overlap always returns 0
		 * and erroneously removes the flag.
		 */
		if (IS_JOB_CONFIGURING(job_ptr)) {
			if (!IS_JOB_RUNNING(job_ptr) ||
			    (bit_overlap(job_ptr->node_bitmap,
					 power_node_bitmap) == 0)) {
				debug("%s: Configuration for job %u is "
				      "complete",
				      __func__, job_ptr->job_id);
				job_ptr->job_state &= (~JOB_CONFIGURING);
			}
		}
#endif
		/* This needs to be near the top of the loop, checks every
		 * running, suspended and pending job */
		resv_status = job_resv_check(job_ptr);

		if (job_ptr->preempt_time &&
		    (IS_JOB_RUNNING(job_ptr) || IS_JOB_SUSPENDED(job_ptr))) {
			if ((job_ptr->warn_time) &&
			    (job_ptr->warn_time + PERIODIC_TIMEOUT + now >=
			     job_ptr->end_time)) {
				debug("%s: preempt warning signal %u to job %u ",
				      __func__, job_ptr->warn_signal,
				      job_ptr->job_id);
				(void) job_signal(job_ptr->job_id,
						  job_ptr->warn_signal,
						  job_ptr->warn_flags, 0,
						  false);
				job_ptr->warn_signal = 0;
				job_ptr->warn_time = 0;
			}
			if (job_ptr->end_time <= now) {
				last_job_update = now;
				info("%s: Preemption GraceTime reached JobId=%u",
				     __func__, job_ptr->job_id);
				_job_timed_out(job_ptr);
				job_ptr->job_state = JOB_PREEMPTED |
						     JOB_COMPLETING;
				xfree(job_ptr->state_desc);
			}
			continue;
		}

		if (!IS_JOB_RUNNING(job_ptr))
			continue;

		if (slurmctld_conf.inactive_limit &&
		    (job_ptr->batch_flag == 0)    &&
		    (job_ptr->time_last_active <= old) &&
		    (job_ptr->other_port) &&
		    (job_ptr->part_ptr) &&
		    (!(job_ptr->part_ptr->flags & PART_FLAG_ROOT_ONLY))) {
			/* job inactive, kill it */
			info("%s: inactivity time limit reached for JobId=%u",
			     __func__, job_ptr->job_id);
			_job_timed_out(job_ptr);
			job_ptr->state_reason = FAIL_INACTIVE_LIMIT;
			xfree(job_ptr->state_desc);
			continue;
		}
		if (job_ptr->time_limit != INFINITE) {
			if ((job_ptr->warn_time) &&
			    (job_ptr->warn_time + PERIODIC_TIMEOUT + now >=
			     job_ptr->end_time)) {

				/* If --signal B option was not specified,
				 * signal only the steps but not the batch step.
				 */
				if (job_ptr->warn_flags == 0)
					job_ptr->warn_flags = KILL_STEPS_ONLY;

				debug("%s: warning signal %u to job %u ",
				      __func__, job_ptr->warn_signal,
				      job_ptr->job_id);

				(void) job_signal(job_ptr->job_id,
						  job_ptr->warn_signal,
						  job_ptr->warn_flags, 0,
						  false);
				job_ptr->warn_signal = 0;
				job_ptr->warn_time = 0;
			}
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
			if (job_ptr->end_time <= over_run) {
				last_job_update = now;
				info("Time limit exhausted for JobId=%u",
				     job_ptr->job_id);
				_job_timed_out(job_ptr);
				job_ptr->state_reason = FAIL_TIMEOUT;
				xfree(job_ptr->state_desc);
				continue;
			}
		}

		if (resv_status != SLURM_SUCCESS) {
			last_job_update = now;
			info("Reservation ended for JobId=%u",
			     job_ptr->job_id);
			_job_timed_out(job_ptr);
			job_ptr->state_reason = FAIL_TIMEOUT;
			xfree(job_ptr->state_desc);
			continue;
		}

		/* check if any individual job steps have exceeded
		 * their time limit */
		if (job_ptr->step_list &&
		    (list_count(job_ptr->step_list) > 0))
			check_job_step_time_limit(job_ptr, now);

		acct_policy_job_time_out(job_ptr);

		if (job_ptr->state_reason == FAIL_TIMEOUT) {
			last_job_update = now;
			_job_timed_out(job_ptr);
			xfree(job_ptr->state_desc);
			continue;
		}

		/* Give srun command warning message about pending timeout */
		if (job_ptr->end_time <= (now + PERIODIC_TIMEOUT * 2))
			srun_timeout (job_ptr);
	}
	list_iterator_destroy(job_iterator);
	fini_job_resv_check();
}

extern int job_update_cpu_cnt(struct job_record *job_ptr, int node_inx)
{
	int cnt, offset, rc = SLURM_SUCCESS;

	xassert(job_ptr);

#ifdef HAVE_BG
	/* This function doesn't apply to a bluegene system since the
	 * cpu count isn't set up on that system. */
	return SLURM_SUCCESS;
#endif
	if (job_ptr->details->whole_node) {
		/* Since we are allocating whole nodes don't rely on
		 * the job_resrcs since it could be less because the
		 * node could of only used 1 thread per core.
		 */
		struct node_record *node_ptr =
			node_record_table_ptr + node_inx;
		if (slurmctld_conf.fast_schedule)
			cnt = node_ptr->config_ptr->cpus;
		else
			cnt = node_ptr->cpus;
	} else {
		if ((offset = job_resources_node_inx_to_cpu_inx(
			     job_ptr->job_resrcs, node_inx)) < 0) {
			error("job_update_cpu_cnt: problem getting "
			      "offset of job %u",
			      job_ptr->job_id);
			job_ptr->cpu_cnt = 0;
			return SLURM_ERROR;
		}

		cnt = job_ptr->job_resrcs->cpus[offset];
	}
	if (cnt > job_ptr->cpu_cnt) {
		error("job_update_cpu_cnt: cpu_cnt underflow on job_id %u",
		      job_ptr->job_id);
		job_ptr->cpu_cnt = 0;
		rc = SLURM_ERROR;
	} else
		job_ptr->cpu_cnt -= cnt;

	if (IS_JOB_RESIZING(job_ptr)) {
		if (cnt > job_ptr->total_cpus) {
			error("job_update_cpu_cnt: total_cpus "
			      "underflow on job_id %u",
			      job_ptr->job_id);
			job_ptr->total_cpus = 0;
			rc = SLURM_ERROR;
		} else
			job_ptr->total_cpus -= cnt;
	}
	return rc;
}

/* Terminate a job that has exhausted its time limit */
static void _job_timed_out(struct job_record *job_ptr)
{
	xassert(job_ptr);

	srun_timeout(job_ptr);
	if (job_ptr->details) {
		time_t now      = time(NULL);
		job_ptr->end_time           = now;
		job_ptr->time_last_active   = now;
		job_ptr->job_state          = JOB_TIMEOUT | JOB_COMPLETING;
		build_cg_bitmap(job_ptr);
		job_ptr->exit_code = MAX(job_ptr->exit_code, 1);
		job_completion_logger(job_ptr, false);
		deallocate_nodes(job_ptr, true, false, false);
	} else
		job_signal(job_ptr->job_id, SIGKILL, 0, 0, false);
	return;
}

/* _validate_job_desc - validate that a job descriptor for job submit or
 *	allocate has valid data, set values to defaults as required
 * IN/OUT job_desc_msg - pointer to job descriptor, modified as needed
 * IN allocate - if clear job to be queued, if set allocate for user now
 * IN submit_uid - who request originated
 */
static int _validate_job_desc(job_desc_msg_t * job_desc_msg, int allocate,
                              uid_t submit_uid, struct part_record *part_ptr,
                              List part_list)
{
	if ((job_desc_msg->min_cpus  == NO_VAL) &&
	    (job_desc_msg->min_nodes == NO_VAL) &&
	    (job_desc_msg->req_nodes == NULL)) {
		info("Job specified no min_cpus, min_nodes or req_nodes");
		return ESLURM_JOB_MISSING_SIZE_SPECIFICATION;
	}
	if ((allocate == SLURM_CREATE_JOB_FLAG_NO_ALLOCATE_0) &&
	    (job_desc_msg->script == NULL)) {
		info("_validate_job_desc: job failed to specify Script");
		return ESLURM_JOB_SCRIPT_MISSING;
	}
	if (job_desc_msg->user_id == NO_VAL) {
		info("_validate_job_desc: job failed to specify User");
		return ESLURM_USER_ID_MISSING;
	}
	if ( job_desc_msg->group_id == NO_VAL ) {
		debug("_validate_job_desc: job failed to specify group");
		job_desc_msg->group_id = 0;	/* uses user default */
	}
	if (job_desc_msg->contiguous == (uint16_t) NO_VAL)
		job_desc_msg->contiguous = 0;

	if (job_desc_msg->task_dist == (uint16_t) NO_VAL) {
		/* not typically set by salloc or sbatch */
		job_desc_msg->task_dist = SLURM_DIST_CYCLIC;
	}
	if (job_desc_msg->plane_size == (uint16_t) NO_VAL)
		job_desc_msg->plane_size = 0;

	if (job_desc_msg->kill_on_node_fail == (uint16_t) NO_VAL)
		job_desc_msg->kill_on_node_fail = 1;

	if (job_desc_msg->job_id != NO_VAL) {
		struct job_record *dup_job_ptr;
		if ((submit_uid != 0) &&
		    (submit_uid != slurmctld_conf.slurm_user_id)) {
			info("attempt by uid %u to set job_id", submit_uid);
			return ESLURM_INVALID_JOB_ID;
		}
		if (job_desc_msg->job_id == 0) {
			info("attempt by uid %u to set zero job_id",
			     submit_uid);
			return ESLURM_INVALID_JOB_ID;
		}
		dup_job_ptr = find_job_record((uint32_t) job_desc_msg->job_id);
		if (dup_job_ptr) {
			info("attempt re-use active job_id %u",
			     job_desc_msg->job_id);
			return ESLURM_DUPLICATE_JOB_ID;
		}
	}


	if (job_desc_msg->nice == (uint16_t) NO_VAL)
		job_desc_msg->nice = NICE_OFFSET;

	if (job_desc_msg->pn_min_memory == NO_VAL) {
		/* Default memory limit is DefMemPerCPU (if set) or no limit */
		if (part_ptr && part_ptr->def_mem_per_cpu) {
			job_desc_msg->pn_min_memory =
					part_ptr->def_mem_per_cpu;
		} else {
			job_desc_msg->pn_min_memory =
					slurmctld_conf.def_mem_per_cpu;
		}
	} else if (!_validate_min_mem_partition(job_desc_msg, part_ptr, part_list))
		return ESLURM_INVALID_TASK_MEMORY;

	/* Validate a job's accounting frequency, if specified */
	if (acct_gather_check_acct_freq_task(
		    job_desc_msg->pn_min_memory, job_desc_msg->acctg_freq))
		return ESLURMD_INVALID_ACCT_FREQ;

	if (job_desc_msg->min_nodes == NO_VAL)
		job_desc_msg->min_nodes = 1;	/* default node count of 1 */
	if (job_desc_msg->min_cpus == NO_VAL)
		job_desc_msg->min_cpus = job_desc_msg->min_nodes;

	if ((job_desc_msg->pn_min_cpus == (uint16_t) NO_VAL) ||
	    (job_desc_msg->pn_min_cpus == 0))
		job_desc_msg->pn_min_cpus = 1;   /* default 1 cpu per node */
	if (job_desc_msg->pn_min_tmp_disk == NO_VAL)
		job_desc_msg->pn_min_tmp_disk = 0;/* default 0MB disk per node */

	return SLURM_SUCCESS;
}

/* _validate_pn_min_mem()
 * Traverse the list of partitions and invoke the
 * function validating the job memory specification.
 */
static bool
_validate_min_mem_partition(job_desc_msg_t *job_desc_msg,
                            struct part_record *part_ptr, List part_list)
{
	ListIterator iter;
	struct part_record *part;
	bool cc;

	if (part_list == NULL)
		return _valid_pn_min_mem(job_desc_msg, part_ptr);

	cc = false;
	iter = list_iterator_create(part_list);
	while ((part = list_next(iter))) {
		if ((cc = _valid_pn_min_mem(job_desc_msg, part)))
			break;
	}
	list_iterator_destroy(iter);

	return cc;
}

/*
 * _list_delete_job - delete a job record and its corresponding job_details,
 *	see common/list.h for documentation
 * IN job_entry - pointer to job_record to delete
 */
static void _list_delete_job(void *job_entry)
{
	struct job_record *job_ptr = (struct job_record *) job_entry;
	struct job_record **job_pptr, *tmp_ptr;
	int job_array_size, i;

	xassert(job_entry);
	xassert (job_ptr->magic == JOB_MAGIC);
	job_ptr->magic = 0;	/* make sure we don't delete record twice */

	/* Remove the record from job hash table */
	job_pptr = &job_hash[JOB_HASH_INX(job_ptr->job_id)];
	while ((job_pptr != NULL) && (*job_pptr != NULL) &&
	       ((tmp_ptr = *job_pptr) != (struct job_record *) job_entry)) {
		xassert(tmp_ptr->magic == JOB_MAGIC);
		job_pptr = &tmp_ptr->job_next;
	}
	if (job_pptr == NULL)
		error("job hash error");
	else
		*job_pptr = job_ptr->job_next;

	if (job_ptr->array_recs) {
		job_array_size = MAX(1, job_ptr->array_recs->task_cnt);
	} else {
		job_array_size = 1;
	}

	/* Remove the record from job array hash tables, if applicable */
	if (job_ptr->array_task_id != NO_VAL) {
		job_pptr = &job_array_hash_j[
			JOB_HASH_INX(job_ptr->array_job_id)];
		while ((job_pptr != NULL) && (*job_pptr != NULL) &&
		       ((tmp_ptr = *job_pptr) !=
			(struct job_record *) job_entry)) {
			xassert(tmp_ptr->magic == JOB_MAGIC);
			job_pptr = &tmp_ptr->job_array_next_j;
		}
		if (job_pptr == NULL)
			error("job array hash error");
		else
			*job_pptr = job_ptr->job_array_next_j;

		job_pptr = &job_array_hash_t[
			JOB_ARRAY_HASH_INX(job_ptr->array_job_id,
					   job_ptr->array_task_id)];
		while ((job_pptr != NULL) && (*job_pptr != NULL) &&
		       ((tmp_ptr = *job_pptr) !=
			(struct job_record *) job_entry)) {
			xassert(tmp_ptr->magic == JOB_MAGIC);
			job_pptr = &tmp_ptr->job_array_next_t;
		}
		if (job_pptr == NULL)
			error("job array, task ID hash error");
		else
			*job_pptr = job_ptr->job_array_next_t;
	}

	delete_job_details(job_ptr);
	xfree(job_ptr->account);
	xfree(job_ptr->alias_list);
	xfree(job_ptr->alloc_node);
	if (job_ptr->array_recs) {
		FREE_NULL_BITMAP(job_ptr->array_recs->task_id_bitmap);
		xfree(job_ptr->array_recs->task_id_str);
		xfree(job_ptr->array_recs);
	}
	xfree(job_ptr->batch_host);
	xfree(job_ptr->comment);
	free_job_resources(&job_ptr->job_resrcs);
	xfree(job_ptr->gres);
	xfree(job_ptr->gres_alloc);
	xfree(job_ptr->gres_req);
	xfree(job_ptr->gres_used);
	FREE_NULL_LIST(job_ptr->gres_list);
	xfree(job_ptr->licenses);
	FREE_NULL_LIST(job_ptr->license_list);
	xfree(job_ptr->mail_user);
	xfree(job_ptr->name);
	xfree(job_ptr->network);
	xfree(job_ptr->node_addr);
	FREE_NULL_BITMAP(job_ptr->node_bitmap);
	FREE_NULL_BITMAP(job_ptr->node_bitmap_cg);
	xfree(job_ptr->nodes);
	xfree(job_ptr->nodes_completing);
	xfree(job_ptr->partition);
	FREE_NULL_LIST(job_ptr->part_ptr_list);
	xfree(job_ptr->priority_array);
	slurm_destroy_priority_factors_object(job_ptr->prio_factors);
	xfree(job_ptr->resp_host);
	xfree(job_ptr->resv_name);
	xfree(job_ptr->sched_nodes);
	for (i = 0; i < job_ptr->spank_job_env_size; i++)
		xfree(job_ptr->spank_job_env[i]);
	xfree(job_ptr->spank_job_env);
	xfree(job_ptr->state_desc);
	step_list_purge(job_ptr);
	select_g_select_jobinfo_free(job_ptr->select_jobinfo);
	xfree(job_ptr->wckey);
	if (job_array_size > job_count) {
		error("job_count underflow");
		job_count = 0;
	} else {
		job_count -= job_array_size;
	}
	xfree(job_ptr);
}


/*
 * _list_find_job_id - find specific job_id entry in the job list,
 *	see common/list.h for documentation, key is job_id_ptr
 * global- job_list - the global partition list
 */
static int _list_find_job_id(void *job_entry, void *key)
{
	uint32_t *job_id_ptr = (uint32_t *) key;

	if (((struct job_record *) job_entry)->job_id == *job_id_ptr)
		return 1;
	else
		return 0;
}


/*
 * _list_find_job_old - find old entries in the job list,
 *	see common/list.h for documentation, key is ignored
 * global- job_list - the global partition list
 */
static int _list_find_job_old(void *job_entry, void *key)
{
	time_t kill_age, min_age, now = time(NULL);;
	struct job_record *job_ptr = (struct job_record *)job_entry;
	uint16_t cleaning = 0;

	if (IS_JOB_COMPLETING(job_ptr)) {
		kill_age = now - (slurmctld_conf.kill_wait +
				  2 * slurm_get_msg_timeout());
		if (job_ptr->time_last_active < kill_age) {
			job_ptr->time_last_active = now;
			re_kill_job(job_ptr);
		}
		return 0;       /* Job still completing */
	}

	if (job_ptr->epilog_running)
		return 0;       /* EpilogSlurmctld still running */

	if (slurmctld_conf.min_job_age == 0)
		return 0;	/* No job record purging */

	min_age  = now - slurmctld_conf.min_job_age;
	if (job_ptr->end_time > min_age)
		return 0;	/* Too new to purge */

	if (!(IS_JOB_FINISHED(job_ptr)))
		return 0;	/* Job still active */

	if (job_ptr->step_list && list_count(job_ptr->step_list)) {
		debug("Job %u still has %d active steps",
		      job_ptr->job_id, list_count(job_ptr->step_list));
		return 0;	/* steps are still active */
	}

	if (job_ptr->array_recs) {
		if (job_ptr->array_recs->tot_run_tasks ||
		    !test_job_array_completed(job_ptr->array_job_id)) {
			/* Some tasks from this job array still active */
			return 0;
		}
	}

	select_g_select_jobinfo_get(job_ptr->select_jobinfo,
				    SELECT_JOBDATA_CLEANING,
				    &cleaning);
	if (cleaning)
		return 0;      /* Job hasn't finished yet */

	/* If we don't have a db_index by now and we are running with
	   the slurmdbd lets put it on the list to be handled later
	   when it comes back up since we won't get another chance.
	*/
	if (with_slurmdbd && !job_ptr->db_index)
		jobacct_storage_g_job_start(acct_db_conn, job_ptr);

	return 1;		/* Purge the job */
}

/* Determine if ALL partitions associated with a job are hidden */
static bool _all_parts_hidden(struct job_record *job_ptr)
{
	bool rc;
	ListIterator part_iterator;
	struct part_record *part_ptr;

	if (job_ptr->part_ptr_list) {
		rc = true;
		part_iterator = list_iterator_create(job_ptr->part_ptr_list);
		while ((part_ptr = (struct part_record *)
				   list_next(part_iterator))) {
			if (!(part_ptr->flags & PART_FLAG_HIDDEN)) {
				rc = false;
				break;
			}
		}
		list_iterator_destroy(part_iterator);
		return rc;
	}

	if ((job_ptr->part_ptr) &&
	    (job_ptr->part_ptr->flags & PART_FLAG_HIDDEN))
		return true;
	return false;
}

/* Determine if a given job should be seen by a specific user */
static bool _hide_job(struct job_record *job_ptr, uid_t uid)
{
	if ((slurmctld_conf.private_data & PRIVATE_DATA_JOBS) &&
	    (job_ptr->user_id != uid) && !validate_operator(uid) &&
	    !assoc_mgr_is_user_acct_coord(acct_db_conn, uid, job_ptr->account))
		return true;
	return false;
}

/*
 * pack_all_jobs - dump all job information for all jobs in
 *	machine independent form (for network transmission)
 * OUT buffer_ptr - the pointer is set to the allocated buffer.
 * OUT buffer_size - set to size of the buffer in bytes
 * IN show_flags - job filtering options
 * IN uid - uid of user making request (for partition filtering)
 * IN filter_uid - pack only jobs belonging to this user if not NO_VAL
 * global: job_list - global list of job records
 * NOTE: the buffer at *buffer_ptr must be xfreed by the caller
 * NOTE: change _unpack_job_desc_msg() in common/slurm_protocol_pack.c
 *	whenever the data format changes
 */
extern void pack_all_jobs(char **buffer_ptr, int *buffer_size,
			  uint16_t show_flags, uid_t uid, uint32_t filter_uid,
			  uint16_t protocol_version)
{
	ListIterator job_iterator;
	struct job_record *job_ptr;
	uint32_t jobs_packed = 0, tmp_offset;
	Buf buffer;

	buffer_ptr[0] = NULL;
	*buffer_size = 0;

	buffer = init_buf(BUF_SIZE);

	/* write message body header : size and time */
	/* put in a place holder job record count of 0 for now */
	pack32(jobs_packed, buffer);
	pack_time(time(NULL), buffer);

	/* write individual job records */
	part_filter_set(uid);
	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		xassert (job_ptr->magic == JOB_MAGIC);

		if (((show_flags & SHOW_ALL) == 0) && (uid != 0) &&
		    _all_parts_hidden(job_ptr))
			continue;

		if (_hide_job(job_ptr, uid))
			continue;

		if ((filter_uid != NO_VAL) && (filter_uid != job_ptr->user_id))
			continue;

		pack_job(job_ptr, show_flags, buffer, protocol_version, uid);
		jobs_packed++;
	}
	part_filter_clear();
	list_iterator_destroy(job_iterator);

	/* put the real record count in the message body header */
	tmp_offset = get_buf_offset(buffer);
	set_buf_offset(buffer, 0);
	pack32(jobs_packed, buffer);
	set_buf_offset(buffer, tmp_offset);

	*buffer_size = get_buf_offset(buffer);
	buffer_ptr[0] = xfer_buf_data(buffer);
}

/*
 * pack_one_job - dump information for one jobs in
 *	machine independent form (for network transmission)
 * OUT buffer_ptr - the pointer is set to the allocated buffer.
 * OUT buffer_size - set to size of the buffer in bytes
 * IN job_id - ID of job that we want info for
 * IN show_flags - job filtering options
 * IN uid - uid of user making request (for partition filtering)
 * NOTE: the buffer at *buffer_ptr must be xfreed by the caller
 * NOTE: change _unpack_job_desc_msg() in common/slurm_protocol_pack.c
 *	whenever the data format changes
 */
extern int pack_one_job(char **buffer_ptr, int *buffer_size,
			uint32_t job_id, uint16_t show_flags, uid_t uid,
			uint16_t protocol_version)
{
	struct job_record *job_ptr;
	uint32_t jobs_packed = 0, tmp_offset;
	Buf buffer;

	buffer_ptr[0] = NULL;
	*buffer_size = 0;

	buffer = init_buf(BUF_SIZE);

	/* write message body header : size and time */
	/* put in a place holder job record count of 0 for now */
	pack32(jobs_packed, buffer);
	pack_time(time(NULL), buffer);

	job_ptr = find_job_record(job_id);
	if (job_ptr && (job_ptr->array_task_id == NO_VAL) &&
	    !job_ptr->array_recs) {
		if (!_hide_job(job_ptr, uid)) {
			pack_job(job_ptr, show_flags, buffer, protocol_version,
				 uid);
			jobs_packed++;
		}
	} else {
		bool packed_head = false;

		/* Either the job is not found or it is a job array */
		if (job_ptr) {
			packed_head = true;
			if (!_hide_job(job_ptr, uid)) {
				pack_job(job_ptr, show_flags, buffer,
					 protocol_version, uid);
				jobs_packed++;
			}
		}

		job_ptr = job_array_hash_j[JOB_HASH_INX(job_id)];
		while (job_ptr) {
			if ((job_ptr->job_id == job_id) && packed_head) {
				;	/* Already packed */
			} else if (job_ptr->array_job_id == job_id) {
				if (_hide_job(job_ptr, uid))
					break;
				pack_job(job_ptr, show_flags, buffer,
					 protocol_version, uid);
				jobs_packed++;
			}
			job_ptr = job_ptr->job_array_next_j;
		}
	}

	if (jobs_packed == 0) {
		free_buf(buffer);
		return ESLURM_INVALID_JOB_ID;
	}

	/* put the real record count in the message body header */
	tmp_offset = get_buf_offset(buffer);
	set_buf_offset(buffer, 0);
	pack32(jobs_packed, buffer);
	set_buf_offset(buffer, tmp_offset);

	*buffer_size = get_buf_offset(buffer);
	buffer_ptr[0] = xfer_buf_data(buffer);

	return SLURM_SUCCESS;
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
void pack_job(struct job_record *dump_job_ptr, uint16_t show_flags, Buf buffer,
	      uint16_t protocol_version, uid_t uid)
{
	struct job_details *detail_ptr;
	time_t begin_time = 0;
	char *nodelist = NULL;
	assoc_mgr_lock_t locks = { NO_LOCK, NO_LOCK,
				   READ_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };

	if (protocol_version >= SLURM_14_11_PROTOCOL_VERSION) {
		detail_ptr = dump_job_ptr->details;
		pack32(dump_job_ptr->array_job_id, buffer);
		pack32(dump_job_ptr->array_task_id, buffer);
		if (dump_job_ptr->array_recs) {
			build_array_str(dump_job_ptr);
			packstr(dump_job_ptr->array_recs->task_id_str, buffer);
			pack32(dump_job_ptr->array_recs->max_run_tasks, buffer);
		} else {
			packnull(buffer);
			pack32((uint32_t) 0, buffer);
		}
		pack32(dump_job_ptr->assoc_id, buffer);
		pack32(dump_job_ptr->job_id,   buffer);
		pack32(dump_job_ptr->user_id,  buffer);
		pack32(dump_job_ptr->group_id, buffer);
		pack32(dump_job_ptr->profile,  buffer);

		pack16(dump_job_ptr->job_state,    buffer);
		pack16(dump_job_ptr->batch_flag,   buffer);
		pack16(dump_job_ptr->state_reason, buffer);
		pack8(dump_job_ptr->reboot,        buffer);
		pack16(dump_job_ptr->restart_cnt,  buffer);
		pack16(show_flags,  buffer);

		pack32(dump_job_ptr->alloc_sid, buffer);
		if ((dump_job_ptr->time_limit == NO_VAL)
		    && dump_job_ptr->part_ptr)
			pack32(dump_job_ptr->part_ptr->max_time, buffer);
		else
			pack32(dump_job_ptr->time_limit, buffer);
		pack32(dump_job_ptr->time_min, buffer);

		if (dump_job_ptr->details) {
			pack16(dump_job_ptr->details->nice,  buffer);
			pack_time(dump_job_ptr->details->submit_time, buffer);
			/* Earliest possible begin time */
			begin_time = dump_job_ptr->details->begin_time;
		} else {
			pack16(0, buffer);
			pack_time((time_t) 0, buffer);
		}

		pack_time(begin_time, buffer);
		/* Actual or expected start time */
		if ((dump_job_ptr->start_time) || (begin_time <= time(NULL)))
			pack_time(dump_job_ptr->start_time, buffer);
		else	/* earliest start time in the future */
			pack_time(begin_time, buffer);

		pack_time(dump_job_ptr->end_time, buffer);
		pack_time(dump_job_ptr->suspend_time, buffer);
		pack_time(dump_job_ptr->pre_sus_time, buffer);
		pack_time(dump_job_ptr->resize_time, buffer);
		pack_time(dump_job_ptr->preempt_time, buffer);
		pack32(dump_job_ptr->priority, buffer);

		/* Only send the allocated nodelist since we are only sending
		 * the number of cpus and nodes that are currently allocated. */
		if (!IS_JOB_COMPLETING(dump_job_ptr))
			packstr(dump_job_ptr->nodes, buffer);
		else {
			nodelist =
				bitmap2node_name(dump_job_ptr->node_bitmap_cg);
			packstr(nodelist, buffer);
			xfree(nodelist);
		}

		packstr(dump_job_ptr->sched_nodes, buffer);

		if (!IS_JOB_PENDING(dump_job_ptr) && dump_job_ptr->part_ptr)
			packstr(dump_job_ptr->part_ptr->name, buffer);
		else
			packstr(dump_job_ptr->partition, buffer);
		packstr(dump_job_ptr->account, buffer);
		packstr(dump_job_ptr->network, buffer);
		packstr(dump_job_ptr->comment, buffer);
		packstr(dump_job_ptr->gres, buffer);
		packstr(dump_job_ptr->batch_host, buffer);
		if (!IS_JOB_COMPLETED(dump_job_ptr) &&
		    (show_flags & SHOW_DETAIL2) &&
		    ((dump_job_ptr->user_id == (uint32_t) uid) ||
		     validate_operator(uid))) {
			char *batch_script = get_job_script(dump_job_ptr);
			packstr(batch_script, buffer);
			xfree(batch_script);
		} else {
			packnull(buffer);
		}

		assoc_mgr_lock(&locks);
		if (assoc_mgr_qos_list) {
			packstr(slurmdb_qos_str(assoc_mgr_qos_list,
						dump_job_ptr->qos_id), buffer);
		} else
			packnull(buffer);
		assoc_mgr_unlock(&locks);

		packstr(dump_job_ptr->licenses, buffer);
		packstr(dump_job_ptr->state_desc, buffer);
		packstr(dump_job_ptr->resv_name, buffer);

		pack32(dump_job_ptr->exit_code, buffer);
		pack32(dump_job_ptr->derived_ec, buffer);

		if (show_flags & SHOW_DETAIL) {
			pack_job_resources(dump_job_ptr->job_resrcs, buffer,
					   protocol_version);
		} else {
			uint32_t empty = NO_VAL;
			pack32(empty, buffer);
		}

		packstr(dump_job_ptr->name, buffer);
		packstr(dump_job_ptr->wckey, buffer);
		pack32(dump_job_ptr->req_switch, buffer);
		pack32(dump_job_ptr->wait4switch, buffer);

		packstr(dump_job_ptr->alloc_node, buffer);
		if (!IS_JOB_COMPLETING(dump_job_ptr))
			pack_bit_fmt(dump_job_ptr->node_bitmap, buffer);
		else
			pack_bit_fmt(dump_job_ptr->node_bitmap_cg, buffer);

		select_g_select_jobinfo_pack(dump_job_ptr->select_jobinfo,
					     buffer, protocol_version);

		/* A few details are always dumped here */
		_pack_default_job_details(dump_job_ptr, buffer,
					  protocol_version);

		/* other job details are only dumped until the job starts
		 * running (at which time they become meaningless) */
		if (detail_ptr)
			_pack_pending_job_details(detail_ptr, buffer,
						  protocol_version);
		else
			_pack_pending_job_details(NULL, buffer,
						  protocol_version);
	} else if (protocol_version >= SLURM_14_03_PROTOCOL_VERSION) {
		detail_ptr = dump_job_ptr->details;
		pack32(dump_job_ptr->array_job_id, buffer);
		pack32(dump_job_ptr->array_task_id, buffer);
		pack32(dump_job_ptr->assoc_id, buffer);
		pack32(dump_job_ptr->job_id, buffer);
		pack32(dump_job_ptr->user_id, buffer);
		pack32(dump_job_ptr->group_id, buffer);
		pack32(dump_job_ptr->profile, buffer);

		pack16(dump_job_ptr->job_state,    buffer);
		pack16(dump_job_ptr->batch_flag,   buffer);
		pack16(dump_job_ptr->state_reason, buffer);
		pack16(dump_job_ptr->restart_cnt,  buffer);
		pack16(show_flags,  buffer);

		pack32(dump_job_ptr->alloc_sid, buffer);
		if ((dump_job_ptr->time_limit == NO_VAL)
		    && dump_job_ptr->part_ptr)
			pack32(dump_job_ptr->part_ptr->max_time, buffer);
		else
			pack32(dump_job_ptr->time_limit, buffer);
		pack32(dump_job_ptr->time_min, buffer);

		if (dump_job_ptr->details) {
			pack16(dump_job_ptr->details->nice,  buffer);
			pack_time(dump_job_ptr->details->submit_time, buffer);
			/* Earliest possible begin time */
			begin_time = dump_job_ptr->details->begin_time;
		} else {
			pack16(0, buffer);
			pack_time((time_t) 0, buffer);
		}

		pack_time(begin_time, buffer);
		/* Actual or expected start time */
		if ((dump_job_ptr->start_time) || (begin_time <= time(NULL)))
			pack_time(dump_job_ptr->start_time, buffer);
		else	/* earliest start time in the future */
			pack_time(begin_time, buffer);

		pack_time(dump_job_ptr->end_time, buffer);
		pack_time(dump_job_ptr->suspend_time, buffer);
		pack_time(dump_job_ptr->pre_sus_time, buffer);
		pack_time(dump_job_ptr->resize_time, buffer);
		pack_time(dump_job_ptr->preempt_time, buffer);
		pack32(dump_job_ptr->priority, buffer);

		/* Only send the allocated nodelist since we are only sending
		 * the number of cpus and nodes that are currently allocated. */
		if (!IS_JOB_COMPLETING(dump_job_ptr))
			packstr(dump_job_ptr->nodes, buffer);
		else {
			nodelist =
				bitmap2node_name(dump_job_ptr->node_bitmap_cg);
			packstr(nodelist, buffer);
			xfree(nodelist);
		}

		if (!IS_JOB_PENDING(dump_job_ptr) && dump_job_ptr->part_ptr)
			packstr(dump_job_ptr->part_ptr->name, buffer);
		else
			packstr(dump_job_ptr->partition, buffer);
		packstr(dump_job_ptr->account, buffer);
		packstr(dump_job_ptr->network, buffer);
		packstr(dump_job_ptr->comment, buffer);
		packstr(dump_job_ptr->gres, buffer);
		packstr(dump_job_ptr->batch_host, buffer);
		if (!IS_JOB_COMPLETED(dump_job_ptr) &&
		    (show_flags & SHOW_DETAIL2) &&
		    ((dump_job_ptr->user_id == (uint32_t) uid) ||
		     validate_operator(uid))) {
			char *batch_script = get_job_script(dump_job_ptr);
			packstr(batch_script, buffer);
			xfree(batch_script);
		} else {
			packnull(buffer);
		}

		assoc_mgr_lock(&locks);
		if (assoc_mgr_qos_list) {
			packstr(slurmdb_qos_str(assoc_mgr_qos_list,
						dump_job_ptr->qos_id), buffer);
		} else
			packnull(buffer);
		assoc_mgr_unlock(&locks);

		packstr(dump_job_ptr->licenses, buffer);
		packstr(dump_job_ptr->state_desc, buffer);
		packstr(dump_job_ptr->resv_name, buffer);

		pack32(dump_job_ptr->exit_code, buffer);
		pack32(dump_job_ptr->derived_ec, buffer);

		if (show_flags & SHOW_DETAIL) {
			pack_job_resources(dump_job_ptr->job_resrcs, buffer,
					   protocol_version);
		} else {
			uint32_t empty = NO_VAL;
			pack32(empty, buffer);
		}

		packstr(dump_job_ptr->name, buffer);
		packstr(dump_job_ptr->wckey, buffer);
		pack32(dump_job_ptr->req_switch, buffer);
		pack32(dump_job_ptr->wait4switch, buffer);

		packstr(dump_job_ptr->alloc_node, buffer);
		if (!IS_JOB_COMPLETING(dump_job_ptr))
			pack_bit_fmt(dump_job_ptr->node_bitmap, buffer);
		else
			pack_bit_fmt(dump_job_ptr->node_bitmap_cg, buffer);

		select_g_select_jobinfo_pack(dump_job_ptr->select_jobinfo,
					     buffer, protocol_version);

		/* A few details are always dumped here */
		_pack_default_job_details(dump_job_ptr, buffer,
					  protocol_version);

		/* other job details are only dumped until the job starts
		 * running (at which time they become meaningless) */
		if (detail_ptr)
			_pack_pending_job_details(detail_ptr, buffer,
						  protocol_version);
		else
			_pack_pending_job_details(NULL, buffer,
						  protocol_version);
	} else if (protocol_version >= SLURM_2_6_PROTOCOL_VERSION) {
		pack32(dump_job_ptr->array_job_id, buffer);
		pack16((uint16_t) dump_job_ptr->array_task_id, buffer);
		pack32(dump_job_ptr->assoc_id, buffer);
		pack32(dump_job_ptr->job_id, buffer);
		pack32(dump_job_ptr->user_id, buffer);
		pack32(dump_job_ptr->group_id, buffer);
		pack32(dump_job_ptr->profile, buffer);

		pack16(dump_job_ptr->job_state,    buffer);
		pack16(dump_job_ptr->batch_flag,   buffer);
		pack16(dump_job_ptr->state_reason, buffer);
		pack16(dump_job_ptr->restart_cnt,  buffer);
		pack16(show_flags,  buffer);

		pack32(dump_job_ptr->alloc_sid, buffer);
		if ((dump_job_ptr->time_limit == NO_VAL)
		    && dump_job_ptr->part_ptr)
			pack32(dump_job_ptr->part_ptr->max_time, buffer);
		else
			pack32(dump_job_ptr->time_limit, buffer);
		pack32(dump_job_ptr->time_min, buffer);

		if (dump_job_ptr->details) {
			pack16(dump_job_ptr->details->nice,  buffer);
			pack_time(dump_job_ptr->details->submit_time, buffer);
			/* Earliest possible begin time */
			begin_time = dump_job_ptr->details->begin_time;
		} else {
			pack16(0, buffer);
			pack_time((time_t) 0, buffer);
		}

		pack_time(begin_time, buffer);
		/* Actual or expected start time */
		if ((dump_job_ptr->start_time) || (begin_time <= time(NULL)))
			pack_time(dump_job_ptr->start_time, buffer);
		else	/* earliest start time in the future */
			pack_time(begin_time, buffer);

		pack_time(dump_job_ptr->end_time, buffer);
		pack_time(dump_job_ptr->suspend_time, buffer);
		pack_time(dump_job_ptr->pre_sus_time, buffer);
		pack_time(dump_job_ptr->resize_time, buffer);
		pack_time(dump_job_ptr->preempt_time, buffer);
		pack32(dump_job_ptr->priority, buffer);

		/* Only send the allocated nodelist since we are only sending
		 * the number of cpus and nodes that are currently allocated. */
		if (!IS_JOB_COMPLETING(dump_job_ptr))
			packstr(dump_job_ptr->nodes, buffer);
		else {
			nodelist =
				bitmap2node_name(dump_job_ptr->node_bitmap_cg);
			packstr(nodelist, buffer);
			xfree(nodelist);
		}

		if (!IS_JOB_PENDING(dump_job_ptr) && dump_job_ptr->part_ptr)
			packstr(dump_job_ptr->part_ptr->name, buffer);
		else
			packstr(dump_job_ptr->partition, buffer);
		packstr(dump_job_ptr->account, buffer);
		packstr(dump_job_ptr->network, buffer);
		packstr(dump_job_ptr->comment, buffer);
		packstr(dump_job_ptr->gres, buffer);
		packstr(dump_job_ptr->batch_host, buffer);
		if (!IS_JOB_COMPLETED(dump_job_ptr) &&
		    (show_flags & SHOW_DETAIL2) &&
		    ((dump_job_ptr->user_id == (uint32_t) uid) ||
		     validate_operator(uid))) {
			char *batch_script = get_job_script(dump_job_ptr);
			packstr(batch_script, buffer);
			xfree(batch_script);
		} else {
			packnull(buffer);
		}

		assoc_mgr_lock(&locks);
		if (assoc_mgr_qos_list) {
			packstr(slurmdb_qos_str(assoc_mgr_qos_list,
						dump_job_ptr->qos_id), buffer);
		} else
			packnull(buffer);
		assoc_mgr_unlock(&locks);

		packstr(dump_job_ptr->licenses, buffer);
		packstr(dump_job_ptr->state_desc, buffer);
		packstr(dump_job_ptr->resv_name, buffer);

		pack32(dump_job_ptr->exit_code, buffer);
		pack32(dump_job_ptr->derived_ec, buffer);

		if (show_flags & SHOW_DETAIL) {
			pack_job_resources(dump_job_ptr->job_resrcs, buffer,
					   protocol_version);
		} else {
			uint32_t empty = NO_VAL;
			pack32(empty, buffer);
		}

		packstr(dump_job_ptr->name, buffer);
		packstr(dump_job_ptr->wckey, buffer);
		pack32(dump_job_ptr->req_switch, buffer);
		pack32(dump_job_ptr->wait4switch, buffer);

		packstr(dump_job_ptr->alloc_node, buffer);
		if (!IS_JOB_COMPLETING(dump_job_ptr))
			pack_bit_fmt(dump_job_ptr->node_bitmap, buffer);
		else
			pack_bit_fmt(dump_job_ptr->node_bitmap_cg, buffer);

		select_g_select_jobinfo_pack(dump_job_ptr->select_jobinfo,
					     buffer, protocol_version);

		detail_ptr = dump_job_ptr->details;
		/* A few details are always dumped here */
		_pack_default_job_details(dump_job_ptr, buffer,
					  protocol_version);

		/* other job details are only dumped until the job starts
		 * running (at which time they become meaningless) */
		if (detail_ptr)
			_pack_pending_job_details(detail_ptr, buffer,
						  protocol_version);
		else
			_pack_pending_job_details(NULL, buffer,
						  protocol_version);
	} else {
		error("pack_job: protocol_version "
		      "%hu not supported", protocol_version);
	}
}

static void _find_node_config(int *cpu_cnt_ptr, int *core_cnt_ptr)
{
	int i, max_cpu_cnt = 1, max_core_cnt = 1;
	struct node_record *node_ptr = node_record_table_ptr;

	for (i = 0; i < node_record_count; i++, node_ptr++) {
#ifndef HAVE_BG
		if (slurmctld_conf.fast_schedule) {
			/* Only data from config_record used for scheduling */
			max_cpu_cnt = MAX(max_cpu_cnt,
					  node_ptr->config_ptr->cpus);
			max_core_cnt =  MAX(max_core_cnt,
					    node_ptr->config_ptr->cores);
		} else {
#endif
			/* Individual node data used for scheduling */
			max_cpu_cnt = MAX(max_cpu_cnt, node_ptr->cpus);
			max_core_cnt =  MAX(max_core_cnt, node_ptr->cores);
#ifndef HAVE_BG
		}
#endif
	}
	*cpu_cnt_ptr  = max_cpu_cnt;
	*core_cnt_ptr = max_core_cnt;
}

/* pack default job details for "get_job_info" RPC */
static void _pack_default_job_details(struct job_record *job_ptr,
				      Buf buffer, uint16_t protocol_version)
{
	static int max_cpu_cnt = -1, max_core_cnt = -1;
	int i;
	struct job_details *detail_ptr = job_ptr->details;
	char *cmd_line = NULL;
	char *tmp = NULL;
	uint32_t len = 0;
	uint16_t shared = 0;

	if (!detail_ptr)
		shared = (uint16_t) NO_VAL;
	else if (detail_ptr->share_res == 1)	/* User --share */
		shared = 1;
	else if ((detail_ptr->share_res == 0) ||
		 (detail_ptr->whole_node == 1))	/* User --exclusive */
		shared = 0;
	else if (job_ptr->part_ptr) {
		/* Report shared status based upon latest partition info */
		if ((job_ptr->part_ptr->max_share & SHARED_FORCE) &&
		    ((job_ptr->part_ptr->max_share & (~SHARED_FORCE)) > 1))
			shared = 1;		/* Partition Shared=force */
		else if (job_ptr->part_ptr->max_share == 0)
			shared = 0;		/* Partition Shared=exclusive */
		else
			shared = (uint16_t) NO_VAL;  /* Part Shared=yes or no */
	} else
		shared = (uint16_t) NO_VAL;	/* No user or partition info */

	if (max_cpu_cnt == -1)
		_find_node_config(&max_cpu_cnt, &max_core_cnt);

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		if (detail_ptr) {
			packstr(detail_ptr->features,   buffer);
			packstr(detail_ptr->work_dir,   buffer);
			packstr(detail_ptr->dependency, buffer);

			if (detail_ptr->argv) {
				/* Determine size needed for a string
				 * containing all arguments */
				for (i=0; detail_ptr->argv[i]; i++) {
					len += strlen(detail_ptr->argv[i]);
				}
				len += i;

				cmd_line = xmalloc(len*sizeof(char));
				tmp = cmd_line;
				for (i=0; detail_ptr->argv[i]; i++) {
					if (i != 0) {
						*tmp = ' ';
						tmp++;
					}
					strcpy(tmp,detail_ptr->argv[i]);
					tmp += strlen(detail_ptr->argv[i]);
				}
				packstr(cmd_line, buffer);
				xfree(cmd_line);
			} else
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
				 * just incase this is 0 (startup or
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
				uint32_t min_cpus, min_nodes;
				min_cpus = detail_ptr->num_tasks *
					   detail_ptr->cpus_per_task;
				min_nodes = min_cpus + max_cpu_cnt - 1;
				min_nodes /= max_cpu_cnt;
				min_nodes = MAX(min_nodes,
						detail_ptr->min_nodes);
				pack32(min_nodes, buffer);
				pack32(detail_ptr->max_nodes, buffer);
			} else if (detail_ptr->mc_ptr &&
				   detail_ptr->mc_ptr->ntasks_per_core &&
				   (detail_ptr->mc_ptr->ntasks_per_core
				    != (uint16_t)INFINITE)) {
				/* min_nodes based upon task count and ntasks
				 * per core */
				uint32_t min_cores, min_nodes;
				min_cores = detail_ptr->num_tasks +
					    detail_ptr->mc_ptr->ntasks_per_core
					    - 1;
				min_cores /= detail_ptr->mc_ptr->ntasks_per_core;

				min_nodes = min_cores + max_core_cnt - 1;
				min_nodes /= max_core_cnt;
				min_nodes = MAX(min_nodes,
						detail_ptr->min_nodes);
				pack32(min_nodes, buffer);
				pack32(detail_ptr->max_nodes, buffer);
			} else {
				/* min_nodes based upon task count only */
				uint32_t min_nodes;
				min_nodes = detail_ptr->num_tasks +
					    max_cpu_cnt - 1;
				min_nodes /= max_cpu_cnt;
				min_nodes = MAX(min_nodes,
						detail_ptr->min_nodes);
				pack32(min_nodes, buffer);
				pack32(detail_ptr->max_nodes, buffer);
			}
			pack16(detail_ptr->requeue,   buffer);
			pack16(detail_ptr->ntasks_per_node, buffer);
			pack16(shared, buffer);
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
		}
	} else {
		error("_pack_default_job_details: protocol_version "
		      "%hu not supported", protocol_version);
	}
}

/* pack pending job details for "get_job_info" RPC */
static void _pack_pending_job_details(struct job_details *detail_ptr,
				      Buf buffer, uint16_t protocol_version)
{
	if (protocol_version >= SLURM_14_03_PROTOCOL_VERSION) {
		if (detail_ptr) {
			pack16(detail_ptr->contiguous, buffer);
			pack16(detail_ptr->core_spec, buffer);
			pack16(detail_ptr->cpus_per_task, buffer);
			pack16(detail_ptr->pn_min_cpus, buffer);

			pack32(detail_ptr->pn_min_memory, buffer);
			pack32(detail_ptr->pn_min_tmp_disk, buffer);

			packstr(detail_ptr->req_nodes, buffer);
			pack_bit_fmt(detail_ptr->req_node_bitmap, buffer);
			/* detail_ptr->req_node_layout is not packed */
			packstr(detail_ptr->exc_nodes, buffer);
			pack_bit_fmt(detail_ptr->exc_node_bitmap, buffer);

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

			pack32((uint32_t) 0, buffer);
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
	} else if (protocol_version >= SLURM_2_6_PROTOCOL_VERSION) {
		if (detail_ptr) {
			pack16(detail_ptr->contiguous, buffer);
			pack16(detail_ptr->cpus_per_task, buffer);
			pack16(detail_ptr->pn_min_cpus, buffer);

			pack32(detail_ptr->pn_min_memory, buffer);
			pack32(detail_ptr->pn_min_tmp_disk, buffer);

			packstr(detail_ptr->req_nodes, buffer);
			pack_bit_fmt(detail_ptr->req_node_bitmap, buffer);
			/* detail_ptr->req_node_layout is not packed */
			packstr(detail_ptr->exc_nodes, buffer);
			pack_bit_fmt(detail_ptr->exc_node_bitmap, buffer);

			pack_multi_core_data(detail_ptr->mc_ptr, buffer,
					     protocol_version);
		} else {
			pack16((uint16_t) 0, buffer);
			pack16((uint16_t) 0, buffer);
			pack16((uint16_t) 0, buffer);

			pack32((uint32_t) 0, buffer);
			pack32((uint32_t) 0, buffer);

			packnull(buffer);
			packnull(buffer);
			packnull(buffer);
			packnull(buffer);

			pack_multi_core_data(NULL, buffer, protocol_version);
		}
	} else {
		error("_pack_pending_job_details: protocol_version "
		      "%hu not supported", protocol_version);
	}
}

/*
 * purge_old_job - purge old job records.
 *	The jobs must have completed at least MIN_JOB_AGE minutes ago.
 *	Test job dependencies, handle after_ok, after_not_ok before
 *	purging any jobs.
 * NOTE: READ lock slurmctld config and WRITE lock jobs before entry
 */
void purge_old_job(void)
{
	ListIterator job_iterator;
	struct job_record  *job_ptr;
	int i;

	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		if (!IS_JOB_PENDING(job_ptr))
			continue;
		if (test_job_dependency(job_ptr) == 2) {
			char jbuf[JBUFSIZ];

			if (kill_invalid_dep) {
				_kill_dependent(job_ptr);
			} else {
				debug("%s: %s dependency condition never satisfied",
				      __func__, jobid2str(job_ptr, jbuf));
				job_ptr->state_reason = WAIT_DEP_INVALID;
				xfree(job_ptr->state_desc);
			}
		}
		if (job_ptr->state_reason == WAIT_DEP_INVALID
		    && kill_invalid_dep) {
			/* The job got the WAIT_DEP_INVALID
			 * before slurmctld was reconfigured.
			 */
			_kill_dependent(job_ptr);
		}
	}
	list_iterator_destroy(job_iterator);

	i = list_delete_all(job_list, &_list_find_job_old, "");
	if (i) {
		debug2("purge_old_job: purged %d old job records", i);
/*		last_job_update = now;		don't worry about state save */
	}
}


/*
 * _purge_job_record - purge specific job record. No testing is performed to
 *	insure the job records has no active references. Use only for job
 *	records that were never fully operational (e.g. WILL_RUN test, failed
 *	job load, failed job create, etc.).
 * IN job_id - job_id of job record to be purged
 * RET int - count of job's purged
 * global: job_list - global job table
 */
static int _purge_job_record(uint32_t job_id)
{
	return list_delete_all(job_list, &_list_find_job_id, (void *) &job_id);
}


/*
 * reset_job_bitmaps - reestablish bitmaps for existing jobs.
 *	this should be called after rebuilding node information,
 *	but before using any job entries.
 * global: last_job_update - time of last job table update
 *	job_list - pointer to global job list
 */
void reset_job_bitmaps(void)
{
	ListIterator job_iterator;
	struct job_record  *job_ptr;
	struct part_record *part_ptr;
	List part_ptr_list = NULL;
	bool job_fail = false;
	time_t now = time(NULL);
	bool gang_flag = false;
	static uint32_t cr_flag = NO_VAL;

	xassert(job_list);

	if (cr_flag == NO_VAL) {
		cr_flag = 0;  /* call is no-op for select/linear and bluegene */
		if (select_g_get_info_from_plugin(SELECT_CR_PLUGIN,
						  NULL, &cr_flag)) {
			cr_flag = NO_VAL;	/* error */
		}

	}
	if (slurm_get_preempt_mode() == PREEMPT_MODE_GANG)
		gang_flag = true;

	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		xassert (job_ptr->magic == JOB_MAGIC);
		job_fail = false;

		if (job_ptr->partition == NULL) {
			error("No partition for job_id %u", job_ptr->job_id);
			part_ptr = NULL;
			job_fail = true;
		} else {
			part_ptr = find_part_record(job_ptr->partition);
			if (part_ptr == NULL) {
				part_ptr_list = get_part_list(job_ptr->
							      partition);
				if (part_ptr_list)
					part_ptr = list_peek(part_ptr_list);
			}
			if (part_ptr == NULL) {
				error("Invalid partition (%s) for job %u",
				      job_ptr->partition, job_ptr->job_id);
				job_fail = true;
			}
		}
		job_ptr->part_ptr = part_ptr;
		FREE_NULL_LIST(job_ptr->part_ptr_list);
		if (part_ptr_list) {
			job_ptr->part_ptr_list = part_ptr_list;
			part_ptr_list = NULL;	/* clear for next job */
		}

		FREE_NULL_BITMAP(job_ptr->node_bitmap_cg);
		if (job_ptr->nodes_completing &&
		    node_name2bitmap(job_ptr->nodes_completing,
				     false,  &job_ptr->node_bitmap_cg)) {
			error("Invalid nodes (%s) for job_id %u",
			      job_ptr->nodes_completing,
			      job_ptr->job_id);
			job_fail = true;
		}
		FREE_NULL_BITMAP(job_ptr->node_bitmap);
		if (job_ptr->nodes &&
		    node_name2bitmap(job_ptr->nodes, false,
				     &job_ptr->node_bitmap) && !job_fail) {
			error("Invalid nodes (%s) for job_id %u",
			      job_ptr->nodes, job_ptr->job_id);
			job_fail = true;
		}
		if (reset_node_bitmap(job_ptr->job_resrcs, job_ptr->job_id))
			job_fail = true;
		if (!job_fail && !IS_JOB_FINISHED(job_ptr) &&
		    job_ptr->job_resrcs && (cr_flag || gang_flag) &&
		    valid_job_resources(job_ptr->job_resrcs,
					node_record_table_ptr,
					slurmctld_conf.fast_schedule)) {
			error("Aborting JobID %u due to change in socket/core "
			      "configuration of allocated nodes",
			      job_ptr->job_id);
			job_fail = true;
		}
		_reset_step_bitmaps(job_ptr);

		/* Do not increase the job->node_cnt for
		 * completed jobs.
		 */
		if (! IS_JOB_COMPLETED(job_ptr))
			build_node_details(job_ptr, false); /* set node_addr */

		if (_reset_detail_bitmaps(job_ptr))
			job_fail = true;

		if (job_fail) {
			if (IS_JOB_PENDING(job_ptr)) {
				job_ptr->start_time =
					job_ptr->end_time = time(NULL);
				job_ptr->job_state = JOB_NODE_FAIL;
			} else if (IS_JOB_RUNNING(job_ptr)) {
				job_ptr->end_time = time(NULL);
				job_ptr->job_state = JOB_NODE_FAIL |
						     JOB_COMPLETING;
				build_cg_bitmap(job_ptr);
			} else if (IS_JOB_SUSPENDED(job_ptr)) {
				job_ptr->end_time = job_ptr->suspend_time;
				job_ptr->job_state = JOB_NODE_FAIL |
						     JOB_COMPLETING;
				build_cg_bitmap(job_ptr);
				job_ptr->tot_sus_time +=
					difftime(now, job_ptr->suspend_time);
				jobacct_storage_g_job_suspend(acct_db_conn,
							      job_ptr);
			}
			job_ptr->exit_code = MAX(job_ptr->exit_code, 1);
			job_ptr->state_reason = FAIL_DOWN_NODE;
			xfree(job_ptr->state_desc);
			job_completion_logger(job_ptr, false);
			if (job_ptr->job_state == JOB_NODE_FAIL) {
				/* build_cg_bitmap() may clear JOB_COMPLETING */
				epilog_slurmctld(job_ptr);
			}
		}
	}

	list_iterator_reset(job_iterator);
	/* This will reinitialize the select plugin database, which
	 * we can only do after ALL job's states and bitmaps are set
	 * (i.e. it needs to be in this second loop) */
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		if (select_g_select_nodeinfo_set(job_ptr) != SLURM_SUCCESS) {
			error("select_g_select_nodeinfo_set(%u): %m",
			      job_ptr->job_id);
		}
	}
	list_iterator_destroy(job_iterator);

	last_job_update = now;
}

static int _reset_detail_bitmaps(struct job_record *job_ptr)
{
	if (job_ptr->details == NULL)
		return SLURM_SUCCESS;

	FREE_NULL_BITMAP(job_ptr->details->req_node_bitmap);
	xfree(job_ptr->details->req_node_layout); /* layout info is lost
						   * but should be re-generated
						   * at job start time */
	if ((job_ptr->details->req_nodes) &&
	    (node_name2bitmap(job_ptr->details->req_nodes, false,
			      &job_ptr->details->req_node_bitmap))) {
		error("Invalid req_nodes (%s) for job_id %u",
		      job_ptr->details->req_nodes, job_ptr->job_id);
		return SLURM_ERROR;
	}

	FREE_NULL_BITMAP(job_ptr->details->exc_node_bitmap);
	if ((job_ptr->details->exc_nodes) &&
	    (node_name2bitmap(job_ptr->details->exc_nodes, true,
			      &job_ptr->details->exc_node_bitmap))) {
		error("Invalid exc_nodes (%s) for job_id %u",
		      job_ptr->details->exc_nodes, job_ptr->job_id);
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

static void _reset_step_bitmaps(struct job_record *job_ptr)
{
	ListIterator step_iterator;
	struct step_record *step_ptr;

	step_iterator = list_iterator_create (job_ptr->step_list);
	while ((step_ptr = (struct step_record *) list_next (step_iterator))) {
		if (step_ptr->state < JOB_RUNNING)
			continue;
		FREE_NULL_BITMAP(step_ptr->step_node_bitmap);
		if (step_ptr->step_layout &&
		    step_ptr->step_layout->node_list &&
		    (node_name2bitmap(step_ptr->step_layout->node_list, false,
				      &step_ptr->step_node_bitmap))) {
			error("Invalid step_node_list (%s) for step_id %u.%u",
			      step_ptr->step_layout->node_list,
			      job_ptr->job_id, step_ptr->step_id);
			delete_step_record (job_ptr, step_ptr->step_id);
		}
		if ((step_ptr->step_node_bitmap == NULL) &&
		    (step_ptr->batch_step == 0)) {
			error("Missing node_list for step_id %u.%u",
			      job_ptr->job_id, step_ptr->step_id);
			delete_step_record (job_ptr, step_ptr->step_id);
		}
	}

	list_iterator_destroy (step_iterator);
	return;
}

/* update first assigned job id as needed on reconfigure
 * NOTE: READ lock_slurmctld config before entry */
void reset_first_job_id(void)
{
	job_id_sequence = MAX(job_id_sequence, slurmctld_conf.first_job_id);
}

/*
 * get_next_job_id - return the job_id to be used by default for
 *	the next job
 */
extern uint32_t get_next_job_id(void)
{
	uint32_t next_id;

	job_id_sequence = MAX(job_id_sequence, slurmctld_conf.first_job_id);
	next_id = job_id_sequence + 1;
	if (next_id >= slurmctld_conf.max_job_id)
		next_id = slurmctld_conf.first_job_id;
	return next_id;
}

/*
 * _set_job_id - set a default job_id, insure that it is unique
 * IN job_ptr - pointer to the job_record
 */
static int _set_job_id(struct job_record *job_ptr)
{
	int i;
	uint32_t new_id, max_jobs;

	xassert(job_ptr);
	xassert (job_ptr->magic == JOB_MAGIC);

	job_id_sequence = MAX(job_id_sequence, slurmctld_conf.first_job_id);
	max_jobs = slurmctld_conf.max_job_id - slurmctld_conf.first_job_id;

	/* Insure no conflict in job id if we roll over 32 bits */
	for (i = 0; i < max_jobs; i++) {
		if (++job_id_sequence >= slurmctld_conf.max_job_id)
			job_id_sequence = slurmctld_conf.first_job_id;
		new_id = job_id_sequence;
		if (find_job_record(new_id))
			continue;
		if (_dup_job_file_test(new_id))
			continue;

		job_ptr->job_id = new_id;
		/* When we get a new job id might as well make sure
		 * the db_index is 0 since there is no way it will be
		 * correct otherwise :).
		 */
		job_ptr->db_index = 0;
		return SLURM_SUCCESS;
	}
	error("We have exhausted our supply of valid job id values. "
	      "FirstJobId=%u MaxJobId=%u", slurmctld_conf.first_job_id,
	      slurmctld_conf.max_job_id);
	job_ptr->job_id = NO_VAL;
	return EAGAIN;
}


/*
 * set_job_prio - set a default job priority
 * IN job_ptr - pointer to the job_record
 */
extern void set_job_prio(struct job_record *job_ptr)
{
	uint32_t relative_prio;

	xassert(job_ptr);
	xassert (job_ptr->magic == JOB_MAGIC);

	if (IS_JOB_FINISHED(job_ptr))
		return;
	job_ptr->priority = slurm_sched_g_initial_priority(lowest_prio,
							   job_ptr);
	if ((job_ptr->priority == 0) || (job_ptr->direct_set_prio))
		return;

	relative_prio = job_ptr->priority;
	if (job_ptr->details && (job_ptr->details->nice != NICE_OFFSET)) {
		int offset = job_ptr->details->nice;
		offset -= NICE_OFFSET;
		relative_prio += offset;
	}
	lowest_prio = MIN(relative_prio, lowest_prio);
}

/* After recovering job state, if using priority/basic then we increment the
 * priorities of all jobs to avoid decrementing the base down to zero */
extern void sync_job_priorities(void)
{
	ListIterator job_iterator;
	struct job_record *job_ptr;
	uint32_t prio_boost = 0;

	if ((highest_prio != 0) && (highest_prio < TOP_PRIORITY))
		prio_boost = TOP_PRIORITY - highest_prio;
	if (strcmp(slurmctld_conf.priority_type, "priority/basic") ||
	    (prio_boost < 1000000))
		return;

	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		if ((job_ptr->priority) && (job_ptr->direct_set_prio == 0))
			job_ptr->priority += prio_boost;
	}
	list_iterator_destroy(job_iterator);
	lowest_prio += prio_boost;
}

/*
 * _top_priority - determine if any other job has a higher priority than the
 *	specified job
 * IN job_ptr - pointer to selected job
 * RET true if selected job has highest priority
 */
static bool _top_priority(struct job_record *job_ptr)
{
	struct job_details *detail_ptr = job_ptr->details;
	bool top;

#ifdef HAVE_BG
	static uint16_t static_part = (uint16_t)NO_VAL;
	int rc = SLURM_SUCCESS;

	/* On BlueGene with static partitioning, we don't want to delay
	 * jobs based upon priority since jobs of different sizes can
	 * execute on different sets of nodes. While sched/backfill would
	 * eventually start the job if delayed here based upon priority,
	 * that could delay the initiation of a job by a few seconds. */
	if (static_part == (uint16_t)NO_VAL) {
		/* Since this never changes we can just set it once
		   and not look at it again. */
		rc = select_g_get_info_from_plugin(SELECT_STATIC_PART, job_ptr,
						   &static_part);
	}
	if ((rc == SLURM_SUCCESS) && (static_part == 1))
		return true;
#endif

	if (job_ptr->priority == 0)	/* user held */
		top = false;
	else {
		ListIterator job_iterator;
		struct job_record *job_ptr2;

		top = true;	/* assume top priority until found otherwise */
		job_iterator = list_iterator_create(job_list);
		while ((job_ptr2 = (struct job_record *)
					list_next(job_iterator))) {
			if (job_ptr2 == job_ptr)
				continue;
			if (!IS_JOB_PENDING(job_ptr2))
				continue;
			if (IS_JOB_COMPLETING(job_ptr2)) {
				/* Job is hung in pending & completing state,
				 * indicative of job requeue */
				continue;
			}
			if (!acct_policy_job_runnable_state(job_ptr2) ||
			    !misc_policy_job_runnable_state(job_ptr2) ||
			    !part_policy_job_runnable_state(job_ptr2) ||
			    !job_independent(job_ptr2, 0))
				continue;
			if ((job_ptr2->resv_name && (!job_ptr->resv_name)) ||
			    ((!job_ptr2->resv_name) && job_ptr->resv_name))
				continue;	/* different reservation */
			if (job_ptr2->resv_name && job_ptr->resv_name &&
			    (!strcmp(job_ptr2->resv_name,
				     job_ptr->resv_name))) {
				/* same reservation */
				if (job_ptr2->priority <= job_ptr->priority)
					continue;
				top = false;
				break;
			}
			if (job_ptr2->part_ptr == job_ptr->part_ptr) {
				/* same partition */
				if (job_ptr2->priority <= job_ptr->priority)
					continue;
				top = false;
				break;
			}
			if (bit_overlap(job_ptr->part_ptr->node_bitmap,
					job_ptr2->part_ptr->node_bitmap) == 0)
				continue;   /* no node overlap in partitions */
			if ((job_ptr2->part_ptr->priority >
			     job_ptr ->part_ptr->priority) ||
			    ((job_ptr2->part_ptr->priority ==
			      job_ptr ->part_ptr->priority) &&
			     (job_ptr2->priority >  job_ptr->priority))) {
				top = false;
				break;
			}
		}
		list_iterator_destroy(job_iterator);
	}

	if ((!top) && detail_ptr) {	/* not top prio */
		if (job_ptr->priority == 0) {		/* user/admin hold */
			if (job_ptr->state_reason != FAIL_BAD_CONSTRAINTS
			    && (job_ptr->state_reason != WAIT_HELD)
			    && (job_ptr->state_reason != WAIT_HELD_USER)
			    && job_ptr->state_reason != WAIT_MAX_REQUEUE) {
				job_ptr->state_reason = WAIT_HELD;
				xfree(job_ptr->state_desc);
			}
		} else if (job_ptr->state_reason == WAIT_NO_REASON) {
			job_ptr->state_reason = WAIT_PRIORITY;
			xfree(job_ptr->state_desc);
		}
	}
	return top;
}

static void _merge_job_licenses(struct job_record *shrink_job_ptr,
				struct job_record *expand_job_ptr)
{
	xassert(shrink_job_ptr);
	xassert(expand_job_ptr);

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
	return;
}

static int _update_job(struct job_record *job_ptr, job_desc_msg_t * job_specs,
		       uid_t uid)
{
	int error_code = SLURM_SUCCESS;
	enum job_state_reason fail_reason;
	bool authorized = false, admin = false;
	uint32_t save_min_nodes = 0, save_max_nodes = 0;
	uint32_t save_min_cpus = 0, save_max_cpus = 0;
	struct job_details *detail_ptr;
	struct part_record *tmp_part_ptr;
	bitstr_t *exc_bitmap = NULL, *req_bitmap = NULL;
	time_t now = time(NULL);
	multi_core_data_t *mc_ptr = NULL;
	bool update_accounting = false;
	acct_policy_limit_set_t acct_policy_limit_set;

#ifdef HAVE_BG
	uint16_t conn_type[SYSTEM_DIMENSIONS] = {(uint16_t) NO_VAL};
	uint16_t reboot = (uint16_t) NO_VAL;
	uint16_t rotate = (uint16_t) NO_VAL;
	uint16_t geometry[SYSTEM_DIMENSIONS] = {(uint16_t) NO_VAL};
	char *image = NULL;
	static uint32_t cpus_per_mp = 0;
	static uint16_t cpus_per_node = 0;

	if (!cpus_per_mp)
		select_g_alter_node_cnt(SELECT_GET_MP_CPU_CNT, &cpus_per_mp);
	if (!cpus_per_node)
		select_g_alter_node_cnt(SELECT_GET_NODE_CPU_CNT,
					&cpus_per_node);
#endif
	memset(&acct_policy_limit_set, 0, sizeof(acct_policy_limit_set_t));

	error_code = job_submit_plugin_modify(job_specs, job_ptr,
					      (uint32_t) uid);
	if (error_code != SLURM_SUCCESS)
		return error_code;

	admin = validate_operator(uid);
	authorized = admin || assoc_mgr_is_user_acct_coord(
		acct_db_conn, uid, job_ptr->account);
	if ((job_ptr->user_id != uid) && !authorized) {
		error("Security violation, JOB_UPDATE RPC from uid %d",
		      uid);
		return ESLURM_USER_ID_MISSING;
	}

	if (!wiki_sched_test) {
		char *sched_type = slurm_get_sched_type();
		if (strcmp(sched_type, "sched/wiki") == 0)
			wiki_sched  = true;
		if (strcmp(sched_type, "sched/wiki2") == 0) {
			wiki_sched  = true;
			wiki2_sched = true;
		}
		xfree(sched_type);
		wiki_sched_test = true;
	}
	detail_ptr = job_ptr->details;
	if (detail_ptr)
		mc_ptr = detail_ptr->mc_ptr;
	last_job_update = now;

	if (job_specs->account
	    && !xstrcmp(job_specs->account, job_ptr->account)) {
		debug("sched: update_job: new account identical to "
		      "old account %u", job_ptr->job_id);
		xfree(job_specs->account);
	}

	if (job_specs->account) {
		if (!IS_JOB_PENDING(job_ptr))
			error_code = ESLURM_JOB_NOT_PENDING;
		else {
			int rc = update_job_account("update_job", job_ptr,
						    job_specs->account);
			if (rc != SLURM_SUCCESS)
				error_code = rc;
			else
				update_accounting = true;
		}
	}
	if (error_code != SLURM_SUCCESS)
		goto fini;

	if (job_specs->exc_nodes) {
		if ((!IS_JOB_PENDING(job_ptr)) || (detail_ptr == NULL))
			error_code = ESLURM_JOB_NOT_PENDING;
		else if (job_specs->exc_nodes[0] == '\0') {
			xfree(detail_ptr->exc_nodes);
			FREE_NULL_BITMAP(detail_ptr->exc_node_bitmap);
		} else {
			if (node_name2bitmap(job_specs->exc_nodes, false,
					     &exc_bitmap)) {
				error("sched: update_job: Invalid node list "
				      "for update of job %u: %s",
				      job_ptr->job_id, job_specs->exc_nodes);
				FREE_NULL_BITMAP(exc_bitmap);
				error_code = ESLURM_INVALID_NODE_NAME;
			}
			if (exc_bitmap) {
				xfree(detail_ptr->exc_nodes);
				detail_ptr->exc_nodes =
					job_specs->exc_nodes;
				FREE_NULL_BITMAP(detail_ptr->exc_node_bitmap);
				detail_ptr->exc_node_bitmap = exc_bitmap;
				info("sched: update_job: setting exc_nodes to "
				     "%s for job_id %u", job_specs->exc_nodes,
				     job_ptr->job_id);
				job_specs->exc_nodes = NULL;
			}
		}
	}
	if (error_code != SLURM_SUCCESS)
		goto fini;

#ifndef HAVE_BG
	if (job_specs->req_nodes &&
	    (IS_JOB_RUNNING(job_ptr) || IS_JOB_SUSPENDED(job_ptr))) {
		/* Use req_nodes to change the nodes associated with a running
		 * for lack of other field in the job request to use */
		if ((job_specs->req_nodes[0] == '\0') ||
		    node_name2bitmap(job_specs->req_nodes,false, &req_bitmap) ||
		    !bit_super_set(req_bitmap, job_ptr->node_bitmap) ||
		    job_ptr->details->expanding_jobid) {
			info("sched: Invalid node list (%s) for job %u update",
			     job_specs->req_nodes, job_ptr->job_id);
			error_code = ESLURM_INVALID_NODE_NAME;
			goto fini;
		} else if (req_bitmap) {
			int i, i_first, i_last;
			struct node_record *node_ptr;
			info("sched: update_job: setting nodes to %s for "
			     "job_id %u",
			     job_specs->req_nodes, job_ptr->job_id);
			job_pre_resize_acctg(job_ptr);
			i_first = bit_ffs(job_ptr->node_bitmap);
			i_last  = bit_fls(job_ptr->node_bitmap);
			for (i=i_first; i<=i_last; i++) {
				if (bit_test(req_bitmap, i) ||
				    !bit_test(job_ptr->node_bitmap, i))
					continue;
				node_ptr = node_record_table_ptr + i;
				kill_step_on_node(job_ptr, node_ptr, false);
				excise_node_from_job(job_ptr, node_ptr);
			}
			job_post_resize_acctg(job_ptr);
			/* Since job_post_resize_acctg will restart
			 * things, don't do it again. */
			update_accounting = false;
		} else {
			update_accounting = true;
		}
		FREE_NULL_BITMAP(req_bitmap);
		xfree(job_specs->req_nodes);
	}
#endif

	if (job_specs->req_nodes) {
		if ((!IS_JOB_PENDING(job_ptr)) || (detail_ptr == NULL))
			error_code = ESLURM_JOB_NOT_PENDING;
		else if (job_specs->req_nodes[0] == '\0') {
			xfree(detail_ptr->req_nodes);
			FREE_NULL_BITMAP(detail_ptr->req_node_bitmap);
			xfree(detail_ptr->req_node_layout);
		} else {
			if (node_name2bitmap(job_specs->req_nodes, false,
					     &req_bitmap)) {
				info("sched: Invalid node list for "
				     "job_update: %s", job_specs->req_nodes);
				FREE_NULL_BITMAP(req_bitmap);
				error_code = ESLURM_INVALID_NODE_NAME;
			}
			if (req_bitmap) {
				xfree(detail_ptr->req_nodes);
				detail_ptr->req_nodes =
					job_specs->req_nodes;
				FREE_NULL_BITMAP(detail_ptr->req_node_bitmap);
				xfree(detail_ptr->req_node_layout);
				detail_ptr->req_node_bitmap = req_bitmap;
				info("sched: update_job: setting req_nodes to "
				     "%s for job_id %u", job_specs->req_nodes,
				     job_ptr->job_id);
				job_specs->req_nodes = NULL;
			}
		}
	}
	if (error_code != SLURM_SUCCESS)
		goto fini;

	if (job_specs->min_nodes == INFINITE) {
		/* Used by scontrol just to get current configuration info */
		job_specs->min_nodes = NO_VAL;
	}
#if defined(HAVE_BG) || defined(HAVE_ALPS_CRAY)
	if ((job_specs->min_nodes != NO_VAL) &&
	    (IS_JOB_RUNNING(job_ptr) || IS_JOB_SUSPENDED(job_ptr))) {
#else
	if ((job_specs->min_nodes != NO_VAL) &&
	    (job_specs->min_nodes > job_ptr->node_cnt) &&
	    !select_g_job_expand_allow() &&
	    (IS_JOB_RUNNING(job_ptr) || IS_JOB_SUSPENDED(job_ptr))) {
#endif
		info("Change of size for job %u not supported",
		     job_ptr->job_id);
		error_code = ESLURM_NOT_SUPPORTED;
		goto fini;
	}

	if (job_specs->req_switch != NO_VAL) {
		job_ptr->req_switch = job_specs->req_switch;
		info("Change of switches to %u job %u",
		     job_specs->req_switch, job_ptr->job_id);
	}
	if (job_specs->wait4switch != NO_VAL) {
		job_ptr->wait4switch = _max_switch_wait(job_specs->wait4switch);
		info("Change of switch wait to %u secs job %u",
		     job_ptr->wait4switch, job_ptr->job_id);
	}

	if (job_specs->partition
	    && !xstrcmp(job_specs->partition, job_ptr->partition)) {
		debug("sched: update_job: new partition identical to "
		      "old partition %u", job_ptr->job_id);
		xfree(job_specs->partition);
	}

	if (job_specs->partition) {
		List part_ptr_list = NULL;
		bool old_res = false;

		if (!IS_JOB_PENDING(job_ptr)) {
			error_code = ESLURM_JOB_NOT_PENDING;
			goto fini;
		}

		if (job_specs->min_nodes == NO_VAL) {
#ifdef HAVE_BG
			select_g_select_jobinfo_get(
				job_ptr->select_jobinfo,
				SELECT_JOBDATA_NODE_CNT,
				&job_specs->min_nodes);
#else
			job_specs->min_nodes = detail_ptr->min_nodes;
#endif
		}
		if ((job_specs->max_nodes == NO_VAL) &&
		    (detail_ptr->max_nodes != 0)) {
#ifdef HAVE_BG
			select_g_select_jobinfo_get(
				job_ptr->select_jobinfo,
				SELECT_JOBDATA_NODE_CNT,
				&job_specs->max_nodes);
#else
			job_specs->max_nodes = detail_ptr->max_nodes;
#endif
		}

		if ((job_specs->time_min == NO_VAL) &&
		    (job_ptr->time_min != 0))
			job_specs->time_min = job_ptr->time_min;
		if (job_specs->time_limit == NO_VAL)
			job_specs->time_limit = job_ptr->time_limit;
		if (!job_specs->reservation
		    || job_specs->reservation[0] == '\0') {
			/* just incase the reservation is '\0' */
			xfree(job_specs->reservation);
			job_specs->reservation = job_ptr->resv_name;
			old_res = true;
		}

		error_code = _get_job_parts(job_specs,
					    &tmp_part_ptr, &part_ptr_list);

		if (error_code != SLURM_SUCCESS)
			;
		else if ((tmp_part_ptr->state_up & PARTITION_SUBMIT) == 0)
			error_code = ESLURM_PARTITION_NOT_AVAIL;
		else {
			slurmdb_association_rec_t assoc_rec;
			memset(&assoc_rec, 0,
			       sizeof(slurmdb_association_rec_t));
			assoc_rec.acct      = job_ptr->account;
			assoc_rec.partition = tmp_part_ptr->name;
			assoc_rec.uid       = job_ptr->user_id;
			if (assoc_mgr_fill_in_assoc(
				    acct_db_conn, &assoc_rec,
				    accounting_enforce,
				    (slurmdb_association_rec_t **)
				    &job_ptr->assoc_ptr, false)) {
				info("job_update: invalid account %s "
				     "for job %u",
				     job_specs->account, job_ptr->job_id);
				error_code = ESLURM_INVALID_ACCOUNT;
				/* Let update proceed. Note there is an invalid
				 * association ID for accounting purposes */
			} else
				job_ptr->assoc_id = assoc_rec.id;

			error_code = _valid_job_part(
				job_specs, uid,
				job_ptr->details->req_node_bitmap,
				&tmp_part_ptr, part_ptr_list,
				job_ptr->assoc_ptr, job_ptr->qos_ptr);

			xfree(job_ptr->partition);
			job_ptr->partition = xstrdup(job_specs->partition);
			job_ptr->part_ptr = tmp_part_ptr;
			xfree(job_ptr->priority_array);	/* Rebuilt in plugin */
			FREE_NULL_LIST(job_ptr->part_ptr_list);
			job_ptr->part_ptr_list = part_ptr_list;
			part_ptr_list = NULL;	/* nothing to free */
			info("update_job: setting partition to %s for "
			     "job_id %u", job_specs->partition,
			     job_ptr->job_id);
			update_accounting = true;
		}
		FREE_NULL_LIST(part_ptr_list);	/* error clean-up */

		if (old_res)
			job_specs->reservation = NULL;

		if (error_code != SLURM_SUCCESS)
			goto fini;
	}

	/* Always do this last just in case the assoc_ptr changed */
	if (job_specs->comment && wiki_sched && !validate_slurm_user(uid)) {
		/* User must use Moab command to change job comment */
		error("Attempt to change comment for job %u",
		      job_ptr->job_id);
		error_code = ESLURM_ACCESS_DENIED;
	} else if (job_specs->comment) {
		xfree(job_ptr->comment);
		job_ptr->comment = job_specs->comment;
		job_specs->comment = NULL;	/* Nothing left to free */
		info("update_job: setting comment to %s for job_id %u",
		     job_ptr->comment, job_ptr->job_id);

		if (wiki_sched && strstr(job_ptr->comment, "QOS:")) {
			if (!IS_JOB_PENDING(job_ptr))
				error_code = ESLURM_JOB_NOT_PENDING;
			else {
				slurmdb_qos_rec_t qos_rec;
				slurmdb_qos_rec_t *new_qos_ptr;
				char *resv_name;
				if (job_specs->reservation
				    && job_specs->reservation[0] != '\0')
					resv_name = job_specs->reservation;
				else
					resv_name = job_ptr->resv_name;

				memset(&qos_rec, 0, sizeof(slurmdb_qos_rec_t));
				if (strstr(job_ptr->comment,
					   "FLAGS:PREEMPTOR"))
					qos_rec.name = "expedite";
				else if (strstr(job_ptr->comment,
						"FLAGS:PREEMPTEE"))
					qos_rec.name = "standby";

				new_qos_ptr = _determine_and_validate_qos(
					resv_name, job_ptr->assoc_ptr,
					authorized, &qos_rec, &error_code);
				if (error_code == SLURM_SUCCESS) {
					info("update_job: setting qos to %s "
					     "for job_id %u",
					     job_specs->qos, job_ptr->job_id);
					if (job_ptr->qos_id != qos_rec.id) {
						job_ptr->qos_id = qos_rec.id;
						job_ptr->qos_ptr = new_qos_ptr;
						if (authorized)
							job_ptr->limit_set_qos =
								ADMIN_SET_LIMIT;
						else
							job_ptr->limit_set_qos
								= 0;
						update_accounting = true;
					} else
						debug("sched: update_job: "
						      "new qos identical to "
						      "old qos %u",
						      job_ptr->job_id);
				}
			}
		}
	}

	if (error_code != SLURM_SUCCESS)
		goto fini;

	if (job_specs->qos) {

		if (!authorized && !IS_JOB_PENDING(job_ptr))
			error_code = ESLURM_JOB_NOT_PENDING;
		else {
			slurmdb_qos_rec_t qos_rec;
			slurmdb_qos_rec_t *new_qos_ptr;
			char *resv_name;

			if (job_specs->reservation
			    && job_specs->reservation[0] != '\0')
				resv_name = job_specs->reservation;
			else
				resv_name = job_ptr->resv_name;

			memset(&qos_rec, 0, sizeof(slurmdb_qos_rec_t));
			qos_rec.name = job_specs->qos;

			new_qos_ptr = _determine_and_validate_qos(
				resv_name, job_ptr->assoc_ptr,
				authorized, &qos_rec, &error_code);
			if (error_code == SLURM_SUCCESS) {
				info("update_job: setting qos to %s "
				     "for job_id %u",
				     job_specs->qos, job_ptr->job_id);
				if (job_ptr->qos_id != qos_rec.id) {
					job_ptr->qos_id = qos_rec.id;
					job_ptr->qos_ptr = new_qos_ptr;
					if (authorized)
						job_ptr->limit_set_qos =
							ADMIN_SET_LIMIT;
					else
						job_ptr->limit_set_qos = 0;
					update_accounting = true;
				} else
					debug("sched: update_job: new qos "
					      "identical to old qos %u",
					      job_ptr->job_id);
			}
		}
	}
	if (error_code != SLURM_SUCCESS)
		goto fini;

	if (!authorized && (accounting_enforce & ACCOUNTING_ENFORCE_LIMITS)) {
		if (!acct_policy_validate(job_specs, job_ptr->part_ptr,
					  job_ptr->assoc_ptr, job_ptr->qos_ptr,
					  NULL, &acct_policy_limit_set, 1)) {
			info("update_job: exceeded association's cpu, node, "
			     "memory or time limit for user %u",
			     job_specs->user_id);
			error_code = ESLURM_ACCOUNTING_POLICY;
			goto fini;
		}

		/* Perhaps the limit was removed, so we will remove it
		   since it was imposed previously.
		*/
		if (!acct_policy_limit_set.max_cpus
		    && (job_ptr->limit_set_max_cpus == 1))
			job_ptr->details->max_cpus = NO_VAL;

		if (!acct_policy_limit_set.max_nodes
		    && (job_ptr->limit_set_max_nodes == 1))
			job_ptr->details->max_nodes = NO_VAL;

		if (!acct_policy_limit_set.time
		    && (job_ptr->limit_set_time == 1))
			job_ptr->time_limit = NO_VAL;

		if (job_ptr->limit_set_max_cpus != ADMIN_SET_LIMIT)
			job_ptr->limit_set_max_cpus =
				acct_policy_limit_set.max_cpus;
		if (job_ptr->limit_set_max_nodes != ADMIN_SET_LIMIT)
			job_ptr->limit_set_max_nodes =
				acct_policy_limit_set.max_nodes;
		if (job_ptr->limit_set_time != ADMIN_SET_LIMIT)
			job_ptr->limit_set_time = acct_policy_limit_set.time;
	} else if (authorized) {
		acct_policy_limit_set.max_cpus = ADMIN_SET_LIMIT;
		acct_policy_limit_set.max_nodes = ADMIN_SET_LIMIT;
		acct_policy_limit_set.min_cpus = ADMIN_SET_LIMIT;
		acct_policy_limit_set.min_nodes = ADMIN_SET_LIMIT;
		acct_policy_limit_set.pn_min_memory = ADMIN_SET_LIMIT;
		acct_policy_limit_set.time = ADMIN_SET_LIMIT;
		acct_policy_limit_set.qos = ADMIN_SET_LIMIT;
	}


	/* This needs to be done after the association acct policy check since
	 * it looks at unaltered nodes for bluegene systems
	 */
	debug3("update before alteration asking for nodes %u-%u cpus %u-%u",
	       job_specs->min_nodes, job_specs->max_nodes,
	       job_specs->min_cpus, job_specs->max_cpus);
	if (select_g_alter_node_cnt(SELECT_SET_NODE_CNT, job_specs)
	    != SLURM_SUCCESS) {
		error_code = ESLURM_INVALID_NODE_COUNT;
		goto fini;
	}
	debug3("update after alteration asking for nodes %u-%u cpus %u-%u",
	       job_specs->min_nodes, job_specs->max_nodes,
	       job_specs->min_cpus, job_specs->max_cpus);

	/* Reset min and max cpu counts as needed, insure consistency */
	if (job_specs->min_cpus != NO_VAL) {
		if ((!IS_JOB_PENDING(job_ptr)) || (detail_ptr == NULL))
			error_code = ESLURM_JOB_NOT_PENDING;
		else if (job_specs->min_cpus < 1)
			error_code = ESLURM_INVALID_CPU_COUNT;
		else {
			save_min_cpus = detail_ptr->min_cpus;
			detail_ptr->min_cpus = job_specs->min_cpus;
		}
	}
	if (job_specs->max_cpus != NO_VAL) {
		if ((!IS_JOB_PENDING(job_ptr)) || (detail_ptr == NULL))
			error_code = ESLURM_JOB_NOT_PENDING;
		else {
			save_max_cpus = detail_ptr->max_cpus;
			detail_ptr->max_cpus = job_specs->max_cpus;
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
#ifdef HAVE_BG
		uint32_t node_cnt = detail_ptr->min_cpus;
		if (cpus_per_node)
			node_cnt /= cpus_per_node;
		/* Ensure that accounting is set up correctly */
		select_g_select_jobinfo_set(job_ptr->select_jobinfo,
					    SELECT_JOBDATA_NODE_CNT,
					    &node_cnt);
		/* Reset geo since changing this makes any geo
		 * potentially invalid */
		select_g_select_jobinfo_set(job_ptr->select_jobinfo,
					    SELECT_JOBDATA_GEOMETRY,
					    geometry);
#endif
		info("update_job: setting min_cpus from "
		     "%u to %u for job_id %u",
		     save_min_cpus, detail_ptr->min_cpus, job_ptr->job_id);
		job_ptr->limit_set_min_cpus = acct_policy_limit_set.min_cpus;
		update_accounting = true;
	}
	if (save_max_cpus && (detail_ptr->max_cpus != save_max_cpus)) {
		info("update_job: setting max_cpus from "
		     "%u to %u for job_id %u",
		     save_max_cpus, detail_ptr->max_cpus, job_ptr->job_id);
		/* Always use the acct_policy_limit_set.* since if set by a
		 * super user it be set correctly */
		job_ptr->limit_set_max_cpus = acct_policy_limit_set.max_cpus;
		update_accounting = true;
	}

	if ((job_specs->pn_min_cpus != (uint16_t) NO_VAL) &&
	    (job_specs->pn_min_cpus != 0)) {
		if ((!IS_JOB_PENDING(job_ptr)) || (detail_ptr == NULL))
			error_code = ESLURM_JOB_NOT_PENDING;
		else if (authorized
			 || (detail_ptr->pn_min_cpus
			     > job_specs->pn_min_cpus)) {
			detail_ptr->pn_min_cpus = job_specs->pn_min_cpus;
			info("update_job: setting pn_min_cpus to %u for "
			     "job_id %u", job_specs->pn_min_cpus,
			     job_ptr->job_id);
		} else {
			error("Attempt to increase pn_min_cpus for job %u",
			      job_ptr->job_id);
			error_code = ESLURM_ACCESS_DENIED;
		}
	}
	if (error_code != SLURM_SUCCESS)
		goto fini;

	if (job_specs->num_tasks != NO_VAL) {
		if (!IS_JOB_PENDING(job_ptr))
			error_code = ESLURM_JOB_NOT_PENDING;
		else if (job_specs->num_tasks < 1)
			error_code = ESLURM_BAD_TASK_COUNT;
		else {
#ifdef HAVE_BG
			uint32_t node_cnt = job_specs->num_tasks;
			if (cpus_per_node)
				node_cnt /= cpus_per_node;
			/* This is only set up so accounting is set up
			   correctly */
			select_g_select_jobinfo_set(job_ptr->select_jobinfo,
						    SELECT_JOBDATA_NODE_CNT,
						    &node_cnt);
#endif
			detail_ptr->num_tasks = job_specs->num_tasks;
			info("update_job: setting num_tasks to %u for "
			     "job_id %u", job_specs->num_tasks,
			     job_ptr->job_id);
			if (detail_ptr->cpus_per_task) {
				uint32_t new_cpus = detail_ptr->num_tasks
					/ detail_ptr->cpus_per_task;
				if ((new_cpus < detail_ptr->min_cpus) ||
				    (!detail_ptr->overcommit &&
				     (new_cpus > detail_ptr->min_cpus))) {
					detail_ptr->min_cpus = new_cpus;
					detail_ptr->max_cpus = new_cpus;
					info("update_job: setting "
					     "min_cpus to %u for "
					     "job_id %u", detail_ptr->min_cpus,
					     job_ptr->job_id);
					/* Always use the
					 * acct_policy_limit_set.*
					 * since if set by a
					 * super user it be set correctly */
					job_ptr->limit_set_min_cpus =
						acct_policy_limit_set.min_cpus;
					job_ptr->limit_set_max_cpus =
						acct_policy_limit_set.max_cpus;
				}
			}
		}
	}
	if (error_code != SLURM_SUCCESS)
		goto fini;

	/* Reset min and max node counts as needed, insure consistency */
	if (job_specs->min_nodes != NO_VAL) {
		if (IS_JOB_RUNNING(job_ptr) || IS_JOB_SUSPENDED(job_ptr))
			;	/* shrink running job, processed later */
		else if ((!IS_JOB_PENDING(job_ptr)) || (detail_ptr == NULL))
			error_code = ESLURM_JOB_NOT_PENDING;
		else if (job_specs->min_nodes < 1) {
			info("update_job: min_nodes < 1 for job %u",
			     job_ptr->job_id);
			error_code = ESLURM_INVALID_NODE_COUNT;
		} else {
			/* Resize of pending job */
			save_min_nodes = detail_ptr->min_nodes;
			detail_ptr->min_nodes = job_specs->min_nodes;
		}
	}
	if (job_specs->max_nodes != NO_VAL) {
		if ((!IS_JOB_PENDING(job_ptr)) || (detail_ptr == NULL))
			error_code = ESLURM_JOB_NOT_PENDING;
		else {
			save_max_nodes = detail_ptr->max_nodes;
			detail_ptr->max_nodes = job_specs->max_nodes;
		}
	}
	if ((save_min_nodes || save_max_nodes) && detail_ptr->max_nodes &&
	    (detail_ptr->max_nodes < detail_ptr->min_nodes)) {
		info("update_job: max_nodes < min_nodes (%u < %u) for job %u",
		     detail_ptr->max_nodes, detail_ptr->min_nodes,
		     job_ptr->job_id);
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
		info("update_job: setting min_nodes from "
		     "%u to %u for job_id %u",
		     save_min_nodes, detail_ptr->min_nodes, job_ptr->job_id);
		job_ptr->limit_set_min_nodes = acct_policy_limit_set.min_nodes;
		update_accounting = true;
	}
	if (save_max_nodes && (save_max_nodes != detail_ptr->max_nodes)) {
		info("update_job: setting max_nodes from "
		     "%u to %u for job_id %u",
		     save_max_nodes, detail_ptr->max_nodes, job_ptr->job_id);
		/* Always use the acct_policy_limit_set.* since if set by a
		 * super user it be set correctly */
		job_ptr->limit_set_max_nodes = acct_policy_limit_set.max_nodes;
		update_accounting = true;
	}

	if (job_specs->time_limit != NO_VAL) {
		if (IS_JOB_FINISHED(job_ptr) || job_ptr->preempt_time)
			error_code = ESLURM_JOB_FINISHED;
		else if (job_ptr->time_limit == job_specs->time_limit) {
			debug("sched: update_job: new time limit identical to "
			      "old time limit %u", job_ptr->job_id);
		} else if (authorized ||
			   (job_ptr->time_limit > job_specs->time_limit)) {
			time_t old_time =  job_ptr->time_limit;
			if (old_time == INFINITE)	/* one year in mins */
				old_time = (365 * 24 * 60);
			acct_policy_alter_job(job_ptr, job_specs->time_limit);
			job_ptr->time_limit = job_specs->time_limit;
			if (IS_JOB_RUNNING(job_ptr) ||
			    IS_JOB_SUSPENDED(job_ptr)) {
				if (job_ptr->preempt_time) {
					;	/* Preemption in progress */
				} else if (job_ptr->time_limit == INFINITE) {
					/* Set end time in one year */
					job_ptr->end_time = now +
						(365 * 24 * 60 * 60);
				} else {
					/* Update end_time based upon change
					 * to preserve suspend time info */
					job_ptr->end_time = job_ptr->end_time +
						((job_ptr->time_limit -
						  old_time) * 60);
				}
				if (job_ptr->end_time < now)
					job_ptr->end_time = now;
				if (IS_JOB_RUNNING(job_ptr) &&
				    (list_is_empty(job_ptr->step_list) == 0)) {
					_xmit_new_end_time(job_ptr);
				}
			}
			info("sched: update_job: setting time_limit to %u for "
			     "job_id %u", job_specs->time_limit,
			     job_ptr->job_id);
			/* Always use the acct_policy_limit_set.*
			 * since if set by a super user it be set correctly */
			job_ptr->limit_set_time = acct_policy_limit_set.time;
			update_accounting = true;
		} else if (IS_JOB_PENDING(job_ptr) && job_ptr->part_ptr &&
			   (job_ptr->part_ptr->max_time >=
			    job_specs->time_limit)) {
			job_ptr->time_limit = job_specs->time_limit;
			info("sched: update_job: setting time_limit to %u for "
			     "job_id %u", job_specs->time_limit,
			     job_ptr->job_id);
			/* Always use the acct_policy_limit_set.*
			 * since if set by a super user it be set correctly */
			job_ptr->limit_set_time = acct_policy_limit_set.time;
			update_accounting = true;
		} else {
			info("sched: Attempt to increase time limit for job %u",
			     job_ptr->job_id);
			error_code = ESLURM_ACCESS_DENIED;
		}
	}
	if (error_code != SLURM_SUCCESS)
		goto fini;

	if ((job_specs->time_min != NO_VAL) && IS_JOB_PENDING(job_ptr)) {
		if (job_specs->time_min > job_ptr->time_limit) {
			info("update_job: attempt to set TimeMin > TimeLimit "
			     "(%u > %u)",
			     job_specs->time_min, job_ptr->time_limit);
			error_code = ESLURM_INVALID_TIME_LIMIT;
		} else if (job_ptr->time_min != job_specs->time_min) {
			job_ptr->time_min = job_specs->time_min;
			info("update_job: setting TimeMin to %u for job_id %u",
			     job_specs->time_min, job_ptr->job_id);
		}
	}
	if (error_code != SLURM_SUCCESS)
		goto fini;

	if (job_specs->end_time) {
		if (!IS_JOB_RUNNING(job_ptr) || job_ptr->preempt_time) {
			/* We may want to use this for deadline scheduling
			 * at some point in the future. For now only reset
			 * the time limit of running jobs. */
			error_code = ESLURM_JOB_NOT_RUNNING;
		} else if (job_specs->end_time < now) {
			error_code = ESLURM_INVALID_TIME_VALUE;
		} else if (authorized ||
			   (job_ptr->end_time > job_specs->end_time)) {
			int delta_t  = job_specs->end_time - job_ptr->end_time;
			job_ptr->end_time = job_specs->end_time;
			job_ptr->time_limit += (delta_t+30)/60; /* Sec->min */
			info("sched: update_job: setting time_limit to %u for "
			     "job_id %u", job_ptr->time_limit,
			     job_ptr->job_id);
			/* Always use the acct_policy_limit_set.*
			 * since if set by a super user it be set correctly */
			job_ptr->limit_set_time = acct_policy_limit_set.time;
			update_accounting = true;
		} else {
			info("sched: Attempt to extend end time for job %u",
			     job_ptr->job_id);
			error_code = ESLURM_ACCESS_DENIED;
		}
	}
	if (error_code != SLURM_SUCCESS)
		goto fini;

	if (job_specs->reservation
	    && !xstrcmp(job_specs->reservation, job_ptr->resv_name)) {
		debug("sched: update_job: new reservation identical to "
		      "old reservation %u", job_ptr->job_id);
		xfree(job_specs->reservation);
	}

	/* this needs to be after partition and qos checks */
	if (job_specs->reservation) {
		if (!IS_JOB_PENDING(job_ptr) && !IS_JOB_RUNNING(job_ptr)) {
			error_code = ESLURM_JOB_NOT_PENDING_NOR_RUNNING;
		} else {
			int rc;
			char *save_resv_name = job_ptr->resv_name;
			slurmctld_resv_t *save_resv_ptr = job_ptr->resv_ptr;

			job_ptr->resv_name = job_specs->reservation;
			job_specs->reservation = NULL;	/* Nothing to free */
			rc = validate_job_resv(job_ptr);
			/* Make sure this job isn't using a partition
			   or qos that requires it to be in a
			   reservation.
			*/
			if (rc == SLURM_SUCCESS && !job_ptr->resv_name) {
				struct part_record *part_ptr =
					job_ptr->part_ptr;
				slurmdb_qos_rec_t *qos_ptr =
					(slurmdb_qos_rec_t *)job_ptr->qos_ptr;

				if (part_ptr
				    && part_ptr->flags & PART_FLAG_REQ_RESV)
					rc = ESLURM_ACCESS_DENIED;

				if (qos_ptr
				    && qos_ptr->flags & QOS_FLAG_REQ_RESV)
					rc = ESLURM_INVALID_QOS;
			}

			if (rc == SLURM_SUCCESS) {
				info("sched: update_job: setting reservation "
				     "to %s for job_id %u", job_ptr->resv_name,
				     job_ptr->job_id);
				xfree(save_resv_name);
				update_accounting = true;
			} else {
				/* Restore reservation info */
				job_specs->reservation = job_ptr->resv_name;
				job_ptr->resv_name = save_resv_name;
				job_ptr->resv_ptr = save_resv_ptr;
				error_code = rc;
			}
		}
	}
	if (error_code != SLURM_SUCCESS)
		goto fini;

	if ((job_specs->requeue != (uint16_t) NO_VAL) && detail_ptr) {
		detail_ptr->requeue = MIN(job_specs->requeue, 1);
		info("sched: update_job: setting requeue to %u for job_id %u",
		     job_specs->requeue, job_ptr->job_id);
	}

	if (job_specs->priority != NO_VAL) {
		/* If we are doing time slicing we could update the
		   priority of the job while running to give better
		   position (larger time slices) than competing jobs
		*/
		if (IS_JOB_FINISHED(job_ptr) || (detail_ptr == NULL))
			error_code = ESLURM_JOB_FINISHED;
		else if (job_ptr->priority == job_specs->priority) {
			debug("update_job: setting priority to current value");
			if ((job_ptr->priority == 0) &&
			    (job_ptr->user_id != uid) && authorized) {
				/* Authorized user can change from user hold
				 * to admin hold or admin hold to user hold */
				if (job_specs->alloc_sid == ALLOC_SID_USER_HOLD)
					job_ptr->state_reason = WAIT_HELD_USER;
				else
					job_ptr->state_reason = WAIT_HELD;
			}
		} else if ((job_ptr->priority == 0) &&
			   (job_specs->priority == INFINITE) &&
			   (authorized ||
			    (job_ptr->state_reason == WAIT_HELD_USER))) {
			job_ptr->direct_set_prio = 0;
			set_job_prio(job_ptr);
			info("sched: update_job: releasing hold for job_id %u",
			     job_ptr->job_id);
			job_ptr->state_reason = WAIT_NO_REASON;
			job_ptr->job_state &= ~JOB_SPECIAL_EXIT;
			xfree(job_ptr->state_desc);
			job_ptr->exit_code = 0;
		} else if ((job_ptr->priority == 0) &&
			   (job_specs->priority != INFINITE)) {
			info("ignore priority reset request on held job %u",
			     job_ptr->job_id);
		} else if (authorized ||
			 (job_ptr->priority > job_specs->priority)) {
			if (job_specs->priority != 0)
				job_ptr->details->nice = NICE_OFFSET;
			if (job_specs->priority == INFINITE) {
				job_ptr->direct_set_prio = 0;
				set_job_prio(job_ptr);
			} else {
				job_ptr->direct_set_prio = 1;
				job_ptr->priority = job_specs->priority;
			}
			info("sched: update_job: setting priority to %u for "
			     "job_id %u", job_ptr->priority,
			     job_ptr->job_id);
			update_accounting = true;
			if (job_ptr->priority == 0) {
				if ((job_ptr->user_id == uid) ||
				    (job_specs->alloc_sid ==
				     ALLOC_SID_USER_HOLD)) {
					job_ptr->state_reason = WAIT_HELD_USER;
				} else
					job_ptr->state_reason = WAIT_HELD;
				xfree(job_ptr->state_desc);
			}
		} else if (job_specs->priority == INFINITE
			   && job_ptr->state_reason != WAIT_HELD_USER) {
			/* If the job was already released ignore another
			 * release request.
			 */
			debug("%s: job %d already release ignoring request",
			      __func__, job_ptr->job_id);
		} else {
			error("sched: Attempt to modify priority for job %u",
			      job_ptr->job_id);
			error_code = ESLURM_ACCESS_DENIED;
		}
	}
	if (error_code != SLURM_SUCCESS)
		goto fini;

	if (job_specs->nice != (uint16_t) NO_VAL) {
		if (IS_JOB_FINISHED(job_ptr) || (job_ptr->details == NULL))
			error_code = ESLURM_JOB_FINISHED;
		else if (job_ptr->details &&
			 (job_ptr->details->nice == job_specs->nice))
			debug("sched: update_job: new nice identical to "
			      "old nice %u", job_ptr->job_id);
		else if (authorized || (job_specs->nice >= NICE_OFFSET)) {
			int64_t new_prio = job_ptr->priority;
			new_prio += job_ptr->details->nice;
			new_prio -= job_specs->nice;
			job_ptr->priority = MAX(new_prio, 2);
			job_ptr->details->nice = job_specs->nice;
			info("sched: update_job: setting priority to %u for "
			     "job_id %u", job_ptr->priority,
			     job_ptr->job_id);
			update_accounting = true;
		} else {
			error("sched: Attempt to modify nice for "
			      "job %u", job_ptr->job_id);
			error_code = ESLURM_ACCESS_DENIED;
		}
	}
	if (error_code != SLURM_SUCCESS)
		goto fini;

	if (job_specs->pn_min_memory != NO_VAL) {
		if ((!IS_JOB_PENDING(job_ptr)) || (detail_ptr == NULL))
			error_code = ESLURM_JOB_NOT_PENDING;
		else if (job_specs->pn_min_memory
			 == detail_ptr->pn_min_memory)
			debug("sched: update_job: new memory limit identical "
			      "to old limit for job %u", job_ptr->job_id);
		else if (authorized) {
			char *entity;
			if (job_specs->pn_min_memory & MEM_PER_CPU)
				entity = "cpu";
			else
				entity = "job";

			detail_ptr->pn_min_memory = job_specs->pn_min_memory;
			info("sched: update_job: setting min_memory_%s to %u "
			     "for job_id %u", entity,
			     (job_specs->pn_min_memory & (~MEM_PER_CPU)),
			     job_ptr->job_id);
			/* Always use the acct_policy_limit_set.*
			 * since if set by a super user it be set correctly */
			job_ptr->limit_set_pn_min_memory =
				acct_policy_limit_set.pn_min_memory;
		} else {
			error("sched: Attempt to modify pn_min_memory for "
			      "job %u", job_ptr->job_id);
			error_code = ESLURM_ACCESS_DENIED;
		}
	}
	if (error_code != SLURM_SUCCESS)
		goto fini;

	if (job_specs->pn_min_tmp_disk != NO_VAL) {
		if ((!IS_JOB_PENDING(job_ptr)) || (detail_ptr == NULL))
			error_code = ESLURM_JOB_NOT_PENDING;
		else if (authorized
			 || (detail_ptr->pn_min_tmp_disk
			     > job_specs->pn_min_tmp_disk)) {
			detail_ptr->pn_min_tmp_disk =
				job_specs->pn_min_tmp_disk;
			info("sched: update_job: setting job_min_tmp_disk to "
			     "%u for job_id %u", job_specs->pn_min_tmp_disk,
			     job_ptr->job_id);
		} else {

			error("sched: Attempt to modify pn_min_tmp_disk "
			      "for job %u",
			      job_ptr->job_id);
			error_code = ESLURM_ACCESS_DENIED;
		}
	}
	if (error_code != SLURM_SUCCESS)
		goto fini;

	if (job_specs->sockets_per_node != (uint16_t) NO_VAL) {
		if ((!IS_JOB_PENDING(job_ptr)) || (mc_ptr == NULL)) {
			error_code = ESLURM_JOB_NOT_PENDING;
			goto fini;
		} else {
			mc_ptr->sockets_per_node = job_specs->sockets_per_node;
			info("sched: update_job: setting sockets_per_node to "
			     "%u for job_id %u", job_specs->sockets_per_node,
			     job_ptr->job_id);
		}
	}

	if (job_specs->cores_per_socket != (uint16_t) NO_VAL) {
		if ((!IS_JOB_PENDING(job_ptr)) || (mc_ptr == NULL)) {
			error_code = ESLURM_JOB_NOT_PENDING;
			goto fini;
		} else {
			mc_ptr->cores_per_socket = job_specs->cores_per_socket;
			info("sched: update_job: setting cores_per_socket to "
			     "%u for job_id %u", job_specs->cores_per_socket,
			     job_ptr->job_id);
		}
	}

	if ((job_specs->threads_per_core != (uint16_t) NO_VAL)) {
		if ((!IS_JOB_PENDING(job_ptr)) || (mc_ptr == NULL)) {
			error_code = ESLURM_JOB_NOT_PENDING;
			goto fini;
		} else {
			mc_ptr->threads_per_core = job_specs->threads_per_core;
			info("sched: update_job: setting threads_per_core to "
			     "%u for job_id %u", job_specs->threads_per_core,
			     job_ptr->job_id);
		}
	}

	if (job_specs->shared != (uint16_t) NO_VAL) {
		if ((!IS_JOB_PENDING(job_ptr)) || (detail_ptr == NULL)) {
			error_code = ESLURM_JOB_NOT_PENDING;
		} else if (!authorized) {
			error("sched: Attempt to change sharing for job %u",
			      job_ptr->job_id);
			error_code = ESLURM_ACCESS_DENIED;
		} else {
			if (job_specs->shared) {
				detail_ptr->share_res = 1;
				detail_ptr->whole_node = 0;
			} else {
				detail_ptr->share_res = 0;
			}
			info("sched: update_job: setting shared to %u for "
			     "job_id %u",
			     job_specs->shared, job_ptr->job_id);
		}
	}
	if (error_code != SLURM_SUCCESS)
		goto fini;

	if (job_specs->contiguous != (uint16_t) NO_VAL) {
		if ((!IS_JOB_PENDING(job_ptr)) || (detail_ptr == NULL))
			error_code = ESLURM_JOB_NOT_PENDING;
		else if (authorized
			 || (detail_ptr->contiguous > job_specs->contiguous)) {
			detail_ptr->contiguous = job_specs->contiguous;
			info("sched: update_job: setting contiguous to %u "
			     "for job_id %u", job_specs->contiguous,
			     job_ptr->job_id);
		} else {
			error("sched: Attempt to add contiguous for job %u",
			      job_ptr->job_id);
			error_code = ESLURM_ACCESS_DENIED;
		}
	}
	if (error_code != SLURM_SUCCESS)
		goto fini;

	if (job_specs->core_spec != (uint16_t) NO_VAL) {
		if ((!IS_JOB_PENDING(job_ptr)) || (detail_ptr == NULL))
			error_code = ESLURM_JOB_NOT_PENDING;
		else if (authorized) {
			if (job_specs->core_spec == (uint16_t) INFINITE)
				detail_ptr->core_spec = (uint16_t) NO_VAL;
			else
				detail_ptr->core_spec = job_specs->core_spec;
			info("sched: update_job: setting core_spec to %u "
			     "for job_id %u", detail_ptr->core_spec,
			     job_ptr->job_id);
		} else {
			error("sched: Attempt to modify core_spec for job %u",
			      job_ptr->job_id);
			error_code = ESLURM_ACCESS_DENIED;
		}
	}
	if (error_code != SLURM_SUCCESS)
		goto fini;

	if (job_specs->features) {
		if ((!IS_JOB_PENDING(job_ptr)) || (detail_ptr == NULL))
			error_code = ESLURM_JOB_NOT_PENDING;
		else if (job_specs->features[0] != '\0') {
			char *old_features = detail_ptr->features;
			List old_list = detail_ptr->feature_list;
			detail_ptr->features = job_specs->features;
			detail_ptr->feature_list = NULL;
			if (build_feature_list(job_ptr)) {
				info("sched: update_job: invalid features"
				     "(%s) for job_id %u",
				     job_specs->features, job_ptr->job_id);
				if (detail_ptr->feature_list)
					list_destroy(detail_ptr->feature_list);
				detail_ptr->features = old_features;
				detail_ptr->feature_list = old_list;
				error_code = ESLURM_INVALID_FEATURE;
			} else {
				info("sched: update_job: setting features to "
				     "%s for job_id %u",
				     job_specs->features, job_ptr->job_id);
				xfree(old_features);
				if (old_list)
					list_destroy(old_list);
				job_specs->features = NULL;
			}
		} else {
			info("sched: update_job: cleared features for job %u",
			     job_ptr->job_id);
			xfree(detail_ptr->features);
			if (detail_ptr->feature_list) {
				list_destroy(detail_ptr->feature_list);
				detail_ptr->feature_list = NULL;
			}
		}
	}
	if (error_code != SLURM_SUCCESS)
		goto fini;

	if (job_specs->gres) {
		List tmp_gres_list = NULL;
		if ((!IS_JOB_PENDING(job_ptr)) || (detail_ptr == NULL) ||
		    (detail_ptr->expanding_jobid != 0)) {
			error_code = ESLURM_JOB_NOT_PENDING;
		} else if (job_specs->gres[0] == '\0') {
			info("sched: update_job: cleared gres for job %u",
			     job_ptr->job_id);
			xfree(job_ptr->gres);
			FREE_NULL_LIST(job_ptr->gres_list);
		} else if (gres_plugin_job_state_validate(job_specs->gres,
							  &tmp_gres_list)) {
			info("sched: update_job: invalid gres %s for job %u",
			     job_specs->gres, job_ptr->job_id);
			error_code = ESLURM_INVALID_GRES;
			FREE_NULL_LIST(tmp_gres_list);
		} else {
			info("sched: update_job: setting gres to "
			     "%s for job_id %u",
			     job_specs->gres, job_ptr->job_id);
			xfree(job_ptr->gres);
			job_ptr->gres = job_specs->gres;
			job_specs->gres = NULL;
			FREE_NULL_LIST(job_ptr->gres_list);
			job_ptr->gres_list = tmp_gres_list;
		}
	}
	if (error_code != SLURM_SUCCESS)
		goto fini;

	if (job_specs->name
	    && !xstrcmp(job_specs->name, job_ptr->name)) {
		debug("sched: update_job: new name identical to "
		      "old name %u", job_ptr->job_id);
		xfree(job_specs->name);
	}

	if (job_specs->name) {
		if (IS_JOB_FINISHED(job_ptr)) {
			error_code = ESLURM_JOB_FINISHED;
			goto fini;
		} else {
			xfree(job_ptr->name);
			job_ptr->name = job_specs->name;
			job_specs->name = NULL;

			info("sched: update_job: setting name to %s for "
			     "job_id %u", job_ptr->name, job_ptr->job_id);
			update_accounting = true;
		}
	}

	if (job_specs->std_out) {
		if (!IS_JOB_PENDING(job_ptr))
			error_code = ESLURM_JOB_NOT_PENDING;
		else if (detail_ptr) {
			xfree(detail_ptr->std_out);
			detail_ptr->std_out = job_specs->std_out;
			job_specs->std_out = NULL;
		}
	}
	if (error_code != SLURM_SUCCESS)
		goto fini;

	if (job_specs->wckey
	    && !xstrcmp(job_specs->wckey, job_ptr->wckey)) {
		debug("sched: update_job: new wckey identical to "
		      "old wckey %u", job_ptr->job_id);
		xfree(job_specs->wckey);
	}

	if (job_specs->wckey) {
		if (!IS_JOB_PENDING(job_ptr))
			error_code = ESLURM_JOB_NOT_PENDING;
		else {
			int rc = update_job_wckey("update_job",
						  job_ptr,
						  job_specs->wckey);
			if (rc != SLURM_SUCCESS)
				error_code = rc;
			else
				update_accounting = true;
		}
	}
	if (error_code != SLURM_SUCCESS)
		goto fini;

	if ((job_specs->min_nodes != NO_VAL) &&
	    (IS_JOB_RUNNING(job_ptr) || IS_JOB_SUSPENDED(job_ptr))) {
		/* Use req_nodes to change the nodes associated with a running
		 * for lack of other field in the job request to use */
		if ((job_specs->min_nodes == 0) && (job_ptr->node_cnt > 0) &&
		    job_ptr->details && job_ptr->details->expanding_jobid) {
			struct job_record *expand_job_ptr;
			bitstr_t *orig_job_node_bitmap;

			expand_job_ptr = find_job_record(job_ptr->details->
							 expanding_jobid);
			if (expand_job_ptr == NULL) {
				info("Invalid node count (%u) for job %u "
				     "update, job %u to expand not found",
				     job_specs->min_nodes, job_ptr->job_id,
				     job_ptr->details->expanding_jobid);
				error_code = ESLURM_INVALID_JOB_ID;
				goto fini;
			}
			if (IS_JOB_SUSPENDED(job_ptr) ||
			    IS_JOB_SUSPENDED(expand_job_ptr)) {
				info("Can not expand job %u from job %u, "
				     "job is suspended",
				     expand_job_ptr->job_id, job_ptr->job_id);
				error_code = ESLURM_JOB_SUSPENDED;
				goto fini;
			}
			if ((job_ptr->step_list != NULL) &&
			    (list_count(job_ptr->step_list) != 0)) {
				info("Attempt to merge job %u with active "
				     "steps into job %u",
				     job_ptr->job_id,
				     job_ptr->details->expanding_jobid);
				error_code = ESLURMD_STEP_EXISTS;
				goto fini;
			}
			info("sched: killing job %u and moving all resources "
			     "to job %u", job_ptr->job_id,
			     expand_job_ptr->job_id);
			job_pre_resize_acctg(job_ptr);
			job_pre_resize_acctg(expand_job_ptr);
			_send_job_kill(job_ptr);

			xassert(job_ptr->job_resrcs);
			xassert(job_ptr->job_resrcs->node_bitmap);
			orig_job_node_bitmap = bit_copy(expand_job_ptr->
							job_resrcs->
							node_bitmap);
			error_code = select_g_job_expand(job_ptr,
							 expand_job_ptr);
			if (error_code == SLURM_SUCCESS) {
				_merge_job_licenses(job_ptr, expand_job_ptr);
				rebuild_step_bitmaps(expand_job_ptr,
						     orig_job_node_bitmap);
			}
			bit_free(orig_job_node_bitmap);
			job_post_resize_acctg(job_ptr);
			job_post_resize_acctg(expand_job_ptr);
			/* Since job_post_resize_acctg will restart things,
			 * don't do it again. */
			update_accounting = false;
			if (error_code)
				goto fini;
		} else if ((job_specs->min_nodes == 0) ||
			   (job_specs->min_nodes > job_ptr->node_cnt) ||
			   job_ptr->details->expanding_jobid) {
			info("sched: Invalid node count (%u) for job %u update",
			     job_specs->min_nodes, job_ptr->job_id);
			error_code = ESLURM_INVALID_NODE_COUNT;
			goto fini;
		} else if (job_specs->min_nodes == job_ptr->node_cnt) {
			debug2("No change in node count update for job %u",
			       job_ptr->job_id);
		} else {
			int i, i_first, i_last, total;
			struct node_record *node_ptr;
			info("sched: update_job: set node count to %u for "
			     "job_id %u",
			     job_specs->min_nodes, job_ptr->job_id);
			job_pre_resize_acctg(job_ptr);
			i_first = bit_ffs(job_ptr->node_bitmap);
			i_last  = bit_fls(job_ptr->node_bitmap);
			for (i=i_first, total=0; i<=i_last; i++) {
				if (!bit_test(job_ptr->node_bitmap, i))
					continue;
				if (++total <= job_specs->min_nodes)
					continue;
				node_ptr = node_record_table_ptr + i;
				kill_step_on_node(job_ptr, node_ptr, false);
				excise_node_from_job(job_ptr, node_ptr);
			}
			job_post_resize_acctg(job_ptr);
			info("sched: update_job: set nodes to %s for "
			     "job_id %u",
			     job_ptr->nodes, job_ptr->job_id);
			/* Since job_post_resize_acctg will restart
			 * things don't do it again. */
			update_accounting = false;
		}
	}

	if (job_specs->ntasks_per_node != (uint16_t) NO_VAL) {
		if ((!IS_JOB_PENDING(job_ptr)) || (detail_ptr == NULL))
			error_code = ESLURM_JOB_NOT_PENDING;
		else if (authorized) {
			detail_ptr->ntasks_per_node =
				job_specs->ntasks_per_node;
			info("sched: update_job: setting ntasks_per_node to %u"
			     " for job_id %u", job_specs->ntasks_per_node,
			     job_ptr->job_id);
		} else {
			error("sched: Not super user: ignore ntasks_oper_node "
			      "change for job %u", job_ptr->job_id);
			error_code = ESLURM_ACCESS_DENIED;
		}
	}
	if (error_code != SLURM_SUCCESS)
		goto fini;

	if (job_specs->dependency) {
		if ((!IS_JOB_PENDING(job_ptr)) || (job_ptr->details == NULL))
			error_code = ESLURM_JOB_NOT_PENDING;
		else {
			int rc;
			rc = update_job_dependency(job_ptr,
						   job_specs->dependency);
			if (rc != SLURM_SUCCESS)
				error_code = rc;
			else {
				job_ptr->details->orig_dependency =
					xstrdup(job_ptr->details->dependency);
				info("sched: update_job: setting dependency to "
				     "%s for job_id %u",
				     job_ptr->details->dependency,
				     job_ptr->job_id);
			}
		}
	}
	if (error_code != SLURM_SUCCESS)
		goto fini;

	if (job_specs->begin_time) {
		if (IS_JOB_PENDING(job_ptr) && detail_ptr) {
			char time_str[32];
			/* Make sure this time is current, it does no good for
			 * accounting to say this job could have started before
			 * now */
			if (job_specs->begin_time < now)
				job_specs->begin_time = now;

			if (detail_ptr->begin_time != job_specs->begin_time) {
				detail_ptr->begin_time = job_specs->begin_time;
				update_accounting = true;
				slurm_make_time_str(&detail_ptr->begin_time,
						    time_str, sizeof(time_str));
				info("sched: update_job: setting begin "
				     "to %s for job_id %u",
				     time_str, job_ptr->job_id);
			} else
				debug("sched: update_job: new begin time "
				      "identical to old begin time %u",
				      job_ptr->job_id);
		} else {
			error_code = ESLURM_JOB_NOT_PENDING;
			goto fini;
		}
	}

	if (job_specs->licenses) {
		List license_list;
		bool valid;

		license_list = license_validate(job_specs->licenses, &valid);
		if (!valid) {
			info("sched: update_job: invalid licenses: %s",
			     job_specs->licenses);
			error_code = ESLURM_INVALID_LICENSES;
		} else if (IS_JOB_PENDING(job_ptr)) {
			FREE_NULL_LIST(job_ptr->license_list);
			job_ptr->license_list = license_list;
			info("sched: update_job: changing licenses from '%s' "
			     "to '%s' for pending job %u",
			     job_ptr->licenses, job_specs->licenses,
			     job_ptr->job_id);
			xfree(job_ptr->licenses);
			job_ptr->licenses = job_specs->licenses;
			job_specs->licenses = NULL; /* nothing to free */
		} else if (IS_JOB_RUNNING(job_ptr) &&
			   (authorized || (license_list == NULL))) {
			/* NOTE: This can result in oversubscription of
			 * licenses */
			license_job_return(job_ptr);
			FREE_NULL_LIST(job_ptr->license_list);
			job_ptr->license_list = license_list;
			info("sched: update_job: changing licenses from '%s' "
			     "to '%s' for running job %u",
			     job_ptr->licenses, job_specs->licenses,
			     job_ptr->job_id);
			xfree(job_ptr->licenses);
			job_ptr->licenses = job_specs->licenses;
			job_specs->licenses = NULL; /* nothing to free */
			license_job_get(job_ptr);
		} else {
			/* licenses are valid, but job state or user not
			 * allowed to make changes */
			info("sched: update_job: could not change licenses "
			     "for job %u", job_ptr->job_id);
			error_code = ESLURM_JOB_NOT_PENDING_NOR_RUNNING;
			FREE_NULL_LIST(license_list);
		}
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
		if ((job_ptr->state_reason != WAIT_HELD) &&
		    (job_ptr->state_reason != WAIT_HELD_USER)) {
			job_ptr->state_reason = fail_reason;
			xfree(job_ptr->state_desc);
		}
		return error_code;
	} else if ((job_ptr->state_reason != WAIT_HELD)
		   && (job_ptr->state_reason != WAIT_HELD_USER)
		   && job_ptr->state_reason != WAIT_MAX_REQUEUE) {
		job_ptr->state_reason = WAIT_NO_REASON;
	}

#ifdef HAVE_BG
	select_g_select_jobinfo_get(job_specs->select_jobinfo,
				    SELECT_JOBDATA_CONN_TYPE, &conn_type);
	if (conn_type[0] != (uint16_t) NO_VAL) {
		if ((!IS_JOB_PENDING(job_ptr)) || (detail_ptr == NULL))
			error_code = ESLURM_JOB_NOT_PENDING;
		else {
			char *conn_type_char = conn_type_string_full(conn_type);
			if ((conn_type[0] >= SELECT_SMALL)
			   && (detail_ptr->min_cpus >= cpus_per_mp)) {
				info("update_job: could not change "
				     "conn_type to '%s' because cpu "
				     "count is %u for job %u making "
				     "the conn_type invalid.",
				     conn_type_char,
				     detail_ptr->min_cpus,
				     job_ptr->job_id);
				error_code = ESLURM_INVALID_NODE_COUNT;
			} else if (((conn_type[0] == SELECT_TORUS)
				   || (conn_type[0] == SELECT_MESH))
				  && (detail_ptr->min_cpus < cpus_per_mp)) {
				info("update_job: could not change "
				     "conn_type to '%s' because cpu "
				     "count is %u for job %u making "
				     "the conn_type invalid.",
				     conn_type_char,
				     detail_ptr->min_cpus,
				     job_ptr->job_id);
				error_code = ESLURM_INVALID_NODE_COUNT;
			} else {
				info("update_job: setting conn_type to '%s' "
				     "for jobid %u",
				     conn_type_char,
				     job_ptr->job_id);
				select_g_select_jobinfo_set(
					job_ptr->select_jobinfo,
					SELECT_JOBDATA_CONN_TYPE, &conn_type);
			}
			xfree(conn_type_char);
		}
	}

	if (error_code != SLURM_SUCCESS)
		goto fini;

	/* check to make sure we didn't mess up with the proc count */
	select_g_select_jobinfo_get(job_ptr->select_jobinfo,
				    SELECT_JOBDATA_CONN_TYPE, &conn_type);
	if (detail_ptr &&
	   (((conn_type[0] >= SELECT_SMALL)
	     && (detail_ptr->min_cpus >= cpus_per_mp))
	    || (((conn_type[0] == SELECT_TORUS)|| (conn_type[0] == SELECT_MESH))
		&& (detail_ptr->min_cpus < cpus_per_mp)))) {
		char *conn_type_char = conn_type_string_full(conn_type);
		info("update_job: With cpu count at %u our conn_type "
		     "of '%s' is invalid for job %u.",
		     detail_ptr->min_cpus,
		     conn_type_char,
		     job_ptr->job_id);
		xfree(conn_type_char);
		error_code = ESLURM_INVALID_NODE_COUNT;
		goto fini;
	}

	select_g_select_jobinfo_get(job_specs->select_jobinfo,
				    SELECT_JOBDATA_ROTATE, &rotate);
	if (rotate != (uint16_t) NO_VAL) {
		if (!IS_JOB_PENDING(job_ptr)) {
			error_code = ESLURM_JOB_NOT_PENDING;
			goto fini;
		} else {
			info("sched: update_job: setting rotate to %u for "
			     "jobid %u", rotate, job_ptr->job_id);
			select_g_select_jobinfo_set(
				job_ptr->select_jobinfo,
				SELECT_JOBDATA_ROTATE, &rotate);
		}
	}

	select_g_select_jobinfo_get(job_specs->select_jobinfo,
				    SELECT_JOBDATA_REBOOT, &reboot);
	if (reboot != (uint16_t) NO_VAL) {
		if (!IS_JOB_PENDING(job_ptr)) {
			error_code = ESLURM_JOB_NOT_PENDING;
			goto fini;
		} else {
			info("sched: update_job: setting reboot to %u for "
			     "jobid %u", reboot, job_ptr->job_id);
			select_g_select_jobinfo_set(
				job_ptr->select_jobinfo,
				SELECT_JOBDATA_REBOOT, &reboot);
		}
	}

	select_g_select_jobinfo_get(job_specs->select_jobinfo,
				    SELECT_JOBDATA_GEOMETRY, geometry);
	if (geometry[0] != (uint16_t) NO_VAL) {
		if (!IS_JOB_PENDING(job_ptr))
			error_code = ESLURM_JOB_NOT_PENDING;
		else if (authorized) {
			uint32_t i, tot = 1;
			for (i=0; i<SYSTEM_DIMENSIONS; i++)
				tot *= geometry[i];
			info("sched: update_job: setting geometry to %ux%ux%u"
			     " min_nodes=%u for jobid %u",
			     geometry[0], geometry[1],
			     geometry[2], tot, job_ptr->job_id);
			select_g_select_jobinfo_set(job_ptr->select_jobinfo,
						    SELECT_JOBDATA_GEOMETRY,
						    geometry);
			detail_ptr->min_nodes = tot;
		} else {
			error("sched: Attempt to change geometry for job %u",
			      job_ptr->job_id);
			error_code = ESLURM_ACCESS_DENIED;
		}
	}
	if (error_code != SLURM_SUCCESS)
		goto fini;

	select_g_select_jobinfo_get(job_specs->select_jobinfo,
				    SELECT_JOBDATA_BLRTS_IMAGE, &image);
	if (image) {
		if (!IS_JOB_PENDING(job_ptr)) {
			xfree(image);
			error_code = ESLURM_JOB_NOT_PENDING;
			goto fini;
		} else {
			info("sched: update_job: setting BlrtsImage to %s "
			     "for jobid %u", image, job_ptr->job_id);
			select_g_select_jobinfo_set(
				job_ptr->select_jobinfo,
				SELECT_JOBDATA_BLRTS_IMAGE,
				image);
		}
		xfree(image);
	}
	select_g_select_jobinfo_get(job_specs->select_jobinfo,
				    SELECT_JOBDATA_LINUX_IMAGE, &image);
	if (image) {
		if (!IS_JOB_PENDING(job_ptr)) {
			error_code = ESLURM_JOB_NOT_PENDING;
			xfree(image);
			goto fini;
		} else {
			info("sched: update_job: setting LinuxImage to %s "
			     "for jobid %u", image, job_ptr->job_id);
			select_g_select_jobinfo_set(
				job_ptr->select_jobinfo,
				SELECT_JOBDATA_LINUX_IMAGE, image);
		}
		xfree(image);
	}
	select_g_select_jobinfo_get(job_specs->select_jobinfo,
				    SELECT_JOBDATA_MLOADER_IMAGE, &image);
	if (image) {
		if (!IS_JOB_PENDING(job_ptr)) {
			error_code = ESLURM_JOB_NOT_PENDING;
			xfree(image);
			goto fini;
		} else {
			info("sched: update_job: setting MloaderImage to %s "
			     "for jobid %u", image, job_ptr->job_id);
			select_g_select_jobinfo_set(
				job_ptr->select_jobinfo,
				SELECT_JOBDATA_MLOADER_IMAGE,
				image);
		}
		xfree(image);
	}
	select_g_select_jobinfo_get(job_specs->select_jobinfo,
				    SELECT_JOBDATA_RAMDISK_IMAGE, &image);
	if (image) {
		if (!IS_JOB_PENDING(job_ptr)) {
			error_code = ESLURM_JOB_NOT_PENDING;
			xfree(image);
			goto fini;
		} else {
			info("sched: update_job: setting RamdiskImage to %s "
			     "for jobid %u", image, job_ptr->job_id);
			select_g_select_jobinfo_set(
				job_ptr->select_jobinfo,
				SELECT_JOBDATA_RAMDISK_IMAGE,
				image);
		}
		xfree(image);
	}
#else
	if (job_specs->reboot != (uint16_t) NO_VAL) {
		if (!IS_JOB_PENDING(job_ptr)) {
			error_code = ESLURM_JOB_NOT_PENDING;
			goto fini;
		} else {
			info("sched: update_job: setting reboot to %u for "
			     "jobid %u", job_specs->reboot, job_ptr->job_id);
			if (job_specs->reboot == 0)
				job_ptr->reboot = 0;
			else
				job_ptr->reboot = MAX(1, job_specs->reboot);
		}
	}
#endif

	if (job_specs->network) {
		xfree(job_ptr->network);
		if (!strlen(job_specs->network)
		    || !strcmp(job_specs->network, "none")) {
			info("sched: update_job: clearing Network option "
			     "for jobid %u", job_ptr->job_id);

		} else {
			job_ptr->network = xstrdup(job_specs->network);
			info("sched: update_job: setting Network to %s "
			     "for jobid %u", job_ptr->network, job_ptr->job_id);
			select_g_select_jobinfo_set(
				job_ptr->select_jobinfo,
				SELECT_JOBDATA_NETWORK,
				job_ptr->network);
		}
	}

fini:
	if (update_accounting) {
		info("updating accounting");
		if (job_ptr->details && job_ptr->details->begin_time) {
			/* Update job record in accounting to reflect changes */
			jobacct_storage_g_job_start(acct_db_conn,
						    job_ptr);
		}
	}

	/* If job update is successful and priority is calculated (not only
	 * based upon job submit order), recalculate the job priority, since
	 * many factors of an update may affect priority considerations.
	 * If job has a hold then do nothing */
	if ((error_code == SLURM_SUCCESS) && (job_ptr->priority != 0) &&
	    strcmp(slurmctld_conf.priority_type, "priority/basic"))
		set_job_prio(job_ptr);

	return error_code;
}

/*
 * update_job - update a job's parameters per the supplied specifications
 * IN msg - RPC to update job, including change specification
 * IN uid - uid of user issuing RPC
 * RET returns an error code from slurm_errno.h
 * global: job_list - global list of job entries
 *	last_job_update - time of last job table update
 */
extern int update_job(slurm_msg_t *msg, uid_t uid)
{
	job_desc_msg_t *job_specs = (job_desc_msg_t *) msg->data;
	struct job_record *job_ptr;
	int rc;

	xfree(job_specs->job_id_str);
	xstrfmtcat(job_specs->job_id_str, "%u", job_specs->job_id);

	job_ptr = find_job_record(job_specs->job_id);
	if (job_ptr == NULL) {
		error("update_job: job_id %u does not exist.",
		      job_specs->job_id);
		rc = ESLURM_INVALID_JOB_ID;
	} else {
		rc = _update_job(job_ptr, job_specs, uid);
	}

	slurm_send_rc_msg(msg, rc);
	return rc;
}

/*
 * IN msg - RPC to update job, including change specification
 * IN job_specs - a job's specification
 * IN uid - uid of user issuing RPC
 * RET returns an error code from slurm_errno.h
 * global: job_list - global list of job entries
 *	last_job_update - time of last job table update
 */
extern int update_job_str(slurm_msg_t *msg, uid_t uid)
{
	slurm_msg_t resp_msg;
	job_desc_msg_t *job_specs = (job_desc_msg_t *) msg->data;
	struct job_record *job_ptr, *new_job_ptr;
	slurm_ctl_conf_t *conf;
	long int long_id;
	uint32_t job_id = 0;
	bitstr_t *array_bitmap = NULL, *tmp_bitmap;
	bool valid = true;
	int32_t i, i_first, i_last;
	int len, rc = SLURM_SUCCESS, rc2;
	char *end_ptr, *tok, *tmp;
	char *job_id_str;
	resp_array_struct_t *resp_array = NULL;
	job_array_resp_msg_t *resp_array_msg = NULL;
	return_code_msg_t rc_msg;

	job_id_str = job_specs->job_id_str;

	if (max_array_size == NO_VAL) {
		conf = slurm_conf_lock();
		max_array_size = conf->max_array_sz;
		slurm_conf_unlock();
	}

	long_id = strtol(job_id_str, &end_ptr, 10);
	if ((long_id <= 0) || (long_id == LONG_MAX) ||
	    ((end_ptr[0] != '\0') && (end_ptr[0] != '_'))) {
		info("update_job_str: invalid job id %s", job_id_str);
		rc = ESLURM_INVALID_JOB_ID;
		goto reply;
	}
	job_id = (uint32_t) long_id;
	if (end_ptr[0] == '\0') {	/* Single job (or full job array) */
		struct job_record *job_ptr_done = NULL;
		job_ptr = find_job_record(job_id);
		if (job_ptr &&
		    (((job_ptr->array_task_id == NO_VAL) &&
		      (job_ptr->array_recs == NULL)) ||
		     ((job_ptr->array_task_id != NO_VAL) &&
		      (job_ptr->array_job_id  != job_id)))) {
			/* This is a regular job or single task of job array */
			rc = _update_job(job_ptr, job_specs, uid);
			goto reply;
		}

		if (job_ptr && job_ptr->array_recs) {
			/* This is a job array */
			job_ptr_done = job_ptr;
			rc2 = _update_job(job_ptr, job_specs, uid);
			_resp_array_add(&resp_array, job_ptr, rc2);
		}

		/* Update all tasks of this job array */
		job_ptr = job_array_hash_j[JOB_HASH_INX(job_id)];
		if (!job_ptr && !job_ptr_done) {
			info("update_job_str: invalid job id %u", job_id);
			rc = ESLURM_INVALID_JOB_ID;
			goto reply;
		}
		while (job_ptr) {
			if ((job_ptr->array_job_id == job_id) &&
			    (job_ptr != job_ptr_done)) {
				rc2 = _update_job(job_ptr, job_specs, uid);
				_resp_array_add(&resp_array, job_ptr, rc2);
			}
			job_ptr = job_ptr->job_array_next_j;
		}
		goto reply;
	}

	array_bitmap = bit_alloc(max_array_size);
	tmp = xstrdup(end_ptr + 1);
	tok = strtok_r(tmp, ",", &end_ptr);
	while (tok && valid) {
		valid = _parse_array_tok(tok, array_bitmap,
					 max_array_size);
		tok = strtok_r(NULL, ",", &end_ptr);
	}
	xfree(tmp);
	if (valid) {
		i_last = bit_fls(array_bitmap);
		if (i_last < 0)
			valid = false;
	}
	if (!valid) {
		info("update_job_str: invalid job id %s", job_id_str);
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
			array_bitmap = bit_realloc(array_bitmap, len);
		} else {
			array_bitmap = bit_realloc(array_bitmap, i_last);
			job_ptr->array_recs->task_id_bitmap = bit_realloc(
				job_ptr->array_recs->task_id_bitmap, i_last);
		}
		if (!bit_overlap(job_ptr->array_recs->task_id_bitmap,
				 array_bitmap)) {
			/* Nothing to do with this job record */
		} else if (bit_super_set(job_ptr->array_recs->task_id_bitmap,
					 array_bitmap)) {
			/* Update the record with all pending tasks */
			rc2 = _update_job(job_ptr, job_specs, uid);
			_resp_array_add(&resp_array, job_ptr, rc2);
			bit_not(job_ptr->array_recs->task_id_bitmap);
			bit_and(array_bitmap,
				job_ptr->array_recs->task_id_bitmap);
			bit_not(job_ptr->array_recs->task_id_bitmap);
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
				if (!bit_test(array_bitmap, i))
					continue;
				job_ptr->array_task_id = i;
				new_job_ptr = _job_rec_copy(job_ptr);
				if (!new_job_ptr) {
					error("update_job_str: Unable to copy "
					      "record for job %u",
					      job_ptr->job_id);
				} else {
					/* The array_recs structure is moved
					 * to the new job record copy */
					job_ptr = new_job_ptr;
				}
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
			info("update_job_str: invalid job id %u_%d", job_id, i);
			_resp_array_add_id(&resp_array, job_id, i,
					   ESLURM_INVALID_JOB_ID);
			continue;
		}

		rc2 = _update_job(job_ptr, job_specs, uid);
		_resp_array_add(&resp_array, job_ptr, rc2);
	}

reply:
        if (msg->conn_fd >= 0) {
		slurm_msg_t_init(&resp_msg);
		resp_msg.protocol_version = msg->protocol_version;
		if (resp_array) {
		        resp_array_msg = _resp_array_xlate(resp_array, job_id);
		        resp_msg.msg_type  = RESPONSE_JOB_ARRAY_ERRORS;
		        resp_msg.data      = resp_array_msg;
		} else {
		        resp_msg.msg_type  = RESPONSE_SLURM_RC;
		        rc_msg.return_code = rc;
		        resp_msg.data      = &rc_msg;
		}
		slurm_send_node_msg(msg->conn_fd, &resp_msg);

		if (resp_array_msg) {
			slurm_free_job_array_resp(resp_array_msg);
			resp_msg.data = NULL;
		}
	}
        _resp_array_free(resp_array);

	FREE_NULL_BITMAP(array_bitmap);

	return rc;
}

static void _send_job_kill(struct job_record *job_ptr)
{
	kill_job_msg_t *kill_job = NULL;
	agent_arg_t *agent_args = NULL;
#ifdef HAVE_FRONT_END
	front_end_record_t *front_end_ptr;
#else
	int i;
	struct node_record *node_ptr;
#endif

	if (select_serial == -1) {
		if (strcmp(slurmctld_conf.select_type, "select/serial"))
			select_serial = 0;
		else
			select_serial = 1;
	}

	xassert(job_ptr);
	xassert(job_ptr->details);

	agent_args = xmalloc(sizeof(agent_arg_t));
	agent_args->msg_type = REQUEST_TERMINATE_JOB;
	agent_args->retry = 0;	/* re_kill_job() resends as needed */
	agent_args->hostlist = hostlist_create(NULL);
	kill_job = xmalloc(sizeof(kill_job_msg_t));
	last_node_update    = time(NULL);
	kill_job->job_id    = job_ptr->job_id;
	kill_job->step_id   = NO_VAL;
	kill_job->job_state = job_ptr->job_state;
	kill_job->job_uid   = job_ptr->user_id;
	kill_job->nodes     = xstrdup(job_ptr->nodes);
	kill_job->time      = time(NULL);
	kill_job->start_time = job_ptr->start_time;
	kill_job->select_jobinfo = select_g_select_jobinfo_copy(
			job_ptr->select_jobinfo);
	kill_job->spank_job_env = xduparray(job_ptr->spank_job_env_size,
					    job_ptr->spank_job_env);
	kill_job->spank_job_env_size = job_ptr->spank_job_env_size;

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
	for (i = 0, node_ptr = node_record_table_ptr;
	     i < node_record_count; i++, node_ptr++) {
		if (!bit_test(job_ptr->node_bitmap_cg, i))
			continue;
		if (agent_args->protocol_version > node_ptr->protocol_version)
			agent_args->protocol_version =
				node_ptr->protocol_version;
		hostlist_push_host(agent_args->hostlist, node_ptr->name);
		agent_args->node_count++;
	}
#endif
	if (agent_args->node_count == 0) {
		if ((job_ptr->details->expanding_jobid == 0) &&
		    (select_serial == 0)) {
			error("%s: job %u allocated no nodes to be killed on",
			      __func__, job_ptr->job_id);
		}
		xfree(kill_job->nodes);
		xfree(kill_job);
		hostlist_destroy(agent_args->hostlist);
		xfree(agent_args);
		return;
	}

	agent_args->msg_args = kill_job;
	agent_queue_request(agent_args);
	return;
}

/* Record accounting information for a job immediately before changing size */
extern void job_pre_resize_acctg(struct job_record *job_ptr)
{
	/* if we don't have a db_index go a start this one up since if
	   running with the slurmDBD the job may not have started yet.
	*/

	if ((!job_ptr->db_index || job_ptr->db_index == NO_VAL)
	    && !job_ptr->resize_time)
		jobacct_storage_g_job_start(acct_db_conn, job_ptr);

	job_ptr->job_state |= JOB_RESIZING;
	/* NOTE: job_completion_logger() calls
	 *	 acct_policy_remove_job_submit() */
	job_completion_logger(job_ptr, false);

	/* This doesn't happen in job_completion_logger, but gets
	 * added back in with job_post_resize_acctg so remove it here. */
	acct_policy_job_fini(job_ptr);

	/* NOTE: The RESIZING FLAG needed to be cleared with
	   job_post_resize_acctg */
}

/* Record accounting information for a job immediately after changing size */
extern void job_post_resize_acctg(struct job_record *job_ptr)
{
	time_t org_submit = job_ptr->details->submit_time;

	/* NOTE: The RESIZING FLAG needed to be set with
	   job_pre_resize_acctg the assert is here to make sure we
	   code it that way. */
	xassert(IS_JOB_RESIZING(job_ptr));
	acct_policy_add_job_submit(job_ptr);
	acct_policy_job_begin(job_ptr);

	if (job_ptr->resize_time)
		job_ptr->details->submit_time = job_ptr->resize_time;

	job_ptr->resize_time = time(NULL);

	jobacct_storage_g_job_start(acct_db_conn, job_ptr);

	job_ptr->details->submit_time = org_submit;
	job_ptr->job_state &= (~JOB_RESIZING);
}

/*
 * validate_jobs_on_node - validate that any jobs that should be on the node
 *	are actually running, if not clean up the job records and/or node
 *	records, call this function after validate_node_specs() sets the node
 *	state properly
 * IN reg_msg - node registration message
 */
extern void
validate_jobs_on_node(slurm_node_registration_status_msg_t *reg_msg)
{
	int i, node_inx, jobs_on_node;
	struct node_record *node_ptr;
	struct job_record *job_ptr;
	struct step_record *step_ptr;
	time_t now = time(NULL);

	node_ptr = find_node_record(reg_msg->node_name);
	if (node_ptr == NULL) {
		error("slurmd registered on unknown node %s",
			reg_msg->node_name);
		return;
	}

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

	node_inx = node_ptr - node_record_table_ptr;

	/* Check that jobs running are really supposed to be there */
	for (i = 0; i < reg_msg->job_count; i++) {
		if ( (reg_msg->job_id[i] >= MIN_NOALLOC_JOBID) &&
		     (reg_msg->job_id[i] <= MAX_NOALLOC_JOBID) ) {
			info("NoAllocate job %u.%u reported on node %s",
			     reg_msg->job_id[i], reg_msg->step_id[i],
			     reg_msg->node_name);
			continue;
		}

		job_ptr = find_job_record(reg_msg->job_id[i]);
		if (job_ptr == NULL) {
			error("Orphan job %u.%u reported on node %s",
			      reg_msg->job_id[i], reg_msg->step_id[i],
			      reg_msg->node_name);
			abort_job_on_node(reg_msg->job_id[i],
					  job_ptr, node_ptr->name);
		}

		else if (IS_JOB_RUNNING(job_ptr) ||
			 IS_JOB_SUSPENDED(job_ptr)) {
			if (bit_test(job_ptr->node_bitmap, node_inx)) {
				debug3("Registered job %u.%u on node %s ",
				       reg_msg->job_id[i],
				       reg_msg->step_id[i],
				       reg_msg->node_name);
				if ((job_ptr->batch_flag) &&
				    (node_inx == bit_ffs(
						job_ptr->node_bitmap))) {
					/* NOTE: Used for purging defunct
					 * batch jobs */
					job_ptr->time_last_active = now;
				}
				step_ptr = find_step_record(job_ptr,
							    reg_msg->
							    step_id[i]);
				if (step_ptr)
					step_ptr->time_last_active = now;
			} else {
				/* Typically indicates a job requeue and
				 * restart on another nodes. A node from the
				 * original allocation just responded here. */
				error("Registered job %u.%u on wrong node %s ",
				      reg_msg->job_id[i],
				      reg_msg->step_id[i],
				      reg_msg->node_name);
				info("%s: job nodes %s count %d inx %d",
				     __func__, job_ptr->nodes,
				     job_ptr->node_cnt, node_inx);
				abort_job_on_node(reg_msg->job_id[i], job_ptr,
						  node_ptr->name);
			}
		}

		else if (IS_JOB_COMPLETING(job_ptr)) {
			/* Re-send kill request as needed,
			 * not necessarily an error */
			kill_job_on_node(reg_msg->job_id[i], job_ptr,
					 node_ptr);
		}


		else if (IS_JOB_PENDING(job_ptr)) {
			/* Typically indicates a job requeue and the hung
			 * slurmd that went DOWN is now responding */
			error("Registered PENDING job %u.%u on node %s ",
			      reg_msg->job_id[i], reg_msg->step_id[i],
			      reg_msg->node_name);
			abort_job_on_node(reg_msg->job_id[i],
					  job_ptr, node_ptr->name);
		}

		else if (difftime(now, job_ptr->end_time) <
			 slurm_get_msg_timeout()) {	/* Race condition */
			debug("Registered newly completed job %u.%u on %s",
				reg_msg->job_id[i], reg_msg->step_id[i],
				node_ptr->name);
		}

		else {		/* else job is supposed to be done */
			error("Registered job %u.%u in state %s on node %s ",
			      reg_msg->job_id[i], reg_msg->step_id[i],
			      job_state_string(job_ptr->job_state),
			      reg_msg->node_name);
			kill_job_on_node(reg_msg->job_id[i], job_ptr,
					 node_ptr);
		}
	}

	jobs_on_node = node_ptr->run_job_cnt + node_ptr->comp_job_cnt;
	if (jobs_on_node)
		_purge_missing_jobs(node_inx, now);

	if (jobs_on_node != reg_msg->job_count) {
		/* slurmd will not know of a job unless the job has
		 * steps active at registration time, so this is not
		 * an error condition, slurmd is also reporting steps
		 * rather than jobs */
		debug3("resetting job_count on node %s from %u to %d",
			reg_msg->node_name, reg_msg->job_count, jobs_on_node);
		reg_msg->job_count = jobs_on_node;
	}

	return;
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
	ListIterator job_iterator;
	struct job_record *job_ptr;
	struct node_record *node_ptr = node_record_table_ptr + node_inx;
	uint16_t batch_start_timeout	= slurm_get_batch_start_timeout();
	uint16_t msg_timeout		= slurm_get_msg_timeout();
	uint16_t resume_timeout		= slurm_get_resume_timeout();
	uint32_t suspend_time		= slurm_get_suspend_time();
	time_t batch_startup_time, node_boot_time = (time_t) 0, startup_time;

	if (node_ptr->boot_time > (msg_timeout + 5)) {
		/* allow for message timeout and other delays */
		node_boot_time = node_ptr->boot_time - (msg_timeout + 5);
	}
	batch_startup_time  = now - batch_start_timeout;
	batch_startup_time -= msg_timeout;

	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		bool job_active = IS_JOB_RUNNING(job_ptr) ||
				  IS_JOB_SUSPENDED(job_ptr);

		if ((!job_active) ||
		    (!bit_test(job_ptr->node_bitmap, node_inx)))
			continue;
		if ((job_ptr->batch_flag != 0)			&&
		    (suspend_time != 0) /* power mgmt on */	&&
		    (job_ptr->start_time < node_boot_time)) {
			startup_time = batch_startup_time - resume_timeout;
		} else
			startup_time = batch_startup_time;

		if ((job_ptr->batch_flag != 0)			&&
		    (job_ptr->time_last_active < startup_time)	&&
		    (job_ptr->start_time       < startup_time)	&&
		    (node_inx == bit_ffs(job_ptr->node_bitmap))) {
			bool requeue = false;
			if ((job_ptr->start_time < node_ptr->boot_time) &&
			    (job_ptr->details && job_ptr->details->requeue))
				requeue = true;
			info("Batch JobId=%u missing from node 0 (not found "
			     "BatchStartTime after startup)", job_ptr->job_id);
			job_ptr->exit_code = 1;
			job_complete(job_ptr->job_id, 0, requeue, true, NO_VAL);
		} else {
			_notify_srun_missing_step(job_ptr, node_inx,
						  now, node_boot_time);
		}
	}
	list_iterator_destroy(job_iterator);
}

static void _notify_srun_missing_step(struct job_record *job_ptr, int node_inx,
				      time_t now, time_t node_boot_time)
{
	ListIterator step_iterator;
	struct step_record *step_ptr;
	char *node_name = node_record_table_ptr[node_inx].name;

	xassert(job_ptr);
	step_iterator = list_iterator_create (job_ptr->step_list);
	while ((step_ptr = (struct step_record *) list_next (step_iterator))) {
		if (step_ptr->state != JOB_RUNNING)
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
			   (step_ptr->no_kill == 0)) {
			/* There is a risk that the job step's tasks completed
			 * on this node before its reboot, but that should be
			 * very rare and there is no srun to work with (POE) */
			info("Node %s rebooted, killing missing step %u.%u",
			     node_name, job_ptr->job_id, step_ptr->step_id);
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
 * IN job_ptr - pointer to terminating job (NULL if unknown, e.g. orphaned)
 * IN node_name - name of the node on which the job resides
 */
extern void
abort_job_on_node(uint32_t job_id, struct job_record *job_ptr, char *node_name)
{
	agent_arg_t *agent_info;
	kill_job_msg_t *kill_req;

	kill_req = xmalloc(sizeof(kill_job_msg_t));
	kill_req->job_id	= job_id;
	kill_req->step_id	= NO_VAL;
	kill_req->time          = time(NULL);
	kill_req->nodes		= xstrdup(node_name);
	if (job_ptr) {  /* NULL if unknown */
		kill_req->start_time = job_ptr->start_time;
		kill_req->select_jobinfo =
			select_g_select_jobinfo_copy(job_ptr->select_jobinfo);
		kill_req->spank_job_env = xduparray(job_ptr->spank_job_env_size,
						    job_ptr->spank_job_env);
		kill_req->spank_job_env_size = job_ptr->spank_job_env_size;
	} else {
		/* kill_req->start_time = 0;  Default value */
	}

	agent_info = xmalloc(sizeof(agent_arg_t));
	agent_info->node_count	= 1;
	agent_info->retry	= 0;
	agent_info->hostlist	= hostlist_create(node_name);
#ifdef HAVE_FRONT_END
	if (job_ptr && job_ptr->front_end_ptr)
		agent_info->protocol_version =
			job_ptr->front_end_ptr->protocol_version;
	debug("Aborting job %u on front end node %s", job_id, node_name);
#else
	struct node_record *node_ptr;
	if ((node_ptr = find_node_record(node_name)))
		agent_info->protocol_version = node_ptr->protocol_version;

	debug("Aborting job %u on node %s", job_id, node_name);
#endif
	agent_info->msg_type	= REQUEST_ABORT_JOB;
	agent_info->msg_args	= kill_req;

	agent_queue_request(agent_info);
}

/*
 * kill_job_on_node - Kill the specific job_id on a specific node.
 * IN job_id - id of the job to be killed
 * IN job_ptr - pointer to terminating job (NULL if unknown, e.g. orphaned)
 * IN node_ptr - pointer to the node on which the job resides
 */
extern void
kill_job_on_node(uint32_t job_id, struct job_record *job_ptr,
		struct node_record *node_ptr)
{
	agent_arg_t *agent_info;
	kill_job_msg_t *kill_req;

	kill_req = xmalloc(sizeof(kill_job_msg_t));
	kill_req->job_id	= job_id;
	kill_req->step_id	= NO_VAL;
	kill_req->time          = time(NULL);
	kill_req->start_time	= job_ptr->start_time;
	kill_req->nodes		= xstrdup(node_ptr->name);
	if (job_ptr) {  /* NULL if unknown */
		kill_req->select_jobinfo =
			select_g_select_jobinfo_copy(job_ptr->select_jobinfo);
		kill_req->job_state = job_ptr->job_state;
	}
	kill_req->spank_job_env = xduparray(job_ptr->spank_job_env_size,
					    job_ptr->spank_job_env);
	kill_req->spank_job_env_size = job_ptr->spank_job_env_size;

	agent_info = xmalloc(sizeof(agent_arg_t));
	agent_info->node_count	= 1;
	agent_info->retry	= 0;
#ifdef HAVE_FRONT_END
	xassert(job_ptr->batch_host);
	if (job_ptr->front_end_ptr)
		agent_info->protocol_version =
			job_ptr->front_end_ptr->protocol_version;
	agent_info->hostlist	= hostlist_create(job_ptr->batch_host);
	debug("Killing job %u on front end node %s", job_id,
	      job_ptr->batch_host);
#else
	agent_info->protocol_version = node_ptr->protocol_version;
	agent_info->hostlist	= hostlist_create(node_ptr->name);
	debug("Killing job %u on node %s", job_id, node_ptr->name);
#endif
	agent_info->msg_type	= REQUEST_TERMINATE_JOB;
	agent_info->msg_args	= kill_req;

	agent_queue_request(agent_info);
}


/*
 * job_alloc_info - get details about an existing job allocation
 * IN uid - job issuing the code
 * IN job_id - ID of job for which info is requested
 * OUT job_pptr - set to pointer to job record
 */
extern int
job_alloc_info(uint32_t uid, uint32_t job_id, struct job_record **job_pptr)
{
	struct job_record *job_ptr;

	job_ptr = find_job_record(job_id);
	if (job_ptr == NULL)
		return ESLURM_INVALID_JOB_ID;
	if ((slurmctld_conf.private_data & PRIVATE_DATA_JOBS) &&
	    (job_ptr->user_id != uid) && !validate_operator(uid) &&
	    !assoc_mgr_is_user_acct_coord(acct_db_conn, uid, job_ptr->account))
		return ESLURM_ACCESS_DENIED;
	if (IS_JOB_PENDING(job_ptr))
		return ESLURM_JOB_PENDING;
	if (IS_JOB_FINISHED(job_ptr))
		return ESLURM_ALREADY_DONE;

	if (job_ptr->alias_list && !strcmp(job_ptr->alias_list, "TBD") &&
	    job_ptr->node_bitmap &&
	    (bit_overlap(power_node_bitmap, job_ptr->node_bitmap) == 0)) {
		job_ptr->job_state &= (~JOB_CONFIGURING);
		set_job_alias_list(job_ptr);
	}

	*job_pptr = job_ptr;
	return SLURM_SUCCESS;
}

/*
 * Synchronize the batch job in the system with their files.
 * All pending batch jobs must have script and environment files
 * No other jobs should have such files
 * NOTE: READ lock_slurmctld config before entry
 */
int sync_job_files(void)
{
	List batch_dirs;

	if (!slurmctld_primary)	/* Don't purge files from backup slurmctld */
		return SLURM_SUCCESS;

	batch_dirs = list_create(_del_batch_list_rec);
	_get_batch_job_dir_ids(batch_dirs);
	_validate_job_files(batch_dirs);
	_remove_defunct_batch_dirs(batch_dirs);
	list_destroy(batch_dirs);
	return SLURM_SUCCESS;
}

/* Append to the batch_dirs list the job_id's associated with
 *	every batch job directory in existence
 * NOTE: READ lock_slurmctld config before entry
 */
static void _get_batch_job_dir_ids(List batch_dirs)
{
	DIR *f_dir, *h_dir;
	struct dirent *dir_ent, *hash_ent;
	long long_job_id;
	uint32_t *job_id_ptr;
	char *endptr;

	xassert(slurmctld_conf.state_save_location);
	f_dir = opendir(slurmctld_conf.state_save_location);
	if (!f_dir) {
		error("opendir(%s): %m",
		      slurmctld_conf.state_save_location);
		return;
	}

	while ((dir_ent = readdir(f_dir))) {
		if (!strncmp("job.#", dir_ent->d_name, 4)) {
			/* Read version 14.03 or earlier format state */
			long_job_id = strtol(&dir_ent->d_name[4], &endptr, 10);
			if ((long_job_id == 0) || (endptr[0] != '\0'))
				continue;
			debug3("found batch directory for job_id %ld",
			      long_job_id);
			job_id_ptr = xmalloc(sizeof(uint32_t));
			*job_id_ptr = long_job_id;
			list_append(batch_dirs, job_id_ptr);
		} else if (!strncmp("hash.#", dir_ent->d_name, 5)) {
			char *h_path = NULL;
			xstrfmtcat(h_path, "%s/%s",
				   slurmctld_conf.state_save_location,
				   dir_ent->d_name);
			h_dir = opendir(h_path);
			xfree(h_path);
			if (!h_dir)
				continue;
			while ((hash_ent = readdir(h_dir))) {
				if (strncmp("job.#", hash_ent->d_name, 4))
					continue;
				long_job_id = strtol(&hash_ent->d_name[4],
						     &endptr, 10);
				if ((long_job_id == 0) || (endptr[0] != '\0'))
					continue;
				debug3("Found batch directory for job_id %ld",
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

/* All pending batch jobs must have a batch_dir entry,
 *	otherwise we flag it as FAILED and don't schedule
 * If the batch_dir entry exists for a PENDING or RUNNING batch job,
 *	remove it the list (of directories to be deleted) */
static void _validate_job_files(List batch_dirs)
{
	ListIterator job_iterator;
	struct job_record *job_ptr;
	int del_cnt;

	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		if (!job_ptr->batch_flag)
			continue;
		/* Want to keep this job's files */
		del_cnt = list_delete_all(batch_dirs, _find_batch_dir,
					  &(job_ptr->job_id));
		if ((del_cnt == 0) && IS_JOB_PENDING(job_ptr)) {
			error("Script for job %u lost, state set to FAILED",
			      job_ptr->job_id);
			job_ptr->job_state = JOB_FAILED;
			job_ptr->exit_code = 1;
			job_ptr->state_reason = FAIL_SYSTEM;
			xfree(job_ptr->state_desc);
			job_ptr->start_time = job_ptr->end_time = time(NULL);
			job_completion_logger(job_ptr, false);
		}
	}
	list_iterator_destroy(job_iterator);
}

/* List matching function, see common/list.h */
static int _find_batch_dir(void *x, void *key)
{
	uint32_t *key1 = x;
	uint32_t *key2 = key;
	return (int)(*key1 == *key2);
}
/* List entry deletion function, see common/list.h */
static void _del_batch_list_rec(void *x)
{
	xfree(x);
}

/* Remove all batch_dir entries in the list
 * NOTE: READ lock_slurmctld config before entry */
static void _remove_defunct_batch_dirs(List batch_dirs)
{
	ListIterator batch_dir_inx;
	uint32_t *job_id_ptr;

	batch_dir_inx = list_iterator_create(batch_dirs);
	while ((job_id_ptr = list_next(batch_dir_inx))) {
		info("Purging files for defunct batch job %u",
		     *job_id_ptr);
		_delete_job_desc_files(*job_id_ptr);
	}
	list_iterator_destroy(batch_dir_inx);
}

/*
 *  _xmit_new_end_time
 *	Tell all slurmd's associated with a job of its new end time
 * IN job_ptr - pointer to terminating job
 * globals: node_record_count - number of nodes in the system
 *	node_record_table_ptr - pointer to global node table
 */
static void
_xmit_new_end_time(struct job_record *job_ptr)
{
#ifndef HAVE_FRONT_END
	int i;
#endif
	job_time_msg_t *job_time_msg_ptr;
	agent_arg_t *agent_args;

	agent_args = xmalloc(sizeof(agent_arg_t));
	agent_args->msg_type = REQUEST_UPDATE_JOB_TIME;
	agent_args->retry = 1;
	agent_args->hostlist = hostlist_create(NULL);
	job_time_msg_ptr = xmalloc(sizeof(job_time_msg_t));
	job_time_msg_ptr->job_id          = job_ptr->job_id;
	job_time_msg_ptr->expiration_time = job_ptr->end_time;

#ifdef HAVE_FRONT_END
	xassert(job_ptr->batch_host);
	if (job_ptr->front_end_ptr)
		agent_args->protocol_version =
			job_ptr->front_end_ptr->protocol_version;
	hostlist_push_host(agent_args->hostlist, job_ptr->batch_host);
	agent_args->node_count  = 1;
#else
	agent_args->protocol_version = SLURM_PROTOCOL_VERSION;
	for (i = 0; i < node_record_count; i++) {
		if (bit_test(job_ptr->node_bitmap, i) == 0)
			continue;
		if (agent_args->protocol_version >
		    node_record_table_ptr[i].protocol_version)
			agent_args->protocol_version =
				node_record_table_ptr[i].protocol_version;
		hostlist_push_host(agent_args->hostlist,
			      node_record_table_ptr[i].name);
		agent_args->node_count++;
	}
#endif

	agent_args->msg_args = job_time_msg_ptr;
	agent_queue_request(agent_args);
	return;
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
#ifdef HAVE_FRONT_END
	int i;
#endif
	struct job_record  *job_ptr = find_job_record(job_id);
	struct node_record *node_ptr;
	char jbuf[JBUFSIZ];

	if (job_ptr == NULL)
		return true;

	trace_job(job_ptr, __func__, "enter");

	/* There is a potential race condition this handles.
	 * If slurmctld cold-starts while slurmd keeps running,
	 * slurmd could notify slurmctld of a job epilog completion
	 * before getting synced up with slurmctld state. If
	 * a new job arrives and the job_id is reused, we
	 * could try to note the termination of a job that
	 * hasn't really started. Very rare obviously. */
	if ((IS_JOB_PENDING(job_ptr) && (!IS_JOB_COMPLETING(job_ptr))) ||
	    (job_ptr->node_bitmap == NULL)) {
#ifndef HAVE_FRONT_END
		uint32_t base_state = NODE_STATE_UNKNOWN;
		node_ptr = find_node_record(node_name);
		if (node_ptr)
			base_state = node_ptr->node_state & NODE_STATE_BASE;
		if (base_state == NODE_STATE_DOWN) {
			debug("%s: %s complete response from DOWN "
			      "node %s", __func__,
			      jobid2str(job_ptr, jbuf), node_name);
		} else if (job_ptr->restart_cnt) {
			/* Duplicate epilog complete can be due to race
			 * condition, especially with select/serial */
			debug("%s: %s duplicate epilog complete response",
			      __func__, jobid2str(job_ptr, jbuf));
		} else {

			error("%s: %s is non-running slurmctld"
			      "and slurmd out of sync",
			      __func__, jobid2str(job_ptr, jbuf));
		}
#endif
		return false;
	}

#ifdef HAVE_FRONT_END
	xassert(job_ptr->batch_host);
	/* If there is a bad epilog error don't down the frontend
	   node.  If needed (not on a bluegene) the nodes in use by
	   the job will be downed below.
	*/
	if (return_code)
		error("%s: %s epilog error on %s",
		      __func__, jobid2str(job_ptr, jbuf),
		      job_ptr->batch_host);

	if (job_ptr->front_end_ptr && IS_JOB_COMPLETING(job_ptr)) {
		front_end_record_t *front_end_ptr = job_ptr->front_end_ptr;
		if (front_end_ptr->job_cnt_comp)
			front_end_ptr->job_cnt_comp--;
		else {
			error("%s: %s job_cnt_comp underflow on "
			      "front end %s", __func__,
			      jobid2str(job_ptr, jbuf),
			      front_end_ptr->name);
		}
		if (front_end_ptr->job_cnt_comp == 0)
			front_end_ptr->node_state &= (~NODE_STATE_COMPLETING);
	}

	if ((job_ptr->total_nodes == 0) && IS_JOB_COMPLETING(job_ptr)) {
		/* Job resources moved into another job and
		 *  tasks already killed */
		front_end_record_t *front_end_ptr = job_ptr->front_end_ptr;
		if (front_end_ptr)
			front_end_ptr->node_state &= (~NODE_STATE_COMPLETING);
	} else {
		for (i = 0; i < node_record_count; i++) {
			if (!bit_test(job_ptr->node_bitmap, i))
				continue;
			node_ptr = &node_record_table_ptr[i];
#ifndef HAVE_BG
			/* If this is a bluegene system we do not want to mark
			 * the entire midplane down if we have an epilog error.
			 * This would most likely kill other jobs sharing that
			 * midplane and that is not what we want. */
			if (return_code) {
				static uint32_t slurm_user_id = NO_VAL;
				if (slurm_user_id == NO_VAL)
					slurm_user_id=slurm_get_slurm_user_id();
				drain_nodes(node_ptr->name, "Epilog error",
					    slurm_user_id);
			}
#endif
			/* Change job from completing to completed */
			make_node_idle(node_ptr, job_ptr);
		}
	}
#else
	if (return_code) {
		error("%s: %s epilog error on %s, draining the node",
		      __func__, jobid2str(job_ptr, jbuf), node_name);
		drain_nodes(node_name, "Epilog error",
			    slurm_get_slurm_user_id());
	}
	/* Change job from completing to completed */
	node_ptr = find_node_record(node_name);
	if (node_ptr)
		make_node_idle(node_ptr, job_ptr);
#endif

	step_epilog_complete(job_ptr, node_name);
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
void batch_requeue_fini(struct job_record  *job_ptr)
{
	if (IS_JOB_COMPLETING(job_ptr) ||
	    !IS_JOB_PENDING(job_ptr) || !job_ptr->batch_flag)
		return;

	info("requeue batch job %u", job_ptr->job_id);

	/* Clear everything so this appears to be a new job and then restart
	 * it in accounting. */
	job_ptr->start_time = 0;
	job_ptr->end_time = 0;
	job_ptr->total_cpus = 0;
	job_ptr->pre_sus_time = 0;
	job_ptr->suspend_time = 0;
	job_ptr->tot_sus_time = 0;
	/* Current code (<= 2.1) has it so we start the new job with the next
	 * step id.  This could be used when restarting to figure out which
	 * step the previous run of this job stopped on. */
	//job_ptr->next_step_id = 0;

	job_ptr->node_cnt = 0;
#ifdef HAVE_BG
	select_g_select_jobinfo_set(job_ptr->select_jobinfo,
				    SELECT_JOBDATA_BLOCK_ID, "unassigned");
	/* If on a bluegene system we want to remove the job_resrcs so
	 * we don't get an error message about them already existing
	 * when the job goes to run again. */
	free_job_resources(&job_ptr->job_resrcs);
#endif
	xfree(job_ptr->nodes);
	xfree(job_ptr->nodes_completing);
	FREE_NULL_BITMAP(job_ptr->node_bitmap);
	FREE_NULL_BITMAP(job_ptr->node_bitmap_cg);
	if (job_ptr->details) {
		time_t now = time(NULL);
		/* the time stamp on the new batch launch credential must be
		 * larger than the time stamp on the revoke request. Also the
		 * I/O must be all cleared out and the named socket purged,
		 * so delay for at least ten seconds. */
		if (job_ptr->details->begin_time <= now)
			job_ptr->details->begin_time = now + 10;

		/* Since this could happen on a launch we need to make sure the
		 * submit isn't the same as the last submit so put now + 1 so
		 * we get different records in the database */
		if (now == job_ptr->details->submit_time)
			now++;
		job_ptr->details->submit_time = now;
	}

	/* Reset this after the batch step has finished or the batch step
	 * information will be attributed to the next run of the job. */
	job_ptr->db_index = 0;
	if (!with_slurmdbd)
		jobacct_storage_g_job_start(acct_db_conn, job_ptr);
}


/* job_fini - free all memory associated with job records */
void job_fini (void)
{
	if (job_list) {
		list_destroy(job_list);
		job_list = NULL;
	}
	xfree(job_hash);
	xfree(job_array_hash_j);
	xfree(job_array_hash_t);
}

/* Record the start of one job array task */
extern void job_array_start(struct job_record *job_ptr)
{
	struct job_record *base_job_ptr;

	if ((job_ptr->array_task_id != NO_VAL) || job_ptr->array_recs) {
		base_job_ptr = find_job_record(job_ptr->array_job_id);
		if (base_job_ptr && base_job_ptr->array_recs) {
			base_job_ptr->array_recs->tot_run_tasks++;
		}
	}
}

/* Return true if a job array task can be started */
extern bool job_array_start_test(struct job_record *job_ptr)
{
	struct job_record *base_job_ptr;
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
	if (job_ptr->details && !job_ptr->details->begin_time)
		job_ptr->details->begin_time = now;
	return true;
}

static void _job_array_comp(struct job_record *job_ptr)
{
	struct job_record *base_job_ptr;
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
			if (base_job_ptr->array_recs->tot_comp_tasks == 0) {
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
			if (base_job_ptr->array_recs->tot_run_tasks)
				base_job_ptr->array_recs->tot_run_tasks--;
			base_job_ptr->array_recs->tot_comp_tasks++;
		}
	}
}

/* log the completion of the specified job */
extern void job_completion_logger(struct job_record *job_ptr, bool requeue)
{
	int base_state;

	xassert(job_ptr);

	acct_policy_remove_job_submit(job_ptr);

	if (!IS_JOB_RESIZING(job_ptr)) {
		/* Remove configuring state just to make sure it isn't there
		 * since it will throw off displays of the job. */
		job_ptr->job_state &= (~JOB_CONFIGURING);

		/* make sure all parts of the job are notified */
		srun_job_complete(job_ptr);

		/* mail out notifications of completion */
		base_state = job_ptr->job_state & JOB_STATE_BASE;
		if ((base_state == JOB_COMPLETE) ||
		    (base_state == JOB_CANCELLED)) {
			if (requeue && (job_ptr->mail_type & MAIL_JOB_REQUEUE))
				mail_job_info(job_ptr, MAIL_JOB_REQUEUE);
			if (!requeue && (job_ptr->mail_type & MAIL_JOB_END))
				mail_job_info(job_ptr, MAIL_JOB_END);
		} else {	/* JOB_FAILED, JOB_TIMEOUT, etc. */
			if (job_ptr->mail_type & MAIL_JOB_FAIL)
				mail_job_info(job_ptr, MAIL_JOB_FAIL);
			else if (job_ptr->mail_type & MAIL_JOB_END)
				mail_job_info(job_ptr, MAIL_JOB_END);
		}
	}

	_job_array_comp(job_ptr);

	g_slurm_jobcomp_write(job_ptr);

	/* When starting the resized job everything is taken care of
	 * elsewhere, so don't call it here. */
	if (IS_JOB_RESIZING(job_ptr))
		return;

	if (!job_ptr->assoc_id) {
		slurmdb_association_rec_t assoc_rec;
		/* In case accounting enabled after starting the job */
		memset(&assoc_rec, 0, sizeof(slurmdb_association_rec_t));
		assoc_rec.acct      = job_ptr->account;
		if (job_ptr->part_ptr)
			assoc_rec.partition = job_ptr->part_ptr->name;
		assoc_rec.uid       = job_ptr->user_id;

		if (!(assoc_mgr_fill_in_assoc(acct_db_conn, &assoc_rec,
					      accounting_enforce,
					      (slurmdb_association_rec_t **)
					      &job_ptr->assoc_ptr, false))) {
			job_ptr->assoc_id = assoc_rec.id;
			/* we have to call job start again because the
			 * associd does not get updated in job complete */
			jobacct_storage_g_job_start(acct_db_conn, job_ptr);
		}
	}

	if (!with_slurmdbd && !job_ptr->db_index)
		jobacct_storage_g_job_start(acct_db_conn, job_ptr);

	jobacct_storage_g_job_complete(acct_db_conn, job_ptr);
}

/*
 * job_independent - determine if this job has a dependent job pending
 *	or if the job's scheduled begin time is in the future
 * IN job_ptr - pointer to job being tested
 * RET - true if job no longer must be deferred for another job
 */
extern bool job_independent(struct job_record *job_ptr, int will_run)
{
	struct job_details *detail_ptr = job_ptr->details;
	time_t now = time(NULL);
	int depend_rc;

	if ((job_ptr->state_reason == WAIT_HELD)
	    || (job_ptr->state_reason == WAIT_HELD_USER)
	    || job_ptr->state_reason == WAIT_MAX_REQUEUE
	    || job_ptr->state_reason == WAIT_DEP_INVALID)
		return false;

	/* Check for maximum number of running tasks in a job array */
	if (!job_array_start_test(job_ptr))
		return false;

	/* Test dependencies first so we can cancel jobs before dependent
	 * job records get purged (e.g. afterok, afternotok) */
	depend_rc = test_job_dependency(job_ptr);
	if (depend_rc == 1) {
		job_ptr->state_reason = WAIT_DEPENDENCY;
		xfree(job_ptr->state_desc);
		return false;
	} else if (depend_rc == 2) {
		char jbuf[JBUFSIZ];

		if (kill_invalid_dep) {
			_kill_dependent(job_ptr);
		} else {
			debug("%s: %s dependency condition never satisfied",
			      __func__, jobid2str(job_ptr, jbuf));
			job_ptr->state_reason = WAIT_DEP_INVALID;
			xfree(job_ptr->state_desc);
		}
		return false;
	}

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

	/* Job is eligible to start now */
	if (job_ptr->state_reason == WAIT_DEPENDENCY) {
		job_ptr->state_reason = WAIT_NO_REASON;
		xfree(job_ptr->state_desc);
	}
	if ((detail_ptr && (detail_ptr->begin_time == 0) &&
	    (job_ptr->priority != 0))) {
		detail_ptr->begin_time = now;
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
 * RET SLURM error code
 */
extern int job_node_ready(uint32_t job_id, int *ready)
{
	int rc;
	struct job_record *job_ptr;
	xassert(ready);

	*ready = 0;
	job_ptr = find_job_record(job_id);
	if (job_ptr == NULL)
		return ESLURM_INVALID_JOB_ID;

	/* Always call select_g_job_ready() so that select/bluegene can
	 * test and update block state information. */
	rc = select_g_job_ready(job_ptr);
	if (rc == READY_JOB_FATAL)
		return ESLURM_INVALID_PARTITION_NAME;
	if (rc == READY_JOB_ERROR)
		return EAGAIN;
	if (rc)
		rc = READY_NODE_STATE;

	if (job_ptr->details && job_ptr->details->prolog_running)
		rc &= (~READY_NODE_STATE);

	if (IS_JOB_RUNNING(job_ptr) || IS_JOB_SUSPENDED(job_ptr))
		rc |= READY_JOB_STATE;
	if ((rc == (READY_NODE_STATE | READY_JOB_STATE)) &&
	    job_ptr->alias_list && !strcmp(job_ptr->alias_list, "TBD") &&
	    job_ptr->node_bitmap &&
	    (bit_overlap(power_node_bitmap, job_ptr->node_bitmap) == 0)) {
		job_ptr->job_state &= (~JOB_CONFIGURING);
		set_job_alias_list(job_ptr);
	}

	*ready = rc;
	return SLURM_SUCCESS;
}

/* Send specified signal to all steps associated with a job */
static void _signal_job(struct job_record *job_ptr, int signal)
{
#ifndef HAVE_FRONT_END
	int i;
#endif
	agent_arg_t *agent_args = NULL;
	signal_job_msg_t *signal_job_msg = NULL;
	static int notify_srun_static = -1;
	int notify_srun = 0;

	if (notify_srun_static == -1) {
		char *launch_type = slurm_get_launch_type();
		/* do this for all but slurm (poe, aprun, etc...) */
		if (strcmp(launch_type, "launch/slurm"))
			notify_srun_static = 1;
		else
			notify_srun_static = 0;
		xfree(launch_type);
	}

#ifdef HAVE_FRONT_END
	/* On a front end system always notify_srun instead of slurmd */
	if (notify_srun_static)
		notify_srun = 1;
#else
	/* For launch/poe all signals are forwarded by srun to poe to tasks
	 * except SIGSTOP/SIGCONT, which are used for job preemption. In that
	 * case the slurmd must directly suspend tasks and switch resources. */
	if (notify_srun_static && (signal != SIGSTOP) && (signal != SIGCONT))
		notify_srun = 1;
#endif

	if (notify_srun) {
		ListIterator step_iterator;
		struct step_record *step_ptr;
		step_iterator = list_iterator_create(job_ptr->step_list);
		while ((step_ptr = list_next(step_iterator))) {
			/* Since we have already checked the uid,
			 * we can send this signal as uid 0. */
			job_step_signal(job_ptr->job_id, step_ptr->step_id,
					signal, 0);
		}
		list_iterator_destroy (step_iterator);

		return;
	}

	agent_args = xmalloc(sizeof(agent_arg_t));
	agent_args->msg_type = REQUEST_SIGNAL_JOB;
	agent_args->retry = 1;
	agent_args->hostlist = hostlist_create(NULL);
	signal_job_msg = xmalloc(sizeof(kill_tasks_msg_t));
	signal_job_msg->job_id = job_ptr->job_id;
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
	for (i = 0; i < node_record_count; i++) {
		if (bit_test(job_ptr->node_bitmap, i) == 0)
			continue;
		if (agent_args->protocol_version >
		    node_record_table_ptr[i].protocol_version)
			agent_args->protocol_version =
				node_record_table_ptr[i].protocol_version;
		hostlist_push_host(agent_args->hostlist,
			      node_record_table_ptr[i].name);
		agent_args->node_count++;
	}
#endif

	if (agent_args->node_count == 0) {
		xfree(signal_job_msg);
		xfree(agent_args);
		return;
	}

	agent_args->msg_args = signal_job_msg;
	agent_queue_request(agent_args);
	return;
}

static void *_switch_suspend_info(struct job_record *job_ptr)
{
	ListIterator step_iterator;
	struct step_record *step_ptr;
	void *switch_suspend_info = NULL;

	step_iterator = list_iterator_create (job_ptr->step_list);
	while ((step_ptr = (struct step_record *) list_next (step_iterator))) {
		if (step_ptr->state != JOB_RUNNING)
			continue;
		switch_g_job_suspend_info_get(step_ptr->switch_job,
					      &switch_suspend_info);
	}
	list_iterator_destroy (step_iterator);

	return switch_suspend_info;
}

/* Send suspend request to slumrd of all nodes associated with a job
 * job_ptr IN - job to be suspended or resumed
 * op IN - SUSPEND_JOB or RESUME_JOB
 * indf_susp IN - set if job is being suspended indefinitely by user
 *                or admin, otherwise suspended for gang scheduling
 */
static void _suspend_job(struct job_record *job_ptr, uint16_t op,
			 bool indf_susp)
{
#ifndef HAVE_FRONT_END
	int i;
#endif
	agent_arg_t *agent_args;
	suspend_int_msg_t *sus_ptr;

	agent_args = xmalloc(sizeof(agent_arg_t));
	agent_args->msg_type = REQUEST_SUSPEND_INT;
	agent_args->retry = 0;	/* don't resend, gang scheduler
				 * sched/wiki or sched/wiki2 can
				 * quickly induce huge backlog
				 * of agent.c RPCs */
	agent_args->hostlist = hostlist_create(NULL);
	sus_ptr = xmalloc(sizeof(suspend_int_msg_t));
	sus_ptr->job_core_spec = job_ptr->details->core_spec;
	sus_ptr->job_id = job_ptr->job_id;
	sus_ptr->op = op;
	sus_ptr->indf_susp = indf_susp;
	sus_ptr->switch_info = _switch_suspend_info(job_ptr);

#ifdef HAVE_FRONT_END
	xassert(job_ptr->batch_host);
	if (job_ptr->front_end_ptr)
		agent_args->protocol_version =
			job_ptr->front_end_ptr->protocol_version;
	hostlist_push_host(agent_args->hostlist, job_ptr->batch_host);
	agent_args->node_count = 1;
#else
	agent_args->protocol_version = SLURM_PROTOCOL_VERSION;
	for (i = 0; i < node_record_count; i++) {
		if (bit_test(job_ptr->node_bitmap, i) == 0)
			continue;
		if (agent_args->protocol_version >
		    node_record_table_ptr[i].protocol_version)
			agent_args->protocol_version =
				node_record_table_ptr[i].protocol_version;
		hostlist_push_host(agent_args->hostlist,
				   node_record_table_ptr[i].name);
		agent_args->node_count++;
	}
#endif

	if (agent_args->node_count == 0) {
		slurm_free_suspend_int_msg(sus_ptr);
		xfree(agent_args);
		return;
	}

	agent_args->msg_args = sus_ptr;
	agent_queue_request(agent_args);
	return;
}

/*
 * Specified job is being suspended, release allocated nodes
 * job_ptr IN - job to be suspended
 * indf_susp IN - set if job is being suspended indefinitely by user
 *                or admin, otherwise suspended for gang scheduling
 */
static int _suspend_job_nodes(struct job_record *job_ptr, bool indf_susp)
{
	int i, rc = SLURM_SUCCESS;
	struct node_record *node_ptr = node_record_table_ptr;
	uint32_t node_flags;
	time_t now = time(NULL);

	if ((rc = select_g_job_suspend(job_ptr, indf_susp)) != SLURM_SUCCESS)
		return rc;

	for (i=0; i<node_record_count; i++, node_ptr++) {
		if (bit_test(job_ptr->node_bitmap, i) == 0)
			continue;

		node_ptr->sus_job_cnt++;
		if (node_ptr->run_job_cnt)
			(node_ptr->run_job_cnt)--;
		else {
			error("Node %s run_job_cnt underflow",
				node_ptr->name);
		}
		if (job_ptr->details && (job_ptr->details->share_res == 0)) {
			if (node_ptr->no_share_job_cnt)
				(node_ptr->no_share_job_cnt)--;
			else {
				error("Node %s no_share_job_cnt "
					"underflow", node_ptr->name);
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
			debug3("_suspend_job_nodes: Node %s left DOWN",
				node_ptr->name);
		} else if (node_ptr->run_job_cnt) {
			node_ptr->node_state = NODE_STATE_ALLOCATED |
					       node_flags;
		} else {
			node_ptr->node_state = NODE_STATE_IDLE | node_flags;
			node_ptr->last_idle  = now;
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
static int _resume_job_nodes(struct job_record *job_ptr, bool indf_susp)
{
	int i, rc = SLURM_SUCCESS;
	struct node_record *node_ptr = node_record_table_ptr;
	uint32_t node_flags;

	if ((rc = select_g_job_resume(job_ptr, indf_susp)) != SLURM_SUCCESS)
		return rc;

	for (i=0; i<node_record_count; i++, node_ptr++) {
		if (bit_test(job_ptr->node_bitmap, i) == 0)
			continue;
		if (IS_NODE_DOWN(node_ptr))
			return SLURM_ERROR;
	}

	node_ptr = node_record_table_ptr;
	for (i=0; i<node_record_count; i++, node_ptr++) {
		if (bit_test(job_ptr->node_bitmap, i) == 0)
			continue;

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
		bit_clear(idle_node_bitmap, i);
		node_flags = node_ptr->node_state & NODE_STATE_FLAGS;
		node_ptr->node_state = NODE_STATE_ALLOCATED | node_flags;
	}
	last_job_update = last_node_update = time(NULL);
	return rc;
}

static int _job_suspend_switch_test(struct job_record *job_ptr)
{
	int rc = SLURM_SUCCESS;
	ListIterator step_iterator;
	struct step_record *step_ptr;

	step_iterator = list_iterator_create(job_ptr->step_list);
	while ((step_ptr = (struct step_record *) list_next (step_iterator))) {
		if (step_ptr->state != JOB_RUNNING)
			continue;
		rc = switch_g_job_suspend_test(step_ptr->switch_job);
		if (rc != SLURM_SUCCESS)
			break;
	}
	list_iterator_destroy (step_iterator);

	return rc;
}

/*
 * Determine if a job can be resumed.
 * Check for multiple jobs on the same nodes with core specialization.
 * RET 0 on success, otherwise ESLURM error code
 */
static int _job_resume_test(struct job_record *job_ptr)
{
	int rc = SLURM_SUCCESS;
	ListIterator job_iterator;
	struct job_record *test_job_ptr;

	if ((job_ptr->details == NULL) ||
	    (job_ptr->details->core_spec == (uint16_t) NO_VAL) ||
	    (job_ptr->node_bitmap == NULL))
		return rc;

	job_iterator = list_iterator_create(job_list);
	while ((test_job_ptr = (struct job_record *) list_next(job_iterator))) {
		if (test_job_ptr->details &&
		    (test_job_ptr->details->core_spec != (uint16_t) NO_VAL) &&
		    IS_JOB_RUNNING(test_job_ptr) &&
		    test_job_ptr->node_bitmap &&
		    bit_overlap(test_job_ptr->node_bitmap,
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
 * _job_suspend - perform some suspend/resume operation
 * job_ptr - job to operate upon
 * op IN - operation: suspend/resume
 * indf_susp IN - set if job is being suspended indefinitely by user or admin
 *                and we should clear it's priority, otherwise suspended
 *		  temporarily for gang scheduling
 * RET 0 on success, otherwise ESLURM error code
 */
static int _job_suspend(struct job_record *job_ptr, uint16_t op, bool indf_susp)
{
	int rc = SLURM_SUCCESS;
	time_t now = time(NULL);

	if (IS_JOB_PENDING(job_ptr))
		return ESLURM_JOB_PENDING;
	if (IS_JOB_FINISHED(job_ptr))
		return ESLURM_ALREADY_DONE;
	if ((op == SUSPEND_JOB) &&
	    (_job_suspend_switch_test(job_ptr) != SLURM_SUCCESS))
		return ESLURM_NOT_SUPPORTED;
	if ((op == RESUME_JOB) && (rc = _job_resume_test(job_ptr)))
		return rc;

	/* Notify salloc/srun of suspend/resume */
	srun_job_suspend(job_ptr, op);

	/* perform the operation */
	if (op == SUSPEND_JOB) {
		if (IS_JOB_SUSPENDED(job_ptr) && indf_susp) {
			job_ptr->priority = 0;	/* Prevent gang sched resume */
			return SLURM_SUCCESS;
		}
		if (!IS_JOB_RUNNING(job_ptr))
			return ESLURM_JOB_NOT_RUNNING;
		rc = _suspend_job_nodes(job_ptr, indf_susp);
		if (rc != SLURM_SUCCESS)
			return rc;
		_suspend_job(job_ptr, op, indf_susp);
		job_ptr->job_state = JOB_SUSPENDED;
		if (indf_susp)
			job_ptr->priority = 0;
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
		_suspend_job(job_ptr, op, indf_susp);
		if (job_ptr->priority == 0)
			set_job_prio(job_ptr);
		job_ptr->job_state = JOB_RUNNING;
		job_ptr->tot_sus_time +=
			difftime(now, job_ptr->suspend_time);
		if (!wiki_sched_test) {
			char *sched_type = slurm_get_sched_type();
			if (strcmp(sched_type, "sched/wiki") == 0)
				wiki_sched  = true;
			if (strcmp(sched_type, "sched/wiki2") == 0) {
				wiki_sched  = true;
				wiki2_sched = true;
			}
			xfree(sched_type);
			wiki_sched_test = true;
		}
		if ((job_ptr->time_limit != INFINITE) && (!wiki2_sched) &&
		    (!job_ptr->preempt_time)) {
			debug3("Job %u resumed, updating end_time",
			       job_ptr->job_id);
			job_ptr->end_time = now + (job_ptr->time_limit * 60)
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
 * job_suspend - perform some suspend/resume operation
 * NB job_suspend  - Uses the job_id field and ignores job_id_str
 *
 * IN sus_ptr - suspend/resume request message
 * IN uid - user id of the user issuing the RPC
 * IN conn_fd - file descriptor on which to send reply,
 *              -1 if none
 * indf_susp IN - set if job is being suspended indefinitely by user or admin
 *                and we should clear it's priority, otherwise suspended
 *		  temporarily for gang scheduling
 * IN protocol_version - slurm protocol version of client
 * RET 0 on success, otherwise ESLURM error code
 */
extern int job_suspend(suspend_msg_t *sus_ptr, uid_t uid,
		       slurm_fd_t conn_fd, bool indf_susp,
		       uint16_t protocol_version)
{
	int rc = SLURM_SUCCESS;
	struct job_record *job_ptr = NULL;
	slurm_msg_t resp_msg;
	return_code_msg_t rc_msg;

	xfree(sus_ptr->job_id_str);
	xstrfmtcat(sus_ptr->job_id_str, "%u", sus_ptr->job_id);

#ifdef HAVE_BG
	rc = ESLURM_NOT_SUPPORTED;
	goto reply;
#endif

	/* validate the request */
	if ((uid != 0) && (uid != getuid())) {
		error("SECURITY VIOLATION: Attempt to suspend job from user %u",
		      (int) uid);
		rc = ESLURM_ACCESS_DENIED;
		goto reply;
	}

	/* find the job */
	job_ptr = find_job_record (sus_ptr->job_id);
	if (job_ptr == NULL) {
		rc = ESLURM_INVALID_JOB_ID;
		goto reply;
	}

	rc = _job_suspend(job_ptr, sus_ptr->op, indf_susp);

    reply:

	/* Since we have already used it lets make sure we don't leak
	   memory */
	xfree(sus_ptr->job_id_str);

	if (conn_fd >= 0) {
		slurm_msg_t_init(&resp_msg);
		resp_msg.protocol_version = protocol_version;
		resp_msg.msg_type  = RESPONSE_SLURM_RC;
		rc_msg.return_code = rc;
		resp_msg.data      = &rc_msg;
		slurm_send_node_msg(conn_fd, &resp_msg);
	}
	return rc;
}

/*
 * job_suspend2 - perform some suspend/resume operation
 * NB job_suspend2 - Ignores the job_id field and uses job_id_str
 *
 * IN sus_ptr - suspend/resume request message
 * IN uid - user id of the user issuing the RPC
 * IN conn_fd - file descriptor on which to send reply,
 *              -1 if none
 * indf_susp IN - set if job is being suspended indefinitely by user or admin
 *                and we should clear it's priority, otherwise suspended
 *		  temporarily for gang scheduling
 * IN protocol_version - slurm protocol version of client
 * RET 0 on success, otherwise ESLURM error code
 */
extern int job_suspend2(suspend_msg_t *sus_ptr, uid_t uid,
			slurm_fd_t conn_fd, bool indf_susp,
			uint16_t protocol_version)
{
	slurm_ctl_conf_t *conf;
	int rc = SLURM_SUCCESS, rc2;
	struct job_record *job_ptr = NULL;
	long int long_id;
	uint32_t job_id = 0;
	char *end_ptr = NULL, *tok, *tmp;
	bitstr_t *array_bitmap = NULL;
	bool valid = true;
	int32_t i, i_first, i_last;
	slurm_msg_t resp_msg;
	return_code_msg_t rc_msg;
	resp_array_struct_t *resp_array = NULL;
	job_array_resp_msg_t *resp_array_msg = NULL;

#ifdef HAVE_BG
	rc = ESLURM_NOT_SUPPORTED;
	goto reply;
#endif

	if (max_array_size == NO_VAL) {
		conf = slurm_conf_lock();
		max_array_size = conf->max_array_sz;
		slurm_conf_unlock();
	}

	/* validate the request */
	if ((uid != 0) && (uid != getuid())) {
		error("SECURITY VIOLATION: Attempt to suspend job from user %u",
		      (int) uid);
		rc = ESLURM_ACCESS_DENIED;
		goto reply;
	}

	long_id = strtol(sus_ptr->job_id_str, &end_ptr, 10);
	if ((long_id <= 0) || (long_id == LONG_MAX) ||
	    ((end_ptr[0] != '\0') && (end_ptr[0] != '_'))) {
		info("job_suspend2: invalid job id %s", sus_ptr->job_id_str);
		rc = ESLURM_INVALID_JOB_ID;
		goto reply;
	}
	job_id = (uint32_t) long_id;
	if (end_ptr[0] == '\0') {	/* Single job (or full job array) */
		struct job_record *job_ptr_done = NULL;
		job_ptr = find_job_record(job_id);
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
			_resp_array_add(&resp_array, job_ptr, rc2);
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
				_resp_array_add(&resp_array, job_ptr, rc2);
			}
			job_ptr = job_ptr->job_array_next_j;
		}
		goto reply;
	}

	array_bitmap = bit_alloc(max_array_size);
	tmp = xstrdup(end_ptr + 1);
	tok = strtok_r(tmp, ",", &end_ptr);
	while (tok && valid) {
		valid = _parse_array_tok(tok, array_bitmap,
					 max_array_size);
		tok = strtok_r(NULL, ",", &end_ptr);
	}
	xfree(tmp);
	if (valid) {
		i_last = bit_fls(array_bitmap);
		if (i_last < 0)
			valid = false;
	}
	if (!valid) {
		info("job_suspend2: invalid job id %s", sus_ptr->job_id_str);
		rc = ESLURM_INVALID_JOB_ID;
		goto reply;
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
			info("job_suspend2: invalid job id %u_%d", job_id, i);
			_resp_array_add_id(&resp_array, job_id, i,
					   ESLURM_INVALID_JOB_ID);
			continue;
		}
		rc2 = _job_suspend(job_ptr, sus_ptr->op, indf_susp);
		_resp_array_add(&resp_array, job_ptr, rc2);
	}

    reply:
	if (conn_fd >= 0) {
		slurm_msg_t_init(&resp_msg);
		resp_msg.protocol_version = protocol_version;
		if (resp_array) {
			resp_array_msg = _resp_array_xlate(resp_array, job_id);
			resp_msg.msg_type  = RESPONSE_JOB_ARRAY_ERRORS;
			resp_msg.data      = resp_array_msg;
		} else {
			resp_msg.msg_type  = RESPONSE_SLURM_RC;
			rc_msg.return_code = rc;
			resp_msg.data      = &rc_msg;
		}
		slurm_send_node_msg(conn_fd, &resp_msg);

		if (resp_array_msg) {
			slurm_free_job_array_resp(resp_array_msg);
			resp_msg.data = NULL;
		}
	}
	_resp_array_free(resp_array);

	FREE_NULL_BITMAP(array_bitmap);

	return rc;
}

/*
 * _job_requeue - Requeue a running or pending batch job
 * IN uid - user id of user issuing the RPC
 * IN job_ptr - job to be requeued
 * IN preempt - true if job being preempted
 * RET 0 on success, otherwise ESLURM error code
 */
static int _job_requeue(uid_t uid, struct job_record *job_ptr, bool preempt,
			uint32_t state)
{
	bool suspended = false;
	time_t now = time(NULL);
	bool is_running;

	/* validate the request */
	if ((uid != job_ptr->user_id) && !validate_operator(uid) &&
	    !assoc_mgr_is_user_acct_coord(acct_db_conn, uid,
					  job_ptr->account)) {
		return ESLURM_ACCESS_DENIED;
	}

	/* If the partition was removed don't allow the job to be
	 * requeued.  If it doesn't have details then something is very
	 * wrong and if the job doesn't want to be requeued don't.
	 */
	if (!job_ptr->part_ptr || !job_ptr->details
	    || !job_ptr->details->requeue) {
		return ESLURM_DISABLED;
	}

	/* In the job is in the process of completing
	 * return SLURM_SUCCESS and set the status
	 * to JOB_PENDING since we support requeue
	 * of done/exit/exiting jobs.
	 */
	if (IS_JOB_COMPLETING(job_ptr)) {
		uint32_t flags;
		flags = job_ptr->job_state & JOB_STATE_FLAGS;
		job_ptr->job_state = JOB_PENDING | flags;
		return SLURM_SUCCESS;
	}

	/* If the job is already pending do nothing
	 * and return  is well to the library.
	 */
	if (IS_JOB_PENDING(job_ptr))
		return ESLURM_JOB_PENDING;

	if (job_ptr->batch_flag == 0) {
		debug("Job-requeue can only be done for batch jobs");
		return ESLURM_BATCH_ONLY;
	}

	slurm_sched_g_requeue(job_ptr, "Job requeued by user/admin");
	last_job_update = now;

	if (IS_JOB_SUSPENDED(job_ptr)) {
		enum job_states suspend_job_state = job_ptr->job_state;
		/* we can't have it as suspended when we call the
		 * accounting stuff.
		 */
		job_ptr->job_state = JOB_REQUEUE;
		jobacct_storage_g_job_suspend(acct_db_conn, job_ptr);
		job_ptr->job_state = suspend_job_state;
		suspended = true;
	}

	job_ptr->time_last_active  = now;
	if (suspended)
		job_ptr->end_time = job_ptr->suspend_time;
	else
		job_ptr->end_time = now;

	/* Save the state of the job so that
	 * we deallocate the nodes if is in
	 * running state.
	 */
	is_running = false;
	if (IS_JOB_SUSPENDED(job_ptr) || IS_JOB_RUNNING(job_ptr))
		is_running = true;

	/* We want this job to have the requeued state in the
	 * accounting logs. Set a new submit time so the restarted
	 * job looks like a new job. */
	job_ptr->job_state  = JOB_REQUEUE;
	build_cg_bitmap(job_ptr);
	job_completion_logger(job_ptr, true);

	/* Deallocate resources only if the job has some.
	 * JOB_COMPLETING is needed to properly clean up steps. */
	if (is_running) {
		job_ptr->job_state |= JOB_COMPLETING;
		deallocate_nodes(job_ptr, false, suspended, preempt);
		job_ptr->job_state &= (~JOB_COMPLETING);
	}

	xfree(job_ptr->details->req_node_layout);

	/* do this after the epilog complete, setting it here is too early */
	//job_ptr->db_index = 0;
	//job_ptr->details->submit_time = now;

	job_ptr->job_state = JOB_PENDING;
	if (job_ptr->node_cnt)
		job_ptr->job_state |= JOB_COMPLETING;

	job_ptr->pre_sus_time = (time_t) 0;
	job_ptr->suspend_time = (time_t) 0;
	job_ptr->tot_sus_time = (time_t) 0;

	job_ptr->restart_cnt++;
	/* Since the job completion logger removes the submit we need
	 * to add it again. */
	acct_policy_add_job_submit(job_ptr);

	if (state & JOB_SPECIAL_EXIT) {
		job_ptr->job_state |= JOB_SPECIAL_EXIT;
		job_ptr->state_reason = WAIT_HELD_USER;
		xfree(job_ptr->state_desc);
		job_ptr->state_desc =
			xstrdup("job requeued in special exit state");
		job_ptr->priority = 0;
	}
	if (state & JOB_REQUEUE_HOLD) {
		job_ptr->state_reason = WAIT_HELD_USER;
		xfree(job_ptr->state_desc);
		job_ptr->state_desc = xstrdup("job requeued in held state");
		job_ptr->priority = 0;
	}

	debug("%s: job %u state 0x%x reason %u priority %d", __func__,
	      job_ptr->job_id, job_ptr->job_state,
	      job_ptr->state_reason, job_ptr->priority);

	return SLURM_SUCCESS;
}

/*
 * job_requeue - Requeue a running or pending batch job
 * IN uid - user id of user issuing the RPC
 * IN job_id - id of the job to be requeued
 * IN conn_fd - file descriptor on which to send reply
 * IN protocol_version - slurm protocol version of client
 * IN preempt - true if job being preempted
 * IN state - may be set to JOB_SPECIAL_EXIT and/or JOB_REQUEUE_HOLD
 * RET 0 on success, otherwise ESLURM error code
 */
extern int job_requeue(uid_t uid, uint32_t job_id,
                       slurm_fd_t conn_fd, uint16_t protocol_version,
                       bool preempt, uint32_t state)
{
	int rc = SLURM_SUCCESS;
	struct job_record *job_ptr = NULL;
	slurm_msg_t resp_msg;
	return_code_msg_t rc_msg;

	/* find the job */
	job_ptr = find_job_record(job_id);
	if (job_ptr == NULL) {
		rc = ESLURM_INVALID_JOB_ID;
	} else {
		rc = _job_requeue(uid, job_ptr, preempt, state);
	}

	if (conn_fd >= 0) {
		slurm_msg_t_init(&resp_msg);
		resp_msg.protocol_version = protocol_version;
		resp_msg.msg_type  = RESPONSE_SLURM_RC;
		rc_msg.return_code = rc;
		resp_msg.data      = &rc_msg;
		slurm_send_node_msg(conn_fd, &resp_msg);
	}
	return rc;
}

/*
 * job_requeue2 - Requeue a running or pending batch job
 * IN uid - user id of user issuing the RPC
 * IN req_ptr - request including ID of the job to be requeued
 * IN conn_fd - file descriptor on which to send reply
 * IN protocol_version - slurm protocol version of client
 * IN preempt - true if job being preempted
 * RET 0 on success, otherwise ESLURM error code
 */
extern int job_requeue2(uid_t uid, requeue_msg_t *req_ptr,
                       slurm_fd_t conn_fd, uint16_t protocol_version,
                       bool preempt)
{
	slurm_ctl_conf_t *conf;
	int rc = SLURM_SUCCESS, rc2;
	struct job_record *job_ptr = NULL;
	long int long_id;
	uint32_t job_id = 0;
	char *end_ptr = NULL, *tok, *tmp;
	bitstr_t *array_bitmap = NULL;
	bool valid = true;
	int32_t i, i_first, i_last;
	slurm_msg_t resp_msg;
	return_code_msg_t rc_msg;
	uint32_t state = req_ptr->state;
	char *job_id_str = req_ptr->job_id_str;
	resp_array_struct_t *resp_array = NULL;
	job_array_resp_msg_t *resp_array_msg = NULL;

	if (max_array_size == NO_VAL) {
		conf = slurm_conf_lock();
		max_array_size = conf->max_array_sz;
		slurm_conf_unlock();
	}

	long_id = strtol(job_id_str, &end_ptr, 10);
	if ((long_id <= 0) || (long_id == LONG_MAX) ||
	    ((end_ptr[0] != '\0') && (end_ptr[0] != '_'))) {
		info("job_requeue2: invalid job id %s", job_id_str);
		rc = ESLURM_INVALID_JOB_ID;
		goto reply;
	}
	job_id = (uint32_t) long_id;
	if (end_ptr[0] == '\0') {	/* Single job (or full job array) */
		struct job_record *job_ptr_done = NULL;
		job_ptr = find_job_record(job_id);
		if (job_ptr &&
		    (((job_ptr->array_task_id == NO_VAL) &&
		      (job_ptr->array_recs == NULL)) ||
		     ((job_ptr->array_task_id != NO_VAL) &&
		      (job_ptr->array_job_id  != job_id)))) {
			/* This is a regular job or single task of job array */
			rc = _job_requeue(uid, job_ptr, preempt, state);
			goto reply;
		}

		if (job_ptr && job_ptr->array_recs) {
			/* This is a job array */
			job_ptr_done = job_ptr;
			rc2 = _job_requeue(uid, job_ptr, preempt, state);
			_resp_array_add(&resp_array, job_ptr, rc2);
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
				rc2 = _job_requeue(uid, job_ptr, preempt,state);
				_resp_array_add(&resp_array, job_ptr, rc2);
			}
			job_ptr = job_ptr->job_array_next_j;
		}
		goto reply;
	}

	array_bitmap = bit_alloc(max_array_size);
	tmp = xstrdup(end_ptr + 1);
	tok = strtok_r(tmp, ",", &end_ptr);
	while (tok && valid) {
		valid = _parse_array_tok(tok, array_bitmap,
					 max_array_size);
		tok = strtok_r(NULL, ",", &end_ptr);
	}
	xfree(tmp);
	if (valid) {
		i_last = bit_fls(array_bitmap);
		if (i_last < 0)
			valid = false;
	}
	if (!valid) {
		info("job_requeue2: invalid job id %s", job_id_str);
		rc = ESLURM_INVALID_JOB_ID;
		goto reply;
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
			info("job_requeue2: invalid job id %u_%d", job_id, i);
			_resp_array_add_id(&resp_array, job_id, i,
					   ESLURM_INVALID_JOB_ID);
			continue;
		}

		rc2 = _job_requeue(uid, job_ptr, preempt, state);
		_resp_array_add(&resp_array, job_ptr, rc2);
	}

    reply:
	if (conn_fd >= 0) {
		slurm_msg_t_init(&resp_msg);
		resp_msg.protocol_version = protocol_version;
		if (resp_array) {
			resp_array_msg = _resp_array_xlate(resp_array, job_id);
			resp_msg.msg_type  = RESPONSE_JOB_ARRAY_ERRORS;
			resp_msg.data      = resp_array_msg;
		} else {
			resp_msg.msg_type  = RESPONSE_SLURM_RC;
			rc_msg.return_code = rc;
			resp_msg.data      = &rc_msg;
		}
		slurm_send_node_msg(conn_fd, &resp_msg);

		if (resp_array_msg) {
			slurm_free_job_array_resp(resp_array_msg);
			resp_msg.data = NULL;
		}
	}
	_resp_array_free(resp_array);

	FREE_NULL_BITMAP(array_bitmap);

	return rc;
}

/*
 * job_end_time - Process JOB_END_TIME
 * IN time_req_msg - job end time request
 * OUT timeout_msg - job timeout response to be sent
 * RET SLURM_SUCESS or an error code
 */
extern int job_end_time(job_alloc_info_msg_t *time_req_msg,
			srun_timeout_msg_t *timeout_msg)
{
	struct job_record *job_ptr;
	xassert(timeout_msg);

	job_ptr = find_job_record(time_req_msg->job_id);
	if (!job_ptr)
		return ESLURM_INVALID_JOB_ID;

	timeout_msg->job_id  = time_req_msg->job_id;
	timeout_msg->step_id = NO_VAL;
	timeout_msg->timeout = job_ptr->end_time;
	return SLURM_SUCCESS;
}

/* Reset nodes_completing field for all jobs.
 * Job write lock must be set before calling. */
extern void update_job_nodes_completing(void)
{
	ListIterator job_iterator;
	struct job_record *job_ptr;

	if (!job_list)
		return;

	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		if ((!IS_JOB_COMPLETING(job_ptr)) ||
		    (job_ptr->node_bitmap == NULL))
			continue;
		xfree(job_ptr->nodes_completing);
		if (job_ptr->node_bitmap_cg) {
			job_ptr->nodes_completing =
				bitmap2node_name(job_ptr->node_bitmap_cg);
		} else {
			job_ptr->nodes_completing =
				bitmap2node_name(job_ptr->node_bitmap);
		}
	}
	list_iterator_destroy(job_iterator);
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
	ListIterator job_iterator;
	struct job_record *job_ptr;
	/* Write lock on jobs */
	slurmctld_lock_t job_write_lock =
		{ NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK };

	if (!job_list)
		return cnt;

	lock_slurmctld(job_write_lock);
	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		if (job_ptr->assoc_id != assoc_id)
			continue;

		/* move up to the parent that should still exist */
		if (job_ptr->assoc_ptr) {
			/* Force a start so the association doesn't
			   get lost.  Since there could be some delay
			   in the start of the job when running with
			   the slurmdbd.
			*/
			if (!job_ptr->db_index) {
				jobacct_storage_g_job_start(acct_db_conn,
							    job_ptr);
			}

			job_ptr->assoc_ptr =
				((slurmdb_association_rec_t *)
				 job_ptr->assoc_ptr)->usage->parent_assoc_ptr;
			if (job_ptr->assoc_ptr)
				job_ptr->assoc_id =
					((slurmdb_association_rec_t *)
					 job_ptr->assoc_ptr)->id;
		}

		if (IS_JOB_FINISHED(job_ptr))
			continue;

		info("Association deleted, holding job %u",
		     job_ptr->job_id);
		xfree(job_ptr->state_desc);
		job_ptr->state_reason = FAIL_ACCOUNT;
		cnt++;
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
	ListIterator job_iterator;
	struct job_record *job_ptr;
	/* Write lock on jobs */
	slurmctld_lock_t job_write_lock =
		{ NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK };

	if (!job_list)
		return cnt;

	lock_slurmctld(job_write_lock);
	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		if (job_ptr->qos_id != qos_id)
			continue;

		/* move up to the parent that should still exist */
		if (job_ptr->qos_ptr) {
			/* Force a start so the association doesn't
			   get lost.  Since there could be some delay
			   in the start of the job when running with
			   the slurmdbd.
			*/
			if (!job_ptr->db_index) {
				jobacct_storage_g_job_start(acct_db_conn,
							    job_ptr);
			}
			job_ptr->qos_ptr = NULL;
		}

		if (IS_JOB_FINISHED(job_ptr))
			continue;

		info("QOS deleted, holding job %u", job_ptr->job_id);
		xfree(job_ptr->state_desc);
		job_ptr->state_reason = FAIL_QOS;
		cnt++;
	}
	list_iterator_destroy(job_iterator);
	unlock_slurmctld(job_write_lock);
	return cnt;
}

/*
 * Modify the account associated with a pending job
 * IN module - where this is called from
 * IN job_ptr - pointer to job which should be modified
 * IN new_account - desired account name
 * RET SLURM_SUCCESS or error code
 */
extern int update_job_account(char *module, struct job_record *job_ptr,
			      char *new_account)
{
	slurmdb_association_rec_t assoc_rec;

	if ((!IS_JOB_PENDING(job_ptr)) || (job_ptr->details == NULL)) {
		info("%s: attempt to modify account for non-pending "
		     "job_id %u", module, job_ptr->job_id);
		return ESLURM_JOB_NOT_PENDING;
	}


	memset(&assoc_rec, 0, sizeof(slurmdb_association_rec_t));
	assoc_rec.acct      = new_account;
	if (job_ptr->part_ptr)
		assoc_rec.partition = job_ptr->part_ptr->name;
	assoc_rec.uid       = job_ptr->user_id;
	if (assoc_mgr_fill_in_assoc(acct_db_conn, &assoc_rec,
				    accounting_enforce,
				    (slurmdb_association_rec_t **)
				    &job_ptr->assoc_ptr, false)) {
		info("%s: invalid account %s for job_id %u",
		     module, new_account, job_ptr->job_id);
		return ESLURM_INVALID_ACCOUNT;
	} else if (association_based_accounting &&
		   !job_ptr->assoc_ptr          &&
		   !(accounting_enforce & ACCOUNTING_ENFORCE_ASSOCS)) {
		/* if not enforcing associations we want to look for
		 * the default account and use it to avoid getting
		 * trash in the accounting records.
		 */
		assoc_rec.acct = NULL;
		assoc_mgr_fill_in_assoc(acct_db_conn, &assoc_rec,
					accounting_enforce,
					(slurmdb_association_rec_t **)
					&job_ptr->assoc_ptr, false);
		if (!job_ptr->assoc_ptr) {
			debug("%s: we didn't have an association for account "
			      "'%s' and user '%u', and we can't seem to find "
			      "a default one either.  Keeping new account "
			      "'%s'.  This will produce trash in accounting.  "
			      "If this is not what you desire please put "
			      "AccountStorageEnforce=associations "
			      "in your slurm.conf "
			      "file.", module, new_account,
			      job_ptr->user_id, new_account);
			assoc_rec.acct = new_account;
		}
	}

	xfree(job_ptr->account);
	if (assoc_rec.acct && assoc_rec.acct[0] != '\0') {
		job_ptr->account = xstrdup(assoc_rec.acct);
		info("%s: setting account to %s for job_id %u",
		     module, assoc_rec.acct, job_ptr->job_id);
	} else {
		info("%s: cleared account for job_id %u",
		     module, job_ptr->job_id);
	}
	job_ptr->assoc_id = assoc_rec.id;

	last_job_update = time(NULL);

	return SLURM_SUCCESS;
}

/*
 * Modify the account associated with a pending job
 * IN module - where this is called from
 * IN job_ptr - pointer to job which should be modified
 * IN new_wckey - desired wckey name
 * RET SLURM_SUCCESS or error code
 */
extern int update_job_wckey(char *module, struct job_record *job_ptr,
			    char *new_wckey)
{
	slurmdb_wckey_rec_t wckey_rec, *wckey_ptr;

	if ((!IS_JOB_PENDING(job_ptr)) || (job_ptr->details == NULL)) {
		info("%s: attempt to modify account for non-pending "
		     "job_id %u", module, job_ptr->job_id);
		return ESLURM_JOB_NOT_PENDING;
	}

	memset(&wckey_rec, 0, sizeof(slurmdb_wckey_rec_t));
	wckey_rec.uid       = job_ptr->user_id;
	wckey_rec.name      = new_wckey;
	if (assoc_mgr_fill_in_wckey(acct_db_conn, &wckey_rec,
				    accounting_enforce, &wckey_ptr)) {
		info("%s: invalid wckey %s for job_id %u",
		     module, new_wckey, job_ptr->job_id);
		return ESLURM_INVALID_WCKEY;
	} else if (association_based_accounting
		  && !wckey_ptr
		  && !(accounting_enforce & ACCOUNTING_ENFORCE_WCKEYS)) {
		/* if not enforcing associations we want to look for
		   the default account and use it to avoid getting
		   trash in the accounting records.
		*/
		wckey_rec.name = NULL;
		assoc_mgr_fill_in_wckey(acct_db_conn, &wckey_rec,
					accounting_enforce, &wckey_ptr);
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
		info("%s: setting wckey to %s for job_id %u",
		     module, wckey_rec.name, job_ptr->job_id);
	} else {
		info("%s: cleared wckey for job_id %u",
		     module, job_ptr->job_id);
	}

	last_job_update = time(NULL);

	return SLURM_SUCCESS;
}

extern int send_jobs_to_accounting(void)
{
	ListIterator itr = NULL;
	struct job_record *job_ptr;
	slurmctld_lock_t job_write_lock = {
		NO_LOCK, WRITE_LOCK, READ_LOCK, READ_LOCK };

	/* send jobs in pending or running state */
	lock_slurmctld(job_write_lock);
	itr = list_iterator_create(job_list);
	while ((job_ptr = list_next(itr))) {
		if (!job_ptr->assoc_id) {
			slurmdb_association_rec_t assoc_rec;
			memset(&assoc_rec, 0,
			       sizeof(slurmdb_association_rec_t));
			assoc_rec.acct      = job_ptr->account;
			if (job_ptr->part_ptr)
				assoc_rec.partition = job_ptr->part_ptr->name;
			assoc_rec.uid       = job_ptr->user_id;

			if (assoc_mgr_fill_in_assoc(
				   acct_db_conn, &assoc_rec,
				   accounting_enforce,
				   (slurmdb_association_rec_t **)
				   &job_ptr->assoc_ptr, false) &&
			    (accounting_enforce & ACCOUNTING_ENFORCE_ASSOCS)
			    && (!IS_JOB_FINISHED(job_ptr))) {
				info("Holding job %u with "
				     "invalid association",
				     job_ptr->job_id);
				xfree(job_ptr->state_desc);
				job_ptr->state_reason = FAIL_ACCOUNT;
				continue;
			} else
				job_ptr->assoc_id = assoc_rec.id;
		}

		/* we only want active, un accounted for jobs */
		if (job_ptr->db_index || IS_JOB_FINISHED(job_ptr))
			continue;

		debug("first reg: starting job %u in accounting",
		      job_ptr->job_id);
		jobacct_storage_g_job_start(acct_db_conn, job_ptr);

		if (IS_JOB_SUSPENDED(job_ptr))
			jobacct_storage_g_job_suspend(acct_db_conn, job_ptr);
	}
	list_iterator_destroy(itr);
	unlock_slurmctld(job_write_lock);

	return SLURM_SUCCESS;
}

/* Perform checkpoint operation on a job */
extern int job_checkpoint(checkpoint_msg_t *ckpt_ptr, uid_t uid,
			  slurm_fd_t conn_fd, uint16_t protocol_version)
{
	int rc = SLURM_SUCCESS;
	struct job_record *job_ptr;
	struct step_record *step_ptr;
	checkpoint_resp_msg_t resp_data;
	slurm_msg_t resp_msg;

	slurm_msg_t_init(&resp_msg);
	resp_msg.protocol_version = protocol_version;

	/* find the job */
	job_ptr = find_job_record (ckpt_ptr->job_id);
	if (job_ptr == NULL) {
		rc = ESLURM_INVALID_JOB_ID;
		goto reply;
	}
	if ((uid != job_ptr->user_id) && !validate_slurm_user(uid)) {
		rc = ESLURM_ACCESS_DENIED ;
		goto reply;
	}
	if (IS_JOB_PENDING(job_ptr)) {
		rc = ESLURM_JOB_PENDING;
		goto reply;
	} else if (IS_JOB_SUSPENDED(job_ptr)) {
		/* job can't get cycles for checkpoint
		 * if it is already suspended */
		rc = ESLURM_JOB_SUSPENDED;
		goto reply;
	} else if (!IS_JOB_RUNNING(job_ptr)) {
		rc = ESLURM_ALREADY_DONE;
		goto reply;
	}

	memset((void *)&resp_data, 0, sizeof(checkpoint_resp_msg_t));

	if (job_ptr->batch_flag) { /* operate on batch job */
		if ((ckpt_ptr->op == CHECK_CREATE)  ||
		    (ckpt_ptr->op == CHECK_REQUEUE) ||
		    (ckpt_ptr->op == CHECK_VACATE)) {
			if (job_ptr->details == NULL) {
				rc = ESLURM_DISABLED;
				goto reply;
			}
			if (ckpt_ptr->image_dir == NULL) {
				if (job_ptr->details->ckpt_dir == NULL) {
					rc = ESLURM_DISABLED;
					goto reply;
				}
				ckpt_ptr->image_dir = xstrdup(job_ptr->details
							      ->ckpt_dir);
			}

			rc = _checkpoint_job_record(job_ptr,
						    ckpt_ptr->image_dir);
			if (rc != SLURM_SUCCESS)
				goto reply;
		}
		/* append job id to ckpt image dir */
		xstrfmtcat(ckpt_ptr->image_dir, "/%u", job_ptr->job_id);
		rc = checkpoint_op(ckpt_ptr->job_id, ckpt_ptr->step_id, NULL,
				   ckpt_ptr->op, ckpt_ptr->data,
				   ckpt_ptr->image_dir, &resp_data.event_time,
				   &resp_data.error_code,
				   &resp_data.error_msg);
		info("checkpoint_op %u of %u.%u complete, rc=%d",
		     ckpt_ptr->op, ckpt_ptr->job_id, ckpt_ptr->step_id, rc);
		last_job_update = time(NULL);
	} else {		/* operate on all of a job's steps */
		int update_rc = -2;
		ListIterator step_iterator;

		step_iterator = list_iterator_create (job_ptr->step_list);
		while ((step_ptr = (struct step_record *)
					list_next (step_iterator))) {
			char *image_dir = NULL;
			if (step_ptr->state != JOB_RUNNING)
				continue;
			if (ckpt_ptr->image_dir) {
				image_dir = xstrdup(ckpt_ptr->image_dir);
			} else {
				image_dir = xstrdup(step_ptr->ckpt_dir);
			}
			xstrfmtcat(image_dir, "/%u.%u", job_ptr->job_id,
				   step_ptr->step_id);
			update_rc = checkpoint_op(ckpt_ptr->job_id,
						  step_ptr->step_id,
						  step_ptr,
						  ckpt_ptr->op,
						  ckpt_ptr->data,
						  image_dir,
						  &resp_data.event_time,
						  &resp_data.error_code,
						  &resp_data.error_msg);
			info("checkpoint_op %u of %u.%u complete, rc=%d",
			     ckpt_ptr->op, ckpt_ptr->job_id,
			     step_ptr->step_id, rc);
			rc = MAX(rc, update_rc);
			xfree(image_dir);
		}
		if (update_rc != -2)	/* some work done */
			last_job_update = time(NULL);
		list_iterator_destroy (step_iterator);
	}

    reply:
	if (conn_fd < 0)	/* periodic checkpoint */
		return rc;

	if ((rc == SLURM_SUCCESS) &&
	    ((ckpt_ptr->op == CHECK_ABLE) || (ckpt_ptr->op == CHECK_ERROR))) {
		resp_msg.msg_type = RESPONSE_CHECKPOINT;
		resp_msg.data = &resp_data;
		(void) slurm_send_node_msg(conn_fd, &resp_msg);
	} else {
		return_code_msg_t rc_msg;
		rc_msg.return_code = rc;
		resp_msg.msg_type  = RESPONSE_SLURM_RC;
		resp_msg.data      = &rc_msg;
		(void) slurm_send_node_msg(conn_fd, &resp_msg);
	}
	return rc;
}

/*
 * _checkpoint_job_record - save job to file for checkpoint
 *
 */
static int _checkpoint_job_record (struct job_record *job_ptr, char *image_dir)
{
	static int high_buffer_size = (1024*1024);
	char *ckpt_file = NULL, *old_file = NULL, *new_file = NULL;
	int ckpt_fd, error_code = SLURM_SUCCESS;
	Buf buffer = init_buf(high_buffer_size);

	ckpt_file = xstrdup(slurmctld_conf.job_ckpt_dir);
	xstrfmtcat(ckpt_file, "/%u.ckpt", job_ptr->job_id);

	debug("_checkpoint_job_record: checkpoint job record of %u to file %s",
	      job_ptr->job_id, ckpt_file);

	old_file = xstrdup(ckpt_file);
	xstrcat(old_file, ".old");

	new_file = xstrdup(ckpt_file);
	xstrcat(new_file, ".new");

	/* save version string */
	packstr(JOB_CKPT_VERSION, buffer);
	pack16(SLURM_PROTOCOL_VERSION, buffer);

	/* save checkpoint image directory */
	packstr(image_dir, buffer);

	_pack_job_for_ckpt(job_ptr, buffer);

	ckpt_fd = creat(new_file, 0600);
	if (ckpt_fd < 0) {
		error("Can't ckpt job, create file %s error: %m",
		      new_file);
		error_code = errno;
	} else {
		int pos = 0, nwrite = get_buf_offset(buffer), amount, rc;
		char *data = (char *)get_buf_data(buffer);
		while (nwrite > 0) {
			amount = write(ckpt_fd, &data[pos], nwrite);
			if ((amount < 0) && (errno != EINTR)) {
				error("Error writing file %s, %m", new_file);
				error_code = errno;
				break;
			} else if (amount >= 0) {
				nwrite -= amount;
				pos    += amount;
			}
		}

		rc = fsync_and_close(ckpt_fd, "checkpoint");
		if (rc && !error_code)
			error_code = rc;
	}
	if (error_code)
		(void) unlink(new_file);
	else {			/* file shuffle */
		(void) unlink(old_file);
		if (link(ckpt_file, old_file))
			debug4("unable to create link for %s -> %s: %m",
			       ckpt_file, old_file);
		(void) unlink(ckpt_file);
		if (link(new_file, ckpt_file))
			debug4("unable to create link for %s -> %s: %m",
			       new_file, ckpt_file);
		(void) unlink(new_file);
	}

	xfree(ckpt_file);
	xfree(old_file);
	xfree(new_file);
	free_buf(buffer);

	return error_code;
}

/*
 * _pack_job_for_ckpt - save RUNNING job to buffer for checkpoint
 *
 *   Just save enough information to restart it
 *
 * IN job_ptr - id of the job to be checkpointed
 * IN buffer - buffer to save the job state
 */
static void _pack_job_for_ckpt (struct job_record *job_ptr, Buf buffer)
{
	slurm_msg_t msg;
	job_desc_msg_t *job_desc;

	/* save allocated nodes */
	packstr(job_ptr->nodes, buffer);

	/* save job req */
	job_desc = _copy_job_record_to_job_desc(job_ptr);
	msg.msg_type = REQUEST_SUBMIT_BATCH_JOB;
	msg.protocol_version = SLURM_PROTOCOL_VERSION;
	msg.data = job_desc;
	pack_msg(&msg, buffer);

	/* free the environment since all strings are stored in one
	 * xmalloced buffer */
	if (job_desc->environment) {
		xfree(job_desc->environment[0]);
		xfree(job_desc->environment);
		job_desc->env_size = 0;
	}
	slurm_free_job_desc_msg(job_desc);
}

/*
 * _copy_job_record_to_job_desc - construct a job_desc_msg_t for a job.
 * IN job_ptr - the job record
 * RET the job_desc_msg_t, NULL on error
 */
static job_desc_msg_t *
_copy_job_record_to_job_desc(struct job_record *job_ptr)
{
	job_desc_msg_t *job_desc;
	struct job_details *details = job_ptr->details;
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
	job_desc->argv              = xmalloc(sizeof(char *) * job_desc->argc);
	for (i = 0; i < job_desc->argc; i ++)
		job_desc->argv[i]   = xstrdup(details->argv[i]);
	job_desc->begin_time        = details->begin_time;
	job_desc->ckpt_interval     = job_ptr->ckpt_interval;
	job_desc->ckpt_dir          = xstrdup(details->ckpt_dir);
	job_desc->comment           = xstrdup(job_ptr->comment);
	job_desc->contiguous        = details->contiguous;
	job_desc->core_spec         = details->core_spec;
	job_desc->cpu_bind          = xstrdup(details->cpu_bind);
	job_desc->cpu_bind_type     = details->cpu_bind_type;
	job_desc->dependency        = xstrdup(details->dependency);
	job_desc->end_time          = 0; /* Unused today */
	job_desc->environment       = get_job_env(job_ptr,
						  &job_desc->env_size);
	job_desc->exc_nodes         = xstrdup(details->exc_nodes);
	job_desc->features          = xstrdup(details->features);
	job_desc->gres              = xstrdup(job_ptr->gres);
	job_desc->group_id          = job_ptr->group_id;
	job_desc->immediate         = 0; /* nowhere to get this value */
	job_desc->job_id            = job_ptr->job_id;
	job_desc->kill_on_node_fail = job_ptr->kill_on_node_fail;
	job_desc->licenses          = xstrdup(job_ptr->licenses);
	job_desc->mail_type         = job_ptr->mail_type;
	job_desc->mail_user         = xstrdup(job_ptr->mail_user);
	job_desc->mem_bind          = xstrdup(details->mem_bind);
	job_desc->mem_bind_type     = details->mem_bind_type;
	job_desc->name              = xstrdup(job_ptr->name);
	job_desc->network           = xstrdup(job_ptr->network);
	job_desc->nice              = details->nice;
	job_desc->num_tasks         = details->num_tasks;
	job_desc->open_mode         = details->open_mode;
	job_desc->other_port        = job_ptr->other_port;
	job_desc->overcommit        = details->overcommit;
	job_desc->partition         = xstrdup(job_ptr->partition);
	job_desc->plane_size        = details->plane_size;
	job_desc->priority          = job_ptr->priority;
	if (job_ptr->qos_ptr) {
		slurmdb_qos_rec_t *qos_ptr =
			(slurmdb_qos_rec_t *)job_ptr->qos_ptr;
		job_desc->qos       = xstrdup(qos_ptr->name);
	}
	job_desc->resp_host         = xstrdup(job_ptr->resp_host);
	job_desc->req_nodes         = xstrdup(details->req_nodes);
	job_desc->requeue           = details->requeue;
	job_desc->reservation       = xstrdup(job_ptr->resv_name);
	job_desc->script            = get_job_script(job_ptr);
	if (details->share_res == 1)
		job_desc->shared     = 1;
	else if (details->whole_node)
		job_desc->shared     = 0;
	else
		job_desc->shared     = (uint16_t) NO_VAL;
	job_desc->spank_job_env_size = job_ptr->spank_job_env_size;
	job_desc->spank_job_env      = xmalloc(sizeof(char *) *
					       job_desc->spank_job_env_size);
	for (i = 0; i < job_desc->spank_job_env_size; i ++)
		job_desc->spank_job_env[i]= xstrdup(job_ptr->spank_job_env[i]);
	job_desc->std_err           = xstrdup(details->std_err);
	job_desc->std_in            = xstrdup(details->std_in);
	job_desc->std_out           = xstrdup(details->std_out);
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
#if 0
	/* select_jobinfo is unused at job submit time, only it's
	 * components are set. We recover those from the structure below.
	 * job_desc->select_jobinfo = select_g_select_jobinfo_copy(job_ptr->
							    select_jobinfo); */

	/* The following fields are used only on BlueGene systems.
	 * Since BlueGene does not use the checkpoint/restart logic today,
	 * we do not them. */
	select_g_select_jobinfo_get(job_ptr->select_jobinfo,
				    SELECT_JOBDATA_GEOMETRY,
				    &job_desc->geometry);
	select_g_select_jobinfo_get(job_ptr->select_jobinfo,
				    SELECT_JOBDATA_CONN_TYPE,
				    &job_desc->conn_type);
	select_g_select_jobinfo_get(job_ptr->select_jobinfo,
				    SELECT_JOBDATA_REBOOT,
				    &job_desc->reboot);
	select_g_select_jobinfo_get(job_ptr->select_jobinfo,
				    SELECT_JOBDATA_ROTATE,
				    &job_desc->rotate);
	select_g_select_jobinfo_get(job_ptr->select_jobinfo,
				    SELECT_JOBDATA_BLRTS_IMAGE,
				    &job_desc->blrtsimage);
	select_g_select_jobinfo_get(job_ptr->select_jobinfo,
				    SELECT_JOBDATA_LINUX_IMAGE,
				    &job_desc->linuximage);
	select_g_select_jobinfo_get(job_ptr->select_jobinfo,
				    SELECT_JOBDATA_MLOADER_IMAGE,
				    &job_desc->mloaderimage);
	select_g_select_jobinfo_get(job_ptr->select_jobinfo,
				    SELECT_JOBDATA_RAMDISK_IMAGE,
				    &job_desc->ramdiskimage);
#endif

	return job_desc;
}


/*
 * job_restart - Restart a batch job from checkpointed state
 *
 * Restarting a job is similar to submitting a new job, except that
 * the job requirements are loaded from the checkpoint file, and
 * the job id is restored.
 *
 * IN ckpt_ptr - checkpoint request message
 * IN uid - user id of the user issuing the RPC
 * IN conn_fd - file descriptor on which to send reply
 * IN protocol_version - slurm protocol version of client
 * RET 0 on success, otherwise ESLURM error code
 */
extern int job_restart(checkpoint_msg_t *ckpt_ptr, uid_t uid, slurm_fd_t conn_fd,
		       uint16_t protocol_version)
{
	struct job_record *job_ptr;
	char *image_dir, *ckpt_file, *data, *ver_str = NULL;
	char *alloc_nodes = NULL;
	int data_size = 0;
	Buf buffer;
	uint32_t tmp_uint32;
	slurm_msg_t msg, resp_msg;
	return_code_msg_t rc_msg;
	job_desc_msg_t *job_desc = NULL;
	int rc = SLURM_SUCCESS;
	uint16_t ckpt_version = (uint16_t) NO_VAL;

	if (ckpt_ptr->step_id != SLURM_BATCH_SCRIPT) {
		rc = ESLURM_NOT_SUPPORTED;
		goto reply;
	}

	if ((job_ptr = find_job_record(ckpt_ptr->job_id)) &&
	    ! IS_JOB_FINISHED(job_ptr)) {
		rc = ESLURM_JOB_NOT_FINISHED;
		goto reply;
	}

	ckpt_file = xstrdup(slurmctld_conf.job_ckpt_dir);
	xstrfmtcat(ckpt_file, "/%u.ckpt", ckpt_ptr->job_id);

	data = _read_job_ckpt_file(ckpt_file, &data_size);
	xfree(ckpt_file);

	if (data == NULL) {
		rc = errno;
		xfree (ckpt_file);
		goto reply;
	}
	buffer = create_buf(data, data_size);

	/* unpack version string */
	safe_unpackstr_xmalloc(&ver_str, &tmp_uint32, buffer);
	debug3("Version string in job_ckpt header is %s", ver_str);
	if (ver_str) {
		if (!strcmp(ver_str, JOB_CKPT_VERSION))
			safe_unpack16(&ckpt_version, buffer);
		else
			ckpt_version = SLURM_2_6_PROTOCOL_VERSION;
	}

	if (ckpt_version == (uint16_t)NO_VAL) {
		error("***************************************************");
		error("Can not restart from job ckpt, incompatible version");
		error("***************************************************");
		rc = EINVAL;
		goto unpack_error;
	}

	/* unpack checkpoint image directory */
	safe_unpackstr_xmalloc(&image_dir, &tmp_uint32, buffer);

	/* unpack the allocated nodes */
	safe_unpackstr_xmalloc(&alloc_nodes, &tmp_uint32, buffer);

	/* unpack the job req */
	msg.msg_type = REQUEST_SUBMIT_BATCH_JOB;
	msg.protocol_version = ckpt_version;
	if (unpack_msg(&msg, buffer) != SLURM_SUCCESS)
		goto unpack_error;

	job_desc = msg.data;

	/* sanity check */
	if (job_desc->job_id != ckpt_ptr->job_id) {
		error("saved job id(%u) is different from required job id(%u)",
		      job_desc->job_id, ckpt_ptr->job_id);
		rc = EINVAL;
		goto unpack_error;
	}
	if (!validate_slurm_user(uid) && (job_desc->user_id != uid)) {
		error("Security violation, user %u not allowed to restart "
		      "job %u of user %u",
		      uid, ckpt_ptr->job_id, job_desc->user_id);
		rc = EPERM;
		goto unpack_error;
	}

	if (ckpt_ptr->data == 1) { /* stick to nodes */
		xfree(job_desc->req_nodes);
		job_desc->req_nodes = alloc_nodes;
		alloc_nodes = NULL;	/* Nothing left to xfree */
	}

	/* set open mode to append */
	job_desc->open_mode = OPEN_MODE_APPEND;

	/* Set new job priority */
	job_desc->priority = NO_VAL;

	/*
	 * XXX: we set submit_uid to 0 in the following job_allocate() call
	 * This is for setting the job_id to the original one.
	 * But this will bypass some partition access permission checks.
	 * TODO: fix this.
	 */
	rc = job_allocate(job_desc,
			  0,		/* immediate */
			  0,		/* will_run */
			  NULL,		/* resp */
			  0,		/* allocate */
			  0,		/* submit_uid. set to 0 to set job_id */
			  &job_ptr, NULL, SLURM_PROTOCOL_VERSION);

	/* set restart directory */
	if (job_ptr) {
		if (ckpt_ptr->image_dir) {
			xfree (image_dir);
			image_dir = xstrdup(ckpt_ptr->image_dir);
		}
		xstrfmtcat(image_dir, "/%u", ckpt_ptr->job_id);

		job_ptr->details->restart_dir = image_dir;
		image_dir = NULL;	/* Nothing left to xfree */

		last_job_update = time(NULL);
	}

 unpack_error:
	free_buf(buffer);
	xfree(ver_str);
	xfree(image_dir);
	xfree(alloc_nodes);
	xfree(ckpt_file);

 reply:
	slurm_msg_t_init(&resp_msg);
	resp_msg.protocol_version = protocol_version;
	rc_msg.return_code = rc;
	resp_msg.msg_type  = RESPONSE_SLURM_RC;
	resp_msg.data      = &rc_msg;
	(void) slurm_send_node_msg(conn_fd, &resp_msg);

	return rc;
}

static char *
_read_job_ckpt_file(char *ckpt_file, int *size_ptr)
{
	int ckpt_fd, error_code = 0;
	int data_allocated, data_read, data_size = 0;
	char *data = NULL;

	ckpt_fd = open(ckpt_file, O_RDONLY);
	if (ckpt_fd < 0) {
		info("No job ckpt file (%s) to read", ckpt_file);
		error_code = ENOENT;
	} else {
		data_allocated = BUF_SIZE;
		data = xmalloc(data_allocated);
		while (1) {
			data_read = read(ckpt_fd, &data[data_size],
					 BUF_SIZE);
			if (data_read < 0) {
				if (errno == EINTR)
					continue;
				else {
					error("Read error on %s: %m",
					      ckpt_file);
					error_code = errno;
					break;
				}
			} else if (data_read == 0)	/* eof */
				break;
			data_size      += data_read;
			data_allocated += data_read;
			xrealloc(data, data_allocated);
		}
		close(ckpt_fd);
	}

	if (error_code) {
		xfree(data);
		return NULL;
	}
	*size_ptr = data_size;
	return data;
}

/* Build a bitmap of nodes completing this job */
extern void build_cg_bitmap(struct job_record *job_ptr)
{
	FREE_NULL_BITMAP(job_ptr->node_bitmap_cg);
	if (job_ptr->node_bitmap) {
		job_ptr->node_bitmap_cg = bit_copy(job_ptr->node_bitmap);
		if (bit_set_count(job_ptr->node_bitmap_cg) == 0)
			job_ptr->job_state &= (~JOB_COMPLETING);
	} else {
		error("build_cg_bitmap: node_bitmap is NULL");
		job_ptr->node_bitmap_cg = bit_alloc(node_record_count);
		job_ptr->job_state &= (~JOB_COMPLETING);
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
 */
extern void job_hold_requeue(struct job_record *job_ptr)
{
	uint32_t state;
	uint32_t flags;

	xassert(job_ptr);

	/* If the job is already pending it was
	 * eventually requeued somewhere else.
	 */
	if (IS_JOB_PENDING(job_ptr))
		return;

	/* Check if the job exit with one of the
	 * configured requeue values.
	 */
	_set_job_requeue_exit_value(job_ptr);

	state = job_ptr->job_state;

	if (! (state & JOB_REQUEUE))
		return;

	/* Sent event requeue to the database.
	 */
	jobacct_storage_g_job_complete(acct_db_conn, job_ptr);

	debug("%s: job %u state 0x%x", __func__, job_ptr->job_id, state);

	/* Set the job pending
	 */
	flags = job_ptr->job_state & JOB_STATE_FLAGS;
	job_ptr->job_state = JOB_PENDING | flags;
	job_ptr->restart_cnt++;

	/* Test if user wants to requeue the job
	 * in hold or with a special exit value.
	 */
	if (state & JOB_SPECIAL_EXIT) {
		/* JOB_SPECIAL_EXIT means requeue the
		 * the job, put it on hold and display
		 * it as JOB_SPECIAL_EXIT.
		 */
		job_ptr->job_state |= JOB_SPECIAL_EXIT;
		job_ptr->state_reason = WAIT_HELD_USER;
		job_ptr->priority = 0;
	}

	job_ptr->job_state &= ~JOB_REQUEUE;

	debug("%s: job %u state 0x%x reason %u priority %d", __func__,
	      job_ptr->job_id, job_ptr->job_state,
	      job_ptr->state_reason, job_ptr->priority);
}

/* init_requeue_policy()
 * Initialize the requeue exit/hold arrays.
 */
void
init_requeue_policy(void)
{
	char *sched_params;

	/* clean first as we can be reconfiguring
	 */
	num_exit = 0;
	xfree(requeue_exit);
	num_hold = 0;
	xfree(requeue_exit_hold);

	requeue_exit = _make_requeue_array(slurmctld_conf.requeue_exit,
					   &num_exit);
	requeue_exit_hold = _make_requeue_array(
		slurmctld_conf.requeue_exit_hold, &num_hold);
	/* Check if users want to kill a job whose dependency
	 * can never be satisfied.
	 */
	kill_invalid_dep = false;
	sched_params = slurm_get_sched_params();
	if (sched_params) {
		if (strstr(sched_params, "kill_invalid_depend"))
			kill_invalid_dep = true;
		xfree(sched_params);
	}

	info("%s: kill_invalid_depend is set to %d",
	     __func__, kill_invalid_dep);
}

/* _make_requeue_array()
 *
 * Process the RequeueExit|RequeueExitHold configuration
 * parameters creating two arrays holding the exit values
 * of jobs for which they have to be requeued.
 */
static int32_t *
_make_requeue_array(char *conf_buf, uint32_t *num)
{
	char *p;
	char *p0;
	char cnum[12];
	int cc;
	int n;
	int32_t *ar;

	if (conf_buf == NULL) {
		*num = 0;
		return NULL;
	}

	info("%s: exit values: %s", __func__, conf_buf);

	p0 = p = xstrdup(conf_buf);
	/* First tokenize the string removing ,
	 */
	for (cc = 0; p[cc] != 0; cc++) {
		if (p[cc] == ',')
			p[cc] = ' ';
	}

	/* Count the number of exit values
	 */
	cc = 0;
	while (sscanf(p, "%s%n", cnum, &n) != EOF) {
		++cc;
		p += n;
	}

	ar = xmalloc(cc * sizeof(int));

	cc = 0;
	p = p0;
	while (sscanf(p, "%s%n", cnum, &n) != EOF) {
		ar[cc] = atoi(cnum);
		++cc;
		p += n;
	}

	*num = cc;
	xfree(p0);

	return ar;
}

/* _set_job_requeue_exit_value()
 *
 * Compared the job exit values with the configured
 * RequeueExit and RequeueHoldExit and it mach is
 * found set the appropriate state for job_hold_requeue()
 * If RequeueExit or RequeueExitHold are not defined
 * the mum_exit and num_hold are zero.
 *
 */
static void
_set_job_requeue_exit_value(struct job_record *job_ptr)
{
	int cc;
	int exit_code;

	/* Search the arrays for a matching value
	 * based on the job exit code
	 */
	exit_code = WEXITSTATUS(job_ptr->exit_code);
	for (cc = 0; cc < num_exit; cc++) {
		if (exit_code == requeue_exit[cc]) {
			debug2("%s: job %d exit code %d state JOB_REQUEUE",
			       __func__, job_ptr->job_id, exit_code);
			job_ptr->job_state |= JOB_REQUEUE;
			return;
		}
	}

	for (cc = 0; cc < num_hold; cc++) {
		if (exit_code == requeue_exit_hold[cc]) {
			/* Bah... not sure if want to set special
			 * exit state in this case, but for sure
			 * don't want another array...
			 */
			debug2("%s: job %d exit code %d state JOB_SPECIAL_EXIT",
			       __func__, job_ptr->job_id, exit_code);
			job_ptr->job_state |= JOB_REQUEUE;
			job_ptr->job_state |= JOB_SPECIAL_EXIT;
			return;
		}
	}
}

/* Reset a job's end_time based upon it's start_time and time_limit.
 * NOTE: Do not reset the end_time if already being preempted */
extern void job_end_time_reset(struct job_record  *job_ptr)
{
	if (job_ptr->preempt_time)
		return;
	if (job_ptr->time_limit == INFINITE) {
		job_ptr->end_time = job_ptr->start_time +
				    (365 * 24 * 60 * 60); /* secs in year */
	} else {
		job_ptr->end_time = job_ptr->start_time +
				    (job_ptr->time_limit * 60);	/* secs */
	}
}

/*
 * jobid2str() - print all the parts that uniquely identify a job.
 */
extern char *
jobid2str(struct job_record *job_ptr, char *buf)
{
	if (job_ptr == NULL)
		return "jobid2str: Invalid job_ptr argument";
	if (buf == NULL)
		return "jobid2str: Invalid buf argument";

	if (job_ptr->array_recs && (job_ptr->array_task_id == NO_VAL)) {
		sprintf(buf, "JobID=%u_* State=0x%x NodeCnt=%u",
			job_ptr->job_id, job_ptr->job_state,
			job_ptr->node_cnt);
	} else if (job_ptr->array_task_id == NO_VAL) {
		sprintf(buf, "JobID=%u State=0x%x NodeCnt=%u",
			job_ptr->job_id, job_ptr->job_state,
			job_ptr->node_cnt);
	} else {
		sprintf(buf, "JobID=%u_%u(%u) State=0x%x NodeCnt=%u",
			job_ptr->array_job_id, job_ptr->array_task_id,
			job_ptr->job_id, job_ptr->job_state,job_ptr->node_cnt);
	}

       return buf;
}

/* trace_job() - print the job details if
 *               the DEBUG_FLAG_TRACE_JOBS is set
 */
extern void
trace_job(struct job_record *job_ptr, const char *func, const char *extra)
{
	char jbuf[JBUFSIZ];

	if (slurmctld_conf.debug_flags & DEBUG_FLAG_TRACE_JOBS)
		info("%s: %s job %s", func, extra, jobid2str(job_ptr, jbuf));
}

/* If this is a job array meta-job, prepare it for being scheduled */
extern void job_array_pre_sched(struct job_record *job_ptr)
{
	int32_t i;

	if (!job_ptr->array_recs || !job_ptr->array_recs->task_id_bitmap)
		return;

	i = bit_ffs(job_ptr->array_recs->task_id_bitmap);
	if (i < 0) {
		error("job %u has empty task_id_bitmap", job_ptr->job_id);
		FREE_NULL_BITMAP(job_ptr->array_recs->task_id_bitmap);
		return;
	}

	job_ptr->array_job_id  = job_ptr->job_id;
	job_ptr->array_task_id = i;
}

/* If this is a job array meta-job, clean up after scheduling attempt */
extern void job_array_post_sched(struct job_record *job_ptr)
{
	struct job_record *new_job_ptr;

	if (!job_ptr->array_recs || !job_ptr->array_recs->task_id_bitmap)
		return;

	if (job_ptr->array_recs->task_cnt <= 1) {
		/* Preserve array_recs for min/max exit codes for job array */
		if (job_ptr->array_recs->task_cnt) {
			job_ptr->array_recs->task_cnt--;
		} else if (job_ptr->restart_cnt) {
			/* Last task of a job array has been requeued */
		} else {
			error("job %u_%u array_recs task count underflow",
			      job_ptr->array_job_id, job_ptr->array_task_id);
		}
		xfree(job_ptr->array_recs->task_id_str);
		/* Most efficient way to update new task_id_str to accounting
		 * for pending tasks. */
		job_ptr->db_index = 0;

		/* If job is requeued, it will already be in the hash table */
		if (!find_job_array_rec(job_ptr->array_job_id,
					job_ptr->array_task_id)) {
			_add_job_array_hash(job_ptr);
		}
	} else {
		new_job_ptr = _job_rec_copy(job_ptr);
		if (new_job_ptr) {
			new_job_ptr->job_state = JOB_PENDING;
			new_job_ptr->start_time = (time_t) 0;
			/* Do NOT clear db_index here, it is handled when
			 * task_id_str is created elsewhere */
		}
	}
}

/* _copy_job_file()
 *
 * This function is invoked in case the controller fails
 * to link the job array job files. If the link fails the
 * controller tries to copy the files instead.
 *
 */
static int
_copy_job_file(const char *src, const char *dst)
{
	struct stat stat_buf;
	int fsrc;
	int fdst;
	int cc;
	char buf[BUFSIZ];

	if (stat(src, &stat_buf) < 0)
		return -1;

	fsrc = open(src, O_RDONLY);
	if (fsrc < 0)
		return -1;


	fdst = creat(dst, stat_buf.st_mode);
	if (fdst < 0) {
		close(fsrc);
		return -1;
	}

	while (1) {
		cc = read(fsrc, buf, BUFSIZ);
		if (cc == 0)
			break;
		if (cc < 0) {
			close(fsrc);
			close(fdst);
			return -1;
		}
		if (write(fdst, buf, cc) != cc) {
			close(fsrc);
			close(fdst);
			return -1;
		}
	}

	close(fsrc);
	close(fdst);

	return 0;
}

/* _kill_dependent()
 *
 * Exterminate the job that has invalid dependency
 * condition.
 */
static void
_kill_dependent(struct job_record *job_ptr)
{
	char jbuf[JBUFSIZ];
	time_t now;

	now = time(NULL);

	info("%s: Job dependency can't be satisfied, cancelling "
	     "job %s", __func__, jobid2str(job_ptr, jbuf));
	job_ptr->job_state = JOB_CANCELLED;
	xfree(job_ptr->state_desc);
	job_ptr->start_time = now;
	job_ptr->end_time = now;
	job_completion_logger(job_ptr, false);
	last_job_update = now;
	srun_allocate_abort(job_ptr);
}
