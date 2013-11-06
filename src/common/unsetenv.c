/*****************************************************************************\
 *  src/common/unsetenv.c - Kludge for unsetenv on AIX
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>.
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "src/common/log.h"

extern int unsetenv (const char *name)
{
	int len, rc;
	char *tmp;

	if (!getenv(name))	/* Nothing to clear */
		return 0;

	len = strlen(name);
	tmp = malloc(len + 3);
	if (!tmp) {
		log_oom(__FILE__, __LINE__, __CURRENT_FUNC__);
		abort();
	}
	strcpy(tmp, name);
	strcat(tmp, "=x");
	if ((rc = putenv(tmp)) != 0)
		return rc;

	/* Here's the real kludge, just clear the variable out.
	 * This does result in a memory leak. */
	tmp[0] = '\0';
	return 0;
}
