

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#include <src/api/slurm.h>
#include <src/common/slurm_protocol_api.h>

#include "get_resp.h"
#define DEBUG_MODULE
/* report results of successful job allocation */
void report_results(resource_allocation_response_msg_t* resp_msg);

int
main( int argc, char* argv[])
{
	job_step_create_request_msg_t request = {   5, 5, 4,4 , 0, "" }; 
	resource_allocation_response_msg_t* resp_msg ;

	slurm_msg_t request_msg ;
	slurm_msg_t response_msg;

	request_msg.msg_type = REQUEST_JOB_STEP_CREATE;
	request_msg.data = &request;

	request.job_id = get_int_resp( "job_id", 1 );
	request.user_id = get_int_resp( "user_id", 1500 ); 
	request.node_count = get_int_resp( "node_count", 10 ); 
	request.cpu_count = get_int_resp( "cpu_count", 10 ); 
	request.relative = get_int_resp( "relative", 0 ); 
	request.node_list = get_string_resp( "node_list", NULL );
	 
	
	/*create job step */
	slurm_send_recv_controller_msg ( &request_msg , &response_msg);	


	if ( response_msg.msg_type != RESPONSE_JOB_STEP_CREATE )
	{
		printf("job_step_create failed\n");
	}
	else 
	{
		job_step_create_response_msg_t* msg = (job_step_create_response_msg_t *) response_msg.data ;
		printf("job_step_id = %u\n ", msg->	job_step_id );
		printf("node_list = %s\n", msg->node_list );
		printf("credentials:\n\tjob_id = %u\n\tuser_id = %u\n\tnode_list = %s\n\texpiration_time = %lu\n\tsignature = %u\n\n", 
					msg->credentials->job_id, 
					msg->credentials->user_id, 
					msg->credentials->node_list, 
					msg->credentials->expiration_time, 
					msg->credentials->signature);
#ifdef HAVE_LIBELAN3
    /* print the elan stuff */
#endif


	}

	slurm_free_resource_allocation_response_msg ( resp_msg );
	return SLURM_SUCCESS ;
}


void
report_results(resource_allocation_response_msg_t* resp_msg)
{
	int i;

	printf ("allocate nodes %s to job %u\n", resp_msg->node_list, resp_msg->job_id);
	if (resp_msg->num_cpu_groups > 0) {
		printf ("processor counts: ");
		for (i=0; i<resp_msg->num_cpu_groups; i++) {
			if (i > 0)
				printf(", ");
			printf ("%u(x%u)", resp_msg->cpus_per_node[i], resp_msg->cpu_count_reps[i]);
		}
		printf ("\n");
	}
}
