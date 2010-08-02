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

typedef struct xcgroup_opts {

	uid_t uid;        /* uid of the owner */
	gid_t gid;        /* gid of the owner */

	int create_only;  /* do nothing if the cgroup already exists */
	int notify;       /* notify_on_release flag value (0/1) */

} xcgroup_opts_t;

/*
 * test if cgroup system is currently available (mounted)
 *
 * returned values:
 *  - 0 if not available
 *  - 1 if available
 */
int xcgroup_is_available();

/*
 * mount the cgroup system using given options
 *
 * returned values:
 *  - XCGROUP_ERROR
 *  - XCGROUP_SUCCESS
 */
int xcgroup_mount(char* mount_opts);

/*
 * set cgroup system release agent
 *
 * returned values:
 *  - XCGROUP_ERROR
 *  - XCGROUP_SUCCESS
 */
int xcgroup_set_release_agent(char* agent);

/*
 * create a cgroup according to input properties
 *
 * returned values:
 *  - XCGROUP_ERROR
 *  - XCGROUP_SUCCESS
 */
int xcgroup_create(char* cpath, xcgroup_opts_t* opts);

/*
 * destroy a cgroup (do nothing for now)
 *
 * returned values:
 *  - XCGROUP_ERROR
 *  - XCGROUP_SUCCESS
 */
int xcgroup_destroy(char* cpath);

/*
 * add a list of pids to a cgroup
 *
 * returned values:
 *  - XCGROUP_ERROR
 *  - XCGROUP_SUCCESS
 */
int xcgroup_add_pids(char* cpath,pid_t* pids,int npids);

/*
 * extract the pids list of a cgroup
 *
 * pids array must be freed using xfree(...)
 *
 * returned values:
 *  - XCGROUP_ERROR
 *  - XCGROUP_SUCCESS
 */
int xcgroup_get_pids(char* cpath, pid_t **pids, int *npids);

/*
 * return the cpath containing the input pid
 *
 * returned values:
 *  - XCGROUP_ERROR
 *  - XCGROUP_SUCCESS
 */
int xcgroup_find_by_pid(char* cpath, pid_t pid);

/*
 * set cgroup memory limit to the value ot memlimit
 *
 * memlimit must be expressed in MB
 *
 * returned values:
 *  - XCGROUP_ERROR
 *  - XCGROUP_SUCCESS
 */
int xcgroup_set_memlimit(char* cpath,uint32_t memlimit);

/*
 * get cgroup memory limit
 *
 * memlimit will be expressed in MB
 *
 * returned values:
 *  - XCGROUP_ERROR
 *  - XCGROUP_SUCCESS
 */
int xcgroup_get_memlimit(char* cpath,uint32_t* memlimit);

/*
 * set cgroup mem+swap limit to the value ot memlimit
 *
 * memlimit must be expressed in MB
 *
 * returned values:
 *  - XCGROUP_ERROR
 *  - XCGROUP_SUCCESS
 */
int xcgroup_set_memswlimit(char* cpath,uint32_t memlimit);

/*
 * get cgroup mem+swap limit
 *
 * memlimit will be expressed in MB
 *
 * returned values:
 *  - XCGROUP_ERROR
 *  - XCGROUP_SUCCESS
 */
int xcgroup_get_memswlimit(char* cpath,uint32_t* memlimit);

/*
 * toggle memory use hierarchy behavior using flag value
 *
 * flag values are 0/1 to disable/enable the feature
 *
 * returned values:
 *  - XCGROUP_ERROR
 *  - XCGROUP_SUCCESS
 */
int xcgroup_set_mem_use_hierarchy(char* cpath,int flag);

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
int xcgroup_set_cpuset_cpus(char* cpath,char* range);

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
int xcgroup_set_params(char* cpath,char* parameters);

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
int xcgroup_get_param(char* cpath,char* param,char **content,size_t *csize);

#endif
