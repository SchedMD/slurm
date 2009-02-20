/*****************************************************************************\
 *  safeopen.c - safer interface to open()
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
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

#if HAVE_UNISTD_H
#  include <unistd.h>
#endif
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "src/common/safeopen.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

FILE * safeopen(const char *path, const char *mode, int flags)
{
	int fd;
	int oflags;
	struct stat fb1, fb2;

	if(mode[0] == 'w') {
		oflags = O_WRONLY;
	} else if (mode[0] == 'a') {
		oflags = O_CREAT | O_WRONLY | O_APPEND;
	} else
		oflags = O_RDONLY;

	oflags |= !(flags & SAFEOPEN_NOCREATE)   ? O_CREAT : 0;
	oflags |= (flags & SAFEOPEN_CREATE_ONLY) ? O_EXCL  : 0;

	if ((fd = open(path, oflags, S_IRUSR|S_IWUSR)) < 0)
		return NULL;

	if (!(flags & SAFEOPEN_LINK_OK)) {
		lstat(path, &fb1);
		fstat(fd,   &fb2);

		if (fb2.st_ino != fb1.st_ino) {
			fprintf(stderr, "safeopen(): refusing to open `%s', " 
					"which is a soft link\n", path);
			close(fd);
			return NULL;
		}
	}

	return fdopen(fd, mode);

}

