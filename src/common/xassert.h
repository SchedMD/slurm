/*****************************************************************************\
 *  xassert.h: assert type macro with configurable handling
 *             If NDEBUG is defined, do nothing.
 *             If not, and expression is zero, log an error message and abort.
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette@llnl.gov>.
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

#ifndef _XASSERT_H
#define _XASSERT_H	1

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include "macros.h"

#ifdef NDEBUG

#  define xassert(expr)	((void) (0))

#else /* !NDEBUG */

#  define xassert(__ex)  _STMT_START { \
     (__ex) ? ((void)0) : \
     __xassert_failed(__STRING(__ex), __FILE__,  __LINE__, __CURRENT_FUNC__);\
     } _STMT_END

/*  This prints the assertion failed message to the slurm log facility
 *  (see log.h) and aborts the calling program
 *  (messages go to stderr if log is not initialized)
 */
extern void __xassert_failed(char *, const char *, int, const char *)
	    __NORETURN_ATTR;

#endif /* NDEBUG. */

#endif /* !__XASSERT_H */
