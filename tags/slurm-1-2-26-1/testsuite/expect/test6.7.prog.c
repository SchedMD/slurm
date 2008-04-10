/*****************************************************************************\
 * prog6.7.prog.c - Simple signal catching test program for Moab regression 
 *****************************************************************************
 * Copyright (C) 2002-2007 The Regents of the University of California.
 * Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 * Written by Morris Jette <jette1@llnl.gov>
\*****************************************************************************/
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

int sig_cnt = 0;

void sig_handler(int sig)
{
	switch (sig)
	{
		case SIGUSR1:
			printf("Received SIGUSR1\n");
			fflush(stdout);
			sig_cnt++;
			break;
		default:
			printf("Received unexpected signal %d\n", sig);
			fflush(stdout);
	}
}

main (int argc, char **argv) 
{
	struct sigaction act;
	time_t begin_time = time(NULL);

	act.sa_handler = sig_handler;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	if (sigaction(SIGUSR1, &act, NULL) < 0) {
		perror("sigaction");
		exit(2);
	}

	printf("WAITING\n");
	fflush(stdout);

	while (!sig_cnt) {
		sleep(1);
	}
	printf("Job ran for %d secs\n", (int) (time(NULL) - begin_time));
	exit(0);
}
