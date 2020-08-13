/*****************************************************************************\
 *  http.h - handling HTTP
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

#ifndef SLURMRESTD_HTTP_H
#define SLURMRESTD_HTTP_H

#include <stddef.h>
#include <stdint.h>

#include "src/common/list.h"
#include "src/slurmrestd/conmgr.h"

/*
 * HTTP status codes from rfc2616&rfc7231 for http1.1
 */
typedef enum {
	HTTP_STATUS_NONE = 0,
	/* 1xx (Informational) */
	HTTP_STATUS_CODE_CONTINUE = 100,
	HTTP_STATUS_CODE_SWITCH_PROTOCOLS = 101,
	/* 2xx (Successful) */
	HTTP_STATUS_CODE_SUCCESS_OK = 200,
	HTTP_STATUS_CODE_SUCCESS_CREATED = 201,
	HTTP_STATUS_CODE_SUCCESS_ACCEPTED = 202,
	HTTP_STATUS_CODE_SUCCESS_NON_AUTHORITATIVE = 203,
	HTTP_STATUS_CODE_SUCCESS_NO_CONTENT = 204,
	HTTP_STATUS_CODE_SUCCESS_RESET_CONNECTION = 205,
	HTTP_STATUS_CODE_SUCCESS_PARTIAL_CONENT = 206,
	/* 3xx (Redirection) */
	HTTP_STATUS_CODE_REDIRECT_MULTIPLE_CHOICES = 300,
	HTTP_STATUS_CODE_REDIRECT_MOVED_PERMANENTLY = 301,
	HTTP_STATUS_CODE_REDIRECT_FOUND = 302,
	HTTP_STATUS_CODE_REDIRECT_SEE_OTHER = 303,
	HTTP_STATUS_CODE_REDIRECT_NOT_MODIFIED = 304,
	HTTP_STATUS_CODE_REDIRECT_USE_PROXY = 305,
	HTTP_STATUS_CODE_REDIRECT_TEMP_REDIRCT = 307,
	/* 4xx (Client Error) */
	HTTP_STATUS_CODE_ERROR_BAD_REQUEST = 400,
	HTTP_STATUS_CODE_ERROR_UNAUTHORIZED = 401,
	HTTP_STATUS_CODE_ERROR_PAYMENT_REQUIRED = 402,
	HTTP_STATUS_CODE_ERROR_FORBIDDEN = 403,
	HTTP_STATUS_CODE_ERROR_NOT_FOUND = 404,
	HTTP_STATUS_CODE_ERROR_METHOD_NOT_ALLOWED = 405,
	HTTP_STATUS_CODE_ERROR_NOT_ACCEPTABLE = 406,
	HTTP_STATUS_CODE_ERROR_PROXY_AUTH_REQ = 407,
	HTTP_STATUS_CODE_ERROR_REQUEST_TIMEOUT = 408,
	HTTP_STATUS_CODE_ERROR_CONFLICT = 409,
	HTTP_STATUS_CODE_ERROR_GONE = 410,
	HTTP_STATUS_CODE_ERROR_LENGTH_REQUIRED = 411,
	HTTP_STATUS_CODE_ERROR_PRECONDITION_FAILED = 412,
	HTTP_STATUS_CODE_ERROR_ENTITY_TOO_LARGE = 413,
	HTTP_STATUS_CODE_ERROR_URI_TOO_LONG = 414,
	HTTP_STATUS_CODE_ERROR_UNSUPPORTED_MEDIA_TYPE = 415,
	HTTP_STATUS_CODE_ERROR_REQUEST_RANGE_UNSATISFIABLE = 416,
	HTTP_STATUS_CODE_ERROR_EXPECTATION_FAILED = 417,
	HTTP_STATUS_CODE_ERROR_IM_A_TEAPOT = 418,
	HTTP_STATUS_CODE_ERROR_UPGRADE_REQUIRED = 426,
	/* 5xx (Server Error) */
	HTTP_STATUS_CODE_SRVERR_INTERNAL = 500,
	HTTP_STATUS_CODE_SRVERR_NOT_IMPLEMENTED = 501,
	HTTP_STATUS_CODE_SRVERR_BAD_GATEWAY = 502,
	HTTP_STATUS_CODE_SRVERR_SERVICE_UNAVAILABLE = 503,
	HTTP_STATUS_CODE_SRVERR_GATEWAY_TIMEOUT = 504,
	HTTP_STATUS_CODE_SRVERR_HTTP_VERSION_NOT_SUPPORTED = 505,
} http_status_code_t;
/*
 * Convert status code to string of status code
 * IN code status code to convert
 * RET string of status code or NULL on error
 */
extern const char *get_http_status_code_string(http_status_code_t code);

/*
 * Supported HTTP request Methods.
 * All others will be rejected.
 */
typedef enum {
	HTTP_REQUEST_INVALID = 0,	/* should never happen */
	HTTP_REQUEST_GET,
	HTTP_REQUEST_POST,
	HTTP_REQUEST_PUT,
	HTTP_REQUEST_DELETE,
	HTTP_REQUEST_OPTIONS,
	HTTP_REQUEST_HEAD,
	HTTP_REQUEST_PATCH,
	HTTP_REQUEST_TRACE,
	HTTP_REQUEST_MAX		/* keep at end */
} http_request_method_t;

struct on_http_request_args_s;
typedef struct on_http_request_args_s on_http_request_args_t;

/*
 * Get HTTP method from string
 * IN string containing method name (case insensitive)
 * RET method or HTTP_REQUEST_INVALID if unknown
 */
extern http_request_method_t get_http_method(const char *str);
extern const char *get_http_method_string(const http_request_method_t method);

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
	int magic;
	/* assigned connection */
	con_mgr_fd_t *con;
	/* Authentication context (auth_context_type_t) */
	void *auth;
	/* callback to call on each HTTP request */
	on_http_request_t on_http_request;
	/* http parser context */
	void *parser;
	/* http request_t */
	void *request;
} http_context_t;

typedef struct on_http_request_args_s {
	const http_request_method_t method; /* HTTP request method */
	const List headers; /* list of http_header_entry_t from client */
	const char *path; /* requested URL path (may be NULL) */
	const char *query; /* requested URL query (may be NULL) */
	http_context_t *context; /* calling context (do not xfree) */
	uint16_t http_major; /* HTTP major version */
	uint16_t http_minor; /* HTTP minor version */
	const char *content_type; /* header content-type */
	const char *accept; /* header accepted content-types */
	const char *body; /* body sent by client or NULL (do not xfree) */
	const size_t body_length; /* bytes in body to send or 0 */
	const char *body_encoding; /* body encoding type or NULL */
} on_http_request_args_t;

typedef struct {
	char *name;
	char *value;
} http_header_entry_t;

/* find http header from header list
 * IN headers List of http_header_entry_t
 * IN name name of header to find
 * RET ptr to header value or NULL if not found
 */
extern const char *find_http_header(List headers, const char *name);

/*
 * Call back for new connection to setup HTTP
 *
 * IN fd file descriptor of new connection
 * RET ptr to context to hand to parse_http()
 */
typedef http_context_t *(*on_http_connection_t)(int fd);

/*
 * Parse HTTP and call on_http_request on each HTTP request
 * must call send_http_response() on success
 * IN con conmgr connection of client
 * IN context connection context to hand to callback (do not xfree)
 * RET SLURM_SUCCESS or error
 */
extern int parse_http(con_mgr_fd_t *con, void *context);

typedef struct {
	con_mgr_fd_t *con; /* assigned connection */
	uint16_t http_major; /* HTTP major version */
	uint16_t http_minor; /* HTTP minor version */
	http_status_code_t status_code; /* HTTP status code to send */
	List headers; /* list of http_header_entry_t to send (can be empty) */
	const char *body; /* body to send or NULL */
	size_t body_length; /* bytes in body to send or 0 */
	const char *body_encoding; /* body encoding type or NULL */
} send_http_response_args_t;

/*
 * Send HTTP response
 * IN args arguments of response
 * RET SLURM_SUCESS or error
 */
extern int send_http_response(const send_http_response_args_t *args);

typedef struct {
	const char *host;
	const char *port; /* port as string for later parsing */
} parsed_host_port_t;

/*
 * Parse a combined host:port string into host and port
 * IN str host:port string for parsing
 * OUT parsed will be populated with strings (must xfree())
 * RET SLURM_SUCCESS or error
 */
extern parsed_host_port_t *parse_host_port(const char *str);

/*
 * Free parsed_host_port_t returned from parse_host_port()
 */
extern void free_parse_host_port(parsed_host_port_t *parsed);

/*
 * setup http context against a given new socket
 * IN fd file descriptor of socket (must be connected!)
 * IN on_http_request callback to call on each HTTP request
 * RET NULL on error or new http context (must xfree)
 */
extern http_context_t *setup_http_context(con_mgr_fd_t *con,
					  on_http_request_t on_http_request);

/*
 * cleanup http context on finished connection
 * IN context - context to connection to free
 */
extern void on_http_connection_finish(void *ctxt);

#endif /* SLURMRESTD_HTTP_H */
