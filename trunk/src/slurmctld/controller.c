/*****************************************************************************\
 *  controller.c - main control machine daemon for slurm
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Moe Jette <jette@llnl.gov>, Kevin Tew <tew1@llnl.gov>, et. al.
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
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <netinet/in.h>
#include <unistd.h>

#include <src/common/hostlist.h>
#include <src/common/log.h>
#include <src/common/pack.h>
#include <src/common/slurm_protocol_api.h>
#include <src/common/macros.h>
#include <src/common/xstring.h>
#include <src/slurmctld/agent.h>
#include <src/slurmctld/locks.h>
#include <src/slurmctld/slurmctld.h>
#include <src/common/credential_utils.h>
#include <src/common/slurm_auth.h>

#define BUF_SIZE 1024
#define DEFAULT_DAEMONIZE 0
#define DEFAULT_RECOVER 0
#define MAX_SERVER_THREAD_COUNT 20
#define MEM_LEAK_TEST 0

/* Log to stderr and syslog until becomes a daemon */
log_options_t log_opts = { 1, LOG_LEVEL_INFO,  LOG_LEVEL_INFO, LOG_LEVEL_QUIET } ;
slurm_ctl_conf_t slurmctld_conf;
time_t shutdown_time = (time_t)0;
static pthread_mutex_t thread_count_lock = PTHREAD_MUTEX_INITIALIZER;
int server_thread_count = 0;
pid_t slurmctld_pid;
pthread_t thread_id_main = (pthread_t)0;
pthread_t thread_id_sig = (pthread_t)0;
pthread_t thread_id_rpc = (pthread_t)0;
extern slurm_ssl_key_ctx_t sign_ctx ;
int daemonize = DEFAULT_DAEMONIZE;
int recover = DEFAULT_RECOVER;

int msg_from_root (void);
void slurmctld_req ( slurm_msg_t * msg );
void fill_ctld_conf ( slurm_ctl_conf_t * build_ptr );
void init_ctld_conf ( slurm_ctl_conf_t * build_ptr );
void parse_commandline( int argc, char* argv[], slurm_ctl_conf_t * );
void *process_rpc ( void * req );
inline int report_locks_set ( void );
inline static void save_all_state ( void );
void *slurmctld_background ( void * no_data );
void *slurmctld_signal_hand ( void * no_data );
void *slurmctld_rpc_mgr( void * no_data );
inline static int slurmctld_shutdown ( void );
void * service_connection ( void * arg );
void usage (char *prog_name);

inline static void slurm_rpc_allocate_resources ( slurm_msg_t * msg , uint8_t immediate ) ;
inline static void slurm_rpc_allocate_and_run ( slurm_msg_t * msg );
inline static void slurm_rpc_dump_build ( slurm_msg_t * msg ) ;
inline static void slurm_rpc_dump_nodes ( slurm_msg_t * msg ) ;
inline static void slurm_rpc_dump_partitions ( slurm_msg_t * msg ) ;
inline static void slurm_rpc_dump_jobs ( slurm_msg_t * msg ) ;
inline static void slurm_rpc_job_step_cancel ( slurm_msg_t * msg ) ;
inline static void slurm_rpc_job_step_complete ( slurm_msg_t * msg ) ;
inline static void slurm_rpc_job_step_create( slurm_msg_t* msg ) ;	
inline static void slurm_rpc_job_step_get_info ( slurm_msg_t * msg ) ;
inline static void slurm_rpc_job_will_run ( slurm_msg_t * msg ) ;
inline static void slurm_rpc_node_registration ( slurm_msg_t * msg ) ;
inline static void slurm_rpc_old_job_alloc ( slurm_msg_t * msg ) ;
inline static void slurm_rpc_ping ( slurm_msg_t * msg ) ;
inline static void slurm_rpc_reconfigure_controller ( slurm_msg_t * msg ) ;
inline static void slurm_rpc_shutdown_controller ( slurm_msg_t * msg );
inline static void slurm_rpc_shutdown_controller_immediate ( slurm_msg_t * msg );
inline static void slurm_rpc_submit_batch_job ( slurm_msg_t * msg ) ;
inline static void slurm_rpc_update_job ( slurm_msg_t * msg ) ;
inline static void slurm_rpc_update_node ( slurm_msg_t * msg ) ;
inline static void slurm_rpc_update_partition ( slurm_msg_t * msg ) ;

typedef struct connection_arg
{
	int newsockfd ;
} connection_arg_t ;

/* main - slurmctld main function, start various threads and process RPCs */
int 
main (int argc, char *argv[]) 
{
	int error_code;
	char node_name[MAX_NAME_LEN];
	pthread_attr_t thread_attr_sig, thread_attr_rpc;
	sigset_t set;

	/*
	 * Establish initial configuration
	 */
	log_init(argv[0], log_opts, SYSLOG_FACILITY_DAEMON, NULL);
	thread_id_main = pthread_self();

	slurmctld_pid = getpid ( );
	init_ctld_conf ( &slurmctld_conf );
	parse_commandline ( argc, argv, &slurmctld_conf );
	if (daemonize) {
		error_code = daemon (0, 0);
		if (error_code)
			error ("daemon error %d", error_code);
	}
	init_locks ( );

	if ( ( error_code = read_slurm_conf (recover)) ) 
		fatal ("read_slurm_conf error %d reading %s", error_code, SLURM_CONFIG_FILE);
	if (daemonize) {
		if (chdir (slurmctld_conf.state_save_location))
			fatal ("chdir to %s error %m", slurmctld_conf.state_save_location);
	}
	if ( ( error_code = getnodename (node_name, MAX_NAME_LEN) ) ) 
		fatal ("getnodename error %d", error_code);

	if ( strcmp (node_name, slurmctld_conf.control_machine) &&  
	     strcmp ("localhost", slurmctld_conf.control_machine) &&
		/* this is not the control machine AND */
	     ((slurmctld_conf.backup_controller == NULL) ||
		/* there is no backup controller OR */
	      (strcmp (node_name, slurmctld_conf.backup_controller) &&
	       strcmp ("localhost", slurmctld_conf.backup_controller))) )
		/* this is not the backup controller */
	       	fatal ("this machine (%s) is not the primary (%s) or backup (%s) controller", 
			node_name, slurmctld_conf.control_machine, 
			slurmctld_conf.backup_controller);

	/* init ssl job credential stuff */
        slurm_ssl_init ( ) ;
	slurm_init_signer ( &sign_ctx, slurmctld_conf.job_credential_private_key ) ;

	/* Block SIGALRM everyone not explicitly enabled */
	if (sigemptyset (&set))
		error ("sigemptyset error: %m");
	if (sigaddset (&set, SIGALRM))
		error ("sigaddset error on SIGALRM: %m");
	if (sigprocmask (SIG_BLOCK, &set, NULL) != 0)
		fatal ("sigprocmask error: %m");

	/*
	 * create attached thread signal handling
	 */
	if (pthread_attr_init (&thread_attr_sig))
		fatal ("pthread_attr_init error %m");
#ifdef PTHREAD_SCOPE_SYSTEM
	/* we want 1:1 threads if there is a choice */
	if (pthread_attr_setscope (&thread_attr_sig, PTHREAD_SCOPE_SYSTEM))
		error ("pthread_attr_setscope error %m");
#endif
	if (pthread_create ( &thread_id_sig, &thread_attr_sig, slurmctld_signal_hand, NULL))
		fatal ("pthread_create %m");

	/*
	 * create attached thread to process RPCs
	 */
	pthread_mutex_lock(&thread_count_lock);
	server_thread_count++;
	pthread_mutex_unlock(&thread_count_lock);
	if (pthread_attr_init (&thread_attr_rpc))
		fatal ("pthread_attr_init error %m");
#ifdef PTHREAD_SCOPE_SYSTEM
	/* we want 1:1 threads if there is a choice */
	if (pthread_attr_setscope (&thread_attr_rpc, PTHREAD_SCOPE_SYSTEM))
		error ("pthread_attr_setscope error %m");
#endif
	if (pthread_create ( &thread_id_rpc, &thread_attr_rpc, slurmctld_rpc_mgr, NULL))
		fatal ("pthread_create error %m");

	slurmctld_background (NULL);	/* This could be run as a pthread */
	return SLURM_SUCCESS;
}

/* slurmctld_signal_hand - Process daemon-wide signals */
void *
slurmctld_signal_hand ( void * no_data ) 
{
	int sig ;
	int error_code;
	sigset_t set;
	/* Locks: Write configuration, write job, write node, write partition */
	slurmctld_lock_t config_write_lock = { WRITE_LOCK, WRITE_LOCK, WRITE_LOCK, WRITE_LOCK };

	(void) pthread_setcancelstate (PTHREAD_CANCEL_ENABLE, NULL);
	(void) pthread_setcanceltype (PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	info ("Send signals to slurmctld_signal_hand, pid = %u", getpid ());

	if (sigemptyset (&set))
		error ("sigemptyset error: %m");
	if (sigaddset (&set, SIGINT))
		error ("sigaddset error on SIGINT: %m");
	if (sigaddset (&set, SIGTERM))
		error ("sigaddset error on SIGTERM: %m");
	if (sigaddset (&set, SIGHUP))
		error ("sigaddset error on SIGHUP: %m");
	if (sigaddset (&set, SIGABRT))
		error ("sigaddset error on SIGABRT: %m");

	if (sigprocmask (SIG_BLOCK, &set, NULL) != 0)
		fatal ("sigprocmask error: %m");

	while (1) {
		sigwait (&set, &sig);
		switch (sig) {
			case SIGINT:	/* kill -2  or <CTRL-C> */
			case SIGTERM:	/* kill -15 */
				info ("Terminate signal (SIGINT or SIGTERM) received\n");
				shutdown_time = time (NULL);
				/* send REQUEST_SHUTDOWN_IMMEDIATE RPC */
				slurmctld_shutdown ();
				/* ssl clean up */
				slurm_destroy_ssl_key_ctx ( & sign_ctx ) ;
				slurm_ssl_destroy ( ) ;

				pthread_join (thread_id_rpc, NULL);
				return NULL;	/* Normal termination */
				break;
			case SIGHUP:	/* kill -1 */
				info ("Reconfigure signal (SIGHUP) received");
				lock_slurmctld (config_write_lock);
				error_code = read_slurm_conf (0);
				if (error_code == 0)
					reset_job_bitmaps ();
				unlock_slurmctld (config_write_lock);
				if (error_code)
					error ("read_slurm_conf error %d", error_code);
				break;
			case SIGABRT:	/* abort */
				fatal ("SIGABRT received");
				break;
			default:
				error ("Invalid signal (%d) received", sig);
		}
	}

}

/* slurmctld_rpc_mgr - Read incoming RPCs and create individual threads for each */
void *
slurmctld_rpc_mgr ( void * no_data ) 
{
	slurm_fd newsockfd;
        slurm_fd sockfd;
	slurm_addr cli_addr ;
	pthread_t thread_id_rpc_req;
	pthread_attr_t thread_attr_rpc_req;
	int no_thread;
	connection_arg_t * conn_arg;

	(void) pthread_setcancelstate (PTHREAD_CANCEL_ENABLE, NULL);
	(void) pthread_setcanceltype (PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	debug3 ("slurmctld_rpc_mgr pid = %u", getpid ());

	/* threads to process individual RPC's are detached */
	if (pthread_attr_init (&thread_attr_rpc_req))
		fatal ("pthread_attr_init %m");
	if (pthread_attr_setdetachstate (&thread_attr_rpc_req, PTHREAD_CREATE_DETACHED))
		fatal ("pthread_attr_setdetachstate %m");
#ifdef PTHREAD_SCOPE_SYSTEM
	/* we want 1:1 threads if there is a choice */
	if (pthread_attr_setscope (&thread_attr_rpc_req, PTHREAD_SCOPE_SYSTEM))
		error ("pthread_attr_setscope error %m");
#endif

	/* initialize port for RPCs */
	if ( ( sockfd = slurm_init_msg_engine_port ( slurmctld_conf . slurmctld_port ) ) 
			== SLURM_SOCKET_ERROR )
		fatal ("slurm_init_msg_engine_port error %m");

	/*
	 * Procss incoming RPCs indefinitely
	 */
	while (1) 
	{
		conn_arg = xmalloc ( sizeof ( connection_arg_t ) ) ;
		/* accept needed for stream implementation 
		 * is a no-op in message implementation that just passes sockfd to newsockfd
		 */
		if ( ( newsockfd = slurm_accept_msg_conn ( sockfd , & cli_addr ) ) == SLURM_SOCKET_ERROR )
		{
			error ("slurm_accept_msg_conn error %m") ;
			continue ;
		}
		conn_arg -> newsockfd = newsockfd ;
		pthread_mutex_lock(&thread_count_lock);
		server_thread_count++;
		pthread_mutex_unlock(&thread_count_lock);
		if (server_thread_count >= MAX_SERVER_THREAD_COUNT) {
			info ("Warning: server_thread_count is %d, over system limit", server_thread_count);
			no_thread = 1;
		}
		else if (shutdown_time)
			no_thread = 1;
		else if (pthread_create ( &thread_id_rpc_req, &thread_attr_rpc_req, service_connection, (void *) conn_arg )) {
			error ("pthread_create error %m");
			no_thread = 1;
		}
		else
			no_thread = 0;

		if (no_thread) {
			if ( service_connection ( ( void * ) conn_arg ) ) 
				break;
		}

	}

	debug3 ("slurmctld_rpc_mgr shutting down");
	pthread_mutex_lock(&thread_count_lock);
	server_thread_count--;
	pthread_mutex_unlock(&thread_count_lock);
	pthread_exit ((void *)0);
}

/* service_connection - service the RPC, return NULL except in case of REQUEST_SHUTDOWN_IMMEDIATE */
void * service_connection ( void * arg )
{
	int error_code;
	slurm_fd newsockfd = ( ( connection_arg_t * ) arg ) -> newsockfd ;
	slurm_msg_t * msg = NULL ;
	void * return_code = NULL;
	
	msg = xmalloc ( sizeof ( slurm_msg_t ) ) ;	

	if ( ( error_code = slurm_receive_msg ( newsockfd , msg ) ) == SLURM_SOCKET_ERROR )
	{
		error ("slurm_receive_msg error %m");
		slurm_free_msg ( msg ) ;
	}
	else {
		if (msg->msg_type == REQUEST_SHUTDOWN_IMMEDIATE)
			return_code = (void *) "fini";
		msg -> conn_fd = newsockfd ;	
		slurmctld_req ( msg );	/* process the request */
	}

	/* close should only be called when the socket implementation is being used 
	 * the following call will be a no-op in a message/mongo implementation */
	slurm_close_accepted_conn ( newsockfd ); /* close the new socket */

	xfree ( arg ) ;
	pthread_mutex_lock(&thread_count_lock);
	server_thread_count--;
	pthread_mutex_unlock(&thread_count_lock);
	return return_code ;	
}

/* slurmctld_background - process slurmctld background activities */
void *
slurmctld_background ( void * no_data )
{
	static time_t last_sched_time;
	static time_t last_checkpoint_time;
	static time_t last_ping_time;
	static time_t last_rpc_retry_time;
	static time_t last_timelimit_time;
	time_t now;
	/* Locks: Write job, write node, read partition */
	slurmctld_lock_t job_write_lock = { NO_LOCK, WRITE_LOCK, WRITE_LOCK, READ_LOCK };
	/* Locks: Write job, write node */
	slurmctld_lock_t node_write_lock = { NO_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK };
	/* Locks: Write partition */
	slurmctld_lock_t part_write_lock = { NO_LOCK, NO_LOCK, NO_LOCK, WRITE_LOCK };

	/* Let the dust settle before doing work */
	last_sched_time = last_checkpoint_time = last_timelimit_time = 
		last_ping_time = last_rpc_retry_time = time (NULL);
	(void) pthread_setcancelstate (PTHREAD_CANCEL_ENABLE, NULL);
	(void) pthread_setcanceltype (PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	debug3 ("slurmctld_background pid = %u", getpid ());

	while (shutdown_time == 0) {
		sleep (1);

		now = time (NULL);

		if (difftime (now, last_timelimit_time) > PERIODIC_TIMEOUT) {
			last_timelimit_time = now;
			debug ("Performing job time limit check");
			lock_slurmctld (job_write_lock);
			job_time_limit ();
			unlock_slurmctld (job_write_lock);
		}

		if (difftime (now, last_ping_time) >= slurmctld_conf.heartbeat_interval) {
			last_ping_time = now;
			debug ("Performing node ping");
			lock_slurmctld (node_write_lock);
			ping_nodes ();
			unlock_slurmctld (node_write_lock);
		}

		if (difftime (now, last_rpc_retry_time) >= RPC_RETRY_INTERVAL) {
			last_rpc_retry_time = now;
			agent_retry (NULL);
		}

		if (difftime (now, last_timelimit_time) >= PERIODIC_GROUP_CHECK) {
			last_timelimit_time = now;
			lock_slurmctld (part_write_lock);
			load_part_uid_allow_list ( 0 );
			unlock_slurmctld (part_write_lock);
		}

		if (difftime (now, last_sched_time) >= PERIODIC_SCHEDULE) {
			last_sched_time = now;
			debug ("Performing purge of old job records");
			lock_slurmctld (job_write_lock);
			purge_old_job ();	/* remove defunct job records */
			unlock_slurmctld (job_write_lock);
			if (schedule ())
				last_checkpoint_time = 0;	/* force state save */
		}

		if (shutdown_time || 
		    (difftime (now, last_checkpoint_time) >= PERIODIC_CHECKPOINT) ) {
			if (shutdown_time) {	
				/* wait for any RPC's to complete */
				if (server_thread_count)
					sleep (1);
				if (server_thread_count)
					sleep (1);
				if (server_thread_count)
					info ("warning: shutting down with server_thread_count of %d", server_thread_count);
				if (report_locks_set ( ) == 0) {
					last_checkpoint_time = now;
					save_all_state ( );
				} else
					error ("unable to save state due to set semaphores");
			}
			else {
				last_checkpoint_time = now;
				debug ("Performing full system state save");
				save_all_state ( );
			}
		}

	}
	debug3 ("slurmctld_background shutting down");

#if	MEM_LEAK_TEST
	/* This should purge all allocated memory,	*\
	\*	Anything left over represents a leak.	*/
	if (job_list)
		list_destroy (job_list);

	if (part_list)
		list_destroy (part_list);

	if (config_list)
		list_destroy (config_list);
	if (node_record_table_ptr)
		xfree (node_record_table_ptr);
	if (hash_table)
		xfree (hash_table);

	agent_purge ();
#endif
	return NULL;
}

/* save_all_state - save slurmctld state for later recovery */
void
save_all_state ( void )
{
	clock_t start_time;

	start_time = clock ();
	/* Each of these functions lock their own databases */
	(void) dump_all_node_state ( );
	(void) dump_all_part_state ( );
	(void) dump_all_job_state ( );
	info ("save_all_state complete, time=%ld", (long) (clock () - start_time));
}

/* report_locks_set - report any slurmctld locks left set, return count */
int
report_locks_set ( void )
{
	slurmctld_lock_flags_t lock_flags;
	char config[4]="", job[4]="", node[4]="", partition[4]="";
	int lock_count;

	get_lock_values (&lock_flags);

	if (lock_flags.entity[read_lock (CONFIG_LOCK)]) strcat (config, "R");
	if (lock_flags.entity[write_lock (CONFIG_LOCK)]) strcat (config, "W");
	if (lock_flags.entity[write_wait_lock (CONFIG_LOCK)]) strcat (config, "P");

	if (lock_flags.entity[read_lock (JOB_LOCK)]) strcat (job, "R");
	if (lock_flags.entity[write_lock (JOB_LOCK)]) strcat (job, "W");
	if (lock_flags.entity[write_wait_lock (JOB_LOCK)]) strcat (job, "P");

	if (lock_flags.entity[read_lock (NODE_LOCK)]) strcat (node, "R");
	if (lock_flags.entity[write_lock (NODE_LOCK)]) strcat (node, "W");
	if (lock_flags.entity[write_wait_lock (NODE_LOCK)]) strcat (node, "P");

	if (lock_flags.entity[read_lock (PART_LOCK)]) strcat (partition, "R");
	if (lock_flags.entity[write_lock (PART_LOCK)]) strcat (partition, "W");
	if (lock_flags.entity[write_wait_lock (PART_LOCK)]) strcat (partition, "P");

	lock_count = strlen (config) + strlen (job) + strlen (node) + strlen (partition);
	if (lock_count > 0)
		error ("The following locks were left set config:%s, job:%s, node:%s, partition:%s",
			config, job, node, partition);
	return lock_count;
}

/* process_rpc - process an RPC request and close the connection */
void *
process_rpc ( void * req )
{
	slurm_msg_t * msg;
	slurm_fd newsockfd;

	msg = (slurm_msg_t *) req;
	newsockfd = msg -> conn_fd;
	slurmctld_req ( msg );	/* process the request */

	/* close should only be called when the stream implementation is being used 
	 * the following call will be a no-op in the message implementation */
	slurm_close_accepted_conn ( newsockfd ); /* close the new socket */

	pthread_exit (NULL);
}

/* slurmctld_req - Process an individual RPC request */
void
slurmctld_req ( slurm_msg_t * msg )
{
	
	switch ( msg->msg_type )
	{
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
			slurm_free_job_info_request_msg ( msg -> data ) ;
			break;
		case REQUEST_PARTITION_INFO:
			slurm_rpc_dump_partitions ( msg ) ;
			slurm_free_last_update_msg ( msg -> data ) ;
			break;
		case REQUEST_RESOURCE_ALLOCATION :
			slurm_rpc_allocate_resources ( msg, false ) ;
			slurm_free_job_desc_msg ( msg -> data ) ;
			break;
		case REQUEST_IMMEDIATE_RESOURCE_ALLOCATION :
			slurm_rpc_allocate_resources ( msg, true ) ;
			slurm_free_job_desc_msg ( msg -> data ) ;
			break;
		case REQUEST_ALLOCATION_AND_RUN_JOB_STEP :
			slurm_rpc_allocate_and_run ( msg );
			slurm_free_job_desc_msg ( msg -> data ) ;
			break;
		case REQUEST_OLD_JOB_RESOURCE_ALLOCATION :
			slurm_rpc_old_job_alloc ( msg );
			break;
		case REQUEST_JOB_WILL_RUN :
			slurm_rpc_job_will_run ( msg -> data ) ;
			slurm_free_job_desc_msg ( msg -> data ) ;
			break;
		case REQUEST_CANCEL_JOB_STEP:
			slurm_rpc_job_step_cancel ( msg ) ;
			slurm_free_job_step_id_msg ( msg -> data ) ;
			break;
		case REQUEST_COMPLETE_JOB_STEP:
			slurm_rpc_job_step_complete ( msg ) ;
			slurm_free_job_step_id_msg ( msg -> data ) ;
			break;
		case REQUEST_SUBMIT_BATCH_JOB: 
			slurm_rpc_submit_batch_job ( msg ) ;
			slurm_free_job_desc_msg ( msg -> data ) ; 
			break;
		case MESSAGE_NODE_REGISTRATION_STATUS:
			slurm_rpc_node_registration ( msg ) ;
			slurm_free_node_registration_status_msg ( msg -> data ) ;
			break;
		case REQUEST_RECONFIGURE:
			slurm_rpc_reconfigure_controller ( msg ) ;
			break;
		case REQUEST_SHUTDOWN:
			slurm_rpc_shutdown_controller ( msg ) ;
			slurm_free_shutdown_msg ( msg -> data ) ;
			break;
		case REQUEST_SHUTDOWN_IMMEDIATE:
			slurm_rpc_shutdown_controller_immediate ( msg ) ;
			break;
		case REQUEST_PING:
			slurm_rpc_ping ( msg ) ;
			break;
		case REQUEST_UPDATE_JOB:
			slurm_rpc_update_job ( msg ) ;
			slurm_free_job_desc_msg ( msg -> data ) ;
			break;
		case REQUEST_UPDATE_NODE:
			slurm_rpc_update_node ( msg ) ;
			slurm_free_update_node_msg ( msg -> data ) ;
			break;
		case REQUEST_JOB_STEP_CREATE:
			slurm_rpc_job_step_create( msg ) ;	
			slurm_free_job_step_create_request_msg( msg->data );
			break;
		case REQUEST_JOB_STEP_INFO:
			slurm_rpc_job_step_get_info ( msg );
			slurm_free_job_step_info_request_msg( msg -> data );
			break;
		case REQUEST_UPDATE_PARTITION:
			slurm_rpc_update_partition ( msg ) ;
			slurm_free_update_part_msg ( msg -> data ) ;
			break;
		default:
			error ("invalid RPC message type %d\n", msg-> msg_type);
			slurm_send_rc_msg ( msg , EINVAL );
			break;
	}
	slurm_free_msg ( msg ) ;
}

/* slurm_rpc_dump_build - process RPC for Slurm configuration information */
void
slurm_rpc_dump_build ( slurm_msg_t * msg )
{
	clock_t start_time;
	slurm_msg_t response_msg ;
	last_update_msg_t * last_time_msg = ( last_update_msg_t * ) msg-> data ;
	slurm_ctl_conf_info_msg_t build_tbl ;
	/* Locks: Read config */
	slurmctld_lock_t config_read_lock = { READ_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };

	start_time = clock ();
	debug ("Processing RPC: REQUEST BUILD_INFO");
	lock_slurmctld (config_read_lock);
	
	/* check to see if configuration data has changed */	
	if ( last_time_msg -> last_update >= slurmctld_conf.last_update )
	{
		unlock_slurmctld (config_read_lock);
		info ("slurm_rpc_dump_build, no change, time=%ld", 
			(long) (clock () - start_time));
		slurm_send_rc_msg ( msg , SLURM_NO_CHANGE_IN_DATA );
	}
	else
	{
		fill_ctld_conf ( & build_tbl ) ;
		unlock_slurmctld (config_read_lock);

		/* init response_msg structure */
		response_msg . address = msg -> address ;
		response_msg . msg_type = RESPONSE_BUILD_INFO ;
		response_msg . data = & build_tbl ;

		/* send message */
		info ("slurm_rpc_dump_build time=%ld", (long) (clock () - start_time));
		slurm_send_node_msg( msg -> conn_fd , &response_msg ) ;
	}
}

/* slurm_rpc_dump_jobs - process RPC for job state information */
void
slurm_rpc_dump_jobs ( slurm_msg_t * msg )
{
	clock_t start_time;
	char *dump;
	int dump_size;
	slurm_msg_t response_msg ;
	job_info_request_msg_t * last_time_msg = ( job_info_request_msg_t * ) msg-> data ;
	time_t last_update = last_time_msg -> last_update ;
	/* Locks: Read job */
	slurmctld_lock_t job_read_lock = { NO_LOCK, READ_LOCK, NO_LOCK, NO_LOCK };

	start_time = clock ();
	debug ("Processing RPC: REQUEST_JOB_INFO");
	lock_slurmctld (job_read_lock);

	if ( last_time_msg -> last_update >= last_job_update )
	{
		unlock_slurmctld (job_read_lock);
		info ("slurm_rpc_dump_jobs, no change, time=%ld", 
			(long) (clock () - start_time));
		slurm_send_rc_msg ( msg , SLURM_NO_CHANGE_IN_DATA );
	}
	else
	{
		pack_all_jobs (&dump, &dump_size, &last_update);
		unlock_slurmctld (job_read_lock);

		/* init response_msg structure */
		response_msg . address = msg -> address ;
		response_msg . msg_type = RESPONSE_JOB_INFO ;
		response_msg . data = dump ;
		response_msg . data_size = dump_size ;

		/* send message */
		slurm_send_node_msg( msg -> conn_fd , &response_msg ) ;
		info ("slurm_rpc_dump_jobs, size=%d, time=%ld", 
		      dump_size, (long) (clock () - start_time));
		if (dump)
			xfree (dump);
	}
}

/* slurm_rpc_dump_nodes - process RPC for node state information */
void
slurm_rpc_dump_nodes ( slurm_msg_t * msg )
{
	clock_t start_time;
	char *dump;
	int dump_size;
	slurm_msg_t response_msg ;
	last_update_msg_t * last_time_msg = ( last_update_msg_t * ) msg-> data ;
	time_t last_update = last_time_msg -> last_update ;
	/* Locks: Read node */
	slurmctld_lock_t node_read_lock = { NO_LOCK, NO_LOCK, READ_LOCK, NO_LOCK };

	start_time = clock ();
	debug ("Processing RPC: REQUEST_NODE_INFO");
	lock_slurmctld (node_read_lock);

	if ( last_time_msg -> last_update >= last_node_update )
	{
		unlock_slurmctld (node_read_lock);
		info ("slurm_rpc_dump_nodes, no change, time=%ld", 
			(long) (clock () - start_time));
		slurm_send_rc_msg ( msg , SLURM_NO_CHANGE_IN_DATA );
	}
	else
	{
		pack_all_node (&dump, &dump_size, &last_update);
		unlock_slurmctld (node_read_lock);

		/* init response_msg structure */
		response_msg . address = msg -> address ;
		response_msg . msg_type = RESPONSE_NODE_INFO ;
		response_msg . data = dump ;
		response_msg . data_size = dump_size ;

		/* send message */
		slurm_send_node_msg( msg -> conn_fd , &response_msg ) ;
		info ("slurm_rpc_dump_nodes, size=%d, time=%ld", 
		      dump_size, (long) (clock () - start_time));
		if (dump)
			xfree (dump);
	}
}

/* slurm_rpc_dump_partitions - process RPC for partition state information */
void
slurm_rpc_dump_partitions ( slurm_msg_t * msg )
{
	clock_t start_time;
	char *dump;
	int dump_size;
	slurm_msg_t response_msg ;
	last_update_msg_t * last_time_msg = ( last_update_msg_t * ) msg-> data ;
	time_t last_update = last_time_msg -> last_update ;
	/* Locks: Read partition */
	slurmctld_lock_t part_read_lock = { NO_LOCK, NO_LOCK, NO_LOCK, READ_LOCK };

	start_time = clock ();
	debug ("Processing RPC: REQUEST_PARTITION_INFO");
	lock_slurmctld (part_read_lock);

	if ( last_time_msg -> last_update >= last_part_update )
	{
		unlock_slurmctld (part_read_lock);
		info ("slurm_rpc_dump_partitions, no change, time=%ld", 
			(long) (clock () - start_time));
		slurm_send_rc_msg ( msg , SLURM_NO_CHANGE_IN_DATA );
	}
	else
	{
		pack_all_part (&dump, &dump_size, &last_update);
		unlock_slurmctld (part_read_lock);

		/* init response_msg structure */
		response_msg . address = msg -> address ;
		response_msg . msg_type = RESPONSE_PARTITION_INFO ;
		response_msg . data = dump ;
		response_msg . data_size = dump_size ;

		/* send message */
		slurm_send_node_msg( msg -> conn_fd , &response_msg ) ;
		info ("slurm_rpc_dump_partitions, size=%d, time=%ld", 
		      dump_size, (long) (clock () - start_time));
		if (dump)
			xfree (dump);
	}
}

/* slurm_rpc_job_step_cancel - process RPC to cancel an entire job or an individual job step */
void 
slurm_rpc_job_step_cancel ( slurm_msg_t * msg )
{
	/* init */
	int error_code = 0;
	clock_t start_time;
	job_step_id_msg_t * job_step_id_msg = ( job_step_id_msg_t * ) msg-> data ;
	/* Locks: Write job, write node */
	slurmctld_lock_t job_write_lock = { NO_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK };
	uid_t uid = 0;
	
	start_time = clock ();
	debug ("Processing RPC: REQUEST_CANCEL_JOB_STEP");
#ifdef	HAVE_AUTHD
	uid = slurm_auth_uid (msg->cred);
#endif
	lock_slurmctld (job_write_lock);

	/* do RPC call */
	if (job_step_id_msg->job_step_id == NO_VAL) {
		error_code = job_cancel ( job_step_id_msg->job_id, uid );
		unlock_slurmctld (job_write_lock);

		/* return result */
		if (error_code)
		{
			info ("slurm_rpc_job_step_cancel error %d for %u, time=%ld",
				error_code, job_step_id_msg->job_id, (long) (clock () - start_time));
			slurm_send_rc_msg ( msg , error_code );
		}
		else
		{
			info ("slurm_rpc_job_step_cancel success for JobId=%u, time=%ld",
				job_step_id_msg->job_id, (long) (clock () - start_time));
			slurm_send_rc_msg ( msg , SLURM_SUCCESS );

			/* Below functions provide their own locking */
			schedule ();
			(void) dump_all_job_state ( );

		}
	}
	else {
		error_code = job_step_cancel (  job_step_id_msg->job_id , 
						job_step_id_msg->job_step_id ,
						uid );
		unlock_slurmctld (job_write_lock);

		/* return result */
		if (error_code)
		{
			info ("slurm_rpc_job_step_cancel error %d for %u.%u, time=%ld", error_code, 
				job_step_id_msg->job_id, job_step_id_msg->job_step_id, 
				(long) (clock () - start_time));
			slurm_send_rc_msg ( msg , error_code );
		}
		else
		{
			info ("slurm_rpc_job_step_cancel success for %u.%u, time=%ld",
				job_step_id_msg->job_id, job_step_id_msg->job_step_id, 
				(long) (clock () - start_time));
			slurm_send_rc_msg ( msg , SLURM_SUCCESS );

			/* Below function provides its own locking */
			(void) dump_all_job_state ( );
		}
	}
}

/* slurm_rpc_job_step_complete - process RPC to note the completion an entire job or 
 *	an individual job step */
void 
slurm_rpc_job_step_complete ( slurm_msg_t * msg )
{
	int error_code;
	clock_t start_time;
	job_step_id_msg_t * job_step_id_msg = ( job_step_id_msg_t * ) msg-> data ;
	/* Locks: Write job, write node */
	slurmctld_lock_t job_write_lock = { NO_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK };
	uid_t uid = 0;

	/* init */
	start_time = clock ();
	debug ("Processing RPC: REQUEST_COMPLETE_JOB_STEP");
#ifdef	HAVE_AUTHD
	uid = slurm_auth_uid (msg->cred);
#endif
	lock_slurmctld (job_write_lock);

	/* do RPC call */
	if (job_step_id_msg->job_step_id == NO_VAL) {
		error_code = job_complete ( job_step_id_msg->job_id, uid );
		unlock_slurmctld (job_write_lock);

		/* return result */
		if (error_code)
		{
			info ("slurm_rpc_job_step_complete error %d for %u, time=%ld",
				error_code, job_step_id_msg->job_id, (long) (clock () - start_time));
			slurm_send_rc_msg ( msg , error_code );
		}
		else
		{
			info ("slurm_rpc_job_step_complete success for JobId=%u, time=%ld",
				job_step_id_msg->job_id, (long) (clock () - start_time));
			slurm_send_rc_msg ( msg , SLURM_SUCCESS );
			schedule ();		/* Has own locking */
			(void) dump_all_job_state ();	/* Has own locking */
		}
	}
	else {
		error_code = job_step_complete (  job_step_id_msg->job_id, 
						job_step_id_msg->job_step_id, uid);
		unlock_slurmctld (job_write_lock);

		/* return result */
		if (error_code)
		{
			info ("slurm_rpc_job_step_complete error %d for %u.%u, time=%ld", error_code, 
				job_step_id_msg->job_id, job_step_id_msg->job_step_id, 
				(long) (clock () - start_time));
			slurm_send_rc_msg ( msg , error_code );
		}
		else
		{
			info ("slurm_rpc_job_step_complete success for %u.%u, time=%ld",
				job_step_id_msg->job_id, job_step_id_msg->job_step_id, 
				(long) (clock () - start_time));
			slurm_send_rc_msg ( msg , SLURM_SUCCESS );
			(void) dump_all_job_state ( );	/* Has own locking */
		}
	}
}

void 
slurm_rpc_job_step_get_info ( slurm_msg_t * msg ) 
{
	clock_t start_time;
	void* resp_buffer = NULL;
	int resp_buffer_size = 0;
	int error_code = 0;
	job_step_info_request_msg_t* request = ( job_step_info_request_msg_t * ) msg-> data ;
	/* Locks: Read job */
	slurmctld_lock_t job_read_lock = { NO_LOCK, READ_LOCK, NO_LOCK, NO_LOCK };

	start_time = clock ();
	debug ("Processing RPC: REQUEST_JOB_STEP_INFO");
	lock_slurmctld (job_read_lock);

	if ( request -> last_update >= last_job_update )
	{
		unlock_slurmctld (job_read_lock);
		info ("slurm_rpc_job_step_get_info, no change, time=%ld", 
			(long) (clock () - start_time));
		error_code = SLURM_NO_CHANGE_IN_DATA;
	}
	else {
		Buf buffer;
		buffer = init_buf (BUF_SIZE);
		error_code = pack_ctld_job_step_info_response_msg (request->job_id, request->step_id, buffer);
		unlock_slurmctld (job_read_lock);
		resp_buffer_size = get_buf_offset (buffer);
		resp_buffer = xfer_buf_data (buffer);
		if (error_code == ESLURM_INVALID_JOB_ID)
			info ("slurm_rpc_job_step_get_info, no such job step %u.%u, time=%ld", 
				request->job_id, request->step_id, (long) (clock () - start_time));
		else if (error_code)
			error ("slurm_rpc_job_step_get_info, error %d, time=%ld", 
				error_code, (long) (clock () - start_time));
	}

	if ( error_code )
		slurm_send_rc_msg ( msg , error_code );
	else {
		slurm_msg_t response_msg ;
	
		info ("slurm_rpc_job_step_get_info, size=%d, time=%ld", 
		      resp_buffer_size, (long) (clock () - start_time));
		response_msg . address = msg -> address ;
		response_msg . msg_type = RESPONSE_JOB_STEP_INFO;
		response_msg . data = resp_buffer ;
		response_msg . data_size = resp_buffer_size ;
		slurm_send_node_msg( msg->conn_fd , &response_msg ) ;

	}
}

/* slurm_rpc_update_job - process RPC to update the configuration of a job (e.g. priority) */
void 
slurm_rpc_update_job ( slurm_msg_t * msg )
{
	/* init */
	int error_code;
	clock_t start_time;
	job_desc_msg_t * job_desc_msg = ( job_desc_msg_t * ) msg-> data ;
	/* Locks: Write job, read node, read partition */
	slurmctld_lock_t job_write_lock = { NO_LOCK, WRITE_LOCK, READ_LOCK, READ_LOCK };
	uid_t uid = 0;

	start_time = clock ();
	debug ("Processing RPC: REQUEST_UPDATE_JOB");
	lock_slurmctld (job_write_lock);
	unlock_slurmctld (job_write_lock);

	/* do RPC call */
#ifdef	HAVE_AUTHD
	uid = slurm_auth_uid (msg->cred);
#endif
	error_code = update_job ( job_desc_msg, uid );

	/* return result */
	if (error_code)
	{
		error ("slurm_rpc_update_job error %d for job id %u, time=%ld",
				error_code, job_desc_msg->job_id, 
				(long) (clock () - start_time));
		slurm_send_rc_msg ( msg , error_code );
	}
	else
	{
		info ("slurm_rpc_update_job complete for job id %u, time=%ld",
				job_desc_msg->job_id, 
				(long) (clock () - start_time));
		slurm_send_rc_msg ( msg , SLURM_SUCCESS );
		/* Below functions provide their own locking */
		schedule ();
		(void) dump_all_job_state ();
	}
}

/* slurm_rpc_update_node - process RPC to update the configuration of a node (e.g. UP/DOWN) */
void 
slurm_rpc_update_node ( slurm_msg_t * msg )
{
	/* init */
	int error_code = 0;
	clock_t start_time;
	update_node_msg_t * update_node_msg_ptr = (update_node_msg_t * ) msg-> data ;
	/* Locks: Write node */
	slurmctld_lock_t node_write_lock = { NO_LOCK, NO_LOCK, WRITE_LOCK, NO_LOCK };
#ifdef	HAVE_AUTHD
	uid_t uid = 0;
#endif

	start_time = clock ();
	debug ("Processing RPC: REQUEST_UPDATE_NODE");
#ifdef	HAVE_AUTHD
	uid = slurm_auth_uid (msg->cred);
	if ( (uid != 0) && (uid != getuid ()) ) {
		error_code = ESLURM_USER_ID_MISSING;
		error ("Security violation, UPDATE_NODE RPC from uid %u", (unsigned int) uid);
	}
#endif

	if (error_code == 0) {
		/* do RPC call */
		lock_slurmctld (node_write_lock);
		error_code = update_node ( update_node_msg_ptr );
		unlock_slurmctld (node_write_lock);			
	}

	/* return result */
	if (error_code)
	{
		error ("slurm_rpc_update_node error %d for node %s, time=%ld",
				error_code, update_node_msg_ptr->node_names, 
				(long) (clock () - start_time));
		slurm_send_rc_msg ( msg , error_code );
	}
	else
	{
		info ("slurm_rpc_update_node complete for node %s, time=%ld",
				update_node_msg_ptr->node_names, 
				(long) (clock () - start_time));
		slurm_send_rc_msg ( msg , SLURM_SUCCESS );
	}

	/* Below functions provide their own locks */
	if (schedule ())
		(void) dump_all_job_state ();
	(void) dump_all_node_state ();
}

/* slurm_rpc_update_partition - process RPC to update the configuration of a partition (e.g. UP/DOWN) */
void 
slurm_rpc_update_partition ( slurm_msg_t * msg )
{
	/* init */
	int error_code = 0;
	clock_t start_time;
	update_part_msg_t * part_desc_ptr = (update_part_msg_t * ) msg-> data ;
	/* Locks: Read node, write partition */
	slurmctld_lock_t part_write_lock = { NO_LOCK, NO_LOCK, READ_LOCK, WRITE_LOCK };
#ifdef	HAVE_AUTHD
	uid_t uid = 0;
#endif

	start_time = clock ();
	debug ("Processing RPC: REQUEST_UPDATE_PARTITION");
#ifdef	HAVE_AUTHD
	uid = slurm_auth_uid (msg->cred);
	if ( (uid != 0) && (uid != getuid ()) ) {
		error_code = ESLURM_USER_ID_MISSING;
		error ("Security violation, UPDATE_PARTITION RPC from uid %u", (unsigned int) uid);
	}
#endif

	if (error_code == 0) {
		/* do RPC call */
		lock_slurmctld (part_write_lock);
		error_code = update_part ( part_desc_ptr );
		unlock_slurmctld (part_write_lock);
	}

	/* return result */
	if (error_code)
	{
		error ("slurm_rpc_update_partition error %d for partition %s, time=%ld",
				error_code, part_desc_ptr->name, (long) (clock () - start_time));
		slurm_send_rc_msg ( msg , error_code );
	}
	else
	{
		info ("slurm_rpc_update_partition complete for partition %s, time=%ld",
				part_desc_ptr->name, (long) (clock () - start_time));
		slurm_send_rc_msg ( msg , SLURM_SUCCESS );

		/* NOTE: These functions provide their own locks */
		(void) dump_all_part_state ();
		if (schedule ())
			(void) dump_all_job_state ();
	}
}

/* slurm_rpc_submit_batch_job - process RPC to submit a batch job */
void
slurm_rpc_submit_batch_job ( slurm_msg_t * msg )
{
	/* init */
	int error_code = 0;
	clock_t start_time;
	uint32_t job_id ;
	slurm_msg_t response_msg ;
	submit_response_msg_t submit_msg ;
	job_desc_msg_t * job_desc_msg = ( job_desc_msg_t * ) msg-> data ;
	/* Locks: Write job, read node, read partition */
	slurmctld_lock_t job_write_lock = { NO_LOCK, WRITE_LOCK, READ_LOCK, READ_LOCK };
	uid_t uid = 0;

	start_time = clock ();
	debug ("Processing RPC: REQUEST_SUBMIT_BATCH_JOB");

	/* do RPC call */
	dump_job_desc(job_desc_msg);
#ifdef	HAVE_AUTHD
	uid = slurm_auth_uid (msg->cred);
	if ( (uid != job_desc_msg->user_id) &&
	     (uid != 0) && (uid != getuid ()) ) {
		error_code = ESLURM_USER_ID_MISSING;
		error ("Security violation, SUBMIT_JOB from uid %u", (unsigned int) uid);
	}
#endif
	if (error_code == 0) {
		lock_slurmctld (job_write_lock);
		error_code = job_allocate (job_desc_msg, &job_id, (char **) NULL, 
			(uint16_t *) NULL, (uint32_t **) NULL, (uint32_t **) NULL,
			false, false, false, uid );
		unlock_slurmctld (job_write_lock);
	}

	/* return result */
	if (error_code)
	{
		info ("slurm_rpc_submit_batch_job error %d, time=%ld",
				error_code, (long) (clock () - start_time));
		slurm_send_rc_msg ( msg , error_code );
	}
	else
	{
		info ("slurm_rpc_submit_batch_job success for id=%u, time=%ld",
				job_id, (long) (clock () - start_time));
		/* send job_ID */
		submit_msg . job_id = job_id ;
		response_msg . msg_type = RESPONSE_SUBMIT_BATCH_JOB ;
		response_msg . data = & submit_msg ;
		slurm_send_node_msg ( msg->conn_fd , & response_msg ) ;
		schedule ();			/* has own locks */
		(void) dump_all_job_state ();	/* has own locks */
	}
}

/* slurm_rpc_allocate_resources:  process RPC to allocate resources for a job */
void 
slurm_rpc_allocate_resources ( slurm_msg_t * msg , uint8_t immediate )
{
	/* init */
	int error_code = 0;
	slurm_msg_t response_msg ;
	clock_t start_time;
	job_desc_msg_t * job_desc_msg = ( job_desc_msg_t * ) msg-> data ;
	char * node_list_ptr = NULL;
	uint16_t num_cpu_groups = 0;
	uint32_t * cpus_per_node = NULL, * cpu_count_reps = NULL;
	uint32_t job_id ;
	resource_allocation_response_msg_t alloc_msg ;
	/* Locks: Write job, write node, read partition */
	slurmctld_lock_t job_write_lock = { NO_LOCK, WRITE_LOCK, WRITE_LOCK, READ_LOCK };
	uid_t uid = 0;

	start_time = clock ();
	if (immediate)
		debug ("Processing RPC: REQUEST_IMMEDIATE_RESOURCE_ALLOCATION");
	else
		debug ("Processing RPC: REQUEST_RESOURCE_ALLOCATION");

	/* do RPC call */
	dump_job_desc (job_desc_msg);
#ifdef	HAVE_AUTHD
	uid = slurm_auth_uid (msg->cred);
	if ( (uid != job_desc_msg->user_id) && 
	     (uid != 0) && (uid != getuid ()) ) {
		error_code = ESLURM_USER_ID_MISSING;
		error ("Security violation, RESOURCE_ALLOCATE from uid %u", (unsigned int) uid);
	}	
#endif
	if (error_code == 0) {
		lock_slurmctld (job_write_lock);
		error_code = job_allocate (job_desc_msg, &job_id, 
			&node_list_ptr, &num_cpu_groups, &cpus_per_node, &cpu_count_reps, 
			immediate , false, true, uid );
		unlock_slurmctld (job_write_lock);
	}

	/* return result */
	if (error_code)
	{
		info ("slurm_rpc_allocate_resources error %d allocating resources, time=%ld",
				error_code,  (long) (clock () - start_time));
		slurm_send_rc_msg ( msg , error_code );
	}
	else
	{
		info ("slurm_rpc_allocate_resources allocated nodes %s to JobId=%u, time=%ld",
				node_list_ptr , job_id , 	
				(long) (clock () - start_time));
		
		/* send job_ID  and node_name_ptr */

		alloc_msg . job_id = job_id ;
		alloc_msg . node_list = node_list_ptr ;
		alloc_msg . num_cpu_groups = num_cpu_groups;
		alloc_msg . cpus_per_node  = cpus_per_node;
		alloc_msg . cpu_count_reps = cpu_count_reps;
		response_msg . msg_type = ( immediate ) ? 
				RESPONSE_IMMEDIATE_RESOURCE_ALLOCATION : RESPONSE_RESOURCE_ALLOCATION ;
		response_msg . data =  & alloc_msg ;

		slurm_send_node_msg ( msg->conn_fd , & response_msg ) ;
		(void) dump_all_job_state ( );
	}
}

/* slurm_rpc_allocate_and_run: process RPC to allocate resources for a job and
 *	initiate a job step */
void
slurm_rpc_allocate_and_run ( slurm_msg_t * msg )
{
        /* init */
        int error_code = 0;
        slurm_msg_t response_msg ;
        clock_t start_time;
        job_desc_msg_t * job_desc_msg = ( job_desc_msg_t * ) msg-> data ;
        char * node_list_ptr = NULL;
        uint16_t num_cpu_groups = 0;
        uint32_t * cpus_per_node = NULL, * cpu_count_reps = NULL;
        uint32_t job_id ;
        resource_allocation_and_run_response_msg_t alloc_msg ;
	struct step_record* step_rec; 
	job_step_create_request_msg_t req_step_msg;
	/* Locks: Write job, write node, read partition */
	slurmctld_lock_t job_write_lock = { NO_LOCK, WRITE_LOCK, WRITE_LOCK, READ_LOCK };
	uid_t uid = 0;

        start_time = clock ();
	debug ("Processing RPC: REQUEST_ALLOCATE_AND_RUN_JOB_STEP");

        /* do RPC call */
        dump_job_desc (job_desc_msg);
#ifdef	HAVE_AUTHD
	uid = slurm_auth_uid (msg->cred);
	if ( (uid != job_desc_msg->user_id) &&
	     (uid != 0) && (uid != getuid ()) ) {
		error_code = ESLURM_USER_ID_MISSING;
		error ("Security violation, ALLOCATE_AND_RUN RPC from uid %u", (unsigned int) uid);
	}
#endif
	if (error_code == 0) {
		lock_slurmctld (job_write_lock);
        	error_code = job_allocate(job_desc_msg, &job_id,
                        &node_list_ptr, &num_cpu_groups, &cpus_per_node, &cpu_count_reps,
                        true , false, true, uid );
	}

        /* return result */
        if (error_code) {
		unlock_slurmctld (job_write_lock);
                info ("slurm_rpc_allocate_and_run error %d allocating resources, time=%ld",
                        error_code,  (long) (clock () - start_time));
                slurm_send_rc_msg ( msg , error_code );
		return;
	}

	req_step_msg . job_id = job_id;
	req_step_msg . user_id = job_desc_msg -> user_id;
	req_step_msg . node_count = INFINITE;
	error_code = step_create ( &req_step_msg, &step_rec );
	/* note: no need to free step_rec, pointer to global job step record */
	if ( error_code ) {
		unlock_slurmctld (job_write_lock);
		info ("slurm_rpc_allocate_and_run error %d creating job step, time=%ld",
			error_code,  (long) (clock () - start_time));
		slurm_send_rc_msg ( msg , error_code );
	}
	else {

		info ("slurm_rpc_allocate_and_run allocated nodes %s to JobId=%u, time=%ld",
                        node_list_ptr , job_id , (long) (clock () - start_time));

		/* send job_ID  and node_name_ptr */
                alloc_msg . job_id = job_id ;
	        alloc_msg . node_list = node_list_ptr ;
	        alloc_msg . num_cpu_groups = num_cpu_groups;
	        alloc_msg . cpus_per_node  = cpus_per_node;
	        alloc_msg . cpu_count_reps = cpu_count_reps;
		alloc_msg . job_step_id = step_rec->step_id;
		alloc_msg . credentials = & step_rec-> job_ptr-> details-> credential ;
#ifdef HAVE_LIBELAN3
		alloc_msg . qsw_job =  step_rec-> qsw_job ;
#endif
	        response_msg . msg_type = RESPONSE_ALLOCATION_AND_RUN_JOB_STEP;
                response_msg . data =  & alloc_msg ;

		unlock_slurmctld (job_write_lock);
		slurm_send_node_msg ( msg->conn_fd , & response_msg ) ;
		(void) dump_all_job_state ( );	/* Has its own locks */
	}
}

/* slurm_rpc_old_job_alloc - process RPC to get details on existing job */
void slurm_rpc_old_job_alloc ( slurm_msg_t * msg )
{
	int error_code = 0;
	slurm_msg_t response_msg ;
	clock_t start_time;
	old_job_alloc_msg_t * job_desc_msg = ( old_job_alloc_msg_t * ) msg-> data ;
	char * node_list_ptr = NULL;
	uint16_t num_cpu_groups = 0;
	uint32_t * cpus_per_node = NULL, * cpu_count_reps = NULL;
	resource_allocation_response_msg_t alloc_msg ;
	/* Locks: Read job, read node */
	slurmctld_lock_t job_read_lock = { NO_LOCK, READ_LOCK, READ_LOCK, NO_LOCK };
	uid_t uid = 0;

	start_time = clock ();
	debug ("Processing RPC: REQUEST_OLD_JOB_RESOURCE_ALLOCATION");

	/* do RPC call */
#ifdef	HAVE_AUTHD
	uid = slurm_auth_uid (msg->cred);
	if ( (uid != job_desc_msg->uid) && 
	     (uid != 0) && (uid != getuid ()) ) {
		error_code = ESLURM_USER_ID_MISSING;
		error ("Security violation, RESOURCE_ALLOCATE from uid %u", (unsigned int) uid);
	}	
#endif
	if (error_code == 0) {
		lock_slurmctld (job_read_lock);
		error_code = old_job_info (job_desc_msg->uid, job_desc_msg->job_id, 
			&node_list_ptr, &num_cpu_groups, &cpus_per_node, &cpu_count_reps );
		unlock_slurmctld (job_read_lock);
	}

	/* return result */
	if (error_code)
	{
		info ("slurm_rpc_old_job_alloc error %d getting info, job=%u, uid=%u, time=%ld",
				error_code, job_desc_msg->job_id, job_desc_msg->uid,
				(long) (clock () - start_time));
		slurm_send_rc_msg ( msg , error_code );
	}
	else
	{
		info ("slurm_rpc_old_job_alloc job=%u has nodes %s, time=%ld",
				job_desc_msg->job_id, node_list_ptr,  	
				(long) (clock () - start_time));
		
		/* send job_ID  and node_name_ptr */

		alloc_msg . job_id = job_desc_msg->job_id ;
		alloc_msg . node_list = node_list_ptr ;
		alloc_msg . num_cpu_groups = num_cpu_groups;
		alloc_msg . cpus_per_node  = cpus_per_node;
		alloc_msg . cpu_count_reps = cpu_count_reps;
		response_msg . msg_type = RESPONSE_RESOURCE_ALLOCATION ;
		response_msg . data =  & alloc_msg ;

		slurm_send_node_msg ( msg->conn_fd , & response_msg ) ;
		(void) dump_all_job_state ( );
	}
}

/* slurm_rpc_job_will_run - process RPC to determine if job with given configuration can be initiated */
void slurm_rpc_job_will_run ( slurm_msg_t * msg )
{
	/* init */
	int error_code = 0;
	clock_t start_time;
	uint16_t num_cpu_groups = 0;
	uint32_t * cpus_per_node = NULL, * cpu_count_reps = NULL;
	uint32_t job_id ;
	job_desc_msg_t * job_desc_msg = ( job_desc_msg_t * ) msg-> data ;
	char * node_list_ptr = NULL;
	/* Locks: Write job, read node, read partition */
	slurmctld_lock_t job_write_lock = { NO_LOCK, WRITE_LOCK, READ_LOCK, READ_LOCK };
	uid_t uid = 0;

	start_time = clock ();
	debug ("Processing RPC: REQUEST_JOB_WILL_RUN");

	/* do RPC call */
	dump_job_desc(job_desc_msg);
#ifdef	HAVE_AUTHD
	uid = slurm_auth_uid (msg->cred);
	if ( (uid != job_desc_msg->user_id) &&
	     (uid != 0) && (uid != getuid ()) ) {
		error_code = ESLURM_USER_ID_MISSING;
		error ("Security violation, JOB_WILL_RUN RPC from uid %u", (unsigned int) uid);
	}
#endif

	if (error_code == 0) {
		lock_slurmctld (job_write_lock);
		error_code = job_allocate(job_desc_msg, &job_id, 
			&node_list_ptr, &num_cpu_groups, &cpus_per_node, &cpu_count_reps, 
			false , true, true, uid );
		unlock_slurmctld (job_write_lock);
	}

	/* return result */
	if (error_code) {
		info ("slurm_rpc_job_will_run error %d, time=%ld",
				error_code, (long) (clock () - start_time));
		slurm_send_rc_msg ( msg , error_code );
	}
	else
	{
		info ("slurm_rpc_job_will_run success for , time=%ld",
				(long) (clock () - start_time));
		slurm_send_rc_msg ( msg , SLURM_SUCCESS );
	}
}

/* slurm_rpc_ping - process ping RPC */
void 
slurm_rpc_ping ( slurm_msg_t * msg )
{
	/* init */
	int error_code = SLURM_SUCCESS;
#ifdef HAVE_AUTHD
	uid_t uid = 0;
#endif

#ifdef	HAVE_AUTHD
	uid = slurm_auth_uid (msg->cred);
	if ( (uid != 0) && (uid != getuid ()) ) {
		error ("Security violation, PING RPC from uid %u", (unsigned int) uid);
		error_code = ESLURM_USER_ID_MISSING;
	}
#endif

	/* return result */
	slurm_send_rc_msg ( msg , error_code );
}


/* slurm_rpc_reconfigure_controller - process RPC to re-initialize slurmctld from configuration file */
void 
slurm_rpc_reconfigure_controller ( slurm_msg_t * msg )
{
	/* init */
	int error_code = 0;
	clock_t start_time;
	/* Locks: Write configuration, write job, write node, write partition */
	slurmctld_lock_t config_write_lock = { WRITE_LOCK, WRITE_LOCK, WRITE_LOCK, WRITE_LOCK };
#ifdef HAVE_AUTHD
	uid_t uid = 0;
#endif

	start_time = clock ();
	debug ("Processing RPC: REQUEST_RECONFIGURE");
#ifdef	HAVE_AUTHD
	uid = slurm_auth_uid (msg->cred);
	if ( (uid != 0) && (uid != getuid ()) ) {
		error ("Security violation, RECONFIGURE RPC from uid %u", (unsigned int) uid);
		error_code = ESLURM_USER_ID_MISSING;
	}
#endif

	/* do RPC call */
	if (error_code == 0) {
		lock_slurmctld (config_write_lock);
		error_code = read_slurm_conf (0);
		if (error_code == 0)
			reset_job_bitmaps ();
		unlock_slurmctld (config_write_lock);

		if (daemonize) {
			if (chdir (slurmctld_conf.state_save_location))
				fatal ("chdir to %s error %m", slurmctld_conf.state_save_location);
		}
	}
	
	/* return result */
	if (error_code)
	{
		error ("slurm_rpc_reconfigure_controller error %d, time=%ld",
				error_code, (long) (clock () - start_time));
		slurm_send_rc_msg ( msg , error_code );
	}
	else
	{
		info ("slurm_rpc_reconfigure_controller completed successfully, time=%ld", 
				(long) (clock () - start_time));
		slurm_send_rc_msg ( msg , SLURM_SUCCESS );
		schedule ();
		save_all_state ();
	}
}


/* slurm_rpc_shutdown_controller - process RPC to shutdown slurmctld */
void 
slurm_rpc_shutdown_controller ( slurm_msg_t * msg )
{
	int error_code = 0;
	shutdown_msg_t * shutdown_msg = (shutdown_msg_t *) msg->data;
#ifdef	HAVE_AUTHD
	uid_t uid = 0;
#endif

	/* do RPC call */
	debug ("Performing RPC: REQUEST_SHUTDOWN");
#ifdef	HAVE_AUTHD
	uid = slurm_auth_uid (msg->cred);
	if ( (uid != 0) &&  (uid != getuid ()) ) {
		error ("Security violation, SHUTDOWN RPC from uid %u", (unsigned int) uid);
		error_code = ESLURM_USER_ID_MISSING;
	}
#endif

	if (error_code)
		;
	else if (shutdown_msg->core)
		debug3 ("performing immeditate shutdown without state save");
	else if (shutdown_time)
		debug3 ("slurm_rpc_shutdown_controller RPC issued after shutdown in progress");
	else if (thread_id_sig) {
		pthread_kill (thread_id_sig, SIGTERM);	/* tell master to clean-up */
		info ("slurm_rpc_shutdown_controller completed successfully");
	} 
	else {
		error ("thread_id_sig undefined, doing shutdown the hard way");
		shutdown_time = time (NULL);
		/* send REQUEST_SHUTDOWN_IMMEDIATE RPC */
		slurmctld_shutdown ();
	}

	slurm_send_rc_msg ( msg , error_code );
	if ((error_code == 0) && (shutdown_msg->core))
		fatal ("Aborting per RPC request");
}

/* slurm_rpc_shutdown_controller_immediate - process RPC to shutdown slurmctld */
void 
slurm_rpc_shutdown_controller_immediate ( slurm_msg_t * msg )
{
	int error_code = 0;
#ifdef	HAVE_AUTHD
	uid_t uid = 0;

	uid = slurm_auth_uid (msg->cred);
	if ( (uid != 0) && (uid != getuid ()) ) {
		error ("Security violation, SHUTDOWN_IMMEDIATE RPC from uid %u", (unsigned int) uid);
		error_code = ESLURM_USER_ID_MISSING;
	}
#endif

	/* do RPC call */
	/* No op: just used to knock loose accept RPC thread */
	if (error_code == 0)
		debug ("Performing RPC: REQUEST_SHUTDOWN_IMMEDIATE");
}
/* slurm_rpc_job_step_create - process RPC to creates/registers a job step with the step_mgr */
void 
slurm_rpc_job_step_create( slurm_msg_t* msg )
{
	/* init */
	int error_code = 0;
	clock_t start_time;

	slurm_msg_t resp;
	struct step_record* step_rec;
	job_step_create_response_msg_t job_step_resp;
	job_step_create_request_msg_t * req_step_msg = 
			( job_step_create_request_msg_t* ) msg-> data ;
	/* Locks: Write jobs, read nodes */
	slurmctld_lock_t job_write_lock = { NO_LOCK, WRITE_LOCK, READ_LOCK, NO_LOCK };
#ifdef	HAVE_AUTHD
	uid_t uid = 0;
#endif

	start_time = clock ();
	debug ("Processing RPC: REQUEST_JOB_STEP_CREATE");
	dump_step_desc ( req_step_msg );
#ifdef	HAVE_AUTHD
	uid = slurm_auth_uid (msg->cred);
	if ( (uid != req_step_msg->user_id) &&
	     (uid != 0) && (uid != getuid ()) ) {
		error_code = ESLURM_USER_ID_MISSING;
		error ("Security violation, JOB_STEP_CREATE RPC from uid %u", (unsigned int) uid);
	}
#endif

	if (error_code == 0) {
		/* issue the RPC */
		lock_slurmctld (job_write_lock);
		error_code = step_create ( req_step_msg, &step_rec );
	}

	/* return result */
	if ( error_code )
	{
		unlock_slurmctld (job_write_lock);
		info ("slurm_rpc_job_step_create error %s, time=%ld", 
			slurm_strerror( error_code ), (long) (clock () - start_time));
		slurm_send_rc_msg ( msg , error_code );
	}
	else
	{
		/* FIXME Needs to be fixed to really work with a credential */
		//slurm_job_credential_t cred = { 1,1,"test",start_time, "signature"} ;
		info ("slurm_rpc_job_step_create success time=%ld",
				(long) (clock () - start_time));
		
		job_step_resp.job_step_id = step_rec->step_id;
		job_step_resp.node_list = bitmap2node_name( step_rec->node_bitmap );
		job_step_resp.credentials = & step_rec-> job_ptr-> details-> credential ;
				
#ifdef HAVE_LIBELAN3
		job_step_resp.qsw_job = step_rec-> qsw_job ;
#endif
		unlock_slurmctld (job_write_lock);
		resp. address = msg -> address ;
		resp. msg_type = RESPONSE_JOB_STEP_CREATE ;
		resp. data = &job_step_resp  ;

		slurm_send_node_msg ( msg->conn_fd , &resp);
		(void) dump_all_job_state ( );	/* Sets own locks */
	}
}

/* slurm_rpc_node_registration - process RPC to determine if a node's actual configuration satisfies the
 * configured specification */
void 
slurm_rpc_node_registration ( slurm_msg_t * msg )
{
	/* init */
	int error_code = 0;
	clock_t start_time;
	slurm_node_registration_status_msg_t * node_reg_stat_msg = 
			( slurm_node_registration_status_msg_t * ) msg-> data ;
	/* Locks: Write job and node */
	slurmctld_lock_t job_write_lock = { NO_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK };
#ifdef	HAVE_AUTHD
	uid_t uid = 0;
#endif

	start_time = clock ();
	debug ("Processing RPC: MESSAGE_NODE_REGISTRATION_STATUS");
#ifdef	HAVE_AUTHD
	uid = slurm_auth_uid (msg->cred);
	if ( (uid != 0) && (uid != getuid ()) ) {
		error_code = ESLURM_USER_ID_MISSING;
		error ("Security violation,  NODE_REGISTER RPC from uid %u", (unsigned int) uid);
	}
#endif
	if (error_code == 0) {
		/* do RPC call */
		lock_slurmctld (job_write_lock);
		error_code = validate_node_specs (
			node_reg_stat_msg -> node_name ,
			node_reg_stat_msg -> cpus ,
			node_reg_stat_msg -> real_memory_size ,
			node_reg_stat_msg -> temporary_disk_space ,
			node_reg_stat_msg -> job_count ) ;
		validate_jobs_on_node (
			node_reg_stat_msg -> node_name ,
			node_reg_stat_msg -> job_count ,
			node_reg_stat_msg -> job_id ,
			node_reg_stat_msg -> step_id ) ;
		unlock_slurmctld (job_write_lock);
	}

	/* return result */
	if (error_code)
	{
		error ("slurm_rpc_node_registration error %d for %s, time=%ld",
			error_code, node_reg_stat_msg -> node_name, (long) (clock () - start_time));
		slurm_send_rc_msg ( msg , error_code );
	}
	else
	{
		info ("slurm_rpc_node_registration complete for %s, time=%ld",
			node_reg_stat_msg -> node_name, (long) (clock () - start_time));
		slurm_send_rc_msg ( msg , SLURM_SUCCESS );
	}
}

/*
 * slurmctld_shutdown - issue RPC to have slurmctld shutdown, 
 *	knocks loose an slurm_accept_msg_conn() if we have a thread hung there
 */
int
slurmctld_shutdown ()
{
	int rc ;
	slurm_fd sockfd ;
	slurm_msg_t request_msg ;

        /* init message connection for message communication with controller */
	if ( ( sockfd = slurm_open_controller_conn ( ) ) == SLURM_SOCKET_ERROR ) {
		error ("slurm_open_controller_conn error");
		return SLURM_SOCKET_ERROR ;
	}

	/* send request message */
	request_msg . msg_type = REQUEST_SHUTDOWN_IMMEDIATE ;

	if ( ( rc = slurm_send_controller_msg ( sockfd , & request_msg ) ) == SLURM_SOCKET_ERROR ) {
		error ("slurm_send_controller_msg error");
		return SLURM_SOCKET_ERROR ;
	}

	/* no response */

	/* shutdown message connection */
	if ( ( rc = slurm_shutdown_msg_conn ( sockfd ) ) == SLURM_SOCKET_ERROR ) {
		error ("slurm_shutdown_msg_conn error");
		return SLURM_SOCKET_ERROR ;
	}

        return SLURM_PROTOCOL_SUCCESS ;
}

/*
 * init_ctld_conf - set default configuration parameters
 * NOTE: slurmctld and slurmd ports are built thus:
 *	if SLURMCTLD_PORT/SLURMD_PORT are set then
 *		get the port number based upon a look-up in /etc/services
 *		if the lookup fails, translate SLURMCTLD_PORT/SLURMD_PORT into a number
 *	These port numbers are overridden if set in the configuration file
 */
void
init_ctld_conf ( slurm_ctl_conf_t * conf_ptr )
{
	struct servent *servent;

	conf_ptr->last_update		= time (NULL) ;
	conf_ptr->backup_controller   	= NULL ;
	conf_ptr->control_machine    	= NULL ;
	conf_ptr->epilog           	= NULL ;
	conf_ptr->fast_schedule     	= 1 ;
	conf_ptr->hash_base         	= 10 ;
	conf_ptr->heartbeat_interval	= 30;
	conf_ptr->inactive_limit	= 0;		/* unlimited */
	conf_ptr->kill_wait         	= 30 ;
	conf_ptr->prioritize        	= NULL ;
	conf_ptr->prolog            	= NULL ;
	conf_ptr->ret2service           = 0 ;
	conf_ptr->slurmctld_timeout   	= 300 ;
	conf_ptr->slurmd_timeout   	= 300 ;
	conf_ptr->slurm_conf       	= SLURM_CONFIG_FILE ;
	conf_ptr->state_save_location   = xstrdup (DEFAULT_TMP_FS) ;
	conf_ptr->tmp_fs            	= xstrdup (DEFAULT_TMP_FS) ;

	servent = getservbyname (SLURMCTLD_PORT, NULL);
	if (servent)
		conf_ptr->slurmctld_port   = servent -> s_port;
	else
		conf_ptr->slurmctld_port   = strtol (SLURMCTLD_PORT, (char **) NULL, 10);
	endservent ();

	servent = getservbyname (SLURMD_PORT, NULL);
	if (servent)
		conf_ptr->slurmd_port   = servent -> s_port;
	else
		conf_ptr->slurmd_port   = strtol (SLURMD_PORT, (char **) NULL, 10);
	endservent ();
}


void
fill_ctld_conf ( slurm_ctl_conf_t * conf_ptr )
{
	conf_ptr->last_update		= slurmctld_conf.last_update ;
	conf_ptr->backup_controller   	= slurmctld_conf.backup_controller ;
	conf_ptr->control_machine    	= slurmctld_conf.control_machine ;
	conf_ptr->epilog           	= slurmctld_conf.epilog ;
	conf_ptr->fast_schedule     	= slurmctld_conf.fast_schedule ;
	conf_ptr->hash_base         	= slurmctld_conf.hash_base ;
	conf_ptr->heartbeat_interval	= slurmctld_conf.heartbeat_interval;
	conf_ptr->inactive_limit	= slurmctld_conf.inactive_limit;
	conf_ptr->kill_wait         	= slurmctld_conf.kill_wait ;
	conf_ptr->prioritize        	= slurmctld_conf.prioritize ;
	conf_ptr->prolog            	= slurmctld_conf.prolog ;
	conf_ptr->ret2service           = slurmctld_conf.ret2service ;
	conf_ptr->slurmctld_port   	= slurmctld_conf.slurmctld_port ;
	conf_ptr->slurmctld_timeout   	= slurmctld_conf.slurmctld_timeout ;
	conf_ptr->slurmd_port   	= slurmctld_conf.slurmd_port ;
	conf_ptr->slurmd_timeout   	= slurmctld_conf.slurmd_timeout ;
	conf_ptr->slurm_conf       	= slurmctld_conf.slurm_conf ;
	conf_ptr->state_save_location   = slurmctld_conf.state_save_location ;
	conf_ptr->tmp_fs            	= slurmctld_conf.tmp_fs ;
	return;
}

/* Variables for commandline passing using getopt */
extern char *optarg;
extern int optind, opterr, optopt;

/* parse_commandline - parse and process any command line arguments */
void 
parse_commandline( int argc, char* argv[], slurm_ctl_conf_t * conf_ptr )
{
	int c = 0, errlev;
	char *log_file = NULL;

	opterr = 0;
	while ((c = getopt (argc, argv, "dDe:f:hl:L:rs:")) != -1)
		switch (c)
		{
			case 'd':
				daemonize = 1;
				log_opts . stderr_level = LOG_LEVEL_QUIET;
				break;
			case 'D':
				daemonize = 0;
				break;
			case 'e':
				errlev = strtol (optarg, (char **) NULL, 10);
				if ((errlev < LOG_LEVEL_QUIET) || 
				    (errlev > LOG_LEVEL_DEBUG3)) {
					fprintf (stderr, "invalid errlev argument\n");
					usage (argv[0]);
					exit (1);
				}
				log_opts . stderr_level = errlev;
				break;
			case 'h':
				usage (argv[0]);
				exit (0);
				break;
			case 'f':
				slurmctld_conf.slurm_conf = optarg;
				break;
			case 'l':
				errlev = strtol (optarg, (char **) NULL, 10);
				if ((errlev < LOG_LEVEL_QUIET) || 
				    (errlev > LOG_LEVEL_DEBUG3)) {
					fprintf (stderr, "invalid errlev argument\n");
					usage (argv[0]);
					exit (1);
				}
				log_opts . logfile_level = errlev;
				break;
			case 'L':
				log_file = optarg;
				break;
			case 'r':
				recover = 1;
				break;
			case 's':
				errlev = strtol (optarg, (char **) NULL, 10);
				if ((errlev < LOG_LEVEL_QUIET) || 
				    (errlev > LOG_LEVEL_DEBUG3)) {
					fprintf (stderr, "invalid errlev argument\n");
					usage (argv[0]);
					exit (1);
				}
				log_opts . syslog_level = errlev;
				break;
			default:
				usage (argv[0]);
				exit (1);
		}

	log_init(argv[0], log_opts, SYSLOG_FACILITY_DAEMON, log_file);
}

/* usage - print a message describing the command line arguments of slurmctld */
void
usage (char *prog_name) 
{
	printf ("%s [OPTIONS]\n", prog_name);
	printf ("  -d           Become a daemon\n");
	printf ("  -D           Debug mode, do not become a daemon, stay in the foreground\n");
	printf ("  -e <errlev>  Set stderr logging to the specified level\n");
	printf ("  -f <file>    Use specified configuration file name\n");
	printf ("  -h           Print a help message describing usage\n");
	printf ("  -l <errlev>  Set logfile logging to the specified level\n");
	printf ("  -L <file>    Set logfile to the supplied file name\n");
	printf ("  -s <errlev>  Set syslog logging to the specified level\n");
	printf ("  -r           Recover state from last checkpoint\n");
	printf ("<errlev> is an integer between 0 and 7 with higher numbers providing more detail.\n");
}
