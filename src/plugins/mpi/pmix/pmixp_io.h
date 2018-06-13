/*****************************************************************************\
 **  pmix_io.h - PMIx non-blocking IO routines
 *****************************************************************************
 *  Copyright (C) 2014-2015 Artem Polyakov. All rights reserved.
 *  Copyright (C) 2015-2017 Mellanox Technologies. All rights reserved.
 *  Written by Artem Polyakov <artpol84@gmail.com, artemp@mellanox.com>.
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

#ifndef PMIXP_IO_H
#define PMIXP_IO_H

#include <poll.h>
#include "pmixp_common.h"
#include "pmixp_utils.h"

typedef enum {
	PMIXP_IO_NONE = 0,
	PMIXP_IO_INIT,
	PMIXP_IO_OPERATING,
	PMIXP_IO_CONN_CLOSED,
	PMIXP_IO_FINALIZED
} pmixp_io_state_t;

typedef struct {
#ifndef NDEBUG
#define PMIXP_MSGSTATE_MAGIC 0xC0FFEEEE
	int magic;
#endif
	/* User supplied information */
	int sd;
	int error;
	pmixp_p2p_data_t h;
	pmixp_io_state_t io_state;
	/* receiver */
	uint32_t rcvd_hdr_offs;
	void *rcvd_hdr_net;
	void *rcvd_hdr_host;
	uint32_t rcvd_pay_size;
	uint32_t rcvd_pay_offs;
	void *rcvd_payload;
	uint32_t rcvd_pad_recvd;
	/* sender */
	pthread_mutex_t send_lock;
	void *send_current;
	uint32_t send_offs;
	uint32_t send_msg_size;
	void *send_msg_ptr;
	List send_queue, complete_queue;
} pmixp_io_engine_t;

static inline int pmixp_io_fd(pmixp_io_engine_t *eng)
{
	return eng->sd;
}


static inline bool pmixp_io_rcvd_ready(pmixp_io_engine_t *eng)
{
	xassert(eng->magic == PMIXP_MSGSTATE_MAGIC);
	return (eng->rcvd_hdr_offs == eng->h.rhdr_net_size)
			&& (eng->rcvd_pay_size == eng->rcvd_pay_offs);
}

static inline bool pmixp_io_operating(pmixp_io_engine_t *eng)
{
	xassert(eng->magic == PMIXP_MSGSTATE_MAGIC);
	return (PMIXP_IO_OPERATING == eng->io_state);
}

static inline bool pmixp_io_conn_closed(pmixp_io_engine_t *eng)
{
	xassert(eng->magic == PMIXP_MSGSTATE_MAGIC);
	return (PMIXP_IO_CONN_CLOSED == eng->io_state);
}

static inline bool pmixp_io_enqueue_ok(pmixp_io_engine_t *eng)
{
	xassert(eng->magic == PMIXP_MSGSTATE_MAGIC);
	return (PMIXP_IO_OPERATING == eng->io_state) ||
			(PMIXP_IO_INIT == eng->io_state);
}

static inline bool pmixp_io_finalized(pmixp_io_engine_t *eng)
{
	xassert(eng->magic == PMIXP_MSGSTATE_MAGIC);
	return (PMIXP_IO_FINALIZED == eng->io_state);
}

static inline int pmixp_io_error(pmixp_io_engine_t *eng)
{
	xassert(eng->magic == PMIXP_MSGSTATE_MAGIC);
	return eng->error;
}

/* initialize all the data structures to prepare
 * engine for operation.
 * file descriptor needs to be provided to put it
 * to the operation mode
 */
void pmixp_io_init(pmixp_io_engine_t *eng,
		   pmixp_p2p_data_t header);

/* attach engine to the specific file descriptor */
static inline void
pmixp_io_attach(pmixp_io_engine_t *eng, int fd)
{
	/* Initialize general options */
	xassert(PMIXP_MSGSTATE_MAGIC == eng->magic);
	xassert(PMIXP_IO_INIT == eng->io_state);
	eng->sd = fd;
	eng->io_state = PMIXP_IO_OPERATING;
}

/* detach engine from the current file descriptor.
 * the `fd` is returned and can be used with other
 * engine if needed
 */
int pmixp_io_detach(pmixp_io_engine_t *eng);

/* cleanup all the data structures allocated by this
 * engine.
 * If engine wasn't detached, corresponding `fd` will
 * be also closed
 */
void pmixp_io_finalize(pmixp_io_engine_t *eng, int error);

/* Receiver */
void pmixp_io_rcvd_progress(pmixp_io_engine_t *eng);
void *pmixp_io_rcvd_extract(pmixp_io_engine_t *eng, void *header);
static inline void*
pmixp_io_recv_hdr_alloc_host(pmixp_io_engine_t *eng)
{
	xassert(eng->magic == PMIXP_MSGSTATE_MAGIC);
	return xmalloc(eng->h.rhdr_host_size);
}

static inline void*
pmixp_io_recv_hdr_alloc_net(pmixp_io_engine_t *eng)
{
	xassert(eng->magic == PMIXP_MSGSTATE_MAGIC);
	return xmalloc(eng->h.rhdr_net_size);
}

/* Transmitter */
/* thread-safe function, only calls Slurm list append */
int pmixp_io_send_enqueue(pmixp_io_engine_t *eng, void *msg);
int pmixp_io_send_urgent(pmixp_io_engine_t *eng, void *msg);
void pmixp_io_send_progress(pmixp_io_engine_t *eng);
bool pmixp_io_send_pending(pmixp_io_engine_t *eng);
void pmixp_io_send_cleanup(pmixp_io_engine_t *eng, pmixp_p2p_ctx_t ctx);

#endif /* PMIXP_IO_H */
