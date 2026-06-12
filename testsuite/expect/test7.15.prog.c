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
		/* NOTE: If the slurmd is started from a terminal such as
		 * rxvt-unicode or anything like it (aterm) it will ignore
		 * SIGFPE (8) thus failing this test.
		 */
		if (act.sa_handler == SIG_IGN) {
			fprintf (stderr, "%s: Signal %d is ignored!\n",
				 hostname, i);
			if (i == SIGFPE)
				fprintf (stderr, "%s: Terminals like rxvt-unicode/aterm will ignore SIGFPE.  Rerun this test where the slurmd isn't started from that terminal if you get this message.\n",
					 hostname);
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
