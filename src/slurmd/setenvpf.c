/*****************************************************************************\
 * src/slurmd/setenvpf.c - add an environment variable to environment vector
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

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "src/common/xmalloc.h"
#include "src/common/xassert.h"
#include "src/common/xstring.h"

/* add environment variable to end of env vector allocated with
 * xmalloc() extending *envp if necessary.
 *
 * envp		Pointer to environment array allocated with xmalloc()
 * envc		Pointer to current count of environment vars
 * fmt		printf style format (e.g. "SLURM_NPROCS=%d")
 *
 */    
int
setenvpf(char ***envp, int *envc, const char *fmt, ...)
{
	va_list ap;
	char buf[BUFSIZ];
	char **env = *envp;
		
	xassert(env[*envc] == NULL);
	
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	xrealloc(env, (*envc+2)*sizeof(char *));
	env[(*envc)++] = xstrdup(buf);
	env[(*envc)]   = NULL;

	*envp = env;

	xassert(strcmp(env[(*envc) - 1], buf) == 0);
	return *envc;
}


