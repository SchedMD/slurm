/* 
 * partition_info.c - get the partition information of slurm
 * see slurm.h for documentation on external functions and data structures
 *
 * author: moe jette, jette@llnl.gov
 */

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
#include <arpa/inet.h>
#include <unistd.h>

#include "pack.h"

#include "slurm.h"
#include <src/common/slurm_errno.h>
#include <src/common/slurm_protocol_api.h>
#define SLURM_PORT 7000 

/* prototypes */

#if DEBUG_MODULE
/* main is used here for module testing purposes only */
int
main (int argc, char *argv[]) 
{
	static time_t last_update_time = (time_t) NULL;
	int error_code;
	struct build_table  *build_table_ptr = NULL;

	error_code = slurm_load_build (last_update_time, &build_table_ptr);
	if (error_code) {
		printf ("slurm_load_build error %d\n", error_code);
		exit (1);
	}
	
	slurm_print_build_info ( build_table_ptr );
	
	slurm_free_build_info ( build_table_ptr );

	exit (0);
}
#endif

void slurm_print_build_info ( build_info_msg_t * build_table_ptr )
{
	if ( build_table_ptr == NULL )
		return ;
	printf("Build updated at %lx\n", build_table_ptr->last_update);
	printf("BACKUP_INTERVAL	= %u\n", build_table_ptr->backup_interval);
	printf("BACKUP_LOCATION	= %s\n", build_table_ptr->backup_location);
	printf("BACKUP_MACHINE	= %s\n", build_table_ptr->backup_machine);
	printf("CONTROL_DAEMON	= %s\n", build_table_ptr->control_daemon);
	printf("CONTROL_MACHINE	= %s\n", build_table_ptr->control_machine);
	printf("EPILOG		= %s\n", build_table_ptr->epilog);
	printf("FAST_SCHEDULE	= %u\n", build_table_ptr->fast_schedule);
	printf("HASH_BASE	= %u\n", build_table_ptr->hash_base);
	printf("HEARTBEAT_INTERVAL	= %u\n", build_table_ptr->heartbeat_interval);
	printf("INIT_PROGRAM	= %s\n", build_table_ptr->init_program);
	printf("KILL_WAIT	= %u\n", build_table_ptr->kill_wait);
	printf("PRIORITIZE	= %s\n", build_table_ptr->prioritize);
	printf("PROLOG		= %s\n", build_table_ptr->prolog);
	printf("SERVER_DAEMON	= %s\n", build_table_ptr->server_daemon);
	printf("SERVER_TIMEOUT	= %u\n", build_table_ptr->server_timeout);
	printf("SLURM_CONF	= %s\n", build_table_ptr->slurm_conf);
	printf("TMP_FS		= %s\n", build_table_ptr->tmp_fs);
}

void slurm_free_build_info ( struct build_table * build_ptr )
{
	if ( build_ptr == NULL )
		return ;
	free ( build_ptr->backup_location ) ;
	free ( build_ptr->backup_machine ) ;
	free ( build_ptr->control_daemon ) ;
	free ( build_ptr->control_machine ) ;
	free ( build_ptr->epilog ) ;
	free ( build_ptr->init_program ) ;
	free ( build_ptr->prolog ) ;
	free ( build_ptr->server_daemon ) ;
	free ( build_ptr->slurm_conf ) ;
	free ( build_ptr->tmp_fs ) ;
	free ( build_ptr ) ;
}

int
slurm_load_build (time_t update_time, struct build_table **build_table_ptr )
{
	int msg_size ;
	int rc ;
	slurm_fd sockfd ;
	slurm_msg_t request_msg ;
	slurm_msg_t response_msg ;
        last_update_msg_t last_time_msg ; 
	
	/* init message connection for message communication with controller */
	
	if ( ( sockfd = slurm_open_controller_conn ( SLURM_PORT ) ) == SLURM_SOCKET_ERROR )
		return SLURM_SOCKET_ERROR ;	

	
	/* send request message */
	/* pack32 ( update_time , &buf_ptr , &buffer_size ); */
	last_time_msg . last_update = update_time ;
	request_msg . msg_type = REQUEST_BUILD_INFO ;
	request_msg . data = &last_time_msg ;
	
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
                case RESPONSE_BUILD_INFO:
                        *build_table_ptr = ( build_info_msg_t * ) response_msg . data ; 
        		return SLURM_SUCCESS ;
                        break ;
                case RESPONSE_SLURM_RC:
			return SLURM_NO_CHANGE_IN_DATA ;
                        break ;
                default:
			return SLURM_UNEXPECTED_MSG_ERROR ;
                        break ;
        }

        return SLURM_SUCCESS ;
}
