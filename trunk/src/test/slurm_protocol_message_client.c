#include <netinet/in.h>
#include <src/common/slurm_protocol_api.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int32_t main ( int32_t argc , char * argv[] )
{
	/* declare file descriptors */
	slurm_fd worker_socket ;

	/* declare address structures */
	slurm_addr worker_address ;
	slurm_addr peer_address ;

	slurm_message_type_t message_type;

	uint32_t buffer_len = 1024 ;
	char buf_temp [ buffer_len ] ;
	char * buffer = buf_temp ;
	char * test_send = "This is a test of simple socket communication" ;
	uint32_t test_send_len = strlen ( test_send ) ;
	uint32_t length_io ;
		
	/* init address sturctures */
	set_slurm_addr_hton ( & worker_address , 7001 , 0x7f000001 ) ;
	/* open and listen on socket */
	worker_socket = slurm_init_message_engine ( & worker_address ) ;

	message_type = 1 ;
	set_slurm_addr_hton ( & peer_address , 7000 , 0x7f000001 ) ;
	length_io = slurm_send_node_buffer ( worker_socket , & peer_address, message_type , test_send , test_send_len ) ;
	printf ( "Bytes Sent %i\n", length_io ) ;
	
	length_io = slurm_receive_buffer ( worker_socket , & peer_address, & message_type ,  buffer , buffer_len ) ;
	printf ( "Bytes Recieved %i\n", length_io ) ;

	slurm_shutdown_message_engine ( worker_socket ) ;

	return 0 ;
}
