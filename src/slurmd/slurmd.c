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

#include <errno.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <src/common/xmalloc.h>
#include <src/common/xstring.h>
#include <src/common/list.h>
#include <src/common/slurm_protocol_api.h>
#include <src/common/log.h>

#include <src/slurmd/get_mach_stat.h>
#include <src/slurmd/slurmd.h> 

#define BUF_SIZE 1024
#define MAX_NAME_LEN 1024
#define PTHREAD_IMPL

/* global variables */

static List task_list ;
time_t init_time;

/* function prototypes */
void slurmd_req ( slurm_msg_t * msg );
int send_node_registration_status_msg ( ) ;
int fill_in_node_registration_status_msg ( slurm_node_registration_status_msg_t * node_reg_msg ) ;
int slurmd_msg_engine ( void * args ) ;
void * request_thread ( void * arg ) ;
void init ( ) ;

void slurm_rpc_launch_tasks ( slurm_msg_t * msg ) ;
void slurm_rpc_kill_tasks ( slurm_msg_t * msg ) ;

int main (int argc, char *argv[]) 
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
	init ( ) ;	
	send_node_registration_status_msg ( ) ;
	slurmd_msg_engine ( NULL ) ;
	return SLURM_SUCCESS ;
}

void init ( ) 
{
	task_list = list_create ( NULL ) ;
}


int send_node_registration_status_msg ( )
{
	slurm_msg_t request_msg ;
	slurm_msg_t response_msg ;
	slurm_node_registration_status_msg_t node_reg_msg ;
	
	fill_in_node_registration_status_msg ( & node_reg_msg ) ;
 
	request_msg . msg_type = MESSAGE_NODE_REGISTRATION_STATUS ;
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
	pthread_t request_thread_id ;
	pthread_attr_t thread_attr ;

	if ( ( sockfd = slurm_init_msg_engine_port ( SLURM_PORT+1 ) ) == SLURM_SOCKET_ERROR )
		fatal ("slurmctld: error starting message engine \n", errno);
#ifdef PTHREAD_IMPL
	if ( ( error_code = pthread_attr_init ( & thread_attr ) ) ) 
	{
		error ("slurmd: error %d initializing thread attr", error_code ) ;
	}
	if ( ( error_code = pthread_attr_setdetachstate  ( & thread_attr , PTHREAD_CREATE_DETACHED ) ) )
	{
		error ("slurmd: error %d setting detach thread state", error_code ) ;
	}
#endif
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

#ifdef PTHREAD_IMPL		
		if ( ( error_code = pthread_create ( & request_thread_id , & thread_attr , request_thread , ( void * ) msg ) ) ) 
		{
			error ("slurmctld: error %d creating request thread", error_code ) ;
		}
#else
                slurmd_req ( msg );     /* process the request */
                /* close should only be called when the stream implementation is being used
                 * the following call will be a no-op in the message implementation */
                slurm_close_accepted_conn ( newsockfd ); /* close the new socket */

#endif
	}			
	return 0 ;
}

void * request_thread ( void * arg )
{
	slurm_msg_t * msg = ( slurm_msg_t * ) arg ;

	slurmd_req ( msg ) ;

	slurm_close_accepted_conn ( msg -> conn_fd ) ;
	pthread_exit ( 0 ) ;
	return NULL ;
}

void slurmd_req ( slurm_msg_t * msg )
{
	
	switch ( msg->msg_type )
	{	
		case REQUEST_LAUNCH_TASKS:
			slurm_rpc_launch_tasks ( msg ) ;
			slurm_free_launch_tasks_msg ( msg -> data ) ;
			break;
		case REQUEST_KILL_TASKS:
			slurm_rpc_kill_tasks ( msg ) ;
			slurm_free_kill_tasks_msg ( msg -> data ) ;
			break ;
		default:
			error ("slurmctld_req: invalid request msg type %d\n", msg-> msg_type);
			slurm_send_rc_msg ( msg , EINVAL );
			break;
	}
	slurm_free_msg ( msg ) ;
}

/* Launches tasks */
void slurm_rpc_launch_tasks ( slurm_msg_t * msg )
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

/* Kills Launched Tasks */
void slurm_rpc_kill_tasks ( slurm_msg_t * msg )
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

/* Reconfigure - re-initialized from configuration files */
void slurm_rpc_slurmd_example ( slurm_msg_t * msg )
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
