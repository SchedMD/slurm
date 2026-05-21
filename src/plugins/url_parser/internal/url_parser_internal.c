/*****************************************************************************\
 url_parser_internal.c - url parser handler
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

#include "http_parser_url_port.h"

#include "slurm/slurm.h"

#include "src/common/http.h"
#include "src/common/log.h"
#include "src/common/read_config.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/url_parser.h"

/* Required Slurm plugin symbols: */
const char plugin_name[] = "Slurm url_parser plugin";
const char plugin_type[] = URL_PARSER_PREFIX URL_PARSER_INTERNAL_PLUGIN;
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

#define URL_PARSE_ERROR(error_number, name, buffer) \
	_on_url_parse_error(error_number, name, buffer, __func__)
#define LOG_URL_PARSE(name, buffer, fmt, ...) \
	_log_url_parse(name, buffer, __func__, fmt, ##__VA_ARGS__)

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

/*
 * Load and initialize URL parser plugin
 * RET SLURM_SUCCESS or error
 */
extern int url_parser_p_init(void)
{
	return SLURM_SUCCESS;
}

/* Unload URL plugin */
extern void url_parser_p_fini(void) {}

/*
 * Parse URL using a copy of libhttp_parser's URL parser implementation
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
	struct parsed_url url;
	const void *data = get_buf_data(buffer);
	const size_t bytes = get_buf_offset(buffer);
	int rc = EINVAL;

	memset(&url, 0, sizeof(url));

	xassert(data);
	xassert(bytes > 0);
	xassert(size_buf(buffer) >= bytes);

	/* Try parsing a full URL and then try parsing only a host:port pair */
	if (parse_url(data, bytes, false, &url) &&
	    parse_url(data, bytes, true, &url))
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

	if ((bytes < 1) || (data[0] != ':'))
		return ESLURM_URL_UNSUPPORTED_FORMAT;

	if (bytes == 1)
		return ESLURM_URL_EMPTY;

	dst->port = xstrndup((data + 1), (bytes - 1));
	return SLURM_SUCCESS;
}

/*
 * Parse URL where only the port is given.
 * Examples:
 *	unix:/path/to/socket
 *
 * RET
 *	SLURM_SUCCESS: parsed port successfully
 *	ESLURM_URL_UNSUPPORTED_FORMAT: not a port only URL
 *	*: error
 */
static int _parse_unix_url(const char *name, const buf_t *buffer, url_t *dst)
{
	const char *data = get_buf_data(buffer);
	const size_t bytes = get_buf_offset(buffer);

	if ((bytes < UNIX_PREFIX_BYTES) ||
	    xstrncmp(UNIX_PREFIX, data, UNIX_PREFIX_BYTES))
		return ESLURM_URL_UNSUPPORTED_FORMAT;

	if (bytes == UNIX_PREFIX_BYTES)
		return ESLURM_URL_EMPTY;

	dst->scheme = URL_SCHEME_UNIX;
	dst->path = xstrndup((data + UNIX_PREFIX_BYTES),
			     (bytes - UNIX_PREFIX_BYTES));
	return SLURM_SUCCESS;
}

/*
 * Parse URL
 * IN name - name used for logging
 * IN buffer - buffer containing string to parse
 * IN/OUT url - URL to populate with parsed components of URL
 * RET SLURM_SUCCESS or error
 */
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

	rc = _library_url_parse(name, buffer, dst);
	if (rc == ESLURM_URL_UNSUPPORTED_FORMAT) {
		url_free_members(dst);
		rc = _parse_only_port(name, buffer, dst);
	}
	if (rc == ESLURM_URL_UNSUPPORTED_FORMAT) {
		url_free_members(dst);
		rc = _parse_unix_url(name, buffer, dst);
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
	return rc;
}
