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
#include <limits.h>
#include <sys/socket.h>
#include <unistd.h>

#include "slurm/slurm.h"

#include "src/common/http.h"
#include "src/common/xstring.h"

extern const char *get_http_status_code_string(http_status_code_t code)
{
	switch (code) {
	case HTTP_STATUS_CODE_CONTINUE:
		return "CONTINUE";
	case HTTP_STATUS_CODE_SWITCH_PROTOCOLS:
		return "SWITCH PROTOCOLS";
	case HTTP_STATUS_CODE_SUCCESS_OK:
		return "OK";
	case HTTP_STATUS_CODE_SUCCESS_CREATED:
		return "CREATED";
	case HTTP_STATUS_CODE_SUCCESS_ACCEPTED:
		return "ACCEPTED";
	case HTTP_STATUS_CODE_SUCCESS_NON_AUTHORITATIVE:
		return "OK (NON AUTHORITATIVE)";
	case HTTP_STATUS_CODE_SUCCESS_NO_CONTENT:
		return "NO CONTENT";
	case HTTP_STATUS_CODE_SUCCESS_RESET_CONNECTION:
		return "RESET CONNECTION";
	case HTTP_STATUS_CODE_SUCCESS_PARTIAL_CONTENT:
		return "PARTIAL CONTENT";
	case HTTP_STATUS_CODE_REDIRECT_MULTIPLE_CHOICES:
		return "REDIRECT MULTIPLE CHOICES";
	case HTTP_STATUS_CODE_REDIRECT_MOVED_PERMANENTLY:
		return "MOVED PERMANENTLY";
	case HTTP_STATUS_CODE_REDIRECT_FOUND:
		return "REDIRECT FOUNT";
	case HTTP_STATUS_CODE_REDIRECT_SEE_OTHER:
		return "REDIRECT SEE OTHER";
	case HTTP_STATUS_CODE_REDIRECT_NOT_MODIFIED:
		return "NOT MODIFIED";
	case HTTP_STATUS_CODE_REDIRECT_USE_PROXY:
		return "USE PROXY";
	case HTTP_STATUS_CODE_REDIRECT_TEMP_REDIRCT:
		return "TEMP REDIRECT";
	case HTTP_STATUS_CODE_ERROR_BAD_REQUEST:
		return "BAD REQUEST";
	case HTTP_STATUS_CODE_ERROR_UNAUTHORIZED:
		return "UNAUTHORIZED";
	case HTTP_STATUS_CODE_ERROR_PAYMENT_REQUIRED:
		return "PAYMENT REQUIRED";
	case HTTP_STATUS_CODE_ERROR_FORBIDDEN:
		return "FORBIDDEN";
	case HTTP_STATUS_CODE_ERROR_NOT_FOUND:
		return "NOT FOUND";
	case HTTP_STATUS_CODE_ERROR_METHOD_NOT_ALLOWED:
		return "NOT ALLOWED";
	case HTTP_STATUS_CODE_ERROR_NOT_ACCEPTABLE:
		return "NOT ACCEPTABLE";
	case HTTP_STATUS_CODE_ERROR_PROXY_AUTH_REQ:
		return "PROXY AUTHENTICATION REQUIRED";
	case HTTP_STATUS_CODE_ERROR_REQUEST_TIMEOUT:
		return "REQUEST TIMEOUT";
	case HTTP_STATUS_CODE_ERROR_CONFLICT:
		return "CONFLICT";
	case HTTP_STATUS_CODE_ERROR_GONE:
		return "GONE";
	case HTTP_STATUS_CODE_ERROR_LENGTH_REQUIRED:
		return "LENGTH REQUIRED";
	case HTTP_STATUS_CODE_ERROR_PRECONDITION_FAILED:
		return "PRECONDITION FAILED";
	case HTTP_STATUS_CODE_ERROR_ENTITY_TOO_LARGE:
		return "ENTITY TOO LARGE";
	case HTTP_STATUS_CODE_ERROR_URI_TOO_LONG:
		return "URI TOO LONG";
	case HTTP_STATUS_CODE_ERROR_UNSUPPORTED_MEDIA_TYPE:
		return "UNSUPPORTED MEDIA TYPE";
	case HTTP_STATUS_CODE_ERROR_REQUEST_RANGE_UNSATISFIABLE:
		return "REQUEST RANGE UNJUSTIFIABLE";
	case HTTP_STATUS_CODE_ERROR_EXPECTATION_FAILED:
		return "EXPECTATION FAILED";
	case HTTP_STATUS_CODE_ERROR_IM_A_TEAPOT: /* rfc7168 */
		return "I'm a Teapot";
	case HTTP_STATUS_CODE_ERROR_UPGRADE_REQUIRED: /* rfc7231 6.5.15 */
		return "UPGRADE REQUIRED";
	case HTTP_STATUS_CODE_SRVERR_INTERNAL:
		return "INTERNAL ERROR";
	case HTTP_STATUS_CODE_SRVERR_NOT_IMPLEMENTED:
		return "NOT IMPLEMENTED";
	case HTTP_STATUS_CODE_SRVERR_BAD_GATEWAY:
		return "BAD GATEWAY";
	case HTTP_STATUS_CODE_SRVERR_SERVICE_UNAVAILABLE:
		return "SERVICE UNAVAILABLE";
	case HTTP_STATUS_CODE_SRVERR_GATEWAY_TIMEOUT:
		return "GATEWAY TIMEOUT";
	case HTTP_STATUS_CODE_SRVERR_HTTP_VERSION_NOT_SUPPORTED:
		return "HTTP VERSION NOT SUPPORTED";
	default:
		return NULL;
	}
}

extern const char *get_http_method_string(http_request_method_t method)
{
	switch (method) {
	case HTTP_REQUEST_GET:
		return "GET";
	case HTTP_REQUEST_POST:
		return "POST";
	case HTTP_REQUEST_PUT:
		return "PUT";
	case HTTP_REQUEST_DELETE:
		return "DELETE";
	case HTTP_REQUEST_OPTIONS:
		return "OPTIONS";
	case HTTP_REQUEST_HEAD:
		return "HEAD";
	case HTTP_REQUEST_PATCH:
		return "PATCH";
	case HTTP_REQUEST_TRACE:
		return "TRACE";
	default:
		return "INVALID";
	}
}

extern http_request_method_t get_http_method(const char *str)
{
	if (str == NULL)
		return HTTP_REQUEST_INVALID;
	if (!xstrcasecmp(str, "get"))
		return HTTP_REQUEST_GET;
	if (!xstrcasecmp(str, "post"))
		return HTTP_REQUEST_POST;
	if (!xstrcasecmp(str, "put"))
		return HTTP_REQUEST_PUT;
	if (!xstrcasecmp(str, "delete"))
		return HTTP_REQUEST_DELETE;
	if (!xstrcasecmp(str, "options"))
		return HTTP_REQUEST_OPTIONS;
	if (!xstrcasecmp(str, "head"))
		return HTTP_REQUEST_HEAD;
	if (!xstrcasecmp(str, "patch"))
		return HTTP_REQUEST_PATCH;
	if (!xstrcasecmp(str, "trace"))
		return HTTP_REQUEST_TRACE;
	return HTTP_REQUEST_INVALID;
}
