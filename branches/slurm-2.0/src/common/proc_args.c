/*****************************************************************************\
 *  proc_args.c - helper functions for command argument processing
 *  $Id: opt.h 11996 2007-08-10 20:36:26Z jette $
 *****************************************************************************
 *  Copyright (C) 2007 Hewlett-Packard Development Company, L.P.
 *  Written by Christopher Holmes <cholmes@hp.com>, who borrowed heavily
 *  from existing SLURM source code, particularly src/srun/opt.c 
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>		/* strcpy, strncasecmp */

#ifdef HAVE_STRINGS_H
#  include <strings.h>
#endif

#ifndef _GNU_SOURCE
#  define _GNU_SOURCE
#endif

#include <fcntl.h>
#include <stdarg.h>		/* va_start   */
#include <stdio.h>
#include <stdlib.h>		/* getenv     */
#include <pwd.h>		/* getpwuid   */
#include <ctype.h>		/* isdigit    */
#include <sys/param.h>		/* MAXPATHLEN */
#include <sys/stat.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>

#include "src/common/list.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/common/proc_args.h"




/* print this version of SLURM */
void print_slurm_version(void)
{
	printf("%s %s\n", PACKAGE, SLURM_VERSION);
}

/* 
 * verify that a distribution type in arg is of a known form
 * returns the task_dist_states, or -1 if state is unknown
 */
task_dist_states_t verify_dist_type(const char *arg, uint32_t *plane_size)
{
	int len = strlen(arg);
	char *dist_str = NULL;
	task_dist_states_t result = SLURM_DIST_UNKNOWN;
	bool lllp_dist = false, plane_dist = false;

	dist_str = strchr(arg,':');
	if (dist_str != NULL) {
		/* -m cyclic|block:cyclic|block */
		lllp_dist = true;
	} else {
		/* -m plane=<plane_size> */
		dist_str = strchr(arg,'=');
		if(dist_str != NULL) {
			*plane_size=atoi(dist_str+1);
			len = dist_str-arg;
			plane_dist = true;
		}
	}

	if (lllp_dist) {
		if (strcasecmp(arg, "cyclic:cyclic") == 0) {
			result = SLURM_DIST_CYCLIC_CYCLIC;
		} else if (strcasecmp(arg, "cyclic:block") == 0) {
			result = SLURM_DIST_CYCLIC_BLOCK;
		} else if (strcasecmp(arg, "block:block") == 0) {
			result = SLURM_DIST_BLOCK_BLOCK;
		} else if (strcasecmp(arg, "block:cyclic") == 0) {
			result = SLURM_DIST_BLOCK_CYCLIC;
		}
	} else if (plane_dist) {
		if (strncasecmp(arg, "plane", len) == 0) {
			result = SLURM_DIST_PLANE;
		}
	} else {
		if (strncasecmp(arg, "cyclic", len) == 0) {
			result = SLURM_DIST_CYCLIC;
		} else if (strncasecmp(arg, "block", len) == 0) {
			result = SLURM_DIST_BLOCK;
		} else if ((strncasecmp(arg, "arbitrary", len) == 0) ||
		           (strncasecmp(arg, "hostfile", len) == 0)) {
			result = SLURM_DIST_ARBITRARY;
		}
	}

	return result;
}

/*
 * verify that a connection type in arg is of known form
 * returns the connection_type or -1 if not recognized
 */
int verify_conn_type(const char *arg)
{
#ifdef HAVE_BG
	int len = strlen(arg);

	if (!strncasecmp(arg, "MESH", len))
		return SELECT_MESH;
	else if (!strncasecmp(arg, "TORUS", len))
		return SELECT_TORUS;
	else if (!strncasecmp(arg, "NAV", len))
		return SELECT_NAV;
#ifndef HAVE_BGL
	else if (!strncasecmp(arg, "HTC", len)
		 || !strncasecmp(arg, "HTC_S", len))
		return SELECT_HTC_S;
	else if (!strncasecmp(arg, "HTC_D", len))
		return SELECT_HTC_D;
	else if (!strncasecmp(arg, "HTC_V", len))
		return SELECT_HTC_V;
	else if (!strncasecmp(arg, "HTC_L", len))
		return SELECT_HTC_L;
#endif
#endif
	error("invalid --conn-type argument %s ignored.", arg);
	return NO_VAL;
}

/*
 * verify geometry arguments, must have proper count
 * returns -1 on error, 0 otherwise
 */
int verify_geometry(const char *arg, uint16_t *geometry)
{
	char* token, *delimiter = ",x", *next_ptr;
	int i, rc = 0;
	char* geometry_tmp = xstrdup(arg);
	char* original_ptr = geometry_tmp;

	token = strtok_r(geometry_tmp, delimiter, &next_ptr);
	for (i=0; i<SYSTEM_DIMENSIONS; i++) {
		if (token == NULL) {
			error("insufficient dimensions in --geometry");
			rc = -1;
			break;
		}
		geometry[i] = (uint16_t)atoi(token);
		if (geometry[i] == 0 || geometry[i] == (uint16_t)NO_VAL) {
			error("invalid --geometry argument");
			rc = -1;
			break;
		}
		geometry_tmp = next_ptr;
		token = strtok_r(geometry_tmp, delimiter, &next_ptr);
	}
	if (token != NULL) {
		error("too many dimensions in --geometry");
		rc = -1;
	}

	if (original_ptr)
		xfree(original_ptr);

	return rc;
}

/* return command name from its full path name */
char * base_name(char* command)
{
	char *char_ptr, *name;
	int i;

	if (command == NULL)
		return NULL;

	char_ptr = strrchr(command, (int)'/');
	if (char_ptr == NULL)
		char_ptr = command;
	else
		char_ptr++;

	i = strlen(char_ptr);
	name = xmalloc(i+1);
	strcpy(name, char_ptr);
	return name;
}

/*
 * str_to_bytes(): verify that arg is numeric with optional "G" or "M" at end
 * if "G" or "M" is there, multiply by proper power of 2 and return
 * number in bytes
 */
long str_to_bytes(const char *arg)
{
	char *buf;
	char *endptr;
	int end;
	int multiplier = 1;
	long result;

	buf = xstrdup(arg);

	end = strlen(buf) - 1;

	if (isdigit(buf[end])) {
		result = strtol(buf, &endptr, 10);

		if (*endptr != '\0')
			result = -result;

	} else {

		switch (toupper(buf[end])) {

		case 'G':
			multiplier = 1024;
			break;

		case 'M':
			/* do nothing */
			break;

		default:
			multiplier = -1;
		}

		buf[end] = '\0';

		result = multiplier * strtol(buf, &endptr, 10);

		if (*endptr != '\0')
			result = -result;
	}

	return result;
}

/* Convert a string into a node count */
static int
_str_to_nodes(const char *num_str, char **leftover)
{
	long int num;
	char *endptr;

	num = strtol(num_str, &endptr, 10);
	if (endptr == num_str) { /* no valid digits */
		*leftover = (char *)num_str;
		return 0;
	} 
	if (*endptr != '\0' && (*endptr == 'k' || *endptr == 'K')) {
		num *= 1024;
		endptr++;
	}
	*leftover = endptr;

	return (int)num;
}

/* 
 * verify that a node count in arg is of a known form (count or min-max)
 * OUT min, max specified minimum and maximum node counts
 * RET true if valid
 */
bool verify_node_count(const char *arg, int *min_nodes, int *max_nodes)
{
	char *ptr, *min_str, *max_str;
	char *leftover;
	
	/* Does the string contain a "-" character?  If so, treat as a range.
	 * otherwise treat as an absolute node count. */
	if ((ptr = index(arg, '-')) != NULL) {
		min_str = xstrndup(arg, ptr-arg);
		*min_nodes = _str_to_nodes(min_str, &leftover);
		if (!xstring_is_whitespace(leftover)) {
			error("\"%s\" is not a valid node count", min_str);
			xfree(min_str);
			return false;
		}
		xfree(min_str);
		if (*min_nodes == 0)
			*min_nodes = 1;

		max_str = xstrndup(ptr+1, strlen(arg)-((ptr+1)-arg));
		*max_nodes = _str_to_nodes(max_str, &leftover);
		if (!xstring_is_whitespace(leftover)) {
			error("\"%s\" is not a valid node count", max_str);
			xfree(max_str);
			return false;
		}
		xfree(max_str);
	} else {
		*min_nodes = *max_nodes = _str_to_nodes(arg, &leftover);
		if (!xstring_is_whitespace(leftover)) {
			error("\"%s\" is not a valid node count", arg);
			return false;
		}
		if (*min_nodes == 0) {
			/* whitespace does not a valid node count make */
			error("\"%s\" is not a valid node count", arg);
			return false;
		}
	}

	if ((*max_nodes != 0) && (*max_nodes < *min_nodes)) {
		error("Maximum node count %d is less than"
		      " minimum node count %d",
		      *max_nodes, *min_nodes);
		return false;
	}

	return true;
}

/*
 * If the node list supplied is a file name, translate that into 
 *	a list of nodes, we orphan the data pointed to
 * RET true if the node list is a valid one
 */
bool verify_node_list(char **node_list_pptr, enum task_dist_states dist,
		      int task_count)
{
	char *nodelist = NULL;
	
	xassert (node_list_pptr);
	xassert (*node_list_pptr);

	if (strchr(*node_list_pptr, '/') == NULL)
		return true;	/* not a file name */

	/* If we are using Arbitrary grab count out of the hostfile
	   using them exactly the way we read it in since we are
	   saying, lay it out this way! */
	if(dist == SLURM_DIST_ARBITRARY) 
		nodelist = slurm_read_hostfile(*node_list_pptr, task_count);
        else
		nodelist = slurm_read_hostfile(*node_list_pptr, NO_VAL);
		
	if (!nodelist) 
		return false;

	xfree(*node_list_pptr);
	*node_list_pptr = xstrdup(nodelist);
	free(nodelist);

	return true;
}


/* 
 * get either 1 or 2 integers for a resource count in the form of either
 * (count, min-max, or '*')
 * A partial error message is passed in via the 'what' param.
 * RET true if valid
 */
bool
get_resource_arg_range(const char *arg, const char *what, int* min, int *max, 
		       bool isFatal)
{
	char *p;
	long int result;

	if (*arg == '\0') return true;

	/* wildcard meaning every possible value in range */
	if (*arg == '*' ) {
		*min = 1;
		*max = INT_MAX;
		return true;
	}

	result = strtol(arg, &p, 10);
        if (*p == 'k' || *p == 'K') {
		result *= 1024;
		p++;
	}

	if (((*p != '\0')&&(*p != '-')) || (result <= 0L)) {
		error ("Invalid numeric value \"%s\" for %s.", arg, what);
		if (isFatal) exit(1);
		return false;
	} else if (result > INT_MAX) {
		error ("Numeric argument (%ld) to big for %s.", result, what);
		if (isFatal) exit(1);
		return false;
	}

	*min = (int) result;

	if (*p == '\0') return true;
	if (*p == '-') p++;

	result = strtol(p, &p, 10);
        if (*p == 'k' || *p == 'K') {
		result *= 1024;
		p++;
	}
	
	if (((*p != '\0')&&(*p != '-')) || (result <= 0L)) {
		error ("Invalid numeric value \"%s\" for %s.", arg, what);
		if (isFatal) exit(1);
		return false;
	} else if (result > INT_MAX) {
		error ("Numeric argument (%ld) to big for %s.", result, what);
		if (isFatal) exit(1);
		return false;
	}

	*max = (int) result;

	return true;
}

/* 
 * verify that a resource counts in arg are of a known form X, X:X, X:X:X, or 
 * X:X:X:X, where X is defined as either (count, min-max, or '*')
 * RET true if valid
 */
bool verify_socket_core_thread_count(const char *arg, 
				     int *min_sockets, int *max_sockets,
				     int *min_cores, int *max_cores,
				     int *min_threads, int  *max_threads,
				     cpu_bind_type_t *cpu_bind_type)
{
	bool tmp_val,ret_val;
	int i,j;
	const char *cur_ptr = arg;
	char buf[3][48]; /* each can hold INT64_MAX - INT64_MAX */
	buf[0][0] = '\0';
	buf[1][0] = '\0';
	buf[2][0] = '\0';

 	for (j=0;j<3;j++) {	
		for (i=0;i<47;i++) {
			if (*cur_ptr == '\0' || *cur_ptr ==':')
				break;
			buf[j][i] = *cur_ptr++;
		}
		if (*cur_ptr == '\0')
			break;
		xassert(*cur_ptr == ':');
		buf[j][i] = '\0';
		cur_ptr++;
	}
	/* if cpu_bind_type doesn't already have a auto preference, choose
	 * the level based on the level of the -E specification
	 */
	if (!(*cpu_bind_type & (CPU_BIND_TO_SOCKETS |
				CPU_BIND_TO_CORES |
				CPU_BIND_TO_THREADS))) {
		if (j == 0) {
			*cpu_bind_type |= CPU_BIND_TO_SOCKETS;
		} else if (j == 1) {
			*cpu_bind_type |= CPU_BIND_TO_CORES;
		} else if (j == 2) {
			*cpu_bind_type |= CPU_BIND_TO_THREADS;
		}
        }
	buf[j][i] = '\0';

	ret_val = true;
	tmp_val = get_resource_arg_range(&buf[0][0], "first arg of -B", 
					 min_sockets, max_sockets, true);
	ret_val = ret_val && tmp_val;
	tmp_val = get_resource_arg_range(&buf[1][0], "second arg of -B", 
					 min_cores, max_cores, true);
	ret_val = ret_val && tmp_val;
	tmp_val = get_resource_arg_range(&buf[2][0], "third arg of -B", 
					 min_threads, max_threads, true);
	ret_val = ret_val && tmp_val;

	return ret_val;
}

/* 
 * verify that a hint is valid and convert it into the implied settings
 * RET true if valid
 */
bool verify_hint(const char *arg, int *min_sockets, int *max_sockets,
		 int *min_cores, int *max_cores, int *min_threads,
		 int *max_threads, cpu_bind_type_t *cpu_bind_type)
{
	char *buf, *p, *tok;
	if (!arg) {
		return true;
	}

	buf = xstrdup(arg);
	p = buf;
	/* change all ',' delimiters not followed by a digit to ';'  */
	/* simplifies parsing tokens while keeping map/mask together */
	while (p[0] != '\0') {
		if ((p[0] == ',') && (!isdigit(p[1])))
			p[0] = ';';
		p++;
	}

	p = buf;
	while ((tok = strsep(&p, ";"))) {
		if (strcasecmp(tok, "help") == 0) {
			printf(
"Application hint options:\n"
"    --hint=             Bind tasks according to application hints\n"
"        compute_bound   use all cores in each physical CPU\n"
"        memory_bound    use only one core in each physical CPU\n"
"        [no]multithread [don't] use extra threads with in-core multi-threading\n"
"        help            show this help message\n");
			return 1;
		} else if (strcasecmp(tok, "compute_bound") == 0) {
		        *min_sockets = 1;
		        *max_sockets = INT_MAX;
		        *min_cores   = 1;
		        *max_cores   = INT_MAX;
			*cpu_bind_type |= CPU_BIND_TO_CORES;
		} else if (strcasecmp(tok, "memory_bound") == 0) {
		        *min_cores = 1;
		        *max_cores = 1;
			*cpu_bind_type |= CPU_BIND_TO_CORES;
		} else if (strcasecmp(tok, "multithread") == 0) {
		        *min_threads = 1;
		        *max_threads = INT_MAX;
			*cpu_bind_type |= CPU_BIND_TO_THREADS;
		} else if (strcasecmp(tok, "nomultithread") == 0) {
		        *min_threads = 1;
		        *max_threads = 1;
			*cpu_bind_type |= CPU_BIND_TO_THREADS;
		} else {
			error("unrecognized --hint argument \"%s\", "
			      "see --hint=help", tok);
			xfree(buf);
			return 1;
		}
	}

	xfree(buf);
	return 0;
}

uint16_t parse_mail_type(const char *arg)
{
	uint16_t rc;

	if (strcasecmp(arg, "BEGIN") == 0)
		rc = MAIL_JOB_BEGIN;
	else if  (strcasecmp(arg, "END") == 0)
		rc = MAIL_JOB_END;
	else if (strcasecmp(arg, "FAIL") == 0)
		rc = MAIL_JOB_FAIL;
	else if (strcasecmp(arg, "ALL") == 0)
		rc = MAIL_JOB_BEGIN |  MAIL_JOB_END |  MAIL_JOB_FAIL;
	else
		rc = 0;		/* failure */

	return rc;
}
char *print_mail_type(const uint16_t type)
{
	if (type == 0)
		return "NONE";

	if (type == MAIL_JOB_BEGIN)
		return "BEGIN";
	if (type == MAIL_JOB_END)
		return "END";
	if (type == MAIL_JOB_FAIL)
		return "FAIL";
	if (type == (MAIL_JOB_BEGIN |  MAIL_JOB_END |  MAIL_JOB_FAIL))
		return "ALL";

	return "MULTIPLE";
}

static void
_freeF(void *data)
{
	xfree(data);
}

static List
_create_path_list(void)
{
	List l = list_create(_freeF);
	char *path = xstrdup(getenv("PATH"));
	char *c, *lc;

	c = getenv("PATH");
	if (!c) {
		error("No PATH environment variable");
		return l;
	}
	path = xstrdup(c);
	c = lc = path;

	while (*c != '\0') {
		if (*c == ':') {
			/* nullify and push token onto list */
			*c = '\0';
			if (lc != NULL && strlen(lc) > 0)
				list_append(l, xstrdup(lc));
			lc = ++c;
		} else
			c++;
	}

	if (strlen(lc) > 0)
		list_append(l, xstrdup(lc));

	xfree(path);

	return l;
}

char *
search_path(char *cwd, char *cmd, bool check_current_dir, int access_mode)
{
	List         l        = NULL;
	ListIterator i        = NULL;
	char *path, *fullpath = NULL;

	if (  (cmd[0] == '.' || cmd[0] == '/') 
           && (access(cmd, access_mode) == 0 ) ) {
		if (cmd[0] == '.')
			xstrfmtcat(fullpath, "%s/", cwd);
		xstrcat(fullpath, cmd);
		goto done;
	}

	l = _create_path_list();
	if (l == NULL)
		return NULL;

	if (check_current_dir) 
		list_prepend(l, xstrdup(cwd));

	i = list_iterator_create(l);
	while ((path = list_next(i))) {
		xstrfmtcat(fullpath, "%s/%s", path, cmd);

		if (access(fullpath, access_mode) == 0)
			goto done;

		xfree(fullpath);
		fullpath = NULL;
	}
  done:
	if (l)
		list_destroy(l);
	return fullpath;
}

char *print_commandline(const int script_argc, char **script_argv)
{
	int i;
	char tmp[256], *out_buf = NULL, *prefix;

	for (i = 0; i < script_argc; i++) {
		if (out_buf)
			prefix = " ";
		else
			prefix = "";
		snprintf(tmp, 256,  "%s%s", prefix, script_argv[i]);
		xstrcat(out_buf, tmp);
	}
	return out_buf;
}

char *print_geometry(const uint16_t *geometry)
{
	int i;
	char buf[32], *rc = NULL;

	if ((SYSTEM_DIMENSIONS == 0)
	||  (geometry[0] == (uint16_t)NO_VAL))
		return NULL;

	for (i=0; i<SYSTEM_DIMENSIONS; i++) {
		if (i > 0)
			snprintf(buf, sizeof(buf), "x%u", geometry[i]);
		else
			snprintf(buf, sizeof(buf), "%u", geometry[i]);
		xstrcat(rc, buf);
	}

	return rc;
}
