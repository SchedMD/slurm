/*****************************************************************************\
 *  operations.c - Slurm REST API http operations handlers
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

#include "config.h"

#include <unistd.h>

#include "slurm/slurm_errno.h"
#include "slurm/slurm.h"

#include "src/common/http.h"
#include "src/common/http_mime.h"
#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/read_config.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/serializer.h"

#include "src/slurmrestd/http.h"
#include "src/slurmrestd/operations.h"
#include "src/slurmrestd/rest_auth.h"

static pthread_rwlock_t paths_lock = PTHREAD_RWLOCK_INITIALIZER;
static list_t *paths = NULL;
static data_parser_t **parsers; /* symlink to parser array */
bool inetd_mode = false;

typedef struct {
#define PATH_MAGIC 0xDFFEA1AE
	int magic; /* PATH_MAGIC */
	/* unique tag per path */
	int tag;
	/* handler's ctxt callback to call on match */
	const openapi_path_binding_t *op_path;
	/* meta info from plugin */
	const openapi_resp_meta_t *meta;
	/* tag to hand to handler */
	int callback_tag;
	/* assigned parser */
	data_parser_t *parser;
} path_t;

static void _check_path_magic(const path_t *path)
{
	xassert(path->magic == PATH_MAGIC);
	xassert(path->tag >= 0);
	xassert(path->op_path->callback);
}

static void _free_path(void *x)
{
	path_t *path = (path_t *) x;

	if (!path)
		return;

	_check_path_magic(path);

	path->magic = ~PATH_MAGIC;
	xfree(path);
}

extern int init_operations(data_parser_t **init_parsers)
{
	slurm_rwlock_wrlock(&paths_lock);

	if (paths)
		fatal_abort("%s called twice", __func__);

	paths = list_create(_free_path);
	parsers = init_parsers;

	slurm_rwlock_unlock(&paths_lock);

	return SLURM_SUCCESS;
}

extern void destroy_operations(void)
{
	slurm_rwlock_wrlock(&paths_lock);

	FREE_NULL_LIST(paths);
	parsers = NULL;

	slurm_rwlock_unlock(&paths_lock);
}

static int _operations_router_reject(on_http_request_args_t *args,
				     const char *err, slurm_err_t error_code,
				     const char *body_encoding)
{
	send_http_response_args_t send_args = {
		.headers = list_create(NULL),
		.hcon = args->hcon,
		.http_major = args->http_major,
		.http_minor = args->http_minor,
		.body_encoding =
			(body_encoding ? body_encoding : MIME_TYPE_TEXT),
	};
	http_header_t close = {
		.magic = HTTP_HEADER_MAGIC,
		.name = "Connection",
		.value = "Close",
	};

	send_args.status_code = http_status_from_error(error_code);

	if (!err)
		send_args.body = slurm_strerror(error_code);
	else
		send_args.body = err;

	send_args.body_length = strlen(send_args.body);

	/* Always warn that connection will be closed after the body is sent */
	list_append(send_args.headers, &close);

	(void) send_http_response(args->context, &send_args);

	FREE_NULL_LIST(send_args.headers);

	xassert(error_code);
	args->rejected = true;
	return error_code;
}

static int _get_query(on_http_request_args_t *args, data_t **query,
		      const char *read_mime)
{
	int rc = SLURM_SUCCESS;

	/*
	 * RFC 7230 3.3:
	 * 	The presence of a message body in a request is signaled by a
	 * 	Content-Length or Transfer-Encoding header field.
	 */
	if (args->body_length > 0)
		rc = serialize_g_string_to_data(query, args->body,
						args->body_length, read_mime);
	else
		rc = serialize_g_string_to_data(
			query, args->query,
			(args->query ? strlen(args->query) : 0), read_mime);

	if (!rc && !*query)
		rc = ESLURM_REST_INVALID_QUERY;

	if (rc)
		return _operations_router_reject(args, NULL, rc, NULL);
	else
		return SLURM_SUCCESS;
}

static int _call_handler(on_http_request_args_t *args, data_t *params,
			 data_t *query, const openapi_path_binding_t *op_path,
			 const char *write_mime, data_parser_t *parser,
			 const openapi_resp_meta_t *meta)
{
	int rc;
	data_t *resp = data_new();
	char *body = NULL;
	http_status_code_t e = HTTP_STATUS_CODE_INVALID;
	void *auth = NULL;
	void *db_conn = NULL;

	xassert(op_path);
	debug3("%s: [%s] BEGIN: calling ctxt handler: %p for path: %s",
	       __func__, args->name, op_path->callback, args->path);

	auth = http_context_set_auth(args->context, NULL);

	if (!(op_path->flags & OPENAPI_BIND_NO_SLURMDBD) &&
	    slurm_conf.accounting_storage_type)
		db_conn = openapi_get_db_conn(auth);

	rc = wrap_openapi_ctxt_callback(args->name, args->method, params, query,
					0, resp, db_conn, parser, op_path,
					meta);

	/*
	 * Clear auth context after callback is complete. Client has to provide
	 * full auth for every request already.
	 */
	FREE_NULL_REST_AUTH(auth);

	if (data_get_type(resp) != DATA_TYPE_NULL) {
		int rc2;
		serializer_flags_t sflags = SER_FLAGS_NONE;

		if (data_parser_g_is_complex(parser))
			sflags |= SER_FLAGS_COMPLEX;

		rc2 = serialize_g_data_to_string(&body, NULL, resp, write_mime,
						 sflags);

		if (!rc)
			rc = rc2;
	}

	if (rc == SLURM_NO_CHANGE_IN_DATA) {
		/*
		 * RFC#7232 Section:4.1
		 *
		 * Send minimal response that nothing has changed
		 *
		 */
		send_http_response_args_t send_args = {
			.http_major = args->http_major,
			.http_minor = args->http_minor,
			.hcon = args->hcon,
			.status_code = HTTP_STATUS_CODE_REDIRECT_NOT_MODIFIED,
		};
		e = send_args.status_code;
		rc = send_http_response(args->context, &send_args);
	} else if (rc && (rc != ESLURM_REST_EMPTY_RESULT)) {
		rc = _operations_router_reject(args, body, rc, write_mime);
	} else {
		send_http_response_args_t send_args = {
			.http_major = args->http_major,
			.http_minor = args->http_minor,
			.hcon = args->hcon,
			.status_code = HTTP_STATUS_CODE_SUCCESS_OK,
			.body = NULL,
			.body_length = 0,
		};

		if (rc == ESLURM_REST_EMPTY_RESULT) {
			send_args.status_code =
				HTTP_STATUS_CODE_SUCCESS_NO_CONTENT;
		} else if (body) {
			send_args.body = body;
			send_args.body_length = strlen(body);
			send_args.body_encoding = write_mime;
		}

		rc = send_http_response(args->context, &send_args);
		e = send_args.status_code;
	}

	debug3("%s: [%s] END: calling handler: %p for path: %s rc[%d]=%s status[%d]=%s",
	       __func__, args->name, op_path->callback, args->path, rc,
	       slurm_strerror(rc), ((e == HTTP_STATUS_CODE_INVALID) ?
				    http_status_from_error(rc) : e),
	       get_http_status_code_string(e));

	xfree(body);
	FREE_NULL_DATA(resp);

	return rc;
}

extern int on_request(http_con_t *hcon, const char *name, http_context_t *ctxt,
		      const http_con_request_t *request,
		      const openapi_path_binding_t *op_path,
		      const openapi_path_binding_method_t *op_method,
		      const openapi_resp_meta_t *meta, data_parser_t *parser,
		      const openapi_entry_t *openapi_entry)
{
	int rc = SLURM_SUCCESS;
	data_t *query = NULL;
	data_t *params = NULL;
	const char *read_mime = NULL, *write_mime = NULL;
	const char *body =
		(request->content ? get_buf_data(request->content) : NULL);
	const size_t body_length =
		(request->content ? get_buf_offset(request->content) : 0);
	on_http_request_args_t hargs = {
		.context = ctxt,
		.method = request->method,
		.headers = request->headers,
		.path = request->url.path,
		.hcon = hcon,
		.name = name,
		.http_minor = request->http_version.minor,
		.http_major = request->http_version.major,
		.content_type = request->content_type,
		.accept = request->accept,
		.body = body,
		.body_length = body_length,
		.body_encoding = NULL,
		.query = request->url.query,
	};
	on_http_request_args_t *args = &hargs;

	info("%s: [%s] %s %s",
	     __func__, name, get_http_method_string(args->method), args->path);

	if ((rc = rest_authenticate_http_request(args))) {
		error("%s: [%s] authentication failed: %s",
		      __func__, name, slurm_strerror(rc));
		rc = _operations_router_reject(args, NULL, rc, NULL);
		goto cleanup;
	}

	params = data_set_dict(data_new());
	if (openapi_entry &&
	    (rc = resolve_params(openapi_entry, request->url.path, params)))
		goto cleanup;

	xassert(parser);
	debug5("%s: [%s] found callback handler: (%p) path=%s parser=%s",
	       __func__, name, op_path->callback, args->path,
	       data_parser_get_plugin(parser));

	if ((rc = http_resolve_mime_types(name, request, &read_mime,
					  &write_mime)))
		goto cleanup;

	if ((rc = _get_query(args, &query, read_mime)))
		goto cleanup;

	rc = _call_handler(args, params, query, op_path, write_mime, parser,
			   meta);

cleanup:
	FREE_NULL_DATA(query);
	FREE_NULL_DATA(params);

	/* always clear the auth context */
	http_context_free_null_auth(args->context);

	/*
	 * The client has already been answered. Returning the error would make
	 * http_con treat the request as unparsed and send a second response.
	 */
	if (args->rejected)
		rc = SLURM_SUCCESS;

	return rc;
}
