/*****************************************************************************\
 *  slurm_protocol_common.h - slurm communications definitions common to
 *	all protocols
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Kevin Tew <tew1@llnl.gov>, et. al.
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

#ifndef _SLURM_PROTOCOL_COMMON_H
#define _SLURM_PROTOCOL_COMMON_H

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include <sys/time.h>
#include <time.h>

#include "slurm/slurm_errno.h"

/* for sendto and recvfrom commands */
#define SLURM_PROTOCOL_NO_SEND_RECV_FLAGS 0

/* for listen API */
#ifndef SLURM_PROTOCOL_DEFAULT_LISTEN_BACKLOG
#define SLURM_PROTOCOL_DEFAULT_LISTEN_BACKLOG 4096
#endif

/* used in interface methods */
#define SLURM_PROTOCOL_FUNCTION_NOT_IMPLEMENTED -2

/* max slurm message send and receive buffer size
 * this may need to be increased to 350k-512k */
#define SLURM_PROTOCOL_MAX_MESSAGE_BUFFER_SIZE (512*1024)

/* slurm protocol header defines, based upon config.h, 16 bits */
/* A new SLURM_PROTOCOL_VERSION needs to be made each time the version
 * changes so the slurmdbd can talk all versions for update messages.
 * In slurm_protocol_util.c check_header_version(), and init_header()
 * need to be updated also when changes are added */
/* NOTE: The API version can not be the same as the Slurm version.  The
 *       version in the code is referenced as a uint16_t which if 1403 was the
 *       api it would go over the limit.  So keep is a relatively
 *       small number.
*/
#define SLURM_14_11_PROTOCOL_VERSION ((28 << 8) | 0)
#define SLURM_14_03_PROTOCOL_VERSION ((27 << 8) | 0)
#define SLURM_2_6_PROTOCOL_VERSION ((26 << 8) | 0)

#define SLURM_PROTOCOL_VERSION SLURM_14_11_PROTOCOL_VERSION
#define SLURM_MIN_PROTOCOL_VERSION SLURM_2_6_PROTOCOL_VERSION

#if 0
/* SLURM version 14.11 code removed support for protocol versions before 2.5 */
#define SLURM_2_5_PROTOCOL_VERSION ((25 << 8) | 0)
#define SLURM_2_4_PROTOCOL_VERSION ((24 << 8) | 0)
#define SLURM_2_3_PROTOCOL_VERSION ((23 << 8) | 0)
#define SLURM_2_2_PROTOCOL_VERSION ((22 << 8) | 0)
#define SLURM_2_1_PROTOCOL_VERSION ((21 << 8) | 0)
#define SLURM_2_0_PROTOCOL_VERSION ((20 << 8) | 0)
#define SLURM_1_3_PROTOCOL_VERSION ((13 << 8) | 0)
#endif

/* used to set flags to empty */
#define SLURM_PROTOCOL_NO_FLAGS 0
#define SLURM_GLOBAL_AUTH_KEY   0x0001

#include "src/common/slurm_protocol_socket_common.h"

#endif
