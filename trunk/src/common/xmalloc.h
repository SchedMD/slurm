/* $Id$ */

/* 
 * xmalloc: enhanced malloc routines for slurm
 * 
 *  o default: never return if errors are encountered.
 *
 *  o attempt to report file, line, and calling function on assertion failure
 *
 *  o use configurable slurm log facility for reporting errors
 *
 * Description:
 *
 * void *xmalloc(size_t size);
 * void *try_xmalloc(size_t size);
 * void xrealloc(void *p, size_t newsize);
 * int  try_xrealloc(void *p, size_t newsize);
 * void xfree(void *p);
 * int  xsize(void *p);
 *
 * xmalloc(size) allocates size bytes and returns a pointer to the allocated
 * memory. The memory is set to zero. xmalloc() will not return unless
 * there are no errors. The memory must be freed using xfree().
 *
 * try_xmalloc(size) is the same as above, but a NULL pointer is returned
 * when there is an error allocating the memory.
 *
 * xrealloc(p, newsize) changes the size of the block pointed to by p to the
 * value of newsize. Newly allocated memory is not zeroed. If p is NULL, 
 * xrealloc() performs the same function as  `p = xmalloc(newsize)'. If p 
 * is not NULL, it is required to have been initialized with a call to 
 * [try_]xmalloc() or [try_]xrealloc().
 *
 * try_xrealloc(p, newsize) is the same as above, but returns <= 0 if the
 * there is an error allocating the requested memory.
 * 
 * xfree(p) frees the memory block pointed to by p. The memory must have been
 * initialized with a call to [try_]xmalloc() or [try_]xrealloc().
 *
 * xsize(p) returns the current size of the memory allocation pointed to by
 * p. The memory must have been allocated with [try_]xmalloc() or 
 * [try_]xrealloc().
 *
 */

#ifndef _XMALLOC_H
#define _XMALLOC_H

#if HAVE_SYS_TYPES_H
#  include <sys/types.h>
#endif

#include "macros.h"

#define xmalloc(__sz) 		\
	_xmalloc (__sz, __FILE__, __LINE__, __CURRENT_FUNC__)

#define try_xmalloc(__sz)	\
	_try_xmalloc(__sz, __FILE__, __LINE__, __CURRENT_FUNC__)

#define xfree(__p)		\
	_xfree((void **)&(__p), __FILE__, __LINE__, __CURRENT_FUNC__)

#define xrealloc(__p, __sz) 	\
        _xrealloc((void **)&(__p), __sz, __FILE__, __LINE__, __CURRENT_FUNC__)

#define try_xrealloc(__p, __sz) \
	_try_xrealloc((void **)&(__p), __sz, __FILE__, __LINE__, \
		      __CURRENT_FUNC__)

#define xsize(__p)		\
	_xsize((void *)__p, __FILE__, __LINE__, __CURRENT_FUNC__)

void *_xmalloc(size_t size, const char *file, int line, const char *func);
void *_try_xmalloc(size_t size, const char *file, int line, const char *func);
void _xfree(void **p, const char *file, int line, const char *func);
void _xrealloc(void **p, size_t newsize, 
	       const char *file, int line, const char *func);
int  _try_xrealloc(void **p, size_t newsize,
		   const char *file, int line, const char *func);
int _xsize(void *p, const char *file, int line, const char *func);

#define XMALLOC_MAGIC 0x42

#endif /* !_XMALLOC_H */
