/*****************************************************************************\
 *  xmalloc.h - enhanced malloc routines for slurm
 *  - default: never return if errors are encountered.
 *  - attempt to report file, line, and calling function on assertion failure
 *  - use configurable slurm log facility for reporting errors
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
 *****************************************************************************
 * Description:
 *
 * void *xmalloc(size_t size);
 * void xrealloc(void *p, size_t newsize);
 * void xfree(void *p);
 * int  xsize(void *p);
 *
 * xmalloc(size) allocates size bytes and returns a pointer to the allocated
 * memory. The memory is set to zero. xmalloc() will not return unless
 * there are no errors. The memory must be freed using xfree().
 *
 * xrealloc(p, newsize) changes the size of the block pointed to by p to the
 * value of newsize. Newly allocated memory is zeroed. If p is NULL,
 * xrealloc() performs the same function as  `p = xmalloc(newsize)'. If p
 * is not NULL, it is required to have been initialized with a call to
 * [try_]xmalloc() or [try_]xrealloc().
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

#include <stdbool.h>
#include <sys/types.h>

#define xcalloc(__cnt, __sz) \
	slurm_xcalloc(__cnt, __sz, true, false, __FILE__, __LINE__, __func__)

#define try_xcalloc(__cnt, __sz) \
	slurm_xcalloc(__cnt, __sz, true, true, __FILE__, __LINE__, __func__)

#define xcalloc_nz(__cnt, __sz) \
	slurm_xcalloc(__cnt, __sz, false, false, __FILE__, __LINE__, __func__)

#define xmalloc(__sz) \
	slurm_xcalloc(1, __sz, true, false, __FILE__, __LINE__, __func__)

#define try_xmalloc(__sz) \
	slurm_xcalloc(1, __sz, true, true, __FILE__, __LINE__, __func__)

#define xmalloc_nz(__sz) \
	slurm_xcalloc(1, __sz, false, false, __FILE__, __LINE__, __func__)

#define xfree(__p) \
	slurm_xfree((void **)&(__p), __FILE__, __LINE__, __func__)

#define xrecalloc(__p, __cnt, __sz) \
        slurm_xrecalloc((void **)&(__p), __cnt, __sz, true, false, __FILE__, __LINE__, __func__)

#define xrealloc(__p, __sz) \
        slurm_xrecalloc((void **)&(__p), 1, __sz, true, false, __FILE__, __LINE__, __func__)

#define try_xrealloc(__p, __sz) \
        slurm_xrecalloc((void **)&(__p), 1, __sz, true, true, __FILE__, __LINE__, __func__)

#define xrealloc_nz(__p, __sz) \
        slurm_xrecalloc((void **)&(__p), 1, __sz, false, false, __FILE__, __LINE__, __func__)

#define xsize(__p) \
	slurm_xsize((void *)__p, __FILE__, __LINE__, __func__)

void *slurm_xcalloc(size_t, size_t, bool, bool, const char *, int, const char *);
void slurm_xfree(void **, const char *, int, const char *);
void *slurm_xrecalloc(void **, size_t, size_t, bool, bool, const char *, int, const char *);
size_t slurm_xsize(void *, const char *, int, const char *);

void xfree_ptr(void *);

#endif /* !_XMALLOC_H */
