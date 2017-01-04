/*****************************************************************************\
 **  pmix_io.c - PMIx non-blocking IO routines
 *****************************************************************************
 *  Copyright (C) 2014-2015 Artem Polyakov. All rights reserved.
 *  Copyright (C) 2015      Mellanox Technologies. All rights reserved.
 *  Written by Artem Polyakov <artpol84@gmail.com, artemp@mellanox.com>.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
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
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>

#include "pmixp_common.h"
#include "pmixp_io.h"
#include "pmixp_debug.h"
#include "pmixp_utils.h"

static void
_verify_transceiver(pmixp_io_engine_header_t header)
{
	/* sanity checks */
	if (NULL == header.payload_size_cb){
		PMIXP_ERROR("No payload size callback provided");
		goto check_fail;
	}

	if( header.recv_on ){
		if (0 == header.recv_host_hsize){
			PMIXP_ERROR("Bad host header size");
			goto check_fail;
		}
		if (0 == header.recv_net_hsize){
			PMIXP_ERROR("Bad net header size");
			goto check_fail;
		}
		if (NULL == header.hdr_unpack_cb){
			PMIXP_ERROR("No header unpack callback provided");
			goto check_fail;
		}
	}

	/* sanity checks */
	if( header.send_on ){
		if (0 == header.send_host_hsize){
			PMIXP_ERROR("Bad host header size");
			goto check_fail;
		}
		if (0 == header.send_net_hsize){
			PMIXP_ERROR("Bad net header size");
			goto check_fail;
		}
		if (NULL == header.hdr_ptr_cb){
			PMIXP_ERROR("No header pointer callback provided");
			goto check_fail;
		}
		if (NULL == header.hdr_pack_cb){
			PMIXP_ERROR("No header pack callback provided");
			goto check_fail;
		}
		if (NULL == header.payload_ptr_cb){
			PMIXP_ERROR("No payload pointer callback provided");
			goto check_fail;
		}
		if (NULL == header.msg_free_cb){
			PMIXP_ERROR("No message free callback provided");
			goto check_fail;
		}
	}
	return;
check_fail:
	abort();
}

void pmixp_io_init(pmixp_io_engine_t *eng,
		  pmixp_io_engine_header_t header)
{
	memset(eng, 0, sizeof(*eng));
	/* Initialize general options */
#ifndef NDEBUG
	eng->magic = PMIX_MSGSTATE_MAGIC;
#endif
	eng->error = 0;
	eng->h = header;
	eng->io_state = PMIXP_IO_INIT;

	/* Init receiver */
	eng->rcvd_pay_size = 0;
	eng->rcvd_payload = NULL;
	eng->rcvd_hdr_offs = eng->rcvd_pay_offs = 0;

	_verify_transceiver(header);

	if( eng->h.recv_on ){
		/* we are going to receive data */
		eng->rcvd_hdr_net = xmalloc(eng->h.recv_net_hsize);
		eng->rcvd_hdr_host = xmalloc(eng->h.recv_host_hsize);
	} else {
		/* receiver won't be used */
		eng->rcvd_hdr_host = NULL;
		eng->rcvd_hdr_net = NULL;
	}

	/* Init transmitter */
	eng->send_current = NULL;
	eng->send_hdr_size = eng->send_hdr_offs = 0;
	eng->send_payload = NULL;
	eng->send_pay_size = eng->send_pay_offs = 0;
	eng->send_queue = list_create(eng->h.msg_free_cb);
	if (eng->h.send_on) {
		/* we are going to send data */
		eng->send_hdr_net = xmalloc(eng->h.send_net_hsize);
	} else {
		/* transmitter won't be used */
		eng->send_hdr_net = NULL;
	}
}

static inline void
_pmixp_io_drop_messages(pmixp_io_engine_t *eng)
{
	if (eng->h.recv_on) {
		if (NULL != eng->rcvd_payload) {
			xfree(eng->rcvd_payload);
			eng->rcvd_payload = NULL;
		}
		eng->rcvd_pay_size = 0;
		eng->rcvd_payload = NULL;
		eng->rcvd_hdr_offs = eng->rcvd_pay_offs = 0;
	}

	if (eng->h.send_on) {
		if (list_count(eng->send_queue)) {
			list_destroy(eng->send_queue);
		}
		if (NULL != eng->send_current) {
			eng->h.msg_free_cb(eng->send_current);
			eng->send_current = NULL;
		}
		eng->send_payload = NULL;
		eng->send_pay_size = eng->send_pay_offs = 0;
	}
}

int pmixp_io_detach(pmixp_io_engine_t *eng)
{
	int ret = 0;
	/* Initialize general options */
	xassert(PMIX_MSGSTATE_MAGIC == eng->magic);
	xassert(PMIXP_IO_OPERATING == eng->io_state ||
		PMIXP_IO_CONN_CLOSED == eng->io_state);
	_pmixp_io_drop_messages(eng);
	ret = eng->sd;
	eng->sd = -1;
	eng->io_state = PMIXP_IO_INIT;
	return ret;
}

void pmixp_io_finalize(pmixp_io_engine_t *eng, int err)
{
	switch (eng->io_state)
	{
	case PMIXP_IO_FINALIZED:
		/* avoid double finalization */
		PMIXP_ERROR("Attempt to finalize already finalized I/O engine");
		return;
	case PMIXP_IO_OPERATING:
		close(eng->sd);
		eng->sd = -1;
		/* fall throug to init cleanup */
	case PMIXP_IO_INIT:
		/* release all messages in progress */
		_pmixp_io_drop_messages(eng);

		/* Release all receiver resources*/
		if( eng->h.recv_on ){
			xfree(eng->rcvd_hdr_net);
			xfree(eng->rcvd_hdr_host);
			eng->rcvd_hdr_net = NULL;
			eng->rcvd_hdr_host = NULL;
		}

		/* Release all sender resources*/
		if( eng->h.send_on ){
			xfree(eng->send_hdr_net);
			eng->send_hdr_size = eng->send_hdr_offs = 0;
		}
		break;
	case PMIXP_IO_NONE:
		PMIXP_ERROR("Attempt to finalize non-initialized I/O engine");
		break;
	default:
		PMIXP_ERROR("I/O engine was damaged, unknown state: %d", (int)eng->io_state);
		break;
	}

	eng->io_state = PMIXP_IO_NONE;
	if (err < 0) {
		eng->error = -err;
	} else {
		eng->error = 0;
	}
}

/* Receiver */

static inline void _rcvd_next_message(pmixp_io_engine_t *eng)
{
	xassert(PMIX_MSGSTATE_MAGIC == eng->magic);
	xassert(NULL != eng->rcvd_hdr_net);
	xassert(pmixp_io_operating(eng));
	xassert(eng->h.recv_on);

	eng->rcvd_pad_recvd = 0;
	eng->rcvd_hdr_offs = 0;
	eng->rcvd_pay_offs = eng->rcvd_pay_size = 0;
	eng->rcvd_payload = NULL;

}

static inline int _rcvd_swithch_to_body(pmixp_io_engine_t *eng)
{
	int rc;
	xassert(PMIX_MSGSTATE_MAGIC == eng->magic);
	xassert(NULL != eng->rcvd_hdr_net);
	xassert(pmixp_io_operating(eng));
	xassert(eng->h.recv_on);
	xassert(eng->h.recv_net_hsize == eng->rcvd_hdr_offs);

	eng->rcvd_pay_offs = eng->rcvd_pay_size = 0;
	eng->rcvd_payload = NULL;
	rc = eng->h.hdr_unpack_cb(eng->rcvd_hdr_net, eng->rcvd_hdr_host);
	if (0 != rc) {
		PMIXP_ERROR_NO(rc, "Cannot unpack message header");
		return rc;
	}
	eng->rcvd_pay_size = eng->h.payload_size_cb(eng->rcvd_hdr_host);
	if (0 != eng->rcvd_pay_size) {
		eng->rcvd_payload = xmalloc(eng->rcvd_pay_size);
	}
	return 0;
}

static inline bool _rcvd_have_padding(pmixp_io_engine_t *eng)
{
	xassert(PMIX_MSGSTATE_MAGIC == eng->magic);
	xassert(NULL != eng->rcvd_hdr_net);
	xassert(pmixp_io_operating(eng));
	xassert(eng->h.recv_on);
	return eng->h.recv_padding && (eng->rcvd_pad_recvd < eng->h.recv_padding);
}

static inline bool _rcvd_need_header(pmixp_io_engine_t *eng)
{
	xassert(PMIX_MSGSTATE_MAGIC == eng->magic);
	xassert(NULL != eng->rcvd_hdr_net);
	xassert(pmixp_io_operating(eng));
	xassert(eng->h.recv_on);

	return eng->rcvd_hdr_offs < eng->h.recv_net_hsize;
}

void pmixp_io_rcvd_progress(pmixp_io_engine_t *eng)
{
	size_t size, remain;
	void *offs;
	int shutdown;
	int fd = eng->sd;

	xassert(PMIX_MSGSTATE_MAGIC == eng->magic);
	xassert(NULL != eng->rcvd_hdr_net);
	xassert(pmixp_io_operating(eng));
	xassert(eng->h.recv_on);

	if (!pmixp_io_operating(eng)) {
		return;
	}

	if (pmixp_io_rcvd_ready(eng)) {
		/* nothing to do,
		 * first the current message has to be extracted
		 */
		return;
	}
	/* Drop padding first so it won't corrupt the message */
	if (_rcvd_have_padding(eng)) {
		char buf[eng->h.recv_padding];
		size = eng->h.recv_padding;
		remain = size - eng->rcvd_pad_recvd;
		eng->rcvd_pad_recvd +=
			pmixp_read_buf(fd, buf, remain, &shutdown, false);
		if (shutdown) {
			eng->io_state = PMIXP_IO_CONN_CLOSED;
			return;
		}
		if (eng->rcvd_pad_recvd < size) {
			/* normal return. receive another portion of header later */
			return;
		}
	}

	if (_rcvd_need_header(eng)) {
		/* need to finish with the header */
		size = eng->h.recv_net_hsize;
		remain = size - eng->rcvd_hdr_offs;
		offs = eng->rcvd_hdr_net + eng->rcvd_hdr_offs;
		eng->rcvd_hdr_offs +=
			pmixp_read_buf(fd, offs, remain, &shutdown, false);
		if (shutdown) {
			eng->io_state = PMIXP_IO_CONN_CLOSED;
			return;
		}
		if (_rcvd_need_header(eng)) {
			/* normal return. receive another portion of header later */
			return;
		}
		/* if we are here then header is received and we can adjust buffer */
		if ((shutdown = _rcvd_swithch_to_body(eng))) {
			/* drop # of received bytes to identify that
			 * message is not ready
			 */
			eng->rcvd_hdr_offs = 0;
			eng->io_state = PMIXP_IO_CONN_CLOSED;
			return;
		}
		/* go ahared with body receive */
	}
	/* we are receiving the body */
	xassert(eng->rcvd_hdr_offs == eng->h.recv_net_hsize);
	if (0 == eng->rcvd_pay_size) {
		/* zero-byte message. Exit so we will hit pmixp_io_rcvd_ready */
		return;
	}
	size = eng->rcvd_pay_size;
	remain = size - eng->rcvd_pay_offs;
	eng->rcvd_pay_offs +=
		pmixp_read_buf(fd, eng->rcvd_payload + eng->rcvd_pay_offs,
			remain, &shutdown, false);
	if (shutdown) {
		eng->io_state = PMIXP_IO_CONN_CLOSED;
		return;
	}
	if (eng->rcvd_pay_offs != size) {
		/* normal return. receive another portion later */
		PMIXP_DEBUG("Message is ready for processing!");
		return;
	}
	return;
}

void *pmixp_io_rcvd_extract(pmixp_io_engine_t *eng, void *header)
{
	xassert(PMIX_MSGSTATE_MAGIC == eng->magic);
	xassert(NULL != eng->rcvd_hdr_net);
	xassert(pmixp_io_operating(eng));
	xassert(eng->h.recv_on);
	xassert(pmixp_io_rcvd_ready(eng));

	if( !pmixp_io_operating(eng)){
		return NULL;
	}
	void *ptr = eng->rcvd_payload;
	memcpy(header, eng->rcvd_hdr_host, (size_t)eng->h.recv_host_hsize);
	/* Drop message state to receive new one */
	_rcvd_next_message(eng);
	return ptr;
}

/* Transmitter */

static inline int _send_set_current(pmixp_io_engine_t *eng, void *msg)
{
	xassert(PMIX_MSGSTATE_MAGIC == eng->magic);
	xassert(NULL != eng->rcvd_hdr_net);
	xassert(pmixp_io_enqueue_ok(eng));
	xassert(eng->h.send_on);
	xassert(NULL == eng->send_current);
	void *hdr = eng->h.hdr_ptr_cb(msg);

	/* Set message basis */
	eng->send_current = msg;

	/* Setup header for sending */
	eng->send_hdr_size = eng->h.send_net_hsize;
	eng->h.hdr_pack_cb(hdr, eng->send_hdr_net);
	eng->send_hdr_offs = 0;

	/* Setup payload for sending */
	eng->send_payload = eng->h.payload_ptr_cb(msg);
	eng->send_pay_size = eng->h.payload_size_cb(hdr);
	eng->send_pay_offs = 0;
	return 0;
}

static inline void _send_free_current(pmixp_io_engine_t *eng)
{
	xassert(PMIX_MSGSTATE_MAGIC == eng->magic);
	xassert(NULL != eng->rcvd_hdr_net);
	xassert(pmixp_io_operating(eng));
	xassert(eng->h.send_on);
	xassert(NULL != eng->send_current);

	eng->send_payload = NULL;
	eng->send_pay_size = eng->send_pay_offs = 0;
	eng->send_hdr_size = eng->send_hdr_offs = 0;
	eng->h.msg_free_cb(eng->send_current);
	eng->send_current = NULL;
}

static inline int _send_header_ok(pmixp_io_engine_t *eng)
{
	xassert(PMIX_MSGSTATE_MAGIC == eng->magic);
	xassert(NULL != eng->rcvd_hdr_net);
	xassert(pmixp_io_operating(eng));
	xassert(eng->h.send_on);
	xassert(NULL != eng->send_current);

	return (NULL != eng->send_current) && (eng->send_hdr_size > 0)
			&& (eng->send_hdr_offs == eng->send_hdr_size);
}

static inline int _send_payload_ok(pmixp_io_engine_t *eng)
{
	xassert(PMIX_MSGSTATE_MAGIC == eng->magic);
	xassert(NULL != eng->rcvd_hdr_net);
	xassert(pmixp_io_enqueue_ok(eng));
	xassert(eng->h.send_on);
	xassert(NULL != eng->send_current);

	return (eng->send_current != NULL) && _send_header_ok(eng)
			&& (eng->send_pay_offs == eng->send_pay_size);
}

void pmixp_io_send_enqueue(pmixp_io_engine_t *eng, void *msg)
{
	xassert(PMIX_MSGSTATE_MAGIC == eng->magic);
	xassert(NULL != eng->rcvd_hdr_net);
	xassert(pmixp_io_enqueue_ok(eng));
	xassert(eng->h.send_on);

	/* We should be in the proper state
	 * to accept new messages
	 */
	if( !pmixp_io_enqueue_ok(eng)){
		return;
	}

	list_enqueue(eng->send_queue, msg);
}

bool pmixp_io_send_pending(pmixp_io_engine_t *eng)
{
	int rc;
	xassert(PMIX_MSGSTATE_MAGIC == eng->magic);
	xassert(NULL != eng->rcvd_hdr_net);
	xassert(pmixp_io_enqueue_ok(eng));
	xassert(eng->h.send_on);

	if (!pmixp_io_enqueue_ok(eng)){
		return false;
	}

	if (_send_payload_ok(eng)) {
		/* The current message is send. Cleanup current msg */
		_send_free_current(eng);
	}

	if (eng->send_current == NULL) {
		/* Try next element */
		int n = list_count(eng->send_queue);
		if (0 == n) {
			/* Nothing to do */
			return false;
		}
		void *msg = list_dequeue(eng->send_queue);
		xassert(msg != NULL);
		if ((rc = _send_set_current(eng, msg))) {
			PMIXP_ERROR_NO(rc, "Cannot switch to the next message");
			pmixp_io_finalize(eng, rc);
		}
	}
	return true;
}

void pmixp_io_send_progress(pmixp_io_engine_t *eng)
{
	int fd = eng->sd;
	uint32_t size, remain;
	void *offs;

	xassert(eng->magic == PMIX_MSGSTATE_MAGIC);
	xassert(pmixp_io_operating(eng));

	if (!pmixp_io_operating(eng)){
		/* no progress until in the operational mode */
		return;
	}

	while (pmixp_io_send_pending(eng)) {
		/* try to send everything untill fd became blockable
		 * FIXME: maybe set some restriction on number of messages sended at once
		 */
		int shutdown = 0;
		if (!_send_header_ok(eng)) {
			size = eng->send_hdr_size;
			remain = size - eng->send_hdr_offs;
			offs = eng->send_hdr_net + eng->send_hdr_offs;
			int cnt = pmixp_write_buf(fd, offs, remain, &shutdown,
					false);
			if (shutdown) {
				pmixp_io_finalize(eng, shutdown);
				return;
			}
			if (cnt == 0) {
				break;
			}
			eng->send_hdr_offs += cnt;
			if (!_send_header_ok(eng)) {
				/* Go to the next interation and try to finish with the header */
				continue;
			}
		}

		if (_send_header_ok(eng)) {
			size = eng->send_pay_size;
			remain = size - eng->send_pay_offs;
			offs = eng->send_payload + eng->send_pay_offs;
			int cnt = pmixp_write_buf(fd, offs, remain, &shutdown,
					false);
			if (shutdown) {
				pmixp_io_finalize(eng, shutdown);
				return;
			}
			if (0 == cnt) {
				break;
			}
			eng->send_pay_offs += cnt;
		}
	}
}
