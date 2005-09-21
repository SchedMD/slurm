/*****************************************************************************\
 *  xstring.c - heap-oriented string manipulation functions with "safe" 
 *	string expansion as needed.
 ******************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Jim Garlick <garlick@llnl.gov>
 *             Mark Grondona <grondona@llnl.gov>, et al.
 *	
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

#if     HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>
#include <stdio.h>
#include <errno.h>
#if 	HAVE_UNISTD_H
#  include <unistd.h>
#endif

#ifdef WITH_PTHREADS
#  include <pthread.h>
#endif

#include <stdarg.h>

#include <slurm/slurm_errno.h>

#include "src/common/macros.h"
#include "src/common/strlcpy.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#define XFGETS_CHUNKSIZE 64

/*
 * Define slurm-specific aliases for use by plugins, see slurm_xlator.h 
 * for details. 
 */
strong_alias(_xstrcat,		slurm_xstrcat);
strong_alias(_xstrcatchar,	slurm_xstrcatchar);
strong_alias(_xslurm_strerrorcat, slurm_xslurm_strerrorcat);
strong_alias(_xstrftimecat,	slurm_xstrftimecat);
strong_alias(_xstrfmtcat,	slurm_xstrfmtcat);
strong_alias(_xmemcat,		slurm_xmemcat);
strong_alias(xstrdup,		slurm_xstrdup);
strong_alias(xbasename,		slurm_xbasename);

/*
 * Ensure that a string has enough space to add 'needed' characters.
 * If the string is uninitialized, it should be NULL.
 */
static void makespace(char **str, int needed)
{
	int used;

	if (*str == NULL)
		*str = xmalloc(needed + 1);
	else {
		used = strlen(*str) + 1;
		while (used + needed > xsize(*str)) {
			int newsize = xsize(*str) + XFGETS_CHUNKSIZE;
			int actualsize;

			xrealloc(*str, newsize);
			actualsize = xsize(*str);

			xassert(actualsize == newsize);
		}
	}
}

/* 
 * Concatenate str2 onto str1, expanding str1 as needed.
 *   str1 (IN/OUT)	target string (pointer to in case of expansion)
 *   str2 (IN)		source string
 */
void _xstrcat(char **str1, const char *str2)
{
	if (str2 == NULL) 
		str2 = "(null)";

	makespace(str1, strlen(str2));
	strcat(*str1, str2);
}

/* 
 * append one char to str and null terminate
 */
static void strcatchar(char *str, char c)
{
	int len = strlen(str);

	str[len++] = c;
	str[len] = '\0';
}

/* 
 * Add a character to str, expanding str1 as needed.
 *   str1 (IN/OUT)	target string (pointer to in case of expansion)
 *   size (IN/OUT)	size of str1 (pointer to in case of expansion)
 *   c (IN)		character to add
 */
void _xstrcatchar(char **str, char c)
{
	makespace(str, 1);
	strcatchar(*str, c);
}


/*
 * concatenate slurm_strerror(errno) onto string in buf, expand buf as needed
 *
 */
void _xslurm_strerrorcat(char **buf)
{

	char *err = slurm_strerror(errno);

	xstrcat(*buf, err);
}

/* 
 * append strftime of fmt to buffer buf, expand buf as needed 
 *
 */
void _xstrftimecat(char **buf, const char *fmt)
{
	char p[256];		/* output truncated to 256 chars */
	time_t t;
	struct tm tm;

	const char default_fmt[] = "%m/%d/%Y %H:%M:%S %Z";

	if (fmt == NULL)
		fmt = default_fmt;

	if (time(&t) == (time_t) -1) 
		fprintf(stderr, "time() failed\n");

	if (!localtime_r(&t, &tm))
		fprintf(stderr, "localtime_r() failed\n");

	strftime(p, sizeof(p), fmt, &tm);


	_xstrcat(buf, p);
}

/*
 * append formatted string with printf-style args to buf, expanding
 * buf as needed
 */
int _xstrfmtcat(char **str, const char *fmt, ...)
{
	va_list ap;
	int     rc; 
	char    buf[4096];

	va_start(ap, fmt);
	rc = vsnprintf(buf, 4096, fmt, ap);
	va_end(ap);

	xstrcat(*str, buf);

	return rc;
}

/*
 * append a range of memory from start to end to the string str,
 * expanding str as needed
 */
void _xmemcat(char **str, char *start, char *end)
{
	char buf[4096];
	size_t len;

	xassert(end >= start);

	len = (size_t) end - (size_t) start;

	if (len == 0)
		return;

	if (len > 4095)
		len = 4095;

	memcpy(buf, start, len);
	buf[len] = '\0';
	xstrcat(*str, buf);
}

/* 
 * Replacement for libc basename
 *   path (IN)		path possibly containing '/' characters
 *   RETURN		last component of path
 */
char * xbasename(char *path)
{
	char *p;

	p = strrchr(path , '/');
	return (p ? (p + 1) : path);
}	

/*
 * Duplicate a string.
 *   str (IN)		string to duplicate
 *   RETURN		copy of string
 */
char * xstrdup(const char *str)
{
	size_t siz,
	       rsiz;
	char   *result;

	if (str == NULL)
		return NULL;

	siz = strlen(str) + 1;
	result = (char *)xmalloc(siz);

	rsiz = strlcpy(result, str, siz);

	xassert(rsiz == siz-1);

	return result;
}
