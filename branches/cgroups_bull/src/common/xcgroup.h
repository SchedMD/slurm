/*****************************************************************************\
 *  cgroup.h - cgroup related primitives headers
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

#ifndef _XCGROUP_H_
#define _XCGROUP_H_

#include <sys/types.h>

#define XCGROUP_ERROR    1
#define XCGROUP_SUCCESS  0

#ifndef CGROUP_BASEDIR
#define CGROUP_BASEDIR "/dev/cgroup"
#endif

typedef struct xcgroup_ns {

	char* mnt_point;  /* mount point to use for the associated cgroup */
	char* mnt_args;   /* mount args to use in addition */

	char* subsystems; /* list of comma separated subsystems to provide */

	char* notify_prog;/* prog to use with notify on release action */

} xcgroup_ns_t;

typedef struct xcgroup {

	xcgroup_ns_t* ns; /* xcgroup namespace of this xcgroup */
	char* name;       /* name of the xcgroup relative to the ns */
	char* path;       /* absolute path of the xcgroup in the ns */
	uid_t uid;        /* uid of the owner */
	gid_t gid;        /* gid of the owner */

} xcgroup_t;

/*
 * create a cgroup namespace for tasks containment
 *
 * returned values:
 *  - XCGROUP_ERROR
 *  - XCGROUP_SUCCESS
 */
int xcgroup_ns_create(xcgroup_ns_t* cgns,
		      char* mnt_point,char* mnt_args,
		      char* subsys,char* notify_prog);

/*
 * destroy a cgroup namespace
 *
 * returned values:
 *  - XCGROUP_ERROR
 *  - XCGROUP_SUCCESS
 */
int xcgroup_ns_destroy(xcgroup_ns_t* cgns);

/*
 * mount a cgroup namespace
 *
 * returned values:
 *  - XCGROUP_ERROR
 *  - XCGROUP_SUCCESS
 */
int xcgroup_ns_mount(xcgroup_ns_t* cgns);

/*
 * umount a cgroup namespace
 *
 * returned values:
 *  - XCGROUP_ERROR
 *  - XCGROUP_SUCCESS
 */
int xcgroup_ns_umount(xcgroup_ns_t* cgns);

/*
 * test if cgroup namespace is currently available (mounted)
 *
 * returned values:
 *  - 0 if not available
 *  - 1 if available
 */
int xcgroup_ns_is_available();

/*
 * load a cgroup from a cgroup namespace given a pid
 *
 * returned values:
 *  - XCGROUP_ERROR
 *  - XCGROUP_SUCCESS
 */
int xcgroup_ns_find_by_pid(xcgroup_ns_t* cgns,xcgroup_t* cg,pid_t pid);

/*
 * create a cgroup structure
 *
 * returned values:
 *  - XCGROUP_ERROR
 *  - XCGROUP_SUCCESS
 */
int xcgroup_create(xcgroup_ns_t* cgns,xcgroup_t* cg,
		   char* uri,uid_t uid, gid_t gid);

/*
 * destroy a cgroup internal structure
 *
 * returned values:
 *  - XCGROUP_ERROR
 *  - XCGROUP_SUCCESS
 */
int xcgroup_destroy(xcgroup_t* cg);

/*
 * instanciate a cgroup in a cgroup namespace (mkdir)
 *
 * returned values:
 *  - XCGROUP_ERROR
 *  - XCGROUP_SUCCESS
 */
int xcgroup_instanciate(xcgroup_t* cg);

/*
 * load a cgroup from a cgroup namespace into a structure
 *
 * returned values:
 *  - XCGROUP_ERROR
 *  - XCGROUP_SUCCESS
 */
int xcgroup_load(xcgroup_ns_t* cgns,xcgroup_t* cg,
		 char* uri);

/*
 * delete a cgroup instance in a cgroup namespace (rmdir)
 *
 * returned values:
 *  - XCGROUP_ERROR
 *  - XCGROUP_SUCCESS
 */
int xcgroup_delete(xcgroup_t* cg);

/*
 * add a list of pids to a cgroup
 *
 * returned values:
 *  - XCGROUP_ERROR
 *  - XCGROUP_SUCCESS
 */
int xcgroup_add_pids(xcgroup_t* cg,pid_t* pids,int npids);

/*
 * extract the pids list of a cgroup
 *
 * pids array must be freed using xfree(...)
 *
 * returned values:
 *  - XCGROUP_ERROR
 *  - XCGROUP_SUCCESS
 */
int xcgroup_get_pids(xcgroup_t* cg, pid_t **pids, int *npids);

/*
 * set cgroup parameters using string of the form :
 * parameteres="param=value[ param=value]*"
 *
 * param must correspond to a file of the cgroup that
 * will be written with the value content
 *
 * i.e. xcgroup_set_params("/dev/cgroup/slurm",
 *                         "memory.swappiness=10");
 *
 * returned values:
 *  - XCGROUP_ERROR
 *  - XCGROUP_SUCCESS
 */
int xcgroup_set_params(xcgroup_t* cg,char* parameters);

/*
 * get cgroup cpuset cpus configuration
 *
 * range is the ranges of cores to constrain the cgroup to
 * i.e. 0-1,4-5
 *
 * returned values:
 *  - XCGROUP_ERROR
 *  - XCGROUP_SUCCESS
 */
int xcgroup_get_cpuset_cpus(xcgroup_t* cg,char* range);

/*
 * set cgroup cpuset cpus configuration
 *
 * range is the ranges of cores to constrain the cgroup to
 * i.e. 0-1,4-5
 *
 * returned values:
 *  - XCGROUP_ERROR
 *  - XCGROUP_SUCCESS
 */
int xcgroup_set_cpuset_cpus(xcgroup_t* cg,char* range);

/*
 * set a cgroup parameter
 *
 * param must correspond to a file of the cgroup that
 * will be written with the value content
 *
 * i.e. xcgroup_set_params("/dev/cgroup/slurm",
 *                         "memory.swappiness","10");
 *
 * returned values:
 *  - XCGROUP_ERROR
 *  - XCGROUP_SUCCESS
 */
int xcgroup_set_param(xcgroup_t* cg,char* parameter,char* content);

/*
 * get a cgroup parameter
 *
 * param must correspond to a file of the cgroup that
 * will be read for its content
 *
 * i.e. xcgroup_get_param("/dev/cgroup/slurm",
 *                         "memory.swappiness",&value,&size);
 *
 * on success, content must be free using xfree
 *
 * returned values:
 *  - XCGROUP_ERROR
 *  - XCGROUP_SUCCESS
 */
int xcgroup_get_param(xcgroup_t* cg,char* param,char **content,size_t *csize);

#endif
