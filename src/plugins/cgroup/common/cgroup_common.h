/*****************************************************************************\
 *  cgroup_common.h - Cgroup plugin common header file
 *****************************************************************************
 *  Copyright (C) 2021 SchedMD LLC
 *  Written by Felip Moll <felip.moll@schedmd.com>
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

#ifndef _CGROUP_COMMON_H
#define _CGROUP_COMMON_H

#include <fcntl.h>
#include <limits.h>
#include <sys/file.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/log.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

typedef struct {
	bitstr_t *avail_controllers;
	char *mnt_point;	/* mount point to use */
	char *mnt_args;		/* additional mount args */
	char *subsystems;	/* comma-separated subsystems to provide */
} xcgroup_ns_t;

typedef struct {
	xcgroup_ns_t *ns;	/* xcgroup namespace of this xcgroup */
	char *name;		/* name of the xcgroup relative to the ns */
	char *path;		/* absolute path of the xcgroup in the ns */
	uid_t uid;		/* uid of the owner */
	gid_t gid;		/* gid of the owner */
	int fd;			/* used for locking */
} xcgroup_t;

extern size_t common_file_getsize(int fd);
extern int common_file_write_uint64s(char *file_path, uint64_t *values, int nb);
extern int common_file_read_uint64s(char *file_path, uint64_t **pvalues,
				    int *pnb);
extern int common_file_write_uint32s(char *file_path, uint32_t *values, int nb);
extern int common_file_read_uint32s(char *file_path, uint32_t **pvalues,
				    int *pnb);
extern int common_file_write_content(char *file_path, char *content,
				     size_t csize);
extern int common_file_read_content(char *file_path, char **content,
				    size_t *csize);

/*
 * instantiate a cgroup in a cgroup namespace (mkdir)
 *
 * returned values:
 *  - SLURM_ERROR
 *  - SLURM_SUCCESS
 */
extern int common_cgroup_instantiate(xcgroup_t *cg);

/*
 * create a cgroup structure
 *
 * returned values:
 *  - SLURM_ERROR
 *  - SLURM_SUCCESS
 */
extern int common_cgroup_create(xcgroup_ns_t *cgns, xcgroup_t *cg, char *uri,
				uid_t uid, gid_t gid);

/*
 * Move process 'pid' (and all its threads) to cgroup 'cg'
 *
 *  This call ensures that pid and all its threads are moved to the
 *   cgroup cg. If the cgroup.procs file is not writable, then threads
 *   must be moved individually and this call can be racy.
 *
 *  returns:
 *   - SLURM_ERROR
 *   - SLURM_SUCCESS
 */
extern int common_cgroup_move_process(xcgroup_t *cg, pid_t pid);

/*
 * set a cgroup parameter
 *
 * param must correspond to a file of the cgroup that
 * will be written with the value content
 *
 * i.e. common_cgroup_set_params(&cf, "memory.swappiness", "10");
 *
 * returned values:
 *  - SLURM_ERROR
 *  - SLURM_SUCCESS
 */
extern int common_cgroup_set_param(xcgroup_t *cg, char *param, char *content);

/*
 * destroy a cgroup namespace
 */
extern void common_cgroup_ns_destroy(xcgroup_ns_t *cgns);

/*
 * destroy a cgroup internal structure
 */
extern void common_cgroup_destroy(xcgroup_t *cg);

/*
 * delete a cgroup instance in a cgroup namespace (rmdir)
 *
 * returned values:
 *  - SLURM_ERROR
 *  - SLURM_SUCCESS
 */
extern int common_cgroup_delete(xcgroup_t *cg);

/*
 * add a list of pids to a cgroup
 *
 * returned values:
 *  - SLURM_ERROR
 *  - SLURM_SUCCESS
 */
extern int common_cgroup_add_pids(xcgroup_t *cg, pid_t *pids, int npids);

/*
 * extract the pids list of a cgroup
 *
 * pids array must be freed using xfree(...)
 *
 * returned values:
 *  - SLURM_ERROR
 *  - SLURM_SUCCESS
 */
extern int common_cgroup_get_pids(xcgroup_t *cg, pid_t **pids, int *npids);

/*
 * get a cgroup parameter
 *
 * param must correspond to a file of the cgroup that
 * will be read for its content
 *
 * i.e. common_cgroup_get_param(&cg, "memory.swappiness", &value, &size);
 *
 * on success, content must be free using xfree
 *
 * returned values:
 *  - SLURM_ERROR
 *  - SLURM_SUCCESS
 */
extern int common_cgroup_get_param(xcgroup_t *cg, char *param, char **content,
				   size_t *csize);

/*
 * set a cgroup parameter in the form of a uint64_t
 *
 * param must correspond to a file of the cgroup that
 * will be written with the uint64_t value
 *
 * i.e. common_cgroup_set_uint64_param(&cf, "memory.swappiness", value);
 *
 * returned values:
 *  - SLURM_ERROR
 *  - SLURM_SUCCESS
 */
extern int common_cgroup_set_uint64_param(xcgroup_t *cg, char *parameter,
					  uint64_t value);

/*
 * Use filesystem lock over a cgroup path typically to avoid removal from one
 * step when another one is creating it.
 *
 * IN cg - Cgroup object containing path to lock.
 * RETURN SLURM_SUCCESS if lock was successful, SLURM_ERROR otherwise.
 */
extern int common_cgroup_lock(xcgroup_t *cg);

/*
 * Unlock a cgroup using filesystem lock.
 *
 * IN cg - Cgroup object containing path to unlock.
 * RETURN SLURM_SUCCESS if unlock was successful, SLURM_ERROR otherwise.
 */
extern int common_cgroup_unlock(xcgroup_t *cg);

#endif /* !_CGROUP_COMMON_H */
