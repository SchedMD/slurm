/*****************************************************************************\
 * src/common/setenvpf.c - add an environment variable to environment vector
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
#include "src/common/log.h"
#include "src/common/xassert.h"
#include "src/common/xstring.h"


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

int 
setenvpf(char ***envp, const char *name, const char *fmt, ...)
{
	char buf[BUFSIZ];
	char **ep = NULL;
	char *str = NULL;
	va_list ap;

	va_start(ap, fmt);
	vsnprintf (buf, BUFSIZ, fmt, ap);
	va_end(ap);

	xstrfmtcat (str, "%s=%s", name, buf);

	ep = _find_name_in_env (*envp, name);

	if (*ep != NULL) 
		xfree (*ep);
	else
		ep = _extend_env (envp);
		
	*ep = str;

	return (0);
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

char *
getenvp(char **env, const char *name)
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

