/*****************************************************************************\
 *  proc_args.c - helper functions for command argument processing
 *  $Id: opt.h 11996 2007-08-10 20:36:26Z jette $
 *****************************************************************************
 *  Copyright (C) 2007 Hewlett-Packard Development Company, L.P.
 *  Written by Christopher Holmes <cholmes@hp.com>, who borrowed heavily
 *  from existing SLURM source code, particularly src/srun/opt.c
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
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

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>		/* strcpy, strncasecmp */

#ifdef HAVE_STRINGS_H
#  include <strings.h>
#endif

#ifndef __USE_ISOC99
#define __USE_ISOC99
#endif

#ifdef HAVE_LIMITS_H
#  include <limits.h>
#endif

#ifndef _GNU_SOURCE
#  define _GNU_SOURCE
#endif

#ifndef SYSTEM_DIMENSIONS
#  define SYSTEM_DIMENSIONS 1
#endif

#include <fcntl.h>
#include <stdarg.h>		/* va_start   */
#include <stdio.h>
#include <stdlib.h>		/* getenv, strtoll */
#include <pwd.h>		/* getpwuid   */
#include <ctype.h>		/* isdigit    */
#include <sys/param.h>		/* MAXPATHLEN */
#include <sys/stat.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>

#include "src/common/gres.h"
#include "src/common/list.h"
#include "src/common/proc_args.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"


/* print this version of SLURM */
void print_slurm_version(void)
{
	printf("%s %s\n", PACKAGE_NAME, SLURM_VERSION_STRING);
}

/* print the available gres options */
void print_gres_help(void)
{
	char help_msg[1024] = "";

	gres_plugin_help_msg(help_msg, sizeof(help_msg));
	if (help_msg[0])
		printf("%s", help_msg);
	else
		printf("No gres help is available\n");
}

void set_distribution(task_dist_states_t distribution,
		      char **dist, char **lllp_dist)
{
	if (((int)distribution >= 0)
	    && (distribution != SLURM_DIST_UNKNOWN)) {
		switch (distribution) {
		case SLURM_DIST_CYCLIC:
			*dist      = "cyclic";
			break;
		case SLURM_DIST_BLOCK:
			*dist      = "block";
			break;
		case SLURM_DIST_PLANE:
			*dist      = "plane";
			*lllp_dist = "plane";
			break;
		case SLURM_DIST_ARBITRARY:
			*dist      = "arbitrary";
			break;
		case SLURM_DIST_CYCLIC_CYCLIC:
			*dist      = "cyclic:cyclic";
			*lllp_dist = "cyclic";
			break;
		case SLURM_DIST_CYCLIC_BLOCK:
			*dist      = "cyclic:block";
			*lllp_dist = "block";
			break;
		case SLURM_DIST_BLOCK_CYCLIC:
			*dist      = "block:cyclic";
			*lllp_dist = "cyclic";
			break;
		case SLURM_DIST_BLOCK_BLOCK:
			*dist      = "block:block";
			*lllp_dist = "block";
			break;
		case SLURM_DIST_CYCLIC_CFULL:
			*dist      = "cyclic:fcyclic";
			*lllp_dist = "fcyclic";
			break;
		case SLURM_DIST_BLOCK_CFULL:
			*dist      = "block:fcyclic";
			*lllp_dist = "cyclic";
			break;
		default:
			error("unknown dist, type %d", distribution);
			break;
		}
	}
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
		if (dist_str != NULL) {
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
		} else if (strcasecmp(arg, "block:fcyclic") == 0) {
			result = SLURM_DIST_BLOCK_CFULL;
		} else if (strcasecmp(arg, "cyclic:fcyclic") == 0) {
			result = SLURM_DIST_CYCLIC_CFULL;
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

static uint16_t _get_conn_type(char *arg, bool bgp)
{
	uint16_t len = strlen(arg);
	if (!len) {
		/* no input given */
		error("no conn-type argument given.");
		return (uint16_t)NO_VAL;
	} else if (!strncasecmp(arg, "MESH", len))
		return SELECT_MESH;
	else if (!strncasecmp(arg, "TORUS", len))
		return SELECT_TORUS;
	else if (!strncasecmp(arg, "NAV", len))
		return SELECT_NAV;
	else if (!strncasecmp(arg, "SMALL", len))
		return SELECT_SMALL;
	else if (bgp) {
		if (!strncasecmp(arg, "HTC", len) ||
		    !strncasecmp(arg, "HTC_S", len))
			return SELECT_HTC_S;
		else if (!strncasecmp(arg, "HTC_D", len))
			return SELECT_HTC_D;
		else if (!strncasecmp(arg, "HTC_V", len))
			return SELECT_HTC_V;
		else if (!strncasecmp(arg, "HTC_L", len))
			return SELECT_HTC_L;
	}

	error("invalid conn-type argument '%s' ignored.", arg);
	return (uint16_t)NO_VAL;
}

/*
 * verify comma separated list of connection types to array of uint16_t
 * connection_types or NO_VAL if not recognized
 */
extern void verify_conn_type(const char *arg, uint16_t *conn_type)
{
	bool got_bgp = 0;
	int inx = 0;
	int highest_dims = 1;
	char *arg_tmp = xstrdup(arg), *tok, *save_ptr = NULL;

	if (working_cluster_rec) {
		if (working_cluster_rec->flags & CLUSTER_FLAG_BGP)
			got_bgp = 1;
		else if (working_cluster_rec->flags & CLUSTER_FLAG_BGQ)
			highest_dims = 4;
	} else {
#ifdef HAVE_BGP
		got_bgp = 1;
# elif defined HAVE_BGQ
		highest_dims = 4;
#endif
	}

	tok = strtok_r(arg_tmp, ",", &save_ptr);
	while (tok) {
		if (inx >= highest_dims) {
			error("too many conn-type arguments: %s", arg);
			break;
		}
		conn_type[inx++] = _get_conn_type(tok, got_bgp);
		tok = strtok_r(NULL, ",", &save_ptr);
	}
	if (inx == 0)
		error("invalid conn-type argument '%s' ignored.", arg);
	/* Fill the rest in with NO_VALS (use HIGHEST_DIMS here
	 * instead of highest_dims since that is the size of the
	 * array. */
	for ( ; inx < HIGHEST_DIMENSIONS; inx++) {
		conn_type[inx] = (uint16_t)NO_VAL;
	}

	xfree(arg_tmp);
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
	int dims = slurmdb_setup_cluster_dims();

	token = strtok_r(geometry_tmp, delimiter, &next_ptr);
	for (i=0; i<dims; i++) {
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
 * str_to_mbytes(): verify that arg is numeric with optional "K", "M", "G"
 * or "T" at end and return the number in mega-bytes
 */
long str_to_mbytes(const char *arg)
{
	long result;
	char *endptr;

	errno = 0;
	result = strtol(arg, &endptr, 10);
	if ((errno != 0) && ((result == LONG_MIN) || (result == LONG_MAX)))
		result = -1;
	else if (endptr[0] == '\0')
		;
	else if ((endptr[0] == 'k') || (endptr[0] == 'K'))
		result = (result + 1023) / 1024;	/* round up */
	else if ((endptr[0] == 'm') || (endptr[0] == 'M'))
		;
	else if ((endptr[0] == 'g') || (endptr[0] == 'G'))
		result *= 1024;
	else if ((endptr[0] == 't') || (endptr[0] == 'T'))
		result *= (1024 * 1024);
	else
		result = -1;

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
	if (*endptr != '\0' && (*endptr == 'm' || *endptr == 'M')) {
		num *= (1024 * 1024);
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
#ifdef HAVE_ALPS_CRAY
		if (*min_nodes < 0) {
#else
		if (*min_nodes == 0) {
#endif
			*min_nodes = 1;
		}

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
#ifdef HAVE_ALPS_CRAY
		if (*min_nodes < 0) {
#else
		if (*min_nodes == 0) {
#endif
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
	if (dist == SLURM_DIST_ARBITRARY)
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
 * IN arg - argument
 * IN what - variable name (for errors)
 * OUT min - first number
 * OUT max - maximum value if specified, NULL if don't care
 * IN isFatal - if set, exit on error
 * RET true if valid
 */
bool get_resource_arg_range(const char *arg, const char *what, int* min,
			    int *max, bool isFatal)
{
	char *p;
	long int result;

	/* wildcard meaning every possible value in range */
	if ((*arg == '\0') || (*arg == '*' )) {
		*min = 1;
		if (max)
			*max = INT_MAX;
		return true;
	}

	result = strtol(arg, &p, 10);
	if (*p == 'k' || *p == 'K') {
		result *= 1024;
		p++;
	} else if (*p == 'm' || *p == 'M') {
		result *= 1048576;
		p++;
	}

	if (((*p != '\0') && (*p != '-')) || (result <= 0L)) {
		error ("Invalid numeric value \"%s\" for %s.", arg, what);
		if (isFatal)
			exit(1);
		return false;
	} else if (result > INT_MAX) {
		error ("Numeric argument (%ld) to big for %s.", result, what);
		if (isFatal)
			exit(1);
		return false;
	}

	*min = (int) result;

	if (*p == '\0')
		return true;
	if (*p == '-')
		p++;

	result = strtol(p, &p, 10);
	if ((*p == 'k') || (*p == 'K')) {
		result *= 1024;
		p++;
	} else if (*p == 'm' || *p == 'M') {
		result *= 1048576;
		p++;
	}

	if (((*p != '\0') && (*p != '-')) || (result <= 0L)) {
		error ("Invalid numeric value \"%s\" for %s.", arg, what);
		if (isFatal)
			exit(1);
		return false;
	} else if (result > INT_MAX) {
		error ("Numeric argument (%ld) to big for %s.", result, what);
		if (isFatal)
			exit(1);
		return false;
	}

	if (max)
		*max = (int) result;

	return true;
}

/*
 * verify that a resource counts in arg are of a known form X, X:X, X:X:X, or
 * X:X:X:X, where X is defined as either (count, min-max, or '*')
 * RET true if valid
 */
bool verify_socket_core_thread_count(const char *arg, int *min_sockets,
				     int *min_cores, int *min_threads,
				     cpu_bind_type_t *cpu_bind_type)
{
	bool tmp_val,ret_val;
	int i,j;
	int max_sockets = 0, max_cores = 0, max_threads = 0;
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
	if (cpu_bind_type &&
	    !(*cpu_bind_type & (CPU_BIND_TO_SOCKETS |
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
					 min_sockets, &max_sockets, true);
	if ((*min_sockets == 1) && (max_sockets == INT_MAX))
		*min_sockets = NO_VAL;	/* Use full range of values */
	ret_val = ret_val && tmp_val;


	tmp_val = get_resource_arg_range(&buf[1][0], "second arg of -B",
					 min_cores, &max_cores, true);
	if ((*min_cores == 1) && (max_cores == INT_MAX))
		*min_cores = NO_VAL;	/* Use full range of values */
	ret_val = ret_val && tmp_val;

	tmp_val = get_resource_arg_range(&buf[2][0], "third arg of -B",
					 min_threads, &max_threads, true);
	if ((*min_threads == 1) && (max_threads == INT_MAX))
		*min_threads = NO_VAL;	/* Use full range of values */
	ret_val = ret_val && tmp_val;

	return ret_val;
}

/*
 * verify that a hint is valid and convert it into the implied settings
 * RET true if valid
 */
bool verify_hint(const char *arg, int *min_sockets, int *min_cores,
		 int *min_threads, int *ntasks_per_core,
		 cpu_bind_type_t *cpu_bind_type)
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
		if ((p[0] == ',') && (!isdigit((int)p[1])))
			p[0] = ';';
		p++;
	}

	p = buf;
	while ((tok = strsep(&p, ";"))) {
		if (strcasecmp(tok, "help") == 0) {
			printf(
"Application hint options:\n"
"    --hint=             Bind tasks according to application hints\n"
"        compute_bound   use all cores in each socket\n"
"        memory_bound    use only one core in each socket\n"
"        [no]multithread [don't] use extra threads with in-core multi-threading\n"
"        help            show this help message\n");
			return 1;
		} else if (strcasecmp(tok, "compute_bound") == 0) {
			*min_sockets = NO_VAL;
			*min_cores   = NO_VAL;
			*min_threads = 1;
			if (cpu_bind_type)
				*cpu_bind_type |= CPU_BIND_TO_CORES;
		} else if (strcasecmp(tok, "memory_bound") == 0) {
			*min_cores   = 1;
			*min_threads = 1;
			if (cpu_bind_type)
				*cpu_bind_type |= CPU_BIND_TO_CORES;
		} else if (strcasecmp(tok, "multithread") == 0) {
			*min_threads = NO_VAL;
			if (cpu_bind_type) {
				*cpu_bind_type |= CPU_BIND_TO_THREADS;
				*cpu_bind_type &=
					(~CPU_BIND_ONE_THREAD_PER_CORE);
			}
			if (*ntasks_per_core == NO_VAL)
				*ntasks_per_core = INFINITE;
		} else if (strcasecmp(tok, "nomultithread") == 0) {
			*min_threads = 1;
			if (cpu_bind_type) {
				*cpu_bind_type |= CPU_BIND_TO_THREADS;
				*cpu_bind_type |= CPU_BIND_ONE_THREAD_PER_CORE;
			}
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
	char *buf, *tok, *save_ptr = NULL;
	uint16_t rc = 0;

	if (!arg)
		return rc;

	buf = xstrdup(arg);
	tok = strtok_r(buf, ",", &save_ptr);
	while (tok) {
		if (strcasecmp(tok, "BEGIN") == 0)
			rc |= MAIL_JOB_BEGIN;
		else if  (strcasecmp(tok, "END") == 0)
			rc |= MAIL_JOB_END;
		else if (strcasecmp(tok, "FAIL") == 0)
			rc |= MAIL_JOB_FAIL;
		else if (strcasecmp(tok, "REQUEUE") == 0)
			rc |= MAIL_JOB_REQUEUE;
		else if (strcasecmp(tok, "ALL") == 0)
			rc |= MAIL_JOB_BEGIN |  MAIL_JOB_END |  MAIL_JOB_FAIL |
			      MAIL_JOB_REQUEUE;
		else if (strcasecmp(tok, "TIME_LIMIT") == 0)
			rc |= MAIL_JOB_TIME100;
		else if (strcasecmp(tok, "TIME_LIMIT_90") == 0)
			rc |= MAIL_JOB_TIME90;
		else if (strcasecmp(tok, "TIME_LIMIT_80") == 0)
			rc |= MAIL_JOB_TIME80;
		else if (strcasecmp(tok, "TIME_LIMIT_50") == 0)
			rc |= MAIL_JOB_TIME50;
		tok = strtok_r(NULL, ",", &save_ptr);
	}
	xfree(buf);

	return rc;
}
char *print_mail_type(const uint16_t type)
{
	static char buf[256];

	buf[0] = '\0';

	if (type == 0)
		return "NONE";

	if (type & MAIL_JOB_BEGIN) {
		if (buf[0])
			strcat(buf, ",");
		strcat(buf, "BEGIN");
	}
	if (type & MAIL_JOB_END) {
		if (buf[0])
			strcat(buf, ",");
		strcat(buf, "END");
	}
	if (type & MAIL_JOB_FAIL) {
		if (buf[0])
			strcat(buf, ",");
		strcat(buf, "FAIL");
	}
	if (type & MAIL_JOB_REQUEUE) {
		if (buf[0])
			strcat(buf, ",");
		strcat(buf, "REQUEUE");
	}
	if (type & MAIL_JOB_TIME50) {
		if (buf[0])
			strcat(buf, ",");
		strcat(buf, "TIME_LIMIT_50");
	}
	if (type & MAIL_JOB_TIME80) {
		if (buf[0])
			strcat(buf, ",");
		strcat(buf, "TIME_LIMIT_80");
	}
	if (type & MAIL_JOB_TIME90) {
		if (buf[0])
			strcat(buf, ",");
		strcat(buf, "TIME_LIMIT_90");
	}
	if (type & MAIL_JOB_TIME100) {
		if (buf[0])
			strcat(buf, ",");
		strcat(buf, "TIME_LIMIT");
	}

	return buf;
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
	char *path;
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

#if defined HAVE_BG && !defined HAVE_BG_L_P
	/* BGQ's runjob command required a fully qualified path */
	if ( (cmd[0] == '.' || cmd[0] == '/') &&
	     (access(cmd, access_mode) == 0 ) ) {
		if (cmd[0] == '.')
			xstrfmtcat(fullpath, "%s/", cwd);
		xstrcat(fullpath, cmd);
		goto done;
	}
#else
	if ((cmd[0] == '.') || (cmd[0] == '/'))
		return NULL;
#endif

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
	int dims = slurmdb_setup_cluster_dims();

	if ((dims == 0) || !geometry[0]
	    ||  (geometry[0] == (uint16_t)NO_VAL))
		return NULL;

	for (i=0; i<dims; i++) {
		if (i > 0)
			snprintf(buf, sizeof(buf), "x%u", geometry[i]);
		else
			snprintf(buf, sizeof(buf), "%u", geometry[i]);
		xstrcat(rc, buf);
	}

	return rc;
}

/* Translate a signal option string "--signal=<int>[@<time>]" into
 * it's warn_signal and warn_time components.
 * RET 0 on success, -1 on failure */
int get_signal_opts(char *optarg, uint16_t *warn_signal, uint16_t *warn_time,
		    uint16_t *warn_flags)
{
	char *endptr;
	long num;

	if (optarg == NULL)
		return -1;

	if (!strncasecmp(optarg, "B:", 2)) {
		*warn_flags = KILL_JOB_BATCH;
		optarg += 2;
	}

	endptr = strchr(optarg, '@');
	if (endptr)
		endptr[0] = '\0';
	num = (uint16_t) sig_name2num(optarg);
	if (endptr)
		endptr[0] = '@';
	if ((num < 1) || (num > 0x0ffff))
		return -1;
	*warn_signal = (uint16_t) num;

	if (!endptr) {
		*warn_time = 60;
		return 0;
	}

	num = strtol(endptr+1, &endptr, 10);
	if ((num < 0) || (num > 0x0ffff))
		return -1;
	*warn_time = (uint16_t) num;
	if (endptr[0] == '\0')
		return 0;
	return -1;
}

/* Convert a signal name to it's numeric equivalent.
 * Return -1 on failure */
int sig_name2num(char *signal_name)
{
	char *sig_name[] = {"HUP", "INT", "QUIT", "KILL", "TERM",
			    "USR1", "USR2", "CONT", NULL};
	int sig_num[] = {SIGHUP, SIGINT, SIGQUIT, SIGKILL, SIGTERM,
			       SIGUSR1, SIGUSR2, SIGCONT};
	char *ptr;
	long tmp;
	int sig;
	int i;

	tmp = strtol(signal_name, &ptr, 10);
	if (ptr != signal_name) { /* found a number */
		if (xstring_is_whitespace(ptr))
			sig = (int)tmp;
		else
			return 0;
	} else {
		ptr = (char *)signal_name;
		while (isspace((int)*ptr))
			ptr++;
		if (strncasecmp(ptr, "SIG", 3) == 0)
			ptr += 3;
		for (i = 0; ; i++) {
			if (sig_name[i] == NULL)
				return 0;
			if (strncasecmp(ptr, sig_name[i],
					strlen(sig_name[i])) == 0) {
				/* found the signal name */
				if (!xstring_is_whitespace(ptr +
							   strlen(sig_name[i])))
					return 0;
				sig = sig_num[i];
				break;
			}
		}
	}

	return sig;
}


/*
 * parse_uint32 - Convert anscii string to a 32 bit unsigned int.
 * IN      aval - ascii string.
 * IN/OUT  ival - 32 bit pointer.
 * RET     0 if no error, 1 otherwise.
 */
extern int parse_uint32(char *aval, uint32_t *ival)
{
	/*
	 * First,  convert the ascii value it to a
	 * long long int. If the result is greater
	 * than or equal to 0 and less than NO_VAL
	 * set the value and return. Otherwise return
	 * an error.
	 */
	uint32_t max32uint = (uint32_t) NO_VAL;
	long long tval;
	char *p;

	/*
 	 * Return error for invalid value.
	 */
	tval = strtoll(aval, &p, 10);
	if (p[0] || (tval == LLONG_MIN) || (tval == LLONG_MAX) ||
	    (tval < 0) || (tval >= max32uint))
		return 1;

	*ival = (uint32_t) tval;

	return 0;
}

/*
 * parse_uint16 - Convert anscii string to a 16 bit unsigned int.
 * IN      aval - ascii string.
 * IN/OUT  ival - 16 bit pointer.
 * RET     0 if no error, 1 otherwise.
 */
extern int parse_uint16(char *aval, uint16_t *ival)
{
	/*
	 * First,  convert the ascii value it to a
	 * long long int. If the result is greater then
	 * or equal to 0 and less than (uint16_t) NO_VAL
	 * set the value and return. Otherwise
	 * return an error.
	 */
	uint16_t max16uint = (uint16_t) NO_VAL;
	long long tval;
	char *p;

	/*
 	 * Return error for invalid value.
	 */
	tval = strtoll(aval, &p, 10);
	if (p[0] || (tval == LLONG_MIN) || (tval == LLONG_MAX) ||
	    (tval < 0) || (tval >= max16uint))
		return 1;

	*ival = (uint16_t) tval;

	return 0;
}

/* print_db_notok() - Print an error message about slurmdbd
 *                    is unreachable or wrong cluster name.
 * IN  cname - char * cluster name
 * IN  isenv - bool  cluster name from env or from command line option.
 */
void print_db_notok(const char *cname, bool isenv)
{
	if (errno)
		error("There is a problem talking to the database: %m.  "
		      "Only local cluster communication is available, remove "
		      "%s or contact your admin to resolve the problem.",
		      isenv ? "SLURM_CLUSTERS from your environment" :
		      "--cluster from your command line");
	else if (!strcasecmp("all", cname))
		error("No clusters can be reached now. "
		      "Contact your admin to resolve the problem.");
	else
		error("'%s' can't be reached now, "
		      "or it is an invalid entry for %s.  "
		      "Use 'sacctmgr list clusters' to see available clusters.",
		      cname, isenv ? "SLURM_CLUSTERS" : "--cluster");
}

static bool _check_is_pow_of_2(int32_t n) {
	/* Bitwise ANDing a power of 2 number like 16 with its
	 * negative (-16) gives itself back.  Only integers which are power of
	 * 2 behave like that.
	 */
	return ((n!=0) && (n&(-n))==n);
}

extern void bg_figure_nodes_tasks(int *min_nodes, int *max_nodes,
				  int *ntasks_per_node, bool *ntasks_set,
				  int *ntasks, bool nodes_set,
				  bool nodes_set_opt, bool overcommit,
				  bool set_tasks)
{
	/* BGQ has certain restrictions to run a job.  So lets validate
	 * and correct what the user asked for if possible.
	 */
	int32_t node_cnt;
	bool figured = false;
	uint32_t cluster_flags = slurmdb_setup_cluster_flags();

	if (!(cluster_flags & CLUSTER_FLAG_BGQ))
		fatal("bg_figure_nodes_tasks is only valid on a BGQ system.");

	if (!(*ntasks_set)
	    && (*ntasks_per_node) && (*ntasks_per_node != NO_VAL)) {
		if ((*ntasks_per_node != 1)
		    && (*ntasks_per_node != 2)
		    && (*ntasks_per_node != 4)
		    && (*ntasks_per_node != 8)
		    && (*ntasks_per_node != 16)
		    && (*ntasks_per_node != 32)
		    && (*ntasks_per_node != 64))
			fatal("You requested --ntasks-per-node=%d, "
			      "which is not valid, it must be a power of 2.  "
			      "Please validate your request and try again.",
			      *ntasks_per_node);
		else if (!overcommit
			 && ((*ntasks_per_node == 32)
			     || (*ntasks_per_node == 64)))
			fatal("You requested --ntasks-per-node=%d, "
			      "which is not valid without --overcommit.",
			      *ntasks_per_node);
	}

	if (*max_nodes)
		node_cnt = *max_nodes;
	else
		node_cnt = *min_nodes;

	if (*ntasks_set) {
		int32_t ntpn;

		if (nodes_set) {
			if (node_cnt > *ntasks) {
				if (nodes_set_opt)
					info("You asked for %d nodes, "
					     "but only %d tasks, resetting "
					     "node count to %u.",
					     node_cnt, *ntasks, *ntasks);
				*max_nodes = *min_nodes = node_cnt
					= *ntasks;
			}
		}
		/* If nodes not set do not try to set min/max nodes
		   yet since that would result in an incorrect
		   allocation.  For a step allocation it is figured
		   out later in srun_job.c _job_create_structure().
		*/

		if ((!*ntasks_per_node || (*ntasks_per_node == NO_VAL))) {
			/* We always want the next larger number if
			   there is a fraction so we try to stay in
			   the allocation requested.
			*/
			*ntasks_per_node =
				(*ntasks + node_cnt - 1) / node_cnt;
			figured = true;
		}

		/* On a Q we need ntasks_per_node to be a multiple of 2 */
		ntpn = *ntasks_per_node;
		while (!_check_is_pow_of_2(ntpn))
			ntpn++;
		if (!figured && ntpn > 64)
			fatal("You requested --ntasks-per-node=%d, "
			      "which is not a power of 2.  But the next "
			      "largest power of 2 (%d) is greater than the "
			      "largest valid power which is 64.  Please "
			      "validate your request and try again.",
			      *ntasks_per_node, ntpn);
		if (!figured && (ntpn != *ntasks_per_node)) {
			info("You requested --ntasks-per-node=%d, which is not "
			     "a power of 2.  Setting --ntasks-per-node=%d "
			     "for you.", *ntasks_per_node, ntpn);
			figured = true;
		}
		*ntasks_per_node = ntpn;

		/* We always want the next larger number if
		   there is a fraction so we try to stay in
		   the allocation requested.
		*/
		ntpn = ((*ntasks) + (*ntasks_per_node) - 1)
			/ (*ntasks_per_node);
		/* Make sure we are requesting the correct number of nodes. */
		if (node_cnt < ntpn) {
			*max_nodes = *min_nodes = ntpn;
			if (nodes_set && !figured) {
				fatal("You requested -N %d and -n %d "
				      "with --ntasks-per-node=%d.  "
				      "This isn't a valid request.",
				      node_cnt, *ntasks,
				      *ntasks_per_node);
			}
			node_cnt = *max_nodes;
		}

		/* Do this again to make sure we have a legitimate
		   ratio. */
		ntpn = *ntasks_per_node;
		if ((node_cnt * ntpn) < *ntasks) {
			ntpn++;
			while (!_check_is_pow_of_2(ntpn))
				ntpn++;
			if (!figured && (ntpn != *ntasks_per_node))
				info("You requested --ntasks-per-node=%d, "
				     "which cannot spread across %d nodes "
				     "correctly.  Setting --ntasks-per-node=%d "
				     "for you.",
				     *ntasks_per_node, node_cnt, ntpn);
			*ntasks_per_node = ntpn;
		} else if (!overcommit && ((node_cnt * ntpn) > *ntasks)) {
			ntpn = (*ntasks + node_cnt - 1) / node_cnt;
			while (!_check_is_pow_of_2(ntpn))
				ntpn++;
			if (!figured && (ntpn != *ntasks_per_node))
				info("You requested --ntasks-per-node=%d, "
				     "which is more than the tasks you "
				     "requested.  Setting --ntasks-per-node=%d "
				     "for you.",
				     *ntasks_per_node, ntpn);
			*ntasks_per_node = ntpn;
		}
	} else if (set_tasks) {
		if (*ntasks_per_node && (*ntasks_per_node != NO_VAL))
			*ntasks = node_cnt * (*ntasks_per_node);
		else {
			*ntasks = node_cnt;
			*ntasks_per_node = 1;
		}
		*ntasks_set = true;
	}

	/* If set_tasks isn't set we are coming in for the
	   allocation so verify it will work first before we
	   go any futher.
	*/
	if (nodes_set && (*ntasks_per_node && (*ntasks_per_node != NO_VAL))) {
		if ((*ntasks_per_node != 1)
		    && (*ntasks_per_node != 2)
		    && (*ntasks_per_node != 4)
		    && (*ntasks_per_node != 8)
		    && (*ntasks_per_node != 16)
		    && (*ntasks_per_node != 32)
		    && (*ntasks_per_node != 64)) {
			if (*ntasks_set)
				fatal("You requested -N %d and -n %d "
				      "which gives --ntasks-per-node=%d.  "
				      "This isn't a valid request.",
				      node_cnt, *ntasks,
				      *ntasks_per_node);
			else
				fatal("You requested -N %d and "
				      "--ntasks-per-node=%d.  "
				      "This isn't a valid request.",
				      node_cnt, *ntasks_per_node);
		} else if (!overcommit
			 && ((*ntasks_per_node == 32)
			     || (*ntasks_per_node == 64))) {
			if (*ntasks_set)
				fatal("You requested -N %d and -n %d "
				      "which gives --ntasks-per-node=%d.  "
				      "This isn't a valid request "
				      "without --overcommit.",
				      node_cnt, *ntasks,
				      *ntasks_per_node);
			else
				fatal("You requested -N %d and "
				      "--ntasks-per-node=%d.  "
				      "This isn't a valid request "
				      "without --overcommit.",
				      node_cnt, *ntasks_per_node);
		}
	}

	/* If we aren't setting tasks reset ntasks_per_node as well. */
	if (!set_tasks && figured)
		*ntasks_per_node = 0;

}

/* parse_resv_flags()
 */
uint32_t
parse_resv_flags(const char *flagstr, const char *msg)
{
	int flip;
	uint32_t outflags = 0;
	const char *curr = flagstr;
	int taglen = 0;

	while (*curr != '\0') {
		flip = 0;
		if (*curr == '+') {
			curr++;
		} else if (*curr == '-') {
			flip = 1;
			curr++;
		}
		taglen = 0;
		while (curr[taglen] != ',' && curr[taglen] != '\0')
			taglen++;

		if (strncasecmp(curr, "Maintenance", MAX(taglen,1)) == 0) {
			curr += taglen;
			if (flip)
				outflags |= RESERVE_FLAG_NO_MAINT;
			else
				outflags |= RESERVE_FLAG_MAINT;
		} else if ((strncasecmp(curr, "Overlap", MAX(taglen,1))
			    == 0) && (!flip)) {
			curr += taglen;
			outflags |= RESERVE_FLAG_OVERLAP;
			/* "-OVERLAP" is not supported since that's the
			 * default behavior and the option only applies
			 * for reservation creation, not updates */
		} else if (strncasecmp(curr, "Ignore_Jobs", MAX(taglen,1))
			   == 0) {
			curr += taglen;
			if (flip)
				outflags |= RESERVE_FLAG_NO_IGN_JOB;
			else
				outflags |= RESERVE_FLAG_IGN_JOBS;
		} else if (strncasecmp(curr, "Daily", MAX(taglen,1)) == 0) {
			curr += taglen;
			if (flip)
				outflags |= RESERVE_FLAG_NO_DAILY;
			else
				outflags |= RESERVE_FLAG_DAILY;
		} else if (strncasecmp(curr, "Weekly", MAX(taglen,1)) == 0) {
			curr += taglen;
			if (flip)
				outflags |= RESERVE_FLAG_NO_WEEKLY;
			else
				outflags |= RESERVE_FLAG_WEEKLY;
		} else if (strncasecmp(curr, "License_Only", MAX(taglen,1))
			   == 0) {
			curr += taglen;
			if (flip)
				outflags |= RESERVE_FLAG_NO_LIC_ONLY;
			else
				outflags |= RESERVE_FLAG_LIC_ONLY;
		} else if (strncasecmp(curr, "Static_Alloc", MAX(taglen,1))
			   == 0) {
			curr += taglen;
			if (flip)
				outflags |= RESERVE_FLAG_NO_STATIC;
			else
				outflags |= RESERVE_FLAG_STATIC;
		} else if (strncasecmp(curr, "Part_Nodes", MAX(taglen,1))
			   == 0) {
			curr += taglen;
			if (flip)
				outflags |= RESERVE_FLAG_NO_PART_NODES;
			else
				outflags |= RESERVE_FLAG_PART_NODES;
		} else if (!strncasecmp(curr, "First_Cores", MAX(taglen,1)) &&
			   !flip) {
			curr += taglen;
			outflags |= RESERVE_FLAG_FIRST_CORES;
		} else if (!strncasecmp(curr, "Time_Float", MAX(taglen,1)) &&
			   !flip) {
			curr += taglen;
			outflags |= RESERVE_FLAG_TIME_FLOAT;
		} else {
			error("Error parsing flags %s.  %s", flagstr, msg);
			return 0xffffffff;
		}

		if (*curr == ',') {
			curr++;
		}
	}
	return outflags;
}
