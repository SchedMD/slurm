#include <signal.h>
#include <stdio.h>
#include <string.h>

static const char *prefix = "TEST_PROCESS";

void signal_handler(int sig)
{
	static int logged = 0;

	if (logged == 0) {
		logged = 1;
		printf("%s: Signal received: %s\n", prefix, strsignal(sig));
		fflush(stdout);
	}
}

int main()
{
	struct sigaction act = {
		.sa_handler = signal_handler,
		.sa_flags = SA_NODEFER,
	};
	sigemptyset(&act.sa_mask);
	sigaction(SIGINT, &act, 0);

	printf("%s: Signal handler ready.\n", prefix);
	fflush(stdout);
	while (1) {
		sleep(1);
	}
}
