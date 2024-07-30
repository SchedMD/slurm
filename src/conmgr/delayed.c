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

#include "src/common/read_config.h"
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
	struct timespec time;
} foreach_delayed_work_t;

/* timer to trigger SIGALRM */
static timer_t timer = {0};

static int _inspect_work(void *x, void *key);
static void _update_timer(work_t *shortest, const struct timespec time);

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

static struct timespec _get_time(void)
{
	struct timespec time;
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
		if (!work_clear_time_delay(work))
			fatal_abort("should never happen");

		handle_work(true, work);
	}

	log_flag(CONMGR, "%s: checked all timers and triggered %d/%d delayed work",
		 __func__, count, total);

	return elapsed;
}

static void _update_timer(work_t *shortest, const struct timespec time)
{
	int rc;
	struct itimerspec spec = {{0}};

	if (shortest) {
		const conmgr_work_time_begin_t begin =
			shortest->control.time_begin;

		spec.it_value.tv_sec = begin.seconds;

		if (begin.seconds <= 0)
			spec.it_value.tv_nsec = begin.nanoseconds;

		if (slurm_conf.debug_flags & DEBUG_FLAG_CONMGR) {
			int64_t remain_sec, remain_nsec;

			remain_sec = begin.seconds - time.tv_sec;
			if (remain_sec == 0) {
				remain_nsec = begin.nanoseconds - time.tv_nsec;
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
	} else {
		log_flag(CONMGR, "%s: disabling conmgr timer", __func__);
	}

	if ((rc = timer_settime(timer, TIMER_ABSTIME, &spec, NULL))) {
		if ((rc == -1) && errno)
			rc = errno;
	}
}

/* check begin times to see if the work delay has elapsed */
static int _inspect_work(void *x, void *key)
{
	bool trigger;
	work_t *work = x;
	const conmgr_work_time_begin_t begin = work->control.time_begin;
	foreach_delayed_work_t *args = key;
	const struct timespec time = args->time;
	const int64_t remain_sec = begin.seconds - time.tv_sec;
	int64_t remain_nsec;

	xassert(args->magic == MAGIC_FOREACH_DELAYED_WORK);
	xassert(work->magic == MAGIC_WORK);

	if (remain_sec == 0) {
		remain_nsec = begin.nanoseconds - time.tv_nsec;
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
		const conmgr_work_time_begin_t shortest_begin =
			args->shortest->control.time_begin;

		if (shortest_begin.seconds == begin.seconds) {
			if (shortest_begin.nanoseconds > begin.nanoseconds)
				args->shortest = work;
		} else if (shortest_begin.seconds > begin.seconds) {
			args->shortest = work;
		}
	}

	return trigger ? 1 : 0;
}

extern conmgr_work_time_begin_t conmgr_calc_work_time_delay(
	time_t delay_seconds,
	long delay_nanoseconds)
{
	const struct timespec time = _get_time();

	/*
	 * Renormalize ns into seconds to only have partial seconds in
	 * nanoseconds. Nanoseconds won't matter with a larger number of
	 * seconds.
	 */
	delay_seconds += delay_nanoseconds / NSEC_IN_SEC;
	delay_nanoseconds = delay_nanoseconds % NSEC_IN_SEC;

	/* catch integer overflows */
	xassert((delay_seconds + time.tv_sec) >= time.tv_sec);

	return (conmgr_work_time_begin_t) {
		.seconds = (delay_seconds + time.tv_sec),
		.nanoseconds = delay_nanoseconds,
	};
}

extern void init_delayed_work(void)
{
	int rc;
	struct sigevent sevp = {
		.sigev_notify = SIGEV_SIGNAL,
		.sigev_signo = SIGALRM,
		.sigev_value.sival_ptr = &timer,
	};

	mgr.delayed_work = list_create(xfree_ptr);

again:
	if ((rc = timer_create(CLOCK_TYPE, &sevp, &timer))) {
		if ((rc == -1) && errno)
			rc = errno;

		if (rc == EAGAIN)
			goto again;
		else if (rc)
			fatal("%s: timer_create() failed: %s",
			      __func__, slurm_strerror(rc));
	}
}

extern void free_delayed_work(void)
{
	if (!mgr.delayed_work)
		return;

	FREE_NULL_LIST(mgr.delayed_work);
	if (timer_delete(timer))
		fatal("%s: timer_delete() failed: %m", __func__);
}

extern void update_delayed_work(bool locked)
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
	update_delayed_work(false);
}

extern bool work_clear_time_delay(work_t *work)
{
	xassert(work->magic == MAGIC_WORK);

	if (work->status != CONMGR_WORK_STATUS_PENDING)
		return false;

	if (!(work->control.depend_type & CONMGR_WORK_DEP_TIME_DELAY))
		return false;

#ifndef NDEBUG
	work->control.time_begin = (conmgr_work_time_begin_t) {0};
#endif /* !NDEBUG */
	work_mask_depend(work, ~CONMGR_WORK_DEP_TIME_DELAY);

	return true;
}

extern void add_work_delayed(work_t *work)
{
	list_append(mgr.delayed_work, work);
	update_delayed_work(true);
}

extern char *work_delayed_to_str(work_t *work)
{
	const struct timespec time = _get_time();
	uint32_t diff, days, hours, minutes, seconds, nanoseconds;
	char *delay = NULL;

	if (!(work->control.depend_type & CONMGR_WORK_DEP_TIME_DELAY))
		return NULL;

	diff = work->control.time_begin.seconds - time.tv_sec;

	days = diff / (DAY_HOURS * HOUR_SECONDS);
	diff = diff % (DAY_HOURS * HOUR_SECONDS);

	hours = diff / HOUR_SECONDS;
	diff = diff % HOUR_SECONDS;

	minutes = diff / MINUTE_SECONDS;
	diff = diff % MINUTE_SECONDS;

	seconds = diff;

	if (!seconds)
		nanoseconds = work->control.time_begin.nanoseconds;
	else
		nanoseconds = (work->control.time_begin.nanoseconds -
			       time.tv_nsec);

	xstrfmtcat(delay, " time_begin=%u-%u:%u:%u.%u",
		   days, hours, minutes, seconds, nanoseconds);

	return delay;
}
