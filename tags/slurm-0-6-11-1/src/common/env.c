/*****************************************************************************\
 * src/common/env.c - add an environment variable to environment vector
 * $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>, Danny Auble <da@llnl.gov>.
 *  UCRL-CODE-2002-040.
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

#if HAVE_CONFIG_H
#  include "config.h"
#endif 

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <slurm/slurm.h>

#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/env.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

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
	char *bgl_part_id = NULL;
	char addrbuf[INET_ADDRSTRLEN];

	if (env == NULL)
		return SLURM_ERROR;
	
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

	if (env->cpus_on_node 
	   && setenvf(&env->env, "SLURM_CPUS_ON_NODE", "%d", 
		      env->cpus_on_node) ) {
		error("Unable to set SLURM_CPUS_PER_TASK");
		rc = SLURM_FAILURE;
	} 

	if (env->distribution 
	    && env->distribution != SRUN_DIST_UNKNOWN) {
		dist = (env->distribution == SRUN_DIST_BLOCK) ?  
			"block" : "cyclic";
		
		if (setenvf(&env->env, "SLURM_DISTRIBUTION", "%s", dist)) {
			error("Can't set SLURM_DISTRIBUTION env variable");
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
				     SELECT_DATA_PART_ID, &bgl_part_id);
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
		char res_env[128], tmp_env[32];
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
