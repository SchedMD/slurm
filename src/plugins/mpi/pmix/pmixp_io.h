/*****************************************************************************\
 **  pmix_io.h - PMIx non-blocking IO routines
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

#ifndef PMIXP_IO_H
#define PMIXP_IO_H

#include <poll.h>
#include "pmixp_common.h"
#include "pmixp_utils.h"

/* Message management */

typedef uint32_t (*pmix_io_engine_hsize_cb_t)(void *hdr);
typedef int (*pmix_io_engine_hpack_cb_t)(void *hdr_host, void *hdr_net);
typedef int (*pmix_io_engine_hunpack_cb_t)(void *hdr_net, void *hdr_host);
typedef struct {
	uint32_t host_size, net_size;
	pmix_io_engine_hpack_cb_t pack_hdr_cb;
	pmix_io_engine_hunpack_cb_t unpack_hdr_cb;
	pmix_io_engine_hsize_cb_t pay_size_cb;
} pmixp_io_engine_header_t;

typedef struct {
#ifndef NDEBUG
#define PMIX_MSGSTATE_MAGIC 0xC0FFEEEE
	int magic;
#endif
	/* User supplied information */
	int sd;
	int error;
	pmixp_io_engine_header_t header;
	bool operating;
	/* receiver */
	uint32_t rcvd_hdr_offs;
	void *rcvd_hdr;
	void *rcvd_hdr_host;
	uint32_t rcvd_pay_size;
	uint32_t rcvd_pay_offs;
	void *rcvd_payload;
	uint32_t rcvd_padding;
	uint32_t rcvd_pad_recvd;
	/* sender */
	void *send_current;
	void *send_hdr_net;
	uint32_t send_hdr_offs;
	uint32_t send_hdr_size;
	void *send_payload;
	uint32_t send_pay_offs;
	uint32_t send_pay_size;
	List send_queue;
} pmixp_io_engine_t;

static inline void pmix_io_rcvd_padding(pmixp_io_engine_t *eng,
		uint32_t padsize)
{
	xassert(eng->magic == PMIX_MSGSTATE_MAGIC);
	eng->rcvd_padding = padsize;
}

static inline bool pmix_io_rcvd_ready(pmixp_io_engine_t *eng)
{
	xassert(eng->magic == PMIX_MSGSTATE_MAGIC);
	return (eng->rcvd_hdr_offs == eng->header.net_size)
			&& (eng->rcvd_pay_size == eng->rcvd_pay_offs);
}

static inline bool pmix_io_finalized(pmixp_io_engine_t *eng)
{
	xassert(eng->magic == PMIX_MSGSTATE_MAGIC);
	return !(eng->operating);
}

static inline int pmix_io_error(pmixp_io_engine_t *eng)
{
	xassert(eng->magic == PMIX_MSGSTATE_MAGIC);
	return eng->error;
}

void pmix_io_init(pmixp_io_engine_t *eng, int fd,
		pmixp_io_engine_header_t header);
void pmix_io_finalize(pmixp_io_engine_t *eng, int error);

/* Receiver */
int pmix_io_first_header(int fd, void *buf, uint32_t *_offs, uint32_t len);
void pmix_io_rcvd(pmixp_io_engine_t *eng);
void *pmix_io_rcvd_extract(pmixp_io_engine_t *eng, void *header);
/* Transmitter */
void pmix_io_send_enqueue(pmixp_io_engine_t *eng, void *msg);
void pmix_io_send_progress(pmixp_io_engine_t *eng);
bool pmix_io_send_pending(pmixp_io_engine_t *eng);

#endif /* PMIXP_IO_H */
