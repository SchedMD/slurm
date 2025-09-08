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

#include "config.h"

#include <ctype.h>
#include <limits.h>
#include <sys/socket.h>
#include <unistd.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/http.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

typedef struct {
	http_status_code_t code;
	char *text;
} http_status_code_txt_t;

#define T(code, text) { code, text }
static const http_status_code_txt_t http_status_codes[] = {
	// clang-format off
	T(HTTP_STATUS_CODE_CONTINUE, "CONTINUE"),
	T(HTTP_STATUS_CODE_SWITCH_PROTOCOLS, "SWITCH PROTOCOLS"),
	T(HTTP_STATUS_CODE_SUCCESS_OK, "OK"),
	T(HTTP_STATUS_CODE_SUCCESS_CREATED, "CREATED"),
	T(HTTP_STATUS_CODE_SUCCESS_ACCEPTED, "ACCEPTED"),
	T(HTTP_STATUS_CODE_SUCCESS_NON_AUTHORITATIVE, "OK (NON AUTHORITATIVE)"),
	T(HTTP_STATUS_CODE_SUCCESS_NO_CONTENT, "NO CONTENT"),
	T(HTTP_STATUS_CODE_SUCCESS_RESET_CONNECTION, "RESET CONNECTION"),
	T(HTTP_STATUS_CODE_SUCCESS_PARTIAL_CONTENT, "PARTIAL CONTENT"),
	T(HTTP_STATUS_CODE_REDIRECT_MULTIPLE_CHOICES, "REDIRECT MULTIPLE CHOICES"),
	T(HTTP_STATUS_CODE_REDIRECT_MOVED_PERMANENTLY, "MOVED PERMANENTLY"),
	T(HTTP_STATUS_CODE_REDIRECT_FOUND, "REDIRECT FOUND"),
	T(HTTP_STATUS_CODE_REDIRECT_SEE_OTHER, "REDIRECT SEE OTHER"),
	T(HTTP_STATUS_CODE_REDIRECT_NOT_MODIFIED, "NOT MODIFIED"),
	T(HTTP_STATUS_CODE_REDIRECT_USE_PROXY, "USE PROXY"),
	T(HTTP_STATUS_CODE_REDIRECT_TEMP_REDIRCT, "TEMP REDIRECT"),
	T(HTTP_STATUS_CODE_ERROR_BAD_REQUEST, "BAD REQUEST"),
	T(HTTP_STATUS_CODE_ERROR_UNAUTHORIZED, "UNAUTHORIZED"),
	T(HTTP_STATUS_CODE_ERROR_PAYMENT_REQUIRED, "PAYMENT REQUIRED"),
	T(HTTP_STATUS_CODE_ERROR_FORBIDDEN, "FORBIDDEN"),
	T(HTTP_STATUS_CODE_ERROR_NOT_FOUND, "NOT FOUND"),
	T(HTTP_STATUS_CODE_ERROR_METHOD_NOT_ALLOWED, "NOT ALLOWED"),
	T(HTTP_STATUS_CODE_ERROR_NOT_ACCEPTABLE, "NOT ACCEPTABLE"),
	T(HTTP_STATUS_CODE_ERROR_PROXY_AUTH_REQ, "PROXY AUTHENTICATION REQUIRED"),
	T(HTTP_STATUS_CODE_ERROR_REQUEST_TIMEOUT, "REQUEST TIMEOUT"),
	T(HTTP_STATUS_CODE_ERROR_CONFLICT, "CONFLICT"),
	T(HTTP_STATUS_CODE_ERROR_GONE, "GONE"),
	T(HTTP_STATUS_CODE_ERROR_LENGTH_REQUIRED, "LENGTH REQUIRED"),
	T(HTTP_STATUS_CODE_ERROR_PRECONDITION_FAILED, "PRECONDITION FAILED"),
	T(HTTP_STATUS_CODE_ERROR_ENTITY_TOO_LARGE, "ENTITY TOO LARGE"),
	T(HTTP_STATUS_CODE_ERROR_URI_TOO_LONG, "URI TOO LONG"),
	T(HTTP_STATUS_CODE_ERROR_UNSUPPORTED_MEDIA_TYPE, "UNSUPPORTED MEDIA TYPE"),
	T(HTTP_STATUS_CODE_ERROR_REQUEST_RANGE_UNSATISFIABLE, "REQUEST RANGE UNJUSTIFIABLE"),
	T(HTTP_STATUS_CODE_ERROR_EXPECTATION_FAILED, "EXPECTATION FAILED"),
	/* rfc7168 */
	T(HTTP_STATUS_CODE_ERROR_IM_A_TEAPOT, "I'm a Teapot"),
	/* rfc9110 15.5.20 */
	T(HTTP_STATUS_CODE_ERROR_MISDIRECT_REQUESTED, "MISDIRECTED REQUEST"),
	/* rfc9110 15.5.21 */
	T(HTTP_STATUS_CODE_ERROR_UNPROCESSABLE_CONTENT, "UNPROCESSABLE CONTENT"),
	/* rfc7231 6.5.15 */
	T(HTTP_STATUS_CODE_ERROR_UPGRADE_REQUIRED, "UPGRADE REQUIRED"),
	T(HTTP_STATUS_CODE_SRVERR_INTERNAL, "INTERNAL ERROR"),
	T(HTTP_STATUS_CODE_SRVERR_NOT_IMPLEMENTED, "NOT IMPLEMENTED"),
	T(HTTP_STATUS_CODE_SRVERR_BAD_GATEWAY, "BAD GATEWAY"),
	T(HTTP_STATUS_CODE_SRVERR_SERVICE_UNAVAILABLE, "SERVICE UNAVAILABLE"),
	T(HTTP_STATUS_CODE_SRVERR_GATEWAY_TIMEOUT, "GATEWAY TIMEOUT"),
	T(HTTP_STATUS_CODE_SRVERR_HTTP_VERSION_NOT_SUPPORTED, "HTTP VERSION NOT SUPPORTED"),
	T(HTTP_STATUS_CODE_SRVERR_VARIANT_ALSO_NEGOTIATES, "Variant Also Negotiates"),
	T(HTTP_STATUS_CODE_SRVERR_INSUFFICENT_STORAGE, "Insufficient Storage"),
	T(HTTP_STATUS_CODE_SRVERR_LOOP_DETECTED, "Loop Detected"),
	T(HTTP_STATUS_CODE_SRVERR_NOT_EXTENDED, "Not Extended"),
	T(HTTP_STATUS_CODE_SRVERR_NETWORK_AUTH_REQ, "Network Authentication Required"),
	T(HTTP_STATUS_CODE_DEFAULT, "default"),
	// clang-format on
};
#undef T

/* Conversion from slurm_error to http_status code map */
#define T(error, code) { error, code }

static const struct {
	slurm_err_t error;
	http_status_code_t code;
} http_status_errors[] = {
	// clang-format off
	T(SLURM_NO_CHANGE_IN_DATA, HTTP_STATUS_CODE_REDIRECT_NOT_MODIFIED),
	T(ESLURM_REST_INVALID_QUERY, HTTP_STATUS_CODE_ERROR_UNPROCESSABLE_CONTENT),
	T(ESLURM_REST_FAIL_PARSING, HTTP_STATUS_CODE_ERROR_BAD_REQUEST),
	T(ESLURM_REST_INVALID_JOBS_DESC, HTTP_STATUS_CODE_ERROR_BAD_REQUEST),
	T(ESLURM_DATA_UNKNOWN_MIME_TYPE, HTTP_STATUS_CODE_ERROR_UNSUPPORTED_MEDIA_TYPE),
	T(ESLURM_INVALID_JOB_ID, HTTP_STATUS_CODE_ERROR_NOT_FOUND),
	T(ESLURM_REST_UNKNOWN_URL, HTTP_STATUS_CODE_ERROR_NOT_FOUND),
	T(ESLURM_URL_INVALID_PATH, HTTP_STATUS_CODE_ERROR_NOT_FOUND),
	T(SLURM_PROTOCOL_SOCKET_ZERO_BYTES_SENT, HTTP_STATUS_CODE_SRVERR_BAD_GATEWAY),
	T(SLURM_COMMUNICATIONS_CONNECTION_ERROR, HTTP_STATUS_CODE_SRVERR_BAD_GATEWAY),
	T(SLURM_COMMUNICATIONS_SEND_ERROR, HTTP_STATUS_CODE_SRVERR_BAD_GATEWAY),
	T(SLURM_COMMUNICATIONS_RECEIVE_ERROR, HTTP_STATUS_CODE_SRVERR_BAD_GATEWAY),
	T(SLURM_COMMUNICATIONS_SHUTDOWN_ERROR, HTTP_STATUS_CODE_SRVERR_BAD_GATEWAY),
	T(SLURMCTLD_COMMUNICATIONS_CONNECTION_ERROR, HTTP_STATUS_CODE_SRVERR_BAD_GATEWAY),
	T(SLURMCTLD_COMMUNICATIONS_SEND_ERROR, HTTP_STATUS_CODE_SRVERR_BAD_GATEWAY),
	T(SLURMCTLD_COMMUNICATIONS_RECEIVE_ERROR, HTTP_STATUS_CODE_SRVERR_BAD_GATEWAY),
	T(SLURMCTLD_COMMUNICATIONS_SHUTDOWN_ERROR, HTTP_STATUS_CODE_SRVERR_BAD_GATEWAY),
	T(SLURMCTLD_COMMUNICATIONS_BACKOFF, HTTP_STATUS_CODE_SRVERR_BAD_GATEWAY),
	T(ESLURM_DB_CONNECTION, HTTP_STATUS_CODE_SRVERR_BAD_GATEWAY),
	T(ESLURM_PROTOCOL_INCOMPLETE_PACKET, HTTP_STATUS_CODE_SRVERR_BAD_GATEWAY),
	T(SLURM_PROTOCOL_SOCKET_IMPL_TIMEOUT, HTTP_STATUS_CODE_SRVERR_GATEWAY_TIMEOUT),
	T(SLURM_PROTOCOL_AUTHENTICATION_ERROR, HTTP_STATUS_CODE_SRVERR_NETWORK_AUTH_REQ),
	T(ESLURM_HTTP_INVALID_CONTENT_LENGTH, HTTP_STATUS_CODE_ERROR_BAD_REQUEST),
	T(ESLURM_HTTP_CONTENT_LENGTH_TOO_LARGE, HTTP_STATUS_CODE_ERROR_ENTITY_TOO_LARGE),
	T(ESLURM_HTTP_POST_MISSING_CONTENT_LENGTH, HTTP_STATUS_CODE_ERROR_LENGTH_REQUIRED),
	T(ESLURM_HTTP_INVALID_CONTENT_ENCODING, HTTP_STATUS_CODE_ERROR_NOT_ACCEPTABLE),
	T(ESLURM_HTTP_UNSUPPORTED_EXPECT, HTTP_STATUS_CODE_ERROR_EXPECTATION_FAILED),
	T(ESLURM_HTTP_UNSUPPORTED_KEEP_ALIVE, HTTP_STATUS_CODE_ERROR_NOT_ACCEPTABLE),
	T(ESLURM_HTTP_INVALID_METHOD, HTTP_STATUS_CODE_ERROR_METHOD_NOT_ALLOWED),
	T(ESLURM_HTTP_UNSUPPORTED_UPGRADE, HTTP_STATUS_CODE_ERROR_NOT_ACCEPTABLE),
	T(ESLURM_HTTP_INVALID_TRANSFER_ENCODING, HTTP_STATUS_CODE_ERROR_NOT_ACCEPTABLE),
	T(ESLURM_AUTH_CRED_INVALID, HTTP_STATUS_CODE_ERROR_UNAUTHORIZED),
	T(ESLURM_AUTH_EXPIRED, HTTP_STATUS_CODE_ERROR_UNAUTHORIZED),
	T(ESLURM_AUTH_UNABLE_TO_GENERATE_TOKEN, HTTP_STATUS_CODE_ERROR_UNAUTHORIZED),
	T(ESLURM_HTTP_UNEXPECTED_BODY, HTTP_STATUS_CODE_ERROR_BAD_REQUEST),
	T(ESLURM_DATA_UNKNOWN_MIME_TYPE, HTTP_STATUS_CODE_ERROR_UNSUPPORTED_MEDIA_TYPE),
	T(ESLURM_HTTP_UNKNOWN_ACCEPT_MIME_TYPE, HTTP_STATUS_CODE_ERROR_UNSUPPORTED_MEDIA_TYPE),
	T(ESLURM_REST_UNKNOWN_URL_METHOD, HTTP_STATUS_CODE_ERROR_METHOD_NOT_ALLOWED),
	// clang-format on
};

#undef T

#define T(method, upper_case, lower_case) \
	[method] = { method, upper_case, lower_case }
static const struct {
	http_request_method_t method;
	const char *uc_text;
	const char *lc_text;
} method_strings[] = {
	T(HTTP_REQUEST_INVALID, NULL, NULL),
	T(HTTP_REQUEST_GET, "GET", "get"),
	T(HTTP_REQUEST_POST, "POST", "post"),
	T(HTTP_REQUEST_PUT, "PUT", "put"),
	T(HTTP_REQUEST_DELETE, "DELETE", "delete"),
	T(HTTP_REQUEST_OPTIONS, "OPTIONS", "options"),
	T(HTTP_REQUEST_HEAD, "HEAD", "head"),
	T(HTTP_REQUEST_PATCH, "PATCH", "patch"),
	T(HTTP_REQUEST_TRACE, "TRACE", "trace"),
	T(HTTP_REQUEST_INVALID_MAX, NULL, NULL),
};

#undef T

#define T(tscheme, str) \
	{ \
		.scheme = tscheme, \
		.string = str, \
		.bytes = (sizeof(str) - 1), \
	}
static const struct {
	url_scheme_t scheme;
	const char *string;
	size_t bytes;
} schemes[] = {
	T(URL_SCHEME_INVALID, "INVALID"),
	T(URL_SCHEME_HTTP, "http"),
	T(URL_SCHEME_HTTPS, "https"),
	T(URL_SCHEME_UNIX, "unix"),
	T(URL_SCHEME_INVALID_MAX, "INVALID_MAX"),
};
#undef T

extern int url_get_scheme(const char *str, size_t bytes,
			  url_scheme_t *scheme_ptr)
{
	if (!str || !str[0] || !bytes) {
		*scheme_ptr = URL_SCHEME_INVALID;
		return ESLURM_URL_EMPTY;
	}

	for (int i = (URL_SCHEME_INVALID + 1); i < URL_SCHEME_INVALID_MAX;
	     i++) {
		if (bytes != schemes[i].bytes)
			continue;

		if (xstrncasecmp(schemes[i].string, str, bytes))
			continue;

		*scheme_ptr = schemes[i].scheme;
		return SLURM_SUCCESS;
	}

	*scheme_ptr = URL_SCHEME_INVALID;
	return ESLURM_URL_UNKNOWN_SCHEME;
}

extern const char *url_get_scheme_string(const url_scheme_t scheme)
{
	xassert(scheme >= URL_SCHEME_INVALID);
	xassert(scheme < URL_SCHEME_INVALID_MAX);

	if (scheme == URL_SCHEME_INVALID)
		return NULL;

	for (int i = (URL_SCHEME_INVALID + 1); i < URL_SCHEME_INVALID_MAX;
	     i++) {
		if (scheme == schemes[i].scheme)
			return schemes[i].string;
	}

	fatal_abort("should never happen");
}

/*
 * chars that can pass without decoding.
 * rfc3986: unreserved characters.
 */
static bool _is_valid_url_char(char buffer)
{
	return (isxdigit(buffer) || isalpha(buffer) || buffer == '~' ||
		buffer == '-' || buffer == '.' || buffer == '_');
}

extern unsigned char url_decode_escape_seq(const char *ptr)
{
	if (isxdigit(*(ptr + 1)) && isxdigit(*(ptr + 2))) {
		/* using uint16_t char to catch any overflows */
		uint16_t high = *(ptr + 1);
		uint16_t low = *(ptr + 2);
		uint16_t decoded = ((slurm_char_to_hex(high) << 4) +
				    slurm_char_to_hex(low));

		//TODO: find more invalid characters?
		if (decoded == '\0') {
			log_flag(DATA, "%s: invalid URL escape sequence for 0x00",
				 __func__);
			return '\0';
		} else if (decoded >= 0xff) {
			log_flag(DATA, "%s: invalid URL escape sequence for 0x%02" PRIx16,
				 __func__, decoded);
			return '\0';
		}

		log_flag(DATA, "%s: URL decoded: 0x%c%c -> %c (0x%02" PRIx16 ")",
		       __func__, (unsigned char) high, (unsigned char) low,
		       (unsigned char) decoded, decoded);

		return (unsigned char) decoded;
	} else {
		log_flag_hex(DATA, ptr, strnlen(ptr, 3),
			     "%s: invalid URL escape sequence: %s", __func__,
			     ptr);
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
			const char c = url_decode_escape_seq(ptr);
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

extern http_status_code_t get_http_status_code(const char *str)
{
	if (isdigit(str[0])) {
		uint64_t n = slurm_atoul(str);

		/*
		 * Check for default explicitly as it is outside the valid range
		 * that is checked by next if()
		 */
		if (n == HTTP_STATUS_CODE_DEFAULT)
			return HTTP_STATUS_CODE_DEFAULT;

		if ((n <= HTTP_STATUS_CODE_INVALID) ||
		    (n >= HTTP_STATUS_CODE_INVALID_MAX))
			return HTTP_STATUS_CODE_INVALID;

		return n;
	}

	for (int i = 0; i < ARRAY_SIZE(http_status_codes); i++)
		if (!xstrcasecmp(http_status_codes[i].text, str))
			return http_status_codes[i].code;

	return HTTP_STATUS_CODE_INVALID;
}

extern http_status_code_t http_status_from_error(slurm_err_t error)
{
	for (int i = 0; i < ARRAY_SIZE(http_status_errors); i++)
		if (error == http_status_errors[i].error)
			return http_status_errors[i].code;

	return HTTP_STATUS_CODE_SRVERR_INTERNAL;
}

extern slurm_err_t http_status_to_error(http_status_code_t code)
{
	for (int i = 0; i < ARRAY_SIZE(http_status_errors); i++)
		if (code == http_status_errors[i].code)
			return http_status_errors[i].error;

	return SLURM_ERROR;
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
	if ((method <= HTTP_REQUEST_INVALID) ||
	    (method >= HTTP_REQUEST_INVALID_MAX))
		return NULL;

	return method_strings[method].uc_text;
}

extern const char *get_http_method_string_lc(http_request_method_t method)
{
	if ((method <= HTTP_REQUEST_INVALID) ||
	    (method >= HTTP_REQUEST_INVALID_MAX))
		return NULL;

	return method_strings[method].lc_text;
}

extern http_request_method_t get_http_method(const char *str)
{
	if (!str || !str[0])
		return HTTP_REQUEST_INVALID;

	for (int i = 0; i < ARRAY_SIZE(method_strings); i++)
		if (method_strings[i].method &&
		    !xstrcasecmp(method_strings[i].lc_text, str))
			return method_strings[i].method;

	return HTTP_REQUEST_INVALID;
}

extern void url_free_members(url_t *url)
{
	url->scheme = URL_SCHEME_INVALID;
	xfree(url->host);
	xfree(url->port);
	xfree(url->user);
	xfree(url->path);
	xfree(url->query);
	xfree(url->fragment);
}

extern void url_copy_members(url_t *dst, const url_t *src)
{
	url_free_members(dst);

	dst->scheme = src->scheme;
	dst->host = xstrdup(src->host);
	dst->port = xstrdup(src->port);
	dst->user = xstrdup(src->user);
	dst->path = xstrdup(src->path);
	dst->query = xstrdup(src->query);
	dst->fragment = xstrdup(src->fragment);
}

extern void free_http_header(http_header_t *header)
{
	xassert(header->magic == HTTP_HEADER_MAGIC);
	xfree(header->name);
	xfree(header->value);
	header->magic = ~HTTP_HEADER_MAGIC;
	xfree(header);
}

/* find operator against http_header_t */
static int _http_header_find_key(void *x, void *y)
{
	const http_header_t *entry = x;
	const char *key = y;

	xassert(entry->name);
	xassert(entry->magic == HTTP_HEADER_MAGIC);

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
	http_header_t *header = NULL;

	if (!headers || !name)
		return NULL;

	header = (http_header_t *) list_find_first(headers,
						   _http_header_find_key,
						   (void *) name);

	if (header) {
		xassert(header->magic == HTTP_HEADER_MAGIC);
		return header->value;
	}

	return NULL;
}
