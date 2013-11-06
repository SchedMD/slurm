/*****************************************************************************\
 *  prog1.32.prog.c - Simple signal catching test program for SLURM regression
 *  test1.32. Report caught signals. Exit after SIGUSR1 and SIGUSR2 received.
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>


int sigusr1_cnt = 0, sigusr2_cnt = 0;

void sig_handler(int sig)
{
	switch (sig)
	{
		case SIGUSR1:
			printf("Received SIGUSR1\n");
			fflush(stdout);
			sigusr1_cnt++;
			break;
		case SIGUSR2:
			printf("Received SIGUSR2\n");
			fflush(stdout);
			sigusr2_cnt++;
			break;
		default:
			printf("Received signal %d\n", sig);
			fflush(stdout);
	}
}

main (int argc, char **argv)
{
	struct sigaction act;

	act.sa_handler = sig_handler;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	if (sigaction(SIGUSR1, &act, NULL) < 0) {
		perror("setting SIGUSR1 handler");
		exit(2);
	}
	if (sigaction(SIGUSR2, &act, NULL) < 0) {
		perror("setting SIGUSR2 handler");
		exit(2);
	}

	printf("WAITING\n");
	fflush(stdout);

	while ((sigusr1_cnt + sigusr2_cnt) < 2) {
		sleep(1);
	}
	printf("FINI: sig1:%d sig2:%d\n", sigusr1_cnt, sigusr2_cnt);
	exit(0);
}
