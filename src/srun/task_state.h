/*****************************************************************************\
 *  src/srun/task_state.h - task state container for srun
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

#ifndef _HAVE_TASK_STATE_H
#define _HAVE_TASK_STATE_H

#include "src/common/list.h"

typedef struct task_state_struct task_state_t;

typedef enum {
	TS_START_SUCCESS,
	TS_START_FAILURE,
	TS_NORMAL_EXIT,
	TS_ABNORMAL_EXIT
} task_state_type_t;

typedef void (*log_f) (const char *, ...);

/*
 * Given a het group and task count, return a task_state structure
 * Free memory using task_state_destroy()
 */
extern task_state_t *task_state_create(slurm_step_id_t *step_id,
				       int ntasks,
				       uint32_t task_offset);

/*
 * Find the task_state structure for a given job_id, step_id and/or het group
 * on a list. Specify values of NO_VAL for values that are not to be matched
 * Returns NULL if not found
 */
extern task_state_t *task_state_find(slurm_step_id_t *step_id,
				     List task_state_list);

/*
 * Modify the task count for a previously created task_state structure
 */
extern void task_state_alter(task_state_t *ts, int ntasks);

/*
 * Destroy a task_state structure build by task_state_create()
 */
extern void task_state_destroy(task_state_t *ts);

/*
 * Update the state of a specific task ID in a specific task_state structure
 */
extern void task_state_update(task_state_t *ts, int task_id,
			      task_state_type_t t);

/*
 * Return TRUE if this is the first task exit for this job step
 * (ALL hetjob components)
 */
extern bool task_state_first_exit(List task_state_list);

/*
 * Return TRUE if this is the first abnormal task exit for this job step
 * (ALL hetjob components)
 */
extern bool task_state_first_abnormal_exit(List task_state_list);

/*
 * Print summary of a task_state structure's contents
 */
extern void task_state_print(List task_state_list, log_f fn);

/*
 * Translate hetjob local task ID to a global task ID
 */
extern uint32_t task_state_global_id(task_state_t *ts, uint32_t local_task_id);

#endif /* !_HAVE_TASK_STATE_H */
