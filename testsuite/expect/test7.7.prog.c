/*****************************************************************************\
 *  test7.7.prog.c - Test of sched/wiki plugin
 *****************************************************************************
 *  Copyright (C) 2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  UCRL-CODE-217948.
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
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include <stdio.h>
#include <stdlib.h>

#include "./test7.7.crypto.c"

int main(int argc, char * argv[])
{
	int auth_key, e_port, job_id, sched_port;

	if (argc < 4) {
		printf("Usage: %s, auth_key e_port job_id sched_port\n", 
			argv[0]);
		exit(1);
	}

	auth_key   = atoi(argv[1]);
	e_port     = atoi(argv[2]);
	job_id     = atoi(argv[3]);
	sched_port = atoi(argv[4]);
	printf("auth_key=%d e_port=%d job_id=%d sched_port=%d\n", 
		auth_key, e_port, job_id, sched_port);

	printf("SUCCESS\n");
	exit(0);
}

