/*****************************************************************************\
 **	pmix_utils.c - Various PMIx utility functions
 *****************************************************************************
 *  Copyright (C) 2014-2015 Artem Polyakov. All rights reserved.
 *  Copyright (C) 2015      Mellanox Technologies. All rights reserved.
 *  Written by Artem Polyakov <artpol84@gmail.com, artemp@mellanox.com>.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
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

#ifndef PMIXP_UTILS_H
#define PMIXP_UTILS_H

#include "pmixp_common.h"

void pmixp_xfree_xmalloced(void *x);
void pmixp_free_Buf(void *x);
int pmixp_usock_create_srv(char *path);
size_t pmixp_read_buf(int fd, void *buf, size_t count, int *shutdown,
		bool blocking);
size_t pmixp_write_buf(int fd, void *buf, size_t count, int *shutdown,
		bool blocking);
bool pmixp_fd_read_ready(int fd, int *shutdown);
bool pmixp_fd_write_ready(int fd, int *shutdown);
int pmixp_srun_send(slurm_addr_t *addr, uint32_t len, char *data);
int pmixp_stepd_send(char *nodelist, const char *address, char *data,
		uint32_t len);
int pmixp_rmdir_recursively(char *path);

#endif /* PMIXP_UTILS_H*/
