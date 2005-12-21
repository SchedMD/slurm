/*****************************************************************************\
 *  xstring.h - "safe" string processing functions with automatic memory 
 *	        management
 ******************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Jim Garlick <garlick@llnl.gov>, et. al.
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

#ifndef _XSTRING_H
#define _XSTRING_H	1

#include "src/common/macros.h"

#define xstrcat(__p, __q)		_xstrcat(&(__p), __q)
#define xstrcatchar(__p, __c)		_xstrcatchar(&(__p), __c)
#define xslurm_strerrorcat(__p)		_xslurm_strerrorcat(&(__p))
#define xstrftimecat(__p, __fmt)	_xstrftimecat(&(__p), __fmt)
#define xstrfmtcat(__p, __fmt, args...)	_xstrfmtcat(&(__p), __fmt, ## args)
#define xmemcat(__p, __s, __e)          _xmemcat(&(__p), __s, __e)

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
** replacement for libc basename
*/
char *xbasename(char *path);

#endif /* !_XSTRING_H */
