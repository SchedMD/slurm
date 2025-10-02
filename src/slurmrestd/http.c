/*****************************************************************************\
 *  http.c - handling HTTP
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
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

#include "src/common/http.h"
#include "src/common/http_con.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"

#include "src/conmgr/conmgr.h"

#include "src/slurmrestd/http.h"
#include "src/slurmrestd/operations.h"
#include "src/slurmrestd/rest_auth.h"

#define MAGIC 0xDFBFFEEF

typedef struct http_context_s {
	int magic; /* MAGIC */
	conmgr_fd_ref_t *con;
	rest_auth_context_t *auth;
} http_context_t;

static http_con_t *_ctxt_get_hcon(http_context_t *ctxt)
{
	http_con_t *hcon = (((void *) ctxt) + sizeof(*ctxt));

	xassert(ctxt->magic == MAGIC);

	return hcon;
}

extern int send_http_response(http_context_t *context,
			      const send_http_response_args_t *args)
{
	/* Fake having a shadow buffer for body */
	buf_t buffer = {
		.magic = BUF_MAGIC,
		.head = (void *) args->body,
		.size = args->body_length,
		.processed = args->body_length,
		.shadow = true,
	};

	xassert(context->magic == MAGIC);

	return http_con_send_response(_ctxt_get_hcon(context),
				      args->status_code, args->headers, false,
				      (args->body ? &buffer : NULL),
				      args->body_encoding);
}

static void _connection_finish(http_context_t *ctxt)
{
	xassert(ctxt->magic == MAGIC);

	conmgr_fd_free_ref(&ctxt->con);

	/* auth should have been released long before now */
	xassert(!ctxt->auth);
	FREE_NULL_REST_AUTH(ctxt->auth);

	/* http_con will free() hcon before on_close() */
	ctxt->magic = ~MAGIC;
	xfree(ctxt);

	if (inetd_mode)
		conmgr_request_shutdown();
}

static int _on_request(http_con_t *hcon, const char *name,
		       const http_con_request_t *request, void *arg)
{
	http_context_t *ctxt = arg;
	int rc = EINVAL;
	on_http_request_args_t args = {
		.method = request->method,
		.headers = request->headers,
		.path = request->url.path,
		.query = request->url.query,
		.context = ctxt,
		.con = NULL,
		.name = name,
		.http_major = request->http_version.major,
		.http_minor = request->http_version.minor,
		.content_type = request->content_type,
		.accept = request->accept,
		.body = (request->content ? get_buf_data(request->content) :
					    NULL),
		.body_length =
			(request->content ? get_buf_offset(request->content) :
					    0),
		.body_encoding = request->content_type,
	};

	xassert(ctxt->magic == MAGIC);

	args.con = conmgr_con_link(ctxt->con);

	rc = operations_router(&args);

	conmgr_fd_free_ref(&args.con);
	FREE_NULL_REST_AUTH(ctxt->auth);

	return rc;
}

static void _on_close(const char *name, void *arg)
{
	http_context_t *ctxt = arg;

	xassert(ctxt->magic == MAGIC);

	_connection_finish(ctxt);
}

static void *_on_connection(conmgr_fd_t *con, void *arg)
{
	static const http_con_server_events_t events = {
		.on_request = _on_request,
		.on_close = _on_close,
	};
	http_context_t *ctxt = NULL;

	ctxt = xmalloc(sizeof(*ctxt) + http_con_bytes());
	ctxt->magic = MAGIC;
	ctxt->con = conmgr_fd_new_ref(con);

	/*
	 * Assign as HTTP if not already shutting down and close the connection
	 * on any failure
	 */
	if (http_con_assign_server(ctxt->con, _ctxt_get_hcon(ctxt), &events,
				   ctxt)) {
		_connection_finish(ctxt);
		return NULL;
	}

	return _ctxt_get_hcon(ctxt);
}

static int _on_data(conmgr_fd_t *con, void *arg)
{
	fatal_abort("this should never happen");
}

const conmgr_events_t *http_events_get(void)
{
	static const conmgr_events_t events = {
		.on_connection = _on_connection,
		.on_data = _on_data,
	};

	return &events;
}

extern void *http_context_get_auth(http_context_t *context)
{
	if (!context)
		return NULL;

	xassert(context->magic == MAGIC);

	return context->auth;
}

extern void *http_context_set_auth(http_context_t *context, void *auth)
{
	void *old = NULL;

	if (!context)
		return auth;

	xassert(context->magic == MAGIC);

	old = context->auth;
	context->auth = auth;

	return old;
}

extern void http_context_free_null_auth(http_context_t *context)
{
	if (!context)
		return;

	xassert(context->magic == MAGIC);

	FREE_NULL_REST_AUTH(context->auth);
}
