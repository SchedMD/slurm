/* $Id$ */

/* 
** enhanced malloc routines for slurm
** 
**  o attempt to report file, line, and calling function on error
**  o use configurable slurm log facility for reporting error
**
*/

#ifndef _XMALLOC_H
#define _XMALLOC_H

#if HAVE_SYS_TYPES_H
#  include <sys/types.h>
#endif

#include <src/common/macros.h>

#define xmalloc(__sz) 		\
         _xmalloc (__sz, __FILE__, __LINE__, __CURRENT_FUNC__)
#define xfree(__p)		\
         _xfree((void **)&(__p), __FILE__, __LINE__, __CURRENT_FUNC__)
#define xrealloc(__p, __sz) 	\
         _xrealloc((void **)&(__p), __sz, __FILE__, __LINE__, __CURRENT_FUNC__)
#define xsize(__p)		\
	 _xsize((void *)__p, __FILE__, __LINE__, __CURRENT_FUNC__)

void *_xmalloc(size_t size, const char *file, int line, const char *func);
void _xfree(void **p, const char *file, int line, const char *func);
void _xrealloc(void **p, size_t newsize, 
	       const char *file, int line, const char *func);
int _xsize(void *p, const char *file, int line, const char *func);

#define XMALLOC_MAGIC 0x42

#endif /* !_XMALLOC_H */
