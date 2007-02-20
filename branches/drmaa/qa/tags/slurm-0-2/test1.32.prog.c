/**********************************************************************\
 * Simple signal catching test program for SLURM regression test1.32
 * Report caught signals. Exit after SIGUSR1 and SIGUSR2 received
\**********************************************************************/
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
