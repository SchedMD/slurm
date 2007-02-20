/* Slurm stream client test, read stdin until "quit" is entered, anything else is sent
 * out to the specified SLURM port, anything received on that port is printed */
#include <src/common/log.h>
#include <src/common/slurm_protocol_api.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>

extern int errno;

void* read2stdout_thread( void* arg )
{
	unsigned int buffer_len = 1024*1024 ;
	int bytes_read ;
	char buffer[ buffer_len ] ;
	slurm_fd *fd = (slurm_fd *) arg ;

	while(1)
	{
		if ( ( bytes_read = slurm_read_stream( *fd, buffer, buffer_len ) ) < 0 )
			break;
		buffer[ bytes_read ] = '\0' ;
		printf( "%s", buffer );
	}
	return NULL;
}

void stdout2socket_loop( slurm_fd fd )
{
	unsigned int buffer_len = 1024*1024 ;
	int bytes_read, rc;
	char buffer[ buffer_len ] ;

	while (1)
	{
		if( (bytes_read = read( STDIN_FILENO, buffer, buffer_len ) ) < 0 )
		{
			printf("read error\n");
			break;
		}

		if ( strncmp( "quit", buffer, 4 ) == 0 )
			break;

		if ( (rc = slurm_write_stream( fd, buffer, bytes_read )) < 0 )
		{
			printf("client: could not sent to slurm_socket\n" );
			break;
		}
	}
}

int main ( int argc , char * argv[] )
{
	log_options_t log_opts = { 1, LOG_LEVEL_DEBUG3,  LOG_LEVEL_INFO, LOG_LEVEL_QUIET } ;
	slurm_fd worker_socket ; /* declare file descriptors */
	slurm_addr worker_address ; /* declare address structures */
	pthread_t read_pth ;
	int16_t port = 0;

	if (argc > 1)
		port = atoi( argv[1] ) ;

	if ((argc < 2) || (port < 1)) {
		printf("Usage: %s <port_number>\n", argv[0] );
		exit( 1 );
	}

	/* init address sturctures */
	log_init(argv[0], log_opts, SYSLOG_FACILITY_DAEMON, NULL);
	slurm_set_addr_uint ( & worker_address , port , SLURM_INADDR_ANY) ;

	/* connect socket */
	if ((worker_socket = slurm_open_stream ( & worker_address )) == -1)
	{
		printf("Could not open slurm stream errno=%d\n", errno );
		exit( errno );
	}

	/* create a reading pthread */
	if ( pthread_create( &read_pth, NULL, &read2stdout_thread, (void*) &worker_socket ) ) 	
	{
		printf("Could not create read_thread: error=%d\n", errno );
		exit( errno );
	}

	/* read from stdin */
	stdout2socket_loop( worker_socket );

	slurm_close_stream ( worker_socket ) ;
	return 0 ;
}
