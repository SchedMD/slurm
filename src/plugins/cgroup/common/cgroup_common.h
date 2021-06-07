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
#include <unistd.h>
#include <sys/mount.h>
#include <sys/file.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/log.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/slurm_protocol_defs.h"

extern size_t common_file_getsize(int fd);
extern int common_file_write_uint64s(char* file_path, uint64_t* values, int nb);
extern int common_file_read_uint64s(char* file_path, uint64_t** pvalues,
				    int* pnb);
extern int common_file_write_uint32s(char* file_path, uint32_t* values, int nb);
extern int common_file_read_uint32s(char* file_path, uint32_t** pvalues,
				    int* pnb);
extern int common_file_write_content(char* file_path, char* content,
				     size_t csize);
extern int common_file_read_content(char* file_path, char** content,
				    size_t *csize);
#endif /* !_CGROUP_COMMON_H */
