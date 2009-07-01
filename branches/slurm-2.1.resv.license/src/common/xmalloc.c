/*****************************************************************************\
 *  xmalloc.c - enhanced malloc routines
 *  Started with Jim Garlick's xmalloc and tied into slurm log facility.
 *  Also added ability to print file, line, and function of caller.
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Jim Garlick <garlick1@llnl.gov> and 
 *	Mark Grondona <mgrondona@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
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

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>	/* for INT_MAX */

#include "src/common/xmalloc.h"
#include "src/common/log.h"
#include "src/common/macros.h"

#if	HAVE_UNSAFE_MALLOC
#  include <pthread.h>
   static pthread_mutex_t malloc_lock = PTHREAD_MUTEX_INITIALIZER;
#  define MALLOC_LOCK()		pthread_mutex_lock(&malloc_lock)
#  define MALLOC_UNLOCK()	pthread_mutex_unlock(&malloc_lock)
#else
#  define MALLOC_LOCK()	
#  define MALLOC_UNLOCK()	
#endif


#if NDEBUG
#  define xmalloc_assert(expr)  ((void) (0))
#else
static void malloc_assert_failed(char *, const char *, int, 
                                 const char *, const char *); 
#  define xmalloc_assert(expr)  _STMT_START {                                 \
          (expr) ? ((void)(0)) :                                              \
          malloc_assert_failed(__STRING(expr), file, line, func,              \
                               __CURRENT_FUNC__);                             \
          } _STMT_END
#endif /* NDEBUG */


/*
 * "Safe" version of malloc().
 *   size (IN)	number of bytes to malloc
 *   RETURN	pointer to allocate heap space
 */
void *slurm_xmalloc(size_t size, const char *file, int line, const char *func)
{	
	void *new;
	int *p;


	xmalloc_assert(size >= 0 && size <= INT_MAX);
	MALLOC_LOCK();
	p = (int *)malloc(size + 2*sizeof(int));
	MALLOC_UNLOCK();
	if (!p) {
		/* don't call log functions here, we're probably OOM 
		 */
		fprintf(log_fp(), "%s:%d: %s: xmalloc(%d) failed\n", 
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
void *slurm_try_xmalloc(size_t size, const char *file, int line, 
                        const char *func)
{	
	void *new;
	int *p;

	xmalloc_assert(size >= 0 && size <= INT_MAX);
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
void * slurm_xrealloc(void **item, size_t newsize, 
	              const char *file, int line, const char *func)
{
	int *p = NULL;

	/* xmalloc_assert(*item != NULL, file, line, func); */
	xmalloc_assert(newsize >= 0 && (int)newsize <= INT_MAX);

	if (*item != NULL) {
		int old_size;
		p = (int *)*item - 2;

		/* magic cookie still there? */
		xmalloc_assert(p[0] == XMALLOC_MAGIC);		
		old_size = p[1];

		MALLOC_LOCK();
		p = (int *)realloc(p, newsize + 2*sizeof(int));
		MALLOC_UNLOCK();

		if (p == NULL) 
			goto error;

		if (old_size < newsize) {
			char *p_new = (char *)(&p[2]) + old_size;
			memset(p_new, 0, (int)(newsize-old_size));
		}
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
	return *item;

  error:
	fprintf(log_fp(), "%s:%d: %s: xrealloc(%d) failed\n", 
		file, line, func, (int)newsize);
	abort();
}

/* 
 * same as above, but return <= 0 on malloc() failure instead of aborting.
 * `*item' will be unchanged.
 */
int slurm_try_xrealloc(void **item, size_t newsize, 
	               const char *file, int line, const char *func)
{
	int *p = NULL;

	/* xmalloc_assert(*item != NULL, file, line, func); */
	xmalloc_assert(newsize >= 0 && (int)newsize <= INT_MAX);

	if (*item != NULL) {
		int old_size;
		p = (int *)*item - 2;

		/* magic cookie still there? */
		xmalloc_assert(p[0] == XMALLOC_MAGIC);		
		old_size = p[1];

		MALLOC_LOCK();
		p = (int *)realloc(p, newsize + 2*sizeof(int));
		MALLOC_UNLOCK();

		if (p == NULL) 
			return 0;

		if (old_size < newsize) {
			char *p_new = (char *)(&p[2]) + old_size;
			memset(p_new, 0, (int)(newsize-old_size));
		}
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
int slurm_xsize(void *item, const char *file, int line, const char *func)
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
void slurm_xfree(void **item, const char *file, int line, const char *func)
{
	if (*item != NULL) {
		int *p = (int *)*item - 2;
		/* magic cookie still there? */
		xmalloc_assert(p[0] == XMALLOC_MAGIC);	
		p[0] = 0;	/* make sure xfree isn't called twice */
		MALLOC_LOCK();
		free(p);
		MALLOC_UNLOCK();
		*item = NULL;
	}
}

#ifndef NDEBUG
static void malloc_assert_failed(char *expr, const char *file, 
		                 int line, const char *caller, const char *func)
{
	error("%s() Error: from %s:%d: %s(): Assertion (%s) failed",
	      func, file, line, caller, expr);
	abort();
}
#endif
