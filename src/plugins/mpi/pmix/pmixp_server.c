/*****************************************************************************\
 **  pmix_server.c - PMIx server side functionality
 *****************************************************************************
 *  Copyright (C) 2014-2015 Artem Polyakov. All rights reserved.
 *  Copyright (C) 2015-2018 Mellanox Technologies. All rights reserved.
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

#include "src/common/slurm_auth.h"

#define PMIXP_DEBUG_SERVER 1

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
	uint8_t ext_flag;
} pmixp_base_hdr_t;

#define PMIXP_BASE_HDR_SIZE (5 * sizeof(uint32_t) + sizeof(uint8_t))
#define PMIXP_BASE_HDR_EXT_SIZE(ep_len) (sizeof(uint32_t) + ep_len)
#define PMIXP_BASE_HDR_MAX (PMIXP_BASE_HDR_SIZE + \
	PMIXP_BASE_HDR_EXT_SIZE(pmixp_dconn_ep_len()))
/* In the server buffer we have one service field of type uint32_t */
#define PMIXP_SERVER_BUFFER_OFFS (PMIXP_BASE_HDR_MAX + sizeof(uint32_t))

typedef struct {
	uint32_t size;		/* Has to be first (appended by Slurm API) */
	pmixp_base_hdr_t shdr;
} pmixp_slurm_rhdr_t;
#define PMIXP_SAPI_RECV_HDR_SIZE (sizeof(uint32_t) + PMIXP_BASE_HDR_SIZE)

#define PMIXP_BASE_HDR_SETUP(bhdr, mtype, mseq, buf)			\
{									\
	bhdr.magic = PMIXP_SERVER_MSG_MAGIC;				\
	bhdr.type = mtype;						\
	bhdr.msgsize = get_buf_offset(buf) - PMIXP_SERVER_BUFFER_OFFS;	\
	bhdr.seq = mseq;						\
	bhdr.nodeid = pmixp_info_nodeid_job();				\
	bhdr.ext_flag = 0;						\
}

#define PMIXP_SERVER_BUF_MAGIC 0xCA11CAFE
Buf pmixp_server_buf_new(void)
{
	size_t offset = PMIXP_SERVER_BUFFER_OFFS;
	Buf buf = create_buf(xmalloc(offset), offset);
	uint32_t *service = (uint32_t*)get_buf_data(buf);
	/* Use the first size_t cell to identify the payload
	 * offset. Value 0 is special meaning that buffer wasn't
	 * yet finalized
	 */
	service[0] = 0;

#ifdef PMIXP_DEBUG_SERVER
	xassert( PMIXP_BASE_HDR_MAX >= sizeof(uint32_t));

	/* Makesure that we only use buffers allocated through
	 * this call, because we reserve the space for the
	 * header here
	 */
	service[1] = PMIXP_SERVER_BUF_MAGIC;
#endif

	/* Skip header. It will be filled right before the sending */
	set_buf_offset(buf, offset);
	return buf;
}

size_t pmixp_server_buf_reset(Buf buf)
{
	uint32_t *service = (uint32_t*)get_buf_data(buf);
	service[0] = 0;
#ifdef PMIXP_DEBUG_SERVER
	xassert( PMIXP_BASE_HDR_MAX >= sizeof(uint32_t));
	xassert( PMIXP_BASE_HDR_MAX <= get_buf_offset(buf) );
	/* Makesure that we only use buffers allocated through
	 * this call, because we reserve the space for the
	 * header here
	 */
	service[1] = PMIXP_SERVER_BUF_MAGIC;
#endif
	set_buf_offset(buf, PMIXP_SERVER_BUFFER_OFFS);
	return PMIXP_SERVER_BUFFER_OFFS;
}


static void *_buf_finalize(Buf buf, void *nhdr, size_t hsize,
			   size_t *dsize)
{
	size_t offset;
	uint32_t *service = (uint32_t*)get_buf_data(buf);
	char *ptr = get_buf_data(buf);
	if (!service[0]) {
		offset = PMIXP_SERVER_BUFFER_OFFS - hsize;
#ifdef PMIXP_DEBUG_SERVER
		xassert(PMIXP_BASE_HDR_MAX >= hsize);
		xassert(PMIXP_BASE_HDR_MAX <= get_buf_offset(buf));
		/* Makesure that we only use buffers allocated through
		 * this call, because we reserve the space for the
		 * header here
		 */
		xassert(PMIXP_SERVER_BUF_MAGIC == service[1]);
#endif
		/* Enough space for any header was reserved at the
		 * time of buffer initialization in `pmixp_server_new_buf`
		 * put the header in place and return proper pointer
		 */
		if (hsize) {
			memcpy(ptr + offset, nhdr, hsize);
		}
		service[0] = offset;
	} else {
		/* This buffer was already finalized */
		offset = service[0];
#ifdef PMIXP_DEBUG_SERVER
		/* We expect header to be the same */
		xassert(0 == memcmp(ptr+offset, nhdr, hsize));
#endif
	}
	*dsize = get_buf_offset(buf) - offset;
	return ptr + offset;
}

static void _base_hdr_pack_full(Buf packbuf, pmixp_base_hdr_t *hdr)
{
	if (hdr->ext_flag) {
		hdr->msgsize += PMIXP_BASE_HDR_EXT_SIZE(pmixp_dconn_ep_len());
	}
	pack32(hdr->magic, packbuf);
	pack32(hdr->type, packbuf);
	pack32(hdr->seq, packbuf);
	pack32(hdr->nodeid, packbuf);
	pack32(hdr->msgsize, packbuf);
	pack8(hdr->ext_flag, packbuf);
	if (hdr->ext_flag) {
		packmem(pmixp_dconn_ep_data(), pmixp_dconn_ep_len(), packbuf);
		xassert(get_buf_offset(packbuf) ==
			(PMIXP_BASE_HDR_SIZE +
			 PMIXP_BASE_HDR_EXT_SIZE(pmixp_dconn_ep_len())));
	}
}

#define WRITE_HDR_FIELD(dst, offset, field) {			\
	memcpy((dst) + (offset), &(field), sizeof(field));	\
	offset += sizeof(field);				\
}

static size_t _base_hdr_pack_full_samearch(pmixp_base_hdr_t *hdr, void *net)
{
	int offset = 0;

	if (hdr->ext_flag) {
		hdr->msgsize += PMIXP_BASE_HDR_EXT_SIZE(pmixp_dconn_ep_len());
	}

	WRITE_HDR_FIELD(net, offset, hdr->magic);
	WRITE_HDR_FIELD(net, offset, hdr->type);
	WRITE_HDR_FIELD(net, offset, hdr->seq);
	WRITE_HDR_FIELD(net, offset, hdr->nodeid);
	WRITE_HDR_FIELD(net, offset, hdr->msgsize);
	WRITE_HDR_FIELD(net, offset, hdr->ext_flag);
	if (hdr->ext_flag) {
		Buf buf = create_buf(net + offset, PMIXP_BASE_HDR_MAX);
		packmem(pmixp_dconn_ep_data(), pmixp_dconn_ep_len(), buf);
		offset += get_buf_offset(buf);
		buf->head = NULL;
		FREE_NULL_BUFFER(buf);
	}
	return offset;
}


static int _base_hdr_unpack_fixed(Buf packbuf, pmixp_base_hdr_t *hdr)
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

	if (unpack8(&hdr->ext_flag, packbuf)) {
		return -EINVAL;
	}

	return 0;
}

#define READ_HDR_FIELD(src, offset, field) {			\
	memcpy(&(field), (src) + (offset), sizeof(field));	\
	offset += sizeof(field);				\
}

static int _base_hdr_unpack_fixed_samearch(void *net, void *host)
{
	size_t offset = 0;
	pmixp_base_hdr_t *hdr = (pmixp_base_hdr_t *)host;

	READ_HDR_FIELD(net, offset, hdr->magic);
	READ_HDR_FIELD(net, offset, hdr->type);
	READ_HDR_FIELD(net, offset, hdr->seq);
	READ_HDR_FIELD(net, offset, hdr->nodeid);
	READ_HDR_FIELD(net, offset, hdr->msgsize);
	READ_HDR_FIELD(net, offset, hdr->ext_flag);

	return 0;
}


static int _base_hdr_unpack_ext(Buf packbuf, char **ep_data, uint32_t *ep_len)
{
	if (unpackmem_xmalloc(ep_data, ep_len, packbuf)) {
		return -EINVAL;
	}
	return 0;
}


static int _sapi_rhdr_unpack_fixed(Buf packbuf, pmixp_slurm_rhdr_t *hdr)
{
	if (unpack32(&hdr->size, packbuf)) {
		return -EINVAL;
	}

	if (_base_hdr_unpack_fixed(packbuf, &hdr->shdr)) {
		return -EINVAL;
	}

	return 0;
}

/* Slurm protocol I/O header */
static uint32_t _slurm_proto_msize(void *buf);
static int _slurm_pack_hdr(pmixp_base_hdr_t *hdr, void *net);
static int _slurm_proto_unpack_hdr(void *net, void *host);
static void _slurm_new_msg(pmixp_conn_t *conn, void *_hdr, void *msg);
static int _slurm_send(pmixp_ep_t *ep, pmixp_base_hdr_t bhdr, Buf buf);

pmixp_p2p_data_t _slurm_proto = {
	/* generic callbacks */
	.payload_size_cb = _slurm_proto_msize,
	/* receiver-related fields */
	.recv_on = 1,
	.rhdr_host_size = sizeof(pmixp_slurm_rhdr_t),
	.rhdr_net_size = PMIXP_SAPI_RECV_HDR_SIZE, /*need to skip user ID*/
	.recv_padding = sizeof(uint32_t),
	.hdr_unpack_cb = _slurm_proto_unpack_hdr,
};

/* direct protocol I/O header */
static uint32_t _direct_paysize(void *hdr);
static size_t _direct_hdr_pack_portable(pmixp_base_hdr_t *hdr, void *net);
static int _direct_hdr_unpack_portable(void *net, void *host);
static size_t _direct_hdr_pack_samearch(pmixp_base_hdr_t *hdr, void *net);
static int _direct_hdr_unpack_samearch(void *net, void *host);
typedef size_t (*_direct_hdr_pack_t)(pmixp_base_hdr_t *hdr, void *net);
_direct_hdr_pack_t _direct_hdr_pack = _direct_hdr_pack_samearch;


static void *_direct_msg_ptr(void *msg);
static size_t _direct_msg_size(void *msg);
static void _direct_send_complete(void *msg, pmixp_p2p_ctx_t ctx, int rc);

static void _direct_new_msg(void *hdr, Buf buf);

static void _direct_new_msg_conn(pmixp_conn_t *conn, void *_hdr, void *msg);
static void _direct_send(pmixp_dconn_t *dconn, pmixp_ep_t *ep,
			 pmixp_base_hdr_t bhdr, Buf buf,
			 pmixp_server_sent_cb_t complete_cb, void *cb_data);
static void _direct_return_connection(pmixp_conn_t *conn);

typedef struct {
	pmixp_base_hdr_t hdr;
	void *buffer;
	Buf buf_ptr;
	pmixp_server_sent_cb_t sent_cb;
	void *cbdata;
}_direct_proto_message_t;

pmixp_p2p_data_t _direct_proto = {
	/* receiver-related fields */
	.recv_on = 1,
	.rhdr_host_size = sizeof(pmixp_base_hdr_t),
	.rhdr_net_size = PMIXP_BASE_HDR_SIZE,
	.recv_padding = 0, /* no padding for the direct proto */
	.payload_size_cb = _direct_paysize,
	.hdr_unpack_cb = _direct_hdr_unpack_samearch,
	.new_msg = _direct_new_msg,
	/* transmitter-related fields */
	.send_on = 1,
	.buf_ptr = _direct_msg_ptr,
	.buf_size = _direct_msg_size,
	.send_complete = _direct_send_complete
};


/*
 * --------------------- Initi/Finalize -------------------
 */

static volatile int _was_initialized = 0;

int pmixp_stepd_init(const stepd_step_rec_t *job, char ***env)
{
	char *path;
	int fd, rc;

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


	if (!pmixp_info_same_arch()){
		_direct_proto.hdr_unpack_cb = _direct_hdr_unpack_portable;
		_direct_hdr_pack = _direct_hdr_pack_portable;
	}

	pmixp_conn_init(_slurm_proto, _direct_proto);

	if((rc = pmixp_dconn_init(pmixp_info_nodes_uni(), _direct_proto)) ){
		PMIXP_ERROR("pmixp_dconn_init() failed");
		goto err_dconn;
	}

	if ((rc = pmixp_nspaces_init())) {
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
	pmixp_server_init_cperf(env);

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
	pmixp_dconn_fini();
err_dconn:
	pmixp_conn_fini();
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
 * --------------------- Authentication functionality -------------------
 */

static int _auth_cred_create(Buf buf)
{
	void *auth_cred = NULL;
	char *auth_info = slurm_get_auth_info();
	int rc = SLURM_SUCCESS;

	auth_cred = g_slurm_auth_create(AUTH_DEFAULT_INDEX, auth_info);
	xfree(auth_info);
	if (!auth_cred) {
		PMIXP_ERROR("Creating authentication credential: %m");
		return errno;
	}

	/*
	 * We can use SLURM_PROTOCOL_VERSION here since there is no possibility
	 * of protocol mismatch.
	 */
	rc = g_slurm_auth_pack(auth_cred, buf, SLURM_PROTOCOL_VERSION);
	if (rc)
		PMIXP_ERROR("Packing authentication credential: %m");

	g_slurm_auth_destroy(auth_cred);

	return rc;
}

static int _auth_cred_verify(Buf buf)
{
	void *auth_cred = NULL;
	char *auth_info = NULL;
	int rc = SLURM_SUCCESS;

	/*
	 * We can use SLURM_PROTOCOL_VERSION here since there is no possibility
	 * of protocol mismatch.
	 */
	auth_cred = g_slurm_auth_unpack(buf, SLURM_PROTOCOL_VERSION);
	if (!auth_cred) {
		PMIXP_ERROR("Unpacking authentication credential: %m");
		return SLURM_ERROR;
	}

	auth_info = slurm_get_auth_info();
	rc = g_slurm_auth_verify(auth_cred, auth_info);
	xfree(auth_info);

	if (rc)
		PMIXP_ERROR("Verifying authentication credential: %m");
	g_slurm_auth_destroy(auth_cred);
	return rc;
}

/*
 * --------------------- Generic I/O functionality -------------------
 */

static bool _serv_readable(eio_obj_t *obj);
static int _serv_read(eio_obj_t *obj, List objs);
static bool _serv_writable(eio_obj_t *obj);
static int _serv_write(eio_obj_t *obj, List objs);
static void _process_server_request(pmixp_base_hdr_t *hdr, Buf buf);


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
	if (obj->shutdown) {
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
	if (obj->shutdown) {
		/* corresponding connection will be
		 * cleaned up during plugin finalize
		 */
		return 0;
	}

	pmixp_conn_t *conn = (pmixp_conn_t *)obj->arg;
	bool proceed = true;

	/* debug stub */
	pmixp_debug_hang(0);

	/* Read and process all received messages */
	while (proceed) {
		if (!pmixp_conn_progress_rcv(conn)) {
			proceed = 0;
		}
		if (!pmixp_conn_is_alive(conn)) {
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
	if (obj->shutdown) {
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
	pmixp_io_send_cleanup(eng, PMIXP_P2P_REGULAR);

	/* check if we have something to send */
	if (pmixp_io_send_pending(eng)) {
		return true;
	}
	return false;
}

static int _serv_write(eio_obj_t *obj, List objs)
{
	/* sanity check */
	xassert(NULL != obj );
	if (obj->shutdown) {
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
	if (!pmixp_conn_is_alive(conn)) {
		obj->shutdown = true;
		PMIXP_DEBUG("Connection finalized fd = %d", obj->fd);
		/* cleanup after this connection */
		eio_remove_obj(obj, objs);
		pmixp_conn_return(conn);
	}
	return 0;
}

static int _process_extended_hdr(pmixp_base_hdr_t *hdr, Buf buf)
{
	char nhdr[PMIXP_BASE_HDR_MAX];
	bool send_init = false;
	size_t dsize = 0, hsize = 0;
	pmixp_dconn_t *dconn;
	_direct_proto_message_t *init_msg = NULL;
	int rc = SLURM_SUCCESS;
	char *ep_data = NULL;
	uint32_t ep_len = 0;

	dconn = pmixp_dconn_lock(hdr->nodeid);
	if (!dconn) {
		/* Should not happen */
		xassert( dconn );
		abort();
	}

	/* Retrieve endpoint information */
	_base_hdr_unpack_ext(buf, &ep_data, &ep_len);

	/* Check if init message is required to establish
	 * the connection
	 */
	if (!pmixp_dconn_require_connect(dconn, &send_init)) {
		goto unlock;
	}

	if (send_init) {
		Buf buf_init = pmixp_server_buf_new();
		pmixp_base_hdr_t bhdr;
		init_msg = xmalloc(sizeof(*init_msg));

		rc = _auth_cred_create(buf_init);
		if (rc) {
			FREE_NULL_BUFFER(init_msg->buf_ptr);
			xfree(init_msg);
			goto unlock;
		}
		PMIXP_BASE_HDR_SETUP(bhdr, PMIXP_MSG_INIT_DIRECT, 0, buf_init);
		bhdr.ext_flag = 1;
		hsize = _direct_hdr_pack(&bhdr, nhdr);

		init_msg->sent_cb = pmixp_server_sent_buf_cb;
		init_msg->cbdata = buf_init;
		init_msg->hdr = bhdr;
		init_msg->buffer = _buf_finalize(buf_init, nhdr, hsize,
						 &dsize);
		init_msg->buf_ptr = buf_init;
	}

	rc = pmixp_dconn_connect(dconn, ep_data, ep_len, init_msg);
	if (rc) {
		PMIXP_ERROR("Unable to connect to %d", dconn->nodeid);
		if (init_msg) {
			/* need to release `init_msg` here */
			FREE_NULL_BUFFER(init_msg->buf_ptr);
			xfree(init_msg);
		}
		goto unlock;
	}

	switch (pmixp_dconn_progress_type(dconn)) {
	case PMIXP_DCONN_PROGRESS_SW:{
		/* this direct connection has fd that needs to be
		 * polled to progress, use connection interface for that
		 */
		pmixp_io_engine_t *eng = pmixp_dconn_engine(dconn);
		pmixp_conn_t *conn;
		conn = pmixp_conn_new_persist(PMIXP_PROTO_DIRECT, eng,
					      _direct_new_msg_conn,
					      _direct_return_connection,
					      dconn);
		if (conn) {
			eio_obj_t *obj;
			obj = eio_obj_create(pmixp_io_fd(eng),
					     &direct_peer_ops,
					     (void *)conn);
			eio_new_obj(pmixp_info_io(), obj);
			eio_signal_wakeup(pmixp_info_io());
		} else {
			/* TODO: handle this error */
			rc = SLURM_ERROR;
			goto unlock;
		}
		break;
	}
	case PMIXP_DCONN_PROGRESS_HW: {
		break;
	}
	default:
		/* Should not happen */
		xassert(0 && pmixp_dconn_progress_type(dconn));
		/* TODO: handle this error */
	}
unlock:
	pmixp_dconn_unlock(dconn);
	return rc;
}

static void _process_server_request(pmixp_base_hdr_t *hdr, Buf buf)
{
	int rc;

	switch (hdr->type) {
	case PMIXP_MSG_FAN_IN:
	case PMIXP_MSG_FAN_OUT: {
		pmixp_coll_t *coll;
		pmixp_proc_t *procs = NULL;
		size_t nprocs = 0;
		pmixp_coll_type_t type = 0;
		int c_nodeid;

		rc = pmixp_coll_tree_unpack(buf, &type, &c_nodeid,
					    &procs, &nprocs);
		if (SLURM_SUCCESS != rc) {
			char *nodename = pmixp_info_job_host(hdr->nodeid);
			PMIXP_ERROR("Bad message header from node %s",
				    nodename);
			xfree(nodename);
			goto exit;
		}
		if (PMIXP_COLL_TYPE_FENCE_TREE != type) {
			char *nodename = pmixp_info_job_host(hdr->nodeid);
			PMIXP_ERROR("Unexpected collective type=%s from node %s, expected=%s",
				    pmixp_coll_type2str(type), nodename,
				    pmixp_coll_type2str(PMIXP_COLL_TYPE_FENCE_TREE));
			xfree(nodename);
			goto exit;
		}
		coll = pmixp_state_coll_get(type, procs, nprocs);
		xfree(procs);
		if (!coll) {
			PMIXP_ERROR("Unable to pmixp_state_coll_get()");
			break;
		}
		pmixp_coll_sanity_check(coll);
#ifdef PMIXP_COLL_DEBUG
		PMIXP_DEBUG("%s collective message from nodeid = %u, type = %s, seq = %d",
			    pmixp_coll_type2str(type),
			    hdr->nodeid,
			    ((PMIXP_MSG_FAN_IN == hdr->type) ?
				     "fan-in" : "fan-out"),
			    hdr->seq);
#endif
		rc = pmixp_coll_check(coll, hdr->seq);
		if (PMIXP_COLL_REQ_FAILURE == rc) {
			/* this is an unacceptable event: either something went
			 * really wrong or the state machine is incorrect.
			 * This will 100% lead to application hang.
			 */
			char *nodename = pmixp_info_job_host(hdr->nodeid);
			PMIXP_ERROR("Bad collective seq. #%d from %s:%u, current is %d",
				    hdr->seq, nodename, hdr->nodeid, coll->seq);
			pmixp_debug_hang(0); /* enable hang to debug this! */
			slurm_kill_job_step(pmixp_info_jobid(),
					    pmixp_info_stepid(), SIGKILL);
			xfree(nodename);
			break;
		} else if (PMIXP_COLL_REQ_SKIP == rc) {
#ifdef PMIXP_COLL_DEBUG
			PMIXP_DEBUG("Wrong collective seq. #%d from nodeid %u, current is %d, skip this message",
				    hdr->seq, hdr->nodeid, coll->seq);
#endif
			goto exit;
		}

		if (PMIXP_MSG_FAN_IN == hdr->type) {
			pmixp_coll_tree_child(coll, hdr->nodeid,
					      hdr->seq, buf);
		} else {
			pmixp_coll_tree_parent(coll, hdr->nodeid,
					       hdr->seq, buf);
		}

		break;
	}
	case PMIXP_MSG_DMDX: {
		pmixp_dmdx_process(buf, hdr->nodeid, hdr->seq);
		/* buf will be free'd by the PMIx callback so
		 * protect the data by voiding the buffer.
		 * Use the statement below instead of (buf = NULL)
		 * to maintain incapsulation - in general `buf`is
		 * not a pointer, but opaque type.
		 */
		buf = create_buf(NULL, 0);
		break;
	}
	case PMIXP_MSG_INIT_DIRECT:
		PMIXP_DEBUG("Direct connection init from %d", hdr->nodeid);
		break;
#ifndef NDEBUG
	case PMIXP_MSG_PINGPONG: {
		/* if the pingpong mode was activated -
		 * node 0 sends ping requests
		 * and receiver assumed to respond back to node 0
		 */
		int msize = remaining_buf(buf);

		if (pmixp_info_nodeid()) {
			pmixp_server_pp_send(0, msize);
		} else {
			if (pmixp_server_pp_same_thread()) {
				if (pmixp_server_pp_count() ==
				    pmixp_server_pp_warmups()) {
					pmixp_server_pp_start();
				}
				if (!pmixp_server_pp_check_fini(msize)) {
					pmixp_server_pp_send(1, msize);
				}
			}
		}
		pmixp_server_pp_inc();
		break;
	}
#endif
	case PMIXP_MSG_RING: {
		pmixp_coll_t *coll = NULL;
		pmixp_proc_t *procs = NULL;
		size_t nprocs = 0;
		pmixp_coll_ring_msg_hdr_t ring_hdr;
		pmixp_coll_type_t type = 0;

		if (pmixp_coll_ring_unpack(buf, &type, &ring_hdr,
					   &procs, &nprocs)) {
			char *nodename = pmixp_info_job_host(hdr->nodeid);
			PMIXP_ERROR("Bad message header from node %s",
				    nodename);
			xfree(nodename);
			goto exit;
		}
		if (PMIXP_COLL_TYPE_FENCE_RING != type) {
			char *nodename = pmixp_info_job_host(hdr->nodeid);
			PMIXP_ERROR("Unexpected collective type=%s from node %s:%u, expected=%s",
				   pmixp_coll_type2str(type), nodename, hdr->nodeid,
				   pmixp_coll_type2str(PMIXP_COLL_TYPE_FENCE_RING));
			xfree(nodename);
			goto exit;
		}

		coll = pmixp_state_coll_get(type, procs, nprocs);
		xfree(procs);
		if (!coll) {
			PMIXP_ERROR("Unable to pmixp_state_coll_get()");
			break;
		}
		pmixp_coll_sanity_check(coll);
#ifdef PMIXP_COLL_DEBUG
		PMIXP_DEBUG("%s collective message from nodeid=%u, contrib_id=%u, seq=%u, hop=%u, msgsize=%lu",
			    pmixp_coll_type2str(type),
			    hdr->nodeid, ring_hdr.contrib_id,
			    ring_hdr.seq, ring_hdr.hop_seq, ring_hdr.msgsize);
#endif
		if (pmixp_coll_ring_check(coll, &ring_hdr)) {
			char *nodename = pmixp_info_job_host(hdr->nodeid);
			PMIXP_ERROR("%p: unexpected contrib from %s:%u, coll->seq=%d, seq=%d",
				    coll, nodename, hdr->nodeid,
				    coll->seq, hdr->seq);
			xfree(nodename);
			break;
		}
		pmixp_coll_ring_neighbor(coll, &ring_hdr, buf);
		break;
	}
	default:
		PMIXP_ERROR("Unknown message type %d", hdr->type);
		break;
	}

exit:
	FREE_NULL_BUFFER(buf);
}

void pmixp_server_sent_buf_cb(int rc, pmixp_p2p_ctx_t ctx, void *data)
{
	Buf buf = (Buf)data;
	FREE_NULL_BUFFER(buf);
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
	 * always use Slurm protocol
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
			PMIXP_ERROR("Bad direct connection state: %d",
				    (int)state);
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
	complete_cb(rc, PMIXP_P2P_INLINE, cb_data);
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

/* Size of the payload */
static uint32_t _direct_paysize(void *buf)
{
	pmixp_base_hdr_t *hdr = (pmixp_base_hdr_t *)buf;
	return hdr->msgsize;
}

/*
 * Unpack message header.
 * Returns 0 on success and -errno on failure
 */
static int _direct_hdr_unpack_portable(void *net, void *host)
{
	pmixp_base_hdr_t *hdr = (pmixp_base_hdr_t *)host;
	Buf packbuf = create_buf(net, PMIXP_BASE_HDR_SIZE);

	if (_base_hdr_unpack_fixed(packbuf, hdr)) {
		return -EINVAL;
	}

	/* free the Buf packbuf, but not the memory it points to */
	packbuf->head = NULL;
	FREE_NULL_BUFFER(packbuf);
	return 0;
}

static int _direct_hdr_unpack_samearch(void *net, void *host)
{
	return _base_hdr_unpack_fixed_samearch(net, host);
}

/*
 * Pack message header. Returns packed size
 */
static size_t _direct_hdr_pack_portable(pmixp_base_hdr_t *hdr, void *net)
{
	Buf buf = create_buf(net, PMIXP_BASE_HDR_MAX);
	int size = 0;
	_base_hdr_pack_full(buf, hdr);
	size = get_buf_offset(buf);
	xassert(size >= PMIXP_BASE_HDR_SIZE);
	xassert(size <= PMIXP_BASE_HDR_MAX);
	/* free the Buf packbuf, but not the memory it points to */
	buf->head = NULL;
	FREE_NULL_BUFFER(buf);
	return size;
}

static size_t _direct_hdr_pack_samearch(pmixp_base_hdr_t *hdr, void *net)
{
	return _base_hdr_pack_full_samearch(hdr, net);
}

/* Get te pointer to the message bufer */
static void *_direct_msg_ptr(void *msg)
{
	_direct_proto_message_t *_msg = (_direct_proto_message_t*)msg;
	return _msg->buffer;
}

/* Message size */
static size_t _direct_msg_size(void *msg)
{
	_direct_proto_message_t *_msg = (_direct_proto_message_t*)msg;
	return (_msg->hdr.msgsize + PMIXP_BASE_HDR_SIZE);
}

/* Release message.
 * TODO: We need to fix that: I/O engine needs a way
 * to provide the error code
 */
static void _direct_send_complete(void *_msg, pmixp_p2p_ctx_t ctx, int rc)
{
	_direct_proto_message_t *msg = (_direct_proto_message_t*)_msg;
	msg->sent_cb(rc, ctx, msg->cbdata);
	xfree(msg);
}

/*
 * TODO: merge with _direct_new_msg as they have nearly similar functionality
 * This one is part of I/O header.
 */
static void _direct_new_msg(void *_hdr, Buf buf)
{
	pmixp_base_hdr_t *hdr = (pmixp_base_hdr_t*)_hdr;
	if (hdr->ext_flag) {
		/* Extra information was incorporated into this message.
		 * This should be an endpoint data
		 */
		_process_extended_hdr(hdr, buf);
	}
	_process_server_request(hdr, buf);
}

/*
 * See process_handler_t prototype description
 * on the details of this function output values
 */
static void _direct_new_msg_conn(pmixp_conn_t *conn, void *_hdr, void *msg)
{
	pmixp_base_hdr_t *hdr = (pmixp_base_hdr_t*)_hdr;
	Buf buf = create_buf(msg, hdr->msgsize);
	_process_server_request(hdr, buf);
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
	int fd = pmixp_io_detach(eng);
	char *ep_data = NULL;
	uint32_t ep_len = 0;
	Buf buf_msg;
	int rc;
	char *nodename = NULL;

	if (!hdr->ext_flag) {
		nodename = pmixp_info_job_host(hdr->nodeid);
		PMIXP_ERROR("Connection failed from %u(%s)",
			    hdr->nodeid, nodename);
		xfree(nodename);
		close(fd);
		return;
	}

	buf_msg = create_buf(msg, hdr->msgsize);
	/* Retrieve endpoint information */
	rc = _base_hdr_unpack_ext(buf_msg, &ep_data, &ep_len);
	if (rc) {
		FREE_NULL_BUFFER(buf_msg);
		close(fd);
		nodename = pmixp_info_job_host(hdr->nodeid);
		PMIXP_ERROR("Failed to unpack the direct connection message from %u(%s)",
			    hdr->nodeid, nodename);
		xfree(nodename);
		return;
	}
	/* Unpack and verify the auth credential */
	rc = _auth_cred_verify(buf_msg);
	FREE_NULL_BUFFER(buf_msg);
	if (rc) {
		close(fd);
		nodename = pmixp_info_job_host(hdr->nodeid);
		PMIXP_ERROR("Connection reject from %u(%s)",
			    hdr->nodeid, nodename);
		xfree(nodename);
		return;
	}

	dconn = pmixp_dconn_accept(hdr->nodeid, fd);
	if (!dconn) {
		/* connection was refused because we already
		 * have established connection
		 * It seems that some sort of race condition occured
		 */
		close(fd);
		nodename = pmixp_info_job_host(hdr->nodeid);
		PMIXP_ERROR("Failed to accept direct connection from %u(%s)",
			    hdr->nodeid, nodename);
		xfree(nodename);
		return;
	}
	new_conn = pmixp_conn_new_persist(PMIXP_PROTO_DIRECT,
					  pmixp_dconn_engine(dconn),
					  _direct_new_msg_conn,
					  _direct_return_connection, dconn);
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
	conn = pmixp_conn_new_temp(PMIXP_PROTO_DIRECT, fd,
				   _direct_conn_establish);

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
	char nhdr[PMIXP_BASE_HDR_SIZE];
	size_t dsize = 0, hsize = 0;
	int rc;

	hsize = _direct_hdr_pack(&bhdr, nhdr);

	xassert(PMIXP_EP_NOIDEID == ep->type);
	/* TODO: I think we can avoid locking */
	_direct_proto_message_t *msg = xmalloc(sizeof(*msg));
	msg->sent_cb = complete_cb;
	msg->cbdata = cb_data;
	msg->hdr = bhdr;
	msg->buffer = _buf_finalize(buf, nhdr, hsize, &dsize);
	msg->buf_ptr = buf;


	rc = pmixp_dconn_send(dconn, msg);
	if (SLURM_SUCCESS != rc) {
		msg->sent_cb(rc, PMIXP_P2P_INLINE, msg->cbdata);
		xfree( msg );
	}
}

int pmixp_server_direct_conn_early(void)
{
	pmixp_coll_type_t types[] = { PMIXP_COLL_TYPE_FENCE_TREE, PMIXP_COLL_TYPE_FENCE_RING };
	pmixp_coll_type_t type = pmixp_info_srv_fence_coll_type();
	pmixp_coll_t *coll[PMIXP_COLL_TYPE_FENCE_MAX] = { NULL };
	int i, rc, count = 0;
	pmixp_proc_t proc;

	PMIXP_DEBUG("called");
	proc.rank = pmixp_lib_get_wildcard();
	strncpy(proc.nspace, _pmixp_job_info.nspace, PMIXP_MAX_NSLEN);

	for (i=0; i < sizeof(types)/sizeof(types[0]); i++){
		if (type != PMIXP_COLL_TYPE_FENCE_MAX && type != types[i]) {
			continue;
		}
		coll[count++] = pmixp_state_coll_get(types[i], &proc, 1);
	}
	/* use Tree algo by defaut */
	if (!count) {
		coll[count++] = pmixp_state_coll_get(PMIXP_COLL_TYPE_FENCE_TREE, &proc, 1);
	}
	for (i = 0; i < count; i++) {
		if (coll[i]) {
			pmixp_ep_t ep = {0};
			Buf buf;

			ep.type = PMIXP_EP_NOIDEID;

			switch (coll[i]->type) {
			case PMIXP_COLL_TYPE_FENCE_TREE:
				ep.ep.nodeid = coll[i]->state.tree.prnt_peerid;
				if (ep.ep.nodeid < 0) {
					/* this is the root node, it has no
					 * the parent node to early connect */
					continue;
				}
				break;
			case PMIXP_COLL_TYPE_FENCE_RING:
				/* calculate the id of the next ring neighbor */
				ep.ep.nodeid = (coll[i]->my_peerid + 1) %
						coll[i]->peers_cnt;
				break;
			default:
				PMIXP_ERROR("Unknown coll type");
				return SLURM_ERROR;
			}

			buf = pmixp_server_buf_new();
			rc = pmixp_server_send_nb(
				&ep, PMIXP_MSG_INIT_DIRECT, coll[i]->seq,
				buf, pmixp_server_sent_buf_cb, buf);

			if (SLURM_SUCCESS != rc) {
				PMIXP_ERROR_STD("send init msg error");
				return SLURM_ERROR;
			}
		}
	}
	return SLURM_SUCCESS;
}

/*
 * ------------------- Slurm communication protocol -----------------------
 */

/*
 * See process_handler_t prototype description
 * on the details of this function output values
 */
static void _slurm_new_msg(pmixp_conn_t *conn,
			   void *_hdr, void *msg)
{
	pmixp_slurm_rhdr_t *hdr = (pmixp_slurm_rhdr_t *)_hdr;
	Buf buf_msg = create_buf(msg, hdr->shdr.msgsize);

	if (hdr->shdr.ext_flag ) {
		/* Extra information was incorporated into this message.
		 * This should be an endpoint data
		 */
		_process_extended_hdr(&hdr->shdr, buf_msg);
	}
	_process_server_request(&hdr->shdr, buf_msg);
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
	pmixp_base_hdr_t *hdr = &ptr->shdr;
	xassert(ptr->size == hdr->msgsize + PMIXP_BASE_HDR_SIZE);
	xassert(hdr->magic == PMIXP_SERVER_MSG_MAGIC);
	return hdr->msgsize;
}

/*
 * Pack message header.
 * Returns packed size
 * Note: asymmetric to _recv_unpack_hdr because of additional Slurm header
 */
static int _slurm_pack_hdr(pmixp_base_hdr_t *hdr, void *net)
{
	Buf buf = create_buf(net, PMIXP_BASE_HDR_MAX);
	int size = 0;

	_base_hdr_pack_full(buf, hdr);
	size = get_buf_offset(buf);
	/* free the Buf packbuf, but not the memory it points to */
	buf->head = NULL;
	FREE_NULL_BUFFER(buf);
	return size;
}

/*
 * Unpack message header.
 * Returns 0 on success and -errno on failure
 * Note: asymmetric to _send_pack_hdr because of additional Slurm header
 */
static int _slurm_proto_unpack_hdr(void *net, void *host)
{
	pmixp_slurm_rhdr_t *rhdr = (pmixp_slurm_rhdr_t *)host;
	Buf packbuf = create_buf(net, PMIXP_SAPI_RECV_HDR_SIZE);
	if (_sapi_rhdr_unpack_fixed(packbuf, rhdr) ) {
		return -EINVAL;
	}
	/* free the Buf packbuf, but not the memory it points to */
	packbuf->head = NULL;
	FREE_NULL_BUFFER(packbuf);

	return 0;
}

static int _slurm_send(pmixp_ep_t *ep, pmixp_base_hdr_t bhdr, Buf buf)
{
	const char *addr = NULL, *data = NULL, *hostlist = NULL;
	char nhdr[PMIXP_BASE_HDR_MAX];
	size_t hsize = 0, dsize = 0;
	int rc;

	/* setup the header */
	addr = pmixp_info_srv_usock_path();

	bhdr.ext_flag = 0;
	if (pmixp_info_srv_direct_conn() && PMIXP_EP_NOIDEID == ep->type) {
		bhdr.ext_flag = 1;
	}

	hsize = _slurm_pack_hdr(&bhdr, nhdr);
	data = _buf_finalize(buf, nhdr, hsize, &dsize);

	switch( ep->type ){
	case PMIXP_EP_HLIST:
		hostlist = ep->ep.hostlist;
		rc = pmixp_stepd_send(ep->ep.hostlist, addr,
				      data, dsize, 500, 7, 0);
		break;
	case PMIXP_EP_NOIDEID: {
		char *nodename = pmixp_info_job_host(ep->ep.nodeid);
		char *address = xstrdup(addr);

		xstrsubstitute(address, "%n", nodename);
		xstrsubstitute(address, "%h", nodename);

		rc = pmixp_p2p_send(nodename, address, data, dsize,
				    500, 7, 0);
		xfree(address);
		xfree(nodename);
		break;
	}
	default:
		PMIXP_ERROR("Bad value of the EP type: %d", (int)ep->type);
		abort();
	}

	if (SLURM_SUCCESS != rc) {
		PMIXP_ERROR("Cannot send message to %s, size = %u, "
			    "hostlist:\n%s",
			    addr, (uint32_t) dsize, hostlist);
	}
	return rc;
}

/*
 * ------------------- communication DEBUG tools -----------------------
 */

#ifndef NDEBUG

/*
 * This is solely a debug code that helps to estimate
 * the performance of the communication subsystem
 * of the plugin
 */

static pthread_mutex_t _pmixp_pp_lock;

#define PMIXP_PP_PWR2_MIN 0
#define PMIXP_PP_PWR2_MAX 24

static bool _pmixp_pp_on = false;
static bool _pmixp_pp_same_thr = false;
static int _pmixp_pp_low = PMIXP_PP_PWR2_MIN;
static int _pmixp_pp_up = PMIXP_PP_PWR2_MAX;
static int _pmixp_pp_bound = 10;
static int _pmixp_pp_siter = 1000;
static int _pmixp_pp_liter = 100;

static volatile int _pmixp_pp_count = 0;

#include <time.h>
#define GET_TS() ({                             \
	struct timespec ts;                     \
	double ret = 0;                         \
	clock_gettime(CLOCK_MONOTONIC, &ts);    \
	ret = ts.tv_sec + 1E-9*ts.tv_nsec;      \
	ret;                                    \
	})

static volatile int _pmixp_pp_warmup = 0;
static volatile int _pmixp_pp_iters = 0;
static double _pmixp_pp_start = 0;

int pmixp_server_pp_count(void)
{
	return _pmixp_pp_count;
}

int pmixp_server_pp_warmups(void)
{
	return _pmixp_pp_warmup;
}

void pmixp_server_pp_inc(void)
{
	_pmixp_pp_count++;
}

int pmixp_server_pp_same_thread(void)
{
	return _pmixp_pp_same_thr;
}

void pmixp_server_pp_start(void)
{
	_pmixp_pp_start = GET_TS();
}

bool pmixp_server_pp_check_fini(int size)
{
	if ( (pmixp_server_pp_count() + 1) >=
	     (_pmixp_pp_warmup + _pmixp_pp_iters)){
		slurm_mutex_lock(&_pmixp_pp_lock);
		PMIXP_ERROR("latency: %d - %.9lf", size,
			    (GET_TS() - _pmixp_pp_start) / _pmixp_pp_iters );
		slurm_mutex_unlock(&_pmixp_pp_lock);
		return true;
	}
	return false;
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
	int tmp_int;

	slurm_mutex_init(&_pmixp_pp_lock);

	/* check if we want to run ping-pong */
	if (!(env_ptr = getenvp(*env, PMIXP_PP_ON))) {
		return;
	}
	if (!xstrcmp("1", env_ptr) || !xstrcmp("true", env_ptr)) {
		_pmixp_pp_on = true;
	}

	if ((env_ptr = getenvp(*env, PMIXP_PP_SAMETHR))) {
		if (!xstrcmp("1", env_ptr) || !xstrcmp("true", env_ptr)) {
			_pmixp_pp_same_thr = true;
		}
	}

	if ((env_ptr = getenvp(*env, PMIXP_PP_LOW))) {
		if (_consists_from_digits(env_ptr)) {
			tmp_int = atoi(env_ptr);
			_pmixp_pp_low = tmp_int < PMIXP_PP_PWR2_MAX ?
				tmp_int : PMIXP_PP_PWR2_MAX;
		}
	}

	if ((env_ptr = getenvp(*env, PMIXP_PP_UP))) {
		if (_consists_from_digits(env_ptr)) {
			tmp_int = atoi(env_ptr);
			_pmixp_pp_up = tmp_int < PMIXP_PP_PWR2_MAX ?
				tmp_int : PMIXP_PP_PWR2_MAX;
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

bool pmixp_server_want_pp(void)
{
	return _pmixp_pp_on;
}

/*
 * For this to work the following conditions supposed to be
 * satisfied:
 * - Slurm has to be configured with `--enable-debug` option
 * - jobstep needs to have at least two nodes
 * In this case communication exchange will be done between
 * the first two nodes.
 */
void pmixp_server_run_pp(void)
{
	int i;
	size_t start, end, bound;
	/* ping is initiated by the nodeid == 0
	 * all the rest - just exit
	 */
	if (pmixp_info_nodeid()) {
		return;
	}


	start = 1 << _pmixp_pp_low;
	end = 1 << _pmixp_pp_up;
	bound = 1 << _pmixp_pp_bound;

	for (i = start; i <= end; i *= 2) {
		int count, iters = _pmixp_pp_siter;
		struct timeval tv1, tv2;
		double time;
		if (i >= bound) {
			iters = _pmixp_pp_liter;
		}

		if (!_pmixp_pp_same_thr) {
			/* warmup - 10% of iters # */
			count = pmixp_server_pp_count() + iters/10;
			while (pmixp_server_pp_count() < count) {
				int cur_count = pmixp_server_pp_count();
				pmixp_server_pp_send(1, i);
				while (cur_count == pmixp_server_pp_count()) {
					usleep(1);
				}
			}

			count = pmixp_server_pp_count() + iters;
			gettimeofday(&tv1, NULL);
			while (pmixp_server_pp_count() < count) {
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
			PMIXP_ERROR("latency: %d - %.9lf", i, time / iters );
		} else {
			int count = iters + iters/10;

			slurm_mutex_lock(&_pmixp_pp_lock);
			_pmixp_pp_warmup = iters/10;
			_pmixp_pp_iters = iters;
			_pmixp_pp_count = 0;
			slurm_mutex_unlock(&_pmixp_pp_lock);
			/* initiate sends */
			pmixp_server_pp_send(1, i);
			while (pmixp_server_pp_count() < count){
				sched_yield();
			}
		}
	}
}


struct pp_cbdata
{
	Buf buf;
	double start;
	int size;
};

void pingpong_complete(int rc, pmixp_p2p_ctx_t ctx, void *data)
{
	struct pp_cbdata *d = (struct pp_cbdata*)data;
	FREE_NULL_BUFFER(d->buf);
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
				  _pmixp_pp_count, buf, pingpong_complete,
				  (void*)cbdata);
	if (SLURM_SUCCESS != rc) {
		char *nodename = pmixp_info_job_host(nodeid);
		PMIXP_ERROR("Was unable to wait for the parent %s to "
			    "become alive",
			    nodename);
		xfree(nodename);
	}
	return rc;
}



static pthread_mutex_t _pmixp_pp_lock;

#define PMIXP_CPERF_PWR2_MIN 0
#define PMIXP_CPERF_PWR2_MAX 20

static bool _pmixp_cperf_on = false;
static int _pmixp_cperf_low = PMIXP_CPERF_PWR2_MIN;
static int _pmixp_cperf_up = PMIXP_CPERF_PWR2_MAX;
static int _pmixp_cperf_bound = 10;
static int _pmixp_cperf_siter = 1000;
static int _pmixp_cperf_liter = 100;

static volatile int _pmixp_cperf_count = 0;

static int _pmixp_server_cperf_count()
{
	return _pmixp_cperf_count;
}

static void _pmixp_server_cperf_inc()
{
	_pmixp_cperf_count++;
}

void pmixp_server_init_cperf(char ***env)
{
	char *env_ptr = NULL;
	int tmp_int;

	slurm_mutex_init(&_pmixp_pp_lock);

	/* check if we want to run ping-pong */
	if (!(env_ptr = getenvp(*env, PMIXP_CPERF_ON))) {
		return;
	}
	if (!strcmp("1", env_ptr) || !strcmp("true", env_ptr)) {
		_pmixp_cperf_on = true;
	}

	if ((env_ptr = getenvp(*env, PMIXP_CPERF_LOW))) {
		if (_consists_from_digits(env_ptr)) {
			tmp_int = atoi(env_ptr);
			_pmixp_cperf_low = tmp_int < PMIXP_CPERF_PWR2_MAX ?
				tmp_int : PMIXP_CPERF_PWR2_MAX;
		}
	}

	if ((env_ptr = getenvp(*env, PMIXP_CPERF_UP))) {
		if (_consists_from_digits(env_ptr)) {
			tmp_int = atoi(env_ptr);
			_pmixp_cperf_up = tmp_int < PMIXP_CPERF_PWR2_MAX ?
				tmp_int : PMIXP_CPERF_PWR2_MAX;
		}
	}

	if ((env_ptr = getenvp(*env, PMIXP_CPERF_SITER))) {
		if (_consists_from_digits(env_ptr)) {
			_pmixp_cperf_siter = atoi(env_ptr);
		}
	}

	if ((env_ptr = getenvp(*env, PMIXP_CPERF_LITER))) {
		if (_consists_from_digits(env_ptr)) {
			_pmixp_cperf_liter = atoi(env_ptr);
		}
	}

	if ((env_ptr = getenvp(*env, PMIXP_CPERF_BOUND))) {
		if (_consists_from_digits(env_ptr)) {
			_pmixp_cperf_bound = atoi(env_ptr);
		}
	}
}

bool pmixp_server_want_cperf(void)
{
	return _pmixp_cperf_on;
}

inline static void _pmixp_cperf_cbfunc(pmixp_coll_t *coll,
				       void *r_fn, void *r_cbdata)
{
	/*
	 * we will be called with mutex locked.
	 * need to unlock it so that callback won't
	 * deadlock
	 */
	slurm_mutex_unlock(&coll->lock);

	/* invoke the callbak */
	pmixp_lib_release_invoke(r_fn, r_cbdata);

	/* lock it back before proceed */
	slurm_mutex_lock(&coll->lock);

	/* go to the next iteration */
	_pmixp_server_cperf_inc();
}

static void _pmixp_cperf_tree_cbfunc(int status, const char *data,
				     size_t ndata, void *cbdata,
				     void *r_fn, void *r_cbdata)
{
	/* small violation - we kinow what is the type of release
	 * data and will use that knowledge to avoid the deadlock
	 */
	pmixp_coll_t *coll = pmixp_coll_tree_from_cbdata(r_cbdata);
	xassert(SLURM_SUCCESS == status);
	_pmixp_cperf_cbfunc(coll, r_fn, r_cbdata);
}

static void _pmixp_cperf_ring_cbfunc(int status, const char *data,
				     size_t ndata, void *cbdata,
				     void *r_fn, void *r_cbdata)
{
	/* small violation - we kinow what is the type of release
	 * data and will use that knowledge to avoid the deadlock
	 */
	pmixp_coll_t *coll = pmixp_coll_ring_from_cbdata(r_cbdata);
	xassert(SLURM_SUCCESS == status);
	_pmixp_cperf_cbfunc(coll, r_fn, r_cbdata);
}

typedef void (*pmixp_cperf_cbfunc_fn_t)(int status, const char *data,
					size_t ndata, void *cbdata,
					void *r_fn, void *r_cbdata);

static int _pmixp_server_cperf_iter(pmixp_coll_type_t type, char *data, int ndata)
{
	pmixp_coll_t *coll;
	pmixp_proc_t procs;
	int cur_count = _pmixp_server_cperf_count();
	pmixp_cperf_cbfunc_fn_t cperf_cbfunc = _pmixp_cperf_tree_cbfunc;

	strncpy(procs.nspace, pmixp_info_namespace(), PMIXP_MAX_NSLEN);
	procs.rank = pmixp_lib_get_wildcard();

	switch (type) {
	case PMIXP_COLL_TYPE_FENCE_RING:
		cperf_cbfunc = _pmixp_cperf_ring_cbfunc;
		break;
	case PMIXP_COLL_TYPE_FENCE_TREE:
		cperf_cbfunc = _pmixp_cperf_tree_cbfunc;
		break;
	default:
		PMIXP_ERROR("Uncnown coll type");
		return SLURM_ERROR;
	}
	coll = pmixp_state_coll_get(type, &procs, 1);
	pmixp_coll_sanity_check(coll);
	xassert(!pmixp_coll_contrib_local(coll, type, data, ndata,
					  cperf_cbfunc, NULL));

	while (cur_count == _pmixp_server_cperf_count()) {
		usleep(1);
	}
	return SLURM_SUCCESS;
}

/*
 * For this to work the following conditions supposed to be
 * satisfied:
 * - Slurm has to be configured with `--enable-debug` option
 * - jobstep needs to have at least two nodes
 * In this case communication exchange will be done between
 * the first two nodes.
 */
void pmixp_server_run_cperf(void)
{
	int size;
	size_t start, end, bound;
	pmixp_coll_type_t type;
	pmixp_coll_type_t types[] = { PMIXP_COLL_TYPE_FENCE_TREE, PMIXP_COLL_TYPE_FENCE_RING };
	pmixp_coll_cperf_mode_t mode = pmixp_info_srv_fence_coll_type();
	bool is_barrier = pmixp_info_srv_fence_coll_barrier();

	int rc = SLURM_SUCCESS;

	pmixp_debug_hang(0);

	if (!is_barrier) {
		start = 1 << _pmixp_cperf_low;
		end = 1 << _pmixp_cperf_up;
		bound = 1 << _pmixp_cperf_bound;
	} else {
		start = 0;
		end = 1;
		bound = 1;
	}

	PMIXP_ERROR("coll perf mode=%s", pmixp_coll_cperf_mode2str(mode));
	for (size = start; size <= end; size *= 2) {
		int j, iters = _pmixp_cperf_siter;
		struct timeval tv1, tv2;
		if (size >= bound) {
			iters = _pmixp_cperf_liter;
		}
		double times[iters];
		char *data = xmalloc(size);

		PMIXP_ERROR("coll perf %d", size);

		bzero(times, (sizeof(double) * iters));
		for(j=0; j<iters && !rc; j++){
			switch (mode) {
			case PMIXP_COLL_CPERF_MIXED:
				type = types[j%(sizeof(types)/sizeof(types[0]))];
				break;
			case PMIXP_COLL_CPERF_RING:
				type = PMIXP_COLL_TYPE_FENCE_RING;
				break;
			case PMIXP_COLL_CPERF_TREE:
				type = PMIXP_COLL_TYPE_FENCE_TREE;
				break;
			default:
				type = PMIXP_COLL_TYPE_FENCE_RING;
				break;
			}
			gettimeofday(&tv1, NULL);
			rc = _pmixp_server_cperf_iter(type, data, size);
			gettimeofday(&tv2, NULL);
			times[j] = tv2.tv_sec + 1E-6 * tv2.tv_usec -
					(tv1.tv_sec + 1E-6 * tv1.tv_usec);
		}

		for(j=0; j<iters; j++){
			/* Output measurements to the slurmd.log */
			PMIXP_ERROR("\t%d %d: %.9lf", j, size, times[j]);
		}
		xfree(data);
		if (is_barrier) {
			break;
		}
	}
}

#endif // NDEBUG
