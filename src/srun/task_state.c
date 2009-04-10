/*****************************************************************************\
 * src/srun/task_state.c - task state container
 * $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>

#include "src/common/xmalloc.h"
#include "src/common/bitstring.h"
#include "src/common/xassert.h"

#include "src/srun/task_state.h"

struct task_state_struct {
	int n_tasks;
	int n_started;
	int n_abnormal;
	int n_exited;
	unsigned int first_exit:1;
	unsigned int first_abnormal_exit:1;
	bitstr_t *start_failed;
	bitstr_t *running;
	bitstr_t *normal_exit;
	bitstr_t *abnormal_exit;
};

task_state_t task_state_create (int ntasks)
{
	task_state_t ts = xmalloc (sizeof (*ts));

	/* ts is zero filled by xmalloc() */
	ts->n_tasks = ntasks;
	ts->running = bit_alloc (ntasks);
	ts->start_failed = bit_alloc (ntasks);
	ts->normal_exit = bit_alloc (ntasks);
	ts->abnormal_exit = bit_alloc (ntasks);

	return (ts);
}

void task_state_destroy (task_state_t ts)
{
	if (ts == NULL)
		return;
	if (ts->start_failed)
		bit_free (ts->start_failed);
	if (ts->running)
		bit_free (ts->running);
	if (ts->normal_exit)
		bit_free (ts->normal_exit);
	if (ts->abnormal_exit)
		bit_free (ts->abnormal_exit);
	xfree (ts);
}

static const char *_task_state_type_str (task_state_type_t t)
{
	switch (t) {
	case TS_START_SUCCESS:
		return ("TS_START_SUCCESS");
	case TS_START_FAILURE:
		return ("TS_START_FAILURE");
	case TS_NORMAL_EXIT:
		return ("TS_NORMAL_EXIT");
	case TS_ABNORMAL_EXIT:
		return ("TS_ABNORMAL_EXIT");
	}
	return ("Unknown");
}

void task_state_update (task_state_t ts, int taskid, task_state_type_t t)
{
	xassert (ts != NULL);
	xassert (taskid >= 0);
	xassert (taskid < ts->n_tasks);

	debug3("task_state_update(taskid=%d, %s)",
	       taskid, _task_state_type_str (t));

	switch (t) {
	case TS_START_SUCCESS:
		bit_set (ts->running, taskid);
		ts->n_started++;
		break;
	case TS_START_FAILURE:
		bit_set (ts->start_failed, taskid);
		break;
	case TS_NORMAL_EXIT:
		bit_set (ts->normal_exit, taskid);
		bit_clear (ts->running, taskid);
		ts->n_exited++;
		break;
	case TS_ABNORMAL_EXIT:
		bit_clear (ts->running, taskid);
		bit_set (ts->abnormal_exit, taskid);
		ts->n_exited++;
		ts->n_abnormal++;
		break;
	}

	xassert ((bit_set_count(ts->abnormal_exit) +
		  bit_set_count(ts->normal_exit)) == ts->n_exited);
}

int task_state_first_exit (task_state_t ts)
{
	if (!ts->first_exit && ts->n_exited) {
		ts->first_exit = 1;
		return (1);
	}
	return (0);
}

int task_state_first_abnormal_exit (task_state_t ts)
{
	if (!ts->first_abnormal_exit && ts->n_abnormal) {
		ts->first_abnormal_exit = 1;
		return (1);
	}
	return (0);
}

static void _do_log_msg (bitstr_t *b, log_f fn, const char *msg)
{
	char buf [65536];
	char *s = bit_set_count (b) == 1 ? "" : "s";
	(*fn) ("task%s %s: %s\n", s, bit_fmt (buf, sizeof(buf), b), msg);
}

void task_state_print (task_state_t ts, log_f fn)
{
	bitstr_t *unseen = bit_alloc (ts->n_tasks);

	if (bit_set_count (ts->start_failed)) {
		_do_log_msg (ts->start_failed, fn, "failed to start");
		bit_or (unseen, ts->start_failed);
	}
	if (bit_set_count (ts->running)) {
		_do_log_msg (ts->running, fn, "running");
		bit_or (unseen, ts->running);
	}
	if (bit_set_count (ts->abnormal_exit)) {
		_do_log_msg (ts->abnormal_exit, fn, "exited abnormally");
		bit_or (unseen, ts->abnormal_exit);
	}
	if (bit_set_count (ts->normal_exit)) {
		_do_log_msg (ts->normal_exit, fn, "exited");
		bit_or (unseen, ts->normal_exit);
	}
	bit_not (unseen);
	if (bit_set_count (unseen))
		_do_log_msg (unseen, fn, "unknown");
	bit_free (unseen);
}

