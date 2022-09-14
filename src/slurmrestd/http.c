/*****************************************************************************\
 *  http.c - handling HTTP
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

#include <ctype.h>
#include <http_parser.h>
#include <limits.h>
#include <sys/socket.h>
#include <unistd.h>

#include "slurm/slurm.h"

#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/net.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmrestd/http.h"
#include "src/slurmrestd/rest_auth.h"

#define CRLF "\r\n"
#define MAGIC 0xDFAFFEEF

/* return magic number 2 to close the connection */
#define HTTP_PARSER_RETURN_ERROR 1

#define MAGIC_REQUEST_T 0xdbadaaaf
/* Data to handed around by http_parser to call backs */
typedef struct {
	int magic;
	/* URL path requested by client */
	const char *path;
	/* URL query parameters by client */
	const char *query;
	/* list of each header received (to be handed to callback) */
	List headers;
	/* state tracking of last header received */
	char *last_header;
	/* client requested to keep_alive header or -1 to disable */
	int keep_alive;
	/* RFC7230-6.1 "Connection: Close" */
	bool connection_close;
	int expect; /* RFC7231-5.1.1 expect requested */
	/* Connection context */
	http_context_t *context;
	/* Body of request (may be NULL) */
	char *body;
	/* if provided: expected body length to process or 0 */
	size_t expected_body_length;
	size_t body_length;
	const char *body_encoding; //TODO: implement detection of this
	const char *content_type;
	const char *accept;
} request_t;

/* default keep_alive value which appears to be implementation specific */
static int DEFAULT_KEEP_ALIVE = 5; //default to 5s to match apache2

static int _send_reject(const http_parser *parser,
			http_status_code_t status_code);

extern void free_http_header(http_header_entry_t *header)
{
	xfree(header->name);
	xfree(header->value);
	xfree(header);
}

static void _free_http_header(void *header)
{
	free_http_header(header);
}

static void _free_request_t(request_t *request)
{
	if (!request)
		return;

	xassert(request->magic == MAGIC_REQUEST_T);
	request->magic = ~MAGIC_REQUEST_T;
	FREE_NULL_LIST(request->headers);
	xfree(request->path);
	xfree(request->query);
	xfree(request->last_header);
	xfree(request->content_type);
	xfree(request->accept);
	xfree(request->body);
	xfree(request->body_encoding);
	request->body_length = 0;
	xfree(request);
}

static void _http_parser_url_init(struct http_parser_url *url)
{
#if (HTTP_PARSER_VERSION_MAJOR == 2 && HTTP_PARSER_VERSION_MINOR >= 6) || \
	(HTTP_PARSER_VERSION_MAJOR > 2)
	http_parser_url_init(url);
#else
	/*
	 * Explicit init was only added with 2.6.0
	 * https://github.com/nodejs/http-parser/pull/225
	 */
	memset(url, 0, sizeof(*url));
#endif
}

static int _on_message_begin(http_parser *parser)
{
	log_flag(NET, "%s: stub called", __func__);
	return 0;
}

static int _on_url(http_parser *parser, const char *at, size_t length)
{
	struct http_parser_url url;
	request_t *request = parser->data;
	xassert(request->magic == MAGIC_REQUEST_T);
	xassert(request->path == NULL);

	_http_parser_url_init(&url);

	if (http_parser_parse_url(at, length, false, &url)) {
		if (strnlen(at, length) == length)
			error("%s: [%s] Invalid non-null terminated URL",
			      __func__, request->context->con->name);
		else
			error("%s: [%s] Invalid format for URL: %s",
			      __func__, request->context->con->name, at);

		return 1;
	}

	if (url.field_set & (1 << UF_SCHEMA))
		log_flag(NET, "%s: [%s] URL Schema currently not supported",
			 __func__, request->context->con->name);
	if (url.field_set & (1 << UF_HOST))
		log_flag(NET, "%s: [%s] URL host currently not supported",
			 __func__, request->context->con->name);
	if (url.field_set & (1 << UF_PORT))
		log_flag(NET, "%s: [%s] URL port currently not supported",
			 __func__, request->context->con->name);
	if (url.field_set & (1 << UF_PATH)) {
		xassert(url.field_data[UF_PATH].len <= length);
		request->path = xstrndup(at + url.field_data[UF_PATH].off,
					 url.field_data[UF_PATH].len);
	}
	if (url.field_set & (1 << UF_QUERY)) {
		xassert(url.field_data[UF_QUERY].len <= length);
		request->query = xstrndup(at + url.field_data[UF_QUERY].off,
					  url.field_data[UF_QUERY].len);
	}
	if (url.field_set & (1 << UF_FRAGMENT))
		log_flag(NET, "%s: [%s] URL fragment currently not supported",
			 __func__, request->context->con->name);
	if (url.field_set & (1 << UF_USERINFO))
		log_flag(NET, "%s: [%s] URL user currently not supported",
			 __func__, request->context->con->name);

	debug("%s: [%s] url path: %s query: %s",
	      __func__, request->context->con->name, request->path,
	      request->query);

	return 0;
}

static int _on_status(http_parser *parser, const char *at, size_t length)
{
	log_flag(NET, "%s: stub called", __func__);
	return 0;
}

static int _on_header_field(http_parser *parser, const char *at, size_t length)
{
	request_t *request = parser->data;
	xassert(request->magic == MAGIC_REQUEST_T);
	xassert(request->last_header == NULL);
	xfree(request->last_header);
	request->last_header = xstrndup(at, length);
	return 0;
}

static int _on_header_value(http_parser *parser, const char *at, size_t length)
{
	request_t *request = parser->data;
	http_header_entry_t *buffer;

	xassert(request->magic == MAGIC_REQUEST_T);

	if (!request->last_header) {
		error("%s: [%s] received invalid empty header",
		      __func__, request->context->con->name);
		return SLURM_COMMUNICATIONS_RECEIVE_ERROR;
	}

	buffer = xmalloc(sizeof(*buffer));
	buffer->name = request->last_header;
	request->last_header = NULL;
	buffer->value = xstrndup(at, length);

	/* trim header field-name per rfc2616:4.2 */
	xstrtrim(buffer->name);

	list_append(request->headers, buffer);
	log_flag(NET, "%s: [%s] Header: %s Value: %s",
		 __func__, request->context->con->name, buffer->name,
		 buffer->value);

	/* Watch for connection headers */
	if (!xstrcasecmp(buffer->name, "Connection")) {
		if (!xstrcasecmp(buffer->value, "Keep-Alive")) {
			if (request->keep_alive == -1)
				request->keep_alive = DEFAULT_KEEP_ALIVE;
		} else if (!xstrcasecmp(buffer->value, "Close")) {
			request->connection_close = true;
		} else {
			error("%s: [%s] ignoring unsupported header request: %s",
			      __func__, request->context->con->name,
			      buffer->value);
		}
	} else if (!xstrcasecmp(buffer->name, "Keep-Alive")) {
		int ibuffer = atoi(buffer->value);
		//FIXME: figure out correct bounds!
		if (ibuffer > 1) {
			request->keep_alive = ibuffer;
		} else {
			error("%s: [%s] invalid Keep-Alive value %s",
			      __func__, request->context->con->name,
			      buffer->value);
			return 1;
		}
	} else if (!xstrcasecmp(buffer->name, "Content-Type")) {
		xfree(request->content_type);
		request->content_type = xstrdup(buffer->value);
	} else if (!xstrcasecmp(buffer->name, "Content-Type")) {
		xfree(request->content_type);
		request->content_type = xstrdup(buffer->value);
	} else if (!xstrcasecmp(buffer->name, "Content-Length")) {
		/* Use signed buffer to catch if negative length is provided */
		ssize_t cl;

		if ((sscanf(buffer->value, "%zd", &cl) != 1) || (cl < 0))
			return _send_reject(
				parser, HTTP_STATUS_CODE_ERROR_NOT_ACCEPTABLE);

		request->expected_body_length = cl;
	} else if (!xstrcasecmp(buffer->name, "Accept")) {
		xfree(request->accept);
		request->accept = xstrdup(buffer->value);
	} else if (!xstrcasecmp(buffer->name, "Expect")) {
		if (!xstrcasecmp(buffer->value, "100-continue"))
			request->expect = 100;
		else
			return _send_reject(parser,
				HTTP_STATUS_CODE_ERROR_EXPECTATION_FAILED);
	} else if (!xstrcasecmp(buffer->name, "Transfer-Encoding")) {
		/* Transfer encoding is not allowed */
		return _send_reject(parser,
				    HTTP_STATUS_CODE_ERROR_NOT_ACCEPTABLE);
	} else if (!xstrcasecmp(buffer->name, "Content-Encoding")) {
		/* Content encoding is not allowed */
		return _send_reject(parser,
				    HTTP_STATUS_CODE_ERROR_NOT_ACCEPTABLE);
	}
	return 0;
}

/*
 * Note: Special return rules for this callback
 * return 0 for SUCCESS
 * return 1 to tell parser to not expect a body.
 * return 2 to tell parser to not expect a body or
 * 	anything else from this HTTP request.
 * return all others to indicate failure to parse
 */
static int _on_headers_complete(http_parser *parser)
{
	request_t *request = parser->data;
	xassert(request->magic == MAGIC_REQUEST_T);

	if (parser->http_major == 1 && parser->http_minor == 0) {
		log_flag(NET, "%s: [%s] HTTP/1.0 connection",
			 __func__, request->context->con->name);

		/* 1.0 defaults to close w/o keep_alive */
		if (!request->keep_alive)
			request->connection_close = true;
	} else if (parser->http_major == 1 && parser->http_minor == 1) {
		log_flag(NET, "%s: [%s] HTTP/1.1 connection",
			 __func__, request->context->con->name);

		/* keep alive is assumed for 1.1 */
		if (request->keep_alive == -1)
			request->keep_alive = DEFAULT_KEEP_ALIVE;
	} else {
		error("%s: [%s] unsupported HTTP/%d.%d",
		      __func__, request->context->con->name,
		      parser->http_major, parser->http_minor);
		/* notify http_parser of failure */
		return 10;
	}

	if (((parser->http_major == 1) && (parser->http_minor >= 1)) ||
	     (parser->http_major > 1)) {
		if ((parser->method == HTTP_REQUEST_POST) &&
		    (request->expected_body_length <= 0)) {
			(void) _send_reject(parser,
				HTTP_STATUS_CODE_ERROR_LENGTH_REQUIRED);
			/* notify http_parser of failure */
			return 10;
		}

		if (request->expect) {
			send_http_response_args_t args = {
				.con = request->context->con,
				.http_major = parser->http_major,
				.http_minor = parser->http_minor,
				.status_code = request->expect,
				.body_length = 0,
			};

			if (send_http_response(&args))
				return 10;
		}
	}

	return 0;
}

#define MAX_BODY_BYTES 52428800 /* 50MB */
static int _on_body(http_parser *parser, const char *at, size_t length)
{
	request_t *request = parser->data;
	xassert(request->magic == MAGIC_REQUEST_T);

	log_flag_hex(NET_RAW, at, length, "%s: [%s] received HTTP body",
	       __func__, request->context->con->name);

	if (request->body) {
		size_t nlength = length + request->body_length;

		xassert(request->body_length > 0);
		xassert(request->body_length <= MAX_BODY_BYTES);
		xassert((request->body_length + 1) == xsize(request->body));

		if (nlength > MAX_BODY_BYTES)
			goto no_mem;

		if (request->expected_body_length &&
		    (nlength > request->expected_body_length))
			goto no_mem;

		if (!try_xrealloc(request->body, (nlength + 1)))
			goto no_mem;

		memmove((request->body + request->body_length), at, length);
		request->body_length += length;
	} else {
		if ((length >= MAX_BODY_BYTES) ||
		    (request->expected_body_length &&
		     (length > request->expected_body_length)) ||
		    !(request->body = try_xmalloc(length + 1)))
			goto no_mem;

		request->body_length = length;
		memmove(request->body, at, length);
	}

	/* final byte must in body must always be NULL terminated */
	xassert(!request->body[request->body_length]);
	request->body[request->body_length] = '\0';

	log_flag(NET, "%s: [%s] received %zu bytes for HTTP body length %zu/%zu bytes",
		 __func__, request->context->con->name, length,
		 request->body_length, request->expected_body_length);

	return 0;

no_mem:
	/* total body was way too large to store */
	return _send_reject(parser, HTTP_STATUS_CODE_ERROR_ENTITY_TOO_LARGE);
}

/*
 * Create rfc2616 formatted header
 * TODO: add more sanity checks
 * IN name header name
 * IN value header value
 * RET formatted string (must xfree)
 * */
static char *_fmt_header(const char *name, const char *value)
{
	return xstrdup_printf("%s: %s"CRLF, name, value);
}

/*
 * Create and write formatted header
 * IN request HTTP request
 * IN name header name
 * IN value header value
 * RET formatted string (must xfree)
 * */
static int _write_fmt_header(con_mgr_fd_t *con, const char *name,
			     const char *value)
{
	const char *buffer = _fmt_header(name, value);
	int rc = con_mgr_queue_write_fd(con, buffer, strlen(buffer));
	xfree(buffer);
	return rc;
}

/*
 * Create rfc2616 formatted numerical header
 * TODO: add sanity checks
 * IN name header name
 * IN value header value
 * RET formatted string (must xfree)
 * */
static char *_fmt_header_num(const char *name, size_t value)
{
	return xstrdup_printf("%s: %zu" CRLF, name, value);
}

extern int send_http_connection_close(http_context_t *ctxt)
{
	return _write_fmt_header(ctxt->con, "Connection", "Close");
}

/*
 * Create and write formatted numerical header
 * IN request HTTP request
 * IN name header name
 * IN value header value
 * RET formatted string (must xfree)
 * */
static int _write_fmt_num_header(con_mgr_fd_t *con, const char *name,
				 size_t value)
{
	const char *buffer = _fmt_header_num(name, value);
	int rc = con_mgr_queue_write_fd(con, buffer, strlen(buffer));
	xfree(buffer);
	return rc;
}

extern int send_http_response(const send_http_response_args_t *args)
{
	char *buffer = NULL;
	int rc = SLURM_SUCCESS;
	xassert(args->status_code != HTTP_STATUS_NONE);
	xassert(args->body_length == 0 || (args->body_length && args->body));

	log_flag(NET, "%s: [%s] sending response %u: %s",
	       __func__, args->con->name,
	       args->status_code,
	       get_http_status_code_string(args->status_code));

	/* send rfc2616 response */
	xstrfmtcat(buffer, "HTTP/%d.%d %d %s"CRLF,
		   args->http_major, args->http_minor, args->status_code,
		   get_http_status_code_string(args->status_code));

	rc = con_mgr_queue_write_fd(args->con, buffer, strlen(buffer));
	xfree(buffer);

	if (rc)
		return rc;

	/* send along any requested headers */
	if (args->headers) {
		ListIterator itr = list_iterator_create(args->headers);
		http_header_entry_t *header = NULL;
		while ((header = list_next(itr))) {
			if ((rc = _write_fmt_header(args->con, header->name,
						    header->value)))
				break;
		}
		list_iterator_destroy(itr);

		if (rc)
			return rc;
	}

	if (args->body && args->body_length) {
		/* RFC7230-3.3.2 limits response of Content-Length */
		if ((args->status_code < 100) ||
		    ((args->status_code >= 200) &&
		     (args->status_code != 204))) {
			if ((rc = _write_fmt_num_header(args->con,
				"Content-Length", args->body_length))) {
				return rc;
			}
		}

		if (args->body_encoding &&
		    (rc = _write_fmt_header(
			     args->con, "Content-Type", args->body_encoding)))
			return rc;

		if ((rc = con_mgr_queue_write_fd(args->con, CRLF,
						 strlen(CRLF))))
			return rc;

		log_flag(NET, "%s: [%s] rc=%s(%u) sending body:\n%s",
			 __func__, args->con->name,
			 get_http_status_code_string(args->status_code),
			 args->status_code, args->body);

		if ((rc = con_mgr_queue_write_fd(args->con, args->body,
						 args->body_length)))
			return rc;
	} else if (((args->status_code >= 100) && (args->status_code < 200)) ||
		   (args->status_code == 204) ||
		   (args->status_code == 304)) {
		/*
		 * RFC2616 requires empty line after headers for return code
		 * that "MUST NOT" include a message body
		 */
		if ((rc = con_mgr_queue_write_fd(args->con, CRLF,
						 strlen(CRLF))))
			return rc;
	}

	return rc;
}

static int _send_reject(const http_parser *parser,
			http_status_code_t status_code)
{
	request_t *request = parser->data;
	xassert(request->magic == MAGIC_REQUEST_T);

	send_http_response_args_t args = {
		.con = request->context->con,
		.http_major = parser->http_major,
		.http_minor = parser->http_minor,
		.status_code = status_code,
		.body_length = 0,
		.headers = list_create(_free_http_header),
	};

	/* If we don't have a requested client version, default to 0.9 */
	if ((args.http_major == 0) && (args.http_minor == 0))
		args.http_minor = 9;

	/* Ignore response since this connection is already dead */
	(void) send_http_response(&args);
	FREE_NULL_LIST(args.headers);

	if (request->connection_close ||
	    ((parser->http_major == 1) && (parser->http_minor >= 1)) ||
	     (parser->http_major > 1))
		send_http_connection_close(request->context);

	/* ensure connection gets closed */
	(void) con_mgr_queue_close_fd(request->context->con);

	return HTTP_PARSER_RETURN_ERROR;
}

static int _on_message_complete_request(http_parser *parser,
				       http_request_method_t method,
				       request_t *request)
{
	int rc;
	on_http_request_args_t args = {
		.method = method,
		.headers = request->headers,
		.path = request->path,
		.query = request->query,
		.context = request->context,
		.http_major = parser->http_major,
		.http_minor = parser->http_minor,
		.content_type = request->content_type,
		.accept = request->accept,
		.body = request->body,
		.body_length = request->body_length,
		.body_encoding = request->body_encoding
	};

	xassert(request->magic == MAGIC_REQUEST_T);

	if ((rc = request->context->on_http_request(&args))) {
		log_flag(NET, "%s: [%s] on_http_request rejected: %s",
			 __func__, request->context->con->name,
			 slurm_strerror(rc));
		return HTTP_PARSER_RETURN_ERROR;
	} else
		return SLURM_SUCCESS;
}

static int _on_message_complete(http_parser *parser)
{
	int rc;
	request_t *request = parser->data;
	http_request_method_t method = HTTP_REQUEST_INVALID;

	xassert(request->magic == MAGIC_REQUEST_T);

	/* skip if there is already an known error */
	if (parser->http_errno)
		return _send_reject(parser, HTTP_STATUS_CODE_ERROR_BAD_REQUEST);
	if (parser->upgrade)
		return _send_reject(parser,
				    HTTP_STATUS_CODE_ERROR_METHOD_NOT_ALLOWED);
	if (!request->path) {
		error("%s: [%s] message complete with empty URL path",
		      __func__, request->context->con->name);

		return _send_reject(parser, HTTP_STATUS_CODE_ERROR_NOT_FOUND);
	}

	switch (parser->method) {
	case HTTP_GET:
		method = HTTP_REQUEST_GET;
		break;
	case HTTP_POST:
		method = HTTP_REQUEST_POST;
		break;
	case HTTP_PUT:
		method = HTTP_REQUEST_PUT;
		break;
	case HTTP_DELETE:
		method = HTTP_REQUEST_DELETE;
		break;
	case HTTP_OPTIONS:
		method = HTTP_REQUEST_OPTIONS;
		break;
	case HTTP_HEAD:
		method = HTTP_REQUEST_HEAD;
		break;
	case HTTP_PATCH:
		method = HTTP_REQUEST_PATCH;
		break;
	case HTTP_TRACE:
		method = HTTP_REQUEST_TRACE;
		break;
	default:
		error("%s: [%s] unsupported HTTP method: %s",
		      __func__, request->context->con->name,
		      http_method_str(parser->method));

		return _send_reject(parser,
				    HTTP_STATUS_CODE_ERROR_METHOD_NOT_ALLOWED);
	}

	if ((request->expected_body_length > 0) &&
	    (request->expected_body_length != request->body_length)) {
		error("%s: [%s] Content-Length %zu and received body length %zu mismatch",
		      __func__, request->context->con->name,
		      request->expected_body_length, request->body_length);
		return _send_reject(parser, HTTP_STATUS_CODE_ERROR_BAD_REQUEST);
	}

	if ((rc = _on_message_complete_request(parser, method, request)))
		return rc;

	if (request->keep_alive) {
		//TODO: implement keep alive correctly
		log_flag(NET, "%s: [%s] keep alive not currently implemented",
			 __func__, request->context->con->name);
	}

	if (!request->connection_close) {
		/*
		 * Create a new HTTP request to allow persistent connections to
		 * continue but without inheirting previous requests.
		 */
		request_t *nrequest = xmalloc(sizeof(*request));
		nrequest->magic = MAGIC_REQUEST_T;
		nrequest->headers = list_create(_free_http_header);
		nrequest->context = request->context;
		request->context->request = nrequest;
		parser->data = nrequest;
		_free_request_t(request);
	} else {
		/* Notify client that this connection will be closed now */
		if (request->connection_close)
			send_http_connection_close(request->context);

		con_mgr_queue_close_fd(request->context->con);

		request->context->request = NULL;
		_free_request_t(request);
		parser->data = NULL;
	}

	return 0;
}

static int _on_chunk_header(http_parser *parser)
{
	log_flag(NET, "%s: stub called", __func__);
	return 0;
}

static int _on_chunk_complete(http_parser *parser)
{
	log_flag(NET, "%s: stub called", __func__);
	return 0;
}

extern int parse_http(con_mgr_fd_t *con, void *x)
{
	http_context_t *context = (http_context_t *) x;
	static const http_parser_settings settings = {
		.on_url = _on_url,
		.on_message_begin = _on_message_begin,
		.on_status = _on_status,
		.on_header_field = _on_header_field,
		.on_header_value = _on_header_value,
		.on_headers_complete = _on_headers_complete,
		.on_body = _on_body,
		.on_message_complete = _on_message_complete,
		.on_chunk_header = _on_chunk_header,
		.on_chunk_complete = _on_chunk_complete
	};
	int rc = SLURM_SUCCESS;
	buf_t *buffer = con->in;
	request_t *request = context->request;
	http_parser *parser = context->parser;

	xassert(con->name);
	xassert(con->name[0] != '\0');
	xassert(size_buf(buffer));
	xassert(context->magic == MAGIC);

	if (!request) {
		/* Connection has already been closed */
		rest_auth_g_clear();
		log_flag(NET, "%s: [%s] Rejecting continued HTTP connection",
			 __func__, con->name);
		return SLURM_UNEXPECTED_MSG_ERROR;
	}

	xassert(request->magic == MAGIC_REQUEST_T);
	if (request->context)
		xassert(request->context == context);
	request->context = context;

	/* make sure there is no auth context inherited */
	rest_auth_g_clear();

	parser->data = request;

	log_flag(NET, "%s: [%s] Accepted HTTP connection", __func__, con->name);

	size_t bytes_parsed = http_parser_execute(parser, &settings,
						  get_buf_data(buffer),
						  size_buf(buffer));

	log_flag(NET, "%s: [%s] parsed %zu/%u bytes",
		 __func__, con->name, bytes_parsed, size_buf(buffer));

	if (bytes_parsed > 0)
		set_buf_offset(buffer, bytes_parsed);
	else if (parser->http_errno) {
		error("%s: [%s] unexpected HTTP error %s: %s",
		      __func__, con->name, http_errno_name(parser->http_errno),
		      http_errno_description(parser->http_errno));
		rc = _send_reject(parser, HTTP_STATUS_CODE_ERROR_BAD_REQUEST);
	} else if (parser->upgrade) {
		log_flag(NET, "%s: [%s] HTTP Upgrade currently not supported",
			 __func__, con->name);
		rc = SLURM_UNEXPECTED_MSG_ERROR;
	}

	rest_auth_g_clear();

	return rc;
}

extern parsed_host_port_t *parse_host_port(const char *str)
{
	struct http_parser_url url;
	parsed_host_port_t *parsed;

	if (!str || str[0] == '\0') {
		error("%s: invalid empty host string", __func__);
		return NULL;
	}

	/* Allow :::PORT and :PORT to default to in6addr_any */
	if (str[0] == ':') {
		char *pstr = xstrdup(str);
		pstr[0] = ' ';

		if (pstr[1] == ':' && pstr[2] == ':') {
			pstr[1] = ' ';
			pstr[2] = ' ';
		}

		/* remove any whitespace */
		xstrtrim(pstr);

		parsed = xmalloc(sizeof(*parsed));
		parsed->host = xstrdup("::");
		parsed->port = pstr;
		return parsed;
	}

	/* Only useful for RFC3986 addresses */
	_http_parser_url_init(&url);

	if (http_parser_parse_url(str, strlen(str), true, &url)) {
		error("%s: invalid host string: %s", __func__, str);
		return NULL;
	}

	parsed = xmalloc(sizeof(*parsed));

	if (url.field_set & (1 << UF_HOST))
		parsed->host = xstrndup(str + url.field_data[UF_HOST].off,
					url.field_data[UF_HOST].len);
	if (url.field_set & (1 << UF_PORT))
		parsed->port = xstrndup(str + url.field_data[UF_PORT].off,
					url.field_data[UF_PORT].len);

	if (parsed->host && parsed->port)
		log_flag(NET, "%s: parsed: %s -> %s:%s",
			 __func__, str, parsed->host, parsed->port);

	return parsed;
}

extern void free_parse_host_port(parsed_host_port_t *parsed)
{
	if (!parsed)
		return;

	xfree(parsed->host);
	xfree(parsed->port);
	xfree(parsed);
}

static http_context_t *_http_context_new(void)
{
	http_context_t *context = xmalloc(sizeof(*context));
	http_parser *parser = xmalloc(sizeof(*parser));

	context->magic = MAGIC;
	http_parser_init(parser, HTTP_REQUEST);
	context->parser = parser;

	context->auth = NULL;

	return context;
}

/* find operator against http_header_entry_t */
static int _http_header_find_key(void *x, void *y)
{
	http_header_entry_t *entry = (http_header_entry_t *)x;
	const char *key = (const char *)y;
	xassert(entry->name);

	if (key == NULL)
		return 0;
	/* case insensitive compare per rfc2616:4.2 */
	if (entry->name && !xstrcasecmp(entry->name, key))
		return 1;
	else
		return 0;
}

extern const char *find_http_header(List headers, const char *name)
{
	http_header_entry_t *header = NULL;

	if (!headers || !name)
		return NULL;

	header = (http_header_entry_t *)list_find_first(
		headers, _http_header_find_key, (void *)name);

	if (header)
		return header->value;
	else
		return NULL;
}

extern http_context_t *setup_http_context(con_mgr_fd_t *con,
					  on_http_request_t on_http_request)
{
	http_context_t *context = _http_context_new();
	request_t *request = xmalloc(sizeof(*request));

	xassert(context->magic == MAGIC);
	xassert(!context->con);
	xassert(!context->request);
	request->magic = MAGIC_REQUEST_T;
	context->con = con;
	context->on_http_request = on_http_request;
	context->request = request;
	request->headers = list_create(_free_http_header);

	return context;
}

extern void on_http_connection_finish(void *ctxt)
{
	http_context_t *context = (http_context_t *) ctxt;

	if (!context)
		return;
	xassert(context->magic == MAGIC);

	xfree(context->parser);
	_free_request_t(context->request);
	rest_auth_g_free(context->auth);
	context->magic = ~MAGIC;
	xfree(context);
}
