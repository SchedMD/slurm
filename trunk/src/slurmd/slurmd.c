/*****************************************************************************\
 * slurmd.c - main server machine daemon for slurm
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
#include <src/slurmd/task_mgr.h> 
#include <src/slurmd/shmem_struct.h> 

#define BUF_SIZE 1024
#define MAX_NAME_LEN 1024
#define PTHREAD_IMPL

/* global variables */

time_t init_time;
slurmd_shmem_t * shmem_seg ;

/* function prototypes */
static void * request_thread ( void * arg ) ;
static void slurmd_req ( slurm_msg_t * msg );
static int slurmd_msg_engine ( void * args ) ;
inline static int send_node_registration_status_msg ( ) ;
inline static void slurm_rpc_kill_tasks ( slurm_msg_t * msg ) ;
inline static void slurm_rpc_launch_tasks ( slurm_msg_t * msg ) ;
inline static int fill_in_node_registration_status_msg ( slurm_node_registration_status_msg_t * node_reg_msg ) ;
static void * service_connection ( void * arg ) ;

typedef struct connection_arg
{
	int newsockfd ;
} connection_arg_t ;

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
	if ( ( error_code = read_slurm_conf ( ) ) ) 
		fatal ("slurmd: error %d from read_slurm_conf reading %s", error_code, SLURM_CONFIG_FILE);
*/

	/* shared memory init */
	shmem_seg = get_shmem ( ) ;
	init_shmem ( shmem_seg ) ;

	if ( ( error_code = gethostname (node_name, MAX_NAME_LEN) ) ) 
		fatal ("slurmd: errno %d from gethostname", errno);

	/* send registration message to slurmctld*/
	send_node_registration_status_msg ( ) ;

	slurmd_msg_engine ( NULL ) ;

	/*slurm_msg_engine is a infinite io loop, but just in case we get back here */
	rel_shmem ( shmem_seg ) ;
	return SLURM_SUCCESS ;
}

/* sends a node_registration_status_msg to the slurmctld upon boot
 * announcing availibility for computationt */
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

/* calls machine dependent system info calls to fill structure
 * node_reg_msg - structure to fill with system info
 * returns - return code
 */
int fill_in_node_registration_status_msg ( slurm_node_registration_status_msg_t * node_reg_msg )
{
	int error_code ;
	char node_name[MAX_NAME_LEN];

	/* get hostname */
	if ( ( error_code = gethostname (node_name, MAX_NAME_LEN) ) )
		fatal ("slurmd: errno %d from gethostname", errno);

	/* fill in data structure */
	node_reg_msg -> timestamp = time ( NULL ) ;
	node_reg_msg -> node_name = xstrdup ( node_name ) ; 
	get_procs ( & node_reg_msg -> cpus );
	get_memory ( & node_reg_msg -> real_memory_size ) ;
	get_tmp_disk ( & node_reg_msg -> temporary_disk_space ) ;

	return SLURM_SUCCESS ;
}

/* accept thread for incomming slurm messages 
 * args - do nothing right now */
int slurmd_msg_engine ( void * args )
{
	int error_code ;
	slurm_fd newsockfd;
        slurm_fd sockfd;
	slurm_addr cli_addr ;
	pthread_t request_thread_id ;
	pthread_attr_t thread_attr ;

	if ( ( error_code = read_slurm_port_config ( ) ) )
		fatal ("slurmd: error reading configuration file \n", error_code);

	if ( ( sockfd = slurm_init_msg_engine_port ( slurm_get_slurmd_port ( ) ) )
			 == SLURM_SOCKET_ERROR )
		fatal ("slurmd: error starting message engine \n", errno);
	
	if ( ( error_code = pthread_attr_init ( & thread_attr ) ) ) 
	{
		error ("slurmd: error %d initializing thread attr", error_code ) ;
	}
	if ( ( error_code = pthread_attr_setdetachstate  ( & thread_attr , PTHREAD_CREATE_DETACHED ) ) )
	{
		error ("slurmd: error %d setting detach thread state", error_code ) ;
	}
	
	while (true) 
	{
		connection_arg_t * conn_arg = xmalloc ( sizeof ( connection_arg_t ) ) ;
			
		/* accept needed for stream implementation 
		 * is a no-op in mongo implementation that just passes sockfd to newsockfd
		 */
		if ( ( newsockfd = slurm_accept_msg_conn ( sockfd , & cli_addr ) ) == SLURM_SOCKET_ERROR )
		{
			error ("slurmctld: error %d from connect", errno) ;
			continue ;
		}
		
		/* receive message call that must occur before thread spawn because in message 
		 * implementation their is no connection and the message is the sign of a new connection */
		conn_arg -> newsockfd = newsockfd ;
		
		if ( ( error_code = pthread_create ( & request_thread_id , & thread_attr , service_connection , ( void * ) conn_arg ) ) ) 
		{
			/* Do without threads on failure */
			error ("pthread_create errno %d", errno);
			service_connection ( ( void * ) conn_arg ) ;
		}
	}			
	return 0 ;
}

/* worker thread method for accepted message connections
 * arg - a slurm_msg_t representing the accepted incomming message
 * returns - nothing, void * because of pthread def
 */
void * service_connection ( void * arg ) 
{
	int error_code;
	slurm_fd newsockfd = ( ( connection_arg_t * ) arg ) -> newsockfd ;
	slurm_msg_t * msg = NULL ;

	msg = xmalloc ( sizeof ( slurm_msg_t ) ) ;	

	if ( ( error_code = slurm_receive_msg ( newsockfd , msg ) ) == SLURM_SOCKET_ERROR )
	{
		error ("slurmctld: error %d from accept", errno);
		slurm_free_msg ( msg ) ;
	}
	else
	{
		msg -> conn_fd = newsockfd ;	
		slurmd_req ( msg );     /* process the request */
	}

	/* close should only be called when the stream implementation is being used
	 * the following call will be a no-op in the message implementation */
	slurm_close_accepted_conn ( newsockfd ); /* close the new socket */
	xfree ( arg ) ;
	return NULL ;
}

/* multiplexing message handler
 * msg - incomming request message 
 */
void slurmd_req ( slurm_msg_t * msg )
{
	
	switch ( msg->msg_type )
	{	
		case REQUEST_LAUNCH_TASKS:
			slurm_rpc_launch_tasks ( msg ) ;
			slurm_free_launch_tasks_request_msg ( msg -> data ) ;
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


/******************************/
/* rpc methods */
/******************************/

/* Launches tasks */
void slurm_rpc_launch_tasks ( slurm_msg_t * msg )
{
	/* init */
	int error_code;
	clock_t start_time;
	launch_tasks_request_msg_t * task_desc = ( launch_tasks_request_msg_t * ) msg->data ;

	start_time = clock ();
	info ("slurmd_req: launch tasks message received");

	/* do RPC call */
	error_code = launch_tasks ( task_desc );

	/* return result */
	if (error_code)
	{
		error ("slurmd_req: launch tasks error %d, time=%ld",
				error_code, (long) (clock () - start_time));
		slurm_send_rc_msg ( msg , error_code );
	}
	else
	{
		info ("slurmd_req: launch tasks completed successfully, time=%ld", 
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
	kill_tasks_msg_t * kill_tasks_msg = ( kill_tasks_msg_t * ) msg->data ;

	start_time = clock ( );

	/* do RPC call */
	error_code = kill_tasks ( kill_tasks_msg );

	/* return result */
	if (error_code)
	{
		error ("slurmd_req: kill tasks error %d, time=%ld",
				error_code, (long) (clock () - start_time));
		slurm_send_rc_msg ( msg , error_code );
	}
	else
	{
		info ("slurmd_req: kill tasks completed successfully, time=%ld", 
				(long) (clock () - start_time));
		slurm_send_rc_msg ( msg , SLURM_SUCCESS );
	}
}

void slurm_rpc_reattach_tasks_streams ( slurm_msg_t * msg )
{
	/* init */
	int error_code = SLURM_SUCCESS;
	clock_t start_time;
	reattach_tasks_streams_msg_t * reattach_tasks_steams_msg = ( reattach_tasks_streams_msg_t * ) msg->data ;

	start_time = clock ();

	/* do RPC call */
	error_code = reattach_tasks_streams ( reattach_tasks_steams_msg );
	
	/* return result */
	if (error_code)
	{
		error ("slurmd_req: reattach streams error %d, time=%ld",
				error_code, (long) (clock () - start_time));
		slurm_send_rc_msg ( msg , error_code );
	}
	else
	{
		info ("slurmd_req: reattach_streams completed successfully, time=%ld", 
				(long) (clock () - start_time));
		slurm_send_rc_msg ( msg , SLURM_SUCCESS );
	}

}

void slurm_rpc_slurmd_template ( slurm_msg_t * msg )
{
	/* init */
	int error_code = SLURM_SUCCESS;
	clock_t start_time;
	/*_msg_t * _msg = ( _msg_t * ) msg->data ; */

	start_time = clock ();

	/* do RPC call */
	
	/*error_code = init_slurm_conf (); */
	
	/* return result */
	if (error_code)
	{
		error ("slurmd_req:  error %d, time=%ld",
				error_code, (long) (clock () - start_time));
		slurm_send_rc_msg ( msg , error_code );
	}
	else
	{
		info ("slurmd_req:  completed successfully, time=%ld", 
				(long) (clock () - start_time));
		slurm_send_rc_msg ( msg , SLURM_SUCCESS );
	}

}

