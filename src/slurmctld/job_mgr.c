/*****************************************************************************\
 *  job_mgr.c - manage the job information of slurm
 *	Note: there is a global job list (job_list), time stamp
 *	(last_job_update), and hash table (job_hash)
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Portions Copyright (C) 2010-2015 SchedMD <https://www.schedmd.com>.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
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

#include "config.h"
#define _GNU_SOURCE

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/param.h>
#include <unistd.h>

#include "slurm/slurm_errno.h"

#include "src/common/slurm_acct_gather.h"
#include "src/common/assoc_mgr.h"
#include "src/common/bitstring.h"
#include "src/common/cpu_frequency.h"
#include "src/common/fd.h"
#include "src/common/forward.h"
#include "src/common/gres.h"
#include "src/common/hostlist.h"
#include "src/common/node_features.h"
#include "src/common/node_select.h"
#include "src/common/parse_time.h"
#include "src/common/power.h"
#include "src/common/slurm_accounting_storage.h"
#include "src/common/slurm_jobcomp.h"
#include "src/common/slurm_mcs.h"
#include "src/common/slurm_priority.h"
#include "src/common/slurm_protocol_pack.h"
#include "src/common/switch.h"
#include "src/common/timers.h"
#include "src/common/xassert.h"
#include "src/common/xstring.h"

#include "src/slurmctld/acct_policy.h"
#include "src/slurmctld/agent.h"
#include "src/slurmctld/burst_buffer.h"
#include "src/slurmctld/fed_mgr.h"
#include "src/slurmctld/front_end.h"
#include "src/slurmctld/gang.h"
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
#define MAX_EXIT_VAL 255	/* Maximum value returned by WIFEXITED() */
#define SLURM_CREATE_JOB_FLAG_NO_ALLOCATE_0 0
#define TOP_PRIORITY 0xffff0000	/* large, but leave headroom for higher */

#define JOB_HASH_INX(_job_id)	(_job_id % hash_table_size)
#define JOB_ARRAY_HASH_INX(_job_id, _task_id) \
	((_job_id + _task_id) % hash_table_size)

/* No need to change we always pack SLURM_PROTOCOL_VERSION */
#define JOB_STATE_VERSION       "PROTOCOL_VERSION"

#define JOB_CKPT_VERSION      "PROTOCOL_VERSION"

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
} resp_array_struct_t;

typedef struct {
	Buf       buffer;
	uint32_t  filter_uid;
	uint32_t *jobs_packed;
	uint16_t  protocol_version;
	uint16_t  show_flags;
	uid_t     uid;
} _foreach_pack_job_info_t;

/* Global variables */
List   job_list = NULL;		/* job_record list */
time_t last_job_update;		/* time of last update to job records */

List purge_files_list = NULL;	/* job files to delete */

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
static int	select_serial = -1;

/* Local functions */
static void _add_job_hash(struct job_record *job_ptr);
static void _add_job_array_hash(struct job_record *job_ptr);
static int  _checkpoint_job_record (struct job_record *job_ptr,
				    char *image_dir);
static void _clear_job_gres_details(struct job_record *job_ptr);
static int  _copy_job_desc_to_file(job_desc_msg_t * job_desc,
				   uint32_t job_id);
static int  _copy_job_desc_to_job_record(job_desc_msg_t * job_desc,
					 struct job_record **job_ptr,
					 bitstr_t ** exc_bitmap,
					 bitstr_t ** req_bitmap);
static char *_copy_nodelist_no_dup(char *node_list);
static struct job_record *_create_job_record(uint32_t num_jobs);
static void _delete_job_details(struct job_record *job_entry);
static void _del_batch_list_rec(void *x);
static slurmdb_qos_rec_t *_determine_and_validate_qos(
	char *resv_name, slurmdb_assoc_rec_t *assoc_ptr,
	bool operator, slurmdb_qos_rec_t *qos_rec, int *error_code,
	bool locked);
static void _dump_job_details(struct job_details *detail_ptr, Buf buffer);
static void _dump_job_state(struct job_record *dump_job_ptr, Buf buffer);
static void _dump_job_fed_details(job_fed_details_t *fed_details_ptr,
				  Buf buffer);
static job_fed_details_t *_dup_job_fed_details(job_fed_details_t *src);
static void _get_batch_job_dir_ids(List batch_dirs);
static void _job_array_comp(struct job_record *job_ptr, bool was_running,
			    bool requeue);
static int  _job_create(job_desc_msg_t * job_specs, int allocate, int will_run,
			struct job_record **job_rec_ptr, uid_t submit_uid,
			char **err_msg, uint16_t protocol_version);
static void _job_timed_out(struct job_record *job_ptr);
static void _kill_dependent(struct job_record *job_ptr);
static void _list_delete_job(void *job_entry);
static int  _list_find_job_old(void *job_entry, void *key);
static int  _load_job_details(struct job_record *job_ptr, Buf buffer,
			      uint16_t protocol_version);
static int  _load_job_fed_details(job_fed_details_t **fed_details_pptr,
				  Buf buffer, uint16_t protocol_version);
static int  _load_job_state(Buf buffer,	uint16_t protocol_version);
static bitstr_t *_make_requeue_array(char *conf_buf);
static uint32_t _max_switch_wait(uint32_t input_wait);
static void _notify_srun_missing_step(struct job_record *job_ptr, int node_inx,
				      time_t now, time_t node_boot_time);
static int  _open_job_state_file(char **state_file);
static time_t _get_last_job_state_write_time(void);
static void _pack_job_for_ckpt (struct job_record *job_ptr, Buf buffer);
static void _pack_default_job_details(struct job_record *job_ptr,
				      Buf buffer,
				      uint16_t protocol_version);
static void _pack_pending_job_details(struct job_details *detail_ptr,
				      Buf buffer,
				      uint16_t protocol_version);
static bool _parse_array_tok(char *tok, bitstr_t *array_bitmap, uint32_t max);
static uint64_t _part_node_lowest_mem(struct part_record *part_ptr);
static void _purge_missing_jobs(int node_inx, time_t now);
static int  _read_data_array_from_file(int fd, char *file_name, char ***data,
				       uint32_t * size,
				       struct job_record *job_ptr);
static int   _read_data_from_file(int fd, char *file_name, char **data);
static char *_read_job_ckpt_file(char *ckpt_file, int *size_ptr);
static void _remove_defunct_batch_dirs(List batch_dirs);
static void _remove_job_hash(struct job_record *job_ptr,
			     job_hash_type_t type);
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
static void _set_job_requeue_exit_value(struct job_record *job_ptr);
static void _signal_batch_job(struct job_record *job_ptr,
			      uint16_t signal,
			      uint16_t flags);
static void _signal_job(struct job_record *job_ptr, int signal, uint16_t flags);
static void _suspend_job(struct job_record *job_ptr, uint16_t op,
			 bool indf_susp);
static int  _suspend_job_nodes(struct job_record *job_ptr, bool indf_susp);
static bool _top_priority(struct job_record *job_ptr, uint32_t pack_job_offset);
static int  _valid_job_part(job_desc_msg_t * job_desc,
			    uid_t submit_uid, bitstr_t *req_bitmap,
			    struct part_record **part_pptr,
			    List part_ptr_list,
			    slurmdb_assoc_rec_t *assoc_ptr,
			    slurmdb_qos_rec_t *qos_ptr);
static int  _validate_job_desc(job_desc_msg_t * job_desc_msg, int allocate,
			       uid_t submit_uid, struct part_record *part_ptr,
			       List part_list);
static void _validate_job_files(List batch_dirs);
static bool _valid_pn_min_mem(struct job_record *job_ptr,
			      struct part_record *part_ptr);
static int  _write_data_to_file(char *file_name, char *data);
static int  _write_data_array_to_file(char *file_name, char **data,
				      uint32_t size);
static void _xmit_new_end_time(struct job_record *job_ptr);

/*
 * Functions used to manage job array responses with a separate return code
 * possible for each task ID
 */
/* Add job record to resp_array_struct_t, free with _resp_array_free() */
static void _resp_array_add(resp_array_struct_t **resp,
			    struct job_record *job_ptr, uint32_t rc)
{
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
		max_array_size = slurmctld_conf.max_array_sz;
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
 * IN num_jobs - number of jobs this record should represent
 *    = 0 - split out a job array record to its own job record
 *    = 1 - simple job OR job array with one task
 *    > 1 - job array create with the task count as num_jobs
 * RET pointer to the record or NULL if error
 * NOTE: allocates memory that should be xfreed with _list_delete_job
 */
static struct job_record *_create_job_record(uint32_t num_jobs)
{
	struct job_record  *job_ptr;
	struct job_details *detail_ptr;

	if ((job_count + num_jobs) >= slurmctld_conf.max_job_cnt) {
		error("%s: MaxJobCount limit from slurm.conf reached (%u)",
		      __func__, slurmctld_conf.max_job_cnt);
	}

	job_count += num_jobs;
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
	job_ptr->billable_tres = (double)NO_VAL;
	(void) list_append(job_list, job_ptr);

	return job_ptr;
}


/*
 * _delete_job_details - delete a job's detail record and clear it's pointer
 * IN job_entry - pointer to job_record to clear the record of
 */
static void _delete_job_details(struct job_record *job_entry)
{
	int i;

	if (job_entry->details == NULL)
		return;

	xassert (job_entry->details->magic == DETAILS_MAGIC);

	/*
	 * Queue up job to have the batch script and environment deleted.
	 * This is handled by a separate thread to limit the amount of
	 * time purge_old_job needs to spend holding locks.
	 */
	if (IS_JOB_FINISHED(job_entry)) {
		uint32_t *job_id = xmalloc(sizeof(uint32_t));
		*job_id = job_entry->job_id;
		list_enqueue(purge_files_list, job_id);
	}

	xfree(job_entry->details->acctg_freq);
	for (i=0; i<job_entry->details->argc; i++)
		xfree(job_entry->details->argv[i]);
	xfree(job_entry->details->argv);
	xfree(job_entry->details->ckpt_dir);
	xfree(job_entry->details->cpu_bind);
	FREE_NULL_LIST(job_entry->details->depend_list);
	xfree(job_entry->details->dependency);
	xfree(job_entry->details->orig_dependency);
	for (i=0; i<job_entry->details->env_cnt; i++)
		xfree(job_entry->details->env_sup[i]);
	xfree(job_entry->details->env_sup);
	xfree(job_entry->details->std_err);
	FREE_NULL_BITMAP(job_entry->details->exc_node_bitmap);
	xfree(job_entry->details->exc_nodes);
	xfree(job_entry->details->extra);
	FREE_NULL_LIST(job_entry->details->feature_list);
	xfree(job_entry->details->features);
	xfree(job_entry->details->cluster_features);
	xfree(job_entry->details->std_in);
	xfree(job_entry->details->mc_ptr);
	xfree(job_entry->details->mem_bind);
	xfree(job_entry->details->std_out);
	FREE_NULL_BITMAP(job_entry->details->req_node_bitmap);
	xfree(job_entry->details->req_nodes);
	xfree(job_entry->details->restart_dir);
	xfree(job_entry->details->work_dir);
	xfree(job_entry->details->x11_magic_cookie);
	/* no x11_target_host, it's the same as alloc_node */
	xfree(job_entry->details);	/* Must be last */
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
	struct stat sbuf;
	int hash = job_id % 10;
	DIR *f_dir;
	struct dirent *dir_ent;

	dir_name = xstrdup_printf("%s/hash.%d/job.%u",
				  slurmctld_conf.state_save_location,
				  hash, job_id);
	if (stat(dir_name, &sbuf)) {
		xfree(dir_name);
		return;
	}

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
	char *sched_params, *tmp_ptr;
	int i;

	if (sched_update != slurmctld_conf.last_update) {
		sched_update = slurmctld_conf.last_update;
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
	char *resv_name, slurmdb_assoc_rec_t *assoc_ptr,
	bool operator, slurmdb_qos_rec_t *qos_rec, int *error_code,
	bool locked)
{
	slurmdb_qos_rec_t *qos_ptr = NULL;

	/* If enforcing associations make sure this is a valid qos
	   with the association.  If not just fill in the qos and
	   continue. */

	xassert(qos_rec);

	assoc_mgr_get_default_qos_info(assoc_ptr, qos_rec);
	if (assoc_mgr_fill_in_qos(acct_db_conn, qos_rec, accounting_enforce,
				  &qos_ptr, locked) != SLURM_SUCCESS) {
		error("Invalid qos (%s)", qos_rec->name);
		*error_code = ESLURM_INVALID_QOS;
		return NULL;
	}

	if ((accounting_enforce & ACCOUNTING_ENFORCE_QOS)
	    && assoc_ptr
	    && !operator
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
		{ READ_LOCK, READ_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };
	ListIterator job_iterator;
	struct job_record *job_ptr;
	Buf buffer = init_buf(high_buffer_size);
	time_t now = time(NULL);
	time_t last_state_file_time;
	DEF_TIMERS;

	START_TIMER;
	/* Check that last state file was written at expected time.
	 * This is a check for two slurmctld daemons running at the same
	 * time in primary mode (a split-brain problem). */
	last_state_file_time = _get_last_job_state_write_time();
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
	log_fd = open(new_file, O_CREAT|O_WRONLY|O_TRUNC|O_CLOEXEC, 0600);
	if (log_fd < 0) {
		error("Can't save state, create file %s error %m",
		      new_file);
		error_code = errno;
	} else {
		int pos = 0, nwrite, amount, rc;
		char *data;

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

static int _find_resv_part(void *x, void *key)
{
	slurmctld_resv_t *resv_ptr = (slurmctld_resv_t *) x;

	if (resv_ptr->part_ptr != (struct part_record *) key)
		return 0;
	else
		return 1;	/* match */
}

/* Open the job state save file, or backup if necessary.
 * state_file IN - the name of the state save file used
 * RET the file description to read from or error code
 */
static int _open_job_state_file(char **state_file)
{
	int state_fd;
	struct stat stat_buf;

	*state_file = xstrdup_printf("%s/job_state",
				     slurmctld_conf.state_save_location);
	state_fd = open(*state_file, O_RDONLY);
	if (state_fd < 0) {
		error("Could not open job state file %s: %m", *state_file);
	} else if (fstat(state_fd, &stat_buf) < 0) {
		error("Could not stat job state file %s: %m", *state_file);
		(void) close(state_fd);
	} else if (stat_buf.st_size < 10) {
		error("Job state file %s too small", *state_file);
		(void) close(state_fd);
	} else	/* Success */
		return state_fd;

	error("NOTE: Trying backup state save file. Jobs may be lost!");
	xstrcat(*state_file, ".old");
	state_fd = open(*state_file, O_RDONLY);
	return state_fd;
}

extern void set_job_tres_req_str(struct job_record *job_ptr,
				 bool assoc_mgr_locked)
{
	assoc_mgr_lock_t locks = { NO_LOCK, NO_LOCK, NO_LOCK, NO_LOCK,
				   READ_LOCK, NO_LOCK, NO_LOCK };
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

extern void set_job_tres_alloc_str(struct job_record *job_ptr,
				   bool assoc_mgr_locked)
{
	assoc_mgr_lock_t locks = { NO_LOCK, NO_LOCK, NO_LOCK, NO_LOCK,
				   READ_LOCK, NO_LOCK, NO_LOCK };
	xassert(job_ptr);

	if (!assoc_mgr_locked)
		assoc_mgr_lock(&locks);

	xfree(job_ptr->tres_alloc_str);
	job_ptr->tres_alloc_str = assoc_mgr_make_tres_str_from_array(
		job_ptr->tres_alloc_cnt, TRES_STR_FLAG_SIMPLE, true);

	xfree(job_ptr->tres_fmt_alloc_str);
	job_ptr->tres_fmt_alloc_str = assoc_mgr_make_tres_str_from_array(
		job_ptr->tres_alloc_cnt, TRES_STR_CONVERT_UNITS, true);

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
	int data_allocated, data_read = 0, error_code = SLURM_SUCCESS;
	uint32_t data_size = 0;
	int state_fd;
	char *data, *state_file;
	Buf buffer;
	time_t buf_time = (time_t) 0;
	char *ver_str = NULL;
	uint32_t ver_str_len;
	uint16_t protocol_version = NO_VAL16;

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
		return buf_time;

	buffer = create_buf(data, data_size);
	safe_unpackstr_xmalloc(&ver_str, &ver_str_len, buffer);
	if (ver_str && !xstrcmp(ver_str, JOB_STATE_VERSION))
		safe_unpack16(&protocol_version, buffer);
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
	uint16_t protocol_version = NO_VAL16;
	assoc_mgr_lock_t locks = { READ_LOCK, NO_LOCK, NO_LOCK, NO_LOCK,
				   READ_LOCK, NO_LOCK, NO_LOCK };

	/* read the file */
	lock_state_files();
	state_fd = _open_job_state_file(&state_file);
	if (state_fd < 0) {
		info("No job state file (%s) to recover", state_file);
		xfree(state_file);
		unlock_state_files();
		return ENOENT;
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

	buffer = create_buf(data, data_size);
	safe_unpackstr_xmalloc(&ver_str, &ver_str_len, buffer);
	debug3("Version string in job_state header is %s", ver_str);
	if (ver_str && !xstrcmp(ver_str, JOB_STATE_VERSION))
		safe_unpack16(&protocol_version, buffer);
	xfree(ver_str);

	if (protocol_version == NO_VAL16) {
		if (!ignore_state_errors)
			fatal("Can not recover job state, incompatible version, start with '-i' to ignore this");
		error("***********************************************");
		error("Can not recover job state, incompatible version");
		error("***********************************************");
		free_buf(buffer);
		return EFAULT;
	}

	safe_unpack_time(&buf_time, buffer);
	safe_unpack32(&saved_job_id, buffer);
	if (saved_job_id <= slurmctld_conf.max_job_id)
		job_id_sequence = MAX(saved_job_id, job_id_sequence);
	debug3("Job id in job_state header is %u", saved_job_id);

	assoc_mgr_lock(&locks);
	while (remaining_buf(buffer) > 0) {
		error_code = _load_job_state(buffer, protocol_version);
		if (error_code != SLURM_SUCCESS)
			goto unpack_error;
		job_cnt++;
	}
	assoc_mgr_unlock(&locks);
	debug3("Set job_id_sequence to %u", job_id_sequence);

	free_buf(buffer);
	info("Recovered information about %d jobs", job_cnt);
	return error_code;

unpack_error:
	assoc_mgr_unlock(&locks);
	if (!ignore_state_errors)
		fatal("Incomplete job state save file, start with '-i' to ignore this");
	error("Incomplete job state save file");
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
	int data_allocated, data_read = 0;
	uint32_t data_size = 0;
	int state_fd;
	char *data = NULL, *state_file;
	Buf buffer;
	time_t buf_time;
	char *ver_str = NULL;
	uint32_t ver_str_len;
	uint16_t protocol_version = NO_VAL16;

	/* read the file */
	state_file = xstrdup_printf("%s/job_state",
				    slurmctld_conf.state_save_location);
	lock_state_files();
	state_fd = open(state_file, O_RDONLY);
	if (state_fd < 0) {
		debug("No job state file (%s) to recover", state_file);
		xfree(state_file);
		unlock_state_files();
		return ENOENT;
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

	buffer = create_buf(data, data_size);
	safe_unpackstr_xmalloc(&ver_str, &ver_str_len, buffer);
	debug3("Version string in job_state header is %s", ver_str);
	if (ver_str && !xstrcmp(ver_str, JOB_STATE_VERSION))
		safe_unpack16(&protocol_version, buffer);
	xfree(ver_str);

	if (protocol_version == NO_VAL16) {
		if (!ignore_state_errors)
			fatal("Can not recover last job ID, incompatible version, start with '-i' to ignore this");
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

	xfree(ver_str);
	free_buf(buffer);
	return SLURM_SUCCESS;

unpack_error:
	if (!ignore_state_errors)
		fatal("Invalid job data checkpoint file, start with '-i' to ignore this");
	error("Invalid job data checkpoint file");
	xfree(ver_str);
	free_buf(buffer);
	return SLURM_FAILURE;
}

static void _pack_acct_policy_limit(acct_policy_limit_set_t *limit_set,
				    Buf buffer, uint16_t protocol_version)
{
	xassert(limit_set);

	pack16(limit_set->qos, buffer);
	pack16(limit_set->time, buffer);
	pack16_array(limit_set->tres, slurmctld_tres_cnt, buffer);
}

static int _unpack_acct_policy_limit_members(
	acct_policy_limit_set_t *limit_set,
	Buf buffer, uint16_t protocol_version)
{
	uint32_t tmp32;

	xassert(limit_set);

	safe_unpack16(&limit_set->qos, buffer);
	safe_unpack16(&limit_set->time, buffer);
	xfree(limit_set->tres);
	safe_unpack16_array(&limit_set->tres, &tmp32, buffer);

	return SLURM_SUCCESS;

unpack_error:
	xfree(limit_set->tres);

	return SLURM_ERROR;
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
	uint32_t tmp_32;

	xassert(dump_job_ptr->magic == JOB_MAGIC);

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
	pack32(dump_job_ptr->delay_boot, buffer);
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
	pack64(dump_job_ptr->db_index, buffer);
	pack32(dump_job_ptr->resv_id, buffer);
	pack32(dump_job_ptr->next_step_id, buffer);
	pack32(dump_job_ptr->pack_job_id, buffer);
	packstr(dump_job_ptr->pack_job_id_set, buffer);
	pack32(dump_job_ptr->pack_job_offset, buffer);
	pack32(dump_job_ptr->qos_id, buffer);
	pack32(dump_job_ptr->req_switch, buffer);
	pack32(dump_job_ptr->wait4switch, buffer);
	pack32(dump_job_ptr->profile, buffer);

	pack_time(dump_job_ptr->last_sched_eval, buffer);
	pack_time(dump_job_ptr->preempt_time, buffer);
	pack_time(dump_job_ptr->start_time, buffer);
	pack_time(dump_job_ptr->end_time, buffer);
	pack_time(dump_job_ptr->end_time_exp, buffer);
	pack_time(dump_job_ptr->suspend_time, buffer);
	pack_time(dump_job_ptr->pre_sus_time, buffer);
	pack_time(dump_job_ptr->resize_time, buffer);
	pack_time(dump_job_ptr->tot_sus_time, buffer);
	pack_time(dump_job_ptr->deadline, buffer);

	pack16(dump_job_ptr->direct_set_prio, buffer);
	pack32(dump_job_ptr->job_state, buffer);
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

	_pack_acct_policy_limit(&dump_job_ptr->limit_set, buffer,
				SLURM_PROTOCOL_VERSION);

	packstr(dump_job_ptr->state_desc, buffer);
	packstr(dump_job_ptr->resp_host, buffer);

	pack16(dump_job_ptr->alloc_resp_port, buffer);
	pack16(dump_job_ptr->other_port, buffer);
	pack8(dump_job_ptr->power_flags, buffer);
	pack16(dump_job_ptr->start_protocol_ver, buffer);
	packdouble(dump_job_ptr->billable_tres, buffer);

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
	packstr(dump_job_ptr->user_name, buffer);
	packstr(dump_job_ptr->wckey, buffer);
	packstr(dump_job_ptr->alloc_node, buffer);
	packstr(dump_job_ptr->account, buffer);
	packstr(dump_job_ptr->admin_comment, buffer);
	packstr(dump_job_ptr->comment, buffer);
	packstr(dump_job_ptr->gres, buffer);
	packstr(dump_job_ptr->gres_alloc, buffer);
	packstr(dump_job_ptr->gres_req, buffer);
	packstr(dump_job_ptr->gres_used, buffer);
	packstr(dump_job_ptr->network, buffer);
	packstr(dump_job_ptr->licenses, buffer);
	packstr(dump_job_ptr->mail_user, buffer);
	packstr(dump_job_ptr->mcs_label, buffer);
	packstr(dump_job_ptr->resv_name, buffer);
	packstr(dump_job_ptr->batch_host, buffer);
	packstr(dump_job_ptr->burst_buffer, buffer);
	packstr(dump_job_ptr->burst_buffer_state, buffer);

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
	list_for_each(dump_job_ptr->step_list, dump_job_step_state, buffer);

	pack16((uint16_t) 0, buffer);	/* no step flag */
	pack32(dump_job_ptr->bit_flags, buffer);
	packstr(dump_job_ptr->tres_alloc_str, buffer);
	packstr(dump_job_ptr->tres_fmt_alloc_str, buffer);
	packstr(dump_job_ptr->tres_req_str, buffer);
	packstr(dump_job_ptr->tres_fmt_req_str, buffer);

	packstr(dump_job_ptr->clusters, buffer);
	_dump_job_fed_details(dump_job_ptr->fed_details, buffer);

	packstr(dump_job_ptr->origin_cluster, buffer);
}

/* Unpack a job's state information from a buffer */
/* NOTE: assoc_mgr tres and assoc read lock must be locked before calling */
static int _load_job_state(Buf buffer, uint16_t protocol_version)
{
	uint64_t db_index;
	uint32_t job_id, user_id, group_id, time_limit, priority, alloc_sid;
	uint32_t exit_code, assoc_id, name_len, time_min, uint32_tmp;
	uint32_t next_step_id, total_cpus, total_nodes = 0, cpu_cnt;
	uint32_t resv_id, spank_job_env_size = 0, qos_id, derived_ec = 0;
	uint32_t array_job_id = 0, req_switch = 0, wait4switch = 0;
	uint32_t profile = ACCT_GATHER_PROFILE_NOT_SET;
	uint32_t job_state, delay_boot = 0;
	time_t start_time, end_time, end_time_exp, suspend_time,
		pre_sus_time, tot_sus_time;
	time_t preempt_time = 0, deadline = 0;
	time_t last_sched_eval = 0;
	time_t resize_time = 0, now = time(NULL);
	uint8_t reboot = 0, power_flags = 0;
	uint32_t array_task_id = NO_VAL;
	uint32_t array_flags = 0, max_run_tasks = 0, tot_run_tasks = 0;
	uint32_t min_exit_code = 0, max_exit_code = 0, tot_comp_tasks = 0;
	uint32_t pack_job_id = 0, pack_job_offset = 0;
	uint16_t details, batch_flag, step_flag;
	uint16_t kill_on_node_fail, direct_set_prio;
	uint16_t alloc_resp_port, other_port, mail_type, state_reason;
	uint16_t restart_cnt, ckpt_interval;
	uint16_t wait_all_nodes, warn_flags = 0, warn_signal, warn_time;
	acct_policy_limit_set_t limit_set;
	uint16_t start_protocol_ver = SLURM_MIN_PROTOCOL_VERSION;
	char *nodes = NULL, *partition = NULL, *name = NULL, *resp_host = NULL;
	char *account = NULL, *network = NULL, *mail_user = NULL;
	char *comment = NULL, *nodes_completing = NULL, *alloc_node = NULL;
	char *licenses = NULL, *state_desc = NULL, *wckey = NULL;
	char *resv_name = NULL, *gres = NULL, *batch_host = NULL;
	char *gres_alloc = NULL, *gres_req = NULL, *gres_used = NULL;
	char *burst_buffer = NULL, *burst_buffer_state = NULL;
	char *admin_comment = NULL, *task_id_str = NULL, *mcs_label = NULL;
	char *clusters = NULL, *pack_job_id_set = NULL, *user_name = NULL;
	uint32_t task_id_size = NO_VAL;
	char **spank_job_env = (char **) NULL;
	List gres_list = NULL, part_ptr_list = NULL;
	struct job_record *job_ptr = NULL;
	struct part_record *part_ptr;
	int error_code, i, qos_error;
	dynamic_plugin_data_t *select_jobinfo = NULL;
	job_resources_t *job_resources = NULL;
	check_jobinfo_t check_job = NULL;
	slurmdb_assoc_rec_t assoc_rec;
	slurmdb_qos_rec_t qos_rec;
	bool job_finished = false;
	char jbuf[JBUFSIZ];
	double billable_tres = (double)NO_VAL;
	char *tres_alloc_str = NULL, *tres_fmt_alloc_str = NULL,
		*tres_req_str = NULL, *tres_fmt_req_str = NULL;
	uint32_t pelog_env_size = 0;
	char **pelog_env = (char **) NULL;
	uint32_t pack_leader = 0;
	job_fed_details_t *job_fed_details = NULL;

	memset(&limit_set, 0, sizeof(acct_policy_limit_set_t));
	limit_set.tres = xmalloc(sizeof(uint16_t) * slurmctld_tres_cnt);

	if (protocol_version >= SLURM_17_11_PROTOCOL_VERSION) {
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
		safe_unpack32(&delay_boot, buffer);
		safe_unpack32(&job_id, buffer);

		/* validity test as possible */
		if (job_id == 0) {
			verbose("Invalid job_id %u", job_id);
			goto unpack_error;
		}

		job_ptr = find_job_record(job_id);
		if (job_ptr == NULL) {
			job_ptr = _create_job_record(1);
			if (!job_ptr) {
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
		safe_unpack64(&db_index, buffer);
		safe_unpack32(&resv_id, buffer);
		safe_unpack32(&next_step_id, buffer);
		safe_unpack32(&pack_job_id, buffer);
		safe_unpackstr_xmalloc(&pack_job_id_set, &name_len, buffer);
		safe_unpack32(&pack_job_offset, buffer);
		safe_unpack32(&qos_id, buffer);
		safe_unpack32(&req_switch, buffer);
		safe_unpack32(&wait4switch, buffer);
		safe_unpack32(&profile, buffer);

		safe_unpack_time(&last_sched_eval, buffer);
		safe_unpack_time(&preempt_time, buffer);
		safe_unpack_time(&start_time, buffer);
		safe_unpack_time(&end_time, buffer);
		safe_unpack_time(&end_time_exp, buffer);
		safe_unpack_time(&suspend_time, buffer);
		safe_unpack_time(&pre_sus_time, buffer);
		safe_unpack_time(&resize_time, buffer);
		safe_unpack_time(&tot_sus_time, buffer);
		safe_unpack_time(&deadline, buffer);

		safe_unpack16(&direct_set_prio, buffer);
		safe_unpack32(&job_state, buffer);
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

		_unpack_acct_policy_limit_members(&limit_set, buffer,
						  protocol_version);

		safe_unpackstr_xmalloc(&state_desc, &name_len, buffer);
		safe_unpackstr_xmalloc(&resp_host, &name_len, buffer);

		safe_unpack16(&alloc_resp_port, buffer);
		safe_unpack16(&other_port, buffer);
		safe_unpack8(&power_flags, buffer);
		safe_unpack16(&start_protocol_ver, buffer);
		safe_unpackdouble(&billable_tres, buffer);

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
			char *err_part = NULL;
			part_ptr_list = get_part_list(partition, &err_part);
			if (part_ptr_list) {
				part_ptr = list_peek(part_ptr_list);
			} else {
				verbose("Invalid partition (%s) for job_id %u",
					err_part, job_id);
				xfree(err_part);
				/* not fatal error, partition could have been
				 * removed, reset_job_bitmaps() will clean-up
				 * this job */
			}
		}

		safe_unpackstr_xmalloc(&name, &name_len, buffer);
		safe_unpackstr_xmalloc(&user_name, &name_len, buffer);
		safe_unpackstr_xmalloc(&wckey, &name_len, buffer);
		safe_unpackstr_xmalloc(&alloc_node, &name_len, buffer);
		safe_unpackstr_xmalloc(&account, &name_len, buffer);
		safe_unpackstr_xmalloc(&admin_comment, &name_len, buffer);
		safe_unpackstr_xmalloc(&comment, &name_len, buffer);
		safe_unpackstr_xmalloc(&gres, &name_len, buffer);
		safe_unpackstr_xmalloc(&gres_alloc, &name_len, buffer);
		safe_unpackstr_xmalloc(&gres_req, &name_len, buffer);
		safe_unpackstr_xmalloc(&gres_used, &name_len, buffer);
		safe_unpackstr_xmalloc(&network, &name_len, buffer);
		safe_unpackstr_xmalloc(&licenses, &name_len, buffer);
		safe_unpackstr_xmalloc(&mail_user, &name_len, buffer);
		safe_unpackstr_xmalloc(&mcs_label, &name_len, buffer);
		safe_unpackstr_xmalloc(&resv_name, &name_len, buffer);
		safe_unpackstr_xmalloc(&batch_host, &name_len, buffer);
		safe_unpackstr_xmalloc(&burst_buffer, &name_len, buffer);
		safe_unpackstr_xmalloc(&burst_buffer_state, &name_len, buffer);

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
		safe_unpack32(&job_ptr->bit_flags, buffer);
		job_ptr->bit_flags &= ~BACKFILL_TEST;
		safe_unpackstr_xmalloc(&tres_alloc_str,
				       &name_len, buffer);
		safe_unpackstr_xmalloc(&tres_fmt_alloc_str,
				       &name_len, buffer);
		safe_unpackstr_xmalloc(&tres_req_str, &name_len, buffer);
		safe_unpackstr_xmalloc(&tres_fmt_req_str, &name_len, buffer);
		safe_unpackstr_xmalloc(&clusters, &name_len, buffer);
		if ((error_code = _load_job_fed_details(&job_fed_details,
							buffer,
							protocol_version)))
			goto unpack_error;

		safe_unpackstr_xmalloc(&job_ptr->origin_cluster, &name_len,
				       buffer);

	} else if (protocol_version >= SLURM_17_02_PROTOCOL_VERSION) {
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
		safe_unpack32(&delay_boot, buffer);
		safe_unpack32(&job_id, buffer);

		/* validity test as possible */
		if (job_id == 0) {
			verbose("Invalid job_id %u", job_id);
			goto unpack_error;
		}

		job_ptr = find_job_record(job_id);
		if (job_ptr == NULL) {
			job_ptr = _create_job_record(1);
			if (!job_ptr) {
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
		safe_unpack64(&db_index, buffer);
		safe_unpack32(&resv_id, buffer);
		safe_unpack32(&next_step_id, buffer);
		safe_unpack32(&qos_id, buffer);
		safe_unpack32(&req_switch, buffer);
		safe_unpack32(&wait4switch, buffer);
		safe_unpack32(&profile, buffer);

		safe_unpack_time(&preempt_time, buffer);
		safe_unpack_time(&start_time, buffer);
		safe_unpack_time(&end_time, buffer);
		safe_unpack_time(&end_time_exp, buffer);
		safe_unpack_time(&suspend_time, buffer);
		safe_unpack_time(&pre_sus_time, buffer);
		safe_unpack_time(&resize_time, buffer);
		safe_unpack_time(&tot_sus_time, buffer);
		safe_unpack_time(&deadline, buffer);

		safe_unpack16(&direct_set_prio, buffer);
		safe_unpack32(&job_state, buffer);
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

		_unpack_acct_policy_limit_members(&limit_set, buffer,
						  protocol_version);

		safe_unpackstr_xmalloc(&state_desc, &name_len, buffer);
		safe_unpackstr_xmalloc(&resp_host, &name_len, buffer);

		safe_unpack16(&alloc_resp_port, buffer);
		safe_unpack16(&other_port, buffer);
		safe_unpack8(&power_flags, buffer);
		safe_unpack16(&start_protocol_ver, buffer);
		safe_unpackdouble(&billable_tres, buffer);

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
			char *err_part = NULL;
			part_ptr_list = get_part_list(partition, &err_part);
			if (part_ptr_list) {
				part_ptr = list_peek(part_ptr_list);
			} else {
				verbose("Invalid partition (%s) for job_id %u",
					err_part, job_id);
				xfree(err_part);
				/* not fatal error, partition could have been
				 * removed, reset_job_bitmaps() will clean-up
				 * this job */
			}
		}

		safe_unpackstr_xmalloc(&name, &name_len, buffer);
		safe_unpackstr_xmalloc(&wckey, &name_len, buffer);
		safe_unpackstr_xmalloc(&alloc_node, &name_len, buffer);
		safe_unpackstr_xmalloc(&account, &name_len, buffer);
		safe_unpackstr_xmalloc(&admin_comment, &name_len, buffer);
		safe_unpackstr_xmalloc(&comment, &name_len, buffer);
		safe_unpackstr_xmalloc(&gres, &name_len, buffer);
		safe_unpackstr_xmalloc(&gres_alloc, &name_len, buffer);
		safe_unpackstr_xmalloc(&gres_req, &name_len, buffer);
		safe_unpackstr_xmalloc(&gres_used, &name_len, buffer);
		safe_unpackstr_xmalloc(&network, &name_len, buffer);
		safe_unpackstr_xmalloc(&licenses, &name_len, buffer);
		safe_unpackstr_xmalloc(&mail_user, &name_len, buffer);
		safe_unpackstr_xmalloc(&mcs_label, &name_len, buffer);
		safe_unpackstr_xmalloc(&resv_name, &name_len, buffer);
		safe_unpackstr_xmalloc(&batch_host, &name_len, buffer);
		safe_unpackstr_xmalloc(&burst_buffer, &name_len, buffer);
		safe_unpackstr_xmalloc(&burst_buffer_state, &name_len, buffer);

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
		safe_unpack32(&job_ptr->bit_flags, buffer);
		job_ptr->bit_flags &= ~BACKFILL_TEST;
		safe_unpackstr_xmalloc(&tres_alloc_str,
				       &name_len, buffer);
		safe_unpackstr_xmalloc(&tres_fmt_alloc_str,
				       &name_len, buffer);
		safe_unpackstr_xmalloc(&tres_req_str, &name_len, buffer);
		safe_unpackstr_xmalloc(&tres_fmt_req_str, &name_len, buffer);
		safe_unpackstr_array(&pelog_env, &pelog_env_size,
				     buffer);		/* Vestigial */
		for (i = 0; i < pelog_env_size; i++)
			xfree(pelog_env[i]);
		xfree(pelog_env);
		safe_unpack32(&pack_leader, buffer);	/* Vestigial */
		safe_unpackstr_xmalloc(&clusters, &name_len, buffer);
		if ((error_code = _load_job_fed_details(&job_fed_details,
							buffer,
							protocol_version)))
			goto unpack_error;

	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
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
			job_ptr = _create_job_record(1);
			if (!job_ptr) {
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
		safe_unpack32(&uint32_tmp, buffer);
		if (uint32_tmp == NO_VAL)
			db_index = NO_VAL64;
		else
			db_index = uint32_tmp;
		safe_unpack32(&resv_id, buffer);
		safe_unpack32(&next_step_id, buffer);
		safe_unpack32(&qos_id, buffer);
		safe_unpack32(&req_switch, buffer);
		safe_unpack32(&wait4switch, buffer);
		safe_unpack32(&profile, buffer);

		safe_unpack_time(&preempt_time, buffer);
		safe_unpack_time(&start_time, buffer);
		safe_unpack_time(&end_time, buffer);
		end_time_exp = end_time;
		safe_unpack_time(&suspend_time, buffer);
		safe_unpack_time(&pre_sus_time, buffer);
		safe_unpack_time(&resize_time, buffer);
		safe_unpack_time(&tot_sus_time, buffer);
		safe_unpack_time(&deadline, buffer);

		safe_unpack16(&direct_set_prio, buffer);
		safe_unpack32(&job_state, buffer);
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

		_unpack_acct_policy_limit_members(&limit_set, buffer,
						  protocol_version);

		safe_unpackstr_xmalloc(&state_desc, &name_len, buffer);
		safe_unpackstr_xmalloc(&resp_host, &name_len, buffer);

		safe_unpack16(&alloc_resp_port, buffer);
		safe_unpack16(&other_port, buffer);
		safe_unpack8(&power_flags, buffer);
		safe_unpack16(&start_protocol_ver, buffer);
		safe_unpackdouble(&billable_tres, buffer);

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
			char *err_part = NULL;
			part_ptr_list = get_part_list(partition, &err_part);
			if (part_ptr_list) {
				part_ptr = list_peek(part_ptr_list);
			} else {
				verbose("Invalid partition (%s) for job_id %u",
					err_part, job_id);
				xfree(err_part);
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
		safe_unpackstr_xmalloc(&mcs_label, &name_len, buffer);
		safe_unpackstr_xmalloc(&resv_name, &name_len, buffer);
		safe_unpackstr_xmalloc(&batch_host, &name_len, buffer);
		safe_unpackstr_xmalloc(&burst_buffer, &name_len, buffer);

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
		safe_unpack32(&job_ptr->bit_flags, buffer);
		job_ptr->bit_flags &= ~BACKFILL_TEST;
		safe_unpackstr_xmalloc(&tres_alloc_str,
				       &name_len, buffer);
		safe_unpackstr_xmalloc(&tres_fmt_alloc_str,
				       &name_len, buffer);
		safe_unpackstr_xmalloc(&tres_req_str, &name_len, buffer);
		safe_unpackstr_xmalloc(&tres_fmt_req_str, &name_len, buffer);
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
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

#if 0
	/*
	 * This is not necessary since the job_id_sequence is checkpointed and
	 * the jobid will be checked if it's in use in get_next_job_id().
	 */

	/* Base job_id_sequence off of local job id but only if the job
	 * originated from this cluster -- so that the local job id of a
	 * different cluster isn't restored here. */
	if (!job_fed_details ||
	    !xstrcmp(job_fed_details->origin_str, slurmctld_conf.cluster_name))
		local_job_id = fed_mgr_get_local_id(job_id);
	if (job_id_sequence <= local_job_id)
		job_id_sequence = local_job_id + 1;
#endif

	xfree(job_ptr->tres_alloc_str);
	job_ptr->tres_alloc_str = tres_alloc_str;
	tres_alloc_str = NULL;

	xfree(job_ptr->tres_req_str);
	job_ptr->tres_req_str = tres_req_str;
	tres_req_str = NULL;

	xfree(job_ptr->tres_fmt_alloc_str);
	job_ptr->tres_fmt_alloc_str = tres_fmt_alloc_str;
	tres_fmt_alloc_str = NULL;

	xfree(job_ptr->tres_fmt_req_str);
	job_ptr->tres_fmt_req_str = tres_fmt_req_str;
	tres_fmt_req_str = NULL;

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
	job_ptr->delay_boot   = delay_boot;
	xfree(job_ptr->admin_comment);
	job_ptr->admin_comment = admin_comment;
	admin_comment          = NULL;  /* reused, nothing left to free */
	job_ptr->batch_flag   = batch_flag;
	xfree(job_ptr->batch_host);
	job_ptr->batch_host   = batch_host;
	batch_host            = NULL;  /* reused, nothing left to free */
	xfree(job_ptr->burst_buffer);
	job_ptr->burst_buffer = burst_buffer;
	burst_buffer          = NULL;  /* reused, nothing left to free */
	xfree(job_ptr->burst_buffer_state);
	job_ptr->burst_buffer_state = burst_buffer_state;
	burst_buffer_state    = NULL;  /* reused, nothing left to free */
	xfree(job_ptr->comment);
	job_ptr->comment      = comment;
	comment               = NULL;  /* reused, nothing left to free */
	job_ptr->billable_tres = billable_tres;
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
	job_ptr->end_time_exp = end_time_exp;
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
	xfree(job_ptr->mcs_label);
	job_ptr->mcs_label    = mcs_label;
	mcs_label	      = NULL;   /* reused, nothing left to free */
	xfree(job_ptr->name);		/* in case duplicate record */
	job_ptr->name         = name;
	name                  = NULL;	/* reused, nothing left to free */
	xfree(job_ptr->user_name);
	job_ptr->user_name    = user_name;
	user_name             = NULL;   /* reused, nothing left to free */
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
	job_ptr->power_flags  = power_flags;
	job_ptr->pack_job_id     = pack_job_id;
	xfree(job_ptr->pack_job_id_set);
	job_ptr->pack_job_id_set = pack_job_id_set;
	pack_job_id_set       = NULL;	/* reused, nothing left to free */
	job_ptr->pack_job_offset = pack_job_offset;
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
	job_ptr->deadline     = deadline;
	if (task_id_size != NO_VAL) {
		if (!job_ptr->array_recs)
			job_ptr->array_recs=xmalloc(sizeof(job_array_struct_t));
		FREE_NULL_BITMAP(job_ptr->array_recs->task_id_bitmap);
		xfree(job_ptr->array_recs->task_id_str);
		if (task_id_size) {
			job_ptr->array_recs->task_id_bitmap =
				bit_alloc(task_id_size);
			if (task_id_str) {
				bit_unfmt_hexmask(
					job_ptr->array_recs->task_id_bitmap,
					task_id_str);
				job_ptr->array_recs->task_id_str = task_id_str;
				task_id_str = NULL;
			}
			job_ptr->array_recs->task_cnt =
				bit_set_count(job_ptr->array_recs->
					      task_id_bitmap);

			if (job_ptr->array_recs->task_cnt > 1)
				job_count += (job_ptr->array_recs->task_cnt-1);
		} else
			xfree(task_id_str);
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
	job_ptr->last_sched_eval = last_sched_eval;
	job_ptr->preempt_time = preempt_time;
	job_ptr->user_id      = user_id;
	select_g_select_jobinfo_set(job_ptr->select_jobinfo,
				    SELECT_JOBDATA_USER_NAME, &user_id);
	job_ptr->wait_all_nodes = wait_all_nodes;
	job_ptr->warn_flags   = warn_flags;
	job_ptr->warn_signal  = warn_signal;
	job_ptr->warn_time    = warn_time;

	memcpy(&job_ptr->limit_set, &limit_set,
	       sizeof(acct_policy_limit_set_t));
	limit_set.tres = NULL;

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

	memset(&assoc_rec, 0, sizeof(slurmdb_assoc_rec_t));

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
				    &job_ptr->assoc_ptr, true) &&
	    (accounting_enforce & ACCOUNTING_ENFORCE_ASSOCS)
	    && (!IS_JOB_FINISHED(job_ptr))) {
		info("Holding job %u with invalid association", job_id);
		xfree(job_ptr->state_desc);
		job_ptr->state_reason = FAIL_ACCOUNT;
	} else {
		job_ptr->assoc_id = assoc_rec.id;
		info("Recovered %s Assoc=%u",
		     jobid2str(job_ptr, jbuf, sizeof(jbuf)), job_ptr->assoc_id);

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
			if (slurmctld_init_db &&
			    !(job_ptr->bit_flags & TRES_STR_CALC) &&
			    job_ptr->tres_alloc_cnt &&
			    (job_ptr->tres_alloc_cnt[TRES_ENERGY] != NO_VAL64))
				set_job_tres_alloc_str(job_ptr, false);
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
			job_ptr->limit_set.qos, &qos_rec,
			&qos_error, true);
		if ((qos_error != SLURM_SUCCESS) && !job_ptr->limit_set.qos) {
			info("Holding job %u with invalid qos", job_id);
			xfree(job_ptr->state_desc);
			job_ptr->state_reason = FAIL_QOS;
			job_ptr->qos_id = 0;
		} else
			job_ptr->qos_id = qos_rec.id;
	}

	/* do this after the format string just in case for some
	 * reason the tres_alloc_str is NULL but not the fmt_str */
	if (job_ptr->tres_alloc_str)
		assoc_mgr_set_tres_cnt_array(
			&job_ptr->tres_alloc_cnt, job_ptr->tres_alloc_str,
			0, true);
	else
		job_set_alloc_tres(job_ptr, true);

	if (job_ptr->tres_req_str)
		assoc_mgr_set_tres_cnt_array(
			&job_ptr->tres_req_cnt, job_ptr->tres_req_str, 0, true);
	else
		job_set_req_tres(job_ptr, true);

	build_node_details(job_ptr, false);	/* set node_addr */
	gres_build_job_details(job_ptr->gres_list,
			       &job_ptr->gres_detail_cnt,
			       &job_ptr->gres_detail_str);
	job_ptr->clusters     = clusters;
	job_ptr->fed_details  = job_fed_details;
	return SLURM_SUCCESS;

unpack_error:
	error("Incomplete job record");
	xfree(alloc_node);
	xfree(account);
	xfree(admin_comment);
	xfree(batch_host);
	xfree(burst_buffer);
	xfree(clusters);
	xfree(comment);
	xfree(gres);
	xfree(gres_alloc);
	xfree(gres_req);
	xfree(gres_used);
	free_job_fed_details(&job_fed_details);
	free_job_resources(&job_resources);
	xfree(resp_host);
	xfree(licenses);
	xfree(limit_set.tres);
	xfree(mail_user);
	xfree(mcs_label);
	xfree(name);
	xfree(nodes);
	xfree(nodes_completing);
	xfree(pack_job_id_set);
	xfree(partition);
	FREE_NULL_LIST(part_ptr_list);
	xfree(resv_name);
	for (i = 0; i < spank_job_env_size; i++)
		xfree(spank_job_env[i]);
	xfree(spank_job_env);
	xfree(state_desc);
	xfree(task_id_str);
	xfree(tres_alloc_str);
	xfree(tres_fmt_alloc_str);
	xfree(tres_fmt_req_str);
	xfree(tres_req_str);
	xfree(user_name);
	xfree(wckey);
	select_g_select_jobinfo_free(select_jobinfo);
	checkpoint_free_jobinfo(check_job);
	if (job_ptr) {
		if (job_ptr->job_id == 0)
			job_ptr->job_id = NO_VAL;
		purge_job_record(job_ptr->job_id);
	}
	for (i=0; i<pelog_env_size; i++)
		xfree(pelog_env[i]);
	xfree(pelog_env);
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
	/*
	 * Some job fields can change in the course of scheduling, so we
	 * report the original values supplied by the user rather than
	 * an intermediate value that might be set by our scheduling
	 * logic (e.g. to enforce a partition, association or QOS limit).
	 *
	 * Fields subject to change and their original values are as follows:
	 * min_cpus		orig_min_cpus
	 * max_cpus		orig_max_cpus
	 * pn_min_memory	orig_pn_min_memory
	 * dependency		orig_dependency
	 */
	pack32(detail_ptr->orig_min_cpus, buffer);	/* subject to change */
	pack32(detail_ptr->orig_max_cpus, buffer);	/* subject to change */
	pack32(detail_ptr->min_nodes, buffer);
	pack32(detail_ptr->max_nodes, buffer);
	pack32(detail_ptr->num_tasks, buffer);

	packstr(detail_ptr->acctg_freq, buffer);
	pack16(detail_ptr->contiguous, buffer);
	pack16(detail_ptr->core_spec, buffer);
	pack16(detail_ptr->cpus_per_task, buffer);
	pack32(detail_ptr->nice, buffer);
	pack16(detail_ptr->ntasks_per_node, buffer);
	pack16(detail_ptr->requeue, buffer);
	pack32(detail_ptr->task_dist, buffer);

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
	pack64(detail_ptr->orig_pn_min_memory, buffer);	/* subject to change */
	pack32(detail_ptr->pn_min_tmp_disk, buffer);
	pack32(detail_ptr->cpu_freq_min, buffer);
	pack32(detail_ptr->cpu_freq_max, buffer);
	pack32(detail_ptr->cpu_freq_gov, buffer);
	pack_time(detail_ptr->begin_time, buffer);
	pack_time(detail_ptr->submit_time, buffer);

	packstr(detail_ptr->req_nodes,  buffer);
	packstr(detail_ptr->exc_nodes,  buffer);
	packstr(detail_ptr->features,   buffer);
	packstr(detail_ptr->cluster_features, buffer);
	packstr(detail_ptr->dependency, buffer);	/* subject to change */
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
	char *orig_dependency = NULL, *mem_bind, *cluster_features = NULL;
	char *err = NULL, *in = NULL, *out = NULL, *work_dir = NULL;
	char *ckpt_dir = NULL, *restart_dir = NULL;
	char **argv = (char **) NULL, **env_sup = (char **) NULL;
	uint32_t min_nodes, max_nodes;
	uint32_t min_cpus = 1, max_cpus = NO_VAL;
	uint32_t pn_min_cpus, pn_min_tmp_disk;
	uint64_t pn_min_memory;
	uint32_t cpu_freq_min = NO_VAL;
	uint32_t cpu_freq_max = NO_VAL;
	uint32_t cpu_freq_gov = NO_VAL, nice = 0;
	uint32_t num_tasks, name_len, argc = 0, env_cnt = 0, task_dist;
	uint16_t contiguous, core_spec = NO_VAL16;
	uint16_t ntasks_per_node, cpus_per_task, requeue;
	uint16_t cpu_bind_type, mem_bind_type, plane_size;
	uint8_t open_mode, overcommit, prolog_running;
	uint8_t share_res, whole_node;
	time_t begin_time, submit_time;
	int i;
	multi_core_data_t *mc_ptr;

	/* unpack the job's details from the buffer */
	if (protocol_version >= SLURM_17_11_PROTOCOL_VERSION) {
		safe_unpack32(&min_cpus, buffer);
		safe_unpack32(&max_cpus, buffer);
		safe_unpack32(&min_nodes, buffer);
		safe_unpack32(&max_nodes, buffer);
		safe_unpack32(&num_tasks, buffer);

		safe_unpackstr_xmalloc(&acctg_freq, &name_len, buffer);
		safe_unpack16(&contiguous, buffer);
		safe_unpack16(&core_spec, buffer);
		safe_unpack16(&cpus_per_task, buffer);
		safe_unpack32(&nice, buffer);
		safe_unpack16(&ntasks_per_node, buffer);
		safe_unpack16(&requeue, buffer);
		safe_unpack32(&task_dist, buffer);

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
		safe_unpack64(&pn_min_memory, buffer);
		safe_unpack32(&pn_min_tmp_disk, buffer);
		safe_unpack32(&cpu_freq_min, buffer);
		safe_unpack32(&cpu_freq_max, buffer);
		safe_unpack32(&cpu_freq_gov, buffer);
		safe_unpack_time(&begin_time, buffer);
		safe_unpack_time(&submit_time, buffer);

		safe_unpackstr_xmalloc(&req_nodes,  &name_len, buffer);
		safe_unpackstr_xmalloc(&exc_nodes,  &name_len, buffer);
		safe_unpackstr_xmalloc(&features,   &name_len, buffer);
		safe_unpackstr_xmalloc(&cluster_features, &name_len, buffer);
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
	} else if (protocol_version >= SLURM_17_02_PROTOCOL_VERSION) {
		safe_unpack32(&min_cpus, buffer);
		safe_unpack32(&max_cpus, buffer);
		safe_unpack32(&min_nodes, buffer);
		safe_unpack32(&max_nodes, buffer);
		safe_unpack32(&num_tasks, buffer);

		safe_unpackstr_xmalloc(&acctg_freq, &name_len, buffer);
		safe_unpack16(&contiguous, buffer);
		safe_unpack16(&core_spec, buffer);
		safe_unpack16(&cpus_per_task, buffer);
		safe_unpack32(&nice, buffer);
		safe_unpack16(&ntasks_per_node, buffer);
		safe_unpack16(&requeue, buffer);
		safe_unpack32(&task_dist, buffer);

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
		safe_unpack64(&pn_min_memory, buffer);
		safe_unpack32(&pn_min_tmp_disk, buffer);
		safe_unpack32(&cpu_freq_min, buffer);
		safe_unpack32(&cpu_freq_max, buffer);
		safe_unpack32(&cpu_freq_gov, buffer);
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
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		uint32_t tmp_mem;
		safe_unpack32(&min_cpus, buffer);
		safe_unpack32(&max_cpus, buffer);
		safe_unpack32(&min_nodes, buffer);
		safe_unpack32(&max_nodes, buffer);
		safe_unpack32(&num_tasks, buffer);

		safe_unpackstr_xmalloc(&acctg_freq, &name_len, buffer);
		safe_unpack16(&contiguous, buffer);
		safe_unpack16(&core_spec, buffer);
		safe_unpack16(&cpus_per_task, buffer);
		safe_unpack32(&nice, buffer);
		safe_unpack16(&ntasks_per_node, buffer);
		safe_unpack16(&requeue, buffer);
		safe_unpack32(&task_dist, buffer);

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
		safe_unpack32(&tmp_mem, buffer);
		pn_min_memory = xlate_mem_old2new(tmp_mem);
		safe_unpack32(&pn_min_tmp_disk, buffer);
		safe_unpack32(&cpu_freq_min, buffer);
		safe_unpack32(&cpu_freq_max, buffer);
		safe_unpack32(&cpu_freq_gov, buffer);
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
	if (prolog_running > 4) {
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
	xfree(job_ptr->details->cluster_features);
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
	job_ptr->details->cpu_freq_min = cpu_freq_min;
	job_ptr->details->cpu_freq_max = cpu_freq_max;
	job_ptr->details->cpu_freq_gov = cpu_freq_gov;
	job_ptr->details->cpus_per_task = cpus_per_task;
	job_ptr->details->dependency = dependency;
	job_ptr->details->orig_dependency = orig_dependency;
	job_ptr->details->env_cnt = env_cnt;
	job_ptr->details->env_sup = env_sup;
	job_ptr->details->std_err = err;
	job_ptr->details->exc_nodes = exc_nodes;
	job_ptr->details->features = features;
	job_ptr->details->cluster_features = cluster_features;
	job_ptr->details->std_in = in;
	job_ptr->details->pn_min_cpus = pn_min_cpus;
	job_ptr->details->pn_min_memory = pn_min_memory;
	job_ptr->details->orig_pn_min_memory = pn_min_memory;
	job_ptr->details->pn_min_tmp_disk = pn_min_tmp_disk;
	job_ptr->details->max_cpus = max_cpus;
	job_ptr->details->orig_max_cpus = max_cpus;
	job_ptr->details->max_nodes = max_nodes;
	job_ptr->details->mc_ptr = mc_ptr;
	job_ptr->details->mem_bind = mem_bind;
	job_ptr->details->mem_bind_type = mem_bind_type;
	job_ptr->details->min_cpus = min_cpus;
	job_ptr->details->orig_min_cpus = min_cpus;
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
	xfree(cluster_features);
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
 * IN type - which hash to work with
 * Globals: hash table updated
 */
static void _remove_job_hash(struct job_record *job_entry,
			     job_hash_type_t type)
{
	struct job_record *job_ptr, **job_pptr;

	xassert(job_entry);

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

	if (job_pptr == NULL) {
		switch (type) {
		case JOB_HASH_JOB:
			error("%s: Could not find hash entry for job %u",
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

	if (!array_recs || array_recs->task_id_str ||
	    !array_recs->task_id_bitmap ||
	    (job_ptr->array_task_id != NO_VAL) ||
	    (bit_ffs(job_ptr->array_recs->task_id_bitmap) == -1))
		return;


	array_recs->task_id_str = bit_fmt_hexmask(array_recs->task_id_bitmap);

	/* While it is efficient to set the db_index to 0 here
	 * to get the database to update the record for
	 * pending tasks it also creates a window in which if
	 * the association id is changed (different account or
	 * partition) instead of returning the previous
	 * db_index (expected) it would create a new one
	 * leaving the other orphaned.  Setting the job_state
	 * sets things up so the db_index isn't lost but the
	 * start message is still sent to get the desired behavior. */

	/* Here we set the JOB_UPDATE_DB flag so we resend the start of the
	 * job updating the array task string and count of pending
	 * jobs.  This is faster than sending the start again since
	 * this could happen many times (like lots of array elements
	 * starting at once) instead of just ever so often.
	 */

	job_ptr->job_state |= JOB_UPDATE_DB;
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

/* Return true if ALL tasks of specific array job ID are finished */
extern bool test_job_array_finished(uint32_t array_job_id)
{
	struct job_record *job_ptr;
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

/* For a given job ID return the number of PENDING tasks which have their
 * own separate job_record (do not count tasks in pending META job record) */
extern int num_pending_job_array_tasks(uint32_t array_job_id)
{
	struct job_record *job_ptr;
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
 * find_job_pack_record - return a pointer to the job record with the given ID
 * IN job_id - requested job's ID
 * in pack_id - pack job component ID
 * RET pointer to the job's record, NULL on error
 */
extern struct job_record *find_job_pack_record(uint32_t job_id,
					       uint32_t pack_id)
{
	struct job_record *pack_leader, *pack_job;
	ListIterator iter;

	pack_leader = job_hash[JOB_HASH_INX(job_id)];
	while (pack_leader) {
		if (pack_leader->job_id == job_id)
			break;
		pack_leader = pack_leader->job_next;
	}
	if (!pack_leader)
		return NULL;
	if (pack_leader->pack_job_offset == pack_id)
		return pack_leader;

	if (!pack_leader->pack_job_list)
		return NULL;
	iter = list_iterator_create(pack_leader->pack_job_list);
	while ((pack_job = (struct job_record *) list_next(iter))) {
		if (pack_leader->pack_job_id != pack_job->pack_job_id) {
			error("%s: Bad pack_job_list for job %u",
			      __func__, pack_leader->pack_job_id);
			continue;
		}
		if (pack_job->pack_job_offset == pack_id)
			break;
	}
	list_iterator_destroy(iter);

	return pack_job;
}

/*
 * find_job_record - return a pointer to the job record with the given job_id
 * IN job_id - requested job's id
 * RET pointer to the job's record, NULL on error
 */
extern struct job_record *find_job_record(uint32_t job_id)
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
	struct job_record *job_ptr;
	int error_code = SLURM_SUCCESS;

	START_TIMER;
	lock_slurmctld(job_write_lock);
	job_ptr = find_job_record(job_step_kill_msg->job_id);
	trace_job(job_ptr, __func__, "enter");

	/* do RPC call */
	if (job_step_kill_msg->job_step_id == SLURM_BATCH_SCRIPT) {
		/* NOTE: SLURM_BATCH_SCRIPT == NO_VAL */
		error_code = job_signal(job_step_kill_msg->job_id,
					job_step_kill_msg->signal,
					job_step_kill_msg->flags, uid,
					false);
		unlock_slurmctld(job_write_lock);
		END_TIMER2(__func__);

		/* return result */
		if (error_code) {
			if (slurmctld_conf.debug_flags & DEBUG_FLAG_STEPS)
				info("Signal %u JobId=%u by UID=%u: %s",
				     job_step_kill_msg->signal,
				     job_step_kill_msg->job_id, uid,
				     slurm_strerror(error_code));
		} else {
			if (job_step_kill_msg->signal == SIGKILL) {
				if (slurmctld_conf.debug_flags &
						DEBUG_FLAG_STEPS)
					info("%s: Cancel of JobId=%u by "
					     "UID=%u, %s",
					     __func__,
					     job_step_kill_msg->job_id, uid,
					     TIME_STR);
				slurmctld_diag_stats.jobs_canceled++;
			} else {
				if (slurmctld_conf.debug_flags &
						DEBUG_FLAG_STEPS)
					info("%s: Signal %u of JobId=%u by "
					     "UID=%u, %s",
					     __func__,
					     job_step_kill_msg->signal,
					     job_step_kill_msg->job_id, uid,
					     TIME_STR);
			}

			/* Below function provides its own locking */
			schedule_job_save();
		}
	} else {
		error_code = job_step_signal(job_step_kill_msg->job_id,
					     job_step_kill_msg->job_step_id,
					     job_step_kill_msg->signal,
					     job_step_kill_msg->flags,
					     uid);
		unlock_slurmctld(job_write_lock);
		END_TIMER2(__func__);

		/* return result */
		if (error_code) {
			if (slurmctld_conf.debug_flags & DEBUG_FLAG_STEPS)
				info("Signal %u of StepId=%u.%u by UID=%u: %s",
				     job_step_kill_msg->signal,
				     job_step_kill_msg->job_id,
				     job_step_kill_msg->job_step_id, uid,
				     slurm_strerror(error_code));
		} else {
			if (job_step_kill_msg->signal == SIGKILL) {
				if (slurmctld_conf.debug_flags &
						DEBUG_FLAG_STEPS)
					info("%s: Cancel of StepId=%u.%u by "
					     "UID=%u %s", __func__,
					     job_step_kill_msg->job_id,
					     job_step_kill_msg->job_step_id,
					     uid, TIME_STR);
			} else {
				if (slurmctld_conf.debug_flags &
						DEBUG_FLAG_STEPS)
					info("%s: Signal %u of StepId=%u.%u "
					     "by UID=%u %s",
					     __func__,
					     job_step_kill_msg->signal,
					     job_step_kill_msg->job_id,
					     job_step_kill_msg->job_step_id,
					     uid, TIME_STR);
			}

			/* Below function provides its own locking */
			schedule_job_save();
		}
	}

	trace_job(job_ptr, __func__, "return");
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
	struct job_record *job_ptr, *job_pack_ptr;
	uint32_t *pack_job_ids = NULL;
	int cnt = 0, i, rc;
	int error_code = SLURM_SUCCESS;
	ListIterator iter;

	lock_slurmctld(job_read_lock);
	job_ptr = find_job_record(job_step_kill_msg->job_id);
	if (job_ptr && job_ptr->pack_job_list &&
	    (job_step_kill_msg->signal == SIGKILL) &&
	    (job_step_kill_msg->job_step_id != SLURM_BATCH_SCRIPT)) {
		cnt = list_count(job_ptr->pack_job_list);
		pack_job_ids = xmalloc(sizeof(uint32_t) * cnt);
		i = 0;
		iter = list_iterator_create(job_ptr->pack_job_list);
		while ((job_pack_ptr = (struct job_record *) list_next(iter))) {
			pack_job_ids[i++] = job_pack_ptr->job_id;
		}
		list_iterator_destroy(iter);
	}
	unlock_slurmctld(job_read_lock);

	if (!job_ptr) {
		info("%s: invalid job id %u", __func__,
		     job_step_kill_msg->job_id);
		error_code = ESLURM_INVALID_JOB_ID;
	} else if (pack_job_ids) {
		for (i = 0; i < cnt; i++) {
			job_step_kill_msg->job_id = pack_job_ids[i];
			rc = _kill_job_step(job_step_kill_msg, uid);
			if (rc != SLURM_SUCCESS)
				error_code = rc;
		}
		xfree(pack_job_ids);
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
			uint32_t suspend_job_state = job_ptr->job_state;
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
	ListIterator job_iterator;
	struct job_record  *job_ptr, *pack_leader;
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
		pack_leader = NULL;
		if (job_ptr->pack_job_id)
			pack_leader = find_job_record(job_ptr->pack_job_id);
		if (!pack_leader)
			pack_leader = job_ptr;
		if ((pack_leader->batch_host == NULL) ||
		    xstrcmp(pack_leader->batch_host, node_name))
			continue;	/* no match on node name */

		if (IS_JOB_SUSPENDED(job_ptr)) {
			uint32_t suspend_job_state = job_ptr->job_state;
			/*
			 * we can't have it as suspended when we call the
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
				if (job_ptr->node_cnt)
					(job_ptr->node_cnt)--;
				else {
					error("node_cnt underflow on JobId=%u",
					      job_ptr->job_id);
				}
				job_update_tres_cnt(job_ptr, i);
				if (job_ptr->node_cnt == 0) {
					cleanup_completing(job_ptr);
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

				/* clear signal sent flag on requeue */
				job_ptr->warn_flags &= ~WARN_SENT;

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
 *	PENDING or SUSPENDED job or reservations
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

	/* check jobs */
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

	/* check reservations */
	if (list_find_first(resv_list, _find_resv_part, part_ptr))
		return true;

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
		NO_LOCK, READ_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };

	if ((new_alloc->script != NULL) || (new_alloc->alloc_node == NULL))
		return false;

	lock_slurmctld(job_read_lock);
	job_iter = list_iterator_create(job_list);

	while ((job_ptr = (struct job_record *)list_next(job_iter))) {
		if (job_ptr->batch_flag || IS_JOB_FINISHED(job_ptr))
			continue;
		if (job_ptr->alloc_node &&
		    (xstrcmp(job_ptr->alloc_node, new_alloc->alloc_node) == 0) &&
		    (job_ptr->alloc_sid == new_alloc->alloc_sid))
			break;
	}
	list_iterator_destroy(job_iter);
	unlock_slurmctld(job_read_lock);

	return job_ptr != NULL;
}

/* Clear a job's GRES details per node strings, rebuilt later on demand */
static void _clear_job_gres_details(struct job_record *job_ptr)
{
	int i;

	for (i = 0; i < job_ptr->gres_detail_cnt; i++)
		xfree(job_ptr->gres_detail_str[i]);
	xfree(job_ptr->gres_detail_str);
	job_ptr->gres_detail_cnt = 0;
}


static bool _job_node_test(struct job_record *job_ptr, int node_inx)
{
	if (job_ptr->node_bitmap &&
	    bit_test(job_ptr->node_bitmap, node_inx))
		return true;
	return false;
}

static bool _pack_job_on_node(struct job_record *job_ptr, int node_inx)
{
	struct job_record *pack_leader, *pack_job;
	ListIterator iter;
	static bool result = false;

	if (!job_ptr->pack_job_id)
		return _job_node_test(job_ptr, node_inx);

	pack_leader = find_job_record(job_ptr->pack_job_id);
	if (!pack_leader) {
		error("%s: Job pack leader %u not found",
		      __func__, job_ptr->pack_job_id);
		return _job_node_test(job_ptr, node_inx);
	}
	if (!pack_leader->pack_job_list) {
		error("%s: Job pack leader %u job list is NULL",
		      __func__, job_ptr->pack_job_id);
		return _job_node_test(job_ptr, node_inx);
	}

	iter = list_iterator_create(pack_leader->pack_job_list);
	while ((pack_job = (struct job_record *) list_next(iter))) {
		if ((result = _job_node_test(pack_job, node_inx)))
			break;
		/*
		 * After a DOWN node is removed from another job component,
		 * we have no way to identify other pack job components with
		 * the same node, so assume if one component is in NODE_FAILED
		 * state, they all should be.
		 */
		if (IS_JOB_NODE_FAILED(pack_job)) {
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
	ListIterator job_iterator;
	struct job_record *job_ptr;
	struct node_record *node_ptr;
	int node_inx;
	int kill_job_cnt = 0;
	time_t now = time(NULL);

	node_ptr = find_node_record(node_name);
	if (node_ptr == NULL)	/* No such node */
		return 0;
	node_inx = node_ptr - node_record_table_ptr;

	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		bool suspended = false;
		if (!_pack_job_on_node(job_ptr, node_inx))
			continue;	/* job not on this node */
		if (nonstop_ops.node_fail)
			(nonstop_ops.node_fail)(job_ptr, node_ptr);
		if (IS_JOB_SUSPENDED(job_ptr)) {
			uint32_t suspend_job_state = job_ptr->job_state;
			/*
			 * we can't have it as suspended when we call the
			 * accounting stuff.
			 */
			job_ptr->job_state = JOB_CANCELLED;
			jobacct_storage_g_job_suspend(acct_db_conn, job_ptr);
			job_ptr->job_state = suspend_job_state;
			suspended = true;
		}

		if (IS_JOB_COMPLETING(job_ptr)) {
			if (!bit_test(job_ptr->node_bitmap_cg, node_inx))
				continue;
			kill_job_cnt++;
			bit_clear(job_ptr->node_bitmap_cg, node_inx);
			job_update_tres_cnt(job_ptr, node_inx);
			if (job_ptr->node_cnt)
				(job_ptr->node_cnt)--;
			else {
				error("node_cnt underflow on JobId=%u",
				      job_ptr->job_id);
			}
			if (job_ptr->node_cnt == 0)
				cleanup_completing(job_ptr);

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
			    (job_ptr->node_cnt > 1) &&
			    !IS_JOB_CONFIGURING(job_ptr)) {
				/* keep job running on remaining nodes */
				srun_node_fail(job_ptr->job_id, node_name);
				error("Removing failed node %s from job_id %u",
				      node_name, job_ptr->job_id);
				job_pre_resize_acctg(job_ptr);
				kill_step_on_node(job_ptr, node_ptr, true);
				excise_node_from_job(job_ptr, node_ptr);
				(void) gs_job_start(job_ptr);
				gres_build_job_details(job_ptr->gres_list,
						       &job_ptr->gres_detail_cnt,
						       &job_ptr->gres_detail_str);
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
				job_ptr->job_state = JOB_NODE_FAIL;
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

				/* clear signal sent flag on requeue */
				job_ptr->warn_flags &= ~WARN_SENT;

				/*
				 * Since the job completion logger
				 * removes the submit we need to add it
				 * again.
				 */
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
	long pn_min_cpus, pn_min_tmp_disk, min_cpus;
	uint64_t pn_min_memory;
	long time_limit, priority, contiguous, nice, time_min;
	long kill_on_node_fail, shared, immediate, wait_all_nodes;
	long cpus_per_task, requeue, num_tasks, overcommit;
	long ntasks_per_node, ntasks_per_socket, ntasks_per_core;
	int spec_count;
	char *mem_type, buf[100], *signal_flags, *spec_type, *job_id;

	if (get_log_level() < LOG_LEVEL_DEBUG3)
		return;

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
	pn_min_cpus    = (job_specs->pn_min_cpus != NO_VAL16) ?
		(long) job_specs->pn_min_cpus : -1L;
	if (job_specs->core_spec == NO_VAL16) {
		spec_type  = "core";
		spec_count = -1;
	} else if (job_specs->core_spec & CORE_SPEC_THREAD) {
		spec_type  = "thread";
		spec_count = job_specs->core_spec & (~CORE_SPEC_THREAD);
	} else {
		spec_type  = "core";
		spec_count = job_specs->core_spec;
	}
	debug3("   cpus=%ld-%u pn_min_cpus=%ld %s_spec=%d",
	       min_cpus, job_specs->max_cpus, pn_min_cpus,
	       spec_type, spec_count);

	debug3("   Nodes=%u-[%u] Sock/Node=%u Core/Sock=%u Thread/Core=%u",
	       job_specs->min_nodes, job_specs->max_nodes,
	       job_specs->sockets_per_node, job_specs->cores_per_socket,
	       job_specs->threads_per_core);

	if (job_specs->pn_min_memory == NO_VAL64) {
		pn_min_memory = -1L;
		mem_type = "job";
	} else if (job_specs->pn_min_memory & MEM_PER_CPU) {
		pn_min_memory = job_specs->pn_min_memory & (~MEM_PER_CPU);
		mem_type = "cpu";
	} else {
		pn_min_memory = job_specs->pn_min_memory;
		mem_type = "job";
	}
	pn_min_tmp_disk = (job_specs->pn_min_tmp_disk != NO_VAL) ?
		(long) job_specs->pn_min_tmp_disk : -1L;
	debug3("   pn_min_memory_%s=%"PRIu64" pn_min_tmp_disk=%ld",
	       mem_type, pn_min_memory, pn_min_tmp_disk);
	immediate = (job_specs->immediate == 0) ? 0L : 1L;
	debug3("   immediate=%ld reservation=%s",
	       immediate, job_specs->reservation);
	debug3("   features=%s cluster_features=%s",
	       job_specs->features, job_specs->cluster_features);

	debug3("   req_nodes=%s exc_nodes=%s gres=%s",
	       job_specs->req_nodes, job_specs->exc_nodes, job_specs->gres);

	time_limit = (job_specs->time_limit != NO_VAL) ?
		(long) job_specs->time_limit : -1L;
	time_min = (job_specs->time_min != NO_VAL) ?
		(long) job_specs->time_min : time_limit;
	priority   = (job_specs->priority != NO_VAL) ?
		(long) job_specs->priority : -1L;
	contiguous = (job_specs->contiguous != NO_VAL16) ?
		(long) job_specs->contiguous : -1L;
	shared = (job_specs->shared != NO_VAL16) ?
		(long) job_specs->shared : -1L;
	debug3("   time_limit=%ld-%ld priority=%ld contiguous=%ld shared=%ld",
	       time_min, time_limit, priority, contiguous, shared);

	kill_on_node_fail = (job_specs->kill_on_node_fail !=
			     NO_VAL16) ?
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

	debug3("   power_flags=%s",
	       power_flags_str(job_specs->power_flags));

	debug3("   resp_host=%s alloc_resp_port=%u other_port=%u",
	       job_specs->resp_host,
	       job_specs->alloc_resp_port, job_specs->other_port);
	debug3("   dependency=%s account=%s qos=%s comment=%s",
	       job_specs->dependency, job_specs->account,
	       job_specs->qos, job_specs->comment);

	num_tasks = (job_specs->num_tasks != NO_VAL) ?
		(long) job_specs->num_tasks : -1L;
	overcommit = (job_specs->overcommit != NO_VAL8) ?
		(long) job_specs->overcommit : -1L;
	nice = (job_specs->nice != NO_VAL) ?
		((int64_t)job_specs->nice - NICE_OFFSET) : 0;
	debug3("   mail_type=%u mail_user=%s nice=%ld num_tasks=%ld "
	       "open_mode=%u overcommit=%ld acctg_freq=%s",
	       job_specs->mail_type, job_specs->mail_user, nice, num_tasks,
	       job_specs->open_mode, overcommit, job_specs->acctg_freq);

	slurm_make_time_str(&job_specs->begin_time, buf, sizeof(buf));
	cpus_per_task = (job_specs->cpus_per_task != NO_VAL16) ?
		(long) job_specs->cpus_per_task : -1L;
	requeue = (job_specs->requeue != NO_VAL16) ?
		(long) job_specs->requeue : -1L;
	debug3("   network=%s begin=%s cpus_per_task=%ld requeue=%ld "
	       "licenses=%s",
	       job_specs->network, buf, cpus_per_task, requeue,
	       job_specs->licenses);

	slurm_make_time_str(&job_specs->end_time, buf, sizeof(buf));
	wait_all_nodes = (job_specs->wait_all_nodes != NO_VAL16) ?
			 (long) job_specs->wait_all_nodes : -1L;
	if (job_specs->warn_flags & KILL_JOB_BATCH)
		signal_flags = "B:";
	else
		signal_flags = "";
	cpu_freq_debug(NULL, NULL, buf, sizeof(buf), job_specs->cpu_freq_gov,
		       job_specs->cpu_freq_min, job_specs->cpu_freq_max,
		       NO_VAL);
	debug3("   end_time=%s signal=%s%u@%u wait_all_nodes=%ld cpu_freq=%s",
	       buf, signal_flags, job_specs->warn_signal, job_specs->warn_time,
	       wait_all_nodes, buf);

	ntasks_per_node = (job_specs->ntasks_per_node != NO_VAL16) ?
		(long) job_specs->ntasks_per_node : -1L;
	ntasks_per_socket = (job_specs->ntasks_per_socket !=
			     NO_VAL16) ?
		(long) job_specs->ntasks_per_socket : -1L;
	ntasks_per_core = (job_specs->ntasks_per_core != NO_VAL16) ?
		(long) job_specs->ntasks_per_core : -1L;
	debug3("   ntasks_per_node=%ld ntasks_per_socket=%ld "
	       "ntasks_per_core=%ld",
	       ntasks_per_node, ntasks_per_socket, ntasks_per_core);

	debug3("   mem_bind=%u:%s plane_size:%u",
	       job_specs->mem_bind_type, job_specs->mem_bind,
	       job_specs->plane_size);
	debug3("   array_inx=%s", job_specs->array_inx);
	debug3("   burst_buffer=%s", job_specs->burst_buffer);
	debug3("   mcs_label=%s", job_specs->mcs_label);
	slurm_make_time_str(&job_specs->deadline, buf, sizeof(buf));
	debug3("   deadline=%s", buf);
	debug3("   bitflags=%u delay_boot=%u", job_specs->bitflags,
	       job_specs->delay_boot);

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

	if (!purge_files_list) {
		purge_files_list = list_create(slurm_destroy_uint32_ptr);
	}

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
 * IN job_ptr - META job record for a job array, which is to become an
 *		individial task of the job array.
 *		Set the job's array_task_id to the task to be split out.
 * RET - The new job record, which is the new META job record. */
extern struct job_record *job_array_split(struct job_record *job_ptr)
{
	struct job_record *job_ptr_pend = NULL, *save_job_next;
	struct job_details *job_details, *details_new, *save_details;
	uint32_t save_job_id;
	uint64_t save_db_index = job_ptr->db_index;
	priority_factors_object_t *save_prio_factors;
	List save_step_list;
	int i;

	job_ptr_pend = _create_job_record(0);
	if (!job_ptr_pend)
		return NULL;

	_remove_job_hash(job_ptr, JOB_HASH_JOB);
	job_ptr_pend->job_id = job_ptr->job_id;
	if (_set_job_id(job_ptr) != SLURM_SUCCESS)
		fatal("%s: _set_job_id error", __func__);
	if (!job_ptr->array_recs) {
		fatal("%s: job %u record lacks array structure",
		      __func__, job_ptr->job_id);
	}

	/*
	 * Copy most of original job data.
	 * This could be done in parallel, but performance was worse.
	 */
	save_job_id   = job_ptr_pend->job_id;
	save_job_next = job_ptr_pend->job_next;
	save_details  = job_ptr_pend->details;
	save_prio_factors = job_ptr_pend->prio_factors;
	save_step_list = job_ptr_pend->step_list;
	memcpy(job_ptr_pend, job_ptr, sizeof(struct job_record));

	job_ptr_pend->job_id   = save_job_id;
	job_ptr_pend->job_next = save_job_next;
	job_ptr_pend->details  = save_details;
	job_ptr_pend->step_list = save_step_list;
	job_ptr_pend->db_index = save_db_index;

	job_ptr_pend->prio_factors = save_prio_factors;
	slurm_copy_priority_factors_object(job_ptr_pend->prio_factors,
					   job_ptr->prio_factors);

	job_ptr_pend->account = xstrdup(job_ptr->account);
	job_ptr_pend->admin_comment = xstrdup(job_ptr->admin_comment);
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
	if (job_ptr_pend->array_recs->task_cnt) {
		job_ptr_pend->array_recs->task_cnt--;
	} else {
		error("Job %u array_recs->task_cnt underflow",
		      job_ptr->array_job_id);
	}
	job_ptr_pend->array_task_id = NO_VAL;

	job_ptr_pend->batch_host = NULL;
	if (job_ptr->check_job) {
		job_ptr_pend->check_job =
			checkpoint_copy_jobinfo(job_ptr->check_job);
	}
	job_ptr_pend->burst_buffer = xstrdup(job_ptr->burst_buffer);
	job_ptr_pend->burst_buffer_state = xstrdup(job_ptr->burst_buffer_state);
	job_ptr_pend->clusters = xstrdup(job_ptr->clusters);
	job_ptr_pend->comment = xstrdup(job_ptr->comment);

	job_ptr_pend->fed_details = _dup_job_fed_details(job_ptr->fed_details);

	job_ptr_pend->front_end_ptr = NULL;
	/* struct job_details *details;		*** NOTE: Copied below */
	job_ptr_pend->gres = xstrdup(job_ptr->gres);
	if (job_ptr->gres_list) {
		job_ptr_pend->gres_list =
			gres_plugin_job_state_dup(job_ptr->gres_list);
	}
	job_ptr_pend->gres_detail_cnt = 0;
	job_ptr_pend->gres_detail_str = NULL;
	job_ptr_pend->gres_alloc = NULL;
	job_ptr_pend->gres_req = NULL;
	job_ptr_pend->gres_used = NULL;

	job_ptr_pend->limit_set.tres =
		xmalloc(sizeof(uint16_t) * slurmctld_tres_cnt);
	memcpy(job_ptr_pend->limit_set.tres, job_ptr->limit_set.tres,
	       sizeof(uint16_t) * slurmctld_tres_cnt);

	_add_job_hash(job_ptr);		/* Sets job_next */
	_add_job_hash(job_ptr_pend);	/* Sets job_next */
	_add_job_array_hash(job_ptr);
	job_ptr_pend->job_resrcs = NULL;

	job_ptr_pend->licenses = xstrdup(job_ptr->licenses);
	job_ptr_pend->license_list = license_job_copy(job_ptr->license_list);
	job_ptr_pend->mail_user = xstrdup(job_ptr->mail_user);
	job_ptr_pend->mcs_label = xstrdup(job_ptr->mcs_label);
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

	i = sizeof(uint64_t) * slurmctld_tres_cnt;
	job_ptr_pend->tres_req_cnt = xmalloc(i);
	memcpy(job_ptr_pend->tres_req_cnt, job_ptr->tres_req_cnt, i);
	job_ptr_pend->tres_req_str = xstrdup(job_ptr->tres_req_str);
	job_ptr_pend->tres_fmt_req_str = xstrdup(job_ptr->tres_fmt_req_str);
	job_ptr_pend->tres_alloc_str = NULL;
	job_ptr_pend->tres_fmt_alloc_str = NULL;

	job_ptr_pend->user_name = xstrdup(job_ptr->user_name);
	job_ptr_pend->wckey = xstrdup(job_ptr->wckey);
	job_ptr_pend->deadline = job_ptr->deadline;

	job_details = job_ptr->details;
	details_new = job_ptr_pend->details;
	memcpy(details_new, job_details, sizeof(struct job_details));

	/*
	 * Reset the preempt_start_time or high priority array jobs will hang
	 * for a period before preempting more jobs.
	 */
	details_new->preempt_start_time = 0;

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
	details_new->cpu_freq_min = job_details->cpu_freq_min;
	details_new->cpu_freq_max = job_details->cpu_freq_max;
	details_new->cpu_freq_gov = job_details->cpu_freq_gov;
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
	details_new->cluster_features = xstrdup(job_details->cluster_features);
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
	details_new->req_nodes = xstrdup(job_details->req_nodes);
	details_new->restart_dir = xstrdup(job_details->restart_dir);
	details_new->std_err = xstrdup(job_details->std_err);
	details_new->std_in = xstrdup(job_details->std_in);
	details_new->std_out = xstrdup(job_details->std_out);
	details_new->work_dir = xstrdup(job_details->work_dir);

	if (job_ptr->fed_details)
		add_fed_job_info(job_ptr);

	return job_ptr_pend;
}

/* Add job array data stucture to the job record */
static void _create_job_array(struct job_record *job_ptr,
			      job_desc_msg_t *job_specs)
{
	struct job_details *details;
	char *sep = NULL;
	int max_run_tasks, min_task_id, max_task_id, step_task_id = 1, task_cnt;
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
	min_task_id = bit_ffs(job_specs->array_bitmap);
	max_task_id = bit_fls(job_specs->array_bitmap);
	task_cnt = bit_set_count(job_specs->array_bitmap);
	i_cnt = max_task_id + 1;
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

	details = job_ptr->details;
	if (details) {
		if (job_specs->array_inx) {
			sep = strchr(job_specs->array_inx, ':');
			if (sep)
				step_task_id = atoi(sep + 1);
		}
		details->env_sup = xrealloc(details->env_sup,
					    (sizeof(char *) *
					    (details->env_cnt + 4)));
		xstrfmtcat(details->env_sup[details->env_cnt++],
			   "SLURM_ARRAY_TASK_COUNT=%d", task_cnt);
		xstrfmtcat(details->env_sup[details->env_cnt++],
			   "SLURM_ARRAY_TASK_MIN=%d", min_task_id);
		xstrfmtcat(details->env_sup[details->env_cnt++],
			   "SLURM_ARRAY_TASK_MAX=%d", max_task_id);
		xstrfmtcat(details->env_sup[details->env_cnt++],
			   "SLURM_ARRAY_TASK_STEP=%d", step_task_id);
	}
}

static int _sort_part_tier(void *x, void *y)
{
	struct part_record *parta = *(struct part_record **) x;
	struct part_record *partb = *(struct part_record **) y;

	if (parta->priority_tier > partb->priority_tier)
		return -1;
	if (parta->priority_tier < partb->priority_tier)
		return 1;

	return 0;
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
	int best_rc = -1, part_limits_rc = WAIT_NO_REASON;

	if (job_ptr->part_ptr_list) {
		list_sort(job_ptr->part_ptr_list, _sort_part_tier);
		iter = list_iterator_create(job_ptr->part_ptr_list);
		while ((part_ptr = list_next(iter))) {
			job_ptr->part_ptr = part_ptr;
			debug2("Try job %u on next partition %s",
			       job_ptr->job_id, part_ptr->name);

			part_limits_rc = job_limits_check(&job_ptr, false);

			if ((part_limits_rc != WAIT_NO_REASON) &&
			    (slurmctld_conf.enforce_part_limits ==
			     PARTITION_ENFORCE_ANY))
				continue;
			if ((part_limits_rc != WAIT_NO_REASON) &&
			    (slurmctld_conf.enforce_part_limits ==
			     PARTITION_ENFORCE_ALL)) {
				if (part_limits_rc != WAIT_PART_DOWN) {
					best_rc = ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE;
					break;
				} else {
					best_rc = ESLURM_PARTITION_DOWN;
				}
			}

			if (part_limits_rc == WAIT_NO_REASON) {
				rc = select_nodes(job_ptr, test_only,
						  select_node_bitmap, err_msg,
						  true);
			} else {
				rc = select_nodes(job_ptr, true,
						  select_node_bitmap, err_msg,
						  true);
				if ((rc == SLURM_SUCCESS) &&
				    (part_limits_rc == WAIT_PART_DOWN))
					rc = ESLURM_PARTITION_DOWN;
			}
			if ((rc == ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE) &&
			    (slurmctld_conf.enforce_part_limits ==
			     PARTITION_ENFORCE_ALL)) {
				best_rc = rc;	/* Job can not run */
				break;
			}
			if ((rc != ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE) &&
			    (rc != ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE) &&
			    (rc != ESLURM_RESERVATION_BUSY) &&
			    (rc != ESLURM_NODES_BUSY)) {
				best_rc = rc;	/* Job can run now */
				if ((slurmctld_conf.enforce_part_limits ==
				     PARTITION_ENFORCE_ANY) ||
				    (slurmctld_conf.enforce_part_limits ==
				     PARTITION_ENFORCE_NONE)) {
					break;
				}
			}
			if (((rc == ESLURM_NODES_BUSY) ||
			     (rc == ESLURM_RESERVATION_BUSY)) &&
			    (best_rc == -1) &&
			    ((slurmctld_conf.enforce_part_limits ==
			      PARTITION_ENFORCE_ANY) ||
			     (slurmctld_conf.enforce_part_limits ==
			      PARTITION_ENFORCE_NONE))) {
				if (test_only)
					break;
				best_rc = rc;	/* Keep looking for partition
						 * where job can start now */
			}
			if ((job_ptr->preempt_in_progress) &&
			    (rc != ESLURM_NODES_BUSY)) {
				/* Already started preempting jobs, don't
				 * consider starting this job in another
				 * partition as we iterator over others. */
				test_only = true;
			}
		}
		list_iterator_destroy(iter);
		if (best_rc != -1)
			rc = best_rc;
		else if (part_limits_rc == WAIT_PART_DOWN)
			rc = ESLURM_PARTITION_DOWN;
	} else {
		part_limits_rc = job_limits_check(&job_ptr, false);
		if (part_limits_rc == WAIT_NO_REASON) {
			rc = select_nodes(job_ptr, test_only,
					  select_node_bitmap, err_msg, true);
		} else if (part_limits_rc == WAIT_PART_DOWN) {
			rc = select_nodes(job_ptr, true,
					  select_node_bitmap, err_msg, true);
			if (rc == SLURM_SUCCESS)
				rc = ESLURM_PARTITION_DOWN;
		}
	}

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
	else if (rc == ESLURM_POWER_NOT_AVAIL)
		job_ptr->state_reason = WAIT_POWER_NOT_AVAIL;
	else if (rc == ESLURM_BURST_BUFFER_WAIT)
		job_ptr->state_reason = WAIT_BURST_BUFFER_RESOURCE;
	else if (rc == ESLURM_POWER_RESERVED)
		job_ptr->state_reason = WAIT_POWER_RESERVED;
	else if (rc == ESLURM_PARTITION_DOWN)
		job_ptr->state_reason = WAIT_PART_DOWN;
	return rc;
}

static inline bool _has_deadline(struct job_record *job_ptr)
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
	static time_t sched_update = 0;
	static int defer_sched = 0;
	char *sched_params, *tmp_ptr;
	int error_code, i;
	bool no_alloc, top_prio, test_only, too_fragmented, independent;
	struct job_record *job_ptr;
	time_t now = time(NULL);

	if (sched_update != slurmctld_conf.last_update) {
		sched_update = slurmctld_conf.last_update;
		sched_params = slurm_get_sched_params();
		if (sched_params && strstr(sched_params, "defer"))
			defer_sched = 1;
		else
			defer_sched = 0;
		if (sched_params &&
		    (tmp_ptr = strstr(sched_params, "delay_boot="))) {
			i = time_str2secs(tmp_ptr + 11);
			if (i != NO_VAL)
				delay_boot = i;
		}
		bf_min_age_reserve = 0;
		if (sched_params &&
		    (tmp_ptr = strstr(sched_params, "bf_min_age_reserve="))) {
			int min_age = atoi(tmp_ptr + 19);
			if (min_age > 0)
				bf_min_age_reserve = min_age;
		}
		xfree(sched_params);
	}

	if (job_specs->array_bitmap)
		i = bit_set_count(job_specs->array_bitmap);
	else
		i = 1;

	if ((job_count + i) >= slurmctld_conf.max_job_cnt) {
		error("%s: MaxJobCount limit from slurm.conf reached (%u)",
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
	    (license_job_test(job_ptr, time(NULL), true) != SLURM_SUCCESS))
		independent = false;

	/* Avoid resource fragmentation if important */
	if ((submit_uid || (job_specs->req_nodes == NULL)) &&
	    independent && job_is_completing(NULL))
		too_fragmented = true;	/* Don't pick nodes for job now */
	/* FIXME: Ideally we only want to refuse the request if the
	 * required node list is insufficient to satisfy the job's
	 * processor or node count requirements, but the overhead is
	 * rather high to do that right here. We let requests from
	 * user root proceed if a node list is specified, for
	 * meta-schedulers (e.g. LCRM). */
	else
		too_fragmented = false;

	if (defer_sched == 1)
		too_fragmented = true;

	if (independent && (!too_fragmented))
		top_prio = _top_priority(job_ptr, job_specs->pack_job_offset);
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
		slurm_init_job_desc_msg(&job_desc_msg);
		job_desc_msg.job_id = job_ptr->job_id;
		rc = job_start_data(&job_desc_msg, resp);
		job_ptr->job_state  = JOB_FAILED;
		job_ptr->exit_code  = 1;
		job_ptr->start_time = job_ptr->end_time = now;
		purge_job_record(job_ptr->job_id);
		return rc;
	}

	/* fed jobs need to go to the siblings first so don't attempt to
	 * schedule the job now. */
	test_only = will_run || job_ptr->deadline || (allocate == 0) ||
		    job_ptr->fed_details;

	no_alloc = test_only || too_fragmented || _has_deadline(job_ptr) ||
		   (!top_prio) || (!independent) || !avail_front_end(job_ptr) ||
		   (job_specs->pack_job_offset != NO_VAL);

	no_alloc = no_alloc || (bb_g_job_test_stage_in(job_ptr, no_alloc) != 1);

	error_code = _select_nodes_parts(job_ptr, no_alloc, NULL, err_msg);
	if (!test_only) {
		last_job_update = now;
	}

       /* Moved this (_create_job_array) here to handle when a job
	* array is submitted since we
	* want to know the array task count when we check the job against
	* QoS/Assoc limits
	*/
	_create_job_array(job_ptr, job_specs);

	slurmctld_diag_stats.jobs_submitted +=
		(job_ptr->array_recs && job_ptr->array_recs->task_cnt) ?
		job_ptr->array_recs->task_cnt : 1;

	acct_policy_add_job_submit(job_ptr);

	if ((error_code == ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE) &&
	    (slurmctld_conf.enforce_part_limits != PARTITION_ENFORCE_NONE))
		;	/* Reject job submission */
	else if ((error_code == ESLURM_NODES_BUSY) ||
		 (error_code == ESLURM_RESERVATION_BUSY) ||
		 (error_code == ESLURM_JOB_HELD) ||
		 (error_code == ESLURM_NODE_NOT_AVAIL) ||
		 (error_code == ESLURM_QOS_THRES) ||
		 (error_code == ESLURM_ACCOUNTING_POLICY) ||
		 (error_code == ESLURM_RESERVATION_NOT_USABLE) ||
		 (error_code == ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE) ||
		 (error_code == ESLURM_POWER_NOT_AVAIL) ||
		 (error_code == ESLURM_BURST_BUFFER_WAIT) ||
		 (error_code == ESLURM_POWER_RESERVED) ||
		 (error_code == ESLURM_PARTITION_DOWN)) {
		/* Not fatal error, but job can't be scheduled right now */
		if (immediate) {
			job_ptr->job_state  = JOB_FAILED;
			job_ptr->exit_code  = 1;
			job_ptr->state_reason = FAIL_BAD_CONSTRAINTS;
			xfree(job_ptr->state_desc);
			job_ptr->start_time = job_ptr->end_time = now;
			job_completion_logger(job_ptr, false);
		} else {	/* job remains queued */
			if ((error_code == ESLURM_NODES_BUSY) ||
			    (error_code == ESLURM_BURST_BUFFER_WAIT) ||
			    (error_code == ESLURM_RESERVATION_BUSY) ||
			    (error_code == ESLURM_ACCOUNTING_POLICY) ||
			    ((error_code == ESLURM_PARTITION_DOWN) &&
			    (job_ptr->batch_flag))) {
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
		purge_job_record(job_ptr->job_id);
	} else if (!with_slurmdbd)
		jobacct_storage_g_job_start(acct_db_conn, job_ptr);

	if (!will_run) {
		debug2("sched: JobId=%u allocated resources: NodeList=%s",
		       job_ptr->job_id, job_ptr->nodes);
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
static int _job_fail(struct job_record *job_ptr, uint32_t job_state)
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
		job_ptr->job_state = JOB_CANCELLED;
		jobacct_storage_g_job_suspend(acct_db_conn, job_ptr);
		job_ptr->job_state = suspend_job_state;
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
		job_ptr->job_id, job_state_string(job_ptr->job_state));

	return ESLURM_TRANSITION_STATE_NO_UPDATE;

}

/*
 * job_fail - terminate a job due to initiation failure
 * IN job_id - ID of the job to be killed
 * IN job_state - desired job state (JOB_BOOT_FAIL, JOB_NODE_FAIL, etc.)
 * RET 0 on success, otherwise ESLURM error code
 */
extern int job_fail(uint32_t job_id, uint32_t job_state)
{
	struct job_record *job_ptr, *pack_job, *pack_leader;
	ListIterator iter;
	int rc = SLURM_SUCCESS, rc1;

	job_ptr = find_job_record(job_id);
	if (job_ptr == NULL) {
		error("job_fail: invalid job id %u", job_id);
		return ESLURM_INVALID_JOB_ID;
	}

	if (job_ptr->pack_job_id == 0)
		return _job_fail(job_ptr, job_state);

	pack_leader = find_job_record(job_ptr->pack_job_id);
	if (!pack_leader) {
		error("%s: Job pack leader %u not found",
		      __func__, job_ptr->pack_job_id);
		return _job_fail(job_ptr, job_state);
	}
	if (!pack_leader->pack_job_list) {
		error("%s: Job pack leader %u job list is NULL",
		      __func__, job_ptr->pack_job_id);
		return _job_fail(job_ptr, job_state);
	}

	iter = list_iterator_create(pack_leader->pack_job_list);
	while ((pack_job = (struct job_record *) list_next(iter))) {
		if (pack_leader->pack_job_id != pack_job->pack_job_id) {
			error("%s: Bad pack_job_list for job %u",
			      __func__, pack_leader->pack_job_id);
			continue;
		}
		rc1 = _job_fail(pack_job, job_state);
		if (rc1 != SLURM_SUCCESS)
			rc = rc1;
	}
	list_iterator_destroy(iter);

	return rc;
}

/*
 * Signal a job based upon job pointer.
 * Authentication and authorization checks must be performed before calling.
 */
static int _job_signal(struct job_record *job_ptr, uint16_t signal,
		       uint16_t flags, uid_t uid, bool preempt)
{
	uint16_t job_term_state;
	char jbuf[JBUFSIZ];
	time_t now = time(NULL);

	trace_job(job_ptr, __func__, "enter");

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
		    job_ptr->fed_details->cluster_lock &&
		    (job_ptr->fed_details->cluster_lock !=
		     fed_mgr_cluster_rec->fed.id)) {
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
		} else if (!origin ||
			   !origin->fed.send ||
			   (((slurm_persist_conn_t *)origin->fed.send)->fd
			    == -1)) {
			/*
			 * The origin is down just signal all of the viable
			 * sibling jobs
			 */
			fed_mgr_job_cancel(job_ptr, signal, flags, uid, true);
		}
	}

	/* let node select plugin do any state-dependent signaling actions */
	select_g_job_signal(job_ptr, signal);
	last_job_update = now;

	/* save user ID of the one who requested the job be cancelled */
	if (signal == SIGKILL)
		job_ptr->requid = uid;
	if (IS_JOB_PENDING(job_ptr) && IS_JOB_COMPLETING(job_ptr) &&
	    (signal == SIGKILL)) {
		/* Prevent job requeue, otherwise preserve state */
		job_ptr->job_state = JOB_CANCELLED | JOB_COMPLETING;

		/* build_cg_bitmap() not needed, job already completing */
		verbose("%s: of requeuing %s successful",
			__func__, jobid2str(job_ptr, jbuf, sizeof(jbuf)));
		return SLURM_SUCCESS;
	}

	if (flags & KILL_HURRY)
		job_ptr->bit_flags |= JOB_KILL_HURRY;

	if (IS_JOB_CONFIGURING(job_ptr) && (signal == SIGKILL)) {
		last_job_update         = now;
		job_ptr->end_time       = now;
		job_ptr->job_state      = JOB_CANCELLED | JOB_COMPLETING;
		if (flags & KILL_FED_REQUEUE)
			job_ptr->job_state |= JOB_REQUEUE;
		build_cg_bitmap(job_ptr);
		job_completion_logger(job_ptr, false);
		deallocate_nodes(job_ptr, false, false, false);
		if (flags & KILL_FED_REQUEUE) {
			job_ptr->job_state &= (~JOB_REQUEUE);
		}
		verbose("%s: %u of configuring %s successful",
			__func__, signal, jobid2str(job_ptr, jbuf,
						    sizeof(jbuf)));
		return SLURM_SUCCESS;
	}

	if (IS_JOB_PENDING(job_ptr) && (signal == SIGKILL)) {
		job_ptr->job_state	= JOB_CANCELLED;
		if (flags & KILL_FED_REQUEUE)
			job_ptr->job_state |= JOB_REQUEUE;
		job_ptr->start_time	= now;
		job_ptr->end_time	= now;
		srun_allocate_abort(job_ptr);
		job_completion_logger(job_ptr, false);
		if (flags & KILL_FED_REQUEUE) {
			job_ptr->job_state &= (~JOB_REQUEUE);
		}
		/*
		 * Send back a response to the origin cluster, in other cases
		 * where the job is running the job will send back a response
		 * after the job is is completed. This can happen when the
		 * pending origin job is put into a hold state and the siblings
		 * are removed or when the job is canceled from the origin.
		 */
		fed_mgr_job_complete(job_ptr, 0, now);
		verbose("%s: of pending %s successful",
			__func__, jobid2str(job_ptr, jbuf, sizeof(jbuf)));
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
		if (flags & KILL_FED_REQUEUE)
			job_ptr->job_state |= JOB_REQUEUE;
		build_cg_bitmap(job_ptr);
		jobacct_storage_g_job_suspend(acct_db_conn, job_ptr);
		job_completion_logger(job_ptr, false);
		if (flags & KILL_FED_REQUEUE)
			job_ptr->job_state &= (~JOB_REQUEUE);
		deallocate_nodes(job_ptr, false, true, preempt);
		verbose("%s: %u of suspended %s successful",
			__func__, signal, jobid2str(job_ptr, jbuf,
						    sizeof(jbuf)));
		return SLURM_SUCCESS;
	}

	if (IS_JOB_RUNNING(job_ptr)) {
		if (signal == SIGSTOP)
			job_ptr->job_state |= JOB_STOPPED;
		else if (signal == SIGCONT)
			job_ptr->job_state &= (~JOB_STOPPED);

		if ((signal == SIGKILL)
		    && !(flags & KILL_STEPS_ONLY)
		    && !(flags & KILL_JOB_BATCH)) {
			/* No need to signal steps, deallocate kills them
			 */
			job_ptr->time_last_active	= now;
			job_ptr->end_time		= now;
			last_job_update			= now;
			job_ptr->job_state = job_term_state | JOB_COMPLETING;
			if (flags & KILL_FED_REQUEUE)
				job_ptr->job_state |= JOB_REQUEUE;
			build_cg_bitmap(job_ptr);
			job_completion_logger(job_ptr, false);
			deallocate_nodes(job_ptr, false, false, preempt);
			if (flags & KILL_FED_REQUEUE)
				job_ptr->job_state &= (~JOB_REQUEUE);
		} else if (job_ptr->batch_flag && (flags & KILL_JOB_BATCH)) {
			_signal_batch_job(job_ptr, signal, flags);
		} else if ((flags & KILL_JOB_BATCH) && !job_ptr->batch_flag) {
			return ESLURM_JOB_SCRIPT_MISSING;
		} else {
			_signal_job(job_ptr, signal, flags);
		}
		verbose("%s: %u of running %s successful 0x%x",
			__func__, signal, jobid2str(job_ptr, jbuf,
						    sizeof(jbuf)),
			job_ptr->job_state);
		return SLURM_SUCCESS;
	}

	verbose("%s: %s can't be sent signal %u from state=%s",
		__func__, jobid2str(job_ptr, jbuf, sizeof(jbuf)), signal,
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

	return _job_signal(job_ptr, signal, flags, uid, preempt);
}

/* Signal all components of a pack job */
extern int pack_job_signal(struct job_record *pack_leader, uint16_t signal,
			    uint16_t flags, uid_t uid, bool preempt)
{
	ListIterator iter;
	int rc = SLURM_SUCCESS, rc1;
	struct job_record *pack_job;

	iter = list_iterator_create(pack_leader->pack_job_list);
	while ((pack_job = (struct job_record *) list_next(iter))) {
		if (pack_leader->pack_job_id != pack_job->pack_job_id) {
			error("%s: Bad pack_job_list for job %u",
			      __func__, pack_leader->pack_job_id);
			continue;
		}
		rc1 = _job_signal(pack_job, signal, flags, uid, preempt);
		if (rc1 != SLURM_SUCCESS)
			rc = rc1;
	}
	list_iterator_destroy(iter);

	return rc;
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
	static time_t sched_update = 0;
	static bool whole_pack = false;
	char *sched_params;
	struct job_record *job_ptr;
	uint32_t job_id;
	time_t now = time(NULL);
	char *end_ptr = NULL, *tok, *tmp;
	long int long_id;
	bitstr_t *array_bitmap = NULL;
	bool valid = true;
	int32_t i, i_first, i_last;
	int rc = SLURM_SUCCESS, rc2, len;

	if (sched_update != slurmctld_conf.last_update) {
		sched_update = slurmctld_conf.last_update;
		sched_params = slurm_get_sched_params();
		if (sched_params) {
			if (strstr(sched_params, "whole_pack"))
				whole_pack = true;
			else
				whole_pack = false;
			xfree(sched_params);
		}
	}

	if (max_array_size == NO_VAL) {
		max_array_size = slurmctld_conf.max_array_sz;
	}

	long_id = strtol(job_id_str, &end_ptr, 10);
	if ((long_id <= 0) || (long_id == LONG_MAX) ||
	    ((end_ptr[0] != '\0') && (end_ptr[0] != '_') &&
	     (end_ptr[0] != '+'))) {
		info("%s(1): invalid job id %s", __func__, job_id_str);
		return ESLURM_INVALID_JOB_ID;
	}
	if ((end_ptr[0] == '_') && (end_ptr[1] == '*'))
		end_ptr += 2;	/* Defaults to full job array */

	if (end_ptr[0] == '+') {	/* Signal pack job element */
		job_id = (uint32_t) long_id;
		long_id = strtol(end_ptr + 1, &end_ptr, 10);
		if ((long_id < 0) || (long_id == LONG_MAX) ||
		    (end_ptr[0] != '\0')) {
			info("%s(2): invalid job id %s", __func__, job_id_str);
			return ESLURM_INVALID_JOB_ID;
		}
		job_ptr = find_job_pack_record(job_id, (uint32_t) long_id);
		if (!job_ptr)
			return ESLURM_ALREADY_DONE;
		if ((job_ptr->user_id != uid) && !validate_operator(uid) &&
		    !assoc_mgr_is_user_acct_coord(acct_db_conn, uid,
						  job_ptr->account)) {
			error("Security violation, REQUEST_KILL_JOB RPC for "
			      "jobID %u from uid %d", job_ptr->job_id, uid);
			return ESLURM_ACCESS_DENIED;
		}
		if (IS_JOB_PENDING(job_ptr))
			return ESLURM_NOT_PACK_WHOLE;
		return _job_signal(job_ptr, signal, flags, uid,preempt);
	}

	last_job_update = now;
	job_id = (uint32_t) long_id;
	if (end_ptr[0] == '\0') {	/* Single job (or full job array) */
		int jobs_done = 0, jobs_signaled = 0;
		struct job_record *job_ptr_done = NULL;
		job_ptr = find_job_record(job_id);
		if (job_ptr && (job_ptr->user_id != uid) &&
		    !validate_operator(uid) &&
		    !assoc_mgr_is_user_acct_coord(acct_db_conn, uid,
						  job_ptr->account)) {
			error("Security violation, REQUEST_KILL_JOB RPC for "
			      "jobID %u from uid %d", job_ptr->job_id, uid);
			return ESLURM_ACCESS_DENIED;
		}
		if (job_ptr && job_ptr->pack_job_list) {   /* Pack leader */
			return pack_job_signal(job_ptr, signal, flags, uid,
						preempt);
		}
		if (job_ptr && job_ptr->pack_job_id && whole_pack) {
			struct job_record *pack_leader;
			pack_leader = find_job_record(job_ptr->pack_job_id);
			if (pack_leader && pack_leader->pack_job_list) {
				return pack_job_signal(pack_leader, signal,
						       flags, uid, preempt);
			}
			error("%s: Job pack leader %u not found",
			      __func__, job_ptr->pack_job_id);
		}
		if (job_ptr && job_ptr->pack_job_id && IS_JOB_PENDING(job_ptr))
			return ESLURM_NOT_PACK_WHOLE;	/* Pack job child */
		if (job_ptr && (job_ptr->array_task_id == NO_VAL) &&
		    (job_ptr->array_recs == NULL)) {
			/* This is a regular job, not a job array */
			return job_signal(job_id, signal, flags, uid, preempt);
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
			rc = _job_signal(job_ptr, signal, flags, uid, preempt);
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
			info("%s(3): invalid job id %u", __func__, job_id);
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
				rc2 = _job_signal(job_ptr, signal, flags, uid,
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
		info("%s(4): invalid job id %s", __func__, job_id_str);
		rc = ESLURM_INVALID_JOB_ID;
		goto endit;
	}

	/* Find some job record and validate the user signaling the job */
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
		info("%s(5): invalid job id %s", __func__, job_id_str);
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
		if (signal == SIGKILL) {
			uint32_t orig_task_cnt, new_task_count;
			/* task_id_bitmap changes, so we need a copy of it */
			bitstr_t *task_id_bitmap_orig =
				bit_copy(job_ptr->array_recs->task_id_bitmap);
			bit_and_not(job_ptr->array_recs->task_id_bitmap,
				array_bitmap);
			xfree(job_ptr->array_recs->task_id_str);
			orig_task_cnt = job_ptr->array_recs->task_cnt;
			new_task_count = bit_set_count(job_ptr->array_recs->
						       task_id_bitmap);
			if (!new_task_count) {
				last_job_update		= now;
				job_ptr->job_state	= JOB_CANCELLED;
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
			}

			/*
			 * Set the task_cnt here since
			 * job_completion_logger needs the total
			 * pending count to handle the acct_policy
			 * limit for submitted jobs correctly.
			 */
			job_ptr->array_recs->task_cnt = new_task_count;
			bit_and_not(array_bitmap, task_id_bitmap_orig);
			FREE_NULL_BITMAP(task_id_bitmap_orig);
		} else {
			bit_and_not(array_bitmap,
				    job_ptr->array_recs->task_id_bitmap);
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
			info("%s(6): invalid job id %u_%d",
			      __func__, job_id, i);
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
	signal_tasks_msg_t *signal_tasks_msg = NULL;
	agent_arg_t *agent_args = NULL;

	xassert(job_ptr);
	xassert(job_ptr->batch_host);
	i = bit_ffs(job_ptr->node_bitmap);
	if (i < 0) {
		error("%s: JobId=%u lacks assigned nodes",
		      __func__, job_ptr->job_id);
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
	signal_tasks_msg = xmalloc(sizeof(signal_tasks_msg_t));
	signal_tasks_msg->job_id      = job_ptr->job_id;
	signal_tasks_msg->job_step_id = NO_VAL;

	if (flags == KILL_FULL_JOB ||
	    flags == KILL_JOB_BATCH ||
	    flags == KILL_STEPS_ONLY)
		signal_tasks_msg->flags = flags;
	signal_tasks_msg->signal = signal;

	agent_args->msg_args = signal_tasks_msg;
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

static int _job_complete(struct job_record *job_ptr, uid_t uid, bool requeue,
			 bool node_fail, uint32_t job_return_code)
{
	struct node_record *node_ptr;
	time_t now = time(NULL);
	uint32_t job_comp_flag = 0;
	bool suspended = false;
	char jbuf[JBUFSIZ];
	int i;
	int use_cloud = false;
	uint16_t over_time_limit;

	xassert(verify_lock(JOB_LOCK, READ_LOCK));
	xassert(verify_lock(FED_LOCK, READ_LOCK));

	if (IS_JOB_FINISHED(job_ptr)) {
		if (job_ptr->exit_code == 0)
			job_ptr->exit_code = job_return_code;
		return ESLURM_ALREADY_DONE;
	}

	if (IS_JOB_COMPLETING(job_ptr))
		return SLURM_SUCCESS;	/* avoid replay */

	if ((job_return_code & 0xff) == SIG_OOM) {
		info("%s: %s OOM failure",  __func__,
		     jobid2str(job_ptr, jbuf, sizeof(jbuf)));
	} else if (WIFSIGNALED(job_return_code)) {
		info("%s: %s WTERMSIG %d",  __func__,
		     jobid2str(job_ptr, jbuf, sizeof(jbuf)),
		     WTERMSIG(job_return_code));
	} else if (WIFEXITED(job_return_code)) {
		info("%s: %s WEXITSTATUS %d",  __func__,
		     jobid2str(job_ptr, jbuf, sizeof(jbuf)),
		     WEXITSTATUS(job_return_code));
	}

	if (IS_JOB_RUNNING(job_ptr))
		job_comp_flag = JOB_COMPLETING;
	else if (IS_JOB_PENDING(job_ptr)) {
		job_return_code = NO_VAL;
		job_ptr->start_time = now;
		fed_mgr_job_revoke_sibs(job_ptr);
	}

	if ((job_return_code == NO_VAL) &&
	    (IS_JOB_RUNNING(job_ptr) || IS_JOB_PENDING(job_ptr))) {
		if (node_fail) {
			info("%s: %s cancelled by node failure",
			     __func__, jobid2str(job_ptr, jbuf, sizeof(jbuf)));
		} else {
			info("%s: %s cancelled by interactive user",
			     __func__, jobid2str(job_ptr, jbuf, sizeof(jbuf)));
		}
	}

	if (IS_JOB_SUSPENDED(job_ptr)) {
		uint32_t suspend_job_state = job_ptr->job_state;
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

		/* clear signal sent flag on requeue */
		job_ptr->warn_flags &= ~WARN_SENT;

		job_ptr->job_state = JOB_PENDING | job_comp_flag;
		/* Since the job completion logger removes the job submit
		 * information, we need to add it again. */
		acct_policy_add_job_submit(job_ptr);
		if (node_fail) {
			info("%s: requeue %s due to node failure",
			     __func__, jobid2str(job_ptr, jbuf, sizeof(jbuf)));
		} else {
			info("%s: requeue %s per user/system request",
			     __func__, jobid2str(job_ptr, jbuf, sizeof(jbuf)));
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
		if (job_ptr->part_ptr &&
		    (job_ptr->part_ptr->over_time_limit != NO_VAL16)) {
			over_time_limit = job_ptr->part_ptr->over_time_limit;
		} else {
			over_time_limit = slurmctld_conf.over_time_limit;
		}

		if (node_fail) {
			job_ptr->job_state = JOB_NODE_FAIL | job_comp_flag;
			job_ptr->requid = uid;
		} else if (job_return_code == NO_VAL) {
			job_ptr->job_state = JOB_CANCELLED | job_comp_flag;
			job_ptr->requid = uid;
		} else if ((job_return_code & 0xff) == SIG_OOM) {
			job_ptr->job_state = JOB_OOM | job_comp_flag;
			job_ptr->exit_code = job_return_code;
			job_ptr->state_reason = FAIL_OOM;
			xfree(job_ptr->state_desc);
		} else if (WIFEXITED(job_return_code) &&
			   WEXITSTATUS(job_return_code)) {
			job_ptr->job_state = JOB_FAILED   | job_comp_flag;
			job_ptr->exit_code = job_return_code;
			job_ptr->state_reason = FAIL_EXIT_CODE;
			xfree(job_ptr->state_desc);
		} else if (WIFSIGNALED(job_return_code)) {
			job_ptr->job_state = JOB_FAILED | job_comp_flag;
			job_ptr->exit_code = job_return_code;
			job_ptr->state_reason = FAIL_LAUNCH;
		} else if (job_comp_flag
			   && ((job_ptr->end_time
				+ over_time_limit * 60) < now)) {
			/* Test if the job has finished before its allowed
			 * over time has expired.
			 */
			job_ptr->job_state = JOB_TIMEOUT  | job_comp_flag;
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

	info("%s: %s done", __func__, jobid2str(job_ptr, jbuf, sizeof(jbuf)));

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
	struct job_record *job_ptr, *job_pack_ptr;
	ListIterator iter;
	int rc, rc1;

	xassert(verify_lock(JOB_LOCK, READ_LOCK));
	xassert(verify_lock(FED_LOCK, READ_LOCK));

	job_ptr = find_job_record(job_id);
	if (job_ptr == NULL) {
		info("%s: invalid JobId=%u", __func__, job_id);
		return ESLURM_INVALID_JOB_ID;
	}

	if ((job_ptr->user_id != uid) && !validate_slurm_user(uid)) {
		error("%s: Security violation, JOB_COMPLETE RPC for job %u "
		      "from uid %u", __func__,
		      job_ptr->job_id, (unsigned int) uid);
		return ESLURM_USER_ID_MISSING;
	}

	if (job_ptr->pack_job_list) {
		rc = SLURM_SUCCESS;
		iter = list_iterator_create(job_ptr->pack_job_list);
		while ((job_pack_ptr = (struct job_record *) list_next(iter))) {
			if (job_ptr->pack_job_id != job_pack_ptr->pack_job_id) {
				error("%s: Bad pack_job_list for job %u",
				      __func__, job_ptr->pack_job_id);
				continue;
			}
			rc1 = _job_complete(job_pack_ptr, uid, requeue,
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

/*
 * Test if this job can use this partition
 *
 * NOTE: This function is also called with a dummy job_desc_msg_t from
 * job_limits_check() if there is any new check added here you may also have to
 * add that parameter to the job_desc_msg_t in that function.
 */
static int _part_access_check(struct part_record *part_ptr,
			      job_desc_msg_t * job_desc, bitstr_t *req_bitmap,
			      uid_t submit_uid, slurmdb_qos_rec_t *qos_ptr,
			      char *acct)
{
	uint32_t total_nodes, min_nodes_tmp, max_nodes_tmp;
	uint32_t job_min_nodes, job_max_nodes;
	int rc = SLURM_SUCCESS;

	if ((part_ptr->flags & PART_FLAG_REQ_RESV) &&
	    (!job_desc->reservation || !strlen(job_desc->reservation))) {
		debug2("%s: uid %u access to partition %s "
		     "denied, requires reservation", __func__,
		     (unsigned int) submit_uid, part_ptr->name);
		return ESLURM_ACCESS_DENIED;
	}

	if ((part_ptr->flags & PART_FLAG_ROOT_ONLY) && (submit_uid != 0) &&
	    (submit_uid != slurmctld_conf.slurm_user_id)) {
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

	if (validate_group(part_ptr, job_desc->user_id) == 0) {
		debug2("%s: uid %u access to partition %s "
		     "denied, bad group", __func__,
		     (unsigned int) job_desc->user_id, part_ptr->name);
		return ESLURM_JOB_MISSING_REQUIRED_PARTITION_GROUP;
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
	select_g_alter_node_cnt(SELECT_APPLY_NODE_MAX_OFFSET, &total_nodes);
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

	/* The node counts have not been altered yet, so do not figure them out
	 * by using the cpu counts.  The partitions have already been altered
	 * so we have to use the original values.
	 */
	job_min_nodes = job_desc->min_nodes;
	job_max_nodes = job_desc->max_nodes;
#ifdef HAVE_BG
	min_nodes_tmp = part_ptr->min_nodes_orig;
	max_nodes_tmp = part_ptr->max_nodes_orig;
#else
	min_nodes_tmp = part_ptr->min_nodes;
	max_nodes_tmp = part_ptr->max_nodes;
#endif

	/* Check against min/max node limits in the partition */

	if ((part_ptr->state_up & PARTITION_SCHED) &&
	    (job_min_nodes != NO_VAL) &&
	    (job_min_nodes < min_nodes_tmp) &&
	    (!qos_ptr || (qos_ptr && !(qos_ptr->flags
				       & QOS_FLAG_PART_MIN_NODE)))) {
		debug2("%s: Job requested for nodes (%u) "
		     "smaller than partition %s(%u) min nodes", __func__,
		     job_min_nodes, part_ptr->name, min_nodes_tmp);
		return  ESLURM_INVALID_NODE_COUNT;
	}

	if ((part_ptr->state_up & PARTITION_SCHED) &&
	    (job_max_nodes != NO_VAL) &&
	    (job_max_nodes > max_nodes_tmp) &&
	    (!qos_ptr || (qos_ptr && !(qos_ptr->flags
				       & QOS_FLAG_PART_MAX_NODE)))) {
		debug2("%s: Job requested for nodes (%u) greater than partition"
		     " %s(%u) max nodes", __func__, job_max_nodes,
		     part_ptr->name, max_nodes_tmp);
		return ESLURM_INVALID_NODE_COUNT;
	}

	if ((part_ptr->state_up & PARTITION_SCHED) &&
	    (job_desc->time_limit != NO_VAL) &&
	    (job_desc->time_limit > part_ptr->max_time) &&
	    (!qos_ptr || !(qos_ptr->flags & QOS_FLAG_PART_TIME_LIMIT))) {
		debug2("%s: Job time limit (%u) exceeds limit of partition "
		     "%s(%u)", __func__, job_desc->time_limit, part_ptr->name,
		     part_ptr->max_time);
		return ESLURM_INVALID_TIME_LIMIT;
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
			  List *part_pptr_list,
			  char **err_msg)
{
	struct part_record *part_ptr = NULL, *part_ptr_new = NULL;
	List part_ptr_list = NULL;
	int rc = SLURM_SUCCESS;

	/* Identify partition(s) and set pointer(s) to their struct */
	if (job_desc->partition) {
		char *err_part = NULL;
		part_ptr = find_part_record(job_desc->partition);
		if (part_ptr == NULL) {
			part_ptr_list = get_part_list(job_desc->partition,
						      &err_part);
			if (part_ptr_list)
				part_ptr = list_peek(part_ptr_list);
		}
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
			   slurmdb_assoc_rec_t *assoc_ptr,
			   slurmdb_qos_rec_t *qos_ptr)
{
	int rc = SLURM_SUCCESS;
	struct part_record *part_ptr = *part_pptr, *part_ptr_tmp;
	slurmdb_assoc_rec_t assoc_rec;
	uint32_t min_nodes_orig = INFINITE, max_nodes_orig = 1;
	uint32_t max_time = 0;
	bool any_check = false;

	/* Change partition pointer(s) to alternates as needed */
	if (part_ptr_list) {
		int fail_rc = SLURM_SUCCESS;
		ListIterator iter = list_iterator_create(part_ptr_list);

		while ((part_ptr_tmp = (struct part_record *)list_next(iter))) {
			/* FIXME: When dealing with multiple partitions we
			 * currently can't deal with partition based
			 * associations.
			 */
			memset(&assoc_rec, 0, sizeof(slurmdb_assoc_rec_t));
			if (assoc_ptr) {
				assoc_rec.acct      = assoc_ptr->acct;
				assoc_rec.partition = part_ptr_tmp->name;
				assoc_rec.uid       = job_desc->user_id;
				(void) assoc_mgr_fill_in_assoc(
					acct_db_conn, &assoc_rec,
					accounting_enforce, NULL, false);
			}

			if (assoc_ptr && assoc_rec.id != assoc_ptr->id) {
				info("%s: can't check multiple "
				     "partitions with partition based "
				     "associations", __func__);
				rc = SLURM_ERROR;
			} else {
				rc = _part_access_check(part_ptr_tmp, job_desc,
							req_bitmap, submit_uid,
							qos_ptr, assoc_ptr ?
							assoc_ptr->acct : NULL);
			}
			if ((rc != SLURM_SUCCESS) &&
			    ((rc == ESLURM_ACCESS_DENIED) ||
			     (rc == ESLURM_USER_ID_MISSING) ||
			     (rc == ESLURM_JOB_MISSING_REQUIRED_PARTITION_GROUP) ||
			     (slurmctld_conf.enforce_part_limits ==
			      PARTITION_ENFORCE_ALL))) {
				break;
			} else if (rc != SLURM_SUCCESS) {
				fail_rc = rc;
			} else {
				any_check = true;
			}

			// Set to success since we found a usable partition
			if (any_check && slurmctld_conf.enforce_part_limits ==
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
		    (slurmctld_conf.enforce_part_limits &&
		     (fail_rc != SLURM_SUCCESS))) {
			if (slurmctld_conf.enforce_part_limits ==
			    PARTITION_ENFORCE_ALL)
				rc = fail_rc;
			else if (slurmctld_conf.enforce_part_limits ==
				 PARTITION_ENFORCE_ANY && !any_check)
				rc = fail_rc;
			else {
				rc = ESLURM_PARTITION_NOT_AVAIL;
			}
			goto fini;
		}
		rc = SLURM_SUCCESS;	/* At least some partition
					 * usable */
	} else {
		min_nodes_orig = part_ptr->min_nodes_orig;
		max_nodes_orig = part_ptr->max_nodes_orig;
		max_time = part_ptr->max_time;
		rc = _part_access_check(part_ptr, job_desc, req_bitmap,
					submit_uid, qos_ptr,
					assoc_ptr ? assoc_ptr->acct : NULL);
		if ((rc != SLURM_SUCCESS) &&
		    ((rc == ESLURM_ACCESS_DENIED) ||
		     (rc == ESLURM_USER_ID_MISSING) ||
		     (rc == ESLURM_JOB_MISSING_REQUIRED_PARTITION_GROUP) ||
		     slurmctld_conf.enforce_part_limits))
			goto fini;
		// Enforce Part Limit = no
		rc = SLURM_SUCCESS;
	}

	/* Validate job limits against partition limits */

	// Check Partition with the highest limits when there are
	// muliple
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
	    slurmctld_conf.enforce_part_limits &&
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
	if ((job_desc->min_nodes == 0) && (job_desc->script == NULL)) {
		info("%s: min_nodes==0 for non-batch job", __func__);
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
		rc = ESLURM_INVALID_TIME_LIMIT;
		goto fini;
	}
	if ((job_desc->time_limit != NO_VAL) &&
	    (job_desc->time_limit >  max_time) &&
	    (job_desc->time_min   == NO_VAL) &&
	    slurmctld_conf.enforce_part_limits &&
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
		rc = ESLURM_INVALID_TIME_LIMIT;
		goto fini;
	}
	if ((job_desc->deadline) && (job_desc->deadline != NO_VAL)) {
		char time_str_now[32];
		char time_str_deadline[32];
		time_t now = time(NULL);
		slurm_make_time_str(&job_desc->deadline, time_str_deadline,
				    sizeof(time_str_deadline));
		slurm_make_time_str(&now, time_str_now, sizeof(time_str_now));
		if (job_desc->deadline < now) {
			info("%s: job's deadline smaller than now (%s < %s)",
			     __func__, time_str_deadline, time_str_now);
			rc = ESLURM_INVALID_TIME_LIMIT;
			goto fini;
		}
		if ((job_desc->time_min) && (job_desc->time_min != NO_VAL) &&
		    (job_desc->deadline < (now + job_desc->time_min * 60))) {
			info("%s: job's min_time greater than deadline (%u > %s)",
			     __func__, job_desc->time_min, time_str_deadline);
			rc = ESLURM_INVALID_TIME_LIMIT;
			goto fini;
		}
		if ((job_desc->time_min == 0) && (job_desc->time_limit) &&
		    (job_desc->time_limit != NO_VAL) &&
		    (job_desc->deadline < (now + job_desc->time_limit * 60))) {
			info("%s: job's time_limit greater than deadline (%u > %s)",
			     __func__, job_desc->time_limit, time_str_deadline);
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
extern int job_limits_check(struct job_record **job_pptr, bool check_min_time)
{
	struct job_details *detail_ptr;
	enum job_state_reason fail_reason;
	struct part_record *part_ptr = NULL;
	struct job_record *job_ptr = NULL;
	slurmdb_qos_rec_t  *qos_ptr;
	slurmdb_assoc_rec_t *assoc_ptr;
	job_desc_msg_t job_desc;
	int rc;

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
	if (!detail_ptr) {
		fatal("job %u has NULL details_ptr", job_ptr->job_id);
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
#ifdef HAVE_BG
	/*
	 * The node counts have been altered to reflect slurm nodes instead of
	 * cnodes, so we need to figure out the cnode count
	 * by using the cpu counts.  The partitions have been altered as well
	 * so we have to use the original values.
	 */
	job_desc.min_nodes = detail_ptr->orig_min_cpus / cpus_per_node;
	job_desc.max_nodes = detail_ptr->orig_max_cpus / cpus_per_node;
#else
	job_desc.min_nodes = detail_ptr->min_nodes;
	/* _part_access_check looks for NO_VAL instead of 0 */
	job_desc.max_nodes = detail_ptr->max_nodes ?
		detail_ptr->max_nodes : NO_VAL;;
#endif
	if (check_min_time && job_ptr->time_min)
		job_desc.time_limit = job_ptr->time_min;
	else
		job_desc.time_limit = job_ptr->time_limit;

	if ((rc = _part_access_check(part_ptr, &job_desc, NULL,
				     job_ptr->user_id, qos_ptr,
				     job_ptr->account))) {
		debug2("Job %u can't run in partition %s: %s",
		       job_ptr->job_id, part_ptr->name, slurm_strerror(rc));
		switch (rc) {
		case ESLURM_INVALID_TIME_LIMIT:
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
		debug2("Job %u requested down partition %s",
		       job_ptr->job_id, part_ptr->name);
		fail_reason = WAIT_PART_DOWN;
	} else if (part_ptr->state_up == PARTITION_INACTIVE) {
		debug2("Job %u requested inactive partition %s",
		       job_ptr->job_id, part_ptr->name);
		fail_reason = WAIT_PART_INACTIVE;
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
			debug2("Job %u exceeds usage threshold",
			       job_ptr->job_id);
			fail_reason = WAIT_QOS_THRES;
		}
	} else if (fail_reason == WAIT_NO_REASON) {
		if (!_valid_pn_min_mem(job_ptr, part_ptr)) {
			/* debug2 message already logged inside the function. */
			fail_reason = WAIT_PN_MEM_LIMIT;
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

static int _job_create(job_desc_msg_t *job_desc, int allocate, int will_run,
		       struct job_record **job_pptr, uid_t submit_uid,
		       char **err_msg, uint16_t protocol_version)
{
	static int launch_type_poe = -1;
	int error_code = SLURM_SUCCESS, i, qos_error;
	struct part_record *part_ptr = NULL;
	List part_ptr_list = NULL;
	bitstr_t *req_bitmap = NULL, *exc_bitmap = NULL;
	struct job_record *job_ptr = NULL;
	slurmdb_assoc_rec_t assoc_rec, *assoc_ptr = NULL;
	List license_list = NULL, gres_list = NULL;
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
		if (xstrcmp(slurmctld_conf.select_type, "select/serial"))
			select_serial = 0;
		else
			select_serial = 1;
	}

	memset(&acct_policy_limit_set, 0, sizeof(acct_policy_limit_set_t));
	acct_policy_limit_set.tres =
		xmalloc(sizeof(uint16_t) * slurmctld_tres_cnt);

	*job_pptr = (struct job_record *) NULL;

	user_submit_priority = job_desc->priority;

	/* ensure that selected nodes are in this partition */
	if (job_desc->req_nodes) {
		error_code = node_name2bitmap(job_desc->req_nodes, false,
					      &req_bitmap);
		if (error_code) {
			error_code = ESLURM_INVALID_NODE_NAME;
			goto cleanup_fail;
		}
		if ((job_desc->contiguous != NO_VAL16) &&
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
			info("%s: max node count less than required hostlist "
			     "size for user %u", __func__, job_desc->user_id);
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
		info("%s: max_nodes == 0", __func__);
		error_code = ESLURM_INVALID_NODE_COUNT;
		goto cleanup_fail;
	}

	error_code = _get_job_parts(job_desc, &part_ptr, &part_ptr_list,
				    err_msg);
	if (error_code != SLURM_SUCCESS)
		goto cleanup_fail;


	memset(&assoc_rec, 0, sizeof(slurmdb_assoc_rec_t));
	assoc_rec.acct      = job_desc->account;
	assoc_rec.partition = part_ptr->name;
	assoc_rec.uid       = job_desc->user_id;
	/* Checks are done later to validate assoc_ptr, so we don't
	   need to lock outside of fill_in_assoc.
	*/
	if (assoc_mgr_fill_in_assoc(acct_db_conn, &assoc_rec,
				    accounting_enforce, &assoc_ptr, false)) {
		info("%s: invalid account or partition for user %u, "
		     "account '%s', and partition '%s'", __func__,
		     job_desc->user_id, assoc_rec.acct, assoc_rec.partition);
		error_code = ESLURM_INVALID_ACCOUNT;
		goto cleanup_fail;
	} else if (association_based_accounting &&
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
					       false);
		if (assoc_ptr) {
			info("%s: account '%s' has no association for user %u "
			     "using default account '%s'",
			     __func__, job_desc->account, job_desc->user_id,
			     assoc_rec.acct);
			xfree(job_desc->account);
		}
	}

	if (job_desc->account == NULL)
		job_desc->account = xstrdup(assoc_rec.acct);

	/* This must be done after we have the assoc_ptr set */
	memset(&qos_rec, 0, sizeof(slurmdb_qos_rec_t));
	qos_rec.name = job_desc->qos;

	qos_ptr = _determine_and_validate_qos(
		job_desc->reservation, assoc_ptr, false, &qos_rec, &qos_error,
		false);

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

	job_desc->tres_req_cnt = xmalloc(sizeof(uint64_t) * slurmctld_tres_cnt);
	job_desc->tres_req_cnt[TRES_ARRAY_NODE] = job_desc->min_nodes;
	job_desc->tres_req_cnt[TRES_ARRAY_CPU] = job_desc->min_cpus;
	job_desc->tres_req_cnt[TRES_ARRAY_MEM] =  job_get_tres_mem(
		job_desc->pn_min_memory,
		job_desc->tres_req_cnt[TRES_ARRAY_CPU],
		job_desc->min_nodes);

	license_list = license_validate(job_desc->licenses,
					job_desc->tres_req_cnt, &valid);
	if (!valid) {
		info("Job's requested licenses are invalid: %s",
		     job_desc->licenses);
		error_code = ESLURM_INVALID_LICENSES;
		goto cleanup_fail;
	}

	if ((error_code =
	     gres_plugin_job_state_validate(&job_desc->gres, &gres_list)))
		goto cleanup_fail;

	gres_set_job_tres_cnt(gres_list,
			      job_desc->min_nodes,
			      job_desc->tres_req_cnt,
			      false);

	if ((error_code = bb_g_job_validate(job_desc, submit_uid))
	    != SLURM_SUCCESS)
		goto cleanup_fail;

	if (job_desc->deadline && (job_desc->time_limit == NO_VAL) &&
	    (job_desc->time_min == NO_VAL))
		job_desc->time_min = 1;
	if ((accounting_enforce & ACCOUNTING_ENFORCE_LIMITS) &&
	    (!acct_policy_validate(job_desc, part_ptr,
				   assoc_ptr, qos_ptr, NULL,
				   &acct_policy_limit_set, 0))) {
		info("%s: exceeded association/QOS limit for user %u",
		     __func__, job_desc->user_id);
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
	if (geo[0] == NO_VAL16) {
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
	if (reboot == NO_VAL16) {
		reboot = 0;	/* default is no reboot */
		select_g_select_jobinfo_set(job_desc->select_jobinfo,
					    SELECT_JOBDATA_REBOOT, &reboot);
	}
	select_g_select_jobinfo_get(job_desc->select_jobinfo,
				    SELECT_JOBDATA_ROTATE, &rotate);
	if (rotate == NO_VAL16) {
		rotate = 1;	/* refault is to rotate */
		select_g_select_jobinfo_set(job_desc->select_jobinfo,
					    SELECT_JOBDATA_ROTATE, &rotate);
	}
	select_g_select_jobinfo_get(job_desc->select_jobinfo,
				    SELECT_JOBDATA_CONN_TYPE, &conn_type);

	if ((conn_type[0] != NO_VAL16)
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
		if (conn_type[i] == NO_VAL16)
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
	job_ptr->last_sched_eval = time(NULL);

	part_ptr_list = NULL;
	if ((error_code = checkpoint_alloc_jobinfo(&(job_ptr->check_job)))) {
		error("Failed to allocate checkpoint info for job");
		goto cleanup_fail;
	}

	memcpy(&job_ptr->limit_set, &acct_policy_limit_set,
	       sizeof(acct_policy_limit_set_t));
	acct_policy_limit_set.tres = NULL;

	job_ptr->assoc_id = assoc_rec.id;
	job_ptr->assoc_ptr = (void *) assoc_ptr;
	job_ptr->qos_ptr = (void *) qos_ptr;
	job_ptr->qos_id = qos_rec.id;

	if (mcs_g_set_mcs_label(job_ptr, job_desc->mcs_label) != 0 ) {
		if (job_desc->mcs_label == NULL)
			error("Failed to create job : no valid mcs_label found");
		else
			error("Failed to create job : invalid mcs-label : %s",
				job_desc->mcs_label);
		error_code = ESLURM_INVALID_MCS_LABEL;
		goto cleanup_fail;
	}

	if (launch_type_poe == -1) {
		if (!xstrcmp(slurmctld_conf.launch_type, "launch/poe"))
			launch_type_poe = 1;
		else
			launch_type_poe = 0;
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
	if (!job_ptr->details->expanding_jobid) {
		job_ptr->gres_list = gres_list;
		gres_list = NULL;
	}

	job_ptr->gres_detail_cnt = 0;
	job_ptr->gres_detail_str = NULL;
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

	FREE_NULL_LIST(license_list);
	FREE_NULL_LIST(gres_list);
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
		purge_job_record(job_ptr->job_id);
		*job_pptr = (struct job_record *) NULL;
	}
	FREE_NULL_LIST(license_list);
	xfree(acct_policy_limit_set.tres);
	FREE_NULL_LIST(gres_list);
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

	if (tok[0] == '[')	/* Strip leading "[" */
		tok++;
	first = strtol(tok, &end_ptr, 10);
	if (end_ptr[0] == ']')	/* Strip trailing "]" */
		end_ptr++;
	if (first < 0)
		return false;
	if (end_ptr[0] == '-') {
		last = strtol(end_ptr + 1, &end_ptr, 10);
		if (end_ptr[0] == ']')	/* Strip trailing "]" */
			end_ptr++;
		if (end_ptr[0] == ':') {
			step = strtol(end_ptr + 1, &end_ptr, 10);
			if (end_ptr[0] == ']')	/* Strip trailing "]" */
				end_ptr++;
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
	static time_t sched_update = 0;
	static uint32_t max_task_cnt = NO_VAL;
	char *sched_params, *key;
	uint32_t task_cnt;
	bool valid = true;
	char *tmp, *tok, *last = NULL;

	FREE_NULL_BITMAP(job_desc->array_bitmap);
	if (!job_desc->array_inx || !job_desc->array_inx[0])
		return true;
	if (!job_desc->script || !job_desc->script[0])
		return false;

	if (max_array_size == NO_VAL) {
		max_array_size = slurmctld_conf.max_array_sz;
	}
	if (max_array_size == 0) {
		verbose("Job arrays disabled, MaxArraySize=0");
		return false;
	}

	if (sched_update != slurmctld_conf.last_update) {
		max_task_cnt = max_array_size;
		sched_update = slurmctld_conf.last_update;
		if ((sched_params = slurm_get_sched_params()) &&
		    (key = strcasestr(sched_params, "max_array_tasks="))) {
			key += 16;
			max_task_cnt = atoi(key);
		}
		xfree(sched_params);
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
	static int max_script = -1;
	char *sched_params, *tmp_ptr;

	if (max_script == -1) {
		max_script = 4 * 1024 * 1024;
		sched_params = slurm_get_sched_params();
		if (sched_params &&
		    (tmp_ptr = strstr(sched_params, "max_script_size="))) {
			max_script = atoi(tmp_ptr + 16);
		}
		xfree(sched_params);
	}

	if (_test_strlen(job_desc->account, "account", 1024)		||
	    _test_strlen(job_desc->alloc_node, "alloc_node", 1024)	||
	    _test_strlen(job_desc->array_inx, "array_inx", 1024 * 4)	||
	    _test_strlen(job_desc->blrtsimage, "blrtsimage", 1024)	||
	    _test_strlen(job_desc->burst_buffer, "burst_buffer",1024*8) ||
	    _test_strlen(job_desc->ckpt_dir, "ckpt_dir", 1024)		||
	    _test_strlen(job_desc->comment, "comment", 1024)		||
	    _test_strlen(job_desc->cpu_bind, "cpu-bind", 1024 * 128)	||
	    _test_strlen(job_desc->dependency, "dependency", 1024*128)	||
	    _test_strlen(job_desc->features, "features", 1024)		||
	    _test_strlen(
		job_desc->cluster_features, "cluster_features", 1024)   ||
	    _test_strlen(job_desc->gres, "gres", 1024)			||
	    _test_strlen(job_desc->licenses, "licenses", 1024)		||
	    _test_strlen(job_desc->linuximage, "linuximage", 1024)	||
	    _test_strlen(job_desc->mail_user, "mail_user", 1024)	||
	    _test_strlen(job_desc->mcs_label, "mcs_label", 1024)	||
	    _test_strlen(job_desc->mem_bind, "mem-bind", 1024 * 128)	||
	    _test_strlen(job_desc->mloaderimage, "mloaderimage", 1024)	||
	    _test_strlen(job_desc->name, "name", 1024)			||
	    _test_strlen(job_desc->network, "network", 1024)		||
	    _test_strlen(job_desc->partition, "partition", 1024)	||
	    _test_strlen(job_desc->qos, "qos", 1024)			||
	    _test_strlen(job_desc->ramdiskimage, "ramdiskimage", 1024)	||
	    _test_strlen(job_desc->reservation, "reservation", 1024)	||
	    _test_strlen(job_desc->script, "script", max_script)	||
	    _test_strlen(job_desc->std_err, "std_err", MAXPATHLEN)	||
	    _test_strlen(job_desc->std_in, "std_in", MAXPATHLEN)	||
	    _test_strlen(job_desc->std_out, "std_out", MAXPATHLEN)	||
	    _test_strlen(job_desc->wckey, "wckey", 1024)		||
	    _test_strlen(job_desc->work_dir, "work_dir", MAXPATHLEN))
		return ESLURM_PATHNAME_TOO_LONG;

	return SLURM_SUCCESS;
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

	/*
	 * Check user permission for negative 'nice' and non-0 priority values
	 * (restricted to root, SlurmUser, or SLURMDB_ADMIN_OPERATOR) _before_
	 * running the job_submit plugin.
	 * Also prevent unpriviledged users from submitting jobs with an
	 * AdminComment field set.
	 */
	if (!validate_operator(submit_uid)) {
		if (job_desc->priority != 0)
			job_desc->priority = NO_VAL;
		if (job_desc->nice < NICE_OFFSET)
			job_desc->nice = NICE_OFFSET;

		if (job_desc->admin_comment)
			return ESLURM_ACCESS_DENIED;
	}

	rc = job_submit_plugin_submit(job_desc, (uint32_t) submit_uid, err_msg);
	if (rc != SLURM_SUCCESS)
		return rc;
	rc = node_features_g_job_valid(job_desc->features);
	if (rc != SLURM_SUCCESS)
		return rc;

	rc = _test_job_desc_fields(job_desc);
	if (rc != SLURM_SUCCESS)
		return rc;

	if (!_valid_array_inx(job_desc))
		return ESLURM_INVALID_ARRAY;

	if (job_desc->x11 && !(slurmctld_conf.prolog_flags & PROLOG_FLAG_X11))
		return ESLURM_X11_NOT_AVAIL;

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

	/* If max nodes is different than min nodes don't set tasks or
	 * it will hard code the range.
	 */
	if ((job_desc->ntasks_per_node != NO_VAL16) &&
	    (job_desc->min_nodes       != NO_VAL) &&
	    (job_desc->num_tasks       == NO_VAL)) {
		job_desc->num_tasks =
			job_desc->ntasks_per_node * job_desc->min_nodes;
	}

	/* Only set min and max cpus if overcommit isn't set */
	if ((job_desc->overcommit == NO_VAL8) &&
	    (job_desc->min_cpus   != NO_VAL)  &&
	    (job_desc->num_tasks  != NO_VAL)  &&
	    (job_desc->num_tasks > job_desc->min_cpus)) {
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

	return SLURM_SUCCESS;
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

	/* Create directory based upon job ID due to limitations on the number
	 * of files possible in a directory on some file system types (e.g.
	 * up to 64k files on a FAT32 file system). */
	hash = job_id % 10;
	dir_name = xstrdup_printf("%s/hash.%d",
				  slurmctld_conf.state_save_location, hash);
	(void) mkdir(dir_name, 0700);

	/* Create job_id specific directory */
	xstrfmtcat(dir_name, "/job.%u", job_id);
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
	file_name = xstrdup_printf("%s/environment", dir_name);
	error_code = _write_data_array_to_file(file_name,
					       job_desc->environment,
					       job_desc->env_size);
	xfree(file_name);

	if (error_code == 0) {
		/* Create script file */
		file_name = xstrdup_printf("%s/script", dir_name);
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
	char *dir_name_src;
	struct stat buf;
	int rc, hash = job_id % 10;

	dir_name_src = xstrdup_printf("%s/hash.%d/job.%u",
				      slurmctld_conf.state_save_location,
				      hash, job_id);
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
	char *file_name = NULL, **environment = NULL;
	int cc, fd = -1, hash;
	uint32_t use_id;

	use_id = (job_ptr->array_task_id != NO_VAL) ?
		job_ptr->array_job_id : job_ptr->job_id;
	hash = use_id % 10;
	file_name = xstrdup_printf("%s/hash.%d/job.%u/environment",
				   slurmctld_conf.state_save_location,
				   hash, use_id);
	fd = open(file_name, 0);

	if (fd >= 0) {
		cc = _read_data_array_from_file(fd, file_name, &environment,
						env_size, job_ptr);
		if (cc < 0)
			environment = NULL;
		close(fd);
	} else {
		error("Could not open environment file for job %u",
		      job_ptr->job_id);
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
	char *file_name = NULL, *script = NULL;
	int fd = -1, hash;
	uint32_t use_id;

	if (!job_ptr->batch_flag)
		return NULL;

	use_id = (job_ptr->array_task_id != NO_VAL) ?
		job_ptr->array_job_id : job_ptr->job_id;
	hash = use_id % 10;
	file_name = xstrdup_printf("%s/hash.%d/job.%u/script",
				   slurmctld_conf.state_save_location,
				   hash, use_id);
	fd = open(file_name, 0);

	if (fd >= 0) {
		_read_data_from_file(fd, file_name, &script);
		close(fd);
	} else {
		error("Could not open script file for job %u", job_ptr->job_id);
	}

	xfree(file_name);
	return script;
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
static int
_read_data_array_from_file(int fd, char *file_name, char ***data,
			    uint32_t * size, struct job_record *job_ptr)
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
 * IN fd - file descriptor to read from
 * IN file_name - file to read from
 * OUT data - pointer to  string
 *	must be xfreed when no longer needed
 * RET - 0 on success, -1 on error
 */
static int _read_data_from_file(int fd, char *file_name, char **data)
{
	struct stat stat_buf;
	int pos, buf_size, amount, count;
	char *buffer;

	xassert(file_name);
	xassert(data);
	*data = NULL;

	if (fstat(fd, &stat_buf)) {
		error("%s: Unable to stat file %s", __func__, file_name);
		return -1;
	}

	pos = 0;
	buf_size = stat_buf.st_size;
	buffer = xmalloc(buf_size);
	while (pos < buf_size) {
		count = buf_size - pos;
		amount = read(fd, &buffer[pos], count);
		if (amount < 0) {
			if (errno == EINTR)
				continue;
			error("%s: Error reading file %s, %m",
			      __func__, file_name);
			xfree(buffer);
			close(fd);
			return -1;
		}
		if (amount < count) {	/* end of file */
			error("%s: File %s shortened??", __func__, file_name);
			break;
		}
		pos += amount;
	}

	*data = buffer;
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
	else if (slurmctld_conf.select_type_param & CR_ONE_TASK_PER_CORE)
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
	char *sched_params;

	if (!job_desc->script)
		return 0;

	if ((default_batch_wait != NO_VAL16) &&
	    (sched_update == slurmctld_conf.last_update))
		return default_batch_wait;

	sched_params = slurm_get_sched_params();
	if (sched_params && strstr(sched_params, "sbatch_wait_nodes"))
		default_batch_wait = 1;
	else
		default_batch_wait = 0;
	xfree(sched_params);
	sched_update = slurmctld_conf.last_update;

	return default_batch_wait;
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

	job_ptr = _create_job_record(1);
	if (!job_ptr)
		return SLURM_ERROR;

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

	/* Since this is only used in the slurmctld copy it now.
	 */
	job_ptr->tres_req_cnt = job_desc->tres_req_cnt;
	job_desc->tres_req_cnt = NULL;
	set_job_tres_req_str(job_ptr, false);
	_add_job_hash(job_ptr);

	job_ptr->user_id    = (uid_t) job_desc->user_id;
	job_ptr->group_id   = (gid_t) job_desc->group_id;
	job_ptr->job_state  = JOB_PENDING;
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
	job_ptr->burst_buffer = xstrdup(job_desc->burst_buffer);
	job_ptr->gres       = xstrdup(job_desc->gres);
	job_ptr->network    = xstrdup(job_desc->network);
	job_ptr->resv_name  = xstrdup(job_desc->reservation);
	job_ptr->restart_cnt = job_desc->restart_cnt;
	job_ptr->comment    = xstrdup(job_desc->comment);
	job_ptr->admin_comment = xstrdup(job_desc->admin_comment);

	if (job_desc->kill_on_node_fail != NO_VAL16)
		job_ptr->kill_on_node_fail = job_desc->kill_on_node_fail;

	job_ptr->resp_host = xstrdup(job_desc->resp_host);
	job_ptr->alloc_resp_port = job_desc->alloc_resp_port;
	job_ptr->other_port = job_desc->other_port;
	job_ptr->power_flags = job_desc->power_flags;
	job_ptr->time_last_active = time(NULL);
	job_ptr->cr_enabled = 0;
	job_ptr->derived_ec = 0;

	job_ptr->licenses  = xstrdup(job_desc->licenses);
	job_ptr->mail_type = job_desc->mail_type;
	job_ptr->mail_user = xstrdup(job_desc->mail_user);
	job_ptr->bit_flags = job_desc->bitflags;
	job_ptr->bit_flags &= ~BACKFILL_TEST;
	job_ptr->ckpt_interval = job_desc->ckpt_interval;
	job_ptr->spank_job_env = job_desc->spank_job_env;
	job_ptr->spank_job_env_size = job_desc->spank_job_env_size;
	job_desc->spank_job_env = (char **) NULL; /* nothing left to free */
	job_desc->spank_job_env_size = 0;         /* nothing left to free */
	job_ptr->mcs_label = xstrdup(job_desc->mcs_label);
	job_ptr->origin_cluster = xstrdup(job_desc->origin_cluster);

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
	detail_ptr->extra      = job_desc->extra;
	detail_ptr->nice       = job_desc->nice;
	detail_ptr->open_mode  = job_desc->open_mode;
	detail_ptr->min_cpus   = job_desc->min_cpus;
	detail_ptr->orig_min_cpus   = job_desc->min_cpus;
	detail_ptr->max_cpus   = job_desc->max_cpus;
	detail_ptr->orig_max_cpus   = job_desc->max_cpus;
	detail_ptr->min_nodes  = job_desc->min_nodes;
	detail_ptr->max_nodes  = job_desc->max_nodes;
	detail_ptr->pn_min_memory = job_desc->pn_min_memory;
	detail_ptr->orig_pn_min_memory = job_desc->pn_min_memory;
	detail_ptr->x11        = job_desc->x11;
	detail_ptr->x11_magic_cookie = xstrdup(job_desc->x11_magic_cookie);
	/* no x11_target_host, alloc_nodes is the same */
	detail_ptr->x11_target_port = job_desc->x11_target_port;
	if (job_desc->req_nodes) {
		detail_ptr->req_nodes =
			_copy_nodelist_no_dup(job_desc->req_nodes);
		detail_ptr->req_node_bitmap = *req_bitmap;
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
	if (job_desc->cluster_features)
		detail_ptr->cluster_features =
			xstrdup(job_desc->cluster_features);
	if (job_desc->fed_siblings_viable) {
		job_ptr->fed_details = xmalloc(sizeof(job_fed_details_t));
		job_ptr->fed_details->siblings_viable =
			job_desc->fed_siblings_viable;
		update_job_fed_details(job_ptr);
	}
	if ((job_desc->shared == JOB_SHARED_NONE) && (select_serial == 0)) {
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
	} else {
		detail_ptr->share_res  = NO_VAL8;
		detail_ptr->whole_node = 0;
	}
	if (job_desc->contiguous != NO_VAL16)
		detail_ptr->contiguous = job_desc->contiguous;
	if (slurm_get_use_spec_resources())
		detail_ptr->core_spec = job_desc->core_spec;
	else
		detail_ptr->core_spec = NO_VAL16;
	if (detail_ptr->core_spec != NO_VAL16)
		detail_ptr->whole_node = 1;
	if (job_desc->task_dist != NO_VAL)
		detail_ptr->task_dist = job_desc->task_dist;
	if (job_desc->cpus_per_task != NO_VAL16)
		detail_ptr->cpus_per_task = MAX(job_desc->cpus_per_task, 1);
	else
		detail_ptr->cpus_per_task = 1;
	if (job_desc->pn_min_cpus != NO_VAL16)
		detail_ptr->pn_min_cpus = job_desc->pn_min_cpus;
	if (job_desc->overcommit != NO_VAL8)
		detail_ptr->overcommit = job_desc->overcommit;
	if (job_desc->ntasks_per_node != NO_VAL16) {
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
	if (job_desc->reboot != NO_VAL16)
		job_ptr->reboot = MIN(job_desc->reboot, 1);
	else
		job_ptr->reboot = 0;
	if (job_desc->requeue != NO_VAL16)
		detail_ptr->requeue = MIN(job_desc->requeue, 1);
	else
		detail_ptr->requeue = slurmctld_conf.job_requeue;
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

	job_ptr->clusters = xstrdup(job_desc->clusters);

	/* The priority needs to be set after this since we don't have
	 * an association rec yet
	 */
	detail_ptr->mc_ptr = _set_multi_core_data(job_desc);

	if ((job_ptr->bit_flags & SPREAD_JOB) && (detail_ptr->max_nodes == 0) &&
	    (detail_ptr->num_tasks != 0)) {
		if (detail_ptr->min_nodes == 0)
			detail_ptr->min_nodes = 1;
		detail_ptr->max_nodes =
			MIN(node_record_count, detail_ptr->num_tasks);
	}

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

/* Return the number of CPUs on the first node in the identified partition */
static uint16_t _cpus_per_node_part(struct part_record *part_ptr)
{
	int node_inx = -1;
	struct node_record *node_ptr;

	if (part_ptr->node_bitmap)
		node_inx = bit_ffs(part_ptr->node_bitmap);
	if (node_inx >= 0) {
		node_ptr = node_record_table_ptr + node_inx;
		if (slurmctld_conf.fast_schedule)
			return node_ptr->config_ptr->cpus;
		else
			return node_ptr->cpus;
	}
	return 0;
}

/*
 * Find lowest allocatable node memory size across all the nodes belonging
 * to the given partition. Allocatable as RealMemory - MemSpecLimit.
 *
 * IN - part_record to retrieve the information.
 * RET - lowest allocatable node memory size (-1 if none found).
 */
static uint64_t _part_node_lowest_mem(struct part_record *part_ptr)
{
	uint64_t allocatable;
	uint64_t lowest = -1;
	struct node_record *node_ptr = NULL;
	bitoff_t first, last, i;

	if (!part_ptr) {
		error("%s: no part_record pointer.", __func__);
		return -1;
	}

	if (!part_ptr->name) {
		error("%s: part_record has no name.", __func__);
		return -1;
	}

	if (!part_ptr->node_bitmap) {
		error("%s: partition %s has no node_bitmap.", __func__,
		      part_ptr->name);
		return -1;
	}

	first = bit_ffs(part_ptr->node_bitmap);
	if (first == -1) {
		error("%s: no first bit found in partition %s node_bitmap.",
		      __func__, part_ptr->name);
		return -1;
	}

	last = bit_fls(part_ptr->node_bitmap);
	if (last == -1) {
		error("%s: no last bit found in partition %s node_bitmap.",
		      __func__, part_ptr->name);
		return -1;
	}

	for (i = first; i <= last; i++) {
		if (!bit_test(part_ptr->node_bitmap, i))
			continue;
		node_ptr = node_record_table_ptr + i;
		if (slurmctld_conf.fast_schedule) {
			if (!node_ptr->config_ptr) {
				error("%s: node has no config_ptr", __func__);
				return -1;
			}
			allocatable = node_ptr->config_ptr->real_memory -
					node_ptr->config_ptr->mem_spec_limit;
		} else
			allocatable = node_ptr->real_memory -
					node_ptr->mem_spec_limit;
		if (allocatable < lowest)
			lowest = allocatable;
		if (lowest == 0)
			break;
	}

	return lowest;
}

/*
 * Test if job pn_min_memory exceeds MaxMemPer[CPU|Node] limit, previously
 * setting it to the cluster or current tested partition default if the original
 * job request didn't specify memory.
 *
 * IN job_ptr - job_record pointer to test pn_min_memory.
 * IN part_ptr - part_record pointer to check limits against.
 * RET - true if job memory doesn't exceed the limit.
 */
static bool _valid_pn_min_mem(struct job_record *job_ptr,
			      struct part_record *part_ptr)
{
	uint64_t job_mem, def_mem, max_mem, lowest_mem, tmp_max_mem;
	uint32_t job_cpus_per_node = 1, avail_cpus_per_node = 1;
	bool cpus_called = false;

	if (!job_ptr->details) {
		error("%s: job %u has no details pointer.", __func__,
		      job_ptr->job_id);
		return false;
	}

	if (!part_ptr) {
		error("%s: called with no part_record pointer.", __func__);
		return false;
	}

	if (part_ptr->max_mem_per_cpu)
		max_mem = part_ptr->max_mem_per_cpu;
	else
		max_mem = slurmctld_conf.max_mem_per_cpu;

	/*
	 * Set job_ptr->details->pn_min_memory starting from the original user
	 * requested memory (orig_pn_min_memory), since pn_min_memory could
	 * have been modified through the course of scheduling (i.e. when
	 * testing different partitions). If the original request didn't specify
	 * memory, then use the cluster or partition DefMemPer[CPU|Node].
	 * If the value is 0, handle the special case below.
	 */
	if (job_ptr->details->orig_pn_min_memory == NO_VAL64) {
		if (part_ptr->def_mem_per_cpu)
			def_mem = part_ptr->def_mem_per_cpu;
		else
			def_mem = slurmctld_conf.def_mem_per_cpu;
		job_ptr->details->pn_min_memory = def_mem;
		debug2("%s: setting job %u memory %s to default %"PRIu64"M in partition %s",
		      __func__, job_ptr->job_id,
		      (def_mem & MEM_PER_CPU) ? "per cpu" : "per node",
		      (def_mem & MEM_PER_CPU) ? (def_mem & (~MEM_PER_CPU)) :
		      def_mem, (part_ptr->name) ? part_ptr->name : "N/A");
	} else
		job_ptr->details->pn_min_memory =
			job_ptr->details->orig_pn_min_memory;

	if ((job_ptr->details->pn_min_memory == 0) ||
	    (job_ptr->details->pn_min_memory == MEM_PER_CPU)) {
		/*
		 * Job --mem[-per-cpu]=0, special case where job requests Slurm
		 * to allocate all the possible memory on the node.
		 * Since the partition may have nodes of different memory sizes,
		 * find node with the smallest RealMemory - MemSpecLimit value.
		 */

		/* Force map pn_min_memory to per-node. */
		job_ptr->details->pn_min_memory = 0;
		lowest_mem = _part_node_lowest_mem(part_ptr);
		if (lowest_mem == -1) {
			error("%s: no lowest allocatable memory size found in partition %s",
			      __func__, (part_ptr->name) ? part_ptr->name :
			      "N/A");
			return false;
		} else if ((max_mem == 0) || (max_mem == MEM_PER_CPU)) {
			/* No MaxMemPER[CPU|Node] configured (unlimited). */
			job_ptr->details->pn_min_memory = lowest_mem;
		} else {
			/*
			 * MIN that value with MaxMemPer[CPU|Node], so that it
			 * ends up the highest possible. Wondering if we should
			 * only do this if ACCOUNTING_ENFORCE_LIMITS flag set.
			 */
			 tmp_max_mem = max_mem;
			if (max_mem & MEM_PER_CPU) {
				/* max_mem PerCPU, set tmp to PerNode. */
				avail_cpus_per_node =
						_cpus_per_node_part(part_ptr);
				cpus_called = true;
				if (avail_cpus_per_node)
					tmp_max_mem *= avail_cpus_per_node;
				else
					avail_cpus_per_node = 1;
			}
			job_ptr->details->pn_min_memory =
						MIN(lowest_mem, tmp_max_mem);
		}
		debug2("%s: job %u memory per node set to %"PRIu64"M in partition %s", __func__,
		       job_ptr->job_id, job_ptr->details->pn_min_memory,
		       (part_ptr->name) ? part_ptr->name : "N/A");
	}
	job_mem = job_ptr->details->pn_min_memory;

	/* No MaxMemPer[CPU|Node] configured (unlimited). */
	if ((max_mem == 0) || (max_mem == MEM_PER_CPU))
		return true;

	/*
	 * Job memory and configured max limit have same form, thus
	 * job --mem-per-cpu and limit MaxMemPerCPU or
	 * job --mem and limit MaxMemPerNode.
	 */
	if (((job_mem & MEM_PER_CPU) && (max_mem & MEM_PER_CPU)) ||
	   (((job_mem & MEM_PER_CPU) == 0) && ((max_mem & MEM_PER_CPU) == 0))) {
		if (job_mem <= max_mem) /* No need to remove flag to compare. */
			return true;
		else {
			debug2("%s: job %u mem%s=%"PRIu64"M > MaxMemPer%s=%"PRIu64"M in partition %s",
			       __func__, job_ptr->job_id,
			       (job_mem & MEM_PER_CPU) ? "_per_cpu" :
			       "_per_node", (job_mem & MEM_PER_CPU) ?
			       (job_mem & (~MEM_PER_CPU)) : job_mem,
			       (max_mem & MEM_PER_CPU) ? "CPU" : "Node",
			       (max_mem & MEM_PER_CPU) ?
			       (max_mem & (~MEM_PER_CPU)) : max_mem,
			       (part_ptr->name) ? part_ptr->name : "N/A");
			return false;
		}
	}

	/*
	 * Job memory and configured limit forms differ (i.e. one is a per-cpu
	 * and the other is per-node). Covert them both to per-node values for
	 * comparison.
	 *
	 * NOTE: the conversion assumes a simplification since in order to
	 * retrieve the number of cpus per node, we use the first node in the
	 * partition, and nodes may differ... and this can have unexpected
	 * consequences.
	 *
	 * Ideally I think we should also remove CoreSpecCount from the
	 * calculated avail_cpus_per_node value, the same way we remove
	 * MemSpecLimit from RealMemory to calculate the allocatable memory on
	 * the node.
	 *
	 * Should we use this function instead?
	 * select_g_alter_node_cnt(SELECT_GET_NODE_CPU_CNT,
	 *			   &avail_cpus_per_node);
	 */
	 if (!cpus_called) {
		 avail_cpus_per_node = _cpus_per_node_part(part_ptr);
		 if (!avail_cpus_per_node)
			 avail_cpus_per_node = 1;
	}

	if (job_mem & MEM_PER_CPU) {
		/*
		 * Job has per-cpu form and limit has per-node one. Estimate
		 * the job_cpus_per_node requested and then MIN that to the
		 * avail_cpus_per_node. Then use the result as a factor to
		 * obtain the job per-node form and compare it with the limit.
		 */
		if ((job_ptr->details->ntasks_per_node != NO_VAL16) &&
		    (job_ptr->details->ntasks_per_node != 0))
			job_cpus_per_node = job_ptr->details->ntasks_per_node;
		else
			job_cpus_per_node = 1;

		if ((job_ptr->details->num_tasks != NO_VAL) &&
		    (job_ptr->details->num_tasks != 0) &&
		    (job_ptr->details->max_nodes != NO_VAL) &&
		    (job_ptr->details->max_nodes != 0)) {
			job_cpus_per_node = MAX(job_cpus_per_node,
				((job_ptr->details->num_tasks +
				  job_ptr->details->max_nodes - 1) /
				 job_ptr->details->max_nodes));
		}

		if ((job_ptr->details->cpus_per_task != NO_VAL16) &&
		    (job_ptr->details->cpus_per_task != 0))
			job_cpus_per_node *= job_ptr->details->cpus_per_task;

		if ((job_ptr->details->pn_min_cpus != NO_VAL16) &&
		    (job_ptr->details->pn_min_cpus > job_cpus_per_node))
			job_cpus_per_node = job_ptr->details->pn_min_cpus;

		if ((job_ptr->details->min_cpus != NO_VAL16) &&
		    (job_ptr->details->min_cpus > job_cpus_per_node))
			job_cpus_per_node = job_ptr->details->min_cpus;

		job_mem &= (~MEM_PER_CPU);
		job_mem *= MIN(job_cpus_per_node, avail_cpus_per_node);
	} else {
		/*
		 * Job has per-node form and limit has per-cpu one. Use the
		 * avail_cpus_per_node as a factor to obtain the limit per-node
		 * form and compare it with the job memory.
		 */
		max_mem &= (~MEM_PER_CPU);
		max_mem *= avail_cpus_per_node;
	}

	if (job_mem <= max_mem)
		return true;

	debug2("%s: job %u mem_per_node=%"PRIu64"M > MaxMemPerNode=%"PRIu64"M in partition %s",
	       __func__, job_ptr->job_id, job_mem, max_mem,
	       (part_ptr->name) ? part_ptr->name : "N/A");

	return false;
}

/*
 * Increment time limit of one job record for node configuraiton.
 */
static void _job_time_limit_incr(struct job_record *job_ptr,
				 uint32_t boot_job_id)
{
	time_t delta_t, now = time(NULL);

	delta_t = difftime(now, job_ptr->start_time);
	if ((job_ptr->job_id != boot_job_id) && !IS_JOB_CONFIGURING(job_ptr))
		job_ptr->tot_sus_time = delta_t;

	if ((job_ptr->time_limit != INFINITE) &&
	    ((job_ptr->job_id == boot_job_id) || (delta_t != 0))) {
		if (delta_t && !IS_JOB_CONFIGURING(job_ptr)) {
			verbose("Extending job %u time limit by %u secs for configuration",
				job_ptr->job_id, (uint32_t) delta_t);
		}
		job_ptr->end_time = now + (job_ptr->time_limit * 60);
		job_ptr->end_time_exp = job_ptr->end_time;
	}
}

/*
 * Increment time limit for all components of a pack job for node configuraiton.
 * job_ptr IN - pointer to job record for which configuration is complete
 * boot_job_id - job ID of record with newly powered up node or 0
 */
static void _pack_time_limit_incr(struct job_record *job_ptr,
				  uint32_t boot_job_id)
{
	struct job_record *pack_leader, *pack_job;
	ListIterator iter;

	if (!job_ptr->pack_job_id) {
		_job_time_limit_incr(job_ptr, boot_job_id);
		return;
	}

	pack_leader = find_job_record(job_ptr->pack_job_id);
	if (!pack_leader) {
		error("%s: Job pack leader %u not found",
		      __func__, job_ptr->pack_job_id);
		_job_time_limit_incr(job_ptr, boot_job_id);
		return;
	}
	if (!pack_leader->pack_job_list) {
		error("%s: Job pack leader %u job list is NULL",
		      __func__, job_ptr->pack_job_id);
		_job_time_limit_incr(job_ptr, boot_job_id);
		return;
	}

	iter = list_iterator_create(pack_leader->pack_job_list);
	while ((pack_job = (struct job_record *) list_next(iter))) {
		_job_time_limit_incr(pack_job, boot_job_id);
	}
	list_iterator_destroy(iter);
}

/* Clear job's CONFIGURING flag and advance end time as needed */
extern void job_config_fini(struct job_record *job_ptr)
{
	time_t now = time(NULL);

	last_job_update = now;
	job_ptr->job_state &= ~JOB_CONFIGURING;
	if (IS_JOB_POWER_UP_NODE(job_ptr)) {
		info("Resetting job %u start time for node power up",
		     job_ptr->job_id);
		job_ptr->job_state &= ~JOB_POWER_UP_NODE;
		job_ptr->start_time = now;
		_pack_time_limit_incr(job_ptr, job_ptr->job_id);
		jobacct_storage_g_job_start(acct_db_conn, job_ptr);
	} else {
		_pack_time_limit_incr(job_ptr, 0);
	}

	/*
	 * Request asynchronous launch of a prolog for a non-batch job.
	 * PROLOG_FLAG_CONTAIN also turns on PROLOG_FLAG_ALLOC.
	 */
	if (slurmctld_conf.prolog_flags & PROLOG_FLAG_ALLOC)
		launch_prolog(job_ptr);
}

/*
 * Determine of the nodes are ready to run a job
 * RET true if ready
 */
extern bool test_job_nodes_ready(struct job_record *job_ptr)
{
	if (IS_JOB_PENDING(job_ptr))
		return false;
	if (!job_ptr->node_bitmap)	/* Revoked allocation */
		return true;
	if (bit_overlap(job_ptr->node_bitmap, power_node_bitmap))
		return false;

	if (!job_ptr->batch_flag ||
	    job_ptr->wait_all_nodes || job_ptr->burst_buffer) {
		/* Make sure all nodes ready to start job */
		if ((select_g_job_ready(job_ptr) & READY_NODE_STATE) == 0)
			return false;
	} else if (job_ptr->batch_flag) {
		/* Make sure first node is ready to start batch job */
		int i_first = bit_ffs(job_ptr->node_bitmap);
		struct node_record *node_ptr = node_record_table_ptr + i_first;
		if ((i_first != -1) &&
		    (IS_NODE_POWER_SAVE(node_ptr) ||
		     IS_NODE_POWER_UP(node_ptr))) {
			return false;
		}
	}

	return true;
}

/*
 * Modify a job's memory limit if allocated all memory on a node and the node
 * reboots, possibly with a different memory size (e.g. KNL MCDRAM mode changed)
 */
extern void job_validate_mem(struct job_record *job_ptr)
{
	if ((job_ptr->bit_flags & NODE_MEM_CALC) &&
	    (slurmctld_conf.fast_schedule == 0)) {
		select_g_job_mem_confirm(job_ptr);
		job_ptr->tres_alloc_cnt[TRES_ARRAY_MEM] =
				job_get_tres_mem(
				  job_ptr->details->pn_min_memory,
				  job_ptr->tres_alloc_cnt[TRES_ARRAY_CPU],
				  job_ptr->tres_alloc_cnt[TRES_ARRAY_NODE]);
		set_job_tres_alloc_str(job_ptr, false);
		jobacct_storage_job_start_direct(acct_db_conn, job_ptr);
	}
}

/*
 * For non-pack job, return true if this job is configuring.
 * For pack job, return true if any component of the job is configuring.
 */
static bool _pack_configuring_test(struct job_record *job_ptr)
{
	struct job_record *pack_leader, *pack_job;
	ListIterator iter;
	bool result = false;

	if (IS_JOB_CONFIGURING(job_ptr))
		return true;
	if (!job_ptr->pack_job_id)
		return false;

	pack_leader = find_job_record(job_ptr->pack_job_id);
	if (!pack_leader) {
		error("%s: Job pack leader %u not found",
		      __func__, job_ptr->pack_job_id);
		return false;
	}
	if (!pack_leader->pack_job_list) {
		error("%s: Job pack leader %u job list is NULL",
		      __func__, job_ptr->pack_job_id);
		return false;
	}

	iter = list_iterator_create(pack_leader->pack_job_list);
	while ((pack_job = (struct job_record *) list_next(iter))) {
		if (IS_JOB_CONFIGURING(pack_job)) {
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
 * NOTE: Job Write lock_slurmctld config before entry
 */
void job_time_limit(void)
{
	ListIterator job_iterator;
	struct job_record *job_ptr;
	time_t now = time(NULL);
	time_t old = now - ((slurmctld_conf.inactive_limit * 4 / 3) +
			    slurmctld_conf.msg_timeout + 1);
	time_t over_run;
	uint16_t over_time_limit;
	int job_test_count = 0;
	uint32_t resv_over_run = slurmctld_conf.resv_over_run;

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

#ifndef HAVE_BG
	uint8_t prolog;
#endif

	job_iterator = list_iterator_create(job_list);
	START_TIMER;
	while ((job_ptr = list_next(job_iterator))) {
		xassert (job_ptr->magic == JOB_MAGIC);
		job_test_count++;

#ifndef HAVE_BG
		/*
		 * If the CONFIGURING flag is removed elsewhere like
		 * on a Bluegene system this check is not needed and
		 * should be avoided.  In the case of BG blocks that
		 * are booting aren't associated with
		 * power_node_bitmap so bit_overlap always returns 0
		 * and erroneously removes the flag.
		 */
		if (job_ptr->details)
			prolog = job_ptr->details->prolog_running;
		else
			prolog = 0;
		if ((prolog == 0) && IS_JOB_CONFIGURING(job_ptr) &&
		    test_job_nodes_ready(job_ptr)) {
			char job_id_buf[JBUFSIZ];
			info("%s: Configuration for %s complete", __func__,
			     jobid2fmt(job_ptr, job_id_buf,sizeof(job_id_buf)));
			job_config_fini(job_ptr);
			if (job_ptr->bit_flags & NODE_REBOOT) {
				job_ptr->bit_flags &= (~NODE_REBOOT);
				job_validate_mem(job_ptr);
				if (job_ptr->batch_flag)
					launch_job(job_ptr);
			}
		}
#endif

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

		if (_pack_configuring_test(job_ptr))
			continue;

		if (!IS_JOB_RUNNING(job_ptr) && !IS_JOB_SUSPENDED(job_ptr))
			continue;

		/*
		 * everything above here is considered "quick", and skips the
		 * timeout at the bottom of the loop by using a continue.
		 * everything below is considered "slow", and needs to jump to
		 * time_check before the next job is tested
		 */
		if (job_ptr->preempt_time &&
		    (IS_JOB_RUNNING(job_ptr) || IS_JOB_SUSPENDED(job_ptr))) {
			if ((job_ptr->warn_time) &&
			    (!(job_ptr->warn_flags & WARN_SENT)) &&
			    (job_ptr->warn_time + PERIODIC_TIMEOUT + now >=
			     job_ptr->end_time)) {
				debug("%s: preempt warning signal %u to job %u ",
				      __func__, job_ptr->warn_signal,
				      job_ptr->job_id);
				(void) job_signal(job_ptr->job_id,
						  job_ptr->warn_signal,
						  job_ptr->warn_flags, 0,
						  false);

				/* mark job as signaled */
				job_ptr->warn_flags |= WARN_SENT;
			}
			if (job_ptr->end_time <= now) {
				last_job_update = now;
				info("%s: Preemption GraceTime reached JobId=%u",
				     __func__, job_ptr->job_id);
				job_ptr->job_state = JOB_PREEMPTED |
						     JOB_COMPLETING;
				_job_timed_out(job_ptr);
				xfree(job_ptr->state_desc);
			}
			goto time_check;
		}

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
			goto time_check;
		}
		if (job_ptr->time_limit != INFINITE) {
			if ((job_ptr->warn_time) &&
			    (!(job_ptr->warn_flags & WARN_SENT)) &&
			    (job_ptr->warn_time + PERIODIC_TIMEOUT + now >=
			     job_ptr->end_time)) {
				/*
				 * If --signal B option was not specified,
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

				/* mark job as signaled */
				job_ptr->warn_flags |= WARN_SENT;
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

			if (job_ptr->part_ptr &&
			    (job_ptr->part_ptr->over_time_limit != NO_VAL16)) {
				over_time_limit =
					job_ptr->part_ptr->over_time_limit;
			} else {
				over_time_limit =
					slurmctld_conf.over_time_limit;
			}
			if (over_time_limit == INFINITE16)
				over_run = now - YEAR_SECONDS;
			else
				over_run = now - (over_time_limit  * 60);
			if (job_ptr->end_time <= over_run) {
				last_job_update = now;
				info("Time limit exhausted for JobId=%u",
				     job_ptr->job_id);
				_job_timed_out(job_ptr);
				job_ptr->state_reason = FAIL_TIMEOUT;
				xfree(job_ptr->state_desc);
				goto time_check;
			}
		}

		if (job_ptr->resv_ptr &&
		    !(job_ptr->resv_ptr->flags & RESERVE_FLAG_FLEX) &&
		    (job_ptr->resv_ptr->end_time + resv_over_run) < time(NULL)){
			last_job_update = now;
			info("Reservation ended for JobId=%u",
			     job_ptr->job_id);
			_job_timed_out(job_ptr);
			job_ptr->state_reason = FAIL_TIMEOUT;
			xfree(job_ptr->state_desc);
			goto time_check;
		}

		/*
		 * check if any individual job steps have exceeded
		 * their time limit
		 */
		if (job_ptr->step_list &&
		    (list_count(job_ptr->step_list) > 0))
			check_job_step_time_limit(job_ptr, now);

		acct_policy_job_time_out(job_ptr);

		if (job_ptr->state_reason == FAIL_TIMEOUT) {
			last_job_update = now;
			_job_timed_out(job_ptr);
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
		if (slurm_delta_tv(&tv1) >= 3000000 && list_peek_next(job_iterator) ) {
			END_TIMER;
			debug("%s: yielding locks after testing"
			      " %d jobs, %s",
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

/* job write lock must be locked before calling this */
extern void job_set_req_tres(
	struct job_record *job_ptr, bool assoc_mgr_locked)
{
	uint32_t cpu_cnt = 0, node_cnt = 0;
	uint64_t mem_cnt = 0;
	assoc_mgr_lock_t locks = { NO_LOCK, NO_LOCK, NO_LOCK, NO_LOCK,
				   READ_LOCK, NO_LOCK, NO_LOCK };

	xfree(job_ptr->tres_req_str);
	xfree(job_ptr->tres_fmt_req_str);
	xfree(job_ptr->tres_req_cnt);

	if (!assoc_mgr_locked)
		assoc_mgr_lock(&locks);

	job_ptr->tres_req_cnt = xmalloc(sizeof(uint64_t) * g_tres_count);

	if (job_ptr->details) {
		node_cnt = job_ptr->details->min_nodes;
		cpu_cnt = job_ptr->details->min_cpus;
		if (job_ptr->details->pn_min_memory)
			mem_cnt = job_ptr->details->pn_min_memory;
	}

	/* if this is set just override */
	if (job_ptr->total_cpus)
		cpu_cnt = job_ptr->total_cpus;

#ifdef HAVE_BG
	select_g_select_jobinfo_get(job_ptr->select_jobinfo,
				    SELECT_JOBDATA_NODE_CNT,
				    &node_cnt);
#else
	if (job_ptr->node_cnt)
		node_cnt = job_ptr->node_cnt;
#endif

	job_ptr->tres_req_cnt[TRES_ARRAY_NODE] = (uint64_t)node_cnt;
	job_ptr->tres_req_cnt[TRES_ARRAY_CPU] = (uint64_t)cpu_cnt;
	job_ptr->tres_req_cnt[TRES_ARRAY_MEM] = job_get_tres_mem(mem_cnt,
								 cpu_cnt,
								 node_cnt);

	license_set_job_tres_cnt(job_ptr->license_list,
				 job_ptr->tres_req_cnt,
				 true);

	/* FIXME: this assumes that all nodes have equal TRES */
	gres_set_job_tres_cnt(job_ptr->gres_list,
			      node_cnt,
			      job_ptr->tres_req_cnt,
			      true);

	bb_g_job_set_tres_cnt(job_ptr,
			      job_ptr->tres_req_cnt,
			      true);

	/* now that the array is filled lets make the string from it */
	set_job_tres_req_str(job_ptr, true);

	if (!assoc_mgr_locked)
		assoc_mgr_unlock(&locks);
}

extern void job_set_alloc_tres(struct job_record *job_ptr,
			       bool assoc_mgr_locked)
{
	uint32_t alloc_nodes = 0;
	assoc_mgr_lock_t locks = { NO_LOCK, NO_LOCK, NO_LOCK, NO_LOCK,
				   READ_LOCK, NO_LOCK, NO_LOCK };

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

	job_ptr->tres_alloc_cnt = xmalloc(
		sizeof(uint64_t) * slurmctld_tres_cnt);

	job_ptr->tres_alloc_cnt[TRES_ARRAY_CPU] = (uint64_t)job_ptr->total_cpus;

#ifdef HAVE_BG
	select_g_select_jobinfo_get(job_ptr->select_jobinfo,
				    SELECT_JOBDATA_NODE_CNT,
				    &alloc_nodes);
#else
	alloc_nodes = job_ptr->node_cnt;
#endif
	job_ptr->tres_alloc_cnt[TRES_ARRAY_NODE] = (uint64_t)alloc_nodes;
	job_ptr->tres_alloc_cnt[TRES_ARRAY_MEM] =
			job_get_tres_mem(
			  job_ptr->details->pn_min_memory,
			  job_ptr->tres_alloc_cnt[TRES_ARRAY_CPU],
			  job_ptr->tres_alloc_cnt[TRES_ARRAY_NODE]);

	job_ptr->tres_alloc_cnt[TRES_ARRAY_ENERGY] = NO_VAL64;

	license_set_job_tres_cnt(job_ptr->license_list,
				 job_ptr->tres_alloc_cnt,
				 true);

	gres_set_job_tres_cnt(job_ptr->gres_list,
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
	set_job_tres_alloc_str(job_ptr, true);

	if (!assoc_mgr_locked)
		assoc_mgr_unlock(&locks);

	return;
}

extern int job_update_tres_cnt(struct job_record *job_ptr, int node_inx)
{
	int cpu_cnt, offset = -1, rc = SLURM_SUCCESS;

	xassert(job_ptr);

#ifdef HAVE_BG
	/* This function doesn't apply to a bluegene system since the
	 * cpu count isn't set up on that system. */
	return SLURM_SUCCESS;
#endif
	if (job_ptr->details->whole_node == 1) {
		/* Since we are allocating whole nodes don't rely on
		 * the job_resrcs since it could be less because the
		 * node could of only used 1 thread per core.
		 */
		struct node_record *node_ptr =
			node_record_table_ptr + node_inx;
		if (slurmctld_conf.fast_schedule)
			cpu_cnt = node_ptr->config_ptr->cpus;
		else
			cpu_cnt = node_ptr->cpus;
	} else {
		if ((offset = job_resources_node_inx_to_cpu_inx(
			     job_ptr->job_resrcs, node_inx)) < 0) {
			error("job_update_tres_cnt: problem getting "
			      "offset of job %u",
			      job_ptr->job_id);
			job_ptr->cpu_cnt = 0;
			return SLURM_ERROR;
		}

		cpu_cnt = job_ptr->job_resrcs->cpus[offset];
	}
	if (cpu_cnt > job_ptr->cpu_cnt) {
		error("job_update_tres_cnt: cpu_cnt underflow on job_id %u",
		      job_ptr->job_id);
		job_ptr->cpu_cnt = 0;
		rc = SLURM_ERROR;
	} else
		job_ptr->cpu_cnt -= cpu_cnt;

	if (IS_JOB_RESIZING(job_ptr)) {
		if (cpu_cnt > job_ptr->total_cpus) {
			error("job_update_tres_cnt: total_cpus "
			      "underflow on job_id %u",
			      job_ptr->job_id);
			job_ptr->total_cpus = 0;
			rc = SLURM_ERROR;
		} else
			job_ptr->total_cpus -= cpu_cnt;

		job_set_alloc_tres(job_ptr, false);
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
		if (!job_ptr->preempt_time)
			job_ptr->job_state = JOB_TIMEOUT | JOB_COMPLETING;
		build_cg_bitmap(job_ptr);
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
		return ESLURM_GROUP_ID_MISSING;
	}
	if (job_desc_msg->contiguous == NO_VAL16)
		job_desc_msg->contiguous = 0;

	if (job_desc_msg->task_dist == NO_VAL) {
		/* not typically set by salloc or sbatch */
		job_desc_msg->task_dist = SLURM_DIST_CYCLIC;
	}
	if (job_desc_msg->plane_size == NO_VAL16)
		job_desc_msg->plane_size = 0;

	if (job_desc_msg->kill_on_node_fail == NO_VAL16)
		job_desc_msg->kill_on_node_fail = 1;

	if (job_desc_msg->job_id != NO_VAL) {
		struct job_record *dup_job_ptr;
		if (!fed_mgr_fed_rec &&
		    (submit_uid != 0) &&
		    (submit_uid != slurmctld_conf.slurm_user_id)) {
			info("attempt by uid %u to set job_id to %u",
			     submit_uid, job_desc_msg->job_id);
			return ESLURM_INVALID_JOB_ID;
		}
		if (job_desc_msg->job_id == 0) {
			info("attempt by uid %u to set zero job_id",
			     submit_uid);
			return ESLURM_INVALID_JOB_ID;
		}
		dup_job_ptr = find_job_record(job_desc_msg->job_id);
		if (dup_job_ptr) {
			info("attempt re-use active job_id %u",
			     job_desc_msg->job_id);
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
					slurmctld_conf.def_mem_per_cpu;
		}
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
 * _list_delete_job - delete a job record and its corresponding job_details,
 *	see common/list.h for documentation
 * IN job_entry - pointer to job_record to delete
 */
static void _list_delete_job(void *job_entry)
{
	struct job_record *job_ptr = (struct job_record *) job_entry;
	int job_array_size, i;

	xassert(job_entry);
	xassert (job_ptr->magic == JOB_MAGIC);
	job_ptr->magic = 0;	/* make sure we don't delete record twice */

	/* Remove record from fed_job_list */
	fed_mgr_remove_fed_job_info(job_ptr->job_id);

	/* Remove the record from job hash table */
	_remove_job_hash(job_ptr, JOB_HASH_JOB);

	if (job_ptr->array_recs) {
		job_array_size = MAX(1, job_ptr->array_recs->task_cnt);
	} else {
		job_array_size = 1;
	}

	/* Remove the record from job array hash tables, if applicable */
	if (job_ptr->array_task_id != NO_VAL) {
		_remove_job_hash(job_ptr, JOB_HASH_ARRAY_JOB);
		_remove_job_hash(job_ptr, JOB_HASH_ARRAY_TASK);
	}

	_delete_job_details(job_ptr);
	xfree(job_ptr->account);
	xfree(job_ptr->admin_comment);
	xfree(job_ptr->alias_list);
	xfree(job_ptr->alloc_node);
	if (job_ptr->array_recs) {
		FREE_NULL_BITMAP(job_ptr->array_recs->task_id_bitmap);
		xfree(job_ptr->array_recs->task_id_str);
		xfree(job_ptr->array_recs);
	}
	xfree(job_ptr->batch_host);
	xfree(job_ptr->burst_buffer);
	checkpoint_free_jobinfo(job_ptr->check_job);
	xfree(job_ptr->comment);
	xfree(job_ptr->clusters);
	free_job_fed_details(&job_ptr->fed_details);
	free_job_resources(&job_ptr->job_resrcs);
	xfree(job_ptr->gres);
	xfree(job_ptr->gres_alloc);
	_clear_job_gres_details(job_ptr);
	xfree(job_ptr->gres_req);
	xfree(job_ptr->gres_used);
	FREE_NULL_LIST(job_ptr->gres_list);
	xfree(job_ptr->licenses);
	FREE_NULL_LIST(job_ptr->license_list);
	xfree(job_ptr->limit_set.tres);
	xfree(job_ptr->mail_user);
	xfree(job_ptr->mcs_label);
	xfree(job_ptr->name);
	xfree(job_ptr->network);
	xfree(job_ptr->node_addr);
	FREE_NULL_BITMAP(job_ptr->node_bitmap);
	FREE_NULL_BITMAP(job_ptr->node_bitmap_cg);
	xfree(job_ptr->nodes);
	xfree(job_ptr->nodes_completing);
	xfree(job_ptr->origin_cluster);
	xfree(job_ptr->pack_job_id_set);
	FREE_NULL_LIST(job_ptr->pack_job_list);
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
	xfree(job_ptr->tres_alloc_cnt);
	xfree(job_ptr->tres_alloc_str);
	xfree(job_ptr->tres_fmt_alloc_str);
	xfree(job_ptr->tres_req_cnt);
	xfree(job_ptr->tres_req_str);
	xfree(job_ptr->tres_fmt_req_str);
	step_list_purge(job_ptr);
	select_g_select_jobinfo_free(job_ptr->select_jobinfo);
	xfree(job_ptr->user_name);
	xfree(job_ptr->wckey);
	if (job_array_size > job_count) {
		error("job_count underflow");
		job_count = 0;
	} else {
		job_count -= job_array_size;
	}
	job_ptr->job_id = 0;
	xfree(job_ptr);
}


/*
 * list_find_job_id - find specific job_id entry in the job list,
 *	see common/list.h for documentation, key is job_id_ptr
 * global- job_list - the global partition list
 */
extern int list_find_job_id(void *job_entry, void *key)
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
 * job_entry IN - job pointer
 * key IN - if not NULL, then skip pack jobs
 */
static int _list_find_job_old(void *job_entry, void *key)
{
	time_t kill_age, min_age, now = time(NULL);
	struct job_record *job_ptr = (struct job_record *)job_entry;
	uint16_t cleaning = 0;

	if (key && job_ptr->pack_job_id)
		return 0;

	if (IS_JOB_COMPLETING(job_ptr) && !LOTS_OF_AGENTS) {
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

	if (fed_mgr_fed_rec && job_ptr->fed_details &&
	    !fed_mgr_is_origin_job(job_ptr)) {
		uint32_t origin_id = fed_mgr_get_cluster_id(job_ptr->job_id);
		slurmdb_cluster_rec_t *origin =
			fed_mgr_get_cluster_by_id(origin_id);

		/* keep job around until origin comes back and is synced */
		if (origin &&
		    (!origin->fed.send ||
		     (((slurm_persist_conn_t *)origin->fed.send)->fd == -1) ||
		     !origin->fed.sync_sent))
		    return 0;
	}

	min_age  = now - slurmctld_conf.min_job_age;
	if (job_ptr->end_time > min_age)
		return 0;	/* Too new to purge */

	if (!(IS_JOB_COMPLETED(job_ptr)))
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

	if (bb_g_job_test_stage_out(job_ptr) != 1)
		return 0;      /* Stage out in progress */

	/* If we don't have a db_index by now and we are running with
	 * the slurmdbd, lets put it on the list to be handled later
	 * when slurmdbd comes back up since we won't get another chance.
	 * job_start won't pend for job_db_inx when the job is finished.
	 */
	if (with_slurmdbd && !job_ptr->db_index)
		jobacct_storage_g_job_start(acct_db_conn, job_ptr);

	return 1;		/* Purge the job */
}

/* Determine if ALL partitions associated with a job are hidden */
static bool _all_parts_hidden(struct job_record *job_ptr, uid_t uid)
{
	bool rc;
	ListIterator part_iterator;
	struct part_record *part_ptr;

	if (job_ptr->part_ptr_list) {
		rc = true;
		part_iterator = list_iterator_create(job_ptr->part_ptr_list);
		while ((part_ptr = (struct part_record *)
				   list_next(part_iterator))) {
			if (part_is_visible(part_ptr, uid)) {
				rc = false;
				break;
			}
		}
		list_iterator_destroy(part_iterator);
		return rc;
	}

	if (job_ptr->part_ptr && part_is_visible(job_ptr->part_ptr, uid))
		return false;
	return true;
}

/* Determine if a given job should be seen by a specific user */
static bool _hide_job(struct job_record *job_ptr, uid_t uid,
		      uint16_t show_flags)
{
	if (!(show_flags & SHOW_ALL) && IS_JOB_REVOKED(job_ptr))
		return true;

	if ((slurmctld_conf.private_data & PRIVATE_DATA_JOBS) &&
	    (job_ptr->user_id != uid) && !validate_operator(uid) &&
	    (((slurm_mcs_get_privatedata() == 0) &&
	      !assoc_mgr_is_user_acct_coord(acct_db_conn, uid,
					    job_ptr->account)) ||
	     ((slurm_mcs_get_privatedata() == 1) &&
	      (mcs_g_check_mcs_label(uid, job_ptr->mcs_label) != 0))))
		return true;
	return false;
}

static void _pack_job(struct job_record *job_ptr,
		      _foreach_pack_job_info_t *pack_info)
{
	xassert (job_ptr->magic == JOB_MAGIC);

	if ((pack_info->filter_uid != NO_VAL) &&
	    (pack_info->filter_uid != job_ptr->user_id))
		return;

	if (((pack_info->show_flags & SHOW_ALL) == 0) &&
	    (pack_info->uid != 0) &&
	    _all_parts_hidden(job_ptr, pack_info->uid))
		return;

	if (_hide_job(job_ptr, pack_info->uid, pack_info->show_flags))
		return;

	pack_job(job_ptr, pack_info->show_flags, pack_info->buffer,
		 pack_info->protocol_version, pack_info->uid);

	(*pack_info->jobs_packed)++;
}

static int _foreach_pack_jobid(void *object, void *arg)
{
	struct job_record *job_ptr;
	uint32_t job_id = *(uint32_t *)object;
	_foreach_pack_job_info_t *info = (_foreach_pack_job_info_t *)arg;

	if (!(job_ptr = find_job_record(job_id)))
		return SLURM_SUCCESS;

	_pack_job(job_ptr, info);

	return SLURM_SUCCESS;
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
	uint32_t jobs_packed = 0, tmp_offset;
	_foreach_pack_job_info_t pack_info = {0};
	Buf buffer;
	ListIterator itr;
	struct job_record *job_ptr = NULL;

	buffer_ptr[0] = NULL;
	*buffer_size = 0;

	buffer = init_buf(BUF_SIZE);

	/* write message body header : size and time */
	/* put in a place holder job record count of 0 for now */
	pack32(jobs_packed, buffer);
	pack_time(time(NULL), buffer);

	/* write individual job records */
	pack_info.buffer           = buffer;
	pack_info.filter_uid       = filter_uid;
	pack_info.jobs_packed      = &jobs_packed;
	pack_info.protocol_version = protocol_version;
	pack_info.show_flags       = show_flags;
	pack_info.uid              = uid;

	itr = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(itr))) {
		_pack_job(job_ptr, &pack_info);
	}
	list_iterator_destroy(itr);

	/* put the real record count in the message body header */
	tmp_offset = get_buf_offset(buffer);
	set_buf_offset(buffer, 0);
	pack32(jobs_packed, buffer);
	set_buf_offset(buffer, tmp_offset);

	*buffer_size = get_buf_offset(buffer);
	buffer_ptr[0] = xfer_buf_data(buffer);
}

/*
 * pack_spec_jobs - dump job information for specified jobs in
 *	machine independent form (for network transmission)
 * OUT buffer_ptr - the pointer is set to the allocated buffer.
 * OUT buffer_size - set to size of the buffer in bytes
 * IN show_flags - job filtering options
 * IN job_ids - list of job_ids to pack
 * IN uid - uid of user making request (for partition filtering)
 * IN filter_uid - pack only jobs belonging to this user if not NO_VAL
 * global: job_list - global list of job records
 * NOTE: the buffer at *buffer_ptr must be xfreed by the caller
 * NOTE: change _unpack_job_desc_msg() in common/slurm_protocol_pack.c
 *	whenever the data format changes
 */
extern void pack_spec_jobs(char **buffer_ptr, int *buffer_size, List job_ids,
			   uint16_t show_flags, uid_t uid, uint32_t filter_uid,
			   uint16_t protocol_version)
{
	uint32_t jobs_packed = 0, tmp_offset;
	_foreach_pack_job_info_t pack_info = {0};
	Buf buffer;

	xassert(job_ids);

	buffer_ptr[0] = NULL;
	*buffer_size = 0;

	buffer = init_buf(BUF_SIZE);

	/* write message body header : size and time */
	/* put in a place holder job record count of 0 for now */
	pack32(jobs_packed, buffer);
	pack_time(time(NULL), buffer);

	/* write individual job records */
	pack_info.buffer           = buffer;
	pack_info.filter_uid       = filter_uid;
	pack_info.jobs_packed      = &jobs_packed;
	pack_info.protocol_version = protocol_version;
	pack_info.show_flags       = show_flags;
	pack_info.uid              = uid;

	list_for_each(job_ids, _foreach_pack_jobid, &pack_info);

	/* put the real record count in the message body header */
	tmp_offset = get_buf_offset(buffer);
	set_buf_offset(buffer, 0);
	pack32(jobs_packed, buffer);
	set_buf_offset(buffer, tmp_offset);

	*buffer_size = get_buf_offset(buffer);
	buffer_ptr[0] = xfer_buf_data(buffer);
}

static int _pack_hetero_job(struct job_record *job_ptr, uint16_t show_flags,
			    Buf buffer, uint16_t protocol_version, uid_t uid)
{
	struct job_record *pack_ptr;
	int job_cnt = 0;
	ListIterator iter;

	iter = list_iterator_create(job_ptr->pack_job_list);
	while ((pack_ptr = (struct job_record *) list_next(iter))) {
		if (pack_ptr->pack_job_id == job_ptr->pack_job_id) {
			pack_job(pack_ptr, show_flags, buffer, protocol_version,
				 uid);
			job_cnt++;
		} else {
			error("%s: Bad pack_job_list for job %u",
			      __func__, job_ptr->pack_job_id);
		}
	}
	list_iterator_destroy(iter);

	return job_cnt;
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
	if (job_ptr && job_ptr->pack_job_list) {
		/* Pack heterogeneous job components */
		if (!_hide_job(job_ptr, uid, show_flags)) {
			jobs_packed = _pack_hetero_job(job_ptr, show_flags,
						       buffer, protocol_version,
						       uid);
		}
	} else if (job_ptr && (job_ptr->array_task_id == NO_VAL) &&
		   !job_ptr->array_recs) {
		/* Pack regular (not array) job */
		if (!_hide_job(job_ptr, uid, show_flags)) {
			pack_job(job_ptr, show_flags, buffer, protocol_version,
				 uid);
			jobs_packed++;
		}
	} else {
		bool packed_head = false;

		/* Either the job is not found or it is a job array */
		if (job_ptr) {
			packed_head = true;
			if (!_hide_job(job_ptr, uid, show_flags)) {
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
				if (_hide_job(job_ptr, uid, show_flags))
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

static void _pack_job_gres(struct job_record *dump_job_ptr, Buf buffer,
			   uint16_t protocol_version)
{
	if (!IS_JOB_STARTED(dump_job_ptr) || IS_JOB_FINISHED(dump_job_ptr) ||
	    (dump_job_ptr->gres_list == NULL)) {
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
void pack_job(struct job_record *dump_job_ptr, uint16_t show_flags, Buf buffer,
	      uint16_t protocol_version, uid_t uid)
{
	struct job_details *detail_ptr;
	time_t begin_time = 0, start_time = 0, end_time = 0;
	uint32_t time_limit;
	char *nodelist = NULL;
	assoc_mgr_lock_t locks = { NO_LOCK, NO_LOCK, READ_LOCK, NO_LOCK,
				   NO_LOCK, NO_LOCK, NO_LOCK };

	if (protocol_version >= SLURM_17_11_PROTOCOL_VERSION) {
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
		pack32(dump_job_ptr->delay_boot, buffer);
		pack32(dump_job_ptr->job_id,   buffer);
		pack32(dump_job_ptr->user_id,  buffer);
		pack32(dump_job_ptr->group_id, buffer);
		pack32(dump_job_ptr->pack_job_id, buffer);
		packstr(dump_job_ptr->pack_job_id_set, buffer);
		pack32(dump_job_ptr->pack_job_offset, buffer);
		pack32(dump_job_ptr->profile,  buffer);

		pack32(dump_job_ptr->job_state,    buffer);
		pack16(dump_job_ptr->batch_flag,   buffer);
		if ((dump_job_ptr->state_reason == WAIT_NO_REASON) &&
		    IS_JOB_PENDING(dump_job_ptr)) {
			/* Scheduling cycle in progress, send latest reason */
			pack16(dump_job_ptr->state_reason_prev, buffer);
		} else
			pack16(dump_job_ptr->state_reason, buffer);
		pack8(dump_job_ptr->power_flags,   buffer);
		pack8(dump_job_ptr->reboot,        buffer);
		pack16(dump_job_ptr->restart_cnt,  buffer);
		pack16(show_flags,  buffer);
		pack_time(dump_job_ptr->deadline, buffer);

		pack32(dump_job_ptr->alloc_sid, buffer);
		if ((dump_job_ptr->time_limit == NO_VAL)
		    && dump_job_ptr->part_ptr)
			time_limit = dump_job_ptr->part_ptr->max_time;
		else
			time_limit = dump_job_ptr->time_limit;

		pack32(time_limit, buffer);
		pack32(dump_job_ptr->time_min, buffer);

		if (dump_job_ptr->details) {
			pack32(dump_job_ptr->details->nice,  buffer);
			pack_time(dump_job_ptr->details->submit_time, buffer);
			/* Earliest possible begin time */
			begin_time = dump_job_ptr->details->begin_time;
		} else {   /* Some job details may be purged after completion */
			pack32(NICE_OFFSET, buffer);	/* Best guess */
			pack_time((time_t) 0, buffer);
		}

		pack_time(begin_time, buffer);

		if (IS_JOB_STARTED(dump_job_ptr)) {
			/* Report actual start time, in past */
			start_time = dump_job_ptr->start_time;
			end_time = dump_job_ptr->end_time;
		} else if (dump_job_ptr->start_time != 0) {
			/* Report expected start time,
			 * making sure that time is not in the past */
			start_time = MAX(dump_job_ptr->start_time, time(NULL));
			if (time_limit != NO_VAL) {
				end_time = MAX(dump_job_ptr->end_time,
					       (start_time + time_limit * 60));
			}
		} else	if (begin_time > time(NULL)) {
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

		packstr(slurmctld_conf.cluster_name, buffer);
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
		packstr(dump_job_ptr->admin_comment, buffer);
		packstr(dump_job_ptr->network, buffer);
		packstr(dump_job_ptr->comment, buffer);
		packstr(dump_job_ptr->gres, buffer);
		packstr(dump_job_ptr->batch_host, buffer);
		packstr(dump_job_ptr->burst_buffer, buffer);
		packstr(dump_job_ptr->burst_buffer_state, buffer);

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
		packstr(dump_job_ptr->mcs_label, buffer);

		pack32(dump_job_ptr->exit_code, buffer);
		pack32(dump_job_ptr->derived_ec, buffer);

		if (show_flags & SHOW_DETAIL) {
			pack_job_resources(dump_job_ptr->job_resrcs, buffer,
					   protocol_version);
			_pack_job_gres(dump_job_ptr, buffer, protocol_version);
		} else {
			pack32(NO_VAL, buffer);
			pack32((uint32_t) 0, buffer);
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
		pack32(dump_job_ptr->bit_flags, buffer);
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
	} else if (protocol_version >= SLURM_17_02_PROTOCOL_VERSION) {
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
		pack32(dump_job_ptr->delay_boot, buffer);
		pack32(dump_job_ptr->job_id,   buffer);
		pack32(dump_job_ptr->user_id,  buffer);
		pack32(dump_job_ptr->group_id, buffer);
		pack32(dump_job_ptr->profile,  buffer);

		pack32(dump_job_ptr->job_state,    buffer);
		pack16(dump_job_ptr->batch_flag,   buffer);
		if ((dump_job_ptr->state_reason == WAIT_NO_REASON) &&
		    IS_JOB_PENDING(dump_job_ptr)) {
			/* Scheduling cycle in progress, send latest reason */
			pack16(dump_job_ptr->state_reason_prev, buffer);
		} else
			pack16(dump_job_ptr->state_reason, buffer);
		pack8(dump_job_ptr->power_flags,   buffer);
		pack8(dump_job_ptr->reboot,        buffer);
		pack16(dump_job_ptr->restart_cnt,  buffer);
		pack16(show_flags,  buffer);
		pack_time(dump_job_ptr->deadline, buffer);

		pack32(dump_job_ptr->alloc_sid, buffer);
		if ((dump_job_ptr->time_limit == NO_VAL)
		    && dump_job_ptr->part_ptr)
			time_limit = dump_job_ptr->part_ptr->max_time;
		else
			time_limit = dump_job_ptr->time_limit;

		pack32(time_limit, buffer);
		pack32(dump_job_ptr->time_min, buffer);

		if (dump_job_ptr->details) {
			pack32(dump_job_ptr->details->nice,  buffer);
			pack_time(dump_job_ptr->details->submit_time, buffer);
			/* Earliest possible begin time */
			begin_time = dump_job_ptr->details->begin_time;
		} else {   /* Some job details may be purged after completion */
			pack32(NICE_OFFSET, buffer);	/* Best guess */
			pack_time((time_t) 0, buffer);
		}

		pack_time(begin_time, buffer);

		if (IS_JOB_STARTED(dump_job_ptr)) {
			/* Report actual start time, in past */
			start_time = dump_job_ptr->start_time;
			end_time = dump_job_ptr->end_time;
		} else if (dump_job_ptr->start_time != 0) {
			/* Report expected start time,
			 * making sure that time is not in the past */
			start_time = MAX(dump_job_ptr->start_time, time(NULL));
			if (time_limit != NO_VAL) {
				end_time = MAX(dump_job_ptr->end_time,
					       (start_time + time_limit * 60));
			}
		} else	if (begin_time > time(NULL)) {
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
		pack_time(dump_job_ptr->preempt_time, buffer);
		pack32(dump_job_ptr->priority, buffer);
		packdouble(dump_job_ptr->billable_tres, buffer);

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
		packstr(dump_job_ptr->admin_comment, buffer);
		packstr(dump_job_ptr->network, buffer);
		packstr(dump_job_ptr->comment, buffer);
		packstr(dump_job_ptr->gres, buffer);
		packstr(dump_job_ptr->batch_host, buffer);
		packnull(buffer); /* was batch_script */
		packstr(dump_job_ptr->burst_buffer, buffer);
		packstr(dump_job_ptr->burst_buffer_state, buffer);

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
		packstr(dump_job_ptr->mcs_label, buffer);

		pack32(dump_job_ptr->exit_code, buffer);
		pack32(dump_job_ptr->derived_ec, buffer);

		if (show_flags & SHOW_DETAIL) {
			pack_job_resources(dump_job_ptr->job_resrcs, buffer,
					   protocol_version);
			_pack_job_gres(dump_job_ptr, buffer, protocol_version);
		} else {
			pack32(NO_VAL, buffer);
			pack32((uint32_t) 0, buffer);
		}

		packstr(dump_job_ptr->name, buffer);
		packstr(dump_job_ptr->wckey, buffer);
		pack32(dump_job_ptr->req_switch, buffer);
		pack32(dump_job_ptr->wait4switch, buffer);

		packstr(dump_job_ptr->alloc_node, buffer);
		if (!IS_JOB_COMPLETING(dump_job_ptr))
			pack_bit_str_hex(dump_job_ptr->node_bitmap, buffer);
		else
			pack_bit_str_hex(dump_job_ptr->node_bitmap_cg, buffer);

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
		pack32(dump_job_ptr->bit_flags, buffer);
		packstr(dump_job_ptr->tres_fmt_alloc_str, buffer);
		packstr(dump_job_ptr->tres_fmt_req_str, buffer);
		pack16(dump_job_ptr->start_protocol_ver, buffer);

		if (dump_job_ptr->fed_details) {
			packstr(dump_job_ptr->fed_details->origin_str, buffer);
			pack64(dump_job_ptr->fed_details->siblings_active,
			       buffer);
			packstr(dump_job_ptr->fed_details->siblings_active_str,
				buffer);
		} else {
			packnull(buffer);
			pack64((uint64_t)0, buffer);
			packnull(buffer);
		}
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
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

		pack32(dump_job_ptr->job_state,    buffer);
		pack16(dump_job_ptr->batch_flag,   buffer);
		if ((dump_job_ptr->state_reason == WAIT_NO_REASON) &&
		    IS_JOB_PENDING(dump_job_ptr)) {
			/* Scheduling cycle in progress, send latest reason */
			pack16(dump_job_ptr->state_reason_prev, buffer);
		} else
			pack16(dump_job_ptr->state_reason, buffer);
		pack8(dump_job_ptr->power_flags,   buffer);
		pack8(dump_job_ptr->reboot,        buffer);
		pack16(dump_job_ptr->restart_cnt,  buffer);
		pack16(show_flags,  buffer);
		pack_time(dump_job_ptr->deadline, buffer);

		pack32(dump_job_ptr->alloc_sid, buffer);
		if ((dump_job_ptr->time_limit == NO_VAL)
		    && dump_job_ptr->part_ptr)
			pack32(dump_job_ptr->part_ptr->max_time, buffer);
		else
			pack32(dump_job_ptr->time_limit, buffer);
		pack32(dump_job_ptr->time_min, buffer);

		if (dump_job_ptr->details) {
			pack32(dump_job_ptr->details->nice,  buffer);
			pack_time(dump_job_ptr->details->submit_time, buffer);
			/* Earliest possible begin time */
			begin_time = dump_job_ptr->details->begin_time;
		} else {   /* Some job details may be purged after completion */
			pack32(NICE_OFFSET, buffer);	/* Best guess */
			pack_time((time_t) 0, buffer);
		}

		pack_time(begin_time, buffer);

		if (IS_JOB_STARTED(dump_job_ptr)) {
			/* Report actual start time, in past */
			start_time = dump_job_ptr->start_time;
		} else if (dump_job_ptr->start_time != 0) {
			/* Report expected start time,
			 * making sure that time is not in the past */
			start_time = MAX(dump_job_ptr->start_time, time(NULL));
		} else	/* earliest start time in the future */
			start_time = begin_time;
		pack_time(start_time, buffer);

		pack_time(dump_job_ptr->end_time, buffer);
		pack_time(dump_job_ptr->suspend_time, buffer);
		pack_time(dump_job_ptr->pre_sus_time, buffer);
		pack_time(dump_job_ptr->resize_time, buffer);
		pack_time(dump_job_ptr->preempt_time, buffer);
		pack32(dump_job_ptr->priority, buffer);
		packdouble(dump_job_ptr->billable_tres, buffer);

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
		packnull(buffer); /* was batch_script */
		packstr(dump_job_ptr->burst_buffer, buffer);

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
		packstr(dump_job_ptr->mcs_label, buffer);

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
		pack32(dump_job_ptr->bit_flags, buffer);
		packstr(dump_job_ptr->tres_fmt_alloc_str, buffer);
		packstr(dump_job_ptr->tres_fmt_req_str, buffer);
		pack16(dump_job_ptr->start_protocol_ver, buffer);
	} else {
		error("pack_job: protocol_version "
		      "%hu not supported", protocol_version);
	}
}

static void _find_node_config(int *cpu_cnt_ptr, int *core_cnt_ptr)
{
	static int max_cpu_cnt = -1, max_core_cnt = -1;
	int i;
	struct node_record *node_ptr = node_record_table_ptr;

	*cpu_cnt_ptr  = max_cpu_cnt;
	*core_cnt_ptr = max_core_cnt;

	if (max_cpu_cnt != -1)
		return;

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
}

/* pack default job details for "get_job_info" RPC */
static void _pack_default_job_details(struct job_record *job_ptr,
				      Buf buffer, uint16_t protocol_version)
{
	int max_cpu_cnt = -1, max_core_cnt = -1;
	int i;
	struct job_details *detail_ptr = job_ptr->details;
	char *cmd_line = NULL;
	char *tmp = NULL;
	uint32_t len = 0;
	uint16_t shared = 0;

	if (!detail_ptr)
		shared = NO_VAL16;
	else if (detail_ptr->share_res == 1)	/* User --share */
		shared = 1;
	else if ((detail_ptr->share_res == 0) ||
		 (detail_ptr->whole_node == 1))
		shared = 0;			/* User --exclusive */
	else if (detail_ptr->whole_node == WHOLE_NODE_USER)
		shared = JOB_SHARED_USER;	/* User --exclusive=user */
	else if (detail_ptr->whole_node == WHOLE_NODE_MCS)
		shared = JOB_SHARED_MCS;	/* User --exclusive=mcs */
	else if (job_ptr->part_ptr) {
		/* Report shared status based upon latest partition info */
		if (job_ptr->part_ptr->flags & PART_FLAG_EXCLUSIVE_USER)
			shared = JOB_SHARED_USER;
		else if ((job_ptr->part_ptr->max_share & SHARED_FORCE) &&
			 ((job_ptr->part_ptr->max_share & (~SHARED_FORCE)) > 1))
			shared = 1;		/* Partition Shared=force */
		else if (job_ptr->part_ptr->max_share == 0)
			shared = 0;		/* Partition Shared=exclusive */
		else
			shared = NO_VAL16;  /* Part Shared=yes or no */
	} else
		shared = NO_VAL16;	/* No user or partition info */

	if (job_ptr->part_ptr && job_ptr->part_ptr->max_cpu_cnt) {
		max_cpu_cnt  = job_ptr->part_ptr->max_cpu_cnt;
		max_core_cnt = job_ptr->part_ptr->max_core_cnt;
	} else
		_find_node_config(&max_cpu_cnt, &max_core_cnt);

	if (protocol_version >= SLURM_17_11_PROTOCOL_VERSION) {
		if (detail_ptr) {
			packstr(detail_ptr->features,   buffer);
			packstr(detail_ptr->cluster_features, buffer);
			packstr(detail_ptr->work_dir,   buffer);
			packstr(detail_ptr->dependency, buffer);

			if (detail_ptr->argv) {
				/* Determine size needed for a string
				 * containing all arguments */
				for (i =0; detail_ptr->argv[i]; i++) {
					len += strlen(detail_ptr->argv[i]);
				}
				len += i;

				cmd_line = xmalloc(len*sizeof(char));
				tmp = cmd_line;
				for (i = 0; detail_ptr->argv[i]; i++) {
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
				    != INFINITE16)) {
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
			if (detail_ptr->num_tasks)
				pack32(detail_ptr->num_tasks, buffer);
			else if (IS_JOB_PENDING(job_ptr))
				pack32(detail_ptr->min_nodes, buffer);
			else
				pack32(job_ptr->node_cnt, buffer);
			pack16(shared, buffer);
			pack32(detail_ptr->cpu_freq_min, buffer);
			pack32(detail_ptr->cpu_freq_max, buffer);
			pack32(detail_ptr->cpu_freq_gov, buffer);
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
		}
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		if (detail_ptr) {
			packstr(detail_ptr->features,   buffer);
			packstr(detail_ptr->work_dir,   buffer);
			packstr(detail_ptr->dependency, buffer);

			if (detail_ptr->argv) {
				/* Determine size needed for a string
				 * containing all arguments */
				for (i =0; detail_ptr->argv[i]; i++) {
					len += strlen(detail_ptr->argv[i]);
				}
				len += i;

				cmd_line = xmalloc(len*sizeof(char));
				tmp = cmd_line;
				for (i = 0; detail_ptr->argv[i]; i++) {
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
				    != INFINITE16)) {
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
			if (detail_ptr->num_tasks)
				pack32(detail_ptr->num_tasks, buffer);
			else if (IS_JOB_PENDING(job_ptr))
				pack32(detail_ptr->min_nodes, buffer);
			else
				pack32(job_ptr->node_cnt, buffer);
			pack16(shared, buffer);
			pack32(detail_ptr->cpu_freq_min, buffer);
			pack32(detail_ptr->cpu_freq_max, buffer);
			pack32(detail_ptr->cpu_freq_gov, buffer);
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
	if (protocol_version >= SLURM_17_02_PROTOCOL_VERSION) {
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
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		if (detail_ptr) {
			pack16(detail_ptr->contiguous, buffer);
			pack16(detail_ptr->core_spec, buffer);
			pack16(detail_ptr->cpus_per_task, buffer);
			pack16(detail_ptr->pn_min_cpus, buffer);

			pack32(xlate_mem_new2old(detail_ptr->pn_min_memory), buffer);
			pack32(detail_ptr->pn_min_tmp_disk, buffer);

			packstr(detail_ptr->req_nodes, buffer);
			pack_bit_fmt(detail_ptr->req_node_bitmap, buffer);
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
	} else {
		error("_pack_pending_job_details: protocol_version "
		      "%hu not supported", protocol_version);
	}
}

static int _purge_pack_job_filter(void *x, void *key)
{
	struct job_record *job_ptr    = (struct job_record *) x;
	struct job_record *job_filter = (struct job_record *) key;
	if (job_ptr->pack_job_id == job_filter->pack_job_id)
		return 1;
	return 0;
}

/* If this is a pack job leader and all components are complete,
 * then purge all job of its pack job records
 * RET true if this record purged */
static inline bool _purge_complete_pack_job(struct job_record *pack_leader)
{
	struct job_record purge_job_rec;
	struct job_record *pack_job;
	ListIterator iter;
	bool incomplete_job = false;
	int i;

	if (!pack_leader->pack_job_list)
		return false;		/* Not pack leader */
	if (!IS_JOB_FINISHED(pack_leader))
		return false;		/* Pack leader incomplete */

	iter = list_iterator_create(pack_leader->pack_job_list);
	while ((pack_job = (struct job_record *) list_next(iter))) {
		if (pack_leader->pack_job_id != pack_job->pack_job_id) {
			error("%s: Bad pack_job_list for job %u",
			      __func__, pack_leader->pack_job_id);
			continue;
		}
		if (!_list_find_job_old(pack_job, NULL)) {
			incomplete_job = true;
			break;
		}
	}
	list_iterator_destroy(iter);

	if (incomplete_job)
		return false;

	purge_job_rec.pack_job_id = pack_leader->pack_job_id;
	i = list_delete_all(job_list, &_purge_pack_job_filter, &purge_job_rec);
	if (i) {
		debug2("%s: purged %d old job records", __func__, i);
		last_job_update = time(NULL);
		slurm_cond_signal(&purge_thread_cond);
	}
	return true;
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
	struct job_record *job_ptr;
	int i, purge_job_count;

	if ((purge_job_count = list_count(purge_files_list)))
		debug("%s: job file deletion is falling behind, "
		      "%d left to remove", __func__, purge_job_count);

	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		if (_purge_complete_pack_job(job_ptr))
			continue;
		if (!IS_JOB_PENDING(job_ptr))
			continue;
		if (test_job_dependency(job_ptr) == 2) {
			char jbuf[JBUFSIZ];

			/* Check what are the job disposition
			 * to deal with invalid dependecies
			 */
			if (job_ptr->bit_flags & KILL_INV_DEP) {
				_kill_dependent(job_ptr);
			} else if (job_ptr->bit_flags & NO_KILL_INV_DEP) {
				debug("%s: %s job dependency never satisfied",
				      __func__,
				      jobid2str(job_ptr, jbuf, sizeof(jbuf)));
				job_ptr->state_reason = WAIT_DEP_INVALID;
				xfree(job_ptr->state_desc);
			} else if (kill_invalid_dep) {
				_kill_dependent(job_ptr);
			} else {
				debug("%s: %s job dependency never satisfied",
				      __func__,
				      jobid2str(job_ptr, jbuf, sizeof(jbuf)));
				job_ptr->state_reason = WAIT_DEP_INVALID;
				xfree(job_ptr->state_desc);
			}
		}

		if (job_ptr->state_reason == WAIT_DEP_INVALID) {
			if (job_ptr->bit_flags & KILL_INV_DEP) {
				/* The job got the WAIT_DEP_INVALID
				 * before slurmctld was reconfigured.
				 */
				_kill_dependent(job_ptr);
			} else if (job_ptr->bit_flags & NO_KILL_INV_DEP) {
				continue;
			} else if (kill_invalid_dep) {
				_kill_dependent(job_ptr);
			}
		}
	}
	list_iterator_destroy(job_iterator);

	i = list_delete_all(job_list, &_list_find_job_old, "");
	if (i) {
		debug2("purge_old_job: purged %d old job records", i);
		last_job_update = time(NULL);
		slurm_cond_signal(&purge_thread_cond);
	}
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
	count = list_delete_all(job_list, &list_find_job_id, (void *)&job_id);
	if (count) {
		last_job_update = time(NULL);
		slurm_cond_signal(&purge_thread_cond);
	}

	return count;
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
	if (slurmctld_conf.preempt_mode & PREEMPT_MODE_GANG)
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
			char *err_part = NULL;
			part_ptr = find_part_record(job_ptr->partition);
			if (part_ptr == NULL) {
				part_ptr_list = get_part_list(
						job_ptr->partition,
						&err_part);
				if (part_ptr_list)
					part_ptr = list_peek(part_ptr_list);
			}
			if (part_ptr == NULL) {
				error("Invalid partition (%s) for job %u",
				      err_part, job_ptr->job_id);
				xfree(err_part);
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
		    (step_ptr->step_id != SLURM_EXTERN_CONT) &&
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
 * Return the next available job_id to be used.
 *
 * Must have job_write and fed_read locks when grabbing a job_id
 *
 * IN test_only - if true, doesn't advance the job_id sequence, just returns
 * 	what the next job id will be.
 * RET a valid job_id or SLURM_ERROR if all job_ids are exhausted.
 */
extern uint32_t get_next_job_id(bool test_only)
{
	int i;
	uint32_t new_id, max_jobs, tmp_id_sequence;

	max_jobs = slurmctld_conf.max_job_id - slurmctld_conf.first_job_id;
	tmp_id_sequence = MAX(job_id_sequence, slurmctld_conf.first_job_id);

	/* Ensure no conflict in job id if we roll over 32 bits */
	for (i = 0; i < max_jobs; i++) {
		if (++tmp_id_sequence >= slurmctld_conf.max_job_id)
			tmp_id_sequence = slurmctld_conf.first_job_id;

		new_id = fed_mgr_get_job_id(tmp_id_sequence);

		if (find_job_record(new_id))
			continue;
		if (_dup_job_file_test(new_id))
			continue;

		if (!test_only)
			job_id_sequence = tmp_id_sequence;

		return new_id;
	}

	error("We have exhausted our supply of valid job id values. "
	      "FirstJobId=%u MaxJobId=%u", slurmctld_conf.first_job_id,
	      slurmctld_conf.max_job_id);
	return SLURM_ERROR;
}

/*
 * _set_job_id - set a default job_id, ensure that it is unique
 * IN job_ptr - pointer to the job_record
 */
static int _set_job_id(struct job_record *job_ptr)
{
	uint32_t new_id;

	xassert(job_ptr);
	xassert (job_ptr->magic == JOB_MAGIC);

	if ((new_id = get_next_job_id(false)) != SLURM_ERROR) {
		job_ptr->job_id = new_id;
		/* When we get a new job id might as well make sure
		 * the db_index is 0 since there is no way it will be
		 * correct otherwise :). */
		job_ptr->db_index = 0;
		return SLURM_SUCCESS;
	}

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
	ListIterator job_iterator;
	struct job_record *job_ptr;
	uint32_t prio_boost = 0;

	if ((highest_prio != 0) && (highest_prio < TOP_PRIORITY))
		prio_boost = TOP_PRIORITY - highest_prio;
	if (xstrcmp(slurmctld_conf.priority_type, "priority/basic") ||
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
static bool _top_priority(struct job_record *job_ptr, uint32_t pack_job_offset)
{
	struct job_details *detail_ptr = job_ptr->details;
	time_t now = time(NULL);
	int pend_time;
	bool top;

#ifdef HAVE_BG
	static uint16_t static_part = NO_VAL16;
	int rc = SLURM_SUCCESS;

	/* On BlueGene with static partitioning, we don't want to delay
	 * jobs based upon priority since jobs of different sizes can
	 * execute on different sets of nodes. While sched/backfill would
	 * eventually start the job if delayed here based upon priority,
	 * that could delay the initiation of a job by a few seconds. */
	if (static_part == NO_VAL16) {
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
			if ((pack_job_offset != NO_VAL) && (job_ptr->job_id ==
			    (job_ptr2->job_id + pack_job_offset)))
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
			if (!acct_policy_job_runnable_state(job_ptr2) ||
			    !misc_policy_job_runnable_state(job_ptr2) ||
			    !part_policy_job_runnable_state(job_ptr2) ||
			    !job_independent(job_ptr2, 0))
				continue;
			if ((job_ptr2->resv_name && (!job_ptr->resv_name)) ||
			    ((!job_ptr2->resv_name) && job_ptr->resv_name))
				continue;	/* different reservation */
			if (job_ptr2->resv_name && job_ptr->resv_name &&
			    (!xstrcmp(job_ptr2->resv_name,
				      job_ptr->resv_name))) {
				/* same reservation */
				if (job_ptr2->priority <= job_ptr->priority)
					continue;
				top = false;
				break;
			}

			if (bb_g_job_test_stage_in(job_ptr2, true) != 1)
				continue;	/* Waiting for buffer */

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
			if ((job_ptr2->part_ptr->priority_tier >
			     job_ptr ->part_ptr->priority_tier) ||
			    ((job_ptr2->part_ptr->priority_tier ==
			      job_ptr ->part_ptr->priority_tier) &&
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
			    && (job_ptr->state_reason != WAIT_RESV_DELETED)
			    && (job_ptr->state_reason != FAIL_BURST_BUFFER_OP)
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
	return;
}

static void _hold_job_rec(struct job_record *job_ptr, uid_t uid)
{
	int i, j;

	job_ptr->direct_set_prio = 1;
	job_ptr->priority = 0;
	if (job_ptr->part_ptr_list && job_ptr->priority_array) {
		j = list_count(job_ptr->part_ptr_list);
		for (i = 0; i < j; i++) {
			job_ptr->priority_array[i] = 0;
		}
	}
	info("sched: %s: hold on job_id %u by uid %u", __func__,
	     job_ptr->job_id, uid);
}
static void _hold_job(struct job_record *job_ptr, uid_t uid)
{
	static time_t sched_update = 0;
	static bool whole_pack = false;
	char *sched_params;
	struct job_record *pack_leader = NULL, *pack_job;
	ListIterator iter;

	if (sched_update != slurmctld_conf.last_update) {
		sched_update = slurmctld_conf.last_update;
		sched_params = slurm_get_sched_params();
		if (sched_params) {
			if (strstr(sched_params, "whole_pack"))
				whole_pack = true;
			else
				whole_pack = false;
			xfree(sched_params);
		}
	}

	if (job_ptr->pack_job_id && whole_pack)
		pack_leader = find_job_record(job_ptr->pack_job_id);
	if (pack_leader && pack_leader->pack_job_list) {
		iter = list_iterator_create(pack_leader->pack_job_list);
		while ((pack_job = (struct job_record *) list_next(iter)))
			_hold_job_rec(pack_job, uid);
		list_iterator_destroy(iter);
		return;
	}
	_hold_job_rec(job_ptr, uid);
}
static void _release_job_rec(struct job_record *job_ptr, uid_t uid)
{
	job_ptr->direct_set_prio = 0;
	set_job_prio(job_ptr);
	job_ptr->state_reason = WAIT_NO_REASON;
	job_ptr->state_reason_prev = WAIT_NO_REASON;
	job_ptr->job_state &= ~JOB_SPECIAL_EXIT;
	xfree(job_ptr->state_desc);
	job_ptr->exit_code = 0;
	fed_mgr_job_requeue(job_ptr); /* submit sibling jobs */
	info("sched: %s: release hold on job_id %u by uid %u", __func__,
	     job_ptr->job_id, uid);
}
static void _release_job(struct job_record *job_ptr, uid_t uid)
{
	static time_t sched_update = 0;
	static bool whole_pack = false;
	char *sched_params;
	struct job_record *pack_leader = NULL, *pack_job;
	ListIterator iter;

	if (sched_update != slurmctld_conf.last_update) {
		sched_update = slurmctld_conf.last_update;
		sched_params = slurm_get_sched_params();
		if (sched_params) {
			if (strstr(sched_params, "whole_pack"))
				whole_pack = true;
			else
				whole_pack = false;
			xfree(sched_params);
		}
	}

	if (job_ptr->pack_job_id && whole_pack)
		pack_leader = find_job_record(job_ptr->pack_job_id);
	if (pack_leader && pack_leader->pack_job_list) {
		iter = list_iterator_create(pack_leader->pack_job_list);
		while ((pack_job = (struct job_record *) list_next(iter)))
			_release_job_rec(pack_job, uid);
		list_iterator_destroy(iter);
		return;
	}
	_release_job_rec(job_ptr, uid);
}

static int _update_job(struct job_record *job_ptr, job_desc_msg_t * job_specs,
		       uid_t uid)
{
	int error_code = SLURM_SUCCESS;
	enum job_state_reason fail_reason;
	bool operator = false;
	uint32_t save_min_nodes = 0, save_max_nodes = 0;
	uint32_t save_min_cpus = 0, save_max_cpus = 0;
	struct job_details *detail_ptr;
	struct part_record *tmp_part_ptr;
	bitstr_t *exc_bitmap = NULL, *req_bitmap = NULL;
	time_t now = time(NULL);
	multi_core_data_t *mc_ptr = NULL;
	bool update_accounting = false;
	acct_policy_limit_set_t acct_policy_limit_set;
	uint16_t tres[slurmctld_tres_cnt];
	bool acct_limit_already_set;
	bool tres_changed = false;
	int tres_pos;
	uint64_t tres_req_cnt[slurmctld_tres_cnt];
	List gres_list = NULL;
	List license_list = NULL;
	assoc_mgr_lock_t locks = { NO_LOCK, NO_LOCK, NO_LOCK, NO_LOCK,
				   READ_LOCK, NO_LOCK, NO_LOCK };

#ifdef HAVE_BG
	uint16_t conn_type[SYSTEM_DIMENSIONS] = {NO_VAL16};
	uint16_t reboot = NO_VAL16;
	uint16_t rotate = NO_VAL16;
	uint16_t geometry[SYSTEM_DIMENSIONS] = {NO_VAL16};
	char *image = NULL;
	static uint32_t cpus_per_mp = 0;
	static uint16_t cpus_per_node = 0;

	if (!cpus_per_mp)
		select_g_alter_node_cnt(SELECT_GET_MP_CPU_CNT, &cpus_per_mp);
	if (!cpus_per_node)
		select_g_alter_node_cnt(SELECT_GET_NODE_CPU_CNT,
					&cpus_per_node);
#endif

	/* This means we are in the middle of requesting the db_inx from the
	 * database. So we can't update right now.  You should try again outside
	 * the job_write lock in a second or so.
	 */
	if (job_ptr->db_index == NO_VAL64)
		return ESLURM_JOB_SETTING_DB_INX;

	operator = validate_operator(uid);
	if (job_specs->burst_buffer) {
		/* burst_buffer contents are validated at job submit time and
		 * data is possibly being staged at later times. It can not
		 * be changed except to clear the value on a completed job and
		 * purge the record in order to recover from a failure mode */
		if (IS_JOB_COMPLETED(job_ptr) && operator &&
		    (job_specs->burst_buffer[0] == '\0')) {
			xfree(job_ptr->burst_buffer);
			last_job_update = now;
		} else {
			error_code = ESLURM_NOT_SUPPORTED;
		}
	}
	if (error_code != SLURM_SUCCESS)
		goto fini;

	if (IS_JOB_FINISHED(job_ptr)) {
		error_code = ESLURM_JOB_FINISHED;
		goto fini;
	}

	if (job_specs->user_id == NO_VAL) {
		/* Used by job_submit/lua to find default partition and
		 * access control logic below to validate partition change */
		job_specs->user_id = job_ptr->user_id;
	}
	error_code = job_submit_plugin_modify(job_specs, job_ptr,
					      (uint32_t) uid);
	if (error_code != SLURM_SUCCESS)
		return error_code;
	error_code = node_features_g_job_valid(job_specs->features);
	if (error_code != SLURM_SUCCESS)
		return error_code;

	error_code = _test_job_desc_fields(job_specs);
	if (error_code != SLURM_SUCCESS)
		return error_code;

	memset(&acct_policy_limit_set, 0, sizeof(acct_policy_limit_set_t));
	acct_policy_limit_set.tres = tres;

	if (operator) {
		/* set up the acct_policy if we are at least an operator */
		for (tres_pos = 0; tres_pos < slurmctld_tres_cnt; tres_pos++)
			acct_policy_limit_set.tres[tres_pos] = ADMIN_SET_LIMIT;
		acct_policy_limit_set.time = ADMIN_SET_LIMIT;
		acct_policy_limit_set.qos = ADMIN_SET_LIMIT;
	} else
		memset(tres, 0, sizeof(tres));

	if ((job_ptr->user_id != uid) && !operator &&
	    !assoc_mgr_is_user_acct_coord(
		    acct_db_conn, uid, job_ptr->account)) {
		error("Security violation, JOB_UPDATE RPC from uid %d",
		      uid);
		return ESLURM_USER_ID_MISSING;
	}

	detail_ptr = job_ptr->details;
	if (detail_ptr)
		mc_ptr = detail_ptr->mc_ptr;
	last_job_update = now;

	/*
	 * Check partition here just in case the min_nodes is changed based on
	 * the partition
	 */
	if (job_specs->partition &&
	    !xstrcmp(job_specs->partition, job_ptr->partition)) {
		debug("sched: update_job: new partition identical to "
		      "old partition %u", job_ptr->job_id);
	} else if (job_specs->partition) {
		List part_ptr_list = NULL;
		bool resv_reset = false;
		char *resv_orig = NULL;

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
			resv_reset = true;
			resv_orig = job_specs->reservation;
			job_specs->reservation = job_ptr->resv_name;
		}

		error_code = _get_job_parts(job_specs,
					    &tmp_part_ptr,
					    &part_ptr_list, NULL);

		if (error_code != SLURM_SUCCESS)
			;
		else if ((tmp_part_ptr->state_up & PARTITION_SUBMIT) == 0)
			error_code = ESLURM_PARTITION_NOT_AVAIL;
		else {
			slurmdb_assoc_rec_t assoc_rec;
			memset(&assoc_rec, 0,
			       sizeof(slurmdb_assoc_rec_t));
			assoc_rec.acct      = job_ptr->account;
			assoc_rec.partition = tmp_part_ptr->name;
			assoc_rec.uid       = job_ptr->user_id;
			if (assoc_mgr_fill_in_assoc(
				    acct_db_conn, &assoc_rec,
				    accounting_enforce,
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
			if (!error_code) {
				acct_policy_remove_job_submit(job_ptr);
				xfree(job_ptr->partition);
				job_ptr->partition =
					xstrdup(job_specs->partition);
				job_ptr->part_ptr = tmp_part_ptr;
				xfree(job_ptr->priority_array);	/* Rebuilt in
								   plugin */
				FREE_NULL_LIST(job_ptr->part_ptr_list);
				job_ptr->part_ptr_list = part_ptr_list;
				part_ptr_list = NULL;	/* nothing to free */
				info("update_job: setting partition to %s for "
				     "job_id %u", job_specs->partition,
				     job_ptr->job_id);
				update_accounting = true;
				acct_policy_add_job_submit(job_ptr);
			}
		}
		FREE_NULL_LIST(part_ptr_list);	/* error clean-up */

		if (resv_reset)
			job_specs->reservation = resv_orig;

		if (error_code != SLURM_SUCCESS)
			goto fini;
	}

	memset(tres_req_cnt, 0, sizeof(tres_req_cnt));
	job_specs->tres_req_cnt = tres_req_cnt;

	if ((job_specs->min_nodes != NO_VAL) &&
	    (job_specs->min_nodes != INFINITE)) {
		uint32_t min_cpus = (job_specs->pn_min_cpus != NO_VAL16 ?
			job_specs->pn_min_cpus : detail_ptr->pn_min_cpus) *
			job_specs->min_nodes;
		uint32_t num_cpus = job_specs->min_cpus != NO_VAL ?
			job_specs->min_cpus :
			job_ptr->tres_req_cnt[TRES_ARRAY_CPU];
		uint32_t num_tasks = job_specs->num_tasks != NO_VAL ?
			job_specs->num_tasks : detail_ptr->num_tasks;

		if (!num_tasks) {
			num_tasks = detail_ptr->min_nodes;

		} else if (num_tasks < job_specs->min_nodes) {
			info("%s: adjusting num_tasks (prev: %u) to be at least min_nodes: %u",
			     __func__, num_tasks, job_specs->min_nodes);
			num_tasks = job_specs->min_nodes;
			if (IS_JOB_PENDING(job_ptr))
				job_specs->num_tasks = num_tasks;
		}

		num_tasks *= job_specs->cpus_per_task != NO_VAL16 ?
			job_specs->cpus_per_task : detail_ptr->cpus_per_task;
		num_tasks = MAX(num_tasks, min_cpus);
		if (num_tasks > num_cpus) {
			info("%s: adjusting min_cpus (prev: %u) to be at least : %u",
			     __func__, num_cpus, num_tasks);
			job_specs->min_cpus = num_tasks;

			job_specs->pn_min_memory =
				job_specs->pn_min_memory != NO_VAL64 ?
				job_specs->pn_min_memory :
				detail_ptr->pn_min_memory;
		}

		assoc_mgr_lock(&locks);
		if (!job_specs->gres) {
			gres_set_job_tres_cnt(job_ptr->gres_list,
					      job_specs->min_nodes,
					      job_specs->tres_req_cnt,
					      true);
		}

		if (!job_specs->licenses) {
			license_set_job_tres_cnt(job_ptr->license_list,
						 job_specs->tres_req_cnt,
						 true);
		}
		assoc_mgr_unlock(&locks);


		job_specs->tres_req_cnt[TRES_ARRAY_NODE] = job_specs->min_nodes;
	}

	if (job_specs->min_cpus != NO_VAL)
		job_specs->tres_req_cnt[TRES_ARRAY_CPU] = job_specs->min_cpus;
	else if ((job_specs->pn_min_cpus != NO_VAL16) &&
		 (job_specs->pn_min_cpus != 0)) {
		job_specs->tres_req_cnt[TRES_ARRAY_CPU] =
			job_specs->pn_min_cpus *
			(job_specs->min_nodes != NO_VAL ?
			 job_specs->min_nodes :
			 detail_ptr ? detail_ptr->min_nodes : 1);
		job_specs->min_cpus = job_specs->tres_req_cnt[TRES_ARRAY_CPU];
	}

	job_specs->tres_req_cnt[TRES_ARRAY_MEM] = job_get_tres_mem(
		job_specs->pn_min_memory,
		job_specs->tres_req_cnt[TRES_ARRAY_CPU] ?
		job_specs->tres_req_cnt[TRES_ARRAY_CPU] :
		job_ptr->tres_req_cnt[TRES_ARRAY_CPU],
		job_specs->min_nodes != NO_VAL ?
		job_specs->min_nodes :
		detail_ptr ? detail_ptr->min_nodes : 1);

	if (job_specs->gres) {
		if (!xstrcmp(job_specs->gres, job_ptr->gres)) {
			debug("sched: update_job: new gres identical to "
			      "old gres \"%s\"", job_ptr->gres);
		} else if ((!IS_JOB_PENDING(job_ptr)) || (detail_ptr == NULL)
			   || (detail_ptr->expanding_jobid != 0)) {
			error_code = ESLURM_JOB_NOT_PENDING;
		} else if ((error_code = gres_plugin_job_state_validate(
				    &job_specs->gres, &gres_list))) {
			if (error_code == ESLURM_DUPLICATE_GRES)
				info("sched: update_job: duplicate gres %s for job %u",
				     job_specs->gres, job_ptr->job_id);
			else
				info("sched: update_job: invalid gres %s for job %u",
				     job_specs->gres, job_ptr->job_id);
		} else {
			gres_set_job_tres_cnt(gres_list,
					      detail_ptr->min_nodes,
					      job_specs->tres_req_cnt,
					      false);
		}
	}

	if (error_code != SLURM_SUCCESS)
		goto fini;

	if (job_specs->licenses && !xstrcmp(job_specs->licenses,
					    job_ptr->licenses)) {
		debug("sched: update_job: new licenses identical to old licenses \"%s\"",
		      job_ptr->licenses);
	} else if (job_specs->licenses) {
		bool valid, pending = IS_JOB_PENDING(job_ptr);
		license_list = license_validate(job_specs->licenses,
						pending ?
						tres_req_cnt : NULL,
						&valid);

		if (!valid) {
			info("sched: update_job: invalid licenses: %s",
			     job_specs->licenses);
			error_code = ESLURM_INVALID_LICENSES;
		}
	}

	if (error_code != SLURM_SUCCESS)
		goto fini;


	/* Check to see if the requested job_specs exceeds any
	 * existing limit.  If it passes cool, we will check the new
	 * association/qos later in the code.  This will prevent the
	 * update returning an error code that is confusing since many
	 * things could successfully update and we are now just
	 * violating a limit.  The job won't be allowed to run, but it
	 * will allow the update to happen which is most likely what
	 * was desired.
	 *
	 * FIXME: Should we really be looking at the potentially old
	 * part, assoc, and qos pointer?  This patch is from bug 1381
	 * for future reference.
	 */

	acct_limit_already_set = false;
	if (!operator && (accounting_enforce & ACCOUNTING_ENFORCE_LIMITS)) {
		uint32_t orig_time_limit = job_specs->time_limit;
		if (!acct_policy_validate(job_specs, job_ptr->part_ptr,
					  job_ptr->assoc_ptr, job_ptr->qos_ptr,
					  NULL, &acct_policy_limit_set, 1)) {
			debug("%s: exceeded association's cpu, node, "
			      "memory or time limit for user %u",
			      __func__, job_specs->user_id);
			acct_limit_already_set = true;
		}
		if ((orig_time_limit == NO_VAL) &&
		    (job_ptr->time_limit < job_specs->time_limit))
			job_specs->time_limit = NO_VAL;
	}

	if (job_specs->account
	    && !xstrcmp(job_specs->account, job_ptr->account)) {
		debug("sched: update_job: new account identical to "
		      "old account %u", job_ptr->job_id);
	} else if (job_specs->account) {
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

	if (job_specs->exc_nodes && detail_ptr &&
	    !xstrcmp(job_specs->exc_nodes, detail_ptr->exc_nodes)) {
		debug("sched: update_job: new exc_nodes identical to old exc_nodes %s",
		      job_specs->exc_nodes);
	} else if (job_specs->exc_nodes) {
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
					xstrdup(job_specs->exc_nodes);
				FREE_NULL_BITMAP(detail_ptr->exc_node_bitmap);
				detail_ptr->exc_node_bitmap = exc_bitmap;
				info("sched: update_job: setting exc_nodes to "
				     "%s for job_id %u", job_specs->exc_nodes,
				     job_ptr->job_id);
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
		    (job_ptr->details && job_ptr->details->expanding_jobid)) {
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
			(void) gs_job_start(job_ptr);
			gres_build_job_details(job_ptr->gres_list,
					       &job_ptr->gres_detail_cnt,
					       &job_ptr->gres_detail_str);
			job_post_resize_acctg(job_ptr);
			/* Since job_post_resize_acctg will restart
			 * things, don't do it again. */
			update_accounting = false;
		} else {
			update_accounting = true;
		}
		FREE_NULL_BITMAP(req_bitmap);
	} else	/* NOTE: continues to "if" logic below */
#endif

	if (job_specs->req_nodes) {
		if ((!IS_JOB_PENDING(job_ptr)) || (detail_ptr == NULL))
			error_code = ESLURM_JOB_NOT_PENDING;
		else if (job_specs->req_nodes[0] == '\0') {
			xfree(detail_ptr->req_nodes);
			FREE_NULL_BITMAP(detail_ptr->req_node_bitmap);
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
					xstrdup(job_specs->req_nodes);
				FREE_NULL_BITMAP(detail_ptr->req_node_bitmap);
				detail_ptr->req_node_bitmap = req_bitmap;
				info("sched: update_job: setting req_nodes to "
				     "%s for job_id %u", job_specs->req_nodes,
				     job_ptr->job_id);
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

	/* NOTE: Update QOS before updating partition in order to enforce
	 * AllowQOS and DenyQOS partition configuration options */
	if (job_specs->qos) {
		slurmdb_qos_rec_t qos_rec;
		slurmdb_qos_rec_t *new_qos_ptr;
		char *resv_name;

		if (job_specs->reservation
		    && job_specs->reservation[0] != '\0')
			resv_name = job_specs->reservation;
		else
			resv_name = job_ptr->resv_name;

		memset(&qos_rec, 0, sizeof(slurmdb_qos_rec_t));

		/* If the qos is blank that means we want the default */
		if (job_specs->qos[0])
			qos_rec.name = job_specs->qos;

		new_qos_ptr = _determine_and_validate_qos(
			resv_name, job_ptr->assoc_ptr,
			operator, &qos_rec, &error_code, false);
		if ((error_code == SLURM_SUCCESS) && new_qos_ptr) {
			if (job_ptr->qos_id != qos_rec.id &&
			    IS_JOB_PENDING(job_ptr)) {
				info("%s: setting QOS to %s for job_id %u",
				     __func__, new_qos_ptr->name,
				     job_ptr->job_id);
				acct_policy_remove_job_submit(job_ptr);
				job_ptr->qos_id = qos_rec.id;
				job_ptr->qos_ptr = new_qos_ptr;
				job_ptr->limit_set.qos =
					acct_policy_limit_set.qos;
				update_accounting = true;
				acct_policy_add_job_submit(job_ptr);
			} else if (job_ptr->qos_id == qos_rec.id) {
				debug("sched: update_job: new QOS identical "
				      "to old QOS %u", job_ptr->job_id);
			} else {
				error_code = ESLURM_JOB_NOT_PENDING;
			}
		}
	}
	if (error_code != SLURM_SUCCESS)
		goto fini;









	/* Always do this last just in case the assoc_ptr changed */
	if (job_specs->admin_comment) {
		if (!validate_super_user(uid)) {
			error("Attempt to change admin_comment for job %u",
			      job_ptr->job_id);
			error_code = ESLURM_ACCESS_DENIED;
		} else if ((job_specs->admin_comment[0] == '+') &&
			   (job_specs->admin_comment[1] == '=')) {
			if (job_ptr->admin_comment)
				xstrcat(job_ptr->admin_comment, ",");
			xstrcat(job_ptr->admin_comment,
				job_specs->admin_comment + 2);
			info("update_job: adding to admin_comment it is now %s for job_id %u",
			     job_ptr->admin_comment, job_ptr->job_id);
		} else if (!xstrcmp(job_ptr->admin_comment,
				   job_specs->admin_comment)) {
			info("update_job: admin_comment the same as before, not changing");
		} else {
			xfree(job_ptr->admin_comment);
			job_ptr->admin_comment =
				xstrdup(job_specs->admin_comment);
			info("update_job: setting admin_comment to %s for job_id %u",
			     job_ptr->admin_comment, job_ptr->job_id);
		}
	}

	/* Always do this last just in case the assoc_ptr changed */
	if (job_specs->comment) {
		xfree(job_ptr->comment);
		job_ptr->comment = xstrdup(job_specs->comment);
		info("update_job: setting comment to %s for job_id %u",
		     job_ptr->comment, job_ptr->job_id);
	}

	if (error_code != SLURM_SUCCESS)
		goto fini;

	if (!operator && (accounting_enforce & ACCOUNTING_ENFORCE_LIMITS)) {
		uint32_t orig_time_limit = job_specs->time_limit;
		if (!acct_policy_validate(job_specs, job_ptr->part_ptr,
					  job_ptr->assoc_ptr, job_ptr->qos_ptr,
					  NULL, &acct_policy_limit_set, 1)
		    && acct_limit_already_set == false) {
			info("update_job: exceeded association's cpu, node, "
			     "memory or time limit for user %u",
			     job_specs->user_id);
			error_code = ESLURM_ACCOUNTING_POLICY;
			goto fini;
		}
		if ((orig_time_limit == NO_VAL) &&
		    (job_ptr->time_limit < job_specs->time_limit))
			job_specs->time_limit = NO_VAL;

		/* Perhaps the limit was removed, so we will remove it
		 * since it was imposed previously.
		 *
		 * acct_policy_validate will only set the time limit
		 * so don't worry about any of the others
		 */
		if (job_ptr->limit_set.time != ADMIN_SET_LIMIT)
			job_ptr->limit_set.time = acct_policy_limit_set.time;
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

	/* Reset min and max cpu counts as needed, ensure consistency */
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
		job_ptr->limit_set.tres[TRES_ARRAY_CPU] =
			acct_policy_limit_set.tres[TRES_ARRAY_CPU];
		detail_ptr->orig_min_cpus = job_specs->min_cpus;
		update_accounting = true;
	}
	if (save_max_cpus && (detail_ptr->max_cpus != save_max_cpus)) {
		info("update_job: setting max_cpus from "
		     "%u to %u for job_id %u",
		     save_max_cpus, detail_ptr->max_cpus, job_ptr->job_id);
		/* Always use the acct_policy_limit_set.* since if set by a
		 * super user it be set correctly */
		job_ptr->limit_set.tres[TRES_ARRAY_CPU] =
			acct_policy_limit_set.tres[TRES_ARRAY_CPU];
		detail_ptr->orig_max_cpus = job_specs->max_cpus;
		update_accounting = true;
	}

	if ((job_specs->pn_min_cpus != NO_VAL16) &&
	    (job_specs->pn_min_cpus != 0)) {

		if ((!IS_JOB_PENDING(job_ptr)) || (detail_ptr == NULL)) {
			error_code = ESLURM_JOB_NOT_PENDING;
		} else {
			detail_ptr->pn_min_cpus = job_specs->pn_min_cpus;
			info("update_job: setting pn_min_cpus to %u for "
			     "job_id %u", job_specs->pn_min_cpus,
			     job_ptr->job_id);
		}
	}

	if (error_code != SLURM_SUCCESS)
		goto fini;

	if (job_specs->cpus_per_task != NO_VAL16) {
		if ((!IS_JOB_PENDING(job_ptr)) || (detail_ptr == NULL)) {
			error_code = ESLURM_JOB_NOT_PENDING;
		} else if (detail_ptr->cpus_per_task !=
			   job_specs->cpus_per_task) {
			info("%s: setting cpus_per_task from %u to %u for "
			     "job_id %u", __func__, detail_ptr->cpus_per_task,
			     job_specs->cpus_per_task,
			     job_ptr->job_id);
			detail_ptr->cpus_per_task = job_specs->cpus_per_task;
		}
	}

	if (error_code != SLURM_SUCCESS)
		goto fini;

	/* Reset min and max node counts as needed, ensure consistency */
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
		job_ptr->limit_set.tres[TRES_ARRAY_NODE] =
			acct_policy_limit_set.tres[TRES_ARRAY_NODE];
		update_accounting = true;
	}
	if (save_max_nodes && (save_max_nodes != detail_ptr->max_nodes)) {
		info("update_job: setting max_nodes from "
		     "%u to %u for job_id %u",
		     save_max_nodes, detail_ptr->max_nodes, job_ptr->job_id);
		/* Always use the acct_policy_limit_set.* since if set by a
		 * super user it be set correctly */
		job_ptr->limit_set.tres[TRES_ARRAY_NODE] =
			acct_policy_limit_set.tres[TRES_ARRAY_NODE];
		update_accounting = true;
	}

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
		}
	}
	if (error_code != SLURM_SUCCESS)
		goto fini;

	if (job_specs->time_limit != NO_VAL) {
		if (IS_JOB_FINISHED(job_ptr) || job_ptr->preempt_time)
			error_code = ESLURM_JOB_FINISHED;
		else if (job_ptr->time_limit == job_specs->time_limit) {
			debug("sched: update_job: new time limit identical to "
			      "old time limit %u", job_ptr->job_id);
		} else if (operator ||
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
				job_ptr->end_time_exp = job_ptr->end_time;
			}
			info("sched: update_job: setting time_limit to %u for "
			     "job_id %u", job_specs->time_limit,
			     job_ptr->job_id);
			/* Always use the acct_policy_limit_set.*
			 * since if set by a super user it be set correctly */
			job_ptr->limit_set.time = acct_policy_limit_set.time;
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
			job_ptr->limit_set.time = acct_policy_limit_set.time;
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
		} else if (operator ||
			   (job_ptr->end_time > job_specs->end_time)) {
			int delta_t  = job_specs->end_time - job_ptr->end_time;
			job_ptr->end_time = job_specs->end_time;
			job_ptr->time_limit += (delta_t+30)/60; /* Sec->min */
			info("sched: update_job: setting time_limit to %u for "
			     "job_id %u", job_ptr->time_limit,
			     job_ptr->job_id);
			/* Always use the acct_policy_limit_set.*
			 * since if set by a super user it be set correctly */
			job_ptr->limit_set.time = acct_policy_limit_set.time;
			update_accounting = true;
		} else {
			info("sched: Attempt to extend end time for job %u",
			     job_ptr->job_id);
			error_code = ESLURM_ACCESS_DENIED;
		}
	}

	if ((job_specs->deadline) && (!IS_JOB_RUNNING(job_ptr))) {
		char time_str[32];
		slurm_make_time_str(&job_ptr->deadline, time_str,
				    sizeof(time_str));
		if (job_specs->deadline < now) {
			error_code = ESLURM_INVALID_TIME_VALUE;
		} else if (operator) {
			/* update deadline */
			job_ptr->deadline = job_specs->deadline;
			info("sched: update_job: setting deadline to %s for "
			     "job_id %u", time_str,
			     job_specs->job_id);
			/* Always use the acct_policy_limit_set.*
			 * since if set by a super user it be set correctly */
			job_ptr->limit_set.time = acct_policy_limit_set.time;
			update_accounting = true;
		} else {
			info("sched: Attempt to extend end time for job %u",
			     job_specs->job_id);
			error_code = ESLURM_ACCESS_DENIED;
		}
	}
	if (error_code != SLURM_SUCCESS)
		goto fini;

	if (job_specs->delay_boot != NO_VAL) {
		job_ptr->delay_boot = job_specs->delay_boot;
		info("sched: update_job: setting delay_boot to %u for job_id %u",
		     job_specs->delay_boot, job_ptr->job_id);
	}

	/* this needs to be after partition and QOS checks */
	if (job_specs->reservation
	    && !xstrcmp(job_specs->reservation, job_ptr->resv_name)) {
		debug("sched: update_job: new reservation identical to "
		      "old reservation %u", job_ptr->job_id);
	} else if (job_specs->reservation) {
		if (!IS_JOB_PENDING(job_ptr) && !IS_JOB_RUNNING(job_ptr)) {
			error_code = ESLURM_JOB_NOT_PENDING_NOR_RUNNING;
		} else {
			int rc;
			char *save_resv_name = job_ptr->resv_name;
			slurmctld_resv_t *save_resv_ptr = job_ptr->resv_ptr;

			job_ptr->resv_name = xstrdup(job_specs->reservation);
			rc = validate_job_resv(job_ptr);
			/* Make sure this job isn't using a partition or QOS
			 * that requires it to be in a reservation. */
			if (rc == SLURM_SUCCESS && !job_ptr->resv_name) {
				struct part_record *part_ptr =
					job_ptr->part_ptr;
				slurmdb_qos_rec_t *qos_ptr =
					job_ptr->qos_ptr;

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
				job_ptr->resv_name = save_resv_name;
				job_ptr->resv_ptr = save_resv_ptr;
				error_code = rc;
			}
		}
	}
	if (error_code != SLURM_SUCCESS)
		goto fini;

	if ((job_specs->requeue != NO_VAL16) && detail_ptr) {
		detail_ptr->requeue = MIN(job_specs->requeue, 1);
		info("sched: update_job: setting requeue to %u for job_id %u",
		     job_specs->requeue, job_ptr->job_id);
	}

	if (job_specs->priority != NO_VAL) {
		/*
		 * If we are doing time slicing we could update the
		 * priority of the job while running to give better
		 * position (larger time slices) than competing jobs
		 */
		if (IS_JOB_FINISHED(job_ptr) || (detail_ptr == NULL))
			error_code = ESLURM_JOB_FINISHED;
		else if (job_ptr->priority == job_specs->priority) {
			debug("%s: setting priority to current value",__func__);
			if ((job_ptr->priority == 0) && operator) {
				/*
				 * Authorized user can change from user hold
				 * to admin hold or admin hold to user hold
				 */
				if (job_specs->alloc_sid == ALLOC_SID_USER_HOLD)
					job_ptr->state_reason = WAIT_HELD_USER;
				else
					job_ptr->state_reason = WAIT_HELD;
			}
		} else if ((job_ptr->priority == 0) &&
			   (job_specs->priority == INFINITE) &&
			   (operator ||
			    (job_ptr->state_reason == WAIT_RESV_DELETED) ||
			    (job_ptr->state_reason == WAIT_HELD_USER))) {
			_release_job(job_ptr, uid);
		} else if ((job_ptr->priority == 0) &&
			   (job_specs->priority != INFINITE)) {
			info("ignore priority reset request on held job %u",
			     job_ptr->job_id);
			error_code = ESLURM_JOB_HELD;
		} else if (operator ||
			 (job_ptr->priority > job_specs->priority)) {
			if (job_specs->priority != 0)
				job_ptr->details->nice = NICE_OFFSET;
			if (job_specs->priority == INFINITE) {
				job_ptr->direct_set_prio = 0;
				set_job_prio(job_ptr);
			} else if (job_specs->priority == 0) {
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
				job_ptr->priority = job_specs->priority;
				if (job_ptr->part_ptr_list &&
				    job_ptr->priority_array) {
					int i, j = list_count(
						job_ptr->part_ptr_list);
					for (i = 0; i < j; i++) {
						job_ptr->priority_array[i] =
						job_specs->priority;
					}
				}
			}
			info("sched: %s: set priority to %u for job_id %u",
			      __func__, job_ptr->priority, job_ptr->job_id);
			update_accounting = true;
			if (job_ptr->priority == 0) {
				if (!operator ||
				    (job_specs->alloc_sid ==
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
			   (job_specs->priority == INFINITE)) {
			/*
			 * If the job was already released, ignore another
			 * release request.
			 */
			debug("%s: job %d already release ignoring request",
			      __func__, job_ptr->job_id);
		} else {
			error("sched: Attempt to modify priority for job %u",
			      job_ptr->job_id);
			error_code = ESLURM_ACCESS_DENIED;
		}
	} else if (job_ptr->state_reason == FAIL_BAD_CONSTRAINTS) {
		/* We need to check if the state is BadConstraints here since we
		 * are altering the job the bad constraint might have gone
		 * away.  If it did the priority (0) wouldn't get reset so the
		 * job would just go into JobAdminHeld otherwise.
		 */
		job_ptr->direct_set_prio = 0;
		set_job_prio(job_ptr);
		debug("sched: update: job request changed somehow, removing the bad constraints to reevaluate job_id %u uid %u",
		     job_ptr->job_id, uid);
		job_ptr->state_reason = WAIT_NO_REASON;
	}

	if (error_code != SLURM_SUCCESS)
		goto fini;

	if (job_specs->nice != NO_VAL) {
		if (IS_JOB_FINISHED(job_ptr) || (job_ptr->details == NULL))
			error_code = ESLURM_JOB_FINISHED;
		else if (job_ptr->details &&
			 (job_ptr->details->nice == job_specs->nice))
			debug("sched: update_job: new nice identical to "
			      "old nice %u", job_ptr->job_id);
		else if (job_ptr->direct_set_prio && job_ptr->priority != 0)
			info("ignore nice set request on  job %u",
			     job_ptr->job_id);
		else if (operator || (job_specs->nice >= NICE_OFFSET)) {
			if (!xstrcmp(slurmctld_conf.priority_type,
			             "priority/basic")) {
				int64_t new_prio = job_ptr->priority;
				new_prio += job_ptr->details->nice;
				new_prio -= job_specs->nice;
				job_ptr->priority = MAX(new_prio, 2);
				info("sched: update_job: nice changed from %u to %u, setting priority to %u for job_id %u",
				     job_ptr->details->nice,
				     job_specs->nice,
				     job_ptr->priority,
				     job_ptr->job_id);
			}
			job_ptr->details->nice = job_specs->nice;
			update_accounting = true;
		} else {
			error("sched: Attempt to modify nice for "
			      "job %u", job_ptr->job_id);
			error_code = ESLURM_ACCESS_DENIED;
		}
	}
	if (error_code != SLURM_SUCCESS)
		goto fini;

	if (job_specs->pn_min_memory != NO_VAL64) {
		if ((!IS_JOB_PENDING(job_ptr)) || (detail_ptr == NULL)) {
			error_code = ESLURM_JOB_NOT_PENDING;
		} else if (job_specs->pn_min_memory
			   == detail_ptr->pn_min_memory) {
			debug("sched: update_job: new memory limit identical "
			      "to old limit for job %u", job_ptr->job_id);
		} else {
			char *entity;
			if (job_specs->pn_min_memory == MEM_PER_CPU) {
				/* Map --mem-per-cpu=0 to --mem=0 */
				job_specs->pn_min_memory = 0;
			}
			if (job_specs->pn_min_memory & MEM_PER_CPU)
				entity = "cpu";
			else
				entity = "job";

			detail_ptr->pn_min_memory = job_specs->pn_min_memory;
			detail_ptr->orig_pn_min_memory =
					job_specs->pn_min_memory;
			info("sched: update_job: setting min_memory_%s to %"
			     ""PRIu64" for job_id %u", entity,
			     (job_specs->pn_min_memory & (~MEM_PER_CPU)),
			     job_ptr->job_id);
			/* Always use the acct_policy_limit_set.*
			 * since if set by a super user it be set correctly */
			job_ptr->limit_set.tres[TRES_ARRAY_MEM] =
				acct_policy_limit_set.tres[TRES_ARRAY_MEM];
		}
	}
	if (error_code != SLURM_SUCCESS)
		goto fini;

	if (job_specs->pn_min_tmp_disk != NO_VAL) {
		if ((!IS_JOB_PENDING(job_ptr)) || (detail_ptr == NULL)) {
			error_code = ESLURM_JOB_NOT_PENDING;
		} else {
			detail_ptr->pn_min_tmp_disk =
				job_specs->pn_min_tmp_disk;

			info("sched: update_job: setting job_min_tmp_disk to "
			     "%u for job_id %u", job_specs->pn_min_tmp_disk,
			     job_ptr->job_id);
		}
	}
	if (error_code != SLURM_SUCCESS)
		goto fini;

	if (job_specs->sockets_per_node != NO_VAL16) {
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

	if (job_specs->cores_per_socket != NO_VAL16) {
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

	if ((job_specs->threads_per_core != NO_VAL16)) {
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

	if (job_specs->shared != NO_VAL16) {
		if ((!IS_JOB_PENDING(job_ptr)) || (detail_ptr == NULL)) {
			error_code = ESLURM_JOB_NOT_PENDING;
		} else if (!operator) {
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

	if (job_specs->contiguous != NO_VAL16) {
		if ((!IS_JOB_PENDING(job_ptr)) || (detail_ptr == NULL))
			error_code = ESLURM_JOB_NOT_PENDING;
		else if (operator
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

	if (job_specs->core_spec != NO_VAL16) {
		if ((!IS_JOB_PENDING(job_ptr)) || (detail_ptr == NULL))
			error_code = ESLURM_JOB_NOT_PENDING;
		else if (operator && slurm_get_use_spec_resources()) {
			if (job_specs->core_spec == INFINITE16)
				detail_ptr->core_spec = NO_VAL16;
			else
				detail_ptr->core_spec = job_specs->core_spec;
			info("sched: update_job: setting core_spec to %u "
			     "for job_id %u", detail_ptr->core_spec,
			     job_ptr->job_id);
			if (detail_ptr->core_spec != NO_VAL16)
				detail_ptr->whole_node = 1;
		} else {
			error("sched: Attempt to modify core_spec for job %u",
			      job_ptr->job_id);
			error_code = ESLURM_ACCESS_DENIED;
		}
	}
	if (error_code != SLURM_SUCCESS)
		goto fini;

	if (job_specs->features && detail_ptr &&
	    !xstrcmp(job_specs->features, detail_ptr->features)) {
		debug("sched: update_job: new features identical to old features %s",
		      job_specs->features);
	} else if (job_specs->features) {
		if ((!IS_JOB_PENDING(job_ptr)) || (detail_ptr == NULL))
			error_code = ESLURM_JOB_NOT_PENDING;
		else if (job_specs->features[0] != '\0') {
			char *old_features = detail_ptr->features;
			List old_list = detail_ptr->feature_list;
			detail_ptr->features = xstrdup(job_specs->features);
			detail_ptr->feature_list = NULL;
			if (build_feature_list(job_ptr)) {
				info("sched: update_job: invalid features"
				     "(%s) for job_id %u",
				     job_specs->features, job_ptr->job_id);
				FREE_NULL_LIST(detail_ptr->feature_list);
				detail_ptr->features = old_features;
				detail_ptr->feature_list = old_list;
				error_code = ESLURM_INVALID_FEATURE;
			} else {
				info("sched: update_job: setting features to "
				     "%s for job_id %u",
				     job_specs->features, job_ptr->job_id);
				xfree(old_features);
				FREE_NULL_LIST(old_list);
			}
		} else {
			info("sched: update_job: cleared features for job %u",
			     job_ptr->job_id);
			xfree(detail_ptr->features);
			FREE_NULL_LIST(detail_ptr->feature_list);
		}
	}
	if (error_code != SLURM_SUCCESS)
		goto fini;

	if (job_specs->cluster_features &&
	    (error_code = fed_mgr_update_job_cluster_features(
					job_ptr, job_specs->cluster_features)))
		goto fini;

	if (job_specs->clusters &&
	    (error_code = fed_mgr_update_job_clusters(job_ptr,
						     job_specs->clusters)))
		goto fini;

	if (gres_list) {
		info("sched: update_job: setting gres to %s for job_id %u",
		     job_specs->gres, job_ptr->job_id);

		xfree(job_ptr->gres);
		job_ptr->gres = job_specs->gres;
		job_specs->gres = NULL;

		FREE_NULL_LIST(job_ptr->gres_list);
		job_ptr->gres_list = gres_list;
		gres_build_job_details(job_ptr->gres_list,
				       &job_ptr->gres_detail_cnt,
				       &job_ptr->gres_detail_str);
		gres_list = NULL;
	}

	if (job_specs->name) {
		if (IS_JOB_FINISHED(job_ptr)) {
			error_code = ESLURM_JOB_FINISHED;
			goto fini;
		} else if (!xstrcmp(job_specs->name, job_ptr->name)) {
			debug("sched: update_job: new name identical to "
			      "old name %u", job_ptr->job_id);
		} else {
			xfree(job_ptr->name);
			job_ptr->name = xstrdup(job_specs->name);

			info("sched: update_job: setting name to %s for "
			     "job_id %u", job_ptr->name, job_ptr->job_id);
			update_accounting = true;
		}
	}

	if (job_specs->std_out && detail_ptr &&
	    !xstrcmp(job_specs->std_out, detail_ptr->std_out)) {
		debug("sched: update_job: new std_out identical to old std_out %s",
		      job_specs->std_out);
	} else if (job_specs->std_out) {
		if (!IS_JOB_PENDING(job_ptr))
			error_code = ESLURM_JOB_NOT_PENDING;
		else if (detail_ptr) {
			xfree(detail_ptr->std_out);
			detail_ptr->std_out = xstrdup(job_specs->std_out);
		}
	}
	if (error_code != SLURM_SUCCESS)
		goto fini;

	if (job_specs->wckey
	    && !xstrcmp(job_specs->wckey, job_ptr->wckey)) {
		debug("sched: update_job: new wckey identical to "
		      "old wckey %u", job_ptr->job_id);
	} else if (job_specs->wckey) {
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
				(void) gs_job_fini(job_ptr);
				(void) gs_job_start(expand_job_ptr);
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
			(void) gs_job_start(job_ptr);
			job_post_resize_acctg(job_ptr);
			info("sched: update_job: set nodes to %s for "
			     "job_id %u",
			     job_ptr->nodes, job_ptr->job_id);
			/* Since job_post_resize_acctg will restart
			 * things don't do it again. */
			update_accounting = false;
		}
		gres_build_job_details(job_ptr->gres_list,
				       &job_ptr->gres_detail_cnt,
				       &job_ptr->gres_detail_str);
	}

	if (job_specs->array_inx && job_ptr->array_recs) {
		int throttle;
		throttle = strtoll(job_specs->array_inx, (char **) NULL, 10);
		if (throttle >= 0) {
			info("update_job: set max_run_tasks to %d for "
			     "job array %u", throttle, job_ptr->job_id);
			job_ptr->array_recs->max_run_tasks = throttle;
		} else {
			info("update_job: invalid max_run_tasks of %d for "
			     "job array %u, ignored",
			     throttle, job_ptr->job_id);
			error_code = ESLURM_BAD_TASK_COUNT;
		}
	}

	if (job_specs->ntasks_per_node != NO_VAL16) {
		if ((!IS_JOB_PENDING(job_ptr)) || (detail_ptr == NULL))
			error_code = ESLURM_JOB_NOT_PENDING;
		else if (operator) {
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
		if (IS_JOB_PENDING(job_ptr)) {
			FREE_NULL_LIST(job_ptr->license_list);
			job_ptr->license_list = license_list;
			license_list = NULL;
			info("sched: update_job: changing licenses from '%s' "
			     "to '%s' for pending job %u",
			     job_ptr->licenses, job_specs->licenses,
			     job_ptr->job_id);
			xfree(job_ptr->licenses);
			job_ptr->licenses = xstrdup(job_specs->licenses);
		} else if (IS_JOB_RUNNING(job_ptr) &&
			   (operator || (license_list == NULL))) {
			/* NOTE: This can result in oversubscription of
			 * licenses */
			license_job_return(job_ptr);
			FREE_NULL_LIST(job_ptr->license_list);
			job_ptr->license_list = license_list;
			license_list = NULL;
			info("sched: update_job: changing licenses from '%s' "
			     "to '%s' for running job %u",
			     job_ptr->licenses, job_specs->licenses,
			     job_ptr->job_id);
			xfree(job_ptr->licenses);
			job_ptr->licenses = xstrdup(job_specs->licenses);
			license_job_get(job_ptr);
		} else {
			/* licenses are valid, but job state or user not
			 * allowed to make changes */
			info("sched: update_job: could not change licenses "
			     "for job %u", job_ptr->job_id);
			error_code = ESLURM_JOB_NOT_PENDING_NOR_RUNNING;
			FREE_NULL_LIST(license_list);
		}

		update_accounting = 1;
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
		   && job_ptr->state_reason != WAIT_MAX_REQUEUE) {
		job_ptr->state_reason = WAIT_NO_REASON;
	}

#ifdef HAVE_BG
	select_g_select_jobinfo_get(job_specs->select_jobinfo,
				    SELECT_JOBDATA_CONN_TYPE, &conn_type);
	if (conn_type[0] != NO_VAL16) {
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
	if (rotate != NO_VAL16) {
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
	if (reboot != NO_VAL16) {
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
	if (geometry[0] != NO_VAL16) {
		if (!IS_JOB_PENDING(job_ptr))
			error_code = ESLURM_JOB_NOT_PENDING;
		else if (operator) {
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
	if (job_specs->reboot != NO_VAL16) {
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

	if (job_specs->network && !xstrcmp(job_specs->network,
					   job_ptr->network)) {
		debug("sched: update_job: new network identical to old network %s",
		      job_ptr->network);
	} else if (job_specs->network) {
		xfree(job_ptr->network);
		if (!strlen(job_specs->network)
		    || !xstrcmp(job_specs->network, "none")) {
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

	if (job_specs->fed_siblings_viable) {
		if (!job_ptr->fed_details) {
			error_code = ESLURM_JOB_NOT_FEDERATED;
			goto fini;
		}

		info("update_job: setting fed_siblings from %"PRIu64" to %"PRIu64" for job_id %u",
		     job_ptr->fed_details->siblings_viable,
		     job_specs->fed_siblings_viable,
		     job_ptr->job_id);

		job_ptr->fed_details->siblings_viable =
			job_specs->fed_siblings_viable;
		update_job_fed_details(job_ptr);
	}

fini:
	if (error_code == SLURM_SUCCESS) {
		for (tres_pos = 0; tres_pos < slurmctld_tres_cnt; tres_pos++) {
			if (!tres_req_cnt[tres_pos] ||
			    (tres_req_cnt[tres_pos] ==
			     job_ptr->tres_req_cnt[tres_pos]))
				continue;

			job_ptr->tres_req_cnt[tres_pos] =
				tres_req_cnt[tres_pos];
			tres_changed = true;
		}
		if (tres_changed) {
			set_job_tres_req_str(job_ptr, false);
			update_accounting = true;
		}
	}

	/* This was a local variable, so set it back to NULL */
	job_specs->tres_req_cnt = NULL;

	FREE_NULL_LIST(gres_list);
	FREE_NULL_LIST(license_list);
	if (update_accounting) {
		info("updating accounting");
		/* Update job record in accounting to reflect changes */
		jobacct_storage_job_start_direct(acct_db_conn, job_ptr);
	}

	/*
	 * If job isn't held recalculate the priority when not using
	 * priority/basic. Since many factors of an update may affect priority
	 * considerations. Do this whether or not the update was successful or
	 * not.
	 */
	if ((job_ptr->priority != 0) &&
	    xstrcmp(slurmctld_conf.priority_type, "priority/basic"))
		set_job_prio(job_ptr);

	if ((error_code == SLURM_SUCCESS) &&
	    fed_mgr_fed_rec &&
	    job_ptr->fed_details && fed_mgr_is_origin_job(job_ptr)) {
		/* Send updates to sibling jobs */
		/* Add the siblings_active to be updated. They could have been
		 * updated if the job's ClusterFeatures were updated. */
		job_specs->fed_siblings_viable =
			job_ptr->fed_details->siblings_viable;
		fed_mgr_update_job(job_ptr->job_id, job_specs,
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
	job_desc_msg_t *job_specs = (job_desc_msg_t *) msg->data;
	struct job_record *job_ptr;
	int rc;

	xfree(job_specs->job_id_str);
	xstrfmtcat(job_specs->job_id_str, "%u", job_specs->job_id);

	job_ptr = find_job_record(job_specs->job_id);
	if (job_ptr == NULL) {
		info("%s: job id %u does not exist",
		     __func__, job_specs->job_id);
		rc = ESLURM_INVALID_JOB_ID;
	} else {
		rc = _update_job(job_ptr, job_specs, uid);
	}
	if (send_msg && rc != ESLURM_JOB_SETTING_DB_INX)
		slurm_send_rc_msg(msg, rc);
	xfree(job_specs->job_id_str);

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
	struct job_record *job_ptr, *new_job_ptr, *pack_job;
	ListIterator iter;
	long int long_id;
	uint32_t job_id = 0, pack_offset;
	bitstr_t *array_bitmap = NULL, *tmp_bitmap;
	bool valid = true;
	int32_t i, i_first, i_last;
	int len, rc = SLURM_SUCCESS, rc2;
	char *end_ptr, *tok, *tmp = NULL;
	char *job_id_str;
	resp_array_struct_t *resp_array = NULL;
	job_array_resp_msg_t *resp_array_msg = NULL;
	return_code_msg_t rc_msg;

	job_id_str = job_specs->job_id_str;

	if (max_array_size == NO_VAL)
		max_array_size = slurmctld_conf.max_array_sz;

	long_id = strtol(job_id_str, &end_ptr, 10);
	if ((long_id <= 0) || (long_id == LONG_MAX) ||
	    ((end_ptr[0] != '\0') && (end_ptr[0] != '_') &&
	     (end_ptr[0] != '+'))) {
		info("%s: invalid job id %s", __func__, job_id_str);
		rc = ESLURM_INVALID_JOB_ID;
		goto reply;
	}
	job_id = (uint32_t) long_id;
	if (end_ptr[0] == '\0') {	/* Single job (or full job array) */
		struct job_record *job_ptr_done = NULL;
		job_ptr = find_job_record(job_id);
		if (job_ptr && job_ptr->pack_job_list) {
			iter = list_iterator_create(job_ptr->pack_job_list);
			while ((pack_job = list_next(iter))) {
				if (job_ptr->pack_job_id !=
				    pack_job->pack_job_id) {
					error("%s: Bad pack_job_list for job %u",
					      __func__, job_ptr->pack_job_id);
					continue;
				}
				rc = _update_job(pack_job, job_specs, uid);
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
			rc = _update_job(job_ptr, job_specs, uid);
			goto reply;
		}

		if (job_ptr && job_ptr->array_recs) {
			/* This is a job array */
			job_ptr_done = job_ptr;
			rc2 = _update_job(job_ptr, job_specs, uid);
			if (rc2 == ESLURM_JOB_SETTING_DB_INX) {
				rc = rc2;
				goto reply;
			}
			_resp_array_add(&resp_array, job_ptr, rc2);
		}

		/* Update all tasks of this job array */
		job_ptr = job_array_hash_j[JOB_HASH_INX(job_id)];
		if (!job_ptr && !job_ptr_done) {
			info("%s: invalid job id %u", __func__, job_id);
			rc = ESLURM_INVALID_JOB_ID;
			goto reply;
		}
		while (job_ptr) {
			if ((job_ptr->array_job_id == job_id) &&
			    (job_ptr != job_ptr_done)) {
				rc2 = _update_job(job_ptr, job_specs, uid);
				if (rc2 == ESLURM_JOB_SETTING_DB_INX) {
					rc = rc2;
					goto reply;
				}
				_resp_array_add(&resp_array, job_ptr, rc2);
			}
			job_ptr = job_ptr->job_array_next_j;
		}
		goto reply;
	} else if (end_ptr[0] == '+') {	/* Pack job element */
		long_id = strtol(end_ptr+1, &tmp, 10);
		if ((long_id < 0) || (long_id == LONG_MAX) ||
		    (tmp[0] != '\0')) {
			info("%s: invalid job id %s", __func__, job_id_str);
			rc = ESLURM_INVALID_JOB_ID;
			goto reply;
		}
		pack_offset = (uint32_t) long_id;
		job_ptr = find_job_pack_record(job_id, pack_offset);
		if (!job_ptr) {
			info("%s: invalid job id %u", __func__, job_id);
			rc = ESLURM_INVALID_JOB_ID;
			goto reply;
		}
		rc = _update_job(job_ptr, job_specs, uid);
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
		info("%s: invalid job id %s", __func__, job_id_str);
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
			if (rc2 == ESLURM_JOB_SETTING_DB_INX) {
				rc = rc2;
				goto reply;
			}
			_resp_array_add(&resp_array, job_ptr, rc2);
			bit_and_not(array_bitmap,
				    job_ptr->array_recs->task_id_bitmap);
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
				new_job_ptr = job_array_split(job_ptr);
				if (!new_job_ptr) {
					error("%s: Unable to copy record for job %u",
					      __func__, job_ptr->job_id);
				} else {
					/* The array_recs structure is moved
					 * to the new job record copy */
					bb_g_job_validate2(job_ptr, NULL);
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
			info("%s: invalid job id %u_%d", __func__, job_id, i);
			_resp_array_add_id(&resp_array, job_id, i,
					   ESLURM_INVALID_JOB_ID);
			continue;
		}

		rc2 = _update_job(job_ptr, job_specs, uid);
		if (rc2 == ESLURM_JOB_SETTING_DB_INX) {
			rc = rc2;
			goto reply;
		}
		_resp_array_add(&resp_array, job_ptr, rc2);
	}

reply:
	if ((rc != ESLURM_JOB_SETTING_DB_INX) && (msg->conn_fd >= 0)) {
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
		resp_msg.conn = msg->conn;
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
		if (xstrcmp(slurmctld_conf.select_type, "select/serial"))
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

	if ((!job_ptr->db_index || job_ptr->db_index == NO_VAL64)
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
	/* job_set_alloc_tres has to be done
	 * before acct_policy_job_begin */
	job_set_alloc_tres(job_ptr, false);
	acct_policy_job_begin(job_ptr);
	job_claim_resv(job_ptr);

	if (job_ptr->resize_time)
		job_ptr->details->submit_time = job_ptr->resize_time;

	job_ptr->resize_time = time(NULL);

	/* FIXME: see if this can be changed to job_start_direct() */
	jobacct_storage_g_job_start(acct_db_conn, job_ptr);

	job_ptr->details->submit_time = org_submit;
	job_ptr->job_state &= (~JOB_RESIZING);

	/* Reset the end_time_exp that was probably set to NO_VAL when
	 * ending the job on the resize.  If using the
	 * priority/multifactor plugin if the end_time_exp is NO_VAL
	 * it will not run again for the job.
	 */
	job_ptr->end_time_exp = job_ptr->end_time;
}

static char *_build_step_id(char *buf, int buf_len,
			    uint32_t job_id, uint32_t step_id)
{
	if (step_id == SLURM_BATCH_SCRIPT)
		snprintf(buf, buf_len, "%u.batch", job_id);
	else
		snprintf(buf, buf_len, "%u.%u", job_id, step_id);
	return buf;
}

/*
 * validate_jobs_on_node - validate that any jobs that should be on the node
 *	are actually running, if not clean up the job records and/or node
 *	records.
 * IN reg_msg - node registration message
 */
extern void
validate_jobs_on_node(slurm_node_registration_status_msg_t *reg_msg)
{
	int i, node_inx, jobs_on_node;
	struct node_record *node_ptr;
	struct job_record *job_ptr;
	struct step_record *step_ptr;
	char step_str[64];
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
	if (IS_NODE_POWER_UP(node_ptr) &&
	    (node_ptr->boot_time < node_ptr->boot_req_time)) {
		debug("Still waiting for boot of node %s", node_ptr->name);
		return;
	}

	node_inx = node_ptr - node_record_table_ptr;

	/* Check that jobs running are really supposed to be there */
	for (i = 0; i < reg_msg->job_count; i++) {
		if ( (reg_msg->job_id[i] >= MIN_NOALLOC_JOBID) &&
		     (reg_msg->job_id[i] <= MAX_NOALLOC_JOBID) ) {
			info("NoAllocate job %s reported on node %s",
			     _build_step_id(step_str, sizeof(step_str),
					    reg_msg->job_id[i],
					    reg_msg->step_id[i]),
			     reg_msg->node_name);
			continue;
		}

		job_ptr = find_job_record(reg_msg->job_id[i]);
		if (job_ptr == NULL) {
			error("Orphan job %s reported on node %s",
			      _build_step_id(step_str, sizeof(step_str),
					     reg_msg->job_id[i],
					     reg_msg->step_id[i]),
			      reg_msg->node_name);
			abort_job_on_node(reg_msg->job_id[i],
					  job_ptr, node_ptr->name);
		}

		else if (IS_JOB_RUNNING(job_ptr) ||
			 IS_JOB_SUSPENDED(job_ptr)) {
			if (bit_test(job_ptr->node_bitmap, node_inx)) {
				debug3("Registered job %s on node %s ",
				       _build_step_id(step_str,
						      sizeof(step_str),
						      reg_msg->job_id[i],
						      reg_msg->step_id[i]),
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
				error("Registered job %s on wrong node %s ",
				       _build_step_id(step_str,
						      sizeof(step_str),
						      reg_msg->job_id[i],
						      reg_msg->step_id[i]),
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
			error("Registered PENDING job %s on node %s ",
			      _build_step_id(step_str, sizeof(step_str),
					     reg_msg->job_id[i],
					     reg_msg->step_id[i]),
			      reg_msg->node_name);
			abort_job_on_node(reg_msg->job_id[i],
					  job_ptr, node_ptr->name);
		}

		else if (difftime(now, job_ptr->end_time) <
			 slurm_get_msg_timeout()) {	/* Race condition */
			debug("Registered newly completed job %s on %s",
			      _build_step_id(step_str, sizeof(step_str),
					     reg_msg->job_id[i],
					     reg_msg->step_id[i]),
			      node_ptr->name);
		}

		else {		/* else job is supposed to be done */
			error("Registered job %s in state %s on node %s ",
			      _build_step_id(step_str, sizeof(step_str),
					     reg_msg->job_id[i],
					     reg_msg->step_id[i]),
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
	batch_startup_time -= MIN(DEFAULT_MSG_TIMEOUT, msg_timeout);

	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		if ((IS_JOB_CONFIGURING(job_ptr) ||
		    (!IS_JOB_RUNNING(job_ptr) && !IS_JOB_SUSPENDED(job_ptr))) ||
		    (!bit_test(job_ptr->node_bitmap, node_inx)))
			continue;
		if ((job_ptr->batch_flag != 0)			&&
		    (suspend_time != 0) /* power mgmt on */	&&
		    (job_ptr->start_time < node_boot_time)) {
			startup_time = batch_startup_time - resume_timeout;
		} else
			startup_time = batch_startup_time;

		if ((job_ptr->batch_flag != 0)			&&
		    (job_ptr->pack_job_offset == 0)		&&
		    (job_ptr->time_last_active < startup_time)	&&
		    (job_ptr->start_time       < startup_time)	&&
		    (node_inx == bit_ffs(job_ptr->node_bitmap))) {
			bool requeue = false;
			char *requeue_msg = "";
			if (job_ptr->details && job_ptr->details->requeue) {
				requeue = true;
				requeue_msg = ", Requeuing job";
			}
			info("Batch JobId=%u missing from node 0 (not found "
			     "BatchStartTime after startup)%s",
			     job_ptr->job_id, requeue_msg);
			job_ptr->exit_code = 1;
			job_complete(job_ptr->job_id,
				     slurmctld_conf.slurm_user_id,
				     requeue, true, NO_VAL);
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
		if ((step_ptr->step_id == SLURM_EXTERN_CONT) ||
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
	kill_req->select_jobinfo =
			select_g_select_jobinfo_copy(job_ptr->select_jobinfo);
	kill_req->job_state	= job_ptr->job_state;
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
 * Return true if this job is complete (including all elements of a pack job
 */
static bool _job_all_finished(struct job_record *job_ptr)
{
	struct job_record *pack_job;
	ListIterator iter;
	bool finished = true;

	if (!IS_JOB_FINISHED(job_ptr))
		return false;

	if (!job_ptr->pack_job_list)
		return true;

	iter = list_iterator_create(job_ptr->pack_job_list);
	while ((pack_job = (struct job_record *) list_next(iter))) {
		if (!IS_JOB_FINISHED(pack_job)) {
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
extern int job_alloc_info_ptr(uint32_t uid, struct job_record *job_ptr)
{
	uint8_t prolog = 0;

	if ((slurmctld_conf.private_data & PRIVATE_DATA_JOBS) &&
	    (job_ptr->user_id != uid) && !validate_operator(uid) &&
	    (((slurm_mcs_get_privatedata() == 0) &&
	      !assoc_mgr_is_user_acct_coord(acct_db_conn, uid,
					    job_ptr->account)) ||
	     ((slurm_mcs_get_privatedata() == 1) &&
	      (mcs_g_check_mcs_label(uid, job_ptr->mcs_label) != 0))))
		return ESLURM_ACCESS_DENIED;
	if (IS_JOB_PENDING(job_ptr))
		return ESLURM_JOB_PENDING;
	if (_job_all_finished(job_ptr))
		return ESLURM_ALREADY_DONE;
	if (job_ptr->details)
		prolog = job_ptr->details->prolog_running;

	if (job_ptr->alias_list && !xstrcmp(job_ptr->alias_list, "TBD") &&
	    (prolog == 0) && job_ptr->node_bitmap &&
	    (bit_overlap(power_node_bitmap, job_ptr->node_bitmap) == 0)) {
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
			  struct job_record **job_pptr)
{
	struct job_record *job_ptr;

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
 * NOTE: READ lock_slurmctld config before entry
 * NOTE: WRITE lock_slurmctld jobs before entry
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
	FREE_NULL_LIST(batch_dirs);
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
		if (!xstrncmp("hash.#", dir_ent->d_name, 5)) {
			char *h_path = NULL;
			xstrfmtcat(h_path, "%s/%s",
				   slurmctld_conf.state_save_location,
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

static int _clear_state_dir_flag(void *x, void *arg)
{
	struct job_record *job_ptr = (struct job_record *)x;
	job_ptr->bit_flags &= ~HAS_STATE_DIR;
	return 0;
}

static int _test_state_dir_flag(void *x, void *arg)
{
	struct job_record *job_ptr = (struct job_record *)x;

	if (job_ptr->bit_flags & HAS_STATE_DIR) {
		job_ptr->bit_flags &= ~HAS_STATE_DIR;
		return 0;
	}

	if (!job_ptr->batch_flag || !IS_JOB_PENDING(job_ptr) ||
	    (job_ptr->pack_job_offset > 0))
		return 0;	/* No files expected */

	error("Script for job %u lost, state set to FAILED", job_ptr->job_id);
	job_ptr->job_state = JOB_FAILED;
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
static void _validate_job_files(List batch_dirs)
{
	struct job_record *job_ptr;
	ListIterator batch_dir_iter;
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
		info("Purged files for defunct batch job %u",
		     *job_id_ptr);
		delete_job_desc_files(*job_id_ptr);
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

extern uint64_t job_get_tres_mem(uint64_t pn_min_memory,
				 uint32_t cpu_cnt, uint32_t node_cnt)
{
	uint64_t count = 0;

	if (pn_min_memory == NO_VAL64)
		return count;

	if (pn_min_memory & MEM_PER_CPU) {
		if (cpu_cnt != NO_VAL) {
			count = pn_min_memory & (~MEM_PER_CPU);
			count *= cpu_cnt;
		}
	} else if (node_cnt != NO_VAL)
		count = pn_min_memory * node_cnt;

	return count;
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
			      jobid2str(job_ptr, jbuf,
					sizeof(jbuf)), node_name);
		} else if (job_ptr->restart_cnt) {
			/* Duplicate epilog complete can be due to race
			 * condition, especially with select/serial */
			debug("%s: %s duplicate epilog complete response",
			      __func__, jobid2str(job_ptr, jbuf, sizeof(jbuf)));
		} else {

			error("%s: %s is non-running slurmctld"
			      "and slurmd out of sync",
			      __func__, jobid2str(job_ptr, jbuf, sizeof(jbuf)));
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
		      __func__, jobid2str(job_ptr, jbuf, sizeof(jbuf)),
		      job_ptr->batch_host);

	if (job_ptr->front_end_ptr && IS_JOB_COMPLETING(job_ptr)) {
		front_end_record_t *front_end_ptr = job_ptr->front_end_ptr;
		if (front_end_ptr->job_cnt_comp)
			front_end_ptr->job_cnt_comp--;
		else {
			error("%s: %s job_cnt_comp underflow on "
			      "front end %s", __func__,
			      jobid2str(job_ptr, jbuf, sizeof(jbuf)),
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
				drain_nodes(node_ptr->name, "Epilog error",
					    slurmctld_conf.slurm_user_id);
			}
#endif
			/* Change job from completing to completed */
			make_node_idle(node_ptr, job_ptr);
		}
	}
#else
	if (return_code) {
		error("%s: %s epilog error on %s, draining the node",
		      __func__, jobid2str(job_ptr, jbuf, sizeof(jbuf)),
		      node_name);
		drain_nodes(node_name, "Epilog error",
			    slurmctld_conf.slurm_user_id);
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
	char jbuf[JBUFSIZ];

	if (IS_JOB_COMPLETING(job_ptr) ||
	    !IS_JOB_PENDING(job_ptr) || !job_ptr->batch_flag)
		return;

	info("Requeuing %s", jobid2str(job_ptr, jbuf, sizeof(jbuf)));

	/* Clear everything so this appears to be a new job and then restart
	 * it in accounting. */
	job_ptr->start_time = 0;
	job_ptr->end_time_exp = job_ptr->end_time = 0;
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
		/* The time stamp on the new batch launch credential must be
		 * larger than the time stamp on the revoke request. Also the
		 * I/O must be all cleared out, the named socket purged and
		 * the job credential purged by slurmd. */
		if (job_ptr->details->begin_time <= now) {
			/* See src/common/slurm_cred.c
			 * #define DEFAULT_EXPIRATION_WINDOW 1200 */
			int cred_lifetime = 1200;
			(void) slurm_cred_ctx_get(slurmctld_config.cred_ctx,
						  SLURM_CRED_OPT_EXPIRY_WINDOW,
						  &cred_lifetime);
			job_ptr->details->begin_time = now + cred_lifetime + 1;
		}

		/* Since this could happen on a launch we need to make sure the
		 * submit isn't the same as the last submit so put now + 1 so
		 * we get different records in the database */
		if (now == job_ptr->details->submit_time)
			now++;
		job_ptr->details->submit_time = now;
	}

	/*
	 * If a reservation ended and was a repeated (e.g., daily, weekly)
	 * reservation, its ID will be different; make sure
	 * job->resv_id matches the reservation id.
	 */
	if (job_ptr->resv_ptr)
		job_ptr->resv_id = job_ptr->resv_ptr->resv_id;

	/* Reset this after the batch step has finished or the batch step
	 * information will be attributed to the next run of the job. */
	job_ptr->db_index = 0;
	if (!with_slurmdbd)
		jobacct_storage_g_job_start(acct_db_conn, job_ptr);

	/* Submit new sibling jobs for fed jobs */
	if (fed_mgr_is_origin_job(job_ptr)) {
		if (fed_mgr_job_requeue(job_ptr)) {
			error("failed to submit requeued sibling jobs for fed job %d",
			      job_ptr->job_id);
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
	FREE_NULL_LIST(purge_files_list);
	FREE_NULL_BITMAP(requeue_exit);
	FREE_NULL_BITMAP(requeue_exit_hold);
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

	return true;
}

static void _job_array_comp(struct job_record *job_ptr, bool was_running,
			    bool requeue)
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
			if (was_running &&
			    base_job_ptr->array_recs->tot_run_tasks)
				base_job_ptr->array_recs->tot_run_tasks--;
			base_job_ptr->array_recs->tot_comp_tasks++;

			if (requeue) {
				base_job_ptr->array_recs->array_flags |=
					ARRAY_TASK_REQUEUED;
			}
		}
	}
}

/* log the completion of the specified job */
extern void job_completion_logger(struct job_record *job_ptr, bool requeue)
{
	int base_state;
	bool arr_finished = false, task_failed = false, task_requeued = false;
	struct job_record *master_job = NULL;
	uint32_t max_exit_code = 0;

	xassert(job_ptr);

	acct_policy_remove_job_submit(job_ptr);
	if (job_ptr->nodes && ((job_ptr->bit_flags & JOB_KILL_HURRY) == 0)
	    && !IS_JOB_RESIZING(job_ptr)) {
		(void) bb_g_job_start_stage_out(job_ptr);
	} else if (job_ptr->nodes && IS_JOB_RESIZING(job_ptr)){
		char jbuf[JBUFSIZ];
		debug("%s: %s resizing, skipping bb stage_out",
		      __func__, jobid2str(job_ptr, jbuf, sizeof(jbuf)));
	} else {
		/*
		 * Never allocated compute nodes.
		 * Unless job ran, there is no data to stage-out
		 */
		(void) bb_g_job_cancel(job_ptr);
	}

	_job_array_comp(job_ptr, true, requeue);

	if (!IS_JOB_RESIZING(job_ptr) &&
	    !IS_JOB_PENDING(job_ptr)  &&
	    ((job_ptr->array_task_id == NO_VAL) ||
	     (job_ptr->mail_type & MAIL_ARRAY_TASKS) ||
	     (arr_finished = test_job_array_finished(job_ptr->array_job_id)))) {
		/* Remove configuring state just to make sure it isn't there
		 * since it will throw off displays of the job. */
		job_ptr->job_state &= ~JOB_CONFIGURING;

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
			if ((base_state == JOB_COMPLETE) ||
			    (base_state == JOB_CANCELLED)) {
				if (requeue &&
				    (job_ptr->mail_type & MAIL_JOB_REQUEUE)) {
					mail_job_info(job_ptr,
						      MAIL_JOB_REQUEUE);
				} else if (job_ptr->mail_type & MAIL_JOB_END) {
					mail_job_info(job_ptr, MAIL_JOB_END);
				}
			} else {	/* JOB_FAILED, JOB_TIMEOUT, etc. */
				if (job_ptr->mail_type & MAIL_JOB_FAIL)
					mail_job_info(job_ptr, MAIL_JOB_FAIL);
				else if (job_ptr->mail_type & MAIL_JOB_END)
					mail_job_info(job_ptr, MAIL_JOB_END);
			}
		}
	}

	g_slurm_jobcomp_write(job_ptr);

	/* When starting the resized job everything is taken care of
	 * elsewhere, so don't call it here. */
	if (IS_JOB_RESIZING(job_ptr))
		return;

	if (!job_ptr->assoc_id) {
		slurmdb_assoc_rec_t assoc_rec;
		/* In case accounting enabled after starting the job */
		memset(&assoc_rec, 0, sizeof(slurmdb_assoc_rec_t));
		assoc_rec.acct      = job_ptr->account;
		if (job_ptr->part_ptr)
			assoc_rec.partition = job_ptr->part_ptr->name;
		assoc_rec.uid       = job_ptr->user_id;

		if (!(assoc_mgr_fill_in_assoc(acct_db_conn, &assoc_rec,
					      accounting_enforce,
					      &job_ptr->assoc_ptr, false))) {
			job_ptr->assoc_id = assoc_rec.id;
			/* we have to call job start again because the
			 * associd does not get updated in job complete */
			jobacct_storage_g_job_start(acct_db_conn, job_ptr);
		}
	}

	if (!with_slurmdbd && !job_ptr->db_index)
		jobacct_storage_g_job_start(acct_db_conn, job_ptr);

	if (!(job_ptr->bit_flags & TRES_STR_CALC) &&
	    job_ptr->tres_alloc_cnt &&
	    (job_ptr->tres_alloc_cnt[TRES_ENERGY] != NO_VAL64))
		set_job_tres_alloc_str(job_ptr, false);

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

	if (job_ptr->state_reason == FAIL_BURST_BUFFER_OP
	    || job_ptr->state_reason == WAIT_HELD
	    || job_ptr->state_reason == WAIT_HELD_USER
	    || job_ptr->state_reason == WAIT_MAX_REQUEUE
	    || job_ptr->state_reason == WAIT_RESV_DELETED
	    || job_ptr->state_reason == WAIT_DEP_INVALID)
		return false;

	/* Test dependencies first so we can cancel jobs before dependent
	 * job records get purged (e.g. afterok, afternotok) */
	depend_rc = test_job_dependency(job_ptr);
	if (depend_rc == 1) {
		/* start_time has passed but still has dependency which
		 * makes it ineligible */
		if (detail_ptr->begin_time < now)
			detail_ptr->begin_time = 0;
		job_ptr->state_reason = WAIT_DEPENDENCY;
		xfree(job_ptr->state_desc);
		return false;
	} else if (depend_rc == 2) {
		char jbuf[JBUFSIZ];

		if (job_ptr->bit_flags & KILL_INV_DEP) {
			_kill_dependent(job_ptr);
		} else if (job_ptr->bit_flags & NO_KILL_INV_DEP) {
			debug("%s: %s job dependency never satisfied",
			      __func__, jobid2str(job_ptr, jbuf, sizeof(jbuf)));
			job_ptr->state_reason = WAIT_DEP_INVALID;
			xfree(job_ptr->state_desc);
		} else if (kill_invalid_dep) {
			_kill_dependent(job_ptr);
		} else {
			debug("%s: %s job dependency never satisfied",
			      __func__, jobid2str(job_ptr, jbuf, sizeof(jbuf)));
			job_ptr->state_reason = WAIT_DEP_INVALID;
			xfree(job_ptr->state_desc);
		}
		return false;
	}
	/* Job is eligible to start now */
	if (job_ptr->state_reason == WAIT_DEPENDENCY) {
		job_ptr->state_reason = WAIT_NO_REASON;
		xfree(job_ptr->state_desc);
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
	    job_ptr->alias_list && !xstrcmp(job_ptr->alias_list, "TBD") &&
	    job_ptr->node_bitmap &&
	    (bit_overlap(power_node_bitmap, job_ptr->node_bitmap) == 0)) {
		last_job_update = time(NULL);
		set_job_alias_list(job_ptr);
	}

	*ready = rc;
	return SLURM_SUCCESS;
}

/* Send specified signal to all steps associated with a job */
static void _signal_job(struct job_record *job_ptr, int signal, uint16_t flags)
{
#ifndef HAVE_FRONT_END
	int i;
#endif
	agent_arg_t *agent_args = NULL;
	signal_tasks_msg_t *signal_job_msg = NULL;
	static int notify_srun_static = -1;
	int notify_srun = 0;

	if (notify_srun_static == -1) {
		/* do this for all but slurm (poe, aprun, etc...) */
		if (xstrcmp(slurmctld_conf.launch_type, "launch/slurm"))
			notify_srun_static = 1;
		else
			notify_srun_static = 0;
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
					signal, 0, 0);
		}
		list_iterator_destroy (step_iterator);

		return;
	}

	agent_args = xmalloc(sizeof(agent_arg_t));
	agent_args->msg_type = REQUEST_SIGNAL_TASKS;
	agent_args->retry = 1;
	agent_args->hostlist = hostlist_create(NULL);
	signal_job_msg = xmalloc(sizeof(signal_tasks_msg_t));
	signal_job_msg->job_id = job_ptr->job_id;

	/*
	 * We don't ever want to kill a step with this message.  The flags below
	 * will make sure that does happen.  Just in case though, set the
	 * step_id to an impossible number.
	 */
	signal_job_msg->job_step_id = slurmctld_conf.max_step_cnt + 1;

	/*
	 * Encode the flags for slurm stepd to know what steps get signaled
	 * Here if we aren't signaling the full job we always only want to
	 * signal all other steps.
	 */
	if (flags == KILL_FULL_JOB ||
	    flags == KILL_JOB_BATCH ||
	    flags == KILL_STEPS_ONLY)
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
	agent_args->retry = 0;	/* don't resend, gang scheduler can
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
	if (job_ptr->front_end_ptr) {
		agent_args->protocol_version =
			job_ptr->front_end_ptr->protocol_version;
	}
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
	    (job_ptr->details->core_spec == NO_VAL16) ||
	    (job_ptr->node_bitmap == NULL))
		return rc;

	job_iterator = list_iterator_create(job_list);
	while ((test_job_ptr = (struct job_record *) list_next(job_iterator))) {
		if (test_job_ptr->details &&
		    (test_job_ptr->details->core_spec != NO_VAL16) &&
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
 * _job_suspend_op - perform some suspend/resume operation on a job
 * op IN - operation: suspend/resume
 * indf_susp IN - set if job is being suspended indefinitely by user or admin
 *                and we should clear it's priority, otherwise suspended
 *		  temporarily for gang scheduling
 * RET 0 on success, otherwise ESLURM error code
 */
static int _job_suspend_op(struct job_record *job_ptr, uint16_t op,
			   bool indf_susp)
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
		if (indf_susp) {    /* Job being manually suspended, not gang */
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
		power_g_job_resume(job_ptr);
		if (rc != SLURM_SUCCESS)
			return rc;
		_suspend_job(job_ptr, op, indf_susp);
		if (job_ptr->priority == 0) {
			/* Job was manually suspended, not gang */
			set_job_prio(job_ptr);
			(void) gs_job_start(job_ptr);
		}
		job_ptr->job_state = JOB_RUNNING;
		job_ptr->tot_sus_time +=
			difftime(now, job_ptr->suspend_time);

		if ((job_ptr->time_limit != INFINITE) &&
		    (!job_ptr->preempt_time)) {
			debug3("Job %u resumed, updating end_time",
			       job_ptr->job_id);
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
 *                job records is a pack leader, perform the operation on all
 *                components of the pack job
 * job_ptr - job to operate upon
 * op IN - operation: suspend/resume
 * indf_susp IN - set if job is being suspended indefinitely by user or admin
 *                and we should clear it's priority, otherwise suspended
 *		  temporarily for gang scheduling
 * RET 0 on success, otherwise ESLURM error code
 */
static int _job_suspend(struct job_record *job_ptr, uint16_t op, bool indf_susp)
{
	struct job_record *pack_job;
	int rc = SLURM_SUCCESS, rc1;
	ListIterator iter;

	if (job_ptr->pack_job_id && !job_ptr->pack_job_list)
		return ESLURM_NOT_PACK_WHOLE;

	/* Notify salloc/srun of suspend/resume */
	srun_job_suspend(job_ptr, op);

	if (job_ptr->pack_job_list) {
		iter = list_iterator_create(job_ptr->pack_job_list);
		while ((pack_job = (struct job_record *) list_next(iter))) {
			if (job_ptr->pack_job_id != pack_job->pack_job_id) {
				error("%s: Bad pack_job_list for job %u",
				      __func__, job_ptr->pack_job_id);
				continue;
			}
			rc1 = _job_suspend_op(pack_job, op, indf_susp);
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
		       int conn_fd, bool indf_susp,
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
	if (!validate_operator(uid)) {
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
			int conn_fd, bool indf_susp,
			uint16_t protocol_version)
{
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
		max_array_size = slurmctld_conf.max_array_sz;
	}

	/* validate the request */
	if (!validate_operator(uid)) {
		error("SECURITY VIOLATION: Attempt to suspend job from user %u",
		      (int) uid);
		rc = ESLURM_ACCESS_DENIED;
		goto reply;
	}

	long_id = strtol(sus_ptr->job_id_str, &end_ptr, 10);
	if (end_ptr[0] == '+')
		rc = ESLURM_NOT_PACK_WHOLE;
	else if ((long_id <= 0) || (long_id == LONG_MAX) ||
		 ((end_ptr[0] != '\0') && (end_ptr[0] != '_')))
		rc = ESLURM_INVALID_JOB_ID;
	if (rc != SLURM_SUCCESS) {
		info("%s: invalid job id %s", __func__, sus_ptr->job_id_str);
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
 * _job_requeue_op - Requeue a running or pending batch job
 * IN uid - user id of user issuing the RPC
 * IN job_ptr - job to be requeued
 * IN preempt - true if job being preempted
 * RET 0 on success, otherwise ESLURM error code
 */
static int _job_requeue_op(uid_t uid, struct job_record *job_ptr, bool preempt,
			   uint32_t state)
{
	bool is_running = false, is_suspended = false, is_completed = false;
	bool is_completing = false;
	time_t now = time(NULL);
	uint32_t completing_flags = 0;

	/* validate the request */
	if ((uid != job_ptr->user_id) && !validate_operator(uid) &&
	    !assoc_mgr_is_user_acct_coord(acct_db_conn, uid,
					  job_ptr->account)) {
		return ESLURM_ACCESS_DENIED;
	}

	if (state & JOB_RECONFIG_FAIL)
		node_features_g_get_node(job_ptr->nodes);

	/*
	 * If the partition was removed don't allow the job to be
	 * requeued.  If it doesn't have details then something is very
	 * wrong and if the job doesn't want to be requeued don't.
	 */
	if (!job_ptr->part_ptr || !job_ptr->details
	    || !job_ptr->details->requeue) {
		if (state & JOB_RECONFIG_FAIL)
			(void) _job_fail(job_ptr, JOB_BOOT_FAIL);
		return ESLURM_DISABLED;
	}

	if (job_ptr->batch_flag == 0) {
		debug("Job-requeue can only be done for batch jobs");
		if (state & JOB_RECONFIG_FAIL)
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

	if ((state & JOB_RECONFIG_FAIL) && IS_JOB_CANCELLED(job_ptr)) {
		/*
		 * Job was cancelled (likely be the user) while node
		 * reconfiguration was in progress, so don't requeue it
		 * if the node reconfiguration failed.
		 */
		return ESLURM_DISABLED;
	}

	if (job_ptr->fed_details) {
		int rc;
		if ((rc = fed_mgr_job_requeue_test(job_ptr, state)))
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
		job_ptr->job_state = JOB_REQUEUE;
		jobacct_storage_g_job_suspend(acct_db_conn, job_ptr);
		job_ptr->job_state = suspend_job_state;
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
			job_ptr->job_state = JOB_PREEMPTED;
			build_cg_bitmap(job_ptr);
			job_completion_logger(job_ptr, false);
			job_ptr->job_state = JOB_REQUEUE;
		} else {
			job_ptr->job_state = JOB_REQUEUE;
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
		job_ptr->job_state = JOB_PENDING | completing_flags;
		goto reply;
	}

	/*
	 * Deallocate resources only if the job has some.
	 * JOB_COMPLETING is needed to properly clean up steps.
	 */
	if (is_running) {
		job_ptr->job_state |= JOB_COMPLETING;
		deallocate_nodes(job_ptr, false, is_suspended, preempt);
		job_ptr->job_state &= (~JOB_COMPLETING);
	}

	/* do this after the epilog complete, setting it here is too early */
	//job_ptr->db_index = 0;
	//job_ptr->details->submit_time = now;

	job_ptr->job_state = JOB_PENDING;
	if (job_ptr->node_cnt)
		job_ptr->job_state |= JOB_COMPLETING;

	/*
	 * Mark the origin job as requeueing. Will finish requeueing fed job
	 * after job has completed.
	 * If it's completed, batch_requeue_fini is called below and will call
	 * fed_mgr_job_requeue() to submit new siblings.
	 * If it's not completed, batch_requeue_fini will either be called when
	 * the running origin job finishes or the running remote sibling job
	 * reports that the job is finished.
	 */
	if (job_ptr->fed_details && !is_completed) {
		job_ptr->job_state |= JOB_COMPLETING;
		job_ptr->job_state |= JOB_REQUEUE_FED;
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

	/* clear signal sent flag on requeue */
	job_ptr->warn_flags &= ~WARN_SENT;

	/*
	 * Since the job completion logger removes the submit we need
	 * to add it again.
	 */
	acct_policy_add_job_submit(job_ptr);

	acct_policy_update_pending_job(job_ptr);

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
		if (state & JOB_LAUNCH_FAILED)
			job_ptr->state_desc
				= xstrdup("launch failed requeued held");
		else
			job_ptr->state_desc
				= xstrdup("job requeued in held state");
		job_ptr->priority = 0;
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

	debug("%s: job %u state 0x%x reason %u priority %d", __func__,
	      job_ptr->job_id, job_ptr->job_state,
	      job_ptr->state_reason, job_ptr->priority);

	return SLURM_SUCCESS;
}

/*
 * _job_requeue - Requeue a running or pending batch job, if the specified
 *		  job records is a pack leader, perform the operation on all
 *		  components of the pack job
 * IN uid - user id of user issuing the RPC
 * IN job_ptr - job to be requeued
 * IN preempt - true if job being preempted
 * RET 0 on success, otherwise ESLURM error code
 */
static int _job_requeue(uid_t uid, struct job_record *job_ptr, bool preempt,
			   uint32_t state)
{
	struct job_record *pack_job;
	int rc = SLURM_SUCCESS, rc1;
	ListIterator iter;

	if (job_ptr->pack_job_id && !job_ptr->pack_job_list)
		return ESLURM_NOT_PACK_JOB_LEADER;

	if (job_ptr->pack_job_list) {
		iter = list_iterator_create(job_ptr->pack_job_list);
		while ((pack_job = (struct job_record *) list_next(iter))) {
			if (job_ptr->pack_job_id != pack_job->pack_job_id) {
				error("%s: Bad pack_job_list for job %u",
				      __func__, job_ptr->pack_job_id);
				continue;
			}
			rc1 = _job_requeue_op(uid, pack_job, preempt, state);
			if (rc1 != SLURM_SUCCESS)
				rc = rc1;
		}
		list_iterator_destroy(iter);
	} else {
		rc = _job_requeue_op(uid, job_ptr, preempt, state);
	}

	return rc;
}

/*
 * job_requeue - Requeue a running or pending batch job
 * IN uid - user id of user issuing the RPC
 * IN job_id - id of the job to be requeued
 * IN msg - slurm_msg to send response back on
 * IN preempt - true if job being preempted
 * IN state - may be set to JOB_SPECIAL_EXIT and/or JOB_REQUEUE_HOLD
 * RET 0 on success, otherwise ESLURM error code
 */
extern int job_requeue(uid_t uid, uint32_t job_id, slurm_msg_t *msg,
		       bool preempt, uint32_t state)
{
	int rc = SLURM_SUCCESS;
	struct job_record *job_ptr = NULL;

	/* find the job */
	job_ptr = find_job_record(job_id);
	if (job_ptr == NULL) {
		rc = ESLURM_INVALID_JOB_ID;
	} else {
		rc = _job_requeue(uid, job_ptr, preempt, state);
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
		max_array_size = slurmctld_conf.max_array_sz;
	}

	long_id = strtol(job_id_str, &end_ptr, 10);
	if ((long_id <= 0) || (long_id == LONG_MAX) ||
	    ((end_ptr[0] != '\0') && (end_ptr[0] != '_'))) {
		info("job_requeue2: invalid job id %s", job_id_str);
		rc = ESLURM_INVALID_JOB_ID;
		goto reply;
	}
	if ((end_ptr[0] == '_') && (end_ptr[1] == '*'))
		end_ptr += 2;	/* Defaults to full job array */

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
	if (msg) {
		slurm_msg_t_init(&resp_msg);
		resp_msg.protocol_version = msg->protocol_version;
		resp_msg.conn             = msg->conn;
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

static int _top_job_flag_clear(void *x, void *arg)
{
	struct job_record *job_ptr = (struct job_record *) x;
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

static void _top_job_prio_del(void *x)
{
	xfree(x);
}

static int _set_top(List top_job_list, uid_t uid)
{
	List prio_list, other_job_list;
	ListIterator iter;
	struct job_record *job_ptr, *first_job_ptr = NULL;
	int rc = SLURM_SUCCESS, rc2 = SLURM_SUCCESS;
	uint32_t last_prio = NO_VAL, next_prio;
	int64_t delta_prio, delta_nice, total_delta = 0;
	int other_job_cnt = 0;
	uint32_t *prio_elem;

	xassert(job_list);
	xassert(top_job_list);
	prio_list = list_create(_top_job_prio_del);
	(void) list_for_each(job_list, _top_job_flag_clear, NULL);

	/* Validate the jobs in our "top" list */
	iter = list_iterator_create(top_job_list);
	while ((job_ptr = (struct job_record *) list_next(iter))) {
		if ((job_ptr->user_id != uid) && (uid != 0)) {
			error("Security violation: REQUEST_TOP_JOB for job %u from uid=%d",
			      job_ptr->job_id, uid);
			rc = ESLURM_ACCESS_DENIED;
			break;
		}
		if (!IS_JOB_PENDING(job_ptr) || (job_ptr->details == NULL)) {
			debug("%s: Job %u not pending",
			      __func__, job_ptr->job_id);
			list_remove(iter);
			rc2 = ESLURM_JOB_NOT_PENDING;
			continue;
		}
		if (job_ptr->part_ptr_list) {
			debug("%s: Job %u in partition list",
			      __func__, job_ptr->job_id);
			list_remove(iter);
			rc = ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE;
			break;
		}
		if (job_ptr->priority == 0) {
			debug("%s: Job %u is held",
			      __func__, job_ptr->job_id);
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
	while ((job_ptr = (struct job_record *) list_next(iter))) {
		if ((job_ptr->bit_flags & TOP_PRIO_TMP) ||
		    (job_ptr->details == NULL) ||
		    (job_ptr->part_ptr_list)   ||
		    (job_ptr->priority == 0)   ||
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
	while ((job_ptr = (struct job_record *) list_next(iter))) {
		prio_elem = list_pop(prio_list);
		next_prio = *prio_elem;
		xfree(prio_elem);
		if ((last_prio != NO_VAL) && (next_prio == last_prio))
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
		while ((job_ptr = (struct job_record *) list_next(iter))) {
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
 * IN top_ptr - user request
 * IN uid - user id of the user issuing the RPC
 * IN conn_fd - file descriptor on which to send reply,
 *              -1 if none
 * IN protocol_version - slurm protocol version of client
 * RET 0 on success, otherwise ESLURM error code
 */
extern int job_set_top(top_job_msg_t *top_ptr, uid_t uid, int conn_fd,
		       uint16_t protocol_version)
{
	int rc = SLURM_SUCCESS;
	List top_job_list = NULL;
	char *job_str_tmp = NULL, *tok, *save_ptr = NULL, *end_ptr = NULL;
	struct job_record *job_ptr = NULL;
	long int long_id;
	uint32_t job_id = 0, task_id = 0;
	slurm_msg_t resp_msg;
	return_code_msg_t rc_msg;

	if (validate_operator(uid)) {
		uid = 0;
	} else {
		char *sched_params;
		bool disable_user_top = true;
		sched_params = slurm_get_sched_params();
		if (sched_params && strstr(sched_params, "enable_user_top"))
			disable_user_top = false;
		xfree(sched_params);
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
	rc = _set_top(top_job_list, uid);

reply:	FREE_NULL_LIST(top_job_list);
	xfree(job_str_tmp);
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
 * job_end_time - Process JOB_END_TIME
 * IN time_req_msg - job end time request
 * OUT timeout_msg - job timeout response to be sent
 * RET SLURM_SUCCESS or an error code
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
		{ NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };

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
				job_ptr->assoc_ptr->usage->parent_assoc_ptr;
			if (job_ptr->assoc_ptr)
				job_ptr->assoc_id =
					job_ptr->assoc_ptr->id;
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
		{ NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };

	if (!job_list)
		return cnt;

	lock_slurmctld(job_write_lock);
	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		if (job_ptr->qos_blocking_ptr &&
		    ((slurmdb_qos_rec_t *)job_ptr->qos_blocking_ptr)->id
		    == qos_id)
			job_ptr->qos_blocking_ptr = NULL;
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
	slurmdb_assoc_rec_t assoc_rec;

	if ((!IS_JOB_PENDING(job_ptr)) || (job_ptr->details == NULL)) {
		info("%s: attempt to modify account for non-pending "
		     "job_id %u", module, job_ptr->job_id);
		return ESLURM_JOB_NOT_PENDING;
	}


	memset(&assoc_rec, 0, sizeof(slurmdb_assoc_rec_t));
	assoc_rec.acct      = new_account;
	if (job_ptr->part_ptr)
		assoc_rec.partition = job_ptr->part_ptr->name;
	assoc_rec.uid       = job_ptr->user_id;
	if (assoc_mgr_fill_in_assoc(acct_db_conn, &assoc_rec,
				    accounting_enforce,
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
		(void) assoc_mgr_fill_in_assoc(acct_db_conn, &assoc_rec,
					       accounting_enforce,
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
		NO_LOCK, WRITE_LOCK, READ_LOCK, READ_LOCK, NO_LOCK };

	/* send jobs in pending or running state */
	lock_slurmctld(job_write_lock);
	itr = list_iterator_create(job_list);
	while ((job_ptr = list_next(itr))) {
		if (!job_ptr->assoc_id) {
			slurmdb_assoc_rec_t assoc_rec;
			memset(&assoc_rec, 0,
			       sizeof(slurmdb_assoc_rec_t));
			assoc_rec.acct      = job_ptr->account;
			if (job_ptr->part_ptr)
				assoc_rec.partition = job_ptr->part_ptr->name;
			assoc_rec.uid       = job_ptr->user_id;

			if (assoc_mgr_fill_in_assoc(
				   acct_db_conn, &assoc_rec,
				   accounting_enforce,
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
			  int conn_fd, uint16_t protocol_version)
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
	job_desc = copy_job_record_to_job_desc(job_ptr);
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
 * copy_job_record_to_job_desc - construct a job_desc_msg_t for a job.
 * IN job_ptr - the job record
 * RET the job_desc_msg_t, NULL on error
 */
extern job_desc_msg_t *copy_job_record_to_job_desc(struct job_record *job_ptr)
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
	job_desc->clusters          = xstrdup(job_ptr->clusters);
	job_desc->comment           = xstrdup(job_ptr->comment);
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
	job_desc->features          = xstrdup(details->features);
	job_desc->cluster_features  = xstrdup(details->cluster_features);
	job_desc->gres              = xstrdup(job_ptr->gres);
	job_desc->group_id          = job_ptr->group_id;
	job_desc->immediate         = 0; /* nowhere to get this value */
	job_desc->job_id            = job_ptr->job_id;
	job_desc->kill_on_node_fail = job_ptr->kill_on_node_fail;
	job_desc->licenses          = xstrdup(job_ptr->licenses);
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
	job_desc->power_flags       = job_ptr->power_flags;
	job_desc->overcommit        = details->overcommit;
	job_desc->partition         = xstrdup(job_ptr->partition);
	job_desc->plane_size        = details->plane_size;
	job_desc->priority          = job_ptr->priority;
	if (job_ptr->qos_ptr)
		job_desc->qos       = xstrdup(job_ptr->qos_ptr->name);
	job_desc->resp_host         = xstrdup(job_ptr->resp_host);
	job_desc->req_nodes         = xstrdup(details->req_nodes);
	job_desc->requeue           = details->requeue;
	job_desc->reservation       = xstrdup(job_ptr->resv_name);
	job_desc->restart_cnt       = job_ptr->restart_cnt;
	job_desc->script            = get_job_script(job_ptr);
	if (details->share_res == 1)
		job_desc->shared     = JOB_SHARED_OK;
	else if (details->whole_node == WHOLE_NODE_REQUIRED)
		job_desc->shared     =  JOB_SHARED_NONE;
	else if (details->whole_node == WHOLE_NODE_USER)
		job_desc->shared     =  JOB_SHARED_USER;
	else if (details->whole_node == WHOLE_NODE_MCS)
		job_desc->shared     =  JOB_SHARED_MCS;
	else
		job_desc->shared     = NO_VAL16;
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

	if (job_ptr->fed_details) {
		job_desc->fed_siblings_active =
			job_ptr->fed_details->siblings_active;
		job_desc->fed_siblings_viable =
			job_ptr->fed_details->siblings_viable;
	}

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
extern int job_restart(checkpoint_msg_t *ckpt_ptr, uid_t uid, int conn_fd,
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
	uint16_t ckpt_version = NO_VAL16;

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
	if (ver_str && !xstrcmp(ver_str, JOB_CKPT_VERSION))
		safe_unpack16(&ckpt_version, buffer);

	if (ckpt_version == NO_VAL16) {
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
	job_desc->pack_job_offset = NO_VAL;
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
 *
 * RET returns true if the job was requeued
 */
extern bool job_hold_requeue(struct job_record *job_ptr)
{
	uint32_t state;
	uint32_t flags;

	xassert(job_ptr);

	/* If the job is already pending it was
	 * eventually requeued somewhere else.
	 */
	if (IS_JOB_PENDING(job_ptr) && !IS_JOB_REVOKED(job_ptr))
		return false;

	/* If the job is not on the origin cluster, then don't worry about
	 * requeueing the job here. The exit code will be sent the origin
	 * cluster and the origin cluster will decide if the job should be
	 * requeued or not. */
	if (!fed_mgr_is_origin_job(job_ptr))
		return false;

	/* Check if the job exit with one of the
	 * configured requeue values. */
	_set_job_requeue_exit_value(job_ptr);

	state = job_ptr->job_state;

	if (! (state & JOB_REQUEUE))
		return false;

	/* Sent event requeue to the database.  */
	if (!(job_ptr->bit_flags & TRES_STR_CALC) &&
	    job_ptr->tres_alloc_cnt &&
	    (job_ptr->tres_alloc_cnt[TRES_ENERGY] != NO_VAL64))
		set_job_tres_alloc_str(job_ptr, false);
	jobacct_storage_g_job_complete(acct_db_conn, job_ptr);

	debug("%s: job %u state 0x%x", __func__, job_ptr->job_id, state);

	/* Set the job pending */
	flags = job_ptr->job_state & JOB_STATE_FLAGS;
	job_ptr->job_state = JOB_PENDING | flags;

	job_ptr->restart_cnt++;

	/* clear signal sent flag on requeue */
	job_ptr->warn_flags &= ~WARN_SENT;

	/* Test if user wants to requeue the job
	 * in hold or with a special exit value.  */
	if (state & JOB_SPECIAL_EXIT) {
		/* JOB_SPECIAL_EXIT means requeue the
		 * the job, put it on hold and display
		 * it as JOB_SPECIAL_EXIT.  */
		job_ptr->job_state |= JOB_SPECIAL_EXIT;
		job_ptr->state_reason = WAIT_HELD_USER;
		job_ptr->priority = 0;
	}

	job_ptr->job_state &= ~JOB_REQUEUE;

	debug("%s: job %u state 0x%x reason %u priority %d", __func__,
	      job_ptr->job_id, job_ptr->job_state,
	      job_ptr->state_reason, job_ptr->priority);

	return true;
}

/* init_requeue_policy()
 * Initialize the requeue exit/hold bitmaps.
 */
extern void init_requeue_policy(void)
{
	char *sched_params;

	/* clean first as we can be reconfiguring */
	FREE_NULL_BITMAP(requeue_exit);
	FREE_NULL_BITMAP(requeue_exit_hold);

	requeue_exit = _make_requeue_array(slurmctld_conf.requeue_exit);
	requeue_exit_hold = _make_requeue_array(
		slurmctld_conf.requeue_exit_hold);
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

	debug2("%s: kill_invalid_depend is set to %d",
	       __func__, kill_invalid_dep);
}

/* _make_requeue_array()
 *
 * Process the RequeueExit|RequeueExitHold configuration
 * parameters creating two bitmaps holding the exit values
 * of jobs for which they have to be requeued.
 */
static bitstr_t *_make_requeue_array(char *conf_buf)
{
	hostset_t hs;
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
static void
_set_job_requeue_exit_value(struct job_record *job_ptr)
{
	int exit_code;

	exit_code = WEXITSTATUS(job_ptr->exit_code);
	if ((exit_code < 0) || (exit_code > MAX_EXIT_VAL))
		return;

	if (requeue_exit && bit_test(requeue_exit, exit_code)) {
		debug2("%s: job %d exit code %d state JOB_REQUEUE",
		       __func__, job_ptr->job_id, exit_code);
		job_ptr->job_state |= JOB_REQUEUE;
		return;
	}

	if (requeue_exit_hold && bit_test(requeue_exit_hold, exit_code)) {
		/* Not sure if want to set special exit state in this case */
		debug2("%s: job %d exit code %d state JOB_SPECIAL_EXIT",
		       __func__, job_ptr->job_id, exit_code);
		job_ptr->job_state |= JOB_REQUEUE;
		job_ptr->job_state |= JOB_SPECIAL_EXIT;
		return;
	}
}

/*
 * Reset a job's end_time based upon it's start_time and time_limit.
 * NOTE: Do not reset the end_time if already being preempted
 */
extern void job_end_time_reset(struct job_record *job_ptr)
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

/*
 * jobid2fmt() - print a job ID including pack job and job array information.
 */
extern char *jobid2fmt(struct job_record *job_ptr, char *buf, int buf_size)
{
	if (job_ptr == NULL)
		return "jobid2fmt: Invalid job_ptr argument";
	if (buf == NULL)
		return "jobid2fmt: Invalid buf argument";

	if (job_ptr->pack_job_id) {
		snprintf(buf, buf_size, "JobID=%u+%u(%u)",
			 job_ptr->pack_job_id, job_ptr->pack_job_offset,
			 job_ptr->job_id);
	} else if (job_ptr->array_recs && (job_ptr->array_task_id == NO_VAL)) {
		snprintf(buf, buf_size, "JobID=%u_*",
			 job_ptr->array_job_id);
	} else if (job_ptr->array_task_id == NO_VAL) {
		snprintf(buf, buf_size, "JobID=%u", job_ptr->job_id);
	} else {
		snprintf(buf, buf_size, "JobID=%u_%u(%u)",
			 job_ptr->array_job_id, job_ptr->array_task_id,
			 job_ptr->job_id);
	}

       return buf;
}

/*
 * jobid2str() - print all the parts that uniquely identify a job.
 */
extern char *
jobid2str(struct job_record *job_ptr, char *buf, int buf_size)
{

	if (job_ptr == NULL)
		return "jobid2str: Invalid job_ptr argument";
	if (buf == NULL)
		return "jobid2str: Invalid buf argument";

	if (job_ptr->pack_job_id) {
		snprintf(buf, buf_size, "JobID=%u+%u(%u) State=0x%x NodeCnt=%u",
			job_ptr->pack_job_id, job_ptr->pack_job_offset,
			job_ptr->job_id, job_ptr->job_state, job_ptr->node_cnt);
	} else if (job_ptr->array_recs && (job_ptr->array_task_id == NO_VAL)) {
		snprintf(buf, buf_size, "JobID=%u_* State=0x%x NodeCnt=%u",
			job_ptr->array_job_id, job_ptr->job_state,
			job_ptr->node_cnt);
	} else if (job_ptr->array_task_id == NO_VAL) {
		snprintf(buf, buf_size, "JobID=%u State=0x%x NodeCnt=%u",
			job_ptr->job_id, job_ptr->job_state,
			job_ptr->node_cnt);
	} else {
		snprintf(buf, buf_size, "JobID=%u_%u(%u) State=0x%x NodeCnt=%u",
			job_ptr->array_job_id, job_ptr->array_task_id,
			job_ptr->job_id, job_ptr->job_state, job_ptr->node_cnt);
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

	if (slurmctld_conf.debug_flags & DEBUG_FLAG_TRACE_JOBS) {
		info("%s: %s %s", func, extra, jobid2str(job_ptr, jbuf,
							 sizeof(jbuf)));
	}
}

/* If this is a job array meta-job, prepare it for being scheduled */
extern void job_array_pre_sched(struct job_record *job_ptr)
{
	char jbuf[JBUFSIZ];
	int32_t i;

	if (!job_ptr->array_recs || !job_ptr->array_recs->task_id_bitmap)
		return;

	i = bit_ffs(job_ptr->array_recs->task_id_bitmap);
	if (i < 0) {
		/* This happens if the final task in a meta-job is requeued */
		if (job_ptr->restart_cnt == 0) {
			error("%s has empty task_id_bitmap",
			      jobid2str(job_ptr, jbuf, sizeof(jbuf)));
		}
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
	char jobid_buf[32];

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
		if (job_ptr->array_recs->task_cnt == 0)
			FREE_NULL_BITMAP(job_ptr->array_recs->task_id_bitmap);

		/* While it is efficient to set the db_index to 0 here
		 * to get the database to update the record for
		 * pending tasks it also creates a window in which if
		 * the association id is changed (different account or
		 * partition) instead of returning the previous
		 * db_index (expected) it would create a new one
		 * leaving the other orphaned.  Setting the job_state
		 * sets things up so the db_index isn't lost but the
		 * start message is still sent to get the desired behavior. */
		job_ptr->job_state |= JOB_UPDATE_DB;

		/* If job is requeued, it will already be in the hash table */
		if (!find_job_array_rec(job_ptr->array_job_id,
					job_ptr->array_task_id)) {
			_add_job_array_hash(job_ptr);
		}
	} else {
		new_job_ptr = job_array_split(job_ptr);
		if (new_job_ptr) {
			new_job_ptr->job_state = JOB_PENDING;
			new_job_ptr->start_time = (time_t) 0;
			/* Do NOT set the JOB_UPDATE_DB flag here, it
			 * is handled when task_id_str is created elsewhere */
		} else {
			error("%s: Unable to copy record for %s", __func__,
			      jobid2fmt(job_ptr, jobid_buf, sizeof(jobid_buf)));
		}
	}
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
	     "job %s", __func__, jobid2str(job_ptr, jbuf, sizeof(jbuf)));
	job_ptr->job_state = JOB_CANCELLED;
	xfree(job_ptr->state_desc);
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

extern void free_job_fed_details(job_fed_details_t **fed_details_pptr)
{
	job_fed_details_t *fed_details_ptr = *fed_details_pptr;

	if (fed_details_ptr) {
		xfree(fed_details_ptr->origin_str);
		xfree(fed_details_ptr->siblings_active_str);
		xfree(fed_details_ptr->siblings_viable_str);
		xfree(fed_details_ptr);
		*fed_details_pptr = NULL;
	}
}

static void _dump_job_fed_details(job_fed_details_t *fed_details_ptr,
				  Buf buffer)
{
	if (fed_details_ptr) {
		pack16(1, buffer);
		pack32(fed_details_ptr->cluster_lock, buffer);
		packstr(fed_details_ptr->origin_str, buffer);
		pack64(fed_details_ptr->siblings_active, buffer);
		packstr(fed_details_ptr->siblings_active_str, buffer);
		pack64(fed_details_ptr->siblings_viable, buffer);
		packstr(fed_details_ptr->siblings_viable_str, buffer);
	} else {
		pack16(0, buffer);
	}
}

static int _load_job_fed_details(job_fed_details_t **fed_details_pptr,
				 Buf buffer,
				 uint16_t protocol_version)
{
	uint16_t tmp_uint16;
	uint32_t tmp_uint32;
	job_fed_details_t *fed_details_ptr = NULL;

	xassert(fed_details_pptr);

	if (protocol_version >= SLURM_17_11_PROTOCOL_VERSION) {
		safe_unpack16(&tmp_uint16, buffer);
		if (tmp_uint16) {
			*fed_details_pptr = xmalloc(sizeof(job_fed_details_t));
			fed_details_ptr = *fed_details_pptr;
			safe_unpack32(&fed_details_ptr->cluster_lock, buffer);
			safe_unpackstr_xmalloc(&fed_details_ptr->origin_str,
					       &tmp_uint32, buffer);
			safe_unpack64(&fed_details_ptr->siblings_active,
				      buffer);
			safe_unpackstr_xmalloc(
					&fed_details_ptr->siblings_active_str,
					&tmp_uint32, buffer);
			safe_unpack64(&fed_details_ptr->siblings_viable,
				      buffer);
			safe_unpackstr_xmalloc(
					&fed_details_ptr->siblings_viable_str,
					&tmp_uint32, buffer);
		}
	} else if (protocol_version >= SLURM_17_02_PROTOCOL_VERSION) {
		safe_unpack16(&tmp_uint16, buffer);
		if (tmp_uint16) {
			*fed_details_pptr = xmalloc(sizeof(job_fed_details_t));
			fed_details_ptr = *fed_details_pptr;
			safe_unpack32(&fed_details_ptr->cluster_lock, buffer);
			safe_unpackstr_xmalloc(&fed_details_ptr->origin_str,
					       &tmp_uint32, buffer);
			safe_unpack64(&fed_details_ptr->siblings_viable,
				      buffer);
			safe_unpackstr_xmalloc(
					&fed_details_ptr->siblings_viable_str,
					&tmp_uint32, buffer);
		}
	}

	return SLURM_SUCCESS;

unpack_error:
	free_job_fed_details(fed_details_pptr);
	*fed_details_pptr = NULL;

	return SLURM_ERROR;
}

/* Set federated job's sibling strings. */
extern void update_job_fed_details(struct job_record *job_ptr)
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
extern void
set_remote_working_response(resource_allocation_response_msg_t *resp,
			    struct job_record *job_ptr,
			    const char *req_cluster)
{
	xassert(resp);
	xassert(job_ptr);

	if (job_ptr->node_cnt &&
	    req_cluster && slurmctld_conf.cluster_name &&
	    xstrcmp(slurmctld_conf.cluster_name, req_cluster)) {
		if (job_ptr->fed_details &&
		    fed_mgr_cluster_rec) {
			resp->working_cluster_rec = fed_mgr_cluster_rec;
		} else {
			if (!response_cluster_rec) {
				response_cluster_rec =
					xmalloc(sizeof(slurmdb_cluster_rec_t));
				response_cluster_rec->name =
					xstrdup(slurmctld_conf.cluster_name);
				response_cluster_rec->control_host =
					slurmctld_conf.control_addr;
				response_cluster_rec->control_port =
					slurmctld_conf.slurmctld_port;
				response_cluster_rec->rpc_version =
					SLURM_PROTOCOL_VERSION;
			}
			resp->working_cluster_rec = response_cluster_rec;
		}

		resp->node_addr = xmalloc(sizeof(slurm_addr_t) *
					  job_ptr->node_cnt);
		memcpy(resp->node_addr, job_ptr->node_addr,
		       (sizeof(slurm_addr_t) * job_ptr->node_cnt));
	}
}

/* Build structure with job allocation details */
extern resource_allocation_response_msg_t *
		build_job_info_resp(struct job_record *job_ptr)
{
	resource_allocation_response_msg_t *job_info_resp_msg;
	int i, j;

	job_info_resp_msg = xmalloc(sizeof(resource_allocation_response_msg_t));


	if (!job_ptr->job_resrcs) {
		;
	} else if (bit_equal(job_ptr->node_bitmap,
			     job_ptr->job_resrcs->node_bitmap)) {
		job_info_resp_msg->num_cpu_groups =
			job_ptr->job_resrcs->cpu_array_cnt;
		job_info_resp_msg->cpu_count_reps =
			xmalloc(sizeof(uint32_t) *
				job_ptr->job_resrcs->cpu_array_cnt);
		memcpy(job_info_resp_msg->cpu_count_reps,
		       job_ptr->job_resrcs->cpu_array_reps,
		       (sizeof(uint32_t) * job_ptr->job_resrcs->cpu_array_cnt));
		job_info_resp_msg->cpus_per_node  =
			xmalloc(sizeof(uint16_t) *
				job_ptr->job_resrcs->cpu_array_cnt);
		memcpy(job_info_resp_msg->cpus_per_node,
		       job_ptr->job_resrcs->cpu_array_value,
		       (sizeof(uint16_t) * job_ptr->job_resrcs->cpu_array_cnt));
	} else {
		/* Job has changed size, rebuild CPU count info */
		job_info_resp_msg->num_cpu_groups = job_ptr->node_cnt;
		job_info_resp_msg->cpu_count_reps =
			xmalloc(sizeof(uint32_t) * job_ptr->node_cnt);
		job_info_resp_msg->cpus_per_node =
			xmalloc(sizeof(uint32_t) * job_ptr->node_cnt);
		for (i = 0, j = -1; i < job_ptr->job_resrcs->nhosts; i++) {
			if (job_ptr->job_resrcs->cpus[i] == 0)
				continue;
			if ((j == -1) ||
			    (job_info_resp_msg->cpus_per_node[j] !=
			     job_ptr->job_resrcs->cpus[i])) {
				j++;
				job_info_resp_msg->cpus_per_node[j] =
					job_ptr->job_resrcs->cpus[i];
				job_info_resp_msg->cpu_count_reps[j] = 1;
			} else {
				job_info_resp_msg->cpu_count_reps[j]++;
			}
		}
		job_info_resp_msg->num_cpu_groups = j + 1;
	}
	job_info_resp_msg->account        = xstrdup(job_ptr->account);
	job_info_resp_msg->alias_list     = xstrdup(job_ptr->alias_list);
	job_info_resp_msg->job_id         = job_ptr->job_id;
	job_info_resp_msg->node_cnt       = job_ptr->node_cnt;
	job_info_resp_msg->node_list      = xstrdup(job_ptr->nodes);
	job_info_resp_msg->partition      = xstrdup(job_ptr->partition);
	if (job_ptr->qos_ptr) {
		slurmdb_qos_rec_t *qos;
		qos = (slurmdb_qos_rec_t *)job_ptr->qos_ptr;
		job_info_resp_msg->qos = xstrdup(qos->name);
	}
	job_info_resp_msg->resv_name      = xstrdup(job_ptr->resv_name);
	job_info_resp_msg->select_jobinfo =
		select_g_select_jobinfo_copy(job_ptr->select_jobinfo);
	if (job_ptr->details) {
		job_info_resp_msg->pn_min_memory =
			job_ptr->details->pn_min_memory;

		if (job_ptr->details->mc_ptr) {
			job_info_resp_msg->ntasks_per_board =
				job_ptr->details->mc_ptr->ntasks_per_board;
			job_info_resp_msg->ntasks_per_core =
				job_ptr->details->mc_ptr->ntasks_per_core;
			job_info_resp_msg->ntasks_per_socket =
				job_ptr->details->mc_ptr->ntasks_per_socket;
		}
	} else {
		job_info_resp_msg->pn_min_memory     = 0;
		job_info_resp_msg->ntasks_per_board  = NO_VAL16;
		job_info_resp_msg->ntasks_per_core   = NO_VAL16;
		job_info_resp_msg->ntasks_per_socket = NO_VAL16;
	}

	if (job_ptr->details && job_ptr->details->env_cnt) {
		job_info_resp_msg->env_size = job_ptr->details->env_cnt;
		job_info_resp_msg->environment =
			xmalloc(sizeof(char *) * job_info_resp_msg->env_size);
		for (i = 0; i < job_info_resp_msg->env_size; i++) {
			job_info_resp_msg->environment[i] =
				xstrdup(job_ptr->details->env_sup[i]);
		}
	}

	return job_info_resp_msg;
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
extern double calc_job_billable_tres(struct job_record *job_ptr,
				     time_t start_time, bool assoc_mgr_locked)
{
	xassert(job_ptr);

	struct part_record *part_ptr = job_ptr->part_ptr;

	/* We don't have any resources allocated, just return 0. */
	if (!job_ptr->tres_alloc_cnt)
		return 0;

	/* Don't recalculate unless the job is new or resized */
	if ((!fuzzy_equal(job_ptr->billable_tres, NO_VAL)) &&
	    difftime(job_ptr->resize_time, start_time) < 0.0)
		return job_ptr->billable_tres;

	if (slurmctld_conf.debug_flags & DEBUG_FLAG_PRIO)
		info("BillingWeight: job %d is either new or it was resized",
		     job_ptr->job_id);

	/* No billing weights defined. Return CPU count */
	if (!part_ptr || !part_ptr->billing_weights) {
		job_ptr->billable_tres = job_ptr->total_cpus;
		return job_ptr->billable_tres;
	}

	if (slurmctld_conf.debug_flags & DEBUG_FLAG_PRIO)
		info("BillingWeight: job %d using \"%s\" from partition %s",
		     job_ptr->job_id, part_ptr->billing_weights_str,
		     job_ptr->part_ptr->name);

	job_ptr->billable_tres =
		assoc_mgr_tres_weighted(job_ptr->tres_alloc_cnt,
					part_ptr->billing_weights,
					slurmctld_conf.priority_flags,
					assoc_mgr_locked);

	if (slurmctld_conf.debug_flags & DEBUG_FLAG_PRIO)
		info("BillingWeight: Job %d %s = %f", job_ptr->job_id,
		     (slurmctld_conf.priority_flags & PRIORITY_FLAGS_MAX_TRES) ?
		     "MAX(node TRES) + SUM(Global TRES)" : "SUM(TRES)",
		     job_ptr->billable_tres);

	return job_ptr->billable_tres;
}

