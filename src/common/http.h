/*****************************************************************************\
 *  http.h - handling HTTP
 *****************************************************************************
 *  Copyright (C) 2019-2021 SchedMD LLC.
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

#ifndef SLURM_HTTP_H
#define SLURM_HTTP_H

#include "src/common/data.h"

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
	HTTP_STATUS_CODE_SUCCESS_PARTIAL_CONTENT = 206,
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

/*
 * Get HTTP method from string
 * IN string containing method name (case insensitive)
 * RET method or HTTP_REQUEST_INVALID if unknown
 */
extern http_request_method_t get_http_method(const char *str);
extern const char *get_http_method_string(const http_request_method_t method);

/*
 * Parses url path into a data struct.
 * IN query rfc3986&rfc1866 query string
 * 	application/x-www-form-urlencoded
 * 	breaks /path/to/url/ -> [path,to,url]
 * 	into a data_t sequence
 * IN convert_types if true, call data_convert_type() on each value
 * IN allow_templates - allow sections to be template variables e.g.: "{name}"
 * RET data ptr or NULL on error
 */
extern data_t *parse_url_path(const char *path, bool convert_types,
			      bool allow_templates);

#endif /* SLURM_HTTP_H */
