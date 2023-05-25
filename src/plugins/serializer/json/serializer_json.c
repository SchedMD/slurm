/*****************************************************************************\
 *  serializer_json.c - Serializer for JSON.
 *****************************************************************************
 *  Copyright (C) 2021 SchedMD LLC
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

#include <ctype.h>
#include <math.h>
#include "src/common/data.h"
#include "src/common/log.h"
#include "src/common/read_config.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/utf.h"
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
const char plugin_name[] = "Serializer JSON plugin";
const char plugin_type[] = "serializer/json";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;
const char *mime_types[] = {
	"application/json",
	"application/jsonrequest",
	NULL
};

/* count of data_t* in a depth chunk */
#define DEPTH_CHUNK 15

/* Max number of levels to parse */
#define MAX_DEPTH 50

/* Default quoted string alloc size */
#define STRING_ALLOC_MIN 64

/* Default alloc size when serializing data to string */
#define INITIAL_OUTPUT_STRING_ALLOC (STRING_ALLOC_MIN * 1024)

#ifndef NDEBUG

/*
 * Verify all casting between char and utf8_t to avoid corruption while in
 * developer mode.
 */

#if (defined(__has_builtin) && __has_builtin(__builtin_types_compatible_p))
#define IS_TYPE_COMPAT(x, y) __builtin_types_compatible_p(x, y)
#else
#define IS_TYPE_COMPAT(x, y) (sizeof(x) == sizeof(y))
#endif

#define IS_TYPE_STRING(ptr)                       \
	(IS_TYPE_COMPAT(typeof(ptr), char *) ||   \
	 IS_TYPE_COMPAT(typeof(ptr), char []) ||  \
	 IS_TYPE_COMPAT(typeof(ptr), utf8_t *) || \
	 IS_TYPE_COMPAT(typeof(ptr), utf8_t []))
#define IS_TYPE_STRING_PTR(ptr)                    \
	(IS_TYPE_COMPAT(typeof(ptr), char **) ||   \
	 IS_TYPE_COMPAT(typeof(ptr), utf8_t **))
#define IS_TYPE_CONST_STRING(ptr)                        \
	(IS_TYPE_COMPAT(typeof(ptr), const char *) ||    \
	 IS_TYPE_COMPAT(typeof(ptr), const char []) ||   \
	 IS_TYPE_COMPAT(typeof(ptr), const utf8_t *) ||  \
	 IS_TYPE_COMPAT(typeof(ptr), const utf8_t []) || \
	 IS_TYPE_STRING(ptr))

static const char *_cast_const_cstring(const void *ptr, const bool is_compat)
{
	xassert(is_compat);
	return ptr;
}
#define cast_const_cstring(ptr)                             \
	_cast_const_cstring(ptr, IS_TYPE_CONST_STRING(ptr))

static char *_cast_cstring(void *ptr, const bool is_compat)
{
	xassert(is_compat);
	return ptr;
}
#define cast_cstring(ptr) _cast_cstring(ptr, IS_TYPE_STRING(ptr))

static char **_cast_cstring_ptr(void *ptr, const bool is_compat)
{
	xassert(is_compat);
	return ptr;
}
#define cast_cstring_ptr(ptr) _cast_cstring_ptr(ptr, IS_TYPE_STRING_PTR(ptr))

static utf8_t *_cast_utf8string(void *ptr, const bool is_compat)
{
	xassert(is_compat);
	return ptr;
}
#define cast_utf8string(ptr) _cast_utf8string(ptr, IS_TYPE_STRING(ptr))

static const utf8_t *_cast_const_utf8string(const void *ptr,
					    const bool is_compat)
{
	xassert(is_compat);
	return ptr;
}
#define cast_const_utf8string(ptr)                             \
	_cast_const_utf8string(ptr, IS_TYPE_CONST_STRING(ptr))

#else /* !NDEBUG */

#define cast_cstring(ptr) ((char *) ptr)
#define cast_const_cstring(ptr) ((const char *) ptr)
#define cast_cstring_ptr(ptr) ((char **) ptr)
#define cast_utf8string(ptr) ((utf8_t *) ptr)
#define cast_const_utf8string(ptr) ((const utf8_t *) ptr)

#endif

/* wrapper to convert xstrcatat() to using char * from utf8_t */
#define catat(src, src_at, str)                                               \
	_xstrcatat(cast_cstring_ptr(&src), cast_cstring_ptr(&src_at),         \
		   cast_const_cstring(str))

/* wrapper to add a single utf character */
#define catcharat(src_str, src_str_at, character, return_code)                 \
	_cat_char_at(cast_cstring_ptr(&src_str), cast_cstring_ptr(&src_str_at),\
		     character, &return_code)

/* wrapper to convert xstrfmtcat() to using char * from utf8_t */
#define catfmtat(src, src_at, fmt, ...)                                       \
	_xstrfmtcatat(cast_cstring_ptr(&src), cast_cstring_ptr(&src_at), fmt, \
		      ##__VA_ARGS__)

/* wrapper for utf8_t* to char* */
#define utf8len(x) strlen(cast_const_cstring(x))

/* wrap data_set_string_own() for utf8_t */
#define data_set_utf8_own(d_ptr, str)     \
do {                                      \
	char *cstr = cast_cstring(str);   \
	xassert(xsize(str) >= 0);         \
	data_set_string_own(d_ptr, cstr); \
	str = NULL;                       \
} while (false)

/* wrap xstrndup() for utf8_t */
#define utf8ndup(src, len)                                              \
		cast_utf8string(xstrndup(cast_const_cstring(src), len))
#define utf8dup(src) cast_utf8string(xstrdup(cast_const_cstring(src)))

#define parse_debug_hex(state, src, src_length, fmt, ...)              \
do {                                                                   \
	if (is_debug_active())                                         \
		log_flag_hex(DATA, src, src_length,                    \
			     "%s:[%04d:%04d] "fmt, __func__,           \
			     state->line, state->col, ##__VA_ARGS__);  \
} while (0)

#define parse_error(state, utf, rc, fmt, ...)                          \
	_parse_log(state, LOG_LEVEL_ERROR, utf, rc, __func__, fmt,     \
		   ##__VA_ARGS__)

#define parse_debug(state, utf, fmt, ...)                              \
do {                                                                   \
	if (is_debug_active())                                         \
		_parse_log(state, LOG_LEVEL_DEBUG, utf, SLURM_SUCCESS, \
			   __func__, fmt, ##__VA_ARGS__);              \
} while (0)

#define dump_debug_hex(state, src, src_length, fmt, ...)               \
do {                                                                   \
	if (is_debug_active())                                         \
		log_flag_hex(DATA, src, src_length, "[%04zu] "#fmt,    \
			     (utf8len(state->dst) + 1), ##__VA_ARGS__);\
} while (0)

#define dump_error(state, utf, rc, fmt, ...)                          \
	_dump_log(state, LOG_LEVEL_ERROR, utf, rc, __func__, fmt,     \
		  ##__VA_ARGS__)

#define dump_debug(state, utf, fmt, ...)                              \
do {                                                                  \
	if (is_debug_active())                                        \
		_dump_log(state, LOG_LEVEL_DEBUG, utf, SLURM_SUCCESS, \
			  __func__, fmt, ##__VA_ARGS__);              \
} while (0)

typedef struct {
	data_t **stack;
	int depth;
	int max_depth;
} parents_t;

typedef struct {
	int line;
	int col;

	const utf8_t *comment; /* start of comment */

	enum {
		COMMENT_UNKNOWN = 0,
		COMMENT_LINE, /* comment is //.*$ */
		COMMENT_SPAN_BEGIN, // comment is /*.**/
		COMMENT_SPAN_END, // comment is /*.**/
	} comment_type;

	const utf8_t *unquoted; /* start of unquoted string */

	utf8_t *quoted;
	utf8_t *quoted_at;

	const utf8_t *escaped; /* start of escaped sequence */
	uint8_t escaped_chars; /* number of match hex'ed chars */

	utf8_t *key; /* dictionary key before setting */
	char *key_printable; /* key printable cached */
	const char *key_source;

	data_t *target;
	parents_t parents;
} state_t;

typedef struct {
	int rc;

	int depth;
	const data_t *parent;
	int index;

	utf8_t *dst;
	utf8_t *dst_at;

	serializer_flags_t flags;
} dump_state_t;

/* List of JSON support escape characters */
#define E(utf, escaped) { utf, (utf8_t *) escaped }
static const struct {
	utf_code_t utf;
	utf8_t *escaped;
} escaped_chars[] = {
	E('\"', "\""),
	E('\\', "\\"),
	E('/', "/"),
	E('b', "\b"),
	E('f', "\f"),
	E('n', "\n"),
	E('r', "\r"),
	E('t', "\t"),
};
#undef E

static int _cat_data(dump_state_t *state, const data_t *src);
static int _parse_log(state_t *state, const log_level_t level, utf_code_t utf,
		      int rc, const char *func_name, const char *fmt, ...);

static bool is_debug_active()
{
	return ((slurm_conf.debug_flags & DEBUG_FLAG_DATA) &&
		(get_log_level() >= LOG_LEVEL_DEBUG));
}

static void _cat_char_at(char **src_ptr, char **src_at_ptr, utf_code_t utf,
			 int *rc_ptr)
{
	xassert(!*rc_ptr);

	if (!*src_ptr)
		*src_at_ptr = *src_ptr = xmalloc(STRING_ALLOC_MIN);

	if (utf <= UTF_ASCII_MAX_CODE) {
		utf8_t c[] = { utf, 0};
		/* avoid penatly to build stack with write_utf8_character() */
		_xstrcatat(src_ptr, src_at_ptr, (const char *) c);
	} else {
		utf8_t c[UTF8_CHAR_MAX_BYTES];
		*rc_ptr = write_utf8_character(utf, c, true);
		_xstrcatat(src_ptr, src_at_ptr, (const char *) c);
	}
}

extern int serializer_p_init(void)
{
	debug("%s: %s loaded", plugin_type, __func__);

	return SLURM_SUCCESS;
}

extern int serializer_p_fini(void)
{
	debug("%s: %s unloaded", plugin_type, __func__);

	return SLURM_SUCCESS;
}

static utf8_t *_dump_target_stack(state_t *state)
{
	utf8_t *stack = NULL;
	utf8_t *stack_at = NULL;

	for (int i = 0; i < state->parents.depth; i++) {
		catfmtat(stack, stack_at, "%s"PRINTF_DATA_T,
			 (stack ? "->" : ""),
			 PRINTF_DATA_T_VAL(state->parents.stack[i]));
	}

	return stack;
}

static void _push_target(state_t *state, data_t *t)
{
	parents_t *parents = &state->parents;

	/* should only ever be stacking list/dicts or root null */
	xassert((data_get_type(t) == DATA_TYPE_DICT) ||
		(data_get_type(t) == DATA_TYPE_LIST) ||
		((data_get_type(t) == DATA_TYPE_NULL) &&
		 (parents->depth == 0)));

	xassert(parents->depth >= 0);
	xassert(parents->max_depth >= 0);
	xassert(parents->stack || !parents->max_depth);

	if (parents->depth >= parents->max_depth) {
		parents->max_depth += DEPTH_CHUNK;
		xrecalloc(parents->stack, parents->max_depth,
			  sizeof(*parents->stack));
		log_flag(DATA, "%s stack(0x%"PRIxPTR") size: %d/%d",
			 ((parents->max_depth == 0) ?
			  "allocating" : "increasing"),
			 (uintptr_t) parents->stack, parents->depth,
			 parents->max_depth);
	}

	if (is_debug_active()) {
		utf8_t *stack = _dump_target_stack(state);

		log_flag(DATA, "pushing %s(0x%"PRIxPTR") at stack[%d/%d]:%s",
			 data_type_to_string(data_get_type(t)), (uintptr_t) t,
			 parents->depth, parents->max_depth, stack);

		xfree(stack);
	}

	parents->stack[parents->depth] = t;
	parents->depth++;

	state->target = t;
}

static int _pop_target(state_t *state, utf_code_t utf)
{
	int rc = SLURM_SUCCESS;
	parents_t *parents = &state->parents;
	data_t *t = NULL;

	xassert(parents->depth >= 0);
	if (parents->depth >= 1) {
		xassert(parents->max_depth >= 0);
		xassert(parents->depth < parents->max_depth);
		xassert(parents->stack);

		parents->depth--;
		t = parents->stack[parents->depth];
	}

	if (!t) {
		utf8_t *stack = _dump_target_stack(state);
		rc = parse_error(state, utf, ESLURM_JSON_PARSE_DEPTH_MIN,
				 "Unbalanced stack[%d/%d]:%s",
				 parents->depth, parents->max_depth, stack);
		xfree(stack);
	} else if (is_debug_active()) {
		utf8_t *stack = _dump_target_stack(state);
		log_flag(DATA, "popped %s(0x%"PRIxPTR") at stack[%d/%d]:%s",
			 data_type_to_string(data_get_type(t)), (uintptr_t) t,
			 parents->depth, parents->max_depth, stack);
		xfree(stack);
	}

	state->target = t;
	return rc;
}

static int _dump_log(dump_state_t *state, const log_level_t level,
		     utf_code_t utf, int rc, const char *func_name,
		     const char *fmt, ...)
{
	va_list ap;
	char *log;
	utf8_t c[UTF8_CHAR_MAX_BYTES];

	va_start(ap, fmt);
	log = vxstrfmt(fmt, ap);
	va_end(ap);

	if (write_utf8_character(get_utf8_loggable(utf), c, true))
		xassert(false);

	log_var(level, "%s%s[%04zu]="UTF8_PRINTF"=%s %s",
			  (func_name ? func_name : ""),
			  (func_name ? ":" : ""),
			  (utf8len(state->dst) + 1), utf, c, log);

	xfree(log);

	return rc;
}

static int _parse_log(state_t *state, const log_level_t level, utf_code_t utf,
		      int rc, const char *func_name, const char *fmt, ...)
{
	va_list ap;
	char *log;
	utf8_t c[UTF8_CHAR_MAX_BYTES];

	va_start(ap, fmt);
	log = vxstrfmt(fmt, ap);
	va_end(ap);

	if (write_utf8_character(get_utf8_loggable(utf), c, true))
		xassert(false);

	log_var(level, "%s%s[%04d:%04d]="UTF8_PRINTF"=%s %s",
			  (func_name ? func_name : ""),
			  (func_name ? ":" : ""),
			  state->line, state->col, utf, c, log);

	xfree(log);

	return rc;
}

static char *_printable(const utf8_t *src)
{
	int rc = SLURM_SUCCESS;
	const int len = !src ? 0 : utf8len(src);
	const utf8_t *end = src + len;
	utf8_t *pos = NULL, *output = NULL;

	if (!len)
		return NULL;

	log_flag_hex(DATA, src, len, "%s: source %zu byte string 0x%"PRIxPTR,
		     __func__, len, (uintptr_t) src);

	while (src < end) {
		int bytes;
		utf_code_t utf;

		/* ignore invalid UTF8 errors */
		if (rc || (rc = read_utf8_character(src, end, &utf, &bytes)) ||
		    (utf <= 0)) {
			/*
			 * src is corrupt, so we just dump replacements from
			 * here on.
			 */
			catcharat(output, pos, UTF_REPLACEMENT_CODE, rc);
		} else {
			catcharat(output, pos, get_utf8_loggable(utf), rc);
			src += bytes;
		}
	}

	log_flag_hex(DATA, cast_cstring(output), utf8len(output),
		     "%s: printable string 0x%"PRIxPTR,
		     __func__, (uintptr_t) output);

	return cast_cstring(output);
}

static bool _is_unquoted_char(utf_code_t utf)
{
	static const utf_code_t codes[] = {
		/* all JSON control characters */
		'\"',
		'\'',
		'{',
		'}',
		'[',
		']',
		':',
		'\\',
		'/',
		',',
		/* unwanted UTF chars */
		'\b',
		UTF_BYTE_ORDER_MARK_CODE,
	};

	/*
	 * We are going to allow any non-whitespace and non-control characters
	 * that are not part of the JSON schema to act as an unquoted character.
	 */

	xassert(!is_utf_valid(utf));

	if (is_utf8_whitespace(utf))
		return false;
	if (is_utf8_control(utf))
		return false;

	for (int i = 0; i < ARRAY_SIZE(codes); i++)
		if (codes[i] == utf)
			return false;

	return true;
}

static int _on_enter_dict(state_t *state, utf_code_t utf)
{
	if (data_get_type(state->target) == DATA_TYPE_DICT) {
		return parse_error(state, utf,
				   ESLURM_JSON_UNEXPECTED_DICTIONARY,
				   "unexpected dictionary while parsing %s before key provided",
				   data_type_to_string(data_get_type(
					state->target)));
	} else if (data_get_type(state->target) == DATA_TYPE_LIST) {
		data_t *parent = state->target;

		_push_target(state, state->target);
		state->target = data_set_dict(data_list_append(state->target));

		parse_debug(state, utf, "BEGIN: "PRINTF_DATA_T" in "PRINTF_DATA_T,
			    PRINTF_DATA_T_VAL(state->target),
			    PRINTF_DATA_T_VAL(parent));

		return SLURM_SUCCESS;
	} else if (data_get_type(state->target) != DATA_TYPE_NULL) {
		return parse_error(state, utf,
				   ESLURM_JSON_UNEXPECTED_DICTIONARY,
				   "unexpected dictionary while parsing "PRINTF_DATA_T,
				   PRINTF_DATA_T_VAL(state->target));
	} else {
		xassert(data_get_type(state->target) == DATA_TYPE_NULL);

		data_set_dict(state->target);

		parse_debug(state, utf,
			    "BEGIN: dictionary while parsing "PRINTF_DATA_T,
			    PRINTF_DATA_T_VAL(state->target));
		return SLURM_SUCCESS;
	}
}

/* takes ownership of new key */
static void _on_dict_key(state_t *state, utf_code_t utf, utf8_t *key,
			 const char *key_source)
{
	xassert(!state->key);
	xfree(state->key);
	state->key = key;

	xassert(state->key && xsize(state->key));

	if (is_debug_active()) {
		state->key_source = key_source;
		xassert(state->key_source);

		xfree(state->key_printable);
		state->key_printable = _printable(state->key);

		parse_debug_hex(state, state->key, utf8len(state->key),
				"new dictionary key \"%s\" for "PRINTF_DATA_T,
				state->key_printable,
				PRINTF_DATA_T_VAL(state->target));
	}
}

static void _enter_dict_key(state_t *state, utf_code_t utf)
{
	data_t *d = state->target;

	xassert(data_get_type(d) == DATA_TYPE_DICT);
	xassert(state->key && state->key[0] && xsize(state->key));

	_push_target(state, state->target);
	state->target = data_key_set(d, cast_cstring(state->key));

	parse_debug(state, utf, "setting "PRINTF_DATA_T_INDEX" = "PRINTF_DATA_T" from %s%s",
		    PRINTF_DATA_T_INDEX_VAL(d, state->key_printable),
		    PRINTF_DATA_T_VAL(state->target), state->key_source,
		    ((data_get_type(state->target) != DATA_TYPE_NULL) ?
		     " overwritting to null" : ""));

	data_set_null(state->target);

	xfree(state->key);
	xfree(state->key_printable);
	state->key_source = NULL;
}

static int _on_comma(state_t *state, utf_code_t utf)
{
	if (data_get_type(state->target) == DATA_TYPE_DICT) {
		if (state->key) {
			return parse_error(state, utf,
					   ESLURM_JSON_UNEXPECTED_COMMA,
					   "comma while parsing "PRINTF_DATA_T_INDEX,
					   PRINTF_DATA_T_INDEX_VAL(state->target,
						state->key_printable));
		} else {
			/*
			 * Nothing to do as key and : will
			 * trigger new entry creation.
			 */
			parse_debug(state, utf, "comma while parsing "PRINTF_DATA_T" without key",
				    PRINTF_DATA_T_VAL(state->target));
			return SLURM_SUCCESS;
		}
	} else if (data_get_type(state->target) == DATA_TYPE_LIST) {
		parse_debug(state, utf, "comma while parsing list");
		return SLURM_SUCCESS;
	} else {
		return parse_error(state, utf, ESLURM_JSON_UNEXPECTED_COMMA,
				   "unexpected comma while parsing "PRINTF_DATA_T,
				   PRINTF_DATA_T_VAL(state->target));
	}
}

static int _on_enter_quoted(state_t *state, utf_code_t utf)
{
	if (data_get_type(state->target) == DATA_TYPE_DICT) {
		if (state->key) {
			parse_debug(state, utf, "BEGIN: quoted string under "PRINTF_DATA_T_INDEX,
				    PRINTF_DATA_T_INDEX_VAL(state->target,
					state->key_printable));
		} else {
			parse_debug(state, utf, "BEGIN: quoted string "PRINTF_DATA_T" key",
				    PRINTF_DATA_T_VAL(state->target));
		}
	} else if (data_get_type(state->target) == DATA_TYPE_LIST) {
		parse_debug(state, utf, "BEGIN: quoted string in "PRINTF_DATA_T,
			    PRINTF_DATA_T_VAL(state->target));
	} else if (data_get_type(state->target) != DATA_TYPE_NULL) {
		return parse_error(state, utf, ESLURM_JSON_UNEXPECTED_QUOTES,
				   "unexpected quotes while parsing "PRINTF_DATA_T,
				   PRINTF_DATA_T_VAL(state->target));
	}

	xassert(!state->quoted);
	xassert(!state->quoted_at);

	state->quoted_at = state->quoted = xmalloc(STRING_ALLOC_MIN);

	parse_debug(state, utf,
		    "BEGIN: quoted string while parsing "PRINTF_DATA_T,
		    PRINTF_DATA_T_VAL(state->target));
	return SLURM_SUCCESS;
}

static int _on_quoted(state_t *state, utf_code_t utf)
{
	int rc = SLURM_SUCCESS;

	if (data_get_type(state->target) == DATA_TYPE_DICT) {
		if (state->key) {
			char *printable = _printable(state->quoted);

			rc = parse_error(state, utf,
					 ESLURM_JSON_UNEXPECTED_QUOTED_STRING,
					 "unexpected quoted string \"%s\" while parsing "PRINTF_DATA_T_INDEX" key",
					 printable,
					 PRINTF_DATA_T_INDEX_VAL(state->target,
						state->key_printable));

			xfree(state->quoted);
			xfree(printable);
		} else {
			_on_dict_key(state, utf, state->quoted, "quoted string");
			state->quoted = NULL;
		}
	} else if (data_get_type(state->target) == DATA_TYPE_LIST) {
		data_t *parent = state->target, *target;

		target = data_list_append(parent);

		if (is_debug_active()) {
			char *index = xstrdup_printf("%zu",
				(data_get_list_length(parent) - 1));

			parse_debug_hex(state,
					state->quoted,
					utf8len(state->quoted),
					"END: parsed quoted string while parsing "PRINTF_DATA_T_INDEX"="PRINTF_DATA_T,
					PRINTF_DATA_T_INDEX_VAL(parent, index),
					PRINTF_DATA_T_VAL(target));

			xfree(index);
		}

		data_set_utf8_own(target, state->quoted);
	} else if (data_get_type(state->target) == DATA_TYPE_NULL) {
		parse_debug_hex(state, state->quoted, utf8len(state->quoted),
				"END: parsed quoted string while parsing "PRINTF_DATA_T,
				PRINTF_DATA_T_VAL(state->target));

		data_set_utf8_own(state->target, state->quoted);
		rc = _pop_target(state, utf);
	} else {
		char *printable = _printable(state->quoted);

		rc = parse_error(state, utf,
				 ESLURM_JSON_UNEXPECTED_QUOTED_STRING,
				 "unexpected quoted string \"%s\" while parsing "PRINTF_DATA_T,
				 printable, PRINTF_DATA_T_VAL(state->target));

		xfree(state->quoted);
		xfree(printable);
	}

	xassert(!state->quoted);
	state->quoted_at = NULL;

	return rc;
}

static bool _is_hex_char(const utf_code_t utf)
{
	return ((utf >= 'a') && (utf <= 'f')) ||
		((utf >= 'A') && (utf <= 'F')) ||
		((utf >= '0') && (utf <= '9'));
}

static int _on_escaped_utf_code(state_t *state, utf_code_t utf)
{
	int rc = SLURM_SUCCESS;
	utf8_t *escaped = NULL;
	utf_code_t eutf;

	xassert(state->quoted);
	xassert(state->escaped[0] == '\\');
	xassert(state->escaped[1] == 'u');

	if (state->escaped_chars <= 0) {
		parse_error(state, utf, ESLURM_JSON_INVALID_ESCAPED,
			    "rejecting \\u escape without any hex characters");
		goto cleanup;
	}

	xassert(state->escaped_chars <= 6);
	xassert(state->escaped_chars >= 4);

	if (!(escaped = utf8ndup(state->escaped + 2, state->escaped_chars)))
		goto cleanup;

	xassert(utf8len(escaped) <= 6);

	if (sscanf(cast_cstring(escaped), "%"SCNx32, &eutf) == 1) {
		parse_debug(state, utf, "END: escaped UTF string \\u%s = "UTF8_PRINTF,
			    escaped, eutf);
		catcharat(state->quoted, state->quoted_at, eutf, rc);
	} else {
		rc = parse_error(state, eutf, ESLURM_JSON_INVALID_ESCAPED,
				 "unable to parse \\u%s to integer for UTF encoding",
				 escaped);
	}

cleanup:
	state->escaped = NULL;
	state->escaped_chars = 0;
	xfree(escaped);
	return rc;
}

static int _on_escaped_utf_char(state_t *state, const utf8_t *p,
				const utf_code_t utf, bool *go_next_char)
{
	xassert(state->escaped[0] == '\\');
	xassert(state->escaped[1] == 'u');
	xassert(p > state->escaped + 1);

	/*
	 * JSON is a little too ambiguous with unicode escape characters
	 *
	 * https://mathiasbynens.be/notes/javascript-escapes:
	 * 	You could define Unicode escape syntax using the following
	 * 	regular expression: \\u[a-fA-F0-9]{4}
	 *
	 * https://www.crockford.com/mckeeman.html:
	 *	A hexcode can contain 4, 5, or 6 hexadecimal digits.
	 *
	 * ECMA-262 11.8.4.3:
	 *	The SV of UnicodeEscapeSequence :: u Hex4Digits is the SV of
	 *	Hex4Digits.
	 *
	 * ECMA-262 10.1:
	 *	All Unicode code point values from U+0000 to U+10FFFF, including
	 *	surrogate code points, may occur in source text where permitted
	 *	by the ECMAScript grammars.
	 *
	 * Javascript only allows \u#### but then allows the full UTF range
	 * which requires 6 hex characters.
	 *
	 * We are also just going to ignore the allowing surrogate code points
	 * as we don't allow UTF-16 encoding.
	 */

	if (_is_hex_char(utf) && (state->escaped_chars < 6)) {
		state->escaped_chars++;
		*go_next_char = true;
		return SLURM_SUCCESS;
	}

	if (state->escaped_chars < 4) {
		int rc;
		utf8_t *escaped = utf8ndup(state->escaped,
					   (state->escaped_chars + 2));
		rc = parse_error(state, utf,
				 ESLURM_JSON_INVALID_ESCAPED,
				 "rejecting %s with %hu/4 required hex characters",
				 escaped, state->escaped_chars);
		xfree(escaped);
		return rc;
	}

	/*
	 * Continue parsing as escaped string already finished
	 */
	*go_next_char = false;
	return _on_escaped_utf_code(state, utf);
}

static int _on_escaped(state_t *state, const utf8_t *p, const utf_code_t utf,
		       bool *go_next_char)
{
	int rc = SLURM_SUCCESS;

	/* inside of escaped sequence */
	xassert(state->quoted);

	if (p != (state->escaped + 1))
		return _on_escaped_utf_char(state, p, utf, go_next_char);

	xassert(p == (state->escaped + 1));

	*go_next_char = true;

	if (utf == 'u') {
		/* capture UTF hex code */
		xassert(!state->escaped_chars);
		return SLURM_SUCCESS;
	}

	/* determine escaped character */
	for (int i = 0; i < ARRAY_SIZE(escaped_chars); i++) {
		if (utf == escaped_chars[i].utf) {
			parse_debug(state, utf, "END: escaped string \\%c",
				    escaped_chars[i].utf);

			catat(state->quoted, state->quoted_at,
			      escaped_chars[i].escaped);

			state->escaped = NULL;
			break;
		}
	}

	if (state->escaped) {
		utf8_t c[UTF8_CHAR_MAX_BYTES];

		(void) write_utf8_character(get_utf8_loggable(utf), c, true);
		rc = parse_error(state, utf, ESLURM_JSON_INVALID_ESCAPED,
				 "Invalid escaped character \"\\%s\"", c);
		state->escaped = NULL;
	}

	return rc;
}

static int _on_enter_list(state_t *state, utf_code_t utf)
{
	data_t *target = state->target, *parent = state->target;

	if (data_get_type(target) == DATA_TYPE_LIST) {
		_push_target(state, parent);
		target = data_set_list(data_list_append(parent));
		state->target = target;

		if (is_debug_active()) {
			char *index = xstrdup_printf("%zu",
				(data_get_list_length(parent) - 1));

			parse_debug(state, utf,
				    "BEGIN: new list in "PRINTF_DATA_T_INDEX"="PRINTF_DATA_T,
				    PRINTF_DATA_T_INDEX_VAL(parent, index),
				    PRINTF_DATA_T_VAL(target));

			xfree(index);
		}

		return SLURM_SUCCESS;
	} else if (data_get_type(target) != DATA_TYPE_NULL) {
		return parse_error(state, utf, ESLURM_JSON_UNEXPECTED_LIST,
				   "unexpected list while parsing "PRINTF_DATA_T,
				   PRINTF_DATA_T_VAL(target));
	}

	data_set_list(target);

	parse_debug(state, utf, "BEGIN: new "PRINTF_DATA_T,
		    PRINTF_DATA_T_VAL(target));
	return SLURM_SUCCESS;
}

static int _on_exit_list(state_t *state, utf_code_t utf)
{
	int rc;
	data_t *list = state->target;

	if (data_get_type(list) != DATA_TYPE_LIST) {
		return parse_error(state, utf, ESLURM_JSON_UNEXPECTED_LIST_END,
				   "unexpected ] while parsing "PRINTF_DATA_T,
				   PRINTF_DATA_T_VAL(list));
	}

	parse_debug(state, utf, "END: "PRINTF_DATA_T, PRINTF_DATA_T_VAL(list));

	rc = _pop_target(state, utf);

	xassert((list != state->target) || (state->parents.stack[0] == list));
	return rc;
}

static int _on_exit_dict(state_t *state, utf_code_t utf)
{
	int rc;
	data_t *dict = state->target;

	if (state->key) {
		/* set already provided key as null */
		if (data_get_type(dict) != DATA_TYPE_NULL) {
			return parse_error(state, utf,
				ESLURM_JSON_UNEXPECTED_DICTIONARY_END,
				"unexpected } while parsing "PRINTF_DATA_T,
				PRINTF_DATA_T_VAL(dict));
		}
		_enter_dict_key(state, utf);
		if ((rc = _pop_target(state, utf)))
			return rc;
	}

	if (data_get_type(dict) != DATA_TYPE_DICT) {
		return parse_error(state, utf,
				   ESLURM_JSON_UNEXPECTED_DICTIONARY_END,
				   "unexpected } while parsing "PRINTF_DATA_T,
				   PRINTF_DATA_T_VAL(dict));
	}

	parse_debug(state, utf, "END: "PRINTF_DATA_T, PRINTF_DATA_T_VAL(dict));

	rc = _pop_target(state, utf);

	xassert((dict != state->target) || (state->parents.stack[0] == dict));
	return rc;
}

static int _on_enter_comment(state_t *state, const utf8_t *p, utf_code_t utf)
{
	state->comment = p;
	state->comment_type = COMMENT_UNKNOWN;
	parse_debug(state, utf, "BEGIN: comment");
	return SLURM_SUCCESS;
}

static int _on_comment(state_t *state, utf_code_t utf)
{
	if (state->comment_type == COMMENT_UNKNOWN) {
		if (utf == '/') {
			state->comment_type = COMMENT_LINE;
		} else if (utf == '*') {
			state->comment_type = COMMENT_SPAN_BEGIN;
		} else {
			utf8_t c[UTF8_CHAR_MAX_BYTES];
			(void) write_utf8_character(get_utf8_loggable(utf), c,
						    true);
			return parse_error(state, utf,
					   ESLURM_JSON_INVALID_COMMENT,
					   "unexpected character %s after starting comment with '/'",
					   c);
		}
	} else if (state->comment_type == COMMENT_LINE) {
		if (utf == '\n') {
			state->comment = NULL;
			parse_debug(state, utf, "END: line comment complete");
		}
	} else if (state->comment_type == COMMENT_SPAN_BEGIN) {
		if (utf == '*')
			state->comment_type = COMMENT_SPAN_END;
	} else if (state->comment_type == COMMENT_SPAN_END) {
		if (utf == '/') {
			parse_debug(state, utf, "END: span comment complete");
			state->comment = NULL;
		} else if (utf == '*') {
			/* do nothing as next char may be '/' */
		} else {
			/* '*' was not followed by '/' */
			state->comment_type = COMMENT_SPAN_BEGIN;
		}
	} else {
		/* should never execute */
		xassert(false);
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

static int _on_unquoted_key(state_t *state, const utf8_t *p, utf_code_t utf,
			    utf8_t *unquoted, const int size)
{
	/* auto convert unquote string before using as key */
	data_t *q = data_new();
	data_set_utf8_own(q, unquoted);

	/* detect and convert type */
	data_convert_type(q, DATA_TYPE_NONE);

	/* convert back to a string needed for key */
	if (data_convert_type(q, DATA_TYPE_STRING) != DATA_TYPE_STRING) {
		int rc;
		char *str_key;

		str_key = xstrdup_printf("%s->"PRINTF_DATA_T,
					 data_get_string_const(q),
					 PRINTF_DATA_T_VAL(q));

		rc = parse_error(state, utf, ESLURM_JSON_INVALID_DICTIONARY_KEY,
				 "unable to determine type of unquoted key "PRINTF_DATA_T_INDEX,
				 PRINTF_DATA_T_INDEX_VAL(q, str_key));

		FREE_NULL_DATA(q);
		xfree(str_key);
		return rc;
	}

	unquoted = utf8dup(data_get_string_const(q));

	_on_dict_key(state, utf, unquoted, "unquoted string");
	unquoted = NULL;

	FREE_NULL_DATA(q);
	return SLURM_SUCCESS;
}

/* takes ownership of unquoted */
static void _apply_type_unquoted(state_t *state, utf_code_t utf, data_t *target,
				 utf8_t *unquoted)
{
	char *unquoted_printable = NULL;

	if (is_debug_active())
		unquoted_printable = _printable(unquoted);

	data_set_utf8_own(target, unquoted);
	unquoted = NULL;

	/*
	 * JSON requires unquoted strings to only be "true", "false", or "null",
	 * or a number (float or integer). I see no reason to apply such a
	 * limitation to Slurm's JSON parser.  Instead, guess the type as best
	 * as possible or just leave it as a string.
	 */
	data_convert_type(target, DATA_TYPE_NONE);

	parse_debug(state, utf, "parsed unquoted string %s as "PRINTF_DATA_T,
		    unquoted_printable, PRINTF_DATA_T_VAL(target));

	xfree(unquoted_printable);
}

static int _on_unquoted(state_t *state, const utf8_t *p, utf_code_t utf)
{
	char *unquoted_printable = NULL;
	const int size = (p - state->unquoted);
	utf8_t *unquoted = utf8ndup(state->unquoted, size);
	data_t *target = state->target;
	int rc = SLURM_ERROR;

	state->unquoted = NULL;

	if (is_debug_active())
		unquoted_printable = _printable(unquoted);

	parse_debug_hex(state, unquoted, size,
			"parsed unquoted string");

	if (data_get_type(target) == DATA_TYPE_DICT) {
		if (state->key) {
			rc = parse_error(state, utf,
					 ESLURM_JSON_INVALID_DICTIONARY_COLON,
					 "unexpected unquoted string %s before : but after key while parsing "PRINTF_DATA_T_INDEX,
					 unquoted_printable,
					 PRINTF_DATA_T_INDEX_VAL(target,
						state->key_printable));
		} else {
			rc = _on_unquoted_key(state, p, utf, unquoted, size);
			unquoted = NULL;
		}
	} else if (data_get_type(target) == DATA_TYPE_LIST) {
		char *str_key;
		data_t *parent = state->target;

		target = data_list_append(parent);
		_apply_type_unquoted(state, utf, target, unquoted);
		unquoted = NULL;
		str_key = xstrdup_printf("%zu",
					 data_get_list_length(parent) - 1);

		parse_debug(state, utf, PRINTF_DATA_T_INDEX"="PRINTF_DATA_T,
			    PRINTF_DATA_T_INDEX_VAL(parent, str_key),
			    PRINTF_DATA_T_VAL(target));

		 xfree(str_key);
		rc = SLURM_SUCCESS;
	} else if (data_get_type(target) != DATA_TYPE_NULL) {
		rc = parse_error(state, utf,
				 ESLURM_JSON_UNEXPECTED_UNQUOTED_STRING,
				 "unexpected unquoted string %s while parsing "PRINTF_DATA_T,
				 unquoted_printable, PRINTF_DATA_T_VAL(target));
	} else {
		xassert(data_get_type(target) == DATA_TYPE_NULL);
		_apply_type_unquoted(state, utf, target, unquoted);
		unquoted = NULL;

		/*
		 * Target was null, so either it is already a child or it is the
		 * last parsing item so we need to go pop from the target stack.
		 */
		rc = _pop_target(state, utf);
	}

	xfree(unquoted);
	xfree(unquoted_printable);
	return rc;
}

extern int serialize_p_string_to_data(data_t **dest, const char *src_ptr,
				      size_t length)
{
	int rc = SLURM_SUCCESS;
	int utf_bytes = 0;
	state_t state = { 0 };
	const utf8_t *src = cast_const_utf8string(src_ptr);
	const utf8_t *p = src; /* floating pointer */
	const utf8_t *end = src + length;

	xassert(dest);

	if (*dest)
		state.target = *dest;
	else
		state.target = *dest = data_new();

	log_flag_hex(DATA, src, length,
		     "parsing string 0x%"PRIxPTR" to "PRINTF_DATA_T,
		     (uintptr_t) src, PRINTF_DATA_T_VAL(state.target));

	_push_target(&state, data_set_null(state.target));

	/* ignore empty source */
	if (!length || !src || !src[0]) {
		goto cleanup;
	} else {
		utf_encoding_schemes_t encoding;

		/* verify string is UTF-8 or unmarked */
		encoding = read_utf_encoding_schema(src, (src + length));

		if ((encoding != UTF_8_ENCODING) &&
		    (encoding != UTF_UNKNOWN_ENCODING)) {
			if (encoding == UTF_16BE_ENCODING)
				rc = ESLURM_UTF16BE_SCHEMA;
			else if (encoding == UTF_16LE_ENCODING)
				rc = ESLURM_UTF16LE_SCHEMA;
			else if (encoding == UTF_32BE_ENCODING)
				rc = ESLURM_UTF32BE_SCHEMA;
			else if (encoding == UTF_32LE_ENCODING)
				rc = ESLURM_UTF32LE_SCHEMA;
			else
				rc = ESLURM_JSON_PARSE_FAILED;

			(void) parse_error(&state, UTF_BYTE_ORDER_MARK_CODE, rc,
					   slurm_strerror(rc));
			goto cleanup;
		}
	}

	while (true) {
		bool is_newline, is_space, is_space_checked = false;
		utf_code_t utf;

		/* increment by size of utf-8 */
		p += utf_bytes;

		xassert(state.target);
		if (state.parents.depth > MAX_DEPTH) {
			rc = parse_error(&state, utf,
					 ESLURM_JSON_PARSE_DEPTH_MAX,
					 slurm_strerror(rc));
			goto cleanup;
		}

		xassert(!rc);
		xassert((!state.unquoted && !state.quoted) ||
			(state.unquoted && !state.quoted) ||
			(!state.unquoted && state.quoted));
		xassert(state.parents.depth >= 0);
		xassert(state.parents.max_depth > 0);
		xassert(xsize(state.parents.stack) > 0);
		xassert(state.line >= 0);
		xassert(state.line <= length);
		xassert(state.col >= 0);
		xassert(state.col <= length);
		xassert(state.comment_type >= COMMENT_UNKNOWN);
		xassert(state.comment_type <= COMMENT_SPAN_END);
		xassert(!state.escaped || state.quoted);
		xassert(!state.quoted_at || state.quoted);
		xassert(!state.key_printable || state.key);
		xassert(!state.key_source || state.key);
		/* there should always be target unless stream is complete */
		xassert(state.target || !state.parents.depth);
		xassert(utf_bytes >= 0);
		/* check that all pointers are in src string */
		xassert((src >= cast_const_utf8string(src_ptr)) && (src <= end));
		xassert(!state.comment ||
			((state.comment >= cast_const_utf8string(src_ptr))
			 && (state.comment <= end)));
		xassert(!state.escaped ||
			((state.escaped >= cast_const_utf8string(src_ptr)) &&
			 (state.escaped <= end)));

		if (p >= end) {
			if (state.unquoted) {
				/* unquoted may be the last character */
				if ((rc = _on_unquoted(&state, p, utf)))
					goto cleanup;
			}
			if (state.key) {
				rc = parse_error(&state, utf,
					ESLURM_JSON_INCOMPLETE_DICTIONARY_KEY,
					"Dictionary key \"%s\" without value",
					state.key_printable);
				goto cleanup;
			}
			if (state.comment) {
				xassert(state.comment_type != COMMENT_UNKNOWN);

				if (state.comment_type == COMMENT_LINE) {
					parse_debug(&state, utf,
						    "END: line comment complete at end of source string");
				} else {
					parse_debug(&state, utf,
						    "END: span comment incomplete at end of source string");
				}
			}
			if (state.escaped) {
				if ((rc = _on_escaped_utf_code(&state, utf)))
					goto cleanup;
			}
			if (state.quoted) {
				rc = parse_error(&state, utf,
					ESLURM_JSON_UNCLOSED_QUOTED_STRING,
					"Invalid quoted string at end of source string");
				goto cleanup;
			}
			if ((state.parents.depth == 1) &&
			    (data_get_type(state.target) != DATA_TYPE_DICT) &&
			    (data_get_type(state.target) != DATA_TYPE_LIST)) {
				parse_debug(&state, utf,
					    "END: parsing completed with "PRINTF_DATA_T" on stack",
					    PRINTF_DATA_T_VAL(state.target));
			} else if (state.parents.depth > 0) {
				utf8_t *stack = _dump_target_stack(&state);

				if (data_get_type(state.target) ==
				    DATA_TYPE_DICT)
					rc = ESLURM_JSON_UNCLOSED_DICTIONARY;
				else if (data_get_type(state.target) ==
					 DATA_TYPE_LIST)
					rc = ESLURM_JSON_UNCLOSED_LIST;
				else
					rc = ESLURM_JSON_PARSE_FAILED;

				(void) parse_error(&state, utf, rc,
					"JSON string terminated unexpectedly with parsing stack[%d/%d]:%s",
					state.parents.depth,
					state.parents.max_depth, stack);
				xfree(stack);
				goto cleanup;
			}

			parse_debug(&state, '\0',
				    "END: parsing %zd byte string at 0x%"PRIxPTR,
				    length, (uintptr_t) src);
			break;
		}

		if (*p < UTF_ASCII_MAX_CODE) {
			/* avoid parsing UTF when it's only ASCII */
			utf = *p;
			utf_bytes = 1;
		} else if ((rc = read_utf8_character(p, end, &utf,
						     &utf_bytes))) {
			rc = parse_error(&state, utf, rc, slurm_strerror(rc));
			goto cleanup;
		}

		xassert(utf >= 0);

		/* track col/line separately so logged offsets are valid */
		if ((is_newline = is_utf8_newline(utf))) {
			state.line++;
			state.col = 0;
		} else {
			/*
			 * column count is not perfect (due to zero width,
			 * halfwidth and fullwidth) but should be good enough as
			 * UTF has multiple spacing characters which are hard to
			 * count here.
			 */
			state.col++;
		}

		if (is_debug_active()) {
			utf_code_t log_utf = get_utf8_loggable(utf);
			utf8_t c[UTF8_CHAR_MAX_BYTES];

			(void) write_utf8_character(log_utf, c, true);

			parse_debug(&state, utf,
				    "parsing whitespace=%c newline=%c control=%c "UTF8_PRINTF"=%s",
				    (is_utf8_whitespace(utf) ? 'T' : 'F'),
				    (is_newline ? 'T' : 'F'),
				    (is_utf8_control(utf) ? 'T' : 'F'),
				    utf, c);
		}

		if (state.comment) {
			if ((rc = _on_comment(&state, utf)))
				goto cleanup;

			continue;
		}

		if (state.escaped) {
			bool go_next_char = true;

			if ((rc = _on_escaped(&state, p, utf, &go_next_char)))
				goto cleanup;

			if (go_next_char)
				continue;
		}

		if (state.quoted) {
			if (!is_space_checked) {
				is_space = is_utf8_space(utf);
				is_space_checked = true;
			}

			if (!is_newline && !is_space && is_utf8_control(utf)) {
				/*
				 * Control characters are never valid in quoted
				 * string as they are expected to be escaped but
				 * we are going to allow whitespace.
				 */
				rc = parse_error(&state, utf,
					ESLURM_JSON_INVALID_CHAR,
					"unexpected control character");
				goto cleanup;
			}

			if (utf == '"') {
				rc = _on_quoted(&state, utf);
			} else if (utf == '\\') {
				/* escaped sequence */
				parse_debug(&state, utf,
					    "BEGIN: escaped string");
				xassert(!state.escaped);
				state.escaped = p;
			} else {
				catcharat(state.quoted, state.quoted_at, utf,
					  rc);
			}

			if (rc)
				goto cleanup;

			continue;
		}

		if (state.unquoted) {
			if (_is_unquoted_char(utf)) {
				/* still accruing unquoted chars */
				continue;
			} else {
				/* end unquoted string */
				if ((rc = _on_unquoted(&state, p, utf)))
					goto cleanup;
			}
		}

		/* ignore whitespace */
		if (is_newline)
			continue;
		if (!is_space_checked) {
			is_space = is_utf8_space(utf);
			is_space_checked = true;
		}
		if (is_space)
			continue;

		if (!state.target) {
			rc = parse_error(&state, utf, ESLURM_JSON_INVALID_CHAR,
					 "unexpected character at expected end of input");
			goto cleanup;
		}

		xassert(!state.key ||
			(data_get_type(state.target) == DATA_TYPE_DICT));

		if (utf == '"') {
			/* begin quoted string */
			if ((rc = _on_enter_quoted(&state, utf)))
				goto cleanup;
			continue;
		}

		if (utf == ',') {
			if ((rc = _on_comma(&state, utf)))
				goto cleanup;
			continue;
		}

		if (utf == ':') {
			/* dict/object member */

			if (data_get_type(state.target) != DATA_TYPE_DICT) {
				rc = parse_error(&state, utf,
					ESLURM_JSON_INVALID_DICTIONARY_COLON,
					"unexpected colon while parsing "PRINTF_DATA_T,
					PRINTF_DATA_T_VAL(state.target));
				goto cleanup;
			}

			if (state.key) {
				_enter_dict_key(&state, utf);
			} else {
				rc = parse_error(&state, utf,
					ESLURM_JSON_INVALID_DICTIONARY_COLON,
					"unexpected colon before dictionary key string while parsing "PRINTF_DATA_T,
					PRINTF_DATA_T_VAL(state.target));
				goto cleanup;
			}

			continue;
		}

		if (utf == '[') {
			/* begin list/array */
			if ((rc = _on_enter_list(&state, utf)))
				goto cleanup;
			continue;
		}

		if (utf == ']') {
			/* end list/array */
			if ((rc = _on_exit_list(&state, utf)))
				goto cleanup;
			continue;
		}

		if (utf == '{') {
			/* dictionary/object  */
			if ((rc = _on_enter_dict(&state, utf)))
				goto cleanup;
			continue;
		}

		if (utf == '}') {
			/* end dictionary/object */
			if ((rc = _on_exit_dict(&state, utf)))
				goto cleanup;
			continue;
		}

		if (utf == '/') {
			if ((rc = _on_enter_comment(&state, p, utf)))
				goto cleanup;
			continue;
		}

		/* match all possible valid unquoted strings */
		if (_is_unquoted_char(utf)) {
			xassert(!state.unquoted);

			/* mark start of unquoted string */
			state.unquoted = p;

			parse_debug(&state, utf, "BEGIN: unquoted string");
			continue;
		}

		if (utf == UTF_BYTE_ORDER_MARK_CODE) {
			parse_debug(&state, utf,
				    "ignoring byte order mark code");
			continue;
		}

		rc = parse_error(&state, utf, ESLURM_JSON_INVALID_CHAR,
				 "unexpected character");
		goto cleanup;
	}

cleanup:
	xfree(state.quoted);
	xfree(state.key);
	xfree(state.parents.stack);

	parse_debug(&state, 0, "END: parsed string 0x%"PRIxPTR" to "PRINTF_DATA_T": %s",
		    (uintptr_t) src, PRINTF_DATA_T_VAL(state.target),
		    slurm_strerror(rc));

	if (rc) {
		if (data_get_type(*dest) != DATA_TYPE_NULL) {
			const utf8_t *line = p;
			size_t debug_len = 0;

			if (state.col >= 0) {
				line -= state.col;
				debug_len += state.col * 2;
			}

			if (debug_len < 40)
				debug_len = 40;

			if (line < src)
				line = src;

			if (line + debug_len > end)
				debug_len = end - line;

			/*
			 * Try logging area around failure to help with
			 * debugging
			 */
			log_flag_hex(DATA, line, debug_len,
				     "%s: failed parsing %zu byte string 0x%"PRIxPTR" around [%04d:%04d]",
				     __func__, length, (uintptr_t) src,
				     state.line, state.col);

			parse_debug(&state, 0,
				    "releasing destination "PRINTF_DATA_T" on failure: %s",
				    PRINTF_DATA_T_VAL(state.target),
				    slurm_strerror(rc));
		}
		FREE_NULL_DATA(*dest);
	}

	return rc;
}

static void _cat_depth(dump_state_t *state)
{
	if (state->flags & SER_FLAGS_PRETTY)
		for (int i = 0; i < state->depth; i++)
			catat(state->dst, state->dst_at, "\t");
}

static int _cat_data_string(dump_state_t *state, const utf8_t *src,
			    const size_t len)
{
	log_flag_hex(DATA, src, len, "dump quoted string");

	catat(state->dst, state->dst_at, "\"");

	for (const utf8_t *end = src + len; src < end; src++) {
		bool found = false;
		int utf_bytes;
		utf_code_t utf;
		int rc;

		if ((rc = read_utf8_character(src, end, &utf, &utf_bytes)))
			return dump_error(state, utf, rc, slurm_strerror(rc));

		if (utf > UTF_ASCII_MAX_CODE) {
			dump_debug(state, utf,
				   "Dumping escaped %d bytes UTF-8 character",
				   utf_bytes);
			catfmtat(state->dst, state->dst_at, "\\u%06"PRIx32,
				     ((utf < 0) ? UTF_REPLACEMENT_CODE : utf));
			src += (utf_bytes - 1);
			continue;
		}

		for (int i = 0; i < ARRAY_SIZE(escaped_chars); i++) {
			if (utf == escaped_chars[i].escaped[0]) {
				const utf8_t c[] = {
					'\\',
					escaped_chars[i].utf,
					'\0'
				};
				dump_debug(state, utf, "Dumping escaped character "UTF8_PRINTF"=\\%c",
					   utf, escaped_chars[i].utf);
				catat(state->dst, state->dst_at, c);
				found = true;
				break;
			}
		}

		if (!found) {
			int rc = SLURM_SUCCESS;

			dump_debug(state, utf, "dumping ASCII character");
			catcharat(state->dst, state->dst_at, *src, rc);

			if (rc)
				return rc;
		}
	}

	catat(state->dst, state->dst_at, "\"");
	return SLURM_SUCCESS;
}

static data_for_each_cmd_t _foreach_cat_data_list(const data_t *src, void *arg)
{
	dump_state_t *state = arg;
	int rc;

	if (state->index > 0) {
		if (data_get_type(state->parent) != DATA_TYPE_DICT) {
			catat(state->dst, state->dst_at, ",");

			if (state->flags & SER_FLAGS_PRETTY)
				catat(state->dst, state->dst_at, "\n");
		}
		_cat_depth(state);
	} else {
		if (state->flags & SER_FLAGS_PRETTY)
			catat(state->dst, state->dst_at, "\n");
		_cat_depth(state);
	}

	state->index++;

	if ((rc = _cat_data(state, src))) {
		if (!state->rc)
			state->rc = rc;

		return DATA_FOR_EACH_FAIL;
	}

	return DATA_FOR_EACH_CONT;
}

static data_for_each_cmd_t _foreach_cat_data_dict(const char *key,
						  const data_t *src, void *arg)
{
	dump_state_t *state = arg;
	int rc;

	if (state->index > 0) {
		if (data_get_type(state->parent) != DATA_TYPE_LIST) {
			if (state->flags & SER_FLAGS_PRETTY)
				catat(state->dst, state->dst_at, ",\n");
			else
				catat(state->dst, state->dst_at, "\n");
		}
		_cat_depth(state);
	} else {
		if (state->flags & SER_FLAGS_PRETTY)
			catat(state->dst, state->dst_at, "\n");
		_cat_depth(state);
	}

	state->index++;

	if ((rc = _cat_data_string(state, cast_const_utf8string(key),
				   utf8len(key)))) {
		if (!state->rc)
			state->rc = rc;

		return DATA_FOR_EACH_FAIL;
	}

	if (state->flags & SER_FLAGS_PRETTY)
		catat(state->dst, state->dst_at, ": ");
	else
		catat(state->dst, state->dst_at, ":");

	if ((rc = _cat_data(state, src))) {
		if (!state->rc)
			state->rc = rc;

		return DATA_FOR_EACH_FAIL;
	}

	return DATA_FOR_EACH_CONT;
}

static int _cat_data_null(dump_state_t *state, const data_t *src)
{
	xassert(data_get_type(src) == DATA_TYPE_NULL);
	catat(state->dst, state->dst_at, "null");
	return SLURM_SUCCESS;
}

static int _cat_data_int_64(dump_state_t *state, const data_t *src)
{
	xassert(data_get_type(src) == DATA_TYPE_INT_64);
	catfmtat(state->dst, state->dst_at, "%"PRId64, data_get_int(src));
	return SLURM_SUCCESS;
}

static int _cat_data_float(dump_state_t *state, const data_t *src)
{
	double f;
	char *str = NULL;

	xassert(data_get_type(src) == DATA_TYPE_FLOAT);

	f = data_get_float(src);

	/*
	 * RFC4627 and ECMA-262 section 24.5.2:
	 *	Finite numbers are stringified as if by calling
	 *	ToString(number). NaN and Infinity regardless of sign are
	 *	represented as the String null.
	 *
	 * The relavent standards say we should coerce basically everything
	 * that's not a number (or defined in the std) into null but every
	 * implementation I have found of JSON will honor +-Infinity and +-NaN
	 * as unquoted strings. So we are going to dump them so that information
	 * is not getting lost during conversion to JSON and hope the clients
	 * don't explode.  Failure to do this breaks the test unit where we
	 * parse, dump and parse and then compare for equiventancy too which is
	 * also super annoying.
	 */

	if (signbit(f) >= 0) {
		if (isinf(f))
			str = "Infinity";
		else if (isnan(f))
			str = "NaN";
	} else {
		if (isinf(f))
			str = "-Infinity";
		else if (isnan(f))
			str = "-NaN";
	}

	if (!str)
		catfmtat(state->dst, state->dst_at, "%e",
		      data_get_float(src));
	else
		catat(state->dst, state->dst_at, str);

	return SLURM_SUCCESS;
}

static int _cat_data_bool(dump_state_t *state, const data_t *src)
{
	xassert(data_get_type(src) == DATA_TYPE_BOOL);
	catat(state->dst, state->dst_at,
	      (data_get_bool(src) ? "true" : "false"));
	return SLURM_SUCCESS;
}

static int _cat_data_list(dump_state_t *state, const data_t *src)
{
	const data_t *parent = state->parent;
	int index = state->index;

	catat(state->dst, state->dst_at, "[");

	if (data_get_list_length(src) > 0) {
		state->depth++;
		state->index = 0;
		state->parent = src;

		if (data_list_for_each_const(src, _foreach_cat_data_list,
					     state) < 0)
			return SLURM_ERROR;

		if ((state->flags & SER_FLAGS_PRETTY) && (state->index > 0))
			catat(state->dst, state->dst_at, "\n");

		state->parent = parent;
		state->index = index;
		state->depth--;

		_cat_depth(state);
	}

	catat(state->dst, state->dst_at, "]");
	return SLURM_SUCCESS;
}

static int _cat_data_dict(dump_state_t *state, const data_t *src)
{
	const data_t *parent = state->parent;
	int index = state->index;

	catat(state->dst, state->dst_at, "{");

	if (data_get_dict_length(src) > 0) {
		state->depth++;
		state->index = 0;
		state->parent = src;

		if (data_dict_for_each_const(src, _foreach_cat_data_dict,
					     state) < 0)
			return SLURM_ERROR;

		if ((state->flags & SER_FLAGS_PRETTY) && (state->index > 0))
			catat(state->dst, state->dst_at, "\n");

		state->parent = parent;
		state->index = index;
		state->depth--;

		_cat_depth(state);
	}

	catat(state->dst, state->dst_at, "}");
	return SLURM_SUCCESS;
}

static int _cat_data(dump_state_t *state, const data_t *src)
{
	switch (data_get_type(src)) {
	case DATA_TYPE_NULL:
		return _cat_data_null(state, src);
	case DATA_TYPE_INT_64:
		return _cat_data_int_64(state, src);
	case DATA_TYPE_STRING:
	{
		const utf8_t *str =
			cast_const_utf8string(data_get_string_const(src));
		return _cat_data_string(state, str, utf8len(str));
	}
	case DATA_TYPE_FLOAT:
		return _cat_data_float(state, src);
	case DATA_TYPE_BOOL:
		return _cat_data_bool(state, src);
	case DATA_TYPE_LIST:
		return _cat_data_list(state, src);
	case DATA_TYPE_DICT:
		return _cat_data_dict(state, src);
	case DATA_TYPE_MAX:
	case DATA_TYPE_NONE:
		fatal_abort("%s: invalid data type: %s", __func__,
			    data_type_to_string(data_get_type(src)));
	}

	fatal_abort("%s: should never get here", __func__);
}

extern int serialize_p_data_to_string(char **dest, size_t *length,
				      const data_t *src,
				      serializer_flags_t flags)
{
	int rc = SLURM_SUCCESS;
	dump_state_t state = {
		.flags = flags,
	};

	state.dst_at = state.dst = xmalloc(INITIAL_OUTPUT_STRING_ALLOC);

	/*
	 * Always start JSON output with BOM to notify reader we are outputting
	 * with UTF-8 encoding. Will not be visible on any UTF compatible
	 * terminal emulator but may break pre-UTF terminals...do those even
	 * exist any more?
	 */
	catcharat(state.dst, state.dst_at, UTF_BYTE_ORDER_MARK_CODE, rc);
	xassert(!rc);

	if (!(rc = _cat_data(&state, src))) {
		xassert(!state.depth);
		xfree(*dest);
		*dest = cast_cstring(state.dst);
		if (length)
			*length = utf8len(state.dst);

		log_flag_hex(DATA, state.dst, (length ? *length : 0),
			     "%s: dumped "PRINTF_DATA_T" successfully",
			     __func__, PRINTF_DATA_T_VAL(src));
	} else {
		log_flag_hex(DATA, src, utf8len(state.dst),
			     "%s: dumping "PRINTF_DATA_T" failed",
			     __func__, PRINTF_DATA_T_VAL(src));

		if (length)
			*length = 0;

		if (!state.rc)
			state.rc = rc;
		xfree(state.dst);
	}

	return state.rc;
}
