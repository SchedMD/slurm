

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

#include <src/common/slurm_protocol_api.h>

#define DEBUG_MODULE
int
main( int argc, char* argv[])
{

	job_step_create_request_msg_t request = {   5, 5, 1, "lx[1-10]" }; 
	slurm_msg_t request_msg ;
	slurm_msg_t response_msg;

	request_msg.msg_type = REQUEST_JOB_STEP_CREATE;
	request_msg.data = &request;

	slurm_send_recv_controller_msg ( &request_msg , &response_msg);	


	if ( response_msg.msg_type != RESPONSE_JOB_STEP_CREATE )
	{
		printf("DAMN\n");
	}
	else 
	{
		job_step_create_response_msg_t* msg = (job_step_create_response_msg_t *) response_msg.data ;
		printf("job_step_id = %u\n ", msg->	job_step_id );
		printf("node_list = %s\n", msg->node_list );
		printf("credentials:\n\tjob_id = %u\n\tuser_id = %u\n\tnode_list = %s\n\texperation_time = %lu\n\tsignature = %u\n\n", 
					msg->credentials->job_id, 
					msg->credentials->user_id, 
					msg->credentials->node_list, 
					msg->credentials->experation_time, 
					msg->credentials->signature);
#ifdef HAVE_LIBELAN3
    /* print the elan stuff */
#endif


	}
	return SLURM_SUCCESS ;
}
