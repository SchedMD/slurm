/* 
 * allocate.c - allocate nodes for a job with supplied contraints
 * see slurm.h for documentation on external functions and data structures
 *
 * author: moe jette, jette@llnl.gov
 */

#define DEBUG_SYSTEM 1

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <errno.h>
#include <stdio.h>

#include <src/api/slurm.h>
#include <src/common/slurm_protocol_api.h>

#if DEBUG_MODULE
/* main is used here for testing purposes only */
int 
main (int argc, char *argv[])
{
	int error_code;
	char *node_list;
	uint32_t job_id;

	error_code = slurm_allocate
		("User=1500 JobName=job01 TotalNodes=400 TotalProcs=1000 ReqNodes=lx[3000-3003] Partition=batch MinRealMemory=1024 MinTmpDisk=2034 Groups=students,employee MinProcs=4 Contiguous=YES Key=1234 Immediate",
		 &node_list, &job_id);
	if (error_code)
		printf ("allocate error %d\n", error_code);
	else {
		printf ("allocate nodes %s to job %u\n", node_list, job_id);
		free (node_list);
	}

	while (1) {
		error_code = slurm_allocate
			("User=1500 JobName=more TotalProcs=4000 Partition=batch Key=1234 Immediate",
			 &node_list, &job_id);
		if (error_code) {
			printf ("allocate error %d\n", error_code);
			break;
		}
		else {
			printf ("allocate nodes %s to job %u\n", node_list, job_id);
			free (node_list);
		}
	}

	while (1) {
		error_code = slurm_allocate
			("User=1500 JobName=more TotalProcs=40 Partition=batch Key=1234 Immediate",
			 &node_list, &job_id);
		if (error_code) {
			printf ("allocate error %d\n", error_code);
			break;
		}
		else {
			printf ("allocate nodes %s to job %u\n", node_list, job_id);
			free (node_list);
		}
	}

	exit (0);
}
#endif

/* slurm_submit_job - load the supplied node information buffer if changed */
int
slurm_submit_batch_job (job_desc_msg_t * job_desc_msg )
{
        int msg_size ;
        int rc ;
        slurm_fd sockfd ;
        slurm_msg_t request_msg ;
        slurm_msg_t response_msg ;
        return_code_msg_t * slurm_rc_msg ;

        /* init message connection for message communication with controller */
        if ( ( sockfd = slurm_open_controller_conn ( SLURM_PORT ) ) == SLURM_SOCKET_ERROR )
                return SLURM_SOCKET_ERROR ;


        /* send request message */
        request_msg . msg_type = REQUEST_RESOURCE_ALLOCATION ;
        request_msg . data = job_desc_msg ; 
        if ( ( rc = slurm_send_controller_msg ( sockfd , & request_msg ) ) == SLURM_SOCKET_ERROR )
                return SLURM_SOCKET_ERROR ;

        /* receive message */
        if ( ( msg_size = slurm_receive_msg ( sockfd , & response_msg ) ) == SLURM_SOCKET_ERROR )
                return SLURM_SOCKET_ERROR ;
        /* shutdown message connection */
        if ( ( rc = slurm_shutdown_msg_conn ( sockfd ) ) == SLURM_SOCKET_ERROR )
                return SLURM_SOCKET_ERROR ;

        switch ( response_msg . msg_type )
        {
                case RESPONSE_SLURM_RC:
                        slurm_rc_msg = ( return_code_msg_t * ) response_msg . data ;
                        break ;
                default:
                        return SLURM_UNEXPECTED_MSG_ERROR ;
                        break ;
        }

        return SLURM_SUCCESS ;
}

