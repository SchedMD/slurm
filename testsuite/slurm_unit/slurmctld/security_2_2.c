#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>

/* This functions are not defined in slurm/slurm.h for external use.
 * They are used internal security testing only. */
extern char *slurm_get_auth_type(void);
extern int slurm_set_auth_type(char *auth_type);

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

	printf("Changing command's authtype from %s to ",
		slurm_get_auth_type());
	slurm_set_auth_type("auth/dummy"); 
	printf("%s\n",slurm_get_auth_type());

	error_code = slurm_allocate_resources_and_run ( &job_mesg , 
							&run_resp_msg ); 
	if (error_code == SLURM_SUCCESS) {
		fprintf (stderr, "ERROR: The allocate succeeded\n");
		exit(1);
	} else {
		printf ("SUCCESS!\n");
		printf ("The allocate request was rejected as expected.\n");
		printf ("Check SlurmctldLog for an error message.\n");
		printf ("Error returned from API: %s\n", 
			slurm_strerror(slurm_get_errno()));
		exit(0);
	}
}
