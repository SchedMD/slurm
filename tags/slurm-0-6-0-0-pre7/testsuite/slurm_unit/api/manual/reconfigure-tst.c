#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include <slurm/slurm.h>
#include "testsuite/dejagnu.h"

/* main is used here for module testing purposes only */
int 
main (int argc, char *argv[]) {
	int i, count, error_code;

	if (argc < 2)
		count = 1;
	else
		count = atoi (argv[1]);

	for (i = 0; i < count; i++) {
		error_code = slurm_reconfigure ();
		if (error_code != 0) {
			slurm_perror ("slurm_reconfigure");
			return (1);
		}
	}
	return (0);
}
