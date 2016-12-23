/*****************************************************************************\
 **  pmix_server.c - PMIx server side functionality
 *****************************************************************************
 *  Copyright (C) 2014-2015 Artem Polyakov. All rights reserved.
 *  Copyright (C) 2015-2016 Mellanox Technologies. All rights reserved.
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

#include <pmix_server.h>

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
		return rc;
	}

	/* Create UNIX socket for slurmd communication */
	path = pmixp_info_nspace_usock(pmixp_info_namespace());
	if (NULL == path) {
		PMIXP_ERROR("Out-of-memory");
		rc = SLURM_ERROR;
		goto err_path;
	}
	if ((fd = pmixp_usock_create_srv(path)) < 0) {
		rc = SLURM_ERROR;
		goto err_usock;
	}
	fd_set_close_on_exec(fd);
	pmixp_info_srv_contacts(path, fd);

	if (SLURM_SUCCESS != (rc = pmixp_nspaces_init())) {
		PMIXP_ERROR("pmixp_nspaces_init() failed");
		goto err_usock;
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
err_usock:
	xfree(path);
err_path:
	pmixp_info_free();
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
	pmixp_state_finalize();
	pmixp_nspaces_finalize();

	/* cleanup the usock */
	PMIXP_DEBUG("Remove PMIx plugin usock");
	close(pmixp_info_srv_fd());
	path = pmixp_info_nspace_usock(pmixp_info_namespace());
	unlink(path);
	xfree(path);

	/* free the information */
	pmixp_info_free();
	return SLURM_SUCCESS;
}

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

#define PMIX_BASE_HDR_SIZE (5 * sizeof(uint32_t))

typedef struct {
	pmixp_base_hdr_t base_hdr;
	uint16_t rport;		/* STUB: remote port for persistent connection */
} pmixp_slurm_api_shdr_t;
#define PMIXP_SLURM_API_SHDR_SIZE (PMIX_BASE_HDR_SIZE + sizeof(uint16_t))

#define PMIXP_MAX_SEND_HDR PMIXP_SLURM_API_SHDR_SIZE

typedef struct {
	uint32_t size;		/* Has to be first (appended by SLURM API) */
	pmixp_slurm_api_shdr_t sapi_shdr;
} pmixp_slurm_api_rhdr_t;
#define PMIXP_SLURM_API_RHDR_SIZE (sizeof(uint32_t) + PMIXP_SLURM_API_SHDR_SIZE)

Buf pmixp_server_new_buf(void)
{
	Buf buf = create_buf(xmalloc(PMIXP_MAX_SEND_HDR), PMIXP_MAX_SEND_HDR);
	/* Skip header. It will be filled right before the sending */
	set_buf_offset(buf, PMIXP_MAX_SEND_HDR);
	return buf;
}

/*
 * --------------------- Generic I/O functionality -------------------
 */

/* this routine tries to complete message processing on message
 * engine (me). Return value:
 * - 0: no progress was observed on the descriptor
 * - 1: one more message was successfuly processed
 * - 2: all messages are completed
 */
typedef int (*pmixp_msg_callback_t)(pmixp_io_engine_t *eng);

typedef struct {
	pmixp_io_engine_t *eng;
	pmixp_msg_callback_t process;
} pmixp_msg_handler_t;

static bool _serv_readable(eio_obj_t *obj);
static int _serv_read(eio_obj_t *obj, List objs);
static void _process_server_request(pmixp_base_hdr_t *hdr, void *payload);


static struct io_operations peer_ops = {
	.readable = _serv_readable,
	.handle_read = _serv_read
};

static bool _serv_readable(eio_obj_t *obj)
{
	/* We should delete connection right when it  was closed or failed */
	xassert(obj->shutdown == false);
	return true;
}

static int _serv_read(eio_obj_t *obj, List objs)
{
	PMIXP_DEBUG("fd = %d", obj->fd);
	pmixp_msg_handler_t *mhndl = (pmixp_msg_handler_t *)obj->arg;
	pmixp_io_engine_t *eng = mhndl->eng;
	bool proceed = true;

	pmixp_debug_hang(0);

	/* Read and process all received messages */
	while (proceed) {
		switch( mhndl->process(eng) ){
		case 2:
			obj->shutdown = true;
			PMIXP_DEBUG("Connection finalized fd = %d", obj->fd);
			/* cleanup after this connection */
			eio_remove_obj(obj, objs);
			xfree(eng);
		case 0:
			proceed = 0;
		case 1:
			break;
		}
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
	case PMIXP_MSG_HEALTH_CHK: {
		/* this is just health ping.
		 * TODO: can we do something more sophisticated?
		 */
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


/*
 * ------------------- SLURM API communication -----------------------
 */

static uint32_t _slurm_proto_msize(void *buf);
static int _slurm_proto_pack_hdr(pmixp_slurm_api_shdr_t *shdr,
				    void *net);
static int _slurm_proto_unpack_hdr(void *net, void *host);
static int _slurm_proto_new_msg(pmixp_io_engine_t *me);

pmixp_io_engine_header_t srv_rcvd_header = {
	.host_size = sizeof(pmixp_slurm_api_rhdr_t),
	.net_size = PMIXP_SLURM_API_RHDR_SIZE,
	.pack_hdr_cb = NULL,
	.unpack_hdr_cb = _slurm_proto_unpack_hdr,
	.pay_size_cb = _slurm_proto_msize
};

/*
 * See process_handler_t prototype description
 * on the details of this function output values
 */
static int _slurm_proto_new_msg(pmixp_io_engine_t *me)
{
	int ret = 0;
	pmix_io_rcvd(me);
	if (pmix_io_rcvd_ready(me)) {
		pmixp_slurm_api_rhdr_t hdr;
		void *msg = pmix_io_rcvd_extract(me, &hdr);
		_process_server_request(&hdr.sapi_shdr.base_hdr, msg);
		ret = 1;
	}
	if (pmix_io_finalized(me)) {
		ret = 2;
	}
	return ret;
}


/*
 * TODO: we need to keep track of the "me"
 * structures created here, because we need to
 * free them in "pmixp_stepd_finalize"
 */
void pmixp_server_slurm_conn(int fd)
{
	eio_obj_t *obj;
	PMIXP_DEBUG("Request from fd = %d", fd);

	/* Set nonblocking */
	fd_set_nonblocking(fd);
	fd_set_close_on_exec(fd);

	pmixp_io_engine_t *eng = xmalloc(sizeof(pmixp_io_engine_t));
	pmix_io_init(eng, fd, srv_rcvd_header);
	/* We use slurm_forward_data to send message to stepd's
	 * SLURM will put user ID there. We need to skip it.
	 */
	pmix_io_rcvd_padding(eng, sizeof(uint32_t));

	if( 2 == _slurm_proto_new_msg(eng) ){
		/* connection was fully processed here */
		xfree(eng);
		return;
	}

	/* If it is a blocking operation: create AIO object to
	 * handle it */
	pmixp_msg_handler_t *hndl = xmalloc( sizeof(pmixp_msg_handler_t));
	hndl->eng = eng;
	hndl->process = _slurm_proto_new_msg;
	obj = eio_obj_create(fd, &peer_ops, (void *)hndl);
	eio_new_obj(pmixp_info_io(), obj);
}

/*
 *  Server message processing
 */

static uint32_t _slurm_proto_msize(void *buf)
{
	pmixp_slurm_api_rhdr_t *ptr = (pmixp_slurm_api_rhdr_t *)buf;
	pmixp_base_hdr_t *hdr = &ptr->sapi_shdr.base_hdr;
	xassert(ptr->size == hdr->msgsize + PMIXP_SLURM_API_SHDR_SIZE);
	xassert(hdr->magic == PMIX_SERVER_MSG_MAGIC);
	return hdr->msgsize;
}

/*
 * Pack message header.
 * Returns packed size
 * Note: asymmetric to _recv_unpack_hdr because of additional SLURM header
 */
static int _slurm_proto_pack_hdr(pmixp_slurm_api_shdr_t *shdr, void *net)
{
	pmixp_base_hdr_t *bhdr = &shdr->base_hdr;
	Buf packbuf = create_buf(net, sizeof(pmixp_slurm_api_shdr_t));
	int size = 0;
	pack32(bhdr->magic, packbuf);
	pack32(bhdr->type, packbuf);
	pack32(bhdr->seq, packbuf);
	pack32(bhdr->nodeid, packbuf);
	pack32(bhdr->msgsize, packbuf);
	pack16(shdr->rport, packbuf);
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
	pmixp_slurm_api_rhdr_t *rhdr = (pmixp_slurm_api_rhdr_t *)host;
	pmixp_slurm_api_shdr_t *shdr = &rhdr->sapi_shdr;
	pmixp_base_hdr_t *bhdr = &shdr->base_hdr;
	Buf packbuf = create_buf(net, sizeof(pmixp_slurm_api_rhdr_t));
	if (unpack32(&rhdr->size, packbuf)) {
		return -EINVAL;
	}
	if (unpack32(&bhdr->magic, packbuf)) {
		return -EINVAL;
	}
	xassert(PMIX_SERVER_MSG_MAGIC == bhdr->magic);

	if (unpack32(&bhdr->type, packbuf)) {
		return -EINVAL;
	}

	if (unpack32(&bhdr->seq, packbuf)) {
		return -EINVAL;
	}

	if (unpack32(&bhdr->nodeid, packbuf)) {
		return -EINVAL;
	}

	if (unpack32(&bhdr->msgsize, packbuf)) {
		return -EINVAL;
	}

	if (unpack16(&shdr->rport, packbuf)) {
		return -EINVAL;
	}
	/* Temp: we don't use rport now, but will in the future.
	 * Just check that it is = 100 for now
	 */
	xassert(100 == shdr->rport);

	/* free the Buf packbuf, but not the memory it points to */
	packbuf->head = NULL;
	free_buf(packbuf);
	return 0;
}

int pmixp_server_send(char *hostlist, pmixp_srv_cmd_t type, uint32_t seq,
		const char *addr, void *data, size_t size, int p2p)
{
	pmixp_slurm_api_shdr_t hdr;
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

	hsize = _slurm_proto_pack_hdr(&hdr, nhdr);
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

int pmixp_server_health_chk(char *hostlist,  const char *addr)
{
	pmixp_slurm_api_shdr_t hdr;
	pmixp_base_hdr_t *bhdr = &hdr.base_hdr;
	char nhdr[sizeof(hdr)];
	size_t hsize;
	Buf buf = pmixp_server_new_buf();
	char *data = get_buf_data(buf);
	int rc;

	bhdr->magic = PMIX_SERVER_MSG_MAGIC;
	bhdr->type = PMIXP_MSG_HEALTH_CHK;
	bhdr->msgsize = 1;
	bhdr->seq = 0;
	/* Store global nodeid that is
	 *  independent from exact collective */
	bhdr->nodeid = pmixp_info_nodeid_job();

	/* Temp: for the verification purposes */
	hdr.rport = 100;

	hsize = pmixp_slurm_api_pack_hdr(&hdr, nhdr);
	memcpy(data, nhdr, hsize);

	grow_buf(buf, sizeof(char));
	pack8('\n', buf);

	rc = pmixp_stepd_send(hostlist, addr, data, get_buf_offset(buf), 4, 14, 1);
	if (SLURM_SUCCESS != rc) {
		PMIXP_ERROR("Was unable to wait for the parent %s to become alive on addr %s",
			    hostlist, addr);
	}

	return rc;
}
