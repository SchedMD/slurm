#include <src/common/slurm_protocol_api.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

int main ( int argc , char * argv[] )
{
	/* declare file descriptors */
	slurm_fd worker_socket ;

	/* declare address structures */
	slurm_addr worker_address ;

	unsigned int buffer_len = 1024 ;
	char buf_temp [ buffer_len ] ;
	char * buffer = buf_temp ;
	char * test_send = "This is a test of simple socket communication" ;
	unsigned int test_send_len = strlen ( test_send ) ;
	unsigned int length_io ;
		
	/* init address sturctures */
	slurm_set_addr_uint ( & worker_address , 7000 , SLURM_INADDR_ANY ) ;
	/* connect socket */
	worker_socket = slurm_open_stream ( & worker_address ) ;

	length_io = slurm_read_stream ( worker_socket , buffer , buffer_len ) ;
	printf ( "Bytes Recieved %i\n", length_io ) ;

	length_io = slurm_write_stream ( worker_socket , test_send , test_send_len ) ;
	printf ( "Bytes Sent %i\n", length_io ) ;

	slurm_close_stream ( worker_socket ) ;

	return 0 ;
}
