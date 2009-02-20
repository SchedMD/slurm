/*****************************************************************************\
 *  test3.7.prog.c - Test of slurm job suspend/resume.
 *
 *  Counts down, printing counter, with sleep(1) with each interation.
 *  Prints "JobSuspended" if execution is suspended and then resumed.
 *****************************************************************************
 *  Copyright (C) 2005 The Regents of the University of California.
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
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	int i, start;
	time_t last=time(NULL), now;

	if (argc > 1)
		start = atoi(argv[1]);
	else
		start = 30;

	for (i=start; i>0; i--) {
		fprintf(stdout, "%d\n", i);
		fflush(stdout);
		sleep(1);
		now = time(NULL);
		if (difftime(now, last) > 2)
			fprintf(stdout, "JobSuspended\n");
		last = now;
	}

	fprintf(stdout, "AllDone\n");
	return (0);
}
