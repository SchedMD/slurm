/*****************************************************************************\
 *  src/common/env.c - add an environment variable to environment vector
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2002-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>, Danny Auble <da@llnl.gov>.
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

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "slurm/slurm.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/env.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/node_select.h"
#include "src/common/slurm_protocol_api.h"

/*
 * Define slurm-specific aliases for use by plugins, see slurm_xlator.h 
 * for details. 
 */
strong_alias(setenvf,		slurm_setenvpf);
strong_alias(unsetenvp,		slurm_unsetenvp);
strong_alias(getenvp,		slurm_getenvp);

/*
 *  Return pointer to `name' entry in environment if found, or
 *   pointer to the last entry (i.e. NULL) if `name' is not
 *   currently set in `env'
 *
 */
static char **
_find_name_in_env(char **env, const char *name)
{
	char **ep;

	ep = env;
	while (*ep != NULL) {
		size_t cnt = 0;

		while ( ((*ep)[cnt] == name[cnt]) 
		      && ( name[cnt] != '\0')
		      && ((*ep)[cnt] != '\0')    )
			++cnt;

		if (name[cnt] == '\0' && (*ep)[cnt] == '=') {
			break;
		} else
			++ep;
	}

	return (ep);
}

/*
 *  Extend memory allocation for env by 1 entry. Make last entry == NULL.
 *   return pointer to last env entry;
 */
static char **
_extend_env(char ***envp)
{
	char **ep;
	size_t newcnt = (xsize (*envp) / sizeof (char *)) + 1;

	*envp = xrealloc (*envp, newcnt * sizeof (char *));

	(*envp)[newcnt - 1] = NULL;
	ep = &((*envp)[newcnt - 2]);

	/* 
	 *  Find last non-NULL entry
	 */
	while (*ep == NULL)
		--ep;

	return (++ep);
}

/*
 * Return the number of elements in the environment `env'
 */
int 
envcount (char **env)
{
	int envc = 0;
	while (env[envc] != NULL)
		envc++;
	return (envc);
}

/*
 * _setenvfs() (stolen from pdsh)
 *
 * Set a variable in the callers environment.  Args are printf style.
 * XXX Space is allocated on the heap and will never be reclaimed.
 * Example: setenvfs("RMS_RANK=%d", rank);
 */
int
setenvfs(const char *fmt, ...)
{
	va_list ap;
	char buf[BUFSIZ];
	char *bufcpy;
	int rc;

	va_start(ap, fmt);
	vsnprintf(buf, BUFSIZ, fmt, ap);
	va_end(ap);
	
	bufcpy = xstrdup(buf);
	rc = putenv(bufcpy);
	return rc;
}

int 
setenvf(char ***envp, const char *name, const char *fmt, ...)
{
	char buf[BUFSIZ];
	char **ep = NULL;
	char *str = NULL;
	va_list ap;
	int rc;
	char *bufcpy;

	va_start(ap, fmt);
	vsnprintf (buf, BUFSIZ, fmt, ap);
	va_end(ap);
	bufcpy = xstrdup(buf);
	
	xstrfmtcat (str, "%s=%s", name, bufcpy);
	xfree(bufcpy);
	if(envp && *envp) {				
		ep = _find_name_in_env (*envp, name);
		
		if (*ep != NULL) 
			xfree (*ep);
		else
			ep = _extend_env (envp);
		
		*ep = str;
		
		return (0);
	} else {
		rc = putenv(str);
		return rc;
	}
}

/*
 *  Remove environment variable `name' from "environment" 
 *   contained in `env'
 *
 *  [ This was taken almost verbatim from glibc's 
 *    unsetenv()  code. ]
 */
void unsetenvp(char **env, const char *name)
{
	char **ep;

	if (env == NULL)
		return;

	ep = env;
	while ((ep = _find_name_in_env (ep, name)) && (*ep != NULL)) {
		char **dp = ep;
		xfree (*ep);
		do 
			dp[0] = dp[1];
		while (*dp++);

		/*  Continue loop in case `name' appears again. */
		++ep;
	}
	return;
}

char *getenvp(char **env, const char *name)
{
	size_t len = strlen(name);
	char **ep;

	if ((env == NULL) || (env[0] == '\0'))
		return (NULL);

	ep = _find_name_in_env (env, name);

	if (*ep != NULL)
		return (&(*ep)[len+1]);

	return NULL;
}

int setup_env(env_t *env)
{
	int rc = SLURM_SUCCESS;
	char *dist = NULL;
	char *lllp_dist = NULL;
	char *bgl_part_id = NULL;
	char addrbuf[INET_ADDRSTRLEN];

	if (env == NULL)
		return SLURM_ERROR;
	
	if (env->task_pid
	  && setenvf(&env->env, "SLURM_TASK_PID", "%d", (int)env->task_pid)) {
		error("Unable to set SLURM_TASK_PID environment variable");
		 rc = SLURM_FAILURE;
	}

	if (env->nprocs
	   && setenvf(&env->env, "SLURM_NPROCS", "%d", env->nprocs)) {
		error("Unable to set SLURM_NPROCS environment variable");
		rc = SLURM_FAILURE;
	} 
	
	if (env->cpus_per_task 
	   && setenvf(&env->env, "SLURM_CPUS_PER_TASK", "%d", 
		      env->cpus_per_task) ) {
		error("Unable to set SLURM_CPUS_PER_TASK");
		rc = SLURM_FAILURE;
	} 
 
	if (env->ntasks_per_node 
	   && setenvf(&env->env, "SLURM_NTASKS_PER_NODE", "%d", 
		      env->ntasks_per_node) ) {
		error("Unable to set SLURM_NTASKS_PER_NODE");
		rc = SLURM_FAILURE;
	} 
 
	if (env->ntasks_per_socket 
	   && setenvf(&env->env, "SLURM_NTASKS_PER_SOCKET", "%d", 
		      env->ntasks_per_socket) ) {
		error("Unable to set SLURM_NTASKS_PER_SOCKET");
		rc = SLURM_FAILURE;
	} 

	if (env->ntasks_per_core 
	   && setenvf(&env->env, "SLURM_NTASKS_PER_CORE", "%d", 
		      env->ntasks_per_core) ) {
		error("Unable to set SLURM_NTASKS_PER_CORE");
		rc = SLURM_FAILURE;
	} 

	if (env->cpus_on_node 
	   && setenvf(&env->env, "SLURM_CPUS_ON_NODE", "%d", 
		      env->cpus_on_node) ) {
		error("Unable to set SLURM_CPUS_PER_TASK");
		rc = SLURM_FAILURE;
	} 

	if (((int)env->distribution >= 0)
	&&  (env->distribution != SLURM_DIST_UNKNOWN)) {
		switch(env->distribution) {
		case SLURM_DIST_CYCLIC:
			dist      = "cyclic";
			lllp_dist = "";
			break;
		case SLURM_DIST_BLOCK:
			dist      = "block";
			lllp_dist = "";
			break;
		case SLURM_DIST_PLANE:
			dist      = "plane";
			lllp_dist = "plane";
			break;
		case SLURM_DIST_ARBITRARY:
			dist      = "arbitrary";
			lllp_dist = "";
			break;
		case SLURM_DIST_CYCLIC_CYCLIC:
			dist      = "cyclic";
			lllp_dist = "cyclic";
			break;
		case SLURM_DIST_CYCLIC_BLOCK:
			dist      = "cyclic";
			lllp_dist = "block";
			break;
		case SLURM_DIST_BLOCK_CYCLIC:
			dist      = "block";
			lllp_dist = "cyclic";
			break;
		case SLURM_DIST_BLOCK_BLOCK:
			dist      = "block";
			lllp_dist = "block";
			break;
		default:
			error("unknown dist, type %d", env->distribution);
			dist      = "unknown";
			lllp_dist = "unknown";
			break;
		}

		if (setenvf(&env->env, "SLURM_DISTRIBUTION", "%s", dist)) {
			error("Can't set SLURM_DISTRIBUTION env variable");
			rc = SLURM_FAILURE;
		}

		if (setenvf(&env->env, "SLURM_DIST_PLANESIZE", "%d", 
			    env->plane_size)) {
			error("Can't set SLURM_DIST_PLANESIZE env variable");
			rc = SLURM_FAILURE;
		}

		if (setenvf(&env->env, "SLURM_DIST_LLLP", "%s", lllp_dist)) {
			error("Can't set SLURM_DIST_LLLP env variable");
			rc = SLURM_FAILURE;
		}
	}
	
	if (env->cpu_bind_type) {
		char *str_verbose, *str_bind_type, *str_bind_list;
		char *str_bind;
		int len;

		unsetenvp(env->env, "SLURM_CPU_BIND_VERBOSE");
		unsetenvp(env->env, "SLURM_CPU_BIND_TYPE");
		unsetenvp(env->env, "SLURM_CPU_BIND_LIST");
		unsetenvp(env->env, "SLURM_CPU_BIND");

		str_verbose = xstrdup ("");
		if (env->cpu_bind_type & CPU_BIND_VERBOSE) {
			xstrcat(str_verbose, "verbose");
		} else {
			xstrcat(str_verbose, "quiet");
		}
		if (setenvf(&env->env, "SLURM_CPU_BIND_VERBOSE", str_verbose)) {
			error("Unable to set SLURM_CPU_BIND_VERBOSE");
			rc = SLURM_FAILURE;
		}

		str_bind_type = xstrdup ("");
		if (env->cpu_bind_type & CPU_BIND_TO_THREADS) {
			xstrcat(str_bind_type, "threads,");
		} else if (env->cpu_bind_type & CPU_BIND_TO_CORES) {
			xstrcat(str_bind_type, "cores,");
		} else if (env->cpu_bind_type & CPU_BIND_TO_SOCKETS) {
			xstrcat(str_bind_type, "sockets,");
		}
		if (env->cpu_bind_type & CPU_BIND_NONE) {
			xstrcat(str_bind_type, "none");
		} else if (env->cpu_bind_type & CPU_BIND_RANK) {
			xstrcat(str_bind_type, "rank");
		} else if (env->cpu_bind_type & CPU_BIND_MAP) {
			xstrcat(str_bind_type, "map_cpu:");
		} else if (env->cpu_bind_type & CPU_BIND_MASK) {
			xstrcat(str_bind_type, "mask_cpu:");
		}
		len = strlen(str_bind_type);
		if (len) {		/* remove a possible trailing ',' */
		    	if (str_bind_type[len-1] == ',') {
			    	str_bind_type[len-1] = '\0';
			}
		}
		if (setenvf(&env->env, "SLURM_CPU_BIND_TYPE", str_bind_type)) {
			error("Unable to set SLURM_CPU_BIND_TYPE");
			rc = SLURM_FAILURE;
		}

		str_bind_list = xstrdup ("");
		if (env->cpu_bind) {
			xstrcat(str_bind_list, env->cpu_bind);
		}
		if (setenvf(&env->env, "SLURM_CPU_BIND_LIST", str_bind_list)) {
			error("Unable to set SLURM_CPU_BIND_LIST");
			rc = SLURM_FAILURE;
		}

		str_bind = xstrdup ("");
		xstrcat(str_bind, str_verbose);
		if (str_bind[0]) {		/* add ',' if str_verbose */
			xstrcatchar(str_bind, ',');
		}
		xstrcat(str_bind, str_bind_type);
		xstrcat(str_bind, str_bind_list);

		if (setenvf(&env->env, "SLURM_CPU_BIND", str_bind)) {
			error("Unable to set SLURM_CPU_BIND");
			rc = SLURM_FAILURE;
		}
	}

	if (env->mem_bind_type) {
		char *str_verbose, *str_bind_type, *str_bind_list;
		char *str_bind;

		unsetenvp(env->env, "SLURM_MEM_BIND_VERBOSE");
		unsetenvp(env->env, "SLURM_MEM_BIND_TYPE");
		unsetenvp(env->env, "SLURM_MEM_BIND_LIST");
		unsetenvp(env->env, "SLURM_MEM_BIND");

		str_verbose = xstrdup ("");
		if (env->mem_bind_type & MEM_BIND_VERBOSE) {
			xstrcat(str_verbose, "verbose");
		} else {
			xstrcat(str_verbose, "quiet");
		}
		if (setenvf(&env->env, "SLURM_MEM_BIND_VERBOSE", str_verbose)) {
			error("Unable to set SLURM_MEM_BIND_VERBOSE");
			rc = SLURM_FAILURE;
		}
 
		str_bind_type = xstrdup ("");
		if (env->mem_bind_type & MEM_BIND_NONE) {
			xstrcat(str_bind_type, "none");
		} else if (env->mem_bind_type & MEM_BIND_RANK) {
			xstrcat(str_bind_type, "rank");
		} else if (env->mem_bind_type & MEM_BIND_MAP) {
			xstrcat(str_bind_type, "map_mem:");
		} else if (env->mem_bind_type & MEM_BIND_MASK) {
			xstrcat(str_bind_type, "mask_mem:");
		} else if (env->mem_bind_type & MEM_BIND_LOCAL) {
			xstrcat(str_bind_type, "local");
		}
		if (setenvf(&env->env, "SLURM_MEM_BIND_TYPE", str_bind_type)) {
			error("Unable to set SLURM_MEM_BIND_TYPE");
			rc = SLURM_FAILURE;
		}

		str_bind_list = xstrdup ("");
		if (env->mem_bind) {
			xstrcat(str_bind_list, env->mem_bind);
		}
		if (setenvf(&env->env, "SLURM_MEM_BIND_LIST", str_bind_list)) {
			error("Unable to set SLURM_MEM_BIND_LIST");
			rc = SLURM_FAILURE;
		}

		str_bind = xstrdup ("");
		xstrcat(str_bind, str_verbose);
		if (str_bind[0]) {		/* add ',' if str_verbose */
			xstrcatchar(str_bind, ',');
		}
		xstrcat(str_bind, str_bind_type);
		xstrcat(str_bind, str_bind_list);

		if (setenvf(&env->env, "SLURM_MEM_BIND", str_bind)) {
			error("Unable to set SLURM_MEM_BIND");
			rc = SLURM_FAILURE;
		}
	}

	if (env->overcommit 
	    && (setenvf(&env->env, "SLURM_OVERCOMMIT", "1"))) {
		error("Unable to set SLURM_OVERCOMMIT environment variable");
		rc = SLURM_FAILURE;
	}

	if (env->slurmd_debug
	    && setenvf(&env->env, "SLURMD_DEBUG", "%d", env->slurmd_debug)) {
		error("Can't set SLURMD_DEBUG environment variable");
		rc = SLURM_FAILURE;
	}

	if (env->labelio 
	   && setenvf(&env->env, "SLURM_LABELIO", "1")) {
		error("Unable to set SLURM_LABELIO environment variable");
		rc = SLURM_FAILURE;
	}

	if(env->select_jobinfo) {
		select_g_get_jobinfo(env->select_jobinfo, 
				     SELECT_DATA_BLOCK_ID, &bgl_part_id);
		if (bgl_part_id) {
			if(setenvf(&env->env, 
				   "MPIRUN_PARTITION", "%s", bgl_part_id))
				rc = SLURM_FAILURE;
			
			if(setenvf(&env->env, "MPIRUN_NOFREE", "%d", 1))
				rc = SLURM_FAILURE;
			if(setenvf(&env->env, "MPIRUN_NOALLOCATE", "%d", 1))
				rc = SLURM_FAILURE;
		} else 
			rc = SLURM_FAILURE;
		
		if(rc == SLURM_FAILURE)
			error("Can't set MPIRUN_PARTITION "
			      "environment variable");
		xfree(bgl_part_id);
	}
	
	if (env->jobid >= 0
	    && setenvf(&env->env, "SLURM_JOBID", "%d", env->jobid)) {
		error("Unable to set SLURM_JOBID environment");
		rc = SLURM_FAILURE;
	}
	
	if (env->nodeid >= 0
	    && setenvf(&env->env, "SLURM_NODEID", "%d", env->nodeid)) {
		error("Unable to set SLURM_NODEID environment");
		rc = SLURM_FAILURE;
	}
	
	if (env->procid >= 0
	    && setenvf(&env->env, "SLURM_PROCID", "%d", env->procid)) {
		error("Unable to set SLURM_PROCID environment");
		rc = SLURM_FAILURE;
	}
	
	if (env->localid >= 0
	    && setenvf(&env->env, "SLURM_LOCALID", "%d", env->localid)) {
		error("Unable to set SLURM_LOCALID environment");
		rc = SLURM_FAILURE;
	}

	if (env->stepid >= 0
	    && setenvf(&env->env, "SLURM_STEPID", "%d", env->stepid)) {
		error("Unable to set SLURM_STEPID environment");
		rc = SLURM_FAILURE;
	}
	
	if (env->nhosts
	    && setenvf(&env->env, "SLURM_NNODES", "%d", env->nhosts)) {
		error("Unable to set SLURM_NNODES environment var");
		rc = SLURM_FAILURE;
	}

	if (env->nodelist
	    && setenvf(&env->env, "SLURM_NODELIST", "%s", env->nodelist)) {
		error("Unable to set SLURM_NODELIST environment var.");
		rc = SLURM_FAILURE;
	}
	
	if (env->task_count
	    && setenvf (&env->env, 
			"SLURM_TASKS_PER_NODE", "%s", env->task_count)) {
		error ("Can't set SLURM_TASKS_PER_NODE env variable");
		rc = SLURM_FAILURE;
	}
	
	if (env->comm_port
	    && setenvf (&env->env, "SLURM_SRUN_COMM_PORT", "%u", env->comm_port)) {
		error ("Can't set SLURM_SRUN_COMM_PORT env variable");
		rc = SLURM_FAILURE;
	}
	if (env->comm_hostname
	    && setenvf (&env->env, "SLURM_SRUN_COMM_HOST", "%s", env->comm_hostname)) {
		error ("Can't set SLURM_SRUN_COMM_HOST env variable");
		rc = SLURM_FAILURE;
	}
		
	if (env->cli) {
		
		slurm_print_slurm_addr (env->cli, addrbuf, INET_ADDRSTRLEN);

		/* 
		 *  XXX: Eventually, need a function for slurm_addrs that
		 *   returns just the IP address (not addr:port)
		 */   

		if ((dist = strchr (addrbuf, ':')) != NULL)
			*dist = '\0';
		setenvf (&env->env, "SLURM_LAUNCH_NODE_IPADDR", "%s", addrbuf);
	}

#ifdef HAVE_AIX
	{
		char res_env[128];
		char *debug_env = (char *)getenv("SLURM_LL_API_DEBUG");
		int  debug_num = 0;

		/* MP_POERESTART_ENV causes a warning message for "poe", but
		 * is needed for "poerestart". Presently we have no means to
		 * determine what command a user will execute. We could
		 * possibly add a "srestart" command which would set
		 * MP_POERESTART_ENV, but that presently seems unnecessary. */
		if (debug_env)
			debug_num = atoi(debug_env);
		snprintf(res_env, sizeof(res_env), "SLURM_LL_API_DEBUG=%d",
			debug_num);
		
		setenvf(&env->env, "MP_POERESTART_ENV", res_env);

		/* Required for AIX/POE systems indicating pre-allocation */
		setenvf(&env->env, "LOADLBATCH", "yes");
		setenvf(&env->env, "LOADL_ACTIVE", "3.2.0");
	}
#endif
	
	return SLURM_SUCCESS;
}

/**********************************************************************
 * From here on are the new environment variable management functions,
 * used by the "new" commands: salloc, sbatch, an slaunch.
 **********************************************************************/

/*
 * Return a string representation of an array of uint16_t elements.
 * Each value in the array is printed in decimal notation and elements
 * are seperated by a comma.  If sequential elements in the array
 * contain the same value, the value is written out just once followed
 * by "(xN)", where "N" is the number of times the value is repeated.
 *
 * Example:
 *   The array "1, 2, 1, 1, 1, 3, 2" becomes the string "1,2,1(x3),3,2"
 *
 * Returns an xmalloc'ed string.  Free with xfree().
 */
static char *_uint16_array_to_str(int array_len, const uint16_t *array)
{
	int i;
	int previous = 0;
	char *sep = ",";  /* seperator */
	char *str = xstrdup("");

	if(array == NULL)
		return str;

	for (i = 0; i < array_len; i++) {
		if ((i+1 < array_len)
		    && (array[i] == array[i+1])) {
				previous++;
				continue;
		}

		if (i == array_len-1) /* last time through loop */
			sep = "";
		if (previous > 0) {
			xstrfmtcat(str, "%u(x%u)%s",
				   array[i], previous+1, sep);
		} else {
			xstrfmtcat(str, "%u%s", array[i], sep);
		}
		previous = 0;
	}
	
	return str;
}


/*
 * The cpus-per-node representation in SLURM (and perhaps tasks-per-node
 * in the future) is stored in a compressed format comprised of two
 * equal-length arrays of uint32_t, and an integer holding the array length.
 * In one array an element represents a count (number of cpus, number of tasks,
 * etc.), and the corresponding element in the other array contains the
 * number of times the count is repeated sequentially in the uncompressed
 * something-per-node array.
 *
 * This function returns the string representation of the compressed
 * array.  Free with xfree().
 */
static char *_uint32_compressed_to_str(uint32_t array_len,
				       const uint32_t *array,
				       const uint32_t *array_reps)
{
	int i;
	char *sep = ","; /* seperator */
	char *str = xstrdup("");

	for (i = 0; i < array_len; i++) {
		if (i == array_len-1) /* last time through loop */
			sep = "";
		if (array_reps[i] > 1) {
			xstrfmtcat(str, "%u(x%u)%s",
				   array[i], array_reps[i], sep);
		} else {
			xstrfmtcat(str, "%u%s", array[i], sep);
		}
	}

	return str;
}

/*
 * Set in "dest" the environment variables relevant to a SLURM job
 * allocation, overwriting any environment variables of the same name.
 * If the address pointed to by "dest" is NULL, memory will automatically be
 * xmalloc'ed.  The array is terminated by a NULL pointer, and thus is
 * suitable for use by execle() and other env_array_* functions.
 *
 * Sets the variables:
 *	SLURM_JOB_ID
 *	SLURM_JOB_NUM_NODES
 *	SLURM_JOB_NODELIST
 *	SLURM_JOB_CPUS_PER_NODE
 *
 * Sets OBSOLETE variables:
 *	SLURM_JOBID
 *	SLURM_NNODES
 *	SLURM_NODELIST
 *	SLURM_TASKS_PER_NODE <- poorly named, really CPUs per node
 *	? probably only needed for users...
 */
void
env_array_for_job(char ***dest, const resource_allocation_response_msg_t *alloc)
{
	char *tmp;

	env_array_overwrite_fmt(dest, "SLURM_JOB_ID", "%u", alloc->job_id);
	env_array_overwrite_fmt(dest, "SLURM_JOB_NUM_NODES", "%u",
				alloc->node_cnt);
	env_array_overwrite_fmt(dest, "SLURM_JOB_NODELIST", "%s",
				alloc->node_list);

	tmp = _uint32_compressed_to_str((uint32_t)alloc->num_cpu_groups,
					alloc->cpus_per_node,
					alloc->cpu_count_reps);
	env_array_overwrite_fmt(dest, "SLURM_JOB_CPUS_PER_NODE", "%s", tmp);

	/* obsolete */
	env_array_overwrite_fmt(dest, "SLURM_JOBID", "%u", alloc->job_id);
	env_array_overwrite_fmt(dest, "SLURM_NNODES", "%u", alloc->node_cnt);
	env_array_overwrite_fmt(dest, "SLURM_NODELIST", "%s", alloc->node_list);
	env_array_overwrite_fmt(dest, "SLURM_TASKS_PER_NODE", "%s", tmp);

	xfree(tmp);
}

/*
 * Set in "dest" the environment variables strings relevant to a SLURM batch
 * job allocation, overwriting any environment variables of the same name.
 * If the address pointed to by "dest" is NULL, memory will automatically be
 * xmalloc'ed.  The array is terminated by a NULL pointer, and thus is
 * suitable for use by execle() and other env_array_* functions.
 *
 * Sets the variables:
 *	SLURM_JOB_ID
 *	SLURM_JOB_NUM_NODES
 *	SLURM_JOB_NODELIST
 *	SLURM_JOB_CPUS_PER_NODE
 *      ENVIRONMENT=BATCH
 *
 * Sets OBSOLETE variables:
 *	SLURM_JOBID
 *	SLURM_NNODES
 *	SLURM_NODELIST
 *	SLURM_TASKS_PER_NODE <- poorly named, really CPUs per node
 *	? probably only needed for users...
 */
void
env_array_for_batch_job(char ***dest, const batch_job_launch_msg_t *batch)
{
	char *tmp;
	uint32_t num_nodes = 0;
	int i;

	/* there is no explicit node count in the batch structure,
	   so we need to calculate the node count */
	for (i = 0; i < batch->num_cpu_groups; i++) {
		num_nodes += batch->cpu_count_reps[i];
	}

	env_array_overwrite_fmt(dest, "SLURM_JOB_ID", "%u", batch->job_id);
	env_array_overwrite_fmt(dest, "SLURM_JOB_NUM_NODES", "%u", num_nodes);
	env_array_overwrite_fmt(dest, "SLURM_JOB_NODELIST", "%s", batch->nodes);
	tmp = _uint32_compressed_to_str((uint32_t)batch->num_cpu_groups,
					batch->cpus_per_node,
					batch->cpu_count_reps);
	env_array_overwrite_fmt(dest, "SLURM_JOB_CPUS_PER_NODE", "%s", tmp);
	env_array_overwrite_fmt(dest, "ENVIRONMENT", "BATCH");

	/* OBSOLETE */
	env_array_overwrite_fmt(dest, "SLURM_JOBID", "%u", batch->job_id);
	env_array_overwrite_fmt(dest, "SLURM_NNODES", "%u", num_nodes);
	env_array_overwrite_fmt(dest, "SLURM_NODELIST", "%s", batch->nodes);
	env_array_overwrite_fmt(dest, "SLURM_TASKS_PER_NODE", "%s", tmp);

	xfree(tmp);
}

/*
 * Set in "dest" the environment variables relevant to a SLURM job step,
 * overwriting any environment variables of the same name.  If the address
 * pointed to by "dest" is NULL, memory will automatically be xmalloc'ed.
 * The array is terminated by a NULL pointer, and thus is suitable for
 * use by execle() and other env_array_* functions.
 *
 * Sets variables:
 *	SLURM_STEP_ID
 *	SLURM_STEP_NUM_NODES
 *	SLURM_STEP_NUM_TASKS
 *	SLURM_STEP_TASKS_PER_NODE
 *	SLURM_STEP_LAUNCHER_HOSTNAME
 *	SLURM_STEP_LAUNCHER_PORT
 *	SLURM_STEP_LAUNCHER_IPADDR
 *
 * Sets OBSOLETE variables:
 *	SLURM_STEPID
 *      SLURM_NNODES
 *	SLURM_NPROCS
 *	SLURM_NODELIST
 *	SLURM_TASKS_PER_NODE
 *	SLURM_SRUN_COMM_HOST
 *	SLURM_SRUN_COMM_PORT
 *	SLURM_LAUNCH_NODE_IPADDR
 *
 */
void
env_array_for_step(char ***dest, 
		   const job_step_create_response_msg_t *step,
		   const char *launcher_hostname,
		   uint16_t launcher_port,
		   const char *ip_addr_str)
{
	char *tmp;

	tmp = _uint16_array_to_str(step->step_layout->node_cnt,
				   step->step_layout->tasks);
	env_array_overwrite_fmt(dest, "SLURM_STEP_ID", "%u", step->job_step_id);
	env_array_overwrite_fmt(dest, "SLURM_STEP_NODELIST",
				"%s", step->step_layout->node_list);
	env_array_overwrite_fmt(dest, "SLURM_STEP_NUM_NODES",
			 "%hu", step->step_layout->node_cnt);
	env_array_overwrite_fmt(dest, "SLURM_STEP_NUM_TASKS",
			 "%u", step->step_layout->task_cnt);
	env_array_overwrite_fmt(dest, "SLURM_STEP_TASKS_PER_NODE", "%s", tmp);
	env_array_overwrite_fmt(dest, "SLURM_STEP_LAUNCHER_HOSTNAME",
			 "%s", launcher_hostname);
	env_array_overwrite_fmt(dest, "SLURM_STEP_LAUNCHER_PORT",
			 "%hu", launcher_port);
/* 	env_array_overwrite_fmt(dest, "SLURM_STEP_LAUNCHER_IPADDR", */
/* 			 "%s", ip_addr_str); */

	/* OBSOLETE */
	env_array_overwrite_fmt(dest, "SLURM_STEPID", "%u", step->job_step_id);
	env_array_overwrite_fmt(dest, "SLURM_NNODES",
			 "%hu", step->step_layout->node_cnt);
	env_array_overwrite_fmt(dest, "SLURM_NPROCS",
			 "%u", step->step_layout->task_cnt);
	env_array_overwrite_fmt(dest, "SLURM_TASKS_PER_NODE", "%s", tmp);
	env_array_overwrite_fmt(dest, "SLURM_SRUN_COMM_HOST",
			 "%s", launcher_hostname);
	env_array_overwrite_fmt(dest, "SLURM_SRUN_COMM_PORT",
			 "%hu", launcher_port);
/* 	env_array_overwrite_fmt(dest, "SLURM_LAUNCH_NODE_IPADDR", */
/* 			 "%s", ip_addr_str); */

	xfree(tmp);
}

/*
 * Enviroment variables set elsewhere
 * ----------------------------------
 *
 * Set by slurmstepd:
 *	SLURM_STEP_NODEID
 *	SLURM_STEP_PROCID
 *	SLURM_STEP_LOCALID
 *
 * OBSOLETE set by slurmstepd:
 *	SLURM_NODEID
 *	SLURM_PROCID
 *	SLURM_LOCALID
 */

/***********************************************************************
 * Environment variable array support functions
 ***********************************************************************/

/*
 * Return an empty environment variable array (contains a single
 * pointer to NULL).
 */
char **env_array_create(void)
{
	char **env_array;

	env_array = (char **)xmalloc(sizeof(char **));
	env_array[0] = NULL;

	return env_array;
}

/*
 * Append a single environment variable to an environment variable array,
 * if and only if a variable by that name does not already exist in the
 * array.
 *
 * Return 1 on success, and 0 on error.
 */
int env_array_append(char ***array_ptr, const char *name,
		     const char *value_fmt, ...)
{
	char buf[BUFSIZ];
	char **ep = NULL;
	char *str = NULL;
	va_list ap;

	buf[0] = '\0';
	if (array_ptr == NULL) {
		return 0;
	}

	if (*array_ptr == NULL) {
		*array_ptr = env_array_create();
	}

	va_start(ap, value_fmt);
	vsnprintf (buf, BUFSIZ, value_fmt, ap);
	va_end(ap);
	
	ep = _find_name_in_env(*array_ptr, name);
	if (*ep != NULL) {
		return 0;
	}

	xstrfmtcat (str, "%s=%s", name, buf);
	ep = _extend_env(array_ptr);
	*ep = str;
	
	return 1;
}

/*
 * Append a single environment variable to an environment variable array
 * if a variable by that name does not already exist.  If a variable
 * by the same name is found in the array, it is overwritten with the
 * new value.
 *
 * Return 1 on success, and 0 on error.
 */
int env_array_overwrite_fmt(char ***array_ptr, const char *name,
			    const char *value_fmt, ...)
{
	char buf[BUFSIZ];
	char **ep = NULL;
	char *str = NULL;
	va_list ap;

	buf[0] = '\0';
	if (array_ptr == NULL) {
		return 0;
	}

	if (*array_ptr == NULL) {
		*array_ptr = env_array_create();
	}

	va_start(ap, value_fmt);
	vsnprintf (buf, BUFSIZ, value_fmt, ap);
	va_end(ap);
	
	xstrfmtcat (str, "%s=%s", name, buf);
	ep = _find_name_in_env(*array_ptr, name);
	if (*ep != NULL) {
		xfree (*ep);
	} else {
		ep = _extend_env(array_ptr);
	}

	*ep = str;
	
	return 1;
}

/*
 * Append a single environment variable to an environment variable array
 * if a variable by that name does not already exist.  If a variable
 * by the same name is found in the array, it is overwritten with the
 * new value.
 *
 * Return 1 on success, and 0 on error.
 */
int env_array_overwrite(char ***array_ptr, const char *name,
			const char *value)
{
	char **ep = NULL;
	char *str = NULL;

	if (array_ptr == NULL) {
		return 0;
	}

	if (*array_ptr == NULL) {
		*array_ptr = env_array_create();
	}

	xstrfmtcat (str, "%s=%s", name, value);
	ep = _find_name_in_env(*array_ptr, name);
	if (*ep != NULL) {
		xfree (*ep);
	} else {
		ep = _extend_env(array_ptr);
	}

	*ep = str;
	
	return 1;
}

/* 
 * Copy env_array must be freed by env_array_free 
 */
char **env_array_copy(const char **array)
{
	char **ptr = NULL;

	env_array_merge(&ptr, array);

	return ptr;
}

/*
 * Free the memory used by an environment variable array.
 */
void env_array_free(char **env_array)
{
	char **ptr;

	if (env_array == NULL)
		return;

	for (ptr = env_array; *ptr != NULL; ptr++) {
		xfree(*ptr);
	}
	xfree(env_array);
}

/*
 * Given an environment variable "name=value" string,
 * copy the name portion into the "name" buffer, and the
 * value portion into the "value" buffer.
 *
 * Return 1 on success, 0 on failure.
 */
static int _env_array_entry_splitter(const char *entry,
				     char *name, int name_len,
				     char *value, int value_len)
{
	char *ptr;
	int len;

	ptr = index(entry, '=');
	len = ptr - entry;
	if (len > name_len-1)
		return 0;
	strncpy(name, entry, len);
	name[len] = '\0';

	ptr = ptr + 1;
	len = strlen(ptr);
	if (len > value_len-1)
		return 0;
	strncpy(value, ptr, len);
	value[len] = '\0';

	return 1;
}

/*
 * Work similarly to putenv() (from C stdlib), but uses setenv()
 * under the covers.  This avoids having pointers from the global
 * array "environ" into "string".
 *
 * Return 1 on success, 0 on failure.
 */
static int _env_array_putenv(const char *string)
{
	char name[BUFSIZ];
	char value[BUFSIZ];

	if (!_env_array_entry_splitter(string, name, BUFSIZ, value, BUFSIZ))
		return 0;
	if (setenv(name, value, 1) == -1)
		return 0;
	
	return 1;
}

/*
 * Set all of the environment variables in a supplied environment
 * variable array.
 */
void env_array_set_environment(char **env_array)
{
	char **ptr;

	if (env_array == NULL)
		return;

	for (ptr = env_array; *ptr != NULL; ptr++) {
		_env_array_putenv(*ptr);
	}
}

/*
 * Merge all of the environment variables in src_array into the
 * array dest_array.  Any variables already found in dest_array
 * will be overwritten with the value from src_array.
 */
void env_array_merge(char ***dest_array, const char **src_array)
{
	char **ptr;
	char name[BUFSIZ];
	char value[BUFSIZ];

	if (src_array == NULL)
		return;

	for (ptr = (char **)src_array; *ptr != NULL; ptr++) {
		_env_array_entry_splitter(*ptr, name, BUFSIZ, value, BUFSIZ);
		env_array_overwrite(dest_array, name, value);
	}
}

