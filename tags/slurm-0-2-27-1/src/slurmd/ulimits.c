/*****************************************************************************\
 * src/slurmd/ulimits.c - set user limits for job
 * $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
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

#include <sys/resource.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include "src/common/log.h"
#include "src/common/xmalloc.h"

#include "src/slurmd/job.h"
#include "src/slurmd/setenvpf.h" /* For unsetenvp() */

struct userlim {
	char *var;
	int   resource;
};

/*
 * This is a list of SLURM environment variables that contain the
 * desired user limits for this node, along with the corresponding
 * get/setrlimit resource number.
 */
static struct userlim ulims[] =
        { { "SLURM_RLIMIT_CORE"  , RLIMIT_CORE  },
	  { "SLURM_RLIMIT_FSIZE" , RLIMIT_FSIZE },
	  { "SLURM_RLIMIT_NPROC" , RLIMIT_NPROC },
	  { "SLURM_RLIMIT_NOFILE", RLIMIT_NOFILE},
	  { "SLURM_RLIMIT_STACK",  RLIMIT_STACK },
	  { NULL, 0 } 
	};

/*
 * Prototypes:
 *
 */
static char * _getenvp(char **env, const char *name);
static long   _get_env_val(char **env, const char *name);
static int    _set_limit(char **env, struct userlim *ulim);


/*
 * Set all user limits off environment variables as detailed in
 * the local ulims[] var. Sets limits off environment variables
 * in job->env.
 */
int set_user_limits(slurmd_job_t *job)
{
	struct userlim *uptr = &ulims[0];

	while (uptr && (uptr->var != NULL)) {
		_set_limit(job->env, uptr);
		uptr++;
	}

	return SLURM_SUCCESS;
}

static int
_set_limit(char **env, struct userlim *u)
{
	long          val;
	const char *  name = u->var+6;
	struct rlimit r;

	if ((val = _get_env_val(env, u->var)) < -1L) {
		error ("couldn't find %s in environment", u->var);
		return SLURM_ERROR;
	}

	if (getrlimit(u->resource, &r) < 0)
		error("getrlimit(%s): %m", name);

	debug2("%s: max:%ld cur:%ld req:%ld", name, 
	       (long)r.rlim_max, (long) r.rlim_cur, (long) val);

	/* 
	 *  Only call setrlimit() if new value does not
	 *   equal current value.
	 */
	if (r.rlim_cur != (rlim_t) val) {
		r.rlim_cur = (val == -1L) ? RLIM_INFINITY : (rlim_t) val;

		if (setrlimit(u->resource, &r) < 0)
			error("Can't propagate %s of %ld from submit host: %m",
				name, (long)r.rlim_cur);	
	}

	unsetenvp(env, u->var); 

	return SLURM_SUCCESS;
}


static long
_get_env_val(char **env, const char *name)
{
	char *val    = NULL;
	char *p      = NULL;
	long  retval = 0L; 

	xassert(env  != NULL);
	xassert(name != NULL);

	if(!(val = _getenvp(env, name))) 
		return -2L;

	retval = strtol(val, &p, 10);

	if (p && (*p != '\0'))  {
		error("Invalid %s env var, value = `%s'", name, val);
		return -2L;
	}

	return retval;
}

static char *
_getenvp(char **env, const char *name)
{
	size_t len = strlen(name);
	char **ep;

	if ((env == NULL) || (env[0] == '\0'))
		return NULL;

	for (ep = env; *ep != NULL; ++ep) {
		if (!strncmp(*ep, name, len) && ((*ep)[len] == '=')) 
			return &(*ep)[len+1];
	}

	return NULL;
}


