/*****************************************************************************\
 *  job_state.c
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

#include "src/common/macros.h"

#include "src/slurmctld/locks.h"
#include "src/slurmctld/slurmctld.h"

#define MAGIC_JOB_STATE_ARGS 0x0a0beeee

typedef struct {
	int magic; /* MAGIC_JOB_STATE_ARGS */
	int rc;
	uint32_t count;
	job_state_response_job_t *jobs;
	int jobs_count; /* Number of entries in jobs[] */
	bool count_only;
} job_state_args_t;

static void _log_job_state_change(const job_record_t *job_ptr,
				  const uint32_t new_state, const char *caller)
{
	char *before_str, *after_str;

	if (!(slurm_conf.debug_flags & DEBUG_FLAG_TRACE_JOBS))
		return;

	before_str = job_state_string_complete(job_ptr->job_state);
	after_str = job_state_string_complete(new_state);

	if (job_ptr->job_state == new_state) {
		if (get_log_level() >= LOG_LEVEL_DEBUG4)
			log_flag(TRACE_JOBS, "%s: [%pJ] no-op change state: %s",
				 caller, job_ptr, before_str);
	} else {
		log_flag(TRACE_JOBS, "%s: [%pJ] change state: %s -> %s",
			 caller, job_ptr, before_str, after_str);
	}

	xfree(before_str);
	xfree(after_str);
}

extern void job_state_set(job_record_t *job_ptr, uint32_t state)
{
	xassert(verify_lock(JOB_LOCK, WRITE_LOCK));
	_log_job_state_change(job_ptr, state, __func__);

	on_job_state_change(job_ptr, state);

	job_ptr->job_state = state;
}

extern void slurm_job_state_set_flag(job_record_t *job_ptr, uint32_t flag,
				     const char *caller)
{
	uint32_t job_state;

	xassert(verify_lock(JOB_LOCK, WRITE_LOCK));
	xassert(!(flag & JOB_STATE_BASE));
	xassert(flag & JOB_STATE_FLAGS);

	job_state = job_ptr->job_state | flag;
	_log_job_state_change(job_ptr, job_state, caller);

	slurm_on_job_state_change(job_ptr, job_state, caller);

	job_ptr->job_state = job_state;
}

extern void job_state_unset_flag(job_record_t *job_ptr, uint32_t flag)
{
	uint32_t job_state;

	xassert(verify_lock(JOB_LOCK, WRITE_LOCK));
	xassert(!(flag & JOB_STATE_BASE));
	xassert(flag & JOB_STATE_FLAGS);

	job_state = job_ptr->job_state & ~flag;
	_log_job_state_change(job_ptr, job_state, __func__);

	on_job_state_change(job_ptr, job_state);

	job_ptr->job_state = job_state;
}

static job_state_response_job_t *_append_job_state(job_state_args_t *args)
{
	int index;
	job_state_response_job_t *rjob;

	xassert(args->magic == MAGIC_JOB_STATE_ARGS);

	args->count++;

	if (args->count_only)
		return NULL;

	if (args->count > args->jobs_count)
		return NULL;

	index = args->count - 1;
	rjob = &args->jobs[index];
	xassert(!rjob->job_id);
	return rjob;
}

static bitstr_t *_job_state_array_bitmap(const job_record_t *job_ptr)
{
	if (!job_ptr->array_recs)
		return NULL;

	if (job_ptr->array_recs->task_id_bitmap &&
	    (bit_ffs(job_ptr->array_recs->task_id_bitmap) != -1))
		return bit_copy(job_ptr->array_recs->task_id_bitmap);

	return NULL;
}

static foreach_job_by_id_control_t _foreach_job(const job_record_t *job_ptr,
						const slurm_selected_step_t *id,
						void *arg)
{
	job_state_args_t *args = arg;
	job_state_response_job_t *rjob;

	xassert(args->magic == MAGIC_JOB_STATE_ARGS);

	rjob = _append_job_state(args);

	if (args->count_only)
		return FOR_EACH_JOB_BY_ID_EACH_CONT;

	if (!rjob)
		return FOR_EACH_JOB_BY_ID_EACH_FAIL;

	rjob->job_id = job_ptr->job_id;
	rjob->array_job_id = job_ptr->array_job_id;
	rjob->array_task_id = job_ptr->array_task_id;
	rjob->array_task_id_bitmap = _job_state_array_bitmap(job_ptr);
	rjob->het_job_id = job_ptr->het_job_id;
	rjob->state = job_ptr->job_state;
	return FOR_EACH_JOB_BY_ID_EACH_CONT;
}

static void _dump_job_state_locked(job_state_args_t *args,
				   const uint32_t filter_jobs_count,
				   const slurm_selected_step_t *filter_jobs_ptr)
{
	xassert(verify_lock(JOB_LOCK, READ_LOCK));
	xassert(args->magic == MAGIC_JOB_STATE_ARGS);

	if (!filter_jobs_count) {
		slurm_selected_step_t filter = SLURM_SELECTED_STEP_INITIALIZER;
		(void) foreach_job_by_id_ro(&filter, _foreach_job, NULL, args);
	} else {
		for (int i = 0; !args->rc && (i < filter_jobs_count); i++) {
			(void) foreach_job_by_id_ro(&filter_jobs_ptr[i],
						    _foreach_job, NULL, args);
		}
	}
}

extern int dump_job_state(const uint32_t filter_jobs_count,
			  const slurm_selected_step_t *filter_jobs_ptr,
			  uint32_t *jobs_count_ptr,
			  job_state_response_job_t **jobs_pptr)
{
	slurmctld_lock_t job_read_lock = { .job = READ_LOCK };
	job_state_args_t args = {
		.magic = MAGIC_JOB_STATE_ARGS,
		.count_only = true,
	};

	lock_slurmctld(job_read_lock);

	/*
	 * Loop once to grab the job count and then allocate the job array and
	 * then populate the array.
	 */
	_dump_job_state_locked(&args, filter_jobs_count, filter_jobs_ptr);

	if (args.count > 0) {
		if (!try_xrecalloc(args.jobs, args.count, sizeof(*args.jobs))) {
			args.rc = ENOMEM;
			goto cleanup;
		}

		args.jobs_count = args.count;

		/* reset count */
		args.count_only = false;
		args.count = 0;

		_dump_job_state_locked(&args, filter_jobs_count,
				       filter_jobs_ptr);
	}

	*jobs_pptr = args.jobs;
	*jobs_count_ptr = args.jobs_count;
cleanup:
	unlock_slurmctld(job_read_lock);

	return args.rc;
}

extern void slurm_on_job_state_change(job_record_t *job_ptr, uint32_t new_state,
				      const char *caller)
{
	_log_job_state_change(job_ptr, new_state, caller);
}
