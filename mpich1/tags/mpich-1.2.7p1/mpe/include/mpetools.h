/* $Id: mpetools.h,v 1.1 2000/06/14 06:16:47 chan Exp $ */

/*
    This file contains some basic definitions that the tools routines
    may use.  They include:

    The name of the storage allocator
 */    
#ifndef __MPETOOLS
#define __MPETOOLS

#include <stdio.h>
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#define MALLOC(a)    malloc((unsigned)(a))
#define FREE(a)      free((char *)(a))
#define CALLOC(a,b)    calloc((unsigned)(a),(unsigned)(b))
#define REALLOC(a,b)   realloc(a,(unsigned)(b))

#define NEW(a)    (a *)MALLOC(sizeof(a))

#define MEMSET(s,c,n)   memset((char*)(s),c,n)


#endif
