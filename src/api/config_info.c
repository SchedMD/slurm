/* 
 * slurm_ctl_conf.c - get the build information of slurm
 * see slurm.h for documentation on external functions and data structures
 *
 * author: moe jette, jette@llnl.gov
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <src/common/slurm_protocol_api.h>

void slurm_print_ctl_conf ( FILE* out, slurm_ctl_conf_info_msg_t * slurm_ctl_conf_ptr )
{
	if ( slurm_ctl_conf_ptr == NULL )
		return ;
	fprintf(out, "Build updated at %lx\n", (time_t)slurm_ctl_conf_ptr->last_update);
	fprintf(out, "BACKUP_INTERVAL	= %u\n", slurm_ctl_conf_ptr->backup_interval);
	fprintf(out, "BACKUP_LOCATION	= %s\n", slurm_ctl_conf_ptr->backup_location);
	fprintf(out, "BACKUP_MACHINE	= %s\n", slurm_ctl_conf_ptr->backup_machine);
	fprintf(out, "CONTROL_DAEMON	= %s\n", slurm_ctl_conf_ptr->control_daemon);
	fprintf(out, "CONTROL_MACHINE	= %s\n", slurm_ctl_conf_ptr->control_machine);
	fprintf(out, "EPILOG		= %s\n", slurm_ctl_conf_ptr->epilog);
	fprintf(out, "FAST_SCHEDULE	= %u\n", slurm_ctl_conf_ptr->fast_schedule);
	fprintf(out, "HASH_BASE	= %u\n", slurm_ctl_conf_ptr->hash_base);
	fprintf(out, "HEARTBEAT_INTERVAL	= %u\n", slurm_ctl_conf_ptr->heartbeat_interval);
	fprintf(out, "INIT_PROGRAM	= %s\n", slurm_ctl_conf_ptr->init_program);
	fprintf(out, "KILL_WAIT	= %u\n", slurm_ctl_conf_ptr->kill_wait);
	fprintf(out, "PRIORITIZE	= %s\n", slurm_ctl_conf_ptr->prioritize);
	fprintf(out, "PROLOG		= %s\n", slurm_ctl_conf_ptr->prolog);
	fprintf(out, "SERVER_DAEMON	= %s\n", slurm_ctl_conf_ptr->server_daemon);
	fprintf(out, "SERVER_TIMEOUT	= %u\n", slurm_ctl_conf_ptr->server_timeout);
	fprintf(out, "SLURM_CONF	= %s\n", slurm_ctl_conf_ptr->slurm_conf);
	fprintf(out, "TMP_FS		= %s\n", slurm_ctl_conf_ptr->tmp_fs);
}

int
slurm_load_ctl_conf (time_t update_time, slurm_ctl_conf_t **slurm_ctl_conf_ptr )
{
	int msg_size ;
	int rc ;
	slurm_fd sockfd ;
	slurm_msg_t request_msg ;
	slurm_msg_t response_msg ;
        last_update_msg_t last_time_msg ; 
	return_code_msg_t * rc_msg ;
	
	/* init message connection for message communication with controller */	
	if ( ( sockfd = slurm_open_controller_conn ( ) ) == SLURM_SOCKET_ERROR )
		return SLURM_SOCKET_ERROR ;	

	/* send request message */
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
	if ( msg_size )
		return msg_size;

	switch ( response_msg . msg_type )
        {
                case RESPONSE_BUILD_INFO:
                        *slurm_ctl_conf_ptr = ( slurm_ctl_conf_info_msg_t * ) response_msg . data ; 
        		return SLURM_SUCCESS ;
                        break ;
                case RESPONSE_SLURM_RC:
                        rc_msg = ( return_code_msg_t * ) response_msg . data ; 
			return (int) rc_msg->return_code ;
                        break ;
                default:
			return SLURM_UNEXPECTED_MSG_ERROR ;
                        break ;
        }

        return SLURM_SUCCESS ;
}
