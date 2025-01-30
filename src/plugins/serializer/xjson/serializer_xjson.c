/*****************************************************************************\
 *  serializer_xjson.c - Serializer for JSON6
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

#include <float.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/slurm_xlator.h"

#include "src/common/data.h"
#include "src/common/pack.h"
#include "src/common/read_config.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/xutf.h"

#include "src/interfaces/data_parser.h"
#include "src/interfaces/serializer.h"

/*
 * These variables are required by the generic plugin interface.  If they
 * are not found in the plugin, the plugin loader will ignore it.
 *
 * plugin_name - A string giving a human-readable description of the
 * plugin.  There is no maximum length, but the symbol must refer to
 * a valid string.
 *
 * plugin_type - A string suggesting the type of the plugin or its
 * applicability to a particular form of data or method of data handling.
 * If the low-level plugin API is used, the contents of this string are
 * unimportant and may be anything.  Slurm uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *	<application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "auth" for Slurm authentication) and <method> is a
 * description of how this plugin satisfies that application.  Slurm will
 * only load authentication plugins if the plugin_type string has a prefix
 * of "auth/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char plugin_name[] = "Serializer xJSON plugin";
const char plugin_type[] = "serializer/xjson";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;
const char *mime_types[] = { "application/json", "application/jsonrequest",
			     NULL };

/*
 * Maximum number of nested containers (dicts and lists) in a document,
 * counting the root.
 */
#define MAX_DEPTH 50

/*
 * Fixed size string for printf() style dumping on stack.
 *
 * Must be large enough for the widest fixed format dumped: MAX_DEPTH tabs of
 * indentation along with the surrounding "," and newline.
 */
#define FMT_STR_BYTES ((MAX_DEPTH * 2) + 8)

#define DUMP_BUF_BYTES BUF_SIZE
#define DUMP_BUF_GROW_BYTES BUF_SIZE

/* Match serializer/json so both plugins serving application/json agree */
#define SERIALIZER_XJSON_DEFAULT_FLAGS SER_FLAGS_PRETTY

/*
 * Logging here is excessive and called multiple times for every single byte
 * which makes even a simple if() expensive. We are disabling the checks here
 * outside of developer mode instead of trying to cache or reduce the costs
 * here. The logging is being kept as bugs here are extremely hard to track down
 * without them though and can easily pop up given the nature of the utf8
 * characters involved.
 *
 * _is_trace_enabled() is the single gate every trace macro tests. Outside of
 * developer builds it returns a constant false, which folds away along with the
 * call it guards, and at run time it requires both DebugFlags=Data and debug5
 * as the traces fire for every byte.
 */
static bool _is_trace_enabled(void)
{
#ifndef NDEBUG
	return ((slurm_conf.debug_flags & DEBUG_FLAG_DATA) &&
		(get_log_level() >= LOG_LEVEL_DEBUG5));
#else
	return false;
#endif
}

#define LOG(fmt, ...) \
	do { \
		if (_is_trace_enabled()) \
			log_flag(DATA, "%s: " fmt, __func__, ##__VA_ARGS__); \
	} while (false)
#define LOG_HEX(data, len, fmt, ...) \
	do { \
		if (_is_trace_enabled()) \
			log_flag_hex(DATA, (data), (len), "%s: " fmt, \
				     __func__, ##__VA_ARGS__); \
	} while (false)
#define LOG_HEX_RANGE(data, len, start, end, fmt, ...) \
	do { \
		if (_is_trace_enabled()) \
			log_flag_hex_range(DATA, (data), (len), (start), \
					   (end), "%s: " fmt, __func__, \
					   ##__VA_ARGS__); \
	} while (false)
#define parse_error(state, utf, rc, fmt, ...) \
	_parse_log(state, LOG_LEVEL_ERROR, utf, rc, __func__, \
		   fmt, ##__VA_ARGS__)
#define parse_debug(state, utf, fmt, ...) \
	do { \
		if (_is_trace_enabled()) \
			_parse_log(state, LOG_LEVEL_DEBUG, utf, SLURM_SUCCESS, \
				   __func__, fmt, ##__VA_ARGS__); \
	} while (0)

#define dump_error(state, index, utf, rc, fmt, ...) \
	_dump_log(state, index, LOG_LEVEL_ERROR, utf, rc, __func__, \
		  fmt, ##__VA_ARGS__)

#define dump_debug(state, index, utf, fmt, ...) \
	do { \
		if (_is_trace_enabled()) \
			_dump_log(state, index, LOG_LEVEL_DEBUG, utf, \
				  SLURM_SUCCESS, __func__, \
				  fmt, ##__VA_ARGS__); \
	} while (0)

typedef enum {
	PARSE_INVALID = 0,
	PARSE_TOKEN, /* Expecting token to parse */
	PARSE_COMMENT_LINE, /* comment is //.*$ */
	PARSE_COMMENT_SPAN, /* span comment opened (slash-star) */
	PARSE_COMMENT_POUND, /* comment is #.*$ */
	PARSE_STRING, /* string (" or ' or unquoted) */
	PARSE_DICT, /* dictionary */
	PARSE_LIST, /* list */
	PARSE_INVALID_MAX,
} parse_mode_t;

typedef enum {
	/* Parsed / for / *...* / comment */
	PARSE_OPEN_STAR = SLURM_BIT(0),
	/* Parsed / *...* for / *...* / comment */
	PARSE_CLOSE_STAR = SLURM_BIT(1),
	/* Start of escape */
	PARSE_BACKSLASH = SLURM_BIT(2),
	/* release dst pointer */
	PARSE_FREE_DST = SLURM_BIT(3),
	/* parsing child data for parent on stack */
	PARSE_CHILD = SLURM_BIT(4),
	/* parsing expecting comma  */
	PARSE_NEED_COMMA = SLURM_BIT(5),
	/* Parsed key: */
	PARSE_KEY = SLURM_BIT(6),
	/* Escaped Hex code */
	PARSE_HEX = SLURM_BIT(7),
	/* Escaped UTF code - High surrogate */
	PARSE_UTF_HIGH = SLURM_BIT(8),
	/* Escaped UTF code - Low surrogate */
	PARSE_UTF_LOW = SLURM_BIT(9),
	/* Double " quoted string */
	PARSE_DOUBLE_QUOTED = SLURM_BIT(10),
	/* Single ' quoted string */
	PARSE_SINGLE_QUOTED = SLURM_BIT(11),
	/* unquoted string */
	PARSE_UNQUOTED = SLURM_BIT(12),
	/* Swallowed a backslash-CR continuation, an LF may follow */
	PARSE_CONTINUATION_CR = SLURM_BIT(13),
} parse_status_t;

typedef struct {
	parse_mode_t mode;
	parse_status_t status;
	/* Destination being parsed */
	data_t *dst;
	/* Parsed key */
	data_t *key;
} parse_stack_t;

#define PARSE_STATE_MAGIC 0x33affeef

typedef struct serialize_parse_state_s {
	int magic; /* PARSE_STATE_MAGIC */

	/* Current line being parsed (starts at 1) */
	int line;
	/* Current line's column being parsed */
	int col;

	enum {
		/* Checked for BOM */
		PARSED_BOM = SLURM_BIT(0),
		/* parse_error() has already reported this parse's failure */
		PARSED_ERROR_LOGGED = SLURM_BIT(1),
		/* Previous character was CR: an LF next is the same line ending */
		PARSED_CR = SLURM_BIT(2),
	} status;

	/* Buffer to hold string being parsed */
	buf_t *string;

	/* Buffer to hold escaped chars (8 for full surrogate) */
	utf8_t escaped[8];
	/* Number of populated bytes in escaped[] */
	uint8_t escaped_bytes;

	/* source buffer being read */
	buf_t *src;
	/* Fully parsed destination */
	data_t *dst;

	/*
	 * Frame 0 is the root token, frames 1..MAX_DEPTH the nested containers,
	 * and one more the string or comment inside the deepest of them
	 */
	parse_stack_t stack[MAX_DEPTH + 2];
	/* Current position in stack[] */
	int index;
} serialize_parse_state_t;

typedef serialize_parse_state_t parse_state_t;

typedef struct {
	const data_t *src;

	enum {
		/* \s{ or \s[ was dumped */
		DUMPED_BLOCK_ENTER = SLURM_BIT(0),
		/* dict/list contents was dumped */
		DUMPED_BLOCK = SLURM_BIT(1),
		/* \s, or \s was dumped */
		DUMPED_KEY_ENTER = SLURM_BIT(2),
		/* Key was dumped */
		DUMPED_KEY = SLURM_BIT(3),
		/* : dumped */
		DUMPED_VALUE_ENTER = SLURM_BIT(4),
	} status;
} dump_stack_t;

typedef struct {
	/* string being dumped */
	const utf8_t *src;
	/* offset in string being dumped */
	uint32_t offset;
	uint32_t bytes;

	enum {
		/* First " was dumped */
		DUMPED_QUOTE = SLURM_BIT(0),
		/* String was dumped */
		DUMPED_STRING = SLURM_BIT(1),
	} status;
} dump_string_state_t;

typedef struct {
	/* printf() string being dumped */
	const char *fmt;
	/* generated string being dumped */
	utf8_t str[FMT_STR_BYTES];
	/* offset in string being dumped */
	uint32_t offset;
	/* number of populated bytes in string to dump */
	int32_t bytes;
} dump_fmt_string_state_t;

#define DUMP_STATE_MAGIC 0x13affe0f

typedef struct serialize_dump_state_s {
	int magic; /* DUMP_STATE_MAGIC */
	int rc;

	/*
	 * Source owned by this state and released by _dump_state_free().
	 * NULL when the caller retains ownership of the source. The dump
	 * clears stack[] as it completes, so the pointer can not be recovered
	 * from stack[0].src once dumping has succeeded.
	 */
	data_t *free_src;

	/*
	 * Two frames per nesting level: the container at 2n and the marker for
	 * the child being written at 2n + 1. The child's own frame is 2n + 2,
	 * which is the next level's container frame, so MAX_DEPTH containers
	 * with a value inside the deepest need 2 * MAX_DEPTH + 1 frames.
	 */
	dump_stack_t stack[(MAX_DEPTH * 2) + 1];

	/* Destination buffer */
	buf_t *dst;

	/* dst offset at start of last _dump() */
	uint32_t dump_start;

	/* Dump quoted string */
	dump_string_state_t string;
	/* Dump formatted string */
	dump_fmt_string_state_t fmt_string;

	serializer_flags_t flags;
} serialize_dump_state_t;

typedef serialize_dump_state_t dump_state_t;

#define DUMP_DICT_ARGS_MAGIC 0x235ffefa

typedef struct {
	int magic; /* DUMP_DICT_ARGS_MAGIC */
	serialize_dump_state_t *state;
	int index;
	/* True if first item in foreach */
	bool first;
} dump_dict_args_t;

#define DUMP_LIST_ARGS_MAGIC 0xfbaffefa

typedef struct {
	int magic; /* DUMP_LIST_ARGS_MAGIC  */
	serialize_dump_state_t *state;
	int index;
	/* True if first item in foreach */
	bool first;
	int list_index;
} dump_list_args_t;

/*
 * List of JSON support escape characters.
 *
 * Expanded twice, once per table below, so escaped_chars[] and
 * dump_escape_row[] can not disagree. The row number is explicit because
 * dump_escape_row[] holds it to index back into escaped_chars[].
 */
#define ESCAPED_CHARS \
	T(0, '\"', "\\\"", '\"', false) \
	T(1, '\'', "\\\'", '\'', true) \
	T(2, '\\', "\\\\", '\\', false) \
	T(3, '/', "\\/", '/', false) \
	T(4, 'b', "\\b", '\b', false) \
	T(5, 'f', "\\f", '\f', false) \
	T(6, 'n', "\\n", '\n', false) \
	T(7, 'r', "\\r", '\r', false) \
	T(8, 't', "\\t", '\t', false) \
	T(9, 'v', "\\v", '\v', true)

/* \0 is not allowed in Slurm */

static const struct escaped_char {
	utf_code_t utf;
	char escaped[3];
	char value;
	/*
	 * True if the character must be dumped as a \uXXXX escape instead of
	 * this escape sequence. The parser accepts the wider JSON6 escape set
	 * but RFC 8259 only allows \" \\ \/ \b \f \n \r \t \uXXXX, so every
	 * escape outside of that set is dumped as \uXXXX to avoid emitting
	 * JSON that strict parsers reject.
	 */
	bool dump_utf16;
} escaped_chars[] = {

#define T(row, utf, escaped, value, dump_utf16) \
	[row] = { utf, escaped, value, dump_utf16 },
	ESCAPED_CHARS
#undef T
};

/*
 * Row of escaped_chars[] plus one for each ASCII code the dumper must escape,
 * 0 for the rest. Consulted once per character written instead of scanning the
 * table for every byte of every string.
 *
 * Built from the same list as escaped_chars[] at compile time, so the two can
 * not disagree, the table is read only, and a value that is not ASCII is a
 * compile error here rather than a write past the end of the array.
 */
static const uint8_t dump_escape_row[UTF_ASCII_MAX_CODE + 1] = {
#define T(row, utf, escaped, value, dump_utf16) \
	[(unsigned char) (value)] = ((row) + 1),
	ESCAPED_CHARS
#undef T
};

static int _dump_data(dump_state_t *state, int index);
static int _dump_quoted_utf16(dump_state_t *state, const int index,
			      utf_code_t utf, int utf_bytes);
static int _parse_utf(parse_state_t *state, const int index,
		      const utf_code_t utf);
static int _parse_token(parse_state_t *state, const int index,
			const utf_code_t utf, data_t *dst,
			parse_status_t status);

/*
 * Indentation for the nesting depth a stack[] index sits at
 * IN index - dump stack[] index. Dumping uses two frames per nesting level, so
 *	the depth is half of it.
 * RET depth tabs, pointing into indent_tabs[]
 */
static const char *_indent(const int index)
{
	static const char indent_tabs[] =
		"\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t"
		"\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t";
	const int depth = (index / 2);

	_Static_assert((sizeof(indent_tabs) - 1) == MAX_DEPTH,
		       "indent_tabs must hold exactly MAX_DEPTH tabs");
	xassert(depth >= 0);
	xassert(depth <= MAX_DEPTH);

	return &indent_tabs[MAX_DEPTH - depth];
}

static int _dump_log(dump_state_t *state, const int index,
		     const log_level_t level, utf_code_t utf, int rc,
		     const char *func_name, const char *fmt, ...)
{
	char *log = NULL;
	utf8_t str[UTF8_CHAR_MAX_BYTES] = { 0 };
	int bytes = -1;

	{
		va_list ap;

		va_start(ap, fmt);
		log = vxstrfmt(fmt, ap);
		va_end(ap);
	}

	if (utf8_write_character(utf8_get_loggable(utf), str, &bytes))
		fatal_abort("should never happen");

	xassert(bytes < sizeof(str));

	log_var(level, "%s%s[%p+%u] [%04zu]=U+%06" PRIx32 "=%s %s",
		(func_name ? func_name : ""), (func_name ? ": " : ""), state,
		index, (size_t) get_buf_offset(state->dst), utf, str, log);

	xfree(log);
	return rc;
}

/* Append fixed size printf-formatted string to buf */
static int _dump_str_fmt(dump_state_t *state, const char *fmt, ...)
{
	dump_fmt_string_state_t *fstr = &state->fmt_string;
	uint32_t bytes = 0;

	xassert(state->magic == DUMP_STATE_MAGIC);
	xassert(state->dst->magic == BUF_MAGIC);
	xassert(fmt);

	if (!fstr->bytes) {
		va_list ap;

		xassert(!fstr->fmt);
		xassert(!fstr->offset);
		fstr->fmt = fmt;

		/* Generate formatted string */
		va_start(ap, fmt);
		fstr->bytes = vsnprintf((char *) fstr->str, sizeof(fstr->str),
					fmt, ap);
		va_end(ap);

		xassert(fstr->bytes >= 0);
		if (fstr->bytes < 0)
			return EINVAL;

		/* catch str buffer not being large enough */
		xassert(fstr->bytes < sizeof(fstr->str));
		if (fstr->bytes >= sizeof(fstr->str))
			return EINVAL;
	}

	/* catch state being from wrong source format string */
	xassert(fstr->fmt == fmt);
	xassert(fstr->offset <= fstr->bytes);

	/*
	 * Never fill dst completely, so that a spare byte is always left for
	 * whoever hands the buffer back as a C string to place a NUL
	 * terminator in. serialize_p_data_to_string() is the one that writes
	 * it; serialize_p_dump() only guarantees the byte is there and leaves
	 * writing it to the caller.
	 */
	if ((bytes = remaining_buf(state->dst)))
		bytes--;

	if (bytes > (fstr->bytes - fstr->offset))
		bytes = (fstr->bytes - fstr->offset);

	if (bytes > 0) {
		const utf8_t *str = (fstr->str + fstr->offset);
		int rc = EINVAL;

		xassert(bytes >= 0);

		if ((rc = buf_append_bytes(state->dst, str, bytes)))
			return rc;

		fstr->offset += bytes;
		xassert(fstr->offset <= fstr->bytes);
	}

	if (fstr->offset < fstr->bytes)
		return ENOSPC;

	*fstr = (dump_fmt_string_state_t) { 0 };
	return SLURM_SUCCESS;
}

/* Concatenate UTF-8 character onto state buffer */
static int _dump_utf8(dump_state_t *state, const int index,
		      const utf_code_t utf)
{
	/* Always allow for write of UTF_REPLACEMENT_CODE */
	utf8_t str[UTF8_CHAR_MAX_BYTES] = { 0 };
	buf_t *dst = state->dst;
	int bytes = -1;
	int rc = EINVAL;

	xassert(state->magic == DUMP_STATE_MAGIC);
	xassert(state->dst->magic == BUF_MAGIC);

	/* Reserve a byte for the NUL terminator: see _dump_str_fmt() */
	if (remaining_buf(dst) <= sizeof(str))
		return ENOSPC;

	if ((rc = utf8_write_character(utf, str, &bytes)))
		return rc;

	xassert(bytes > 0);
	xassert(bytes <= UTF8_CHAR_MAX_BYTES);

	if ((rc = buf_append_bytes(dst, str, bytes)))
		dump_debug(state, index, utf, "dumping utf failed: %s",
			   slurm_strerror(rc));
	else
		dump_debug(state, index, utf, "dumped utf");

	return rc;
}

/* Concatenate whole string literal onto state buffer */
static int _dump_str(dump_state_t *state, const int index, const char *src)
{
	const size_t bytes = strlen(src);
	int rc = EINVAL;

	xassert(state->magic == DUMP_STATE_MAGIC);
	xassert(state->dst->magic == BUF_MAGIC);

	/* Reserve a byte for the NUL terminator: see _dump_str_fmt() */
	if (remaining_buf(state->dst) <= bytes)
		return ENOSPC;

	if ((rc = buf_append_bytes(state->dst, src, bytes)))
		LOG_HEX(src, bytes, "[%p+%u] dumped string failed: %s", state,
			index, slurm_strerror(rc));
	else
		LOG_HEX(src, bytes, "[%p+%u] dumped string", state, index);

	return rc;
}

static int _dump_escaped(dump_state_t *state, const int index, utf_code_t utf,
			 bool *found_ptr)
{
	const struct escaped_char *e = NULL;

	/*
	 * Every JSON-escapable byte is ASCII, so a non-ASCII code can never
	 * match one of the escaped characters.
	 */
	if (utf > UTF_ASCII_MAX_CODE)
		return SLURM_SUCCESS;

	/* not found */
	if (!dump_escape_row[utf])
		return SLURM_SUCCESS;

	e = &escaped_chars[dump_escape_row[utf] - 1];
	*found_ptr = true;

	/*
	 * Escape sequence is only accepted while parsing. Every character here
	 * is ASCII as non-ASCII codes were rejected above, so it is always 1
	 * byte of UTF-8.
	 */
	if (e->dump_utf16)
		return _dump_quoted_utf16(state, index, utf, 1);

	dump_debug(state, index, utf, "escaped U+%06" PRIx32 "=\\%c", utf,
		   e->utf);

	return _dump_str(state, index, e->escaped);
}

static int _dump_quoted_utf16(dump_state_t *state, const int index,
			      utf_code_t utf, int utf_bytes)
{
	int rc = EINVAL;
	utf16_t high = 0, low = 0;

	if ((rc = utf16_from_coding(utf, &high, &low)))
		return dump_error(state, index, utf, rc, "%s",
				  slurm_strerror(rc));

	if (low) {
		dump_debug(state, index, utf,
			   "Dumping %d bytes UTF-8 character as \\u%04" PRIx16
			   "\\u%04" PRIx16,
			   utf_bytes, high, low);

		return _dump_str_fmt(state, "\\u%04" PRIx16 "\\u%04" PRIx16,
				     high, low);
	} else {
		dump_debug(state, index, utf,
			   "Dumping %d bytes UTF-8 character as \\u%04" PRIx16,
			   utf_bytes, high);

		return _dump_str_fmt(state, "\\u%04" PRIx16, high);
	}
}

/*
 * Dump bytes that can not be represented as JSON text.
 *
 * RFC 8259 requires JSON text to be valid Unicode, so a string holding bytes
 * that are not well formed UTF-8, or that encode a code point utf_is_valid()
 * rejects, can not be dumped as conformant JSON by any escaping at all. A
 * \uXXXX escape names a code point and not a byte: the parser decodes it and
 * re-encodes it as UTF-8, so à rebuilds as 2 bytes and never as the byte
 * 0xe0.
 *
 * SER_FLAGS_JSON6 picks which way to break:
 *	set - dump the source bytes as \xNN escapes, which round trip exactly
 *	      but are JSON6 only and are rejected by strict parsers
 *	clear - substitute U+FFFD, which loses the bytes but always leaves
 *	        valid RFC 8259 output
 *
 * This is deliberately not SER_FLAGS_COMPLEX. That flag is about values that
 * have no JSON representation (Infinity and NaN) and is set implicitly by
 * slurmrestd and serdes whenever the data_parser is complex, which would have
 * put \xNN escapes into responses that nothing asked to be JSON6.
 */
static int _dump_unrepresentable(dump_state_t *state, const int index,
				 const utf8_t *ptr, const int bytes)
{
	xassert(bytes > 0);
	xassert(bytes < UTF8_CHAR_MAX_BYTES);

	if (!(state->flags & SER_FLAGS_JSON6)) {
		LOG_HEX(ptr, bytes,
			"[%p+%u] replaced unrepresentable bytes with U+FFFD",
			state, index);
		return _dump_utf8(state, index, UTF_REPLACEMENT_CODE);
	}

	LOG_HEX(ptr, bytes, "[%p+%u] dumping unrepresentable bytes as \\xNN",
		state, index);

	/*
	 * Each group must be a single _dump_str_fmt() call as it only tracks
	 * one partially written format string for ENOSPC resumption.
	 */
	switch (bytes) {
	case 1:
		return _dump_str_fmt(state, "\\x%02" PRIx8, ptr[0]);
	case 2:
		return _dump_str_fmt(state, "\\x%02" PRIx8 "\\x%02" PRIx8,
				     ptr[0], ptr[1]);
	case 3:
		return _dump_str_fmt(state,
				     "\\x%02" PRIx8 "\\x%02" PRIx8
				     "\\x%02" PRIx8,
				     ptr[0], ptr[1], ptr[2]);
	case 4:
		return _dump_str_fmt(state,
				     "\\x%02" PRIx8 "\\x%02" PRIx8
				     "\\x%02" PRIx8 "\\x%02" PRIx8,
				     ptr[0], ptr[1], ptr[2], ptr[3]);
	default:
		fatal_abort("%s: invalid byte count: %d", __func__, bytes);
	}
}

static int _dump_quoted_string_next(dump_state_t *state, const int index)
{
	int rc = EINVAL, utf_bytes = 0;
	utf_code_t utf = -1;
	bool escape = false;
	const utf8_t *end = (state->string.src + state->string.bytes);
	const utf8_t *ptr = (state->string.src + state->string.offset);

	/*
	 * Test rc first: a failed read leaves utf as UTF_REPLACEMENT_CODE,
	 * which is itself a valid code that utf_is_valid() would accept.
	 */
	if ((rc = utf8_read_character(ptr, end, &utf, &utf_bytes, false)) ||
	    utf_is_valid(utf)) {
		/*
		 * Either the bytes are not well formed UTF-8 or they encode a
		 * code point Slurm rejects. The value can not be named as a
		 * JSON code point, so dump the source bytes instead. The parser
		 * rejects these codes in the raw body and in a \uXXXX escape,
		 * but the JSON6 \xNN escape appends its byte without
		 * validating it, so a string parsed from JSON6 can reach here.
		 */
		if ((rc = _dump_unrepresentable(state, index, ptr, utf_bytes)))
			return rc;
	} else {
		/*
		 * utf8_is_control() excludes these characters as they normally
		 * have their own escape sequences:
		 *	TAB (horizontal tab)
		 *	LF  (NL line feed, new line)
		 *	VT  (vertical tab)
		 *	FF  (NP form feed, new page)
		 *	CR  (carriage return)
		 * RFC 8259 has an escape for every one of them except VT, which
		 * escaped_chars[] dumps as a \uXXXX escape instead.
		 */
		if (utf8_is_control(utf))
			escape = true;
		else if ((state->flags & SER_FLAGS_COMPACT) &&
			 utf8_is_newline(utf))
			escape = true;

		if (escape) {
			if ((rc = _dump_quoted_utf16(state, index, utf,
						     utf_bytes)))
				return rc;
		} else {
			bool found = false;

			/* Handle escaped character */
			if ((rc = _dump_escaped(state, index, utf, &found)))
				return rc;

			/* Print non-escaped character */
			if (!found && (rc = _dump_utf8(state, index, utf)))
				return rc;
		}
	}

	xassert(utf_bytes > 0);
	xassert(utf_bytes < UTF8_CHAR_MAX_BYTES);
	state->string.offset += utf_bytes;
	xassert(state->string.offset <= state->string.bytes);
	xassert(!rc);
	return rc;
}

/*
 * Verify that last call to _dump_quoted_string() completed.
 *
 * Several callers run this immediately before a _dump_str_fmt() that is itself
 * resumable. Once that call has returned ENOSPC, the format string state stays
 * populated until a later call finishes writing it, so requiring it to be
 * zeroed here rejected valid states: closing a list or dict whose last element
 * left only the reserved NUL byte free does exactly that.
 *
 * Skip the zeroed check while such a write is pending and verify the retained
 * state is coherent instead. _dump_str_fmt() separately confirms the retained
 * state belongs to the format string it is handed.
 */
static void _assert_dump_string_zero(dump_state_t *state)
{
#ifndef NDEBUG
	static const dump_string_state_t str_zero = { 0 };
	static const dump_fmt_string_state_t fstr_zero = { 0 };

	xassert(state->magic == DUMP_STATE_MAGIC);
	xassert(!memcmp(&state->string, &str_zero, sizeof(str_zero)));

	if (state->fmt_string.bytes) {
		/* Partially dumped format string is waiting to be resumed */
		xassert(state->fmt_string.fmt);
		xassert(state->fmt_string.bytes > 0);
		xassert(state->fmt_string.offset <
			(uint32_t) state->fmt_string.bytes);
		return;
	}

	xassert(!memcmp(&state->fmt_string, &fstr_zero, sizeof(fstr_zero)));
#endif
}

/*
 * Length of a string being dumped.
 *
 * _dump_quoted_string() only records src/bytes on the first call, so on an
 * ENOSPC resume the length is already known. Reuse it rather than rescanning
 * the whole string once per resume round.
 */
static uint32_t _dump_string_bytes(dump_state_t *state, const char *str)
{
	ssize_t len = 0;

	if (state->string.src) {
		xassert(strlen(str) == state->string.bytes);
		len = state->string.bytes;
	} else {
		len = strlen(str);
	}

	xassert(len >= 0);
	xassert(len < MAX_BUF_SIZE);
	return len;
}

static int _dump_quoted_string(dump_state_t *state, const int index,
			       const utf8_t *src, const uint32_t bytes)
{
	uint32_t start_offset = 0;
	int rc = EINVAL;

	/* Dump empty strings instantly */
	if (!bytes) {
		_assert_dump_string_zero(state);
		rc = _dump_str_fmt(state, "\"\"");
		LOG("[%p+%u] dumped empty string @ %p: %s",
		    state, index, src, slurm_strerror(rc));
		return rc;
	}

	if (!state->string.src) {
		xassert(src);
		xassert(bytes > 0);
		xassert(bytes < NO_VAL);
		_assert_dump_string_zero(state);

		state->string = (dump_string_state_t) {
			.src = src,
			.bytes = bytes,
		};
	}

	/* catch the source string changing */
	xassert(state->string.src == src);
	xassert(state->string.bytes == bytes);
	xassert(state->string.offset <= bytes);
	xassert(*(src + bytes) == '\0');

	if (!(state->string.status & DUMPED_QUOTE) &&
	    (rc = _dump_utf8(state, index, '\"')))
		return rc;

	state->string.status |= DUMPED_QUOTE;
	start_offset = state->string.offset;

	if (!(state->string.status & DUMPED_STRING)) {
		while (state->string.offset < state->string.bytes) {
			if ((rc = _dump_quoted_string_next(state, index))) {
				LOG_HEX_RANGE(src, bytes, start_offset,
					      state->string.offset,
					      "[%p+%u] dumped partial quoted string",
					      state, index);
				return rc;
			}
		}
	}

	state->string.status |= DUMPED_STRING;
	xassert(state->string.offset == state->string.bytes);

	if ((rc = _dump_utf8(state, index, '\"')))
		return rc;

	LOG_HEX_RANGE(src, bytes, start_offset, state->string.offset,
		      "[%p+%u] dumped %s quoted string", state, index,
		      (!start_offset ? "complete" : "ending of"));

	/* Only clear after the closing quote has been dumped */
	state->string = (dump_string_state_t) { 0 };
	return SLURM_SUCCESS;
}

static int _dump_float(dump_state_t *state, const int index, const data_t *src)
{
	double f = NAN;
	bool is_inf = true, is_nan = true;

	xassert(data_get_type(src) == DATA_TYPE_FLOAT);

	f = data_get_float(src);
	is_inf = isinf(f);
	is_nan = isnan(f);

	/*
	 * RFC4627 and ECMA-262 section 24.5.2:
	 *	Finite numbers are stringified as if by calling
	 *	ToString(number). NaN and Infinity regardless of sign are
	 *	represented as the String null.
	 *
	 * The relevant standards say we should coerce basically everything
	 * that's not a number (or defined in the std) into null but every
	 * implementation of JSON found will honor +-Infinity and +-NaN as
	 * unquoted strings. Dump them under SER_FLAGS_COMPLEX so that
	 * information is not lost during conversion to JSON. The parse, dump,
	 * parse round trip in test_serializer.c compares the two trees for
	 * equivalency and needs the value preserved.
	 */

	if (is_inf || is_nan) {
		if (!(state->flags & SER_FLAGS_COMPLEX))
			return _dump_str(state, index, "null");

		/*
		 * JSON6 allows:
		 *	Numbers can include Infinity, -Infinity, NaN, and -NaN.
		 *	(-NaN results as NaN)
		 */
		if (!signbit(f)) {
			if (is_inf)
				return _dump_str(state, index, "Infinity");
			else if (is_nan)
				return _dump_str(state, index, "NaN");
		} else {
			if (is_inf)
				return _dump_str(state, index, "-Infinity");
			else if (is_nan)
				return _dump_str(state, index, "-NaN");
		}
	}

	/*
	 * %e prints one digit before the point, so a precision of N yields N+1
	 * significant digits. A double needs DBL_DECIMAL_DIG (17) of them to
	 * dump and reparse unchanged, hence the -1. Asking for fewer silently
	 * changes the value; asking for more only spells out further binary to
	 * decimal expansion, which reparses identically but implies precision
	 * the value never had:
	 *
	 *	34.821400825097506
	 *	  %.15e -> 3.482140082509751e+01    reparses as another double
	 *	  %.16e -> 3.4821400825097506e+01   exact (what we emit)
	 *	  %.17e -> 3.48214008250975056e+01  exact, trailing 6 is noise
	 */
	return _dump_str_fmt(state, "%.*e", (DBL_DECIMAL_DIG - 1),
			     data_get_float(src));
}

static data_for_each_cmd_t _dump_list_foreach(const data_t *src, void *arg)
{
	dump_list_args_t *args = arg;
	dump_state_t *state = args->state;
	const int index = args->index;
	dump_stack_t *stack = &state->stack[index];
	dump_stack_t *stack_value = &state->stack[index + 1];
	const bool first = args->first;
	const int list_index = args->list_index;

	xassert(args->magic == DUMP_LIST_ARGS_MAGIC);
	xassert(state->magic == DUMP_STATE_MAGIC);
	xassert((index + 1) < ARRAY_SIZE(state->stack));
	xassert(args->list_index >= 0);

	args->first = false;
	args->list_index++;

	if (!stack->src) {
		*stack = (dump_stack_t) {
			.src = src,
		};
		_assert_dump_string_zero(state);
		xassert(!stack_value->src);
		LOG("[%p+%u] enter LIST+%d %pD", state, index, list_index,
		    stack->src);
	} else if (stack->src && (src != stack->src)) {
		/*
		 * Return to prior dump's stack position by searching values
		 * until match found. Assumes foreach walks the list the same
		 * every time.
		 */
		LOG("[%p+%u] skip LIST+%d %pD", state, index, list_index, src);
		return DATA_FOR_EACH_CONT;
	} else {
		LOG("[%p+%u] resume LIST+%d %pD",
		    state, index, list_index, src);
	}

	if (!stack_value->src) {
		xassert(!(stack->status & DUMPED_KEY_ENTER));
		xassert(!(stack->status & DUMPED_BLOCK_ENTER));

		if (!(stack->status & DUMPED_VALUE_ENTER) &&
		    (state->rc =
			     _dump_str_fmt(state, "%s%s%s", (first ? "" : ","),
					   ((state->flags & SER_FLAGS_PRETTY) ?
						    "\n" :
						    ""),
					   ((state->flags & SER_FLAGS_PRETTY) ?
						    _indent(index + 1) :
						    ""))))
			return DATA_FOR_EACH_FAIL;

		stack->status |= DUMPED_VALUE_ENTER;

		*stack_value = (dump_stack_t) {
			.src = src,
		};
	}

	xassert(stack->status == DUMPED_VALUE_ENTER);

	if ((state->rc = _dump_data(state, (index + 1))))
		return DATA_FOR_EACH_FAIL;

	LOG("[%p+%u] exit LIST+%d %pD", state, index, list_index, stack->src);
	xassert(!state->rc);
	xassert(!stack_value->src);
	_assert_dump_string_zero(state);
	*stack = (dump_stack_t) { 0 };
	return DATA_FOR_EACH_CONT;
}

static int _dump_list(dump_state_t *state, const int index)
{
	int rc = EINVAL;
	dump_stack_t *stack = &state->stack[index];
	dump_list_args_t args = {
		.magic = DUMP_LIST_ARGS_MAGIC,
		.state = state,
		.index = (index + 1),
		.first = true,
	};

	xassert(state->magic == DUMP_STATE_MAGIC);
	xassert(!(stack->status & ~(DUMPED_BLOCK_ENTER | DUMPED_BLOCK)));

	/* Container frames sit at even indexes: this one is index / 2 deep */
	if ((index / 2) >= MAX_DEPTH)
		return ESLURM_DATA_PARSING_DEPTH;

	if (!data_get_list_length(stack->src))
		return _dump_str_fmt(state, "[]");

	LOG("[%p+%u] enter LIST %pD", state, index, stack->src);

	if (!(stack->status & DUMPED_BLOCK_ENTER) &&
	    (rc = _dump_utf8(state, index, '[')))
		return rc;

	stack->status |= DUMPED_BLOCK_ENTER;

	if (!(stack->status & DUMPED_BLOCK) &&
	    (data_list_for_each_const(stack->src, _dump_list_foreach, &args) <
	     0))
		return state->rc;

	stack->status |= DUMPED_BLOCK;
	_assert_dump_string_zero(state);
	xassert(!state->stack[index + 1].src);

	if ((rc = _dump_str_fmt(state, "%s%s]",
				((state->flags & SER_FLAGS_PRETTY) ? "\n" : ""),
				((state->flags & SER_FLAGS_PRETTY) ?
					 _indent(index) :
					 ""))))
		return rc;

	LOG("[%p+%u] exit LIST %pD", state, index, stack->src);
	xassert(stack->status == (DUMPED_BLOCK_ENTER | DUMPED_BLOCK));
	_assert_dump_string_zero(state);
	return rc;
}

static data_for_each_cmd_t _dump_dict_foreach(const char *key,
					      const data_t *src, void *arg)
{
	dump_dict_args_t *args = arg;
	dump_state_t *state = args->state;
	const int index = args->index;
	dump_stack_t *stack = &state->stack[index];
	dump_stack_t *stack_value = &state->stack[index + 1];
	const bool first = args->first;

	xassert(args->magic == DUMP_DICT_ARGS_MAGIC);
	xassert(state->magic == DUMP_STATE_MAGIC);
	xassert((index + 1) < ARRAY_SIZE(state->stack));

	args->first = false;

	if (!stack->src) {
		*stack = (dump_stack_t) {
			.src = src,
		};
		_assert_dump_string_zero(state);
		xassert(!stack_value->src);
		LOG("[%p+%u] enter DICT@%s %pD", state, index, key, src);
	} else if (stack->src && (src != stack->src)) {
		/*
		 * Return to prior dump's stack position by searching dict's
		 * value until match found. Assumes foreach walks the keys the
		 * same every time.
		 */
		LOG("[%p+%u] skip DICT@%s %pD", state, index, key, src);
		return DATA_FOR_EACH_CONT;
	} else {
		LOG("[%p+%u] resume DICT@%s %pD", state, index, key, src);
	}

	if (!stack_value->src) {
		xassert(!(stack->status & DUMPED_BLOCK_ENTER));
		xassert(!(stack->status & DUMPED_BLOCK));

		if (!(stack->status & DUMPED_KEY_ENTER) &&
		    (state->rc =
			     _dump_str_fmt(state, "%s%s%s", (first ? "" : ","),
					   ((state->flags & SER_FLAGS_PRETTY) ?
						    "\n" :
						    ""),
					   ((state->flags & SER_FLAGS_PRETTY) ?
						    _indent(index + 1) :
						    ""))))
			return DATA_FOR_EACH_FAIL;

		stack->status |= DUMPED_KEY_ENTER;

		if (!(stack->status & DUMPED_KEY) &&
		    (state->rc = _dump_quoted_string(state, index,
						     (const utf8_t *) key,
						     _dump_string_bytes(state,
									key))))
			return DATA_FOR_EACH_FAIL;

		stack->status |= DUMPED_KEY;
		_assert_dump_string_zero(state);

		if (!(stack->status & DUMPED_VALUE_ENTER) &&
		    (state->rc =
			     _dump_str_fmt(state, "%s:%s",
					   ((state->flags & SER_FLAGS_PRETTY) ?
						    " " :
						    ""),
					   ((state->flags & SER_FLAGS_PRETTY) ?
						    " " :
						    ""))))
			return DATA_FOR_EACH_FAIL;

		stack->status |= DUMPED_VALUE_ENTER;

		xassert(!stack_value->src);
		*stack_value = (dump_stack_t) {
			.src = src,
		};
	}

	xassert(stack->status ==
		(DUMPED_KEY_ENTER | DUMPED_KEY | DUMPED_VALUE_ENTER));

	if ((state->rc = _dump_data(state, (index + 1))))
		return DATA_FOR_EACH_FAIL;

	LOG("[%p+%u] exit DICT@%s %pD", state, index, key, src);
	xassert(!state->rc);
	xassert(!stack_value->src);
	_assert_dump_string_zero(state);
	*stack = (dump_stack_t) { 0 };
	return DATA_FOR_EACH_CONT;
}

static int _dump_dict(dump_state_t *state, const int index)
{
	int rc = EINVAL;
	dump_stack_t *stack = &state->stack[index];
	dump_dict_args_t args = {
		.magic = DUMP_DICT_ARGS_MAGIC,
		.state = state,
		.index = (index + 1),
		.first = true,
	};

	xassert(state->magic == DUMP_STATE_MAGIC);
	xassert(!(stack->status & ~(DUMPED_BLOCK_ENTER | DUMPED_BLOCK)));

	/* Container frames sit at even indexes: this one is index / 2 deep */
	if ((index / 2) >= MAX_DEPTH)
		return ESLURM_DATA_PARSING_DEPTH;

	if (!data_get_dict_length(stack->src))
		return _dump_str_fmt(state, "{}");

	LOG("[%p+%u] enter DICT %pD", state, index, stack->src);

	if (!(stack->status & DUMPED_BLOCK_ENTER) &&
	    (rc = _dump_utf8(state, index, '{')))
		return rc;

	stack->status |= DUMPED_BLOCK_ENTER;

	if (!(stack->status & DUMPED_BLOCK) &&
	    (data_dict_for_each_const(stack->src, _dump_dict_foreach, &args) <
	     0)) {
		LOG("[%p+%u] foreach DICT %pD failed: %s",
		    state, args.index, stack->src, slurm_strerror(state->rc));
		return state->rc;
	}

	stack->status |= DUMPED_BLOCK;
	xassert(!state->stack[index + 1].src);
	_assert_dump_string_zero(state);

	if ((rc = _dump_str_fmt(state, "%s%s}",
				((state->flags & SER_FLAGS_PRETTY) ? "\n" : ""),
				((state->flags & SER_FLAGS_PRETTY) ?
					 _indent(index) :
					 ""))))
		return rc;

	LOG("[%p+%u] exit DICT %pD", state, index, stack->src);
	xassert(stack->status == (DUMPED_BLOCK_ENTER | DUMPED_BLOCK));
	_assert_dump_string_zero(state);
	return rc;
}

static int _dump_data(dump_state_t *state, const int index)
{
	int rc = EINVAL;
	dump_stack_t *stack = NULL;
	const data_t *src = NULL;

	xassert(state->magic == DUMP_STATE_MAGIC);
	xassert(state->dst->magic == BUF_MAGIC);

	/* _dump_list() and _dump_dict() bound the nesting; guard the array */
	if (index >= ARRAY_SIZE(state->stack))
		return ESLURM_DATA_PARSING_DEPTH;

	stack = &state->stack[index];
	src = stack->src;
	xassert(data_get_type(src) != DATA_TYPE_NONE);

	switch (data_get_type(src)) {
	case DATA_TYPE_NULL:
		rc = _dump_str(state, index, "null");
		break;
	case DATA_TYPE_INT_64:
		rc = _dump_str_fmt(state, "%" PRId64, data_get_int(src));
		break;
	case DATA_TYPE_STRING:
	{
		const char *str = data_get_string(src);
		rc = _dump_quoted_string(state, index, (const utf8_t *) str,
					 _dump_string_bytes(state, str));
		break;
	}
	case DATA_TYPE_FLOAT:
		rc = _dump_float(state, index, src);
		break;
	case DATA_TYPE_BOOL:
		rc = _dump_str(state, index,
			       (data_get_bool(src) ? "true" : "false"));
		break;
	case DATA_TYPE_LIST:
		rc = _dump_list(state, index);
		break;
	case DATA_TYPE_DICT:
		rc = _dump_dict(state, index);
		break;
	case DATA_TYPE_MAX:
		/* fall through */
	case DATA_TYPE_NONE:
		fatal_abort("%s: invalid data type: %s", __func__,
			    data_type_to_string(data_get_type(src)));
	}

	if (rc) {
		LOG("[%p+%u] dumping %pD failed: %s",
		    state, index, src, slurm_strerror(rc));
		xassert(stack->src == src);
	} else {
		/* pop from stack on success */
		LOG("[%p+%u] dumped %pD", state, index, src);
		*stack = (dump_stack_t) { 0 };
		_assert_dump_string_zero(state);
	}

	return rc;
}

static int _parse_encoding(serialize_parse_state_t *state, const utf8_t *start,
			   const utf8_t *end, int *bytes_ptr)
{
	switch (utf_read_encoding_schema(start, end, bytes_ptr)) {
	case UTF_UNKNOWN_ENCODING:
		/* No encoding detected -> assume UTF8 */
		return SLURM_SUCCESS;
	case UTF_8_ENCODING:
		/* Only UTF8 is allowed */
		return SLURM_SUCCESS;
	case UTF_16BE_ENCODING:
		return ESLURM_UTF16BE_SCHEMA;
	case UTF_16LE_ENCODING:
		return ESLURM_UTF16LE_SCHEMA;
	case UTF_32BE_ENCODING:
		return ESLURM_UTF32BE_SCHEMA;
	case UTF_32LE_ENCODING:
		return ESLURM_UTF32LE_SCHEMA;
	case UTF_INVALID:
		/* do nothing */
	case UTF_INVALID_MAX:
		/* do nothing */
		break;
	}

	fatal_abort("should never happen");
}

static int _parse_log(parse_state_t *state, const log_level_t level,
		      utf_code_t utf, int rc, const char *func_name,
		      const char *fmt, ...)
{
	va_list ap;
	int bytes = -1;
	char *log = NULL;
	utf8_t c[UTF8_CHAR_MAX_BYTES] = { 0 };
	char code[32] = { 0 };
	const char *at = NULL;

	va_start(ap, fmt);
	log = vxstrfmt(fmt, ap);
	va_end(ap);

	/*
	 * INFINITE and NO_VAL are the parser's own sentinels for end of input
	 * and for a completed child value. Neither is a character, so name them
	 * rather than render them as an invalid code point.
	 */
	if (utf == INFINITE) {
		at = "EOF";
	} else if (utf == NO_VAL) {
		at = "child value";
	} else {
		int wrote = -1;

		if (utf8_write_character(utf8_get_loggable(utf), c, &bytes))
			fatal_abort("should never happen");

		wrote = snprintf(code, sizeof(code), "U+%06" PRIx32 " '%s'",
				 utf, c);
		if ((wrote <= 0) || (wrote >= sizeof(code)))
			fatal_abort("should never happen");

		at = code;
	}

	if (level == LOG_LEVEL_ERROR)
		state->status |= PARSED_ERROR_LOGGED;

	log_var(level, "%s%sline %d column %d depth %d %s: %s",
		(func_name ? func_name : ""), (func_name ? ": " : ""),
		state->line, state->col, state->index, at, log);

	xfree(log);
	return rc;
}

static int _parse_pop(parse_state_t *state, const bool failed)
{
	parse_stack_t *stack = NULL;

	xassert(state->magic == PARSE_STATE_MAGIC);

	if (state->index < 0)
		return ESLURM_DATA_PARSING_DEPTH;

	stack = &state->stack[state->index];

	xassert(stack->mode > PARSE_INVALID);
	xassert(stack->mode < PARSE_INVALID_MAX);

	if (!failed && (stack->status & PARSE_CHILD)) {
		int rc = EINVAL;
		const int parent_index = (state->index - 1);

		xassert(state->index > 0);
		xassert(parent_index >= 0);
		xassert(state->stack[parent_index].mode > PARSE_INVALID);
		xassert(state->stack[parent_index].mode < PARSE_INVALID_MAX);

		if ((rc = _parse_utf(state, parent_index, NO_VAL)))
			return rc;
	}

	if (!state->index && !failed) {
		/* Parsing complete! */
		xassert(!(stack->status & PARSE_CHILD));
		xassert(stack->dst);
		xassert(!state->dst);
		SWAP(state->dst, stack->dst);
	}

	if (stack->status & PARSE_FREE_DST)
		FREE_NULL_DATA(stack->dst);

	FREE_NULL_DATA(stack->key);
	*stack = (parse_stack_t) { 0 };
	state->index--;
	return SLURM_SUCCESS;
}

static int _parse_push(parse_state_t *state, parse_mode_t mode, data_t *dst,
		       parse_status_t status)
{
	parse_stack_t *stack = NULL;

	xassert(state->magic == PARSE_STATE_MAGIC);

	/*
	 * Frame 0 holds the root token, so a container pushed onto frame n is
	 * nested n deep. A string or comment inside the deepest container needs
	 * one frame more, which the stack is sized to hold.
	 */
	if (((mode == PARSE_DICT) || (mode == PARSE_LIST)) &&
	    ((state->index + 1) > MAX_DEPTH))
		return ESLURM_JSON_PARSE_DEPTH_MAX;
	if ((state->index + 1) >= ARRAY_SIZE(state->stack))
		return ESLURM_JSON_PARSE_DEPTH_MAX;

	xassert(mode > PARSE_INVALID);
	xassert(mode < PARSE_INVALID_MAX);

	state->index++;
	stack = &state->stack[state->index];
	xassert(stack->mode == PARSE_INVALID);
	*stack = (parse_stack_t) {
		.mode = mode,
		.dst = dst,
		.status = status,
	};

	if (status & PARSE_CHILD) {
		xassert(!stack->dst);
		stack->dst = data_new();
		stack->status |= PARSE_FREE_DST;
	}

	xassert(state->index > 0);
	xassert(state->index < ARRAY_SIZE(state->stack));
	return SLURM_SUCCESS;
}

static int _parse_comment_line(parse_state_t *state, const int index,
			       const utf_code_t utf)
{
	xassert(state->index == index);
	xassert(utf != NO_VAL);

	if ((utf == INFINITE) || utf8_is_newline(utf)) {
		int rc;

		parse_debug(state, utf, "END: comment line");

		if ((rc = _parse_pop(state, false)))
			return rc;

		/* EOF still needs to be applied to the parent */
		return ((utf == INFINITE) ? ENOENT : SLURM_SUCCESS);
	}

	/* character is commented out */
	parse_debug(state, utf, "comment");
	return SLURM_SUCCESS;
}

static int _parse_comment_span(parse_state_t *state, const int index,
			       const utf_code_t utf)
{
	parse_stack_t *stack = &state->stack[index];

	xassert(state->index == index);
	xassert(utf != NO_VAL);

	if (utf == INFINITE)
		return parse_error(state, utf, ESLURM_JSON_INVALID_COMMENT,
				   "EOF before comment terminator");

	if (!(stack->status & PARSE_OPEN_STAR)) {
		if (utf == '/') {
			stack->mode = PARSE_COMMENT_LINE;
			return SLURM_SUCCESS;
		}

		if (utf != '*')
			return parse_error(
				state, utf, ESLURM_JSON_INVALID_COMMENT,
				"unexpected character after starting comment with '/'");

		stack->status |= PARSE_OPEN_STAR;
		return SLURM_SUCCESS;
	}

	if ((utf == '/') && (stack->status & PARSE_CLOSE_STAR)) {
		parse_debug(state, utf, "END: comment span");
		return _parse_pop(state, false);
	}

	if (utf == '*')
		stack->status |= PARSE_CLOSE_STAR;
	else
		stack->status &= ~PARSE_CLOSE_STAR;

	/* character is commented out */
	parse_debug(state, utf, "comment");
	return SLURM_SUCCESS;
}

static int _parse_comment_pound(parse_state_t *state, const int index,
				const utf_code_t utf)
{
	xassert(state->index == index);
	xassert(utf != NO_VAL);

	if ((utf == INFINITE) || utf8_is_newline(utf)) {
		int rc;

		parse_debug(state, utf, "END: comment line");

		if ((rc = _parse_pop(state, false)))
			return rc;

		/* EOF still needs to be applied to the parent */
		return ((utf == INFINITE) ? ENOENT : SLURM_SUCCESS);
	}

	/* character is commented out */
	parse_debug(state, utf, "comment");
	return SLURM_SUCCESS;
}

/*
 * Open a comment if utf starts one.
 *
 * Comments are allowed wherever whitespace is, so every handler that skips
 * whitespace while waiting for a structural character must also call this.
 *
 * RET SLURM_SUCCESS if a comment frame was pushed, ENOENT if utf does not
 *	start a comment, or an error from _parse_push()
 */
static int _parse_comment_start(parse_state_t *state, const utf_code_t utf)
{
	switch (utf) {
	case '#':
		parse_debug(state, utf, "BEGIN: comment pound");
		return _parse_push(state, PARSE_COMMENT_POUND, NULL, 0);
	case '/':
		parse_debug(state, utf, "BEGIN: comment span");
		return _parse_push(state, PARSE_COMMENT_SPAN, NULL, 0);
	}

	return ENOENT;
}

/* True if utf is a hex character */
static bool _is_hex(const utf_code_t utf)
{
	if ((utf >= '0') && (utf <= '9'))
		return true;

	if ((utf >= 'a') && (utf <= 'f'))
		return true;

	if ((utf >= 'A') && (utf <= 'F'))
		return true;

	return false;
}

static utf_code_t _from_hex(const utf_code_t utf)
{
	if ((utf >= '0') && (utf <= '9'))
		return (utf - '0');

	if ((utf >= 'a') && (utf <= 'f'))
		return ((utf - 'a') + 10);

	if ((utf >= 'A') && (utf <= 'F'))
		return ((utf - 'A') + 10);

	fatal_abort("should never happen");
}

static int _parse_append_utf16_code(parse_state_t *state, const int index,
				    utf16_t high, utf16_t low)
{
	utf8_t str[UTF8_CHAR_MAX_BYTES] = { 0 };
	int bytes = -1, rc = EINVAL;
	utf_code_t value = 0;

	if ((rc = utf16_to_coding(high, low, &value)))
		return parse_error(
			state, value, ESLURM_JSON_INVALID_ESCAPED,
			"\\u%04x\\u%04x is not a valid UTF-16 escape code",
			high, low);

	if (!value)
		return parse_error(state, value, ESLURM_JSON_INVALID_ESCAPED,
				   "U+%06" PRIx32
				   " is not a supported escaped unicode",
				   value);

	xassert(!utf_is_valid(value));

	if (utf8_write_character(value, str, &bytes))
		fatal_abort("should never happen");

	return buf_append_bytes(state->string, str, bytes);
}

static utf16_t _parse_escape_utf_high(parse_state_t *state, const int index)
{
	utf16_t high = 0;

#ifndef NDEBUG
	parse_stack_t *stack = &state->stack[index];

	xassert(stack->status & PARSE_UTF_HIGH);
	xassert(stack->status & PARSE_BACKSLASH);
	xassert(((state->escaped_bytes == 4) &&
		 !(stack->status & PARSE_UTF_LOW)) ||
		((state->escaped_bytes == 8) &&
		 (stack->status & PARSE_UTF_LOW)));
#endif

	high = _from_hex(state->escaped[3]);
	high |= (_from_hex(state->escaped[2]) << 4);
	high |= (_from_hex(state->escaped[1]) << 8);
	high |= (_from_hex(state->escaped[0]) << 12);

	return high;
}

static utf16_t _parse_escape_utf_low(parse_state_t *state, const int index)
{
	utf16_t low = 0;

#ifndef NDEBUG
	parse_stack_t *stack = &state->stack[index];

	xassert(stack->status & PARSE_UTF_HIGH);
	xassert(stack->status & PARSE_BACKSLASH);
	xassert((state->escaped_bytes == 8) && (stack->status & PARSE_UTF_LOW));
#endif

	low = _from_hex(state->escaped[7]);
	low |= (_from_hex(state->escaped[6]) << 4);
	low |= (_from_hex(state->escaped[5]) << 8);
	low |= (_from_hex(state->escaped[4]) << 12);

	return low;
}

static int _parse_escaped_utf_code(parse_state_t *state, const int index)
{
	parse_stack_t *stack = &state->stack[index];
	const utf16_t high = _parse_escape_utf_high(state, index);
	int rc = EINVAL;
	bool shift = false;

	xassert(!(stack->status & PARSE_HEX));

	if (state->escaped_bytes == 8) {
		const utf16_t low = _parse_escape_utf_low(state, index);

		if (!utf16_is_high_surrogate(high)) {
			if (!utf16_is_high_surrogate(low)) {
				if ((rc = _parse_append_utf16_code(state, index,
								   high, 0)))
					return rc;
				if ((rc = _parse_append_utf16_code(state, index,
								   low, 0)))
					return rc;
			} else {
				/*
				 * Need to only dump first unicode escape and
				 * then shift the second unicodes escapes as it
				 * is a high surrogate which expects a low
				 * surrogate to follow
				 */
				shift = true;

				if ((rc = _parse_append_utf16_code(state, index,
								   high, 0)))
					return rc;
			}
		} else if ((rc = _parse_append_utf16_code(state, index, high,
							  low))) {
			return rc;
		}
	} else if ((rc = _parse_append_utf16_code(state, index, high, 0))) {
		return rc;
	}

	parse_debug(state, INFINITE, "END: escaped unicode");

	if (shift) {
		state->escaped[0] = state->escaped[4];
		state->escaped[1] = state->escaped[5];
		state->escaped[2] = state->escaped[6];
		state->escaped[3] = state->escaped[7];
		state->escaped_bytes = 4;
		stack->status &= ~PARSE_UTF_LOW;
	} else {
		state->escaped_bytes = 0;
		stack->status &=
			~(PARSE_BACKSLASH | PARSE_UTF_HIGH | PARSE_UTF_LOW);
	}

	return SLURM_SUCCESS;
}

/* detect if \uHHHH\uHHHH or \uHHHH */
static int _parse_escaped_utf_detect(parse_state_t *state, const int index,
				     const utf_code_t utf)
{
	parse_stack_t *stack = &state->stack[index];
	int rc = EINVAL;

	xassert(!(stack->status & PARSE_HEX));
	xassert(!(stack->status & PARSE_UTF_LOW));
	xassert(stack->status & PARSE_UTF_HIGH);
	xassert(stack->status & PARSE_BACKSLASH);

	if (state->escaped_bytes == 4) {
		if (utf == '\\') {
			state->escaped[4] = '\\';
			state->escaped_bytes++;
			return SLURM_SUCCESS;
		} else {
			if ((rc = _parse_escaped_utf_code(state, index)))
				return rc;

			xassert(!state->escaped_bytes);

			/* only \uHHHH */
			return ENOENT;
		}
	}

	if (state->escaped_bytes == 5) {
		if (utf == 'u') {
			state->escaped[5] = 'u';
			state->escaped_bytes++;
			return SLURM_SUCCESS;
		} else {
			xassert(state->escaped[4] == '\\');

			/*
			 * Backslash was not the start of a low surrogate:
			 * release it back to the escape parser and complete the
			 * high surrogate on its own
			 */
			state->escaped_bytes = 4;

			if ((rc = _parse_escaped_utf_code(state, index)))
				return rc;

			xassert(!state->escaped_bytes);

			/* Preserve that backslash was already parsed */
			stack->status |= PARSE_BACKSLASH;

			/* only \uHHHH\ */
			return ENOENT;
		}
	}

	if (state->escaped_bytes == 6) {
		xassert(state->escaped[4] == '\\');
		xassert(state->escaped[5] == 'u');

		/* Reset byte count once low surrogate is confirmed */
		stack->status |= PARSE_UTF_LOW;
		state->escaped_bytes = 4;
		parse_debug(state, utf, "escaped unicode surrogate pair");
		return ENOENT;
	}

	/* Append state->escaped[] */
	return ENOENT;
}

static int _parse_escaped_utf(parse_state_t *state, const int index,
			      const utf_code_t utf)
{
	parse_stack_t *stack = &state->stack[index];

	xassert(stack->status & PARSE_BACKSLASH);
	xassert(!(stack->status & PARSE_HEX));

	if (!(stack->status & PARSE_UTF_LOW)) {
		int rc = _parse_escaped_utf_detect(state, index, utf);

		if ((rc != ENOENT) || !(stack->status & PARSE_UTF_HIGH))
			return rc;
	}

	if (!_is_hex(utf))
		return parse_error(state, utf, ESLURM_JSON_INVALID_ESCAPED,
				   "U+%06" PRIx32
				   " is not a supported hex code",
				   utf);

	parse_debug(state, utf, "escaped unicode");
	state->escaped[state->escaped_bytes] = utf;
	state->escaped_bytes++;
	xassert(state->escaped_bytes <= 8);

	if (state->escaped_bytes != 8)
		return SLURM_SUCCESS;

	/* escape completed */
	return _parse_escaped_utf_code(state, index);
}

static int _parse_escaped_hex(parse_state_t *state, const int index,
			      const utf_code_t utf)
{
	parse_stack_t *stack = &state->stack[index];
	utf_code_t value = 0;
	utf8_t str = '\0';

	xassert(stack->status & PARSE_BACKSLASH);
	xassert(!(stack->status & PARSE_UTF_HIGH));
	xassert(!(stack->status & PARSE_UTF_LOW));

	if (!_is_hex(utf))
		return parse_error(state, utf, ESLURM_JSON_INVALID_ESCAPED,
				   "U+%06" PRIx32
				   " is not a supported hex code",
				   utf);

	state->escaped[state->escaped_bytes] = utf;
	state->escaped_bytes++;
	xassert(state->escaped_bytes <= 2);

	if (state->escaped_bytes != 2)
		return SLURM_SUCCESS;

	value = _from_hex(state->escaped[1]);
	value |= (_from_hex(state->escaped[0]) << 4);

	/* catch overflow */
	xassert(value <= 0xff);

	if (value == 0)
		return parse_error(state, value, ESLURM_JSON_INVALID_ESCAPED,
				   "\\x00 is not a supported hex code");

	parse_debug(state, value, "END: escaped hex");
	stack->status &= ~(PARSE_BACKSLASH | PARSE_HEX);
	state->escaped_bytes = 0;
	str = value;
	return buf_append_bytes(state->string, &str, sizeof(str));
}

static int _parse_escaped_code(parse_state_t *state, const int index,
			       const utf_code_t utf)
{
	parse_stack_t *stack = &state->stack[index];
	utf8_t str[UTF8_CHAR_MAX_BYTES] = { 0 };
	int bytes = -1;
	utf_code_t value = '\0';

	for (int i = 0; i < ARRAY_SIZE(escaped_chars); i++) {
		if (utf == escaped_chars[i].utf) {
			value = escaped_chars[i].value;
			break;
		}
	}

	if (!value)
		return parse_error(state, utf, ESLURM_JSON_INVALID_ESCAPED,
				   "U+%06" PRIx32
				   " is not a supported escaped unicode",
				   utf);

	xassert(!utf_is_valid(value));

	if (utf8_write_character(value, str, &bytes))
		fatal_abort("should never happen");

	parse_debug(state, value, "END: escaped code");
	stack->status &= ~PARSE_BACKSLASH;
	return buf_append_bytes(state->string, str, bytes);
}

static int _parse_escaped(parse_state_t *state, const int index,
			  const utf_code_t utf)
{
	parse_stack_t *stack = &state->stack[index];

	if (stack->status & PARSE_HEX)
		return _parse_escaped_hex(state, index, utf);

	if (stack->status & PARSE_UTF_HIGH) {
		int rc = _parse_escaped_utf(state, index, utf);

		if ((rc != ENOENT) || !(stack->status & PARSE_BACKSLASH))
			return rc;
	}

	xassert(!state->escaped_bytes);
	xassert(!(stack->status & PARSE_UTF_HIGH));
	xassert(!(stack->status & PARSE_UTF_LOW));
	xassert(!(stack->status & PARSE_HEX));
	xassert(stack->status & PARSE_BACKSLASH);

	if (utf == INFINITE)
		return parse_error(state, utf, ESLURM_JSON_INVALID_ESCAPED,
				   "EOF before escape sequence completed");

	if (utf8_is_newline(utf)) {
		/*
		 * JSON6:
		 *	The backslash and line terminator are not included in
		 *	the string value.
		 */
		parse_debug(state, utf, "line continuation");
		stack->status &= ~PARSE_BACKSLASH;
		if (utf == '\r')
			stack->status |= PARSE_CONTINUATION_CR;
		return SLURM_SUCCESS;
	} else if (utf == 'x') {
		stack->status |= PARSE_HEX;
		parse_debug(state, utf, "BEGIN: escaped hex");
		return SLURM_SUCCESS;
	} else if (utf == 'u') {
		stack->status |= PARSE_UTF_HIGH;
		parse_debug(state, utf, "BEGIN: escaped unicode");
		return SLURM_SUCCESS;
	} else if (utf == '0') {
		/* JSON6 allows \0 but it is too dangerous for Slurm */
		return parse_error(state, utf, ESLURM_JSON_INVALID_ESCAPED,
				   "\\0 is not a supported escape code");
	} else {
		return _parse_escaped_code(state, index, utf);
	}
}

/* Copy state->string to stack->dst */
static int _parse_string_assign(parse_state_t *state, const int index)
{
	parse_stack_t *stack = &state->stack[index];
	char *str = NULL;

	if (!(str = try_xstrndup(get_buf_data(state->string),
				 get_buf_offset(state->string))))
		return ENOMEM;

	(void) data_set_string_own(stack->dst, str);
	set_buf_offset(state->string, 0);
	return SLURM_SUCCESS;
}

static int _parse_string_complete(parse_state_t *state, const int index,
				  const utf_code_t utf)
{
	parse_stack_t *stack = &state->stack[index];
	int rc = EINVAL;

	if (stack->status & PARSE_DOUBLE_QUOTED)
		parse_debug(state, utf, "END: double quoted string");
	else if (stack->status & PARSE_SINGLE_QUOTED)
		parse_debug(state, utf, "END: single quoted string");
	else if (stack->status & PARSE_UNQUOTED)
		parse_debug(state, utf, "END: unquoted string");
	else
		fatal_abort("should never happen");

	LOG_HEX_RANGE(get_buf_data(state->string),
		      size_buf(state->string), 0,
		      get_buf_offset(state->string), "string");

	if (!stack->dst) {
		xassert(stack->status & PARSE_CHILD);
		stack->dst = data_new();
		stack->status |= PARSE_FREE_DST;
	}

	xassert(data_get_type(stack->dst) == DATA_TYPE_NULL);

	if ((rc = _parse_string_assign(state, index)))
		return rc;

	/*
	 * Unquoted string's type is auto-detected but dictionary keys must
	 * always stay strings
	 */
	if ((stack->status & PARSE_UNQUOTED) && !(stack->status & PARSE_KEY))
		(void) data_convert_type(stack->dst, DATA_TYPE_NONE);

	return _parse_pop(state, false);
}

static int _parse_string_append(parse_state_t *state, const int index,
				const utf_code_t utf)
{
	utf8_t str[UTF8_CHAR_MAX_BYTES] = { 0 };
	int bytes = -1;

	xassert(!utf_is_valid(utf));

	if (utf8_write_character(utf, str, &bytes))
		fatal_abort("should never happen");

	xassert(bytes > 0);
	return buf_append_bytes(state->string, str, bytes);
}

/* True if utf could start an unquoted string */
static bool _parse_is_unquoted(const utf_code_t utf)
{
	/* characters that are excluded from starting an unquoted string */
	static const utf_code_t excluded[] = {
		INFINITE, '\"', '\'', ',', ':', '[', ']', '{', '}',
	};

	for (int i = 0; i < ARRAY_SIZE(excluded); i++)
		if (excluded[i] == utf)
			return false;

	if (utf8_is_control(utf))
		return false;

	if (utf8_is_whitespace(utf))
		return false;

	return true;
}

static int _parse_string(parse_state_t *state, const int index,
			 const utf_code_t utf)
{
	parse_stack_t *stack = &state->stack[index];
	int rc = EINVAL;

	xassert(state->index == index);

	if ((stack->status & PARSE_BACKSLASH) &&
	    ((rc = _parse_escaped(state, index, utf)) != ENOENT))
		return rc;

	xassert(!(stack->status & PARSE_BACKSLASH));
	xassert(!(stack->status & PARSE_UTF_HIGH));
	xassert(!(stack->status & PARSE_UTF_LOW));
	xassert(!(stack->status & PARSE_HEX));
	xassert((stack->status &
		 (PARSE_DOUBLE_QUOTED | PARSE_SINGLE_QUOTED | PARSE_UNQUOTED)));
	xassert((stack->status & PARSE_DOUBLE_QUOTED) ^
		(stack->status & PARSE_SINGLE_QUOTED) ^
		(stack->status & PARSE_UNQUOTED));

	if (stack->status & PARSE_CONTINUATION_CR) {
		stack->status &= ~PARSE_CONTINUATION_CR;
		if (utf == '\n') {
			parse_debug(state, utf, "line continuation LF");
			return SLURM_SUCCESS;
		}
	}

	if (utf == '\\') {
		parse_debug(state, utf, "BEGIN: escape sequence");
		stack->status |= PARSE_BACKSLASH;
		return SLURM_SUCCESS;
	}

	if ((stack->status & PARSE_SINGLE_QUOTED) && (utf == '\'')) {
		return _parse_string_complete(state, index, utf);
	} else if ((stack->status & PARSE_DOUBLE_QUOTED) && (utf == '\"')) {
		return _parse_string_complete(state, index, utf);
	} else if ((stack->status & PARSE_UNQUOTED) &&
		   !_parse_is_unquoted(utf)) {
		if ((rc = _parse_string_complete(state, index, utf)))
			return rc;
		/*
		 * Continue parsing as unquoted string does not include current
		 * character
		 */
		return ENOENT;
	} else if (utf == INFINITE) {
		return ESLURM_JSON_UNCLOSED_QUOTED_STRING;
	} else {
		return _parse_string_append(state, index, utf);
	}
}

static int _parse_token_common(parse_state_t *state, const int index,
			       const utf_code_t utf, data_t *dst,
			       parse_status_t status)
{
	int rc = EINVAL;

	xassert(state->index == index);
	xassert(utf != NO_VAL);

	switch (utf) {
	case INFINITE:
		return ENOENT;
	case '\"':
		parse_debug(state, utf, "BEGIN: double quoted");
		return _parse_push(state, PARSE_STRING, dst,
				   (status | PARSE_DOUBLE_QUOTED));
	case '\'':
		parse_debug(state, utf, "BEGIN: single quoted");
		return _parse_push(state, PARSE_STRING, dst,
				   (status | PARSE_SINGLE_QUOTED));
	}

	if ((rc = _parse_comment_start(state, utf)) != ENOENT)
		return rc;

	/* ignore all white space */
	if (utf8_is_whitespace(utf)) {
		parse_debug(state, utf, "whitespace");
		return SLURM_SUCCESS;
	}

	if (_parse_is_unquoted(utf)) {
		parse_debug(state, utf, "BEGIN: unquoted");

		if ((rc = _parse_push(state, PARSE_STRING, dst,
				      (status | PARSE_UNQUOTED))))
			return rc;

		/* unquoted always includes the first character */
		return _parse_string(state, (index + 1), utf);
	}

	/* nothing matched */
	return ENOENT;
}

static int _parse_dict_value(parse_state_t *state, const int index,
			     const utf_code_t utf)
{
	int rc = EINVAL;
	parse_stack_t *stack = &state->stack[index];

	if (index == (state->index - 1)) {
		data_t *dst = NULL;
		parse_stack_t *child = &state->stack[index + 1];

		xassert(stack->key);
		xassert(stack->status & PARSE_KEY);
		xassert(utf == NO_VAL);
		xassert(child->dst);
		xassert(!child->key);
		xassert(child->mode != PARSE_INVALID);

		dst = data_key_set(stack->dst, data_get_string(stack->key));
		FREE_NULL_DATA(stack->key);
		stack->status &= ~PARSE_KEY;

		if (!dst)
			return ESLURM_JSON_INVALID_DICTIONARY_KEY;

		(void) data_move(dst, child->dst);
		stack->status |= PARSE_NEED_COMMA;
		return SLURM_SUCCESS;
	}

	if ((rc = _parse_token(state, index, utf, NULL, PARSE_CHILD)) == ENOENT)
		return parse_error(state, utf, ESLURM_JSON_INVALID_CHAR,
				   "Expecting JSON dictionary value");

	return rc;
}

static int _parse_dict_comma(parse_state_t *state, const int index,
			     const utf_code_t utf)
{
	parse_stack_t *stack = &state->stack[index];
	int rc = EINVAL;

	/* ignore all white space */
	if (utf8_is_whitespace(utf)) {
		parse_debug(state, utf, "whitespace");
		return SLURM_SUCCESS;
	}

	if ((rc = _parse_comment_start(state, utf)) != ENOENT)
		return rc;

	if (utf != ',')
		return ESLURM_JSON_INVALID_CHAR;

	parse_debug(state, utf, "comma");
	stack->status &= ~PARSE_NEED_COMMA;
	return SLURM_SUCCESS;
}

static int _parse_dict_key_colon(parse_state_t *state, const int index,
				 const utf_code_t utf)
{
	parse_stack_t *stack = &state->stack[index];
	int rc = EINVAL;

	/* ignore all white space */
	if (utf8_is_whitespace(utf)) {
		parse_debug(state, utf, "whitespace");
		return SLURM_SUCCESS;
	}

	if ((rc = _parse_comment_start(state, utf)) != ENOENT)
		return rc;

	if (utf != ':')
		return ESLURM_JSON_INVALID_CHAR;

	parse_debug(state, utf, "dict key parsed");
	stack->status |= PARSE_KEY;
	return SLURM_SUCCESS;
}

static int _parse_dict_key(parse_state_t *state, const int index,
			   const utf_code_t utf)
{
	int rc = EINVAL;
	parse_stack_t *stack = &state->stack[index];

	if (index == (state->index - 1)) {
		parse_stack_t *child = &state->stack[index + 1];

		if (stack->key)
			return ESLURM_JSON_INVALID_DICTIONARY_KEY;

		xassert(utf == NO_VAL);
		xassert(child->dst);
		xassert(!child->key);
		xassert(!(stack->status & PARSE_KEY));

		stack->key = child->dst;
		child->dst = NULL;
		return SLURM_SUCCESS;
	}

	if (stack->status & PARSE_NEED_COMMA)
		return _parse_dict_comma(state, index, utf);

	if (!stack->key) {
		/* PARSE_KEY tells the child it is being parsed as a key */
		if ((rc = _parse_token_common(state, index, utf, NULL,
					      (PARSE_CHILD | PARSE_KEY))) ==
		    ENOENT)
			return parse_error(state, utf, ESLURM_JSON_INVALID_CHAR,
					   "Expecting JSON dictionary key");
		return rc;
	}

	if (!(stack->status & PARSE_KEY))
		return _parse_dict_key_colon(state, index, utf);

	return rc;
}

static int _parse_dict(parse_state_t *state, const int index,
		       const utf_code_t utf)
{
	parse_stack_t *stack = &state->stack[index];

	xassert((index == state->index) || (index == (state->index - 1)));

	if (!stack->dst && (stack->status & PARSE_CHILD))
		stack->dst = data_set_dict(data_new());
	else if (data_get_type(stack->dst) == DATA_TYPE_NULL)
		data_set_dict(stack->dst);

	xassert(data_get_type(stack->dst) == DATA_TYPE_DICT);

	switch (utf) {
	case INFINITE:
		return parse_error(state, utf, ESLURM_JSON_UNCLOSED_DICTIONARY,
				   "EOF before dictionary terminator");
	case '}':
		if (stack->key)
			return ESLURM_JSON_UNEXPECTED_DICTIONARY_END;

		xassert(index == state->index);
		xassert(!(stack->status & PARSE_KEY));

		parse_debug(state, utf, "END: dict");
		return _parse_pop(state, false);
	}

	if (stack->status & PARSE_KEY)
		return _parse_dict_value(state, index, utf);
	else
		return _parse_dict_key(state, index, utf);
}

static int _parse_list_value(parse_state_t *state, const int index,
			     const utf_code_t utf)
{
	parse_stack_t *stack = &state->stack[index];
	parse_stack_t *child = &state->stack[index + 1];
	data_t *dst = NULL;

	if (stack->status & PARSE_NEED_COMMA)
		return ESLURM_JSON_MISSING_COMMA;

	xassert(utf == NO_VAL);
	xassert(child->dst);

	dst = data_list_append(stack->dst);
	(void) data_move(dst, child->dst);
	stack->status |= PARSE_NEED_COMMA;
	return SLURM_SUCCESS;
}

static int _parse_list(parse_state_t *state, const int index,
		       const utf_code_t utf)
{
	int rc = EINVAL;
	parse_stack_t *stack = &state->stack[index];

	xassert((index == state->index) || (index == (state->index - 1)));
	xassert(!stack->key);

	if (!stack->dst && (stack->status & PARSE_CHILD))
		stack->dst = data_set_list(data_new());
	else if (data_get_type(stack->dst) == DATA_TYPE_NULL)
		data_set_list(stack->dst);

	xassert(data_get_type(stack->dst) == DATA_TYPE_LIST);

	/* was new value parsed? */
	if (index == (state->index - 1))
		return _parse_list_value(state, index, utf);

	switch (utf) {
	case INFINITE:
		return parse_error(state, utf, ESLURM_JSON_UNCLOSED_LIST,
				   "EOF before list terminator");
	case ']':
		parse_debug(state, utf, "END: list");
		return _parse_pop(state, false);
	case ',':
		if (!(stack->status & PARSE_NEED_COMMA))
			parse_debug(state, utf, "IGNORE: bonus comma");

		stack->status &= ~PARSE_NEED_COMMA;
		parse_debug(state, utf, "list comma");
		return SLURM_SUCCESS;
	}

	if ((rc = _parse_token(state, index, utf, NULL, PARSE_CHILD)) == ENOENT)
		return parse_error(state, utf, ESLURM_JSON_INVALID_CHAR,
				   "Expecting JSON token");
	return rc;
}

static int _parse_token_child(parse_state_t *state, const int index,
			      data_t *dst)
{
	parse_stack_t *stack = &state->stack[index];
	parse_stack_t *child = &state->stack[index + 1];

	xassert(stack->mode == PARSE_TOKEN);
	xassert(index == (state->index - 1));
	xassert(child->dst);
	xassert(!child->key);
	xassert(child->mode != PARSE_INVALID);

	if (dst) {
		(void) data_move(dst, child->dst);
	} else {
		xassert(!stack->dst);
		SWAP(stack->dst, child->dst);
		/* Root token now owns the parsed value */
		stack->status |= PARSE_FREE_DST;
	}

	return SLURM_SUCCESS;
}

static int _parse_token(parse_state_t *state, const int index,
			const utf_code_t utf, data_t *dst,
			parse_status_t status)
{
	parse_stack_t *stack = &state->stack[index];
	int rc = EINVAL;

	/*
	 * dst is only populated once the root value has been parsed and JSON
	 * only allows for a single root value per document. Only white space
	 * and comments may trail the root value.
	 */
	if (dst && (utf != NO_VAL) && (utf != INFINITE) && (utf != '#') &&
	    (utf != '/') && !utf8_is_whitespace(utf))
		return parse_error(state, utf, ESLURM_JSON_PARSE_FAILED,
				   "Unexpected token after root value");

	switch (utf) {
	case NO_VAL:
		return _parse_token_child(state, index, dst);
	case INFINITE:
		if (!stack->dst)
			return ESLURM_JSON_PARSE_DEPTH_MIN;

		parse_debug(state, utf, "END: root value");
		return _parse_pop(state, false);
	case ',':
		return ESLURM_JSON_UNEXPECTED_COMMA;
	case ':':
		return ESLURM_JSON_INVALID_DICTIONARY_COLON;
	case '[':
		parse_debug(state, utf, "BEGIN: list");
		return _parse_push(state, PARSE_LIST, dst,
				   (status | PARSE_CHILD));
	case ']':
		return ESLURM_JSON_UNEXPECTED_LIST_END;
	case '{':
		parse_debug(state, utf, "BEGIN: dict");
		return _parse_push(state, PARSE_DICT, dst,
				   (status | PARSE_CHILD));
	case '}':
		return ESLURM_JSON_UNEXPECTED_DICTIONARY_END;
	}

	if ((rc = _parse_token_common(state, index, utf, dst,
				      (status | PARSE_CHILD))) != ENOENT)
		return rc;

	return parse_error(state, utf, ESLURM_JSON_INVALID_CHAR,
			   "Expecting JSON token");
}

static int _parse_utf(parse_state_t *state, const int index,
		      const utf_code_t utf)
{
	parse_stack_t *stack = NULL;
	int rc = EINVAL;

	xassert(state->magic == PARSE_STATE_MAGIC);

	if (index < 0)
		return ESLURM_JSON_PARSE_DEPTH_MIN;

	xassert(index < (int) ARRAY_SIZE(state->stack));

	stack = &state->stack[index];

	xassert(stack->mode > PARSE_INVALID);
	xassert(stack->mode < PARSE_INVALID_MAX);

	if (state->dst)
		return ESLURM_JSON_PARSE_DEPTH_MIN;

	switch (stack->mode) {
	case PARSE_TOKEN:
		rc = _parse_token(state, index, utf, stack->dst, 0);
		break;
	case PARSE_COMMENT_POUND:
		rc = _parse_comment_pound(state, index, utf);
		break;
	case PARSE_COMMENT_SPAN:
		rc = _parse_comment_span(state, index, utf);
		break;
	case PARSE_COMMENT_LINE:
		rc = _parse_comment_line(state, index, utf);
		break;
	case PARSE_DICT:
		rc = _parse_dict(state, index, utf);
		break;
	case PARSE_LIST:
		rc = _parse_list(state, index, utf);
		break;
	case PARSE_STRING:
		rc = _parse_string(state, index, utf);
		break;
	case PARSE_INVALID_MAX:
		/* fall through */
	case PARSE_INVALID:
		fatal_abort("should never happen");
	}

	/* Check if character did not apply and we need to unwind the stack */
	if ((rc == ENOENT) && (index > 0)) {
		/* catch ENOENT if _parse_pop() was not called */
		xassert(stack->mode == PARSE_INVALID);
		xassert(index > 0);
		if (index > 0) {
			parse_debug(state, utf, "stack unwind @ %d", index);
			return _parse_utf(state, (index - 1), utf);
		}
	}

	return rc;
}

/* Report a failed parse exactly once */
static int _parse_failed(parse_state_t *state, const utf_code_t utf,
			 const int rc, const char *what)
{
	xassert(rc);

	if (state->status & PARSED_ERROR_LOGGED) {
		parse_debug(state, utf, "%s: %s", what, slurm_strerror(rc));
		return rc;
	}

	return parse_error(state, utf, rc, "%s: %s", what, slurm_strerror(rc));
}

static int _parse(serialize_parse_state_t *state)
{
	int rc = SLURM_SUCCESS;
	int bytes = 0;
	const utf8_t *start = (utf8_t *) get_buf_data(state->src);
	const utf8_t *ptr = (start + get_buf_offset(state->src));
	const utf8_t *end = (start + size_buf(state->src));

	xassert(state->magic == PARSE_STATE_MAGIC);

	LOG_HEX_RANGE(get_buf_data(state->src), size_buf(state->src),
		      get_buf_offset(state->src), size_buf(state->src),
		      "parsing");

	if (!(state->status & PARSED_BOM) &&
	    (rc = _parse_encoding(state, ptr, end, &bytes)))
		return rc;

	state->status |= PARSED_BOM;
	xassert(bytes >= 0);
	ptr += bytes;
	set_buf_offset(state->src, (ptr - start));

	while (ptr < end) {
		utf_code_t utf = INFINITE;

		if ((rc = utf8_read_character(ptr, end, &utf, &bytes, true)))
			return _parse_failed(state, utf, rc,
					     "Invalid bytestream");

		/* Track human friendly offsets for column and line */
		if (utf8_is_newline(utf)) {
			/* CR LF is one line ending */
			if ((utf != '\n') || !(state->status & PARSED_CR)) {
				state->line++;
				state->col = 0;
			}
		} else if (!utf8_is_control(utf)) {
			state->col++;
		}

		if (utf == '\r')
			state->status |= PARSED_CR;
		else
			state->status &= ~PARSED_CR;

		if ((rc = _parse_utf(state, state->index, utf)))
			return _parse_failed(state, utf, rc, "Parsing failed");

		xassert(bytes >= 0);
		ptr += bytes;
		xassert(ptr <= end);

		/* Track progress so a resume does not re-parse these bytes */
		set_buf_offset(state->src, (ptr - start));
	}

	/* INFINITE acts as an unambiguous unicode for EOF */
	if ((rc = _parse_utf(state, state->index, INFINITE)))
		return _parse_failed(state, INFINITE, rc, "Parsing EOF failed");

	return rc;
}

static void _parse_state_free(parse_state_t **state_ptr, const int rc)
{
	parse_state_t *state = *state_ptr;

	if (!state)
		return;

	*state_ptr = NULL;

	xassert(state->magic == PARSE_STATE_MAGIC);

	/* unwind the stack */
	while (state->index >= 0) {
		parse_debug(state, INFINITE, "cleanup unwind @ %d",
			    state->index);
		_parse_pop(state, rc);
	}

	/* Verify stack has been fully unwound */
	for (int i = 0; i < ARRAY_SIZE(state->stack); i++) {
		xassert(state->stack[i].mode == PARSE_INVALID);
		xassert(!state->stack[i].dst);
		xassert(!state->stack[i].key);
	}
	xassert(state->index == -1);
	xassert(rc || !state->escaped_bytes);
	xassert(rc || !get_buf_offset(state->string));

	state->magic = ~PARSE_STATE_MAGIC;
	FREE_NULL_DATA(state->dst);
	FREE_NULL_BUFFER(state->string);
	xfree(state);
}

static serialize_parse_state_t *_parse_state_new(buf_t *src)
{
	parse_state_t *state = try_xmalloc(sizeof(*state));
	buf_t *string = try_init_buf(BUF_SIZE);

	if (!state || !string) {
		FREE_NULL_BUFFER(string);
		xfree(state);
		return NULL;
	}

	*state = (parse_state_t) {
		.magic = PARSE_STATE_MAGIC,
		.line = 1,
		.src = src,
		.stack = {
			[ 0 ] = {
				.mode = PARSE_TOKEN,
			},
		},
		.string = string,
	};

	xassert(src->magic == BUF_MAGIC);
	xassert(state->string->magic == BUF_MAGIC);

	return state;
}

extern int serialize_p_parse(parse_state_t **state_ptr, data_parser_t *parser,
			     data_parser_type_t type, void *dst,
			     ssize_t dst_bytes, buf_t *src)
{
	int rc = EINVAL;
	parse_state_t *state = *state_ptr;

	if (!src) {
		/*
		 * Caller is abandoning the parse, not completing it which is
		 * not considered a failure
		 */
		_parse_state_free(state_ptr, ECANCELED);
		return SLURM_SUCCESS;
	}

	xassert(type > DATA_PARSER_TYPE_INVALID);
	xassert(type < DATA_PARSER_TYPE_MAX);
	xassert(dst);
	xassert(dst_bytes > 0);

	if (!state && !(state = *state_ptr = _parse_state_new(src)))
		return ENOMEM;

	xassert(state->magic == PARSE_STATE_MAGIC);

	if (!(rc = _parse(state))) {
		data_t *parent_path = data_set_list(data_new());
		rc = data_parser_g_parse(parser, type, dst, dst_bytes,
					 state->dst, parent_path);
		FREE_NULL_DATA(parent_path);
	}

	if (rc != ENOSPC)
		_parse_state_free(state_ptr, rc);

	return rc;
}

extern int serialize_p_string_to_data(data_t **dest, const char *src_ptr,
				      size_t length)
{
	int rc = EINVAL;
	buf_t src = SHADOW_BUF_INITIALIZER(src_ptr, length);
	parse_state_t *state = NULL;

	set_buf_offset(&src, 0);
	FREE_NULL_DATA((*dest));

	if (!src_ptr)
		return ESLURM_DATA_PTR_NULL;

	if (!(state = _parse_state_new(&src)))
		return ENOMEM;

	if (!(rc = _parse(state)))
		SWAP(*dest, state->dst);

	_parse_state_free(&state, rc);
	return rc;
}

static void _dump(serialize_dump_state_t *state)
{
	const data_t *src = state->stack[0].src;
#ifndef NDEBUG
	const int dst_bytes = size_buf(state->dst);

	xassert(state->magic == DUMP_STATE_MAGIC);
	xassert(state->dst);
	xassert(size_buf(state->dst) >= DUMP_BUF_BYTES);
	xassert(src);
#endif

	state->dump_start = get_buf_offset(state->dst);

	/*
	 * A BOM is never emitted. RFC 8259 section 8.1 forbids adding one to
	 * JSON text that is transmitted over a network, which is where all of
	 * Slurm's JSON goes. Note the parser still accepts and strips a leading
	 * BOM on input: tolerating one someone else wrote is allowed.
	 */

	if (!(state->rc = _dump_data(state, 0))) {
		/* verify entire tree was dumped */
		xassert(!state->stack[0].src);

		LOG_HEX_RANGE(get_buf_data(state->dst),
			      size_buf(state->dst), state->dump_start,
			      get_buf_offset(state->dst),
			      "dumped %pD successfully", src);
	} else if (state->rc == ENOSPC) {
		LOG_HEX_RANGE(get_buf_data(state->dst),
			      size_buf(state->dst), state->dump_start,
			      get_buf_offset(state->dst),
			      "partially dumped %pD successfully", src);
	} else {
		LOG("dumping %pD failed: %s", src, slurm_strerror(state->rc));
	}

	/*
	 * Catch any resizes of the destination buffer which should never
	 * happen in this call tree
	 */
	xassert(dst_bytes == size_buf(state->dst));
}

/*
 * Release dump state.
 *
 * IN rc - outcome of the dump being released. Only a dump that completed is
 *	required to have finished its strings: on any failure, including the
 *	caller abandoning the dump by passing a NULL dst, a partially written
 *	string or format string is the expected state and must not be asserted
 *	against.
 */
static int _dump_state_free(dump_state_t **state_ptr, const int rc)
{
	dump_state_t *state = *state_ptr;

	*state_ptr = NULL;

	if (!state)
		return EINVAL;

	xassert(state->magic == DUMP_STATE_MAGIC);

	if (!rc)
		_assert_dump_string_zero(state);

	state->magic = ~DUMP_STATE_MAGIC;
	FREE_NULL_DATA(state->free_src);
	xfree(state);

	return rc;
}

/* SerializerParameters flags from serialize_p_init() merged over the default */
static serializer_flags_t global_flags = SERIALIZER_XJSON_DEFAULT_FLAGS;

static serializer_flags_t _merge_flags(serializer_flags_t flags)
{
	serializer_flags_t ret = global_flags;

	/* Per-call compact/pretty overrides the configured setting */
	if (flags & (SER_FLAGS_COMPACT | SER_FLAGS_PRETTY))
		ret &= ~(SER_FLAGS_COMPACT | SER_FLAGS_PRETTY);

	return (ret | flags);
}

static serialize_dump_state_t *_dump_state_new(serializer_flags_t flags,
					       data_t *src, bool free_src,
					       buf_t *dst)
{
	dump_state_t *state = try_xmalloc(sizeof(*state));

	if (!state)
		return NULL;

	*state = (dump_state_t) {
		.magic = DUMP_STATE_MAGIC,
		.flags = flags,
		.dst = dst,
		.free_src = (free_src ? src : NULL),
		.stack = {
			[0] = {
				.src = src,
			},
		},
	};

	return state;
}

extern int serialize_p_dump(serialize_dump_state_t **state_ptr,
			    data_parser_t *parser, data_parser_type_t type,
			    void *src, ssize_t src_bytes, buf_t *dst,
			    serializer_flags_t flags)
{
	dump_state_t *state = *state_ptr;
	int rc = EINVAL;

	if (!dst) {
		/*
		 * Caller is abandoning the dump, not completing it which is not
		 * considered a failure
		 */
		(void) _dump_state_free(state_ptr, ECANCELED);
		return SLURM_SUCCESS;
	}

	if (!state) {
		data_t *dsrc = data_new();

		if ((rc = data_parser_g_dump(parser, type, src, src_bytes,
					     dsrc))) {
			xassert(!*state_ptr);
			FREE_NULL_DATA(dsrc);
			return rc;
		}

		if (!(state = *state_ptr = _dump_state_new(_merge_flags(flags),
							   dsrc, true, dst))) {
			xassert(!*state_ptr);
			FREE_NULL_DATA(dsrc);
			return ENOMEM;
		}
	}

	xassert(state->magic == DUMP_STATE_MAGIC);
	xassert(!state->rc || (state->rc == ENOSPC));
	xassert(state->stack[0].src || !state->rc);
	xassert(state->dst == dst);

	/* Reset to retry writing to buffer */
	if (state->rc == ENOSPC)
		state->rc = SLURM_SUCCESS;

	xassert(!state->rc);

	_dump(state);

	/* Take a copy as _dump_state_free() releases state */
	rc = state->rc;

	/* Cleanup except when buffer full */
	if (rc != ENOSPC)
		return _dump_state_free(state_ptr, rc);

	return rc;
}

extern int serialize_p_data_to_string(char **dest, size_t *length, data_t *src,
				      serializer_flags_t flags)
{
	int rc = SLURM_SUCCESS;
	buf_t *dst = NULL;
	dump_state_t *state = NULL;

	if (!(dst = try_init_buf(DUMP_BUF_BYTES)))
		return ENOMEM;

	/* Hand over ownership of dst */
	if (!(state = _dump_state_new(_merge_flags(flags), src, false, dst))) {
		FREE_NULL_BUFFER(dst);
		return ENOMEM;
	}

	dst = NULL;
	xassert(state->stack[0].src);
	xassert(state->magic == DUMP_STATE_MAGIC);

	do {
		_dump(state);

		if (((rc = state->rc) == ENOSPC) &&
		    (rc = try_grow_buf(state->dst, DUMP_BUF_GROW_BYTES)))
			break;
	} while (state && (state->rc == ENOSPC));

	/* Take back ownership of dst (which resize may have changed) */
	SWAP(state->dst, dst);
	xassert(dst->magic == BUF_MAGIC);

	if (!rc) {
		const uint32_t bytes = get_buf_offset(dst);

		/*
		 * Every dump function refuses to fill the buffer completely, so
		 * there is always a spare byte here for the NUL terminator.
		 */
		xassert(size_buf(dst) > bytes);
		xassert(dest);

		if (length)
			*length = get_buf_offset(dst);

		*dest = xfer_buf_data_ptr(&dst);
		(*dest)[bytes] = '\0';

		LOG_HEX(*dest, bytes, "dumped %pD successfully", src);
	} else {
		LOG("dumping %pD failed: %s", src, slurm_strerror(rc));
	}

	FREE_NULL_BUFFER(dst);
	return _dump_state_free(&state, rc);
}

extern int serialize_p_init(serializer_flags_t flags)
{
	/* See the matching comment in the json serializer. */
	if (flags & (SER_FLAGS_COMPACT | SER_FLAGS_PRETTY))
		global_flags = flags;
	else
		global_flags = (SERIALIZER_XJSON_DEFAULT_FLAGS | flags);

	return SLURM_SUCCESS;
}

extern void serialize_p_fini(void)
{
	/* do nothing */
}
