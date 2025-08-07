/*****************************************************************************\
 *  HTTP Parser plugin interface
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

#ifndef _INTERFACES_HTTP_PARSER_H
#define _INTERFACES_HTTP_PARSER_H

#include "slurm/slurm_errno.h"

#include "src/common/http.h"
#include "src/common/pack.h"

#define HTTP_PARSER_MAJOR_TYPE "http_parser"
#define HTTP_PARSER_PREFIX HTTP_PARSER_MAJOR_TYPE "/"
#define LIBHTTP_PARSER_PLUGIN "libhttp_parser"

typedef struct {
	struct {
		uint16_t major;
		uint16_t minor;
	} http_version;

	http_request_method_t method;
	const url_t *url;
} http_parser_request_t;

typedef struct {
	const buf_t *buffer;
} http_parser_content_t;

typedef struct {
	const char *name;
	const char *value;
} http_parser_header_t;

typedef struct {
	slurm_err_t error_number; /* error or ESLURM_HTTP_PARSING_FAILURE */
	const char *description;
	/*
	 * Error occurred at (or near) offset in HTTP byte stream or -1 if
	 * unknown or not Applicable
	 */
	ssize_t offset;
	/* Isolated part of stream that caused error of at_bytes or NULL */
	const void *at;
	/*
	 * Number of bytes pointed to by at.
	 *	If at==NULL then at_bytes=-1
	 *	at may be non-NULL with at_bytes=0 (indicating EOF parsing)
	 */
	const ssize_t at_bytes;
} http_parser_error_t;

typedef struct {
	/*
	 * Call back HTTP request is parsed
	 * NOTE: called before headers and body are parsed
	 * RET SLURM_SUCCESS to continue parsing to error to stop
	 */
	int (*on_request)(const http_parser_request_t *request, void *arg);
	/*
	 * Call back on each header received
	 * RET SLURM_SUCCESS to continue parsing to error to stop
	 */
	int (*on_header)(const http_parser_header_t *header, void *arg);
	/*
	 * Call back after all headers received before content or EOF
	 * RET SLURM_SUCCESS to continue parsing to error to stop
	 */
	int (*on_headers_complete)(void *arg);
	/*
	 * Call back after (possibly partial) content/payload was received.
	 * NOTE: content may received in via multiple calls and is not
	 *	considered complete until on_content_complete() is called.
	 * RET SLURM_SUCCESS to continue parsing to error to stop
	 */
	int (*on_content)(const http_parser_content_t *content, void *arg);
	/*
	 * Call back after all content was received
	 * WARNING: Content-Length header may not be enforced before this is
	 *	called or this may be skipped entirely for unexpected EOF.
	 * RET SLURM_SUCCESS to continue parsing to error to stop
	 */
	int (*on_content_complete)(void *arg);
	/*
	 * Error while parsing
	 * RET error to pass along to http_parser_g_parse_request()
	 */
	int (*on_parse_error)(const http_parser_error_t *error, void *arg);
} http_parser_callbacks_t;

/* Pointer to track parsing state */
typedef struct http_parser_state_s http_parser_state_t;

/*
 * Load and initialize http_parser plugin
  * RET SLURM_SUCCESS or error
 */
extern int http_parser_g_init(void);

/* Unload http_parser plugin */
extern void http_parser_g_fini(void);

/*
 * Create new HTTP parsing state for a given connection
 * IN name - Name of connection to log.
 *	Pointer must be valid until http_parser_g_free_parse_request().
 * IN callbacks - event callbacks.
 * IN callback_arg - arbitrary pointer to hand to callback functions.
 * IN state_ptr - Pointer to populate with parsing state.
 *	Pointer must be released via http_parser_g_free_parse_request().
 * RET SLURM_SUCCESS or error
 */
extern int http_parser_g_new_parse_request(const char *name,
					   const http_parser_callbacks_t
						   *callbacks,
					   void *callback_arg,
					   http_parser_state_t **state_ptr);

/*
 * Free http parsing state
 * IN state_ptr - Pointer to release (sets to NULL)
 */
extern void http_parser_g_free_parse_request(http_parser_state_t **state_ptr);

/*
 * Parse connection's incoming HTTP request
 * IN state - Parsing state from http_parser_g_new_parse_request()
 * IN buffer - Input buffer to parse
 *	HTTP to be parsed is expected to be in byte range
 *	[0, get_buf_offset(buffer)) in get_buf_data(buffer).
 *	Buffer must never include NULL terminator in get_buf_offset(buffer).
 * IN/OUT bytes_parsed_ptr
 *	SUCCESS: Populated with number of bytes parsed. Partial parsing is
 *	possible if the buffer lacks the entire HTTP message.
 *	NOTE: Never includes NULL terminator as parsed byte
 *	FAILURE: set to -1
 * RET SLURM_SUCCESS or error
 * WARNING: libhttp_parser will error out for partial message in buffer that
 *	does not include the entire HTTP request line as it will treat the
 *	message as a <http 1.0 request.
 * NOTE: When HTTP stream reads EOF, then pass buffer=NULL to notify plugin that
 *	stream has encountered EOF.
 */
extern int http_parser_g_parse_request(http_parser_state_t *state,
				       const buf_t *buffer,
				       ssize_t *bytes_parsed_ptr);

#endif
