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

static int _match_path_key(void *x, void *ptr)
{
	path_t *path = (path_t *)x;
	int tag = *(int *) ptr;

	_check_path_magic(path);

	if (path->tag == tag)
		return 1;
	else
		return 0;
}

static int _add_binded_path(const char *path_str,
			    const openapi_path_binding_t *op_path,
			    const openapi_resp_meta_t *meta,
			    data_parser_t *parser)
{
	int tag, rc;
	path_t *path;

	slurm_rwlock_wrlock(&paths_lock);
	rc = register_path_binding(path_str, op_path, meta, parser, &tag);
	slurm_rwlock_unlock(&paths_lock);

	if (rc == ESLURM_NOT_SUPPORTED)
		return SLURM_SUCCESS;

	if (rc)
		return rc;

	/* path should never be a duplicate */
	xassert(!list_find_first(paths, _match_path_key, &tag));

	/* add new path */
	debug4("%s: new bound path %s with path_tag %d",
	       __func__, (path_str ? path_str : op_path->path), tag);
	print_path_tag_methods(tag);

	path = xmalloc(sizeof(*path));
	path->magic = PATH_MAGIC;
	path->tag = tag;
	path->parser = parser;
	path->op_path = op_path;
	path->meta = meta;

	list_append(paths, path);

	return SLURM_SUCCESS;
}

extern int bind_operation_path(const openapi_path_binding_t *op_path,
			       const openapi_resp_meta_t *meta)
{
	int rc = SLURM_SUCCESS;
	data_t *resp;

	if (!(op_path->flags & OP_BIND_DATA_PARSER)) {
		data_parser_t *default_parser = NULL;

		if (!parsers[0])
			fatal("No data_parsers plugins loaded. Refusing to load.");

		for (int i = 0; parsers[i]; i++) {
			if (!xstrcmp(data_parser_get_plugin(parsers[i]),
				     SLURM_DATA_PARSER_VERSION)) {
				default_parser = parsers[i];
				break;
			}
		}

		if (!default_parser)
			default_parser = parsers[0];

		return _add_binded_path(NULL, op_path, meta, default_parser);
	}

	resp = data_new();

	xassert(xstrstr(op_path->path, OPENAPI_DATA_PARSER_PARAM));

	for (int i = 0; !rc && parsers[i]; i++) {
		char *path = xstrdup(op_path->path);

		xstrsubstitute(path, OPENAPI_DATA_PARSER_PARAM,
			       data_parser_get_plugin_version(parsers[i]));

		rc = _add_binded_path(path, op_path, meta, parsers[i]);

		xfree(path);
		if (rc)
			break;
	}

	FREE_NULL_DATA(resp);

	return rc;
}

static int _operations_router_reject(on_http_request_args_t *args,
				     const char *err, slurm_err_t error_code,
				     const char *body_encoding)
{
	send_http_response_args_t send_args = {
		.headers = list_create(NULL),
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

	send_args.con = conmgr_fd_get_ref(args->con);
	send_args.status_code = http_status_from_error(error_code);

	if (!err)
		send_args.body = slurm_strerror(error_code);
	else
		send_args.body = err;

	send_args.body_length = strlen(send_args.body);

	/* Always warn that connection will be closed after the body is sent */
	list_append(send_args.headers, &close);

	(void) send_http_response(args->context, &send_args);

	/* close connection on error */
	conmgr_queue_close_fd(send_args.con);

	FREE_NULL_LIST(send_args.headers);

	return error_code;
}

static int _resolve_path(on_http_request_args_t *args, int *path_tag,
			 data_t *params)
{
	data_t *path = parse_url_path(args->path, true, false);
	if (!path)
		return _operations_router_reject(args, NULL,
						 ESLURM_URL_INVALID_PATH, NULL);

	/* attempt to identify path leaf types */
	(void) data_convert_tree(path, DATA_TYPE_NONE);

	*path_tag = find_path_tag(path, params, args->method);

	FREE_NULL_DATA(path);

	if (*path_tag == -1)
		return _operations_router_reject(args, NULL,
						 ESLURM_REST_UNKNOWN_URL, NULL);
	else if (*path_tag == -2)
		return _operations_router_reject(args, NULL,
						 ESLURM_REST_UNKNOWN_URL_METHOD,
						 NULL);
	else
		return SLURM_SUCCESS;
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
			 int callback_tag, const char *write_mime,
			 data_parser_t *parser, const openapi_resp_meta_t *meta)
{
	int rc;
	data_t *resp = data_new();
	char *body = NULL;
	http_status_code_t e = HTTP_STATUS_CODE_INVALID;
	void *auth = NULL;
	void *db_conn = NULL;

	xassert(op_path);
	debug3("%s: [%s] BEGIN: calling ctxt handler: 0x%"PRIXPTR"[%d] for path: %s",
	       __func__, args->name, (uintptr_t) op_path->callback,
	       callback_tag, args->path);

	auth = http_context_set_auth(args->context, NULL);

	if (!(op_path->flags & OP_BIND_NO_SLURMDBD) &&
	    slurm_conf.accounting_storage_type)
		db_conn = openapi_get_db_conn(auth);

	rc = wrap_openapi_ctxt_callback(args->name, args->method, params, query,
					callback_tag, resp, db_conn, parser,
					op_path, meta);

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
			.status_code = HTTP_STATUS_CODE_REDIRECT_NOT_MODIFIED,
		};
		send_args.con = conmgr_fd_get_ref(args->con);
		e = send_args.status_code;
		rc = send_http_response(args->context, &send_args);
	} else if (rc && (rc != ESLURM_REST_EMPTY_RESULT)) {
		rc = _operations_router_reject(args, body, rc, write_mime);
	} else {
		send_http_response_args_t send_args = {
			.http_major = args->http_major,
			.http_minor = args->http_minor,
			.status_code = HTTP_STATUS_CODE_SUCCESS_OK,
			.body = NULL,
			.body_length = 0,
		};

		send_args.con = conmgr_fd_get_ref(args->con);

		if (body) {
			send_args.body = body;
			send_args.body_length = strlen(body);
			send_args.body_encoding = write_mime;
		}

		rc = send_http_response(args->context, &send_args);
		e = send_args.status_code;
	}

	debug3("%s: [%s] END: calling handler: (0x%"PRIXPTR") callback_tag %d for path: %s rc[%d]=%s status[%d]=%s",
	       __func__, args->name, (uintptr_t) op_path->callback,
	       callback_tag, args->path, rc, slurm_strerror(rc),
	       ((e == HTTP_STATUS_CODE_INVALID) ? http_status_from_error(rc) :
		e), get_http_status_code_string(e));

	xfree(body);
	FREE_NULL_DATA(resp);

	return rc;
}

extern int operations_router(on_http_request_args_t *args, http_con_t *hcon,
			     const char *name,
			     const http_con_request_t *request,
			     http_context_t *ctxt)
{
	int rc = SLURM_SUCCESS;
	data_t *query = NULL;
	data_t *params = NULL;
	int path_tag;
	path_t *path = NULL;
	int callback_tag;
	const char *read_mime = NULL, *write_mime = NULL;
	data_parser_t *parser = NULL;

	info("%s: [%s] %s %s",
	     __func__, name, get_http_method_string(args->method), args->path);

	if ((rc = rest_authenticate_http_request(args))) {
		error("%s: [%s] authentication failed: %s",
		      __func__, name, slurm_strerror(rc));
		_operations_router_reject(args, NULL, rc, NULL);
		return rc;
	}

	params = data_set_dict(data_new());
	if ((rc = _resolve_path(args, &path_tag, params)))
		goto cleanup;

	/*
	 * Hold read lock while the callback is executing to avoid
	 * unbind of a function that is actively running
	 */
	slurm_rwlock_rdlock(&paths_lock);

	if (!(path = list_find_first(paths, _match_path_key, &path_tag)))
		fatal_abort("%s: found tag but missing path handler", __func__);
	_check_path_magic(path);

	/* clone over the callback info to release lock */
	callback_tag = path->callback_tag;
	parser = path->parser;
	slurm_rwlock_unlock(&paths_lock);

	debug5("%s: [%s] found callback handler: (0x%"PRIXPTR") callback_tag=%d path=%s parser=%s",
	       __func__, name, (uintptr_t) path->op_path->callback,
	       callback_tag, args->path,
	       (parser ? data_parser_get_plugin(parser) : ""));

	if ((rc = http_resolve_mime_types(name, request, &read_mime,
					  &write_mime)))
		goto cleanup;

	if ((rc = _get_query(args, &query, read_mime)))
		goto cleanup;

	rc = _call_handler(args, params, query, path->op_path, callback_tag,
			   write_mime, parser, path->meta);

cleanup:
	FREE_NULL_DATA(query);
	FREE_NULL_DATA(params);

	/* always clear the auth context */
	http_context_free_null_auth(args->context);

	return rc;
}
