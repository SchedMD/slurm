/*****************************************************************************\
 *  proc1.29.proc.c - Simple user limit set program for SLURM regression 
 *  test1.29. Get the core, fsize, nofile, and nproc limits and print their 
 *  values in the same format as SLURM environment variables.
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <unistd.h>

main (int argc, char **argv) 
{
	struct rlimit u_limit;
	int exit_code = 0;

	(void) getrlimit(RLIMIT_CORE, &u_limit);
	printf("USER_CORE=%d\n", (int)u_limit.rlim_cur);
	(void) getrlimit(RLIMIT_FSIZE, &u_limit);
	printf("USER_FSIZE=%d\n", (int)u_limit.rlim_cur);
	(void) getrlimit(RLIMIT_NOFILE, &u_limit);
	printf("USER_NOFILE=%d\n", (int)u_limit.rlim_cur);
#ifdef RLIMIT_NPROC
	(void) getrlimit(RLIMIT_NPROC, &u_limit);
	printf("USER_NPROC=%d\n", (int)u_limit.rlim_cur);
#else
	printf("USER_NPROC unsupported\n");
#endif
#ifdef RLIMIT_STACK
        (void) getrlimit(RLIMIT_STACK, &u_limit);
        printf("USER_STACK=%d\n", (int)u_limit.rlim_cur);
#else
        printf("USER_STACK unsupported\n");
#endif

	exit(exit_code);
}
