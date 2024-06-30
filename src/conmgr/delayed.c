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

#include "src/common/read_config.h"
#include "src/common/xmalloc.h"

#include "src/conmgr/conmgr.h"
#include "src/conmgr/mgr.h"

#define MAGIC_FOREACH_DELAYED_WORK 0xB233443A

typedef struct {
	int magic; /* MAGIC_FOREACH_DELAYED_WORK */
	work_t *shortest;
} foreach_delayed_work_t;

/* mgr.mutex must be locked when calling this function */
extern void cancel_delayed_work(void)
{
	if (mgr.delayed_work && !list_is_empty(mgr.delayed_work)) {
		work_t *work;

		log_flag(CONMGR, "%s: cancelling %d delayed work",
			 __func__, list_count(mgr.delayed_work));

		/* run everything immediately but with cancelled status */
		while ((work = list_pop(mgr.delayed_work))) {
			work->status = CONMGR_WORK_STATUS_CANCELLED;
			handle_work(true, work);
		}
	}
}

extern void update_last_time(bool locked)
{
	int rc;

	if (!locked)
		slurm_mutex_lock(&mgr.mutex);

	if (!mgr.delayed_work) {
		struct sigevent sevp = {
			.sigev_notify = SIGEV_SIGNAL,
			.sigev_signo = SIGALRM,
			.sigev_value.sival_ptr = &mgr.timer,
		};

		mgr.delayed_work = list_create(xfree_ptr);

again:
		if ((rc = timer_create(CLOCK_MONOTONIC, &sevp, &mgr.timer))) {
			if ((rc == -1) && errno)
				rc = errno;

			if (rc == EAGAIN)
				goto again;
			else if (rc)
				fatal("%s: timer_create() failed: %s",
				      __func__, slurm_strerror(rc));
		}
	}

	if ((rc = clock_gettime(CLOCK_MONOTONIC, &mgr.last_time))) {
		if (rc == -1)
			rc = errno;

		fatal("%s: clock_gettime() failed: %s",
		      __func__, slurm_strerror(rc));
	}

	if (!locked)
		slurm_mutex_unlock(&mgr.mutex);
}

static int _foreach_delayed_work(void *x, void *arg)
{
	work_t *work = x;
	foreach_delayed_work_t *args = arg;

	xassert(args->magic == MAGIC_FOREACH_DELAYED_WORK);
	xassert(work->magic == MAGIC_WORK);

	if (slurm_conf.debug_flags & DEBUG_FLAG_CONMGR) {
		int64_t remain_sec, remain_nsec;

		remain_sec = work->begin.seconds - mgr.last_time.tv_sec;
		if (remain_sec == 0) {
			remain_nsec =
				work->begin.nanoseconds - mgr.last_time.tv_nsec;
		} else if (remain_sec < 0) {
			remain_nsec = NO_VAL64;
		} else {
			remain_nsec = NO_VAL64;
		}

		log_flag(CONMGR, "%s: evaluating delayed work ETA %"PRId64"s %"PRId64"ns for %s@0x%"PRIxPTR,
			 __func__, remain_sec,
			 (remain_nsec == NO_VAL64 ? 0 : remain_nsec),
			 work->tag, (uintptr_t) work->func);
	}

	if (!args->shortest) {
		args->shortest = work;
		return SLURM_SUCCESS;
	}

	if (args->shortest->begin.seconds == work->begin.seconds) {
		if (args->shortest->begin.nanoseconds > work->begin.nanoseconds)
			args->shortest = work;
	} else if (args->shortest->begin.seconds > work->begin.seconds) {
		args->shortest = work;
	}

	return SLURM_SUCCESS;
}

extern void update_timer(bool locked)
{
	int rc;
	struct itimerspec spec = {{0}};

	foreach_delayed_work_t args = {
		.magic = MAGIC_FOREACH_DELAYED_WORK,
	};

	if (!locked)
		slurm_mutex_lock(&mgr.mutex);

	if (slurm_conf.debug_flags & DEBUG_FLAG_CONMGR) {
		/* get updated clock for logging but not needed otherwise */
		update_last_time(true);
	}

	list_for_each(mgr.delayed_work, _foreach_delayed_work, &args);

	if (args.shortest) {
		work_t *work = args.shortest;

		spec.it_value.tv_sec = work->begin.seconds;
		spec.it_value.tv_nsec = work->begin.nanoseconds;

		if (slurm_conf.debug_flags & DEBUG_FLAG_CONMGR) {
			int64_t remain_sec, remain_nsec;

			remain_sec = work->begin.seconds - mgr.last_time.tv_sec;
			if (remain_sec == 0) {
				remain_nsec = work->begin.nanoseconds -
					      mgr.last_time.tv_nsec;
			} else if (remain_sec < 0) {
				remain_nsec = NO_VAL64;
			} else {
				remain_nsec = NO_VAL64;
			}

			log_flag(CONMGR, "%s: setting conmgr timer for %"PRId64"s %"PRId64"ns for %s@0x%"PRIxPTR,
				 __func__, remain_sec,
				 (remain_nsec == NO_VAL64 ? 0 : remain_nsec),
				 work->tag, (uintptr_t) work->func);
		}
	} else {
		log_flag(CONMGR, "%s: disabling conmgr timer", __func__);
	}

	if ((rc = timer_settime(mgr.timer, TIMER_ABSTIME, &spec, NULL))) {
		if ((rc == -1) && errno)
			rc = errno;
	}

	if (!locked)
		slurm_mutex_unlock(&mgr.mutex);
}

/* check begin times to see if the work delay has elapsed */
static int _match_work_elapsed(void *x, void *key)
{
	bool trigger;
	work_t *work = x;
	int64_t remain_sec, remain_nsec;

	xassert(work->magic == MAGIC_WORK);

	remain_sec = work->begin.seconds - mgr.last_time.tv_sec;
	if (remain_sec == 0) {
		remain_nsec = work->begin.nanoseconds - mgr.last_time.tv_nsec;
		trigger = (remain_nsec <= 0);
	} else if (remain_sec < 0) {
		trigger = true;
		remain_nsec = NO_VAL64;
	} else {
		remain_nsec = NO_VAL64;
		trigger = false;
	}

	log_flag(CONMGR, "%s: %s %s@0x%"PRIxPTR" ETA in %"PRId64"s %"PRId64"ns",
		 __func__, (trigger ? "triggering" : "deferring"),
		 work->tag, (uintptr_t) work->func,
		 remain_sec, (remain_nsec == NO_VAL64 ? 0 : remain_nsec));

	return trigger ? 1 : 0;
}

extern void conmgr_add_delayed_work(conmgr_fd_t *con, conmgr_work_func_t func,
				    time_t seconds, long nanoseconds, void *arg,
				    const char *tag)
{
	work_t *work;

	/*
	 * Renormalize ns into seconds to only have partial seconds in
	 * nanoseconds. Nanoseconds won't matter with a larger number of
	 * seconds.
	 */
	seconds += nanoseconds / NSEC_IN_SEC;
	nanoseconds = nanoseconds % NSEC_IN_SEC;

	work = xmalloc(sizeof(*work));
	*work = (work_t){
		.magic = MAGIC_WORK,
		.con = con,
		.func = func,
		.arg = arg,
		.tag = tag,
		.status = CONMGR_WORK_STATUS_PENDING,
		.begin.seconds = seconds,
		.begin.nanoseconds = nanoseconds,
	};

	if (con)
		work->type = CONMGR_WORK_TYPE_CONNECTION_DELAY_FIFO;
	else
		work->type = CONMGR_WORK_TYPE_TIME_DELAY_FIFO;

	log_flag(CONMGR, "%s: adding %lds %ldns delayed work %s@0x%"PRIxPTR,
		 __func__, seconds, nanoseconds, work->tag,
		 (uintptr_t) work->func);

	handle_work(false, work);
}

extern void free_delayed_work(void)
{
	if (!mgr.delayed_work)
		return;

	FREE_NULL_LIST(mgr.delayed_work);
	if (timer_delete(mgr.timer))
		fatal("%s: timer_delete() failed: %m", __func__);
}

static void _handle_timer(void *x)
{
	int count, total;
	work_t *work;
	list_t *elapsed = list_create(xfree_ptr);

	slurm_mutex_lock(&mgr.mutex);
	update_last_time(true);

	total = list_count(mgr.delayed_work);
	count = list_transfer_match(mgr.delayed_work, elapsed,
				    _match_work_elapsed, NULL);

	update_timer(true);

	while ((work = list_pop(elapsed))) {
		work->status = CONMGR_WORK_STATUS_RUN;
		handle_work(true, work);
	}

	if (count > 0)
		signal_change(true);
	slurm_mutex_unlock(&mgr.mutex);

	log_flag(CONMGR, "%s: checked all timers and triggered %d/%d delayed work",
		 __func__, count, total);

	FREE_NULL_LIST(elapsed);
}

extern void on_signal_alarm(conmgr_fd_t *con, conmgr_work_type_t type,
			    conmgr_work_status_t status, const char *tag,
			    void *arg)
{
	log_flag(CONMGR, "%s: caught SIGALRM", __func__);
	queue_func(false, _handle_timer, NULL, XSTRINGIFY(_handle_timer));
	signal_change(false);
}
