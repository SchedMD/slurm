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
#include <stdlib.h>
#include <getopt.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <src/common/hostlist.h>
#include <src/common/xmalloc.h>
#include <src/common/xstring.h>
#include <src/common/list.h>
#include <src/common/slurm_protocol_api.h>
#include <src/common/log.h>

#include <src/slurmd/get_mach_stat.h>
#include <src/slurmd/slurmd.h> 
#include <src/slurmd/task_mgr.h> 
#include <src/slurmd/shmem_struct.h> 
#include <src/slurmd/signature_utils.h> 
#include <src/slurmd/credential_utils.h> 

#define BUF_SIZE 1024
#define MAX_NAME_LEN 1024
#define PTHREAD_IMPL

/* global variables */
typedef struct slurmd_config
{
	log_options_t log_opts ;
	char * slurm_conf ;
} slurmd_config_t ;


time_t init_time;
slurmd_shmem_t * shmem_seg ;
char hostname[MAX_NAME_LEN] ;
slurm_ssl_key_ctx_t verify_ctx ;
List credential_state_list ;
slurmd_config_t slurmd_conf ;

/* function prototypes */
static void slurmd_req ( slurm_msg_t * msg );
static int slurmd_msg_engine ( void * args ) ;
inline static int send_node_registration_status_msg ( ) ;

inline static void slurm_rpc_kill_tasks ( slurm_msg_t * msg ) ;
inline static void slurm_rpc_launch_tasks ( slurm_msg_t * msg ) ;
inline static void slurm_rpc_reattach_tasks_streams ( slurm_msg_t * msg ) ;
inline static void slurm_rpc_revoke_credential ( slurm_msg_t * msg ) ;

inline static int fill_in_node_registration_status_msg ( slurm_node_registration_status_msg_t * node_reg_msg ) ;
static void * service_connection ( void * arg ) ;
inline int slurmd_init ( ) ;
inline int slurmd_destroy ( ) ;
static int parse_commandline_args ( int argc , char ** argv , slurmd_config_t * slurmd_config ) ;

typedef struct connection_arg
{
	int newsockfd ;
} connection_arg_t ;

int main (int argc, char *argv[]) 
{
	int error_code ;
	char node_name[MAX_NAME_LEN];
	log_options_t log_opts_def = LOG_OPTS_STDERR_ONLY ;

	init_time = time (NULL);
	slurmd_conf . log_opts = log_opts_def ;


	parse_commandline_args ( argc, argv, & slurmd_conf ) ;
	log_init(argv[0], slurmd_conf . log_opts, SYSLOG_FACILITY_DAEMON, NULL);

/*
	if ( ( error_code = init_slurm_conf () ) ) 
		fatal ("slurmd: init_slurm_conf error %d", error_code);
	if ( ( error_code = read_slurm_conf ( ) ) ) 
		fatal ("slurmd: error %d from read_slurm_conf reading %s", error_code, SLURM_CONFIG_FILE);
*/

	/* shared memory init */
	slurmd_init ( ) ;
	
	if ( ( error_code = getnodename (node_name, MAX_NAME_LEN) ) ) 
		fatal ("slurmd: %m errno %d from getnodename", errno);

	if ( ( error_code = getnodename (hostname, MAX_NAME_LEN) ) ) 
		fatal ("slurmd: %m errno %d from getnodename", errno);

	/* send registration message to slurmctld*/
	send_node_registration_status_msg ( ) ;

	slurmd_msg_engine ( NULL ) ;

	/*slurm_msg_engine is a infinite io loop, but just in case we get back here */
	slurmd_destroy ( ) ;
	return SLURM_SUCCESS ;
}

int slurmd_init ( )
{
	shmem_seg = get_shmem ( ) ;
	init_shmem ( shmem_seg ) ;
	slurm_ssl_init ( ) ;
	slurm_init_verifier ( & verify_ctx , "pub_key_file" ) ;
	initialize_credential_state_list ( & credential_state_list ) ;
	return SLURM_SUCCESS ;
}

int slurmd_destroy ( )
{
	destroy_credential_state_list ( credential_state_list ) ;
	rel_shmem ( shmem_seg ) ;
	slurm_destroy_ssl_key_ctx ( & verify_ctx ) ;
	slurm_ssl_destroy ( ) ;
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

	/* get nodename */
	if ( ( error_code = getnodename (node_name, MAX_NAME_LEN) ) )
		fatal ("slurmd: errno %d from getnodename", errno);

	/* fill in data structure */
	node_reg_msg -> timestamp = time ( NULL ) ;
	node_reg_msg -> node_name = xstrdup ( node_name ) ; 
	get_procs ( & node_reg_msg -> cpus );
	get_memory ( & node_reg_msg -> real_memory_size ) ;
	get_tmp_disk ( & node_reg_msg -> temporary_disk_space ) ;
	info ("Configuration name=%s cpus=%u real_memory=%u, tmp_disk=%u", 
		node_name, node_reg_msg -> cpus, 
		node_reg_msg -> real_memory_size, node_reg_msg -> temporary_disk_space);
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
		error ("slurmd: %m error %d initializing thread attr", error_code ) ;
	}
	if ( ( error_code = pthread_attr_setdetachstate  ( & thread_attr , PTHREAD_CREATE_DETACHED ) ) )
	{
		error ("slurmd: %m error %d setting detach thread state", error_code ) ;
	}
	
	while (true) 
	{
		connection_arg_t * conn_arg = xmalloc ( sizeof ( connection_arg_t ) ) ;
			
		/* accept needed for stream implementation 
		 * is a no-op in mongo implementation that just passes sockfd to newsockfd
		 */
		if ( ( newsockfd = slurm_accept_msg_conn ( sockfd , & cli_addr ) ) == SLURM_SOCKET_ERROR )
		{
			error ("slurmd: %m error %d from connect", errno) ;
			continue ;
		}
		
		/* receive message call that must occur before thread spawn because in message 
		 * implementation their is no connection and the message is the sign of a new connection */
		conn_arg -> newsockfd = newsockfd ;
		
		if ( ( error_code = pthread_create ( & request_thread_id , & thread_attr , service_connection , ( void * ) conn_arg ) ) ) 
		{
			/* Do without threads on failure */
			error ("slurmd: pthread_create %m errno: %d", errno);
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
		error ("slurmd: error %d from accept", errno);
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
		case REQUEST_REATTACH_TASKS_STREAMS:
			slurm_rpc_reattach_tasks_streams ( msg ) ;
			slurm_free_reattach_tasks_streams_msg ( msg -> data ) ;
			break ;
		case REQUEST_REVOKE_JOB_CREDENTIAL:
			slurm_rpc_revoke_credential ( msg ) ;
			slurm_free_revoke_credential_msg ( msg -> data ) ;
			break ;
		default:
			error ("slurmd_req: invalid request msg type %d\n", msg-> msg_type);
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
	int error_code = SLURM_SUCCESS ;
	clock_t start_time;
	launch_tasks_request_msg_t * task_desc = ( launch_tasks_request_msg_t * ) msg->data ;
	slurm_msg_t resp_msg ;
	launch_tasks_response_msg_t task_resp ;
	char node_name[MAX_NAME_LEN];

	start_time = clock ();
	info ("slurmd_req: launch tasks message received");

	slurm_print_launch_task_msg ( task_desc ) ;

	/* get nodename */
	if ( ( error_code = getnodename (node_name, MAX_NAME_LEN) ) )
		fatal ("slurmd: errno %d from getnodename", errno);


	/* do RPC call */
	/* test credentials */
	verify_credential ( & verify_ctx , task_desc -> credential , credential_state_list ) ;

	task_resp . return_code = error_code ;
	task_resp . node_name = node_name ;

	resp_msg . address = task_desc -> response_addr ;	
	resp_msg . data = & task_resp ;
	resp_msg . msg_type = RESPONSE_LAUNCH_TASKS ;
	

	/* return result */
	if (error_code)
	{
		error ("slurmd_req: launch tasks error %d, time=%ld",
				error_code, (long) (clock () - start_time));
		slurm_send_only_node_msg ( & resp_msg );
	}
	else
	{
		info ("slurmd_req: launch authorization completed successfully, time=%ld", 
				(long) (clock () - start_time));
		slurm_send_only_node_msg ( & resp_msg );
	}
	
	error_code = launch_tasks ( task_desc );

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

void slurm_rpc_revoke_credential ( slurm_msg_t * msg )
{
	/* init */
	int error_code = SLURM_SUCCESS;
	clock_t start_time;
	revoke_credential_msg_t * revoke_credential_msg = ( revoke_credential_msg_t * ) msg->data ;

	start_time = clock ();

	/* do RPC call */
	error_code = revoke_credential ( revoke_credential_msg, credential_state_list ) ;
	
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

void slurm_rpc_slurmd_template ( slurm_msg_t * msg )
{
	/* init */
	int error_code = SLURM_SUCCESS;
	clock_t start_time;
	/*_msg_t * _msg = ( _msg_t * ) msg->data ; */

	start_time = clock ();

	/* do RPC call */
	
	/*error_code = (); */
	
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

void usage (char *prog_name)
{
	printf ("%s [OPTIONS]\n", prog_name);
	printf ("  -e <errlev>  Set stderr logging to the specified level\n");
	printf ("  -f <file>    Use specified configuration file name\n");
	printf ("  -h           Print a help message describing usage\n");
	printf ("  -l <errlev>  Set logfile logging to the specified level\n");
	printf ("  -s <errlev>  Set syslog logging to the specified level\n");
	printf ("<errlev> is an integer between 0 and 7 with higher numbers providing more detail.\n");
}

int parse_commandline_args ( int argc , char ** argv , slurmd_config_t * slurmd_config )
{
	int c;
	int digit_optind = 0;
	int errlev;
	opterr = 0;

	while (1) 
	{
		int this_option_optind = optind ? optind : 1;
		int option_index = 0;
		static struct option long_options[] = 
		{
			{"add", 1, 0, 0},
			{"append", 0, 0, 0},
			{"delete", 1, 0, 0},
			{"verbose", 0, 0, 0},
			{"create", 1, 0, 'c'},
			{"file", 1, 0, 0},
			{0, 0, 0, 0}
		};

		c = getopt_long (argc, argv, "ehfls:d:012", long_options, &option_index);
		if (c == -1)
			break;


		switch (c) 
		{
			case 'e':
				errlev = strtol (optarg, (char **) NULL, 10);
				if ((errlev < LOG_LEVEL_QUIET) ||
						(errlev > LOG_LEVEL_DEBUG3)) {
					fprintf (stderr, "invalid errlev argument\n");
					usage (argv[0]);
					exit (1);
				}
				slurmd_config -> log_opts . stderr_level = errlev;
				break;
			case 'h':
				usage (argv[0]);
				exit (0);
				break;
			case 'f':
				slurmd_config -> slurm_conf = optarg;
				printf("slurmctrld.slurm_conf = %s\n", slurmd_config -> slurm_conf );
				break;
			case 'l':
				errlev = strtol (optarg, (char **) NULL, 10);
				if ((errlev < LOG_LEVEL_QUIET) ||
						(errlev > LOG_LEVEL_DEBUG3)) {
					fprintf (stderr, "invalid errlev argument\n");
					usage (argv[0]);
					exit (1);
				}
				slurmd_config -> log_opts . logfile_level = errlev;
				break;
			case 's':
				errlev = strtol (optarg, (char **) NULL, 10);
				if ((errlev < LOG_LEVEL_QUIET) ||
						(errlev > LOG_LEVEL_DEBUG3)) {
					fprintf (stderr, "invalid errlev argument\n");
					usage (argv[0]);
					exit (1);
				}
				slurmd_config -> log_opts . syslog_level = errlev;
				break;
			case 0:
				info ("option %s", long_options[option_index].name);
				if (optarg)
				{
					info (" with arg %s", optarg);
				}
				break;

			case '0':
			case '1':
			case '2':
				if (digit_optind != 0 && digit_optind != this_option_optind)
				{
					info ("digits occur in two different argv-elements.");
				}
				digit_optind = this_option_optind;
				info ("option %c\n", c);
				break;
			case '?':
				info ("?? getopt returned character code 0%o ??", c);
				break;

			default:
				info ("?? getopt returned character code 0%o ??", c);
				usage (argv[0]);
				exit (1);
				break;
		}

		if (optind < argc) {
			printf ("non-option ARGV-elements: ");
			while (optind < argc)
			{
				printf ("%s ", argv[optind++]);
			}
			printf ("\n");
		}
	}
	return SLURM_SUCCESS ;
}
