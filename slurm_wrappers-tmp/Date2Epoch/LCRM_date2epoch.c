#include <stdio.h>
#include <stdlib.h>
#include <time.h>

main (int argc, char *argv[]) {
	time_t t;
	int ignored[1];

	if (argc != 2) {
		printf("Usage: %s <LCRM date string>\n", argv[0]);
		exit(1);
	}

	t = datetoi(argv[1], ignored);
	printf("%d\n", t);

	exit(0);
}
