#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>

/* Attempt to run a job with the incorrect user id and confirm an error */
int 
main (int argc, char *argv[])
{
	int error_code;
	job_desc_msg_t job_mesg;
	resource_allocation_and_run_response_msg_t* run_resp_msg ;

	slurm_init_job_desc_msg( &job_mesg );
	job_mesg. user_id	= getuid() + 1;
	job_mesg. min_nodes	= 1;

	error_code = slurm_allocate_resources_and_run ( &job_mesg , 
							&run_resp_msg ); 
	if (error_code == SLURM_SUCCESS) {
		fprintf (stderr, "ERROR: The allocate succeeded\n");
		exit(1);
	} else if ((error_code = slurm_get_errno()) != ESLURM_USER_ID_MISSING) {
		fprintf (stderr, 
			 "ERROR: Wrong error code received: %s instead of %s\n",
			 slurm_strerror(error_code), "ESLURM_USER_ID_MISSING");
		exit(1);
	} else {
		printf ("SUCCESS!\n");
		printf ("The allocate request was rejected as expected.\n");
		printf ("Check SlurmctldLog for an error message.\n");
		exit(0);
	}
}

