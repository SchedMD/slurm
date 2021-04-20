/*****************************************************************************\
 *  callerid.h - Identify initiator of ssh connections, etc
 *****************************************************************************
 *  Copyright (C) 2015, Brigham Young University
 *  Author:  Ryan Cox <ryan_cox@byu.edu>
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

#ifndef _SLURM_CALLERID_H
#define _SLURM_CALLERID_H

#include <arpa/inet.h>
#include <sys/types.h>

#ifdef __FreeBSD__
#include <sys/socket.h>
#include <netinet/in.h>
#endif

typedef struct {
	uint32_t port_dst;
	uint32_t port_src;
	struct in6_addr ip_dst;
	struct in6_addr ip_src;
	int af;
} callerid_conn_t;

extern int callerid_get_own_netinfo(callerid_conn_t *conn);
extern int callerid_find_inode_by_conn(callerid_conn_t conn, ino_t *inode);
extern int callerid_find_conn_by_inode(callerid_conn_t *conn, ino_t inode);
extern int find_pid_by_inode (pid_t *pid_result, ino_t inode);

#endif /* _SLURM_CALLERID_H */
