/* $Id$ */

/*
** xassert: assert type macro with configurable handling
**          If NDEBUG is defined, do nothing.
**          If not, and expression is zero, log an error message and abort.
*/

#ifndef _XASSERT_H
#define _XASSERT_H	1

#if HAVE_CONFIG_H
#  include <config.h>
#endif

#include "macros.h"

#ifdef NDEBUG

#  define assert(expr)	((void)0)

#else /* !NDEBUG */

#  define xassert(__ex)  _STMT_START { \
     (__ex) ? ((void)0) : \
     __xassert_failed(__STRING(__ex), __FILE__,  __LINE__, __CURRENT_FUNC__) \
     } _STMT_END 

/*  This prints the assertion failed message to the slurm log facility
**  (see log.h) and aborts the calling program
**  (messages go to stderr if log is not initialized)
*/
extern void __xassert_failed(char *, const char *, int, const char *) 
	    __NORETURN_ATTR;

#endif /* NDEBUG. */

#endif /* !__XASSERT_H */

/* vim: set sw=4 ts=4 expandtabs */
