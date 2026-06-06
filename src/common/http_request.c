/*****************************************************************************\
 *  http_request.c - HTTP on request handler
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

#include "src/common/http_request.h"
#include "src/common/http.h"
#include "src/common/http_con.h"
#include "src/common/http_mime.h"
#include "src/common/http_router.h"
#include "src/common/log.h"
#include "src/common/openapi.h"
#include "src/common/read_config.h"
#include "src/common/serdes.h"
#include "src/common/uid.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/data_parser.h"
#include "src/interfaces/http_auth.h"

#define BOUND_REQUEST_MAGIC 0xab008800

typedef struct {
	int magic; /* BOUND_REQUEST_MAGIC  */
	data_parser_t *parser;
	char *path;
	http_request_on_request_t on_request;
	http_request_on_error_t on_error;
	data_parser_type_t reply_type;
	void *arg;
} bound_request_t;

#define HTTP_REQUEST_EVENT_MAGIC 0xbf0f8830

typedef struct http_request_event_s {
	int magic; /* HTTP_REQUEST_EVENT_MAGIC */
	bound_request_t *breq;
	http_con_t *hcon;
	const char *name;
	const char *read_mime;
	const char *write_mime;
	const http_con_request_t *request;
} http_request_event_t;

static int _on_request(http_con_t *hcon, const char *name,
		       const http_con_request_t *request, void *arg,
		       void *path_arg)
{
	bound_request_t *breq = path_arg;
	http_request_event_t event = {
		.magic = HTTP_REQUEST_EVENT_MAGIC,
		.breq = breq,
		.hcon = hcon,
		.name = name,
		.request = request,
	};
	int rc = EINVAL;
	uid_t uid = SLURM_AUTH_NOBODY;

	xassert(breq->magic == BOUND_REQUEST_MAGIC);

	if ((rc = http_auth_g_authenticate(HTTP_AUTH_PLUGIN_ANY, &uid, hcon,
					   name, request))) {
		error("%s: [%s] Rejecting HTTP authentication: %s",
		      __func__, name, slurm_strerror(rc));
		return breq->on_error(&event, hcon, name, request, rc);
	}

	if ((rc = http_resolve_mime_types(name, request, &event.read_mime,
					  &event.write_mime))) {
		error("%s: [%s] Rejecting HTTP mime headers: %s",
		      __func__, name, slurm_strerror(rc));
		return breq->on_error(&event, hcon, name, request, rc);
	}

	if (slurm_conf.debug_flags & DEBUG_FLAG_NET) {
		char *uid_str = uid_to_string_or_null(uid);

		log_flag(NET, "%s: [%s] Accepted HTTP request: method=%s path=%s uid[%d]:%s read_mime:%s write_mime:%s",
			 __func__, name,
			 get_http_method_string(request->method),
			 request->url.path, uid, uid_str, event.read_mime,
			 event.write_mime);

		xfree(uid_str);
	}

	return breq->on_request(&event, hcon, name, uid, request->method,
				request, breq->arg);
}

static void _breq_free(http_router_on_request_event_t on_request,
		       void *path_arg)
{
	bound_request_t *breq = path_arg;

	xassert(breq->magic == BOUND_REQUEST_MAGIC);
	xassert(on_request == _on_request);

	breq->magic = ~BOUND_REQUEST_MAGIC;
	xfree(breq->path);
	xfree(breq);
}

static void _bind(data_parser_t *parser, http_request_method_t method,
		  const char *path, http_request_on_request_t on_request,
		  http_request_on_error_t on_error,
		  data_parser_type_t reply_type, void *arg)
{
	bound_request_t *breq = xmalloc(sizeof(*breq));
	*breq = (bound_request_t) {
		.magic = BOUND_REQUEST_MAGIC,
		.on_request = on_request,
		.on_error = on_error,
		.reply_type = reply_type,
		.arg = arg,
		.path = xstrdup(path),
		.parser = parser,
	};

	if (parser)
		xstrsubstitute(breq->path, OPENAPI_DATA_PARSER_PARAM,
			       data_parser_get_plugin_version(parser));

	http_router_bind(method, breq->path, _on_request, _breq_free, breq);
}

extern void http_request_bind(data_parser_t **parsers,
			      http_request_method_t method, const char *path,
			      http_request_on_request_t on_request,
			      http_request_on_error_t on_error,
			      data_parser_type_t reply_type, void *arg)
{
	if (reply_type == DATA_PARSER_TYPE_INVALID) {
		_bind(NULL, method, path, on_request, on_error, reply_type,
		      arg);
		return;
	}

	xassert(reply_type > DATA_PARSER_TYPE_INVALID);
	xassert(reply_type < DATA_PARSER_TYPE_MAX);

	for (int i = 0; parsers[i]; i++)
		if (data_parser_g_resolve_type_string(parsers[i], reply_type))
			_bind(parsers[i], method, path, on_request, on_error,
			      reply_type, arg);
}

extern int http_request_reply(http_request_event_t *event, int rc,
			      list_t *headers, bool close_header, void *reply,
			      size_t reply_bytes)
{
	bound_request_t *breq = event->breq;
	data_parser_t *parser = breq->parser;
	http_con_t *hcon = event->hcon;
	const char *name = event->name;
	const http_con_request_t *request = event->request;
	const char *write_mime = event->write_mime;
	http_status_code_t status = http_status_from_error(rc);

	xassert(event->magic == HTTP_REQUEST_EVENT_MAGIC);
	xassert(event->breq->magic == BOUND_REQUEST_MAGIC);

	if (reply && parser) {
		buf_t *buffer = NULL;

		xassert(breq->reply_type > DATA_PARSER_TYPE_INVALID);
		xassert(breq->reply_type < DATA_PARSER_TYPE_MAX);

		if (!(buffer = try_init_buf(BUF_SIZE)))
			return breq->on_error(event, hcon, name, request,
					      ENOMEM);

		if ((rc = serdes_dump_buf(parser, breq->reply_type, reply,
					  reply_bytes, buffer, write_mime,
					  SER_FLAGS_NONE)))
			rc = breq->on_error(event, hcon, name, request, rc);
		else
			rc = http_con_send_response(hcon, status, headers,
						    close_header, buffer,
						    write_mime);

		FREE_NULL_BUFFER(buffer);
	} else if (reply && !parser) {
		const buf_t body = SHADOW_BUF_INITIALIZER(reply, reply_bytes);

		xassert(breq->reply_type == DATA_PARSER_TYPE_INVALID);

		rc = http_con_send_response(hcon, status, headers, close_header,
					    &body, MIME_TYPE_TEXT);
	} else {
		rc = http_con_send_response(hcon, status, headers, close_header,
					    NULL, MIME_TYPE_TEXT);
	}

	return rc;
}
