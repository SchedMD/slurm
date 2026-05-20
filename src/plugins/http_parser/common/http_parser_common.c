/*****************************************************************************\
 *  http_parser_common.c - common http_parser plugin code
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

#include "http_parser_common.h"

#include "slurm/slurm.h"

#include "src/common/log.h"
#include "src/common/pack.h"
#include "src/common/read_config.h"
#include "src/common/xmalloc.h"

static void _log_parse_buffer(const buf_t *buffer, const char *name,
			      const char *caller, const char *log_str,
			      const void *at, const size_t at_bytes)
{
	const void *buffer_ptr = get_buf_data(buffer);
	const size_t buffer_bytes = get_buf_offset(buffer);
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
		 name, offset_begin, offset_end, (uintptr_t) buffer_ptr,
		 log_str);
	log_flag_hex_range(NET_RAW, buffer_ptr, buffer_bytes, offset_begin,
			   offset_end, "%s: [%s] %s", caller, name,
			   log_str);
}

extern void log_parse(const buf_t *buffer, const char *name, const void *at,
		      const size_t at_bytes, const char *caller,
		      const char *fmt, ...)
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
		_log_parse_buffer(buffer, name, caller, log_str, at, at_bytes);
	else
		log_flag(DATA, "%s: [%s] PARSE EOF %s", caller, name, log_str);

	xfree(log_str);
}

/*
 * Notify caller that parsing failed
 * NOTE: use plugin's PARSE_ERROR()/PARSE_ERROR_AT() instead of calling directly
 * IN error_number - Slurm error encountered
 * IN total_bytes - Bytes already parsed (cumulative)
 * IN buffer - Current buffer getting parsed
 * IN callbacks - callback to call on events
 * IN callback_arg - pointer to hand to callbacks
 * IN name - Name of connection for logging
 * IN at - pointer to where failure happened or NULL if N/A
 * IN at_bytes - number of bytes at "at" or ignored if "at" is NULL
 * IN caller - function that caught error
 * OUT rc - error code
 * RET 1 - always 1 to return to http_parser to stop parsing
 */
extern int on_parse_error(slurm_err_t error_number, ssize_t total_bytes,
			  const buf_t *buffer,
			  const http_parser_callbacks_t *callbacks,
			  void *callback_arg, const char *name, const void *at,
			  const size_t at_bytes, const char *caller, int *rc)
{
	http_parser_error_t error = {
		.error_number = error_number,
		.offset = total_bytes,
		.at = at,
		.at_bytes = (at ? at_bytes : -1),
	};

	if (at) {
		xassert(buffer);
		xassert(at >= (const void *) get_buf_data(buffer));
		xassert((at + at_bytes) <=
			(const void *) (get_buf_data(buffer) +
					get_buf_offset(buffer)));

		/* Shift total_bytes to at pointer */
		error.offset += (at - (const void *) get_buf_data(buffer));
	}

	log_parse(buffer, name, (at), (at_bytes), __func__,
		  "Parsing failed: %s", slurm_strerror(error_number));

	if (callbacks->on_parse_error)
		*rc = callbacks->on_parse_error(&error, callback_arg);
	else
		*rc = error_number;

	return 1;
}
