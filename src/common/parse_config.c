/*****************************************************************************\
 *  parse_config.c - parse any slurm.conf-like configuration file
 *
 *  NOTE: when you see the prefix "s_p_", think "slurm parser".
 *****************************************************************************
 *  Copyright (C) 2006-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Christopher J. Morrone <morrone2@llnl.gov>.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  SLURM is free software; you can redistribute it and/or modify it under
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include <ctype.h>
#include <regex.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#include "src/common/hostlist.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/pack.h"
#include "src/common/parse_config.h"
#include "src/common/parse_value.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_interface.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/xassert.h"

#include "slurm/slurm.h"

strong_alias(s_p_hashtbl_create,	slurm_s_p_hashtbl_create);
strong_alias(s_p_hashtbl_destroy,	slurm_s_p_hashtbl_destroy);
strong_alias(s_p_parse_buffer,		slurm_s_p_parse_buffer);
strong_alias(s_p_parse_file,		slurm_s_p_parse_file);
strong_alias(s_p_parse_pair,		slurm_s_p_parse_pair);
strong_alias(s_p_parse_line,		slurm_s_p_parse_line);
strong_alias(s_p_hashtbl_merge, 	slurm_s_p_hashtbl_merge);
strong_alias(s_p_get_string,		slurm_s_p_get_string);
strong_alias(s_p_get_long,		slurm_s_p_get_long);
strong_alias(s_p_get_uint16,		slurm_s_p_get_uint16);
strong_alias(s_p_get_uint32,		slurm_s_p_get_uint32);
strong_alias(s_p_get_uint64,		slurm_s_p_get_uint64);
strong_alias(s_p_get_float,		slurm_s_p_get_float);
strong_alias(s_p_get_double,		slurm_s_p_get_double);
strong_alias(s_p_get_long_double,	slurm_s_p_get_long_double);
strong_alias(s_p_get_pointer,		slurm_s_p_get_pointer);
strong_alias(s_p_get_array,		slurm_s_p_get_array);
strong_alias(s_p_get_boolean,		slurm_s_p_get_boolean);
strong_alias(s_p_dump_values,		slurm_s_p_dump_values);
strong_alias(transfer_s_p_options,	slurm_transfer_s_p_options);

#define BUFFER_SIZE 4096

#define CONF_HASH_LEN 173

static regex_t keyvalue_re;
static char *keyvalue_pattern =
	"^[[:space:]]*"
	"([[:alnum:]_.]+)" /* key */
	"[[:space:]]*([-*+/]?)=[[:space:]]*"
	"((\"([^\"]*)\")|([^[:space:]]+))" /* value: quoted with whitespace,
					    * or unquoted and no whitespace */
	"([[:space:]]|$)";
static bool keyvalue_initialized = false;
static bool pthread_atfork_set = false;

/* The following mutex and atfork() handler protect against receiving
 * a corrupted keyvalue_re state in a forked() child. While regexec() itself
 * appears to be thread-safe (due to internal locking), a process fork()'d
 * while a regexec() is running in a separate thread will inherit an internally
 * locked keyvalue_re leading to deadlock. This appears to have been fixed in
 * glibc with a release in 2013, although I do not know the exact version.
 */
static pthread_mutex_t s_p_lock = PTHREAD_MUTEX_INITIALIZER;

static void _s_p_atfork_child(void)
{
	slurm_mutex_init(&s_p_lock);
	keyvalue_initialized = false;
	pthread_atfork_set = false;
}

struct s_p_values {
	char *key;
	int type;
	slurm_parser_operator_t operator;
	int data_count;
	void *data;
	int (*handler)(void **data, slurm_parser_enum_t type,
		       const char *key, const char *value,
		       const char *line, char **leftover);
	void (*destroy)(void *data);
	s_p_values_t *next;
};

typedef struct _expline_values_st {
	s_p_hashtbl_t*	template;
	s_p_hashtbl_t*	index;
	s_p_hashtbl_t**	values;
} _expline_values_t;

/*
 * NOTE - "key" is case insensitive.
 */
static int _conf_hashtbl_index(const char *key)
{
	unsigned int hashval;

	xassert(key);
	for (hashval = 0; *key != 0; key++)
		hashval = tolower(*key) + 31 * hashval;
	return hashval % CONF_HASH_LEN;
}

static void _conf_hashtbl_insert(s_p_hashtbl_t *hashtbl,
				 s_p_values_t *value)
{
	int idx;

	xassert(value);
	idx = _conf_hashtbl_index(value->key);
	value->next = hashtbl[idx];
	hashtbl[idx] = value;
}

/*
 * NOTE - "key" is case insensitive.
 */
static s_p_values_t *_conf_hashtbl_lookup(
	const s_p_hashtbl_t *hashtbl, const char *key)
{
	int idx;
	s_p_values_t *p;

	xassert(key);
	if (hashtbl == NULL)
		return NULL;

	idx = _conf_hashtbl_index(key);
	for (p = hashtbl[idx]; p != NULL; p = p->next) {
		if (xstrcasecmp(p->key, key) == 0)
			return p;
	}
	return NULL;
}

s_p_hashtbl_t *s_p_hashtbl_create(const s_p_options_t options[])
{
	const s_p_options_t *op = NULL;
	s_p_values_t *value = NULL;
	s_p_hashtbl_t *hashtbl = NULL;
	_expline_values_t* expdata;
	int len;

	len = CONF_HASH_LEN * sizeof(s_p_values_t *);
	hashtbl = (s_p_hashtbl_t *)xmalloc(len);

	for (op = options; op->key != NULL; op++) {
		value = xmalloc(sizeof(s_p_values_t));
		value->key = xstrdup(op->key);
		value->operator = S_P_OPERATOR_SET;
		value->type = op->type;
		value->data_count = 0;
		value->data = NULL;
		value->next = NULL;
		value->handler = op->handler;
		value->destroy = op->destroy;
		if (op->type == S_P_LINE || op->type == S_P_EXPLINE) {
			/* line_options mandatory for S_P_*LINE */
			xassert(op->line_options);
			expdata = (_expline_values_t*)
				xmalloc(sizeof(_expline_values_t));
			expdata->template =
				s_p_hashtbl_create(op->line_options);
			expdata->index = (s_p_hashtbl_t*)xmalloc(len);
			expdata->values = NULL;
			value->data = expdata;
		}
		_conf_hashtbl_insert(hashtbl, value);
	}

	return hashtbl;
}

/* Swap the data in two data structures without changing the linked list
 * pointers */
static void _conf_hashtbl_swap_data(s_p_values_t *data_1,
				    s_p_values_t *data_2)
{
	s_p_values_t *next_1, *next_2;
	s_p_values_t tmp_values;

	next_1 = data_1->next;
	next_2 = data_2->next;

	memcpy(&tmp_values, data_1, sizeof(s_p_values_t));
	memcpy(data_1, data_2, sizeof(s_p_values_t));
	memcpy(data_2, &tmp_values, sizeof(s_p_values_t));

	data_1->next = next_1;
	data_2->next = next_2;
}

static void _conf_file_values_free(s_p_values_t *p)
{
	int i;
	_expline_values_t* v;

	if (p->data_count > 0) {
		switch(p->type) {
		case S_P_ARRAY:
			for (i = 0; i < p->data_count; i++) {
				void **ptr_array = (void **)p->data;
				if (p->destroy != NULL) {
					p->destroy(ptr_array[i]);
				} else {
					xfree(ptr_array[i]);
				}
			}
			xfree(p->data);
			break;
		case S_P_LINE:
		case S_P_EXPLINE:
			v = (_expline_values_t*)p->data;
			s_p_hashtbl_destroy(v->template);
			s_p_hashtbl_destroy(v->index);
			for (i = 0; i < p->data_count; ++i) {
				s_p_hashtbl_destroy(v->values[i]);
			}
			xfree(v->values);
			xfree(p->data);
			break;
		default:
			if (p->destroy != NULL) {
				p->destroy(p->data);
			} else {
				xfree(p->data);
			}
			break;
		}
	}
	xfree(p->key);
	xfree(p);
}

void s_p_hashtbl_destroy(s_p_hashtbl_t *hashtbl) {
	int i;
	s_p_values_t *p, *next;

	if (!hashtbl)
		return;

	for (i = 0; i < CONF_HASH_LEN; i++) {
		for (p = hashtbl[i]; p != NULL; p = next) {
			next = p->next;
			_conf_file_values_free(p);
		}
	}
	xfree(hashtbl);

	/*
	 * Now clear the global variables to free their memory as well.
	 */
	slurm_mutex_lock(&s_p_lock);
	if (keyvalue_initialized) {
		regfree(&keyvalue_re);
		keyvalue_initialized = false;
	}
	slurm_mutex_unlock(&s_p_lock);

}

static void _keyvalue_regex_init(void)
{
	slurm_mutex_lock(&s_p_lock);
	if (!keyvalue_initialized) {
		if (regcomp(&keyvalue_re, keyvalue_pattern,
			    REG_EXTENDED) != 0) {
			/* FIXME - should be fatal? */
			error("keyvalue regex compilation failed");
		}
		keyvalue_initialized = true;
	}
	if (!pthread_atfork_set) {
		pthread_atfork(NULL, NULL, _s_p_atfork_child);
		pthread_atfork_set = true;
	}
	slurm_mutex_unlock(&s_p_lock);
}

/*
 * IN line - string to be search for a key=value pair
 * OUT key - pointer to the key string (caller must free with xfree())
 * OUT value - pointer to the value string (caller must free with xfree())
 * OUT remaining - pointer into the "line" string denoting the start
 *                 of the unsearched portion of the string
 * Return 0 when a key-value pair is found, and -1 otherwise.
 */
static int _keyvalue_regex(const char *line,
			   char **key, char **value, char **remaining,
			   slurm_parser_operator_t *operator)
{
	size_t nmatch = 8;
	regmatch_t pmatch[8];
	char op;

	*key = NULL;
	*value = NULL;
	*remaining = (char *)line;
	*operator = S_P_OPERATOR_SET;
	memset(pmatch, 0, sizeof(regmatch_t)*nmatch);

	if (regexec(&keyvalue_re, line, nmatch, pmatch, 0)
	    == REG_NOMATCH) {
		return -1;
	}

	*key = (char *)(xstrndup(line + pmatch[1].rm_so,
				 pmatch[1].rm_eo - pmatch[1].rm_so));
	if (pmatch[2].rm_so != -1 &&
	    (pmatch[2].rm_so != pmatch[2].rm_eo)) {
		op = *(line + pmatch[2].rm_so);
		if (op == '+') {
			*operator = S_P_OPERATOR_ADD;
		} else if (op == '-') {
			*operator = S_P_OPERATOR_SUB;
		} else if (op == '*') {
			*operator = S_P_OPERATOR_MUL;
		} else if (op == '/') {
			*operator = S_P_OPERATOR_DIV;
		}
	}
	if (pmatch[5].rm_so != -1) {
		*value = (char *)(xstrndup(line + pmatch[5].rm_so,
					   pmatch[5].rm_eo - pmatch[5].rm_so));
	} else if (pmatch[6].rm_so != -1) {
		*value = (char *)(xstrndup(line + pmatch[6].rm_so,
					   pmatch[6].rm_eo - pmatch[6].rm_so));
	} else {
		*value = xstrdup("");
	}

	*remaining = (char *)(line + pmatch[3].rm_eo);

	return 0;
}

static int _strip_continuation(char *buf, int len)
{
	char *ptr;
	int bs = 0;

	if (len == 0)
		return len;	/* Empty line */

	for (ptr = buf+len-1; ptr >= buf; ptr--) {
		if (*ptr == '\\')
			bs++;
		else if (isspace((int)*ptr) && (bs == 0))
			continue;
		else
			break;
	}
	/* Check for an odd number of contiguous backslashes at
	 * the end of the line */
	if ((bs % 2) == 1) {
		ptr = ptr + bs;
		*ptr = '\0';
		return (ptr - buf);
	} else {
		return len; /* no continuation */
	}
}

/*
 * Strip out trailing carriage returns and newlines
 */
static void _strip_cr_nl(char *line)
{
	int len = strlen(line);
	char *ptr;

	for (ptr = line+len-1; ptr >= line; ptr--) {
		if (*ptr=='\r' || *ptr=='\n') {
			*ptr = '\0';
		} else {
			return;
		}
	}
}

/* Strip comments from a line by terminating the string
 * where the comment begins.
 * Everything after a non-escaped "#" is a comment.
 */
static void _strip_comments(char *line)
{
	int i;
	int len = strlen(line);
	int bs_count = 0;

	for (i = 0; i < len; i++) {
		/* if # character is preceded by an even number of
		 * escape characters '\' */
		if (line[i] == '#' && (bs_count%2) == 0) {
			line[i] = '\0';
 			break;
		} else if (line[i] == '\\') {
			bs_count++;
		} else {
			bs_count = 0;
		}
	}
}

/*
 * Strips any escape characters, "\".  If you WANT a back-slash,
 * it must be escaped, "\\".
 */
static void _strip_escapes(char *line)
{
	int i, j;
	int len = strlen(line);

	for (i = 0, j = 0; i < len+1; i++, j++) {
		if (line[i] == '\\')
			i++;
		line[j] = line[i];
	}
}

/* This can be used to make sure files are the same across nodes if needed */
static void _compute_hash_val(uint32_t *hash_val, char *line)
{
	int idx, i, len;

	if (!hash_val)
		return;

	len = strlen(line);
	for (i = 0; i < len; i++) {
		(*hash_val) = ( (*hash_val) ^ line[i] << 8 );

		for (idx = 0; idx < 8; ++idx) {
			if ((*hash_val) & 0x8000) {
				(*hash_val) <<= 1;
				(*hash_val) = (*hash_val) ^ 4129;
			} else
				(*hash_val) <<= 1;
		}
	}
}


/*
 * Reads the next line from the "file" into buffer "buf".
 *
 * Concatonates together lines that are continued on
 * the next line by a trailing "\".  Strips out comments,
 * replaces escaped "\#" with "#", and replaces "\\" with "\".
 */
static int _get_next_line(char *buf, int buf_size,
			  uint32_t *hash_val, FILE *file)
{
	char *ptr = buf;
	int leftover = buf_size;
	int read_size, new_size;
	int lines = 0;

	while (fgets(ptr, leftover, file)) {
		lines++;
		_compute_hash_val(hash_val, ptr);
		_strip_comments(ptr);
		read_size = strlen(ptr);
		new_size = _strip_continuation(ptr, read_size);
		if (new_size < read_size) {
			ptr += new_size;
			leftover -= new_size;
		} else { /* no continuation */
			break;
		}
	}
	/* _strip_cr_nl(buf); */ /* not necessary */
	_strip_escapes(buf);

	return lines;
}

/* copy all the keys from 'from_hashtbl' along with their types, handler, and
 * destroy fields. Omit values in the copy and initialize them to NULL/0.
 *
 * if change_* is true, corresponding field will be updated with the next
 * corresponding parameter. */
s_p_hashtbl_t* _hashtbl_copy_keys(const s_p_hashtbl_t* from_hashtbl,
				  bool change_type,
				  slurm_parser_enum_t new_type,
				  bool change_handler,
				  int (*handler)(void **data,
					  slurm_parser_enum_t type, const char
					  *key, const char *value, const char
					  *line, char **leftover),
				  bool change_destroyer,
				  void (*destroy)(void *data)
				  )
{
	s_p_hashtbl_t* to_hashtbl = NULL;
	s_p_values_t *val_ptr,* val_copy;
	int len, i;

	xassert(from_hashtbl);

	len = CONF_HASH_LEN * sizeof(s_p_values_t *);
	to_hashtbl = (s_p_hashtbl_t *)xmalloc(len);

	for (i = 0; i < CONF_HASH_LEN; ++i) {
		for (val_ptr = from_hashtbl[i]; val_ptr;
		     val_ptr = val_ptr->next) {
			val_copy = xmalloc(sizeof(s_p_values_t));
			val_copy->key = xstrdup(val_ptr->key);
			val_copy->operator = val_ptr->operator;
			val_copy->type = val_ptr->type;
			val_copy->handler = val_ptr->handler;
			val_copy->destroy = val_ptr->destroy;
			if (change_type) {
				val_copy->type = new_type;
			}
			if (change_handler) {
				val_copy->handler = handler;
			}
			if (change_destroyer) {
				val_copy->destroy = destroy;
			}
			_conf_hashtbl_insert(to_hashtbl, val_copy);
		}
	}

	return to_hashtbl;
}


static int _handle_common(s_p_values_t *v,
			  const char *value, const char *line, char **leftover,
			  void* (*convert)(const char* key, const char* value))
{
	if (v->data_count != 0) {
		if (run_in_daemon("slurmctld,slurmd,slurmdbd"))
			error("%s 1 specified more than once, latest value used",
			      v->key);
		xfree(v->data);
		v->data_count = 0;
	}

	if (v->handler != NULL) {
		/* call the handler function */
		int rc;
		rc = v->handler(&v->data, v->type, v->key, value,
				line, leftover);
		if (rc != 1)
			return rc == 0 ? 0 : -1;
	} else {
		v->data = convert(v->key, value);
		if (!v->data) {
			return -1;
		}
	}

	v->data_count = 1;
	return 1;
}

static void* _handle_string(const char* key, const char* value)
{
	return xstrdup(value);
}

static void* _handle_long(const char* key, const char* value)
{
	long* data = (long*)xmalloc(sizeof(long));
	if (s_p_handle_long(data, key, value) == SLURM_ERROR)
		return NULL;
	return data;
}

static void* _handle_uint16(const char* key, const char* value)
{
	uint16_t* data = (uint16_t*)xmalloc(sizeof(uint16_t));
	if (s_p_handle_uint16(data, key, value) == SLURM_ERROR)
		return NULL;
	return data;
}

static void* _handle_uint32(const char* key, const char* value)
{
	uint32_t* data = (uint32_t*)xmalloc(sizeof(uint32_t));
	if (s_p_handle_uint32(data, key, value) == SLURM_ERROR)
		return NULL;
	return data;
}

static void* _handle_uint64(const char* key, const char* value)
{
	uint64_t* data = (uint64_t*)xmalloc(sizeof(uint64_t));
	if (s_p_handle_uint64(data, key, value) == SLURM_ERROR)
		return NULL;
	return data;
}

static void* _handle_boolean(const char* key, const char* value)
{
	bool* data = (bool*)xmalloc(sizeof(bool));
	if (s_p_handle_boolean(data, key, value) == SLURM_ERROR)
		return NULL;
	return data;
}

static void* _handle_float(const char* key, const char* value)
{
	float* data = (float*)xmalloc(sizeof(float));
	if (s_p_handle_float(data, key, value) == SLURM_ERROR)
		return NULL;
	return data;
}

static void* _handle_double(const char* key, const char* value)
{
	double* data = (double*)xmalloc(sizeof(double));
	if (s_p_handle_double(data, key, value) == SLURM_ERROR)
		return NULL;
	return data;
}

static void* _handle_ldouble(const char* key, const char* value)
{
	long double* data = (long double*)xmalloc(sizeof(long double));
	if (s_p_handle_long_double(data, key, value) == SLURM_ERROR)
		return NULL;
	return data;
}

static int _handle_pointer(s_p_values_t *v, const char *value,
			   const char *line, char **leftover)
{
	if (v->handler != NULL) {
		/* call the handler function */
		int rc;
		rc = v->handler(&v->data, v->type, v->key, value,
				line, leftover);
		if (rc != 1)
			return rc == 0 ? 0 : -1;
	} else {
		if (v->data_count != 0) {
			if (run_in_daemon("slurmctld,slurmd,slurmdbd"))
				error("%s 2 specified more than once, latest value used",
				      v->key);
			xfree(v->data);
			v->data_count = 0;
		}
		v->data = xstrdup(value);
	}

	v->data_count = 1;
	return 1;
}

static int _handle_array(s_p_values_t *v, const char *value,
			 const char *line, char **leftover)
{
	void *new_ptr;
	void **data;

	if (v->handler != NULL) {
		/* call the handler function */
		int rc;
		rc = v->handler(&new_ptr, v->type, v->key, value,
				line, leftover);
		if (rc != 1)
			return rc == 0 ? 0 : -1;
	} else {
		new_ptr = xstrdup(value);
	}
	v->data_count += 1;
	v->data = xrealloc(v->data, (v->data_count)*sizeof(void *));
	data = &((void**)v->data)[v->data_count-1];
	*data = new_ptr;

	return 1;
}

/* custom destroyer that just do nothing, sub-hashtable freeing is performed
 * in _conf_file_values_free for S_P_LINE and S_P_EXPLINE */
static void _empty_destroy(void* data) { }

/* sc = string case
 * look for an already indexed table with the same (master) key.
 * if a table is found, merge the new one within.
 * otherwise, add the new table and create an index for further lookup.
 */
static void _handle_expline_sc(s_p_hashtbl_t* index_tbl,
			       const char* master_value,
			       s_p_hashtbl_t* tbl,
			       s_p_hashtbl_t*** tables,
			       int* tables_count)
{
	s_p_values_t* matchp_index,* index_value;
	matchp_index = _conf_hashtbl_lookup(index_tbl, master_value);
	if (matchp_index) {
		s_p_hashtbl_merge_override(
			(s_p_hashtbl_t*)matchp_index->data, tbl);
		s_p_hashtbl_destroy(tbl);
	} else {
		index_value = (s_p_values_t*)xmalloc(sizeof(s_p_values_t));
		index_value->key = xstrdup(master_value);
		index_value->destroy = _empty_destroy;
		index_value->data = tbl;
		_conf_hashtbl_insert(index_tbl, index_value);
		(*tables_count) += 1;
		(*tables) = (s_p_hashtbl_t**)xrealloc(*tables,
				*tables_count * sizeof(s_p_hashtbl_t*));
		(*tables)[*tables_count - 1] = tbl;
	}
}

static int _handle_expline_cmp_long(const void* v1, const void* v2)
{
	return *((long*)v1) != *((long*)v2);
}
static int _handle_expline_cmp_uint16(const void* v1, const void* v2)
{
	return *((uint16_t*)v1) != *((uint16_t*)v2);
}
static int _handle_expline_cmp_uint32(const void* v1, const void* v2)
{
	return *((uint32_t*)v1) != *((uint32_t*)v2);
}
static int _handle_expline_cmp_uint64(const void* v1, const void* v2)
{
	return *((uint64_t*)v1) != *((uint64_t*)v2);
}
static int _handle_expline_cmp_float(const void* v1, const void* v2)
{
	return *((float*)v1) != *((float*)v2);
}
static int _handle_expline_cmp_double(const void* v1, const void* v2)
{
	return *((double*)v1) != *((double*)v2);
}
static int _handle_expline_cmp_ldouble(const void* v1, const void* v2)
{
	return *((long double*)v1) != *((long double*)v2);
}

/* ac = array case
 * the master key type is not string. Iterate over the tables looking
 * for the value associated with the new master to add/update.
 * If a corresponding table is found, update it with the content of the
 * new one, otherwise, add the new table.
 */
static void _handle_expline_ac(s_p_hashtbl_t* tbl,
			       const char* master_key,
			       const void* master_value,
			       int (*cmp)(const void* v1, const void* v2),
			       s_p_hashtbl_t*** tables,
			       int* tables_count)
{
	s_p_values_t* matchp;
	s_p_hashtbl_t* table;
	int i;

	for (i = 0; i < *tables_count; ++i) {
		table = (*tables)[i];
		matchp = _conf_hashtbl_lookup(table, master_key);
		xassert(matchp); /* same template, should never be NULL */
		if (!cmp(matchp->data, master_value)) {
			/* found hash tbl to merge with */
			s_p_hashtbl_merge_override(table, tbl);
			s_p_hashtbl_destroy(tbl);
			return;
		}
	}

	/* not found, just add it */
	*tables_count += 1;
	*tables = (s_p_hashtbl_t**)xrealloc(*tables,
			*tables_count * sizeof(s_p_hashtbl_t*));
	*tables[*tables_count - 1] = tbl;
}

/*
 * merge a freshly generated s_p_hashtbl_t from the line/expline processing
 * with the already added s_p_hashtbl_t elements of the previously processed
 * siblings
 */
static void _handle_expline_merge(_expline_values_t* v_data,
				  int* tables_count, /* not accessible in v_data
							since in v */
				  const char* master_key,
				  s_p_hashtbl_t* current_tbl)
{
	s_p_values_t* matchp = _conf_hashtbl_lookup(current_tbl, master_key);

	/* record should have been skipped if key not in sub-hash-table */
	xassert(matchp);

	switch(matchp->type) {
	case S_P_STRING:
		_handle_expline_sc(v_data->index, matchp->data, current_tbl,
				   &v_data->values, tables_count);
		break;
	case S_P_LONG:
		_handle_expline_ac(current_tbl, master_key, matchp->data,
				   _handle_expline_cmp_long, &v_data->values,
				   tables_count);
		break;
	case S_P_UINT16:
		_handle_expline_ac(current_tbl, master_key, matchp->data,
				   _handle_expline_cmp_uint16, &v_data->values,
				   tables_count);
		break;
	case S_P_UINT32:
		_handle_expline_ac(current_tbl, master_key, matchp->data,
				   _handle_expline_cmp_uint32, &v_data->values,
				   tables_count);
		break;
	case S_P_UINT64:
		_handle_expline_ac(current_tbl, master_key, matchp->data,
				   _handle_expline_cmp_uint64, &v_data->values,
				   tables_count);
		break;
	case S_P_FLOAT:
		_handle_expline_ac(current_tbl, master_key, matchp->data,
				   _handle_expline_cmp_float, &v_data->values,
				   tables_count);
		break;
	case S_P_DOUBLE:
		_handle_expline_ac(current_tbl, master_key, matchp->data,
				   _handle_expline_cmp_double, &v_data->values,
				   tables_count);
		break;
	case S_P_LONG_DOUBLE:
		_handle_expline_ac(current_tbl, master_key, matchp->data,
				   _handle_expline_cmp_ldouble, &v_data->values,
				   tables_count);
		break;
	}
}

static int _handle_line(s_p_values_t* v, const char* value,
			const char* line, char** leftover)
{
	_expline_values_t* v_data = (_expline_values_t*)v->data;
	s_p_hashtbl_t* newtable;

	newtable = _hashtbl_copy_keys(v_data->template, false, S_P_IGNORE,
				      false, NULL, false, NULL);
	if (s_p_parse_line_complete(newtable, v->key, value, line,
				    leftover) == SLURM_ERROR) {
		s_p_hashtbl_destroy(newtable);
		return -1;
	}

	_handle_expline_merge(v_data, &v->data_count, v->key, newtable);

	return 1;
}


static int _handle_expline(s_p_values_t* v, const char* value,
			   const char* line, char** leftover)
{
	_expline_values_t* v_data = (_expline_values_t*)v->data;
	s_p_hashtbl_t** new_tables;
	int new_tables_count, i;

	if (s_p_parse_line_expanded(v_data->template,
				    &new_tables, &new_tables_count,
				    v->key, value,
				    line, leftover) == SLURM_ERROR) {
		return -1;
	}

	for (i = 0; i < new_tables_count; ++i) {
		_handle_expline_merge(v_data, &v->data_count,
				      v->key, new_tables[i]);
	}
	xfree(new_tables);
	return 1;
}

/*
 * IN line: the entire line that currently being parsed
 * IN/OUT leftover: leftover is a pointer into the "line" string.
 *                  The incoming leftover point is a pointer to the
 *                  character just after the already parsed key/value pair.
 *                  If the handler for that key parses more of the line,
 *                  it will move the leftover pointer to point to the character
 *                  after it has finished parsing in the line.
 */
static void _handle_keyvalue_match(s_p_values_t *v,
				   const char *value, const char *line,
				   char **leftover)
{
	switch (v->type) {
	case S_P_IGNORE:
		/* do nothing */
		break;
	case S_P_STRING:
		_handle_common(v, value, line, leftover, _handle_string);
		break;
	case S_P_LONG:
		_handle_common(v, value, line, leftover, _handle_long);
		break;
	case S_P_UINT16:
		_handle_common(v, value, line, leftover, _handle_uint16);
		break;
	case S_P_UINT32:
		_handle_common(v, value, line, leftover, _handle_uint32);
		break;
	case S_P_UINT64:
		_handle_common(v, value, line, leftover, _handle_uint64);
		break;
	case S_P_POINTER:
		_handle_pointer(v, value, line, leftover);
		break;
	case S_P_ARRAY:
		_handle_array(v, value, line, leftover);
		break;
	case S_P_BOOLEAN:
		_handle_common(v, value, line, leftover, _handle_boolean);
		break;
	case S_P_LINE:
		_handle_line(v, value, line, leftover);
		break;
	case S_P_EXPLINE:
		_handle_expline(v, value, line, leftover);
		break;
	case S_P_FLOAT:
		_handle_common(v, value, line, leftover, _handle_float);
		break;
	case S_P_DOUBLE:
		_handle_common(v, value, line, leftover, _handle_double);
		break;
	case S_P_LONG_DOUBLE:
		_handle_common(v, value, line, leftover, _handle_ldouble);
		break;
	}
}

/*
 * Return 1 if all characters in "line" are white-space characters,
 *   otherwise return 0.
 */
static int _line_is_space(const char *line)
{
	int len;
	int i;

	if (line == NULL) {
		return 1;
	}
	len = strlen(line);
	for (i = 0; i < len; i++) {
		if (!isspace((int)line[i]))
			return 0;
	}

	return 1;
}


/*
 * Returns 1 if the line is parsed cleanly, and 0 otherwise.
 */
int s_p_parse_line(s_p_hashtbl_t *hashtbl, const char *line, char **leftover)
{
	char *key, *value;
	char *ptr = (char *)line;
	s_p_values_t *p;
	char *new_leftover;
	slurm_parser_operator_t op;

	_keyvalue_regex_init();

	while (_keyvalue_regex(ptr, &key, &value, &new_leftover, &op) == 0) {
		if ((p = _conf_hashtbl_lookup(hashtbl, key))) {
			p->operator = op;
			_handle_keyvalue_match(p, value,
					       new_leftover, &new_leftover);
			*leftover = ptr = new_leftover;
		} else {
			error("Parsing error at unrecognized key: %s", key);
			xfree(key);
			xfree(value);
			slurm_seterrno(EINVAL);
			return 0;
		}
		xfree(key);
		xfree(value);
	}

	return 1;
}

/*
 * Returns 1 if the line is parsed cleanly, and 0 otherwise.
 * IN ingore_new - if set do not treat unrecongized input as a fatal error
 */
static int _parse_next_key(s_p_hashtbl_t *hashtbl,
			   const char *line, char **leftover, bool ignore_new)
{
	char *key, *value;
	s_p_values_t *p;
	char *new_leftover;
	slurm_parser_operator_t op;

	_keyvalue_regex_init();

	if (_keyvalue_regex(line, &key, &value, &new_leftover, &op) == 0) {
		if ((p = _conf_hashtbl_lookup(hashtbl, key))) {
			p->operator = op;
			_handle_keyvalue_match(p, value,
					       new_leftover, &new_leftover);
			*leftover = new_leftover;
		} else if (ignore_new) {
			debug("%s: Parsing error at unrecognized key: %s",
			      __func__, key);
			*leftover = (char *)line;
		} else {
			error("%s: Parsing error at unrecognized key: %s",
			      __func__, key);
			xfree(key);
			xfree(value);
			*leftover = (char *)line;
			slurm_seterrno(EINVAL);
			return 0;
		}
		xfree(key);
		xfree(value);
	} else {
		*leftover = (char *)line;
	}

	return 1;
}

static char * _add_full_path(char *file_name, char *slurm_conf_path)
{
	char *path_name = NULL, *slash;

	if ((file_name == NULL) || (file_name[0] == '/')) {
		path_name = xstrdup(file_name);
		return path_name;
	}

	path_name = xstrdup(slurm_conf_path);
	slash = strrchr(path_name, '/');
	if (slash)
		slash[0] = '\0';
	xstrcat(path_name, "/");
	xstrcat(path_name, file_name);

	return path_name;
}

static char *_parse_for_format(s_p_hashtbl_t *f_hashtbl, char *path)
{
	char *filename = xstrdup(path);
	char *format = NULL;
	char *tmp_str = NULL;

	while (1) {
		if ((format = strstr(filename, "%c"))) { /* ClusterName */
			if (!s_p_get_string(&tmp_str, "ClusterName",f_hashtbl)){
				error("%s: Did not get ClusterName for include "
				      "path", __func__);
				xfree(filename);
				break;
			}
			xstrtolower(tmp_str);
		} else {	/* No special characters */
			break;
		}

		/* Build the new path if tmp_str is not NULL*/
		if (tmp_str != NULL) {
			format[0] = '\0';
			xstrfmtcat(filename, "%s%s", tmp_str, format+2);
			tmp_str = NULL;
		} else {
			error("%s: Value for include modifier %s could "
			      "not be found", __func__, format);
			xfree(filename);
			break;
		}
	}

	return filename;
}

/*
 * Returns 1 if the line contained an include directive and the included
 * file was parsed without error.  Returns -1 if the line was an include
 * directive but the included file contained errors.  Returns 0 if
 * no include directive is found.
 */
static int _parse_include_directive(s_p_hashtbl_t *hashtbl, uint32_t *hash_val,
				    const char *line, char **leftover,
				    bool ignore_new, char *slurm_conf_path)
{
	char *ptr;
	char *fn_start, *fn_stop;
	char *file_name, *path_name;
	char *file_with_mod;
	int rc;

	*leftover = NULL;
	if (xstrncasecmp("include", line, strlen("include")) == 0) {
		ptr = (char *)line + strlen("include");

		if (!isspace((int)*ptr))
			return 0;
		while (isspace((int)*ptr))
			ptr++;
		fn_start = ptr;
		while (!isspace((int)*ptr))
			ptr++;
		fn_stop = *leftover = ptr;

		file_with_mod = xstrndup(fn_start, fn_stop-fn_start);
		file_name = _parse_for_format(hashtbl, file_with_mod);//
		xfree(file_with_mod);
		if (!file_name)	/* Error printed by _parse_for_format() */
			return -1;
		path_name = _add_full_path(file_name, slurm_conf_path);
		xfree(file_name);
		rc = s_p_parse_file(hashtbl, hash_val, path_name, ignore_new);
		xfree(path_name);
		if (rc == SLURM_SUCCESS)
			return 1;
		else
			return -1;
	} else {
		return 0;
	}
}

int s_p_parse_file(s_p_hashtbl_t *hashtbl, uint32_t *hash_val, char *filename,
		   bool ignore_new)
{
	FILE *f;
	char *leftover = NULL;
	int i, rc = SLURM_SUCCESS;
	int line_number;
	int merged_lines;
	int inc_rc;
	struct stat stat_buf;
	char *line = NULL;

	if (!filename) {
		error("s_p_parse_file: No filename given.");
		return SLURM_ERROR;
	}

	_keyvalue_regex_init();
	for (i = 0; ; i++) {
		if (i == 1) {	/* Long once, on first retry */
			error("s_p_parse_file: unable to status file %s: %m, "
			      "retrying in 1sec up to 60sec",
			      filename);
		}
		if (i >= 60)	/* Give up after 60 seconds */
			return SLURM_ERROR;
		if (i > 0)
			sleep(1);
		if (stat(filename, &stat_buf) >= 0)
			break;
	}
	if (stat_buf.st_size == 0) {
		info("s_p_parse_file: file \"%s\" is empty", filename);
		return SLURM_SUCCESS;
	}
	f = fopen(filename, "r");
	if (f == NULL) {
		error("s_p_parse_file: unable to read \"%s\": %m",
		      filename);
		return SLURM_ERROR;
	}

	/* Buffer needs one extra byte for trailing '\0' */
	line = xmalloc(sizeof(char) * stat_buf.st_size + 1);
	line_number = 1;
	while ((merged_lines = _get_next_line(
			line, stat_buf.st_size + 1, hash_val, f)) > 0) {
		/* skip empty lines */
		if (line[0] == '\0') {
			line_number += merged_lines;
			continue;
		}

		inc_rc = _parse_include_directive(hashtbl, hash_val,
						  line, &leftover, ignore_new,
						  filename);
		if (inc_rc == 0) {
			_parse_next_key(hashtbl, line, &leftover, ignore_new);
		} else if (inc_rc < 0) {
			error("\"Include\" failed in file %s line %d",
			      filename, line_number);
			rc = SLURM_ERROR;
			line_number += merged_lines;
			continue;
		}

		/* Make sure that after parsing only whitespace is left over */
		if (!_line_is_space(leftover)) {
			char *ptr = xstrdup(leftover);
			_strip_cr_nl(ptr);
			if (ignore_new) {
				debug("Parse error in file %s line %d: \"%s\"",
				      filename, line_number, ptr);
			} else {
				error("Parse error in file %s line %d: \"%s\"",
				      filename, line_number, ptr);
				rc = SLURM_ERROR;
			}
			xfree(ptr);
		}
		line_number += merged_lines;
	}

	xfree(line);
	fclose(f);
	return rc;
}

int s_p_parse_buffer(s_p_hashtbl_t *hashtbl, uint32_t *hash_val,
		     Buf buffer, bool ignore_new)
{
	char *leftover = NULL;
	int rc = SLURM_SUCCESS;
	int line_number;
	uint32_t utmp32;
	char *tmp_str = NULL;

	if (!buffer) {
		error("s_p_parse_buffer: No buffer given.");
		return SLURM_ERROR;
	}

	line_number = 0;
	_keyvalue_regex_init();
	while (remaining_buf(buffer) > 0) {
		safe_unpackstr_xmalloc(&tmp_str, &utmp32, buffer);
		if (tmp_str != NULL) {
			line_number++;
			if (*tmp_str == '\0') {
				xfree(tmp_str);
				continue;
			}
			_parse_next_key(hashtbl, tmp_str, &leftover, ignore_new);
			/* Make sure that after parsing only whitespace
			   is left over */
			if (!_line_is_space(leftover)) {
				char *ptr = xstrdup(leftover);
				_strip_cr_nl(ptr);
				if (ignore_new) {
					debug("s_p_parse_buffer : error in line"
					      " %d: \"%s\"", line_number, ptr);
				} else {
					error("s_p_parse_buffer : error in line"
					      " %d: \"%s\"", line_number, ptr);
					rc = SLURM_ERROR;
				}
				xfree(ptr);
			}
			xfree(tmp_str);
			if (rc == SLURM_SUCCESS)
				continue;
		}
	unpack_error:
		debug3("s_p_parse_buffer: ending after line %u",
		       line_number);
		break;
	}

	return rc;
}

/*
 * s_p_hashtbl_merge
 *
 * Merge the contents of two s_p_hashtbl_t data structures. Anything in
 * from_hashtbl that does not also appear in to_hashtbl is transfered to it.
 * This is intended primary to support multiple lines of DEFAULT configuration
 * information and preserve the default values while adding new defaults.
 *
 * IN from_hashtbl - Source of old data
 * IN to_hashtbl - Destination for old data
 */
void s_p_hashtbl_merge(s_p_hashtbl_t *to_hashtbl, s_p_hashtbl_t *from_hashtbl)
{
	int i;
	s_p_values_t **val_pptr, *val_ptr, *match_ptr;

	if (!to_hashtbl || !from_hashtbl)
		return;

	for (i = 0; i < CONF_HASH_LEN; i++) {
		val_pptr = &from_hashtbl[i];
		val_ptr = from_hashtbl[i];
		while (val_ptr) {
			if (val_ptr->data_count == 0) {
				/* No data in from_hashtbl record to move.
				 * Skip record */
				val_pptr = &val_ptr->next;
				val_ptr = val_ptr->next;
				continue;
			}
			match_ptr = _conf_hashtbl_lookup(to_hashtbl,
							 val_ptr->key);
			if (match_ptr) {	/* Found matching key */
				if (match_ptr->data_count == 0) {
					_conf_hashtbl_swap_data(val_ptr,
								match_ptr);
				}
				val_pptr = &val_ptr->next;
				val_ptr = val_ptr->next;
			} else {	/* No match, move record */
				*val_pptr = val_ptr->next;
				val_ptr->next = NULL;
				_conf_hashtbl_insert(to_hashtbl, val_ptr);
				val_ptr = *val_pptr;
			}
		}
	}
}

void s_p_hashtbl_merge_override(s_p_hashtbl_t *to_hashtbl,
				s_p_hashtbl_t *from_hashtbl)
{
	int i;
	s_p_values_t **val_pptr, *val_ptr, *match_ptr;

	if (!to_hashtbl || !from_hashtbl)
		return;

	for (i = 0; i < CONF_HASH_LEN; i++) {
		val_pptr = &from_hashtbl[i];
		val_ptr = from_hashtbl[i];
		while (val_ptr) {
			if (val_ptr->data_count == 0) {
				/* No data in from_hashtbl record to move.
				 * Skip record */
				val_pptr = &val_ptr->next;
				val_ptr = val_ptr->next;
				continue;
			}
			match_ptr = _conf_hashtbl_lookup(to_hashtbl,
							 val_ptr->key);
			if (match_ptr) {	/* Found matching key */
				_conf_hashtbl_swap_data(val_ptr, match_ptr);
				val_pptr = &val_ptr->next;
				val_ptr = val_ptr->next;
			} else {	/* No match, move record */
				*val_pptr = val_ptr->next;
				val_ptr->next = NULL;
				_conf_hashtbl_insert(to_hashtbl, val_ptr);
				val_ptr = *val_pptr;
			}
		}
	}
}

void s_p_hashtbl_merge_keys(s_p_hashtbl_t *to_hashtbl,
			    s_p_hashtbl_t *from_hashtbl)
{
	int i;
	_expline_values_t* f_expline;
	_expline_values_t* t_expline;
	s_p_values_t **pp, *p, *match_ptr;

	if (!to_hashtbl || !from_hashtbl)
		return;

	for (i = 0; i < CONF_HASH_LEN; i++) {
		pp = &from_hashtbl[i];
		p = from_hashtbl[i];
		while (p) {
			match_ptr = _conf_hashtbl_lookup(to_hashtbl, p->key);
			if (match_ptr) {	/* Found matching key */
				if (match_ptr->type == p->type &&
				    (p->type == S_P_LINE ||
				     p->type == S_P_EXPLINE)) {
					t_expline = (_expline_values_t*)
						    match_ptr->data;
					f_expline = (_expline_values_t*)
						    p->data;
					s_p_hashtbl_merge_keys(
							t_expline->template,
							f_expline->template);
					/* Keys merged, free container memory */
					s_p_hashtbl_destroy(f_expline->template);
					s_p_hashtbl_destroy(f_expline->index);
					//FIXME: Destroy "values" ?
					xfree(f_expline);
				}
				pp = &p->next;
				p = p->next;
			} else {	/* No match, move record */
				*pp = p->next;
				p->next = NULL;
				_conf_hashtbl_insert(to_hashtbl, p);
				p = *pp;
			}
		}
	}

}

int s_p_parse_line_complete(s_p_hashtbl_t *hashtbl,
			    const char* key, const char* value,
			    const char *line, char **leftover)
{
	if (!s_p_parse_pair(hashtbl, key, value)) {
		error("Error parsing '%s = %s', most left part of the"
		      " line: %s.", key, value, line);
		return SLURM_ERROR;
	}

	if (!s_p_parse_line(hashtbl, *leftover, leftover)) {
		error("Unable to parse line %s", *leftover);
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

/*
 * custom handlers used by _parse_expline_adapt_table for config elements
 * expansions.
 */
static int _parse_line_expanded_handler(
		void **dest, slurm_parser_enum_t type,
		const char *key, const char *value,
		const char *line, char **leftover)
{
	*dest = hostlist_create(value);
	/* FIXME: this function should always return either an empty list, or
	 * the string as its first element if it was not expandable, or the
	 * list of strings expanded. So the only case where it returns null,
	 * is where there is no more enough memory to allocate the list, and
	 * it should be a crash cause.
	 */
	xassert(*dest);
	return 1;
}
static void _parse_line_expanded_destroyer(void* data)
{
	hostlist_destroy(data);
}

/*
 * convert every s_p_values_t to an S_P_POINTER casted hostlist (except
 * S_P_PLAIN_STRING)
 * This will enable to generate the hostlists corresponding to all the config
 * elements in order to later map the various expanded master keys to their
 * corresponding config values.
 * S_P_PLAIN_STRING specifying not be considered as an expandable string
 * is thus just converted to a real S_P_STRING and not an hostlist.
 */
static s_p_hashtbl_t* _parse_expline_adapt_table(const s_p_hashtbl_t* hashtbl)
{
	s_p_hashtbl_t* to_hashtbl = NULL;
	s_p_values_t *val_ptr,* val_copy;
	int len, i;

	xassert(hashtbl);

	len = CONF_HASH_LEN * sizeof(s_p_values_t *);
	to_hashtbl = (s_p_hashtbl_t *)xmalloc(len);

	for (i = 0; i < CONF_HASH_LEN; ++i) {
		for (val_ptr = hashtbl[i]; val_ptr; val_ptr = val_ptr->next) {
			val_copy = xmalloc(sizeof(s_p_values_t));
			val_copy->key = xstrdup(val_ptr->key);
			val_copy->operator = val_ptr->operator;
			if (val_ptr->type == S_P_PLAIN_STRING) {
				val_copy->type = S_P_STRING;
			} else {
				val_copy->type = S_P_POINTER;
				val_copy->handler =
					_parse_line_expanded_handler;
				val_copy->destroy =
					_parse_line_expanded_destroyer;
			}
			_conf_hashtbl_insert(to_hashtbl, val_copy);
		}
	}

	return to_hashtbl;
}

/*
 * walk down a tree of s_p_values_t converting every S_P_PLAIN_STRING
 * element to an S_P_STRING element.
 */
static void _hashtbl_plain_to_string(s_p_hashtbl_t* hashtbl)
{
	_expline_values_t* v_data;
	s_p_values_t *p;
	int i, j;

	xassert(hashtbl);

	for (i = 0; i < CONF_HASH_LEN; ++i) {
		for (p = hashtbl[i]; p; p = p->next) {
			if (p->type == S_P_PLAIN_STRING) {
				p->type = S_P_STRING;
			} else if (p->type == S_P_LINE
					|| p->type == S_P_EXPLINE) {
				v_data = (_expline_values_t*)p->data;
				for (j = 0; j < p->data_count; ++j) {
					_hashtbl_plain_to_string(
							v_data->values[j]);
				}
			}
		}
	}
}

/*
 * associate a particular config element to a set of tables corresponding
 * to the expanded master keys associated.
 * The config element is either an S_P_STRING or an hostlist inside an
 * S_P_POINTER as transformed in _parse_expline_adapt_table.
 * In case the config element to process is an hostlist, the number of elements
 * must match the number of master keys otherwise an error is returned.
 * The config elements are mapped to their original S_P_* type when associated
 * with the tables using s_p_parse_pair().
 */
static int _parse_expline_doexpand(s_p_hashtbl_t** tables,
				   int tables_count,
				   s_p_values_t* item)
{
	hostlist_t item_hl, sub_item_hl;
	int item_count, i;
	int j, items_per_record, items_idx = 0;
	char* item_str = NULL;

	xassert(item);

	if (!item->data) {
		/* nothing to expand, a line may not have a key specified */
		return 1;
	}

	/* a plain string in the original s_p_options_t,
	 * copy the string as it using s_p_parse_pair() */
	if (item->type == S_P_STRING) {
		for (i = 0; i < tables_count; ++i) {
			if (!s_p_parse_pair(tables[i],
					    item->key,
					    item->data)) {
				error("parsing %s=%s.",
				      item->key, (char*)item->data);
				return 0;
			}
		}
		return 1;
	}

	/*
	 * Not a plain string in the original s_p_options_t, a temporary
	 * hostlist has been generated, parse each expanded value using
	 * s_p_parse_pair() mapping it to the right master key table.
	 *
	 * If the number of expanded value is less than the number of
	 * key tables, cycle around the expanded value in order to
	 * feed all the requested key tables (entities) with a value.
	 *
	 * If the number of expanded value m is greater than the number
	 * of key tables n (entities) and (m mod(n)) is zero, then split the
	 * set of expanded values in n consecutive sets (strings).
	 */
	item_hl = (hostlist_t)(item->data);
	item_count = hostlist_count(item_hl);
	if ((item_count < tables_count) || (item_count == 1)) {
		items_per_record = 1;
	} else if ((item_count >= tables_count) &&
		   ((item_count % tables_count) == 0)) {
		items_per_record = (int) (item_count / tables_count);
	} else {
		item_str = hostlist_ranged_string_malloc(item_hl);
		error("parsing %s=%s : count is not coherent with the"
		      " amount of records or there must be no more than"
		      " one (%d vs %d)", item->key, item_str,
		      item_count, tables_count);
		free(item_str);
		return 0;
	}

	for (i = 0; i < tables_count; ++i) {

		/* Extract the string representation of the proper value(s)
		 * from the expanded one (if not already done) */
		if (item_count > 1) {
			if (item_str)
				free(item_str);
			if (items_per_record > 1) {
				/* multiple items per table,
				 * extract the consecutive set for this table */
				item_str = hostlist_nth(item_hl, items_idx++);
				sub_item_hl = hostlist_create(item_str);
				for (j = 1; j < items_per_record; j++) {
					free(item_str);
					item_str = hostlist_nth(item_hl,
								items_idx++);
					hostlist_push_host(sub_item_hl,
							   item_str);
				}
				free(item_str);
				item_str = hostlist_ranged_string_malloc(
					sub_item_hl);
				hostlist_destroy(sub_item_hl);
			} else {
				/* one item per table,
				 * extract the right item for this table */
				item_str = hostlist_nth(item_hl, items_idx++);
			}
			if (items_idx >= item_count)
				items_idx = 0;
		} else if (item_count == 1) {
			/* only one item, extract it once for all */
			item_count--;
			item_str = hostlist_shift(item_hl);
		}

		/*
		 * The destination tables are created without any info on the
		 * operator associated with the key in s_p_parse_line_expanded.
		 * So, parse the targeted pair injecting that information to
		 * push it into the destination table.
		 */
		if (!s_p_parse_pair_with_op(tables[i], item->key, item_str,
					    item->operator)) {
			error("parsing %s=%s after expansion.", item->key,
			      item_str);
			free(item_str);
			return 0;
		}
	}

	if (item_str)
		free(item_str);
	return 1;
}

int s_p_parse_line_expanded(const s_p_hashtbl_t *hashtbl,
			    s_p_hashtbl_t*** data, int* data_count,
			    const char* key, const char* value,
			    const char *line, char **leftover)
{
	int i, status;
	s_p_hashtbl_t* strhashtbl = NULL;
	s_p_hashtbl_t** tables = NULL;
	int tables_count = 0;
	hostlist_t value_hl = NULL;
	char* value_str = NULL;
	s_p_values_t* attr = NULL;

	status = SLURM_ERROR;

	/* create the adapted temporary hash table used for expansion */
	strhashtbl = _parse_expline_adapt_table(hashtbl);

	/* create hostlist and one iterator over it, since we will walk
	 * through the list for each new attribute to create final expanded
	 * hashtables.
	 */
	value_hl = hostlist_create(value);
	xassert(value_hl);
	*data_count = tables_count = hostlist_count(value_hl);

	/* populate the temporary expansion hash table, it will map the
	 * different config elements to either an hostlist (through S_P_POINTER) or
	 * to an S_P_STRING (for original element of type S_P_PLAIN_STRING) */
	if (!s_p_parse_line(strhashtbl, *leftover, leftover)) {
		error("Unable to parse line %s", *leftover);
		goto cleanup;
	}

	/* create the hash tables of the various master keys to expand and
	 * store the first main key=value pair for each one of them.
	 *
	 * The hash tables will be used to later map the config elements
	 * from the expanded attributes to have something like :
	 * [{key: value , attr1: val1.1, attr2: val2.1},
	 *  {key: value2, attr1: val1.2, attr2: val2.2}
	 * ]
	 */
	tables = (s_p_hashtbl_t**)xmalloc(tables_count *
					  sizeof(s_p_hashtbl_t*));
	for (i = 0; i < tables_count; i++) {
		free(value_str);
		value_str = hostlist_shift(value_hl);
		tables[i] = _hashtbl_copy_keys(hashtbl,
					       false, S_P_IGNORE,
					       false, NULL,
					       false, NULL);
		_hashtbl_plain_to_string(tables[i]);
		if (!s_p_parse_pair(tables[i], key, value_str)) {
			error("Error parsing '%s = %s', most left part of the"
			      " line: %s.", key, value_str, line);
			goto cleanup;
		}
	}

	/* convert each expanded values back to its original hash table, with
	 * conversions and handlers. This is done at the same time as storing
	 * the parsed attribute values with s_p_parse_pair */
	for (i = 0; i < CONF_HASH_LEN; ++i) {
		for (attr = strhashtbl[i]; attr; attr = attr->next) {
			if (!_parse_expline_doexpand(tables,
						     tables_count,
						     attr)) {
				goto cleanup;
			}
		}
	}

	status = SLURM_SUCCESS;

cleanup:
	if (value_str)
		free(value_str);
	if (value_hl)
		hostlist_destroy(value_hl);
	if (strhashtbl)
		s_p_hashtbl_destroy(strhashtbl);

	if (status == SLURM_ERROR && tables) {
		for (i = 0; i < tables_count; i++)
			if (tables[i])
				s_p_hashtbl_destroy(tables[i]);
		xfree(tables);
	}
	else {
		*data = tables;
	}

	return status;
}

/*
 * Returns 1 if the line is parsed cleanly, and 0 otherwise.
 * Set the operator of the targeted s_p_values_t to the provided value.
 */
int s_p_parse_pair_with_op(s_p_hashtbl_t *hashtbl, const char *key,
			   const char *value, slurm_parser_operator_t opt)
{
	s_p_values_t *p;
	char *leftover, *v;

	if ((p = _conf_hashtbl_lookup(hashtbl, key)) == NULL) {
		error("%s: Parsing error at unrecognized key: %s",
		      __func__, key);
		slurm_seterrno(EINVAL);
		return 0;
	}
	if (!value) {
		error("%s: Value pointer is NULL for key %s", __func__, key);
		slurm_seterrno(EINVAL);
		return 0;
	}
	p-> operator = opt;
	/* we have value separated from key here so parse it different way */
	while (*value != '\0' && isspace(*value))
		value++; /* skip spaces at start if any */
	if (*value == '"') { /* quoted value */
		v = (char *)value + 1;
		leftover = strchr(v, '"');
		if (leftover == NULL) {
			error("Parse error in data for key %s: %s", key, value);
			slurm_seterrno(EINVAL);
			return 0;
		}
	} else { /* unqouted value */
		leftover = v = (char *)value;
		while (*leftover != '\0' && !isspace(*leftover))
			leftover++;
	}
	value = xstrndup(v, leftover - v);
	if (*leftover != '\0')
		leftover++;
	while (*leftover != '\0' && isspace(*leftover))
		leftover++; /* skip trailing spaces */
	_handle_keyvalue_match(p, value, leftover, &leftover);
	xfree(value);

	return 1;
}

/*
 * Returns 1 if the line is parsed cleanly, and 0 otherwise.
 */
int s_p_parse_pair(s_p_hashtbl_t *hashtbl, const char *key, const char *value)
{
	return s_p_parse_pair_with_op(hashtbl, key, value, S_P_OPERATOR_SET);
}

/* common checks for s_p_get_* returns NULL if invalid.
 *
 * Information concerning theses function can be found in the header file.
 */
static s_p_values_t* _get_check(slurm_parser_enum_t type,
				const char* key, const s_p_hashtbl_t* hashtbl)
{
	s_p_values_t *p;
	if (!hashtbl)
		return NULL;
	p = _conf_hashtbl_lookup(hashtbl, key);
	if (p == NULL) {
		error("Invalid key \"%s\"", key);
		return NULL;
	}
	if (p->type != type) {
		error("Key \"%s\" is not typed correctly", key);
		return NULL;
	}
	if (p->data_count == 0) {
		return NULL;
	}
	return p;
}

int s_p_get_string(char **str, const char *key, const s_p_hashtbl_t *hashtbl)
{
	s_p_values_t *p = _get_check(S_P_STRING, key, hashtbl);

	if (p) {
		*str = xstrdup((char *)p->data);
		return 1;
	}

	return 0;
}

int s_p_get_long(long *num, const char *key, const s_p_hashtbl_t *hashtbl)
{
	s_p_values_t *p = _get_check(S_P_LONG, key, hashtbl);

	if (p) {
		*num = *(long *)p->data;
		return 1;
	}

	return 0;
}

int s_p_get_uint16(uint16_t *num, const char *key,
		   const s_p_hashtbl_t *hashtbl)
{
	s_p_values_t *p = _get_check(S_P_UINT16, key, hashtbl);

	if (p) {
		*num = *(uint16_t *)p->data;
		return 1;
	}

	return 0;
}

int s_p_get_uint32(uint32_t *num, const char *key,
		   const s_p_hashtbl_t *hashtbl)
{
	s_p_values_t *p = _get_check(S_P_UINT32, key, hashtbl);

	if (p) {
		*num = *(uint32_t *)p->data;
		return 1;
	}

	return 0;
}

int s_p_get_uint64(uint64_t *num, const char *key,
		   const s_p_hashtbl_t *hashtbl)
{
	s_p_values_t *p = _get_check(S_P_UINT64, key, hashtbl);

	if (p) {
		*num = *(uint64_t *)p->data;
		return 1;
	}

	return 0;
}

int s_p_get_operator(slurm_parser_operator_t *opt, const char *key,
		     const s_p_hashtbl_t *hashtbl)
{
	s_p_values_t *p;
	if (!hashtbl)
		return 0;
	p = _conf_hashtbl_lookup(hashtbl, key);
	if (p) {
		*opt = p->operator;
		return 1;
	}
	error("Invalid key \"%s\"", key);
	return 0;
}

int s_p_get_pointer(void **ptr, const char *key, const s_p_hashtbl_t *hashtbl)
{
	s_p_values_t *p = _get_check(S_P_POINTER, key, hashtbl);

	if (p) {
		*ptr = p->data;
		return 1;
	}

	return 0;
}


int s_p_get_array(void **ptr_array[], int *count,
		  const char *key, const s_p_hashtbl_t *hashtbl)
{
	s_p_values_t *p = _get_check(S_P_ARRAY, key, hashtbl);

	if (p) {
		*ptr_array = (void **)p->data;
		*count = p->data_count;
		return 1;
	}

	return 0;
}

int s_p_get_line(s_p_hashtbl_t **ptr_array[], int *count,
		 const char *key, const s_p_hashtbl_t *hashtbl)
{
	s_p_values_t *p = _get_check(S_P_LINE, key, hashtbl);

	if (p) {
		*ptr_array = ((_expline_values_t*)p->data)->values;
		*count = p->data_count;
		return 1;
	}

	return 0;
}

int s_p_get_expline(s_p_hashtbl_t **ptr_array[], int *count,
		    const char *key, const s_p_hashtbl_t *hashtbl)
{
	s_p_values_t *p = _get_check(S_P_EXPLINE, key, hashtbl);

	if (p) {
		*ptr_array = ((_expline_values_t*)p->data)->values;
		*count = p->data_count;
		return 1;
	}

	return 0;
}

int s_p_get_boolean(bool *flag, const char *key, const s_p_hashtbl_t *hashtbl)
{
	s_p_values_t *p = _get_check(S_P_BOOLEAN, key, hashtbl);

	if (p) {
		*flag = *(bool *)p->data;
		return 1;
	}

	return 0;
}

int s_p_get_float(float *num, const char *key,
		  const s_p_hashtbl_t *hashtbl)
{
	s_p_values_t *p = _get_check(S_P_FLOAT, key, hashtbl);

	if (p) {
		*num = *(float *)p->data;
		return 1;
	}

	return 0;
}

int s_p_get_double(double *num, const char *key,
		  const s_p_hashtbl_t *hashtbl)
{
	s_p_values_t *p = _get_check(S_P_DOUBLE, key, hashtbl);

	if (p) {
		*num = *(double *)p->data;
		return 1;
	}

	return 0;
}

int s_p_get_long_double(long double *num, const char *key,
			const s_p_hashtbl_t *hashtbl)
{
	s_p_values_t *p = _get_check(S_P_LONG_DOUBLE, key, hashtbl);

	if (p) {
		*num = *(long double *)p->data;
		return 1;
	}

	return 0;
}

/*
 * Given an "options" array, print the current values of all
 * options in supplied hash table "hashtbl".
 *
 * Primarily for debugging purposes.
 */
void s_p_dump_values(const s_p_hashtbl_t *hashtbl,
		     const s_p_options_t options[])
{
	const s_p_options_t *op = NULL;
	long num;
	uint16_t num16;
	uint32_t num32;
	uint64_t num64;
	float numf;
	double numd;
	long double numld;
	char *str;
	void *ptr;
	void **ptr_array;
	int count;
	bool flag;

	for (op = options; op->key != NULL; op++) {
		switch(op->type) {
		case S_P_STRING:
		case S_P_PLAIN_STRING:
			if (s_p_get_string(&str, op->key, hashtbl)) {
				verbose("%s = %s", op->key, str);
				xfree(str);
			} else {
				verbose("%s", op->key);
			}
			break;
		case S_P_LONG:
			if (s_p_get_long(&num, op->key, hashtbl))
				verbose("%s = %ld", op->key, num);
			else
				verbose("%s", op->key);
			break;
		case S_P_UINT16:
			if (s_p_get_uint16(&num16, op->key, hashtbl))
				verbose("%s = %hu", op->key, num16);
			else
				verbose("%s", op->key);
			break;
		case S_P_UINT32:
			if (s_p_get_uint32(&num32, op->key, hashtbl))
				verbose("%s = %u", op->key, num32);
			else
				verbose("%s", op->key);
			break;
		case S_P_UINT64:
			if (s_p_get_uint64(&num64, op->key, hashtbl))
				verbose("%s = %"PRIu64"", op->key, num64);
			else
				verbose("%s", op->key);
			break;
		case S_P_POINTER:
			if (s_p_get_pointer(&ptr, op->key, hashtbl))
				verbose("%s = %zx", op->key, (size_t)ptr);
			else
				verbose("%s", op->key);
			break;
		case S_P_LINE:
			if (s_p_get_line((s_p_hashtbl_t***)&ptr_array,
					 &count, op->key, hashtbl)) {
				verbose("%s, count = %d", op->key, count);
			} else {
				verbose("%s", op->key);
			}
			break;
		case S_P_EXPLINE:
			if (s_p_get_expline((s_p_hashtbl_t***)&ptr_array,
					    &count, op->key, hashtbl)) {
				verbose("%s, count = %d", op->key, count);
			} else {
				verbose("%s", op->key);
			}
			break;
		case S_P_ARRAY:
			if (s_p_get_array(&ptr_array, &count,
					  op->key, hashtbl)) {
				verbose("%s, count = %d", op->key, count);
			} else {
				verbose("%s", op->key);
			}
			break;
		case S_P_BOOLEAN:
			if (s_p_get_boolean(&flag, op->key, hashtbl)) {
				verbose("%s = %s", op->key,
					flag ? "TRUE" : "FALSE");
			} else {
				verbose("%s", op->key);
			}
			break;
		case S_P_FLOAT:
			if (s_p_get_float(&numf, op->key, hashtbl))
				verbose("%s = %f", op->key, numf);
			else
				verbose("%s", op->key);
			break;
		case S_P_DOUBLE:
			if (s_p_get_double(&numd, op->key, hashtbl))
				verbose("%s = %f", op->key, numd);
			else
				verbose("%s", op->key);
			break;
		case S_P_LONG_DOUBLE:
			if (s_p_get_long_double(&numld, op->key, hashtbl))
				verbose("%s = %Lf", op->key, numld);
			else
				verbose("%s", op->key);
			break;
		case S_P_IGNORE:
			break;
		}
	}
}

extern void transfer_s_p_options(s_p_options_t **full_options,
				 s_p_options_t *options,
				 int *full_options_cnt)
{
	s_p_options_t *op = NULL;
	s_p_options_t *full_options_ptr;
	int cnt = *full_options_cnt;

	xassert(full_options);

	for (op = options; op->key != NULL; op++, cnt++) {
		xrealloc(*full_options, ((cnt + 1) * sizeof(s_p_options_t)));
		full_options_ptr = &(*full_options)[cnt];
		memcpy(full_options_ptr, op, sizeof(s_p_options_t));
		full_options_ptr->key = xstrdup(op->key);
	}
	*full_options_cnt = cnt;
}
