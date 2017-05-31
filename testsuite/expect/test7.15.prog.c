#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

int main (int ac, char **av)
{
	char *hostname = NULL;
	int i, rc = 0;
	struct sigaction act;

	if (!(hostname = getenv("SLURMD_NODENAME"))) {
		fprintf (stderr, "Failed to get hostname on this node\n");
		hostname = "Unknown";
	}

	for (i = 1; i < SIGRTMAX; i++) {
		sigaction (i, NULL, &act);
		if (act.sa_handler == SIG_IGN) {
			fprintf (stderr, "%s: Signal %d is ignored!\n",
				 hostname, i);
			rc = 1;
		} else if (act.sa_handler != SIG_DFL) {
			fprintf (stderr,
				 "%s: Signal %d has handler function!\n",
				 hostname, i);
			rc = 1;
		}
	}
	return (rc);
}
