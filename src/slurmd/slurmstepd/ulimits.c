/*****************************************************************************\
 * src/slurmd/slurmstepd/ulimits.c - set user limits for job
 * $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
 *  LLNL-CODE-402394.
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

#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include "src/common/log.h"
#include "src/common/env.h" /* For unsetenvp() */
#include "src/common/strlcpy.h"
#include "src/common/xmalloc.h"
#include "src/common/macros.h"
#include "src/common/slurm_rlimits_info.h"

#include "src/slurmd/slurmstepd/slurmstepd_job.h"

/*
 * Prototypes:
 *
 */
static int _get_env_val(char **env, const char *name, unsigned long *valp,
		bool *u_req_propagate);
static int _set_limit(char **env, slurm_rlimits_info_t *rli);

/*
 * Set user resource limits using the values of the environment variables
 * of the name "SLURM_RLIMIT_*" that are found in job->env.
 *
 * The sys admin can control the propagation of user limits in the slurm
 * conf file by setting values for the PropagateResourceRlimits and
 * ResourceLimits keywords.
 */

int set_user_limits(slurmd_job_t *job)
{
	slurm_rlimits_info_t *rli;
	struct rlimit r;
	rlim_t task_mem_bytes;

	for (rli = get_slurm_rlimits_info(); rli->name; rli++)
		_set_limit( job->env, rli );

	/* Set soft and hard memory and data size limit for this process, 
	 * try to handle job and task limit (all spawned processes) in slurmd */
	task_mem_bytes  = job->task_mem;	/* MB */
	task_mem_bytes *= (1024 * 1024);
#ifdef RLIMIT_AS
	if ((task_mem_bytes) && (getrlimit(RLIMIT_AS, &r) == 0) &&
	    (r.rlim_max > task_mem_bytes)) {
		r.rlim_max =  r.rlim_cur = task_mem_bytes;
		if (setrlimit(RLIMIT_AS, &r)) {
			/* Indicates that limit has already been exceeded */
			fatal("setrlimit(RLIMIT_AS, %u MB): %m", job->task_mem);
		} else
			info("Set task_mem(%u MB)", job->task_mem);
#if 0
		getrlimit(RLIMIT_AS, &r);
		info("task memory limits: %u %u", r.rlim_cur, r.rlim_max);
#endif
	}
#endif
#ifdef RLIMIT_DATA
	if ((task_mem_bytes) && (getrlimit(RLIMIT_DATA, &r) == 0) &&
	    (r.rlim_max > task_mem_bytes)) {
		r.rlim_max =  r.rlim_cur = task_mem_bytes;
		if (setrlimit(RLIMIT_DATA, &r)) {
			/* Indicates that limit has already been exceeded */
			fatal("setrlimit(RLIMIT_DATA, %u MB): %m", job->task_mem);
		} else
			info("Set task_data(%u MB)", job->task_mem);
#if 0
		getrlimit(RLIMIT_DATA, &r);
		info("task DATA limits: %u %u", r.rlim_cur, r.rlim_max);
#endif
	}
#endif
	return SLURM_SUCCESS;
}

/*
 *  Return an rlimit as a string suitable for printing.
 */
static char * rlim_to_string (unsigned long rlim, char *buf, size_t n)
{
	if (rlim == (unsigned long) RLIM_INFINITY)
		strlcpy (buf, "inf", n);
	else 
		snprintf (buf, n, "%lu", rlim);
	return (buf);
}

/* Set umask using value of env var SLURM_UMASK */
extern int
set_umask(slurmd_job_t *job)
{
	mode_t mask;
	char *val;
	
	if (!(val = getenvp(job->env, "SLURM_UMASK"))) {
		debug("Couldn't find SLURM_UMASK in environment");
		return SLURM_ERROR;
	}

	mask = strtol(val, (char **)NULL, 8);
	umask(mask);
	return SLURM_SUCCESS;
}

/*
 * Set rlimit using value of env vars such as SLURM_RLIMIT_CORE if
 * the slurm config file has PropagateResourceLimits=YES or the user 
 * requested it with srun --propagate.
 */
static int
_set_limit(char **env, slurm_rlimits_info_t *rli)
{
	unsigned long env_value;
	char max[24], cur[24], req[24]; 
	struct rlimit r;
	bool u_req_propagate;  /* e.g. TRUE if 'srun --propagate' */

	char env_name[25] = "SLURM_RLIMIT_";
	char *rlimit_name = &env_name[6];

	strcpy( &env_name[sizeof("SLURM_RLIMIT_")-1], rli->name );

	if (_get_env_val( env, env_name, &env_value, &u_req_propagate )){
		debug( "Couldn't find %s in environment", env_name );
		return SLURM_ERROR;
	}

	/*
	 * Users shouldn't get the SLURM_RLIMIT_* env vars in their environ
	 */
	unsetenvp( env, env_name );

	/* 
	 * We'll only attempt to set the propagated soft rlimit when indicated
	 * by the slurm conf file settings, or the user requested it.
	 */
	if ( ! (rli->propagate_flag == PROPAGATE_RLIMITS || u_req_propagate))
		return SLURM_SUCCESS;

	if (getrlimit( rli->resource, &r ) < 0) {
		error("getrlimit(%s): %m", rlimit_name);
		return SLURM_ERROR;
	}

	/* 
	 * Nothing to do if the rlimit won't change
	 */
	if (r.rlim_cur == (rlim_t) env_value) {
		debug2( "_set_limit: %s setrlimit %s is unnecessary (same val)",
			u_req_propagate?"user":"conf", rlimit_name );
		return SLURM_SUCCESS;
	}

	debug2("_set_limit: %-14s: max:%s cur:%s req:%s", rlimit_name, 
		rlim_to_string (r.rlim_max, max, sizeof (max)),
		rlim_to_string (r.rlim_cur, cur, sizeof (cur)),
		rlim_to_string (env_value,  req, sizeof (req)) );

	r.rlim_cur = (rlim_t) env_value;

	if (setrlimit( rli->resource, &r ) < 0) {
		/*
		 * Report an error only if the user requested propagate 
		 */
		if (u_req_propagate) {
			error( "Can't propagate %s of %s from submit host: %m",
				rlimit_name,
				r.rlim_cur == RLIM_INFINITY ? "'unlimited'" :
				rlim_to_string( r.rlim_cur, cur, sizeof(cur)));
		} else {
			verbose("Can't propagate %s of %s from submit host: %m",
				rlimit_name,
				r.rlim_cur == RLIM_INFINITY ? "'unlimited'" :
				rlim_to_string( r.rlim_cur, cur, sizeof(cur)));
		}
		return SLURM_ERROR;
	}
	debug2( "_set_limit: %s setrlimit %s succeeded",
			u_req_propagate?"user":"conf", rlimit_name );

	return SLURM_SUCCESS;
}

/*
 * Determine the value of the env var 'name' (if it exists) and whether
 * or not the user wants to use its value as the jobs soft rlimit.
 */
static int _get_env_val(char **env, const char *name, unsigned long *valp, 
		bool *u_req_propagate)
{
	char *val    = NULL;
	char *p      = NULL;

	xassert(env  != NULL);
	xassert(name != NULL);

	if (!(val = getenvp(env, name))) 
		return (-1);

	/*
	 * The letter 'U' would have been prepended to the string value if the
	 * user requested to have this rlimit propagated via 'srun --propagate'
	 */
	if (*val == 'U') {
		*u_req_propagate = TRUE;
		debug2( "_get_env_val: %s propagated by user option", &name[6]);
		val++;
	}
	else
		*u_req_propagate = FALSE;

	*valp = strtoul(val, &p, 10);

	if (p && (*p != '\0'))  {
		error("Invalid %s env var, value = `%s'", name, val);
		return (-1);
	}

	return (0);
}


