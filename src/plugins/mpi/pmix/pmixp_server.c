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

#define PMIX_SERVER_MSG_MAGIC 0xCAFECA11
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

Buf pmixp_server_new_buf(void)
{
	Buf buf = create_buf(xmalloc(PMIXP_MAX_SEND_HDR), PMIXP_MAX_SEND_HDR);
	/* Skip header. It will be filled right before the sending */
	set_buf_offset(buf, PMIXP_MAX_SEND_HDR);
	return buf;
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
	xassert(PMIX_SERVER_MSG_MAGIC == hdr->magic);

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

	pmixp_conn_fini();
	pmixp_dconn_fini();

	pmixp_libpmix_finalize();
	pmixp_dmdx_finalize();
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
	/* We should delete connection right when it  was closed or failed */
	xassert(false == obj->shutdown);

	return true;
}

static int _serv_read(eio_obj_t *obj, List objs)
{
	/* sanity check */
	xassert(NULL != obj );
	/* We should delete connection right when it  was closed or failed */
	xassert(false == obj->shutdown);

	PMIXP_DEBUG("fd = %d", obj->fd);
	pmixp_conn_t *conn = (pmixp_conn_t *)obj->arg;
	bool proceed = true;

	/* debug stub */
	static int dbg_block = 0;
	pmixp_debug_hang(dbg_block);

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
		}
	}
	return 0;
}

static bool _serv_writable(eio_obj_t *obj)
{
	/* sanity check */
	xassert(NULL != obj );
	/* We should delete connection right when it  was closed or failed */
	xassert(false == obj->shutdown);

	/* get I/O engine */
	pmixp_conn_t *conn = (pmixp_conn_t *)obj->arg;
	pmixp_io_engine_t *eng = conn->eng;

	/* debug stub */
	static int dbg_block = 0;
	pmixp_debug_hang(dbg_block);

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
	/* We should delete connection right when it  was closed or failed */
	xassert(false == obj->shutdown);

	PMIXP_DEBUG("fd = %d", obj->fd);
	pmixp_conn_t *conn = (pmixp_conn_t *)obj->arg;

	/* debug stub */
	static int dbg_block = 0;
	pmixp_debug_hang(dbg_block);

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
	if( NULL == nodename ){
		goto exit;
	}

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
		rc = pmixp_coll_check_seq(coll, hdr->seq, nodename);
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
			pmixp_coll_bcast(coll, buf);
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
		pmixp_dmdx_process(buf, nodename, hdr->seq);
		/* buf will be free'd by pmixp_dmdx_process or the PMIx callback so
		 * protect the data by voiding the buffer.
		 * Use the statement below instead of (buf = NULL) to maintain
		 * incapsulation - in general `buf` is not a pointer, but opaque type.
		 */
		buf = create_buf(NULL, 0);
		break;
	}
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

int pmixp_server_send(char *hostlist, pmixp_srv_cmd_t type, uint32_t seq,
		const char *addr, void *data, size_t size, int p2p)
{
	pmixp_slurm_shdr_t hdr;
	pmixp_base_hdr_t *bhdr = &hdr.base_hdr;
	char nhdr[sizeof(hdr)];
	size_t hsize;
	int rc;

	bhdr->magic = PMIX_SERVER_MSG_MAGIC;
	bhdr->type = type;
	bhdr->msgsize = size - PMIXP_SLURM_API_SHDR_SIZE;
	bhdr->seq = seq;
	/* Store global nodeid that is
	 *  independent from exact collective */
	bhdr->nodeid = pmixp_info_nodeid_job();

	/* Temp: for the verification purposes */
	hdr.rport = 100;

	hsize = _slurm_pack_hdr(&hdr, nhdr);
	memcpy(data, nhdr, hsize);

	if( !p2p ){
		rc = pmixp_stepd_send(hostlist, addr, data, size, 500, 7, 0);
	} else {
		rc = pmixp_p2p_send(hostlist, addr, data, size, 500, 7, 0);
	}
	if (SLURM_SUCCESS != rc) {
		PMIXP_ERROR("Cannot send message to %s, size = %u, hostlist:\n%s",
			    addr, (uint32_t) size, hostlist);
	}
	return rc;
}

/*
 * ------------------- DIRECT communication protocol -----------------------
 */


typedef struct {
	pmixp_base_hdr_t hdr;
	void *payload;
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
	return &_msg->payload;
}

static void _direct_msg_free(void *msg)
{
	_direct_proto_message_t *_msg = (_direct_proto_message_t*)msg;
	xfree(_msg->payload);
	xfree(_msg);
}

/*
 * See process_handler_t prototype description
 * on the details of this function output values
 */
static void _direct_new_msg(pmixp_conn_t *conn, void *_hdr, void *msg)
{
	pmixp_slurm_rhdr_t *hdr = (pmixp_slurm_rhdr_t *)_hdr;
	_process_server_request(&hdr->shdr.base_hdr, msg);
}

static void _direct_connection_drop(pmixp_conn_t *conn)
{
	pmixp_dconn_t *dconn = (pmixp_dconn_t*)pmixp_conn_get_data(conn);
	pmixp_dconn_lock(dconn->nodeid);
	/* TODO: close conn:
	 * pmixp_dconn_close(dconn);
	 */
	pmixp_dconn_unlock(dconn);
}

/*
 * Receive the first message identifying initiator
 */
static void
_direct_conn_establish(pmixp_conn_t *conn, void *_hdr, void *msg)
{
	pmixp_io_engine_t *eng = pmixp_conn_get_eng(conn);
	pmixp_base_hdr_t *hdr = (pmixp_base_hdr_t *)hdr;
	pmixp_dconn_t *dconn = NULL;
	pmixp_conn_t *new_conn;
	eio_obj_t *obj;
	int fd;

	xassert(0 == hdr->msgsize);
	fd = pmixp_io_detach(eng);

	dconn = pmixp_dconn_establish(hdr->nodeid, fd);
	if( NULL == dconn ){
		/* connection was refused because we already
			 * have established connection
			 * It seems that some sort of race condition occured
			 */
	}
	new_conn = pmixp_conn_new_persist(PMIXP_PROTO_DIRECT, pmixp_dconn_engine(dconn),
				      _direct_new_msg, _direct_connection_drop, dconn);
	obj = eio_obj_create(fd, &direct_peer_ops, (void *)new_conn);
	eio_new_obj(pmixp_info_io(), obj);
}

/*
 * TODO: we need to keep track of the "me"
 * structures created here, because we need to
 * free them in "pmixp_stepd_finalize"
 */
void pmixp_server_direct_conn(int fd)
{
	eio_obj_t *obj;
	pmixp_conn_t *conn;
	PMIXP_DEBUG("Request from fd = %d", fd);

	/* Set nonblocking */
	fd_set_nonblocking(fd);
	fd_set_close_on_exec(fd);
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
	xassert(hdr->magic == PMIX_SERVER_MSG_MAGIC);
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
	/* TODO: use real rport in future */
	shdr->rport = 100;

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

	/* TODO: get rid of this */
	xassert(rhdr->shdr.rport == 100);
	return 0;
}


