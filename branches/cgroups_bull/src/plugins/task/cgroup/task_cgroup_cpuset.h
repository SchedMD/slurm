/*****************************************************************************\
 *  task_cgroup_cpuset.h - cpuset cgroup subsystem primitives for task/cgroup
 *****************************************************************************
 *  Copyright (C) 2009 CEA/DAM/DIF
 *  Written by Matthieu Hautreux <matthieu.hautreux@cea.fr>
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

#if HAVE_CONFIG_H
#   include "config.h"
#endif

#ifndef _TASK_CGROUP_CPUSET_H_
#define _TASK_CGROUP_CPUSET_H_

/*
 * task_cgroup_cpuset_init() initializes the cpuset subsystem
 */
int task_cgroup_cpuset_init();

/*
 * task_build_cgroup_cpuset() builds the cgroups for a job in the
 * cpuset namespace
 */
int	task_build_cgroup_cpuset(slurmd_job_t *job, uid_t uid, gid_t gid);

/*
 * task_create_cgroup_cpuset() creates the cgroups for a job in the
 * cpuset namespace
 */
int task_create_cgroup_cpuset(slurmd_job_t *job, uint32_t id,uid_t uid,gid_t gid);

/*
 * task_cpuset_ns_is_available() determines whether the cpuset ns is available
 */
int task_cpuset_ns_is_available();

/*
 * task_set_cgroup_cpuset() creates the cgroup for a task in the
 * cpuset namespace, sets the cpuset for the cgroup, and sets the
 * task (pid) for the cgroup
 */
int	task_set_cgroup_cpuset(int task, pid_t pid, size_t size,
			 const cpu_set_t *mask);

/*
 * task_get_cgroup_cpuset() gets the cpuset for a task cgroup in
 * the cpuset namespace
 */
int task_get_cgroup_cpuset(pid_t pid, size_t size, cpu_set_t *mask);

/*
 * _cpuset_to_cpustr() converts a cpuset mask to a cpuset string
 */
void _cpuset_to_cpustr(const cpu_set_t *mask, char *str);

#endif
