/*****************************************************************************\
 * src/common/io_hdr.h - IO connection header functions
 * $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark A. Grondona <mgrondona@llnl.gov>.
 *  CODE-OCEC-09-009. All rights reserved.
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

#ifndef _HAVE_IO_HDR_H
#define _HAVE_IO_HDR_H


#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#if HAVE_STDINT_H
#  include <stdint.h>
#endif
#if HAVE_INTTYPES_H
#  include <inttypes.h>
#endif

#include "src/common/macros.h"   /* Containes SLURM_CRED_SIGLEN */
#include "src/common/pack.h"
#include "src/common/cbuf.h"
#include "src/common/xmalloc.h"

#define MAX_MSG_LEN 1024
#define SLURM_IO_KEY_SIZE 8

#define SLURM_IO_STDIN 0
#define SLURM_IO_STDOUT 1
#define SLURM_IO_STDERR 2
#define SLURM_IO_ALLSTDIN 3
#define SLURM_IO_CONNECTION_TEST 4

struct slurm_io_init_msg {
	uint16_t      version;
	unsigned char cred_signature[SLURM_IO_KEY_SIZE];
	uint32_t      nodeid;
	uint32_t      stdout_objs;
	uint32_t      stderr_objs;
};


typedef struct slurm_io_header {
	uint16_t      type;
	uint16_t      gtaskid;
	uint16_t      ltaskid;
	uint32_t      length;
} io_hdr_t;

/*
 * Return the packed size of an IO header in bytes;
 */
int io_hdr_packed_size();
void io_hdr_pack(io_hdr_t *hdr, Buf buffer);
int io_hdr_unpack(io_hdr_t *hdr, Buf buffer);
int io_hdr_read_fd(int fd, io_hdr_t *hdr);

/*
 * Validate io init msg
 */
int io_init_msg_validate(struct slurm_io_init_msg *msg, const char *sig);
int io_init_msg_write_to_fd(int fd, struct slurm_io_init_msg *msg);
int io_init_msg_read_from_fd(int fd, struct slurm_io_init_msg *msg);

#endif /* !_HAVE_IO_HDR_H */
