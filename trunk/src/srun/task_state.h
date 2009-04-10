/*****************************************************************************\
 * src/srun/task_state.h - task state container for srun
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

#ifndef _HAVE_TASK_STATE_H
#define _HAVE_TASK_STATE_H

typedef struct task_state_struct * task_state_t;

typedef enum {
	TS_START_SUCCESS,
	TS_START_FAILURE,
	TS_NORMAL_EXIT,
	TS_ABNORMAL_EXIT
} task_state_type_t;

task_state_t task_state_create (int ntasks);

void task_state_destroy (task_state_t ts);

void task_state_update (task_state_t ts, int taskid, task_state_type_t t);

int task_state_first_exit (task_state_t ts);

int task_state_first_abnormal_exit (task_state_t ts);

typedef void (*log_f) (const char *, ...);

void task_state_print (task_state_t ts, log_f fn);

#endif /* !_HAVE_TASK_STATE_H */
