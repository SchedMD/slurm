/* $Id$ */

/* Started with Jim Garlick's xstring functions from pdsh */

/* $Id$ */

#ifndef _XSTRING_H
#define _XSTRING_H	1

#include <src/common/macros.h>

#define xstrcat(__p, __q)		_xstrcat(&(__p), __q)
#define xstrcatchar(__p, __c)		_xstrcatchar(&(__p), __c)
#define xstrerrorcat(__p)		_xstrerrorcat(&(__p))
#define xstrftimecat(__p, __fmt)	_xstrftimecat(&(__p), __fmt)

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
void _xstrerrorcat(char **str);

/*
** concatenate current time onto str, using fmt if it is non-NUL
** see strftime(3) for the usage of the format string
*/
void _xstrftimecat(char **str, const char *fmt);

/*
** strdup which uses xmalloc routines
*/
char *xstrdup(const char *str);

/*
** replacement for libc basename
*/
char *xbasename(char *path);

#endif /* !_XSTRING_H */
