
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include <src/api/slurm.h>
#include <testsuite/dejagnu.h>

/* main is used here for testing purposes only */
int 
main (int argc, char *argv[]) 
{
	static time_t last_update_time = (time_t) NULL;
	int error_code;
	job_info_msg_t * job_info_msg_ptr = NULL;

	error_code = slurm_load_jobs (last_update_time, &job_info_msg_ptr);
	if (error_code) {
		printf ("slurm_load_jobs error %d\n", errno);
		return (error_code);
	}

	slurm_print_job_info_msg ( stdout, job_info_msg_ptr ) ;

	slurm_free_job_info_msg ( job_info_msg_ptr ) ;
	return (0);
}
