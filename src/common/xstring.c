/*****************************************************************************\
 *  xstring.c - heap-oriented string manipulation functions with "safe"
 *	string expansion as needed.
 ******************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Jim Garlick <garlick@llnl.gov>
 *             Mark Grondona <grondona@llnl.gov>, et al.
 *
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
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

#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>

#include "slurm/slurm_errno.h"

#include "src/common/macros.h"
#include "src/common/slurm_time.h"
#include "src/common/strlcpy.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#define XFGETS_CHUNKSIZE 64

/* Static functions. */
static char *_xstrdup_vprintf(const char *_fmt, va_list _ap);

/*
 * Define slurm-specific aliases for use by plugins, see slurm_xlator.h
 * for details.
 */
strong_alias(_xstrcat,		slurm_xstrcat);
strong_alias(_xstrncat,		slurm_xstrncat);
strong_alias(_xstrcatchar,	slurm_xstrcatchar);
strong_alias(_xslurm_strerrorcat, slurm_xslurm_strerrorcat);
strong_alias(_xstrftimecat,	slurm_xstrftimecat);
strong_alias(_xstrfmtcat,	slurm_xstrfmtcat);
strong_alias(_xmemcat,		slurm_xmemcat);
strong_alias(xstrdup,		slurm_xstrdup);
strong_alias(xstrdup_printf,	slurm_xstrdup_printf);
strong_alias(xstrndup,		slurm_xstrndup);
strong_alias(xbasename,		slurm_xbasename);
strong_alias(_xstrsubstitute,   slurm_xstrsubstitute);
strong_alias(xstrstrip,         slurm_xstrstrip);
strong_alias(xshort_hostname,   slurm_xshort_hostname);
strong_alias(xstring_is_whitespace, slurm_xstring_is_whitespace);
strong_alias(xstrtolower,       slurm_xstrtolower);
strong_alias(xstrchr,           slurm_xstrchr);
strong_alias(xstrrchr,          slurm_xstrrchr);
strong_alias(xstrcmp,           slurm_xstrcmp);
strong_alias(xstrncmp,          slurm_xstrncmp);
strong_alias(xstrcasecmp,       slurm_xstrcasecmp);
strong_alias(xstrncasecmp,      slurm_xstrncasecmp);
strong_alias(xstrcasestr,       slurm_xstrcasestr);

/*
 * Ensure that a string has enough space to add 'needed' characters.
 * If the string is uninitialized, it should be NULL.
 */
static void makespace(char **str, int needed)
{
	if (*str == NULL)
		*str = xmalloc(needed + 1);
	else {
		int actual_size;
		int used = strlen(*str) + 1;
		int min_new_size = used + needed;
		int cur_size = xsize(*str);
		if (min_new_size > cur_size) {
			int new_size = min_new_size;
			if (new_size < (cur_size + XFGETS_CHUNKSIZE))
				new_size = cur_size + XFGETS_CHUNKSIZE;
			if (new_size < (cur_size * 2))
				new_size = cur_size * 2;

			xrealloc(*str, new_size);
			actual_size = xsize(*str);
			if (actual_size)
				xassert(actual_size == new_size);
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
 * Concatenate len of str2 onto str1, expanding str1 as needed.
 *   str1 (IN/OUT)	target string (pointer to in case of expansion)
 *   str2 (IN)		source string
 *   len (IN)		len of str2 to concat
 */
void _xstrncat(char **str1, const char *str2, size_t len)
{
	if (str2 == NULL)
		str2 = "(null)";

	makespace(str1, len);
	strncat(*str1, str2, len);
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

	if (!slurm_localtime_r(&t, &tm))
		fprintf(stderr, "localtime_r() failed\n");

	strftime(p, sizeof(p), fmt, &tm);


	_xstrcat(buf, p);
}

/*
 * Append a ISO 8601 formatted timestamp to buffer buf, expand as needed
 */
void _xiso8601timecat(char **buf, bool msec)
{
	char p[64] = "";
	struct timeval tv;
	struct tm tm;

	if (gettimeofday(&tv, NULL) == -1)
		fprintf(stderr, "gettimeofday() failed\n");

	if (!slurm_localtime_r(&tv.tv_sec, &tm))
		fprintf(stderr, "localtime_r() failed\n");

	if (strftime(p, sizeof(p), "%Y-%m-%dT%T", &tm) == 0)
		fprintf(stderr, "strftime() returned 0\n");

	if (msec)
		_xstrfmtcat(buf, "%s.%3.3d", p, (int)(tv.tv_usec / 1000));
	else
		_xstrfmtcat(buf, "%s", p);
}

/*
 * Append a RFC 5424 formatted timestamp to buffer buf, expand as needed
 *
 */
void _xrfc5424timecat(char **buf, bool msec)
{
	char p[64] = "";
	char z[12] = "";
	struct timeval tv;
	struct tm tm;

	if (gettimeofday(&tv, NULL) == -1)
		fprintf(stderr, "gettimeofday() failed\n");

	if (!slurm_localtime_r(&tv.tv_sec, &tm))
		fprintf(stderr, "localtime_r() failed\n");

	if (strftime(p, sizeof(p), "%Y-%m-%dT%T", &tm) == 0)
		fprintf(stderr, "strftime() returned 0\n");

	/* The strftime %z format creates timezone offsets of the form
	 * (+/-)hhmm, whereas the RFC 5424 format is (+/-)hh:mm. So
	 * shift the minutes one step back and insert the semicolon.
	 */
	if (strftime(z, sizeof(z), "%z", &tm) == 0)
		fprintf(stderr, "strftime() returned 0\n");
	z[5] = z[4];
	z[4] = z[3];
	z[3] = ':';

	if (msec)
		_xstrfmtcat(buf, "%s.%3.3d%s", p, (int)(tv.tv_usec / 1000), z);
	else
		_xstrfmtcat(buf, "%s%s", p, z);
}

/*
 * append formatted string with printf-style args to buf, expanding
 * buf as needed
 */
int _xstrfmtcat(char **str, const char *fmt, ...)
{
	int n;
	char *p = NULL;
	va_list ap;

	va_start(ap, fmt);
	p = _xstrdup_vprintf(fmt, ap);
	va_end(ap);

	if (p == NULL)
		return 0;

	n = strlen(p);
	xstrcat(*str, p);
	xfree(p);

	return n;
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
	size_t siz;
	size_t rsiz;
	char   *result;

	if (str == NULL) {
		return NULL;
	}
	siz = strlen(str) + 1;
	result = (char *)xmalloc(siz);

	rsiz = strlcpy(result, str, siz);
	if (rsiz)
		xassert(rsiz == siz-1);

	return result;
}

/*
 * Give me a copy of the string as if it were printf.
 *   fmt (IN)		format of string and args if any
 *   RETURN		copy of formated string
 */
char *xstrdup_printf(const char *fmt, ...)
{
	char *result;
	va_list ap;

	va_start(ap, fmt);
	result = _xstrdup_vprintf(fmt, ap);
	va_end(ap);

	return result;
}

/*
 * Duplicate at most "n" characters of a string.
 *   str (IN)		string to duplicate
 *   n (IN)
 *   RETURN		copy of string
 */
char * xstrndup(const char *str, size_t n)
{
	size_t siz;
	char   *result;

	if (str == NULL)
		return NULL;

	siz = strlen(str);
	if (n < siz)
		siz = n;
	siz++;
	result = (char *)xmalloc(siz);

	(void) strlcpy(result, str, siz);

	return result;
}

/*
** strtol which only reads 'n' number of chars in the str to get the number
*/
long int xstrntol(const char *str, char **endptr, size_t n, int base)
{
	long int number = 0;
	char new_str[n+1], *new_endptr = NULL;

	memcpy(new_str, str, n);
	new_str[n] = '\0';

	number = strtol(new_str, &new_endptr, base);
	if (endptr)
		*endptr = ((char *)str) + (new_endptr - new_str);

	return number;
}

/*
 * Find the first instance of a sub-string "pattern" in the string "str",
 * and replace it with the string "replacement".
 *   str (IN/OUT)	target string (pointer to in case of expansion)
 *   pattern (IN)	substring to look for in str
 *   replacement (IN)   string with which to replace the "pattern" string
 */
bool _xstrsubstitute(char **str, const char *pattern, const char *replacement)
{
	int pat_len, rep_len;
	char *ptr, *end_copy;
	int pat_offset;

	if (*str == NULL || pattern == NULL || pattern[0] == '\0')
		return 0;

	if ((ptr = strstr(*str, pattern)) == NULL)
		return 0;
	pat_offset = ptr - (*str);
	pat_len = strlen(pattern);
	if (replacement == NULL)
		rep_len = 0;
	else
		rep_len = strlen(replacement);

	end_copy = xstrdup(ptr + pat_len);
	if (rep_len != 0) {
		makespace(str, rep_len-pat_len);
		strcpy((*str)+pat_offset, replacement);
	}
	strcpy((*str)+pat_offset+rep_len, end_copy);
	xfree(end_copy);

	return 1;
}

/*
 * Remove first instance of quotes that surround a string in "str",
 *   and return the result without the quotes
 *   str (IN)	        target string (pointer to in case of expansion)
 *   increased (IN/OUT)	current position in "str"
 *   RET char *         str returned without quotes in it. needs to be xfreed
 */
char *xstrstrip(char *str)
{
	int i=0, start=0, found = 0;
	char *meat = NULL;
	char quote_c = '\0';
	int quote = 0;

	if (!str)
		return NULL;

	/* first strip off the ("|')'s */
	if (str[i] == '\"' || str[i] == '\'') {
		quote_c = str[i];
		quote = 1;
		i++;
	}
	start = i;

	while(str[i]) {
		if (quote && str[i] == quote_c) {
			found = 1;
			break;
		}
		i++;
	}
	if (found) {
		meat = xmalloc((i-start)+1);
		memcpy(meat, str+start, (i-start));
	} else
		meat = xstrdup(str);
	return meat;
}


/* xshort_hostname
 *   Returns an xmalloc'd string containing the hostname
 *   of the local machine.  The hostname contains only
 *   the short version of the hostname (e.g. "linux123.foo.bar"
 *   becomes "linux123")
 *
 *   Returns NULL on error.
 */
char *xshort_hostname(void)
{
	int error_code;
	char *dot_ptr, path_name[1024];

	error_code = gethostname (path_name, sizeof(path_name));
	if (error_code)
		return NULL;

	dot_ptr = strchr (path_name, '.');
	if (dot_ptr != NULL)
		dot_ptr[0] = '\0';

	return xstrdup(path_name);
}

/* Returns true if all characters in a string are whitespace characters,
 * otherwise returns false;
 */
bool xstring_is_whitespace(const char *str)
{
	int i, len;

	len = strlen(str);
	for (i = 0; i < len; i++) {
		if (!isspace((int)str[i])) {
			return false;
		}
	}

	return true;
}

/*
 * If str make everything lowercase.  Should not be called on static char *'s
 */
char *xstrtolower(char *str)
{
	if (str) {
		int j = 0;
		while (str[j]) {
			str[j] = tolower((int)str[j]);
			j++;
		}
	}
	return str;
}

/* safe strchr */
char *xstrchr(const char *s1, int c)
{
	return s1 ? strchr(s1, c) : NULL;
}

/* safe strrchr */
char *xstrrchr(const char *s1, int c)
{
	return s1 ? strrchr(s1, c) : NULL;
}

/* safe strcmp */
int xstrcmp(const char *s1, const char *s2)
{
	if (!s1 && !s2)
		return 0;
	else if (!s1)
		return -1;
	else if (!s2)
		return 1;
	else
		return strcmp(s1, s2);
}


/* safe strncmp */
int xstrncmp(const char *s1, const char *s2, size_t n)
{
	if (!s1 && !s2)
		return 0;
	else if (!s1)
		return -1;
	else if (!s2)
		return 1;
	else
		return strncmp(s1, s2, n);
}

/* safe strcasecmp */
int xstrcasecmp(const char *s1, const char *s2)
{
	if (!s1 && !s2)
		return 0;
	else if (!s1)
		return -1;
	else if (!s2)
		return 1;
	else
		return strcasecmp(s1, s2);
}

/* safe strncasecmp */
int xstrncasecmp(const char *s1, const char *s2, size_t n)
{
	if (!s1 && !s2)
		return 0;
	else if (!s1)
		return -1;
	else if (!s2)
		return 1;
	else
		return strncasecmp(s1, s2, n);
}

char *xstrcasestr(char *haystack, char *needle)
{
	int hay_inx, hay_size, need_inx, need_size;
	char *hay_ptr = haystack;

	if (haystack == NULL || needle == NULL)
		return NULL;

	hay_size = strlen(haystack);
	need_size = strlen(needle);

	for (hay_inx=0; hay_inx<hay_size; hay_inx++) {
		for (need_inx=0; need_inx<need_size; need_inx++) {
			if (tolower((int) hay_ptr[need_inx]) !=
			    tolower((int) needle [need_inx]))
				break;		/* mis-match */
		}

		if (need_inx == need_size)	/* it matched */
			return hay_ptr;
		else				/* keep looking */
			hay_ptr++;
	}

	return NULL;	/* no match anywhere in string */
}

/*
 * Give me a copy of the string as if it were printf.
 * This is stdarg-compatible routine, so vararg-compatible
 * functions can do va_start() and invoke this function.
 *
 *   fmt (IN)		format of string and args if any
 *   RETURN		copy of formated string
 */
static char *_xstrdup_vprintf(const char *fmt, va_list ap)
{
	/* Start out with a size of 100 bytes. */
	int n, size = 100;
	char *p = NULL;
	va_list our_ap;

	if ((p = xmalloc(size)) == NULL)
		return NULL;
	while (1) {
		/* Try to print in the allocated space. */
		va_copy(our_ap, ap);
		n = vsnprintf(p, size, fmt, our_ap);
		va_end(our_ap);
		/* If that worked, return the string. */
		if (n > -1 && n < size)
			return p;
		/* Else try again with more space. */
		if (n > -1)               /* glibc 2.1 */
			size = n + 1;           /* precisely what is needed */
		else                      /* glibc 2.0 */
			size *= 2;              /* twice the old size */
		if ((p = xrealloc(p, size)) == NULL)
			return NULL;
	}
	/* NOTREACHED */
}
