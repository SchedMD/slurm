/*****************************************************************************\
 *  job_state.c
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
 *  Written by Nathan Rini <nate@schedmd.com>
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
	uint32_t *jobs_count_ptr;
	job_state_response_job_t **jobs_pptr;
} job_state_args_t;

typedef struct {
	job_record_t *job_ptr;
	job_state_args_t *job_state_args;
} foreach_het_job_state_args_t;

#ifndef NDEBUG

#define T(x) { x, XSTRINGIFY(x) }
static const struct {
	uint32_t flag;
	char *string;
} job_flags[] = {
	T(JOB_LAUNCH_FAILED),
	T(JOB_UPDATE_DB),
	T(JOB_REQUEUE),
	T(JOB_REQUEUE_HOLD),
	T(JOB_SPECIAL_EXIT),
	T(JOB_RESIZING),
	T(JOB_CONFIGURING),
	T(JOB_COMPLETING),
	T(JOB_STOPPED),
	T(JOB_RECONFIG_FAIL),
	T(JOB_POWER_UP_NODE),
	T(JOB_REVOKED),
	T(JOB_REQUEUE_FED),
	T(JOB_RESV_DEL_HOLD),
	T(JOB_SIGNALING),
	T(JOB_STAGE_OUT),
};

static void _check_job_state(const uint32_t state)
{
	uint32_t flags;

	if (!(slurm_conf.debug_flags & DEBUG_FLAG_TRACE_JOBS))
		return;

	flags = (state & JOB_STATE_FLAGS);

	xassert((state & JOB_STATE_BASE) < JOB_END);

	for (int i = 0; i < ARRAY_SIZE(job_flags); i++)
		if ((flags & job_flags[i].flag) == job_flags[i].flag)
			flags &= ~(job_flags[i].flag);

	/* catch any bits that are not known flags */
	xassert(!flags);
}

static void _log_job_state_change(const job_record_t *job_ptr,
				  const uint32_t new_state)
{
	char *before_str, *after_str;

	if (!(slurm_conf.debug_flags & DEBUG_FLAG_TRACE_JOBS))
		return;

	before_str = job_state_string_complete(job_ptr->job_state);
	after_str = job_state_string_complete(new_state);

	if (job_ptr->job_state == new_state)
		log_flag(TRACE_JOBS, "%s: [%pJ] no-op change state: %s",
			 __func__, job_ptr, before_str);
	else
		log_flag(TRACE_JOBS, "%s: [%pJ] change state: %s -> %s",
			 __func__, job_ptr, before_str, after_str);

	xfree(before_str);
	xfree(after_str);
}

#else /* NDEBUG */

#define _check_job_state(state) {}
#define _log_job_state_change(job_ptr, new_state) {}

#endif /* NDEBUG */

extern void job_state_set(job_record_t *job_ptr, uint32_t state)
{
	_check_job_state(state);
	_log_job_state_change(job_ptr, state);

	job_ptr->job_state = state;
}

extern void job_state_set_flag(job_record_t *job_ptr, uint32_t flag)
{
	uint32_t job_state;

	xassert(!(flag & JOB_STATE_BASE));
	xassert(flag & JOB_STATE_FLAGS);

	job_state = job_ptr->job_state | flag;
	_check_job_state(job_state);
	_log_job_state_change(job_ptr, job_state);

	job_ptr->job_state = job_state;
}

extern void job_state_unset_flag(job_record_t *job_ptr, uint32_t flag)
{
	uint32_t job_state;

	xassert(!(flag & JOB_STATE_BASE));
	xassert(flag & JOB_STATE_FLAGS);

	job_state = job_ptr->job_state & ~flag;
	_check_job_state(job_state);
	_log_job_state_change(job_ptr, job_state);

	job_ptr->job_state = job_state;
}

static job_state_response_job_t *_append_job_state(job_state_args_t *args,
						   uint32_t job_id)
{
	job_state_response_job_t *rjob;

	xassert(args->magic == MAGIC_JOB_STATE_ARGS);
	xassert(job_id > 0);

	(*args->jobs_count_ptr)++;
	if (!try_xrecalloc((*args->jobs_pptr), *args->jobs_count_ptr,
			   sizeof(**args->jobs_pptr))) {
		args->rc = ENOMEM;
		return NULL;
	}

	rjob = &((*args->jobs_pptr)[*args->jobs_count_ptr - 1]);

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

static int _add_job_state_job(job_state_args_t *args,
			      const job_record_t *job_ptr)
{
	job_state_response_job_t *rjob;

	xassert(args->magic == MAGIC_JOB_STATE_ARGS);

	if (!(rjob = _append_job_state(args, job_ptr->job_id)))
		return SLURM_ERROR;

	rjob->job_id = job_ptr->job_id;
	rjob->array_job_id = job_ptr->array_job_id;
	rjob->array_task_id = job_ptr->array_task_id;
	rjob->array_task_id_bitmap = _job_state_array_bitmap(job_ptr);
	rjob->het_job_id = job_ptr->het_job_id;
	rjob->state = job_ptr->job_state;
	return SLURM_SUCCESS;
}

static int _foreach_add_job_state_het_job(void *x, void *arg)
{
	job_record_t *het_job_ptr = x;
	foreach_het_job_state_args_t *het_args = arg;

	if (het_job_ptr->het_job_id == het_args->job_ptr->het_job_id) {
		_add_job_state_job(het_args->job_state_args, het_job_ptr);
		return 0;
	} else {
		error("%s: Bad het_job_list for %pJ",
		      __func__, het_args->job_ptr);
		return -1;
	}
}

static int _add_job_state_by_job_id(const uint32_t job_id,
				    job_state_args_t *args)
{
	job_record_t *job_ptr;
	int rc = SLURM_SUCCESS;

	xassert(args->magic == MAGIC_JOB_STATE_ARGS);

	/*
	 * This uses the similar logic as pack_one_job() but simpler as whole
	 * array is always being dumped.
	 * TODO: Combine the duplicate logic.
	 */
	job_ptr = find_job_record(job_id);

	if (!job_ptr) {
		/* No job found is okay */
		//return ESLURM_INVALID_JOB_ID;
		return SLURM_SUCCESS;
	} else if (job_ptr && job_ptr->het_job_list) {
		foreach_het_job_state_args_t het_args = {
			.job_ptr = job_ptr,
			.job_state_args = args,
		};

		if (list_for_each(job_ptr->het_job_list,
				  _foreach_add_job_state_het_job,
				  &het_args) < 0) {
			return SLURM_ERROR;
		}
		return SLURM_SUCCESS;
	} else if (job_ptr && (job_ptr->array_task_id == NO_VAL) &&
		   !job_ptr->array_recs) {
		/* Pack regular (not array) job */
		return _add_job_state_job(args, job_ptr);
	} else {
		if ((rc = _add_job_state_job(args, job_ptr)))
			return rc;

		while ((job_ptr = job_ptr->job_array_next_j))
			if ((job_ptr->array_job_id == job_id) &&
			    (rc = _add_job_state_job(args, job_ptr)))
				return rc;
	}

	return args->rc;
}

static int _foreach_job_state_filter(void *object, void *arg)
{
	const job_record_t *job_ptr = object;
	job_state_args_t *args = arg;

	xassert(args->magic == MAGIC_JOB_STATE_ARGS);

	if ((args->rc = _add_job_state_job(args, job_ptr)))
		return SLURM_ERROR;

	return SLURM_SUCCESS;
}

static void _dump_job_state_locked(job_state_args_t *args,
				   const uint16_t filter_jobs_count,
				   const uint32_t *filter_jobs_ptr)
{
	xassert(verify_lock(JOB_LOCK, READ_LOCK));

	if (!filter_jobs_count) {
		(void) list_for_each_ro(job_list, _foreach_job_state_filter,
					args);
	} else {
		for (int i = 0; !args->rc && (i < filter_jobs_count); i++)
			args->rc = _add_job_state_by_job_id(filter_jobs_ptr[i],
							    args);
	}
}

extern int dump_job_state(const uint32_t filter_jobs_count,
			  const uint32_t *filter_jobs_ptr,
			  uint32_t *jobs_count_ptr,
			  job_state_response_job_t **jobs_pptr)
{
	job_state_args_t args = {
		.magic = MAGIC_JOB_STATE_ARGS,
		.rc = SLURM_SUCCESS,
		.jobs_count_ptr = jobs_count_ptr,
		.jobs_pptr = jobs_pptr,
	};

	_dump_job_state_locked(&args, filter_jobs_count, filter_jobs_ptr);

	return args.rc;
}
