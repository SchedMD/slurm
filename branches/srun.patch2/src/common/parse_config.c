/*****************************************************************************\
 *  parse_config.c - parse any slurm.conf-like configuration file
 *
 *  NOTE: when you see the prefix "s_p_", think "slurm parser".
 *****************************************************************************
 *  Copyright (C) 2006-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Christopher J. Morrone <morrone2@llnl.gov>.
 *  CODE-OCEC-09-009. All rights reserved.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>
#include <sys/types.h>
#include <regex.h>
#include <stdint.h>
#include <stdlib.h>
#include <ctype.h>

/* #include "src/common/slurm_protocol_defs.h" */
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/xassert.h"
/* #include "src/common/slurm_rlimits_info.h" */
#include "src/common/parse_config.h"

#include <slurm/slurm.h>

#define BUFFER_SIZE 4096

#define CONF_HASH_LEN 26

static regex_t keyvalue_re;
static char *keyvalue_pattern =
	"^[[:space:]]*"
	"([[:alnum:]]+)" /* key */
	"[[:space:]]*=[[:space:]]*"
	"((\"([^\"]*)\")|([^[:space:]]+))" /* value: quoted with whitespace,
					    * or unquoted and no whitespace */
	"([[:space:]]|$)";
static bool keyvalue_initialized = false;

struct s_p_values {
	char *key;
	int type;
	int data_count;
	void *data;
	int (*handler)(void **data, slurm_parser_enum_t type,
		       const char *key, const char *value,
		       const char *line, char **leftover);
	void (*destroy)(void *data);
	s_p_values_t *next;
};

/*
 * NOTE - "key" is case insensitive.
 */
static int _conf_hashtbl_index(const char *key)
{
	int i;
	int idx = 0;

	xassert(key);
	for (i = 0; i < 10; i++) {
		if (key[i] == '\0')
			break;
		idx += tolower(key[i]);
	}
	return idx % CONF_HASH_LEN;
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
		if (strcasecmp(p->key, key) == 0)
			return p;
	}
	return NULL;
}

s_p_hashtbl_t *s_p_hashtbl_create(
	s_p_options_t options[])
{
	s_p_options_t *op = NULL;
	s_p_values_t *value = NULL;
	s_p_hashtbl_t *hashtbl = NULL;
	int len;

	len = CONF_HASH_LEN * sizeof(s_p_values_t *);
	hashtbl = (s_p_hashtbl_t *)xmalloc(len);
	memset(hashtbl, 0, len);
					      
	for (op = options; op->key != NULL; op++) {
		value = xmalloc(sizeof(s_p_values_t));
		value->key = xstrdup(op->key);
		value->type = op->type;
		value->data_count = 0;
		value->data = NULL;
		value->next = NULL;
		value->handler = op->handler;
		value->destroy = op->destroy;
		_conf_hashtbl_insert(hashtbl, value);
	}

	return hashtbl;
}

static void _conf_file_values_free(s_p_values_t *p)
{
	int i;

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

	for (i = 0; i < CONF_HASH_LEN; i++) {
		for (p = hashtbl[i]; p != NULL; p = next) {
			next = p->next;
			_conf_file_values_free(p);
		}
	}
	xfree(hashtbl);
}

static void _keyvalue_regex_init(void)
{
	if (!keyvalue_initialized) {
		if (regcomp(&keyvalue_re, keyvalue_pattern,
			    REG_EXTENDED) != 0) {
			/* FIXME - should be fatal? */
			error("keyvalue regex compilation failed\n");
		}
		keyvalue_initialized = true;
	}
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
			   char **key, char **value, char **remaining)
{
	size_t nmatch = 8;
	regmatch_t pmatch[8];

	*key = NULL;
	*value = NULL;
	*remaining = (char *)line;
	memset(pmatch, 0, sizeof(regmatch_t)*nmatch);
	
	if (regexec(&keyvalue_re, line, nmatch, pmatch, 0)
	    == REG_NOMATCH) {
		return -1;
	}

	*key = (char *)(xstrndup(line + pmatch[1].rm_so,
				 pmatch[1].rm_eo - pmatch[1].rm_so));

	if (pmatch[4].rm_so != -1) {
		*value = (char *)(xstrndup(line + pmatch[4].rm_so,
					   pmatch[4].rm_eo - pmatch[4].rm_so));
	} else if (pmatch[5].rm_so != -1) {
		*value = (char *)(xstrndup(line + pmatch[5].rm_so,
					   pmatch[5].rm_eo - pmatch[5].rm_so));
	} else {
		*value = xstrdup("");
	}

	*remaining = (char *)(line + pmatch[2].rm_eo);

	return 0;
}

static int _strip_continuation(char *buf, int len)
{
	char *ptr;
	int bs = 0;

	for (ptr = buf+len-1; ptr >= buf; ptr--) {
		if (*ptr == '\\')
			bs++;
		else if (isspace(*ptr) && bs == 0)
			continue;
		else
			break;
	}
	/* Check for an odd number of contiguous backslashes at
	   the end of the line */
	if (bs % 2 == 1) {
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

/*
 * Reads the next line from the "file" into buffer "buf".
 *
 * Concatonates together lines that are continued on
 * the next line by a trailing "\".  Strips out comments,
 * replaces escaped "\#" with "#", and replaces "\\" with "\".
 */
static int _get_next_line(char *buf, int buf_size, FILE *file)
{
	char *ptr = buf;
	int leftover = buf_size;
	int read_size, new_size;
	int lines = 0;

	while (fgets(ptr, leftover, file)) {
		lines++;
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

static int _handle_string(s_p_values_t *v,
			  const char *value, const char *line, char **leftover)
{
	if (v->data_count != 0) {
		debug("%s specified more than once", v->key);
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
		v->data = xstrdup(value);
	}

	v->data_count = 1;
	return 1;
}

static int _handle_long(s_p_values_t *v,
			const char *value, const char *line, char **leftover)
{
	if (v->data_count != 0) {
		debug("%s specified more than once", v->key);
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
		char *endptr;
		long num;
		errno = 0;
		num = strtol(value, &endptr, 0);
		if ((num == 0 && errno == EINVAL)
		    || (*endptr != '\0')) {
			if (strcasecmp(value, "UNLIMITED") == 0
			    || strcasecmp(value, "INFINITE") == 0) {
				num = (long) INFINITE;
			} else {
				error("\"%s\" is not a valid number", value);
				return -1;
			}
		} else if (errno == ERANGE) {
			error("\"%s\" is out of range", value);
			return -1;
		}
		v->data = xmalloc(sizeof(long));
		*(long *)v->data = num;
	}

	v->data_count = 1;
	return 1;
}

static int _handle_uint16(s_p_values_t *v,
			  const char *value, const char *line, char **leftover)
{
	if (v->data_count != 0) {
		debug("%s specified more than once", v->key);
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
		char *endptr;
		unsigned long num;

		errno = 0;
		num = strtoul(value, &endptr, 0);
		if ((num == 0 && errno == EINVAL)
		    || (*endptr != '\0')) {
			if (strcasecmp(value, "UNLIMITED") == 0
			    || strcasecmp(value, "INFINITE") == 0) {
				num = (uint16_t) INFINITE;
			} else {
				error("%s value \"%s\" is not a valid number", 
					v->key, value);
				return -1;
			}
		} else if (errno == ERANGE) {
			error("%s value (%s) is out of range", v->key, value);
			return -1;
		} else if (value[0] == '-') {
			error("%s value (%s) is less than zero", v->key, value);
			return -1;
		} else if (num > 0xffff) {
			error("%s value (%s) is greater than 65535", v->key,
			      value);
			return -1;
		}
		v->data = xmalloc(sizeof(uint16_t));
		*(uint16_t *)v->data = (uint16_t)num;
	}

	v->data_count = 1;
	return 1;
}

static int _handle_uint32(s_p_values_t *v,
			  const char *value, const char *line, char **leftover)
{
	if (v->data_count != 0) {
		debug("%s specified more than once", v->key);
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
		char *endptr;
		unsigned long num;

		errno = 0;
		num = strtoul(value, &endptr, 0);
		if ((endptr[0] == 'k') || (endptr[0] == 'K')) {
			num *= 1024;
			endptr++;
		}
		if ((num == 0 && errno == EINVAL)
		    || (*endptr != '\0')) {
			if ((strcasecmp(value, "UNLIMITED") == 0) ||
			    (strcasecmp(value, "INFINITE")  == 0)) {
				num = (uint32_t) INFINITE;
			} else {
				error("%s value (%s) is not a valid number", 
					v->key, value);
				return -1;
			}
		} else if (errno == ERANGE) {
			error("%s value (%s) is out of range", v->key, value);
			return -1;
		} else if (value[0] == '-') {
			error("%s value (%s) is less than zero", v->key, value);
			return -1;
		} else if (num > 0xffffffff) {
			error("%s value (%s) is greater than 4294967295", 
				v->key, value);
			return -1;
		}
		v->data = xmalloc(sizeof(uint32_t));
		*(uint32_t *)v->data = (uint32_t)num;
	}

	v->data_count = 1;
	return 1;
}

static int _handle_pointer(s_p_values_t *v,
			   const char *value, const char *line, char **leftover)
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
			debug("%s specified more than once", v->key);
			xfree(v->data);
			v->data_count = 0;
		}
		v->data = xstrdup(value);
	}

	v->data_count = 1;
	return 1;
}

static int _handle_array(s_p_values_t *v,
			 const char *value, const char *line, char **leftover)
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

static int _handle_boolean(s_p_values_t *v,
			   const char *value, const char *line, char **leftover)
{
	if (v->data_count != 0) {
		debug("%s specified more than once", v->key);
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
		bool flag;

		if (!strcasecmp(value, "yes")
		    || !strcasecmp(value, "up")
		    || !strcasecmp(value, "1")) {
			flag = true;
		} else if (!strcasecmp(value, "no")
			   || !strcasecmp(value, "down")
			   || !strcasecmp(value, "0")) {
			flag = false;
		} else {
			error("\"%s\" is not a valid option for \"%s\"",
			      value, v->key);
			return -1;
		}

		v->data = xmalloc(sizeof(bool));
		*(bool *)v->data = flag;
	}

	v->data_count = 1;
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
/* 	debug3("key = %s, value = %s, line = \"%s\"", */
/* 	       v->key, value, line); */
	switch (v->type) {
	case S_P_IGNORE:
		/* do nothing */
		break;
	case S_P_STRING:
		_handle_string(v, value, line, leftover);
		break;
	case S_P_LONG:
		_handle_long(v, value, line, leftover);
		break;
	case S_P_UINT16:
		_handle_uint16(v, value, line, leftover);
		break;
	case S_P_UINT32:
		_handle_uint32(v, value, line, leftover);
		break;
	case S_P_POINTER:
		_handle_pointer(v, value, line, leftover);
		break;
	case S_P_ARRAY:
		_handle_array(v, value, line, leftover);
		break;
	case S_P_BOOLEAN:
		_handle_boolean(v, value, line, leftover);
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
		if (!isspace(line[i]))
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

	_keyvalue_regex_init();

	while (_keyvalue_regex(ptr, &key, &value, &new_leftover) == 0) {
		if ((p = _conf_hashtbl_lookup(hashtbl, key))) {
			_handle_keyvalue_match(p, value,
					       new_leftover, &new_leftover);
			*leftover = ptr = new_leftover;
		} else {
			error("Parsing error at unrecognized key: %s", key);
			xfree(key);
			xfree(value);
			return 0;
		}
		xfree(key);
		xfree(value);
	}

	return 1;
}

/*
 * Returns 1 if the line is parsed cleanly, and 0 otherwise.
 */
static int _parse_next_key(s_p_hashtbl_t *hashtbl,
			   const char *line, char **leftover)
{
	char *key, *value;
	s_p_values_t *p;
	char *new_leftover;

	_keyvalue_regex_init();

	if (_keyvalue_regex(line, &key, &value, &new_leftover) == 0) {
		if ((p = _conf_hashtbl_lookup(hashtbl, key))) {
			_handle_keyvalue_match(p, value,
					       new_leftover, &new_leftover);
			*leftover = new_leftover;
		} else {
			error("Parsing error at unrecognized key: %s", key);
			xfree(key);
			xfree(value);
			*leftover = (char *)line;
			return 0;
		}
		xfree(key);
		xfree(value);
	} else {
		*leftover = (char *)line;
	}

	return 1;
}

/*
 * Returns 1 if the line contained an include directive and the included
 * file was parsed without error.  Returns -1 if the line was an include
 * directive but the included file contained errors.  Returns 0 if
 * no include directive is found.
 */
static int _parse_include_directive(s_p_hashtbl_t *hashtbl,
				    const char *line, char **leftover)
{
	char *ptr;
	char *fn_start, *fn_stop;
	char *filename;

	*leftover = NULL;
	if (strncasecmp("include", line, strlen("include")) == 0) {
		ptr = (char *)line + strlen("include");
		if (!isspace(*ptr))
			return 0;
		while (isspace(*ptr))
			ptr++;
		fn_start = ptr;
		while (!isspace(*ptr))
			ptr++;
		fn_stop = *leftover = ptr;
		filename = xstrndup(fn_start, fn_stop-fn_start);
		if (s_p_parse_file(hashtbl, filename) == SLURM_SUCCESS) {
			xfree(filename);
			return 1;
		} else {
			xfree(filename);
			return -1;
		}
	} else {
		return 0;
	}
}

int s_p_parse_file(s_p_hashtbl_t *hashtbl, char *filename)
{
	FILE *f;
	char line[BUFFER_SIZE];
	char *leftover = NULL;
	int rc = SLURM_SUCCESS;
	int line_number;
	int merged_lines;
	int inc_rc;

	if(!filename) {
		error("s_p_parse_file: No filename given.");
		return SLURM_ERROR;
	}
	
	_keyvalue_regex_init();
	
	f = fopen(filename, "r");
	if (f == NULL) {
		error("s_p_parse_file: unable to read \"%s\": %m",
		      filename);
		return SLURM_ERROR;
	}

	line_number = 1;
	while((merged_lines = _get_next_line(line, BUFFER_SIZE, f)) > 0) {
		/* skip empty lines */
		if (line[0] == '\0') {
			line_number += merged_lines;
			continue;
		}

		inc_rc = _parse_include_directive(hashtbl, line, &leftover);
		if (inc_rc == 0) {
			_parse_next_key(hashtbl, line, &leftover);
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
			error("Parse error in file %s line %d: \"%s\"",
			      filename, line_number, ptr);
			xfree(ptr);
			rc = SLURM_ERROR;
		}
		line_number += merged_lines;
	}

	fclose(f);
	return rc;
}

/*
 * s_p_get_string
 *
 * Search for a key in a s_p_hashtbl_t with value of type
 * string.  If the key is found and has a set value, the
 * value is retuned in "str".
 *
 * OUT str - pointer to a copy of the string value
 *           (caller is resonsible for freeing str with xfree())
 * IN key - hash table key.
 * IN hashtbl - hash table created by s_p_hashtbl_create()
 *
 * Returns 1 when a value was set for "key" during parsing and "str"
 *   was successfully set, otherwise returns 0;
 *
 * NOTE: Caller is responsible for freeing the returned string with xfree!
 */
int s_p_get_string(char **str, const char *key, const s_p_hashtbl_t *hashtbl)
{
	s_p_values_t *p;

	if (!hashtbl)
		return 0;
	p = _conf_hashtbl_lookup(hashtbl, key);
	if (p == NULL) {
		error("Invalid key \"%s\"", key);
		return 0;
	}
	if (p->type != S_P_STRING) {
		error("Key \"%s\" is not a string\n", key);
		return 0;
	}
	if (p->data_count == 0) {
		return 0;
	}

	*str = xstrdup((char *)p->data);

	return 1;
}

/*
 * s_p_get_long
 *
 * Search for a key in a s_p_hashtbl_t with value of type
 * long.  If the key is found and has a set value, the
 * value is retuned in "num".
 *
 * OUT num - pointer to a long where the value is returned
 * IN key - hash table key
 * IN hashtbl - hash table created by s_p_hashtbl_create()
 *
 * Returns 1 when a value was set for "key" during parsing and "num"
 *   was successfully set, otherwise returns 0;
 */
int s_p_get_long(long *num, const char *key, const s_p_hashtbl_t *hashtbl)
{
	s_p_values_t *p;

	if (!hashtbl)
		return 0;
	p = _conf_hashtbl_lookup(hashtbl, key);
	if (p == NULL) {
		error("Invalid key \"%s\"", key);
		return 0;
	}
	if (p->type != S_P_LONG) {
		error("Key \"%s\" is not a long\n", key);
		return 0;
	}
	if (p->data_count == 0) {
		return 0;
	}

	*num = *(long *)p->data;

	return 1;
}

/*
 * s_p_get_uint16
 *
 * Search for a key in a s_p_hashtbl_t with value of type
 * uint16.  If the key is found and has a set value, the
 * value is retuned in "num".
 *
 * OUT num - pointer to a uint16_t where the value is returned
 * IN key - hash table key
 * IN hashtbl - hash table created by s_p_hashtbl_create()
 *
 * Returns 1 when a value was set for "key" during parsing and "num"
 *   was successfully set, otherwise returns 0;
 */
int s_p_get_uint16(uint16_t *num, const char *key,
		   const s_p_hashtbl_t *hashtbl)
{
	s_p_values_t *p;

	if (!hashtbl)
		return 0;
	p = _conf_hashtbl_lookup(hashtbl, key);
	if (p == NULL) {
		error("Invalid key \"%s\"", key);
		return 0;
	}
	if (p->type != S_P_UINT16) {
		error("Key \"%s\" is not a uint16_t\n", key);
		return 0;
	}
	if (p->data_count == 0) {
		return 0;
	}

	*num = *(uint16_t *)p->data;

	return 1;
}

/*
 * s_p_get_uint32
 *
 * Search for a key in a s_p_hashtbl_t with value of type
 * uint32.  If the key is found and has a set value, the
 * value is retuned in "num".
 *
 * OUT num - pointer to a uint32_t where the value is returned
 * IN key - hash table key
 * IN hashtbl - hash table created by s_p_hashtbl_create()
 *
 * Returns 1 when a value was set for "key" during parsing and "num"
 *   was successfully set, otherwise returns 0;
 */
int s_p_get_uint32(uint32_t *num, const char *key,
		   const s_p_hashtbl_t *hashtbl)
{
	s_p_values_t *p;

	if (!hashtbl)
		return 0;
	p = _conf_hashtbl_lookup(hashtbl, key);
	if (p == NULL) {
		error("Invalid key \"%s\"", key);
		return 0;
	}
	if (p->type != S_P_UINT32) {
		error("Key \"%s\" is not a uint32_t\n", key);
		return 0;
	}
	if (p->data_count == 0) {
		return 0;
	}

	*num = *(uint32_t *)p->data;

	return 1;
}

/*
 * s_p_get_pointer
 *
 * Search for a key in a s_p_hashtbl_t with value of type
 * pointer.  If the key is found and has a set value, the
 * value is retuned in "ptr".
 *
 * OUT ptr - pointer to a void pointer where the value is returned
 * IN key - hash table key
 * IN hashtbl - hash table created by s_p_hashtbl_create()
 *
 * Returns 1 when a value was set for "key" during parsing and "ptr"
 *   was successfully set, otherwise returns 0;
 */
int s_p_get_pointer(void **ptr, const char *key, const s_p_hashtbl_t *hashtbl)
{
	s_p_values_t *p;

	if (!hashtbl)
		return 0;
	p = _conf_hashtbl_lookup(hashtbl, key);
	if (p == NULL) {
		error("Invalid key \"%s\"", key);
		return 0;
	}
	if (p->type != S_P_POINTER) {
		error("Key \"%s\" is not a pointer\n", key);
		return 0;
	}
	if (p->data_count == 0) {
		return 0;
	}

	*ptr = p->data;

	return 1;
}


/*
 * s_p_get_array
 *
 * Most s_p_ data types allow a key to appear only once in a file
 * (s_p_parse_file) or line (s_p_parse_line).  S_P_ARRAY is the exception.
 *
 * S_P_ARRAY allows a key to appear any number of times.  Each time
 * a particular key is found the value array grows by one element, and
 * that element contains a pointer to the newly parsed value.  You can
 * think of this as being an array of S_P_POINTER types.
 *
 * OUT ptr_array - pointer to a void pointer-pointer where the value is returned
 * OUT count - length of ptr_array
 * IN key - hash table key
 * IN hashtbl - hash table created by s_p_hashtbl_create()
 *
 * Returns 1 when a value was set for "key" during parsing and both
 *   "ptr_array" and "count" were successfully set, otherwise returns 0.
 */
int s_p_get_array(void **ptr_array[], int *count, 
		  const char *key, const s_p_hashtbl_t *hashtbl)
{
	s_p_values_t *p;

	if (!hashtbl)
		return 0;
	p = _conf_hashtbl_lookup(hashtbl, key);
	if (p == NULL) {
		error("Invalid key \"%s\"", key);
		return 0;
	}
	if (p->type != S_P_ARRAY) {
		error("Key \"%s\" is not an array\n", key);
		return 0;
	}
	if (p->data_count == 0) {
		return 0;
	}

	*ptr_array = (void **)p->data;
	*count = p->data_count;

	return 1;
}

/*
 * s_p_get_boolean
 *
 * Search for a key in a s_p_hashtbl_t with value of type
 * boolean.  If the key is found and has a set value, the
 * value is retuned in "flag".
 *
 * OUT flag - pointer to a bool where the value is returned
 * IN key - hash table key
 * IN hashtbl - hash table created by s_p_hashtbl_create()
 *
 * Returns 1 when a value was set for "key" during parsing and "num"
 *   was successfully set, otherwise returns 0;
 */
int s_p_get_boolean(bool *flag, const char *key, const s_p_hashtbl_t *hashtbl)
{
	s_p_values_t *p;

	if (!hashtbl)
		return 0;
	p = _conf_hashtbl_lookup(hashtbl, key);
	if (p == NULL) {
		error("Invalid key \"%s\"", key);
		return 0;
	}
	if (p->type != S_P_BOOLEAN) {
		error("Key \"%s\" is not a boolean\n", key);
		return 0;
	}
	if (p->data_count == 0) {
		return 0;
	}

	*flag = *(bool *)p->data;

	return 1;
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
	char *str;
	void *ptr;
	void **ptr_array;
	int count;
	bool flag;

	for (op = options; op->key != NULL; op++) {
		switch(op->type) {
		case S_P_STRING:
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
		case S_P_POINTER:
			if (s_p_get_pointer(&ptr, op->key, hashtbl))
				verbose("%s = %x", op->key, ptr);
			else
				verbose("%s", op->key);
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
		case S_P_IGNORE:
			break;
		}
	}
}
