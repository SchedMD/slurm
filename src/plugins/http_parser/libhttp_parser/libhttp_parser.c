/*****************************************************************************\
 *  libhttp_parser.c - libhttp_parser handler
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

#include <http_parser.h>
#include <stdint.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"
#include "src/common/slurm_xlator.h"

#include "src/common/http.h"
#include "src/common/log.h"
#include "src/common/pack.h"
#include "src/common/read_config.h"
#include "src/common/xstring.h"
#include "src/common/xmalloc.h"

#include "src/interfaces/http_parser.h"

/* Required Slurm plugin symbols: */
const char plugin_name[] = "Slurm http_parser libhttp_parser plugin";
const char plugin_type[] = HTTP_PARSER_PREFIX LIBHTTP_PARSER_PLUGIN;
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

#define LOG_PARSE(state, fmt, ...) \
	_log_parse(state, NULL, 0, __func__, fmt, ##__VA_ARGS__)
#define LOG_PARSE_AT(state, at, bytes, fmt, ...) \
	_log_parse(state, (at), (bytes), __func__, fmt, ##__VA_ARGS__)

#define PARSE_ERROR(error_number, state) \
	_on_parse_error(error_number, state, NULL, 0, __func__)
#define PARSE_ERROR_AT(error_number, state, at, bytes) \
	_on_parse_error(error_number, state, (at), (bytes), __func__)

#define LOG_URL_PARSE(name, buffer, fmt, ...) \
	_log_url_parse(name, buffer, __func__, fmt, ##__VA_ARGS__)
#define URL_PARSE_ERROR(error_number, name, buffer) \
	_on_url_parse_error(error_number, name, buffer, __func__)

#define STATE_MAGIC 0xDFBFBEA0

typedef struct http_parser_state_s {
	int magic; /* STATE_MAGIC */
	/* Name of connection for logging */
	const char *name;
	/* callback to call on events */
	const http_parser_callbacks_t *callbacks;
	/* pointer to hand to callbacks */
	void *callback_arg;
	/* libhttp_parser context */
	http_parser parser;
	/* Requested URL */
	url_t url;
	/* state tracking of last header received */
	char *last_header;
	/* Bytes already parsed (cumulative) */
	ssize_t total_bytes;
	/* Current buffer getting parsed or NULL for EOF */
	const buf_t *buffer;
	/* Status code to return from http_parser_p_parse_request() */
	int rc;
	/* True if libhttp_parser just called _on_message_complete() */
	bool is_message_complete;
} http_parser_state_t;

/* reduce length of typename from long global typedef */
typedef http_parser_state_t state_t;

#define T(l, s) [l] = { l, s }

static const struct {
	uint16_t library;
	http_request_method_t slurm;
} methods[] = {
	/* WARNING: sync with enum http_request_method_t */
	T(HTTP_GET, HTTP_REQUEST_GET),
	T(HTTP_POST, HTTP_REQUEST_POST),
	T(HTTP_PUT, HTTP_REQUEST_PUT),
	T(HTTP_DELETE, HTTP_REQUEST_DELETE),
	T(HTTP_OPTIONS, HTTP_REQUEST_OPTIONS),
	T(HTTP_HEAD, HTTP_REQUEST_HEAD),
	T(HTTP_PATCH, HTTP_REQUEST_PATCH),
	T(HTTP_TRACE, HTTP_REQUEST_TRACE),
};

#undef T

#define T(l, s) [HPE_##l] = { HPE_##l, s }

static const struct {
	uint32_t library;
	slurm_err_t slurm;
} errors[] = {
	T(OK, SLURM_SUCCESS),
	T(CB_message_begin, ESLURM_HTTP_PARSING_FAILURE),
	T(CB_url, ESLURM_HTTP_PARSING_FAILURE),
	T(CB_header_field, ESLURM_HTTP_PARSING_FAILURE),
	T(CB_header_value, ESLURM_HTTP_PARSING_FAILURE),
	T(CB_headers_complete, ESLURM_HTTP_PARSING_FAILURE),
	T(CB_body, ESLURM_HTTP_PARSING_FAILURE),
	T(CB_message_complete, ESLURM_HTTP_PARSING_FAILURE),
	T(CB_status, ESLURM_HTTP_PARSING_FAILURE),
	T(CB_chunk_header, ESLURM_HTTP_PARSING_FAILURE),
	T(CB_chunk_complete, ESLURM_HTTP_PARSING_FAILURE),
	T(INVALID_EOF_STATE, ESLURM_PROTOCOL_INCOMPLETE_PACKET),
	T(HEADER_OVERFLOW, SLURM_PROTOCOL_INSANE_MSG_LENGTH),
	T(CLOSED_CONNECTION, SLURM_UNEXPECTED_MSG_ERROR),
	T(INVALID_VERSION, SLURM_PROTOCOL_VERSION_ERROR),
	T(INVALID_STATUS, ESLURM_HTTP_INVALID_STATUS_CODE),
	T(INVALID_METHOD, ESLURM_HTTP_INVALID_METHOD),
	T(INVALID_URL, ESLURM_URL_INVALID_FORMATING),
	T(INVALID_HOST, ESLURM_URL_INVALID_HOST),
	T(INVALID_PORT, ESLURM_URL_INVALID_PORT),
	T(INVALID_PATH, ESLURM_URL_INVALID_PATH),
	T(INVALID_QUERY_STRING, ESLURM_URL_INVALID_QUERY),
	T(INVALID_FRAGMENT, ESLURM_URL_INVALID_FRAGMENT),
	T(LF_EXPECTED, ESLURM_HTTP_MISSING_LF),
	T(INVALID_HEADER_TOKEN, ESLURM_HTTP_INVALID_CHARACTER),
	T(INVALID_CONTENT_LENGTH, ESLURM_HTTP_INVALID_CONTENT_LENGTH),
	T(UNEXPECTED_CONTENT_LENGTH, ESLURM_HTTP_INVALID_CONTENT_LENGTH),
	T(INVALID_CHUNK_SIZE, ESLURM_HTTP_INVALID_CHARACTER),
	T(INVALID_CONSTANT, ESLURM_HTTP_INVALID_CHARACTER),
	T(INVALID_INTERNAL_STATE, ESLURM_HTTP_PARSING_FAILURE),
	T(STRICT, ESLURM_HTTP_PARSING_FAILURE),
	T(PAUSED, ESLURM_HTTP_PARSING_FAILURE),
	T(UNKNOWN, ESLURM_HTTP_PARSING_FAILURE),
#ifdef HPE_INVALID_TRANSFER_ENCODING
	T(INVALID_TRANSFER_ENCODING, ESLURM_HTTP_INVALID_TRANSFER_ENCODING),
#endif
};

#undef T

/* convert libhttp_parser enum to slurm method enum */
static http_request_method_t _get_parser_method(const http_parser *parser)
{
	if (parser->method < ARRAY_SIZE(methods))
		return methods[parser->method].slurm;

	return HTTP_REQUEST_INVALID;
}

/* convert libhttp_parser enum to slurm error enum */
static slurm_err_t _get_parser_error(const http_parser *parser)
{
	if (parser->http_errno < ARRAY_SIZE(errors))
		return errors[parser->http_errno].slurm;

	return ESLURM_HTTP_PARSING_FAILURE;
}

extern void http_parser_p_free_parse_request(state_t **state_ptr)
{
	state_t *state = NULL;

	xassert(state_ptr);

	SWAP(state, *state_ptr);

	if (!state)
		return;

	xassert(state->magic == STATE_MAGIC);

	url_free_members(&state->url);
	xassert(!state->last_header);
	xassert(!state->buffer);
	xassert(state->total_bytes >= 0);
	state->total_bytes = -1;

	state->magic = ~STATE_MAGIC;
	xfree(state);
}

static void _state_parsing_reset(state_t *state)
{
	xassert(state->magic == STATE_MAGIC);

	http_parser_init(&state->parser, HTTP_REQUEST);
	url_free_members(&state->url);
	xfree(state->last_header);
	state->is_message_complete = false;
}

extern int http_parser_p_new_parse_request(const char *name,
					   const http_parser_callbacks_t
						   *callbacks,
					   void *callback_arg,
					   http_parser_state_t **state_ptr)
{
	state_t *state = NULL;

	if (!(state = try_xmalloc(sizeof(*state))))
		return ENOMEM;

	state->name = name;
	state->magic = STATE_MAGIC;
	state->callbacks = callbacks;
	state->callback_arg = callback_arg;
	state->url = URL_INITIALIZER;
	state->parser.data = state;

	_state_parsing_reset(state);

	*state_ptr = state;
	return SLURM_SUCCESS;
}

static void _log_parse_buffer(state_t *state, const char *caller,
			      const char *log_str, const void *at,
			      const size_t at_bytes)
{
	const void *buffer_ptr = get_buf_data(state->buffer);
	const size_t buffer_bytes = get_buf_offset(state->buffer);
	const void *buffer_begin = buffer_ptr;
	const void *begin = NULL;
#ifndef NDEBUG
	const void *buffer_end = (buffer_ptr + buffer_bytes);
	const void *end = (at ? (at + at_bytes) : buffer_end);
#endif
	size_t offset_begin = 0, offset_end = 0;

	xassert(buffer_begin);
	xassert(buffer_begin <= buffer_end);

	if (at) {
		begin = at;
		offset_begin = (begin - buffer_begin);
		offset_end = (offset_begin + at_bytes);
	} else {
		xassert(!at_bytes);

		begin = buffer_begin;
		offset_begin = 0;
		offset_end = buffer_bytes;
	}

	/* assert that pointers are in the buffer */
	xassert(begin >= buffer_begin);
	xassert(end <= buffer_end);
	xassert(offset_begin <= offset_end);
	xassert(offset_begin <= buffer_bytes);
	xassert(offset_end <= buffer_bytes);

	log_flag(DATA, "%s: [%s] PARSE [%zu,%zu)@0x%"PRIxPTR" %s", caller,
		 state->name, offset_begin, offset_end, (uintptr_t) buffer_ptr,
		 log_str);
	log_flag_hex_range(NET_RAW, buffer_ptr, buffer_bytes, offset_begin,
			   offset_end, "%s: [%s] %s", caller, state->name,
			   log_str);
}

static void _log_parse(state_t *state, const void *at, const size_t at_bytes,
		       const char *caller, const char *fmt, ...)
{
	char *log_str = NULL;

	xassert(state->magic == STATE_MAGIC);

	if (!(slurm_conf.debug_flags & DEBUG_FLAG_DATA) ||
	    (get_log_level() < LOG_LEVEL_VERBOSE))
		return;

	xassert(fmt);
	if (fmt) {
		va_list ap;
		va_start(ap, fmt);
		log_str = vxstrfmt(fmt, ap);
		va_end(ap);
	}

	if (state->buffer)
		_log_parse_buffer(state, caller, log_str, at, at_bytes);
	else
		log_flag(DATA, "%s: [%s] PARSE EOF %s",
			 caller, state->name, log_str);

	xfree(log_str);
}

/*
 * Notify caller that parsing failed
 * NOTE: use PARSE_ERROR() or PARSE_ERROR_AT() instead of calling directly
 * IN error_number - Slurm error encountered
 * IN state - state pointer
 * IN at - pointer to where failure happened or NULL if N/A
 * IN caller - function that caught error
 * RET 1 - always 1 to return to libhttp_parser to stop parsing
 */
static int _on_parse_error(slurm_err_t error_number, state_t *state,
			   const void *at, const size_t at_bytes,
			   const char *caller)
{
	http_parser_error_t error = {
		.error_number = error_number,
		.offset = state->total_bytes,
		.at = at,
		.at_bytes = (at ? at_bytes : -1),
	};

	xassert(state->magic == STATE_MAGIC);

	if (at) {
		xassert(state->buffer);
		xassert(at >= (const void *) get_buf_data(state->buffer));
		xassert((at + at_bytes) <=
			(const void *) (get_buf_data(state->buffer) +
					get_buf_offset(state->buffer)));

		/* Shift total_bytes to at pointer */
		error.offset +=
			(at - (const void *) get_buf_data(state->buffer));
	}

	LOG_PARSE_AT(state, at, at_bytes, "Parsing failed: %s",
		     slurm_strerror(error_number));

	if (state->callbacks->on_parse_error)
		state->rc =
			state->callbacks->on_parse_error(&error,
							 state->callback_arg);
	else
		state->rc = error_number;

	return 1;
}

static void _log_url_parse_buffer(const char *name, const char *caller,
				  const char *log_str, const buf_t *buffer)
{
	const void *begin = get_buf_data(buffer);
	const size_t bytes = get_buf_offset(buffer);

	xassert(buffer->magic == BUF_MAGIC);
	xassert(begin);
	xassert(begin <= (begin + bytes));

	log_flag(DATA, "%s: [%s] URL PARSE [0,%zu)@0x%"PRIxPTR" %s",
		 caller, name, bytes, (uintptr_t) begin, log_str);
	log_flag_hex_range(NET_RAW, begin, bytes, 0, bytes, "%s: [%s] %s",
			   caller, name, log_str);
}

static void _log_url_parse(const char *name, const buf_t *buffer,
			   const char *caller, const char *fmt, ...)
{
	char *log_str = NULL;

	if (!(slurm_conf.debug_flags & DEBUG_FLAG_DATA) ||
	    (get_log_level() < LOG_LEVEL_VERBOSE))
		return;

	xassert(fmt);
	if (fmt) {
		va_list ap;
		va_start(ap, fmt);
		log_str = vxstrfmt(fmt, ap);
		va_end(ap);
	}

	if (buffer)
		_log_url_parse_buffer(name, caller, log_str, buffer);
	else
		log_flag(DATA, "%s: [%s] URL PARSE %s", caller, name, log_str);

	xfree(log_str);
}

/*
 * Log that URL parsing failed
 * NOTE: use URL_PARSE_ERROR() instead of calling directly
 * IN error_number - Slurm error encountered
 * IN buffer - buffer being parsed
 * RET error_number
 */
static int _on_url_parse_error(slurm_err_t error_number, const char *name,
			       const buf_t *buffer, const char *caller)
{
	LOG_URL_PARSE(name, buffer, "Parsing failed: %s",
		      slurm_strerror(error_number));

	return error_number;
}

static void _on_http_parse_error(state_t *state)
{
	http_parser_error_t error = {
		.error_number = _get_parser_error(&state->parser),
		.description = http_errno_description(state->parser.http_errno),
		.offset = state->total_bytes,
		.at = NULL,
		.at_bytes = -1,
	};

	xassert(state->magic == STATE_MAGIC);

	LOG_PARSE(state, "Failure parsing HTTP: %s -> %s", __func__,
		  http_errno_name(state->parser.http_errno),
		  http_errno_description(state->parser.http_errno));

	if (state->callbacks->on_parse_error)
		state->rc =
			state->callbacks->on_parse_error(&error,
							 state->callback_arg);
	else
		state->rc = error.error_number;
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

/*
 * Parse URL where only the port is given.
 * Examples:
 *	:8080
 *	:ssh
 *
 * RET
 *	SLURM_SUCCESS: parsed port successfully
 *	ESLURM_URL_UNSUPPORTED_FORMAT: not a port only URL
 *	*: error
 */
static int _parse_only_port(const char *name, const buf_t *buffer, url_t *dst)
{
	const char *data = get_buf_data(buffer);
	const size_t bytes = get_buf_offset(buffer);

	if (data[0] != ':')
		return ESLURM_URL_UNSUPPORTED_FORMAT;

	dst->port = xstrndup((data + 1), (bytes - 1));
	return SLURM_SUCCESS;
}

/*
 * Parse URL using libhttp_parser's URL parser
 * Examples:
 *	host:port
 *	https://user@[host]:port/path/?query#fragment
 * RET
 *	SLURM_SUCCESS: parsed port successfully
 *	ESLURM_URL_UNSUPPORTED_FORMAT: library doesn't support format
 *	*: error
 */
static int _library_url_parse(const char *name, const buf_t *buffer, url_t *dst)
{
	struct http_parser_url url;
	const void *data = get_buf_data(buffer);
	const size_t bytes = get_buf_offset(buffer);
	int rc = EINVAL;

	_http_parser_url_init(&url);

	xassert(data);
	xassert(bytes > 0);
	xassert(size_buf(buffer) >= bytes);

	/* Try parsing a full URL and then try parsing only a host:port pair */
	if (http_parser_parse_url(data, bytes, false, &url) &&
	    http_parser_parse_url(data, bytes, true, &url))
		return ESLURM_URL_UNSUPPORTED_FORMAT;

	if ((url.field_set & (1 << UF_SCHEMA)) &&
	    (rc = url_get_scheme((data + url.field_data[UF_SCHEMA].off),
				 url.field_data[UF_SCHEMA].len, &dst->scheme)))
		return rc;
	if (url.field_set & (1 << UF_HOST)) {
		xassert(url.field_data[UF_HOST].len <= bytes);
		dst->host = xstrndup((data + url.field_data[UF_HOST].off),
				     url.field_data[UF_HOST].len);
	}
	if (url.field_set & (1 << UF_PORT)) {
		xassert(url.field_data[UF_PORT].len <= bytes);
		dst->port = xstrndup((data + url.field_data[UF_PORT].off),
				     url.field_data[UF_PORT].len);
	}
	if (url.field_set & (1 << UF_PATH)) {
		xassert(url.field_data[UF_PATH].len <= bytes);
		dst->path = xstrndup((data + url.field_data[UF_PATH].off),
				     url.field_data[UF_PATH].len);
	}
	if (url.field_set & (1 << UF_QUERY)) {
		xassert(url.field_data[UF_QUERY].len <= bytes);
		dst->query = xstrndup((data + url.field_data[UF_QUERY].off),
				      url.field_data[UF_QUERY].len);
	}
	if (url.field_set & (1 << UF_FRAGMENT)) {
		xassert(url.field_data[UF_FRAGMENT].len <= bytes);
		dst->fragment =
			xstrndup((data + url.field_data[UF_FRAGMENT].off),
				 url.field_data[UF_FRAGMENT].len);
	}
	if (url.field_set & (1 << UF_USERINFO)) {
		xassert(url.field_data[UF_USERINFO].len <= bytes);
		dst->user = xstrndup((data + url.field_data[UF_USERINFO].off),
				     url.field_data[UF_USERINFO].len);
	}

	return SLURM_SUCCESS;
}

extern int init(void)
{
	debug("loaded");
	return SLURM_SUCCESS;
}

extern void fini(void)
{
	debug("unloaded");
}

extern int url_parser_p_parse(const char *name, const buf_t *buffer, url_t *dst)
{
	int rc = EINVAL;

	xassert(!buffer || (buffer->magic == BUF_MAGIC));
	xassert(dst);
	xassert(name && name[0]);

	url_free_members(dst);

	if (!buffer || !get_buf_offset(buffer))
		return URL_PARSE_ERROR(ESLURM_URL_EMPTY, name, buffer);

	/* Catch any errant NULL terminators */
	if (strnlen(get_buf_data(buffer), get_buf_offset(buffer)) !=
	    get_buf_offset(buffer))
		return URL_PARSE_ERROR(ESLURM_URL_NON_NULL_TERMINATOR, name,
				       buffer);

	/*
	 * Try using libhttp_parser's builtin URL parser and then try additional
	 * parsers for formats it doesn't support
	 */
	if ((rc = _library_url_parse(name, buffer, dst))) {
		if (rc == ESLURM_URL_UNSUPPORTED_FORMAT)
			rc = _parse_only_port(name, buffer, dst);
	}

	if (rc) {
		/*
		 * If none of the parsers apply, then consider the URL to be an
		 * invalid format
		 */
		if (rc == ESLURM_URL_UNSUPPORTED_FORMAT)
			rc = ESLURM_URL_INVALID_FORMATING;

		url_free_members(dst);
		return URL_PARSE_ERROR(rc, name, buffer);
	}

	LOG_URL_PARSE(
		name, buffer,
		"Parsed URL scheme:%s host:%s port:%s user:%s path:%s query:%s fragment:%s",
		url_get_scheme_string(dst->scheme), dst->host, dst->port,
		dst->user, dst->path, dst->query, dst->fragment);
	return SLURM_SUCCESS;
}

static int _on_url(http_parser *parser, const char *at, size_t length)
{
	state_t *state = parser->data;
	buf_t buffer = {
		.magic = BUF_MAGIC,
		.head = (void *) at,
		.processed = length,
		.size = length,
	};
	int rc = EINVAL;

	xassert(state->magic == STATE_MAGIC);

	if (state->url.scheme != URL_SCHEME_INVALID)
		return PARSE_ERROR_AT(ESLURM_HTTP_UNEXPECTED_URL, state, at,
				      length);

	if ((rc = url_parser_p_parse(state->name, &buffer, &state->url)))
		return PARSE_ERROR_AT(rc, state, at, length);

	return rc;
}

static int _on_message_begin(http_parser *parser)
{
	/* Do nothing successfully */
	return 0;
}

static int _on_status(http_parser *parser, const char *at, size_t length)
{
	/* Do nothing successfully */
	return 0;
}

static int _on_header_field(http_parser *parser, const char *at, size_t length)
{
	state_t *state = parser->data;

	xassert(state->magic == STATE_MAGIC);
	xassert(!state->last_header);
	xassert(length > 0);
	xassert(length <= get_buf_offset(state->buffer));
	xassert((const void *) at >=
		(const void *) get_buf_data(state->buffer));

	if (!state->callbacks->on_header)
		return 0;

	xfree(state->last_header);
	state->last_header = xstrndup(at, length);

	/* Trim header field-name per RFC2616:4.2 */
	xstrtrim(state->last_header);

	return 0;
}

static int _on_header_value(http_parser *parser, const char *at, size_t length)
{
	state_t *state = parser->data;
	http_parser_header_t header = {
		.name = state->last_header,
	};
	char *value = NULL;

	xassert(state->magic == STATE_MAGIC);

	if (!state->callbacks->on_header)
		return 0;

	if (!state->last_header)
		return PARSE_ERROR_AT(ESLURM_HTTP_EMPTY_HEADER, state, at,
				      length);

	/* Copy value to trim it */
	value = xstrndup(at, length);

	/* trim header field-name per rfc2616:4.2 */
	xstrtrim(value);

	header.value = value;

	LOG_PARSE_AT(state, at, length, "Parsed Header:%s Value:%s",
		     header.name, header.value);

	if ((state->rc =
		     state->callbacks->on_header(&header, state->callback_arg)))
		return 1;

	xfree(state->last_header);
	xfree(value);
	return 0;
}

static int _headers_callback(http_parser *parser, state_t *state)
{
	http_parser_request_t request = {
		.http_version = {
			.major = parser->http_major,
			.minor = parser->http_minor,
		},
		.method = _get_parser_method(parser),
		.url = &state->url,
	};

	xassert(state->magic == STATE_MAGIC);

	if ((state->rc = state->callbacks->on_request(&request,
						      state->callback_arg)))
		return INFINITE;

	return 0;
}

/*
 * Note: Special return rules for this callback
 * return 0 for SUCCESS
 * return 1 to tell parser to not expect a body.
 * return 2 to tell parser to not expect a body or
 *	anything else from this HTTP request.
 * return all others to indicate failure to parse
 */
static int _on_headers_complete(http_parser *parser)
{
	state_t *state = parser->data;

	xassert(state->magic == STATE_MAGIC);

	if (!state->callbacks->on_request)
		return 0;
	else
		return _headers_callback(parser, state);
}

static int _on_body(http_parser *parser, const char *at, size_t length)
{
	state_t *state = parser->data;
	buf_t buffer = {
		.magic = BUF_MAGIC,
		.head = (void *) at,
		.size = length,
		.processed = length,
	};
	http_parser_content_t content = {
		.buffer = &buffer,
	};

	xassert(state->magic == STATE_MAGIC);

	LOG_PARSE_AT(state, at, length, "received HTTP body");

	if (state->callbacks->on_content &&
	    (state->rc = state->callbacks->on_content(&content,
						      state->callback_arg)))
		return 1;

	return 0;
}

static int _on_message_complete(http_parser *parser)
{
	state_t *state = parser->data;

	xassert(state->magic == STATE_MAGIC);

	xassert(!state->is_message_complete);
	state->is_message_complete = true;

	LOG_PARSE(state, "message complete");

	if (state->callbacks->on_content_complete &&
	    (state->rc = state->callbacks
				 ->on_content_complete(state->callback_arg)))
		return 1;

	return 0;
}

static int _on_chunk_header(http_parser *parser)
{
	state_t *state = parser->data;

	xassert(state->magic == STATE_MAGIC);

	return PARSE_ERROR(ESLURM_HTTP_UNSUPPORTED_CHUNK_ENCODING, state);
}

static int _on_chunk_complete(http_parser *parser)
{
	state_t *state = parser->data;

	xassert(state->magic == STATE_MAGIC);

	return PARSE_ERROR(ESLURM_HTTP_UNSUPPORTED_CHUNK_ENCODING, state);
}

static void _parse(state_t *state, ssize_t *bytes_parsed_ptr)
{
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
	size_t parsed_bytes = 0;
	void *data = NULL;
	size_t bytes = 0;

	if (state->buffer) {
		xassert(state->buffer->magic == BUF_MAGIC);

		/* Assert that the buffer is populated */
		xassert(get_buf_data(state->buffer));
		xassert(get_buf_offset(state->buffer) > 0);

		data = get_buf_data(state->buffer);
		/*
		 * Never include NULL terminator as library will consider any \0
		 * while parsing headers as an invalid token and false error
		 * out.
		 */
		bytes = get_buf_offset(state->buffer);

		/* Invalidate being message complete on more data to parse */
		state->is_message_complete = false;

		LOG_PARSE(state, "BEGIN: Parsing %zu bytes", bytes);
	} else if (state->is_message_complete) {
		/*
		 * Ignore EOF call when parser already thinks message is
		 * complete
		 */
		*bytes_parsed_ptr = 0;

		/*
		 * Reset parsing state to avoid inheriting incorrect state for
		 * parsing next message
		 */
		_state_parsing_reset(state);

		LOG_PARSE(state, "SKIP: Parsing EOF after total %zu bytes",
			  state->total_bytes);
		return;
	} else {
		/* Send NULL terminated empty string to signify EOF */
		data = "";
		bytes = 0;

		LOG_PARSE(state, "BEGIN: Parsing EOF after total %zu bytes",
			  state->total_bytes);
	}

	if ((parsed_bytes = http_parser_execute(&state->parser, &settings, data,
						bytes))) {
		/*
		 * Avoid including NULL terminator in total bytes parsed since
		 * that would inflate the bytes per the number of times parsing
		 * is called uselessly.
		 */
		state->total_bytes += parsed_bytes;
	} else if (state->parser.http_errno) {
		/* Only check for HTTP errors when nothing parsed */
		_on_http_parse_error(state);
	}

	if (!state->rc) {
		if (state->buffer) {
			*bytes_parsed_ptr = parsed_bytes;
		} else {
			/*
			 * library just assumes there is always a NULL
			 * terminator if there is a valid pointer
			 */
			xassert(parsed_bytes == 1);
			/* Always return 0 for NULL buffers */
			*bytes_parsed_ptr = 0;
		}

		LOG_PARSE(
			state,
			"END: Parsed %zu/%zu bytes totalling %zu bytes successfully",
			parsed_bytes, bytes, state->total_bytes);
	} else {
		/* Error already logged */
		*bytes_parsed_ptr = -1;
	}
}

extern int http_parser_p_parse_request(state_t *state, const buf_t *buffer,
				       ssize_t *bytes_parsed_ptr)
{
	xassert(state);
	xassert(state->magic == STATE_MAGIC);
	xassert(state->parser.data == state);

	state->buffer = buffer;

	/* Skip run if nothing to parse in buffer */
	if (buffer && !get_buf_offset(buffer))
		LOG_PARSE(state, "Skipping parse on empty buffer");
	else
		_parse(state, bytes_parsed_ptr);

	xassert(state->buffer == buffer);
	state->buffer = NULL;

	return state->rc;
}
