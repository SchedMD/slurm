/*****************************************************************************\
 *  safeopen.h - safer interface to open()
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
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

#ifndef _SAFEOPEN_H
#define _SAFEOPEN_H

#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

/* safeopen flags:
 *
 * default is to create if needed, and fail if path is a soft link
 */
#define SAFEOPEN_LINK_OK	(1<<0) 	/* do not check for soft link	*/
#define SAFEOPEN_CREATE_ONLY	(1<<1)  /* create, fail if file exists	*/
#define SAFEOPEN_NOCREATE	(1<<2)	/* fail if file doesn't exist	*/

/* open a file for read, write, or append
 * perform some simple sanity checks on file and return stream pointer
 */
FILE *safeopen(const char *path, const char *mode, int flags);

#endif /* _SAFEOPEN_H */
