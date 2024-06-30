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
#include "src/common/read_config.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/conmgr/conmgr.h"
#include "src/conmgr/events.h"
#include "src/conmgr/mgr.h"

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
	conmgr_work_type_t type;
	const char *string;
} types[] = {
	{ CONMGR_WORK_TYPE_INVALID, "INVALID" },
	{ CONMGR_WORK_TYPE_CONNECTION_FIFO, "CONNECTION_FIFO" },
	{ CONMGR_WORK_TYPE_CONNECTION_DELAY_FIFO, "DELAY_CONNECTION_FIFO" },
	{ CONMGR_WORK_TYPE_CONNECTION_WRITE_COMPLETE,
	  "CONNECTION_WRITE_COMPLETE" },
	{ CONMGR_WORK_TYPE_FIFO, "FIFO" },
	{ CONMGR_WORK_TYPE_TIME_DELAY_FIFO, "TIME_DELAY_FIFO" },
};

extern const char *conmgr_work_status_string(conmgr_work_status_t status)
{
	for (int i = 0; i < ARRAY_SIZE(statuses); i++)
		if (statuses[i].status == status)
			return statuses[i].string;

	fatal_abort("%s: invalid work status 0x%x", __func__, status);
}

extern const char *conmgr_work_type_string(conmgr_work_type_t type)
{
	for (int i = 0; i < ARRAY_SIZE(types); i++)
		if (types[i].type == type)
			return types[i].string;

	fatal_abort("%s: invalid work type 0x%x", __func__, type);
}

extern void wrap_work(work_t *work)
{
	conmgr_fd_t *con = work->con;
	char *con_name = NULL;

	xassert(work->magic == MAGIC_WORK);

	if ((slurm_conf.debug_flags & DEBUG_FLAG_CONMGR) && con)
		xstrfmtcat(con_name, "[%s] ", con->name);

	log_flag(CONMGR, "%s: %sBEGIN work=0x%"PRIxPTR" %s@0x%"PRIxPTR" type=%s status=%s arg=0x%"PRIxPTR,
		 __func__, (con_name ? con_name : ""), (uintptr_t) work,
		 work->tag, (uintptr_t) work->func,
		 conmgr_work_type_string(work->type),
		 conmgr_work_status_string(work->status),
		 (uintptr_t) work->arg);

	switch (work->type) {
	case CONMGR_WORK_TYPE_FIFO:
	case CONMGR_WORK_TYPE_TIME_DELAY_FIFO:
		xassert(!con);
		work->func((conmgr_callback_args_t) { .status = work->status },
			   work->arg);
		break;
	case CONMGR_WORK_TYPE_CONNECTION_WRITE_COMPLETE:
	case CONMGR_WORK_TYPE_CONNECTION_FIFO:
	case CONMGR_WORK_TYPE_CONNECTION_DELAY_FIFO:
		wrap_con_work(work, con);
		/* ptr may disappear once work_active=false and mutex released */
		con = NULL;
		break;
	case CONMGR_WORK_TYPE_INVALID:
	case CONMGR_WORK_TYPE_MAX:
		fatal_abort("%s: invalid work type 0x%x", __func__, work->type);
	}

	log_flag(CONMGR, "%s: %sEND work=0x%"PRIxPTR" %s@0x%"PRIxPTR" type=%s status=%s arg=0x%"PRIxPTR,
		 __func__, (con_name ? con_name : ""), (uintptr_t) work,
		 work->tag, (uintptr_t) work->func,
		 conmgr_work_type_string(work->type),
		 conmgr_work_status_string(work->status),
		 (uintptr_t) work->arg);

	work->magic = ~MAGIC_WORK;
	xfree(work);
	xfree(con_name);
}

/*
 * Add work to mgr.work
 * Single point to enqueue internal function callbacks
 * NOTE: _handle_work_run() can add new entries to mgr.work
 *
 * IN locked - true if conmgr is already locked by caller
 * IN work - pointer to work to run
 * NOTE: never add a thread that will never return or conmgr_fini() will never
 *	return either.
 * NOTE: conmgr mutex must be held by caller
 */
static void _handle_work_run(work_t *work)
{
	xassert(work->magic == MAGIC_WORK);

	/* add to work list and signal a thread if watch is active */
	list_append(mgr.work, work);

	if (!mgr.quiesced)
		EVENT_SIGNAL_RELIABLE_SINGULAR(&mgr.worker_sleep);
}

/* mgr must be locked */
static void _handle_work_pending(work_t *work)
{
	conmgr_fd_t *con = work->con;

	switch (work->type) {
	case CONMGR_WORK_TYPE_CONNECTION_DELAY_FIFO:
		xassert(con);
		/* fall through */
	case CONMGR_WORK_TYPE_TIME_DELAY_FIFO:
	{
		update_last_time(true);
		work->begin.seconds += mgr.last_time.tv_sec;
		list_append(mgr.delayed_work, work);
		update_timer(true);
		break;
	}
	case CONMGR_WORK_TYPE_CONNECTION_FIFO:
	{
		xassert(con);
		log_flag(CONMGR, "%s: [%s] work_active=%c queuing \"%s\" pending work: %u total",
			 __func__, con->name, (con->work_active ? 'T' : 'F'),
			 work->tag, list_count(con->work));
		list_append(con->work, work);
		break;
	}
	case CONMGR_WORK_TYPE_CONNECTION_WRITE_COMPLETE:
		xassert(con);
		list_append(con->write_complete_work, work);
		break;
	case CONMGR_WORK_TYPE_FIFO:
		/* can be run now */
		xassert(!con);
		work->status = CONMGR_WORK_STATUS_RUN;
		handle_work(true, work);
		break;
	case CONMGR_WORK_TYPE_INVALID:
	case CONMGR_WORK_TYPE_MAX:
		fatal("%s: invalid type", __func__);
	}

	/* trigger watch() if there is a connection involved */
	if (con)
		EVENT_SIGNAL_RELIABLE_SINGULAR(&mgr.watch_sleep);
}

extern void handle_work(bool locked, work_t *work)
{
	conmgr_fd_t *con = work->con;

	if (con)
		log_flag(CONMGR, "%s: [%s] work=0x%"PRIxPTR" status=%s type=%s func=%s@0x%"PRIxPTR,
			 __func__, con->name, (uintptr_t) work,
			conmgr_work_status_string(work->status),
			conmgr_work_type_string(work->type),
			work->tag, (uintptr_t) work->func);
	else
		log_flag(CONMGR, "%s: work=0x%"PRIxPTR" status=%s type=%s func=%s@0x%"PRIxPTR,
			 __func__, (uintptr_t) work,
			conmgr_work_status_string(work->status),
			conmgr_work_type_string(work->type),
			work->tag, (uintptr_t) work->func);

	if (!locked)
		slurm_mutex_lock(&mgr.mutex);

	switch (work->status) {
	case CONMGR_WORK_STATUS_PENDING:
		_handle_work_pending(work);
		break;
	case CONMGR_WORK_STATUS_RUN:
		_handle_work_run(work);
		break;
	case CONMGR_WORK_STATUS_CANCELLED:
		if (con)
			list_append(con->work, work);
		else
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

extern void add_work(bool locked, conmgr_fd_t *con, conmgr_work_func_t func,
		     conmgr_work_type_t type, void *arg, const char *tag)
{
	work_t *work = xmalloc(sizeof(*work));
	*work = (work_t) {
		.magic = MAGIC_WORK,
		.con = con,
		.func = func,
		.arg = arg,
		.tag = tag,
		.type = type,
		.status = CONMGR_WORK_STATUS_PENDING,
	};

	handle_work(locked, work);
}

extern void conmgr_add_work(conmgr_fd_t *con, conmgr_work_func_t func,
			    conmgr_work_type_t type, void *arg,
			    const char *tag)
{
	add_work(false, con, func, type, arg, tag);
}
