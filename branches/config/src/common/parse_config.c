/*****************************************************************************\
 *  parse_config.c - parse any slurm.conf-like configuration file
 *
 *  $Id: read_config.c 7121 2006-01-30 18:09:40Z jette $
 *****************************************************************************
 *  Copyright (C) 2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Christopher J. Morrone <morrone2@llnl.gov>.
 *  UCRL-CODE-217948.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

/* #include "src/common/hostlist.h" */
/* #include "src/common/slurm_protocol_defs.h" */
/* #include "src/common/log.h" */
/* #include "src/common/macros.h" */
/* #include "src/common/parse_spec.h" */
/* #include "src/common/read_config.h" */
/* #include "src/common/xmalloc.h" */
/* #include "src/common/xstring.h" */
/* #include "src/common/slurm_rlimits_info.h" */

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

#include <sys/types.h>
#include <regex.h>

#include <string.h>
#include <assert.h>
#include <slurm/slurm.h>

#include "parse_config.h"

#define BUFFER_SIZE 4096
#define PARSE_DEBUG 1

/* FIXME - change all malloc/free to xmalloc/xfree */
/* FIXME - change all assert to xassert */
/* FIXME - change all strdup to xstrdup */

#define CONF_HASH_LEN 26

static regex_t keyvalue_re;
static char *keyvalue_pattern =
	"(^|[[:space:]])([[:alpha:]]+)"
	"[[:space:]]*=[[:space:]]*"
	"([[:graph:]]+)([[:space:]]|$)";
static bool keyvalue_initialized = false;

struct conf_file_values {
	char *key;
	int type;
	int data_count;
	void *data;
	conf_file_values_t *next;
	int (*handler)(void **, slurm_conf_enum_t,
		       const char *, const char *, const char *);
	void (*destroy)(void *);
};

/*
 * NOTE - "key" is case insensitive.
 */
static int _conf_hashtbl_index(const char *key)
{
	int i;
	int idx = 0;

	assert(key);
	for (i = 0; i < 10; i++) {
		if (key[i] == '\0')
			break;
		idx += tolower(key[i]);
	}
	return idx % CONF_HASH_LEN;
}

static void _conf_hashtbl_insert(s_c_hashtbl_t *hashtbl,
				 conf_file_values_t *value)
{
	int idx;

	assert(value);
	idx = _conf_hashtbl_index(value->key);
	value->next = hashtbl[idx];
	hashtbl[idx] = value;
}

/*
 * NOTE - "key" is case insensitive.
 */
static conf_file_values_t *_conf_hashtbl_lookup(
	const s_c_hashtbl_t *hashtbl, const char *key)
{
	int idx;
	conf_file_values_t *p;

	assert(key);
	idx = _conf_hashtbl_index(key);
	for (p = hashtbl[idx]; p != NULL; p = p->next) {
		if (strcasecmp(p->key, key) == 0)
			return p;
	}
	return NULL;
}

s_c_hashtbl_t *s_c_hashtbl_create(
	struct conf_file_options options[])
{
	struct conf_file_options *op = NULL;
	conf_file_values_t *value;
	s_c_hashtbl_t *hashtbl;
	int len;

	len = CONF_HASH_LEN * sizeof(conf_file_values_t *);
	hashtbl = (s_c_hashtbl_t *)malloc(len);
	memset(hashtbl, 0, len);
					      
	for (op = options; op->key != NULL; op++) {
		value = malloc(sizeof(conf_file_values_t));
		value->key = strdup(op->key);
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

static void _conf_file_values_free(conf_file_values_t *p)
{
	int i;

	if (p->data_count > 0) {
		switch(p->type) {
		case S_C_ARRAY:
			for (i = 0; i < p->data_count; i++) {
				void **ptr_array = (void **)p->data;
				if (p->destroy != NULL) {
					p->destroy(ptr_array[i]);
				} else {
					free(ptr_array[i]);
				}
			}
			free(p->data);
			break;
		default:
			if (p->destroy != NULL) {
				p->destroy(p->data);
			} else {
				free(p->data);
			}
			break;
		}
	}
	free(p->key);
	free(p);
}

void s_c_hashtbl_destroy(s_c_hashtbl_t *hashtbl) {
	int i;
	conf_file_values_t *p, *next;

	for (i = 0; i < CONF_HASH_LEN; i++) {
		for (p = hashtbl[i]; p != NULL; p = next) {
			next = p->next;
			_conf_file_values_free(p);
		}
	}
	free(hashtbl);
}

static void _keyvalue_regex_init(void)
{
	if (!keyvalue_initialized) {
		if (regcomp(&keyvalue_re, keyvalue_pattern,
			    REG_EXTENDED) != 0) {
			/* FIXME - should be fatal */
			error("keyvalue regex compilation failed\n");
		}
		keyvalue_initialized = true;
	}
}

/*
 * IN line - string to be search for a key=value pair
 * OUT key - pointer to the key string (must be freed by caller)
 * OUT value - pointer to the value string (must be freed by caller)
 * OUT remaining - pointer into the "line" string denoting the start
 *                 of the unsearched portion of the string
 * Returns 0 when a key-value pair is found, and -1 otherwise.
 */
static int _keyvalue_regex(const char *line,
			   char **key, char **value, char **remaining)
{
        size_t nmatch = 5;
        regmatch_t pmatch[5];
	char *start;
	size_t len;
	char *match;

	memset(pmatch, 0, sizeof(regmatch_t)*nmatch);
	if (regexec(&keyvalue_re, line, nmatch, pmatch, 0)
	    == REG_NOMATCH) {
		return -1;
	}
	
	*key = (char *)(strndup(line + pmatch[2].rm_so,
				pmatch[2].rm_eo - pmatch[2].rm_so));
	*value = (char *)(strndup(line + pmatch[3].rm_so,
				  pmatch[3].rm_eo - pmatch[3].rm_so));
	*remaining = (char *)(line + pmatch[3].rm_eo);
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
	int eof = 1;

	while (fgets(ptr, leftover, file)) {
		eof = 0;
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
	/*_strip_cr_nl(buf);*/ /* not necessary */
	_strip_escapes(buf);
	
	return !eof;
}

static int _handle_string(conf_file_values_t *v,
			  const char *value, const char *line)
{
	if (v->data_count != 0) {
		fprintf(stderr, "%s specified more than once\n", v->key);
		return -1;
	}

	if (v->handler != NULL) {
		/* call the handler function */
		/* v->data_count = 1; */
	} else {
		v->data = strdup(value);
		v->data_count = 1;
	}

	return 0;
}

static int _handle_long(conf_file_values_t *v,
		       const char *value, const char *line)
{
	if (v->data_count != 0) {
		fprintf(stderr, "%s specified more than once\n", v->key);
		return -1;
	}

	if (v->handler != NULL) {
		/* call the handler function */
		/* v->data_count = 1; */
	} else {
		char *endptr;
		long num;
		errno = 0;
		num = strtol(value, &endptr, 0);
		if ((num == 0 && errno == EINVAL)
		    || (*endptr != '\0')) {
			fprintf(stderr, "\"%s\" is not a valid number\n", value);
			return -1;
		} else if (errno == ERANGE) {
			fprintf(stderr, "\"%s\" is out of range\n", value);
			return -1;
		}
		v->data = malloc(sizeof(long));
		/* printf("\"%ld\"\n", num); */
		*(long *)v->data = num;
		v->data_count = 1;
	}

	return 0;
}

static int _handle_pointer(conf_file_values_t *v,
			   const char *value, const char *line)
{
	if (v->data_count != 0) {
		fprintf(stderr, "%s specified more than once\n", v->key);
		return -1;
	}

	if (v->handler != NULL) {
		/* call the handler function */
		/* v->data_count = 1; */
	} else {
		v->data = strdup(value);
		v->data_count = 1;
	}

	return 0;
}

static int _handle_array(conf_file_values_t *v,
			 const char *value, const char *line)
{
	void *new_ptr;
	void **data;

	if (v->handler != NULL) {
		/* call the handler function */
		if (v->handler(&new_ptr, v->type, v->key, value, line) != 0)
			return -1;
	} else {
		new_ptr = strdup(value);
	}
	v->data_count += 1;
	v->data = realloc(v->data, (v->data_count)*sizeof(void *));
	data = &((void**)v->data)[v->data_count-1];
	*data = new_ptr;

	return 0;
}

static void _handle_keyvalue_match(conf_file_values_t *v,
				   const char *value, const char *line)
{
	/* printf("key = %s, value = %s, line = \"%s\"\n", */
	/*        v->key, value, line); */
	switch (v->type) {
	case S_C_STRING:
		_handle_string(v, value, line);
		break;
	case S_C_LONG:
		_handle_long(v, value, line);
		break;
	case S_C_POINTER:
		_handle_pointer(v, value, line);
		break;
	case S_C_ARRAY:
		_handle_array(v, value, line);
		break;
	}
}

void s_c_parse_file(s_c_hashtbl_t *hashtbl, char *filename)
{
	FILE *f;
	char line[BUFFER_SIZE];
	char *key, *value, *leftover;

	_keyvalue_regex_init();

	f = fopen(filename, "r");

	while(_get_next_line(line, BUFFER_SIZE, f)) {
		/* skip empty lines */
		if (line[0] == '\0')
			continue;
		/* printf("line = \"%s\"\n", line); */

		if (_keyvalue_regex(line, &key, &value, &leftover) == 0) {
			conf_file_values_t *p;

			if (p = _conf_hashtbl_lookup(hashtbl, key)) {
				_handle_keyvalue_match(p, value, line);
			} else {
				fprintf(stderr, "UNRECOGNIZED KEY %s!\n", key);
				exit(1);
			}
			free(key);
			free(value);
		}
	}

	fclose(f);
}

void s_c_parse_line(s_c_hashtbl_t *hashtbl, const char *line)
{
	char *key, *value, *leftover;
	const char *ptr = line;
	conf_file_values_t *p;

	_keyvalue_regex_init();

	while (_keyvalue_regex(ptr, &key, &value, &leftover) == 0) {
		if (p = _conf_hashtbl_lookup(hashtbl, key)) {
			_handle_keyvalue_match(p, value, leftover);
			ptr = leftover;
		} else {
			fprintf(stderr, "UNRECOGNIZED KEY %s!\n", key);
			/* FIXME - should return error, not exit */
			exit(1);
		}
		free(key);
		free(value);
	}
}

int s_c_get_string(const s_c_hashtbl_t *hashtbl, const char *key, char **str)
{
	conf_file_values_t *p;

	p = _conf_hashtbl_lookup(hashtbl, key);
	if (p == NULL) {
		fprintf(stderr, "Invalid key \"%s\"\n", key);
		return -1;
	}
	if (p->data_count == 0) {
		return -1;
	}

	*str = strdup((char *)p->data);

	return 0;
}

int s_c_get_long(const s_c_hashtbl_t *hashtbl, const char *key, long *num)
{
	conf_file_values_t *p;

	p = _conf_hashtbl_lookup(hashtbl, key);
	if (p == NULL) {
		fprintf(stderr, "Invalid key \"%s\"\n", key);
		return -1;
	}
	if (p->data_count == 0) {
		return -1;
	}

	*num = *(long *)p->data;

	return 0;
}

int s_c_get_pointer(const s_c_hashtbl_t *hashtbl, const char *key, void **ptr)
{
	conf_file_values_t *p;

	p = _conf_hashtbl_lookup(hashtbl, key);
	if (p == NULL) {
		fprintf(stderr, "Invalid key \"%s\"\n", key);
		return -1;
	}
	if (p->data_count == 0) {
		return -1;
	}

	*ptr = p->data;

	return 0;
}

int s_c_get_array(const s_c_hashtbl_t *hashtbl, const char *key,
		  void **ptr_array[], int *count)
{
	conf_file_values_t *p;

	p = _conf_hashtbl_lookup(hashtbl, key);
	if (p == NULL) {
		fprintf(stderr, "Invalid key \"%s\"\n", key);
		return -1;
	}
	if (p->data_count == 0) {
		return -1;
	}

	*ptr_array = (void **)p->data;
	*count = p->data_count;

	return 0;
}


/*
 * Given an "options" array, print the current values of all
 * options in supplied hash table "hashtbl".
 *
 * Primarily for debugging purposes.
 */
void s_c_dump_values(const s_c_hashtbl_t *hashtbl,
		     const struct conf_file_options options[])
{
	const struct conf_file_options *op = NULL;
	long num;
	char *str;
	void *ptr;
	void **ptr_array;
	int count;
	int i;

	for (op = options; op->key != NULL; op++) {
		switch(op->type) {
		case S_C_STRING:
			if (!s_c_get_string(hashtbl, op->key, &str)) {
				printf("%s = %s\n", op->key, str);
				free(str);
			} else {
				printf("%s\n", op->key);
			}
			break;
		case S_C_LONG:
			if (!s_c_get_long(hashtbl, op->key, &num))
				printf("%s = %ld\n", op->key, num);
			else
				printf("%s\n", op->key);
			break;
		case S_C_POINTER:
			if (!s_c_get_pointer(hashtbl, op->key, &ptr))
				printf("%s = %x\n", op->key, ptr);
			else
				printf("%s\n", op->key);
			break;
		case S_C_ARRAY:
			if (!s_c_get_array(hashtbl, op->key,
					   &ptr_array, &count)) {
				printf("%s, count = %d, ", op->key, count);
				for (i = 0; i < count; i++)
					printf("%x ", ptr_array[i]);
				printf("\n");
			} else {
				printf("%s\n", op->key);
			}
			break;
		}
	}
}

/**********************************************************************
 * What follows is specific to parsing the main slurm.conf file.
 **********************************************************************/
#ifdef PARSE_DEBUG
int parse_nodename(void **dest, slurm_conf_enum_t type,
		   const char *key, const char *value, const char *leftover);
void destroy_nodename(void *ptr);
int parse_partitionname(void **dest, slurm_conf_enum_t type,
		   const char *key, const char *value, const char *leftover);
void destroy_partitionname(void *ptr);


struct conf_file_options conf_options[] = {
	{"AuthType", S_C_STRING},
	{"CheckpointType", S_C_STRING},
	{"CacheGroups", S_C_LONG},
	{"BackupAddr", S_C_STRING},
	{"BackupController", S_C_STRING},
	{"ControlAddr", S_C_STRING},
	{"ControlMachine", S_C_STRING},
	{"Epilog", S_C_STRING},
	{"FastSchedule", S_C_LONG},
	{"FirstJobId", S_C_LONG},
	{"HashBase", S_C_LONG}, /* defunct */
	{"HeartbeatInterval", S_C_LONG},
	{"InactiveLimit", S_C_LONG},
	{"JobAcctloc", S_C_STRING},
	{"JobAcctParameters", S_C_STRING},
	{"JobAcctType", S_C_STRING},
	{"JobCompLoc", S_C_STRING},
	{"JobCompType", S_C_STRING},
	{"JobCredentialPrivateKey", S_C_STRING},
	{"JobCredentialPublicCertificate", S_C_STRING},
	{"KillTree", S_C_LONG}, /* FIXME - defunct? */
	{"KillWait", S_C_LONG},
	{"MaxJobCount", S_C_LONG},
	{"MinJobAge", S_C_LONG},
	{"MpichGmDirectSupport", S_C_LONG},
	{"MpiDefault", S_C_STRING},
	{"NodeName", S_C_ARRAY, parse_nodename, destroy_nodename},
	{"PartitionName", S_C_ARRAY, parse_partitionname, destroy_partitionname},
	{"PluginDir", S_C_STRING},
	{"ProctrackType", S_C_STRING},
	{"Prolog", S_C_STRING},
	{"PropagateResourceLimitsExcept", S_C_STRING},
	{"PropagateResourceLimits", S_C_STRING},
	{"ReturnToService", S_C_LONG},
	{"SchedulerAuth", S_C_STRING},
	{"SchedulerPort", S_C_LONG},
	{"SchedulerRootFilter", S_C_LONG},
	{"SchedulerType", S_C_STRING},
	{"SelectType", S_C_STRING},
	{"SlurmUser", S_C_STRING},
	{"SlurmctldDebug", S_C_LONG},
	{"SlurmctldLogFile", S_C_STRING},
	{"SlurmctldPidFile", S_C_STRING},
	{"SlurmctldPort", S_C_LONG},
	{"SlurmctldTimeout", S_C_LONG},
	{"SlurmdDebug", S_C_LONG},
	{"SlurmdLogFile", S_C_STRING},
	{"SlurmdPidFile",  S_C_STRING},
	{"SlurmdPort", S_C_LONG},
	{"SlurmdSpoolDir", S_C_STRING},
	{"SlurmdTimeout", S_C_LONG},
	{"SrunEpilog", S_C_STRING},
	{"SrunProlog", S_C_STRING},
	{"StateSaveLocation", S_C_STRING},
	{"SwitchType", S_C_STRING},
	{"TaskEpilog", S_C_STRING},
	{"TaskProlog", S_C_STRING},
	{"TaskPlugin", S_C_STRING},
	{"TmpFS", S_C_STRING},
	{"TreeWidth", S_C_LONG},
	{"WaitTime", S_C_LONG},
	{NULL}
};

struct conf_file_options nodename_options[] = {
	{"NodeName", S_C_STRING},
	{"NodeHostname", S_C_STRING},
	{"NodeAddr", S_C_STRING},
	{"Feature", S_C_STRING},
	{"Port", S_C_LONG},
	{"Procs", S_C_LONG},
	{"RealMemory", S_C_LONG},
	{"Reason", S_C_STRING},
	{"State", S_C_STRING},
	{"TmpDisk", S_C_LONG},
	{"Weight", S_C_LONG},
	{NULL}
};

struct conf_file_options partitionname_options[] = {
	{"PartitionName", S_C_STRING},
	{"AllowGroups", S_C_STRING},
	{"Default", S_C_STRING},
	{"Hidden", S_C_STRING},
	{"RootOnly", S_C_STRING},
	{"MaxTime", S_C_STRING},
	{"MaxNodes", S_C_LONG},
	{"MinNodes", S_C_LONG},
	{"Nodes", S_C_STRING},
	{"Shared", S_C_STRING},
	{"State", S_C_STRING},
	{NULL}
};

int parse_nodename(void **dest, slurm_conf_enum_t type,
		   const char *key, const char *value, const char *line)
{
	s_c_hashtbl_t *hashtbl;

	hashtbl = s_c_hashtbl_create(nodename_options);
	s_c_parse_line(hashtbl, line);
	s_c_dump_values(hashtbl, nodename_options);

	*dest = (void *)hashtbl;

	return 0;
}

void destroy_nodename(void *ptr)
{
	s_c_hashtbl_destroy((s_c_hashtbl_t *)ptr);
}

int parse_partitionname(void **dest, slurm_conf_enum_t type,
		   const char *key, const char *value, const char *line)
{
	s_c_hashtbl_t *hashtbl;

	hashtbl = s_c_hashtbl_create(partitionname_options);
	s_c_parse_line(hashtbl, line);
	s_c_dump_values(hashtbl, partitionname_options);

	*dest = (void *)hashtbl;

	return 0;
}

void destroy_partitionname(void *ptr)
{
	s_c_hashtbl_destroy((s_c_hashtbl_t *)ptr);
}


void parse_slurm_conf(void) {
	s_c_hashtbl_t *hashtbl;

	hashtbl = s_c_hashtbl_create(conf_options);
	s_c_parse_file(hashtbl, "/home/morrone/slurm.conf");
	s_c_dump_values(hashtbl, conf_options);
	s_c_hashtbl_destroy(hashtbl);
}

int main()
{
	parse_slurm_conf();
}
#endif

