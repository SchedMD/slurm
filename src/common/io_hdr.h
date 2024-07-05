/*****************************************************************************\
 * src/common/io_hdr.h - IO connection header functions
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark A. Grondona <mgrondona@llnl.gov>.
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

/*
 * srun I/O socket connection sequences:
 *
 * Connection Initialization:
 *
 * srun via io_initial_client_connect():
 *	slurmstepd sends: (packed via io_init_msg_pack())
 *	     uint32_t: length of packet
 *	     packed io_init_msg_t
 *	srun recieves via io_init_msg_read_from_fd()
 *	srun validates via io_init_msg_validate()
 *
 * sattach via io_client_connect():
 *
 *
 * Same message format sent bidirectionally after initialization:
 *	packed io_hdr_t via io_hdr_pack()
 *	io_hdr_t.length bytes of payload
 *
 *	srun only sends types:
 *		SLURM_IO_CONNECTION_TEST
 *		SLURM_IO_STDIN
 *		SLURM_IO_ALLSTDIN
 *
 *	slurmstepd honors task_read_info.type to determine where messages sent.
 *
 * Connection ends with io_hdr_t.length=0 packet with no payload
 */

#ifndef _HAVE_IO_HDR_H
#define _HAVE_IO_HDR_H

#include <inttypes.h>

#include "src/common/pack.h"

#define SLURM_IO_MAX_MSG_LEN 1024

typedef enum {
	SLURM_IO_INVALID = -1,
	SLURM_IO_STDIN = 0,
	SLURM_IO_STDOUT = 1,
	SLURM_IO_STDERR = 2,
	SLURM_IO_ALLSTDIN = 3,
	SLURM_IO_CONNECTION_TEST = 4,
	SLURM_IO_INVALID_MAX
} io_hdr_type_t;

typedef struct {
	uint16_t      version;
	char          *io_key;
	uint32_t      nodeid;
	uint32_t      stdout_objs;
	uint32_t      stderr_objs;
} io_init_msg_t;


typedef struct {
	io_hdr_type_t type;
	uint16_t      gtaskid;
	uint16_t      ltaskid;
	uint32_t      length;
} io_hdr_t;

/*
 * IO Header is always written/read with the exact same number of bytes
 * Changing this count will break older srun clients as this packet is
 * unversioned.
 */
#define IO_HDR_PACKET_BYTES 10

/*
 * Return the packed size of an IO header in bytes;
 */
void io_hdr_pack(io_hdr_t *hdr, buf_t *buffer);
/*
 * Pack io_hdr_t into buffer
 * IN hdr - struct to pack
 * IN buffer - destination buffer to populate
 * RET SLURM_SUCCESS or
 * 	EAGAIN if buffer does not container entire packet
 * 	or error
 */
int io_hdr_unpack(io_hdr_t *hdr, buf_t *buffer);
int io_hdr_read_fd(int fd, io_hdr_t *hdr);

/*
 * Validate io init msg
 */
int io_init_msg_validate(io_init_msg_t *msg, const char *sig);
int io_init_msg_write_to_fd(int fd, io_init_msg_t *msg);
int io_init_msg_read_from_fd(int fd, io_init_msg_t *msg);

#endif /* !_HAVE_IO_HDR_H */
