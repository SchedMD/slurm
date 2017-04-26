/*****************************************************************************\
 **  pmix_server.c - PMIx server side functionality
 *****************************************************************************
 *  Copyright (C) 2014-2015 Artem Polyakov. All rights reserved.
 *  Copyright (C) 2015-2017 Mellanox Technologies. All rights reserved.
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

#include "pmixp_common.h"
#include "pmixp_info.h"
#include "pmixp_coll.h"
#include "pmixp_debug.h"
#include "pmixp_io.h"
#include "pmixp_client.h"
#include "pmixp_server.h"
#include "pmixp_nspaces.h"
#include "pmixp_state.h"
#include "pmixp_client.h"
#include "pmixp_dmdx.h"
#include "pmixp_conn.h"
#include "pmixp_dconn.h"

#include <pmix_server.h>

/*
 * --------------------- I/O protocol -------------------
 */

#define PMIXP_SERVER_MSG_MAGIC 0xCAFECA11
typedef struct {
	uint32_t magic;
	uint32_t type;
	uint32_t seq;
	uint32_t nodeid;
	uint32_t msgsize;
} pmixp_base_hdr_t;

#define PMIXP_BASE_HDR_SIZE (5 * sizeof(uint32_t))

typedef struct {
	pmixp_base_hdr_t base_hdr;
	uint16_t rport;		/* STUB: remote port for persistent connection */
} pmixp_slurm_shdr_t;
#define PMIXP_SLURM_API_SHDR_SIZE (PMIXP_BASE_HDR_SIZE + sizeof(uint16_t))

#define PMIXP_MAX_SEND_HDR PMIXP_SLURM_API_SHDR_SIZE

typedef struct {
	uint32_t size;		/* Has to be first (appended by SLURM API) */
	pmixp_slurm_shdr_t shdr;
} pmixp_slurm_rhdr_t;
#define PMIXP_SLURM_API_RHDR_SIZE (sizeof(uint32_t) + PMIXP_SLURM_API_SHDR_SIZE)

#define PMIXP_BASE_HDR_SETUP(bhdr, mtype, mseq, buf)                  \
{                                                                   \
	bhdr.magic = PMIXP_SERVER_MSG_MAGIC;                        \
	bhdr.type = mtype;                                           \
	bhdr.msgsize = get_buf_offset(buf) - PMIXP_MAX_SEND_HDR;    \
	bhdr.seq = mseq;                                             \
	bhdr.nodeid = pmixp_info_nodeid_job();                      \
}


#define PMIXP_SERVER_BUF_MAGIC 0xCA11CAFE
Buf pmixp_server_buf_new(void)
{
	Buf buf = create_buf(xmalloc(PMIXP_MAX_SEND_HDR), PMIXP_MAX_SEND_HDR);
#ifndef NDEBUG
	/* Makesure that we only use buffers allocated through
	 * this call, because we reserve the space for the
	 * header here
	 */
	xassert( PMIXP_MAX_SEND_HDR >= sizeof(uint32_t));
	uint32_t tmp = PMIXP_SERVER_BUF_MAGIC;
	pack32(tmp, buf);
#endif

	/* Skip header. It will be filled right before the sending */
	set_buf_offset(buf, PMIXP_MAX_SEND_HDR);
	return buf;
}

size_t pmixp_server_buf_reset(Buf buf)
{
#ifndef NDEBUG
	xassert( PMIXP_MAX_SEND_HDR <= get_buf_offset(buf) );
	set_buf_offset(buf,0);
	/* Restore the protection magic number
	 */
	uint32_t tmp = PMIXP_SERVER_BUF_MAGIC;
	pack32(tmp, buf);
#endif
	set_buf_offset(buf, PMIXP_MAX_SEND_HDR);
	return PMIXP_MAX_SEND_HDR;
}


static void *_buf_finalize(Buf buf, void *nhdr, size_t hsize,
			  size_t *dsize)
{
	char *ptr = get_buf_data(buf);
	size_t offset = PMIXP_MAX_SEND_HDR - hsize;
#ifndef NDEBUG
	Buf tbuf = create_buf(ptr, get_buf_offset(buf));
	xassert(PMIXP_MAX_SEND_HDR >= hsize);
	xassert(PMIXP_MAX_SEND_HDR <= get_buf_offset(buf));
	uint32_t tmp;
	unpack32(&tmp, tbuf);
	xassert(PMIXP_SERVER_BUF_MAGIC == tmp);
	tbuf->head = NULL;
	free_buf(tbuf);
#endif
	/* Enough space for any header was reserved at the
	 * time of buffer initialization in `pmixp_server_new_buf`
	 * put the header in place and return proper pointer
	 */
	if( 0 != hsize ){
		memcpy(ptr + offset, nhdr, hsize);
	}
	*dsize = get_buf_offset(buf) - offset;
	return ptr + offset;
}

static void _base_hdr_pack(Buf packbuf, pmixp_base_hdr_t *hdr)
{
	pack32(hdr->magic, packbuf);
	pack32(hdr->type, packbuf);
	pack32(hdr->seq, packbuf);
	pack32(hdr->nodeid, packbuf);
	pack32(hdr->msgsize, packbuf);
}

static int _base_hdr_unpack(Buf packbuf, pmixp_base_hdr_t *hdr)
{
	if (unpack32(&hdr->magic, packbuf)) {
		return -EINVAL;
	}
	xassert(PMIXP_SERVER_MSG_MAGIC == hdr->magic);

	if (unpack32(&hdr->type, packbuf)) {
		return -EINVAL;
	}

	if (unpack32(&hdr->seq, packbuf)) {
		return -EINVAL;
	}

	if (unpack32(&hdr->nodeid, packbuf)) {
		return -EINVAL;
	}

	if (unpack32(&hdr->msgsize, packbuf)) {
		return -EINVAL;
	}
	return 0;
}

static void _slurm_hdr_pack(Buf packbuf, pmixp_slurm_shdr_t *hdr)
{
	_base_hdr_pack(packbuf, &hdr->base_hdr);
	pack16(hdr->rport, packbuf);
}

static int _slurm_hdr_unpack(Buf packbuf, pmixp_slurm_rhdr_t *hdr)
{
	if (unpack32(&hdr->size, packbuf)) {
		return -EINVAL;
	}

	if (_base_hdr_unpack(packbuf, &hdr->shdr.base_hdr)) {
		return -EINVAL;
	}

	if (unpack16(&hdr->shdr.rport, packbuf)) {
		return -EINVAL;
	}

	return 0;
}

/* SLURM protocol I/O header */
static uint32_t _slurm_proto_msize(void *buf);
static int _slurm_pack_hdr(void *host, void *net);
static int _slurm_proto_unpack_hdr(void *net, void *host);
static void _slurm_new_msg(pmixp_conn_t *conn,
			   void *_hdr, void *msg);
static int _slurm_send(pmixp_ep_t *ep,
		       pmixp_base_hdr_t bhdr, Buf buf);

pmixp_io_engine_header_t _slurm_proto = {
	/* generic callbacks */
	.payload_size_cb = _slurm_proto_msize,
	/* receiver-related fields */
	.recv_on = 1,
	.recv_host_hsize = sizeof(pmixp_slurm_rhdr_t),
	.recv_net_hsize = PMIXP_SLURM_API_RHDR_SIZE, /*need to skip user ID*/
	.recv_padding = sizeof(uint32_t),
	.hdr_unpack_cb = _slurm_proto_unpack_hdr,
};

/* direct protocol I/O header */
static uint32_t _direct_msize(void *hdr);
static int _direct_hdr_pack(void *host, void *net);
static int _direct_hdr_unpack(void *net, void *host);
static void *_direct_hdr_ptr(void *msg);
static void *_direct_payload_ptr(void *msg);
static void _direct_msg_free(void *msg);
static void _direct_new_msg(pmixp_conn_t *conn, void *_hdr, void *msg);
static void _direct_send(pmixp_dconn_t *dconn, pmixp_ep_t *ep,
			 pmixp_base_hdr_t bhdr, Buf buf,
			pmixp_server_sent_cb_t complete_cb, void *cb_data);


pmixp_io_engine_header_t _direct_proto = {
	/* generic callback */
	.payload_size_cb = _direct_msize,
	/* receiver-related fields */
	.recv_on = 1,
	.recv_host_hsize = sizeof(pmixp_base_hdr_t),
	.recv_net_hsize = PMIXP_BASE_HDR_SIZE,
	.recv_padding = 0, /* no padding for the direct proto */
	.hdr_unpack_cb = _direct_hdr_unpack,
	/* transmitter-related fields */
	.send_on = 1,
	.send_host_hsize = sizeof(pmixp_base_hdr_t),
	.send_net_hsize = PMIXP_BASE_HDR_SIZE,
	.hdr_pack_cb = _direct_hdr_pack,
	.hdr_ptr_cb = _direct_hdr_ptr,
	.payload_ptr_cb = _direct_payload_ptr,
	.msg_free_cb = _direct_msg_free
};


/*
 * --------------------- Initi/Finalize -------------------
 */

static volatile int _was_initialized = 0;

int pmixp_stepd_init(const stepd_step_rec_t *job, char ***env)
{
	char *path;
	int fd, rc;
	uint16_t port;

	if (SLURM_SUCCESS != (rc = pmixp_info_set(job, env))) {
		PMIXP_ERROR("pmixp_info_set(job, env) failed");
		goto err_info;
	}

	/* Create UNIX socket for slurmd communication */
	path = pmixp_info_nspace_usock(pmixp_info_namespace());
	if (NULL == path) {
		PMIXP_ERROR("pmixp_info_nspace_usock: out-of-memory");
		rc = SLURM_ERROR;
		goto err_path;
	}
	if ((fd = pmixp_usock_create_srv(path)) < 0) {
		PMIXP_ERROR("pmixp_usock_create_srv");
		rc = SLURM_ERROR;
		goto err_usock;
	}
	fd_set_close_on_exec(fd);
	pmixp_info_srv_usock_set(path, fd);


	/* Create TCP socket for slurmd communication */
	if (0 > net_stream_listen(&fd, &port)) {
		PMIXP_ERROR("net_stream_listen");
		goto err_tsock;
	}
	pmixp_info_srv_tsock_set(port, fd);

	pmixp_conn_init(_slurm_proto, _direct_proto);
	pmixp_dconn_init(pmixp_info_nodes(), _direct_proto);

	if (SLURM_SUCCESS != (rc = pmixp_nspaces_init())) {
		PMIXP_ERROR("pmixp_nspaces_init() failed");
		goto err_nspaces;
	}

	if (SLURM_SUCCESS != (rc = pmixp_state_init())) {
		PMIXP_ERROR("pmixp_state_init() failed");
		goto err_state;
	}

	if (SLURM_SUCCESS != (rc = pmixp_dmdx_init())) {
		PMIXP_ERROR("pmixp_dmdx_init() failed");
		goto err_dmdx;
	}

	if (SLURM_SUCCESS != (rc = pmixp_libpmix_init())) {
		PMIXP_ERROR("pmixp_libpmix_init() failed");
		goto err_lib;
	}

	if (SLURM_SUCCESS != (rc = pmixp_libpmix_job_set())) {
		PMIXP_ERROR("pmixp_libpmix_job_set() failed");
		goto err_job;
	}

    pmixp_server_init_pp(env);

	xfree(path);
	_was_initialized = 1;
	return SLURM_SUCCESS;

err_job:
	pmixp_libpmix_finalize();
err_lib:
	pmixp_dmdx_finalize();
err_dmdx:
	pmixp_state_finalize();
err_state:
	pmixp_nspaces_finalize();
err_nspaces:
	close(pmixp_info_srv_tsock_fd());
err_tsock:
	close(pmixp_info_srv_usock_fd());
err_usock:
	xfree(path);
err_path:
	pmixp_info_free();
err_info:
	return rc;
}

int pmixp_stepd_finalize(void)
{
	char *path;
	if (!_was_initialized) {
		/* nothing to do */
		return 0;
	}

	pmixp_libpmix_finalize();
	pmixp_dmdx_finalize();

	pmixp_conn_fini();
	pmixp_dconn_fini();

	pmixp_state_finalize();
	pmixp_nspaces_finalize();

	/* close TCP socket */
	close(pmixp_info_srv_tsock_fd());

	/* cleanup the UNIX socket */
	PMIXP_DEBUG("Remove PMIx plugin usock");
	close(pmixp_info_srv_usock_fd());
	path = pmixp_info_nspace_usock(pmixp_info_namespace());
	unlink(path);
	xfree(path);

	/* free the information */
	pmixp_info_free();
	return SLURM_SUCCESS;
}

void pmixp_server_cleanup(void)
{
	pmixp_conn_cleanup();
}

/*
 * --------------------- Generic I/O functionality -------------------
 */

static bool _serv_readable(eio_obj_t *obj);
static int _serv_read(eio_obj_t *obj, List objs);
static bool _serv_writable(eio_obj_t *obj);
static int _serv_write(eio_obj_t *obj, List objs);
static void _process_server_request(pmixp_base_hdr_t *hdr, void *payload);


static struct io_operations slurm_peer_ops = {
	.readable = _serv_readable,
	.handle_read = _serv_read
};

static struct io_operations direct_peer_ops = {
	.readable	= _serv_readable,
	.handle_read	= _serv_read,
	.writable	= _serv_writable,
	.handle_write	= _serv_write
};

static bool _serv_readable(eio_obj_t *obj)
{
	/* sanity check */
	xassert(NULL != obj );
	if( obj->shutdown ){
		/* corresponding connection will be
		 * cleaned up during plugin finalize
		 */
		return false;
	}
	return true;
}

static int _serv_read(eio_obj_t *obj, List objs)
{
	/* sanity check */
	xassert(NULL != obj );
	if( obj->shutdown ){
		/* corresponding connection will be
		 * cleaned up during plugin finalize
		 */
		return 0;
	}

	PMIXP_DEBUG("fd = %d", obj->fd);
	pmixp_conn_t *conn = (pmixp_conn_t *)obj->arg;
	bool proceed = true;

	/* debug stub */
	pmixp_debug_hang(0);

	/* Read and process all received messages */
	while (proceed) {
		if( !pmixp_conn_progress_rcv(conn) ){
			proceed = 0;
		}
		if( !pmixp_conn_is_alive(conn) ){
			obj->shutdown = true;
			PMIXP_DEBUG("Connection closed fd = %d", obj->fd);
			/* cleanup after this connection */
			eio_remove_obj(obj, objs);
			pmixp_conn_return(conn);
			proceed = 0;
		}
	}
	return 0;
}

static bool _serv_writable(eio_obj_t *obj)
{
	/* sanity check */
	xassert(NULL != obj );
	if( obj->shutdown ){
		/* corresponding connection will be
		 * cleaned up during plugin finalize
		 */
		return false;
	}

	/* get I/O engine */
	pmixp_conn_t *conn = (pmixp_conn_t *)obj->arg;
	pmixp_io_engine_t *eng = conn->eng;

	/* debug stub */
	pmixp_debug_hang(0);

	/* Invoke cleanup callbacks if any */
	pmixp_io_send_cleanup(eng);

	/* check if we have something to send */
	if( pmixp_io_send_pending(eng) ){
		return true;
	}
	return false;
}

static int _serv_write(eio_obj_t *obj, List objs)
{
	/* sanity check */
	xassert(NULL != obj );
	if( obj->shutdown ){
		/* corresponding connection will be
		 * cleaned up during plugin finalize
		 */
		return 0;
	}

	PMIXP_DEBUG("fd = %d", obj->fd);
	pmixp_conn_t *conn = (pmixp_conn_t *)obj->arg;

	/* debug stub */
	pmixp_debug_hang(0);

	/* progress sends */
	pmixp_conn_progress_snd(conn);

	/* if we are done with this connection - remove it */
	if( !pmixp_conn_is_alive(conn) ){
		obj->shutdown = true;
		PMIXP_DEBUG("Connection finalized fd = %d", obj->fd);
		/* cleanup after this connection */
		eio_remove_obj(obj, objs);
		pmixp_conn_return(conn);
	}
	return 0;
}

static void _process_server_request(pmixp_base_hdr_t *hdr, void *payload)
{
	char *nodename = pmixp_info_job_host(hdr->nodeid);
	Buf buf = create_buf(payload, hdr->msgsize);
	int rc;

	switch (hdr->type) {
	case PMIXP_MSG_FAN_IN:
	case PMIXP_MSG_FAN_OUT: {
		pmixp_coll_t *coll;
		pmix_proc_t *procs = NULL;
		size_t nprocs = 0;
		pmixp_coll_type_t type = 0;

		rc = pmixp_coll_unpack_ranges(buf, &type, &procs, &nprocs);
		if (SLURM_SUCCESS != rc) {
			PMIXP_ERROR("Bad message header from node %s", nodename);
			goto exit;
		}
		coll = pmixp_state_coll_get(type, procs, nprocs);
		xfree(procs);

		PMIXP_DEBUG("FENCE collective message from node \"%s\", type = %s, seq = %d",
			    nodename, (PMIXP_MSG_FAN_IN == hdr->type) ? "fan-in" : "fan-out",
			    hdr->seq);
		rc = pmixp_coll_check_seq(coll, hdr->seq);
		if (PMIXP_COLL_REQ_FAILURE == rc) {
			/* this is unexepable event: either something went
			 * really wrong or the state machine is incorrect.
			 * This will 100% lead to application hang.
			 */
			PMIXP_ERROR("Bad collective seq. #%d from %s, current is %d",
				    hdr->seq, nodename, coll->seq);
			pmixp_debug_hang(0); /* enable hang to debug this! */
			slurm_kill_job_step(pmixp_info_jobid(), pmixp_info_stepid(),
					    SIGKILL);

			break;
		} else if (PMIXP_COLL_REQ_SKIP == rc) {
			PMIXP_DEBUG("Wrong collective seq. #%d from %s, current is %d, skip this message",
				    hdr->seq, nodename, coll->seq);
			goto exit;
		}

		if (PMIXP_MSG_FAN_IN == hdr->type) {
			pmixp_coll_contrib_node(coll, nodename, buf);
			goto exit;
		} else {
			coll->root_buf = buf;
			pmixp_coll_bcast(coll);
			/* buf will be free'd by the PMIx callback so protect the data by
			 * voiding the buffer.
			 * Use the statement below instead of (buf = NULL) to maintain
			 * incapsulation - in general `buf` is not a pointer, but opaque type.
			 */
			buf = create_buf(NULL, 0);
		}

		break;
	}
	case PMIXP_MSG_DMDX: {
		pmixp_dmdx_process(buf, hdr->nodeid, hdr->seq);
		/* buf will be free'd by pmixp_dmdx_process or the PMIx callback so
		 * protect the data by voiding the buffer.
		 * Use the statement below instead of (buf = NULL) to maintain
		 * incapsulation - in general `buf` is not a pointer, but opaque type.
		 */
		buf = create_buf(NULL, 0);
		break;
	}
#ifndef NDEBUG
	case PMIXP_MSG_PINGPONG: {
		/* if the pingpong mode was activated - node 0 sends ping requests
		 * and receiver assumed to respond back to node 0
		 */
		if( pmixp_info_nodeid() ){
			pmixp_server_pp_send(0, hdr->msgsize);
		}
		pmixp_server_pp_inc();
		break;
	}
#endif
	default:
		PMIXP_ERROR("Unknown message type %d", hdr->type);
		break;
	}

exit:
	free_buf(buf);
	if( NULL != nodename ){
		xfree(nodename);
	}
}

void pmixp_server_sent_buf_cb(int rc, pmixp_srv_cb_context_t ctx, void *data)
{
	Buf buf = (Buf)data;
	free_buf(buf);
	return;
}

int pmixp_server_send_nb(pmixp_ep_t *ep, pmixp_srv_cmd_t type,
			 uint32_t seq, Buf buf,
			 pmixp_server_sent_cb_t complete_cb,
			 void *cb_data)
{
	pmixp_base_hdr_t bhdr;
	int rc = SLURM_ERROR;
	pmixp_dconn_t *dconn = NULL;

	PMIXP_BASE_HDR_SETUP(bhdr, type, seq, buf);

	/* if direct connection is not enabled
	 * always use SLURM protocol
	 */
	if (!pmixp_info_srv_direct_conn()) {
		goto send_slurm;
	}

	switch (ep->type) {
	case PMIXP_EP_HLIST:
		goto send_slurm;
	case PMIXP_EP_NOIDEID:{
		int hostid;
		hostid = ep->ep.nodeid;
		xassert(0 <= hostid);
		dconn = pmixp_dconn_lock(hostid);
		switch (pmixp_dconn_state(dconn)) {
		case PMIXP_DIRECT_EP_SENT:
		case PMIXP_DIRECT_CONNECTED:
			/* keep the lock here and proceed
			 * to the direct send
			 */
			goto send_direct;
		case PMIXP_DIRECT_INIT:
			pmixp_dconn_req_sent(dconn);
			pmixp_dconn_unlock(dconn);
			goto send_slurm;
		default:{
			/* this is a bug! */
			pmixp_dconn_state_t state = pmixp_dconn_state(dconn);
			pmixp_dconn_unlock(dconn);
			PMIXP_ERROR("Bad direct connection state: %d", (int)state);
			xassert( (state == PMIXP_DIRECT_INIT) ||
				 (state == PMIXP_DIRECT_EP_SENT) ||
				 (state == PMIXP_DIRECT_CONNECTED) );
			abort();
		}
		}
	}
	default:
		PMIXP_ERROR("Bad value of the endpoint type: %d",
			    (int)ep->type);
		xassert( PMIXP_EP_HLIST == ep->type ||
			 PMIXP_EP_NOIDEID == ep->type);
		abort();
	}

	return rc;
send_slurm:
	rc = _slurm_send(ep, bhdr, buf);
	complete_cb(rc, PMIXP_SRV_CB_INLINE, cb_data);
	return SLURM_SUCCESS;
send_direct:
	xassert( NULL != dconn );
	_direct_send(dconn, ep, bhdr, buf, complete_cb, cb_data);
	pmixp_dconn_unlock(dconn);
	return SLURM_SUCCESS;
}

/*
 * ------------------- DIRECT communication protocol -----------------------
 */


typedef struct {
	pmixp_base_hdr_t hdr;
	void *payload;
	Buf buf_ptr;
	pmixp_server_sent_cb_t sent_cb;
	void *cbdata;
}_direct_proto_message_t;

/*
 *  Server message processing
 */

static uint32_t _direct_msize(void *buf)
{
	pmixp_base_hdr_t *hdr = (pmixp_base_hdr_t *)buf;
	return hdr->msgsize;
}

/*
 * Unpack message header.
 * Returns 0 on success and -errno on failure
 * Note: asymmetric to _send_pack_hdr because of additional SLURM header
 */
static int _direct_hdr_unpack(void *net, void *host)
{
	pmixp_base_hdr_t *hdr = (pmixp_base_hdr_t *)host;
	Buf packbuf = create_buf(net, PMIXP_BASE_HDR_SIZE);

	if (_base_hdr_unpack(packbuf,hdr)) {
		return -EINVAL;
	}

	/* free the Buf packbuf, but not the memory it points to */
	packbuf->head = NULL;
	free_buf(packbuf);
	return 0;
}

/*
 * Pack message header.
 * Returns packed size
 * Note: asymmetric to _recv_unpack_hdr because of additional SLURM header
 */
static int _direct_hdr_pack(void *host, void *net)
{
	pmixp_base_hdr_t *hdr = (pmixp_base_hdr_t *)host;
	Buf packbuf = create_buf(net, PMIXP_BASE_HDR_SIZE);
	int size = 0;
	_base_hdr_pack(packbuf, hdr);
	size = get_buf_offset(packbuf);
	xassert(size == PMIXP_BASE_HDR_SIZE);
	/* free the Buf packbuf, but not the memory it points to */
	packbuf->head = NULL;
	free_buf(packbuf);
	return size;
}

/*
 * Get te pointer to the message header.
 * Returns packed size
 * Note: asymmetric to _recv_unpack_hdr because of additional SLURM header
 */
static void *_direct_hdr_ptr(void *msg)
{
	_direct_proto_message_t *_msg = (_direct_proto_message_t*)msg;
	return &_msg->hdr;
}

static void *_direct_payload_ptr(void *msg)
{
	_direct_proto_message_t *_msg = (_direct_proto_message_t*)msg;
	return _msg->payload;
}

static void _direct_msg_free(void *_msg)
{
	_direct_proto_message_t *msg = (_direct_proto_message_t*)_msg;
	msg->sent_cb(SLURM_SUCCESS, PMIXP_SRV_CB_REGULAR, msg->cbdata);
	xfree(msg);
}

/*
 * See process_handler_t prototype description
 * on the details of this function output values
 */
static void _direct_new_msg(pmixp_conn_t *conn, void *_hdr, void *msg)
{
	pmixp_base_hdr_t *hdr = (pmixp_base_hdr_t*)_hdr;
	_process_server_request(hdr, msg);
}

/* Process direct connection closure
 */

static void _direct_return_connection(pmixp_conn_t *conn)
{
	pmixp_dconn_t *dconn = (pmixp_dconn_t *)pmixp_conn_get_data(conn);
	pmixp_dconn_lock(dconn->nodeid);
	pmixp_dconn_disconnect(dconn);
	pmixp_dconn_unlock(dconn);
}

/*
 * Receive the first message identifying initiator
 */
static void
_direct_conn_establish(pmixp_conn_t *conn, void *_hdr, void *msg)
{
	pmixp_io_engine_t *eng = pmixp_conn_get_eng(conn);
	pmixp_base_hdr_t *hdr = (pmixp_base_hdr_t *)_hdr;
	pmixp_dconn_t *dconn = NULL;
	pmixp_conn_t *new_conn;
	eio_obj_t *obj;
	int fd;

	xassert(0 == hdr->msgsize);
	fd = pmixp_io_detach(eng);

	dconn = pmixp_dconn_accept(hdr->nodeid, fd);
	if( NULL == dconn ){
		/* connection was refused because we already
		 * have established connection
		 * It seems that some sort of race condition occured
		 */
		char *nodename = pmixp_info_job_host(hdr->nodeid);
		close(fd);
		PMIXP_ERROR("Failed to accept direct connection from %s", nodename);
		xfree(nodename);
		return;
	}
	new_conn = pmixp_conn_new_persist(PMIXP_PROTO_DIRECT, pmixp_dconn_engine(dconn),
				      _direct_new_msg, _direct_return_connection, dconn);
	pmixp_dconn_unlock(dconn);
	obj = eio_obj_create(fd, &direct_peer_ops, (void *)new_conn);
	eio_new_obj(pmixp_info_io(), obj);
	/* wakeup this connection to get processed */
	eio_signal_wakeup(pmixp_info_io());
}

void pmixp_server_direct_conn(int fd)
{
	eio_obj_t *obj;
	pmixp_conn_t *conn;
	PMIXP_DEBUG("Request from fd = %d", fd);

	/* Set nonblocking */
	fd_set_nonblocking(fd);
	fd_set_close_on_exec(fd);
	pmixp_fd_set_nodelay(fd);
	conn = pmixp_conn_new_temp(PMIXP_PROTO_DIRECT, fd, _direct_conn_establish);

	/* try to process right here */
	pmixp_conn_progress_rcv(conn);
	if (!pmixp_conn_is_alive(conn)) {
		/* success, don't need this connection anymore */
		pmixp_conn_return(conn);
		return;
	}

	/* If it is a blocking operation: create AIO object to
	 * handle it */
	obj = eio_obj_create(fd, &direct_peer_ops, (void *)conn);
	eio_new_obj(pmixp_info_io(), obj);
	/* wakeup this connection to get processed */
	eio_signal_wakeup(pmixp_info_io());
}

static void
_direct_send(pmixp_dconn_t *dconn, pmixp_ep_t *ep,
			 pmixp_base_hdr_t bhdr, Buf buf,
			pmixp_server_sent_cb_t complete_cb, void *cb_data)
{
	size_t dsize = 0;
	int rc;

	xassert(PMIXP_EP_NOIDEID == ep->type);
	/* TODO: I think we can avoid locking */
	_direct_proto_message_t *msg = xmalloc(sizeof(*msg));
	msg->sent_cb = complete_cb;
	msg->cbdata = cb_data;
	msg->hdr = bhdr;
	msg->payload = _buf_finalize(buf, NULL, 0, &dsize);
	msg->buf_ptr = buf;
	
	
	rc = pmixp_dconn_send(dconn, msg);
	if (SLURM_SUCCESS != rc) {
		msg->sent_cb(rc, PMIXP_SRV_CB_INLINE, msg->cbdata);
		xfree( msg );
	}
	eio_signal_wakeup(pmixp_info_io());
}

/*
 * ------------------- SLURM communication protocol -----------------------
 */

/*
 * See process_handler_t prototype description
 * on the details of this function output values
 */
static void _slurm_new_msg(pmixp_conn_t *conn,
			   void *_hdr, void *msg)
{
	pmixp_slurm_rhdr_t *hdr = (pmixp_slurm_rhdr_t *)_hdr;

	if( 0 != hdr->shdr.rport ){
		pmixp_dconn_t *dconn;
		Buf buf = pmixp_server_buf_new();
		pmixp_base_hdr_t bhdr;
		_direct_proto_message_t *init_msg = xmalloc(sizeof(*init_msg));
		size_t dsize;

		PMIXP_BASE_HDR_SETUP(bhdr, PMIXP_MSG_INIT_DIRECT, 0, buf);
		init_msg->sent_cb = pmixp_server_sent_buf_cb;
		init_msg->cbdata = buf;
		init_msg->hdr = bhdr;
		init_msg->payload = _buf_finalize(buf, NULL, 0, &dsize);
		init_msg->buf_ptr = buf;

		dconn = pmixp_dconn_connect(hdr->shdr.base_hdr.nodeid,
					    &hdr->shdr.rport, sizeof(hdr->shdr.rport),
					    init_msg);
		if( NULL != dconn ){

			switch (pmixp_dconn_type(dconn)) {
			case PMIXP_DIRECT_TYPE_POLL:{
				/* this direct connection has fd that needs to be
				 * polled to progress, use connection interface for that
				 */
				pmixp_io_engine_t *eng = pmixp_dconn_engine(dconn);
				pmixp_conn_t *conn;
				conn = pmixp_conn_new_persist(PMIXP_PROTO_DIRECT, eng,
							      _direct_new_msg,
							      _direct_return_connection,
							      dconn);
				if( NULL != conn ){
					eio_obj_t *obj;
					obj = eio_obj_create(pmixp_io_fd(eng),
							     &direct_peer_ops,
							     (void *)conn);
					eio_new_obj(pmixp_info_io(), obj);
					eio_signal_wakeup(pmixp_info_io());
				} else {
					/* TODO: handle this error */
				}
				break;
			}
			case PMIXP_DIRECT_TYPE_AM: {
				pmixp_dconn_set_cb(dconn, NULL);
				break;
			}
			default:
				/* Should not happen */
				xassert(0 && pmixp_dconn_type(dconn));
				/* TODO: handle this error */
			}
			pmixp_dconn_unlock(dconn);
		} else {
			/* need to release `init_msg` here */
			xfree(init_msg);
		}
	}
	_process_server_request(&hdr->shdr.base_hdr, msg);
}


/*
 * TODO: we need to keep track of the "me"
 * structures created here, because we need to
 * free them in "pmixp_stepd_finalize"
 */
void pmixp_server_slurm_conn(int fd)
{
	eio_obj_t *obj;
	pmixp_conn_t *conn = NULL;

	PMIXP_DEBUG("Request from fd = %d", fd);
	pmixp_debug_hang(0);

	/* Set nonblocking */
	fd_set_nonblocking(fd);
	fd_set_close_on_exec(fd);
	conn = pmixp_conn_new_temp(PMIXP_PROTO_SLURM, fd, _slurm_new_msg);

	/* try to process right here */
	pmixp_conn_progress_rcv(conn);
	if (!pmixp_conn_is_alive(conn)) {
		/* success, don't need this connection anymore */
		pmixp_conn_return(conn);
		return;
	}

	/* If it is a blocking operation: create AIO object to
	 * handle it */
	obj = eio_obj_create(fd, &slurm_peer_ops, (void *)conn);
	eio_new_obj(pmixp_info_io(), obj);
}

/*
 *  Server message processing
 */

static uint32_t _slurm_proto_msize(void *buf)
{
pmixp_slurm_rhdr_t *ptr = (pmixp_slurm_rhdr_t *)buf;
	pmixp_base_hdr_t *hdr = &ptr->shdr.base_hdr;
	xassert(ptr->size == hdr->msgsize + PMIXP_SLURM_API_SHDR_SIZE);
	xassert(hdr->magic == PMIXP_SERVER_MSG_MAGIC);
	return hdr->msgsize;
}

/*
 * Pack message header.
 * Returns packed size
 * Note: asymmetric to _recv_unpack_hdr because of additional SLURM header
 */
static int _slurm_pack_hdr(void *host, void *net)
{
	pmixp_slurm_shdr_t *shdr = (pmixp_slurm_shdr_t *)host;
	Buf packbuf = create_buf(net, PMIXP_SLURM_API_SHDR_SIZE);
	int size = 0;

	_slurm_hdr_pack(packbuf, shdr);
	size = get_buf_offset(packbuf);
	xassert(size == PMIXP_SLURM_API_SHDR_SIZE);
	/* free the Buf packbuf, but not the memory it points to */
	packbuf->head = NULL;
	free_buf(packbuf);
	return size;
}

/*
 * Unpack message header.
 * Returns 0 on success and -errno on failure
 * Note: asymmetric to _send_pack_hdr because of additional SLURM header
 */
static int _slurm_proto_unpack_hdr(void *net, void *host)
{
	pmixp_slurm_rhdr_t *rhdr = (pmixp_slurm_rhdr_t *)host;
	Buf packbuf = create_buf(net, PMIXP_SLURM_API_RHDR_SIZE);
	if (_slurm_hdr_unpack(packbuf, rhdr) ) {
		return -EINVAL;
	}
	/* free the Buf packbuf, but not the memory it points to */
	packbuf->head = NULL;
	free_buf(packbuf);

	return 0;
}

static int _slurm_send(pmixp_ep_t *ep, pmixp_base_hdr_t bhdr, Buf buf)
{
	const char *addr = NULL, *data = NULL, *hostlist = NULL;
	pmixp_slurm_shdr_t hdr;
	char nhdr[sizeof(hdr)];
	size_t hsize = 0, dsize = 0;
	int rc;

	/* setup the header */
	hdr.base_hdr = bhdr;
	addr = pmixp_info_srv_usock_path();

	hdr.rport = 0;
	if (pmixp_info_srv_direct_conn() && PMIXP_EP_NOIDEID == ep->type) {
		hdr.rport = pmixp_info_srv_tsock_port();
	}

	hsize = _slurm_pack_hdr(&hdr, nhdr);
	data = _buf_finalize(buf, nhdr, hsize, &dsize);

	switch( ep->type ){
	case PMIXP_EP_HLIST:
		hostlist = ep->ep.hostlist;
		rc = pmixp_stepd_send(ep->ep.hostlist, addr,
				 data, dsize, 500, 7, 0);
		break;
	case PMIXP_EP_NOIDEID: {
		char *nodename = pmixp_info_job_host(ep->ep.nodeid);
		rc = pmixp_p2p_send(nodename, addr, data, dsize,
				    500, 7, 0);
		xfree(nodename);
		break;
	}
	default:
		PMIXP_ERROR("Bad value of the EP type: %d", (int)ep->type);
		abort();
	}

	if (SLURM_SUCCESS != rc) {
		PMIXP_ERROR("Cannot send message to %s, size = %u, hostlist:\n%s",
			    addr, (uint32_t) dsize, hostlist);
	}
	return rc;
}

/*
 * ------------------- communication DEBUG tool -----------------------
 */

#ifndef NDEBUG

/*
 * This is solely a debug code that helps to estimate
 * the performance of the communication subsystem
 * of the plugin
 */

static bool _pmixp_pp_on = false;
static int _pmixp_pp_low = 0;
static int _pmixp_pp_up = 24;
static int _pmixp_pp_bound = 10;
static int _pmixp_pp_siter = 1000;
static int _pmixp_pp_liter = 100;

static volatile int _pmixp_pp_count = 0;

int pmixp_server_pp_count()
{
	return _pmixp_pp_count;
}

void pmixp_server_pp_inc()
{
	_pmixp_pp_count++;
}

static bool _consists_from_digits(char *s)
{
	if (strspn(s, "0123456789") == strlen(s)){
		return true;
	}
	return false;
}

void pmixp_server_init_pp(char ***env)
{
	char *env_ptr = NULL;

	/* check if we want to run ping-pong */
	if (!(env_ptr = getenvp(*env, PMIXP_PP_ON))) {
		return;
	}
	if (!strcmp("1", env_ptr) || !strcmp("true", env_ptr)) {
		_pmixp_pp_on = true;
	}

	if ((env_ptr = getenvp(*env, PMIXP_PP_LOW))) {
		if (_consists_from_digits(env_ptr)) {
			_pmixp_pp_low = atoi(env_ptr);
		}
	}

	if ((env_ptr = getenvp(*env, PMIXP_PP_UP))) {
		if (_consists_from_digits(env_ptr)) {
			_pmixp_pp_up = atoi(env_ptr);
		}
	}

	if ((env_ptr = getenvp(*env, PMIXP_PP_SITER))) {
		if (_consists_from_digits(env_ptr)) {
			_pmixp_pp_siter = atoi(env_ptr);
		}
	}

	if ((env_ptr = getenvp(*env, PMIXP_PP_LITER))) {
		if (_consists_from_digits(env_ptr)) {
			_pmixp_pp_liter = atoi(env_ptr);
		}
	}

	if ((env_ptr = getenvp(*env, PMIXP_PP_BOUND))) {
		if (_consists_from_digits(env_ptr)) {
			_pmixp_pp_bound = atoi(env_ptr);
		}
	}
}

bool pmixp_server_want_pp()
{
    return _pmixp_pp_on;
}

/*
 * For this to work the following conditions supposed to be
 * satisfied:
 * - SLURM has to be configured with `--enable-debug` option
 * - jobstep needs to have at least two nodes
 * In this case communication exchange will be done between
 * the first two nodes.
 */
void pmixp_server_run_pp()
{
	int i;
	size_t start, end, bound;
	/* ping is initiated by the nodeid == 0
	 * all the rest - just exit
	 */
	if( pmixp_info_nodeid() ){
		return;
	}

	start = 1 << _pmixp_pp_low;
	end = 1 << _pmixp_pp_up;
	bound = 1 << _pmixp_pp_bound;

	for( i = start; i <= end; i *= 2) {
		int count, iters = _pmixp_pp_siter;
		struct timeval tv1, tv2;
		double time;
		if( i >= bound ) {
			iters = _pmixp_pp_liter;
		}

		/* warmup - 10% of iters # */
		count = pmixp_server_pp_count() + iters/10;
		while( pmixp_server_pp_count() < count ){
			int cur_count = pmixp_server_pp_count();
			pmixp_server_pp_send(1, i);
			while( cur_count == pmixp_server_pp_count() ){
				usleep(1);
			}
		}

		count = pmixp_server_pp_count() + iters;
		gettimeofday(&tv1, NULL);
		while( pmixp_server_pp_count() < count ){
			int cur_count = pmixp_server_pp_count();
			/* Send the message to the (nodeid == 1) */
			pmixp_server_pp_send(1, i);
			/* wait for the response */
			while (cur_count == pmixp_server_pp_count());
		}
		gettimeofday(&tv2, NULL);
		time = tv2.tv_sec + 1E-6 * tv2.tv_usec -
				(tv1.tv_sec + 1E-6 * tv1.tv_usec);
		/* Output measurements to the slurmd.log */
		PMIXP_ERROR("latency: %d - %lf", i, time / iters );
	}
}


struct pp_cbdata
{
	Buf buf;
	double start;
	int size;
};

void pingpong_complete(int rc, pmixp_srv_cb_context_t ctx, void *data)
{
	struct pp_cbdata *d = (struct pp_cbdata*)data;
	free_buf(d->buf);
	xfree(data);
	//    PMIXP_ERROR("Send complete: %d %lf", d->size, GET_TS - d->start);
}

int pmixp_server_pp_send(int nodeid, int size)
{
	Buf buf = pmixp_server_buf_new();
	int rc;
	pmixp_ep_t ep;
	struct pp_cbdata *cbdata = xmalloc(sizeof(*cbdata));

	grow_buf(buf, size);
	ep.type = PMIXP_EP_NOIDEID;
	ep.ep.nodeid = nodeid;
	cbdata->buf = buf;
	cbdata->size = size;
	set_buf_offset(buf,get_buf_offset(buf) + size);
	rc = pmixp_server_send_nb(&ep, PMIXP_MSG_PINGPONG,
				  _pmixp_pp_count, buf, pingpong_complete, (void*)cbdata);
	if (SLURM_SUCCESS != rc) {
		char *nodename = pmixp_info_job_host(nodeid);
		PMIXP_ERROR("Was unable to wait for the parent %s to become alive", nodename);
		xfree(nodename);
	}
	return rc;
}

#endif
