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
 * Create a cgroup namespace and try to mount it if when it is not available and
 * CgroupAutomount option is set.
 *
 * RET SLURM_SUCCESS if cgroup namespace is created and available, SLURM_ERROR
 *     otherwise.
 */
extern int xcgroup_ns_create(xcgroup_ns_t *cgns, char *mnt_args,
			     const char *subsys);

/*
 * Mount a cgroup namespace. If an error occurs, errno will be set.
 *
 * IN cgns - Cgroup namespace to mount.
 * RET SLURM_SUCCESS if cgroup namespace is created and mounted, SLURM_ERROR
 *     otherwise.
 */
extern int xcgroup_ns_mount(xcgroup_ns_t *cgns);

/*
 * Umount a cgroup namespace. If an error occurs, errno will be set.
 *
 * IN cgns - Cgroup namespace to umount.
 * RET SLURM_SUCCESS if umount operation succeeded. SLURM_ERROR otherwise.
 */
extern int xcgroup_ns_umount(xcgroup_ns_t *cgns);

/*
 * Check that a cgroup namespace is ready to be used.
 *
 * IN cgns - Cgroup namespace to check for availability.
 * RET SLURM_ERROR if it is not available, and SLURM_SUCCESS if ready.
 */
extern int xcgroup_ns_is_available(xcgroup_ns_t *cgns);

/*
 * Obtain cgroup in a specific namespace that owns a specified pid.
 *
 * IN cgns - Cgroup namespace to look into.
 * OUT cg - The cgroup which contains the pid in this namespace.
 * IN pid - Pid we want the cgroup containing it.
 * RET SLURM_SUCCESS if pid was found, SLURM_ERROR in all other cases.
 */
extern int xcgroup_ns_find_by_pid(xcgroup_ns_t *cgns, xcgroup_t *cg, pid_t pid);

/*
 * Set the cgroup struct parameters for a given cgroup from a namespace.
 *
 * IN cgns - Cgroup namespace where the cgroup resides.
 * OUT cg - Cgroup to fill data in.
 * IN uri - Relative path of the cgroup.
 * RETURN SLURM_SUCCESS if file operations over the generated path are ok.
 *        SLURM_ERROR otherwise.
 */
extern int xcgroup_load(xcgroup_ns_t *cgns, xcgroup_t *cg, char *uri);

/*
 * Given a cgroup, wait for our pid to disappear from this cgroup. This is
 * typically called from slurmstepd, so it will efectively wait for the pid of
 * slurmstepd to be removed from the cgroup.
 *
 * IN cg - cgroup where we will look into until our pid disappears.
 * IN cg_name - cgroup name for custom logging purposes.
 */
extern void xcgroup_wait_pid_moved(xcgroup_t *cg, const char *cg_name);

/*
 * Get a uint32 from a cgroup  for the specified parameter.
 *
 * IN cg - cgroup to get the value from.
 * IN param - string describing the filename of the parameter.
 * OUT value - pointer to a uint32_t which we will set with the info.
 *
 * RETURN SLURM_SUCCESS if OUT parameter is valid, SLURM_ERROR otherwise.
 */
extern int xcgroup_get_uint32_param(xcgroup_t *cg, char *param,
				    uint32_t *value);

/*
 * Get a uint64 from a cgroup  for the specified parameter.
 *
 * IN cg - cgroup to get the value from.
 * IN param - string describing the filename of the parameter.
 * OUT value - pointer to a uint64_t which we will set with the info.
 *
 * RETURN SLURM_SUCCESS if OUT parameter is valid, SLURM_ERROR otherwise.
 */
extern int xcgroup_get_uint64_param(xcgroup_t *cg, char *param,
				    uint64_t *value);

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

/*
 * Create a slurm cgroup object from a namespace. It will contain the path,
 * ownership and so on.
 *
 * IN ns - Namespace where the cgroup will reside.
 * OUT slurm_cg - Object where that will be created
 * RET SLURM_ERROR typically indicates the directory could not be created.
 *     SLURM_SUCCESS when the object is filled in and the directory created.
 */
extern int xcgroup_create_slurm_cg(xcgroup_ns_t *ns, xcgroup_t *slurm_cg);

/*
 * Create a cgroup hierarchy in the cgroupfs.
 *
 * IN calling_func - Name of the caller, for logging purposes.
 * IN job - Step record which contains required info for creating the paths.
 * IN ns - Namespace used to build paths.
 * OUT int_cg - Array with internal cgroups to be set.
 * OUT job_cgroup_path - Path to job level directory.
 * OUT step_cgroup_path - Path to step level directory.
 * OUT user_cgroup_path - Path to user level directory.
 * RET SLURM_SUCCESS if full hierarchy was created and xcgroup objects were
 *     initialized. SLURM_ERROR otherwise, and changes to filesystem undone.
 */
extern int xcgroup_create_hierarchy(const char *calling_func,
				    stepd_step_rec_t *job,
				    xcgroup_ns_t *ns,
				    xcgroup_t int_cg[],
				    char job_cgroup_path[],
				    char step_cgroup_path[],
				    char user_cgroup_path[]);

#endif
