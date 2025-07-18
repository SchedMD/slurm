/*****************************************************************************\
 *  slurm_protocol_common.h - slurm communications definitions common to
 *	all protocols
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Kevin Tew <tew1@llnl.gov>, et. al.
 *  CODE-OCEC-09-009. All rights reserved.
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

#ifndef _SLURM_PROTOCOL_COMMON_H
#define _SLURM_PROTOCOL_COMMON_H

#include "config.h"

/* for listen API */
#define SLURM_DEFAULT_LISTEN_BACKLOG 4096

/* slurm protocol header defines, based upon config.h, 16 bits */
/* A new SLURM_PROTOCOL_VERSION needs to be made each time the version
 * changes so the slurmdbd can talk all versions for update messages.
 */
/* NOTE: The API version can not be the same as the Slurm version.  The
 *       version in the code is referenced as a uint16_t which if 1403 was the
 *       api it would go over the limit.  So keep is a relatively
 *       small number.
 * NOTE: These values must be moved to
 * src/plugins/accounting_storage/mysql/as_mysql_archive.c when we are
 * done here with them since we have to support old version of archive
 * files since they don't update once they are created.
 */
#define MAKE_SLURM_VER(r) (((r) << 8) | 0)
#define DEC_SLURM_VER(ver, offset) MAKE_SLURM_VER(((ver) >> 8) - offset)

#define SLURM_25_11_PROTOCOL_VERSION MAKE_SLURM_VER(44)
#define SLURM_25_05_PROTOCOL_VERSION MAKE_SLURM_VER(43)
#define SLURM_24_11_PROTOCOL_VERSION MAKE_SLURM_VER(42)
#define SLURM_24_05_PROTOCOL_VERSION MAKE_SLURM_VER(41)

#define SLURM_PROTOCOL_VERSION SLURM_25_11_PROTOCOL_VERSION
#define SLURM_ONE_BACK_PROTOCOL_VERSION DEC_SLURM_VER(SLURM_PROTOCOL_VERSION, 1)
#define SLURM_TWO_BACK_PROTOCOL_VERSION DEC_SLURM_VER(SLURM_PROTOCOL_VERSION, 2)
#define SLURM_MIN_PROTOCOL_VERSION DEC_SLURM_VER(SLURM_PROTOCOL_VERSION, 3)

#if 0
/* Old Slurm versions kept for reference only.  Slurm only actively keeps track
 * of 2 previous versions. */
#define SLURM_23_11_PROTOCOL_VERSION ((40 << 8) | 0)
#define SLURM_23_02_PROTOCOL_VERSION ((39 << 8) | 0)
#define SLURM_22_05_PROTOCOL_VERSION ((38 << 8) | 0)
#define SLURM_21_08_PROTOCOL_VERSION ((37 << 8) | 0)
#define SLURM_20_11_PROTOCOL_VERSION ((36 << 8) | 0)
#define SLURM_20_02_PROTOCOL_VERSION ((35 << 8) | 0)
#define SLURM_19_05_PROTOCOL_VERSION ((34 << 8) | 0)
#define SLURM_18_08_PROTOCOL_VERSION ((33 << 8) | 0)
#define SLURM_17_11_PROTOCOL_VERSION ((32 << 8) | 0)
#define SLURM_17_02_PROTOCOL_VERSION ((31 << 8) | 0)
#define SLURM_16_05_PROTOCOL_VERSION ((30 << 8) | 0)
#define SLURM_15_08_PROTOCOL_VERSION ((29 << 8) | 0)
#define SLURM_14_11_PROTOCOL_VERSION ((28 << 8) | 0)
#define SLURM_14_03_PROTOCOL_VERSION ((27 << 8) | 0)
#define SLURM_2_6_PROTOCOL_VERSION ((26 << 8) | 0)
#define SLURM_2_5_PROTOCOL_VERSION ((25 << 8) | 0)
#define SLURM_2_4_PROTOCOL_VERSION ((24 << 8) | 0)
#define SLURM_2_3_PROTOCOL_VERSION ((23 << 8) | 0)
#define SLURM_2_2_PROTOCOL_VERSION ((22 << 8) | 0)
#define SLURM_2_1_PROTOCOL_VERSION ((21 << 8) | 0)
#define SLURM_2_0_PROTOCOL_VERSION ((20 << 8) | 0)
#define SLURM_1_3_PROTOCOL_VERSION ((13 << 8) | 0)
#endif

/* Below are flags for the header of a message. */

/* Used to set flags to empty */
#define SLURM_PROTOCOL_NO_FLAGS 0
#define SLURM_GLOBAL_AUTH_KEY   SLURM_BIT(0)
#define SLURMDBD_CONNECTION     SLURM_BIT(1)
#define SLURM_MSG_KEEP_BUFFER   SLURM_BIT(2)
/* was  SLURM_DROP_PRIV		SLURM_BIT(3) */
#define USE_BCAST_NETWORK	SLURM_BIT(4)
#define CTLD_QUEUE_PROCESSING	SLURM_BIT(5)
#define SLURM_NO_AUTH_CRED	SLURM_BIT(6)
#define SLURM_PACK_ADDRS	SLURM_BIT(7)

#endif
