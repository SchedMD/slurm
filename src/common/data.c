/*****************************************************************************\
 *  data.c - generic data_t structures
 *****************************************************************************
 *  Copyright (C) 2019 SchedMD LLC.
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

#define _ISOC99_SOURCE	/* needed for lrint */

#include <ctype.h>
#include <math.h>

#include "src/common/data.h"
#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/read_config.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#define DATA_DEFINE_DICT_PATH_BUFFER_SIZE 1024
#define DATA_MAGIC 0x1992189F
#define DATA_LIST_MAGIC 0x1992F89F
#define DATA_LIST_NODE_MAGIC 0x1921F89F

typedef struct data_list_s data_list_t;
typedef struct data_list_node_s data_list_node_t;

typedef enum {
	TYPE_NONE = 0, /* invalid or unknown type */
	/* only for bounds checks to avoid any overlap with DATA_TYPE_* */
	TYPE_START = 0xFF00,
	TYPE_NULL, /* ECMA-262:4.3.13 NULL type */
	TYPE_LIST, /* ECMA-262:22.1 Array Object (ordered list) */
	TYPE_DICT, /* ECMA-262:23.1 Map Object (dictionary) */
	TYPE_INT_64, /*  64bit signed integer
				This exists as an convenient storage type.
				ECMA does not have an integer primitive.
				ECMA-262:7.1.4 ToInteger() returns approx
				this value with some rounding. */
	TYPE_STRING_PTR, /* ECMA-262:4.3.18 String type */
	TYPE_STRING_INLINE, /* string stored in union directly */
	TYPE_FLOAT, /* ECMA-262:6.1.6 Number type */
	TYPE_BOOL, /* ECMA-262:4.3.15 Boolean type */
	TYPE_MAX /* only for bounds checking */
} type_t;

static const struct {
	data_type_t external_type;
	type_t internal_type;
} type_map[] = {
	{ DATA_TYPE_NULL, TYPE_NULL },
	{ DATA_TYPE_LIST, TYPE_LIST },
	{ DATA_TYPE_DICT, TYPE_DICT },
	{ DATA_TYPE_INT_64, TYPE_INT_64 },
	{ DATA_TYPE_STRING, TYPE_STRING_PTR },
	{ DATA_TYPE_STRING, TYPE_STRING_INLINE },
	{ DATA_TYPE_FLOAT, TYPE_FLOAT },
	{ DATA_TYPE_BOOL, TYPE_BOOL },
};

typedef struct data_list_node_s {
	int magic;
	data_list_node_t *next;

	data_t *data;
	char *key; /* key for dictionary (only) */
} data_list_node_t;

/* Single linked list for list_u and dict_u */
typedef struct data_list_s {
	int magic;
	size_t count;

	data_list_node_t *begin;
	data_list_node_t *end;
} data_list_t;

/*
 * Data is based on the JSON data type and has the same types.
 * Data forms a tree structure.
 * Please avoid direct access of this struct and only use access functions.
 * The nature of this struct may change at any time, only pass around pointers
 * created from data_new().
 */
struct data_s {
	int magic;
	type_t type;

	union { /* append "_u" to every type to avoid reserved words */
		data_list_t *list_u;
		data_list_t *dict_u;
		int64_t int_u;
		char string_inline_u[sizeof(void *)];
		char *string_ptr_u;
		double float_u;
		bool bool_u;
	} data;
};

typedef struct {
	char *path;
	char *at;
	const char *token;
} merge_path_strings_t;

typedef struct {
	const data_t *a;
	const data_t *b;
	bool mask;
} find_dict_match_t;

typedef struct {
	size_t count;
	type_t match;
} convert_args_t;

static void _check_magic(const data_t *data);
static void _release(data_t *data);
static void _release_data_list_node(data_list_t *dl, data_list_node_t *dn);
static size_t _convert_tree(data_t *data, const type_t match);
static char *_type_to_string(type_t type);

static data_list_t *_data_list_new(void)
{
	data_list_t *dl = xmalloc(sizeof(*dl));
	dl->magic = DATA_LIST_MAGIC;

	log_flag(DATA, "%s: new data-list(0x%"PRIxPTR")[%zu]",
		 __func__, (uintptr_t) dl, dl->count);

	return dl;
}

static void _check_data_list_node_magic(const data_list_node_t *dn)
{
	xassert(dn);
	xassert(dn->magic == DATA_LIST_NODE_MAGIC);
	/* make sure not linking to self */
	xassert(dn->next != dn);
	/* key can be NULL for list, but not NULL length string */
	xassert(!dn->key || dn->key[0]);
}

static void _check_data_list_magic(const data_list_t *dl)
{
#ifndef NDEBUG
	data_list_node_t *end = NULL;

	xassert(dl);
	xassert(dl->magic == DATA_LIST_MAGIC);

	if (dl->begin) {
		/* walk forwards verify */
		int c = 0;
		data_list_node_t *i = dl->begin;

		while (i) {
			c++;
			_check_data_list_node_magic(i);
			end = i;
			i = i->next;
		}

		xassert(c == dl->count);
	}

	xassert(end == dl->end);
#endif /* !NDEBUG */
}

/* verify node is in parent list */
static void _check_data_list_node_parent(const data_list_t *dl,
					 const data_list_node_t *dn)
{
#ifndef NDEBUG
	data_list_node_t *i = dl->begin;
	while (i) {
		if (i == dn)
			return;
		i = i->next;
	}

	/* found an orphan? */
	fatal_abort("%s: unexpected orphan node", __func__);
#endif /* !NDEBUG */
}

static void _release_data_list_node(data_list_t *dl, data_list_node_t *dn)
{
	_check_data_list_magic(dl);
	_check_data_list_node_magic(dn);
	_check_data_list_node_parent(dl, dn);
	data_list_node_t *prev;

	log_flag(DATA, "%s: free data-list(0x%"PRIxPTR")[%zu]",
		 __func__, (uintptr_t) dl, dl->count);

	/* walk list to find new previous */
	for (prev = dl->begin; prev && prev->next != dn; ) {
		_check_data_list_node_magic(prev);
		prev = prev->next;
		if (prev)
			_check_data_list_node_magic(prev);
	}

	if (dn == dl->begin) {
		/* at the beginning */
		xassert(!prev);
		dl->begin = dn->next;

		if (dl->end == dn) {
			dl->end = NULL;
			xassert(!dn->next);
		}
	} else if (dn == dl->end) {
		/* at the end */
		xassert(!dn->next);
		xassert(prev);
		dl->end = prev;
		prev->next = NULL;
	} else {
		/* somewhere in middle */
		xassert(prev);
		xassert(prev != dn);
		xassert(prev->next == dn);
		xassert(dl->begin != dn);
		xassert(dl->end != dn);
		prev->next = dn->next;
	}

	dl->count--;
	FREE_NULL_DATA(dn->data);
	xfree(dn->key);

	dn->magic = ~DATA_LIST_NODE_MAGIC;
	xfree(dn);
}

static void _release_data_list(data_list_t *dl)
{
	data_list_node_t *n = dl->begin, *i;
#ifndef NDEBUG
	int count = 0;
	const int init_count = dl->count;
#endif

	_check_data_list_magic(dl);

	if (!n) {
		xassert(!dl->count);
		xassert(!dl->end);
		goto finish;
	}

	xassert(dl->end);

	while((i = n)) {
		n = i->next;
		_release_data_list_node(dl, i);

#ifndef NDEBUG
		count++;
#endif
	}


#ifndef NDEBUG
	xassert(count == init_count);
#endif

finish:
	dl->magic = ~DATA_LIST_MAGIC;
	xfree(dl);
}

/*
 * Create new data list node entry
 * IN d - data type to take ownership of
 * IN key - dictionary key to dup or NULL
 */
static data_list_node_t *_new_data_list_node(data_t *d, const char *key)
{
	data_list_node_t *dn = xmalloc(sizeof(*dn));
	dn->magic = DATA_LIST_NODE_MAGIC;

	_check_magic(d);

	dn->data = d;
	if (key) {
		dn->key = xstrdup(key);

		log_flag(DATA, "%s: new dictionary entry data-list-node(0x%"PRIxPTR")[%s]=%pD",
			 __func__, (uintptr_t) dn, dn->key, dn->data);
	} else {
		log_flag(DATA, "%s: new list entry data-list-node(0x%"PRIxPTR")=%pD",
			 __func__, (uintptr_t) dn, dn->data);
	}

	return dn;
}

static void _data_list_append(data_list_t *dl, data_t *d, const char *key)
{
	data_list_node_t *n = _new_data_list_node(d, key);
	_check_data_list_magic(dl);
	_check_magic(d);

	if (dl->end) {
		xassert(!dl->end->next);
		_check_data_list_node_magic(dl->end);
		_check_data_list_node_magic(dl->begin);

		dl->end->next = n;
		dl->end = n;
	} else {
		xassert(!dl->count);
		dl->end = n;
		dl->begin = n;
	}

	dl->count++;

	if (n->key)
		log_flag(DATA, "%s: append dictionary entry data-list-node(0x%"PRIxPTR")[%s]=%pD",
			 __func__, (uintptr_t) n, n->key, n->data);
	else
		log_flag(DATA, "%s: append list entry data-list-node(0x%"PRIxPTR")=%pD",
			 __func__, (uintptr_t) n, n->data);
}

static void _data_list_prepend(data_list_t *dl, data_t *d, const char *key)
{
	data_list_node_t *n = _new_data_list_node(d, key);
	_check_data_list_magic(dl);
	_check_magic(d);

	if (dl->begin) {
		_check_data_list_node_magic(dl->begin);
		n->next = dl->begin;
		dl->begin = n;
	} else {
		xassert(!dl->count);
		dl->begin = n;
		dl->end = n;
	}

	dl->count++;

	log_flag(DATA, "%s: prepend %pD[%s]->data-list-node(0x%"PRIxPTR")[%s]=%pD",
		 __func__, d, key, (uintptr_t) n, n->key, n->data);
}

extern data_t *data_new(void)
{
	data_t *data = xmalloc(sizeof(*data));
	data->magic = DATA_MAGIC;
	data->type = TYPE_NULL;

	log_flag(DATA, "%s: new %pD", __func__, data);

	return data;
}

static void _check_magic(const data_t *data)
{
	if (!data)
		return;

	xassert(data->type > TYPE_START);
	xassert(data->type < TYPE_MAX);
	xassert(data->magic == DATA_MAGIC);

	if (data->type == TYPE_NULL)
		/* make sure NULL type has a NULL value */
		xassert(data->data.list_u == NULL);
	if (data->type == TYPE_LIST)
		_check_data_list_magic(data->data.list_u);
	if (data->type == TYPE_DICT)
		_check_data_list_magic(data->data.dict_u);
}

static void _release(data_t *data)
{
	_check_magic(data);

	switch (data->type) {
	case TYPE_LIST:
		_release_data_list(data->data.list_u);
		break;
	case TYPE_DICT:
		_release_data_list(data->data.dict_u);
		break;
	case TYPE_STRING_PTR:
		xfree(data->data.string_ptr_u);
		break;
	default:
		/* other types don't need to be freed */
		break;
	}

	data->type = TYPE_NONE;
	/* always zero data in debug mode */
	xassert(memset(&data->data, 0, sizeof(data->data)));
}

extern void data_free(data_t *data)
{
	if (!data)
		return;

	log_flag(DATA, "%s: free %pD", __func__, data);

	_check_magic(data);
	_release(data);

	data->magic = ~DATA_MAGIC;
	data->type = TYPE_NONE;
	xfree(data);
}

extern data_type_t data_get_type(const data_t *data)
{
	if (!data)
		return DATA_TYPE_NONE;

	_check_magic(data);

	for (int i = 0; i < ARRAY_SIZE(type_map); i++)
		if (type_map[i].internal_type == data->type)
			return type_map[i].external_type;

	return DATA_TYPE_NONE;
}

extern data_t *data_set_float(data_t *data, double value)
{
	_check_magic(data);
	if (!data)
		return NULL;

	data->type = TYPE_FLOAT;
	data->data.float_u = value;

	log_flag(DATA, "%s: set %pD=%e", __func__, data, value);

	return data;
}

extern data_t *data_set_null(data_t *data)
{
	_check_magic(data);
	if (!data)
		return NULL;
	_release(data);

	data->type = TYPE_NULL;
	xassert((memset(&data->data, 0, sizeof(data->data))));

	log_flag(DATA, "%s: set %pD=null", __func__, data);

	return data;
}

extern data_t *data_set_bool(data_t *data, bool value)
{
	_check_magic(data);
	if (!data)
		return NULL;
	_release(data);

	data->type = TYPE_BOOL;
	data->data.bool_u = value;

	log_flag(DATA, "%s: set %pD=%s",
		 __func__, data, (value ? "true" : "false"));

	return data;
}

extern data_t *data_set_int(data_t *data, int64_t value)
{
	_check_magic(data);
	if (!data)
		return NULL;
	_release(data);

	data->type = TYPE_INT_64;
	data->data.int_u = value;

	log_flag(DATA, "%s: set %pD=%"PRId64, __func__, data, value);

	return data;
}

static void _set_data_string_ptr(data_t *data, const size_t len,
				  char **value_ptr)
{
	data->type = TYPE_STRING_PTR;
	data->data.string_ptr_u = *value_ptr;
	*value_ptr = NULL;
	log_flag_hex(DATA, data->data.string_ptr_u, len, "%s: set string %pD",
		     __func__, data);
}

static void _set_data_string_inline(data_t *data, const size_t len,
				    const char *value)
{
	memmove(data->data.string_inline_u, value, len + 1);
	data->type = TYPE_STRING_INLINE;
	log_flag_hex(DATA, data->data.string_inline_u, len,
		     "%s: set inline string %pD", __func__, data);
}

extern data_t *data_set_string(data_t *data, const char *value)
{
	int len;

	_check_magic(data);

	if (!data)
		return NULL;
	_release(data);

	if (!value) {
		data->type = TYPE_NULL;

		log_flag(DATA, "%s: set %pD=null", __func__, data);
		return data;
	}

	if ((len = strlen(value)) < sizeof(data->data.string_inline_u)) {
		_set_data_string_inline(data, len, value);
	} else {
		char *dval = xstrdup(value);
		_set_data_string_ptr(data, len, &dval);
	}

	return data;
}

extern data_t *_data_set_string_own(data_t *data, char **value_ptr)
{
	char *value;
	int len;
	_check_magic(data);

	if (!data) {
		xfree(*value_ptr);
		return NULL;
	}

	_release(data);

	value = *value_ptr;
	*value_ptr = NULL;

	if (!value) {
		data->type = TYPE_NULL;

		log_flag(DATA, "%s: set %pD=null", __func__, data);
		return data;
	}

#ifndef NDEBUG
	char *old_value = value;

	/* check that the string was xmalloc()ed and actually has contents */
	xassert(xsize(value) > 0);
	/*
	 * catch use after free by the caller by using the existing xfree()
	 * functionality
	 */
	value = xstrdup(value);
	/* releasing original string instead of NULLing original pointer */
	xfree(old_value);
#endif

	if ((len = strlen(value)) < sizeof(data->data.string_inline_u)) {
		_set_data_string_inline(data, len, value);
		/* we don't need to keep this string alloc */
		xfree(value);
	} else {
		_set_data_string_ptr(data, len, &value);
	}

	xassert(!value);
	return data;
}

extern data_t *data_set_dict(data_t *data)
{
	_check_magic(data);

	if (!data)
		return NULL;
	_release(data);

	data->type = TYPE_DICT;
	data->data.dict_u = _data_list_new();

	log_flag(DATA, "%s: set %pD to dictionary", __func__, data);

	return data;
}

extern data_t *data_set_list(data_t *data)
{
	_check_magic(data);

	if (!data)
		return NULL;
	_release(data);

	data->type = TYPE_LIST;
	data->data.list_u = _data_list_new();

	log_flag(DATA, "%s: set %pD to list", __func__, data);

	return data;
}

extern data_t *data_list_append(data_t *data)
{
	data_t *ndata = NULL;
	_check_magic(data);

	xassert(data && (data->type == TYPE_LIST));
	if (!data || data->type != TYPE_LIST)
		return NULL;

	ndata = data_new();
	_data_list_append(data->data.list_u, ndata, NULL);

	log_flag(DATA, "%s: appended %pD[%zu]=%pD",
		 __func__, data, data->data.list_u->count, ndata);

	return ndata;
}

extern data_t *data_list_prepend(data_t *data)
{
	data_t *ndata = NULL;
	_check_magic(data);

	if (!data || data->type != TYPE_LIST)
		return NULL;

	ndata = data_new();
	_data_list_prepend(data->data.list_u, ndata, NULL);

	log_flag(DATA, "%s: prepended %pD[%zu]=%pD",
		 __func__, data, data->data.list_u->count, ndata);

	return ndata;
}

extern data_t *data_list_dequeue(data_t *data)
{
	data_list_node_t *n;
	data_t *ret = NULL;
	_check_magic(data);

	if (!data || data->type != TYPE_LIST)
		return NULL;

	if (!(n = data->data.list_u->begin))
		return NULL;

	_check_data_list_node_magic(n);

	/* extract out data for caller */
	SWAP(ret, n->data);

	/* remove node from list */
	_release_data_list_node(data->data.list_u, n);

	log_flag(DATA, "%s: dequeued %pD[%zu]=%pD",
		 __func__, data, data->data.list_u->count, ret);

	return ret;
}

static data_for_each_cmd_t _data_list_join(const data_t *src, void *arg)
{
	data_t *dst = (data_t *) arg;
	data_t *dst_entry;
	_check_magic(src);
	_check_magic(dst);
	xassert(dst->type == TYPE_LIST);

	log_flag(DATA, "%s: list join data %pD to %pD", __func__, src, dst);

	dst_entry = data_list_append(dst);
	data_copy(dst_entry, src);

	log_flag(DATA, "%s: list join %pD to %pD[%zu]=%pD",
		 __func__, src, dst, dst->data.list_u->count, dst_entry);

	 return DATA_FOR_EACH_CONT;
}

extern data_t *data_list_join(const data_t **data, bool flatten_lists)
{
	data_t *dst = data_set_list(data_new());

	for (size_t i = 0; data[i]; i++) {
		log_flag(DATA, "%s: %s list join %pD to %pD[%zu]",
			 __func__, (flatten_lists ? "flattened" : ""),
			 data[i], dst, dst->data.list_u->count);

		if (flatten_lists && (data[i]->type == TYPE_LIST))
			(void) data_list_for_each_const(data[i],
							_data_list_join, dst);
		else /* simple join */
			_data_list_join(data[i], dst);
	}

	return dst;
}

extern const data_t *data_key_get_const(const data_t *data, const char *key)
{
	const data_list_node_t *i;

	_check_magic(data);
	if (!data)
		return NULL;

	xassert(data->type == TYPE_DICT);
	if (!key || data->type != TYPE_DICT)
		return NULL;

	/* don't bother searching empty dictionary */
	if (!data->data.dict_u->count)
		return NULL;

	_check_data_list_magic(data->data.dict_u);
	i = data->data.dict_u->begin;
	while (i) {
		_check_data_list_node_magic(i);

		if (!xstrcmp(key, i->key))
			break;

		i = i->next;
	}

	if (i)
		return i->data;
	else
		return NULL;
}

static bool _match_string(const char *key, data_t *data, void *needle_ptr)
{
	const char *needle = needle_ptr;
	return !xstrcmp(key, needle);
}

extern data_t *data_key_get(data_t *data, const char *key)
{
	return data_dict_find_first(data, _match_string, (void *) key);
}

extern data_t *data_key_get_int(data_t *data, int64_t key)
{
	char *key_str = xstrdup_printf("%"PRId64, key);
	data_t *node = data_key_get(data, key_str);

	xfree(key_str);

	return node;
}

extern data_t *data_list_find_first(
	data_t *data,
	bool (*match)(const data_t *data, void *needle),
	void *needle)
{
	data_list_node_t *i;

	_check_magic(data);
	if (!data)
		return NULL;

	xassert(data->type == TYPE_LIST);
	if (data->type != TYPE_LIST)
		return NULL;

	/* don't bother searching empty list */
	if (!data->data.list_u->count)
		return NULL;

	_check_data_list_magic(data->data.list_u);
	i = data->data.list_u->begin;
	while (i) {
		_check_data_list_node_magic(i);

		if (match(i->data, needle))
			break;

		i = i->next;
	}

	if (i)
		return i->data;
	else
		return NULL;
}

extern data_t *data_dict_find_first(
	data_t *data,
	bool (*match)(const char *key, data_t *data, void *needle),
	void *needle)
{
	data_list_node_t *i;

	_check_magic(data);
	if (!data)
		return NULL;

	xassert(data->type == TYPE_DICT);
	if (data->type != TYPE_DICT)
		return NULL;

	/* don't bother searching empty dictionary */
	if (!data->data.dict_u->count)
		return NULL;

	_check_data_list_magic(data->data.dict_u);
	i = data->data.dict_u->begin;
	while (i) {
		_check_data_list_node_magic(i);

		if (match(i->key, i->data, needle))
			break;

		i = i->next;
	}

	if (i)
		return i->data;
	else
		return NULL;
}

extern data_t *data_key_set(data_t *data, const char *key)
{
	data_t *d;

	_check_magic(data);

	if (!data)
		return NULL;

	xassert(data->type == TYPE_DICT);
	xassert(key && key[0]);
	if (!key || !key[0] || data->type != TYPE_DICT)
		return NULL;

	if ((d = data_key_get(data, key))) {
		log_flag(DATA, "%s: overwrite existing key in %pD[%s]=%pD",
			 __func__, data, key, d);
		return d;
	}

	d = data_new();
	_data_list_append(data->data.dict_u, d, key);

	log_flag(DATA, "%s: populate new key in %pD[%s]=%pD",
		 __func__, data, key, d);

	return d;
}

extern data_t *data_key_set_int(data_t *data, int64_t key)
{
	char *key_str = xstrdup_printf("%"PRId64, key);
	data_t *node = data_key_set(data, key_str);

	xfree(key_str);

	return node;
}

extern bool data_key_unset(data_t *data, const char *key)
{
	data_list_node_t *i;

	_check_magic(data);
	if (!data)
		return false;

	xassert(data->type == TYPE_DICT);
	if (!key || data->type != TYPE_DICT)
		return NULL;

	_check_data_list_magic(data->data.dict_u);
	i = data->data.dict_u->begin;
	while (i) {
		_check_data_list_node_magic(i);

		if (!xstrcmp(key, i->key))
			break;

		i = i->next;
	}

	if (!i) {
		log_flag(DATA, "%s: remove non-existent key in %pD[%s]",
			 __func__, data, key);
		return false;
	}

	log_flag(DATA, "%s: remove existing key in %pD[%s]=data-list-node(0x%"PRIxPTR")[%s]=%pD",
		 __func__, data, key, (uintptr_t) i, i->key, i->data);

	_release_data_list_node(data->data.dict_u, i);

	return true;
}

extern double data_get_float(const data_t *data)
{
	_check_magic(data);

	if (!data)
		return NAN;

	xassert(data->type == TYPE_FLOAT);
	return data->data.float_u;
}

extern bool data_get_bool(const data_t *data)
{
	_check_magic(data);

	if (!data)
		return false;

	xassert(data->type == TYPE_BOOL);
	return data->data.bool_u;
}

extern int64_t data_get_int(const data_t *data)
{
	_check_magic(data);

	if (!data)
		return 0;

	if (data->type == TYPE_NULL)
		return 0;

	xassert(data->type == TYPE_INT_64);
	return data->data.int_u;
}

extern char *data_get_string(data_t *data)
{
	_check_magic(data);

	if (!data)
		return NULL;

	xassert((data->type == TYPE_STRING_PTR) ||
		(data->type == TYPE_STRING_INLINE) ||
		(data->type == TYPE_NULL));

	if (data->type == TYPE_STRING_PTR) {
		return data->data.string_ptr_u;
	} else if (data->type == TYPE_STRING_INLINE) {
		return data->data.string_inline_u;
	} else {
		return NULL;
	}
}

extern const char *data_get_string_const(const data_t *data)
{
	_check_magic(data);

	if (!data)
		return NULL;

	xassert((data->type == TYPE_STRING_PTR) ||
		(data->type == TYPE_STRING_INLINE));

	if (data->type == TYPE_STRING_PTR) {
		return data->data.string_ptr_u;
	} else if (data->type == TYPE_STRING_INLINE) {
		return data->data.string_inline_u;
	} else {
		return NULL;
	}
}

extern int data_get_string_converted(const data_t *d, char **buffer)
{
	_check_magic(d);
	char *_buffer = NULL;
	bool cloned;

	if (!d || !buffer)
		return ESLURM_DATA_PTR_NULL;

	if ((d->type != TYPE_STRING_PTR) && (d->type != TYPE_STRING_INLINE)) {
		/* copy the data and then convert it to a string type */
		data_t *dclone = data_new();
		data_copy(dclone, d);
		if (data_convert_type(dclone, DATA_TYPE_STRING) ==
		    DATA_TYPE_STRING)
			_buffer = xstrdup(data_get_string(dclone));
		FREE_NULL_DATA(dclone);
		cloned = true;
	} else {
		_buffer = xstrdup(data_get_string_const(d));
		if (!_buffer)
			_buffer = xstrdup("");
		cloned = false;
	}

	if (_buffer) {
		*buffer = _buffer;

		log_flag_hex(DATA, _buffer, strlen(_buffer),
			     "%s: string %sat %pD=string@0x%"PRIxPTR"[%zu]",
			     __func__, (cloned ? "conversion and cloned " : ""),
			     d, (uintptr_t) _buffer, strlen(_buffer));

		return SLURM_SUCCESS;
	}

	log_flag(DATA, "%s: %pD string conversion failed", __func__, d);

	return ESLURM_DATA_CONV_FAILED;
}

extern int data_copy_bool_converted(const data_t *d, bool *buffer)
{
	_check_magic(d);
	int rc = ESLURM_DATA_CONV_FAILED;

	if (!d || !buffer)
		return ESLURM_DATA_PTR_NULL;

	if (d->type != TYPE_BOOL) {
		data_t *dclone = data_new();
		data_copy(dclone, d);
		if (data_convert_type(dclone, DATA_TYPE_BOOL) ==
		    DATA_TYPE_BOOL) {
			*buffer = data_get_bool(dclone);
			rc = SLURM_SUCCESS;
		}
		FREE_NULL_DATA(dclone);

		log_flag(DATA, "%s: converted %pD=%s",
			 __func__, d, (*buffer ? "true" : "false"));
		return rc;
	}

	*buffer = data_get_bool(d);
	return SLURM_SUCCESS;
}

extern int data_get_bool_converted(data_t *d, bool *buffer)
{
	int rc;
	_check_magic(d);

	if (!d || !buffer)
		return ESLURM_DATA_PTR_NULL;

	/* assign value if converted successfully */
	rc = data_copy_bool_converted(d, buffer);
	if (!rc)
		data_set_bool(d, *buffer);

	return rc;
}

extern int data_get_int_converted(const data_t *d, int64_t *buffer)
{
	_check_magic(d);
	int rc = SLURM_SUCCESS;

	if (!d || !buffer)
		return ESLURM_DATA_PTR_NULL;

	if (d->type != TYPE_INT_64) {
		data_t *dclone = data_new();
		data_copy(dclone, d);
		if (data_convert_type(dclone, DATA_TYPE_INT_64) ==
		    DATA_TYPE_INT_64)
			*buffer = data_get_int(dclone);
		else
			rc = ESLURM_DATA_CONV_FAILED;
		FREE_NULL_DATA(dclone);
	} else {
		*buffer = data_get_int(d);
	}

	log_flag(DATA, "%s: converted %pD=%"PRId64, __func__, d, *buffer);

	return rc;
}

extern size_t data_get_dict_length(const data_t *data)
{
	_check_magic(data);

	if (!data)
		return 0;

	xassert(data->type == TYPE_DICT);
	return data->data.dict_u->count;
}

extern size_t data_get_list_length(const data_t *data)
{
	_check_magic(data);

	if (!data)
		return 0;

	xassert(data->type == TYPE_LIST);
	return data->data.list_u->count;
}

extern data_t *data_get_list_last(data_t *data)
{
	data_list_node_t *i;
	_check_magic(data);

	if (!data)
		return NULL;

	xassert(data->type == TYPE_LIST);
	if (data->type != TYPE_LIST)
		return NULL;

	if (!data->data.list_u->count)
		return NULL;

	i = data->data.list_u->begin;
	_check_data_list_magic(data->data.list_u);
	while (i) {
		_check_data_list_node_magic(i);
		xassert(!i->key);

		if (!i->next) {
			log_flag(DATA, "%s: %pD[%s]=%pD",
				 __func__, data, i->key, i->data);
			return i->data;
		}

		i = i->next;
	}

	fatal_abort("%s: malformed data list", __func__);
}

extern int data_list_split_str(data_t *dst, const char *src, const char *token)
{
	char *save_ptr = NULL;
	char *tok = NULL;
	char *str = xstrdup(src);

	if (dst->type == TYPE_NULL)
		data_set_list(dst);

	xassert(dst->type == TYPE_LIST);
	if (dst->type != TYPE_LIST)
		return SLURM_ERROR;

	if (str && !str[0])
		xfree(str);

	if (!str)
		return SLURM_SUCCESS;

	tok = strtok_r(str, "/", &save_ptr);
	while (tok) {
		data_t *e = data_list_append(dst);
		xstrtrim(tok);

		data_set_string(e, tok);

		log_flag_hex(DATA, tok, strlen(tok),
			     "%s: split string from 0x%"PRIxPTR" to %pD[%zu]=%pD",
			     __func__, src, dst, dst->data.list_u->count, e);

		tok = strtok_r(NULL, "/", &save_ptr);
	}
	xfree(str);

	return SLURM_SUCCESS;
}

static data_for_each_cmd_t _foreach_join_str(const data_t *data, void *arg)
{
	char *b = NULL;
	merge_path_strings_t *args = arg;

	data_get_string_converted(data, &b);

	xstrfmtcatat(args->path, &args->at, "%s%s%s",
		     (!args->path ? args->token : ""),
		     (args->at ? args->token : ""), b);

	xfree(b);

	return DATA_FOR_EACH_CONT;
}

extern int data_list_join_str(char **dst, const data_t *src, const char *token)
{
	merge_path_strings_t args = {
		.token = token,
	};

	xassert(!*dst);
	xassert(src->type == TYPE_LIST);

	if (data_list_for_each_const(src, _foreach_join_str, &args) < 0) {
		xfree(args.path);
		return SLURM_ERROR;
	}

	*dst = args.path;

	log_flag_hex(DATA, *dst, strlen(*dst),
		     "%s: %pD string joined with token %s",
		     __func__, src, token);

	return SLURM_SUCCESS;
}

extern int data_list_for_each_const(const data_t *d, DataListForFConst f, void *arg)
{
	int count = 0;
	const data_list_node_t *i;

	_check_magic(d);

	if (!d || (d->type != TYPE_LIST)) {
		error("%s: for each attempted on non-list object (0x%"PRIXPTR")",
		      __func__, (uintptr_t) d);
		return -1;
	}

	i = d->data.list_u->begin;
	_check_data_list_magic(d->data.list_u);
	while (i) {
		_check_data_list_node_magic(i);

		xassert(!i->key);
		data_for_each_cmd_t cmd = f(i->data, arg);
		count++;

		xassert(cmd > DATA_FOR_EACH_INVALID);
		xassert(cmd < DATA_FOR_EACH_MAX);

		switch (cmd) {
		case DATA_FOR_EACH_CONT:
			break;
		case DATA_FOR_EACH_DELETE:
			fatal_abort("%s: delete attempted against const",
				    __func__);
			break;
		case DATA_FOR_EACH_FAIL:
			count *= -1;
			/* fall through */
		case DATA_FOR_EACH_STOP:
			i = NULL;
			break;
		default:
			fatal_abort("%s: invalid cmd", __func__);
		}

		if (i)
			i = i->next;
	}

	return count;
}

extern int data_list_for_each(data_t *d, DataListForF f, void *arg)
{
	int count = 0;
	data_list_node_t *i;

	_check_magic(d);

	if (!d || (d->type != TYPE_LIST)) {
		error("%s: for each attempted on non-list %pD", __func__, d);
		return -1;
	}

	i = d->data.list_u->begin;
	_check_data_list_magic(d->data.list_u);
	while (i) {
		_check_data_list_node_magic(i);

		xassert(!i->key);
		data_for_each_cmd_t cmd = f(i->data, arg);
		count++;

		xassert(cmd > DATA_FOR_EACH_INVALID);
		xassert(cmd < DATA_FOR_EACH_MAX);

		switch (cmd) {
		case DATA_FOR_EACH_CONT:
			if (i)
				i = i->next;
			break;
		case DATA_FOR_EACH_DELETE:
		{
			data_list_node_t *idel = i;
			i = i->next;
			_release_data_list_node(d->data.list_u, idel);
			_check_data_list_magic(d->data.list_u);
			break;
		}
		case DATA_FOR_EACH_FAIL:
			count *= -1;
			/* fall through */
		case DATA_FOR_EACH_STOP:
			i = NULL;
			break;
		default:
			fatal_abort("%s: invalid cmd", __func__);
		}
	}

	return count;
}

extern int data_dict_for_each_const(const data_t *d, DataDictForFConst f, void *arg)
{
	int count = 0;
	const data_list_node_t *i;

	if (!d)
		return 0;

	_check_magic(d);

	if (data_get_type(d) != DATA_TYPE_DICT) {
		error("%s: for each attempted on non-dict %pD", __func__, d);
		return -1;
	}

	i = d->data.dict_u->begin;
	_check_data_list_magic(d->data.dict_u);
	while (i) {
		data_for_each_cmd_t cmd;

		_check_data_list_node_magic(i);

		cmd = f(i->key, i->data, arg);
		count++;

		xassert(cmd > DATA_FOR_EACH_INVALID);
		xassert(cmd < DATA_FOR_EACH_MAX);

		switch (cmd) {
		case DATA_FOR_EACH_CONT:
			break;
		case DATA_FOR_EACH_DELETE:
			fatal_abort("%s: delete attempted against const",
				    __func__);
			break;
		case DATA_FOR_EACH_FAIL:
			count *= -1;
			/* fall through */
		case DATA_FOR_EACH_STOP:
			i = NULL;
			break;
		default:
			fatal_abort("%s: invalid cmd", __func__);
		}

		if (i)
			i = i->next;
	}

	return count;
}

extern int data_dict_for_each(data_t *d, DataDictForF f, void *arg)
{
	int count = 0;
	data_list_node_t *i;

	if (!d)
		return 0;

	_check_magic(d);

	if (data_get_type(d) != DATA_TYPE_DICT) {
		error("%s: for each attempted on non-dict %pD", __func__, d);
		return -1;
	}

	i = d->data.dict_u->begin;
	_check_data_list_magic(d->data.dict_u);
	while (i) {
		_check_data_list_node_magic(i);

		data_for_each_cmd_t cmd = f(i->key, i->data, arg);
		count++;

		xassert(cmd > DATA_FOR_EACH_INVALID);
		xassert(cmd < DATA_FOR_EACH_MAX);

		switch (cmd) {
		case DATA_FOR_EACH_CONT:
			if (i)
				i = i->next;
			break;
		case DATA_FOR_EACH_DELETE:
		{
			data_list_node_t *idel = i;
			i = i->next;
			_release_data_list_node(d->data.list_u, idel);
			_check_data_list_magic(d->data.list_u);
			break;
		}
		case DATA_FOR_EACH_FAIL:
			count *= -1;
			/* fall through */
		case DATA_FOR_EACH_STOP:
			i = NULL;
			break;
		default:
			fatal_abort("%s: invalid cmd", __func__);
		}
	}

	return count;
}

static int _convert_data_string(data_t *data)
{
	_check_magic(data);

	switch (data->type) {
	case TYPE_STRING_INLINE:
	case TYPE_STRING_PTR:
		return SLURM_SUCCESS;
	case TYPE_BOOL:
		data_set_string(data, (data->data.bool_u ? "true" : "false"));
		return SLURM_SUCCESS;
	case TYPE_NULL:
		data_set_string(data, "");
		return SLURM_SUCCESS;
	case TYPE_FLOAT:
	{
		char *str = xstrdup_printf("%lf", data->data.float_u);
		data_set_string(data, str);
		xfree(str);
		return SLURM_SUCCESS;
	}
	case TYPE_INT_64:
	{
		char *str = xstrdup_printf("%"PRId64, data->data.int_u);
		data_set_string(data, str);
		xfree(str);
		return SLURM_SUCCESS;
	}
	default:
		return ESLURM_DATA_CONV_FAILED;
	}

	return ESLURM_DATA_CONV_FAILED;
}

static int _convert_data_force_bool(data_t *data)
{
	_check_magic(data);

	/* attempt to detect the type first */
	(void) data_convert_type(data, DATA_TYPE_NONE);

	switch (data->type) {
	case TYPE_STRING_INLINE:
	case TYPE_STRING_PTR:
		/* non-empty string but not recognized format */
		data_set_bool(data, true);
		return SLURM_SUCCESS;
	case TYPE_BOOL:
		return SLURM_SUCCESS;
	case TYPE_NULL:
		data_set_bool(data, false);
		return SLURM_SUCCESS;
	case TYPE_FLOAT:
		data_set_bool(data, data->data.float_u != 0);
		return SLURM_SUCCESS;
	case TYPE_INT_64:
		data_set_bool(data, data->data.int_u != 0);
		return SLURM_SUCCESS;
	default:
		return ESLURM_DATA_CONV_FAILED;
	}

	return ESLURM_DATA_CONV_FAILED;
}

static int _convert_data_null(data_t *data)
{
	_check_magic(data);

	switch (data->type) {
	case TYPE_STRING_INLINE:
	case TYPE_STRING_PTR:
	{
		const char *str = data_get_string(data);

		if (!str[0])
			goto convert;

		if (str[0] == '~')
			goto convert;

		if (!xstrcasecmp(str, "null"))
			goto convert;

		goto fail;
	}
	case TYPE_NULL:
		return SLURM_SUCCESS;
	default:
		return ESLURM_DATA_CONV_FAILED;
	}
fail:
	return ESLURM_DATA_CONV_FAILED;
convert:
	log_flag_hex(DATA, data_get_string(data), strlen(data_get_string(data)),
		     "%s: converted %pD->null", __func__, data);
	data_set_null(data);
	return SLURM_SUCCESS;
}

static int _convert_data_bool(data_t *data)
{
	const char *str = NULL;

	_check_magic(data);

	switch (data->type) {
	case TYPE_STRING_INLINE:
	case TYPE_STRING_PTR:
	{
		str = data_get_string(data);

		if (tolower(str[0]) == 'y') {
			if (!str[1] || ((tolower(str[1]) == 'e') &&
					(tolower(str[2]) == 's') &&
					(str[3] == '\0'))) {
				data_set_bool(data, true);
				goto converted;
			}
			goto fail;
		} else if (tolower(str[0]) == 't') {
			if (!str[1] || ((tolower(str[1]) == 'r') &&
					(tolower(str[2]) == 'u') &&
					(tolower(str[3]) == 'e') &&
					(str[4] == '\0'))) {
				data_set_bool(data, true);
				goto converted;
			}
			goto fail;
		} else if ((tolower(str[0]) == 'o') &&
			   (tolower(str[1]) == 'n') &&
			   (str[2] == '\0')) {
			data_set_bool(data, true);
			goto converted;
		} else if (tolower(str[0]) == 'n') {
			if (!str[1] || ((tolower(str[1]) == 'o') &&
					(str[2] == '\0'))) {
				data_set_bool(data, false);
				goto converted;
			}
			goto fail;
		} else if (tolower(str[0]) == 'f') {
			if (!str[1] || ((tolower(str[1]) == 'a') &&
					(tolower(str[2]) == 'l') &&
					(tolower(str[3]) == 's') &&
					(tolower(str[4]) == 'e') &&
					(str[5] == '\0'))) {
				data_set_bool(data, false);
				goto converted;
			}
			goto fail;
		} else if ((tolower(str[0]) == 'o') &&
			   (tolower(str[1]) == 'f') &&
			   (tolower(str[2]) == 'f') &&
			   (str[3] == '\0')) {
			data_set_bool(data, false);
			goto converted;
		}

		goto fail;
	}
	case TYPE_BOOL:
		return SLURM_SUCCESS;
	default:
		goto fail;
	}

converted:
	log_flag_hex(DATA, str, strlen(str),
		     "%s: converted %pD->%s",
		 __func__, data, (data_get_bool(data) ? "true" : "false"));
	return SLURM_SUCCESS;

fail:
	if (str)
		log_flag_hex(DATA, str, strlen(str),
			     "%s: converting %pD to bool failed",
			     __func__, data);
	else
		log_flag(DATA, "%s: converting %pD to bool failed",
			 __func__, data);
	return ESLURM_DATA_CONV_FAILED;
}

static int _convert_data_int(data_t *data, bool force)
{
	_check_magic(data);

	switch (data->type) {
	case TYPE_STRING_INLINE:
	case TYPE_STRING_PTR:
	{
		int64_t x;
		char end;
		const char *str = data_get_string(data);

		if (!str[0]) {
			log_flag_hex(DATA, str, strlen(str),
				     "%s: convert empty string %pD to integer failed",
				     __func__, data);
			return ESLURM_DATA_CONV_FAILED;
		}

		if ((str[0] == '0') && (tolower(str[1]) == 'x')) {
			if (sscanf(str, "%"SCNx64"%c", &x, &end) == 1) {
				log_flag_hex(DATA, str, strlen(str),
					     "%s: converted hex number %pD->%"PRId64,
					 __func__, data, x);
				data_set_int(data, x);
				return SLURM_SUCCESS;
			}

			log_flag_hex(DATA, str, strlen(str),
				     "%s: conversion of hex string %pD to integer failed",
				     __func__, data);
			return ESLURM_DATA_CONV_FAILED;
		}

		if (!force) {
			for (const char *p = str; *p; p++) {
				if ((*p < '0') || (*p > '9')) {
					log_flag_hex(DATA, str, strlen(str),
						     "%s: rejecting non-numeric conversion of %pD to integer failed",
						     __func__, data);
					return ESLURM_DATA_CONV_FAILED;
				}
			}
		}

		if (sscanf(str, "%"SCNd64"%c", &x, &end) == 1) {
			log_flag_hex(DATA, str, strlen(str),
				     "%s: converted %pD->%"PRId64,
				     __func__, data, x);
			data_set_int(data, x);
			return SLURM_SUCCESS;
		} else {
			log_flag_hex(DATA, str, strlen(str),
				     "%s: conversion of %pD to integer failed",
				     __func__, data);
			return ESLURM_DATA_CONV_FAILED;
		}
	}
	case TYPE_FLOAT:
		if (force) {
			data_set_int(data, lrint(data_get_float(data)));
			return SLURM_SUCCESS;
		}
		return ESLURM_DATA_CONV_FAILED;
	case TYPE_INT_64:
		return SLURM_SUCCESS;
	case TYPE_NULL:
		if (force) {
			/*
			 * Conversion from NULL to integer is a loss of
			 * information as NULL implies value is not set where as
			 * integer 0 could just mean there is a a value zero as
			 * opposed to there be no value set. This conversion is
			 * only done when force is true as such.
			 */
			data_set_int(data, 0);
			return SLURM_SUCCESS;
		}
	default:
		return ESLURM_DATA_CONV_FAILED;
	}
}

static int _convert_data_float_from_string(data_t *data)
{
	const char *str = data_get_string(data);
	int i = 0;
	bool negative = false;

	xassert(str);

	if (str[i] == '+') {
		i++;
	} else if (str[i] == '-') {
		i++;
		negative = true;
	}

	if ((tolower(str[i]) == 'i')) {
		i++;

		if (!xstrcasecmp(&str[i], "nf") ||
		    !xstrcasecmp(&str[i], "nfinity")) {
			if (negative)
				data_set_float(data, -INFINITY);
			else
				data_set_float(data, INFINITY);

			goto converted;
		}

		goto fail;
	}

	if ((tolower(str[i]) == 'n')) {
		i++;

		if (!xstrcasecmp(&str[i], "an")) {
			if (negative)
				data_set_float(data, -NAN);
			else
				data_set_float(data, NAN);

			goto converted;
		}

		goto fail;
	}

	if ((str[i] >= '0') && (str[i] <= '9')) {
		char end;
		double x;

		if (sscanf(&str[i], "%lf%c", &x, &end) == 1) {
			if (negative)
				x *= -1;
			data_set_float(data, x);
			goto converted;
		}
	}

	goto fail;

converted:
	log_flag(DATA, "%s: converted %pD to float: %s->%lf",
		 __func__, data, str, data_get_float(data));
	return SLURM_SUCCESS;

fail:
	log_flag_hex(DATA, str, strlen(str),
		     "%s: convert %pD to double float failed", __func__, data);
	return ESLURM_DATA_CONV_FAILED;
}

static int _convert_data_float(data_t *data)
{
	_check_magic(data);

	switch (data->type) {
	case TYPE_STRING_INLINE:
	case TYPE_STRING_PTR:
		return _convert_data_float_from_string(data);
	case TYPE_INT_64:
		if (data_get_int(data) == INFINITE64)
			data_set_float(data, HUGE_VAL);
		else if (data_get_int(data) == NO_VAL64)
			data_set_float(data, NAN);
		else /* attempt normal fp conversion */
			data_set_float(data, data_get_int(data));
		return SLURM_SUCCESS;
	case TYPE_FLOAT:
		return SLURM_SUCCESS;
	default:
		return ESLURM_DATA_CONV_FAILED;
	}

	return ESLURM_DATA_CONV_FAILED;
}

extern data_type_t data_convert_type(data_t *data, data_type_t match)
{
	_check_magic(data);

	if (!data)
		return DATA_TYPE_NONE;

	switch (match) {
	case DATA_TYPE_STRING:
		return _convert_data_string(data) ? DATA_TYPE_NONE :
						    DATA_TYPE_STRING;
	case DATA_TYPE_BOOL:
		return _convert_data_force_bool(data) ? DATA_TYPE_NONE :
							DATA_TYPE_BOOL;
	case DATA_TYPE_INT_64:
		return _convert_data_int(data, true) ? DATA_TYPE_NONE :
						       DATA_TYPE_INT_64;
	case DATA_TYPE_FLOAT:
		return _convert_data_float(data) ? DATA_TYPE_NONE :
						   DATA_TYPE_FLOAT;
	case DATA_TYPE_NULL:
		return _convert_data_null(data) ? DATA_TYPE_NONE :
						  DATA_TYPE_NULL;
	case DATA_TYPE_NONE:
		if (!_convert_data_null(data))
			return DATA_TYPE_NULL;

		if (!_convert_data_int(data, false))
			return DATA_TYPE_INT_64;

		if (!_convert_data_float(data))
			return DATA_TYPE_FLOAT;

		if (!_convert_data_int(data, true))
			return DATA_TYPE_INT_64;

		if (!_convert_data_bool(data))
			return DATA_TYPE_BOOL;

		return DATA_TYPE_NONE;
	case DATA_TYPE_DICT:
	case DATA_TYPE_LIST:
		/* data_parser should be used for this conversion instead. */
		return DATA_TYPE_NONE;
	case DATA_TYPE_MAX:
		break;
	}

	xassert(false);
	return DATA_TYPE_NONE;
}

static data_for_each_cmd_t _convert_list_entry(data_t *data, void *arg)
{
	convert_args_t *args = arg;

	args->count += _convert_tree(data, args->match);

	return DATA_FOR_EACH_CONT;
}

static data_for_each_cmd_t _convert_dict_entry(const char *key, data_t *data,
					       void *arg)
{
	convert_args_t *args = arg;

	args->count += _convert_tree(data, args->match);

	return DATA_FOR_EACH_CONT;
}

static size_t _convert_tree(data_t *data, const type_t match)
{
	convert_args_t args = {
		.match = match,
	};
	_check_magic(data);

	if (!data)
		return 0;

	switch (data->type) {
	case TYPE_DICT:
		(void)data_dict_for_each(data, _convert_dict_entry, &args);
		break;
	case TYPE_LIST:
		(void)data_list_for_each(data, _convert_list_entry, &args);
		break;
	default:
		if (match == (int) data_convert_type(data, (int) match))
			args.count++;
		break;
	}

	return args.count;
}

extern size_t data_convert_tree(data_t *data, const data_type_t match)
{
	return _convert_tree(data, (int) match);
}

static data_for_each_cmd_t _find_dict_match(const char *key, const data_t *a,
					    void *arg)
{
	bool rc;
	find_dict_match_t *p = arg;
	const data_t *b = data_key_get_const(p->b, key);

	rc = data_check_match(a, b, p->mask);

	log_flag(DATA, "dictionary compare: %s(0x%"PRIXPTR")=%s(0x%"PRIXPTR") %s %s(0x%"PRIXPTR")=%s(0x%"PRIXPTR")",
		 key, (uintptr_t) p->a, _type_to_string(a->type), (uintptr_t) a,
		 (rc ? "\u2261" : "\u2260"), key, (uintptr_t) p->b,
		 (b ? _type_to_string(b->type) : _type_to_string(TYPE_NONE)),
		 (uintptr_t) b);

	return rc ? DATA_FOR_EACH_CONT : DATA_FOR_EACH_FAIL;
}

static bool _data_match_dict(const data_t *a, const data_t *b, bool mask)
{
	find_dict_match_t p = {
		.mask = mask,
		.a = a,
		.b = b,
	};

	if (!a || (a->type != TYPE_DICT))
		return false;

	if (!b || (b->type != TYPE_DICT))
		return false;

	_check_magic(a);
	_check_magic(b);

	if (a->data.dict_u->count != b->data.dict_u->count)
		return false;

	/* match by key and not order with dictionary */
	return (data_dict_for_each_const(a, _find_dict_match, &p) >= 0);
}

static bool _data_match_lists(const data_t *a, const data_t *b, bool mask)
{
	bool fail = false;
	const data_list_node_t *ptr_a;
	const data_list_node_t *ptr_b;

	if (!a || (a->type != TYPE_LIST))
		return false;
	if (!b || (b->type != TYPE_LIST))
		return false;

	_check_magic(a);
	_check_magic(b);

	if (a->data.list_u->count != b->data.list_u->count)
		return false;

	ptr_a = a->data.list_u->begin;
	ptr_b = b->data.list_u->begin;

	while (ptr_a && !fail) {
		_check_data_list_node_magic(ptr_a);

		if (!ptr_b && mask)
			/* ignore a if b is NULL when masking */
			continue;

		_check_data_list_node_magic(ptr_b);
		if (data_check_match(ptr_a->data, ptr_b->data, mask)) {
			ptr_a = ptr_a->next;
			ptr_b = ptr_b->next;
		} else
			fail = true;
	}

	return !fail;
}

extern bool data_check_match(const data_t *a, const data_t *b, bool mask)
{
	bool rc;

	if (a == NULL && b == NULL)
		return true;

	if (a == NULL || b == NULL)
		return false;

	_check_magic(a);
	_check_magic(b);

	if (a->type != b->type) {
		log_flag(DATA, "type mismatch: %s(0x%"PRIXPTR") != %s(0x%"PRIXPTR")",
			 _type_to_string(a->type), (uintptr_t) a,
			 _type_to_string(b->type), (uintptr_t) b);
		return false;
	}

	switch (a->type) {
	case TYPE_NULL:
		rc = (b->type == TYPE_NULL);
		log_flag(DATA, "compare: %s(0x%"PRIXPTR") %s %s(0x%"PRIXPTR")",
			 _type_to_string(a->type), (uintptr_t) a,
			 (rc ? "=" : "!="),
			 _type_to_string(b->type), (uintptr_t) b);
		return rc;
	case TYPE_STRING_INLINE:
	case TYPE_STRING_PTR:
		rc = !xstrcmp(data_get_string_const(a),
			      data_get_string_const(b));
		log_flag(DATA, "compare: %s(0x%"PRIXPTR")=%s %s %s(0x%"PRIXPTR")=%s",
			 _type_to_string(a->type), (uintptr_t) a,
			 data_get_string_const(a), (rc ? "=" : "!="),
			 _type_to_string(b->type), (uintptr_t) b,
			 data_get_string_const(b));
		return rc;
	case TYPE_BOOL:
		rc = (data_get_bool(a) == data_get_bool(b));
		log_flag(DATA, "compare: %s(0x%"PRIXPTR")=%s %s %s(0x%"PRIXPTR")=%s",
			 _type_to_string(a->type), (uintptr_t) a,
			 (data_get_bool(a) ? "True" : "False"),
			 (rc ? "=" : "!="),
			 _type_to_string(b->type), (uintptr_t) b,
			 (data_get_bool(b) ? "True" : "False"));
		return rc;
	case TYPE_INT_64:
		rc = data_get_int(a) == data_get_int(b);
		log_flag(DATA, "compare: %s(0x%"PRIXPTR")=%"PRId64" %s %s(0x%"PRIXPTR")=%"PRId64,
			 _type_to_string(a->type), (uintptr_t) a,
			 data_get_int(a), (rc ? "=" : "!="),
			 _type_to_string(b->type), (uintptr_t) b,
			 data_get_int(b));
		return rc;
	case TYPE_FLOAT:
		if (!(rc = (data_get_float(a) == data_get_float(b))) ||
		    !(rc = fuzzy_equal(data_get_float(a), data_get_float(b)))) {
			if (isnan(data_get_float(a)) ==
			    isnan(data_get_float(a)))
				rc = true;
			else if (signbit(data_get_float(a)) !=
				 signbit(data_get_float(b)))
				rc = false;
			else if (isinf(data_get_float(a)) !=
				 isinf(data_get_float(b)))
				rc = false;
			else
				rc = false;
		}

		log_flag(DATA, "compare: %s(0x%"PRIXPTR")=%e %s %s(0x%"PRIXPTR")=%e",
			 _type_to_string(a->type), (uintptr_t) a,
			 data_get_float(a), (rc ? "=" : "!="),
			 _type_to_string(b->type), (uintptr_t) b,
			 data_get_float(b));
		return rc;
	case TYPE_DICT:
		rc = _data_match_dict(a, b, mask);
		log_flag(DATA, "compare dictionary: %s(0x%"PRIXPTR")[%zd] %s %s(0x%"PRIXPTR")[%zd]",
			 _type_to_string(a->type), (uintptr_t) a,
			 data_get_dict_length(a), (rc ? "=" : "!="),
			 _type_to_string(b->type), (uintptr_t) b,
			 data_get_dict_length(b));
		return rc;
	case TYPE_LIST:
		rc = _data_match_lists(a, b, mask);
		log_flag(DATA, "compare list: %s(0x%"PRIXPTR")[%zd] %s %s(0x%"PRIXPTR")[%zd]",
			 _type_to_string(a->type), (uintptr_t) a,
			 data_get_list_length(a), (rc ? "=" : "!="),
			 _type_to_string(b->type), (uintptr_t) b,
			 data_get_list_length(b));
		return rc;
	case TYPE_NONE:
		/* fall through */
	case TYPE_START:
		/* fall through */
	case TYPE_MAX:
		fatal_abort("%s: unexpected data type", __func__);
	}

	fatal_abort("%s: should never run", __func__);
}

extern data_t *data_resolve_dict_path(data_t *data, const char *path)
{
	data_t *found = data;
	char *save_ptr = NULL;
	char *token = NULL;
	char *str;
	char local[DATA_DEFINE_DICT_PATH_BUFFER_SIZE];
	size_t len = strlen(path);

	_check_magic(data);

	if (!data)
		return NULL;

	if (len < sizeof(local))
		str = memcpy(local, path, (len + 1));
	else
		str = xstrdup(path);

	token = strtok_r(str, "/", &save_ptr);
	while (token && found) {
		/* walk forward any whitespace */
		while (*token && isspace(*token))
			token++;

		/* zero any ending whitespace */
		for (int i = strlen(token) - 1; i >= 0; i--) {
			if (isspace(token[i]))
				token[i] = '\0';
			else
				break;
		}

		if (!found || (found->type != TYPE_DICT)) {
			found = NULL;
			break;
		}

		if (!(found = data_key_get(found, token)))
			break;

		token = strtok_r(NULL, "/", &save_ptr);
	}

	if (str != local)
		xfree(str);

	if (found)
		log_flag_hex(DATA, path, strlen(path),
			     "%s: %pD resolved dictionary path to %pD",
			     __func__, data, found);
	else
		log_flag_hex(DATA, path, strlen(path),
			     "%s: %pD failed to resolve dictionary path",
			     __func__, data);
	return found;
}

extern const data_t *data_resolve_dict_path_const(const data_t *data,
						  const char *path)
{
	const data_t *found = data;
	char *save_ptr = NULL;
	char *token = NULL;
	char *str;

	_check_magic(data);

	if (!data)
		return NULL;

	str = xstrdup(path);

	token = strtok_r(str, "/", &save_ptr);
	while (token && found) {
		xstrtrim(token);

		if (!found || (found->type != TYPE_DICT)) {
			found = false;
			break;
		}

		if (!(found = data_key_get_const(found, token)))
			break;

		token = strtok_r(NULL, "/", &save_ptr);
	}
	xfree(str);

	if (found)
		log_flag_hex(DATA, path, strlen(path),
			     "%s: data %pD resolved dictionary path to %pD",
			     __func__, data, found);
	else
		log_flag_hex(DATA, path, strlen(path),
			     "%s: data %pD failed to resolve dictionary path",
			     __func__, data);

	return found;
}

extern data_t *data_define_dict_path(data_t *data, const char *path)
{
	data_t *found = data;
	char *save_ptr = NULL;
	char *token = NULL;
	char *str;

	_check_magic(data);

	if (!data)
		return NULL;

	str = xstrdup(path);

	token = strtok_r(str, "/", &save_ptr);
	while (token && found) {
		xstrtrim(token);

		if (found->type == TYPE_NULL)
			data_set_dict(found);
		else if (found->type != TYPE_DICT) {
			found = NULL;
			break;
		}

		if (!(found = data_key_set(found, token)))
			break;

		token = strtok_r(NULL, "/", &save_ptr);
	}
	xfree(str);

	if (found)
		log_flag_hex(DATA, path, strlen(path),
			     "%s: %pD defined dictionary path to %pD",
			     __func__, data, found);
	else
		log_flag_hex(DATA, path, strlen(path),
			     "%s: %pD failed to define dictionary path",
			     __func__, data);

	return found;
}

extern data_t *data_copy(data_t *dest, const data_t *src)
{
	if (!src)
		return NULL;

	if (!dest)
		dest = data_new();

	_check_magic(src);
	_check_magic(dest);

	log_flag(DATA, "%s: copy data %pD to %pD", __func__, src, dest);

	switch (src->type) {
	case TYPE_STRING_INLINE:
	case TYPE_STRING_PTR:
		return data_set_string(dest, data_get_string_const(src));
	case TYPE_BOOL:
		return data_set_bool(dest, data_get_bool(src));
	case TYPE_INT_64:
		return data_set_int(dest, data_get_int(src));
	case TYPE_FLOAT:
		return data_set_float(dest, data_get_float(src));
	case TYPE_NULL:
		return data_set_null(dest);
	case TYPE_LIST:
	{
		data_list_node_t *i = src->data.list_u->begin;

		data_set_list(dest);

		while (i) {
			_check_data_list_node_magic(i);
			xassert(!i->key);
			data_copy(data_list_append(dest), i->data);
			i = i->next;
		}

		return dest;
	}
	case TYPE_DICT:
	{
		data_list_node_t *i = src->data.dict_u->begin;

		data_set_dict(dest);

		while (i) {
			_check_data_list_node_magic(i);
			data_copy(data_key_set(dest, i->key), i->data);
			i = i->next;
		}

		return dest;
	}
	default:
		fatal_abort("%s: unexpected data type", __func__);
		return NULL;
	}
}

extern data_t *data_move(data_t *dest, data_t *src)
{
	if (!src)
		return NULL;

	if (!dest)
		dest = data_new();

	_check_magic(src);
	_check_magic(dest);

	log_flag(DATA, "%s: move data %pD to %pD", __func__, src, dest);

	memmove(&dest->data, &src->data, sizeof(src->data));
	dest->type = src->type;
	src->type = TYPE_NULL;
	xassert((memset(&src->data, 0, sizeof(src->data))));

	return dest;
}

extern int data_retrieve_dict_path_string(const data_t *data, const char *path,
					  char **ptr_buffer)
{
	const data_t *d = NULL;
	int rc;

	_check_magic(data);
	if (!(d = data_resolve_dict_path_const(data, path)))
		return ESLURM_DATA_PATH_NOT_FOUND;

	rc = data_get_string_converted(d, ptr_buffer);

	if (rc)
		log_flag(DATA, "%s: data %pD failed to resolve string at path:%s",
			 __func__, data, path);
	else
		log_flag_hex(DATA, *ptr_buffer, strlen(*ptr_buffer),
			 "%s: data %pD resolved string at path:%s",
			 __func__, data, path);

	return rc;
}

extern int data_retrieve_dict_path_bool(const data_t *data, const char *path,
					bool *ptr_buffer)
{
	const data_t *d = NULL;
	int rc;

	_check_magic(data);
	if (!(d = data_resolve_dict_path_const(data, path)))
		return ESLURM_DATA_PATH_NOT_FOUND;

	rc = data_copy_bool_converted(d, ptr_buffer);

	log_flag(DATA, "%s: data %pD resolved string at path %s=%s: %s",
		 __func__, data, path,
		 (*ptr_buffer ? "true" : "false"), slurm_strerror(rc));

	return rc;
}

extern int data_retrieve_dict_path_int(const data_t *data, const char *path,
				       int64_t *ptr_buffer)
{
	const data_t *d = NULL;
	int rc;

	_check_magic(data);
	if (!(d = data_resolve_dict_path_const(data, path)))
		return ESLURM_DATA_PATH_NOT_FOUND;

	rc = data_get_int_converted(d, ptr_buffer);

	log_flag(DATA, "%s: data %pD resolved string at path %s to %"PRId64": %s",
		 __func__, data, path, *ptr_buffer, slurm_strerror(rc));

	return rc;
}

static char *_type_to_string(type_t type)
{
	return data_type_to_string((int) type);
}

extern char *data_type_to_string(data_type_t type)
{
	switch(type) {
		case DATA_TYPE_NULL:
			return "null";
		case DATA_TYPE_LIST:
			return "list";
		case DATA_TYPE_DICT:
			return "dictionary";
		case DATA_TYPE_INT_64:
			return "64 bit integer";
		case DATA_TYPE_STRING:
			return "string";
		case DATA_TYPE_FLOAT:
			return "floating point number";
		case DATA_TYPE_BOOL:
			return "boolean";
		case DATA_TYPE_NONE:
			/* fall through */
		case DATA_TYPE_MAX:
			return "INVALID";
	}

	for (int i = 0; i < ARRAY_SIZE(type_map); i++) {
		if (type_map[i].internal_type == (int) type)
			return data_type_to_string(type_map[i].external_type);
	}

	return "INVALID";
}

extern const char *data_get_type_string(const data_t *data)
{
	if (!data)
		return "INVALID";

	for (int i = 0; i < ARRAY_SIZE(type_map); i++)
		if (type_map[i].internal_type == data->type)
			return data_type_to_string(type_map[i].external_type);

	xassert(false);
	return "INVALID";
}
