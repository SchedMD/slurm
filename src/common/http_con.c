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

#include <limits.h>
#include <sys/socket.h>
#include <unistd.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/http_con.h"
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

#define CRLF "\r\n"
#define MAX_BODY_BYTES 52428800 /* 50MB */
#define MAX_STATUS_BYTES 1024

typedef struct on_http_request_args_s on_http_request_args_t;

/*
 * Call back for each HTTP requested method.
 * This may be called several times in the same connection.
 * must call send_http_response().
 *
 * IN args see on_http_request_args_t
 * RET SLURM_SUCCESS or error to kill connection
 */
typedef int (*on_http_request_t)(on_http_request_args_t *args);

#define MAGIC 0xab0a8aff

typedef struct http_con_s {
	int magic; /* MAGIC */
	/* reference to assigned connection */
	conmgr_fd_ref_t *con;
	/* callback to call on each HTTP request */
	on_http_request_t on_http_request;
	http_parser_state_t *parser; /* http parser plugin state */
	http_con_request_t request;
} http_con_t;

#define WRITE_EACH_HEADER_MAGIC 0xba3a8aff

typedef struct {
	int magic; /* WRITE_EACH_HEADER_MAGIC */
	int rc;
	conmgr_fd_ref_t *con;
} write_each_header_args_t;

/*
 * Call back for new connection to setup HTTP
 *
 * IN fd file descriptor of new connection
 * RET ptr to http connection to hand to parse_http()
 */
typedef http_con_t *(*on_http_connection_t)(int fd);

typedef struct on_http_request_args_s {
	const http_request_method_t method; /* HTTP request method */
	list_t *headers; /* list_t of http_header_t* from client */
	const char *path; /* requested URL path (may be NULL) */
	const char *query; /* requested URL query (may be NULL) */
	http_con_t *hcon; /* calling http connection (do not xfree) */
	conmgr_fd_ref_t *con; /* reference to connection */
	const char *name; /* connection name */
	uint16_t http_major; /* HTTP major version */
	uint16_t http_minor; /* HTTP minor version */
	const char *content_type; /* header content-type */
	const char *accept; /* header accepted content-types */
	const char *body; /* body sent by client or NULL (do not xfree) */
	const size_t body_length; /* bytes in body to send or 0 */
	const char *body_encoding; /* body encoding type or NULL */
} on_http_request_args_t;

/*
 * Call back for each HTTP requested method.
 * This may be called several times in the same connection.
 * must call send_http_response().
 *
 * IN args see on_http_request_args_t
 * RET SLURM_SUCCESS or error to kill connection
 */
typedef int (*on_http_request_t)(on_http_request_args_t *args);

typedef struct {
	conmgr_fd_t *con; /* assigned connection */
	uint16_t http_major; /* HTTP major version */
	uint16_t http_minor; /* HTTP minor version */
	http_status_code_t status_code; /* HTTP status code to send */
	/* list of http_header_entry_t to send (can be empty) */
	list_t *headers; /* list_t of http_header_t* from client */
	const char *body; /* body to send or NULL */
	size_t body_length; /* bytes in body to send or 0 */
	const char *body_encoding; /* body encoding type or NULL */
} send_http_response_args_t;

extern int send_http_response(http_con_t *hcon,
			      const send_http_response_args_t *args);

/* default keep_alive value which appears to be implementation specific */
static int DEFAULT_KEEP_ALIVE = 5; //default to 5s to match apache2

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
		.keep_alive = -1,
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
			if (request->keep_alive == -1)
				request->keep_alive = DEFAULT_KEEP_ALIVE;
		} else if (!xstrcasecmp(header->value, "Close")) {
			request->connection_close = true;
		} else {
			warning("%s: [%s] ignoring unsupported header request: Connection: %s",
			      __func__,
			      conmgr_con_get_name(hcon->con),
			      header->value);
		}
	} else if (!xstrcasecmp(header->name, "Keep-Alive")) {
		int ibuffer = atoi(header->value);
		if (ibuffer > 1) {
			request->keep_alive = ibuffer;
		} else {
			error("%s: [%s] invalid Keep-Alive value %s",
			      __func__, conmgr_con_get_name(hcon->con),
			      header->value);
			return _send_reject(hcon,
					    ESLURM_HTTP_UNSUPPORTED_KEEP_ALIVE);
		}
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

	if ((request->http_version.major == 1) &&
	    (request->http_version.minor == 0)) {
		log_flag(NET, "%s: [%s] HTTP/1.0 connection",
			 __func__, conmgr_con_get_name(hcon->con));

		/* 1.0 defaults to close w/o keep_alive */
		if (!request->keep_alive)
			request->connection_close = true;
	} else if ((request->http_version.major == 1) &&
		   (request->http_version.minor == 1)) {
		log_flag(NET, "%s: [%s] HTTP/1.1 connection",
			 __func__, conmgr_con_get_name(hcon->con));

		/* keep alive is assumed for 1.1 */
		if (request->keep_alive == -1)
			request->keep_alive = DEFAULT_KEEP_ALIVE;
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

/*
 * Parse HTTP and call on_http_request on each HTTP request
 * must call send_http_response() on success
 * IN con conmgr connection of client
 * IN hcon - http connection to hand to callback (do not xfree)
 * RET SLURM_SUCCESS or error
 */
extern int send_http_response(http_con_t *hcon,
			      const send_http_response_args_t *args)
{
	char *buffer = NULL;
	int rc = SLURM_SUCCESS;
	xassert(args->status_code > HTTP_STATUS_CODE_INVALID);
	xassert(args->status_code < HTTP_STATUS_CODE_INVALID_MAX);
	xassert(args->body_length == 0 || (args->body_length && args->body));

	log_flag(NET, "%s: [%s] sending response %u: %s",
	       __func__, conmgr_con_get_name(hcon->con), args->status_code,
	       get_http_status_code_string(args->status_code));

	/* send rfc2616 response */
	xstrfmtcat(buffer, "HTTP/%d.%d %d %s"CRLF,
		   args->http_major, args->http_minor, args->status_code,
		   get_http_status_code_string(args->status_code));

	rc = conmgr_con_queue_write_data(hcon->con, buffer, strlen(buffer));
	xfree(buffer);

	if (rc)
		return rc;

	/* send along any requested headers */
	if (args->headers) {
		list_itr_t *itr = list_iterator_create(args->headers);
		http_header_t *header = NULL;
		while ((header = list_next(itr))) {
			xassert(header->magic == HTTP_HEADER_MAGIC);
			if ((rc = _write_fmt_header(hcon->con, header->name,
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
			if ((rc = _write_fmt_num_header(hcon->con,
							"Content-Length",
							args->body_length))) {
				return rc;
			}
		}

		if (args->body_encoding &&
		    (rc = _write_fmt_header(hcon->con, "Content-Type",
					    args->body_encoding)))
			return rc;

		if ((rc = conmgr_con_queue_write_data(hcon->con, CRLF,
						      strlen(CRLF))))
			return rc;

		log_flag(NET, "%s: [%s] rc=%s(%u) sending body:\n%s",
			 __func__, conmgr_con_get_name(hcon->con),
			 get_http_status_code_string(args->status_code),
			 args->status_code, args->body);

		if ((rc = conmgr_con_queue_write_data(hcon->con, args->body,
						      args->body_length)))
			return rc;
	} else if (((args->status_code >= 100) && (args->status_code < 200)) ||
		   (args->status_code == 204) ||
		   (args->status_code == 304)) {
		/*
		 * RFC2616 requires empty line after headers for return code
		 * that "MUST NOT" include a message body
		 */
		if ((rc = conmgr_con_queue_write_data(hcon->con, CRLF,
						      strlen(CRLF))))
			return rc;
	}

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
	uint16_t http_major = request->http_version.major;
	uint16_t http_minor = request->http_version.minor;

	xassert(hcon->magic == MAGIC);
	xassert(conmgr_con_get_name(con));
	xassert(status_code > HTTP_STATUS_CODE_INVALID);
	xassert(status_code < HTTP_STATUS_CODE_INVALID_MAX);

	/* If we don't have a requested client version, default to 0.9 */
	if ((http_major == 0) && (http_minor == 0))
		http_minor = 9;

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

static int _on_message_complete_request(http_con_t *hcon)
{
	int rc = EINVAL;
	http_con_request_t *request = &hcon->request;
	on_http_request_args_t args = {
		.method = request->method,
		.headers = request->headers,
		.path = request->url.path,
		.query = request->url.query,
		.hcon = hcon,
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

	xassert(hcon->magic == MAGIC);

	if (!(args.con = conmgr_con_link(hcon->con)) ||
	    !(args.name = conmgr_con_get_name(args.con))) {
		rc = SLURM_COMMUNICATIONS_MISSING_SOCKET_ERROR;
		log_flag(NET, "%s: connection missing: %s",
			 __func__, slurm_strerror(rc));
	} else if ((rc = hcon->on_http_request(&args)))
		log_flag(NET, "%s: [%s] on_http_request rejected: %s",
			 __func__, conmgr_con_get_name(hcon->con),
			 slurm_strerror(rc));

	conmgr_fd_free_ref(&args.con);
	return rc;
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

	if ((rc = _on_message_complete_request(hcon)))
		return rc;

	if (request->keep_alive) {
		//TODO: implement keep alive correctly
		log_flag(NET, "%s: [%s] keep alive not currently implemented",
			 __func__, conmgr_con_get_name(hcon->con));
	}

	if (request->connection_close) {
		/* Notify client that this connection will be closed now */
		_send_http_connection_close(hcon);
		conmgr_con_queue_close(hcon->con);
	}

	_request_reset(hcon);

	return SLURM_SUCCESS;
}

extern int parse_http(conmgr_fd_t *con, void *x)
{
	http_con_t *hcon = (http_con_t *) x;
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

/*
 * setup http connection against a given new socket
 * IN fd file descriptor of socket (must be connected!)
 * IN on_http_request callback to call on each HTTP request
 * RET NULL on error or new http connection (must xfree)
 */
extern http_con_t *setup_http_context(conmgr_fd_t *con,
				      on_http_request_t on_http_request)
{
	http_con_t *hcon = xmalloc(sizeof(*hcon));

	hcon->magic = MAGIC;
	hcon->con = conmgr_fd_new_ref(con);
	hcon->on_http_request = on_http_request;

	_request_init(hcon);

	return hcon;
}

/*
 * cleanup http connection on finished connection
 * IN con - conmgr connection
 * IN hcon - http_connection to connection to free
 */
extern void on_http_connection_finish(conmgr_fd_t *con, void *arg)
{
	http_con_t *hcon = (http_con_t *) arg;

	if (!hcon)
		return;
	xassert(hcon->magic == MAGIC);

	http_parser_g_free_parse_request(&hcon->parser);

	/* release request */
	_request_free_members(hcon);

	xassert(conmgr_fd_get_ref(hcon->con) == con);
	conmgr_fd_free_ref(&hcon->con);

	hcon->magic = ~MAGIC;
	xfree(hcon);
}

extern int http_con_assign_server(conmgr_fd_ref_t *con, http_con_t *hcon,
				  const http_con_server_events_t *events,
				  void *arg)
{
	return ESLURM_NOT_SUPPORTED;
}
