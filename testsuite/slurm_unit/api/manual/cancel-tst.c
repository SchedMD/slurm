#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include <slurm/slurm.h>
#include "testsuite/dejagnu.h"

/* this program takes as and arguments a list of jobids to cancel
 */

int 
main (int argc, char *argv[]) 
{
	int error_code = 0, i;

	if (argc < 2) {
		printf ("Usage: %s job_id\n", argv[0]);
		exit (1);
	}

	for (i=1; i<argc; i++) {
		error_code = slurm_kill_job ((uint32_t) atoi(argv[i]), 
							     SIGKILL, 0);
		if (error_code) {
			char msg[64];
			sprintf(msg, "slurm_kill_job(%.12s)",argv[i]);
			slurm_perror (msg);
		}
	}

	return (error_code);
}


