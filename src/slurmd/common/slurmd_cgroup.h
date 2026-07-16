/***************************************************************************** \
 *  slurmd_cgroup.h - slurmd system cgroup management
 *****************************************************************************
 *  Copyright (C) 2013 Bull S. A. S.
 *		Bull, Rue Jean Jaures, B.P.68, 78340, Les Clayes-sous-Bois.
 *
 *  Written by Martin Perry <martin.perry@bull.com>
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com>.
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
\****************************************************************************/

#ifndef _SLURMD_CGROUP_H
#define _SLURMD_CGROUP_H

/* Initialize slurmd system cpuset cgroup */
extern int init_system_cpuset_cgroup(void);

/* Initialize slurmd system memory cgroup */
extern int init_system_memory_cgroup(void);

/* Free memory allocated by init_system_cpuset_cgroup() and
 * init_system_memory_cgroup() functions */
extern void fini_system_cgroup(void);

/* Set reserved machine CPU IDs in system cpuset cgroup */
extern int set_system_cgroup_cpus(char *phys_core_str);

/* Set memory limit in system memory cgroup */
extern int set_system_cgroup_mem_limit(uint64_t mem_spec_limit);

/* Attach pid to system cpuset cgroup */
extern int attach_system_cpuset_pid(pid_t pid);

/* Attach a pid to system memory cgroup */
extern int attach_system_memory_pid(pid_t pid);

/* Check that corespec cgroup job confinement is configured */
extern bool check_corespec_cgroup_job_confinement(void);

/* Attach a pid to the system cgroups */
extern void attach_system_cgroup_pid(pid_t pid);

#endif	/* _SLURMD_CGROUP_H */
