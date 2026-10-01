/*****************************************************************************\
 *  task_cgroup_pids.h - pids cgroup subsystem primitives for task/cgroup
 *****************************************************************************
 *
 * Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#ifndef _TASK_CGROUP_PIDS_H_
#define _TASK_CGROUP_PIDS_H_

/*
 * Initialize the pids controller of task/cgroup.
 * RET SLURM_SUCCESS or an error code
 */
extern int task_cgroup_pids_init(void);

/*
 * Release the pids controller resources of the step.
 * RET SLURM_SUCCESS or an error code
 */
extern int task_cgroup_pids_fini(void);

/*
 * Create the pids cgroup of the step and apply its PID limit.
 * IN step - step record with the PID limit to apply
 * RET SLURM_SUCCESS or an error code
 */
extern int task_cgroup_pids_create(stepd_step_rec_t *step);

/*
 * Add a pid to the pids cgroup of a task of the step.
 * IN step - step record
 * IN pid - pid to add
 * IN taskid - local id of the task the pid belongs to
 * RET SLURM_SUCCESS or an error code
 */
extern int task_cgroup_pids_add_pid(stepd_step_rec_t *step, pid_t pid,
				    uint32_t taskid);

/*
 * Add a pid to the pids cgroup of the extern step, which has no task level.
 * IN pid - pid to add
 * RET SLURM_SUCCESS or an error code
 */
extern int task_cgroup_pids_add_extern_pid(pid_t pid);

#endif
