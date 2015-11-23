/*****************************************************************************\
 **  pmix_server.c - PMIx server side functionality
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

#define PMIX_SERVER_MSG_MAGIC 0xCAFECA11
typedef struct {
	uint32_t magic;
	uint32_t type;
	uint32_t seq;
	uint32_t nodeid;
	uint32_t msgsize;
} send_header_t;

#define SEND_HDR_SIZE (5 * sizeof(uint32_t))

typedef struct {
	uint32_t size;		/* Has to be first (appended by SLURM API) */
	send_header_t send_hdr;
} recv_header_t;
#define RCVD_HDR_SIZE (sizeof(uint32_t) + SEND_HDR_SIZE)

Buf pmixp_server_new_buf(void)
{
	Buf buf = create_buf(xmalloc(SEND_HDR_SIZE), SEND_HDR_SIZE);
	/* Skip header. It will be filled right before the sending */
	set_buf_offset(buf, SEND_HDR_SIZE);
	return buf;
}

static uint32_t _recv_payload_size(void *buf);
static int _send_pack_hdr(void *host, void *net);
static int _recv_unpack_hdr(void *net, void *host);

static bool _serv_readable(eio_obj_t *obj);
static int _serv_read(eio_obj_t *obj, List objs);
static void _process_server_request(recv_header_t *_hdr, void *payload);

static struct io_operations peer_ops = {
	.readable = _serv_readable,
	.handle_read = _serv_read
};

pmixp_io_engine_header_t srv_rcvd_header = {
	.host_size = sizeof(recv_header_t),
	.net_size = RCVD_HDR_SIZE,
	.pack_hdr_cb = NULL,
	.unpack_hdr_cb = _recv_unpack_hdr,
	.pay_size_cb = _recv_payload_size
};

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
 * TODO: we need to keep track of the "me"
 * structures created here, because we need to
 * free them in "pmixp_stepd_finalize"
 */
void pmix_server_new_conn(int fd)
{
	eio_obj_t *obj;
	PMIXP_DEBUG("Request from fd = %d", fd);

	/* Set nonblocking */
	fd_set_nonblocking(fd);
	fd_set_close_on_exec(fd);

	pmixp_io_engine_t *me = xmalloc(sizeof(pmixp_io_engine_t));
	pmix_io_init(me, fd, srv_rcvd_header);
	/* We use slurm_forward_data to send message to stepd's
	 * SLURM will put user ID there. We need to skip it.
	 */
	pmix_io_rcvd_padding(me, sizeof(uint32_t));

	/* TODO: in future try to process the request right here
	 * use eio only in case of blocking operation
	 * NOW: always defer to debug the blocking case
	 */
	obj = eio_obj_create(fd, &peer_ops, (void *)me);
	eio_new_obj(pmixp_info_io(), obj);
}

/*
 *  Server message processing
 */

static uint32_t _recv_payload_size(void *buf)
{
	recv_header_t *ptr = (recv_header_t *)buf;
	send_header_t *hdr = &ptr->send_hdr;
	xassert(ptr->size == hdr->msgsize + SEND_HDR_SIZE);
	xassert(hdr->magic == PMIX_SERVER_MSG_MAGIC);
	return hdr->msgsize;
}

/*
 * Pack message header.
 * Returns packed size
 * Note: asymmetric to _recv_unpack_hdr because of additional SLURM header
 */
static int _send_pack_hdr(void *host, void *net)
{
	send_header_t *ptr = (send_header_t *)host;
	Buf packbuf = create_buf(net, sizeof(send_header_t));
	int size = 0;
	pack32(ptr->magic, packbuf);
	pack32(ptr->type, packbuf);
	pack32(ptr->seq, packbuf);
	pack32(ptr->nodeid, packbuf);
	pack32(ptr->msgsize, packbuf);
	size = get_buf_offset(packbuf);
	xassert(size == SEND_HDR_SIZE);
	/* free the Buf packbuf, but not the memory to which it points */
	packbuf->head = NULL;
	free_buf(packbuf);
	return size;
}

/*
 * Unpack message header.
 * Returns 0 on success and -errno on failure
 * Note: asymmetric to _send_pack_hdr because of additional SLURM header
 */
static int _recv_unpack_hdr(void *net, void *host)
{
	recv_header_t *ptr = (recv_header_t *)host;
	Buf packbuf = create_buf(net, sizeof(recv_header_t));
	if (unpack32(&ptr->size, packbuf)) {
		return -EINVAL;
	}
	if (unpack32(&ptr->send_hdr.magic, packbuf)) {
		return -EINVAL;
	}
	xassert(ptr->send_hdr.magic == PMIX_SERVER_MSG_MAGIC);

	if (unpack32(&ptr->send_hdr.type, packbuf)) {
		return -EINVAL;
	}

	if (unpack32(&ptr->send_hdr.seq, packbuf)) {
		return -EINVAL;
	}

	if (unpack32(&ptr->send_hdr.nodeid, packbuf)) {
		return -EINVAL;
	}

	if (unpack32(&ptr->send_hdr.msgsize, packbuf)) {
		return -EINVAL;
	}
	/* free the Buf packbuf, but not the memory to which it points */
	packbuf->head = NULL;
	free_buf(packbuf);
	return 0;
}

int pmixp_server_send(char *hostlist, pmixp_srv_cmd_t type, uint32_t seq,
		const char *addr, void *data, size_t size)
{
	send_header_t hdr;
	char nhdr[sizeof(send_header_t)];
	size_t hsize;
	int rc;

	hdr.magic = PMIX_SERVER_MSG_MAGIC;
	hdr.type = type;
	hdr.msgsize = size - SEND_HDR_SIZE;
	hdr.seq = seq;
	/* Store global nodeid that is
	 *  independent from exact collective */
	hdr.nodeid = pmixp_info_nodeid_job();
	hsize = _send_pack_hdr(&hdr, nhdr);
	memcpy(data, nhdr, hsize);

	rc = pmixp_stepd_send(hostlist, addr, data, size);
	if (SLURM_SUCCESS != rc) {
		PMIXP_ERROR(
				"Cannot send message to %s, size = %u, hostlist:\n%s",
				addr, (uint32_t) size, hostlist);
	}
	return rc;
}

static bool _serv_readable(eio_obj_t *obj)
{
	/* We should delete connection right when it  was closed or failed */
	xassert(obj->shutdown == false);
	return true;
}

static void _process_server_request(recv_header_t *_hdr, void *payload)
{
	send_header_t *hdr = &_hdr->send_hdr;
	char *nodename = pmixp_info_job_host(hdr->nodeid);
	Buf buf;
	int rc;

	buf = create_buf(payload, hdr->msgsize);

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
			return;
		}
		coll = pmixp_state_coll_get(type, procs, nprocs);
		xfree(procs);

		PMIXP_DEBUG(
				"FENCE collective message from node \"%s\", type = %s",
				nodename,
				(PMIXP_MSG_FAN_IN == hdr->type) ?
						"fan-in" : "fan-out");

		if (SLURM_SUCCESS
				!= pmixp_coll_check_seq(coll, hdr->seq,
						nodename)) {
			/* stop processing discardig this message */
			break;
		}

		if (PMIXP_MSG_FAN_IN == hdr->type) {
			pmixp_coll_contrib_node(coll, nodename, buf);
			/* we don't need this buffer anymore */
			free_buf(buf);
		} else {
			pmixp_coll_bcast(coll, buf);
			/* buf will be free'd by the PMIx callback */
		}

		break;
	}
	case PMIXP_MSG_DMDX: {
		pmixp_dmdx_process(buf, nodename, hdr->seq);
		break;
	}
	default:
		PMIXP_ERROR("Unknown message type %d", hdr->type);
		break;
	}
	xfree(nodename);
}

static int _serv_read(eio_obj_t *obj, List objs)
{

	PMIXP_DEBUG("fd = %d", obj->fd);
	pmixp_io_engine_t *me = (pmixp_io_engine_t *)obj->arg;

	pmixp_debug_hang(0);

	/* Read and process all received messages */
	while (1) {
		pmix_io_rcvd(me);
		if (pmix_io_finalized(me)) {
			obj->shutdown = true;
			PMIXP_DEBUG("Connection finalized fd = %d", obj->fd);
			eio_remove_obj(obj, objs);
			return 0;
		}
		if (pmix_io_rcvd_ready(me)) {
			recv_header_t hdr;
			void *msg = pmix_io_rcvd_extract(me, &hdr);
			_process_server_request(&hdr, msg);
		} else {
			/* No more complete messages */
			break;
		}
	}
	return 0;
}
