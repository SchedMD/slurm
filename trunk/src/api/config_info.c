/****************************************************************************\
 * config_info.c - get the build information of slurm
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Moe Jette <jette1@llnl.gov> and Kevin Tew <tew1@llnl.gov.
 *  UCRL-CODE-2002-040.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

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
	return_code_msg_t * slurm_rc_msg ;
	
        /* init message connection for message communication with controller */
	if ( ( sockfd = slurm_open_controller_conn ( ) ) == SLURM_SOCKET_ERROR ) {
		slurm_seterrno ( SLURM_COMMUNICATIONS_CONNECTION_ERROR );
		return SLURM_SOCKET_ERROR ;
	}
	/* send request message */
	last_time_msg . last_update = update_time ;
	request_msg . msg_type = REQUEST_BUILD_INFO ;
	request_msg . data = &last_time_msg ;
	if ( ( rc = slurm_send_controller_msg ( sockfd , & request_msg ) ) == SLURM_SOCKET_ERROR ) {
		slurm_seterrno ( SLURM_COMMUNICATIONS_SEND_ERROR );
		return SLURM_SOCKET_ERROR ;
	}
	
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
        		return SLURM_PROTOCOL_SUCCESS ;
                        break ;
		case RESPONSE_SLURM_RC:
			slurm_rc_msg = ( return_code_msg_t * ) response_msg . data ;
			rc = slurm_rc_msg->return_code;
			slurm_free_return_code_msg ( slurm_rc_msg );	
			if (rc) {
				slurm_seterrno ( rc );
				return SLURM_PROTOCOL_ERROR;
			}
			break ;
		default:
			slurm_seterrno ( SLURM_UNEXPECTED_MSG_ERROR );
			return SLURM_PROTOCOL_ERROR;
			break ;
	}

        return SLURM_PROTOCOL_SUCCESS ;
}

