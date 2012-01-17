#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>

int main (int ac, char **av)
{
	char hostname[1024];
	int i, rc = 0;
	struct sigaction act;

	if (gethostname (hostname, sizeof (hostname)) < 0) {
		fprintf (stderr, "Failed to get hostname on this node\n");
		strcpy (hostname, "Unknown");
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
