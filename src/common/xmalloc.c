/*****************************************************************************\
 *  xmalloc.c - enhanced malloc routines
 *  Started with Jim Garlick's xmalloc and tied into slurm log facility.
 *  Also added ability to print file, line, and function of caller.
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Jim Garlick <garlick1@llnl.gov> and
 *	Mark Grondona <mgrondona@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include "config.h"

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/xmalloc.h"

#if NDEBUG
#  define xmalloc_assert(expr)  ((void) (0))
#else
static void malloc_assert_failed(char *, const char *, int,
                                 const char *, const char *);
#  define xmalloc_assert(expr)  do {                                          \
          (expr) ? ((void)(0)) :                                              \
          malloc_assert_failed(__STRING(expr), file, line, func,              \
                               __func__);                             \
          } while (0)
#endif /* NDEBUG */

#define XMALLOC_MAGIC 0x42

/*
 * "Safe" version of malloc().
 *   size (IN)	number of bytes to malloc
 *   clear (IN) initialize to zero
 *   RETURN	pointer to allocate heap space
 */
void *slurm_xcalloc(size_t count, size_t size, bool clear, bool try,
		    const char *file, int line, const char *func)
{
	size_t total_size;
	size_t count_size;
	size_t *p;

	if (!size || !count)
		return NULL;

	/*
	 * Detect overflow of the size calculation and abort().
	 * Ensure there is sufficient space for the two header words used to
	 * store the magic value and the allocation length by dividing by two,
	 * and because on 32-bit systems, if a 2GB allocation request isn't
	 * sufficient (which would attempt to allocate 2GB + 8Bytes),
	 * then we're going to run into other problems anyways.
	 * (And on 64-bit, if a 2EB + 16Bytes request isn't sufficient...)
	 */
	if ((count != 1) && (count > SIZE_MAX / size / 4)) {
		if (try)
			return NULL;
		log_oom(file, line, func);
		abort();
	}

	count_size = count * size;
	total_size = count_size + 2 * sizeof(size_t);

	if (clear)
		p = calloc(1, total_size);
	else
		p = malloc(total_size);

	if (!p && try) {
		return NULL;
	} else if (!p) {
		/* out of memory */
		log_oom(file, line, func);
		abort();
	}
	p[0] = XMALLOC_MAGIC;	/* add "secret" magic cookie */
	p[1] = count_size;	/* store size in buffer */

	return &p[2];
}

/*
 * "Safe" version of realloc() / reallocarray().
 * Args are different: pass in a pointer to the object to be
 * realloced instead of the object itself.
 *   item (IN/OUT)	double-pointer to allocated space
 *   newcount (IN)	requested count
 *   newsize (IN)	requested size
 *   clear (IN)		initialize to zero
 */
extern void * slurm_xrecalloc(void **item, size_t count, size_t size,
			      bool clear, bool try, const char *file,
			      int line, const char *func)
{
	size_t total_size;
	size_t count_size;
	size_t *p;

	if (!size || !count)
		return NULL;

	/*
	 * Detect overflow of the size calculation and abort().
	 * Ensure there is sufficient space for the two header words used to
	 * store the magic value and the allocation length by dividing by two,
	 * and because on 32-bit systems, if a 2GB allocation request isn't
	 * sufficient (which would attempt to allocate 2GB + 8Bytes),
	 * then we're going to run into other problems anyways.
	 * (And on 64-bit, if a 2EB + 16Bytes request isn't sufficient...)
	 */
	if ((count != 1) && (count > SIZE_MAX / size / 4))
		goto error;

	count_size = count * size;
	total_size = count_size + 2 * sizeof(size_t);

	if (*item != NULL) {
		size_t old_size;
		p = (size_t *)*item - 2;

		/* magic cookie still there? */
		xmalloc_assert(p[0] == XMALLOC_MAGIC);
		old_size = p[1];

		p = realloc(p, total_size);
		if (p == NULL)
			goto error;

		if (old_size < count_size) {
			char *p_new = (char *)(&p[2]) + old_size;
			if (clear)
				memset(p_new, 0, (count_size - old_size));
		}
		xmalloc_assert(p[0] == XMALLOC_MAGIC);
	} else {
		/* Initalize new memory */
		if (clear)
			p = calloc(1, total_size);
		else
			p = malloc(total_size);
		if (p == NULL)
			goto error;
		p[0] = XMALLOC_MAGIC;
	}

	p[1] = count_size;
	*item = &p[2];
	return *item;

error:
	if (try)
		return NULL;
	log_oom(file, line, func);
	abort();
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
