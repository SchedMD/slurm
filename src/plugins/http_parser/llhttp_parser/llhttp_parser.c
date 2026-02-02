/*****************************************************************************\
 *  llhttp_parser.c - llhttp_parser handler
 *****************************************************************************
 *  Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#include <llhttp.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/log.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "../common/http_parser_common.h"

#include "src/interfaces/http_parser.h"
#include "src/interfaces/url_parser.h"

/* Required Slurm plugin symbols: */
const char plugin_name[] = "Slurm http_parser llhttp_parser plugin";
const char plugin_type[] = HTTP_PARSER_PREFIX LLHTTP_PARSER_PLUGIN;
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

#define LOG_PARSE(state, fmt, ...) \
	do { \
		xassert(state->magic == LLHTTP_STATE_MAGIC); \
		log_parse(state->buffer, state->name, NULL, 0, __func__, fmt, \
			  ##__VA_ARGS__); \
	} while (0)
#define LOG_PARSE_AT(state, at, bytes, fmt, ...) \
	do { \
		xassert(state->magic == LLHTTP_STATE_MAGIC); \
		log_parse(state->buffer, state->name, (at), (bytes), __func__, \
			  fmt, ##__VA_ARGS__); \
	} while (0)

/* on_parse_error() returns 1; multiply by -1 to stop llhttp parser */
#define PARSE_ERROR(error_number, state) \
	(on_parse_error(error_number, state->total_bytes, state->buffer, \
			state->callbacks, state->callback_arg, state->name, \
			NULL, 0, __func__, &state->rc) * \
	 -1)
#define PARSE_ERROR_AT(error_number, state, at, bytes) \
	(on_parse_error(error_number, state->total_bytes, state->buffer, \
			state->callbacks, state->callback_arg, state->name, \
			(at), (bytes), __func__, &state->rc) * \
	 -1)

#define T(l, s) [HPE_##l] = { HPE_##l, s }

static const struct {
	llhttp_errno_t library;
	slurm_err_t slurm;
} errors[] = {
	T(OK, SLURM_SUCCESS),
	T(INTERNAL, ESLURM_HTTP_PARSING_FAILURE),
	T(STRICT, ESLURM_HTTP_PARSING_FAILURE),
	T(CR_EXPECTED, ESLURM_HTTP_MISSING_CR),
	T(LF_EXPECTED, ESLURM_HTTP_MISSING_LF),
	T(UNEXPECTED_CONTENT_LENGTH, ESLURM_HTTP_INVALID_CONTENT_LENGTH),
	T(UNEXPECTED_SPACE, ESLURM_HTTP_INVALID_CHARACTER),
	T(CLOSED_CONNECTION, SLURM_UNEXPECTED_MSG_ERROR),
	T(INVALID_METHOD, ESLURM_HTTP_INVALID_METHOD),
	T(INVALID_URL, ESLURM_URL_INVALID_FORMATING),
	T(INVALID_CONSTANT, ESLURM_HTTP_INVALID_CHARACTER),
	T(INVALID_VERSION, SLURM_PROTOCOL_VERSION_ERROR),
	T(INVALID_HEADER_TOKEN, ESLURM_HTTP_INVALID_CHARACTER),
	T(INVALID_CONTENT_LENGTH, ESLURM_HTTP_INVALID_CONTENT_LENGTH),
	T(INVALID_CHUNK_SIZE, ESLURM_HTTP_INVALID_CHARACTER),
	T(INVALID_STATUS, ESLURM_HTTP_INVALID_STATUS_CODE),
	T(INVALID_EOF_STATE, ESLURM_PROTOCOL_INCOMPLETE_PACKET),
	T(INVALID_TRANSFER_ENCODING, ESLURM_HTTP_INVALID_TRANSFER_ENCODING),
	T(CB_MESSAGE_BEGIN, ESLURM_HTTP_PARSING_FAILURE),
	T(CB_HEADERS_COMPLETE, ESLURM_HTTP_PARSING_FAILURE),
	T(CB_MESSAGE_COMPLETE, ESLURM_HTTP_PARSING_FAILURE),
	T(CB_CHUNK_HEADER, ESLURM_HTTP_PARSING_FAILURE),
	T(CB_CHUNK_COMPLETE, ESLURM_HTTP_PARSING_FAILURE),
	T(PAUSED, ESLURM_HTTP_PARSING_FAILURE), /* we don't support pausing */
	T(PAUSED_UPGRADE, ESLURM_HTTP_PARSING_FAILURE),
	T(PAUSED_H2_UPGRADE, ESLURM_HTTP_PARSING_FAILURE),
	T(USER, ESLURM_HTTP_PARSING_FAILURE),
	T(CB_URL_COMPLETE, ESLURM_HTTP_PARSING_FAILURE),
	T(CB_STATUS_COMPLETE, ESLURM_HTTP_PARSING_FAILURE),
	T(CB_METHOD_COMPLETE, ESLURM_HTTP_PARSING_FAILURE),
	T(CB_VERSION_COMPLETE, ESLURM_HTTP_PARSING_FAILURE),
	T(CB_HEADER_FIELD_COMPLETE, ESLURM_HTTP_PARSING_FAILURE),
	T(CB_HEADER_VALUE_COMPLETE, ESLURM_HTTP_PARSING_FAILURE),
	T(CB_CHUNK_EXTENSION_NAME_COMPLETE, ESLURM_HTTP_PARSING_FAILURE),
	T(CB_CHUNK_EXTENSION_VALUE_COMPLETE, ESLURM_HTTP_PARSING_FAILURE),
	T(CB_RESET, ESLURM_HTTP_PARSING_FAILURE),
};

#undef T

#define T(l, s) [l] = { l, s }

static const struct {
	llhttp_method_t library;
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

#define LLHTTP_STATE_MAGIC 0xEC647B9A

typedef struct http_parser_state_s {
	int magic; /* LLHTTP_STATE_MAGIC */
	const buf_t *buffer; /* Current buffer getting parsed or NULL for EOF */
	const http_parser_callbacks_t *callbacks; /* callbacks for events */
	void *callback_arg; /* pointer to hand to callbacks */
	char *last_header; /* state tracking of last header received */
	const char *name; /* Name of connection for logging */
	llhttp_t parser; /* llhttp parser context */
	int rc; /* Status code to return from http_parser_p_parse_request() */
	llhttp_settings_t settings; /* internal callbacks passed to parser */
	ssize_t total_bytes; /* Bytes already parsed (cumulative) */
	url_t url; /* Requested URL */
} http_parser_state_t;

/* reduce length of typename from long global typedef */
typedef http_parser_state_t state_t;

static void _state_message_reset(state_t *state);
static void _state_parsing_reset(state_t *state);
static void _parse(state_t *state, ssize_t *bytes_parsed_ptr);

extern int init(void)
{
	debug("loaded");
	return SLURM_SUCCESS;
}

extern void fini(void)
{
	debug("unloaded");
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
	state->magic = LLHTTP_STATE_MAGIC;
	state->callbacks = callbacks;
	state->callback_arg = callback_arg;
	state->url = URL_INITIALIZER;
	state->parser.data = state;

	_state_parsing_reset(state);

	*state_ptr = state;
	return SLURM_SUCCESS;
}

extern void http_parser_p_free_parse_request(http_parser_state_t **state_ptr)
{
	state_t *state = NULL;

	xassert(state_ptr);

	SWAP(state, *state_ptr);

	if (!state)
		return;

	xassert(state->magic == LLHTTP_STATE_MAGIC);

	_state_message_reset(state);
	xassert(!state->buffer);
	xassert(state->total_bytes >= 0);
	state->total_bytes = -1;

	state->magic = ~LLHTTP_STATE_MAGIC;
	xfree(state);
}

extern int http_parser_p_parse_request(http_parser_state_t *state,
				       const buf_t *buffer,
				       ssize_t *bytes_parsed_ptr)
{
	xassert(state);
	xassert(state->magic == LLHTTP_STATE_MAGIC);
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

/* Possible return values 0, -1, HPE_USER */
static int _on_url(llhttp_t *parser, const char *at, size_t length)
{
	state_t *state = parser->data;
	buf_t buffer = {
		.magic = BUF_MAGIC,
		.head = (void *) at,
		.processed = length,
		.size = length,
	};
	int rc = -1;

	xassert(state->magic == LLHTTP_STATE_MAGIC);

	if (state->url.scheme != URL_SCHEME_INVALID)
		return PARSE_ERROR_AT(ESLURM_HTTP_UNEXPECTED_URL, state, at,
				      length);

	if ((rc = url_parser_g_parse(state->name, &buffer, &state->url)))
		return PARSE_ERROR_AT(rc, state, at, length);

	return rc;
}

/* Possible return values 0, -1, `HPE_PAUSED` */
static int _on_message_begin(llhttp_t *parser)
{
	/* Do nothing successfully */
	return 0;
}

/* Possible return values 0, -1, HPE_USER */
static int _on_status(llhttp_t *parser, const char *at, size_t length)
{
	/* Do nothing successfully */
	return 0;
}

/* Possible return values 0, -1, HPE_USER */
static int _on_header_field(llhttp_t *parser, const char *at, size_t length)
{
	state_t *state = parser->data;

	xassert(state->magic == LLHTTP_STATE_MAGIC);
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

/* Possible return values 0, -1, HPE_USER */
static int _on_header_value(llhttp_t *parser, const char *at, size_t length)
{
	state_t *state = parser->data;
	http_parser_header_t header = {
		.name = state->last_header,
	};
	char *value = NULL;

	xassert(state->magic == LLHTTP_STATE_MAGIC);

	if (!state->callbacks->on_header)
		return 0;

	if (!state->last_header)
		return PARSE_ERROR_AT(ESLURM_HTTP_EMPTY_HEADER, state, at,
				      length);

	/* Copy value to trim it */
	value = xstrndup(at, length);

	/* Trim header field-value per RFC2616:4.2 */
	xstrtrim(value);

	header.value = value;

	LOG_PARSE_AT(state, at, length, "Parsed Header:%s Value:%s",
		     header.name, header.value);

	state->rc = state->callbacks->on_header(&header, state->callback_arg);

	xfree(state->last_header);
	xfree(value);

	if (state->rc)
		return -1;

	return 0;
}

/* convert llhttp enum to slurm method enum */
static http_request_method_t _get_parser_method(const llhttp_t *parser)
{
	if ((parser->method < ARRAY_SIZE(methods)) &&
	    (methods[parser->method].library == parser->method))
		return methods[parser->method].slurm;

	return HTTP_REQUEST_INVALID;
}

static int _headers_callback(llhttp_t *parser, state_t *state)
{
	http_parser_request_t request = {
		.http_version = {
			.major = parser->http_major,
			.minor = parser->http_minor,
		},
		.method = _get_parser_method(parser),
		.url = &state->url,
	};

	xassert(state->magic == LLHTTP_STATE_MAGIC);

	if ((state->rc = state->callbacks->on_request(&request,
						      state->callback_arg)))
		return -1;

	return 0;
}

/* Possible return values:
 * 0  - Proceed normally
 * 1  - Assume that request/response has no body, and proceed to parsing the
 *      next message
 * 2  - Assume absence of body (as above) and make `llhttp_execute()` return
 *      `HPE_PAUSED_UPGRADE`
 * -1 - Error
 * `HPE_PAUSED`
 */
static int _on_headers_complete(llhttp_t *parser)
{
	state_t *state = parser->data;

	xassert(state->magic == LLHTTP_STATE_MAGIC);

	if (!state->callbacks->on_request)
		return 0;
	else
		return _headers_callback(parser, state);
}

/* Possible return values 0, -1, HPE_USER */
static int _on_body(llhttp_t *parser, const char *at, size_t length)
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

	xassert(state->magic == LLHTTP_STATE_MAGIC);

	LOG_PARSE_AT(state, at, length, "received HTTP body");

	if (state->callbacks->on_content &&
	    (state->rc = state->callbacks->on_content(&content,
						      state->callback_arg)))
		return -1;

	return 0;
}

/* Possible return values 0, -1, `HPE_PAUSED` */
static int _on_message_complete(llhttp_t *parser)
{
	state_t *state = parser->data;

	xassert(state->magic == LLHTTP_STATE_MAGIC);

	LOG_PARSE(state, "message complete");

	if (state->callbacks->on_content_complete &&
	    (state->rc = state->callbacks
				 ->on_content_complete(state->callback_arg)))
		return -1;

	_state_message_reset(state);
	return 0;
}

/* When on_chunk_header is called, the current chunk length is stored
 * in parser->content_length.
 * Possible return values 0, -1, `HPE_PAUSED`
 */
static int _on_chunk_header(llhttp_t *parser)
{
	state_t *state = parser->data;

	xassert(state->magic == LLHTTP_STATE_MAGIC);

	return PARSE_ERROR(ESLURM_HTTP_UNSUPPORTED_CHUNK_ENCODING, state);
}

/* When on_chunk_header is called, the current chunk length is stored
 * in parser->content_length.
 * Possible return values 0, -1, `HPE_PAUSED`
 */
static int _on_chunk_complete(llhttp_t *parser)
{
	state_t *state = parser->data;

	xassert(state->magic == LLHTTP_STATE_MAGIC);

	return PARSE_ERROR(ESLURM_HTTP_UNSUPPORTED_CHUNK_ENCODING, state);
}

static void _set_settings_callbacks(llhttp_settings_t *settings)
{
	xassert(settings);
	settings->on_url = _on_url;
	settings->on_message_begin = _on_message_begin;
	settings->on_status = _on_status;
	settings->on_header_field = _on_header_field;
	settings->on_header_value = _on_header_value;
	settings->on_headers_complete = _on_headers_complete;
	settings->on_body = _on_body;
	settings->on_message_complete = _on_message_complete;
	settings->on_chunk_header = _on_chunk_header;
	settings->on_chunk_complete = _on_chunk_complete;
}

static void _state_message_reset(state_t *state)
{
	xassert(state && (state->magic == LLHTTP_STATE_MAGIC));

	url_free_members(&state->url);
	state->url = URL_INITIALIZER;
	xfree(state->last_header);
}

static void _state_parsing_reset(state_t *state)
{
	xassert(state && (state->magic == LLHTTP_STATE_MAGIC));

	/* llhttp_init clears parser data */
	void *data = state->parser.data;

	llhttp_settings_init(&state->settings);
	_set_settings_callbacks(&state->settings);
	llhttp_init(&state->parser, HTTP_REQUEST, &state->settings);
	state->parser.data = data;
	_state_message_reset(state);
}

/* convert llhttp enum to slurm error enum */
static slurm_err_t _get_parser_error(const llhttp_t *parser)
{
	if ((parser->error < ARRAY_SIZE(errors)) &&
	    (errors[parser->error].library == parser->error))
		return errors[parser->error].slurm;

	return ESLURM_HTTP_PARSING_FAILURE;
}

static void _on_http_parse_error(state_t *state)
{
	http_parser_error_t error = {
		.error_number = _get_parser_error(&state->parser),
		.description = llhttp_get_error_reason(&state->parser),
		.offset = state->total_bytes,
		.at = NULL,
		.at_bytes = -1,
	};

	xassert(state->magic == LLHTTP_STATE_MAGIC);

	LOG_PARSE(state, "Failure parsing HTTP: %s -> %s", __func__,
		  llhttp_errno_name(state->parser.error),
		  llhttp_get_error_reason(&state->parser));

	xfree(state->last_header);

	if (state->rc && (state->rc != SLURM_ERROR))
		return;

	if (state->callbacks->on_parse_error)
		state->rc =
			state->callbacks->on_parse_error(&error,
							 state->callback_arg);
	else
		state->rc = error.error_number;
}

static void _parse(state_t *state, ssize_t *bytes_parsed_ptr)
{
	size_t parsed_bytes = 0;
	void *data = NULL;
	size_t bytes = 0;
	llhttp_errno_t rc = HPE_OK;

	if (state->buffer) {
		xassert(state->buffer->magic == BUF_MAGIC);

		/* Assert that the buffer is populated */
		xassert(get_buf_data(state->buffer));

		data = get_buf_data(state->buffer);
		/*
		 * Never include NULL terminator as library will consider any \0
		 * while parsing headers as an invalid token and false error
		 * out.
		 */
		bytes = get_buf_offset(state->buffer);

		LOG_PARSE(state, "BEGIN: Parsing %zu bytes", bytes);
	} else {
		LOG_PARSE(state, "BEGIN: Parsing EOF after total %zu bytes",
			  state->total_bytes);
	}

	if (state->buffer)
		rc = llhttp_execute(&state->parser, data, bytes);
	else
		rc = llhttp_finish(&state->parser);

	if (rc == HPE_OK) {
		/*
		 * If no error, then all bytes passed in were parsed.
		 * If data contained extra NULL chars at the end this would
		 * return an error.
		 */
		parsed_bytes = bytes;
		state->total_bytes += parsed_bytes;
	} else {
		const char *at = llhttp_get_error_pos(&state->parser);

		parsed_bytes = at ? (size_t) (at - (char *) data) : 0;
		state->total_bytes += parsed_bytes;
		_on_http_parse_error(state);
	}

	if (!state->rc) {
		if (state->buffer) {
			*bytes_parsed_ptr = parsed_bytes;
		} else {
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
