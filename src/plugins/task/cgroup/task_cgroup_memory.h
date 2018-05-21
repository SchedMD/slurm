/*****************************************************************************\
 *  task_cgroup_memory.h - memory cgroup subsystem primitives for task/cgroup
 *****************************************************************************
 *  Copyright (C) 2009 CEA/DAM/DIF
 *  Written by Matthieu Hautreux <matthieu.hautreux@cea.fr>
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

#ifndef _TASK_CGROUP_MEMORY_H_
#define _TASK_CGROUP_MEMORY_H_

#include "src/common/xcgroup_read_config.h"

/* initialize memory subsystem of task/cgroup */
extern int task_cgroup_memory_init(slurm_cgroup_conf_t *slurm_cgroup_conf);

/* release memory subsystem resources */
extern int task_cgroup_memory_fini(slurm_cgroup_conf_t *slurm_cgroup_conf);

/* create user/job/jobstep memory cgroups */
extern int task_cgroup_memory_create(stepd_step_rec_t *job);

/* create a task cgroup and attach the task to it */
extern int task_cgroup_memory_attach_task(stepd_step_rec_t *job, pid_t pid);

/* detect if oom ran on a step or job and print notice of said event */
extern int task_cgroup_memory_check_oom(stepd_step_rec_t *job);

/* add a pid to the cgroup */
extern int task_cgroup_memory_add_pid(pid_t pid);

#endif
