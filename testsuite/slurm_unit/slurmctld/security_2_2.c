#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>

typedef void * Buf;

/* Attempt to run a job without a credential */
int 
main (int argc, char *argv[])
{
	int error_code;
	job_desc_msg_t job_mesg;
	resource_allocation_and_run_response_msg_t* run_resp_msg ;

	slurm_init_job_desc_msg( &job_mesg );
	job_mesg. user_id	= getuid();
	job_mesg. min_nodes	= 1;

	error_code = slurm_allocate_resources_and_run ( &job_mesg , 
							&run_resp_msg ); 
	if (error_code == SLURM_SUCCESS) {
		fprintf (stderr, "ERROR: The allocate succeeded\n");
		exit(1);
	} else {
		printf ("SUCCESS!\n");
		printf ("The allocate request was rejected as expected.\n");
		printf ("Check SlurmctldLog for an error message.\n");
		exit(0);
	}
}

/* This version supersedes that in libslurm, so a credential is not packed */
int g_slurm_auth_pack(void * auth_cred, Buf buffer)
{
	return 0;
}
