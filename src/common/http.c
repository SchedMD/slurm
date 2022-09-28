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
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

typedef struct {
	http_status_code_t code;
	char *text;
} http_status_code_txt_t;

static const http_status_code_txt_t http_status_codes[] = {
	{ HTTP_STATUS_CODE_CONTINUE, "CONTINUE" },
	{ HTTP_STATUS_CODE_SWITCH_PROTOCOLS, "SWITCH PROTOCOLS" },
	{ HTTP_STATUS_CODE_SUCCESS_OK, "OK" },
	{ HTTP_STATUS_CODE_SUCCESS_CREATED, "CREATED" },
	{ HTTP_STATUS_CODE_SUCCESS_ACCEPTED, "ACCEPTED" },
	{ HTTP_STATUS_CODE_SUCCESS_NON_AUTHORITATIVE,
	  "OK (NON AUTHORITATIVE)" },
	{ HTTP_STATUS_CODE_SUCCESS_NO_CONTENT, "NO CONTENT" },
	{ HTTP_STATUS_CODE_SUCCESS_RESET_CONNECTION, "RESET CONNECTION" },
	{ HTTP_STATUS_CODE_SUCCESS_PARTIAL_CONTENT, "PARTIAL CONTENT" },
	{ HTTP_STATUS_CODE_REDIRECT_MULTIPLE_CHOICES,
	  "REDIRECT MULTIPLE CHOICES" },
	{ HTTP_STATUS_CODE_REDIRECT_MOVED_PERMANENTLY, "MOVED PERMANENTLY" },
	{ HTTP_STATUS_CODE_REDIRECT_FOUND, "REDIRECT FOUNT" },
	{ HTTP_STATUS_CODE_REDIRECT_SEE_OTHER, "REDIRECT SEE OTHER" },
	{ HTTP_STATUS_CODE_REDIRECT_NOT_MODIFIED, "NOT MODIFIED" },
	{ HTTP_STATUS_CODE_REDIRECT_USE_PROXY, "USE PROXY" },
	{ HTTP_STATUS_CODE_REDIRECT_TEMP_REDIRCT, "TEMP REDIRECT" },
	{ HTTP_STATUS_CODE_ERROR_BAD_REQUEST, "BAD REQUEST" },
	{ HTTP_STATUS_CODE_ERROR_UNAUTHORIZED, "UNAUTHORIZED" },
	{ HTTP_STATUS_CODE_ERROR_PAYMENT_REQUIRED, "PAYMENT REQUIRED" },
	{ HTTP_STATUS_CODE_ERROR_FORBIDDEN, "FORBIDDEN" },
	{ HTTP_STATUS_CODE_ERROR_NOT_FOUND, "NOT FOUND" },
	{ HTTP_STATUS_CODE_ERROR_METHOD_NOT_ALLOWED, "NOT ALLOWED" },
	{ HTTP_STATUS_CODE_ERROR_NOT_ACCEPTABLE, "NOT ACCEPTABLE" },
	{ HTTP_STATUS_CODE_ERROR_PROXY_AUTH_REQ,
	  "PROXY AUTHENTICATION REQUIRED" },
	{ HTTP_STATUS_CODE_ERROR_REQUEST_TIMEOUT, "REQUEST TIMEOUT" },
	{ HTTP_STATUS_CODE_ERROR_CONFLICT, "CONFLICT" },
	{ HTTP_STATUS_CODE_ERROR_GONE, "GONE" },
	{ HTTP_STATUS_CODE_ERROR_LENGTH_REQUIRED, "LENGTH REQUIRED" },
	{ HTTP_STATUS_CODE_ERROR_PRECONDITION_FAILED, "PRECONDITION FAILED" },
	{ HTTP_STATUS_CODE_ERROR_ENTITY_TOO_LARGE, "ENTITY TOO LARGE" },
	{ HTTP_STATUS_CODE_ERROR_URI_TOO_LONG, "URI TOO LONG" },
	{ HTTP_STATUS_CODE_ERROR_UNSUPPORTED_MEDIA_TYPE,
	  "UNSUPPORTED MEDIA TYPE" },
	{ HTTP_STATUS_CODE_ERROR_REQUEST_RANGE_UNSATISFIABLE,
	  "REQUEST RANGE UNJUSTIFIABLE" },
	{ HTTP_STATUS_CODE_ERROR_EXPECTATION_FAILED, "EXPECTATION FAILED" },
	{ HTTP_STATUS_CODE_ERROR_IM_A_TEAPOT, "I'm a Teapot" }, /* rfc7168 */
	{ HTTP_STATUS_CODE_ERROR_MISDIRECT_REQUESTED,
	  "MISDIRECTED REQUEST" }, /* rfc9110 15.5.20 */
	{ HTTP_STATUS_CODE_ERROR_UNPROCESSABLE_CONTENT,
	  "UNPROCESSABLE CONTENT" }, /* rfc9110 15.5.21 */
	{ HTTP_STATUS_CODE_ERROR_UPGRADE_REQUIRED,
	  "UPGRADE REQUIRED" }, /* rfc7231 6.5.15 */
	{ HTTP_STATUS_CODE_SRVERR_INTERNAL, "INTERNAL ERROR" },
	{ HTTP_STATUS_CODE_SRVERR_NOT_IMPLEMENTED, "NOT IMPLEMENTED" },
	{ HTTP_STATUS_CODE_SRVERR_BAD_GATEWAY, "BAD GATEWAY" },
	{ HTTP_STATUS_CODE_SRVERR_SERVICE_UNAVAILABLE, "SERVICE UNAVAILABLE" },
	{ HTTP_STATUS_CODE_SRVERR_GATEWAY_TIMEOUT, "GATEWAY TIMEOUT" },
	{ HTTP_STATUS_CODE_SRVERR_HTTP_VERSION_NOT_SUPPORTED,
	  "HTTP VERSION NOT SUPPORTED" },
};

/*
 * chars that can pass without decoding.
 * rfc3986: unreserved characters.
 */
static bool _is_valid_url_char(char buffer)
{
	return (isxdigit(buffer) || isalpha(buffer) || buffer == '~' ||
		buffer == '-' || buffer == '.' || buffer == '_');
}

/*
 * decodes % sequence.
 * IN ptr pointing to % character
 * RET \0 on error or decoded character
 */
static unsigned char _decode_seq(const char *ptr)
{
	if (isxdigit(*(ptr + 1)) && isxdigit(*(ptr + 2))) {
		/* using unsigned char to avoid any rollover */
		unsigned char high = *(ptr + 1);
		unsigned char low = *(ptr + 2);
		unsigned char decoded = (slurm_char_to_hex(high) << 4) +
					slurm_char_to_hex(low);

		//TODO: find more invalid characters?
		if (decoded == '\0') {
			error("%s: invalid URL escape sequence for 0x00",
			      __func__);
			return '\0';
		} else if (decoded == 0xff) {
			error("%s: invalid URL escape sequence for 0xff",
			      __func__);
			return '\0';
		}

		debug5("%s: URL decoded: 0x%c%c -> %c",
		       __func__, high, low, decoded);

		return decoded;
	} else {
		debug("%s: invalid URL escape sequence: %s", __func__, ptr);
		return '\0';
	}
}

static int _add_path(data_t *d, char **buffer, bool convert_types)
{
	if (!xstrcasecmp(*buffer, ".")) {
		debug5("%s: ignoring path . entry", __func__);
	} else if (!xstrcasecmp(*buffer, "..")) {
		//TODO: pop last directory off sequence instead of fail
		debug5("%s: rejecting path .. entry", __func__);
		return SLURM_ERROR;
	} else {
		data_t *c = data_list_append(d);
		data_set_string(c, *buffer);

		if (convert_types)
			(void) data_convert_type(c, DATA_TYPE_NONE);

		xfree(*buffer);
	}

	return SLURM_SUCCESS;
}

extern data_t *parse_url_path(const char *path, bool convert_types,
			      bool allow_templates)
{
	int rc = SLURM_SUCCESS;
	data_t *d = data_set_list(data_new());
	char *buffer = NULL;

	/* extract each word */
	for (const char *ptr = path; !rc && *ptr != '\0'; ++ptr) {
		if (_is_valid_url_char(*ptr)) {
			xstrcatchar(buffer, *ptr);
			continue;
		}

		switch (*ptr) {
		case '{': /* OASv3.0.3 section 4.7.8.2 template variable */
			if (!allow_templates) {
				debug("%s: unexpected OAS template character: %c",
				      __func__, *ptr);
				rc = SLURM_ERROR;
				break;
			} else {
				/* find end of template */
				char *end = xstrstr(ptr, "}");

				if (!end) {
					debug("%s: missing terminated OAS template character: }",
					      __func__);
					rc = SLURM_ERROR;
					break;
				}

				xstrncat(buffer, ptr, (end - ptr + 1));
				ptr = end;
				break;
			}
		case '%': /* rfc3986 */
		{
			const char c = _decode_seq(ptr);
			if (c != '\0') {
				/* shift past the hex value */
				ptr += 2;

				xstrcatchar(buffer, c);
			} else {
				debug("%s: invalid URL escape sequence: %s",
				      __func__, ptr);
				rc = SLURM_ERROR;
				break;
			}
			break;
		}
		case '/': /* rfc3986 */
			if (buffer != NULL)
				rc = _add_path(d, &buffer, convert_types);
			break;
		default:
			debug("%s: unexpected URL character: %c",
			      __func__, *ptr);
			rc = SLURM_ERROR;
		}
	}

	/* last part of path */
	if (!rc && buffer != NULL)
		rc = _add_path(d, &buffer, convert_types);

	if (rc) {
		FREE_NULL_DATA(d);
		return NULL;
	}

	return d;
}

extern const char *get_http_status_code_string(http_status_code_t code)
{
	for (int i = 0; i < ARRAY_SIZE(http_status_codes); i++)
		if (http_status_codes[i].code == code)
			return http_status_codes[i].text;

	return NULL;
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
