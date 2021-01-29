/*****************************************************************************\
 **  pmix_dconn_ucx.c - PMIx direct UCX connection
 *****************************************************************************
 *  Copyright (C) 2017      Mellanox Technologies. All rights reserved.
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

#include "pmixp_dconn.h"
#include "pmixp_dconn_ucx.h"
#include <unistd.h>
#include <dlfcn.h>
#include <ucp/api/ucp.h>

/* local variables */
static int _service_pipe[2];
#define PMIXP_UCX_LIST_PREALLOC 16
static pmixp_list_t _free_list;
static pmixp_rlist_t _rcv_pending, _snd_pending;
static pmixp_rlist_t _rcv_complete, _snd_complete;
static int _server_fd = -1;
static bool _direct_hdr_set = false;
static pmixp_p2p_data_t _direct_hdr;
static void *_host_hdr = NULL;
pthread_mutex_t _ucx_worker_lock;



/* UCX objects */
ucp_context_h ucp_context;
ucp_worker_h ucp_worker;
static ucp_address_t *_ucx_addr;
static size_t _ucx_alen;

typedef enum {
	PMIXP_UCX_ACTIVE = 0,
	PMIXP_UCX_COMPLETE,
	PMIXP_UCX_FAILED
} pmixp_ucx_status_t;

typedef struct {
	volatile pmixp_ucx_status_t status;
	void *buffer;
	size_t len;
	void *msg;
} pmixp_ucx_req_t;

typedef struct {
	int nodeid;
	bool connected;
	ucp_ep_h server_ep;
	void *ucx_addr;
	size_t ucx_alen;
	pmixp_p2p_data_t eng_hdr;
	pmixp_rlist_t pending;
} pmixp_dconn_ucx_t;

static inline void _recv_req_release(pmixp_ucx_req_t *req)
{
	if (req->buffer) {
		xfree(req->buffer);
	}
	memset(req, 0, sizeof(*req));
	ucp_request_release(req);
}

static void request_init(void *request)
{
	pmixp_ucx_req_t *req = (pmixp_ucx_req_t *) request;
	req->status = PMIXP_UCX_ACTIVE;
	memset(req, 0, sizeof(*req));
}

static void send_handle(void *request, ucs_status_t status)
{
	pmixp_ucx_req_t *req = (pmixp_ucx_req_t *) request;
	if (UCS_OK == status){
		req->status = PMIXP_UCX_COMPLETE;
	} else {
		PMIXP_ERROR("UCX send request failed: %s",
			    ucs_status_string(status));
		req->status = PMIXP_UCX_FAILED;
	}
}

static void recv_handle(void *request, ucs_status_t status,
			ucp_tag_recv_info_t *info)
{
	pmixp_ucx_req_t *req = (pmixp_ucx_req_t *) request;
	if (UCS_OK == status){
		req->status = PMIXP_UCX_COMPLETE;
	} else {
		PMIXP_ERROR("UCX send request failed: %s",
			    ucs_status_string(status));
		req->status = PMIXP_UCX_FAILED;
	}
}

static bool _epoll_readable(eio_obj_t *obj);
static int _epoll_read(eio_obj_t *obj, List objs);

static struct io_operations _epoll_ops = {
	.readable = _epoll_readable,
	.handle_read = _epoll_read
};

static bool _progress_readable(eio_obj_t *obj);
static int _progress_read(eio_obj_t *obj, List objs);

static struct io_operations _progress_ops = {
	.readable = _progress_readable,
	.handle_read = _progress_read
};

static void *_ucx_init(int nodeid, pmixp_p2p_data_t direct_hdr);
static void _ucx_fini(void *_priv);
static int _ucx_connect(void *_priv, void *ep_data, size_t ep_len,
			void *init_msg);
static int _ucx_send(void *_priv, void *msg);
static void _ucx_regio(eio_handle_t *h);
static void *_ucx_lib_handler = NULL;

static int _load_ucx_lib()
{
	/* At the time of writing this UCX doesn't support
	 * fork() and it's memory hooks are causing memory
	 * corruptions in the forked processes.
	 * To avoid that we need to disable memory hooks before
	 * loading UCX library.
	 */
#if UCP_API_VERSION < UCP_VERSION(1, 5)
	setenv("UCX_MEM_MMAP_RELOC", "no", 1);
#endif
	setenv("UCX_MEM_MALLOC_HOOKS", "no", 1);
	setenv("UCX_MEM_MALLOC_RELOC", "no", 1);
	setenv("UCX_MEM_EVENTS", "no", 1);

#ifdef PMIXP_UCX_LIBPATH
	/* If this Slurm build doesn't allow RPATH's
	 * try to open library by it's full path that
	 * we have from autoconf
	 */
	char *full_path = NULL;
	xstrfmtcat(full_path, "%s/libucp.so", PMIXP_UCX_LIBPATH);
	_ucx_lib_handler = dlopen(full_path, RTLD_LAZY | RTLD_GLOBAL);
	xfree(full_path);
	if (_ucx_lib_handler) {
		/* successful, exit now */
		return SLURM_SUCCESS;
	}
	/* fall-thru to see if libucp.so is located in the location
	 * known by dynamic linker.
	 */
#endif
	_ucx_lib_handler = dlopen("libucp.so", RTLD_LAZY | RTLD_GLOBAL);
	if (!_ucx_lib_handler) {
		char *err = dlerror();
		PMIXP_ERROR("Cannot open UCX lib: %s", (err) ? err : "unknown");
		return SLURM_ERROR;
	}
	return SLURM_SUCCESS;
}

static void _unload_ucx_lib()
{
	xassert(_ucx_lib_handler);
	if (_ucx_lib_handler) {
		dlclose(_ucx_lib_handler);
	}
}

int pmixp_dconn_ucx_prepare(pmixp_dconn_handlers_t *handlers,
			    char **ep_data, size_t *ep_len)
{
	ucp_config_t *config;
	ucs_status_t status;
	ucp_params_t ucp_params;
	ucp_worker_params_t worker_params;

	/* By default UCX is not loaded until we explicitly
	 * asked for that
	 */
	_load_ucx_lib();

	slurm_mutex_init(&_ucx_worker_lock);

	/* container of the free elements */
	pmixp_list_init(&_free_list);

	/* Containers for the non-completed requests */
	pmixp_rlist_init(&_rcv_pending, &_free_list, PMIXP_UCX_LIST_PREALLOC);
	pmixp_rlist_init(&_snd_pending, &_free_list, PMIXP_UCX_LIST_PREALLOC);

	/* Temp lists to hold completed requests for _ucx_progress */
	pmixp_rlist_init(&_snd_complete, &_free_list, PMIXP_UCX_LIST_PREALLOC);
	pmixp_rlist_init(&_rcv_complete, &_free_list, PMIXP_UCX_LIST_PREALLOC);


	status = ucp_config_read("SLURM", NULL, &config);
	if (status != UCS_OK) {
		PMIXP_ERROR("Fail to read UCX config: %s",
			    ucs_status_string(status));
		return SLURM_ERROR;
	}

	ucp_params.features = UCP_FEATURE_TAG | UCP_FEATURE_WAKEUP;
	ucp_params.request_size    = sizeof(pmixp_ucx_req_t);
	ucp_params.request_init    = request_init;
	ucp_params.request_cleanup = NULL;
	ucp_params.field_mask      = UCP_PARAM_FIELD_FEATURES |
			UCP_PARAM_FIELD_REQUEST_SIZE |
			UCP_PARAM_FIELD_REQUEST_INIT |
			UCP_PARAM_FIELD_REQUEST_CLEANUP;

	status = ucp_init(&ucp_params, config, &ucp_context);

	ucp_config_release(config);
	if (status != UCS_OK) {
		PMIXP_ERROR("Fail to init UCX: %s",
			    ucs_status_string(status));
		return SLURM_ERROR;
	}

	worker_params.field_mask  = UCP_WORKER_PARAM_FIELD_THREAD_MODE;
	worker_params.thread_mode = UCS_THREAD_MODE_MULTI;

	status = ucp_worker_create(ucp_context, &worker_params, &ucp_worker);
	if (status != UCS_OK) {
		PMIXP_ERROR("Fail to create UCX worker: %s",
			    ucs_status_string(status));
		goto err_worker;
	}

	status = ucp_worker_get_address(ucp_worker, &_ucx_addr, &_ucx_alen);
	if (status != UCS_OK) {
		PMIXP_ERROR("Fail to get UCX address: %s",
			    ucs_status_string(status));
		goto err_addr;
	}

	status = ucp_worker_get_efd(ucp_worker, &_server_fd);
	if (status != UCS_OK) {
		PMIXP_ERROR("Fail to get UCX epoll fd: %s",
			    ucs_status_string(status));
		goto err_efd;
	}

	if (pipe(_service_pipe) != 0) {
		PMIXP_ERROR("Failed to open service pipe: %m");
		goto err_pipe;
	}

	memset(handlers, 0, sizeof(*handlers));
	handlers->connect = _ucx_connect;
	handlers->init = _ucx_init;
	handlers->fini = _ucx_fini;
	handlers->send = _ucx_send;
	handlers->getio = NULL;
	handlers->regio = _ucx_regio;

	*ep_data = (void*)_ucx_addr;
	*ep_len  = (uint16_t)_ucx_alen;

	return SLURM_SUCCESS;

err_pipe:
err_efd:
	ucp_worker_release_address(ucp_worker, _ucx_addr);
err_addr:
	ucp_worker_destroy(ucp_worker);
err_worker:
	ucp_cleanup(ucp_context);
	return SLURM_ERROR;

}

static void _release_send_requests(pmixp_rlist_t *l)
{
	size_t count = pmixp_rlist_count(l);
	size_t i;
	for (i=0; i<count; i++) {
		pmixp_ucx_req_t *req;
		req = (pmixp_ucx_req_t*)pmixp_rlist_deq(l);
		xassert(req);

		ucp_request_cancel(ucp_worker, req);
		if (req->buffer) {
			/* NOTE: since we are finalizing, we don't really care
			 * about the status */
			_direct_hdr.send_complete(req->msg, PMIXP_P2P_REGULAR,
						  SLURM_SUCCESS);
		}
		ucp_request_release(req);
	}
}

static void _release_recv_requests(pmixp_rlist_t *l)
{
	size_t count = pmixp_rlist_count(l);
	size_t i;

	for (i=0; i < count; i++) {
		pmixp_ucx_req_t *req;
		req = (pmixp_ucx_req_t*)pmixp_rlist_deq(l);
		xassert(req);
		ucp_request_cancel(ucp_worker, req);
		_recv_req_release(req);
	}
}

void pmixp_dconn_ucx_stop()
{
	slurm_mutex_lock(&_ucx_worker_lock);
	_release_send_requests(&_snd_pending);
	_release_send_requests(&_snd_complete);

	_release_recv_requests(&_rcv_pending);
	_release_recv_requests(&_rcv_complete);
	slurm_mutex_unlock(&_ucx_worker_lock);
}

void pmixp_dconn_ucx_finalize()
{
	pmixp_list_elem_t *elem;
	size_t count, i;
	xassert(_direct_hdr_set);

	pmixp_rlist_fini(&_snd_pending);
	pmixp_rlist_fini(&_snd_complete);
	pmixp_rlist_fini(&_rcv_pending);
	pmixp_rlist_fini(&_rcv_complete);

	/* All elements from the previous lists should settle
	 * down in this free list now. Release it!
	 */
	count = pmixp_list_count(&_free_list);
	for (i=0; i < count; i++) {
		elem = pmixp_list_deq(&_free_list);
		pmixp_list_elem_free(elem);
	}

	/* cleanup UCX */
	ucp_worker_destroy(ucp_worker);
	ucp_cleanup(ucp_context);

	/* unload UCX lib */
	_unload_ucx_lib();
	slurm_mutex_destroy(&_ucx_worker_lock);
}

static int _activate_progress()
{
	char buf = 'c';
	int rc = write(_service_pipe[1], &buf, sizeof(buf));
	if (sizeof(buf) != rc) {
		PMIXP_ERROR("Unable to activate UCX progress");
		if (0 > rc) {
			return rc;
		} else {
			return SLURM_ERROR;
		}
	}
	return SLURM_SUCCESS;
}

void _ucx_process_msg(char *buffer, size_t len)
{
	xassert(_direct_hdr_set);
	_direct_hdr.hdr_unpack_cb(buffer, _host_hdr);

	buf_t *buf = create_buf(buffer, len);
	set_buf_offset(buf, _direct_hdr.rhdr_net_size);
	_direct_hdr.new_msg(_host_hdr, buf);
}

static bool _ucx_progress()
{
	pmixp_ucx_req_t *req = NULL;
	ucp_tag_message_h msg_tag;
	ucp_tag_recv_info_t info_tag;
	pmixp_list_elem_t *elem;
	bool new_msg = false;
	size_t count, i;
	int events_observed = 0;

	/* protected progress of UCX */
	slurm_mutex_lock(&_ucx_worker_lock);
	ucp_worker_progress(ucp_worker);

	/* check for new messages */
	while (1) {
		msg_tag = ucp_tag_probe_nb(ucp_worker, 1, 0,
					   1, &info_tag);
		if (NULL == msg_tag) {
			break;
		}
		events_observed++;

		char *msg = xmalloc(info_tag.length);
		pmixp_ucx_req_t *req = (pmixp_ucx_req_t*)
				ucp_tag_msg_recv_nb(ucp_worker, (void*)msg,
						    info_tag.length,
						    ucp_dt_make_contig(1),
						    msg_tag, recv_handle);
		if (UCS_PTR_IS_ERR(req)) {
			PMIXP_ERROR("ucp_tag_msg_recv_nb failed: %s",
				    ucs_status_string(UCS_PTR_STATUS(req)));
			continue;
		}
		new_msg = true;
		req->buffer = msg;
		req->len = info_tag.length;
		if (PMIXP_UCX_ACTIVE == req->status) {
			/* this message is long enough, so it makes
			 * sense to do the progres one more timer */
			pmixp_rlist_enq(&_rcv_pending, req);
		} else {
			pmixp_rlist_enq(&_rcv_complete, req);
		}
	}

	if (!new_msg && pmixp_rlist_empty(&_rcv_pending) &&
				pmixp_rlist_empty(&_snd_pending)) {
		goto exit;
	}

	/* Check pending requests */
	elem = pmixp_rlist_begin(&_rcv_pending);
	while (pmixp_rlist_end(&_rcv_pending) != elem) {
		req = PMIXP_LIST_VAL(elem);
		if (PMIXP_UCX_ACTIVE == req->status){
			/* go to the next element */
			elem = pmixp_rlist_next(&_rcv_pending, elem);
		} else {
			/* grab this element for processing */
			elem = pmixp_rlist_rem(&_rcv_pending, elem);
			pmixp_rlist_enq(&_rcv_complete, req);
			events_observed++;
		}
	}

	elem = pmixp_rlist_begin(&_snd_pending);
	while (pmixp_rlist_end(&_snd_pending) != elem) {
		req = PMIXP_LIST_VAL(elem);
		if (PMIXP_UCX_ACTIVE == req->status){
			/* go to the next element */
			elem = pmixp_rlist_next(&_snd_pending, elem);
		} else {
			/* grab this element for processing */
			elem = pmixp_rlist_rem(&_snd_pending, elem);
			pmixp_rlist_enq(&_snd_complete, req);
			events_observed++;
		}
	}
	slurm_mutex_unlock(&_ucx_worker_lock);

	/* process sends and receives unlocked */
	elem = pmixp_rlist_begin(&_rcv_complete);
	while (pmixp_rlist_end(&_rcv_complete) != elem) {
		req = PMIXP_LIST_VAL(elem);
		/* Skip failed receives
		 * TODO: what more can we do? */
		if (PMIXP_UCX_FAILED != req->status){
			_ucx_process_msg(req->buffer, req->len);
		}
		elem = pmixp_rlist_next(&_rcv_complete, elem);
	}

	elem = pmixp_rlist_begin(&_snd_complete);
	while (pmixp_rlist_end(&_snd_complete) != elem) {
		req = PMIXP_LIST_VAL(elem);
		int rc = SLURM_SUCCESS;
		if (PMIXP_UCX_FAILED == req->status){
			rc = SLURM_ERROR;
		}
		xassert(_direct_hdr_set);
		if (req->buffer) {
			_direct_hdr.send_complete(req->msg,
						  PMIXP_P2P_REGULAR, rc);
		}
		elem = pmixp_rlist_next(&_snd_complete, elem);
	}

	slurm_mutex_lock(&_ucx_worker_lock);

	count = pmixp_rlist_count(&_rcv_complete);
	for (i=0; i < count; i++){
		req = (pmixp_ucx_req_t *)pmixp_rlist_deq(&_rcv_complete);
		/* release request to UCX */
		memset(req, 0, sizeof(*req));
		ucp_request_release(req);
	}

	count = pmixp_rlist_count(&_snd_complete);
	for (i=0; i < count; i++) {
		req = (pmixp_ucx_req_t *)pmixp_rlist_deq(&_snd_complete);
		/* release request to UCX */
		memset(req, 0, sizeof(*req));
		ucp_request_release(req);
	}

exit:
	slurm_mutex_unlock(&_ucx_worker_lock);
	return !!(events_observed);
}

static bool _epoll_readable(eio_obj_t *obj)
{
	ucs_status_t status = UCS_ERR_BUSY;

	/* sanity check */
	xassert(NULL != obj );
	if (obj->shutdown) {
		/* corresponding connection will be
		 * cleaned up during plugin finalize
		 */
		return false;
	}

	do {
		/* process all outstanding events */
		while (_ucx_progress());

		if (pmixp_rlist_count(&_rcv_pending) ||
		    pmixp_rlist_count(&_snd_pending)){
			/* If we got pending requests don't wait
			 * on epoll, activate poll interuprtion through
			 * the service pipe
			 */
			_activate_progress();
			return false;
		}

		/* arm the poll fd */
		slurm_mutex_lock(&_ucx_worker_lock);
		status = ucp_worker_arm(ucp_worker);
		slurm_mutex_unlock(&_ucx_worker_lock);
	} while (UCS_ERR_BUSY == status);

	return true;
}

static int _epoll_read(eio_obj_t *obj, List objs)
{
	if (obj->shutdown) {
		/* corresponding connection will be
		 * cleaned up during plugin finalize
		 */
		return 0;
	}
	/* process all outstanding events */
	while (_ucx_progress());
	return 0;
}

static bool _progress_readable(eio_obj_t *obj)
{
	/* sanity check */
	xassert(NULL != obj );
	if (obj->shutdown) {
		/* corresponding connection will be
			 * cleaned up during plugin finalize
			 */
		return false;
	}
	/* all the control is located in epoll_readable
	 * here we only say that we are readable
	 */
	return true;
}

static int _progress_read(eio_obj_t *obj, List objs)
{
	char buf;

	/* sanity check */
	xassert(NULL != obj );
	if( obj->shutdown ){
		/* corresponding connection will be
		 * cleaned up during plugin finalize
		 */
		return 0;
	}

	/* empty the pipe */
	while (sizeof(buf) == read(_service_pipe[0], &buf, sizeof(buf)));

	/* process all outstanding events */
	while (_ucx_progress());

	return 0;
}

static void *_ucx_init(int nodeid, pmixp_p2p_data_t direct_hdr)
{
	pmixp_dconn_ucx_t *priv = xmalloc(sizeof(pmixp_dconn_ucx_t));
	priv->nodeid = nodeid;
	priv->connected = false;
	if (!_direct_hdr_set) {
		_direct_hdr = direct_hdr;
		_direct_hdr_set = true;
		_host_hdr = xmalloc(_direct_hdr.rhdr_host_size);
	}

	slurm_mutex_lock(&_ucx_worker_lock);
	pmixp_rlist_init(&priv->pending, &_free_list, PMIXP_UCX_LIST_PREALLOC);
	slurm_mutex_unlock(&_ucx_worker_lock);

	return (void*)priv;
}

static void _ucx_fini(void *_priv)
{
	pmixp_dconn_ucx_t *priv = (pmixp_dconn_ucx_t *)_priv;

	if (priv->connected) {
		xfree(priv->ucx_addr);
		slurm_mutex_lock(&_ucx_worker_lock);
		ucp_ep_destroy(priv->server_ep);
		slurm_mutex_unlock(&_ucx_worker_lock);
	} else {
		slurm_mutex_lock(&_ucx_worker_lock);
		pmixp_rlist_fini(&priv->pending);
		slurm_mutex_unlock(&_ucx_worker_lock);
	}
	xfree(priv);
}

static int _ucx_connect(void *_priv, void *ep_data, size_t ep_len,
			void *init_msg)
{
	pmixp_dconn_ucx_t *priv = (pmixp_dconn_ucx_t *)_priv;
	ucp_ep_params_t ep_params;
	ucs_status_t status;
	int rc = SLURM_SUCCESS;
	size_t i, count;

	priv->ucx_addr = ep_data;
	priv->ucx_alen = ep_len;
	/* Connect to the server */
	ep_params.field_mask = UCP_EP_PARAM_FIELD_REMOTE_ADDRESS;
	ep_params.address    = priv->ucx_addr;

	slurm_mutex_lock(&_ucx_worker_lock);
	status = ucp_ep_create(ucp_worker, &ep_params, &priv->server_ep);
	if (status != UCS_OK) {
		PMIXP_ERROR("ucp_ep_create failed: %s",
			    ucs_status_string(status));
		xfree(priv->ucx_addr);
		slurm_mutex_unlock(&_ucx_worker_lock);
		return SLURM_ERROR;
	}
	priv->connected = true;

	/* Enqueue initialization message if requested */
	if (init_msg) {
		pmixp_rlist_push(&priv->pending, init_msg);
	}
	slurm_mutex_unlock(&_ucx_worker_lock);

	/* we need to send data while being unlocked */
	if (SLURM_SUCCESS == rc){
		pmixp_list_elem_t *elem = NULL;
		/* Send all pending messages */
		elem = pmixp_rlist_begin(&priv->pending);
		while (pmixp_rlist_end(&priv->pending) != elem) {
			_ucx_send(_priv, PMIXP_LIST_VAL(elem));
			elem = pmixp_rlist_next(&priv->pending, elem);
		}
	}

	slurm_mutex_lock(&_ucx_worker_lock);

	count = pmixp_rlist_count(&priv->pending);
	for (i=0; i < count; i++) {
		/* message is already processed, the value is
		 * not valid anymore.
		 * just dequeue from the list to ensure service
		 * structures cleanup
		 */
		(void)pmixp_rlist_deq(&priv->pending);
	}
	/* there will be no more pending messages, we can
	 * safely release pending list now
	 */
	pmixp_rlist_fini(&priv->pending);
	slurm_mutex_unlock(&_ucx_worker_lock);

	return rc;
}


static int _ucx_send(void *_priv, void *msg)
{
	pmixp_dconn_ucx_t *priv = (pmixp_dconn_ucx_t *)_priv;
	int rc = SLURM_SUCCESS;
	bool release = false;

	slurm_mutex_lock(&_ucx_worker_lock);
	if (!priv->connected) {
		pmixp_rlist_enq(&priv->pending, msg);
	} else {
		pmixp_ucx_req_t *req = NULL;
		xassert(_direct_hdr_set);
		char *mptr = _direct_hdr.buf_ptr(msg);
		size_t msize = _direct_hdr.buf_size(msg);
		req = (pmixp_ucx_req_t*)
			ucp_tag_send_nb(priv->server_ep,
					(void*)mptr, msize,
					ucp_dt_make_contig(1),
					pmixp_info_nodeid(), send_handle);
		if (UCS_PTR_IS_ERR(req)) {
			PMIXP_ERROR("Unable to send UCX message: %s\n",
				    ucs_status_string(UCS_PTR_STATUS(req)));
			goto exit;
		} else if (UCS_OK == UCS_PTR_STATUS(req)) {
			/* defer release until we unlock ucp worker */
			release = true;
		} else {
			req->msg = msg;
			req->buffer = mptr;
			req->len = msize;
			pmixp_rlist_enq(&_snd_pending, (void*)req);
			_activate_progress();

		}
	}
exit:
	slurm_mutex_unlock(&_ucx_worker_lock);

	if (release){
		_direct_hdr.send_complete(msg, PMIXP_P2P_INLINE,
					  SLURM_SUCCESS);
	}
	return rc;
}

static void _ucx_regio(eio_handle_t *h)
{
	eio_obj_t *obj;

	fd_set_nonblocking(_service_pipe[0]);
	fd_set_nonblocking(_service_pipe[1]);
	fd_set_close_on_exec(_service_pipe[0]);
	fd_set_close_on_exec(_service_pipe[1]);

	obj = eio_obj_create(_service_pipe[0], &_progress_ops, (void *)(-1));
	eio_new_initial_obj(h, obj);

	obj = eio_obj_create(_server_fd, &_epoll_ops, (void *)(-1));
	eio_new_initial_obj(h, obj);
}
