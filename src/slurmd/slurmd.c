/*****************************************************************************\
 * controller.c - main control machine daemon for slurm
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Kevin Tew <tew1@llnl.gov> et. al.
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
 *  with ConMan; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#ifdef have_config_h
#  include <config.h>
#endif

#include <src/slurmd/slurmd.h> 
#include <errno.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <src/common/xmalloc.h>
#include <src/common/xstring.h>

#include <src/common/slurm_protocol_api.h>
#include <src/slurmd/get_mach_stat.h>
#include <src/common/log.h>

#define BUF_SIZE 1024
#define MAX_NAME_LEN 1024

time_t init_time;

void slurmd_req ( slurm_msg_t * msg );
/*
inline static void slurm_rpc_dump_build ( slurm_msg_t * msg ) ;
*/
int send_node_registration_status_msg ( ) ;
int fill_in_node_registration_status_msg ( slurm_node_registration_status_msg_t * node_reg_msg ) ;
int slurmd_msg_engine ( void * args ) ;

int 
main (int argc, char *argv[]) 
{
	int error_code ;
	
	
	char node_name[MAX_NAME_LEN];
	log_options_t opts = LOG_OPTS_STDERR_ONLY ;

	init_time = time (NULL);
	log_init(argv[0], opts, SYSLOG_FACILITY_DAEMON, NULL);
/*
	if ( ( error_code = init_slurm_conf () ) ) 
		fatal ("slurmd: init_slurm_conf error %d", error_code);
	if ( ( error_code = read_slurm_conf (SLURM_CONF) ) ) 
		fatal ("slurmd: error %d from read_slurm_conf reading %s", error_code, SLURM_CONF);
*/
	if ( ( error_code = gethostname (node_name, MAX_NAME_LEN) ) ) 
		fatal ("slurmd: errno %d from gethostname", errno);
	
	slurmd_msg_engine ( NULL ) ;
	
	send_node_registration_status_msg ( ) ;
	return SLURM_SUCCESS ;
}

int send_node_registration_status_msg ( )
{
	slurm_msg_t request_msg ;
	slurm_msg_t response_msg ;
	slurm_node_registration_status_msg_t node_reg_msg ;
	
	fill_in_node_registration_status_msg ( & node_reg_msg ) ;
 
	request_msg . msg_type = REQUEST_NODE_REGISTRATION_STATUS ;
	request_msg . data = & node_reg_msg ;
		
	slurm_send_recv_controller_msg ( & request_msg , & response_msg ) ;
	return SLURM_SUCCESS ;
}

int fill_in_node_registration_status_msg ( slurm_node_registration_status_msg_t * node_reg_msg )
{
	int error_code ;
	char node_name[MAX_NAME_LEN];
	if ( ( error_code = gethostname (node_name, MAX_NAME_LEN) ) )
		fatal ("slurmd: errno %d from gethostname", errno);

	node_reg_msg -> timestamp = time ( NULL ) ;
	node_reg_msg -> node_name = xstrdup ( node_name ) ; 
	get_procs ( & node_reg_msg -> cpus );
	get_memory ( & node_reg_msg -> real_memory_size ) ;
	get_tmp_disk ( & node_reg_msg -> temporary_disk_space ) ;
	return SLURM_SUCCESS ;
}

int slurmd_msg_engine ( void * args )
{
	int error_code ;
	slurm_fd newsockfd;
        slurm_fd sockfd;
	slurm_msg_t * msg = NULL ;
	slurm_addr cli_addr ;

	if ( ( sockfd = slurm_init_msg_engine_port ( SLURM_PORT ) ) == SLURM_SOCKET_ERROR )
		fatal ("slurmctld: error starting message engine \n", errno);
	while (1) 
	{
		/* accept needed for stream implementation 
		 * is a no-op in mongo implementation that just passes sockfd to newsockfd
		 */
		if ( ( newsockfd = slurm_accept_msg_conn ( sockfd , & cli_addr ) ) == SLURM_SOCKET_ERROR )
		{
			error ("slurmctld: error %d from connect", errno) ;
			break ;
		}
		
		/* receive message call that must occur before thread spawn because in message 
		 * implementation their is no connection and the message is the sign of a new connection */
		msg = xmalloc ( sizeof ( slurm_msg_t ) ) ;	
		if (msg == NULL)
			return ENOMEM;
		
		if ( ( error_code = slurm_receive_msg ( newsockfd , msg ) ) == SLURM_SOCKET_ERROR )
		{
			error ("slurmctld: error %d from accept", errno);
			break ;
		}

		msg -> conn_fd = newsockfd ;	
/************************* 
 * convert to pthread, tbd 
 *************************/
		slurmd_req ( msg );	/* process the request */
		/* close should only be called when the stream implementation is being used 
		 * the following call will be a no-op in the message implementation */
		slurm_close_accepted_conn ( newsockfd ); /* close the new socket */
	}			
	return 0 ;
}

void
slurmd_req ( slurm_msg_t * msg )
{
	
	switch ( msg->msg_type )
	{	
		/*
		case REQUEST_BUILD_INFO:
			slurm_rpc_dump_build ( msg ) ;
			slurm_free_last_update_msg ( msg -> data ) ;
			break;
		case REQUEST_NODE_INFO:
			slurm_rpc_dump_nodes ( msg ) ;
			slurm_free_last_update_msg ( msg -> data ) ;
			break ;
		case REQUEST_JOB_INFO:
			slurm_rpc_dump_jobs ( msg ) ;
			slurm_free_last_update_msg ( msg -> data ) ;
			break;
		case REQUEST_PARTITION_INFO:
			slurm_rpc_dump_partitions ( msg ) ;
			slurm_free_last_update_msg ( msg -> data ) ;
			break;
		case REQUEST_RESOURCE_ALLOCATION:
			break;
		case REQUEST_CANCEL_JOB:
			slurm_rpc_job_cancel ( msg ) ;
			slurm_free_job_id_msg ( msg -> data ) ;
			break;
		case REQUEST_SUBMIT_BATCH_JOB: 
			slurm_rpc_submit_batch_job ( msg ) ;
			slurm_free_job_desc_msg ( msg -> data ) ; 
			break;
		case REQUEST_NODE_REGISRATION_STATUS:
			break;
		case REQUEST_RECONFIGURE:
			slurm_rpc_reconfigure_controller ( msg ) ;
			break;
		*/
		default:
			error ("slurmctld_req: invalid request msg type %d\n", msg-> msg_type);
			slurm_send_rc_msg ( msg , EINVAL );
			break;
	}
	slurm_free_msg ( msg ) ;
}

/* Reconfigure - re-initialized from configuration files */
void 
slurm_rpc_slurmd_example ( slurm_msg_t * msg )
{
	/* init */
	int error_code;
	clock_t start_time;

	start_time = clock ();

	/* do RPC call */
	/*error_code = init_slurm_conf ();
	if (error_code == 0)
		error_code = read_slurm_conf (SLURM_CONF);
	reset_job_bitmaps ();
	*/
	/* return result */
	if (error_code)
	{
		error ("slurmctld_req: reconfigure error %d, time=%ld",
				error_code, (long) (clock () - start_time));
		slurm_send_rc_msg ( msg , error_code );
	}
	else
	{
		info ("slurmctld_req: reconfigure completed successfully, time=%ld", 
				(long) (clock () - start_time));
		slurm_send_rc_msg ( msg , SLURM_SUCCESS );
	}

}
