/*****************************************************************************\
 * src/slurmd/slurmstepd/ulimits.c - set user limits for job
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
 *  CODE-OCEC-09-009. All rights reserved.
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

#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "src/common/env.h" /* For unsetenvp() */
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/slurm_rlimits_info.h"
#include "src/common/strlcpy.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

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
 *
 * NOTE: THIS FUNCTION SHOULD ONLY BE CALLED RIGHT BEFORE THE EXEC OF
 * A SCRIPT AFTER THE FORK SO AS TO LIMIT THE ABOUT OF EFFECT THE
 * LIMITS HAVE WHEN COMBINED WITH THE SLURMSTEPD.  RLIMIT_FSIZE IS THE
 * MAIN REASON SINCE IF THE USER SETS THIS TO BE LOWER THAN THE SIZE
 * OF THE CURRENT SLURMD.LOG THE STEPD WILL CORE THE NEXT TIME
 * ANYTHING IS WRITTEN TO IT.  SO IF RUNNING +DEBUG2 AND THE USER IS
 * GETTING CORES WITH FILE SYSTEM LIMIT ERRORS THIS IS THE REASON.
 *
 * NOTE: The slurmstepd will not normally write a core file due to setuid().
 * Run as normal user to disable setuid() and permit a core file to be written.
 */

int set_user_limits(stepd_step_rec_t *job)
{
#ifdef RLIMIT_AS
#define SLURM_RLIMIT_VSIZE RLIMIT_AS
#define SLURM_RLIMIT_VNAME "RLIMIT_AS"
#elif defined(RLIMIT_DATA)
/* RLIMIT_DATA is useless on many systems which provide anonymous
 * mmap()'s in addition to brk(), use it here only as a fallback for
 * oddball systems lacking RLIMIT_AS. */
#define SLURM_RLIMIT_VSIZE RLIMIT_DATA
#define SLURM_RLIMIT_VNAME "RLIMIT_DATA"
#endif
	slurm_rlimits_info_t *rli;
	struct rlimit r;
	rlim_t task_mem_bytes;
#ifdef SLURM_RLIMIT_VSIZE
	uint16_t vsize_factor;
#endif

	if (getrlimit(RLIMIT_CPU, &r) == 0) {
		if (r.rlim_max != RLIM_INFINITY) {
			error("Slurm process CPU time limit is %d seconds",
			      (int) r.rlim_max);
		}
	}

	for (rli = get_slurm_rlimits_info(); rli->name; rli++)
		_set_limit( job->env, rli );

	/* Set soft and hard rss and vsize limit for this process,
	 * handle job limit (for all spawned processes) in slurmd */
	task_mem_bytes  = job->step_mem;	/* MB */
	task_mem_bytes *= (1024 * 1024);

	/* Many systems, Linux included, ignore RSS limits, but set it
	 * here anyway for consistency and to provide a way for
	 * applications to interrogate what the RSS limit is (with the
	 * caveat that the real RSS limit is over all job tasks on the
	 * node and not per process, but hopefully this is better than
	 * nothing).  */
#ifdef RLIMIT_RSS
	if ((task_mem_bytes) && (getrlimit(RLIMIT_RSS, &r) == 0) &&
	    (r.rlim_max > task_mem_bytes)) {
		r.rlim_max =  r.rlim_cur = task_mem_bytes;
		if (setrlimit(RLIMIT_RSS, &r)) {
			/* Indicates that limit has already been exceeded */
			fatal("setrlimit(RLIMIT_RSS, %"PRIu64" MB): %m",
			      job->step_mem);
		} else
			debug2("Set task rss(%"PRIu64" MB)", job->step_mem);
#if 0
		getrlimit(RLIMIT_RSS, &r);
		info("task RSS limits: %u %u", r.rlim_cur, r.rlim_max);
#endif
	}
#endif

#ifdef SLURM_RLIMIT_VSIZE
	if ((task_mem_bytes) &&
	    ((vsize_factor = slurm_get_vsize_factor()) != 0) &&
	    (getrlimit(SLURM_RLIMIT_VSIZE, &r) == 0) &&
	    (r.rlim_max > task_mem_bytes)) {
		r.rlim_max = task_mem_bytes * (vsize_factor / 100.0);
		r.rlim_cur = r.rlim_max;
		if (setrlimit(SLURM_RLIMIT_VSIZE, &r)) {
			/* Indicates that limit has already been exceeded */
			fatal("setrlimit(%s, %"PRIu64" MB): %m",
			      SLURM_RLIMIT_VNAME, job->step_mem);
		} else
			debug2("Set task vsize(%"PRIu64" MB)", job->step_mem);
#if 0
		getrlimit(SLURM_RLIMIT_VSIZE, &r);
		info("task VSIZE limits:   %u %u", r.rlim_cur, r.rlim_max);
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
set_umask(stepd_step_rec_t *job)
{
	mode_t mask;
	char *val;

	if (!(val = getenvp(job->env, "SLURM_UMASK"))) {
		if (job->stepid != SLURM_EXTERN_CONT)
			debug("Couldn't find SLURM_UMASK in environment");
		return SLURM_ERROR;
	}

	mask = strtol(val, (char **)NULL, 8);
	if ((job->stepid == SLURM_EXTERN_CONT) ||
	    (job->stepid == SLURM_BATCH_SCRIPT))
		unsetenvp(job->env, "SLURM_UMASK");
	umask(mask);
	return SLURM_SUCCESS;
}

/*
 * Set rlimit using value of env vars such as SLURM_RLIMIT_FSIZE if
 * the slurm config file has PropagateResourceLimits set or the user
 * requested it with srun/sbatch --propagate.
 *
 * NOTE: THIS FUNCTION SHOULD ONLY BE CALLED RIGHT BEFORE THE EXEC OF
 * A SCRIPT AFTER THE FORK SO AS TO LIMIT THE ABOUT OF EFFECT THE
 * LIMITS HAVE WHEN COMBINED WITH THE SLURMSTEPD.  RLIMIT_FSIZE IS THE
 * MAIN REASON SINCE IF THE USER SETS THIS TO BE LOWER THAN THE SIZE
 * OF THE CURRENT SLURMD.LOG THE STEPD WILL CORE THE NEXT TIME
 * ANYTHING IS WRITTEN TO IT.  SO IF RUNNING +DEBUG2 AND THE USER IS
 * GETTING CORES WITH FILE SYSTEM LIMIT ERRORS THIS IS THE REASON.
 */
static int
_set_limit(char **env, slurm_rlimits_info_t *rli)
{
	unsigned long env_value;
	char max[24], cur[24], req[24];
	struct rlimit r;
	bool u_req_propagate;  /* e.g. true if 'srun --propagate' */
	char *env_name = NULL, *rlimit_name;
	int rc = SLURM_SUCCESS;

	xstrfmtcat(env_name, "SLURM_RLIMIT_%s", rli->name);
	rlimit_name = xstrdup(env_name + 6);
	if (_get_env_val(env, env_name, &env_value, &u_req_propagate)) {
		debug("Couldn't find %s in environment", env_name);
		xfree(env_name);
		return SLURM_ERROR;
	}

	/*
	 * Users shouldn't get the SLURM_RLIMIT_* env vars in their environ
	 */
	unsetenvp(env, env_name);
	xfree(env_name);

	/*
	 * We'll only attempt to set the propagated soft rlimit when indicated
	 * by the slurm conf file settings, or the user requested it.
	 */
	if ( ! (rli->propagate_flag == PROPAGATE_RLIMITS || u_req_propagate))
		goto cleanup;

	if (getrlimit( rli->resource, &r ) < 0) {
		error("getrlimit(%s): %m", rlimit_name);
		rc = SLURM_ERROR;
		goto cleanup;
	}

	/*
	 * Nothing to do if the rlimit won't change
	 */
	if (r.rlim_cur == (rlim_t) env_value) {
		debug2( "_set_limit: %s setrlimit %s no change in value: %lu",
			u_req_propagate?"user":"conf", rlimit_name,
			(unsigned long) r.rlim_cur);
		goto cleanup;
	}

	debug2("_set_limit: %-14s: max:%s cur:%s req:%s", rlimit_name,
		rlim_to_string (r.rlim_max, max, sizeof (max)),
		rlim_to_string (r.rlim_cur, cur, sizeof (cur)),
		rlim_to_string (env_value,  req, sizeof (req)) );

	r.rlim_cur = (rlim_t) env_value;
	if (r.rlim_max < r.rlim_cur)
		r.rlim_max = r.rlim_cur;

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
		rc = SLURM_ERROR;
		goto cleanup;
	}
	debug2( "_set_limit: %s setrlimit %s succeeded",
			u_req_propagate?"user":"conf", rlimit_name );

cleanup:
	xfree(rlimit_name);
	return rc;
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
		*u_req_propagate = true;
		debug2( "_get_env_val: %s propagated by user option", &name[6]);
		val++;
	}
	else
		*u_req_propagate = false;

	*valp = strtoul(val, &p, 10);

	if (p && (*p != '\0'))  {
		error("Invalid %s env var, value = `%s'", name, val);
		return (-1);
	}

	return (0);
}
