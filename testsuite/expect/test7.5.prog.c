/*****************************************************************************\
 *  prog7.5.prog.c - Simple signal catching test program for SLURM regression
 *  test7.5. Report caught signals. Block SIGTERM.
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
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


int sigterm_cnt = 0;

void sig_handler(int sig)
{
	switch (sig)
	{
		case SIGTERM:
			printf("Received SIGTERM\n");
			fflush(stdout);
			sigterm_cnt++;
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
	if (sigaction(SIGTERM, &act, NULL) < 0) {
		perror("setting SIGTERM handler");
		exit(2);
	}

	printf("WAITING\n");
	fflush(stdout);

	sleep(160);

	printf("FINI: term:%d\n", sigterm_cnt);
	exit(0);
}
