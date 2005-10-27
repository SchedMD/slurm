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

#include "src/common/xmalloc.h"

#include "src/slurmd/slurmd.h"
#include "src/slurmd/mgr.h"
#include "src/slurmd/step_msg_api.h"

static int sock_connect(const char *name);


int
main(int argc, char **argv)
{
	step_loc_t step;

	if (argc != 5) {
		fprintf(stderr, "Wrong number of arguments\n");
		exit(1);
	}

	step.directory = argv[1];
	step.nodename = argv[2];
	step.jobid = atoi(argv[3]);
	step.stepid = atoi(argv[4]);
	printf("Status is %d\n", step_request_status(step));
}

