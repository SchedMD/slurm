/*****************************************************************************\
 *  proc_args.c - helper functions for command argument processing
 *****************************************************************************
 *  Copyright (C) 2007 Hewlett-Packard Development Company, L.P.
 *  Portions Copyright (C) 2010-2015 SchedMD LLC <https://www.schedmd.com>.
 *  Written by Christopher Holmes <cholmes@hp.com>, who borrowed heavily
 *  from existing Slurm source code, particularly src/srun/opt.c
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

#ifndef __USE_ISOC99
#define __USE_ISOC99
#endif

#define _GNU_SOURCE

#ifndef SYSTEM_DIMENSIONS
#  define SYSTEM_DIMENSIONS 1
#endif

#include <ctype.h>		/* isdigit    */
#include <fcntl.h>
#include <limits.h>
#include <pwd.h>		/* getpwuid   */
#include <stdarg.h>		/* va_start   */
#include <stdio.h>
#include <stdlib.h>		/* getenv, strtoll */
#include <string.h>		/* strcpy */
#include <sys/param.h>		/* MAXPATHLEN */
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <unistd.h>

#include "src/common/gres.h"
#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/proc_args.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_acct_gather_profile.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"


/* print this version of Slurm */
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
	    && ((distribution & SLURM_DIST_STATE_BASE) != SLURM_DIST_UNKNOWN)) {
		switch (distribution & SLURM_DIST_STATE_BASE) {
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
		case SLURM_DIST_CYCLIC_CYCLIC_CYCLIC:
			*dist      = "cyclic:cyclic:cyclic";
			*lllp_dist = "cyclic:cyclic";
			break;
		case SLURM_DIST_CYCLIC_CYCLIC_BLOCK:
			*dist      = "cyclic:cyclic:block";
			*lllp_dist = "cyclic:block";
			break;
		case SLURM_DIST_CYCLIC_CYCLIC_CFULL:
			*dist      = "cyclic:cyclic:fcyclic";
			*lllp_dist = "cyclic:fcyclic";
			break;
		case SLURM_DIST_CYCLIC_BLOCK_CYCLIC:
			*dist      = "cyclic:block:cyclic";
			*lllp_dist = "block:cyclic";
			break;
		case SLURM_DIST_CYCLIC_BLOCK_BLOCK:
			*dist      = "cyclic:block:block";
			*lllp_dist = "block:block";
			break;
		case SLURM_DIST_CYCLIC_BLOCK_CFULL:
			*dist      = "cyclic:cylic:cyclic";
			*lllp_dist = "cyclic:cyclic";
			break;
		case SLURM_DIST_CYCLIC_CFULL_CYCLIC:
			*dist      = "cyclic:cylic:cyclic";
			*lllp_dist = "cyclic:cyclic";
			break;
		case SLURM_DIST_CYCLIC_CFULL_BLOCK:
			*dist      = "cyclic:fcyclic:block";
			*lllp_dist = "fcyclic:block";
			break;
		case SLURM_DIST_CYCLIC_CFULL_CFULL:
			*dist      = "cyclic:fcyclic:fcyclic";
			*lllp_dist = "fcyclic:fcyclic";
			break;
		case SLURM_DIST_BLOCK_CYCLIC_CYCLIC:
			*dist      = "block:cyclic:cyclic";
			*lllp_dist = "cyclic:cyclic";
			break;
		case SLURM_DIST_BLOCK_CYCLIC_BLOCK:
			*dist      = "block:cyclic:block";
			*lllp_dist = "cyclic:block";
			break;
		case SLURM_DIST_BLOCK_CYCLIC_CFULL:
			*dist      = "block:cyclic:fcyclic";
			*lllp_dist = "cyclic:fcyclic";
			break;
		case SLURM_DIST_BLOCK_BLOCK_CYCLIC:
			*dist      = "block:block:cyclic";
			*lllp_dist = "block:cyclic";
			break;
		case SLURM_DIST_BLOCK_BLOCK_BLOCK:
			*dist      = "block:block:block";
			*lllp_dist = "block:block";
			break;
		case SLURM_DIST_BLOCK_BLOCK_CFULL:
			*dist      = "block:block:fcyclic";
			*lllp_dist = "block:fcyclic";
			break;
		case SLURM_DIST_BLOCK_CFULL_CYCLIC:
			*dist      = "block:fcyclic:cyclic";
			*lllp_dist = "fcyclic:cyclic";
			break;
		case SLURM_DIST_BLOCK_CFULL_BLOCK:
			*dist      = "block:fcyclic:block";
			*lllp_dist = "fcyclic:block";
			break;
		case SLURM_DIST_BLOCK_CFULL_CFULL:
			*dist      = "block:fcyclic:fcyclic";
			*lllp_dist = "fcyclic:fcyclic";
			break;
		default:
			error("unknown dist, type 0x%X", distribution);
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
	int len;
	char *dist_str = NULL;
	task_dist_states_t result = SLURM_DIST_UNKNOWN;
	bool pack_nodes = false, no_pack_nodes = false;
	char *tok, *tmp, *save_ptr = NULL;
	int i, j;
	char *cur_ptr;
	char buf[3][25];
	buf[0][0] = '\0';
	buf[1][0] = '\0';
	buf[2][0] = '\0';
	char outstr[100];
	outstr[0]='\0';

	if (!arg)
		return result;

	tmp = xstrdup(arg);
	tok = strtok_r(tmp, ",", &save_ptr);
	while (tok) {
		bool lllp_dist = false, plane_dist = false;
		len = strlen(tok);
		dist_str = strchr(tok, ':');
		if (dist_str != NULL) {
			/* -m cyclic|block:cyclic|block */
			lllp_dist = true;
		} else {
			/* -m plane=<plane_size> */
			dist_str = strchr(tok, '=');
			if (!dist_str)
				dist_str = getenv("SLURM_DIST_PLANESIZE");
			else {
				len = dist_str - tok;
				dist_str++;
			}
			if (dist_str) {
				*plane_size = atoi(dist_str);
				plane_dist = true;
			}
		}

		cur_ptr = tok;
	 	for (j = 0; j < 3; j++) {
			for (i = 0; i < 24; i++) {
				if (*cur_ptr == '\0' || *cur_ptr ==':')
					break;
				buf[j][i] = *cur_ptr++;
			}
			buf[j][i] = '\0';
			if (*cur_ptr == '\0')
				break;
			buf[j][i] = '\0';
			cur_ptr++;
		}
		if (xstrcmp(buf[0], "*") == 0)
			/* default node distribution is block */
			strcpy(buf[0], "block");
		strcat(outstr, buf[0]);
		if (xstrcmp(buf[1], "\0") != 0) {
			strcat(outstr, ":");
			if (!xstrcmp(buf[1], "*") || !xstrcmp(buf[1], "\0")) {
				/* default socket distribution is cyclic */
				strcpy(buf[1], "cyclic");
			}
			strcat(outstr, buf[1]);
		}
		if (xstrcmp(buf[2], "\0") != 0) {
			strcat(outstr, ":");
			if (!xstrcmp(buf[2], "*") || !xstrcmp(buf[2], "\0")) {
				/* default core dist is inherited socket dist */
				strcpy(buf[2], buf[1]);
			}
			strcat(outstr, buf[2]);
		}

		if (lllp_dist) {
			if (xstrcasecmp(outstr, "cyclic:cyclic") == 0) {
				result = SLURM_DIST_CYCLIC_CYCLIC;
			} else if (xstrcasecmp(outstr, "cyclic:block") == 0) {
				result = SLURM_DIST_CYCLIC_BLOCK;
			} else if (xstrcasecmp(outstr, "block:block") == 0) {
				result = SLURM_DIST_BLOCK_BLOCK;
			} else if (xstrcasecmp(outstr, "block:cyclic") == 0) {
				result = SLURM_DIST_BLOCK_CYCLIC;
			} else if (xstrcasecmp(outstr, "block:fcyclic") == 0) {
				result = SLURM_DIST_BLOCK_CFULL;
			} else if (xstrcasecmp(outstr, "cyclic:fcyclic") == 0) {
				result = SLURM_DIST_CYCLIC_CFULL;
			} else if (xstrcasecmp(outstr, "cyclic:cyclic:cyclic")
				   == 0) {
				result = SLURM_DIST_CYCLIC_CYCLIC_CYCLIC;
			} else if (xstrcasecmp(outstr, "cyclic:cyclic:block")
				   == 0) {
				result = SLURM_DIST_CYCLIC_CYCLIC_BLOCK;
			} else if (xstrcasecmp(outstr, "cyclic:cyclic:fcyclic")
				== 0) {
				result = SLURM_DIST_CYCLIC_CYCLIC_CFULL;
			} else if (xstrcasecmp(outstr, "cyclic:block:cyclic")
				== 0) {
				result = SLURM_DIST_CYCLIC_BLOCK_CYCLIC;
			} else if (xstrcasecmp(outstr, "cyclic:block:block")
				== 0) {
				result = SLURM_DIST_CYCLIC_BLOCK_BLOCK;
			} else if (xstrcasecmp(outstr, "cyclic:block:fcyclic")
				== 0) {
				result = SLURM_DIST_CYCLIC_BLOCK_CFULL;
			} else if (xstrcasecmp(outstr, "cyclic:fcyclic:cyclic")
				== 0) {
				result = SLURM_DIST_CYCLIC_CFULL_CYCLIC;
			} else if (xstrcasecmp(outstr, "cyclic:fcyclic:block")
				== 0) {
				result = SLURM_DIST_CYCLIC_CFULL_BLOCK;
			} else if (xstrcasecmp(outstr, "cyclic:fcyclic:fcyclic")
				== 0) {
				result = SLURM_DIST_CYCLIC_CFULL_CFULL;
			} else if (xstrcasecmp(outstr, "block:cyclic:cyclic")
				== 0) {
				result = SLURM_DIST_BLOCK_CYCLIC_CYCLIC;
			} else if (xstrcasecmp(outstr, "block:cyclic:block")
				== 0) {
				result = SLURM_DIST_BLOCK_CYCLIC_BLOCK;
			} else if (xstrcasecmp(outstr, "block:cyclic:fcyclic")
				== 0) {
				result = SLURM_DIST_BLOCK_CYCLIC_CFULL;
			} else if (xstrcasecmp(outstr, "block:block:cyclic")
				== 0) {
				result = SLURM_DIST_BLOCK_BLOCK_CYCLIC;
			} else if (xstrcasecmp(outstr, "block:block:block")
				== 0) {
				result = SLURM_DIST_BLOCK_BLOCK_BLOCK;
			} else if (xstrcasecmp(outstr, "block:block:fcyclic")
				== 0) {
				result = SLURM_DIST_BLOCK_BLOCK_CFULL;
			} else if (xstrcasecmp(outstr, "block:fcyclic:cyclic")
				== 0) {
				result = SLURM_DIST_BLOCK_CFULL_CYCLIC;
			} else if (xstrcasecmp(outstr, "block:fcyclic:block")
				== 0) {
				result = SLURM_DIST_BLOCK_CFULL_BLOCK;
			} else if (xstrcasecmp(outstr, "block:fcyclic:fcyclic")
				== 0) {
				result = SLURM_DIST_BLOCK_CFULL_CFULL;
			}
		} else if (plane_dist) {
			if (xstrncasecmp(tok, "plane", len) == 0) {
				result = SLURM_DIST_PLANE;
			}
		} else {
			if (xstrncasecmp(tok, "cyclic", len) == 0) {
				result = SLURM_DIST_CYCLIC;
			} else if (xstrncasecmp(tok, "block", len) == 0) {
				result = SLURM_DIST_BLOCK;
			} else if ((xstrncasecmp(tok, "arbitrary", len) == 0) ||
				   (xstrncasecmp(tok, "hostfile", len) == 0)) {
				result = SLURM_DIST_ARBITRARY;
			} else if (xstrncasecmp(tok, "nopack", len) == 0) {
				no_pack_nodes = true;
			} else if (xstrncasecmp(tok, "pack", len) == 0) {
				pack_nodes = true;
			}
		}
		tok = strtok_r(NULL, ",", &save_ptr);
	}
	xfree(tmp);

	if (pack_nodes)
		result |= SLURM_DIST_PACK_NODES;
	else if (no_pack_nodes)
		result |= SLURM_DIST_NO_PACK_NODES;

	return result;
}

extern char *format_task_dist_states(task_dist_states_t t)
{
	switch (t & SLURM_DIST_STATE_BASE) {
	case SLURM_DIST_BLOCK:
		return "block";
	case SLURM_DIST_CYCLIC:
		return "cyclic";
	case SLURM_DIST_PLANE:
		return "plane";
	case SLURM_DIST_ARBITRARY:
		return "arbitrary";
	case SLURM_DIST_CYCLIC_CYCLIC:
		return "cyclic:cyclic";
	case SLURM_DIST_CYCLIC_BLOCK:
		return "cyclic:block";
	case SLURM_DIST_CYCLIC_CFULL:
		return "cyclic:fcyclic";
	case SLURM_DIST_BLOCK_CYCLIC:
		return "block:cyclic";
	case SLURM_DIST_BLOCK_BLOCK:
		return "block:block";
	case SLURM_DIST_BLOCK_CFULL:
		return "block:fcyclic";
	case SLURM_DIST_CYCLIC_CYCLIC_CYCLIC:
		return "cyclic:cyclic:cyclic";
	case SLURM_DIST_CYCLIC_CYCLIC_BLOCK:
		return "cyclic:cyclic:block";
	case SLURM_DIST_CYCLIC_CYCLIC_CFULL:
		return "cyclic:cyclic:fcyclic";
	case SLURM_DIST_CYCLIC_BLOCK_CYCLIC:
		return "cyclic:block:cyclic";
	case SLURM_DIST_CYCLIC_BLOCK_BLOCK:
		return "cyclic:block:block";
	case SLURM_DIST_CYCLIC_BLOCK_CFULL:
		return "cyclic:block:fcyclic";
	case SLURM_DIST_CYCLIC_CFULL_CYCLIC:
		return "cyclic:fcyclic:cyclic" ;
	case SLURM_DIST_CYCLIC_CFULL_BLOCK:
		return "cyclic:fcyclic:block";
	case SLURM_DIST_CYCLIC_CFULL_CFULL:
		return "cyclic:fcyclic:fcyclic";
	case SLURM_DIST_BLOCK_CYCLIC_CYCLIC:
		return "block:cyclic:cyclic";
	case SLURM_DIST_BLOCK_CYCLIC_BLOCK:
		return "block:cyclic:block";
	case SLURM_DIST_BLOCK_CYCLIC_CFULL:
		return "block:cyclic:fcyclic";
	case SLURM_DIST_BLOCK_BLOCK_CYCLIC:
		return "block:block:cyclic";
	case SLURM_DIST_BLOCK_BLOCK_BLOCK:
		return "block:block:block";
	case SLURM_DIST_BLOCK_BLOCK_CFULL:
		return "block:block:fcyclic";
	case SLURM_DIST_BLOCK_CFULL_CYCLIC:
		return "block:fcyclic:cyclic";
	case SLURM_DIST_BLOCK_CFULL_BLOCK:
		return "block:fcyclic:block";
	case SLURM_DIST_BLOCK_CFULL_CFULL:
		return "block:fcyclic:fcyclic";
	default:
		return "unknown";
	}
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

static long _str_to_mbtyes(const char *arg, int use_gbytes)
{
	long result;
	char *endptr;

	errno = 0;
	result = strtol(arg, &endptr, 10);
	if ((errno != 0) && ((result == LONG_MIN) || (result == LONG_MAX)))
		result = -1;
	else if ((endptr[0] == '\0') && (use_gbytes == 1))  /* GB default */
		result *= 1024;
	else if (endptr[0] == '\0')	/* MB default */
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

/*
 * str_to_mbytes(): verify that arg is numeric with optional "K", "M", "G"
 * or "T" at end and return the number in mega-bytes. Default units are MB.
 */
long str_to_mbytes(const char *arg)
{
	return _str_to_mbtyes(arg, 0);
}

/*
 * str_to_mbytes2(): verify that arg is numeric with optional "K", "M", "G"
 * or "T" at end and return the number in mega-bytes. Default units are GB
 * if "SchedulerParameters=default_gbytes" is configured, otherwise MB.
 */
long str_to_mbytes2(const char *arg)
{
	static int use_gbytes = -1;

	if (use_gbytes == -1) {
		char *sched_params = slurm_get_sched_params();
		if (sched_params && strstr(sched_params, "default_gbytes"))
			use_gbytes = 1;
		else
			use_gbytes = 0;
		xfree(sched_params);
	}

	return _str_to_mbtyes(arg, use_gbytes);
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
		return -1;
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

	/*
	 * Does the string contain a "-" character?  If so, treat as a range.
	 * otherwise treat as an absolute node count.
	 */
	if ((ptr = xstrchr(arg, '-')) != NULL) {
		min_str = xstrndup(arg, ptr-arg);
		*min_nodes = _str_to_nodes(min_str, &leftover);
		if (!xstring_is_whitespace(leftover)) {
			error("\"%s\" is not a valid node count", min_str);
			xfree(min_str);
			return false;
		}
		xfree(min_str);
		if (*min_nodes < 0)
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
		if (*min_nodes < 0) {
			error("\"%s\" is not a valid node count", arg);
			return false;
		}
	}

	if ((*max_nodes != 0) && (*max_nodes < *min_nodes)) {
		error("Maximum node count %d is less than minimum node count %d",
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
	if ((dist & SLURM_DIST_STATE_BASE) == SLURM_DIST_ARBITRARY)
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

	if (((*p != '\0') && (*p != '-')) || (result < 0L)) {
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
	bool tmp_val, ret_val;
	int i, j;
	int max_sockets = 0, max_cores = 0, max_threads = 0;
	const char *cur_ptr = arg;
	char buf[3][48]; /* each can hold INT64_MAX - INT64_MAX */

	if (!arg) {
		error("%s: argument is NULL", __func__);
		return false;
	}
	memset(buf, 0, sizeof(buf));
	for (j = 0; j < 3; j++) {
		for (i = 0; i < 47; i++) {
			if (*cur_ptr == '\0' || *cur_ptr ==':')
				break;
			buf[j][i] = *cur_ptr++;
		}
		if (*cur_ptr == '\0')
			break;
		xassert(*cur_ptr == ':');
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

	if (!arg)
		return true;

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
		if (xstrcasecmp(tok, "help") == 0) {
			printf(
"Application hint options:\n"
"    --hint=             Bind tasks according to application hints\n"
"        compute_bound   use all cores in each socket\n"
"        memory_bound    use only one core in each socket\n"
"        [no]multithread [don't] use extra threads with in-core multi-threading\n"
"        help            show this help message\n");
			xfree(buf);
			return 1;
		} else if (xstrcasecmp(tok, "compute_bound") == 0) {
			*min_sockets = NO_VAL;
			*min_cores   = NO_VAL;
			*min_threads = 1;
			if (cpu_bind_type)
				*cpu_bind_type |= CPU_BIND_TO_CORES;
		} else if (xstrcasecmp(tok, "memory_bound") == 0) {
			*min_cores   = 1;
			*min_threads = 1;
			if (cpu_bind_type)
				*cpu_bind_type |= CPU_BIND_TO_CORES;
		} else if (xstrcasecmp(tok, "multithread") == 0) {
			*min_threads = NO_VAL;
			if (cpu_bind_type) {
				*cpu_bind_type |= CPU_BIND_TO_THREADS;
				*cpu_bind_type &=
					(~CPU_BIND_ONE_THREAD_PER_CORE);
			}
			if (*ntasks_per_core == NO_VAL)
				*ntasks_per_core = INFINITE;
		} else if (xstrcasecmp(tok, "nomultithread") == 0) {
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

	if (!cpu_bind_type)
		setenvf(NULL, "SLURM_HINT", "%s", arg);

	xfree(buf);
	return 0;
}

uint16_t parse_mail_type(const char *arg)
{
	char *buf, *tok, *save_ptr = NULL;
	uint16_t rc = 0;
	bool none_set = false;

	if (!arg)
		return INFINITE16;

	buf = xstrdup(arg);
	tok = strtok_r(buf, ",", &save_ptr);
	while (tok) {
		if (xstrcasecmp(tok, "NONE") == 0) {
			rc = 0;
			none_set = true;
			break;
		}
		else if (xstrcasecmp(tok, "ARRAY_TASKS") == 0)
			rc |= MAIL_ARRAY_TASKS;
		else if (xstrcasecmp(tok, "BEGIN") == 0)
			rc |= MAIL_JOB_BEGIN;
		else if  (xstrcasecmp(tok, "END") == 0)
			rc |= MAIL_JOB_END;
		else if (xstrcasecmp(tok, "FAIL") == 0)
			rc |= MAIL_JOB_FAIL;
		else if (xstrcasecmp(tok, "REQUEUE") == 0)
			rc |= MAIL_JOB_REQUEUE;
		else if (xstrcasecmp(tok, "ALL") == 0)
			rc |= MAIL_JOB_BEGIN |  MAIL_JOB_END |  MAIL_JOB_FAIL |
			      MAIL_JOB_REQUEUE | MAIL_JOB_STAGE_OUT;
		else if (!xstrcasecmp(tok, "STAGE_OUT"))
			rc |= MAIL_JOB_STAGE_OUT;
		else if (xstrcasecmp(tok, "TIME_LIMIT") == 0)
			rc |= MAIL_JOB_TIME100;
		else if (xstrcasecmp(tok, "TIME_LIMIT_90") == 0)
			rc |= MAIL_JOB_TIME90;
		else if (xstrcasecmp(tok, "TIME_LIMIT_80") == 0)
			rc |= MAIL_JOB_TIME80;
		else if (xstrcasecmp(tok, "TIME_LIMIT_50") == 0)
			rc |= MAIL_JOB_TIME50;
		tok = strtok_r(NULL, ",", &save_ptr);
	}
	xfree(buf);
	if (!rc && !none_set)
		rc = INFINITE16;

	return rc;
}
char *print_mail_type(const uint16_t type)
{
	static char buf[256];

	buf[0] = '\0';

	if (type == 0)
		return "NONE";

	if (type & MAIL_ARRAY_TASKS) {
		if (buf[0])
			strcat(buf, ",");
		strcat(buf, "ARRAY_TASKS");
	}
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
	if (type & MAIL_JOB_STAGE_OUT) {
		if (buf[0])
			strcat(buf, ",");
		strcat(buf, "STAGE_OUT");
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

/*
 * Check a specific path to see if the executable exists and is not a directory
 * IN path - path of executable to check
 * RET true if path exists and is not a directory; false otherwise
 */
static bool _exists(const char *path)
{
	struct stat st;
        if (stat(path, &st)) {
		debug2("_check_exec: failed to stat path %s", path);
		return false;
	}
	if (S_ISDIR(st.st_mode)) {
		debug2("_check_exec: path %s is a directory", path);
		return false;
	}
	return true;
}

/*
 * Check a specific path to see if the executable is accessible
 * IN path - path of executable to check
 * IN access_mode - determine if executable is accessible to caller with
 *		    specified mode
 * RET true if path is accessible according to access mode, false otherwise
 */
static bool _accessible(const char *path, int access_mode)
{
	if (access(path, access_mode)) {
		debug2("_check_exec: path %s is not accessible", path);
		return false;
	}
	return true;
}

/*
 * search PATH to confirm the location and access mode of the given command
 * IN cwd - current working directory
 * IN cmd - command to execute
 * IN check_current_dir - if true, search cwd for the command
 * IN access_mode - required access rights of cmd
 * IN test_exec - if false, do not confirm access mode of cmd if full path
 * RET full path of cmd or NULL if not found
 */
char *search_path(char *cwd, char *cmd, bool check_current_dir, int access_mode,
		  bool test_exec)
{
	List         l        = NULL;
	ListIterator i        = NULL;
	char *path, *fullpath = NULL;

	/* Relative path */
	if (cmd[0] == '.') {
		if (test_exec) {
			char *cmd1 = xstrdup_printf("%s/%s", cwd, cmd);
			if (_exists(cmd1) && _accessible(cmd1, access_mode))
				fullpath = xstrdup(cmd1);
			xfree(cmd1);
		}
		return fullpath;
	}
	/* Absolute path */
	if (cmd[0] == '/') {
		if (test_exec && _exists(cmd) && _accessible(cmd, access_mode))
			fullpath = xstrdup(cmd);
		return fullpath;
	}
	/* Otherwise search in PATH */
	l = _create_path_list();
	if (l == NULL)
		return NULL;

	/* Check cwd last, so local binaries do not trump binaries in PATH */
	if (check_current_dir)
		list_append(l, xstrdup(cwd));

	i = list_iterator_create(l);
	while ((path = list_next(i))) {
		if (path[0] == '.')
			xstrfmtcat(fullpath, "%s/%s/%s", cwd, path, cmd);
		else
			xstrfmtcat(fullpath, "%s/%s", path, cmd);
		/* Use first executable found in PATH */
		if (_exists(fullpath)) {
			if (!test_exec)
				break;
			if (_accessible(path, access_mode))
				break;
		}
		xfree(fullpath);
	}
	list_iterator_destroy(i);
	FREE_NULL_LIST(l);
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
	    ||  (geometry[0] == NO_VAL16))
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

	if (!xstrncasecmp(optarg, "B:", 2)) {
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
 * Return 0 on failure */
int sig_name2num(char *signal_name)
{
	struct signal_name_value {
		char *name;
		uint16_t val;
	} signals[] = {
		{ "HUP",	SIGHUP	},
		{ "INT",	SIGINT	},
		{ "QUIT",	SIGQUIT	},
		{ "ABRT",	SIGABRT	},
		{ "KILL",	SIGKILL	},
		{ "ALRM",	SIGALRM	},
		{ "TERM",	SIGTERM	},
		{ "USR1",	SIGUSR1	},
		{ "USR2",	SIGUSR2	},
		{ "URG",	SIGURG	},
		{ "CONT",	SIGCONT	},
		{ "STOP",	SIGSTOP	},
		{ "TSTP",	SIGTSTP	},
		{ "TTIN",	SIGTTIN	},
		{ "TTOU",	SIGTTOU	},
		{ NULL,		0	}	/* terminate array */
	};
	char *ptr;
	long tmp;
	int i;

	tmp = strtol(signal_name, &ptr, 10);
	if (ptr != signal_name) { /* found a number */
		if (xstring_is_whitespace(ptr))
			return (int)tmp;
		else
			return 0;
	}

	/* search the array */
	ptr = signal_name;
	while (isspace((int)*ptr))
		ptr++;
	if (xstrncasecmp(ptr, "SIG", 3) == 0)
		ptr += 3;
	for (i = 0; ; i++) {
		int siglen;
		if (signals[i].name == NULL)
			return 0;
		siglen = strlen(signals[i].name);
		if ((!xstrncasecmp(ptr, signals[i].name, siglen)
		    && xstring_is_whitespace(ptr + siglen))) {
			/* found the signal name */
			return signals[i].val;
		}
	}

	return 0;	/* not found */
}

/*
 * parse_uint16 - Convert ascii string to a 16 bit unsigned int.
 * IN      aval - ascii string.
 * IN/OUT  ival - 16 bit pointer.
 * RET     0 if no error, 1 otherwise.
 */
extern int parse_uint16(char *aval, uint16_t *ival)
{
	/*
	 * First,  convert the ascii value it to a
	 * long long int. If the result is greater then
	 * or equal to 0 and less than NO_VAL16
	 * set the value and return. Otherwise
	 * return an error.
	 */
	uint16_t max16uint = NO_VAL16;
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

/*
 * parse_uint32 - Convert ascii string to a 32 bit unsigned int.
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
	uint32_t max32uint = NO_VAL;
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
 * parse_uint64 - Convert ascii string to a 64 bit unsigned int.
 * IN      aval - ascii string.
 * IN/OUT  ival - 64 bit pointer.
 * RET     0 if no error, 1 otherwise.
 */
extern int parse_uint64(char *aval, uint64_t *ival)
{
	/*
	 * First,  convert the ascii value it to an
	 * unsigned long long. If the result is greater
	 * than or equal to 0 and less than NO_VAL
	 * set the value and return. Otherwise return
	 * an error.
	 */
	uint64_t max64uint = NO_VAL64;
	long long tval;
	char *p;

	/*
 	 * Return error for invalid value.
	 */
	tval = strtoll(aval, &p, 10);
	if (p[0] || (tval == LLONG_MIN) || (tval == LLONG_MAX) ||
	    (tval < 0) || (tval >= max64uint))
		return 1;

	*ival = (uint64_t) tval;

	return 0;
}

/*
 *  Get a decimal integer from arg.
 *
 *  Returns the integer on success, exits program on failure.
 */
extern int parse_int(const char *name, const char *val, bool positive)
{
	char *p = NULL;
	long int result = 0;

	if (val)
		result = strtol(val, &p, 10);

	if ((p == NULL) || (p[0] != '\0') || (result < 0L) ||
	    (positive && (result <= 0L))) {
		error ("Invalid numeric value \"%s\" for %s.", val, name);
		exit(1);
	} else if (result == LONG_MAX) {
		error ("Numeric argument (%ld) to big for %s.", result, name);
		exit(1);
	} else if (result == LONG_MIN) {
		error ("Numeric argument (%ld) to small for %s.", result, name);
		exit(1);
	}

	return (int) result;
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
	else if (!xstrcasecmp("all", cname))
		error("No clusters can be reached now. "
		      "Contact your admin to resolve the problem.");
	else
		error("'%s' can't be reached now, "
		      "or it is an invalid entry for %s.  "
		      "Use 'sacctmgr list clusters' to see available clusters.",
		      cname, isenv ? "SLURM_CLUSTERS" : "--cluster");
}

/*
 * parse_resv_flags() used to parse the Flags= option.  It handles
 * daily, weekly, static_alloc, part_nodes, and maint, optionally
 * preceded by + or -, separated by a comma but no spaces.
 *
 * flagstr IN - reservation flag string
 * msg IN - string to append to error message (e.g. function name)
 * RET equivalent reservation flag bits
 */
extern uint64_t parse_resv_flags(const char *flagstr, const char *msg)
{
	int flip;
	uint64_t outflags = 0;
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

		if (xstrncasecmp(curr, "Maintenance", MAX(taglen,1)) == 0) {
			curr += taglen;
			if (flip)
				outflags |= RESERVE_FLAG_NO_MAINT;
			else
				outflags |= RESERVE_FLAG_MAINT;
		} else if ((xstrncasecmp(curr, "Overlap", MAX(taglen,1))
			    == 0) && (!flip)) {
			curr += taglen;
			outflags |= RESERVE_FLAG_OVERLAP;
			/*
			 * "-OVERLAP" is not supported since that's the
			 * default behavior and the option only applies
			 * for reservation creation, not updates
			 */
		} else if (xstrncasecmp(curr, "Flex", MAX(taglen,1)) == 0) {
			curr += taglen;
			if (flip)
				outflags |= RESERVE_FLAG_NO_FLEX;
			else
				outflags |= RESERVE_FLAG_FLEX;
		} else if (xstrncasecmp(curr, "Ignore_Jobs", MAX(taglen,1))
			   == 0) {
			curr += taglen;
			if (flip)
				outflags |= RESERVE_FLAG_NO_IGN_JOB;
			else
				outflags |= RESERVE_FLAG_IGN_JOBS;
		} else if (xstrncasecmp(curr, "Daily", MAX(taglen,1)) == 0) {
			curr += taglen;
			if (flip)
				outflags |= RESERVE_FLAG_NO_DAILY;
			else
				outflags |= RESERVE_FLAG_DAILY;
		} else if (xstrncasecmp(curr, "Weekday", MAX(taglen,1)) == 0) {
			curr += taglen;
			if (flip)
				outflags |= RESERVE_FLAG_NO_WEEKDAY;
			else
				outflags |= RESERVE_FLAG_WEEKDAY;
		} else if (xstrncasecmp(curr, "Weekend", MAX(taglen,1)) == 0) {
			curr += taglen;
			if (flip)
				outflags |= RESERVE_FLAG_NO_WEEKEND;
			else
				outflags |= RESERVE_FLAG_WEEKEND;
		} else if (xstrncasecmp(curr, "Weekly", MAX(taglen,1)) == 0) {
			curr += taglen;
			if (flip)
				outflags |= RESERVE_FLAG_NO_WEEKLY;
			else
				outflags |= RESERVE_FLAG_WEEKLY;
		} else if (!xstrncasecmp(curr, "Any_Nodes", MAX(taglen,1)) ||
			   !xstrncasecmp(curr, "License_Only", MAX(taglen,1))) {
			curr += taglen;
			if (flip)
				outflags |= RESERVE_FLAG_NO_ANY_NODES;
			else
				outflags |= RESERVE_FLAG_ANY_NODES;
		} else if (xstrncasecmp(curr, "Static_Alloc", MAX(taglen,1))
			   == 0) {
			curr += taglen;
			if (flip)
				outflags |= RESERVE_FLAG_NO_STATIC;
			else
				outflags |= RESERVE_FLAG_STATIC;
		} else if (xstrncasecmp(curr, "Part_Nodes", MAX(taglen, 2))
			   == 0) {
			curr += taglen;
			if (flip)
				outflags |= RESERVE_FLAG_NO_PART_NODES;
			else
				outflags |= RESERVE_FLAG_PART_NODES;
		} else if (xstrncasecmp(curr, "PURGE_COMP", MAX(taglen, 2))
			   == 0) {
			curr += taglen;
			if (flip)
				outflags |= RESERVE_FLAG_NO_PURGE_COMP;
			else
				outflags |= RESERVE_FLAG_PURGE_COMP;
		} else if (!xstrncasecmp(curr, "First_Cores", MAX(taglen,1)) &&
			   !flip) {
			curr += taglen;
			outflags |= RESERVE_FLAG_FIRST_CORES;
		} else if (!xstrncasecmp(curr, "Time_Float", MAX(taglen,1)) &&
			   !flip) {
			curr += taglen;
			outflags |= RESERVE_FLAG_TIME_FLOAT;
		} else if (!xstrncasecmp(curr, "Replace", MAX(taglen, 1)) &&
			   !flip) {
			curr += taglen;
			outflags |= RESERVE_FLAG_REPLACE;
		} else if (!xstrncasecmp(curr, "Replace_Down", MAX(taglen, 8))
			   && !flip) {
			curr += taglen;
			outflags |= RESERVE_FLAG_REPLACE_DOWN;
		} else if (!xstrncasecmp(curr, "NO_HOLD_JOBS_AFTER_END",
					 MAX(taglen, 1)) && !flip) {
			curr += taglen;
			outflags |= RESERVE_FLAG_NO_HOLD_JOBS;
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

/* parse --compress for a compression type, set to default type if not found */
uint16_t parse_compress_type(const char *arg)
{
	/* if called with null string return default compression type */
	if (!arg) {
#if HAVE_LZ4
		return COMPRESS_LZ4;
#elif HAVE_LIBZ
		return COMPRESS_ZLIB;
#else
		error("No compression library available,"
		      " compression disabled.");
		return COMPRESS_OFF;
#endif
	}

	if (!strcasecmp(arg, "zlib"))
		return COMPRESS_ZLIB;
	else if (!strcasecmp(arg, "lz4"))
		return COMPRESS_LZ4;
	else if (!strcasecmp(arg, "none"))
		return COMPRESS_OFF;

	error("Compression type '%s' unknown, disabling compression support.",
	      arg);
	return COMPRESS_OFF;
}

extern int validate_acctg_freq(char *acctg_freq)
{
	int i;
	char *save_ptr = NULL, *tok, *tmp;
	bool valid;
	int rc = SLURM_SUCCESS;

	if (!optarg)
		return rc;

	tmp = xstrdup(optarg);
	tok = strtok_r(tmp, ",", &save_ptr);
	while (tok) {
		valid = false;
		for (i = 0; i < PROFILE_CNT; i++)
			if (acct_gather_parse_freq(i, tok) != -1) {
				valid = true;
				break;
			}

		if (!valid) {
			error("Invalid --acctg-freq specification: %s", tok);
			rc = SLURM_ERROR;
		}
		tok = strtok_r(NULL, ",", &save_ptr);
	}
	xfree(tmp);

	return rc;
}

/*
 * Format a tres_per_* argument
 * dest OUT - resulting string
 * prefix IN - TRES type (e.g. "gpu")
 * src IN - user input, can include multiple comma-separated specifications
 */
extern void xfmt_tres(char **dest, char *prefix, char *src)
{
	char *result = NULL, *save_ptr = NULL, *sep = "", *tmp, *tok;

	if (!src || (src[0] == '\0'))
		return;
	tmp = xstrdup(src);
	tok = strtok_r(tmp, ",", &save_ptr);
	while (tok) {
		xstrfmtcat(result, "%s%s:%s", sep, prefix, tok);
		sep = ",";
		tok = strtok_r(NULL, ",", &save_ptr);
	}
	xfree(tmp);
	*dest = result;
}
