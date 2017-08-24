/*****************************************************************************\
 *  slurm_resource_info.c - Functions to determine number of available resources
 *****************************************************************************
 *  Copyright (C) 2006 Hewlett-Packard Development Company, L.P.
 *  Written by Susanne M. Balle, <susanne.balle@hp.com>
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
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/log.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_resource_info.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

/*
 * First clear all of the bits in "*data" which are set in "clear_mask".
 * Then set all of the bits in "*data" that are set in "set_mask".
 */
static void _clear_then_set(int *data, int clear_mask, int set_mask)
{
	*data &= ~clear_mask;
	*data |= set_mask;
}

/*
 * _isvalue
 * returns 1 is the argument appears to be a value, 0 otherwise
 */
static int _isvalue(char *arg) {
    	if (isdigit((int)*arg)) { /* decimal values and 0x... hex values */
	    	return 1;
	}

	while (isxdigit((int)*arg)) { /* hex values not preceded by 0x */
		arg++;
	}
	if (*arg == ',' || *arg == '\0') { /* end of field or string */
	    	return 1;
	}

	return 0;	/* not a value */
}

/* Expand a list of CPU/memory maps or masks containing multipliers.
 * For example, change "1*4,2*4" to "1,1,1,1,2,2,2,2"
 * list IN - input mask or map
 * type IN - "mask_cpu", "map_mem", used for error messages
 * error_code - output SLURM_SUCCESS or SLURM_ERROR on failure
 * RET - output mask or map, value must be xfreed */
static char *_expand_mult(char *list, char *type, int *error_code)
{
	char *ast, *end_ptr = NULL, *result = NULL, *save_ptr = NULL;
	char *sep = "", *tmp, *tok;
	long int count, i;

	*error_code = SLURM_SUCCESS;

	if (!list)		/* Nothing to convert */	
		return NULL;

	tmp = xstrdup(list);
	if (!strchr(tmp, '*'))	/* No expansion needed*/
		return tmp;

	tok = strtok_r(tmp, ",", &save_ptr);
	while (tok) {
		ast = strchr(tok, '*');
		if (ast) {
			count = strtol(ast + 1, &end_ptr, 10);
			if ((count <= 0) || (end_ptr[0] != '\0') ||
			    (count == LONG_MIN) || (count == LONG_MAX)) {
				error("Invalid %s multiplier: %s",
				      type, ast + 1);
				xfree(result);
				*error_code = SLURM_ERROR;
				break;
			}
			ast[0] = '\0';
			for (i = 0; i < count; i++) {
				xstrfmtcat(result, "%s%s", sep, tok);
				sep = ",";
			}
		} else {
			xstrfmtcat(result, "%s%s", sep, tok);
			sep = ",";
		}
		tok = strtok_r(NULL, ",", &save_ptr);
	}
	xfree(tmp);

	return result;
}

static bool _have_task_affinity(void)
{
	bool rc = true;
	char *plugin_name = slurm_get_task_plugin();

	if (plugin_name && !xstrcmp(plugin_name, "task/none"))
		rc = false;
	xfree(plugin_name);
	return rc;
}

/*
 * slurm_sprint_cpu_bind_type
 *
 * Given a cpu_bind_type, report all flag settings in str
 * IN  - cpu_bind_type
 * OUT - str
 */
void slurm_sprint_cpu_bind_type(char *str, cpu_bind_type_t cpu_bind_type)
{
	if (!str)
		return;

	str[0] = '\0';

	if (cpu_bind_type & CPU_BIND_VERBOSE)
		strcat(str, "verbose,");

	if (cpu_bind_type & CPU_BIND_TO_THREADS)
		strcat(str, "threads,");
	if (cpu_bind_type & CPU_BIND_TO_CORES)
		strcat(str, "cores,");
	if (cpu_bind_type & CPU_BIND_TO_SOCKETS)
		strcat(str, "sockets,");
	if (cpu_bind_type & CPU_BIND_TO_LDOMS)
		strcat(str, "ldoms,");
	if (cpu_bind_type & CPU_BIND_TO_BOARDS)
		strcat(str, "boards,");
	if (cpu_bind_type & CPU_BIND_NONE)
		strcat(str, "none,");
	if (cpu_bind_type & CPU_BIND_RANK)
		strcat(str, "rank,");
	if (cpu_bind_type & CPU_BIND_MAP)
		strcat(str, "map_cpu,");
	if (cpu_bind_type & CPU_BIND_MASK)
		strcat(str, "mask_cpu,");
	if (cpu_bind_type & CPU_BIND_LDRANK)
		strcat(str, "rank_ldom,");
	if (cpu_bind_type & CPU_BIND_LDMAP)
		strcat(str, "map_ldom,");
	if (cpu_bind_type & CPU_BIND_LDMASK)
		strcat(str, "mask_ldom,");
	if (cpu_bind_type & CPU_BIND_CPUSETS)
		strcat(str, "cpusets,");
	if (cpu_bind_type & CPU_BIND_ONE_THREAD_PER_CORE)
		strcat(str, "one_thread,");

	if (cpu_bind_type & CPU_AUTO_BIND_TO_THREADS)
		strcat(str, "autobind=threads,");
	if (cpu_bind_type & CPU_AUTO_BIND_TO_CORES)
		strcat(str, "autobind=cores,");
	if (cpu_bind_type & CPU_AUTO_BIND_TO_SOCKETS)
		strcat(str, "autobind=sockets,");

	if (*str) {
		str[strlen(str)-1] = '\0';	/* remove trailing ',' */
	} else {
	    	strcat(str, "(null type)");	/* no bits set */
	}
}

/*
 * slurm_sprint_mem_bind_type
 *
 * Given a mem_bind_type, report all flag settings in str
 * IN  - mem_bind_type
 * OUT - str
 */
void slurm_sprint_mem_bind_type(char *str, mem_bind_type_t mem_bind_type)
{
	if (!str)
		return;

	str[0] = '\0';

	if (mem_bind_type & MEM_BIND_VERBOSE)
		strcat(str, "verbose,");
	if (mem_bind_type & MEM_BIND_PREFER)
		strcat(str, "prefer,");
	if (mem_bind_type & MEM_BIND_SORT)
		strcat(str, "sort,");
	if (mem_bind_type & MEM_BIND_NONE)
		strcat(str, "none,");
	if (mem_bind_type & MEM_BIND_RANK)
		strcat(str, "rank,");
	if (mem_bind_type & MEM_BIND_LOCAL)
		strcat(str, "local,");
	if (mem_bind_type & MEM_BIND_MAP)
		strcat(str, "map_mem,");
	if (mem_bind_type & MEM_BIND_MASK)
		strcat(str, "mask_mem,");

	if (*str) {
		str[strlen(str)-1] = '\0';	/* remove trailing ',' */
	} else {
	    	strcat(str, "(null type)");	/* no bits set */
	}
}

void slurm_print_cpu_bind_help(void)
{
	if (!_have_task_affinity()) {
		printf("CPU bind options not supported with current "
		       "configuration\n");
	} else {
		printf(
"CPU bind options:\n"
"    --cpu_bind=         Bind tasks to CPUs\n"
"        q[uiet]         quietly bind before task runs (default)\n"
"        v[erbose]       verbosely report binding before task runs\n"
"        no[ne]          don't bind tasks to CPUs (default)\n"
"        rank            bind by task rank\n"
"        map_cpu:<list>  specify a CPU ID binding for each task\n"
"                        where <list> is <cpuid1>,<cpuid2>,...<cpuidN>\n"
"        mask_cpu:<list> specify a CPU ID binding mask for each task\n"
"                        where <list> is <mask1>,<mask2>,...<maskN>\n"
"        rank_ldom       bind task by rank to CPUs in a NUMA locality domain\n"
"        map_ldom:<list> specify a NUMA locality domain ID for each task\n"
"                        where <list> is <ldom1>,<ldom2>,...<ldomN>\n"
"        mask_ldom:<list>specify a NUMA locality domain ID mask for each task\n"
"                        where <list> is <mask1>,<mask2>,...<maskN>\n"
"        sockets         auto-generated masks bind to sockets\n"
"        cores           auto-generated masks bind to cores\n"
"        threads         auto-generated masks bind to threads\n"
"        ldoms           auto-generated masks bind to NUMA locality domains\n"
"        boards          auto-generated masks bind to boards\n"
"        help            show this help message\n");
	}
}

/*
 * verify cpu_bind arguments
 *
 * we support different launch policy names
 * we also allow a verbose setting to be specified
 *     --cpu_bind=threads
 *     --cpu_bind=cores
 *     --cpu_bind=sockets
 *     --cpu_bind=v
 *     --cpu_bind=rank,v
 *     --cpu_bind=rank
 *     --cpu_bind={MAP_CPU|MASK_CPU}:0,1,2,3,4
 *
 *
 * returns -1 on error, 0 otherwise
 */
int slurm_verify_cpu_bind(const char *arg, char **cpu_bind,
			  cpu_bind_type_t *flags)
{
	char *buf, *p, *tok;
	int bind_bits =
		CPU_BIND_NONE|CPU_BIND_RANK|CPU_BIND_MAP|CPU_BIND_MASK;
	int bind_to_bits =
		CPU_BIND_TO_SOCKETS|CPU_BIND_TO_CORES|CPU_BIND_TO_THREADS;
	uint32_t task_plugin_param = slurm_get_task_plugin_param();
	bool have_binding = _have_task_affinity();
	bool log_binding = true;
	int rc = SLURM_SUCCESS;

	bind_bits    |= CPU_BIND_LDRANK|CPU_BIND_LDMAP|CPU_BIND_LDMASK;
	bind_to_bits |= CPU_BIND_TO_LDOMS|CPU_BIND_TO_BOARDS;

	if (arg == NULL) {
		if ((*flags != 0) || 		/* already set values */
		    (task_plugin_param == 0))	/* no system defaults */
			return 0;

		/* set system defaults */
		xfree(*cpu_bind);
		if (task_plugin_param & CPU_BIND_NONE)
			*flags = CPU_BIND_NONE;
		else if (task_plugin_param & CPU_BIND_TO_SOCKETS)
			*flags = CPU_BIND_TO_SOCKETS;
		else if (task_plugin_param & CPU_BIND_TO_CORES)
			*flags = CPU_BIND_TO_CORES;
		else if (task_plugin_param & CPU_BIND_TO_THREADS)
			*flags |= CPU_BIND_TO_THREADS;
		else if (task_plugin_param & CPU_BIND_TO_LDOMS)
			*flags |= CPU_BIND_TO_LDOMS;
		else if (task_plugin_param & CPU_BIND_TO_BOARDS)
			*flags |= CPU_BIND_TO_BOARDS;
		if (task_plugin_param & CPU_BIND_VERBOSE)
			*flags |= CPU_BIND_VERBOSE;
	    	return 0;
	}

	/* Start with system default verbose flag (if set) */
	if (task_plugin_param & CPU_BIND_VERBOSE)
		*flags |= CPU_BIND_VERBOSE;

    	buf = xstrdup(arg);
    	p = buf;
	/* change all ',' delimiters not followed by a digit to ';'  */
	/* simplifies parsing tokens while keeping map/mask together */
	while (p[0] != '\0') {
	    	if ((p[0] == ',') && (!_isvalue(&(p[1]))))
			p[0] = ';';
		p++;
	}

	p = buf;
	while ((rc == SLURM_SUCCESS) && (tok = strsep(&p, ";"))) {
		if (xstrcasecmp(tok, "help") == 0) {
			slurm_print_cpu_bind_help();
			xfree(buf);
			return 1;
		}
		if (!have_binding && log_binding) {
			info("cluster configuration lacks support for cpu "
			     "binding");
			log_binding = false;
		}
		if ((xstrcasecmp(tok, "q") == 0) ||
			   (xstrcasecmp(tok, "quiet") == 0)) {
		        *flags &= ~CPU_BIND_VERBOSE;
		} else if ((xstrcasecmp(tok, "v") == 0) ||
			   (xstrcasecmp(tok, "verbose") == 0)) {
		        *flags |= CPU_BIND_VERBOSE;
		} else if ((xstrcasecmp(tok, "one_thread") == 0)) {
		        *flags |= CPU_BIND_ONE_THREAD_PER_CORE;
		} else if ((xstrcasecmp(tok, "no") == 0) ||
			   (xstrcasecmp(tok, "none") == 0)) {
			_clear_then_set((int *)flags, bind_bits, CPU_BIND_NONE);
			xfree(*cpu_bind);
		} else if (xstrcasecmp(tok, "rank") == 0) {
			_clear_then_set((int *)flags, bind_bits, CPU_BIND_RANK);
			xfree(*cpu_bind);
		} else if ((strncasecmp(tok, "map_cpu", 7) == 0) ||
		           (strncasecmp(tok, "mapcpu", 6) == 0)) {
			char *list;
			(void) strsep(&tok, ":=");
			list = strsep(&tok, ":=");  /* THIS IS NOT REDUNDANT */
			_clear_then_set((int *)flags, bind_bits, CPU_BIND_MAP);
			xfree(*cpu_bind);
			if (list && *list) {
				*cpu_bind = _expand_mult(list, "map_cpu", &rc);
			} else {
				error("missing list for \"--cpu_bind="
				      "map_cpu:<list>\"");
				rc = SLURM_ERROR;
			}
		} else if ((strncasecmp(tok, "mask_cpu", 8) == 0) ||
		           (strncasecmp(tok, "maskcpu", 7) == 0)) {
			char *list;
			(void) strsep(&tok, ":=");
			list = strsep(&tok, ":=");  /* THIS IS NOT REDUNDANT */
			_clear_then_set((int *)flags, bind_bits, CPU_BIND_MASK);
			xfree(*cpu_bind);
			if (list && *list) {
				*cpu_bind = _expand_mult(list, "mask_cpu", &rc);
			} else {
				error("missing list for \"--cpu_bind="
				      "mask_cpu:<list>\"");
				rc = SLURM_ERROR;
			}
		} else if (xstrcasecmp(tok, "rank_ldom") == 0) {
			_clear_then_set((int *)flags, bind_bits,
					CPU_BIND_LDRANK);
			xfree(*cpu_bind);
		} else if ((strncasecmp(tok, "map_ldom", 8) == 0) ||
		           (strncasecmp(tok, "mapldom", 7) == 0)) {
			char *list;
			(void) strsep(&tok, ":=");
			list = strsep(&tok, ":=");  /* THIS IS NOT REDUNDANT */
			_clear_then_set((int *)flags, bind_bits,
					CPU_BIND_LDMAP);
			xfree(*cpu_bind);
			if (list && *list) {
				*cpu_bind = _expand_mult(list, "map_ldom", &rc);
			} else {
				error("missing list for \"--cpu_bind="
				      "map_ldom:<list>\"");
				rc = SLURM_ERROR;
			}
		} else if ((strncasecmp(tok, "mask_ldom", 9) == 0) ||
		           (strncasecmp(tok, "maskldom", 8) == 0)) {
			char *list;
			(void) strsep(&tok, ":=");
			list = strsep(&tok, ":=");  /* THIS IS NOT REDUNDANT */
			_clear_then_set((int *)flags, bind_bits,
					CPU_BIND_LDMASK);
			xfree(*cpu_bind);
			if (list && *list) {
				*cpu_bind = _expand_mult(list, "mask_ldom",&rc);
			} else {
				error("missing list for \"--cpu_bind="
				      "mask_ldom:<list>\"");
				rc = SLURM_ERROR;
			}
		} else if ((xstrcasecmp(tok, "socket") == 0) ||
		           (xstrcasecmp(tok, "sockets") == 0)) {
			if (task_plugin_param &
			    (CPU_BIND_NONE | CPU_BIND_TO_CORES |
			     CPU_BIND_TO_THREADS | CPU_BIND_TO_LDOMS |
			     CPU_BIND_TO_BOARDS)) {
				debug("--cpu_bind=sockets incompatible with "
				      "TaskPluginParam configuration "
				      "parameter");
			}
			_clear_then_set((int *)flags, bind_to_bits,
				       CPU_BIND_TO_SOCKETS);
		} else if ((xstrcasecmp(tok, "core") == 0) ||
		           (xstrcasecmp(tok, "cores") == 0)) {
			if (task_plugin_param &
			    (CPU_BIND_NONE | CPU_BIND_TO_SOCKETS |
			     CPU_BIND_TO_THREADS | CPU_BIND_TO_LDOMS |
			     CPU_BIND_TO_BOARDS)) {
				debug("--cpu_bind=cores incompatible with "
				      "TaskPluginParam configuration "
				      "parameter");
			}
			_clear_then_set((int *)flags, bind_to_bits,
				       CPU_BIND_TO_CORES);
		} else if ((xstrcasecmp(tok, "thread") == 0) ||
		           (xstrcasecmp(tok, "threads") == 0)) {
			if (task_plugin_param &
			    (CPU_BIND_NONE | CPU_BIND_TO_SOCKETS |
			     CPU_BIND_TO_CORES | CPU_BIND_TO_LDOMS |
			     CPU_BIND_TO_BOARDS)) {
				debug("--cpu_bind=threads incompatible with "
				      "TaskPluginParam configuration "
				      "parameter");
			}
			_clear_then_set((int *)flags, bind_to_bits,
				       CPU_BIND_TO_THREADS);
		} else if ((xstrcasecmp(tok, "ldom") == 0) ||
		           (xstrcasecmp(tok, "ldoms") == 0)) {
			if (task_plugin_param &
			    (CPU_BIND_NONE | CPU_BIND_TO_SOCKETS |
			     CPU_BIND_TO_CORES | CPU_BIND_TO_THREADS |
			     CPU_BIND_TO_BOARDS)) {
				debug("--cpu_bind=threads incompatible with "
				      "TaskPluginParam configuration "
				      "parameter");
			}
			_clear_then_set((int *)flags, bind_to_bits,
				       CPU_BIND_TO_LDOMS);
		} else if ((xstrcasecmp(tok, "board") == 0) ||
		           (xstrcasecmp(tok, "boards") == 0)) {
			if (task_plugin_param &
			    (CPU_BIND_NONE | CPU_BIND_TO_SOCKETS |
			     CPU_BIND_TO_CORES | CPU_BIND_TO_THREADS |
			     CPU_BIND_TO_LDOMS)) {
				debug("--cpu_bind=threads incompatible with "
				      "TaskPluginParam configuration "
				      "parameter");
			}
			_clear_then_set((int *)flags, bind_to_bits,
				       CPU_BIND_TO_BOARDS);
		} else {
			error("unrecognized --cpu_bind argument \"%s\"", tok);
			rc = SLURM_ERROR;
		}
	}
	xfree(buf);

	/* Set system default CPU binding as needed */
	if ((rc == SLURM_SUCCESS) && (*flags & (~CPU_BIND_VERBOSE)) == 0) {
                if (task_plugin_param & CPU_BIND_NONE)
                        *flags = CPU_BIND_NONE;
                else if (task_plugin_param & CPU_BIND_TO_SOCKETS)
                        *flags = CPU_BIND_TO_SOCKETS;
                else if (task_plugin_param & CPU_BIND_TO_CORES)
                        *flags = CPU_BIND_TO_CORES;
                else if (task_plugin_param & CPU_BIND_TO_THREADS)
                        *flags |= CPU_BIND_TO_THREADS;
                else if (task_plugin_param & CPU_BIND_TO_LDOMS)
                        *flags |= CPU_BIND_TO_LDOMS;
                else if (task_plugin_param & CPU_BIND_TO_BOARDS)
                        *flags |= CPU_BIND_TO_BOARDS;
	}

	return rc;
}

void slurm_print_mem_bind_help(void)
{
			printf(
"Memory bind options:\n"
"    --mem_bind=         Bind memory to locality domains (ldom)\n"
"        nosort          avoid sorting pages at startup\n"
"        sort            sort pages at startup\n"
"        q[uiet]         quietly bind before task runs (default)\n"
"        v[erbose]       verbosely report binding before task runs\n"
"        no[ne]          don't bind tasks to memory (default)\n"
"        rank            bind by task rank\n"
"        local           bind to memory local to processor\n"
"        map_mem:<list>  specify a memory binding for each task\n"
"                        where <list> is <cpuid1>,<cpuid2>,...<cpuidN>\n"
"        mask_mem:<list> specify a memory binding mask for each tasks\n"
"                        where <list> is <mask1>,<mask2>,...<maskN>\n"
"        help            show this help message\n");
}

/*
 * verify mem_bind arguments
 *
 * we support different memory binding names
 * we also allow a verbose setting to be specified
 *     --mem_bind=v
 *     --mem_bind=rank,v
 *     --mem_bind=rank
 *     --mem_bind={MAP_MEM|MASK_MEM}:0,1,2,3,4
 *
 * returns -1 on error, 0 otherwise
 */
int slurm_verify_mem_bind(const char *arg, char **mem_bind,
			  mem_bind_type_t *flags)
{
	char *buf, *p, *tok;
	int bind_bits = MEM_BIND_NONE|MEM_BIND_RANK|MEM_BIND_LOCAL|
		MEM_BIND_MAP|MEM_BIND_MASK;
	int rc = SLURM_SUCCESS;

	if (arg == NULL) {
	    	return 0;
	}

    	buf = xstrdup(arg);
    	p = buf;
	/* change all ',' delimiters not followed by a digit to ';'  */
	/* simplifies parsing tokens while keeping map/mask together */
	while (p[0] != '\0') {
	    	if ((p[0] == ',') && (!_isvalue(&(p[1]))))
			p[0] = ';';
		p++;
	}

	p = buf;
	while ((rc == SLURM_SUCCESS) && (tok = strsep(&p, ";"))) {
		if (xstrcasecmp(tok, "help") == 0) {
			slurm_print_mem_bind_help();
			xfree(buf);
			return 1;
		} else if ((xstrcasecmp(tok, "p") == 0) ||
			   (xstrcasecmp(tok, "prefer") == 0)) {
		        *flags |= MEM_BIND_PREFER;
		} else if (!xstrcasecmp(tok, "nosort")) {
		        *flags &= ~MEM_BIND_SORT;
		} else if (!xstrcasecmp(tok, "sort")) {
		        *flags |= MEM_BIND_SORT;
		} else if ((xstrcasecmp(tok, "q") == 0) ||
			   (xstrcasecmp(tok, "quiet") == 0)) {
		        *flags &= ~MEM_BIND_VERBOSE;
		} else if ((xstrcasecmp(tok, "v") == 0) ||
			   (xstrcasecmp(tok, "verbose") == 0)) {
		        *flags |= MEM_BIND_VERBOSE;
		} else if ((xstrcasecmp(tok, "no") == 0) ||
			   (xstrcasecmp(tok, "none") == 0)) {
			_clear_then_set((int *)flags, bind_bits, MEM_BIND_NONE);
			xfree(*mem_bind);
		} else if (xstrcasecmp(tok, "rank") == 0) {
			_clear_then_set((int *)flags, bind_bits, MEM_BIND_RANK);
			xfree(*mem_bind);
		} else if (xstrcasecmp(tok, "local") == 0) {
			_clear_then_set((int *)flags, bind_bits,MEM_BIND_LOCAL);
			xfree(*mem_bind);
		} else if ((strncasecmp(tok, "map_mem", 7) == 0) ||
		           (strncasecmp(tok, "mapmem", 6) == 0)) {
			char *list;
			(void) strsep(&tok, ":=");
			list = strsep(&tok, ":=");  /* THIS IS NOT REDUNDANT */
			_clear_then_set((int *)flags, bind_bits, MEM_BIND_MAP);
			xfree(*mem_bind);
			if (list && *list) {
				*mem_bind = _expand_mult(list, "map_mem", &rc);
			} else {
				error("missing list for \"--mem_bind=map_mem:<list>\"");
				rc = SLURM_ERROR;
			}
		} else if ((strncasecmp(tok, "mask_mem", 8) == 0) ||
		           (strncasecmp(tok, "maskmem", 7) == 0)) {
			char *list;
			(void) strsep(&tok, ":=");
			list = strsep(&tok, ":=");  /* THIS IS NOT REDUNDANT */
			_clear_then_set((int *)flags, bind_bits, MEM_BIND_MASK);
			xfree(*mem_bind);
			if (list && *list) {
				*mem_bind = _expand_mult(list, "mask_mem", &rc);
			} else {
				error("missing list for \"--mem_bind=mask_mem:<list>\"");
				rc = SLURM_ERROR;
			}
		} else {
			error("unrecognized --mem_bind argument \"%s\"", tok);
			rc = SLURM_ERROR;
		}
	}

	xfree(buf);
	return rc;
}
