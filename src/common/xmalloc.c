/* $Id$ */

/* xmalloc.c: enhanced malloc routines
 *
 * Mark Grondona <mgrondona@llnl.gov>
 * 
 * Started with Jim Garlick's xmalloc and tied into slurm log facility.
 * Also added ability to print file, line, and function of caller.
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
#  define MALLOC_LOCK()		pthread_mutex_lock(&malloc_lock)
#  define MALLOC_UNLOCK()	pthread_mutex_unlock(&malloc_lock)
#else
#  define MALLOC_LOCK()	
#  define MALLOC_UNLOCK()	
#endif

static void malloc_assert_failed(char *, const char *, int, 
                                 const char *, const char *); 

#if NDEBUG
#  define xmalloc_assert  ((void)0)
#else
#  define xmalloc_assert(expr)  _STMT_START {  			    \
          (expr) ? ((void)(0)) :                                    \
          malloc_assert_failed(__STRING(expr), file, line, func,    \
                               __CURRENT_FUNC__)                    \
          } _STMT_END
#endif /* NDEBUG */


/*
 * "Safe" version of malloc().
 *   size (IN)	number of bytes to malloc
 *   RETURN	pointer to allocate heap space
 */
void *_xmalloc(size_t size, const char *file, int line, const char *func)
{	
	void *new;
	int *p;

	xmalloc_assert(size > 0 && size <= INT_MAX);
	MALLOC_LOCK();
	p = (int *)malloc(size + 2*sizeof(int));
	MALLOC_UNLOCK();
	if (!p) {
		/* don't call log functions here, we're probably OOM 
		 */
		fprintf(stderr, "%s:%d: %s: xmalloc(%d) failed\n", 
				file, line, func, (int)size);
		exit(1);
	}
	p[0] = XMALLOC_MAGIC;	/* add "secret" magic cookie */
	p[1] = (int)size;	/* store size in buffer */

	new = &p[2];
	memset(new, 0, size);
	return new;
}

/*
 * same as above, except return NULL on malloc failure instead of exiting
 */
void *_try_xmalloc(size_t size, const char *file, int line, const char *func)
{	
	void *new;
	int *p;

	xmalloc_assert(size > 0 && size <= INT_MAX);
	MALLOC_LOCK();
	p = (int *)malloc(size + 2*sizeof(int));
	MALLOC_UNLOCK();
	if (!p) {
		return NULL;
	}
	p[0] = XMALLOC_MAGIC;	/* add "secret" magic cookie */
	p[1] = (int)size;	/* store size in buffer */

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
	int *p = NULL;

	/* xmalloc_assert(*item != NULL, file, line, func); */
	xmalloc_assert(newsize >= 0 && (int)newsize <= INT_MAX);

	if (*item != NULL) {
		p = (int *)*item - 2;

		/* magic cookie still there? */
		xmalloc_assert(p[0] == XMALLOC_MAGIC);		

		MALLOC_LOCK();
		p = (int *)realloc(p, newsize + 2*sizeof(int));
		MALLOC_UNLOCK();

		if (p == NULL) 
			goto error;

		xmalloc_assert(p[0] == XMALLOC_MAGIC);

	} else {
		/* Initalize new memory */
		MALLOC_LOCK();
		p = (int *)malloc(newsize + 2*sizeof(int));
		MALLOC_UNLOCK();

		if (p == NULL) 
			goto error;

		memset(&p[2], 0, newsize);
		p[0] = XMALLOC_MAGIC;
	}

	p[1] = (int)newsize;
	*item = &p[2];
	return;

  error:
	fprintf(stderr, "%s:%d: %s: xrealloc(%d) failed\n", 
		file, line, func, (int)newsize);
	abort();
}

/* 
 * same as above, but return <= 0 on malloc() failure instead of aborting.
 * `*item' will be unchanged.
 */
int _try_xrealloc(void **item, size_t newsize, 
	          const char *file, int line, const char *func)
{
	int *p = NULL;

	/* xmalloc_assert(*item != NULL, file, line, func); */
	xmalloc_assert(newsize >= 0 && (int)newsize <= INT_MAX);

	if (*item != NULL) {
		p = (int *)*item - 2;

		/* magic cookie still there? */
		xmalloc_assert(p[0] == XMALLOC_MAGIC);		

		MALLOC_LOCK();
		p = (int *)realloc(p, newsize + 2*sizeof(int));
		MALLOC_UNLOCK();

		if (p == NULL) 
			return 0;

		xmalloc_assert(p[0] == XMALLOC_MAGIC);

	} else {
		/* Initalize new memory */
		MALLOC_LOCK();
		p = (int *)malloc(newsize + 2*sizeof(int));
		MALLOC_UNLOCK();

		if (p == NULL) 
			return 0;

		memset(&p[2], 0, newsize);
		p[0] = XMALLOC_MAGIC;
	}

	p[1] = newsize;
	*item = &p[2];
	return 1;
}


/*
 * Return the size of a buffer.
 *   item (IN)		pointer to allocated space
 */
int _xsize(void *item, const char *file, int line, const char *func)
{
	int *p = (int *)item - 2;
	xmalloc_assert(item != NULL);	
	xmalloc_assert(p[0] == XMALLOC_MAGIC); 
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
		xmalloc_assert(p[0] == XMALLOC_MAGIC);	
		MALLOC_LOCK();
		free(p);
		MALLOC_UNLOCK();
		*item = NULL;
	}
}

static void malloc_assert_failed(char *expr, const char *file, 
		                 int line, const char *caller, const char *func)
{
	fatal("%s() Error: from %s:%d: %s(): Assertion (%s) failed",
	      func, file, line, caller, expr);
	abort();
}
