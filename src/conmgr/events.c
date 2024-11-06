/*****************************************************************************\
 *  events.c - Definitions for event handlers
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

#include <pthread.h>

#include "src/common/macros.h"
#include "src/common/read_config.h"
#include "src/common/timers.h"
#include "src/common/xassert.h"
#include <time.h>

#include "src/conmgr/events.h"

static void _wait_pending(event_signal_t *event, const char *caller)
{
	log_flag(CONMGR, "%s->%s: [EVENT:%s] wait skipped due to %d pending reliable signals",
		 caller, __func__, event->name, event->pending);

	xassert(!event->waiting);

	event->pending--;

	xassert(event->pending >= 0);
}

static void _wait(event_signal_t *event, pthread_mutex_t *mutex,
		  const struct timespec max_sleep, const char *caller)
{
	DEF_TIMERS;

	if (slurm_conf.debug_flags & DEBUG_FLAG_CONMGR) {
		START_TIMER;

		log_flag(CONMGR, "%s->%s: [EVENT:%s] BEGIN wait with %d other waiters",
			 caller, __func__, event->name, event->waiting);
	}

	event->waiting++;

	xassert(event->waiting > 0);

	if (max_sleep.tv_nsec || max_sleep.tv_sec)
		slurm_cond_timedwait(&event->cond, mutex, &max_sleep);
	else
		slurm_cond_wait(&event->cond, mutex);

	event->waiting--;

	xassert(event->waiting >= 0);
	xassert(!event->pending);

	if (slurm_conf.debug_flags & DEBUG_FLAG_CONMGR) {
		/* we want the time but not to warn about a time limit */
		END_TIMER3(NULL, 0);

		log_flag(CONMGR, "%s->%s: [EVENT:%s] END waited after %s with %d other pending waiters",
			 caller, __func__, event->name, TIME_STR,
			 event->waiting);
	}
}

extern void event_wait_now(event_signal_t *event, pthread_mutex_t *mutex,
			   const struct timespec max_sleep, const char *caller)
{
	if (event->pending)
		_wait_pending(event, caller);
	else
		_wait(event, mutex, max_sleep, caller);
}

static void _broadcast(event_signal_t *event, const char *caller)
{
	if (!event->waiting) {
		log_flag(CONMGR, "%s->%s: [EVENT:%s] broadcast skipped due to 0 waiters with %d pending signals",
			 caller, __func__, event->name, event->pending);
		return;
	}

	/* cant have pending signals when there are waiters */
	xassert(!event->pending);

	log_flag(CONMGR, "%s->%s: [EVENT:%s] broadcasting to all %d waiters",
		 caller, __func__, event->name, event->pending);

	slurm_cond_broadcast(&event->cond);
}

static void _signal_waiting(event_signal_t *event, const char *caller)
{
	/* cant have pending signals when there are waiters */
	xassert(!event->pending);

	log_flag(CONMGR, "%s->%s: [EVENT:%s] sending signal to 1/%d waiters",
		 caller, __func__, event->name, event->waiting);

	slurm_cond_signal(&event->cond);
}

static void _signal_no_waiting(event_signal_t *event, const char *caller)
{
	xassert(event->pending >= 0);

	if (event->pending) {
		log_flag(CONMGR, "%s->%s: [EVENT:%s] skipping signal to 0 waiters with %d signals pending",
			 caller, __func__, event->name, event->pending);
	} else {
		log_flag(CONMGR, "%s->%s: [EVENT:%s] enqueuing signal to 0 waiters with 0 signals pending",
			 caller, __func__, event->name);
		event->pending++;
	}
}

extern void event_signal_now(bool broadcast, event_signal_t *event,
			     const char *caller)
{
	if (broadcast) {
		_broadcast(event, caller);
	} else if (!event->waiting) {
		/* signal only with no waiters */
		_signal_no_waiting(event, caller);
	} else {
		/* signal only with waiters */
		_signal_waiting(event, caller);
	}
}
