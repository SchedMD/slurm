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
#include "src/common/setenvpf.h" /* For unsetenvp() */
#include "src/common/xmalloc.h"

#include "src/slurmd/job.h"

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
	{
#ifdef RLIMIT_CPU
          { "SLURM_RLIMIT_CPU"   , RLIMIT_CPU   },
#endif
#ifdef RLIMIT_FSIZE
          { "SLURM_RLIMIT_FSIZE" , RLIMIT_FSIZE },
#endif
#ifdef RLIMIT_DATA
          { "SLURM_RLIMIT_DATA"  , RLIMIT_DATA  },
#endif
#ifdef RLIMIT_STACK
          { "SLURM_RLIMIT_STACK" , RLIMIT_STACK },
#endif
#ifdef RLIMIT_CORE
          { "SLURM_RLIMIT_CORE"  , RLIMIT_CORE  },
#endif
#ifdef RLIMIT_RSS
          { "SLURM_RLIMIT_RSS"   , RLIMIT_RSS  },
#endif
#ifdef RLIMIT_NPROC
	  { "SLURM_RLIMIT_NPROC" , RLIMIT_NPROC },
#endif
#ifdef RLIMIT_NOFILE
	  { "SLURM_RLIMIT_NOFILE", RLIMIT_NOFILE},
#endif
#ifdef RLIMIT_MEMLOCK
	  { "SLURM_RLIMIT_MEMLOCK", RLIMIT_MEMLOCK },
#endif
#ifdef RLIMIT_AS
	  { "SLURM_RLIMIT_AS"    , RLIMIT_AS    },
#endif
	  { NULL, 0 } 
	};

/*
 * Prototypes:
 *
 */
static int _get_env_val(char **env, const char *name, unsigned long *valp);
static int _set_limit(char **env, struct userlim *ulim);


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

/*
 *  Return an rlimit as a string suitable for printing.
 */
static char * rlim_to_string (unsigned long rlim, char *buf, size_t n)
{
	if (rlim == RLIM_INFINITY)
		strlcpy (buf, "inf", n);
	else 
		snprintf (buf, n, "%lu", rlim);
	return (buf);
}

static int
_set_limit(char **env, struct userlim *u)
{
	unsigned long val;
	const char *  name = u->var+6;
	char max[24], cur[24], req[24]; 
	struct rlimit r;


	if (_get_env_val (env, u->var, &val) < 0) {
		error ("couldn't find %s in environment", u->var);
		return SLURM_ERROR;
	}

	if (getrlimit(u->resource, &r) < 0)
		error("getrlimit(%s): %m", name);

	debug2("%-14s: max:%s cur:%s req:%s", name, 
	       rlim_to_string (r.rlim_max, max, sizeof (max)),
	       rlim_to_string (r.rlim_cur, cur, sizeof (cur)),
	       rlim_to_string (val,        req, sizeof (req)) );

	/* 
	 *  Only call setrlimit() if new value does not
	 *   equal current value.
	 */
	if (r.rlim_cur != (rlim_t) val) {
		r.rlim_cur = (rlim_t) val;

		if (setrlimit(u->resource, &r) < 0)
			error("Can't propagate %s of %ld from submit host: %m",
				name, (long) r.rlim_cur);	
	}

	unsetenvp(env, u->var); 

	return SLURM_SUCCESS;
}


static int
_get_env_val(char **env, const char *name, unsigned long *valp)
{
	char *val    = NULL;
	char *p      = NULL;

	xassert(env  != NULL);
	xassert(name != NULL);

	if(!(val = getenvp(env, name))) 
		return (-1);

	*valp = strtoul(val, &p, 10);

	if (p && (*p != '\0'))  {
		error("Invalid %s env var, value = `%s'", name, val);
		return (-1);
	}

	return (0);
}


