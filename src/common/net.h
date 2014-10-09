/*****************************************************************************\
 *  net.h - basic network communications for user application I/O
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <grondona1@llnl.gov>, Kevin Tew <tew1@llnl.gov>,
 *  et. al.
 *  CODE-OCEC-09-009. All rights reserved.
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

#ifndef _NET_H
#define _NET_H

#include <stdint.h>
#include <sys/socket.h>

#include "src/common/macros.h"

/* open a stream socket on an ephemereal port and put it into
 * the listen state. fd and port are filled in with the new
 * socket's file descriptor and port #.
 *
 * OUT fd - listening socket file descriptor number
 * OUT port - TCP port number in host byte order
 */
extern int net_stream_listen(int *fd, uint16_t *port);

/* accept the incoming connection on the stream socket fd
 */
extern int net_accept_stream(int fd);

/* set low water mark on socket
 */
extern int net_set_low_water(int sock, socklen_t size);

/* set keep alive time on socket */
extern int net_set_keep_alive(int sock);

extern int net_stream_listen_ports(int *, uint16_t *, uint16_t *);

#endif /* !_NET_H */
