/*****************************************************************************\
 *  resctrl_fs.h - helpers for the Linux resctrl filesystem
 *****************************************************************************
 *  Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#ifndef _TASK_RESCTRL_FS_H
#define _TASK_RESCTRL_FS_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#include "src/common/list.h"

/* Where the kernel resctrl filesystem is mounted by convention. */
#define RESCTRL_MOUNT_POINT "/sys/fs/resctrl"

/*
 * Build the path of an entry of the resctrl filesystem.
 * IN group - group directory, or NULL for the root group
 * IN file - file name, or NULL for the directory itself
 * RET xmalloc()'d path
 */
extern char *resctrl_fs_path(const char *group, const char *file);

/*
 * Read a whole file.
 * IN path - file to read
 * OUT content - xmalloc()'d NUL terminated content
 * RET SLURM_SUCCESS or errno
 */
extern int resctrl_fs_read_file(const char *path, char **content);

/*
 * Write a string to a file in a single write(2), as the resctrl files
 * expect.
 * IN path - file to write
 * IN content - string to write
 * RET SLURM_SUCCESS or errno
 */
extern int resctrl_fs_write_file(const char *path, const char *content);

/*
 * RET true if the running kernel knows the resctrl filesystem type
 */
extern bool resctrl_fs_supported(void);

/*
 * RET true if resctrl is mounted on RESCTRL_MOUNT_POINT
 */
extern bool resctrl_fs_is_mounted(void);

/*
 * Mount resctrl on RESCTRL_MOUNT_POINT unless it already is. Failures are
 * logged, including resctrl being mounted on another directory.
 * IN options - mount options, may be NULL
 * RET SLURM_SUCCESS or errno
 */
extern int resctrl_fs_mount(const char *options);

/*
 * IN group - group name
 * RET true if the group directory exists
 */
extern bool resctrl_fs_group_exists(const char *group);

/*
 * List the CTRL_MON groups present under the root directory.
 * RET list of xmalloc()'d names, to free with FREE_NULL_LIST()
 */
extern list_t *resctrl_fs_list_groups(void);

/*
 * Number of CTRL_MON groups the hardware supports, including the root group:
 * the smallest num_closids of all the resources.
 * RET the limit, or 0 when it cannot be determined
 */
extern uint32_t resctrl_fs_max_groups(void);

/*
 * Create a group directory.
 * IN group - group name
 * RET SLURM_SUCCESS or errno (EEXIST if it already exists)
 */
extern int resctrl_fs_group_create(const char *group);

/*
 * Write the schemata of a group. A trailing newline is added when missing,
 * as the kernel requires one.
 * IN group - group name
 * IN schemata - contents of the schemata file
 * RET SLURM_SUCCESS or errno
 */
extern int resctrl_fs_write_schemata(const char *group, const char *schemata);

/*
 * Move every thread of a process into a group. Threads that exit while
 * being moved are ignored.
 * IN group - group name
 * IN pid - process whose threads are moved, or 0 for the calling thread only
 * RET SLURM_SUCCESS or errno
 */
extern int resctrl_fs_move_pid(const char *group, pid_t pid);

/*
 * RET xmalloc()'d explanation of the last failed resctrl operation as given
 * by the kernel, or NULL
 */
extern char *resctrl_fs_last_cmd_status(void);

#endif
