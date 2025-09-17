/*****************************************************************************\
 *  http_con.c - handling HTTP connections
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

#include "src/common/http_con.h"
#include "src/common/http.h"
#include "src/common/pack.h"
#include "src/common/read_config.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/conmgr/conmgr.h"
#include "src/interfaces/http_parser.h"

#define CRLF "\r\n"
#define MAX_BODY_BYTES 52428800 /* 50MB */
#define MAX_STATUS_BYTES 1024

#define MAGIC 0xab0a8aff

typedef struct http_con_s {
	int magic; /* MAGIC */
	/* reference to assigned connection */
	conmgr_fd_ref_t *con;
	/* True to xfree() this pointer */
	bool free_on_close;
	const http_con_server_events_t *events;
	void *arg; /* arbitrary pointer from caller */
	http_parser_state_t *parser; /* http parser plugin state */
	http_con_request_t request;
} http_con_t;

#define WRITE_EACH_HEADER_MAGIC 0xba3a8aff

typedef struct {
	int magic; /* WRITE_EACH_HEADER_MAGIC */
	int rc;
	conmgr_fd_ref_t *con;
} write_each_header_args_t;

static int _send_reject(http_con_t *hcon, slurm_err_t error_number);

extern const size_t http_con_bytes(void)
{
	return sizeof(http_con_t);
}

static int _valid_http_version(uint16_t major, uint16_t minor)
{
	if ((major == 0) && (minor == 0))
		return SLURM_SUCCESS;

	if ((major == 1) && ((minor == 0) || (minor == 1)))
		return SLURM_SUCCESS;

	return ESLURM_HTTP_UNSUPPORTED_VERSION;
}

static void _request_init(http_con_t *hcon)
{
	http_con_request_t *request = &hcon->request;

	xassert(hcon->magic == MAGIC);
	/* catch memory leak */
	xassert(!request->headers);

	*request = (http_con_request_t) {
		.url = URL_INITIALIZER,
		.method = HTTP_REQUEST_INVALID,
	};

	request->headers = list_create((ListDelF) free_http_header);
}

static void _request_free_members(http_con_t *hcon)
{
	http_con_request_t *request = &hcon->request;

	xassert(hcon->magic == MAGIC);

	url_free_members(&request->url);
	FREE_NULL_LIST(request->headers);
	xfree(request->accept);
	xfree(request->content_type);
	FREE_NULL_BUFFER(request->content);
}

/* reset state of request */
static void _request_reset(http_con_t *hcon)
{
	xassert(hcon->magic == MAGIC);

	_request_free_members(hcon);
	_request_init(hcon);
}

static int _on_request(const http_parser_request_t *req, void *arg)
{
	http_con_t *hcon = arg;
	http_con_request_t *request = &hcon->request;
	int rc = EINVAL;

	xassert(hcon->magic == MAGIC);

	request->http_version.major = req->http_version.major;
	request->http_version.minor = req->http_version.minor;
	request->method = req->method;
	url_copy_members(&request->url, req->url);

	/* Default to http if none given */
	if (request->url.scheme == URL_SCHEME_INVALID)
		request->url.scheme = URL_SCHEME_HTTP;

	if (!request->url.path) {
		error("%s: [%s] Rejecting request with empty URL path",
		      __func__, conmgr_con_get_name(hcon->con));

		return _send_reject(hcon, ESLURM_URL_INVALID_PATH);
	}

	if (req->method == HTTP_REQUEST_INVALID)
		return _send_reject(hcon, ESLURM_HTTP_INVALID_METHOD);

	if ((rc = _valid_http_version(req->http_version.major,
				      req->http_version.minor))) {
		error("%s: [%s] rejecting unsupported HTTP %hu.%hu version: %s",
		      __func__, conmgr_con_get_name(hcon->con),
		      req->http_version.major, req->http_version.minor,
		      slurm_strerror(rc));
		return rc;
	}

	if ((request->url.scheme != URL_SCHEME_HTTP) &&
	    (request->url.scheme != URL_SCHEME_HTTPS)) {
		error("%s: [%s] URL scheme not supported: %s",
		      __func__, conmgr_con_get_name(hcon->con),
		      url_get_scheme_string(request->url.scheme));
		return ESLURM_URL_UNSUPPORTED_SCHEME;
	}

	if ((request->url.scheme == URL_SCHEME_HTTPS) &&
	    !conmgr_fd_is_tls(hcon->con)) {
		error("%s: [%s] URL requested HTTPS but connection is not TLS wrapped",
		      __func__, conmgr_con_get_name(hcon->con));
		return ESLURM_TLS_REQUIRED;
	}

	return SLURM_SUCCESS;
}

static int _on_header(const http_parser_header_t *header, void *arg)
{
	http_con_t *hcon = arg;
	http_con_request_t *request = &hcon->request;

	xassert(hcon->magic == MAGIC);

	log_flag(NET, "%s: [%s] Header: %s Value: %s",
		 __func__, conmgr_con_get_name(hcon->con), header->name,
		 header->value);

	/* Add header copy to list of headers */
	list_append(request->headers,
		    http_header_new(header->name, header->value));

	/* Watch for connection headers */
	if (!xstrcasecmp(header->name, "Connection")) {
		if (!xstrcasecmp(header->value, "Keep-Alive")) {
			request->keep_alive = true;
		} else if (!xstrcasecmp(header->value, "Close")) {
			request->connection_close = true;
		} else {
			warning("%s: [%s] ignoring unsupported header request: Connection: %s",
			      __func__,
			      conmgr_con_get_name(hcon->con),
			      header->value);
		}
	} else if (!xstrcasecmp(header->name, "Keep-Alive")) {
		/*
		 * RFC2068-19.7.1.1: HTTP/1.1 does not define any parameters.
		 * RFC2068-19.7.1.1: If the Keep-Alive header is sent, the
		 * corresponding connection token MUST be transmitted. The
		 * Keep-Alive header MUST be ignored if received without the
		 * connection token.
		 */
		log_flag(NET, "%s: [%s] Ignoring Keep-Alive header parameter: %s",
			 __func__, conmgr_con_get_name(hcon->con), header->value);
	} else if (!xstrcasecmp(header->name, "Content-Type")) {
		xfree(request->content_type);
		request->content_type = xstrdup(header->value);
	} else if (!xstrcasecmp(header->name, "Content-Length")) {
		/* Use signed buffer to catch if negative length is provided */
		ssize_t cl;

		if ((sscanf(header->value, "%zd", &cl) != 1) || (cl < 0))
			return _send_reject(hcon,
					    ESLURM_HTTP_INVALID_CONTENT_LENGTH);
		request->content_length = cl;
	} else if (!xstrcasecmp(header->name, "Accept")) {
		xfree(request->accept);
		request->accept = xstrdup(header->value);
	} else if (!xstrcasecmp(header->name, "Expect")) {
		if (!xstrcasecmp(header->value, "100-continue"))
			request->expect = 100;
		else
			return _send_reject(hcon,
					    ESLURM_HTTP_UNSUPPORTED_EXPECT);
	} else if (!xstrcasecmp(header->name, "Transfer-Encoding")) {
		/* Transfer encoding is not allowed */
		return _send_reject(hcon,
				    ESLURM_HTTP_INVALID_TRANSFER_ENCODING);
	} else if (!xstrcasecmp(header->name, "Content-Encoding")) {
		/* Content encoding is not allowed */
		return _send_reject(hcon, ESLURM_HTTP_INVALID_CONTENT_ENCODING);
	} else if (!xstrcasecmp(header->name, "Upgrade")) {
		/* Upgrades are not allowed */
		return _send_reject(hcon, ESLURM_HTTP_UNSUPPORTED_UPGRADE);
	}

	return SLURM_SUCCESS;
}

static int _on_headers_complete(void *arg)
{
	http_con_t *hcon = arg;
	http_con_request_t *request = &hcon->request;

	xassert(hcon->magic == MAGIC);

	if (!request->http_version.major && !request->http_version.minor) {
		log_flag(NET, "%s: [%s] HTTP/0.9 connection",
			 __func__, conmgr_con_get_name(hcon->con));

		/*
		 * Force connection without version to HTTP/0.9 as only
		 * recognized versions in RFC2068-19.7
		 */
		request->http_version.major = 0;
		request->http_version.minor = 9;

		/*
		 * Disable persistent connections for HTTP/0.9 connections to
		 * avoid breaking non-compliant clients
		 *
		 * RFC9112-C.2.2: Clients are also encouraged to consider the
		 * use of "Connection: keep-alive" in requests carefully
		 */
		request->connection_close = true;
		request->keep_alive = false;
	} else if ((request->http_version.major == 1) &&
		   (request->http_version.minor == 0)) {
		log_flag(NET, "%s: [%s] HTTP/1.0 connection",
			 __func__, conmgr_con_get_name(hcon->con));
		/*
		 * RFC9112-C.2.2: In HTTP/1.0, each connection is established by
		 * the client prior to the request and closed by the server
		 * after sending the response. Servers might wish to be
		 * compatible with these previous approaches to persistent
		 * connections, by explicitly negotiating for them with a
		 * "Connection: keep-alive" request header field.
		 * RFC2068-19.7.1: Persistent connections in HTTP/1.0 must be
		 * explicitly negotiated as they are not the default behavior.
		 *
		 * Default HTTP/1.0 to close w/o keep_alive being requested.
		 */
		if (!request->keep_alive)
			request->connection_close = true;
	} else if ((request->http_version.major == 1) &&
		   (request->http_version.minor == 1)) {
		log_flag(NET, "%s: [%s] HTTP/1.1 connection",
			 __func__, conmgr_con_get_name(hcon->con));

		/*
		 * RFC2068-8.1.2.1: An HTTP/1.1 server MAY assume that a
		 * HTTP/1.1 client intends to maintain a persistent connection
		 */
		request->keep_alive = true;
	} else {
		log_flag(NET, "%s: [%s] HTTP/%d.%d connection",
				 __func__, conmgr_con_get_name(hcon->con),
				 request->http_version.major,
				 request->http_version.minor);

		/*
		 * RFC9112-9.3: If the received protocol is HTTP/1.1 (or later),
		 * the connection will persist after the current response
		 */
		request->keep_alive = true;
	}

	if (!request->http_version.major && request->http_version.minor)
		return SLURM_SUCCESS;

	if ((request->method == HTTP_REQUEST_POST) &&
	    (request->content_length <= 0))
		return _send_reject(hcon,
				    ESLURM_HTTP_POST_MISSING_CONTENT_LENGTH);

	if (request->expect)
		return http_con_send_response(hcon, request->expect, NULL,
					      false, NULL, NULL);

	return SLURM_SUCCESS;
}

static int _on_content(const http_parser_content_t *content, void *arg)
{
	http_con_t *hcon = arg;
	http_con_request_t *request = &hcon->request;
	const void *at = get_buf_data(content->buffer);
	const size_t length = get_buf_offset(content->buffer);

	xassert(hcon->magic == MAGIC);

	log_flag_hex(NET_RAW, at, length, "%s: [%s] received HTTP content",
	       __func__, conmgr_con_get_name(hcon->con));

	if (!request->url.path) {
		error("%s: [%s] rejecting missing path",
		      __func__, conmgr_con_get_name(hcon->con));
		return ESLURM_HTTP_UNEXPECTED_REQUEST;
	}

	if (get_buf_offset(content->buffer) > 0) {
		size_t nlength = (length + request->content_bytes);
		int rc = EINVAL;

		if (nlength > MAX_BODY_BYTES)
			return _send_reject(
				hcon, ESLURM_HTTP_CONTENT_LENGTH_TOO_LARGE);

		if ((request->content_length > 0) &&
		    (nlength > request->content_length))
			return _send_reject(hcon, ESLURM_HTTP_UNEXPECTED_BODY);

		if (!request->content &&
		    !(request->content = try_init_buf(BUF_SIZE)))
			return _send_reject(hcon, ENOMEM);

		/* Always include 1 extra byte for NULL terminator */
		if ((rc = try_grow_buf_remaining(request->content,
						 (length + 1))))
			return _send_reject(hcon, rc);

		xassert(remaining_buf(request->content) >= nlength);
		memmove((get_buf_data(request->content) +
			 get_buf_offset(request->content)),
			at, length);
		set_buf_offset(request->content,
			       (get_buf_offset(request->content) + length));
		request->content_bytes += length;

		/* final byte must in body must always be NULL terminated */
		{
			char *term = (get_buf_data(request->content) +
				      get_buf_offset(request->content) + 1);
			*term = '\0';
		}
	}

	log_flag(NET, "%s: [%s] received %zu bytes for HTTP body length %zu/%zu bytes",
		 __func__, conmgr_con_get_name(hcon->con), length,
		 request->content_bytes, request->content_length);

	return SLURM_SUCCESS;
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
 * IN con - connection pointer
 * IN name header name
 * IN value header value
 * RET formatted string (must xfree)
 * */
static int _write_fmt_header(conmgr_fd_ref_t *con, const char *name,
			     const char *value)
{
	char *buffer = _fmt_header(name, value);
	int rc = conmgr_con_queue_write_data(con, buffer, strlen(buffer));
	xfree(buffer);
	return rc;
}

/*
 * Send HTTP close notification header
 *	Warns the client that we are about to close the connection.
 * IN hcon - http connection
 * RET SLURM_SUCCESS or error
 */
static int _send_http_connection_close(http_con_t *hcon)
{
	return _write_fmt_header(hcon->con, "Connection", "Close");
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

/*
 * Create and write formatted numerical header
 * IN con - connection pointer
 * IN name header name
 * IN value header value
 * RET formatted string (must xfree)
 * */
static int _write_fmt_num_header(conmgr_fd_ref_t *con, const char *name,
				 size_t value)
{
	const char *buffer = _fmt_header_num(name, value);
	int rc = conmgr_con_queue_write_data(con, buffer, strlen(buffer));
	xfree(buffer);
	return rc;
}

static int _write_each_header(void *x, void *arg)
{
	http_header_t *header = x;
	write_each_header_args_t *args = arg;

	xassert(args->magic == WRITE_EACH_HEADER_MAGIC);
	xassert(header->magic == HTTP_HEADER_MAGIC);

	if ((args->rc =
		     _write_fmt_header(args->con, header->name, header->value)))
		return SLURM_ERROR;

	return SLURM_SUCCESS;
}

/* Send RFC2616 response */
static int _send_http_status_response(http_con_request_t *request,
				      http_status_code_t status_code,
				      conmgr_fd_ref_t *con)
{
	char buffer[MAX_STATUS_BYTES] = { 0 };
	int wrote = -1;

	if ((wrote = snprintf(buffer, sizeof(buffer), "HTTP/%d.%d %d %s%s",
			      request->http_version.major,
			      request->http_version.minor, status_code,
			      get_http_status_code_string(status_code),
			      CRLF)) >= sizeof(buffer)) {
		log_flag(NET, "%s: [%s] HTTP response %s too large: %d/%zu bytes",
			 __func__, conmgr_con_get_name(con),
			 get_http_status_code_string(status_code), wrote,
			 sizeof(buffer));
		return ENOMEM;
	}

	log_flag_hex(NET, buffer, wrote, "%s: [%s] HTTP response",
		     __func__, conmgr_con_get_name(con), status_code,
	       get_http_status_code_string(status_code));

	return conmgr_con_queue_write_data(con, buffer, wrote);
}

extern int http_con_send_response(http_con_t *hcon,
				  http_status_code_t status_code,
				  list_t *headers, bool close_header,
				  buf_t *body, const char *body_encoding)
{
	int rc = SLURM_SUCCESS;
	http_con_request_t *request = &hcon->request;
	conmgr_fd_ref_t *con = hcon->con;

	xassert(hcon->magic == MAGIC);
	xassert(conmgr_con_get_name(con));
	xassert(status_code > HTTP_STATUS_CODE_INVALID);
	xassert(status_code < HTTP_STATUS_CODE_INVALID_MAX);
	xassert(request->http_version.major > 0);

	log_flag(NET, "%s: [%s] sending response %u: %s",
	       __func__, conmgr_con_get_name(con), status_code,
	       get_http_status_code_string(status_code));

	if ((rc = _send_http_status_response(request, status_code, con)))
		return rc;

	/* send along any requested headers */
	if (headers) {
		write_each_header_args_t args = {
			.magic = WRITE_EACH_HEADER_MAGIC,
			.rc = SLURM_SUCCESS,
			.con = hcon->con,
		};

		(void) list_for_each(headers, _write_each_header, &args);

		if (args.rc)
			return args.rc;
	}

	if (close_header && (rc = _send_http_connection_close(hcon)))
		return rc;

	if (body && (get_buf_offset(body) > 0)) {
		const size_t body_length = get_buf_offset(body);

		/* RFC7230-3.3.2 limits response of Content-Length */
		if ((status_code < 100) ||
		    ((status_code >= 200) && (status_code != 204))) {
			if ((rc = _write_fmt_num_header(con, "Content-Length",
							body_length))) {
				return rc;
			}
		}

		if (body_encoding &&
		    (rc = _write_fmt_header(con, "Content-Type",
					    body_encoding)))
			return rc;

		/* Send end of headers */
		if ((rc = conmgr_con_queue_write_data(con, CRLF, strlen(CRLF))))
			return rc;

		log_flag(NET, "%s: [%s] rc=%s(%u) sending %zu bytes of body",
			 __func__, conmgr_con_get_name(con),
			 get_http_status_code_string(status_code), status_code,
			 body_length);

		log_flag_hex(NET_RAW, get_buf_data(body), body_length,
			     "%s: [%s] sending body", __func__,
			     conmgr_con_get_name(con));

		if ((rc = conmgr_con_queue_write_data(con, get_buf_data(body),
						      body_length)))
			return rc;
	} else if (((status_code >= 100) && (status_code < 200)) ||
		   (status_code == HTTP_STATUS_CODE_SUCCESS_NO_CONTENT) ||
		   (status_code == HTTP_STATUS_CODE_REDIRECT_NOT_MODIFIED)) {
		/*
		 * RFC2616 requires empty line after headers for return code
		 * that "MUST NOT" include a message body
		 */
		if ((rc = conmgr_con_queue_write_data(con, CRLF, strlen(CRLF))))
			return rc;
	}

	return rc;
}

static int _send_reject(http_con_t *hcon, slurm_err_t error_number)
{
	http_con_request_t *request = &hcon->request;
	bool close_header = false;

	xassert(hcon->magic == MAGIC);

	close_header = (request->connection_close ||
			_valid_http_version(request->http_version.major,
					    request->http_version.minor));

	(void) http_con_send_response(hcon,
				      http_status_from_error(error_number),
				      NULL, close_header, NULL, NULL);

	/* ensure connection gets closed */
	conmgr_con_queue_close(hcon->con);

	/* reset connection to avoid inheriting request state */
	_request_reset(hcon);

	return error_number;
}

static int _on_content_complete(void *arg)
{
	http_con_t *hcon = arg;
	http_con_request_t *request = &hcon->request;
	int rc = EINVAL;

	xassert(hcon->magic == MAGIC);

	if ((request->content_length > 0) &&
	    (request->content_length != request->content_bytes)) {
		error("%s: [%s] Content-Length %zd and received body length %zd mismatch",
		      __func__, conmgr_con_get_name(hcon->con),
		      request->content_length, request->content_bytes);
		return _send_reject(hcon, ESLURM_HTTP_INVALID_CONTENT_LENGTH);
	}

	rc = hcon->events->on_request(hcon, conmgr_con_get_name(hcon->con),
				      &hcon->request, hcon->arg);

	if (request->connection_close) {
		/* Notify client that this connection will be closed now */
		_send_http_connection_close(hcon);
		conmgr_con_queue_close(hcon->con);
	}

	_request_reset(hcon);

	return rc;
}

extern int _on_data(conmgr_fd_t *con, void *arg)
{
	http_con_t *hcon = arg;
	static const http_parser_callbacks_t callbacks = {
		.on_request = _on_request,
		.on_header = _on_header,
		.on_headers_complete = _on_headers_complete,
		.on_content = _on_content,
		.on_content_complete = _on_content_complete,
	};
	int rc = SLURM_SUCCESS;
	ssize_t bytes_parsed = -1;
	buf_t *buffer = NULL;

	xassert(hcon->magic == MAGIC);
	xassert(conmgr_fd_get_ref(hcon->con) == con);

	if (!hcon->parser && (rc = http_parser_g_new_parse_request(
				      conmgr_con_get_name(hcon->con),
				      &callbacks, hcon, &hcon->parser))) {
		log_flag(NET, "%s: [%s] Creating new HTTP parser failed: %s",
			 __func__, conmgr_con_get_name(hcon->con),
			 slurm_strerror(rc));
		goto cleanup;
	}

	if ((rc = conmgr_con_shadow_in_buffer(hcon->con, &buffer))) {
		log_flag(NET, "%s: [%s] Unable to get HTTP input buffer: %s",
			 __func__, conmgr_con_get_name(hcon->con),
			 slurm_strerror(rc));
		goto cleanup;
	}

	/* Set buffer as fully populated */
	set_buf_offset(buffer, size_buf(buffer));

	log_flag(NET, "%s: [%s] Accepted HTTP connection",
		 __func__, conmgr_con_get_name(hcon->con));

	rc = http_parser_g_parse_request(hcon->parser, buffer, &bytes_parsed);

	if (hcon->con)
		log_flag(NET, "%s: [%s] parsed %zu/%u bytes: %s",
			 __func__, conmgr_con_get_name(hcon->con),
			 bytes_parsed, get_buf_offset(buffer),
			 slurm_strerror(rc));

	if (rc) {
		rc = _send_reject(hcon, rc);
	} else if (hcon->con && (bytes_parsed > 0) &&
		   (rc = conmgr_con_mark_consumed_input_buffer(hcon->con,
							       bytes_parsed))) {
		log_flag(NET, "%s: [%s] Input buffer became invalid after parsing: %s",
			 __func__, conmgr_con_get_name(hcon->con),
			 slurm_strerror(rc));
		goto cleanup;
	}

cleanup:
	FREE_NULL_BUFFER(buffer);
	return rc;
}

static void _on_finish(conmgr_fd_t *con, void *arg)
{
	http_con_t *hcon = arg;
	void *hcon_arg = hcon->arg;
	conmgr_fd_ref_t *hcon_con = NULL;
	const http_con_server_events_t *hcon_events = hcon->events;

	xassert(hcon->magic == MAGIC);
	xassert(conmgr_fd_get_ref(hcon->con) == con);

	/*
	 * Preserve conmgr connection reference to ensure that the connection is
	 * always valid which is redundant since this callback will keep the
	 * conmgr alive until the conversion to only conmgr references.
	 * TODO: remove hcon->con once after conmgr reference conversion
	 */
	SWAP(hcon_con, hcon->con);

	http_parser_g_free_parse_request(&hcon->parser);
	_request_free_members(hcon);
	hcon->magic = ~MAGIC;

	if (hcon->free_on_close)
		xfree(hcon);

	/*
	 * hcon must not be used in any way to call on_close() as it is expected
	 * that this function call will release that memory which would risk a
	 * use after free()
	 */

	if (hcon_events->on_close)
		hcon_events->on_close(conmgr_con_get_name(hcon_con), hcon_arg);

	conmgr_fd_free_ref(&hcon_con);
}

extern int http_con_assign_server(conmgr_fd_ref_t *con, http_con_t *hcon,
				  const http_con_server_events_t *events,
				  void *arg)
{
	static const conmgr_events_t http_events = {
		.on_data = _on_data,
		.on_finish = _on_finish,
	};
	const conmgr_events_t *prior_events = NULL;
	void *prior_arg = NULL;
	bool free_on_close = false;
	int rc = EINVAL;

	xassert(con);
	xassert(events);
	xassert(events->on_request);

	if (!con || !events)
		return rc;

	if (!hcon) {
		hcon = xmalloc(sizeof(*hcon));
		free_on_close = true;
	}

	/* catch over-subscription */
	xassert(hcon->magic != MAGIC);

	*hcon = (http_con_t) {
		.magic = MAGIC,
		.free_on_close = free_on_close,
		.events = events,
		.arg = arg,
	};

	if ((rc = conmgr_con_get_events(con, &prior_events, &prior_arg)))
		goto failed;

	if (!(hcon->con = conmgr_con_link(con)))
		goto failed;

	if ((rc = conmgr_con_set_events(con, &http_events, hcon, __func__)))
		goto failed;

	_request_init(hcon);

	return rc;
failed:
	conmgr_fd_free_ref(&hcon->con);
	hcon->magic = ~MAGIC;

	/* Attempt to revert changes */
	if (prior_events)
		(void) conmgr_con_set_events(con, prior_events, prior_arg,
					     __func__);

	return rc;
}
