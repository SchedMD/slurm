/*****************************************************************************\
 * controller.c - main control machine daemon for slurm
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
#include <src/slurmctld/locks.h>
#include <src/slurmctld/slurmctld.h>

#define BUF_SIZE 1024
#define MAX_SERVER_THREAD_COUNT 20

log_options_t log_opts = LOG_OPTS_STDERR_ONLY ;
slurm_ctl_conf_t slurmctld_conf;
time_t shutdown_time = (time_t)0;
static pthread_mutex_t thread_count_lock = PTHREAD_MUTEX_INITIALIZER;
int server_thread_count = 0;
pid_t slurmctld_pid;
pthread_t thread_id_bg = (pthread_t)0;
pthread_t thread_id_main = (pthread_t)0;
pthread_t thread_id_rpc = (pthread_t)0;

int msg_from_root (void);
void slurmctld_req ( slurm_msg_t * msg );
void fill_ctld_conf ( slurm_ctl_conf_t * build_ptr );
void init_ctld_conf ( slurm_ctl_conf_t * build_ptr );
void parse_commandline( int argc, char* argv[], slurm_ctl_conf_t * );
void *process_rpc ( void * req );
void report_locks_set ( void );
void *slurmctld_background ( void * no_data );
void slurmctld_cleanup (void *context);
void *slurmctld_rpc_mgr( void * no_data );
int slurm_shutdown ( void );
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
inline static void slurm_rpc_reconfigure_controller ( slurm_msg_t * msg ) ;
inline static void slurm_rpc_shutdown_controller ( slurm_msg_t * msg );
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
	int sig ;
	int error_code;
	char node_name[MAX_NAME_LEN];
	pthread_attr_t thread_attr_bg, thread_attr_rpc;
	sigset_t set;

	/*
	 * Establish initial configuration
	 */
	log_init(argv[0], log_opts, SYSLOG_FACILITY_DAEMON, NULL);
	thread_id_main = pthread_self();
	fatal_add_cleanup_job (slurmctld_cleanup, NULL);

	slurmctld_pid = getpid ( );
	init_ctld_conf ( &slurmctld_conf );
	init_locks ( );
	parse_commandline ( argc, argv, &slurmctld_conf );

	if ( ( error_code = read_slurm_conf ()) ) 
		fatal ("read_slurm_conf error %d reading %s", error_code, SLURM_CONFIG_FILE);
	if ( ( error_code = getnodename (node_name, MAX_NAME_LEN) ) ) 
		fatal ("getnodename errno %d", error_code);

	if ( strcmp (node_name, slurmctld_conf.control_machine) &&  
	     strcmp (node_name, slurmctld_conf.backup_controller) &&
	     strcmp ("localhost", slurmctld_conf.control_machine) &&
	     strcmp ("localhost", slurmctld_conf.backup_controller) )
	       	fatal ("this machine (%s) is not the primary (%s) or backup (%s) controller", 
			node_name, slurmctld_conf.control_machine, 
			slurmctld_conf.backup_controller);

	/* block all signals for now */
	if (sigfillset (&set))
		error ("sigfillset errno %d", errno);

	if (pthread_sigmask (SIG_BLOCK, &set, NULL))
		error ("pthread_sigmask errno %d", errno);

	/* create attached thread for background activities */
	if (pthread_attr_init (&thread_attr_bg))
		fatal ("pthread_attr_init errno %d", errno);
	if (pthread_create ( &thread_id_bg, &thread_attr_bg, slurmctld_background, NULL))
		fatal ("pthread_create errno %d", errno);

	/* create attached thread to process RPCs */
	pthread_mutex_lock(&thread_count_lock);
	server_thread_count++;
	pthread_mutex_unlock(&thread_count_lock);
	if (pthread_attr_init (&thread_attr_rpc))
		fatal ("pthread_attr_init errno %d", errno);
	if (pthread_create ( &thread_id_rpc, &thread_attr_rpc, slurmctld_rpc_mgr, NULL))
		fatal ("pthread_create errno %d", errno);

	/* just watch for select signals */
	if (sigemptyset (&set))
		error ("sigemptyset errno %d", errno);
	if (sigaddset (&set, SIGHUP))
		error ("sigaddset errno %d on SIGHUP", errno);
	if (sigaddset (&set, SIGINT))
		error ("sigaddset errno %d on SIGINT", errno);
	if (sigaddset (&set, SIGTERM))
		error ("sigaddset errno %d on SIGTERM", errno);
	while (1) {
		if ( (error_code = sigwait (&set, &sig)) )
			error ("sigwait errno %d\n", error_code);

		switch (sig) {
			case SIGINT:	/* kill -2  or <CTRL-C>*/
			case SIGTERM:	/* kill -15 */
				info ("Terminate signal (SIGINT or SIGTERM) received\n");
				shutdown_time = time (NULL);
				/* send REQUEST_SHUTDOWN_IMMEDIATE RPC */
				slurm_shutdown ();
				pthread_join (thread_id_rpc, NULL);
				/* thread_id_bg waits for all RPCs to complete */
				pthread_join (thread_id_bg, NULL);
				pthread_exit ((void *)0);
				break;
			case SIGHUP:	/* kill -1 */
				info ("Reconfigure signal (SIGHUP) received\n");
				error_code = read_slurm_conf ( );
				if (error_code)
					error ("read_slurm_conf error %d", error_code);
				break;
			default:
				error ("Invalid signal (%d) received\n", sig);
		}
	}

	pthread_exit ((void *)0);

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

	/* threads to process individual RPC's are detached */
	if (pthread_attr_init (&thread_attr_rpc_req))
		fatal ("pthread_attr_init errno %d", errno);
	if (pthread_attr_setdetachstate (&thread_attr_rpc_req, PTHREAD_CREATE_DETACHED))
		fatal ("pthread_attr_setdetachstate errno %d", errno);

	/* initialize port for RPCs */
	if ( ( sockfd = slurm_init_msg_engine_port ( slurmctld_conf . slurmctld_port ) ) 
			== SLURM_SOCKET_ERROR )
		fatal ("slurm_init_msg_engine_port error %d \n", errno);

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
			error ("slurm_accept_msg_conn error %d", errno) ;
			continue ;
		}
		conn_arg -> newsockfd = newsockfd ;
		pthread_mutex_lock(&thread_count_lock);
		server_thread_count++;
		pthread_mutex_unlock(&thread_count_lock);
		if (server_thread_count > MAX_SERVER_THREAD_COUNT) {
			info ("Warning: server_thread_count is %d, over system limit", server_thread_count);
			no_thread = 1;
		}
		else if (shutdown_time)
			no_thread = 1;
		else if (pthread_create ( &thread_id_rpc_req, &thread_attr_rpc_req, service_connection, (void *) conn_arg )) {
			error ("pthread_create errno %d", errno);
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
	void * return_code;
	
	msg = xmalloc ( sizeof ( slurm_msg_t ) ) ;	

	if ( ( error_code = slurm_receive_msg ( newsockfd , msg ) ) == SLURM_SOCKET_ERROR )
	{
		error ("slurm_receive_msg error %d", errno);
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
	static time_t last_sched_time = (time_t) NULL;
	static time_t last_checkpoint_time = (time_t) NULL;
	static time_t last_timelimit_time = (time_t) NULL;
	time_t now;
	/* Locks: Write job, write node, read partition */
	slurmctld_lock_t job_write_lock = { NO_LOCK, WRITE_LOCK, WRITE_LOCK, READ_LOCK };
	/* Locks: Read config, write job, write node, write partition */
	slurmctld_lock_t state_write_lock = { READ_LOCK, WRITE_LOCK, WRITE_LOCK, WRITE_LOCK };

	while (shutdown_time == 0) {
		sleep (1);

		now = time (NULL);

		if ((now - last_timelimit_time) > PERIODIC_TIMEOUT) {
			last_timelimit_time = now;
			job_time_limit ();
		}

		if ((now - last_sched_time) > PERIODIC_SCHEDULE) {
			last_sched_time = now;
			/* locking is done outside of schedule() because it is called  
			 * from other many functions that already have their locks set */
			lock_slurmctld (job_write_lock);
			purge_old_job ();	/* remove defunct job records */
			schedule ();
			unlock_slurmctld (job_write_lock);
		}

		if (shutdown_time || (now - last_checkpoint_time) > PERIODIC_CHECKPOINT) {
			if (shutdown_time) {	
				/* wait for any RPC's to complete */
				if (server_thread_count)
					sleep (1);
				if (server_thread_count)
					sleep (1);
				if (server_thread_count)
					info ("warning: shutting down with server_thread_count of %d", server_thread_count);
				report_locks_set ( );
				last_checkpoint_time = now;
				/* don't lock to insure checkpoint never blocks */
				/* issue call to save state */
			}
			else {
				last_checkpoint_time = now;
				lock_slurmctld (state_write_lock);
				/* issue call to save state */
				unlock_slurmctld (state_write_lock);
			}
		}

	}
	debug3 ("slurmctld_background shutting down");
	pthread_exit ((void *)0);
}

/* report_locks_set - report any slurmctld locks left set */
void
report_locks_set ( void )
{
	slurmctld_lock_flags_t lock_flags;
	char config[4]="", job[4]="", node[4]="", partition[4]="";

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

	if ((strlen (config) + strlen (job) + strlen (node) + strlen (partition)) > 0)
		error ("The following locks were left set config:%s, job:%s, node:%s, part:%s",
			config, job, node, partition);
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
		case REQUEST_SHUTDOWN_IMMEDIATE:
			slurm_rpc_shutdown_controller ( msg ) ;
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

	start_time = clock ();
	
	/* check to see if configuration data has changed */	
	if ( last_time_msg -> last_update >= slurmctld_conf.last_update )
	{
		info ("slurm_rpc_dump_build, no change, time=%ld", 
			(long) (clock () - start_time));
		slurm_send_rc_msg ( msg , SLURM_NO_CHANGE_IN_DATA );
	}
	else
	{
		/* success */
		fill_ctld_conf ( & build_tbl ) ;
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
	
	start_time = clock ();

	if ( last_time_msg -> last_update >= last_job_update )
	{
		info ("slurm_rpc_dump_jobs, no change, time=%ld", 
			(long) (clock () - start_time));
		slurm_send_rc_msg ( msg , SLURM_NO_CHANGE_IN_DATA );
	}
	else
	{
		/* success */
		pack_all_jobs (&dump, &dump_size, &last_update);
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

	start_time = clock ();

	if ( last_time_msg -> last_update >= last_node_update )
	{
		info ("slurm_rpc_dump_nodes, no change, time=%ld", 
			(long) (clock () - start_time));
		slurm_send_rc_msg ( msg , SLURM_NO_CHANGE_IN_DATA );
	}
	else
	{
		/* success */
		pack_all_node (&dump, &dump_size, &last_update);
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

	start_time = clock ();

	if ( last_time_msg -> last_update >= last_part_update )
	{
		info ("slurm_rpc_dump_partitions, no change, time=%ld", 
			(long) (clock () - start_time));
		slurm_send_rc_msg ( msg , SLURM_NO_CHANGE_IN_DATA );
	}
	else
	{
		/* success */
		pack_all_part (&dump, &dump_size, &last_update);
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
	int error_code;
	clock_t start_time;
	job_step_id_msg_t * job_step_id_msg = ( job_step_id_msg_t * ) msg-> data ;

	start_time = clock ();

	/* do RPC call */
	if (job_step_id_msg->job_step_id == NO_VAL) {
		error_code = job_cancel ( job_step_id_msg->job_id );

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
		}
	}
	else {
		error_code = job_step_cancel (  job_step_id_msg->job_id , 
						job_step_id_msg->job_step_id);
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
		}
	}

	schedule();
}

/* slurm_rpc_job_step_complete - process RPC to note the completion an entire job or 
 *	an individual job step */
void 
slurm_rpc_job_step_complete ( slurm_msg_t * msg )
{
	/* init */
	int error_code;
	clock_t start_time;
	job_step_id_msg_t * job_step_id_msg = ( job_step_id_msg_t * ) msg-> data ;

	start_time = clock ();

	/* do RPC call */
	if (job_step_id_msg->job_step_id == NO_VAL) {
		error_code = job_complete ( job_step_id_msg->job_id );

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
		}
	}
	else {
		error_code = job_step_complete (  job_step_id_msg->job_id , 
						job_step_id_msg->job_step_id);
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
		}
	}

	schedule();
}

/* slurm_rpc_job_step_get_info - process RPC msg to get job_step information */
void list_append_list( List to, List from )
{
	ListIterator i_from = list_iterator_create( from );
	void *temp = NULL;
	while ( (temp = list_next( i_from ) ) != NULL )
		list_append( to, temp );

}

void 
slurm_rpc_job_step_get_info ( slurm_msg_t * msg ) 
{
	int error_code = 0;
	clock_t start_time;
	List step_list = list_create( NULL );
	void* resp_buffer = NULL;
	int resp_buffer_size = 0;
	job_step_info_request_msg_t* request = ( job_step_info_request_msg_t * ) msg-> data ;

	start_time = clock ();

	if ( request->job_id == 0 )
	{
		/* Return all steps */
		struct job_record *current_job = NULL;
		ListIterator i_jobs = list_iterator_create( job_list );
		
		while ( (current_job = list_next( i_jobs ) ) != NULL )
			list_append_list( step_list, current_job->step_list );

	}
	else if ( request->job_step_id == 0 )
	{
		/* Return all steps for job_id */
		struct job_record* job_ptr = find_job_record( request->job_id );
		if ( job_ptr == NULL )
			error_code = ESLURM_INVALID_JOB_ID;
		else
			list_append_list( step_list, job_ptr->step_list );

	}
	else
	{
		/* Return  step with give step_id/job_id */
		struct step_record* step =  find_step_record( find_job_record( request->job_id ), request->job_step_id ); 
		if ( step ==  NULL ) 
			error_code = ESLURM_INVALID_JOB_ID;
		else
			list_append( step_list, step );
	}

	if ( error_code )
	{
		error ("slurm_rpc_job_step_get_info error %d for job step %u.%u, time=%ld",
				error_code, request->job_id, request->job_step_id, 
				(long) (clock () - start_time));
		slurm_send_rc_msg ( msg , error_code );
	}
	else
	{
		slurm_msg_t response_msg ;
	
		pack_ctld_job_step_info_reponse_msg( step_list, &resp_buffer, &resp_buffer_size );
		response_msg . address = msg -> address ;
		response_msg . msg_type = RESPONSE_JOB_STEP_INFO;
		response_msg . data = resp_buffer ;
		response_msg . data_size = resp_buffer_size ;
		slurm_send_node_msg( msg->conn_fd , &response_msg ) ;

	}
	
	list_destroy( step_list );
}

/* slurm_rpc_update_job - process RPC to update the configuration of a job (e.g. priority) */
void 
slurm_rpc_update_job ( slurm_msg_t * msg )
{
	/* init */
	int error_code;
	clock_t start_time;
	job_desc_msg_t * job_desc_msg = ( job_desc_msg_t * ) msg-> data ;

	start_time = clock ();
		
	/* do RPC call */
	error_code = update_job ( job_desc_msg );

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
	}

	schedule();
}

/* slurm_rpc_update_node - process RPC to update the configuration of a node (e.g. UP/DOWN) */
void 
slurm_rpc_update_node ( slurm_msg_t * msg )
{
	/* init */
	int error_code;
	clock_t start_time;
	update_node_msg_t * update_node_msg_ptr ;
	start_time = clock ();
	
	update_node_msg_ptr = (update_node_msg_t * ) msg-> data ;
	
	/* do RPC call */
	error_code = update_node ( update_node_msg_ptr );

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
	schedule();
}

/* slurm_rpc_update_partition - process RPC to update the configuration of a partition (e.g. UP/DOWN) */
void 
slurm_rpc_update_partition ( slurm_msg_t * msg )
{
	/* init */
	int error_code;
	clock_t start_time;
	update_part_msg_t * part_desc_ptr = (update_part_msg_t * ) msg-> data ;
	start_time = clock ();

	/* do RPC call */
	error_code = update_part ( part_desc_ptr );

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
	}
	schedule();
}

/* slurm_rpc_submit_batch_job - process RPC to submit a batch job */
void
slurm_rpc_submit_batch_job ( slurm_msg_t * msg )
{
	/* init */
	int error_code;
	clock_t start_time;
	uint32_t job_id ;
	slurm_msg_t response_msg ;
	submit_response_msg_t submit_msg ;
	job_desc_msg_t * job_desc_msg = ( job_desc_msg_t * ) msg-> data ;

	start_time = clock ();

	/* do RPC call */
	dump_job_desc(job_desc_msg);
	error_code = job_allocate (job_desc_msg, &job_id, (char **) NULL, 
		(uint16_t *) NULL, (uint32_t **) NULL, (uint32_t **) NULL,
		false, false, false);

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
		schedule();
	}
}

/* slurm_rpc_allocate_resources:  process RPC to allocate resources for a job */
void 
slurm_rpc_allocate_resources ( slurm_msg_t * msg , uint8_t immediate )
{
	/* init */
	int error_code;
	slurm_msg_t response_msg ;
	clock_t start_time;
	job_desc_msg_t * job_desc_msg = ( job_desc_msg_t * ) msg-> data ;
	char * node_list_ptr = NULL;
	uint16_t num_cpu_groups = 0;
	uint32_t * cpus_per_node = NULL, * cpu_count_reps = NULL;
	uint32_t job_id ;
	resource_allocation_response_msg_t alloc_msg ;

	start_time = clock ();

	/* do RPC call */
	dump_job_desc (job_desc_msg);
	error_code = job_allocate(job_desc_msg, &job_id, 
			&node_list_ptr, &num_cpu_groups, &cpus_per_node, &cpu_count_reps, 
			immediate , false, true );

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
	}
}

/* slurm_rpc_allocate_and_run: process RPC to allocate resources for a job and
 *	initiate a job step */
void
slurm_rpc_allocate_and_run ( slurm_msg_t * msg )
{
        /* init */
        int error_code;
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

        start_time = clock ();

        /* do RPC call */
        dump_job_desc (job_desc_msg);
        error_code = job_allocate(job_desc_msg, &job_id,
                        &node_list_ptr, &num_cpu_groups, &cpus_per_node, &cpu_count_reps,
                        true , false, true );

        /* return result */
        if (error_code) {
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
	if ( step_rec == NULL ) {
		info ("slurm_rpc_allocate_and_run error %d creating job step, , time=%ld",
			error_code,  (long) (clock () - start_time));
		slurm_send_rc_msg ( msg , error_code );
	}
	else {
		/* FIXME Needs to be fixed to really work with a credential */
		slurm_job_credential_t cred = { 1,1,"test",start_time, "signature"} ;

		info ("slurm_rpc_allocate_and_run allocated nodes %s to JobId=%u, time=%ld",
                        node_list_ptr , job_id , (long) (clock () - start_time));

		/* send job_ID  and node_name_ptr */
                alloc_msg . job_id = job_id ;
	        alloc_msg . node_list = node_list_ptr ;
	        alloc_msg . num_cpu_groups = num_cpu_groups;
	        alloc_msg . cpus_per_node  = cpus_per_node;
	        alloc_msg . cpu_count_reps = cpu_count_reps;
		alloc_msg . job_step_id = step_rec->step_id;
		alloc_msg . credentials = &cred;
#ifdef HAVE_LIBELAN3
	        /* FIXME */
#endif
	        response_msg . msg_type = RESPONSE_ALLOCATION_AND_RUN_JOB_STEP;
                response_msg . data =  & alloc_msg ;

		slurm_send_node_msg ( msg->conn_fd , & response_msg ) ;
	
		schedule ();
	}
}


/* slurm_rpc_job_will_run - process RPC to determine if job with given configuration can be initiated */
void slurm_rpc_job_will_run ( slurm_msg_t * msg )
{
	/* init */
	int error_code;
	clock_t start_time;
	uint16_t num_cpu_groups = 0;
	uint32_t * cpus_per_node = NULL, * cpu_count_reps = NULL;
	uint32_t job_id ;
	job_desc_msg_t * job_desc_msg = ( job_desc_msg_t * ) msg-> data ;
	char * node_list_ptr = NULL;

	start_time = clock ();

	/* do RPC call */
	dump_job_desc(job_desc_msg);
	error_code = job_allocate(job_desc_msg, &job_id, 
			&node_list_ptr, &num_cpu_groups, &cpus_per_node, &cpu_count_reps, 
			false , true, true );
	
	/* return result */
	if (error_code)
	{
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

/* slurm_rpc_reconfigure_controller - process RPC to re-initialize slurmctld from configuration file */
void 
slurm_rpc_reconfigure_controller ( slurm_msg_t * msg )
{
	/* init */
	int error_code;
	clock_t start_time;

	start_time = clock ();
/* must be user root */

	/* do RPC call */
	error_code = read_slurm_conf ( );

	/* return result */
	if (error_code)
	{
		error ("slurm_rpc_reconfigure_controller error %d, time=%ld",
				error_code, (long) (clock () - start_time));
		slurm_send_rc_msg ( msg , error_code );
	}
	else
	{
		reset_job_bitmaps ();
		info ("slurm_rpc_reconfigure_controller completed successfully, time=%ld", 
				(long) (clock () - start_time));
		slurm_send_rc_msg ( msg , SLURM_SUCCESS );
	}

	schedule();
}


/* slurm_rpc_shutdown_controller - process RPC to shutdown slurmctld */
void 
slurm_rpc_shutdown_controller ( slurm_msg_t * msg )
{
	/* do RPC call */
/* must be user root */
	if (shutdown_time)
		debug3 ("slurm_rpc_shutdown_controller again");
	else {
		kill (slurmctld_pid, SIGTERM);	/* tell master to clean-up */
		info ("slurm_rpc_shutdown_controller completed successfully");
	}

	/* return result */
	slurm_send_rc_msg ( msg , SLURM_SUCCESS );
}


/* slurm_rpc_create_job_step - process RPC to creates/registers a job step with the step_mgr */
void 
slurm_rpc_job_step_create( slurm_msg_t* msg )
{
	/* init */
	int error_code;
	clock_t start_time;

	slurm_msg_t resp;
	struct step_record* step_rec;
	job_step_create_response_msg_t job_step_resp;
	job_step_create_request_msg_t * req_step_msg = 
			( job_step_create_request_msg_t* ) msg-> data ;

	start_time = clock ();

	error_code = step_create ( req_step_msg, &step_rec );

	/* return result */
	if ( step_rec == NULL )
	{
		info ("slurm_rpc_job_step_create error %s  time=%ld", slurm_strerror( error_code ), 
				(long) (clock () - start_time));
		slurm_send_rc_msg ( msg , error_code );
	}
	else
	{
		/* FIXME Needs to be fixed to really work with a credential */
		slurm_job_credential_t cred = { 1,1,"test",start_time, "signature"} ;
		info ("slurm_rpc_job_step_create success time=%ld",
				(long) (clock () - start_time));
		
		job_step_resp.job_step_id = step_rec->step_id;
		job_step_resp.node_list = bitmap2node_name( step_rec->node_bitmap );
		job_step_resp.credentials = &cred;
				
#ifdef HAVE_LIBELAN3
	/* FIXME */
#endif
		resp. address = msg -> address ;
		resp. msg_type = RESPONSE_JOB_STEP_CREATE ;
		resp. data = &job_step_resp  ;

		slurm_send_node_msg ( msg->conn_fd , &resp);
	}

	schedule();

}

/* slurm_rpc_node_registration - process RPC to determine if a node's actual configuration satisfies the
 * configured specification */
void 
slurm_rpc_node_registration ( slurm_msg_t * msg )
{
	/* init */
	int error_code;
	clock_t start_time;
	slurm_node_registration_status_msg_t * node_reg_stat_msg = 
			( slurm_node_registration_status_msg_t * ) msg-> data ;

	start_time = clock ();

	/* do RPC call */
	/*cpus = real_memory = tmp_disk = NO_VAL;
	 * this should be done client side now */
	error_code = validate_node_specs (
		node_reg_stat_msg -> node_name ,
		node_reg_stat_msg -> cpus ,
		node_reg_stat_msg -> real_memory_size ,
		node_reg_stat_msg -> temporary_disk_space ) ;

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

/* slurm_shutdown - issue RPC to have slurmctld shutdown, knocks loose an accept() */
int
slurm_shutdown ()
{
	int msg_size ;
	int rc ;
	slurm_fd sockfd ;
	slurm_msg_t request_msg ;
	slurm_msg_t response_msg ;
	return_code_msg_t * slurm_rc_msg ;

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

	/* receive message */
	if ( ( msg_size = slurm_receive_msg ( sockfd , & response_msg ) ) == SLURM_SOCKET_ERROR ) {
		error ("slurm_receive_msg error");
		return SLURM_SOCKET_ERROR ;
	}

	/* shutdown message connection */
	if ( ( rc = slurm_shutdown_msg_conn ( sockfd ) ) == SLURM_SOCKET_ERROR ) {
		error ("slurm_shutdown_msg_conn error");
		return SLURM_SOCKET_ERROR ;
	}
	if ( msg_size )
		return msg_size;

	switch ( response_msg . msg_type )
	{
		case RESPONSE_SLURM_RC:
			slurm_rc_msg = ( return_code_msg_t * ) response_msg . data ;
			rc = slurm_rc_msg->return_code;
			slurm_free_return_code_msg ( slurm_rc_msg );	
			if (rc) {
				error ("slurm_shutdown_msg_conn error (%d)", rc);
				return SLURM_PROTOCOL_ERROR;
			}
			break ;
		default:
			error ("slurm_shutdown_msg_conn type bad (%d)", response_msg . msg_type);
			return SLURM_UNEXPECTED_MSG_ERROR;
			break ;
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
	conf_ptr->kill_wait         	= 30 ;
	conf_ptr->prioritize        	= NULL ;
	conf_ptr->prolog            	= NULL ;
	conf_ptr->slurmctld_timeout   	= 300 ;
	conf_ptr->slurmd_timeout   	= 300 ;
	conf_ptr->slurm_conf       	= SLURM_CONFIG_FILE ;
	conf_ptr->state_save_location   = xstrdup ("/tmp") ;
	conf_ptr->tmp_fs            	= xstrdup ("/tmp") ;

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
	/* Locks: Read config */
	slurmctld_lock_t config_read_lock = { READ_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };

	lock_slurmctld (config_read_lock);
	conf_ptr->last_update		= slurmctld_conf.last_update ;
	conf_ptr->backup_controller   	= slurmctld_conf.backup_controller ;
	conf_ptr->control_machine    	= slurmctld_conf.control_machine ;
	conf_ptr->epilog           	= slurmctld_conf.epilog ;
	conf_ptr->fast_schedule     	= slurmctld_conf.fast_schedule ;
	conf_ptr->hash_base         	= slurmctld_conf.hash_base ;
	conf_ptr->heartbeat_interval	= slurmctld_conf.heartbeat_interval;
	conf_ptr->kill_wait         	= slurmctld_conf.kill_wait ;
	conf_ptr->prioritize        	= slurmctld_conf.prioritize ;
	conf_ptr->prolog            	= slurmctld_conf.prolog ;
	conf_ptr->slurmctld_port   	= slurmctld_conf.slurmctld_port ;
	conf_ptr->slurmctld_timeout   	= slurmctld_conf.slurmctld_timeout ;
	conf_ptr->slurmd_port   	= slurmctld_conf.slurmd_port ;
	conf_ptr->slurmd_timeout   	= slurmctld_conf.slurmd_timeout ;
	conf_ptr->slurm_conf       	= slurmctld_conf.slurm_conf ;
	conf_ptr->state_save_location   = slurmctld_conf.state_save_location ;
	conf_ptr->tmp_fs            	= slurmctld_conf.tmp_fs ;

	unlock_slurmctld (config_read_lock);
}

/* Variables for commandline passing using getopt */
extern char *optarg;
extern int optind, opterr, optopt;

/* parse_commandline - parse and process any command line arguments */
void 
parse_commandline( int argc, char* argv[], slurm_ctl_conf_t * conf_ptr )
{
	int c = 0, errlev;
	opterr = 0;

	while ((c = getopt (argc, argv, "e:f:hl:s:")) != -1)
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
				log_opts . stderr_level = errlev;
				break;
			case 'h':
				usage (argv[0]);
				exit (0);
				break;
			case 'f':
				slurmctld_conf.slurm_conf = optarg;
				printf("slurmctrld.slurm_conf = %s\n", slurmctld_conf.slurm_conf );
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

	log_init(argv[0], log_opts, SYSLOG_FACILITY_DAEMON, NULL);
}

/* usage - print a message describing the command line arguments of slurmctld */
void
usage (char *prog_name) 
{
	printf ("%s [OPTIONS]\n", prog_name);
	printf ("  -e <errlev>  Set stderr logging to the specified level\n");
	printf ("  -f <file>    Use specified configuration file name\n");
	printf ("  -h           Print a help message describing usage\n");
	printf ("  -l <errlev>  Set logfile logging to the specified level\n");
	printf ("  -s <errlev>  Set syslog logging to the specified level\n");
	printf ("<errlev> is an integer between 0 and 7 with higher numbers providing more detail.\n");
}

/* slurmctld_cleanup - fatal error occured, kill all tasks */
void 
slurmctld_cleanup (void *context)
{
	pthread_t my_thread_id = pthread_self();

	kill_locked_threads ();

	if (thread_id_bg &&  (thread_id_bg  != my_thread_id))
		pthread_kill (thread_id_bg, SIGKILL);

	if (thread_id_rpc && (thread_id_rpc != my_thread_id))
		pthread_kill (thread_id_rpc, SIGKILL);

	if (thread_id_main && (thread_id_main != my_thread_id))
		pthread_kill  (thread_id_main, SIGKILL);
}
