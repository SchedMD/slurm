/* 
 * update_config.c - request that slurmctld update its configuration
 *
 * author: moe jette, jette@llnl.gov
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <src/api/slurm.h>
#include <src/common/slurm_protocol_api.h>

#if DEBUG_MODULE
/* main is used here for module testing purposes only */
int
main (int argc, char *argv[]) {
	int error_code;
	char part_update1[] = "PartitionName=batch State=DOWN";
	char part_update2[] = "PartitionName=batch State=UP";
	char node_update1[] = "NodeName=lx1234 State=DOWN";
	char node_update2[] = "NodeName=lx1234 State=IDLE";

	error_code = slurm_update_config (part_update1);
	if (error_code)
		printf ("error %d for part_update1\n", error_code);
	error_code = slurm_update_config (part_update2);
	if (error_code)
		printf ("error %d for part_update2\n", error_code);
	error_code = slurm_update_config (node_update1);
	if (error_code)
		printf ("error %d for node_update1\n", error_code);
	error_code = slurm_update_config (node_update2);
	if (error_code)
		printf ("error %d for node_update2\n", error_code);

	exit (error_code);
}
#endif


/* 
 * update_config - _ request that slurmctld update its configuration per request
 * input: a line containing configuration information per the configuration file format
 * output: returns 0 on success, errno otherwise
 */
int
slurm_update_config (char *spec) {
        int msg_size ;
        int rc ;
        slurm_fd sockfd ;
        slurm_msg_t request_msg ;
        slurm_msg_t response_msg ;
	return_code_msg_t * rc_msg ;

        /* init message connection for message communication with controller */

        if ( ( sockfd = slurm_open_controller_conn ( SLURM_PORT ) ) == SLURM_SOCKET_ERROR )
                return SLURM_SOCKET_ERROR ;


        /* send request message */
        /* pack32 ( update_time , &buf_ptr , &buffer_size ); */
        /*request_msg . msg_type = REQUEST_UPDATE_CONFIG_INFO ;*/
        request_msg . data = NULL ; 

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
                        rc_msg = ( return_code_msg_t * ) response_msg . data ;
                        return rc_msg->return_code ;
                        break ;
                default:
                        return SLURM_UNEXPECTED_MSG_ERROR ;
                        break ;
        }

        return SLURM_SUCCESS ;
}
