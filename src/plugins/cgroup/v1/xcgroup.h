/*****************************************************************************\
 *  xcgroup.h - Cgroup v1 internal functions
 *****************************************************************************
 *  Copyright (C) 2009 CEA/DAM/DIF
 *  Written by Matthieu Hautreux <matthieu.hautreux@cea.fr>
 *  Modified by Felip Moll <felip.moll@schedmd.com> 2021 SchedMD
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

#ifndef _XCGROUP_H
#define _XCGROUP_H

/* Cgroup v1 internal functions */
/*
 * create a cgroup namespace for tasks containment
 *
 * returned values:
 *  - SLURM_ERROR
 *  - SLURM_SUCCESS
 */
extern int xcgroup_ns_create(xcgroup_ns_t* cgns, char* mnt_args,
			     const char* subsys);

/*
 * mount a cgroup namespace
 *
 * returned values:
 *  - SLURM_ERROR
 *  - SLURM_SUCCESS
 *
 * If an error occurs, errno will be set.
 */
extern int xcgroup_ns_mount(xcgroup_ns_t* cgns);

/*
 * umount a cgroup namespace
 *
 * returned values:
 *  - SLURM_ERROR
 *  - SLURM_SUCCESS
 *
 * If an error occurs, errno will be set.
 */
extern int xcgroup_ns_umount(xcgroup_ns_t* cgns);

/*
 * test if cgroup namespace is currently available (mounted)
 *
 * returned values:
 *  - 0 if not available
 *  - 1 if available
 */
extern int xcgroup_ns_is_available(xcgroup_ns_t* cgns);

/*
 * load a cgroup from a cgroup namespace given a pid
 *
 * returned values:
 *  - SLURM_ERROR
 *  - SLURM_SUCCESS
 */
extern int xcgroup_ns_find_by_pid(xcgroup_ns_t* cgns, xcgroup_t* cg, pid_t pid);

/*
 * load a cgroup namespace
 *
 * returned values:
 *  - SLURM_ERROR
 *  - SLURM_SUCCESS
 */
extern int xcgroup_ns_load(xcgroup_ns_t *cgns, char *subsys);

/*
 * lock a cgroup (must have been instantiated)
 * (system level using flock)
 *
 * returned values:
 *  - SLURM_ERROR
 *  - SLURM_SUCCESS
 */
extern int xcgroup_lock(xcgroup_t* cg);

/*
 * unlock a cgroup
 *
 * returned values:
 *  - SLURM_ERROR
 *  - SLURM_SUCCESS
 */
extern int xcgroup_unlock(xcgroup_t* cg);

/*
 * load a cgroup from a cgroup namespace into a structure
 *
 * returned values:
 *  - SLURM_ERROR
 *  - SLURM_SUCCESS
 */
extern int xcgroup_load(xcgroup_ns_t* cgns, xcgroup_t* cg, char* uri);

/*
 * get a cgroup parameter in the form of a uint32_t
 *
 * param must correspond to a file of the cgroup that
 * will be read for its content
 *
 * i.e. xcgroup_get_uint32_param(&cg,"memory.swappiness",&value);
 *
 * returned values:
 *  - SLURM_ERROR
 *  - SLURM_SUCCESS
 */
extern int xcgroup_get_uint32_param(xcgroup_t* cg, char* param,
				    uint32_t* value);

/*
 * get a cgroup parameter in the form of a uint64_t
 *
 * param must correspond to a file of the cgroup that
 * will be read for its content
 *
 * i.e. xcgroup_get_uint64_param(&cg,"memory.swappiness",&value);
 *
 * returned values:
 *  - SLURM_ERROR
 *  - SLURM_SUCCESS
 */
extern int xcgroup_get_uint64_param(xcgroup_t* cg, char* param,
				    uint64_t* value);


extern char *xcgroup_create_slurm_cg(xcgroup_ns_t *ns);

/*
 * Create normal hierarchy for Slurm jobs/steps
 *
 * returned values:
 *  - SLURM_ERROR
 *  - SLURM_SUCCESS
 */
extern int xcgroup_create_hierarchy(const char *calling_func,
				    stepd_step_rec_t *job,
				    xcgroup_ns_t *ns,
				    xcgroup_t *job_cg,
				    xcgroup_t *step_cg,
				    xcgroup_t *user_cg,
				    char job_cgroup_path[],
				    char step_cgroup_path[],
				    char user_cgroup_path[]);

/*
 * Wait for a pid to move out of a cgroup.
 *
 * Must call xcgroup_move_process before this function.
 */
extern int xcgroup_wait_pid_moved(xcgroup_t *cg, const char *cg_name);

/*
 * Init cpuset cgroup
 *
 * Will ensure cpuset.mems or cpuset.cpus is correctly set by inheriting parent
 * values or setting it to 0 if there's nothing set. An empty value would mean
 * we don't have any memory nodes/cpus assigned to the cpuset thus processes
 * could not be added to the cgroup.
 *
 * IN: cpuset_prefix - cpuset prefix to set
 * IN/OUT: prefix_set - wheter cpuset prefix is set or not
 * IN: cg - cgroup to initialize
 * OUT: SLURM_ERROR or SLURM_SUCCESS
 *
 */
extern int xcgroup_cpuset_init(xcgroup_t *cg);

#endif
