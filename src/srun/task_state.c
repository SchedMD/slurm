/*****************************************************************************\
 *  src/srun/task_state.c - task state container
 *****************************************************************************
 *  Portions copyright (C) 2017 SchedMD LLC.
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
 *  CODE-OCEC-09-009. All rights reserved.
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

#include <stdio.h>
#include <string.h>

#include "slurm/slurm.h"

#include "src/common/bitstring.h"
#include "src/common/log.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"

#include "src/srun/task_state.h"

struct task_state_struct {
	slurm_step_id_t step_id;
	uint32_t task_offset;
	int n_tasks;
	int n_started;
	int n_abnormal;
	int n_exited;
	bool first_exit;
	bool first_abnormal_exit;
	bitstr_t *start_failed;
	bitstr_t *running;
	bitstr_t *normal_exit;
	bitstr_t *abnormal_exit;
};

/*
 * Given a het group and task count, return a task_state structure
 * Free memory using task_state_destroy()
 */
extern task_state_t *task_state_create(slurm_step_id_t *step_id, int ntasks,
				       uint32_t task_offset)
{
	task_state_t *ts = xmalloc(sizeof(*ts));

	/* ts is zero filled by xmalloc() */
	memcpy(&ts->step_id, step_id, sizeof(ts->step_id));
	ts->task_offset = task_offset;
	ts->n_tasks = ntasks;
	ts->running = bit_alloc(ntasks);
	ts->start_failed = bit_alloc(ntasks);
	ts->normal_exit = bit_alloc(ntasks);
	ts->abnormal_exit = bit_alloc(ntasks);

	return ts;
}

static int _find_task_state(void *object, void *key)
{
	task_state_t *ts = (task_state_t *)object;
	slurm_step_id_t *step_id = (slurm_step_id_t *)key;

	return verify_step_id(&ts->step_id, step_id);
}

/*
 * Find the task_state structure for a given job_id, step_id and/or het group
 * on a list. Specify values of NO_VAL for values that are not to be matched
 * Returns NULL if not found
 */
extern task_state_t *task_state_find(slurm_step_id_t *step_id,
				    List task_state_list)
{
	if (!task_state_list)
		return NULL;

	return list_find_first(task_state_list, _find_task_state, step_id);
}

/*
 * Modify the task count for a previously created task_state structure
 */
extern void task_state_alter(task_state_t *ts, int ntasks)
{
	xassert(ts);
	ts->n_tasks = ntasks;
	bit_realloc(ts->running, ntasks);
	bit_realloc(ts->start_failed, ntasks);
	bit_realloc(ts->normal_exit, ntasks);
	bit_realloc(ts->abnormal_exit, ntasks);
}

/*
 * Destroy a task_state structure build by task_state_create()
 */
extern void task_state_destroy(task_state_t *ts)
{
	if (ts == NULL)
		return;

	FREE_NULL_BITMAP(ts->start_failed);
	FREE_NULL_BITMAP(ts->running);
	FREE_NULL_BITMAP(ts->normal_exit);
	FREE_NULL_BITMAP(ts->abnormal_exit);
	xfree(ts);
}

static const char *_task_state_type_str(task_state_type_t t)
{
	static char buf[16];

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

	snprintf(buf, sizeof(buf), "%d", t);
	return buf;
}

/*
 * Update the state of a specific task ID in a specific task_state structure
 */
extern void task_state_update(task_state_t *ts, int task_id,
			      task_state_type_t t)
{
	xassert(ts != NULL);
	xassert(task_id >= 0);
	xassert(task_id < ts->n_tasks);

	debug3("%s: %ps task_id=%d, %s", __func__,
	       &ts->step_id, task_id,
	       _task_state_type_str(t));

	switch (t) {
	case TS_START_SUCCESS:
		bit_set (ts->running, task_id);
		ts->n_started++;
		break;
	case TS_START_FAILURE:
		bit_set (ts->start_failed, task_id);
		break;
	case TS_NORMAL_EXIT:
		bit_clear(ts->running, task_id);
		if (bit_test(ts->normal_exit, task_id) ||
		    bit_test(ts->abnormal_exit, task_id)) {
			error("Task %d reported exit for a second time.",
			      task_id);
		} else {
			bit_set (ts->normal_exit, task_id);
			ts->n_exited++;
		}
		break;
	case TS_ABNORMAL_EXIT:
		bit_clear(ts->running, task_id);
		if (bit_test(ts->normal_exit, task_id) ||
		    bit_test(ts->abnormal_exit, task_id)) {
			error("Task %d reported exit for a second time.",
			      task_id);
		} else {
			bit_set (ts->abnormal_exit, task_id);
			ts->n_exited++;
			ts->n_abnormal++;
		}
		break;
	}

	xassert((bit_set_count(ts->abnormal_exit) +
		 bit_set_count(ts->normal_exit)) == ts->n_exited);
}

/*
 * Return TRUE if this is the first task exit for this job step
 * (ALL hetjob components)
 */
extern bool task_state_first_exit(List task_state_list)
{
	task_state_t *ts = NULL;
	ListIterator iter;
	bool is_first = true;
	int n_exited = 0;

	if (!task_state_list)
		return true;

	iter = list_iterator_create(task_state_list);
	while ((ts = list_next(iter))) {
		if (ts->first_exit) {
			is_first = false;
			break;
		}
		n_exited += ts->n_exited;
	}
	list_iterator_destroy(iter);

	if (n_exited == 0)
		is_first = false;

	if (is_first) {
		iter = list_iterator_create(task_state_list);
		while ((ts = list_next(iter))) {
			ts->first_exit = true;
		}
		list_iterator_destroy(iter);
	}

	return is_first;
}

/*
 * Return TRUE if this is the first abnormal task exit for this job step
 * (ALL hetjob components)
 */
extern bool task_state_first_abnormal_exit(List task_state_list)
{
	task_state_t *ts = NULL;
	ListIterator iter;
	bool is_first = true;
	int n_abnormal = 0;

	if (!task_state_list)
		return true;

	iter = list_iterator_create(task_state_list);
	while ((ts = list_next(iter))) {
		if (ts->first_abnormal_exit) {
			is_first = false;
			break;
		}
		n_abnormal += ts->n_abnormal;
	}
	list_iterator_destroy(iter);

	if (n_abnormal == 0)
		is_first = false;

	if (is_first) {
		iter = list_iterator_create(task_state_list);
		while ((ts = list_next(iter))) {
			ts->first_abnormal_exit = true;
		}
		list_iterator_destroy(iter);
	}

	return is_first;
}

static void _do_log_msg(task_state_t *ts, bitstr_t *b, log_f fn,
			const char *msg)
{
	char buf[4096];
	char *s = bit_set_count (b) == 1 ? "" : "s";
	(*fn) ("%ps task%s %s: %s",
	       &ts->step_id, s, bit_fmt(buf, sizeof(buf), b), msg);
}

static void _task_state_print(task_state_t *ts, log_f fn)
{
	bitstr_t *unseen;

	if (!ts)	/* Not built yet */
		return;

	unseen = bit_alloc(ts->n_tasks);
	if (bit_set_count(ts->start_failed)) {
		_do_log_msg(ts, ts->start_failed, fn,
			    "failed to start");
		bit_or(unseen, ts->start_failed);
	}
	if (bit_set_count(ts->running)) {
		_do_log_msg(ts, ts->running, fn, "running");
		bit_or(unseen, ts->running);
	}
	if (bit_set_count(ts->abnormal_exit)) {
		_do_log_msg(ts, ts->abnormal_exit, fn,
			    "exited abnormally");
		bit_or(unseen, ts->abnormal_exit);
	}
	if (bit_set_count(ts->normal_exit)) {
		_do_log_msg(ts, ts->normal_exit, fn, "exited");
		bit_or(unseen, ts->normal_exit);
	}
	bit_not(unseen);
	if (bit_set_count(unseen))
		_do_log_msg(ts, unseen, fn, "unknown");
	FREE_NULL_BITMAP(unseen);
}

/*
 * Print summary of a task_state structure's contents
 */
extern void task_state_print(List task_state_list, log_f fn)
{
	task_state_t *ts = NULL;
	ListIterator iter;

	if (!task_state_list)
		return;

	iter = list_iterator_create(task_state_list);
	while ((ts = list_next(iter))) {
		_task_state_print(ts, fn);
	}
	list_iterator_destroy(iter);
}

/*
 * Translate hetjob component local task ID to a global task ID
 */
extern uint32_t task_state_global_id(task_state_t *ts, uint32_t local_task_id)
{
	uint32_t global_task_id = local_task_id;

	if (ts && (ts->task_offset != NO_VAL))
		global_task_id += ts->task_offset;
	return global_task_id;
}
