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
 *  For details, see <http://slurm.schedmd.com/>.
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

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/xmalloc.h"

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
 *   clear (IN) initialize to zero
 *   RETURN	pointer to allocate heap space
 */
void *slurm_xmalloc(size_t size, bool clear,
		    const char *file, int line, const char *func)
{
	void *new;
	size_t *p;
	size_t total_size = size + 2 * sizeof(size_t);

	if (clear)
		p = calloc(1, total_size);
	else
		p = malloc(total_size);
	if (!p) {
		/* out of memory */
		log_oom(file, line, func);
		abort();
	}
	p[0] = XMALLOC_MAGIC;	/* add "secret" magic cookie */
	p[1] = size;		/* store size in buffer */

	new = &p[2];
	return new;
}

/*
 * same as above, except return NULL on malloc failure instead of exiting
 */
void *slurm_try_xmalloc(size_t size, const char *file, int line,
                        const char *func)
{
	void *new;
	size_t *p;
	size_t total_size = size + 2 * sizeof(size_t);

	p = calloc(1, total_size);
	if (!p) {
		return NULL;
	}
	p[0] = XMALLOC_MAGIC;	/* add "secret" magic cookie */
	p[1] = size;		/* store size in buffer */

	new = &p[2];
	return new;
}

/*
 * "Safe" version of realloc().  Args are different: pass in a pointer to
 * the object to be realloced instead of the object itself.
 *   item (IN/OUT)	double-pointer to allocated space
 *   newsize (IN)	requested size
 *   clear (IN)		initialize to zero
 */
extern void * slurm_xrealloc(void **item, size_t newsize, bool clear,
			     const char *file, int line, const char *func)
{
	size_t *p = NULL;

	if (*item != NULL) {
		size_t old_size;
		p = (size_t *)*item - 2;

		/* magic cookie still there? */
		xmalloc_assert(p[0] == XMALLOC_MAGIC);
		old_size = p[1];

		p = realloc(p, newsize + 2*sizeof(size_t));
		if (p == NULL)
			goto error;

		if (old_size < newsize) {
			char *p_new = (char *)(&p[2]) + old_size;
			if (clear)
				memset(p_new, 0, (newsize-old_size));
		}
		xmalloc_assert(p[0] == XMALLOC_MAGIC);

	} else {
		size_t total_size = newsize + 2 * sizeof(size_t);
		/* Initalize new memory */
		if (clear)
			p = calloc(1, total_size);
		else
			p = malloc(total_size);
		if (p == NULL)
			goto error;
		p[0] = XMALLOC_MAGIC;
	}

	p[1] = newsize;
	*item = &p[2];
	return *item;

  error:
	log_oom(file, line, func);
	abort();
}

/*
 * same as above, but return <= 0 on malloc() failure instead of aborting.
 * `*item' will be unchanged.
 */
int slurm_try_xrealloc(void **item, size_t newsize,
	               const char *file, int line, const char *func)
{
	size_t *p = NULL;

	if (*item != NULL) {
		size_t old_size;
		p = (size_t *)*item - 2;

		/* magic cookie still there? */
		xmalloc_assert(p[0] == XMALLOC_MAGIC);
		old_size = p[1];

		p = realloc(p, newsize + 2*sizeof(size_t));
		if (p == NULL)
			return 0;

		if (old_size < newsize) {
			char *p_new = (char *)(&p[2]) + old_size;
			memset(p_new, 0, (newsize-old_size));
		}
		xmalloc_assert(p[0] == XMALLOC_MAGIC);

	} else {
		size_t total_size = newsize + 2 * sizeof(size_t);
		/* Initalize new memory */
		p = calloc(1, total_size);
		if (p == NULL)
			return 0;
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
size_t slurm_xsize(void *item, const char *file, int line, const char *func)
{
	size_t *p = (size_t *)item - 2;
	xmalloc_assert(item != NULL);
	xmalloc_assert(p[0] == XMALLOC_MAGIC); /* CLANG false positive here */
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
		size_t *p = (size_t *)*item - 2;
		/* magic cookie still there? */
		xmalloc_assert(p[0] == XMALLOC_MAGIC);
		p[0] = 0;	/* make sure xfree isn't called twice */
		free(p);
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
