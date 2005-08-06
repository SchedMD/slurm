
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include <slurm/slurm.h>
#include "testsuite/dejagnu.h"

/* main is used here for testing purposes only */
int 
main (int argc, char *argv[]) 
{
	static time_t last_update_time = (time_t) NULL;
	int error_code;
	job_info_msg_t * job_info_msg_ptr = NULL;

	error_code = slurm_load_jobs (last_update_time, &job_info_msg_ptr, 1);
	if (error_code) {
		slurm_perror ("slurm_load_jobs");
		return (error_code);
	}

	slurm_print_job_info_msg ( stdout, job_info_msg_ptr, 1 ) ;

	slurm_free_job_info_msg ( job_info_msg_ptr ) ;
	return (0);
}
