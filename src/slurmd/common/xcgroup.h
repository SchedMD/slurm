/*****************************************************************************\
 *  cgroup.h - cgroup related primitives headers
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

#ifndef _XCGROUP_H_
#define _XCGROUP_H_

#include <dirent.h>
#include <sys/types.h>

#include "src/common/xcgroup_read_config.h"

#define XCGROUP_ERROR    1
#define XCGROUP_SUCCESS  0
#define MAX_MOVE_WAIT 5000

// http://lists.debian.org/debian-boot/2012/04/msg00047.html
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__)
#define	MS_NOSUID	MNT_NOSUID
#define	MS_NOEXEC	MNT_NOEXEC
#define	MS_NODEV	0

#define	umount(d)	unmount(d, 0)
#endif

typedef struct xcgroup_ns {
	char* mnt_point;  /* mount point to use for the associated cgroup */
	char* mnt_args;   /* mount args to use in addition */

	char* subsystems; /* list of comma separated subsystems to provide */
} xcgroup_ns_t;

typedef struct xcgroup {
	xcgroup_ns_t* ns; /* xcgroup namespace of this xcgroup */
	char*    name;    /* name of the xcgroup relative to the ns */
	char*    path;    /* absolute path of the xcgroup in the ns */
	uid_t    uid;     /* uid of the owner */
	gid_t    gid;     /* gid of the owner */
	int      fd;      /* used for locking */
} xcgroup_t;

/*
 * create a cgroup namespace for tasks containment
 *
 * returned values:
 *  - XCGROUP_ERROR
 *  - XCGROUP_SUCCESS
 */
int xcgroup_ns_create(xcgroup_ns_t* cgns, char* mnt_args,
		      char* subsys);

/*
 * destroy a cgroup namespace
 */
void xcgroup_ns_destroy(xcgroup_ns_t* cgns);

/*
 * mount a cgroup namespace
 *
 * returned values:
 *  - XCGROUP_ERROR
 *  - XCGROUP_SUCCESS
 *
 * If an error occurs, errno will be set.
 */
int xcgroup_ns_mount(xcgroup_ns_t* cgns);

/*
 * umount a cgroup namespace
 *
 * returned values:
 *  - XCGROUP_ERROR
 *  - XCGROUP_SUCCESS
 *
 * If an error occurs, errno will be set.
 */
int xcgroup_ns_umount(xcgroup_ns_t* cgns);

/*
 * test if cgroup namespace is currently available (mounted)
 *
 * returned values:
 *  - 0 if not available
 *  - 1 if available
 */
int xcgroup_ns_is_available(xcgroup_ns_t* cgns);

/*
 * load a cgroup from a cgroup namespace given a pid
 *
 * returned values:
 *  - XCGROUP_ERROR
 *  - XCGROUP_SUCCESS
 */
int xcgroup_ns_find_by_pid(xcgroup_ns_t* cgns, xcgroup_t* cg, pid_t pid);

/*
 * load a cgroup namespace
 *
 * returned values:
 *  - XCGROUP_ERROR
 *  - XCGROUP_SUCCESS
 */
int xcgroup_ns_load(xcgroup_ns_t *cgns, char *subsys);

/*
 * create a cgroup structure
 *
 * returned values:
 *  - XCGROUP_ERROR
 *  - XCGROUP_SUCCESS
 */
int xcgroup_create(xcgroup_ns_t* cgns, xcgroup_t* cg,
		   char* uri, uid_t uid, gid_t gid);

/*
 * destroy a cgroup internal structure
 */
void xcgroup_destroy(xcgroup_t* cg);

/*
 * lock a cgroup (must have been instantiated)
 * (system level using flock)
 *
 * returned values:
 *  - XCGROUP_ERROR
 *  - XCGROUP_SUCCESS
 */
int xcgroup_lock(xcgroup_t* cg);

/*
 * unlock a cgroup
 *
 * returned values:
 *  - XCGROUP_ERROR
 *  - XCGROUP_SUCCESS
 */
int xcgroup_unlock(xcgroup_t* cg);

/*
 * instantiate a cgroup in a cgroup namespace (mkdir)
 *
 * returned values:
 *  - XCGROUP_ERROR
 *  - XCGROUP_SUCCESS
 */
int xcgroup_instantiate(xcgroup_t* cg);

/*
 * load a cgroup from a cgroup namespace into a structure
 *
 * returned values:
 *  - XCGROUP_ERROR
 *  - XCGROUP_SUCCESS
 */
int xcgroup_load(xcgroup_ns_t* cgns, xcgroup_t* cg, char* uri);

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
int xcgroup_add_pids(xcgroup_t* cg, pid_t* pids, int npids);

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
 * set a cgroup parameter
 *
 * param must correspond to a file of the cgroup that
 * will be written with the value content
 *
 * i.e. xcgroup_set_params(&cf,"memory.swappiness","10");
 *
 * returned values:
 *  - XCGROUP_ERROR
 *  - XCGROUP_SUCCESS
 */
int xcgroup_set_param(xcgroup_t* cg, char* parameter, char* content);

/*
 * get a cgroup parameter
 *
 * param must correspond to a file of the cgroup that
 * will be read for its content
 *
 * i.e. xcgroup_get_param(&cg,"memory.swappiness",&value,&size);
 *
 * on success, content must be free using xfree
 *
 * returned values:
 *  - XCGROUP_ERROR
 *  - XCGROUP_SUCCESS
 */
int xcgroup_get_param(xcgroup_t* cg, char* param, char **content,
		      size_t *csize);

/*
 * set a cgroup parameter in the form of a uint32_t
 *
 * param must correspond to a file of the cgroup that
 * will be written with the uint32_t value
 *
 * i.e. xcgroup_set_uint32_param(&cf,"memory.swappiness",value);
 *
 * returned values:
 *  - XCGROUP_ERROR
 *  - XCGROUP_SUCCESS
 */
int xcgroup_set_uint32_param(xcgroup_t* cg, char* parameter, uint32_t value);

/*
 * get a cgroup parameter in the form of a uint32_t
 *
 * param must correspond to a file of the cgroup that
 * will be read for its content
 *
 * i.e. xcgroup_get_uint32_param(&cg,"memory.swappiness",&value);
 *
 * returned values:
 *  - XCGROUP_ERROR
 *  - XCGROUP_SUCCESS
 */
int xcgroup_get_uint32_param(xcgroup_t* cg, char* param, uint32_t* value);

/*
 * set a cgroup parameter in the form of a uint64_t
 *
 * param must correspond to a file of the cgroup that
 * will be written with the uint64_t value
 *
 * i.e. xcgroup_set_uint64_param(&cf,"memory.swappiness",value);
 *
 * returned values:
 *  - XCGROUP_ERROR
 *  - XCGROUP_SUCCESS
 */
int xcgroup_set_uint64_param(xcgroup_t* cg, char* parameter, uint64_t value);

/*
 * get a cgroup parameter in the form of a uint64_t
 *
 * param must correspond to a file of the cgroup that
 * will be read for its content
 *
 * i.e. xcgroup_get_uint64_param(&cg,"memory.swappiness",&value);
 *
 * returned values:
 *  - XCGROUP_ERROR
 *  - XCGROUP_SUCCESS
 */
int xcgroup_get_uint64_param(xcgroup_t* cg, char* param, uint64_t* value);


/*
 * Move process 'pid' (and all its threads) to cgroup 'cg'
 *
 *  This call ensures that pid and all its threads are moved to the
 *   cgroup cg. If the cgroup.procs file is not writable, then threads
 *   must be moved individually and this call can be racy.
 *
 *  returns:
 *   - XCGROUP_ERROR
 *   - XCGROUP_SUCCESS
 */
int xcgroup_move_process(xcgroup_t *cg, pid_t pid);

/*
 * Wait for a pid to move out of a cgroup.
 *
 * Must call xcgroup_move_process before this function.
 */
int xcgroup_wait_pid_moved(xcgroup_t *cg, const char *cg_name);

#endif
