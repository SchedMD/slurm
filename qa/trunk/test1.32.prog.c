/*****************************************************************************\
 *  prog1.32.prog.c - Simple signal catching test program for SLURM regression 
 *  test1.32. Report caught signals. Exit after SIGUSR1 and SIGUSR2 received.
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Moe Jette <jette1@llnl.gov>
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
#include <signal.h>
#include <stdio.h>

int wait_sigusr1 = 1, wait_sigusr2 = 1;

void sig_handler(int sig)
{
	switch (sig)
	{
		case SIGUSR1:
			printf("Received SIGUSR1\n");
			fflush(NULL);
			wait_sigusr1 = 0;
			break;
		case SIGUSR2:
			printf("Received SIGUSR2\n");
			fflush(NULL);
			wait_sigusr2 = 0;
			break;
		default:
			printf("Received signal %d\n", sig);
			fflush(NULL);
	}
}

main (int argc, char **argv) 
{
	signal(SIGUSR1, sig_handler);
	signal(SIGUSR2, sig_handler);

	printf("WAITING\n");
	fflush(NULL);

	while (wait_sigusr1 || wait_sigusr2)
		sleep(1);

	exit(0);
}
