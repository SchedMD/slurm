#include <src/common/slurm_protocol_api.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <error.h>

extern int errno;

void* read2stdout_thread( void* arg )
{
	uint32_t buffer_len = 1024*1024 ;
	int32_t bytes_read ;
	char buffer[ buffer_len ] ;
	slurm_fd fd = (slurm_fd) arg ;

	while(1)
	{
		if ( ( bytes_read = slurm_read_stream( fd, buffer, buffer_len ) ) < 0 )
			break;
		buffer[ bytes_read ] = '\0' ;
		printf( "%s", buffer );
	}
	return NULL;
}

void stdout2socket_loop( slurm_fd fd )
{
	uint32_t buffer_len = 1024*1024 ;
	int32_t bytes_read ;
	char buffer[ buffer_len ] ;

	while (1)
	{
		if( (bytes_read = read( 0, buffer, buffer_len ) ) < 0 )
		{
			printf("read error\n");
			break;
		}

		if ( slurm_write_stream( fd, buffer, bytes_read ) < 0 )
		{
			printf("client: could not sent to slurm_socket\n" );
			break;
		}

	}
}

int32_t main ( int32_t argc , char * argv[] )
{
	slurm_fd worker_socket ; /* declare file descriptors */
	slurm_addr worker_address ; /* declare address structures */

	pthread_t read_pth ;

	int16_t port = atoi( argv[1] ) ;
		
	/* init address sturctures */
	slurm_set_addr_uint ( & worker_address , port , 0x7f000001 ) ;
	/* connect socket */
	worker_socket = slurm_open_stream ( & worker_address ) ;

	/* create a reading pthread */
	if ( pthread_create( &read_pth, NULL, &read2stdout_thread, (void*) worker_socket ) ) 	
	{
		printf("Could not create read_thread: error=%d\n", errno );
		exit( errno );
	}

	stdout2socket_loop( worker_socket );

	slurm_close_stream ( worker_socket ) ;
	return 0 ;
}
