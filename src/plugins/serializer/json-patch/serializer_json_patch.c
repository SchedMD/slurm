/*****************************************************************************\
 *  serializer_json_patch.c - Serializer for RFC 6902 JSON Patch
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
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include "config.h"

#include <stdarg.h>
#include <stdint.h>
#include <string.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/slurm_xlator.h"

#include "src/common/data.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/read_config.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/interfaces/serializer.h"

/* Required Slurm plugin symbols: */
const char plugin_name[] = "Serializer JSON Patch plugin";
const char plugin_type[] = "serializer/json-patch";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

/* Required for serializer plugins: */
const serializer_mimetype_t mime_types[] = {
	{ .mimetype = "application/json-patch+json" }, /* RFC 6902 section 6 */
	{ 0 },
};

/*
 * Maximum nesting depth a patch may produce.
 *
 * data_free(), data_copy() and the serializers all recurse over the tree.
 * Parsers bound their own input depth, but "add" and "copy" build nodes
 * without a parser, so the appliers have to bound it themselves.
 */
#define DATA_PATCH_MAX_DEPTH 64

/*
 * Maximum number of nodes "add", "copy" and "replace" may contribute to a
 * document while a patch is applied.
 *
 * A check on the finished result comes too late: "copy" duplicates a whole
 * subtree, so each operation can multiply the size of the working copy. The
 * budget is charged as each operation runs, so a patch is refused before it
 * has built anything large.
 */
#define DATA_PATCH_MAX_NODES (64 * 1024)

#define parse_error(rc, fmt, ...) \
	_parse_log(LOG_LEVEL_ERROR, rc, __func__, fmt, ##__VA_ARGS__)

#ifndef NDEBUG
#define parse_debug(fmt, ...) \
	do { \
		if ((slurm_conf.debug_flags & DEBUG_FLAG_DATA) && \
		    (get_log_level() >= LOG_LEVEL_DEBUG5)) \
			_parse_log(LOG_LEVEL_DEBUG, SLURM_SUCCESS, __func__, \
				   fmt, ##__VA_ARGS__); \
	} while (0)
#else
#define parse_debug(fmt, ...) \
	do { \
	} while (0)
#endif

typedef enum {
	DATA_POINTER_FLAG_NONE = 0,
	/* create missing dictionary keys along the path */
	DATA_POINTER_FLAG_CREATE = SLURM_BIT(0),
	/* accept "-" (end of array) as the final reference token */
	DATA_POINTER_FLAG_ALLOW_APPEND = SLURM_BIT(1),
} data_pointer_flags_t;

/*
 * Result of resolving an RFC 6901 JSON Pointer.
 *
 * found and parent are borrowed from the tree and must not be free'd. Only
 * token is owned; release the struct with _data_pointer_free_members().
 */
#define DATA_POINTER_MAGIC 0x0b1e7701

typedef struct {
	int magic; /* DATA_POINTER_MAGIC */
	data_t *found; /* resolved entry, or NULL when the final token names
			* something that does not exist yet */
	data_t *parent; /* dict or list holding the final token. NULL when the
			 * pointer was "", which addresses the root. */
	char *token; /* unescaped final reference token, NULL for the root */
	size_t index; /* offset into parent when parent is a list, or
		       * data_get_list_length(parent) for the "-" token.
		       * NO_VAL64 when parent is a dict or NULL. */
	bool append; /* final token was "-" */
} data_pointer_t;

#define DATA_POINTER_INITIALIZER \
	((data_pointer_t) { .magic = DATA_POINTER_MAGIC, .index = NO_VAL64 })

#define DEPTH_ARGS_MAGIC 0x0b1e7702

typedef struct {
	int magic; /* DEPTH_ARGS_MAGIC */
	int depth;
	bool ok;
} depth_args_t;

#define COUNT_ARGS_MAGIC 0x0b1e7703

typedef struct {
	int magic; /* COUNT_ARGS_MAGIC */
	size_t count;
} count_args_t;

#define APPLY_PATCH_ARGS_MAGIC 0x0b1e7704

typedef struct {
	int magic; /* APPLY_PATCH_ARGS_MAGIC, DO NOT ALPHABETIZE */
	size_t budget;
	size_t index;
	int rc;
	data_t *target;
} apply_patch_args_t;

typedef enum {
	PATCH_OP_INVALID = 0,
	PATCH_OP_ADD,
	PATCH_OP_REMOVE,
	PATCH_OP_REPLACE,
	PATCH_OP_MOVE,
	PATCH_OP_COPY,
	PATCH_OP_TEST,
} patch_op_t;

static const struct {
	patch_op_t op;
	const char *str;
	bool need_value;
} patch_ops[] = {
	{ PATCH_OP_ADD, "add", true },
	{ PATCH_OP_REMOVE, "remove", false },
	{ PATCH_OP_REPLACE, "replace", true },
	{ PATCH_OP_MOVE, "move", false },
	{ PATCH_OP_COPY, "copy", false },
	{ PATCH_OP_TEST, "test", true },
};

static int _parse_log(const log_level_t level, int rc, const char *func_name,
		      const char *fmt, ...)
	__attribute__((format(printf, 4, 5)));

static bool _is_depth_ok(const data_t *data, int depth);

static void _count_nodes(const data_t *data, count_args_t *args);

static int _parse_log(const log_level_t level, int rc, const char *func_name,
		      const char *fmt, ...)
{
	va_list ap;
	char *log = NULL;

	va_start(ap, fmt);
	log = vxstrfmt(fmt, ap);
	va_end(ap);

	log_var(level, "%s: %s", func_name, log);

	xfree(log);
	return rc;
}

static void _data_pointer_free_members(data_pointer_t *ptr)
{
	if (!ptr)
		return;

	xassert(ptr->magic == DATA_POINTER_MAGIC);

	/* found and parent are borrowed from the tree */
	xfree(ptr->token);
	*ptr = (data_pointer_t) {
		.magic = ~DATA_POINTER_MAGIC,
		.index = NO_VAL64,
	};
}

/*
 * Unescape one RFC 6901 reference token.
 *
 * IN/OUT src - ptr to the first byte of the token. Advanced to the '/' that
 *	ends the token, or to its NUL terminator.
 * OUT dst - unescaped token (caller must xfree()). Not written on failure.
 * RET SLURM_SUCCESS, ENOMEM, or ESLURM_JSON_POINTER_INVALID when the
 *	escaping is invalid. An empty token is valid and addresses the key "".
 */
static int _unescape_token(const char **src, char **dst)
{
	const char *i = *src;
	char *t = NULL, *d = NULL;

	/* unescaping only ever shrinks, so the source length always fits */
	if (!(d = t = try_xmalloc(strlen(i) + 1)))
		return ENOMEM;

	while (*i && (*i != '/')) {
		if (*i != '~') {
			*d++ = *i++;
			continue;
		}

		/*
		 * "~1" is '/' and "~0" is '~'. Unescaping left to right in
		 * this order is what makes "~01" come out as "~1" instead of
		 * '/'. Anything else after '~' is not a valid escape.
		 */
		i++;
		if (*i == '0') {
			*d++ = '~';
		} else if (*i == '1') {
			*d++ = '/';
		} else {
			xfree(t);
			return ESLURM_JSON_POINTER_INVALID;
		}
		i++;
	}

	*src = i;
	*dst = t;

	return SLURM_SUCCESS;
}

/*
 * Parse an RFC 6901 array index.
 *
 * Section 4 only allows "0" or a digit string with no leading zero, so "01"
 * and "+1" are errors rather than 1.
 *
 * RET SLURM_SUCCESS or ESLURM_JSON_POINTER_INVALID
 */
static int _parse_index(const char *token, size_t *index)
{
	size_t v = 0;

	if (!token[0])
		return ESLURM_JSON_POINTER_INVALID;

	if ((token[0] == '0') && token[1])
		return ESLURM_JSON_POINTER_INVALID;

	for (const char *i = token; *i; i++) {
		if ((*i < '0') || (*i > '9'))
			return ESLURM_JSON_POINTER_INVALID;

		/* refuse anything that would wrap rather than truncate it */
		if (v > ((SIZE_MAX - (*i - '0')) / 10))
			return ESLURM_JSON_POINTER_INVALID;

		v = (v * 10) + (*i - '0');
	}

	*index = v;

	return SLURM_SUCCESS;
}

static int _resolve_pointer_char(data_t **data, const char *pointer,
				 const char **src, data_pointer_flags_t flags,
				 data_pointer_t *dst)
{
	data_t *cur = *data;
	char *token = NULL;
	bool last;
	size_t index = NO_VAL64;
	int rc = EINVAL;

	xassert(dst->magic == DATA_POINTER_MAGIC);
	xassert(**src == '/');

	(*src)++;

	if ((rc = _unescape_token(src, &token))) {
		if (rc != ENOMEM)
			return parse_error(
				rc,
				"JSON Pointer \"%s\" has an invalid '~' escape",
				pointer);
		return rc;
	}

	last = !**src;
	parse_debug("token \"%s\": container=%s last=%s", token,
		    data_get_type_string(cur), (last ? "yes" : "no"));

	if (data_get_type(cur) == DATA_TYPE_DICT) {
		data_t *next = data_key_get(cur, token);

		parse_debug("dictionary token \"%s\": %s", token,
			    (next ? "found" : "missing"));
		if (last) {
			dst->parent = cur;
			dst->found = next;
			dst->token = token;
			return SLURM_SUCCESS;
		}

		if (!next && (flags & DATA_POINTER_FLAG_CREATE))
			next = data_set_dict(data_key_set(cur, token));

		if (!next)
			rc = parse_error(
				ESLURM_DATA_PATH_NOT_FOUND,
				"JSON Pointer \"%s\" has no entry \"%s\"",
				pointer, token);

		xfree(token);

		if (rc)
			return rc;

		*data = next;
		return SLURM_SUCCESS;
	}

	if (data_get_type(cur) == DATA_TYPE_LIST) {
		data_t *next = NULL;

		if (!xstrcmp(token, "-")) {
			/*
			 * "-" names the position just past the end,
			 * which only "add" has any use for.
			 */
			if (!last ||
			    !(flags & DATA_POINTER_FLAG_ALLOW_APPEND)) {
				xfree(token);
				return parse_error(
					ESLURM_JSON_POINTER_INVALID,
					"JSON Pointer \"%s\" may not use \"-\" here",
					pointer);
			}

			dst->parent = cur;
			dst->token = token;
			dst->index = data_get_list_length(cur);
			dst->append = true;
			parse_debug("array append position: %zu", dst->index);
			return SLURM_SUCCESS;
		}

		if ((rc = _parse_index(token, &index))) {
			parse_error(
				rc,
				"JSON Pointer \"%s\" has invalid array index \"%s\"",
				pointer, token);
			xfree(token);
			return rc;
		}

		next = data_list_get_at(cur, index);
		parse_debug("array index %zu: %s", index,
			    (next ? "found" : "missing"));

		if (last) {
			dst->parent = cur;
			dst->found = next;
			dst->token = token;
			dst->index = index;
			return SLURM_SUCCESS;
		}

		/*
		 * An array index is never created on the way through:
		 * there is no sensible value to fill the gap with.
		 */
		if (!next)
			rc = parse_error(
				ESLURM_DATA_PATH_NOT_FOUND,
				"JSON Pointer \"%s\" has no entry at index %zu",
				pointer, index);

		xfree(token);

		if (rc)
			return rc;

		*data = next;
		return SLURM_SUCCESS;
	}

	xfree(token);
	return parse_error(
		ESLURM_DATA_PATH_NOT_FOUND,
		"JSON Pointer \"%s\" traverses %s, which holds no entries",
		pointer, data_get_type_string(cur));
}

static int _resolve_pointer(data_t *data, const char *pointer,
			    data_pointer_flags_t flags, data_pointer_t *dst)
{
	const char *i = pointer;
	data_t *cur = data;
	int rc = EINVAL;

	xassert(data);
	xassert(dst);
	xassert(dst->magic == DATA_POINTER_MAGIC);
	xassert(!dst->token);

	if (!data || !pointer)
		return ESLURM_JSON_POINTER_INVALID;

	parse_debug("resolving pointer \"%s\": flags=%#x", pointer,
		    (unsigned int) flags);

	/* "" addresses the whole document */
	if (!pointer[0]) {
		parse_debug("pointer selects document root (%s)",
			    data_get_type_string(data));
		dst->found = data;
		return SLURM_SUCCESS;
	}

	if (pointer[0] != '/')
		return parse_error(
			ESLURM_JSON_POINTER_INVALID,
			"JSON Pointer \"%s\" must be empty or begin with '/'",
			pointer);

	while (*i == '/') {
		if ((rc = _resolve_pointer_char(&cur, pointer, &i, flags, dst)))
			return rc;
	}

	return SLURM_SUCCESS;
}

static data_for_each_cmd_t _foreach_depth_dict(const char *key,
					       const data_t *data, void *arg)
{
	depth_args_t *args = arg;

	xassert(args->magic == DEPTH_ARGS_MAGIC);

	if (!(args->ok = _is_depth_ok(data, args->depth)))
		return DATA_FOR_EACH_FAIL;

	return DATA_FOR_EACH_CONT;
}

static data_for_each_cmd_t _foreach_depth_list(const data_t *data, void *arg)
{
	depth_args_t *args = arg;

	xassert(args->magic == DEPTH_ARGS_MAGIC);

	if (!(args->ok = _is_depth_ok(data, args->depth)))
		return DATA_FOR_EACH_FAIL;

	return DATA_FOR_EACH_CONT;
}

/*
 * RET true when data nests no deeper than DATA_PATCH_MAX_DEPTH
 *
 * data_free(), data_copy() and the serializers all recurse, so a document
 * assembled by repeated "add" or "copy" has to be bounded here: no parser
 * ever saw it.
 */
static bool _is_depth_ok(const data_t *data, int depth)
{
	depth_args_t args = {
		.magic = DEPTH_ARGS_MAGIC,
		.depth = depth + 1,
		.ok = true,
	};

	if (depth > DATA_PATCH_MAX_DEPTH)
		return false;

	switch (data_get_type(data)) {
	case DATA_TYPE_DICT:
		(void) data_dict_for_each_const(data, _foreach_depth_dict,
						&args);
		return args.ok;
	case DATA_TYPE_LIST:
		(void) data_list_for_each_const(data, _foreach_depth_list,
						&args);
		return args.ok;
	default:
		return true;
	}
}

/*
 * Refuse to place value at path when that would nest the document deeper than
 * DATA_PATCH_MAX_DEPTH.
 *
 * This runs before every insertion rather than once on the result: "copy" can
 * double the depth with each operation, and data_copy() and data_free()
 * recurse that deep long before a check on the result could run.
 *
 * RET SLURM_SUCCESS or ESLURM_DATA_PARSING_DEPTH
 */
static int _check_depth(const data_t *value, const char *path)
{
	int depth = 0;

	/* '/' only appears unescaped between reference tokens */
	for (const char *i = path; *i; i++)
		if (*i == '/')
			depth++;

	if (!_is_depth_ok(value, depth))
		return parse_error(
			ESLURM_DATA_PARSING_DEPTH,
			"JSON Pointer \"%s\" would nest the document more than %d levels deep",
			path, DATA_PATCH_MAX_DEPTH);

	return SLURM_SUCCESS;
}

static data_for_each_cmd_t _foreach_count_dict(const char *key,
					       const data_t *data, void *arg)
{
	count_args_t *args = arg;

	xassert(args->magic == COUNT_ARGS_MAGIC);

	_count_nodes(data, args);

	if (args->count > DATA_PATCH_MAX_NODES)
		return DATA_FOR_EACH_FAIL;

	return DATA_FOR_EACH_CONT;
}

static data_for_each_cmd_t _foreach_count_list(const data_t *data, void *arg)
{
	count_args_t *args = arg;

	xassert(args->magic == COUNT_ARGS_MAGIC);

	_count_nodes(data, args);

	if (args->count > DATA_PATCH_MAX_NODES)
		return DATA_FOR_EACH_FAIL;

	return DATA_FOR_EACH_CONT;
}

/*
 * Count data and its descendants into args->count. Stops early once the
 * count passes DATA_PATCH_MAX_NODES, so the cost never exceeds the budget.
 */
static void _count_nodes(const data_t *data, count_args_t *args)
{
	if (++args->count > DATA_PATCH_MAX_NODES)
		return;

	switch (data_get_type(data)) {
	case DATA_TYPE_DICT:
		(void) data_dict_for_each_const(data, _foreach_count_dict,
						args);
		break;
	case DATA_TYPE_LIST:
		(void) data_list_for_each_const(data, _foreach_count_list,
						args);
		break;
	default:
		break;
	}
}

/*
 * Charge the nodes of value against a patch's node budget
 * IN/OUT budget - nodes charged so far
 * IN value - tree about to be added to the document
 * RET SLURM_SUCCESS or ESLURM_DATA_TOO_LARGE
 */
static int _charge(size_t *budget, const data_t *value)
{
	count_args_t args = {
		.magic = COUNT_ARGS_MAGIC,
	};

	_count_nodes(value, &args);
	*budget += args.count;
	parse_debug("charged %zu nodes: budget=%zu limit=%d", args.count,
		    *budget, DATA_PATCH_MAX_NODES);

	if (*budget > DATA_PATCH_MAX_NODES)
		return parse_error(
			ESLURM_DATA_TOO_LARGE,
			"JSON Patch would grow the document past %d entries",
			DATA_PATCH_MAX_NODES);

	return SLURM_SUCCESS;
}

/* RET the entry named by key when it is a string, or NULL */
static const char *_get_str(const data_t *op, const char *key)
{
	const data_t *entry = data_key_get_const(op, key);

	if (!entry || (data_get_type(entry) != DATA_TYPE_STRING))
		return NULL;

	return data_get_string(entry);
}

/* remove the entry a resolved pointer names from its parent */
static int _remove_at(const data_pointer_t *ptr, const char *path)
{
	data_t *removed = NULL;

	xassert(ptr->magic == DATA_POINTER_MAGIC);

	if (!ptr->parent)
		return parse_error(
			ESLURM_JSON_PATCH_FAILED,
			"JSON Pointer \"\" cannot be removed: the whole document has no parent to remove it from");

	if (data_get_type(ptr->parent) == DATA_TYPE_DICT) {
		if (!data_key_unset(ptr->parent, ptr->token))
			return parse_error(ESLURM_DATA_PATH_NOT_FOUND,
					   "JSON Pointer \"%s\" does not exist",
					   path);

		return SLURM_SUCCESS;
	}

	if (ptr->append || !ptr->found)
		return parse_error(ESLURM_DATA_PATH_NOT_FOUND,
				   "JSON Pointer \"%s\" does not exist", path);

	removed = data_list_remove_at(ptr->parent, ptr->index);
	FREE_NULL_DATA(removed);

	return SLURM_SUCCESS;
}

/*
 * Unlink the entry a resolved pointer names from its parent, without copying
 * it. The caller owns the result. ptr must name an existing entry below the
 * root.
 */
static data_t *_detach_at(const data_pointer_t *ptr)
{
	data_t *value = NULL;

	xassert(ptr->magic == DATA_POINTER_MAGIC);
	xassert(ptr->parent && ptr->found);

	if (data_get_type(ptr->parent) == DATA_TYPE_LIST)
		return data_list_remove_at(ptr->parent, ptr->index);

	/* take the contents, then drop the now empty entry from the dict */
	value = data_move(NULL, ptr->found);
	(void) data_key_unset(ptr->parent, ptr->token);

	return value;
}

/*
 * RFC 6902 section 4.1 "add"
 *
 * On a dictionary this creates or replaces the key. On a list it inserts,
 * shifting later entries along, which is what separates it from "replace".
 *
 * The value is copied from copy, or, when take is set, moved out of take,
 * which is left as DATA_TYPE_NULL.
 */
static int _add_value(data_t *target, const char *path, const data_t *copy,
		      data_t *take)
{
	data_pointer_t ptr = DATA_POINTER_INITIALIZER;
	data_t *dst = NULL;
	int rc = EINVAL;

	if ((rc = _check_depth((take ? take : copy), path)))
		return rc;

	if ((rc = _resolve_pointer(target, path, DATA_POINTER_FLAG_ALLOW_APPEND,
				   &ptr)))
		return rc;

	if (!ptr.parent) {
		/* "" replaces the whole document */
		parse_debug("replacing document root");
		dst = target;
	} else if (data_get_type(ptr.parent) == DATA_TYPE_DICT) {
		parse_debug("%s dictionary member \"%s\"",
			    (ptr.found ? "overwriting" : "creating"),
			    ptr.token);
		dst = data_key_set(ptr.parent, ptr.token);
	} else if (ptr.append) {
		parse_debug("appending array entry at index %zu", ptr.index);
		dst = data_list_append(ptr.parent);
	} else if (ptr.index > data_get_list_length(ptr.parent)) {
		rc = parse_error(
			ESLURM_JSON_PATCH_FAILED,
			"JSON Pointer \"%s\" is past the end of a %zu entry array",
			path, data_get_list_length(ptr.parent));
	} else {
		parse_debug("inserting array entry at index %zu", ptr.index);
		dst = data_list_insert_at(ptr.parent, ptr.index);
	}

	if (dst && take)
		data_move(dst, take);
	else if (dst)
		data_copy(dst, copy);

	_data_pointer_free_members(&ptr);

	return rc;
}

static int _op_add(data_t *target, const char *path, const data_t *value)
{
	return _add_value(target, path, value, NULL);
}

/* RFC 6902 section 4.2 "remove" */
static int _op_remove(data_t *target, const char *path)
{
	data_pointer_t ptr = DATA_POINTER_INITIALIZER;
	int rc = EINVAL;

	if ((rc = _resolve_pointer(target, path, DATA_POINTER_FLAG_NONE, &ptr)))
		return rc;

	rc = _remove_at(&ptr, path);

	_data_pointer_free_members(&ptr);

	return rc;
}

/*
 * RFC 6902 section 4.3 "replace"
 *
 * Unlike "add" the location has to exist already.
 */
static int _op_replace(data_t *target, const char *path, const data_t *value)
{
	data_pointer_t ptr = DATA_POINTER_INITIALIZER;
	int rc = EINVAL;

	if ((rc = _check_depth(value, path)))
		return rc;

	if ((rc = _resolve_pointer(target, path, DATA_POINTER_FLAG_NONE, &ptr)))
		return rc;

	if (!ptr.parent)
		data_copy(target, value);
	else if (!ptr.found) {
		rc = parse_error(ESLURM_DATA_PATH_NOT_FOUND,
				 "JSON Pointer \"%s\" does not exist", path);
	} else
		data_copy(ptr.found, value);

	_data_pointer_free_members(&ptr);

	return rc;
}

/* RET true when from names an ancestor of path */
static bool _is_prefix_of(const char *from, const char *path)
{
	size_t len = strlen(from);

	return (!xstrncmp(from, path, len) && (path[len] == '/'));
}

/* RFC 6902 sections 4.4 "move" and 4.5 "copy" */
static int _op_move_copy(data_t *target, const char *from, const char *path,
			 bool remove_src, size_t *budget)
{
	data_pointer_t ptr = DATA_POINTER_INITIALIZER;
	data_t *value = NULL;
	int rc = EINVAL;

	parse_debug("%s from \"%s\" to \"%s\"",
		    (remove_src ? "moving" : "copying"), from, path);

	/*
	 * path is only resolved once the source has been detached or cloned,
	 * so a malformed one has to be refused first. Otherwise a move from ""
	 * would detach the whole document, which has no parent.
	 */
	if (path[0] && (path[0] != '/'))
		return parse_error(
			ESLURM_JSON_POINTER_INVALID,
			"JSON Pointer \"%s\" must be empty or begin with '/'",
			path);

	if (remove_src && _is_prefix_of(from, path))
		return parse_error(
			ESLURM_JSON_PATCH_FAILED,
			"JSON Pointer \"%s\" cannot be moved into its own child \"%s\"",
			from, path);

	if ((rc = _resolve_pointer(target, from, DATA_POINTER_FLAG_NONE, &ptr)))
		return rc;

	if (!ptr.found) {
		_data_pointer_free_members(&ptr);
		return parse_error(ESLURM_DATA_PATH_NOT_FOUND,
				   "JSON Pointer \"%s\" does not exist", from);
	}

	parse_debug("source \"%s\" resolved (%s)", from,
		    data_get_type_string(ptr.found));

	/* A same-location move is a no-op; an array copy still inserts. */
	if (remove_src && !xstrcmp(from, path)) {
		parse_debug("source and destination are identical: no change");
		_data_pointer_free_members(&ptr);
		return SLURM_SUCCESS;
	}

	/* a move only relocates nodes, but a copy adds every one of them */
	if (!remove_src && (rc = _charge(budget, ptr.found))) {
		_data_pointer_free_members(&ptr);
		return rc;
	}

	/*
	 * RFC 6902 defines a move as a remove followed by an add, so the
	 * destination is resolved after the source is gone. Unlinking the
	 * source instead of copying it keeps a move cheap however large the
	 * subtree is. A copy is taken before adding, as the destination may
	 * contain "from", and is then moved into place rather than copied
	 * again.
	 */
	if (remove_src)
		value = _detach_at(&ptr);
	else if ((rc = data_clone(ptr.found, value))) {
		_data_pointer_free_members(&ptr);
		return rc;
	}

	_data_pointer_free_members(&ptr);

	rc = _add_value(target, path, NULL, value);

	FREE_NULL_DATA(value);

	return rc;
}

/* RFC 6902 section 4.6 "test" */
static int _op_test(data_t *target, const char *path, const data_t *value)
{
	data_pointer_t ptr = DATA_POINTER_INITIALIZER;
	int rc = EINVAL;

	if ((rc = _resolve_pointer(target, path, DATA_POINTER_FLAG_NONE, &ptr)))
		return rc;

	/* a missing location never matches, including against a null value */
	if (!data_check_match(ptr.found, value, true)) {
		rc = parse_error(
			ESLURM_JSON_PATCH_TEST_FAILED,
			"JSON Pointer \"%s\" does not hold the tested value",
			path);
	}
	parse_debug("test pointer \"%s\": %s", path,
		    (rc ? "mismatch" : "match"));

	_data_pointer_free_members(&ptr);

	return rc;
}

static int _apply_op(data_t *target, const data_t *op, size_t index,
		     size_t *budget)
{
	const char *op_str, *path, *from = NULL;
	const data_t *value = NULL;
	patch_op_t which = PATCH_OP_INVALID;
	int rc = EINVAL;

	if (data_get_type(op) != DATA_TYPE_DICT)
		return parse_error(ESLURM_JSON_PATCH_INVALID,
				   "JSON Patch operation %zu is not an object",
				   index);

	if (!(op_str = _get_str(op, "op")))
		return parse_error(
			ESLURM_JSON_PATCH_INVALID,
			"JSON Patch operation %zu has no \"op\" string", index);

	for (size_t n = 0; n < ARRAY_SIZE(patch_ops); n++) {
		if (xstrcmp(op_str, patch_ops[n].str))
			continue;

		which = patch_ops[n].op;

		if (patch_ops[n].need_value &&
		    !(value = data_key_get_const(op, "value")))
			return parse_error(
				ESLURM_JSON_PATCH_INVALID,
				"JSON Patch operation %zu (\"%s\") has no \"value\"",
				index, op_str);

		break;
	}

	if (!(path = _get_str(op, "path")))
		return parse_error(
			ESLURM_JSON_PATCH_INVALID,
			"JSON Patch operation %zu (\"%s\") has no \"path\" string",
			index, op_str);

	parse_debug("operation %zu: %s path=\"%s\"", index, op_str, path);

	switch (which) {
	case PATCH_OP_ADD:
		if ((rc = _charge(budget, value)))
			return rc;
		return _op_add(target, path, value);
	case PATCH_OP_REMOVE:
		return _op_remove(target, path);
	case PATCH_OP_REPLACE:
		if ((rc = _charge(budget, value)))
			return rc;
		return _op_replace(target, path, value);
	case PATCH_OP_MOVE:
	case PATCH_OP_COPY:
		if (!(from = _get_str(op, "from")))
			return parse_error(
				ESLURM_JSON_PATCH_INVALID,
				"JSON Patch operation %zu (\"%s\") has no \"from\" string",
				index, op_str);
		return _op_move_copy(target, from, path,
				     (which == PATCH_OP_MOVE), budget);
	case PATCH_OP_TEST:
		return _op_test(target, path, value);
	case PATCH_OP_INVALID:
		return parse_error(
			ESLURM_JSON_PATCH_INVALID,
			"JSON Patch operation %zu has unknown op \"%s\"", index,
			op_str);
	}

	fatal_abort("%s: should never run", __func__);
}

static data_for_each_cmd_t _foreach_apply_op(const data_t *op, void *arg)
{
	apply_patch_args_t *args = arg;

	xassert(args->magic == APPLY_PATCH_ARGS_MAGIC);

	args->rc = _apply_op(args->target, op, args->index, &args->budget);
	parse_debug("operation %zu completed: rc=%d", args->index, args->rc);
	args->index++;

	return args->rc ? DATA_FOR_EACH_FAIL : DATA_FOR_EACH_CONT;
}

static int _patch(data_t *dst, const data_t *patch)
{
	apply_patch_args_t args = {
		.magic = APPLY_PATCH_ARGS_MAGIC,
		.rc = EINVAL,
	};
	data_t *tmp = NULL;
	int rc = SLURM_SUCCESS;

	xassert(dst);
	xassert(patch);
	xassert(dst != patch);

	if (!dst || !patch)
		return ESLURM_DATA_PTR_NULL;

	if (data_get_type(patch) != DATA_TYPE_LIST)
		return parse_error(
			ESLURM_DATA_EXPECTED_LIST,
			"JSON Patch document must be an array of operations");

	/* the document being patched counts against the budget too */
	if ((rc = _charge(&args.budget, dst)))
		return rc;

	/*
	 * RFC 6902 section 5 requires the document to be left untouched when
	 * any operation fails, so every operation runs against a copy that is
	 * only moved into place once the whole patch has applied.
	 */
	tmp = NULL;
	if ((rc = data_clone(dst, tmp)))
		return parse_error(rc, "Unable to copy the target document: %s",
				   slurm_strerror(rc));
	parse_debug("applying %zu operations to working copy",
		    data_get_list_length(patch));

	args.target = tmp;
	if (data_get_list_length(patch)) {
		(void) data_list_for_each_const(patch, _foreach_apply_op,
						&args);
		rc = args.rc;
	}

	/*
	 * Every insertion is bounded as it runs, so this only catches a target
	 * that was already nested too deeply.
	 */
	if (!rc && !_is_depth_ok(tmp, 0)) {
		rc = parse_error(
			ESLURM_DATA_PARSING_DEPTH,
			"JSON Patch result is nested more than %d levels deep",
			DATA_PATCH_MAX_DEPTH);
	}

	if (!rc && data_get_list_length(patch) &&
	    data_check_match(dst, tmp, true)) {
		parse_debug("patch leaves the document unchanged");
		rc = SLURM_NO_CHANGE_IN_DATA;
	}

	if (!rc) {
		parse_debug("committing patch");
		data_move(dst, tmp);
	} else {
		parse_debug("discarding working copy: rc=%d", rc);
	}

	FREE_NULL_DATA(tmp);

	return rc;
}

extern int serialize_p_init(serializer_flags_t flags)
{
	log_flag(DATA, "loaded");

	return SLURM_SUCCESS;
}

extern void serialize_p_fini(void)
{
	log_flag(DATA, "unloaded");
}

/* creating a patch is not supported */
extern int serialize_p_data_to_string(char **dest, size_t *length, data_t *src,
				      serializer_flags_t flags)
{
	return ESLURM_NOT_SUPPORTED;
}

/*
 * Apply a JSON Patch document to a target.
 *
 * Unlike every other serializer, *dest is not NULL here: it holds the
 * target document, which the patch is applied against in place.
 *
 * The patch itself is read by the JSON serializer; this plugin only
 * validates and applies it per RFC 6902.
 *
 * Returns SLURM_NO_CHANGE_IN_DATA when a nonempty patch leaves the target
 * document unchanged, instead of committing an identical result.
 */
extern int serialize_p_string_to_data(data_t **dest, const char *src,
				      size_t length)
{
	data_t *patch = NULL;
	int rc = EINVAL;

	if (!*dest)
		return parse_error(
			ESLURM_DATA_PTR_NULL,
			"a JSON Patch needs the target document in dest, not just the patch");

	parse_debug("reading %zu patch bytes: target=%s", length,
		    data_get_type_string(*dest));

	if ((rc = serialize_g_string_to_data(&patch, src, length,
					     MIME_TYPE_JSON))) {
		parse_debug("JSON parsing failed: rc=%d", rc);
		return rc;
	}

	parse_debug("JSON patch parsed: %s", data_get_type_string(patch));
	rc = _patch(*dest, patch);
	parse_debug("patch finished: rc=%d", rc);

	FREE_NULL_DATA(patch);

	return rc;
}

extern int serialize_p_dump(serialize_dump_state_t **state_ptr,
			    data_parser_t *parser, data_parser_type_t type,
			    void *src, ssize_t src_bytes, buf_t *dst,
			    serializer_flags_t flags)
{
	return ESLURM_NOT_SUPPORTED;
}

extern int serialize_p_parse(serialize_parse_state_t **state_ptr,
			     data_parser_t *parser, data_parser_type_t type,
			     void *dst, ssize_t dst_bytes, buf_t *src)
{
	return ESLURM_NOT_SUPPORTED;
}
