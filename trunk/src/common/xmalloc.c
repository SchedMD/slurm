/* $Id$ */

/* xmalloc.c: enhanced malloc routines
**
** Mark Grondona <mgrondona@llnl.gov>
** 
** Started with Jim Garlick's xmalloc and tied into slurm log facility.
** Also added ability to print file, line, and function of call.
*/

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>	/* for INT_MAX */

#include <src/common/xmalloc.h>
#include <src/common/log.h>

#if	HAVE_UNSAFE_MALLOC
#  include <pthread.h>
   static pthread_mutex_t malloc_lock = PTHREAD_MUTEX_INITIALIZER;
#  define MALLOC_LOCK()	pthread_mutex_lock(&malloc_lock)
#  define MALLOC_UNLOCK()	pthread_mutex_unlock(&malloc_lock)
#else
#  define MALLOC_LOCK()	
#  define MALLOC_UNLOCK()	
#endif


static void malloc_assert_failed(char *, const char *, int, 
        const char *, const char *); 

#define xmalloc_assert(expr, _file, _line, _func)  _STMT_START {    \
        (expr) ? ((void)(0)) :                                      \
        malloc_assert_failed(__STRING(expr), _file, _line, _func,   \
                             __CURRENT_FUNC__)                      \
        } _STMT_END

/*
 * "Safe" version of malloc().
 *   size (IN)	number of bytes to malloc
 *   RETURN	pointer to allocate heap space
 */
void *_xmalloc(size_t size, const char *file, int line, const char *func)
{	
	void *new;
	int *p;

	xmalloc_assert(size > 0 && size <= INT_MAX, file, line, func);
	MALLOC_LOCK();
	p = (int *)malloc(size + 2*sizeof(int));
	MALLOC_UNLOCK();
	if (!p) {
		fprintf(stderr, "%s:%d: %s: xmalloc(%d) failed\n", 
				file, line, func, (int)size);
		exit(1);
	}
	p[0] = XMALLOC_MAGIC;	/* add "secret" magic cookie */
	p[1] = size;		/* store size in buffer */

	new = &p[2];
	memset(new, 0, size);
	return new;
}

/* 
 * "Safe" version of realloc().  Args are different: pass in a pointer to
 * the object to be realloced instead of the object itself.
 *   item (IN/OUT)	double-pointer to allocated space
 *   newsize (IN)	requested size
 */
void _xrealloc(void **item, size_t newsize, 
	       const char *file, int line, const char *func)
{
	int *p = (int *)*item - 2;

	xmalloc_assert(*item != NULL, file, line, func);
	xmalloc_assert(newsize > 0 && newsize <= INT_MAX, file, line, func);

	/* magic cookie still there? */
	xmalloc_assert(p[0] == XMALLOC_MAGIC, file, line, func);		

	MALLOC_LOCK();
	p = (int *)realloc(p, newsize + 2*sizeof(int));
	MALLOC_UNLOCK();
	if (!p) {
		fprintf(stderr, "%s:%d: %s: xrealloc(%d) failed\n", 
				file, line, func, (int)newsize);
		exit(1);
	}
	xmalloc_assert(p[0] == XMALLOC_MAGIC, file, line, func);
	p[1] = newsize;
	*item = &p[2];
}

/*
 * Return the size of a buffer.
 *   item (IN)		pointer to allocated space
 */
int _xsize(void *item, const char *file, int line, const char *func)
{
	int *p = (int *)item - 2;
	xmalloc_assert(item != NULL, file, line, func);	
	xmalloc_assert(p[0] == XMALLOC_MAGIC, file, line, func); 
	return p[1];
}

/* 
 * Free which takes a pointer to object to free, which it turns into a null
 * object.
 *   item (IN/OUT)	double-pointer to allocated space
 */
void _xfree(void **item, const char *file, int line, const char *func)
{
	int *p = (int *)*item - 2;

	if (*item != NULL) {
		/* magic cookie still there? */
		xmalloc_assert(p[0] == XMALLOC_MAGIC, file, line, func);	
		MALLOC_LOCK();
		free(p);
		MALLOC_UNLOCK();
		*item = NULL;
	}
}

static void malloc_assert_failed(char *expr, const char *file, 
		                 int line, const char *caller, const char *func)
{
	fatal("malloc error: %s:%d: %s() caused assertion (%s) to fail in %s()",
	      file, line, caller, expr, func);
	abort();
}
