/* $Id$ */

/* Started with Jim Garlick's xstring functions from pdsh 
 *
 * Mark Grondona <mgrondona@llnl.gov>
 *
 */

/* $Id$ */

/*
 * Heap-oriented string functions.
 */

#if     HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>
#include <stdio.h>
#if 	HAVE_STRERROR_R && !HAVE_DECL_STRERROR_R
char *strerror_r(int, char *, int);
#endif
#include <errno.h>
#if 	HAVE_UNISTD_H
#  include <unistd.h>
#endif

#include <pthread.h>

#include <xmalloc.h>
#include <xstring.h>
#include <strlcpy.h>
#include <xassert.h>

#define XFGETS_CHUNKSIZE 64


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
 *   size (IN/OUT)	size of str1 (pointer to in case of expansion)
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
 * concatenate strerror(errno) onto string in buf, expand buf as needed
 *
 */
void _xstrerrorcat(char **buf)
{
#if HAVE_STRERROR_R 
#  if HAVE_WORKING_STRERROR_R
	char errbuf[64];
	char *err = strerror_r(errno, errbuf, 64);
#  else
	char err[64];
	strerror_r(errno, err, 64);
#  endif
#elif HAVE_STRERROR
	char *err = strerror(errno);
#else
	extern char *sys_errlist[];
	char *err = sys_errlist[errno];
#endif
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
	struct tm *tm_ptr = NULL;
	static pthread_mutex_t localtime_lock = PTHREAD_MUTEX_INITIALIZER;

	const char default_fmt[] = "%m/%d/%Y %H:%M:%S %Z";

	if (fmt == NULL)
		fmt = default_fmt;

	if (time(&t) == (time_t) -1) 
		fprintf(stderr, "time() failed\n");

	pthread_mutex_lock(&localtime_lock);
	if (!(tm_ptr = localtime(&t)))
		fprintf(stderr, "localtime() failed\n");

	strftime(p, sizeof(p), fmt, tm_ptr);

	pthread_mutex_unlock(&localtime_lock);

	_xstrcat(buf, p);
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
