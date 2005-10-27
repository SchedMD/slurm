/*****************************************************************************\
 *  src/slurmd/test_slurmd_step.c - Test the slurmd_step message API.
 *  $Id: $
 *****************************************************************************
 *  Copyright (C) 2005 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Christopher Morrone <morrone2@llnl.gov>.
 *  UCRL-CODE-2002-040.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>

#include "src/slurmd/slurmd.h"
#include "src/slurmd/mgr.h"
#include "src/common/xmalloc.h"

static int sock_connect(const char *name);


int
main(int argc, char **argv)
{
	if (argc != 2) {
		printf("Need domain socket path as sole parameter\n");
		exit(1);
	}

	printf("argv[1] = %s\n", argv[1]);
	sock_connect(argv[1]);
}


static int
sock_connect(const char *name)
{
	int fd;
	int len;
	struct sockaddr_un addr;

	if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
		return -1;

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strcpy(addr.sun_path, name);
	len = strlen(addr.sun_path) + sizeof(addr.sun_family);

	if (connect(fd, (struct sockaddr *) &addr, len) < 0) {
		printf("connect to server socket %s FAILED!\n", name);
		exit(2);
	}

	return fd;
}

