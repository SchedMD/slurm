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

#include <limits.h>
#include <sys/socket.h>
#include <unistd.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/http.h"
#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/pack.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/conmgr/conmgr.h"

#include "src/interfaces/http_parser.h"

#include "src/slurmrestd/http.h"
#include "src/slurmrestd/rest_auth.h"

#define CRLF "\r\n"
#define MAGIC 0xDFAFFEEF
#define MAX_BODY_BYTES 52428800 /* 50MB */

/* Data to handed around by http_parser to call backs */
typedef struct {
	/* Requested URL */
	url_t url;
	/* Request HTTP method */
	http_request_method_t method;
	/* list of each header received (to be handed to callback) */
	list_t *headers;
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

	struct {
		uint16_t major;
		uint16_t minor;
	} http_version;
} request_t;

typedef struct http_context_s {
	int magic; /* MAGIC */
	/* reference to assigned connection */
	conmgr_fd_ref_t *ref;
	/* assigned connection */
	conmgr_fd_t *con;
	/* Authentication context (auth_context_type_t) */
	void *auth;
	/* callback to call on each HTTP request */
	on_http_request_t on_http_request;
	/* http parser plugin state */
	http_parser_state_t *parser;
	/* http request_t */
	request_t request;
} http_context_t;

/* default keep_alive value which appears to be implementation specific */
static int DEFAULT_KEEP_ALIVE = 5; //default to 5s to match apache2

static int _send_reject(http_context_t *context, http_status_code_t status_code,
			slurm_err_t error_number);

static int _valid_http_version(uint16_t major, uint16_t minor)
{
	if ((major == 0) && (minor == 0))
		return SLURM_SUCCESS;

	if ((major == 1) && ((minor == 0) || (minor == 1)))
		return SLURM_SUCCESS;

	return ESLURM_HTTP_UNSUPPORTED_VERSION;
}

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

static void _request_init(http_context_t *context)
{
	request_t *request = &context->request;

	xassert(request);
	xassert(context->magic == MAGIC);

	*request = (request_t) {
		.url = URL_INITIALIZER,
		.method = HTTP_REQUEST_INVALID,
		.context = context,
		.keep_alive = -1,
	};

	request->headers = list_create(_free_http_header);
}

static void _request_free_members(http_context_t *context)
{
	request_t *request = &context->request;

	xassert(context->magic == MAGIC);
	xassert(request->context == context);

	url_free_members(&request->url);
	FREE_NULL_LIST(request->headers);
	xfree(request->last_header);
	xfree(request->content_type);
	xfree(request->accept);
	xfree(request->body);
	xfree(request->body_encoding);
}

/* reset state of request */
static void _request_reset(http_context_t *context)
{
	xassert(context->magic == MAGIC);

	FREE_NULL_REST_AUTH(context->auth);

	_request_free_members(context);
	_request_init(context);
}

static int _on_request(const http_parser_request_t *req, void *arg)
{
	http_context_t *context = arg;
	request_t *request = &context->request;
	int rc = EINVAL;

	xassert(context->magic == MAGIC);

	request->http_version.major = req->http_version.major;
	request->http_version.minor = req->http_version.minor;
	request->method = req->method;
	url_copy_members(&request->url, req->url);

	/* Default to http if none given */
	if (request->url.scheme == URL_SCHEME_INVALID)
		request->url.scheme = URL_SCHEME_HTTP;

	if (!request->url.path) {
		error("%s: [%s] Rejecting request with empty URL path",
		      __func__, conmgr_con_get_name(context->ref));

		return _send_reject(context, HTTP_STATUS_CODE_ERROR_NOT_FOUND,
				    ESLURM_URL_INVALID_PATH);
	}

	if (req->method == HTTP_REQUEST_INVALID)
		return _send_reject(context,
				    HTTP_STATUS_CODE_ERROR_METHOD_NOT_ALLOWED,
				    ESLURM_HTTP_INVALID_METHOD);

	if ((rc = _valid_http_version(req->http_version.major,
				      req->http_version.minor))) {
		error("%s: [%s] rejecting unsupported HTTP %hu.%hu version: %s",
		      __func__, conmgr_con_get_name(context->ref),
		      req->http_version.major, req->http_version.minor,
		      slurm_strerror(rc));
		return rc;
	}

	if ((request->url.scheme != URL_SCHEME_HTTP) &&
	    (request->url.scheme != URL_SCHEME_HTTPS)) {
		error("%s: [%s] URL scheme not supported: %s",
		      __func__, conmgr_con_get_name(context->ref),
		      url_get_scheme_string(request->url.scheme));
		return ESLURM_URL_UNSUPPORTED_SCHEME;
	}

	if ((request->url.scheme == URL_SCHEME_HTTPS) &&
	    !conmgr_fd_is_tls(context->ref)) {
		error("%s: [%s] URL requested HTTPS but connection is not TLS wrapped",
		      __func__, conmgr_con_get_name(context->ref));
		return ESLURM_TLS_REQUIRED;
	}

	return SLURM_SUCCESS;
}

static int _on_header(const http_parser_header_t *header, void *arg)
{
	http_context_t *context = arg;
	request_t *request = &context->request;
	http_header_entry_t *entry = NULL;

	xassert(context->magic == MAGIC);

	log_flag(NET, "%s: [%s] Header: %s Value: %s",
		 __func__, conmgr_con_get_name(context->ref), header->name,
		 header->value);

	/* Add copy to list of headers */
	entry = xmalloc(sizeof(*entry));
	entry->name = xstrdup(header->name);
	entry->value = xstrdup(header->value);
	list_append(request->headers, entry);

	/* Watch for connection headers */
	if (!xstrcasecmp(header->name, "Connection")) {
		if (!xstrcasecmp(header->value, "Keep-Alive")) {
			if (request->keep_alive == -1)
				request->keep_alive = DEFAULT_KEEP_ALIVE;
		} else if (!xstrcasecmp(header->value, "Close")) {
			request->connection_close = true;
		} else {
			warning("%s: [%s] ignoring unsupported header request: Connection: %s",
			      __func__,
			      conmgr_con_get_name(context->ref),
			      header->value);
		}
	} else if (!xstrcasecmp(header->name, "Keep-Alive")) {
		int ibuffer = atoi(header->value);
		if (ibuffer > 1) {
			request->keep_alive = ibuffer;
		} else {
			error("%s: [%s] invalid Keep-Alive value %s",
			      __func__, conmgr_fd_get_name(context->con),
			      header->value);
			return _send_reject(
				context, HTTP_STATUS_CODE_ERROR_NOT_ACCEPTABLE,
				ESLURM_HTTP_UNSUPPORTED_KEEP_ALIVE);
		}
	} else if (!xstrcasecmp(header->name, "Content-Type")) {
		xfree(request->content_type);
		request->content_type = xstrdup(header->value);
	} else if (!xstrcasecmp(header->name, "Content-Type")) {
		xfree(request->content_type);
		request->content_type = xstrdup(header->value);
	} else if (!xstrcasecmp(header->name, "Content-Length")) {
		/* Use signed buffer to catch if negative length is provided */
		ssize_t cl;

		if ((sscanf(header->value, "%zd", &cl) != 1) || (cl < 0))
			return _send_reject(
				context, HTTP_STATUS_CODE_ERROR_NOT_ACCEPTABLE,
				ESLURM_HTTP_INVALID_CONTENT_LENGTH);
		request->expected_body_length = cl;
	} else if (!xstrcasecmp(header->name, "Accept")) {
		xfree(request->accept);
		request->accept = xstrdup(header->value);
	} else if (!xstrcasecmp(header->name, "Expect")) {
		if (!xstrcasecmp(header->value, "100-continue"))
			request->expect = 100;
		else
			return _send_reject(
				context,
				HTTP_STATUS_CODE_ERROR_EXPECTATION_FAILED,
				ESLURM_HTTP_UNSUPPORTED_EXPECT);
	} else if (!xstrcasecmp(header->name, "Transfer-Encoding")) {
		/* Transfer encoding is not allowed */
		return _send_reject(context,
				    HTTP_STATUS_CODE_ERROR_NOT_ACCEPTABLE,
				    ESLURM_HTTP_INVALID_TRANSFER_ENCODING);
	} else if (!xstrcasecmp(header->name, "Content-Encoding")) {
		/* Content encoding is not allowed */
		return _send_reject(context,
				    HTTP_STATUS_CODE_ERROR_NOT_ACCEPTABLE,
				    ESLURM_HTTP_INVALID_CONTENT_ENCODING);
	} else if (!xstrcasecmp(header->name, "Upgrade")) {
		/* Upgrades are not allowed */
		return _send_reject(context,
				    HTTP_STATUS_CODE_ERROR_NOT_ACCEPTABLE,
				    ESLURM_HTTP_UNSUPPORTED_UPGRADE);
	}

	return SLURM_SUCCESS;
}

static int _on_headers_complete(void *arg)
{
	http_context_t *context = arg;
	request_t *request = &context->request;

	xassert(context->magic == MAGIC);

	if ((request->http_version.major == 1) &&
	    (request->http_version.minor == 0)) {
		log_flag(NET, "%s: [%s] HTTP/1.0 connection",
			 __func__, conmgr_con_get_name(context->ref));

		/* 1.0 defaults to close w/o keep_alive */
		if (!request->keep_alive)
			request->connection_close = true;
	} else if ((request->http_version.major == 1) &&
		   (request->http_version.minor == 1)) {
		log_flag(NET, "%s: [%s] HTTP/1.1 connection",
			 __func__, conmgr_con_get_name(context->ref));

		/* keep alive is assumed for 1.1 */
		if (request->keep_alive == -1)
			request->keep_alive = DEFAULT_KEEP_ALIVE;
	}

	if (!request->http_version.major && request->http_version.minor)
		return SLURM_SUCCESS;

	if ((request->method == HTTP_REQUEST_POST) &&
	    (request->expected_body_length <= 0))
		return _send_reject(context,
				    HTTP_STATUS_CODE_ERROR_LENGTH_REQUIRED,
				    ESLURM_HTTP_INVALID_CONTENT_LENGTH);

	if (request->expect) {
		int rc = EINVAL;
		send_http_response_args_t args = {
			.con = context->con,
			.http_major = request->http_version.major,
			.http_minor = request->http_version.minor,
			.status_code = request->expect,
			.body_length = 0,
		};

		if ((rc = send_http_response(&args)))
			return rc;
	}

	return SLURM_SUCCESS;
}

static int _on_content(const http_parser_content_t *content, void *arg)
{
	http_context_t *context = arg;
	request_t *request = &context->request;
	const void *at = get_buf_data(content->buffer);
	const size_t length = get_buf_offset(content->buffer);

	xassert(context->magic == MAGIC);

	log_flag_hex(NET_RAW, at, length, "%s: [%s] received HTTP content",
	       __func__, conmgr_con_get_name(context->ref));

	if (!request->url.path) {
		error("%s: [%s] rejecting missing path",
		      __func__, conmgr_con_get_name(context->ref));
		return ESLURM_HTTP_UNEXPECTED_REQUEST;
	}

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
		 __func__, conmgr_con_get_name(context->ref), length,
		 request->body_length, request->expected_body_length);

	return 0;

no_mem:
	/* total body was way too large to store */
	return _send_reject(context, HTTP_STATUS_CODE_ERROR_ENTITY_TOO_LARGE,
			    ESLURM_HTTP_INVALID_CONTENT_LENGTH);
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
static int _write_fmt_header(conmgr_fd_t *con, const char *name,
			     const char *value)
{
	char *buffer = _fmt_header(name, value);
	int rc = conmgr_queue_write_data(con, buffer, strlen(buffer));
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
static int _write_fmt_num_header(conmgr_fd_t *con, const char *name,
				 size_t value)
{
	const char *buffer = _fmt_header_num(name, value);
	int rc = conmgr_queue_write_data(con, buffer, strlen(buffer));
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
	       __func__, conmgr_fd_get_name(args->con), args->status_code,
	       get_http_status_code_string(args->status_code));

	/* send rfc2616 response */
	xstrfmtcat(buffer, "HTTP/%d.%d %d %s"CRLF,
		   args->http_major, args->http_minor, args->status_code,
		   get_http_status_code_string(args->status_code));

	rc = conmgr_queue_write_data(args->con, buffer, strlen(buffer));
	xfree(buffer);

	if (rc)
		return rc;

	/* send along any requested headers */
	if (args->headers) {
		list_itr_t *itr = list_iterator_create(args->headers);
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

		if ((rc = conmgr_queue_write_data(args->con, CRLF,
						  strlen(CRLF))))
			return rc;

		log_flag(NET, "%s: [%s] rc=%s(%u) sending body:\n%s",
			 __func__, conmgr_fd_get_name(args->con),
			 get_http_status_code_string(args->status_code),
			 args->status_code, args->body);

		if ((rc = conmgr_queue_write_data(args->con, args->body,
						  args->body_length)))
			return rc;
	} else if (((args->status_code >= 100) && (args->status_code < 200)) ||
		   (args->status_code == 204) ||
		   (args->status_code == 304)) {
		/*
		 * RFC2616 requires empty line after headers for return code
		 * that "MUST NOT" include a message body
		 */
		if ((rc = conmgr_queue_write_data(args->con, CRLF,
						  strlen(CRLF))))
			return rc;
	}

	return rc;
}

static int _send_reject(http_context_t *context, http_status_code_t status_code,
			slurm_err_t error_number)
{
	request_t *request = &context->request;
	send_http_response_args_t args = {
		.con = request->context->con,
		.http_major = request->http_version.major,
		.http_minor = request->http_version.minor,
		.status_code = status_code,
		.body_length = 0,
		.headers = list_create(_free_http_header),
	};

	xassert(context->magic == MAGIC);

	/* If we don't have a requested client version, default to 0.9 */
	if ((args.http_major == 0) && (args.http_minor == 0))
		args.http_minor = 9;

	/* Ignore response since this connection is already dead */
	(void) send_http_response(&args);
	FREE_NULL_LIST(args.headers);

	if (request->connection_close ||
	    _valid_http_version(request->http_version.major,
				request->http_version.minor))
		send_http_connection_close(request->context);

	/* ensure connection gets closed */
	(void) conmgr_queue_close_fd(request->context->con);

	/* reset connection to avoid any possible auth inheritance */
	_request_reset(context);

	return error_number;
}

static int _on_message_complete_request(http_context_t *context)
{
	int rc = EINVAL;
	request_t *request = &context->request;
	on_http_request_args_t args = {
		.method = request->method,
		.headers = request->headers,
		.path = request->url.path,
		.query = request->url.query,
		.context = context,
		.http_major = request->http_version.major,
		.http_minor = request->http_version.minor,
		.content_type = request->content_type,
		.accept = request->accept,
		.body = request->body,
		.body_length = request->body_length,
		.body_encoding = request->body_encoding
	};

	xassert(context->magic == MAGIC);

	if (!(args.con = conmgr_con_link(context->ref)) ||
	    !(args.name = conmgr_con_get_name(args.con))) {
		rc = SLURM_COMMUNICATIONS_MISSING_SOCKET_ERROR;
		log_flag(NET, "%s: connection missing: %s",
			 __func__, slurm_strerror(rc));
	} else if ((rc = context->on_http_request(&args)))
		log_flag(NET, "%s: [%s] on_http_request rejected: %s",
			 __func__, conmgr_con_get_name(context->ref),
			 slurm_strerror(rc));

	conmgr_fd_free_ref(&args.con);
	return rc;
}

static int _on_content_complete(void *arg)
{
	http_context_t *context = arg;
	request_t *request = &context->request;
	int rc = EINVAL;

	xassert(context->magic == MAGIC);

	if ((request->expected_body_length > 0) &&
	    (request->expected_body_length != request->body_length)) {
		error("%s: [%s] Content-Length %zu and received body length %zu mismatch",
		      __func__, conmgr_con_get_name(context->ref),
		      request->expected_body_length, request->body_length);
		return _send_reject(context, HTTP_STATUS_CODE_ERROR_BAD_REQUEST,
				    ESLURM_HTTP_INVALID_CONTENT_LENGTH);
	}

	if ((rc = _on_message_complete_request(context)))
		return rc;

	if (request->keep_alive) {
		//TODO: implement keep alive correctly
		log_flag(NET, "%s: [%s] keep alive not currently implemented",
			 __func__, conmgr_con_get_name(context->ref));
	}

	if (request->connection_close) {
		/* Notify client that this connection will be closed now */
		if (request->connection_close)
			send_http_connection_close(context);

		conmgr_con_queue_close_free(&context->ref);
		context->con = NULL;
	}

	_request_reset(context);

	return SLURM_SUCCESS;
}

extern int parse_http(conmgr_fd_t *con, void *x)
{
	http_context_t *context = (http_context_t *) x;
	static const http_parser_callbacks_t callbacks = {
		.on_request = _on_request,
		.on_header = _on_header,
		.on_headers_complete = _on_headers_complete,
		.on_content = _on_content,
		.on_content_complete = _on_content_complete,
	};
	int rc = SLURM_SUCCESS;
	request_t *request = &context->request;
	ssize_t bytes_parsed = -1;
	buf_t *buffer = NULL;

	xassert(context->magic == MAGIC);
	xassert(context->con);
	xassert(context->ref);
	xassert(request->context == context);

	if (!context->parser &&
	    (rc = http_parser_g_new_parse_request(
		     conmgr_con_get_name(context->ref), &callbacks, context,
		     &context->parser))) {
		log_flag(NET, "%s: [%s] Creating new HTTP parser failed: %s",
			 __func__, conmgr_con_get_name(context->ref),
			 slurm_strerror(rc));
		goto cleanup;
	}

	if ((rc = conmgr_con_shadow_in_buffer(context->ref, &buffer))) {
		log_flag(NET, "%s: [%s] Unable to get HTTP input buffer: %s",
			 __func__, conmgr_con_get_name(context->ref),
			 slurm_strerror(rc));
		goto cleanup;
	}

	/* Set buffer as fully populated */
	set_buf_offset(buffer, size_buf(buffer));

	log_flag(NET, "%s: [%s] Accepted HTTP connection",
		 __func__, conmgr_con_get_name(context->ref));

	rc = http_parser_g_parse_request(context->parser, buffer,
					 &bytes_parsed);

	if (context->ref)
		log_flag(NET, "%s: [%s] parsed %zu/%u bytes: %s",
			 __func__, conmgr_con_get_name(context->ref),
			 bytes_parsed, get_buf_offset(buffer),
			 slurm_strerror(rc));

	if (rc) {
		rc = _send_reject(context, HTTP_STATUS_CODE_SRVERR_INTERNAL,
				  rc);
	} else if (context->ref && (bytes_parsed > 0) &&
		   (rc = conmgr_con_mark_consumed_input_buffer(context->ref,
							       bytes_parsed))) {
		log_flag(NET, "%s: [%s] Input buffer became invalid after parsing: %s",
			 __func__, conmgr_con_get_name(context->ref),
			 slurm_strerror(rc));
		goto cleanup;
	}

cleanup:
	FREE_NULL_BUFFER(buffer);
	return rc;
}

static http_context_t *_http_context_new(void)
{
	http_context_t *context = xmalloc(sizeof(*context));
	context->magic = MAGIC;
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

extern const char *find_http_header(list_t *headers, const char *name)
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

extern http_context_t *setup_http_context(conmgr_fd_t *con,
					  on_http_request_t on_http_request)
{
	http_context_t *context = _http_context_new();

	xassert(context->magic == MAGIC);
	xassert(!context->con);
	context->con = con;
	context->ref = conmgr_fd_new_ref(con);
	context->on_http_request = on_http_request;

	_request_init(context);

	return context;
}

extern void on_http_connection_finish(conmgr_fd_t *con, void *ctxt)
{
	http_context_t *context = (http_context_t *) ctxt;

	if (!context)
		return;
	xassert(context->magic == MAGIC);

	http_parser_g_free_parse_request(&context->parser);

	/* release request */
	_request_free_members(context);

	/* auth should have been released long before now */
	xassert(!context->auth);
	FREE_NULL_REST_AUTH(context->auth);

	conmgr_fd_free_ref(&context->ref);
	context->con = NULL;

	context->magic = ~MAGIC;
	xfree(context);
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
