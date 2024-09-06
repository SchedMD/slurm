/*****************************************************************************\
 *  delayed.c - definitions for delayed work in connection manager
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

#include <time.h>

#include "src/common/macros.h"
#include "src/common/read_config.h"
#include "src/common/slurm_time.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/conmgr/conmgr.h"
#include "src/conmgr/delayed.h"
#include "src/conmgr/mgr.h"

#ifdef __linux__
#define CLOCK_TYPE CLOCK_TAI
#else
#define CLOCK_TYPE CLOCK_REALTIME
#endif

typedef struct {
#define MAGIC_FOREACH_DELAYED_WORK 0xB233443A
	int magic; /* MAGIC_FOREACH_DELAYED_WORK */
	work_t *shortest;
	timespec_t time;
} foreach_delayed_work_t;

/* timer to trigger SIGALRM */
static timer_t timer = {0};
/* Mutex to protect timer */
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

static int _inspect_work(void *x, void *key);
static void _update_timer(work_t *shortest, const timespec_t time);
static bool _work_clear_time_delay(work_t *work);

/* mgr.mutex must be locked when calling this function */
extern void cancel_delayed_work(void)
{
	work_t *work;

	if (!mgr.delayed_work || list_is_empty(mgr.delayed_work))
		return;

	log_flag(CONMGR, "%s: cancelling %d delayed work",
		 __func__, list_count(mgr.delayed_work));

	/* run everything immediately but with cancelled status */
	while ((work = list_pop(mgr.delayed_work))) {
		work->status = CONMGR_WORK_STATUS_CANCELLED;
		handle_work(true, work);
	}
}

static timespec_t _get_time(void)
{
	timespec_t time;
	int rc;

	if ((rc = clock_gettime(CLOCK_TYPE, &time))) {
		if (rc == -1)
			rc = errno;

		fatal("%s: clock_gettime() failed: %s",
		      __func__, slurm_strerror(rc));
	}

	return time;
}

static list_t *_inspect(void)
{
	int count, total;
	work_t *work;
	list_t *elapsed = list_create(xfree_ptr);
	foreach_delayed_work_t dargs = {
		.magic = MAGIC_FOREACH_DELAYED_WORK,
		.time = _get_time(),
	};

	total = list_count(mgr.delayed_work);
	count = list_transfer_match(mgr.delayed_work, elapsed,
				    _inspect_work, &dargs);

	_update_timer(dargs.shortest, dargs.time);

	while ((work = list_pop(elapsed))) {
		if (!_work_clear_time_delay(work))
			fatal_abort("should never happen");

		handle_work(true, work);
	}

	log_flag(CONMGR, "%s: checked all timers and triggered %d/%d delayed work",
		 __func__, count, total);

	return elapsed;
}

static struct itimerspec _calc_timer(work_t *shortest,
				     const timespec_t time)
{
	struct itimerspec spec = {{0}};
	timespec_t begin = shortest->control.time_begin;

	spec.it_value.tv_sec = begin.tv_sec;

	if (begin.tv_sec <= 0)
		spec.it_value.tv_nsec = begin.tv_nsec;

	if (slurm_conf.debug_flags & DEBUG_FLAG_CONMGR) {
		int64_t remain_sec, remain_nsec;

		remain_sec = begin.tv_sec - time.tv_sec;
		if (remain_sec == 0) {
			remain_nsec = begin.tv_nsec - time.tv_nsec;
		} else if (remain_sec < 0) {
			remain_nsec = NO_VAL64;
		} else {
			remain_nsec = NO_VAL64;
		}

		log_flag(CONMGR, "%s: setting conmgr timer for %"PRId64"s %"PRId64"ns for %s()",
			 __func__, remain_sec,
			 (remain_nsec == NO_VAL64 ? 0 : remain_nsec),
			 shortest->callback.func_name);
	}

	return spec;
}

static void _update_timer(work_t *shortest, const timespec_t time)
{
	int rc;
	struct itimerspec spec = {{0}};

	if (shortest) {
		spec = _calc_timer(shortest, time);
	} else {
		log_flag(CONMGR, "%s: disabling conmgr timer", __func__);
	}

	slurm_mutex_lock(&mutex);
	rc = timer_settime(timer, TIMER_ABSTIME, &spec, NULL);
	slurm_mutex_unlock(&mutex);

	if (rc) {
		if ((rc == -1) && errno)
			rc = errno;

		error("%s: timer_set_time() failed: %s",
		      __func__, slurm_strerror(rc));
	}
}

/* check begin times to see if the work delay has elapsed */
static int _inspect_work(void *x, void *key)
{
	bool trigger;
	work_t *work = x;
	const timespec_t begin = work->control.time_begin;
	foreach_delayed_work_t *args = key;
	const timespec_t time = args->time;
	const int64_t remain_sec = begin.tv_sec - time.tv_sec;
	int64_t remain_nsec;

	xassert(args->magic == MAGIC_FOREACH_DELAYED_WORK);
	xassert(work->magic == MAGIC_WORK);

	if (remain_sec == 0) {
		remain_nsec = begin.tv_nsec - time.tv_nsec;
		trigger = (remain_nsec <= 0);
	} else if (remain_sec < 0) {
		trigger = true;
		remain_nsec = NO_VAL64;
	} else {
		remain_nsec = NO_VAL64;
		trigger = false;
	}

	log_flag(CONMGR, "%s: %s delayed work ETA %"PRId64"s %"PRId64"ns for %s@0x%"PRIxPTR,
		 __func__, (trigger ? "triggering" : "deferring"),
		 remain_sec,
		 (remain_nsec == NO_VAL64 ? 0 : remain_nsec),
		 work->callback.func_name,
		 (uintptr_t) work->callback.func);

	if (!args->shortest) {
		args->shortest = work;
	} else {
		const timespec_t shortest_begin =
			args->shortest->control.time_begin;

		if (shortest_begin.tv_sec == begin.tv_sec) {
			if (shortest_begin.tv_nsec > begin.tv_nsec)
				args->shortest = work;
		} else if (shortest_begin.tv_sec > begin.tv_sec) {
			args->shortest = work;
		}
	}

	return trigger ? 1 : 0;
}

extern timespec_t conmgr_calc_work_time_delay(
	time_t delay_seconds,
	long delay_nanoseconds)
{
	const timespec_t time = _get_time();

	/*
	 * Renormalize ns into seconds to only have partial seconds in
	 * nanoseconds. Nanoseconds won't matter with a larger number of
	 * seconds.
	 */
	delay_seconds += delay_nanoseconds / NSEC_IN_SEC;
	delay_nanoseconds = delay_nanoseconds % NSEC_IN_SEC;

	/* catch integer overflows */
	xassert((delay_seconds + time.tv_sec) >= time.tv_sec);

	return (timespec_t) {
		.tv_sec = (delay_seconds + time.tv_sec),
		.tv_nsec = delay_nanoseconds,
	};
}

extern void init_delayed_work(void)
{
	int rc;

	mgr.delayed_work = list_create(xfree_ptr);

again:
	slurm_mutex_lock(&mutex);
	{
		struct sigevent sevp = {
			.sigev_notify = SIGEV_SIGNAL,
			.sigev_signo = SIGALRM,
			.sigev_value.sival_ptr = &timer,
		};

		rc = timer_create(CLOCK_TYPE, &sevp, &timer);
	}
	slurm_mutex_unlock(&mutex);

	if (!rc)
		return;

	if ((rc == -1) && errno)
		rc = errno;

	if (rc == EAGAIN)
		goto again;
	else if (rc)
		fatal("%s: timer_create() failed: %s",
		      __func__, slurm_strerror(rc));
}

extern void free_delayed_work(void)
{
	int rc;

	if (!mgr.delayed_work)
		return;

	FREE_NULL_LIST(mgr.delayed_work);

	slurm_mutex_lock(&mutex);
	rc = timer_delete(timer);
	slurm_mutex_unlock(&mutex);

	if (rc)
		fatal("%s: timer_delete() failed: %m", __func__);
}

static void _update_delayed_work(bool locked)
{
	list_t *elapsed = NULL;

	if (!locked)
		slurm_mutex_lock(&mgr.mutex);

	elapsed = _inspect();

	if (!locked)
		slurm_mutex_unlock(&mgr.mutex);

	FREE_NULL_LIST(elapsed);
}

extern void on_signal_alarm(conmgr_callback_args_t conmgr_args, void *arg)
{
	log_flag(CONMGR, "%s: caught SIGALRM", __func__);
	_update_delayed_work(false);
}

/*
 * Clear time delay dependency from work
 * IN work - work to remove CONMGR_WORK_DEP_TIME_DELAY flag
 * NOTE: caller must call update_timer() after to cause work to requeue
 * NOTE: caller must hold mgr.mutex lock
 * RET True if time delay removed
 */
static bool _work_clear_time_delay(work_t *work)
{
	xassert(work->magic == MAGIC_WORK);

	if (work->status != CONMGR_WORK_STATUS_PENDING)
		return false;

	if (!(work->control.depend_type & CONMGR_WORK_DEP_TIME_DELAY))
		return false;

#ifndef NDEBUG
	work->control.time_begin = (timespec_t) {0};
#endif /* !NDEBUG */
	work_mask_depend(work, ~CONMGR_WORK_DEP_TIME_DELAY);

	return true;
}

extern void add_work_delayed(work_t *work)
{
	list_append(mgr.delayed_work, work);
	_update_delayed_work(true);
}

extern char *work_delayed_to_str(work_t *work)
{
	const timespec_t time = _get_time();
	uint32_t diff, days, hours, minutes, seconds, nanoseconds;
	char *delay = NULL;

	if (!(work->control.depend_type & CONMGR_WORK_DEP_TIME_DELAY))
		return NULL;

	diff = work->control.time_begin.tv_sec - time.tv_sec;

	days = diff / (DAY_HOURS * HOUR_SECONDS);
	diff = diff % (DAY_HOURS * HOUR_SECONDS);

	hours = diff / HOUR_SECONDS;
	diff = diff % HOUR_SECONDS;

	minutes = diff / MINUTE_SECONDS;
	diff = diff % MINUTE_SECONDS;

	seconds = diff;

	if (!seconds)
		nanoseconds = work->control.time_begin.tv_nsec;
	else
		nanoseconds = (work->control.time_begin.tv_nsec - time.tv_nsec);

	xstrfmtcat(delay, " time_begin=%u-%u:%u:%u.%u",
		   days, hours, minutes, seconds, nanoseconds);

	return delay;
}
