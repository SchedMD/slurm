/*****************************************************************************\
 *  xstring.h - "safe" string processing functions with automatic memory 
 *	        management
 ******************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Jim Garlick <garlick@llnl.gov>, et. al.
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

#ifndef _XSTRING_H
#define _XSTRING_H	1

#include "src/common/macros.h"

#define xstrcat(__p, __q)		_xstrcat(&(__p), __q)
#define xstrcatchar(__p, __c)		_xstrcatchar(&(__p), __c)
#define xslurm_strerrorcat(__p)		_xslurm_strerrorcat(&(__p))
#define xstrftimecat(__p, __fmt)	_xstrftimecat(&(__p), __fmt)
#define xstrfmtcat(__p, __fmt, args...)	_xstrfmtcat(&(__p), __fmt, ## args)
#define xmemcat(__p, __s, __e)          _xmemcat(&(__p), __s, __e)
#define xstrsubstitute(__p, __pat, __rep) _xstrsubstitute(&(__p), __pat, __rep)

/*
** The following functions take a ptr to a string and expand the
** size of that string as needed to append the requested data.
** the macros above are provided to automatically take the
** address of the first argument, thus simplifying the interface
**
** space is allocated with xmalloc/xrealloc, so caller must use
** xfree to free.
** 
*/

/* 
** cat str2 onto str1, expanding str1 as necessary
*/
void _xstrcat(char **str1, const char *str2);

/* 
** concatenate one char, `c', onto str1, expanding str1 as needed	
*/
void _xstrcatchar(char **str1, char c);

/*
** concatenate stringified errno onto str 
*/
void _xslurm_strerrorcat(char **str);

/*
** concatenate current time onto str, using fmt if it is non-NUL
** see strftime(3) for the usage of the format string
*/
void _xstrftimecat(char **str, const char *fmt);

/*
** concatenate printf-style formatted string onto str
** return value is result from vsnprintf(3)
*/
int _xstrfmtcat(char **str, const char *fmt, ...);

/*
** concatenate range of memory from start to end (not including end)
** onto str. 
*/
void _xmemcat(char **str, char *start, char *end);

/*
** strdup which uses xmalloc routines
*/
char *xstrdup(const char *str);

/*
** strdup formatted which uses xmalloc routines
*/
char *xstrdup_printf(const char *fmt, ...);

/*
** strndup which uses xmalloc routines
*/
char *xstrndup(const char *str, size_t n);

/*
** strtol which only reads 'n' number of chars in the str to get the number
*/
long int xstrntol(const char *str, char **endptr, size_t n, int base);

/*
** replacement for libc basename
*/
char *xbasename(char *path);

/*
** Find the first instance of a sub-string "pattern" in the string "str",
** and replace it with the string "replacement".
*/
void _xstrsubstitute(char **str, const char *pattern, const char *replacement);

/* xshort_hostname
 *   Returns an xmalloc'd string containing the hostname
 *   of the local machine.  The hostname contains only
 *   the short version of the hostname (e.g. "linux123.foo.bar"
 *   becomes "linux123") 
 *
 *   Returns NULL on error.
 */
char *xshort_hostname(void);

/* 
 * Return true if all characters in a string are whitespace characters,
 * otherwise return false.  ("str" must be terminated by a null character)
 */
bool xstring_is_whitespace(const char *str);

#endif /* !_XSTRING_H */
