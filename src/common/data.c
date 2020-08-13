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

#include "config.h"

#include <math.h>
#include <regex.h>

#include "slurm/slurm.h"

#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/read_config.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/common/data.h"

/*
 * Regex matches based on YAML 1.1 section 5.5.
 * Honors ~ as YAML 1.1 allows for null fields.
 */
static const char *bool_pattern_null = "^(\\~|[Nn][uU][lL][lL])$";
static regex_t bool_pattern_null_re;
static const char *bool_pattern_true = "^([Yy](|[eE][sS])|[tT]([rR][uU][eE]|)|[Oo][nN])$";
static regex_t bool_pattern_true_re;
static const char *bool_pattern_false = "^([nN]([Oo]|)|[fF](|[aA][lL][sS][eE])|[oO][fF][fF])$";
static regex_t bool_pattern_false_re;
static const char *bool_pattern_int = "^([+-]?[0-9]+)$";
static regex_t bool_pattern_int_re;
static const char *bool_pattern_float = "^([+-]?[0-9]*[.][0-9]*(|[eE][+-]?[0-9]+))$";
static regex_t bool_pattern_float_re;

static pthread_mutex_t init_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool initialized = false; /* protected by init_mutex */

#define DATA_MAGIC 0x1992189F
#define DATA_LIST_MAGIC 0x1992F89F
#define DATA_LIST_NODE_MAGIC 0x1921F89F

typedef struct data_list_node_s data_list_node_t;
struct data_list_node_s {
	int magic;
	data_list_node_t *next;

	data_t *data;
	char *key; /* key for dictionary (only) */
};

/* single forward linked list */
struct data_list_s {
	int magic;
	size_t count;

	data_list_node_t *begin;
	data_list_node_t *end;
};

static void _check_magic(const data_t *data);
static void _release(data_t *data);
static void _release_data_list_node(data_list_t *dl, data_list_node_t *dn);

static int _dump_regex_error(int rc, const regex_t *preg)
{
	char *buffer = NULL;
	size_t len = regerror(rc, preg, NULL, 0);

	if (len == 0) {
		error("%s: unknown regex error code: %d", __func__, rc);
		return SLURM_ERROR;
	}

	buffer = xmalloc(len);
	len = regerror(rc, preg, buffer, len);

	if (len)
		error("%s: regex error: %s", __func__, buffer);
	else
		error("%s: unexpected failure to get regex error", __func__);

	xfree(buffer);
	return SLURM_ERROR;
}

static bool _regex_quick_match(const char *str, const regex_t *preg)
{
	int rc;

	// TODO: nmatch may safely be able to be 0 for this function
	size_t nmatch = 1;
	regmatch_t pmatch[nmatch];

	/* not possible to match a NULL string */
	if (!str)
		return false;

	rc = regexec(preg, str, nmatch, pmatch, 0);
	if (!rc) { /* matched */
		return true;
	} else if (rc == REG_NOMATCH) {
		return false;
	} else { /* other error */
		_dump_regex_error(rc, preg);
		return false;
	}
}

extern void data_destroy_static(void)
{
	slurm_mutex_lock(&init_mutex);

	if (initialized) {
		regfree(&bool_pattern_null_re);
		regfree(&bool_pattern_true_re);
		regfree(&bool_pattern_false_re);
		regfree(&bool_pattern_int_re);
		regfree(&bool_pattern_float_re);
	}

	slurm_mutex_unlock(&init_mutex);
}

extern int data_init_static(void)
{
	int rc = SLURM_SUCCESS;
	int reg_rc; /* regex rc */

	slurm_mutex_lock(&init_mutex);

	if (initialized)
		goto cleanup;
	initialized = true;

	if (!rc && (reg_rc = regcomp(&bool_pattern_null_re, bool_pattern_null,
			      REG_EXTENDED)) != 0) {
		rc = _dump_regex_error(reg_rc, &bool_pattern_null_re);
	}

	if (!rc && (reg_rc = regcomp(&bool_pattern_true_re, bool_pattern_true,
			      REG_EXTENDED)) != 0) {
		rc = _dump_regex_error(reg_rc, &bool_pattern_true_re);
	}

	if (!rc && (reg_rc = regcomp(&bool_pattern_false_re, bool_pattern_false,
			      REG_EXTENDED)) != 0) {
		rc = _dump_regex_error(reg_rc, &bool_pattern_false_re);
	}

	if (!rc && (reg_rc = regcomp(&bool_pattern_int_re, bool_pattern_int,
			      REG_EXTENDED)) != 0) {
		rc = _dump_regex_error(reg_rc, &bool_pattern_int_re);
	}

	if (!rc && (reg_rc = regcomp(&bool_pattern_float_re, bool_pattern_float,
			      REG_EXTENDED)) != 0) {
		rc = _dump_regex_error(reg_rc, &bool_pattern_float_re);
	}

cleanup:
	slurm_mutex_unlock(&init_mutex);

	return rc;
}

static data_list_t *_data_list_new(void)
{
	data_list_t *dl = xmalloc(sizeof(*dl));
	dl->magic = DATA_LIST_MAGIC;

	log_flag(DATA, "%s: new data list (0x%"PRIXPTR")",
		 __func__, (uintptr_t) dl);

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

	/* walk list to find new previous */
	for (prev = dl->begin; prev && prev->next != dn; ) {
		_check_data_list_node_magic(prev);
		prev = prev->next;
		if (prev)
			_check_data_list_node_magic(prev);
	}

	if (dn == dl->begin) {
		/* at the beginning */
		dl->begin = dn->next;

		if (dl->end == dn) {
			dl->end = NULL;
			xassert(!dn->next);
		}
	} else if (dn == dl->end) {
		/* at the end */
		xassert(!dn->next);
		dl->end = prev;
		prev->next = NULL;
	} else {
		/* somewhere in middle */
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
		xassert(!dl->end);
		return;
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
	if (key)
		dn->key = xstrdup(key);

	log_flag(DATA, "%s: new data list node (0x%"PRIXPTR")",
		 __func__, (uintptr_t) dn);

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
}

data_t *data_new(void)
{
	data_t *data = xmalloc(sizeof(*data));
	data->magic = DATA_MAGIC;
	data->type = DATA_TYPE_NULL;

	log_flag(DATA, "%s: new data (0x%"PRIXPTR")",
		 __func__, (uintptr_t) data);

	return data;
}

static void _check_magic(const data_t *data)
{
	xassert(data);
	xassert(data->type > DATA_TYPE_NONE);
	xassert(data->type < DATA_TYPE_MAX);
	xassert(data->magic == DATA_MAGIC);

	if (data->type == DATA_TYPE_NULL)
		/* make sure NULL type has a NULL value */
		xassert(data->data.list_u == NULL);
	if (data->type == DATA_TYPE_LIST)
		_check_data_list_magic(data->data.list_u);
	if (data->type == DATA_TYPE_DICT)
		_check_data_list_magic(data->data.dict_u);
}

static void _release(data_t *data)
{
	_check_magic(data);

	switch (data->type) {
	case DATA_TYPE_LIST:
		_release_data_list(data->data.list_u);
		break;
	case DATA_TYPE_DICT:
		_release_data_list(data->data.dict_u);
		break;
	case DATA_TYPE_STRING:
		xfree(data->data.string_u);
		break;
	default:
		/* other types don't need to be freed */
		break;
	}

	data->type = DATA_TYPE_NONE;
}

extern void data_free(data_t *data)
{
	if (!data)
		return;

	log_flag(DATA, "%s: free data (0x%"PRIXPTR")",
		 __func__, (uintptr_t) data);

	_check_magic(data);
	_release(data);

	data->magic = ~DATA_MAGIC;
	xfree(data);
}

extern data_type_t data_get_type(const data_t *data)
{
	_check_magic(data);

	if (data)
		return data->type;
	else
		return DATA_TYPE_NONE;
}

extern data_t *data_set_float(data_t *data, double value)
{
	_check_magic(data);
	if (!data)
		return NULL;

	log_flag(DATA, "%s: set data (0x%"PRIXPTR") to float: %lf",
	       __func__, (uintptr_t) data, value);

	data->type = DATA_TYPE_FLOAT;
	data->data.float_u = value;

	return data;
}

extern data_t *data_set_null(data_t *data)
{
	_check_magic(data);
	if (!data)
		return NULL;
	_release(data);

	log_flag(DATA, "%s: set data (0x%"PRIXPTR") to null",
	       __func__, (uintptr_t) data);

	data->type = DATA_TYPE_NULL;
	xassert((memset(&data->data, 0, sizeof(data->data))));

	return data;
}

extern data_t *data_set_bool(data_t *data, bool value)
{
	_check_magic(data);
	if (!data)
		return NULL;
	_release(data);

	log_flag(DATA, "%s: set data (0x%"PRIXPTR") to bool: %d",
	       __func__, (uintptr_t) data, value);

	data->type = DATA_TYPE_BOOL;
	data->data.bool_u = value;

	return data;
}

extern data_t *data_set_int(data_t *data, int64_t value)
{
	_check_magic(data);
	if (!data)
		return NULL;
	_release(data);

	log_flag(DATA, "%s: set data (0x%"PRIXPTR") to int64_t: %"PRId64,
	       __func__, (uintptr_t) data, value);

	data->type = DATA_TYPE_INT_64;
	data->data.int_u = value;

	return data;
}

extern data_t *data_set_string(data_t *data, const char *value)
{
	_check_magic(data);

	if (!data)
		return NULL;
	_release(data);

	log_flag(DATA, "%s: set data (0x%"PRIXPTR") to string: %s",
	       __func__, (uintptr_t) data, value);

	data->type = DATA_TYPE_STRING;
	data->data.string_u = xstrdup(value);

	return data;
}

extern data_t *data_set_dict(data_t *data)
{
	_check_magic(data);

	if (!data)
		return NULL;
	_release(data);

	log_flag(DATA, "%s: set data (0x%"PRIXPTR") to dictionary",
		 __func__, (uintptr_t) data);

	data->type = DATA_TYPE_DICT;
	data->data.dict_u = _data_list_new();

	return data;
}

extern data_t *data_set_list(data_t *data)
{
	_check_magic(data);

	if (!data)
		return NULL;
	_release(data);

	log_flag(DATA, "%s: set data (0x%"PRIXPTR") to list",
		 __func__, (uintptr_t) data);

	data->type = DATA_TYPE_LIST;
	data->data.dict_u = _data_list_new();

	return data;
}

extern data_t *data_list_append(data_t *data)
{
	data_t *ndata = NULL;
	_check_magic(data);

	xassert(data->type == DATA_TYPE_LIST);
	if (!data || data->type != DATA_TYPE_LIST)
		return NULL;

	ndata = data_new();
	_data_list_append(data->data.list_u, ndata, NULL);

	log_flag(DATA, "%s: list append data (0x%"PRIXPTR") to (0x%"PRIXPTR")",
		 __func__, (uintptr_t) ndata, (uintptr_t) data);

	return ndata;
}

extern data_t *data_list_prepend(data_t *data)
{
	data_t *ndata = NULL;
	_check_magic(data);

	xassert(data->type == DATA_TYPE_LIST);
	if (!data || data->type != DATA_TYPE_LIST)
		return NULL;

	ndata = data_new();
	_data_list_prepend(data->data.list_u, ndata, NULL);

	log_flag(DATA, "%s: list prepend data (0x%"PRIXPTR") to (0x%"PRIXPTR")",
		 __func__, (uintptr_t) ndata, (uintptr_t) data);

	return ndata;
}

static data_for_each_cmd_t _data_list_join(const data_t *src, void *arg)
{
	data_t *dst = (data_t *) arg;
	_check_magic(src);
	_check_magic(dst);
	xassert(data_get_type(dst) == DATA_TYPE_LIST);

	log_flag(DATA, "%s: list join data (0x%"PRIXPTR") to (0x%"PRIXPTR")",
		 __func__, (uintptr_t) src, (uintptr_t) dst);

	 data_copy(data_list_append(dst), src);

	 return DATA_FOR_EACH_CONT;
}

extern data_t *data_list_join(const data_t **data, bool flatten_lists)
{
	data_t *dst = data_set_list(data_new());

	for (size_t i = 0; data[i]; i++) {
		if (flatten_lists && (data_get_type(data[i]) == DATA_TYPE_LIST))
			(void) data_list_for_each_const(data[i],
							_data_list_join, dst);
		else /* simple join */
			_data_list_join(data[i], dst);
	}

	return dst;
}

const data_t *data_key_get_const(const data_t *data, const char *key)
{
	const data_list_node_t *i;

	_check_magic(data);
	xassert(data->type == DATA_TYPE_DICT);
	if (!key || data->type != DATA_TYPE_DICT)
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

data_t *data_key_get(data_t *data, const char *key)
{
	data_list_node_t *i;

	_check_magic(data);
	xassert(data->type == DATA_TYPE_DICT);
	if (!key || data->type != DATA_TYPE_DICT)
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

data_t *data_key_set(data_t *data, const char *key)
{
	data_t *d;

	_check_magic(data);
	xassert(data->type == DATA_TYPE_DICT);
	xassert(key && key[0]);
	if (!key || !key[0] || data->type != DATA_TYPE_DICT)
		return NULL;

	if ((d = data_key_get(data, key))) {
		log_flag(DATA, "%s: set existing key in data (0x%"PRIXPTR") key: %s data (0x%"PRIXPTR")",
			 __func__, (uintptr_t) data, key, (uintptr_t) d);
		return d;
	}

	d = data_new();
	_data_list_append(data->data.dict_u, d, key);

	log_flag(DATA, "%s: set new key in data (0x%"PRIXPTR") key: %s data (0x%"PRIXPTR")",
		 __func__, (uintptr_t) data, key, (uintptr_t) d);

	return d;
}

data_t *data_key_set_int(data_t *data, int64_t key)
{
	char *key_str = xstrdup_printf("%"PRId64, key);
	data_t *node = data_key_set(data, key_str);

	xfree(key_str);

	return node;
}

bool data_key_unset(data_t *data, const char *key)
{
	data_list_node_t *i;

	_check_magic(data);
	xassert(data->type == DATA_TYPE_DICT);
	if (!key || data->type != DATA_TYPE_DICT)
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
		log_flag(DATA, "%s: remove non-existent key in data (0x%"PRIXPTR") key: %s",
			 __func__, (uintptr_t) data, key);
		return false;
	}

	_release_data_list_node(data->data.dict_u, i);

	log_flag(DATA, "%s: remove existing key in data (0x%"PRIXPTR") key: %s",
		 __func__, (uintptr_t) data, key);

	return true;
}

double data_get_float(const data_t *data)
{
	_check_magic(data);

	if (!data)
		return NAN;

	xassert(data->type == DATA_TYPE_FLOAT);
	return data->data.float_u;
}

bool data_get_bool(const data_t *data)
{
	_check_magic(data);

	if (!data)
		return false;

	xassert(data->type == DATA_TYPE_BOOL);
	return data->data.bool_u;
}

int64_t data_get_int(const data_t *data)
{
	_check_magic(data);

	if (!data)
		return 0;

	xassert(data->type == DATA_TYPE_INT_64);
	return data->data.int_u;
}

const char *data_get_string(const data_t *data)
{
	_check_magic(data);

	if (!data)
		return NULL;

	xassert(data->type == DATA_TYPE_STRING);
	return data->data.string_u;
}

int data_get_string_converted(const data_t *d, char **buffer)
{
	_check_magic(d);
	char *_buffer = NULL;

	if (!d || !buffer)
		return SLURM_ERROR;

	if (data_get_type(d) != DATA_TYPE_STRING) {
		/* copy the data and then convert it to a string type */
		data_t *dclone = data_new();
		data_copy(dclone, d);
		if (data_convert_type(dclone, DATA_TYPE_STRING) ==
		    DATA_TYPE_STRING)
			_buffer = xstrdup(data_get_string(dclone));
		FREE_NULL_DATA(dclone);
	} else
		_buffer = xstrdup(data_get_string(d));

	if (_buffer) {
		*buffer = _buffer;
		return SLURM_SUCCESS;
	}

	return SLURM_ERROR;
}

extern int data_copy_bool_converted(const data_t *d, bool *buffer)
{
	_check_magic(d);
	int rc = SLURM_ERROR;

	if (!d || !buffer)
		return rc;

	if (data_get_type(d) != DATA_TYPE_BOOL) {
		data_t *dclone = data_new();
		data_copy(dclone, d);
		if (data_convert_type(dclone, DATA_TYPE_BOOL) ==
		    DATA_TYPE_BOOL) {
			*buffer = data_get_bool(dclone);
			rc = SLURM_SUCCESS;
		}
		FREE_NULL_DATA(dclone);

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
		return SLURM_ERROR;

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
		return SLURM_ERROR;

	if (data_get_type(d) != DATA_TYPE_INT_64) {
		data_t *dclone = data_new();
		data_copy(dclone, d);
		if (data_convert_type(dclone, DATA_TYPE_INT_64) ==
		    DATA_TYPE_INT_64)
			*buffer = data_get_int(dclone);
		else
			rc = SLURM_ERROR;
		FREE_NULL_DATA(dclone);
	} else {
		*buffer = data_get_int(d);
	}

	return rc;
}

size_t data_get_dict_length(const data_t *data)
{
	_check_magic(data);

	if (!data)
		return 0;

	xassert(data->type == DATA_TYPE_DICT);
	return data->data.dict_u->count;
}

size_t data_get_list_length(const data_t *data)
{
	_check_magic(data);

	if (!data)
		return 0;

	xassert(data->type == DATA_TYPE_LIST);
	return data->data.list_u->count;
}

extern int data_list_for_each_const(const data_t *d, DataListForFConst f, void *arg)
{
	int count = 0;
	const data_list_node_t *i;

	_check_magic(d);

	if (!d || data_get_type(d) != DATA_TYPE_LIST) {
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

	if (!d || data_get_type(d) != DATA_TYPE_LIST) {
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
			_release_data_list_node(d->data.list_u, i);
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

extern int data_dict_for_each_const(const data_t *d, DataDictForFConst f, void *arg)
{
	int count = 0;
	const data_list_node_t *i;

	_check_magic(d);

	if (!d || data_get_type(d) != DATA_TYPE_DICT) {
		error("%s: for each attempted on non-dict object (0x%"PRIXPTR")",
		      __func__, (uintptr_t) d);
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

	_check_magic(d);

	if (!d || data_get_type(d) != DATA_TYPE_DICT) {
		error("%s: for each attempted on non-dict object (0x%"PRIXPTR")",
		      __func__, (uintptr_t) d);
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
			break;
		case DATA_FOR_EACH_DELETE:
			_release_data_list_node(d->data.dict_u, i);
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

static int _convert_data_string(data_t *data)
{
	_check_magic(data);

	switch (data->type) {
	case DATA_TYPE_STRING:
		return SLURM_SUCCESS;
	case DATA_TYPE_BOOL:
		data_set_string(data, (data->data.bool_u ? "true" : "false"));
		return SLURM_SUCCESS;
	case DATA_TYPE_NULL:
		data_set_string(data, "null");
		return SLURM_SUCCESS;
	case DATA_TYPE_FLOAT:
	{
		char *str = xstrdup_printf("%lf", data->data.float_u);
		data_set_string(data, str);
		xfree(str);
		return SLURM_SUCCESS;
	}
	case DATA_TYPE_INT_64:
	{
		char *str = xstrdup_printf("%"PRId64, data->data.int_u);
		data_set_string(data, str);
		xfree(str);
		return SLURM_SUCCESS;
	}
	default:
		return SLURM_ERROR;
	}

	return SLURM_ERROR;
}


static int _convert_data_force_bool(data_t *data)
{
	_check_magic(data);

	switch (data->type) {
	case DATA_TYPE_STRING:
		if (data->data.string_u == NULL ||
		    data->data.string_u[0] == '\0')
			data_set_bool(data, false);
		else if (_regex_quick_match(data->data.string_u,
					    &bool_pattern_true_re))
			data_set_bool(data, true);
		else { /* try to auto detect the type and try again */
			if (data_convert_type(data, DATA_TYPE_NONE)
			    != DATA_TYPE_NONE)
				return _convert_data_force_bool(data);
			else {
				/*
				 * not NULL or empty and unknown type,
				 * so it must be true
				 */
				data_set_bool(data, true);
			}
		}
		return SLURM_SUCCESS;
	case DATA_TYPE_BOOL:
		return SLURM_SUCCESS;
	case DATA_TYPE_NULL:
		data_set_bool(data, false);
		return SLURM_SUCCESS;
	case DATA_TYPE_FLOAT:
		data_set_bool(data, data->data.float_u != 0);
		return SLURM_SUCCESS;
	case DATA_TYPE_INT_64:
		data_set_bool(data, data->data.int_u != 0);
		return SLURM_SUCCESS;
	default:
		return SLURM_ERROR;
	}

	return SLURM_ERROR;
}

static int _convert_data_null(data_t *data)
{
	_check_magic(data);

	switch (data->type) {
	case DATA_TYPE_STRING:
		if (_regex_quick_match(data->data.string_u,
				       &bool_pattern_null_re)) {
			log_flag(DATA, "%s: convert data (0x%"PRIXPTR") to null: %s->null",
				 __func__, (uintptr_t) data,
				 data->data.string_u);
			data_set_null(data);
			return SLURM_SUCCESS;
		} else {
			return SLURM_ERROR;
		}
	case DATA_TYPE_NULL:
		return SLURM_SUCCESS;
	default:
		return SLURM_ERROR;
	}

	return SLURM_ERROR;
}

static int _convert_data_bool(data_t *data)
{
	_check_magic(data);

	switch (data->type) {
	case DATA_TYPE_STRING:
		if (_regex_quick_match(data->data.string_u,
				       &bool_pattern_true_re)) {
			log_flag(DATA, "%s: convert data (0x%"PRIXPTR") to bool: %s->true",
				 __func__, (uintptr_t) data,
				 data->data.string_u);
			data_set_bool(data, true);
			return SLURM_SUCCESS;
		} else if (_regex_quick_match(data->data.string_u,
					      &bool_pattern_false_re)) {
			log_flag(DATA, "%s: convert data (0x%"PRIXPTR") to bool: %s->false",
				 __func__, (uintptr_t) data,
				 data->data.string_u);
			data_set_bool(data, false);
			return SLURM_SUCCESS;
		} else {
			return SLURM_ERROR;
		}
	case DATA_TYPE_BOOL:
		return SLURM_SUCCESS;
	default:
		return SLURM_ERROR;
	}

	return SLURM_ERROR;
}

static int _convert_data_int(data_t *data)
{
	_check_magic(data);

	switch (data->type) {
	case DATA_TYPE_STRING:
		if (_regex_quick_match(data->data.string_u,
				       &bool_pattern_int_re)) {
			int64_t x;
			if (sscanf(data->data.string_u, "%"SCNd64, &x) == 1) {
				log_flag(DATA, "%s: converted data (0x%"PRIXPTR") to int: %s->%"PRId64,
					 __func__, (uintptr_t) data,
					 data->data.string_u, x);
				data_set_int(data, x);
				return SLURM_SUCCESS;
			} else { /* failed */
				debug2("%s: sscanf of int failed: %s", __func__,
				       data->data.string_u);
				return SLURM_ERROR;
			}
		} else {
			return SLURM_ERROR;
		}
	case DATA_TYPE_INT_64:
		return SLURM_SUCCESS;
	default:
		return SLURM_ERROR;
	}

	return SLURM_ERROR;
}

static int _convert_data_float(data_t *data)
{
	_check_magic(data);

	switch (data->type) {
	case DATA_TYPE_STRING:
		if (_regex_quick_match(data->data.string_u,
				       &bool_pattern_float_re)) {
			double x;
			if (sscanf(data->data.string_u, "%lf", &x) == 1) {
				log_flag(DATA, "%s: convert data (0x%"PRIXPTR") to float: %s->%lf",
					 __func__, (uintptr_t) data,
					 data->data.string_u, x);
				data_set_float(data, x);
				return SLURM_SUCCESS;
			} else { /* failed */
				error("%s: sscanf of double failed: %s",
				      __func__, data->data.string_u);
				return SLURM_ERROR;
			}
		} else {
			return SLURM_ERROR;
		}
	case DATA_TYPE_FLOAT:
		return SLURM_SUCCESS;
	default:
		return SLURM_ERROR;
	}

	return SLURM_ERROR;
}

extern data_type_t data_convert_type(data_t *data, data_type_t match)
{
	_check_magic(data);
	/*
	 * This currently only works on primitive types and doesn't
	 * apply to dictionaries or lists.
	 */
	xassert(data_get_type(data) != DATA_TYPE_DICT);
	xassert(data_get_type(data) != DATA_TYPE_LIST);

	switch (match) {
	case DATA_TYPE_STRING:
		return _convert_data_string(data) ? DATA_TYPE_NONE :
						    DATA_TYPE_STRING;
	case DATA_TYPE_BOOL:
		return _convert_data_force_bool(data) ? DATA_TYPE_NONE :
							DATA_TYPE_BOOL;
	case DATA_TYPE_INT_64:
		return _convert_data_int(data) ? DATA_TYPE_NONE :
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

		if (!_convert_data_bool(data))
			return DATA_TYPE_BOOL;

		if (!_convert_data_int(data))
			return DATA_TYPE_INT_64;

		if (!_convert_data_float(data))
			return DATA_TYPE_FLOAT;

	default:
		break;
	}

	return DATA_TYPE_NONE;
}

typedef struct {
	size_t count;
	data_type_t match;
} convert_args_t;

data_for_each_cmd_t _convert_list_entry(data_t *data, void *arg)
{
	convert_args_t *args = arg;

	args->count += data_convert_tree(data, args->match);

	return DATA_FOR_EACH_CONT;
}

data_for_each_cmd_t _convert_dict_entry(const char *key, data_t *data,
					void *arg)
{
	convert_args_t *args = arg;

	args->count += data_convert_tree(data, args->match);

	return DATA_FOR_EACH_CONT;
}

extern size_t data_convert_tree(data_t *data, const data_type_t match)
{
	convert_args_t args = { .match = match };
	_check_magic(data);

	if (!data)
		return 0;

	switch (data_get_type(data)) {
	case DATA_TYPE_DICT:
		data_dict_for_each(data, _convert_dict_entry, &args);
		break;
	case DATA_TYPE_LIST:
		data_list_for_each(data, _convert_list_entry, &args);
		break;
	default:
		if (match == data_convert_type(data, match))
			args.count++;
		break;
	}

	return args.count;
}

typedef struct {
	const data_t *b;
	bool mask;
} find_dict_match_t;

data_for_each_cmd_t _find_dict_match(const char *key, const data_t *a,
				     void *arg)
{
	find_dict_match_t *p = arg;
	const data_t *b = data_key_get_const(p->b, key);

	if (data_check_match(a, b, p->mask))
		return DATA_FOR_EACH_CONT;
	else
		return DATA_FOR_EACH_FAIL;
}

static bool _data_match_dict(const data_t *a, const data_t *b, bool mask)
{
	find_dict_match_t p = {
		.mask = mask,
		.b = b,
	};

	if (!a || data_get_type(a) != DATA_TYPE_DICT)
		return false;

	if (!b || data_get_type(b) != DATA_TYPE_DICT)
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

	if (!a || data_get_type(a) != DATA_TYPE_LIST)
		return false;
	if (!b || data_get_type(b) != DATA_TYPE_LIST)
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
	if (a == NULL && b == NULL)
		return true;

	if (a == NULL || b == NULL)
		return false;

	_check_magic(a);
	_check_magic(b);

	if (data_get_type(a) != data_get_type(b))
		return false;

	switch (data_get_type(a)) {
	case DATA_TYPE_NULL:
		return (data_get_type(b) == DATA_TYPE_NULL);
	case DATA_TYPE_STRING:
		// TODO: should we have a case insensitive compare?
		return !xstrcmp(data_get_string(a), data_get_string(b));
	case DATA_TYPE_BOOL:
		return (data_get_bool(a) == data_get_bool(b));
	case DATA_TYPE_INT_64:
		return data_get_int(a) == data_get_int(b);
	case DATA_TYPE_FLOAT:
		return fuzzy_equal(data_get_float(a), data_get_float(b));
	case DATA_TYPE_DICT:
		return _data_match_dict(a, b, mask);
	case DATA_TYPE_LIST:
		return _data_match_lists(a, b, mask);
	default:
		fatal_abort("%s: unexpected data type", __func__);
	}
}

extern const data_t *data_resolve_dict_path(const data_t *data, const char *path)
{
	const data_t *found = data;
	char *save_ptr = NULL;
	char *token = NULL;
	char *str = xstrdup(path);

	_check_magic(data);

	token = strtok_r(str, "/", &save_ptr);
	while (token && found) {
		xstrtrim(token);

		if (data_get_type(found) != DATA_TYPE_DICT)
			found = NULL;

		if (found) {
			found = data_key_get_const(found, token);
			token = strtok_r(NULL, "/", &save_ptr);
		}
	}
	xfree(str);

	if (found)
		log_flag(DATA, "%s: data (0x%"PRIXPTR") resolved dictionary path \"%s\" to (0x%"PRIXPTR")",
			 __func__, (uintptr_t) data, path, (uintptr_t) found);
	else
		log_flag(DATA, "%s: data (0x%"PRIXPTR") failed to resolve dictionary path \"%s\"",
			 __func__, (uintptr_t) data, path);

	return found;
}

extern data_t *data_define_dict_path(data_t *data, const char *path)
{
	data_t *found = data;
	char *save_ptr = NULL;
	char *token = NULL;
	char *str = xstrdup(path);

	_check_magic(data);

	token = strtok_r(str, "/", &save_ptr);
	while (token && found) {
		xstrtrim(token);

		if (data_get_type(found) == DATA_TYPE_NULL)
			data_set_dict(found);
		else if (data_get_type(found) != DATA_TYPE_DICT)
			found = NULL;

		if (found) {
			found = data_key_set(found, token);
			token = strtok_r(NULL, "/", &save_ptr);
		}
	}
	xfree(str);

	if (found)
		log_flag(DATA, "%s: data (0x%"PRIXPTR") defined dictionary path \"%s\" to (0x%"PRIXPTR")",
			 __func__, (uintptr_t) data, path, (uintptr_t) found);
	else
		log_flag(DATA, "%s: data (0x%"PRIXPTR") failed to define dictionary path \"%s\"",
			 __func__, (uintptr_t) data, path);

	return found;
}

data_t *data_copy(data_t *dest, const data_t *src)
{
	_check_magic(src);
	_check_magic(dest);

	log_flag(DATA, "%s: copy data (0x%"PRIXPTR") to (0x%"PRIXPTR")",
	       __func__, (uintptr_t) src, (uintptr_t) dest);

	switch (data_get_type(src)) {
	case DATA_TYPE_STRING:
		return data_set_string(dest, data_get_string(src));
	case DATA_TYPE_BOOL:
		return data_set_bool(dest, data_get_bool(src));
	case DATA_TYPE_INT_64:
		return data_set_int(dest, data_get_int(src));
	case DATA_TYPE_FLOAT:
		return data_set_float(dest, data_get_float(src));
	case DATA_TYPE_NULL:
		return data_set_null(dest);
	case DATA_TYPE_LIST:
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
	case DATA_TYPE_DICT:
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

extern int data_retrieve_dict_path_string(const data_t *data, const char *path,
					  char **ptr_buffer)
{
	const data_t *d = NULL;
	int rc;

	_check_magic(data);
	if (!(d = data_resolve_dict_path(data, path)))
		return SLURM_ERROR;

	rc = data_get_string_converted(d, ptr_buffer);

	log_flag(DATA, "%s: data (0x%"PRIXPTR") resolved string at path %s to \"%s\"",
		 __func__, (uintptr_t) data, path, *ptr_buffer);

	return rc;
}

extern int data_retrieve_dict_path_bool(const data_t *data, const char *path,
					bool *ptr_buffer)
{
	const data_t *d = NULL;
	int rc;

	_check_magic(data);
	if (!(d = data_resolve_dict_path(data, path)))
		return SLURM_ERROR;

	rc = data_copy_bool_converted(d, ptr_buffer);

	log_flag(DATA, "%s: data (0x%"PRIXPTR") resolved string at path %s to %s",
		 __func__, (uintptr_t) data, path,
		 (*ptr_buffer ? "true" : "false"));

	return rc;
}

extern int data_retrieve_dict_path_int(const data_t *data, const char *path,
				       int64_t *ptr_buffer)
{
	const data_t *d = NULL;
	int rc;

	_check_magic(data);
	if (!(d = data_resolve_dict_path(data, path)))
		return SLURM_ERROR;

	rc = data_get_int_converted(d, ptr_buffer);

	log_flag(DATA, "%s: data (0x%"PRIXPTR") resolved string at path %s to %"PRId64,
		 __func__, (uintptr_t) data, path, *ptr_buffer);

	return rc;
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
			return "int 64bits";
		case DATA_TYPE_STRING:
			return "string";
		case DATA_TYPE_FLOAT:
			return "floating point";
		case DATA_TYPE_BOOL:
			return "boolean";
		default:
			return "INVALID";
	}
}
