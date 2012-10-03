#include <stdlib.h>
#include <stdio.h>

int main(int argc, char **argv)
{

	if (argc == 2) {
		sleep(strtol(argv[1], NULL, 0));
	} else {
		fprintf(stderr, "usage %s <seconds>\n", argv[0]);
	}

	return(0);
}
