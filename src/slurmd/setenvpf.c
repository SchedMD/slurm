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
 * fmt		printf style format (e.g. "SLURM_NPROCS=%d")
 *
 */    
int
setenvpf(char ***envp, const char *fmt, ...)
{
	va_list ap;
	char buf[BUFSIZ];
	char **env = *envp;
	char **ep  = env;
	int    cnt = 0;
		
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	while (ep[cnt] != NULL)
	       cnt++;	

	if ((cnt+1) >= (xsize(env)/sizeof(char *))) 
		xrealloc(env, (cnt+2)*sizeof(char *));

	env[cnt++] = xstrdup(buf);
	env[cnt]   = NULL;

	*envp = env;

	xassert(strcmp(env[cnt - 1], buf) == 0);

	return cnt;
}

/*
 *  Remove environment variable `name' from "environment" 
 *   contained in `env'
 *
 *  [ This was taken almost verbatim from glibc's 
 *    unsetenv()  code. ]
 */
void
unsetenvp(char **env, const char *name)
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
			/*  Found name. Move later env values 
			 *   to front 
			 */
			char **dp = ep;

			do 
				dp[0] = dp[1];
			while (*dp++);
		} else
			++ep;

		/*  Continue loop in case `name' appears again. */
	}

	return;
}

