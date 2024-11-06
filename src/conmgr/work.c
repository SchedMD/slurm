/*****************************************************************************\
 *  work.c - definitions for work in connection manager
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

#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/common/proc_args.h"
#include "src/common/read_config.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/conmgr/conmgr.h"
#include "src/conmgr/delayed.h"
#include "src/conmgr/events.h"
#include "src/conmgr/mgr.h"
#include "src/conmgr/signals.h"

static struct {
	conmgr_work_status_t status;
	const char *string;
} statuses[] = {
	{ CONMGR_WORK_STATUS_INVALID, "INVALID" },
	{ CONMGR_WORK_STATUS_PENDING, "PENDING" },
	{ CONMGR_WORK_STATUS_RUN, "RUN" },
	{ CONMGR_WORK_STATUS_CANCELLED, "CANCELLED" },
};

static struct {
	conmgr_work_sched_t type;
	const char *string;
} sched_types[] = {
	{ CONMGR_WORK_SCHED_FIFO , "FIFO" },
};

static struct {
	conmgr_work_depend_t type;
	const char *string;
} dep_types[] = {
	{ CONMGR_WORK_DEP_NONE, "NONE" },
	{ CONMGR_WORK_DEP_CON_WRITE_COMPLETE, "CONNECTION_WRITE_COMPLETE" },
	{ CONMGR_WORK_DEP_TIME_DELAY, "TIME_DELAY" },
	{ CONMGR_WORK_DEP_SIGNAL, "SIGNAL" },
};

extern const char *conmgr_work_status_string(conmgr_work_status_t status)
{
	for (int i = 0; i < ARRAY_SIZE(statuses); i++)
		if (statuses[i].status == status)
			return statuses[i].string;

	fatal_abort("%s: invalid work status 0x%x", __func__, status);
}

extern char *conmgr_work_sched_string(conmgr_work_sched_t type)
{
	char *str = NULL, *at = NULL;

	for (int i = 0; i < ARRAY_SIZE(sched_types); i++)
		if ((sched_types[i].type & type) == sched_types[i].type)
			xstrfmtcatat(str, &at, "%s%s", (str ? "|" : ""),
				     sched_types[i].string);

	if (str)
		return str;

	fatal_abort("%s: invalid work sched_type: 0x%x", __func__, type);
}

extern char *conmgr_work_depend_string(conmgr_work_depend_t type)
{
	char *str = NULL, *at = NULL;

	for (int i = 0; i < ARRAY_SIZE(dep_types); i++)
		if ((dep_types[i].type & type) == dep_types[i].type)
			xstrfmtcatat(str, &at, "%s%s", (str ? "|" : ""),
				     dep_types[i].string);

	if (str)
		return str;

	fatal_abort("%s: invalid work depend_type: 0x%x", __func__, type);
}

static void _log_work(work_t *work, const char *caller, const char *fmt, ...)
{
	char *con_name = NULL, *depend = NULL, *sched = NULL, *fmtstr = NULL;
	char *delay = NULL, *signal = NULL, *callback = NULL;
	const char *status = NULL;

	if (!(slurm_conf.debug_flags & DEBUG_FLAG_CONMGR))
		return;

	if (work->con)
		xstrfmtcat(con_name, " [%s]", work->con->name);

	if (work->callback.func)
		xstrfmtcat(callback, "callback=%s(arg=0x%"PRIxPTR") ",
			   work->callback.func_name,
			   (uintptr_t) work->callback.arg);

	status = conmgr_work_status_string(work->status);

	if (work->control.depend_type & CONMGR_WORK_DEP_SIGNAL) {
		char *signame = sig_num2name(work->control.on_signal_number);
		xstrfmtcat(signal, " signal=%s[%d]",
			   signame, work->control.on_signal_number);
		xfree(signame);
	}

	delay = work_delayed_to_str(work);
	depend = conmgr_work_depend_string(work->control.depend_type);
	sched = conmgr_work_sched_string(work->control.schedule_type);

	if (fmt) {
		va_list ap;

		va_start(ap, fmt);
		fmtstr = vxstrfmt(fmt, ap);
		va_end(ap);
	}

	log_flag(CONMGR, "%s->%s:%s work=0x%"PRIxPTR" status=%s %ssched=%s depend=%s%s%s%s%s%s",
		 caller, __func__, (con_name ? con_name : ""), (uintptr_t) work,
		 status,
		 (callback ? callback : ""),
		 sched, depend,
		 (signal ? signal : ""),
		 (delay ? " " : ""),
		 (delay ? delay : ""),
		 (fmtstr ? " -> " : ""),
		 (fmtstr ? fmtstr : ""));

	xfree(con_name);
	xfree(depend);
	xfree(sched);
	xfree(delay);
	xfree(signal);
	xfree(callback);
	xfree(fmtstr);
}

extern void wrap_work(work_t *work)
{
	conmgr_fd_t *con = work->con;

	xassert(work->magic == MAGIC_WORK);

	_log_work(work, __func__, "BEGIN");

	work->callback.func((conmgr_callback_args_t) {
				.con = work->con,
				.status = work->status,
			    }, work->callback.arg);

	_log_work(work, __func__, "END");

	if (con) {
		slurm_mutex_lock(&mgr.mutex);
		con_unset_flag(con, FLAG_WORK_ACTIVE);
		/* con may be xfree()ed any time once lock is released */

		EVENT_SIGNAL(&mgr.watch_sleep);
		slurm_mutex_unlock(&mgr.mutex);
	}

	work->magic = ~MAGIC_WORK;
	xfree(work);
}

/*
 * Add work to mgr.work
 * Single point to enqueue internal function callbacks
 * NOTE: _handle_work_run() can add new entries to mgr.work
 *
 * IN work - pointer to work to run
 * NOTE: never add a thread that will never return or conmgr_fini() will never
 *	return either.
 * NOTE: conmgr mutex must be held by caller
 */
static void _handle_work_run(work_t *work)
{
	xassert(work->magic == MAGIC_WORK);

	_log_work(work, __func__, "Enqueueing work. work:%u",
		  list_count(mgr.work));

	/* add to work list and signal a thread if watch is active */
	list_append(mgr.work, work);

	if (!mgr.quiesce.active)
		EVENT_SIGNAL(&mgr.worker_sleep);
}

/*
 * Routes new pending work to the correct queue
 * WARNING: conmgr.mutex must be locked by calling thread
 * IN work - Work to route. Takes ownership.
 */
static void _handle_work_pending(work_t *work)
{
	conmgr_fd_t *con = work->con;
	conmgr_work_depend_t depend = work->control.depend_type;

	xassert(work->magic == MAGIC_WORK);
	xassert(work->status == CONMGR_WORK_STATUS_PENDING);

	if (depend & CONMGR_WORK_DEP_NONE) {
		/* check for other flags being set too */
		xassert(depend == CONMGR_WORK_DEP_NONE);
	}

	if (depend & CONMGR_WORK_DEP_TIME_DELAY) {
		_log_work(work, __func__, "Enqueueing delayed work. delayed_work:%u",
			  list_count(mgr.delayed_work));
		add_work_delayed(work);
		return;
	}

	if (depend & CONMGR_WORK_DEP_CON_WRITE_COMPLETE) {
		xassert(con);

		if (slurm_conf.debug_flags & DEBUG_FLAG_CONMGR) {
			char *flags = con_flags_string(con->flags);
			_log_work(work, __func__, "Enqueueing connection write complete work. pending_writes=%u pending_write_complete_work:%u flags=%s",
				  list_count(con->out),
				  list_count(con->write_complete_work), flags);
			xfree(flags);
		}

		list_append(con->write_complete_work, work);
		return;
	}

	if (depend & CONMGR_WORK_DEP_SIGNAL) {
		_log_work(work, __func__, "Enqueueing signal work");
		add_work_signal(work);
		return;
	}

	if (con) {
		if (slurm_conf.debug_flags & DEBUG_FLAG_CONMGR) {
			char *flags = con_flags_string(con->flags);
			_log_work(work, __func__, "Enqueueing connection work. pending_work:%u flags=%s",
				  list_count(con->work), flags);
			xfree(flags);
		}

		list_append(con->work, work);

		/* trigger watch() if there is a connection involved */
		EVENT_SIGNAL(&mgr.watch_sleep);
		return;
	}

	/* No dependency blocking work from running now */

	work->status = CONMGR_WORK_STATUS_RUN;
	handle_work(true, work);
}

extern void handle_work(bool locked, work_t *work)
{
	if (!locked)
		slurm_mutex_lock(&mgr.mutex);

	switch (work->status) {
	case CONMGR_WORK_STATUS_PENDING:
		_handle_work_pending(work);
		break;
	case CONMGR_WORK_STATUS_CANCELLED:
		/* fall through as cancelled work runs immediately */
	case CONMGR_WORK_STATUS_RUN:
		_handle_work_run(work);
		break;
	case CONMGR_WORK_STATUS_MAX:
	case CONMGR_WORK_STATUS_INVALID:
		fatal_abort("%s: invalid work status 0x%x",
			    __func__, work->status);
	}

	if (!locked)
		slurm_mutex_unlock(&mgr.mutex);
}

extern void work_mask_depend(work_t *work, conmgr_work_depend_t depend_mask)
{
	/*
	 * Apply dependency mask but set NONE if the mask removes all bits and
	 * skip applying mask if there are !NONE bits set currently.
	 */
	if (!depend_mask || (work->control.depend_type == CONMGR_WORK_DEP_NONE))
		return;

	xassert(~depend_mask != CONMGR_WORK_DEP_NONE);

	if (!(work->control.depend_type & depend_mask))
		work->control.depend_type = CONMGR_WORK_DEP_NONE;
	else
		work->control.depend_type &= depend_mask;
}

extern void add_work(bool locked, conmgr_fd_t *con, conmgr_callback_t callback,
		     conmgr_work_control_t control,
		     conmgr_work_depend_t depend_mask, const char *caller)
{
	work_t *work = xmalloc_nz(sizeof(*work));
	*work = (work_t) {
		.magic = MAGIC_WORK,
		.status = CONMGR_WORK_STATUS_PENDING,
		.con = con,
		.callback = callback,
		.control = control,
	};

	work_mask_depend(work, depend_mask);

	handle_work(locked, work);
}

extern void conmgr_add_work(conmgr_fd_t *con, conmgr_callback_t callback,
			    conmgr_work_control_t control, const char *caller)
{
	add_work(false, con, callback, control, 0, caller);
}
