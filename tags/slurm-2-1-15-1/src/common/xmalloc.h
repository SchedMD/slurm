/*****************************************************************************\
 *  xmalloc.h - enhanced malloc routines for slurm
 *  - default: never return if errors are encountered.
 *  - attempt to report file, line, and calling function on assertion failure
 *  - use configurable slurm log facility for reporting errors
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
 *****************************************************************************
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
\*****************************************************************************/

#ifndef _XMALLOC_H
#define _XMALLOC_H

#if HAVE_SYS_TYPES_H
#  include <sys/types.h>
#endif

#include "macros.h"

#define xmalloc(__sz) \
	slurm_xmalloc (__sz, __FILE__, __LINE__, __CURRENT_FUNC__)

#define try_xmalloc(__sz) \
	slurm_try_xmalloc(__sz, __FILE__, __LINE__, __CURRENT_FUNC__)

#define xfree(__p) \
	slurm_xfree((void **)&(__p), __FILE__, __LINE__, __CURRENT_FUNC__)

#define xrealloc(__p, __sz) \
        slurm_xrealloc((void **)&(__p), __sz, \
                       __FILE__, __LINE__, __CURRENT_FUNC__)

#define try_xrealloc(__p, __sz) \
	slurm_try_xrealloc((void **)&(__p), __sz, \
                           __FILE__, __LINE__,  __CURRENT_FUNC__)

#define xsize(__p) \
	slurm_xsize((void *)__p, __FILE__, __LINE__, __CURRENT_FUNC__)

void *slurm_xmalloc(size_t, const char *, int, const char *);
void *slurm_try_xmalloc(size_t , const char *, int , const char *);
void slurm_xfree(void **, const char *, int, const char *);
void *slurm_xrealloc(void **, size_t, const char *, int, const char *);
int  slurm_try_xrealloc(void **, size_t, const char *, int, const char *);
int  slurm_xsize(void *, const char *, int, const char *);

#define XMALLOC_MAGIC 0x42

#endif /* !_XMALLOC_H */
