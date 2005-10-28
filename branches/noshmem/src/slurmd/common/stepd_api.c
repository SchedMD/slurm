/*****************************************************************************\
 *  src/slurmd/common/stepd_api.c - slurmstepd message API
 *  $Id: $
 *****************************************************************************
 *  Copyright (C) 2005 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Christopher Morrone <morrone2@llnl.gov>
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

#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>

#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmd/common/stepd_api.h"

static int
step_connect(step_loc_t step)
{
	int fd;
	int len;
	struct sockaddr_un addr;
	char *name = NULL;

	if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
		return -1;

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	xstrfmtcat(name, "%s/%s_%u.%u", step.directory, step.nodename, 
		   step.jobid, step.stepid);
	strcpy(addr.sun_path, name);
	len = strlen(addr.sun_path) + sizeof(addr.sun_family);

	if (connect(fd, (struct sockaddr *) &addr, len) < 0) {
		printf("connect to server socket %s FAILED!\n", name);
		xfree(name);
		exit(2);
	}

	xfree(name);
	return fd;
}

int
step_request_status(step_loc_t step)
{
	int req	= REQUEST_STATUS;
	int fd;
	int status = 0;

	fd = step_connect(step);

	write(fd, &req, sizeof(int));
	read(fd, &status, sizeof(int));

	return status;
}

int
step_request_attach(step_loc_t step, slurm_addr *ioaddr,
		    slurm_addr *respaddr, char *cred_signature)
{
	int req = REQUEST_ATTACH;
	int fd;

	fd = step_connect(step);

	write(fd, &req, sizeof(int));
	write(fd, ioaddr, sizeof(slurm_addr));
	write(fd, respaddr, sizeof(slurm_addr));
	write(fd, cred_signature, SLURM_CRED_SIGLEN);
}
