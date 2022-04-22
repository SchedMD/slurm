/*****************************************************************************\
 *  operations.c - Slurm REST API http operations handlers
 *****************************************************************************
 *  Copyright (C) 2019-2020 SchedMD LLC.
 *  Written by Nathan Rini <nate@schedmd.com>
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

#include "slurm/slurm.h"

#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmrestd/operations.h"
#include "src/slurmrestd/rest_auth.h"

openapi_t *openapi_state = NULL;
static pthread_rwlock_t paths_lock = PTHREAD_RWLOCK_INITIALIZER;
static List paths = NULL;

#define MAGIC 0xDFFEAAAE

typedef struct {
	int magic;
	/* unique tag per path */
	int tag;
	/* handler's callback to call on match */
	openapi_handler_t callback;
	/* tag to hand to handler */
	int callback_tag;
} path_t;

typedef struct {
	char *type; /* mime type and sub type unchanged */
	float q; /* quality factor (priority) */
} http_header_accept_t;

static void _check_path_magic(const path_t *path)
{
	xassert(path->magic == MAGIC);
	xassert(path->tag >= 0);
	xassert(path->callback);
}

static void _free_path(void *x)
{
	path_t *path = (path_t *) x;

	if (!path)
		return;

	_check_path_magic(path);

	path->magic = ~MAGIC;
	xfree(path);
}

extern int init_operations(void)
{
	slurm_rwlock_wrlock(&paths_lock);

	if (paths)
		fatal_abort("%s called twice", __func__);

	paths = list_create(_free_path);

	slurm_rwlock_unlock(&paths_lock);

	return SLURM_SUCCESS;
}

extern void destroy_operations(void)
{
	slurm_rwlock_wrlock(&paths_lock);

	FREE_NULL_LIST(paths);

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

extern int bind_operation_handler(const char *str_path,
				  openapi_handler_t callback, int callback_tag)
{
	int path_tag;
	path_t *path;

	slurm_rwlock_wrlock(&paths_lock);

	debug3("%s: binding %s to 0x%"PRIxPTR,
	       __func__, str_path, (uintptr_t) callback);

	path_tag = register_path_tag(openapi_state, str_path);
	if (path_tag == -1)
		fatal_abort("%s: failure registering OpenAPI for path: %s",
			    __func__, str_path);

	if ((path = list_find_first(paths, _match_path_key, &path_tag)))
		goto exists;

	/* add new path */
	debug4("%s: new path %s with path_tag %d callback_tag %d",
	       __func__, str_path, path_tag, callback_tag);
	print_path_tag_methods(openapi_state, path_tag);

	path = xmalloc(sizeof(*path));
	path->magic = MAGIC;
	path->tag = path_tag;
	list_append(paths, path);

exists:
	path->callback = callback;
	path->callback_tag = callback_tag;

	slurm_rwlock_unlock(&paths_lock);

	return SLURM_SUCCESS;
}

static int _rm_path_callback(void *x, void *ptr)
{
	path_t *path = (path_t *)x;
	openapi_handler_t callback = ptr;

	_check_path_magic(path);

	if (path->callback != callback)
		return 0;

	debug5("%s: removing tag %d for callback %"PRIxPTR,
	       __func__, path->tag, (uintptr_t) callback);
	unregister_path_tag(openapi_state, path->tag);

	return 1;
}

extern int unbind_operation_handler(openapi_handler_t callback)
{
	slurm_rwlock_wrlock(&paths_lock);

	if (paths)
		list_delete_all(paths, _rm_path_callback, callback);

	slurm_rwlock_unlock(&paths_lock);
	return SLURM_ERROR;
}

static int _operations_router_reject(const on_http_request_args_t *args,
				     const char *err,
				     http_status_code_t err_code,
				     const char *body_encoding)
{
	send_http_response_args_t send_args = {
		.con = args->context->con,
		.headers = list_create(NULL),
		.http_major = args->http_major,
		.http_minor = args->http_minor,
		.status_code = err_code,
		.body = err,
		.body_encoding = (body_encoding ? body_encoding : "text/plain"),
		.body_length = (err ? strlen(err) : 0),
	};
	http_header_entry_t close = {
		.name = "Connection",
		.value = "Close",
	};

	/* Always warn that connection will be closed after the body is sent */
	list_append(send_args.headers, &close);

	(void) send_http_response(&send_args);

	/* close connection on error */
	con_mgr_queue_close_fd(args->context->con);

	FREE_NULL_LIST(send_args.headers);

	return SLURM_ERROR;
}

static int _resolve_path(on_http_request_args_t *args, int *path_tag,
			 data_t *params)
{
	data_t *path = parse_url_path(args->path, true, false);
	if (!path)
		return _operations_router_reject(
			args, "Unable to parse URL path.",
			HTTP_STATUS_CODE_ERROR_BAD_REQUEST, NULL);

	/* attempt to identify path leaf types */
	(void) data_convert_tree(path, DATA_TYPE_NONE);

	*path_tag = find_path_tag(openapi_state, path, params, args->method);

	FREE_NULL_DATA(path);

	if (*path_tag == -1)
		return _operations_router_reject(
			args,
			"Unable find requested URL. Please view /openapi/v3 for API reference.",
			HTTP_STATUS_CODE_ERROR_NOT_FOUND, NULL);
	else if (*path_tag == -2)
		return _operations_router_reject(
			args,
			"Requested REST method is not defined at URL. Please view /openapi/v3 for API reference.",
			HTTP_STATUS_CODE_ERROR_METHOD_NOT_ALLOWED, NULL);
	else
		return SLURM_SUCCESS;
}

static int _get_query(on_http_request_args_t *args, data_t **query,
		      const char *read_mime)
{
	int rc = SLURM_SUCCESS;

	/* post will have query in the body otherwise it is in the URL */
	if (args->method == HTTP_REQUEST_POST)
		rc = data_g_deserialize(query, args->body,
				      args->body_length,
				      read_mime);
	else
		rc = data_g_deserialize(query, args->query,
				      (args->query ? strlen(args->query) : 0),
				      read_mime);

	if (rc || !*query)
		return _operations_router_reject(
			args, "Unable to parse query.",
			HTTP_STATUS_CODE_ERROR_BAD_REQUEST, NULL);
	else
		return SLURM_SUCCESS;

}

static void _parse_http_accept_entry(char *entry, List l)
{
	char *save_ptr = NULL;
	char *token = NULL;
	char *buffer = xstrdup(entry);
	http_header_accept_t *act = xmalloc(sizeof(*act));
	act->type = NULL;
	act->q = 1; /* default to 1 per rfc7231:5.3.1 */

	token = strtok_r(buffer, ";", &save_ptr);

	if (token) {
		/* first token is the mime type */
		xstrtrim(token);
		act->type = xstrdup(token);
	}
	while ((token = strtok_r(NULL, ",", &save_ptr))) {
		xstrtrim(token);
		sscanf(token, "q=%f", &act->q);
	}
	xfree(buffer);

	debug5("%s: found %s with q=%f", __func__, act->type, act->q);

	list_append(l, act);
}

static int _compare_q(void *x, void *y)
{
	http_header_accept_t *xobj = (http_header_accept_t *) x;
	http_header_accept_t *yobj = (http_header_accept_t *) y;

	if (xobj->q < yobj->q)
		return -1;
	else if (xobj->q > yobj->q)
		return 1;

	return 0;
}

static void _http_accept_list_delete(void *x)
{
	http_header_accept_t *obj = (http_header_accept_t *) x;

	if (!obj)
		return;

	xfree(obj->type);
	xfree(obj);
}

static List _parse_http_accept(const char *accept)
{
	List l = list_create(_http_accept_list_delete);
	xassert(accept);
	char *save_ptr = NULL;
	char *token = NULL;
	char *buffer = xstrdup(accept);

	token = strtok_r(buffer, ",", &save_ptr);
	while (token) {
		xstrtrim(token);
		_parse_http_accept_entry(token, l);
		token = strtok_r(NULL, ",", &save_ptr);
	}
	xfree(buffer);

	list_sort(l, _compare_q);

	return l;
}

static int _resolve_mime(on_http_request_args_t *args, const char **read_mime,
			 const char **write_mime)
{
	*read_mime = args->content_type;

	//TODO: check Content-encoding and make sure it is identity only!
	//https://developer.mozilla.org/en-US/docs/Web/HTTP/Headers/Content-Encoding

	if (!*read_mime) {
		*read_mime = MIME_TYPE_URL_ENCODED;

		debug4("%s: [%s] did not provide a known content type header. Assuming URL encoded.",
		      __func__, args->context->con->name);
	}

	if (args->accept) {
		List accept = _parse_http_accept(args->accept);
		http_header_accept_t *ptr = NULL;
		ListIterator itr = list_iterator_create(accept);
		while ((ptr = list_next(itr))) {
			debug4("%s: [%s] accepts %s with q=%f",
			       __func__, args->context->con->name, ptr->type,
			       ptr->q);

			if ((*write_mime = data_resolve_mime_type(ptr->type))) {
				debug4("%s: [%s] found accepts %s=%s with q=%f",
				       __func__, args->context->con->name,
				       ptr->type, *write_mime, ptr->q);
				break;
			} else {
				debug4("%s: [%s] rejecting accepts %s with q=%f",
				       __func__, args->context->con->name,
				       ptr->type, ptr->q);
			}
		}
		list_iterator_destroy(itr);
		list_destroy(accept);
	} else {
		debug3("%s: [%s] Accept header not specified. Defaulting to JSON.",
		       __func__, args->context->con->name);
		*write_mime = MIME_TYPE_JSON;
	}

	if (!*write_mime)
		return _operations_router_reject(
			args, "Accept content type is unknown",
			HTTP_STATUS_CODE_ERROR_UNSUPPORTED_MEDIA_TYPE, NULL);

	if (args->method != HTTP_REQUEST_POST && args->body_length > 0)
		return _operations_router_reject(
			args,
			"Unexpected http body provided for non-POST method",
			HTTP_STATUS_CODE_ERROR_BAD_REQUEST, NULL);

	if (xstrcasecmp(*read_mime, MIME_TYPE_URL_ENCODED) &&
	    (args->body_length == 0)) {
		/*
		 * RFC7273#3.1.1.5 only specifies a sender SHOULD send
		 * the correct content-type header but allows for them to be
		 * wrong and expects the server to handle that gracefully.
		 *
		 * We will instead override the mime type if there is empty body
		 * content to avoid unneccesssily rejecting otherwise compliant
		 * requests.
		 */
		debug("%s: [%s] Overriding content type from %s to %s for %s",
		      __func__, args->context->con->name,
		      *read_mime,
		      MIME_TYPE_URL_ENCODED,
		      get_http_method_string(args->method));

		*read_mime = MIME_TYPE_URL_ENCODED;
	}

	debug3("%s: [%s] mime read: %s write: %s",
	       __func__, args->context->con->name, *read_mime, *write_mime);

	return SLURM_SUCCESS;
}

static int _call_handler(on_http_request_args_t *args, data_t *params,
			 data_t *query, openapi_handler_t callback,
			 int callback_tag, const char *write_mime)
{
	int rc;
	data_t *resp = data_new();
	char *body = NULL;

	rc = callback(args->context->con->name, args->method, params, query,
		      callback_tag, resp, args->context->auth);

	if (data_get_type(resp) != DATA_TYPE_NULL) {
		int rc2 = data_g_serialize(&body, resp, write_mime,
					   DATA_SER_FLAGS_PRETTY);

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
			.con = args->context->con,
			.http_major = args->http_major,
			.http_minor = args->http_minor,
			.status_code = HTTP_STATUS_CODE_REDIRECT_NOT_MODIFIED,
		};

		rc = send_http_response(&send_args);
	} else if (rc) {
		http_status_code_t e = HTTP_STATUS_CODE_SRVERR_INTERNAL;

		if (rc == ESLURM_REST_INVALID_QUERY)
			e = HTTP_STATUS_CODE_ERROR_BAD_REQUEST;
		else if (rc == ESLURM_REST_FAIL_PARSING)
			e = HTTP_STATUS_CODE_ERROR_BAD_REQUEST;
		else if (rc == ESLURM_REST_INVALID_JOBS_DESC)
			e = HTTP_STATUS_CODE_ERROR_BAD_REQUEST;
		else if (rc == ESLURM_DATA_UNKNOWN_MIME_TYPE)
			e = HTTP_STATUS_CODE_ERROR_UNSUPPORTED_MEDIA_TYPE;

		rc = _operations_router_reject(args, body, e, write_mime);
	} else {
		send_http_response_args_t send_args = {
			.con = args->context->con,
			.http_major = args->http_major,
			.http_minor = args->http_minor,
			.status_code = HTTP_STATUS_CODE_SUCCESS_OK,
			.body = NULL,
			.body_length = 0,
		};

		if (body) {
			send_args.body = body;
			send_args.body_length = strlen(body);
			send_args.body_encoding = write_mime;
		}

		rc = send_http_response(&send_args);
	}

	xfree(body);
	FREE_NULL_DATA(resp);

	return rc;
}

extern int operations_router(on_http_request_args_t *args)
{
	int rc = SLURM_SUCCESS;
	data_t *query = NULL;
	data_t *params = NULL;
	int path_tag;
	path_t *path = NULL;
	openapi_handler_t callback = NULL;
	int callback_tag;
	const char *read_mime = NULL;
	const char *write_mime = NULL;

	info("%s: [%s] %s %s",
	     __func__, args->context->con->name,
	     get_http_method_string(args->method), args->path);

	if ((rc = rest_authenticate_http_request(args))) {
		error("%s: [%s] authentication failed: %s",
		      __func__, args->context->con->name,
		      slurm_strerror(rc));
		_operations_router_reject(args, "Authentication failure",
					  HTTP_STATUS_CODE_ERROR_UNAUTHORIZED,
					  NULL);
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
	callback = path->callback;
	callback_tag = path->callback_tag;
	slurm_rwlock_unlock(&paths_lock);

	debug5("%s: [%s] found callback handler: (0x%"PRIXPTR") callback_tag %d for path: %s",
	       __func__, args->context->con->name, (uintptr_t) callback,
	       callback_tag, args->path);

	if ((rc = _resolve_mime(args, &read_mime, &write_mime)))
		goto cleanup;

	if ((rc = _get_query(args, &query, read_mime)))
		goto cleanup;

	rc = _call_handler(args, params, query, callback, callback_tag,
			   write_mime);

cleanup:
	FREE_NULL_DATA(query);
	FREE_NULL_DATA(params);

	/* always clear the auth context */
	rest_auth_g_free(args->context->auth);
	args->context->auth = NULL;

	return rc;
}
