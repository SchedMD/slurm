/*****************************************************************************\
 **  pmix_debug.h - PMIx debug primitives
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

#include "pmixp_common.h"
#include "pmixp_dmdx.h"
#include "pmixp_server.h"
#include "pmixp_client.h"

/* set default direct modex timeout to 10 sec */
#define DMDX_DEFAULT_TIMEOUT 10

typedef enum {
	DMDX_REQUEST = 1,
	DMDX_RESPONSE
} dmdx_type_t;

typedef struct {
	uint32_t seq_num;
	time_t ts;
#ifndef NDEBUG
	/* we need this only for verification */
	char nspace[PMIXP_MAX_NSLEN+1];
	int rank;
#endif
	void *cbfunc;
	void *cbdata;
} dmdx_req_info_t;

typedef struct {
	uint32_t seq_num;
	pmixp_proc_t proc;
	char *sender_ns;
	int sender_nodeid;
	int rank;
} dmdx_caddy_t;

void _dmdx_free_caddy(dmdx_caddy_t *caddy)
{
	if (NULL == caddy) {
		/* nothing to do */
		return;
	}
	if (NULL != caddy->sender_ns) {
		xfree(caddy->sender_ns);
	}
	xfree(caddy);
}

static List _dmdx_requests;
static uint32_t _dmdx_seq_num = 1;

static void _respond_with_error(int seq_num, int nodeid,
				char *sender_ns, int status);

int pmixp_dmdx_init(void)
{
	_dmdx_requests = list_create(pmixp_xfree_xmalloced);
	_dmdx_seq_num = 1;
	return SLURM_SUCCESS;
}

int pmixp_dmdx_finalize(void)
{
	list_destroy(_dmdx_requests);
	return 0;
}


static void _setup_header(Buf buf, dmdx_type_t t,
			  const char *nspace, int rank, int status)
{
	char *str;
	/* 1. pack message type */
	unsigned char type = (char)t;
	grow_buf(buf, sizeof(char));
	pack8(type, buf);

	/* 2. pack namespace _with_ '\0' (strlen(nspace) + 1)! */
	packmem((char *)nspace, strlen(nspace) + 1, buf);

	/* 3. pack rank */
	grow_buf(buf, sizeof(int));
	pack32((uint32_t)rank, buf);

	/* 4. pack my rendezvous point - local namespace
	 * ! _with_ '\0' (strlen(nspace) + 1) ! */
	str = pmixp_info_namespace();
	packmem(str, strlen(str) + 1, buf);

	/* 5. pack the status */
	pack32((uint32_t)status, buf);
}

static int _read_type(Buf buf, dmdx_type_t *type)
{
	unsigned char t;
	int rc;
	/* 1. unpack message type */
	if (SLURM_SUCCESS != (rc = unpack8(&t, buf))) {
		PMIXP_ERROR("Cannot unpack message type!");
		return SLURM_ERROR;
	}
	*type = (dmdx_type_t)t;
	return SLURM_SUCCESS;
}

static int _read_info(Buf buf, char **ns, int *rank,
		      char **sender_ns, int *status)
{
	uint32_t cnt, uint32_tmp;
	int rc;
	*ns = NULL;
	*sender_ns = NULL;

	/* 1. unpack namespace */
	if (SLURM_SUCCESS != (rc = unpackmem_ptr(ns, &cnt, buf))) {
		PMIXP_ERROR("Cannot unpack requested namespace!");
		return rc;
	}
	/* We supposed to unpack a whole null-terminated string (with '\0')!
	 * (*ns)[cnt] = '\0';
	 */

	/* 2. unpack rank */
	if (SLURM_SUCCESS != (rc = unpack32(&uint32_tmp, buf))) {
		PMIXP_ERROR("Cannot unpack requested rank!");
		return rc;
	}
	*rank = uint32_tmp;

	if (SLURM_SUCCESS != (rc = unpackmem_ptr(sender_ns, &cnt, buf))) {
		PMIXP_ERROR("Cannot unpack sender namespace!");
		return rc;
	}
	/* We supposed to unpack a whole null-terminated string (with '\0')!
	 * (*sender_ns)[cnt] = '\0';
	 */

	/* 4. unpack status */
	if (SLURM_SUCCESS != (rc = unpack32(&uint32_tmp, buf))) {
		PMIXP_ERROR("Cannot unpack rank!");
		return rc;
	}
	*status = uint32_tmp;
	return SLURM_SUCCESS;
}

static void _respond_with_error(int seq_num, int nodeid,
				char *sender_ns, int status)
{
	Buf buf = create_buf(NULL, 0);
	pmixp_ep_t ep;
	int rc;

	ep.type = PMIXP_EP_NOIDEID;
	ep.ep.nodeid = nodeid;

	/* rank doesn't matter here, don't send it */
	_setup_header(buf, DMDX_RESPONSE, sender_ns, -1, status);

	/* send response */
	rc = pmixp_server_send_nb(&ep, PMIXP_MSG_DMDX, seq_num, buf,
				  pmixp_server_sent_buf_cb, buf);
	if (SLURM_SUCCESS != rc) {
		char *nodename = pmixp_info_job_host(nodeid);
		PMIXP_ERROR("Cannot send direct modex error response to %s",
			    nodename);
		xfree(nodename);
	}
}

static void _dmdx_pmix_cb(int status, char *data, size_t sz,
			  void *cbdata)
{
	dmdx_caddy_t *caddy = (dmdx_caddy_t *)cbdata;
	Buf buf = pmixp_server_buf_new();
	pmixp_ep_t ep;
	int rc;

	/* setup response header */
	_setup_header(buf, DMDX_RESPONSE, caddy->proc.nspace, caddy->proc.rank,
		      status);

	/* pack the response */
	packmem(data, sz, buf);

	/* send the request */
	ep.type = PMIXP_EP_NOIDEID;
	ep.ep.nodeid = caddy->sender_nodeid;
	rc = pmixp_server_send_nb(&ep, PMIXP_MSG_DMDX, caddy->seq_num, buf,
				  pmixp_server_sent_buf_cb, buf);
	if (SLURM_SUCCESS != rc) {
		char *nodename = pmixp_info_job_host(caddy->sender_nodeid);
		/* not much we can do here. Caller will react by timeout */
		PMIXP_ERROR("Cannot send direct modex response to %s",
			    nodename);
	}
	_dmdx_free_caddy(caddy);
}

int pmixp_dmdx_get(const char *nspace, int rank,
		   void *cbfunc, void *cbdata)
{
	dmdx_req_info_t *req;
	Buf buf;
	int rc;
	uint32_t seq;
	pmixp_ep_t ep;

	/* need to send the request */
	ep.type = PMIXP_EP_NOIDEID;
	ep.ep.nodeid = pmixp_nspace_resolve(nspace, rank);

	buf = pmixp_server_buf_new();
	/* setup message header */
	_setup_header(buf, DMDX_REQUEST, nspace, rank, SLURM_SUCCESS);

	/* store cur seq. num and move to the next request */
	seq = _dmdx_seq_num++;

	/* track this request */
	req = xmalloc(sizeof(dmdx_req_info_t));
	req->seq_num = seq;
	req->cbfunc = cbfunc;
	req->cbdata = cbdata;
	req->ts = time(NULL);
#ifndef NDEBUG
	strncpy(req->nspace, nspace, PMIXP_MAX_NSLEN);
	req->rank = rank;
#endif
	list_append(_dmdx_requests, req);

	/* send the request */
	rc = pmixp_server_send_nb(&ep, PMIXP_MSG_DMDX, seq, buf,
				  pmixp_server_sent_buf_cb, buf);

	/* check the return status */
	if (SLURM_SUCCESS != rc) {
		char *nodename = pmixp_info_job_host(ep.ep.nodeid);
		PMIXP_ERROR("Cannot send direct modex request to %s, size %d",
			    nodename, get_buf_offset(buf));
		xfree(nodename);
		pmixp_lib_modex_invoke(cbfunc, SLURM_ERROR, NULL, 0,
				       cbdata, NULL, NULL);
		rc = SLURM_ERROR;
	}

	return rc;
}

static void _dmdx_req(Buf buf, int nodeid, uint32_t seq_num)
{
	int rank, rc;
	int status;
	char *ns = NULL, *sender_ns = NULL;
	pmixp_namespace_t *nsptr;
	dmdx_caddy_t *caddy = NULL;

	rc = _read_info(buf, &ns, &rank, &sender_ns,&status);
	if (SLURM_SUCCESS != rc) {
		char *nodename = pmixp_info_job_host(nodeid);
		/* there is not much we can do here, but data corruption
		 *  shouldn't happen */
		PMIXP_ERROR("Fail to unpack header data in request from %s, rc = %d",
			    nodename, rc);
		xfree(nodename);
		goto exit;
	}

	if (0 != xstrcmp(ns, pmixp_info_namespace())) {
		/* request for namespase that is not controlled by this daemon
		 * considered as error. This may change in future.  */
		char *nodename = pmixp_info_job_host(nodeid);
		PMIXP_ERROR("Bad request from %s: asked for nspace = %s, mine is %s",
			    nodename, ns, pmixp_info_namespace());
		_respond_with_error(seq_num, nodeid, sender_ns,
				    PMIXP_ERR_INVALID_NAMESPACE);
		xfree(nodename);
		goto exit;
	}

	nsptr = pmixp_nspaces_local();
	if (nsptr->ntasks <= rank) {
		char *nodename = pmixp_info_job_host(nodeid);
		PMIXP_ERROR("Bad request from %s: nspace \"%s\" has only %d ranks, asked for %d",
			    nodename, ns, nsptr->ntasks, rank);
		_respond_with_error(seq_num, nodeid, sender_ns,
				    PMIXP_ERR_BAD_PARAM);
		xfree(nodename);
		goto exit;
	}

	/* setup temp structure to handle information fro _dmdx_pmix_cb */
	caddy = xmalloc(sizeof(dmdx_caddy_t));
	caddy->seq_num = seq_num;

	/* ns is a pointer inside incoming buffer */
	strncpy(caddy->proc.nspace, ns, PMIXP_MAX_NSLEN);
	ns = NULL; /* protect the data */
	caddy->proc.rank = rank;

	/* sender_host was passed from outside - copy it */
	caddy->sender_nodeid = nodeid;

	/* sender_ns is a pointer inside incoming buffer */
	caddy->sender_ns = xstrdup(sender_ns);
	sender_ns = NULL;

	rc = pmixp_lib_dmodex_request(&caddy->proc, (void *)_dmdx_pmix_cb,
				      (void *)caddy);
	if (SLURM_SUCCESS != rc) {
		char *nodename = pmixp_info_job_host(nodeid);
		PMIXP_ERROR("Can't request modex data from libpmix-server, requesting host = %s, nspace = %s, rank = %d, rc = %d",
			    nodename, caddy->proc.nspace,
			    caddy->proc.rank, rc);
		_respond_with_error(seq_num, nodeid, caddy->sender_ns, rc);
		_dmdx_free_caddy(caddy);
		xfree(nodename);
	}
exit:
	/* we don't need this buffer anymore */
	FREE_NULL_BUFFER(buf);

	/* no sense to return errors, engine can't do anything
	 * anyway. We've notified libpmix, that's enough */
}

static int _dmdx_req_cmp(void *x, void *key)
{
	dmdx_req_info_t *req = (dmdx_req_info_t *)x;
	uint32_t seq_num = *((uint32_t *)key);
	return (req->seq_num == seq_num);
}

static void _dmdx_resp(Buf buf, int nodeid, uint32_t seq_num)
{
	dmdx_req_info_t *req;
	int rank, rc = SLURM_SUCCESS;
	int status;
	char *ns = NULL, *sender_ns = NULL;
	char *data = NULL;
	uint32_t size = 0;

	/* find the request tracker */
	ListIterator it = list_iterator_create(_dmdx_requests);
	req = (dmdx_req_info_t *)list_find(it, _dmdx_req_cmp, &seq_num);
	if (NULL == req) {
		char *nodename = pmixp_info_job_host(nodeid);
		/* We haven't sent this request! */
		PMIXP_ERROR("Received DMDX response with bad seq_num=%d from %s!",
			    seq_num, nodename);
		list_iterator_destroy(it);
		rc = SLURM_ERROR;
		xfree(nodename);
		goto exit;
	}

	/* get the service data */
	rc = _read_info(buf, &ns, &rank, &sender_ns, &status);
	if (SLURM_SUCCESS != rc) {
		/* notify libpmix about an error */
		pmixp_lib_modex_invoke(req->cbfunc, SLURM_ERROR, NULL, 0,
				       req->cbdata, NULL, NULL);
		goto exit;
	}

	/* get the modex blob */
	if (SLURM_SUCCESS != (rc = unpackmem_ptr(&data, &size, buf))) {
		/* notify libpmix about an error */
		pmixp_lib_modex_invoke(req->cbfunc, SLURM_ERROR, NULL, 0,
				       req->cbdata, NULL, NULL);
		goto exit;
	}

	/* call back to libpmix-server */
	pmixp_lib_modex_invoke(req->cbfunc, status, data, size,
			       req->cbdata, pmixp_free_buf, (void *)buf);

	/* release tracker & list iterator */
	req = NULL;
	list_delete_item(it);
	list_iterator_destroy(it);
exit:
	if (SLURM_SUCCESS != rc) {
		/* we are not expect libpmix to call the callback
		 * to cleanup this buffer */
		FREE_NULL_BUFFER(buf);
	}
	/* no sense to return errors, engine can't do anything
	 * anyway. We've notified libpmix, that's enough */
}

void pmixp_dmdx_process(Buf buf, int nodeid, uint32_t seq)
{
	dmdx_type_t type = 0;
	_read_type(buf, &type);

	switch (type) {
	case DMDX_REQUEST:
		_dmdx_req(buf, nodeid, seq);
		break;
	case DMDX_RESPONSE:
		_dmdx_resp(buf, nodeid, seq);
		break;
	default:{
		char *nodename = pmixp_info_job_host(nodeid);
		PMIXP_ERROR("Bad request from host %s. Skip", nodename);
		xfree(nodename);
		break;
	}
	}
}

void pmixp_dmdx_timeout_cleanup(void)
{
	ListIterator it = list_iterator_create(_dmdx_requests);
	dmdx_req_info_t *req = NULL;
	time_t ts = time(NULL);

	/* run through all requests and discard stale one's */
	while ((req = list_next(it))) {
		if ((ts - req->ts) > pmixp_info_timeout()) {
#ifndef NDEBUG
			/* respond with the timeout to libpmix */
			int nodeid = pmixp_nspace_resolve(req->nspace,
							  req->rank);
			char *nodename = pmixp_info_job_host(nodeid);
			xassert(NULL != nodename);
			PMIXP_ERROR("timeout: ns=%s, rank=%d, host=%s, ts=%lu",
				    req->nspace, req->rank,
				    (NULL != nodename) ? nodename : "unknown",
				    ts);
			if (NULL != nodename) {
				xfree(nodename);
			}
#endif
			/* PMIX_ERR_TIMEOUT */
			pmixp_lib_modex_invoke(req->cbfunc, SLURM_ERROR, NULL, 0,
					       req->cbdata, NULL, NULL);
			/* release tracker & list iterator */
			list_delete_item(it);
		}
	}
	list_iterator_destroy(it);
}
