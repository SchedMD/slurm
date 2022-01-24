#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <stdbool.h>

volatile bool  finish = false;
volatile bool  step   = false;

pid_t pid;

void sig_handler(int signo)
{
	if (pid)
		if (step)
			printf("Signaled: parent_step\n");
		else
			printf("Signaled: parent_command\n");
	else
		if (step)
			printf("Signaled: child_step\n");
		else
			printf("Signaled: child_command\n");
	finish = true;
}

int main(int argc, char *argv[])
{
	int i=0;

	if (argc>1)
		step = true;

	signal(SIGUSR1, sig_handler);

	pid = fork();

	if (pid)
		if (step)
			printf("Started: parent_step\n");
		else
			printf("Started: parent_command\n");
	else
		if (step)
			printf("Started: child_step\n");
		else
			printf("Started: child_command\n");

	while (!finish && i<10) {
		fflush(stdout);
		sleep(1);
		i++;
	}

	if (pid)
		wait(NULL);

	return 0;
}
