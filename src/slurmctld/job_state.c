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
#include "src/common/xahash.h"
#include "src/common/xstring.h"

#include "src/slurmctld/locks.h"
#include "src/slurmctld/slurmctld.h"

/* Macro to only include the code inside of ONLY_DEBUG if !NDEBUG */
#ifndef NDEBUG
#define ONLY_DEBUG(...) __VA_ARGS__
#else
#define ONLY_DEBUG(...)
#endif

#define JOB_STATE_MIMIC_RECORD(js)                                             \
	&(const job_record_t)                                                  \
	{                                                                      \
		.magic = JOB_MAGIC, .job_id = js->job_id,                      \
		.array_job_id = js->array_job_id,                              \
		.array_task_id = js->array_task_id,                            \
		.het_job_id = js->het_job_id, .job_state = js->job_state,      \
		.array_recs =                                                  \
			(!js->task_id_bitmap ?                                 \
				 NULL :                                        \
				 &(job_array_struct_t){                        \
					 .task_cnt =                           \
						 bit_size(js->task_id_bitmap), \
					 .task_id_bitmap = js->task_id_bitmap, \
				 }),                                           \
	}

#define ARRAY_JOB_STATE_MIMIC_RECORD(ajs)                  \
	&(const job_record_t)                              \
	{                                                  \
		.magic = JOB_MAGIC, .job_id = ajs->job_id, \
		.array_task_id = NO_VAL,                   \
	}

#define ARRAY_TASK_STATE_KEY_BYTES sizeof(array_task_state_cached_t)
#define ARRAY_TASK_STATE_KEY_JOB_ID(find_array_job_id, find_array_task_id) \
	&(array_task_state_cached_t)                                       \
	{                                                                  \
		ONLY_DEBUG(.magic = MAGIC_ARRAY_TASK_STATE_CACHED, )       \
			.job_id = NO_VAL, /* not used in search */         \
			.array_job_id = find_array_job_id,                 \
		       .array_task_id = find_array_task_id,                \
	}
#define ARRAY_TASK_STATE_KEY_JOB_PTR(job_ptr)                        \
	&(array_task_state_cached_t)                                 \
	{                                                            \
		ONLY_DEBUG(.magic = MAGIC_ARRAY_TASK_STATE_CACHED, ) \
			.job_id = job_ptr->job_id,                   \
		       .array_job_id = job_ptr->array_job_id,        \
		       .array_task_id = job_ptr->array_task_id,      \
	}
#define ARRAY_TASK_STATE_KEY_SELECTED_STEP(selected_step)                    \
	&(array_task_state_cached_t)                                         \
	{                                                                    \
		ONLY_DEBUG(.magic = MAGIC_ARRAY_TASK_STATE_CACHED, )         \
			.job_id = ((selected_step->array_task_id < NO_VAL) ? \
					   NO_VAL :                          \
					   selected_step->step_id.job_id),   \
		       .array_job_id =                                       \
			       ((selected_step->array_task_id < NO_VAL) ?    \
					selected_step->step_id.job_id :      \
					0),                                  \
		       .array_task_id = selected_step->array_task_id,        \
	}

#define ARRAY_TASK_STATE_MIMIC_RECORD(ats)                 \
	&(const job_record_t)                              \
	{                                                  \
		.magic = JOB_MAGIC, .job_id = ats->job_id, \
		.array_job_id = ats->array_job_id,         \
		.array_task_id = ats->array_task_id,       \
	}

#define MAGIC_JOB_STATE_ARGS 0x0a0beeee

typedef struct {
	int magic; /* MAGIC_JOB_STATE_ARGS */
	int rc;
	uint32_t count;
	job_state_response_job_t *jobs;
	bool count_only;
} job_state_args_t;

#define MAGIC_CACHE_TABLE_STATE 0x1a0beffe
typedef struct {
	ONLY_DEBUG(int magic;) /* MAGIC_CACHE_TABLE_STATE */
	int table_size;
} cache_table_state_t;

#define MAGIC_JOB_STATE_CACHED 0x1aa0affb
typedef struct {
	ONLY_DEBUG(int magic;) /* MAGIC_JOB_STATE_CACHED */
	uint32_t job_id;
	uint32_t job_state;
	uint32_t het_job_id;
	uint32_t array_job_id;
	uint32_t array_task_id;
	bitstr_t *task_id_bitmap;
} job_state_cached_t;

/* maps array job_id & task_id to non-array job_id */
#define MAGIC_ARRAY_TASK_STATE_CACHED 0xb2a00fcb
typedef struct {
	ONLY_DEBUG(int magic;) /* MAGIC_ARRAY_TASK_STATE_CACHED */
	uint32_t job_id;
	uint32_t array_job_id;
	uint32_t array_task_id;
} array_task_state_cached_t;

/* maps array job_id to circular linked list of non-array job_ids */
#define MAGIC_ARRAY_JOB_STATE_CACHED 0xb21f0fca
typedef struct {
	ONLY_DEBUG(int magic;) /* MAGIC_ARRAY_JOB_STATE_CACHED */
	uint32_t job_id;
	uint32_t next_job_id; /* next job_id of member of array or 0 */
} array_job_state_cached_t;

/*
 * Caches job state (outside of job_mgr.c and JOB_LOCK).
 *
 * Hash: job_id
 * entry type: job_state_cached_t
 *	Includes all Job ID info required to identify each job.
 *	1 job per entry.
 * state type: cache_table_state_t
 *	Maintains table_size for number of jobs reserved in hashtable
 */
static xahash_table_t *cache_table = NULL;
/*
 * Caches linked list of Array job's job_id to allow finding every job member of
 * an array job.
 *
 * Hash: job_id
 * entry type: job_state_cached_t
 *	Includes all Job ID info required to identify each job.
 *	1 job per entry.
 * state type: cache_table_state_t
 *	Maintains table_size for number of jobs reserved in hashtable
 */
static xahash_table_t *array_job_cache_table = NULL;
/*
 * Caches mapping of array_job_id & array_task_id to job_id of every member of
 * an array job.
 *
 * Hash: array_job_id & array_task_id
 * entry type: array_task_state_cached_t
 * state type: cache_table_state_t
 *	Maintains table_size for number of jobs reserved in hashtable
 */
static xahash_table_t *array_task_cache_table = NULL;
static pthread_rwlock_t cache_lock = PTHREAD_RWLOCK_INITIALIZER;

#ifndef NDEBUG

#define DEF_DEBUG_TIMER DEF_TIMERS
#define START_DEBUG_TIMER START_TIMER
#define END_DEBUG_TIMER END_TIMER2(__func__)

#define T(x) { x, XSTRINGIFY(x) }
static const struct {
	uint32_t flag;
	char *string;
} job_flags[] = {
	T(JOB_LAUNCH_FAILED),
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

/*
 * Replace magic/sanity checks with no-ops that reference args to avoid compile
 * warnings.
 */

#define _check_job_state(state) {(void) state;}
#define _log_job_state_change(job_ptr, new_state) \
	{(void) job_ptr; (void) new_state;}
#define DEF_DEBUG_TIMER
#define START_DEBUG_TIMER
#define END_DEBUG_TIMER

#endif /* NDEBUG */

#define _log_array_job_chain(js, caller, fmt, ...) {}
#define _check_all_jobs(compare_job_ptrs) {}
#define _is_debug() (false)
#define LOG(fmt, ...) do {} while (false)
#define _check_job_id(job_id_ptr) {(void) job_id_ptr;}
#define _check_job_magic(js) {(void) js;}
#define _check_array_job_magic(ajs) {(void) ajs;}
#define _check_array_job_magic_links(job_id, array_job_id, should_be_linked) \
	{(void) job_id; (void) array_job_id; (void) should_be_linked;}
#define _check_array_task_magic(ats) {(void) ats;}
#define _check_state_magic(state) {(void) state;}

extern void job_state_set(job_record_t *job_ptr, uint32_t state)
{
	xassert(verify_lock(JOB_LOCK, WRITE_LOCK));
	_check_job_state(state);
	_log_job_state_change(job_ptr, state);

	on_job_state_change(job_ptr, state);

	job_ptr->job_state = state;
}

extern void job_state_set_flag(job_record_t *job_ptr, uint32_t flag)
{
	uint32_t job_state;

	xassert(verify_lock(JOB_LOCK, WRITE_LOCK));
	xassert(!(flag & JOB_STATE_BASE));
	xassert(flag & JOB_STATE_FLAGS);

	job_state = job_ptr->job_state | flag;
	_check_job_state(job_state);
	_log_job_state_change(job_ptr, job_state);

	on_job_state_change(job_ptr, job_state);

	job_ptr->job_state = job_state;
}

extern void job_state_unset_flag(job_record_t *job_ptr, uint32_t flag)
{
	uint32_t job_state;

	xassert(verify_lock(JOB_LOCK, WRITE_LOCK));
	xassert(!(flag & JOB_STATE_BASE));
	xassert(flag & JOB_STATE_FLAGS);

	job_state = job_ptr->job_state & ~flag;
	_check_job_state(job_state);
	_log_job_state_change(job_ptr, job_state);

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

static void _add_cache_job(job_state_args_t *args, const job_state_cached_t *js)
{
	job_state_response_job_t *rjob;

	xassert(args->magic == MAGIC_JOB_STATE_ARGS);
	xassert(js->magic == MAGIC_JOB_STATE_CACHED);

	rjob = _append_job_state(args);

	if (args->count_only)
		return;

	if (!rjob) {
		xassert(rjob);
		return;
	}

	if (_is_debug()) {
		char *bitstr =
			(js->task_id_bitmap ? bit_fmt_full(js->task_id_bitmap) :
					      NULL);
		const size_t bits =
			(js->task_id_bitmap ? bit_size(js->task_id_bitmap) : 0);

		LOG("[%pJ] packing JobId=%u ArrayJobId=%u ArrayTaskId=%u array_task_id_bitmap[%zu]=%s HetJobId=%u state:%s",
		    JOB_STATE_MIMIC_RECORD(js), js->job_id, js->array_job_id,
		    js->array_task_id, bits, (bitstr ? bitstr : ""),
		    js->het_job_id, job_state_string(js->job_state));

		(void) bits;
		xfree(bitstr);
	}

	rjob->job_id = js->job_id;
	rjob->array_job_id = js->array_job_id;
	rjob->array_task_id = js->array_task_id;
	if (js->task_id_bitmap)
		rjob->array_task_id_bitmap = bit_copy(js->task_id_bitmap);
	rjob->het_job_id = js->het_job_id;
	rjob->state = js->job_state;
}

static xahash_foreach_control_t _foreach_cache_job(void *entry, void *state_ptr,
						   void *arg)
{
	const job_state_cached_t *js = entry;
	ONLY_DEBUG(cache_table_state_t *state = state_ptr;)
	job_state_args_t *args = arg;

	xassert(!state || (state->magic == MAGIC_CACHE_TABLE_STATE));
	xassert(args->magic == MAGIC_JOB_STATE_ARGS);
	xassert(js->magic == MAGIC_JOB_STATE_CACHED);

	_add_cache_job(args, js);
	return XAHASH_FOREACH_CONT;
}

static void _find_job_state_cached_by_job_id(job_state_args_t *args,
					     uint32_t job_id, bool resolve)
{
	const job_state_cached_t *js;

	if (!(js = xahash_find_entry(cache_table, &job_id, sizeof(job_id)))) {
		LOG("[JobId=%u] Unable to resolve job", job_id);
		return;
	}

	_check_job_magic(js);

	LOG("[%pJ] Resolved from JobId=%u", JOB_STATE_MIMIC_RECORD(js), job_id);

	_add_cache_job(args, js);

	if (!resolve) {
		LOG("[%pJ] Not fully resolving job", JOB_STATE_MIMIC_RECORD(js));
		return;
	}

	if ((js->array_job_id > 0) && (js->array_job_id == js->job_id)) {
		array_job_state_cached_t *ajs;

		ajs = xahash_find_entry(array_job_cache_table,
					&js->array_job_id,
					sizeof(js->array_job_id));

		_check_array_job_magic(ajs);

		_log_array_job_chain(js, __func__,
				     "Resolved %pJ with next: JobId=%u",
				     ARRAY_JOB_STATE_MIMIC_RECORD(ajs),
				     ajs->next_job_id);

		while (ajs->next_job_id != js->array_job_id) {
			const job_state_cached_t *next;

			if ((next = xahash_find_entry(
				     cache_table, &ajs->next_job_id,
				     sizeof(ajs->next_job_id)))) {
				LOG("[%pJ] Resolved to %pJ via %pJ",
				    JOB_STATE_MIMIC_RECORD(js),
				    JOB_STATE_MIMIC_RECORD(next),
				    ARRAY_JOB_STATE_MIMIC_RECORD(ajs));
				_add_cache_job(args, next);
			} else {
				fatal_abort("Unable to resolve next_job_id");
			}

			ajs = xahash_find_entry(array_job_cache_table,
						&ajs->next_job_id,
						sizeof(ajs->next_job_id));
		}

		xassert(ajs->next_job_id == js->array_job_id);
	} else if (js->het_job_id == js->job_id) {
		for (uint32_t i = 1; i < MAX_JOB_ID; i++) {
			const job_state_cached_t *hjs;
			uint32_t het_job_id = js->het_job_id + i;

			if ((hjs = xahash_find_entry(cache_table, &het_job_id,
						     sizeof(het_job_id))) &&
			    (hjs->het_job_id == js->het_job_id)) {
				LOG("[%pJ] Resolved HetJobId=%u+%u to %pJ",
				    JOB_STATE_MIMIC_RECORD(js), job_id, i,
				    JOB_STATE_MIMIC_RECORD(hjs));
				_add_cache_job(args, hjs);
			} else {
				/*
				 * Next job not found or not part of the HetJob
				 */
				break;
			}
		}
	} else {
		LOG("[%pJ] Nothing else to resolve",
		    JOB_STATE_MIMIC_RECORD(js));
	}
}

static void _find_job_state_cached_by_id(job_state_args_t *args,
					 const slurm_selected_step_t *filter)
{
	char *filter_str = NULL;

	if (_is_debug())
		(void) fmt_job_id_string((slurm_selected_step_t *) filter,
					 &filter_str);

	if (!filter->step_id.job_id) {
		/* 0 is never a valid job so just return now */
		goto cleanup;
	} else if (filter->step_id.job_id == NO_VAL) {
		/* walk all jobs */
		(void) xahash_foreach_entry(cache_table, _foreach_cache_job,
					    args);
		goto cleanup;
	}

	xassert(!((filter->array_task_id != NO_VAL) &&
		  (filter->het_job_offset != NO_VAL)));

	if (filter->array_task_id != NO_VAL) {
		const array_task_state_cached_t *ats;

		ats = xahash_find_entry(
			array_task_cache_table,
			ARRAY_TASK_STATE_KEY_SELECTED_STEP(filter),
			ARRAY_TASK_STATE_KEY_BYTES);

		if (ats) {
			slurm_selected_step_t aj_filter =
				SLURM_SELECTED_STEP_INITIALIZER;
			aj_filter.step_id.job_id = ats->job_id;

			_check_array_task_magic(ats);
			LOG("[%pJ] Resolved from %s",
			    ARRAY_TASK_STATE_MIMIC_RECORD(ats), filter_str);

			_find_job_state_cached_by_id(args, &aj_filter);
		} else {
			LOG("[%s] Unable to resolve job", filter_str);
		}
	} else if (filter->het_job_offset != NO_VAL) {
		_find_job_state_cached_by_job_id(args,
						 (filter->step_id.job_id +
						  filter->het_job_offset),
						 false);
	} else {
		_find_job_state_cached_by_job_id(args, filter->step_id.job_id,
						 true);
	}

cleanup:
	xfree(filter_str);
}

static void _dump_job_state_cached(job_state_args_t *args,
				   const uint32_t filter_jobs_count,
				   const slurm_selected_step_t *filter_jobs_ptr)
{
	xassert(args->magic == MAGIC_JOB_STATE_ARGS);

	slurm_rwlock_rdlock(&cache_lock);

	if (!filter_jobs_count) {
		(void) xahash_foreach_entry(cache_table, _foreach_cache_job,
					    args);
	} else {
		for (int i = 0; !args->rc && (i < filter_jobs_count); i++)
			_find_job_state_cached_by_id(args, &filter_jobs_ptr[i]);
	}

	slurm_rwlock_unlock(&cache_lock);
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
	/*
	 * In order to avoid the time cost, we are not taking the cache_lock
	 * to check if the cache_table pointer is !NULL as the cost of hitting
	 * an invalid cached state here is very minor.
	 */
	bool use_cache = cache_table;

	if (!use_cache)
		lock_slurmctld(job_read_lock);

	/*
	 * Loop once to grab the job count and then allocate the job array and
	 * then populate the array.
	 */

	if (use_cache)
		_dump_job_state_cached(&args, filter_jobs_count,
				       filter_jobs_ptr);
	else
		_dump_job_state_locked(&args, filter_jobs_count,
				       filter_jobs_ptr);

	if (args.count > 0) {
		if (!try_xrecalloc(args.jobs, args.count, sizeof(*args.jobs))) {
			args.rc = ENOMEM;
			goto cleanup;
		}

		/* reset count */
		args.count_only = false;
		args.count = 0;

		if (use_cache)
			_dump_job_state_cached(&args, filter_jobs_count,
					       filter_jobs_ptr);
		else
			_dump_job_state_locked(&args, filter_jobs_count,
					       filter_jobs_ptr);
	}

	*jobs_pptr = args.jobs;
	*jobs_count_ptr = args.count;
cleanup:
	if (!use_cache)
		unlock_slurmctld(job_read_lock);

	return args.rc;
}

static void _sync_job_task_id_bitmap(const job_record_t *job_ptr,
				     job_state_cached_t *js)
{
	if (!job_ptr->array_recs) {
		if (job_ptr->array_task_id == NO_VAL) {
			/* before array job is split */
			LOG("[%pJ] ignoring array job without array_recs",
			    JOB_STATE_MIMIC_RECORD(js));
			return;
		}

		/* conversion from meta job to normal job */
		xassert(!js->task_id_bitmap || (js->array_task_id == NO_VAL));

		if (_is_debug() && js->task_id_bitmap) {
			char *before = bit_fmt_full(js->task_id_bitmap);
			LOG("[%pJ] releasing array task_id_bitmap[%lu]: %s",
			    JOB_STATE_MIMIC_RECORD(js),
			    bit_size(js->task_id_bitmap), before);
			xfree(before);
		}

		/* bitmap removed by state change or was never there */
		FREE_NULL_BITMAP(js->task_id_bitmap);

		return;
	}

	if (!job_ptr->array_recs->task_id_bitmap) {
		uint32_t task_cnt = job_ptr->array_recs->pend_run_tasks;

		if ((job_ptr->job_state != JOB_PENDING) || !task_cnt) {
			if (_is_debug() &&
			    (job_ptr->job_state != JOB_PENDING) &&
			    js->task_id_bitmap) {
				char *before = bit_fmt_full(js->task_id_bitmap);
				LOG("[%pJ] job no longer pending: releasing array task_id_bitmap[%lu]: %s",
				    JOB_STATE_MIMIC_RECORD(js),
				    bit_size(js->task_id_bitmap), before);
				xfree(before);
			}

			if (_is_debug() && !task_cnt && js->task_id_bitmap)
				LOG("[%pJ] pending array job without pending task count",
				    JOB_STATE_MIMIC_RECORD(js));

			/* bitmap removed by state change or was never there */
			FREE_NULL_BITMAP(js->task_id_bitmap);

			return;
		}

		if (js->task_id_bitmap &&
		    (bit_size(js->task_id_bitmap) != task_cnt)) {
			LOG("[%pJ] array job task_id_bitmap changed from %lu to %u",
			    JOB_STATE_MIMIC_RECORD(js),
			    (js->task_id_bitmap ? bit_size(js->task_id_bitmap) :
						  0),
			    task_cnt);
			FREE_NULL_BITMAP(js->task_id_bitmap);
		}

		if (!js->task_id_bitmap) {
			LOG("[%pJ] mimicing array without task_id_bitmap with new bitmap[%u]",
			    JOB_STATE_MIMIC_RECORD(js), task_cnt);
			js->task_id_bitmap = bit_alloc(task_cnt);
		}

		xassert(js->task_id_bitmap);
		xassert(bit_size(js->task_id_bitmap) == task_cnt);

		bit_set_all(js->task_id_bitmap);

		if (_is_debug()) {
			char *map = bit_fmt_full(js->task_id_bitmap);
			LOG("[%pJ] mimicing array without bitmap as task_id_bitmap[%lu]: %s",
			    JOB_STATE_MIMIC_RECORD(js),
			    bit_size(js->task_id_bitmap), map);
			xfree(map);
		}

		return;
	}

	if (js->task_id_bitmap &&
	    (bit_size(js->task_id_bitmap) ==
	     bit_size(job_ptr->array_recs->task_id_bitmap))) {
		/* resync all bits */

		if (_is_debug()) {
			char *before = bit_fmt_full(js->task_id_bitmap);
			char *after = bit_fmt_full(job_ptr->array_recs
							   ->task_id_bitmap);
			LOG("[%pJ] updating array task_id_bitmap[%lu]: %s -> %s",
			    JOB_STATE_MIMIC_RECORD(js),
			    bit_size(job_ptr->array_recs->task_id_bitmap),
			    before, after);
			xfree(before);
			xfree(after);
		}

		bit_copybits(js->task_id_bitmap,
			     job_ptr->array_recs->task_id_bitmap);
	} else {
		/* new bitmap or bit count changed */

		if (_is_debug()) {
			char *before =
				(js->task_id_bitmap ?
					 bit_fmt_full(js->task_id_bitmap) :
					 NULL);
			char *after =
				(job_ptr->array_recs->task_id_bitmap ?
					 bit_fmt_full(
						 job_ptr->array_recs
							 ->task_id_bitmap) :
					 NULL);
			LOG("[%pJ] new array task_id_bitmap[%lu]: %s -> %s",
			    JOB_STATE_MIMIC_RECORD(js),
			    (job_ptr->array_recs->task_id_bitmap ?
				     bit_size(job_ptr->array_recs
						      ->task_id_bitmap) :
				     0),
			    (before ? before : "∅"), (after ? after : "∅"));
			xfree(before);
			xfree(after);
		}

		FREE_NULL_BITMAP(js->task_id_bitmap);
		js->task_id_bitmap =
			bit_copy(job_ptr->array_recs->task_id_bitmap);
	}
}

static void _link_array_job(const job_record_t *job_ptr, job_state_cached_t *js)
{
	array_task_state_cached_t *ats;
	array_job_state_cached_t *ajs, *meta;
	const uint32_t job_id = job_ptr->job_id;

	xassert(!job_ptr->het_job_id);
	xassert(job_ptr->array_job_id > 0);
	xassert(js->array_job_id > 0);
	xassert(js->array_job_id == job_ptr->array_job_id);
	xassert(js->array_task_id == job_ptr->array_task_id);

	ats = xahash_insert_entry(array_task_cache_table,
				  ARRAY_TASK_STATE_KEY_JOB_PTR(job_ptr),
				  ARRAY_TASK_STATE_KEY_BYTES);
	_check_array_task_magic(ats);
	xassert(ats->array_job_id == job_ptr->array_job_id);
	xassert(ats->array_task_id == job_ptr->array_task_id);

	ajs = xahash_insert_entry(array_job_cache_table, &job_id,
				  sizeof(job_id));
	_check_array_job_magic(ajs);

	if (ajs->next_job_id != job_id) {
		_log_array_job_chain(
			js, __func__,
			"skipping already linked array jobs next:JobId=%u",
			ajs->next_job_id);
		_check_array_job_magic_links(job_ptr->job_id,
					     job_ptr->array_job_id, true);
		return;
	}

	/* Newly inserted jobs only link to themselves */
	xassert(ajs->next_job_id == job_id);
	xassert(ajs->job_id == job_id);

	/* need to add this job into linked list of jobs for array */
	meta = xahash_insert_entry(array_job_cache_table,
				   &job_ptr->array_job_id,
				   sizeof(job_ptr->array_job_id));
	_check_array_job_magic(meta);

	if (job_ptr->job_id == job_ptr->array_job_id) {
		/*
		 * Can't link meta job to itself, so find if another job already
		 * linked to meta and created a stub for the meta job.
		 */

		if (meta->next_job_id != meta->job_id) {
			_log_array_job_chain(js, __func__,
				"skipping already linked array meta job");
			return;
		} else {
			_log_array_job_chain(js, __func__,
				"skipping linking singular array meta job");
			return;
		}
	}

	xassert(meta != ajs);

	ajs->next_job_id = meta->next_job_id;
	meta->next_job_id = job_id;

	_check_array_job_magic(ajs);
	_check_array_job_magic(meta);
	_log_array_job_chain(js, __func__, "linked to %pJ",
			     ARRAY_JOB_STATE_MIMIC_RECORD(meta));
	_check_array_job_magic_links(job_ptr->job_id, job_ptr->array_job_id,
				     true);
}

/* remove ajs from linked list of array jobs */
static void _unlink_array_job(const job_record_t *job_ptr,
			      job_state_cached_t *js,
			      array_job_state_cached_t *ajs)
{
	uint32_t job_id = js->job_id;
	uint32_t array_job_id = js->array_job_id;
	array_job_state_cached_t *next = NULL;

	if (ajs->next_job_id == job_id) {
		_log_array_job_chain(xahash_find_entry(cache_table, &job_id,
						       sizeof(job_id)),
				     __func__,
				     "removing singular chain for %pJ",
				     job_ptr);

		if (!xahash_free_entry(array_job_cache_table, &job_id,
				       sizeof(job_id)))
			fatal_abort("Unable to remove %pJ after just finding it",
				    ARRAY_JOB_STATE_MIMIC_RECORD(ajs));
		return;
	} else if (js->array_job_id == js->job_id) {
		_log_array_job_chain(js, __func__,
				     "skipping removal of meta which would orphan JobId=%u",
				     ajs->next_job_id);
		return;
	}

	next = ajs;

	while (next->next_job_id != job_id) {
		next = xahash_find_entry(array_job_cache_table,
					 &next->next_job_id,
					 sizeof(next->next_job_id));
		_check_array_job_magic(next);
	}

	xassert(next->next_job_id == job_id);

	_log_array_job_chain(js, __func__, "removing from link chain");

	next->next_job_id = ajs->next_job_id;
	ajs->next_job_id = job_id;

	_log_array_job_chain(js, __func__,
			     "array job chain removed for %pJ removal",
			     job_ptr);

	if (array_job_id > 0) {
		_log_array_job_chain(xahash_find_entry(cache_table,
						       &array_job_id,
						       sizeof(array_job_id)),
				     __func__,
				     "removed %pJ from meta link chain",
				     job_ptr);
		_check_array_job_magic_links(job_id, array_job_id, false);
	}

	_check_array_job_magic(ajs);
	_check_array_job_magic(next);

	if (!xahash_free_entry(array_job_cache_table, &job_id, sizeof(job_id)))
		fatal_abort("Unable to remove %pJ after just finding it",
			    ARRAY_JOB_STATE_MIMIC_RECORD(ajs));

	/* check for meta that only exists for this job */
	if (array_job_id && (next->job_id == next->next_job_id)) {
		job_state_cached_t *meta_js =
			xahash_find_entry(cache_table, &array_job_id,
					  sizeof(array_job_id));

		/* should only be the meta in chain left */
		xassert(next->job_id == array_job_id);
		xassert(next->next_job_id == array_job_id);

		if (meta_js) {
			_log_array_job_chain(meta_js, __func__,
					     "keeping meta job in chain after %pJ removal",
					     job_ptr);
		} else {
			/*
			 * if there is no JS for the meta job, then this chain
			 * is only a placeholder that needs to be removed
			 */

			_log_array_job_chain(js, __func__,
					     "removing meta job placeholder in chain after %pJ removal",
					     job_ptr);

			/* prune meta job that only exist to avoid orphans */
			if (!xahash_free_entry(array_job_cache_table,
					       &array_job_id,
					       sizeof(array_job_id)))
				fatal_abort("[JobId=%u] Unable to remove array meta job placeholder link",
					    array_job_id);
		}
	}
}

static void _on_array_job_removal(const job_record_t *job_ptr,
				  job_state_cached_t *js)
{
	array_job_state_cached_t *ajs;

	/*
	 * Need to use the cached array_task_id and not the potentially changed
	 * job_ptr->array_task_id to remove the task cache
	 */

	xassert(js);
	xassert(js->job_id == job_ptr->job_id);

	if ((ajs = xahash_find_entry(array_job_cache_table, &js->job_id,
				     sizeof(js->job_id)))) {
		_check_array_job_magic(ajs);
		xassert(ajs->job_id == job_ptr->job_id);

		_check_array_job_magic_links(js->job_id, js->array_job_id,
					     true);
		_unlink_array_job(job_ptr, js, ajs);
	} else {
		xassert(!js->array_job_id);
	}

	if (js->array_job_id > 0) {
		if (!xahash_free_entry(
			    array_task_cache_table,
			    ARRAY_TASK_STATE_KEY_JOB_ID(js->array_job_id,
							js->array_task_id),
			    ARRAY_TASK_STATE_KEY_BYTES))
			fatal_abort("[%pJ] array task cache not found",
				    JOB_STATE_MIMIC_RECORD(js));

		LOG("[%pJ] array task cache removed for %pJ",
		    JOB_STATE_MIMIC_RECORD(js), job_ptr);
	}

	if (_is_debug()) {
		array_task_state_cached_t *ats;

		ats = xahash_find_entry(array_task_cache_table,
					ARRAY_TASK_STATE_KEY_JOB_ID(
						js->job_id, js->array_task_id),
					ARRAY_TASK_STATE_KEY_BYTES);
		ajs = xahash_find_entry(array_job_cache_table, &job_ptr->job_id,
					sizeof(job_ptr->job_id));

		if (ats)
			fatal_abort("found array task when there should not be one: %pJ",
				    ARRAY_TASK_STATE_MIMIC_RECORD(ats));
		if (ajs && (js->job_id != js->array_job_id))
			fatal_abort("found array job link when there should not be one: %pJ",
				    ARRAY_JOB_STATE_MIMIC_RECORD(ajs));
	}
}

static void _on_array_job_change(const job_record_t *job_ptr,
				 job_state_cached_t *js)
{
	xassert(!js->het_job_id);

	/*
	 * Array IDs can change during meta array job's life.
	 * This could cause any of the existing array array_job_id/task_id ->
	 * job_id or array_job chain to be incorrect and they need to be
	 * rebuilt.
	 */

	if ((js->array_task_id != job_ptr->array_task_id) ||
	    (js->array_job_id != job_ptr->array_job_id)) {
		/* task id should only ever change from meta to a numeric */
		xassert(js->array_task_id == NO_VAL);
		xassert(!js->het_job_id);
		xassert(!job_ptr->het_job_id);
		xassert(job_ptr->array_job_id > 0);
		LOG("[%pJ] changed array_task_id=%u->%u array_job_id=%u->%u",
		    JOB_STATE_MIMIC_RECORD(js), js->array_task_id,
		    job_ptr->array_task_id, js->array_job_id,
		    job_ptr->array_job_id);

		/* Remove task cache but leave the meta job link intact */
		if (js->array_job_id &&
		    !xahash_free_entry(
			    array_task_cache_table,
			    ARRAY_TASK_STATE_KEY_JOB_ID(js->array_job_id,
							js->array_task_id),
			    ARRAY_TASK_STATE_KEY_BYTES))
			fatal_abort("[%pJ] array task cache not found",
				    JOB_STATE_MIMIC_RECORD(js));
	}

	xassert(js->job_id == job_ptr->job_id);
	js->array_task_id = job_ptr->array_task_id;
	js->array_job_id = job_ptr->array_job_id;

	_sync_job_task_id_bitmap(job_ptr, js);
	_link_array_job(job_ptr, js);
}

extern void on_job_state_change(job_record_t *job_ptr, uint32_t new_state)
{
	DEF_DEBUG_TIMER;
	job_state_cached_t *js = NULL;
	const uint32_t job_id = job_ptr->job_id;

	/*
	 * In order to avoid the time cost, we are not taking the cache_lock
	 * to check if the cache_table pointer is !NULL as the cost of hitting
	 * an invalid cached state here is very minor.
	 */
	if (!cache_table)
		return;

	if (!job_id)
		return;

	START_DEBUG_TIMER;

	xassert(job_ptr->magic == JOB_MAGIC);

	slurm_rwlock_wrlock(&cache_lock);

	_check_all_jobs(false);

	if (new_state == NO_VAL) {
		js = xahash_find_entry(cache_table, &job_id, sizeof(job_id));

		if (js && (js->array_job_id > 0))
			_on_array_job_removal(job_ptr, js);

		if (xahash_free_entry(cache_table, &job_id, sizeof(job_id)))
			LOG("[%pJ] job state cache removed", job_ptr);
		else
			LOG("[%pJ] job state cache not found", job_ptr);

		_check_all_jobs(false);

		slurm_rwlock_unlock(&cache_lock);
		END_DEBUG_TIMER;
		return;
	}

	js = xahash_insert_entry(cache_table, &job_id, sizeof(job_id));
	xassert(js->magic == MAGIC_JOB_STATE_CACHED);

	if (_is_debug() && (js->job_state != new_state)) {
		char *before = job_state_string_complete(js->job_state);
		char *after = job_state_string_complete(job_ptr->job_state);

		LOG("[%pJ] changed state: %s -> %s",
		    JOB_STATE_MIMIC_RECORD(js), before, after);

		xfree(before);
		xfree(after);
	}

	js->job_state = new_state;

	if (job_ptr->array_job_id || js->array_job_id)
		_on_array_job_change(job_ptr, js);

	/*
	 * A het job is added to the job state cache after each component is
	 * created. Because this cache exists outside of the job read/write
	 * locks, that means a job state cache query can happen while a het
	 * job is being created, and het job id may not be set for each
	 * component yet. In that case, check that the het job state cache has
	 * not been initialized yet.
	 */

	if (_is_debug() && (js->het_job_id != job_ptr->het_job_id)) {
		xassert(!js->het_job_id);
		xassert(js->array_task_id == NO_VAL);
		xassert(!js->array_job_id);
		xassert(job_ptr->array_task_id == NO_VAL);
		xassert(!job_ptr->array_job_id);
		LOG("[%pJ] changed het_job_id=%u->%u",
		    JOB_STATE_MIMIC_RECORD(js), js->het_job_id,
		    job_ptr->het_job_id);
	}

	js->het_job_id = job_ptr->het_job_id;

	_check_all_jobs(false);

	slurm_rwlock_unlock(&cache_lock);
	END_DEBUG_TIMER;
}

static xahash_hash_t _hash(const void *key, const size_t key_bytes,
			   void *state_ptr)
{
	cache_table_state_t *state = state_ptr;
	const uint32_t *job_id_ptr = key;

	_check_state_magic(state);
	_check_job_id(job_id_ptr);
	xassert(sizeof(*job_id_ptr) == key_bytes);

	return *job_id_ptr % state->table_size;
}

static bool _match(void *entry, const void *key, const size_t key_bytes,
		   void *state_ptr)
{
	job_state_cached_t *js = entry;
	cache_table_state_t *state = state_ptr;
	const uint32_t *job_id_ptr = key;

	_check_job_magic(js);
	_check_state_magic(state);
	_check_job_id(job_id_ptr);
	xassert(sizeof(*job_id_ptr) == key_bytes);

	return (js->job_id == *job_id_ptr);
}

static void _on_insert(void *entry, const void *key, const size_t key_bytes,
		       void *state_ptr)
{
	job_state_cached_t *js = entry;
	const uint32_t *job_id_ptr = key;
	cache_table_state_t *state = state_ptr;

	_check_state_magic(state);
	_check_job_id(job_id_ptr);
	xassert(sizeof(*job_id_ptr) == key_bytes);

	*js = (job_state_cached_t) {
		ONLY_DEBUG(.magic = MAGIC_JOB_STATE_CACHED,)
		.job_id = *job_id_ptr,
		.job_state = NO_VAL,
		.array_task_id = NO_VAL,
	};

	LOG("%pJ inserted", JOB_STATE_MIMIC_RECORD(js));
	_check_job_magic(js);
}

static void _on_free(void *ptr, void *state_ptr)
{
	job_state_cached_t *js = ptr;
	cache_table_state_t *state = state_ptr;

	_check_job_magic(js);
	_check_state_magic(state);

	LOG("%pJ releasing", JOB_STATE_MIMIC_RECORD(js));

	FREE_NULL_BITMAP(js->task_id_bitmap);
	ONLY_DEBUG(memset(js, 0, sizeof(*js)));
	ONLY_DEBUG(js->magic = ~MAGIC_JOB_STATE_CACHED);
}

static bool _array_job_match(void *entry, const void *key,
			     const size_t key_bytes, void *state_ptr)
{
	array_job_state_cached_t *ajs = entry;
	cache_table_state_t *state = state_ptr;
	const uint32_t *job_id_ptr = key;

	_check_state_magic(state);
	_check_array_job_magic(ajs);
	_check_job_id(job_id_ptr);
	xassert(sizeof(*job_id_ptr) == key_bytes);

	return (ajs->job_id == *job_id_ptr);
}

static void _array_job_on_insert(void *entry, const void *key,
				 const size_t key_bytes, void *state_ptr)
{
	array_job_state_cached_t *ajs = entry;
	cache_table_state_t *state = state_ptr;
	const uint32_t *job_id_ptr = key;

	_check_state_magic(state);
	_check_job_id(job_id_ptr);
	xassert(sizeof(*job_id_ptr) == key_bytes);

	*ajs = (array_job_state_cached_t) {
		ONLY_DEBUG(.magic = MAGIC_ARRAY_JOB_STATE_CACHED,)
		.job_id = *job_id_ptr,
		.next_job_id = *job_id_ptr,
	};

	LOG("%pJ inserted", ARRAY_JOB_STATE_MIMIC_RECORD(ajs));
	_check_array_job_magic(ajs);
}

static void _array_job_on_free(void *ptr, void *state_ptr)
{
	array_job_state_cached_t *ajs = ptr;
	cache_table_state_t *state = state_ptr;

	_check_state_magic(state);
	_check_array_job_magic(ajs);

	LOG("%pJ released", ARRAY_JOB_STATE_MIMIC_RECORD(ajs));
	ONLY_DEBUG(memset(ajs, 0, sizeof(*ajs)));
	ONLY_DEBUG(ajs->magic = ~MAGIC_ARRAY_JOB_STATE_CACHED);
}

static xahash_hash_t _array_task_hash(const void *key, const size_t key_bytes,
				      void *state_ptr)
{
	uint64_t seed;
	cache_table_state_t *state = state_ptr;
	const array_task_state_cached_t *ats_key = key;

	_check_state_magic(state);
	_check_array_task_magic(ats_key);

	seed = ((uint64_t) ats_key->array_job_id) << 32;
	seed |= ats_key->array_task_id;

	return seed % state->table_size;
}

static bool _array_task_match(void *entry, const void *key,
			      const size_t key_bytes, void *state_ptr)
{
	array_task_state_cached_t *ats = entry;
	cache_table_state_t *state = state_ptr;
	const array_task_state_cached_t *ats_key = key;

	_check_state_magic(state);
	_check_array_task_magic(ats);
	_check_array_task_magic(ats_key);
	xassert(sizeof(*ats_key) == key_bytes);

	/* treat NO_VAL and INFINTE as * for arrays */
	if ((ats_key->array_task_id < NO_VAL) &&
	    (ats->array_task_id != ats_key->array_task_id))
		return false;

	return (ats->array_job_id == ats_key->array_job_id);
}

static void _array_task_on_insert(void *entry, const void *key,
				  const size_t key_bytes, void *state_ptr)
{
	array_task_state_cached_t *ats = entry;
	const array_task_state_cached_t *ats_key = key;
	cache_table_state_t *state = state_ptr;

	_check_state_magic(state);
	_check_array_task_magic(ats_key);
	xassert(sizeof(*ats_key) == key_bytes);

	*ats = *ats_key;

	LOG("%pJ inserted", ARRAY_TASK_STATE_MIMIC_RECORD(ats));
	_check_array_task_magic(ats);
}

static void _array_task_on_free(void *ptr, void *state_ptr)
{
	array_task_state_cached_t *ats = ptr;
	cache_table_state_t *state = state_ptr;

	_check_state_magic(state);
	_check_array_task_magic(ats);

	LOG("%pJ released", ARRAY_TASK_STATE_MIMIC_RECORD(ats));
	ONLY_DEBUG(memset(ats, 0, sizeof(*ats)));
	ONLY_DEBUG(ats->magic = ~MAGIC_ARRAY_TASK_STATE_CACHED);
}

extern void setup_job_state_hash(int new_hash_table_size)
{
	const cache_table_state_t nstate = (cache_table_state_t) {
		ONLY_DEBUG(.magic = MAGIC_CACHE_TABLE_STATE,)
		.table_size = new_hash_table_size,
	};

	LOG("Job state cache active with %d jobs in hash tables",
	    new_hash_table_size);

	slurm_rwlock_wrlock(&cache_lock);

	xassert(!cache_table);
	cache_table = xahash_new_table(_hash, _match, _on_insert, _on_free,
				       sizeof(cache_table_state_t),
				       sizeof(job_state_cached_t),
				       new_hash_table_size);
	*((cache_table_state_t *) xahash_get_state_ptr(cache_table)) = nstate;

	xassert(!array_job_cache_table);
	array_job_cache_table =
		xahash_new_table(_hash, _array_job_match,
				 _array_job_on_insert, _array_job_on_free,
				 sizeof(cache_table_state_t),
				 sizeof(array_job_state_cached_t),
				 new_hash_table_size);
	*((cache_table_state_t *) xahash_get_state_ptr(array_job_cache_table)) =
		nstate;

	xassert(!array_task_cache_table);
	array_task_cache_table =
		xahash_new_table(_array_task_hash, _array_task_match,
				 _array_task_on_insert, _array_task_on_free,
				 sizeof(cache_table_state_t),
				 sizeof(array_task_state_cached_t),
				 new_hash_table_size);
	*((cache_table_state_t *)
		  xahash_get_state_ptr(array_task_cache_table)) = nstate;

	slurm_rwlock_unlock(&cache_lock);
}
