/* 
 * controller.c - main control machine daemon for slurm
 * see slurm.h for documentation on external functions and data structures
 *
 * NOTE: DEBUG_MODULE of read_config requires that it be loaded with 
 *       bits_bytes, partition_mgr, read_config, and node_mgr
 *
 * author: moe jette, jette@llnl.gov
 */
 /* Changes
  * Kevin Tew June 3, 2002 
  * reimplemented the entire routine to use the new communication library*/

#ifdef have_config_h
#  include <config.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#include "slurmctld.h"
#include "pack.h"
#include <src/common/slurm_protocol_api.h>

#define BUF_SIZE 1024

time_t init_time;

int msg_from_root (void);
void slurmctld_req ( slurm_msg_t * msg );
void fill_build_table ( struct build_table * build_ptr );
inline static void slurm_rpc_dump_build ( slurm_msg_t * msg ) ;
inline static void slurm_rpc_dump_nodes ( slurm_msg_t * msg ) ;
inline static void slurm_rpc_dump_partitions ( slurm_msg_t * msg ) ;
inline static void slurm_rpc_dump_jobs ( slurm_msg_t * msg ) ;
inline static void slurm_rpc_job_cancel ( slurm_msg_t * msg ) ;
inline static void slurm_rpc_job_will_run ( slurm_msg_t * msg ) ;
inline static void slurm_rpc_reconfigure_controller ( slurm_msg_t * msg ) ;
inline static void slurm_rpc_node_registration ( slurm_msg_t * msg ) ;

int 
main (int argc, char *argv[]) 
{
	int error_code ;
	slurm_fd newsockfd;
        slurm_fd sockfd;
	slurm_msg_t * msg = NULL ;
	slurm_addr cli_addr ;
	char node_name[MAX_NAME_LEN];
	log_options_t opts = LOG_OPTS_STDERR_ONLY ;

	init_time = time (NULL);
	log_init(argv[0], opts, SYSLOG_FACILITY_DAEMON, NULL);

	if ( ( error_code = init_slurm_conf () ) ) 
		fatal ("slurmctld: init_slurm_conf error %d", error_code);
	if ( ( error_code = read_slurm_conf (SLURM_CONF) ) ) 
		fatal ("slurmctld: error %d from read_slurm_conf reading %s", error_code, SLURM_CONF);
	if ( ( error_code = gethostname (node_name, MAX_NAME_LEN) ) ) 
		fatal ("slurmctld: errno %d from gethostname", errno);
	if ( ( strcmp (node_name, control_machine) ) )
	       	fatal ("slurmctld: this machine (%s) is not the primary control machine (%s)", node_name, control_machine);

	
	if ( ( sockfd = slurm_init_msg_engine_port ( SLURM_PORT ) ) == SLURM_SOCKET_ERROR )
		fatal ("slurmctld: error starting message engine \n", errno);
		
	while (1) 
	{
		/* accept needed for stream implementation 
		 * is a no-op in message implementation that just passes sockfd to newsockfd
		 */
		if ( ( newsockfd = slurm_accept_msg_conn ( sockfd , & cli_addr ) ) == SLURM_SOCKET_ERROR )
		{
			error ("slurmctld: error %d from connect", errno) ;
			break ;
		}
		
		/* receive message call that must occur before thread spawn because in message 
		 * implementation their is no connection and the message is the sign of a new connection */
		msg = malloc ( sizeof ( slurm_msg_t ) ) ;	
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
		slurmctld_req ( msg );	/* process the request */
		/* close should only be called when the stream implementation is being used 
		 * the following call will be a no-op in the message implementation */
		slurm_close_accepted_conn ( newsockfd ); /* close the new socket */
	}			
	return 0 ;
}

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
			break;
		case REQUEST_NODE_REGISRATION_STATUS:
			break;
		case REQUEST_RECONFIGURE:
			break;
		default:
			error ("slurmctld_req: invalid request msg type %d\n", msg-> msg_type);
			slurm_send_rc_msg ( msg , EINVAL );
			break;
	}
	slurm_free_msg ( msg ) ;
}


/* 
 * dump_build - dump all build parameters to a buffer
 * input: buffer_ptr - location into which a pointer to the data is to be stored.
 *                     the data buffer is actually allocated by dump_part and the 
 *                     calling function must xfree the storage.
 *         buffer_size - location into which the size of the created buffer is in bytes
 *	   last_update - only perform dump if updated since time specified
 * output: buffer_ptr - the pointer is set to the allocated buffer.
 *         buffer_size - set to size of the buffer in bytes
 *         returns 0 if no error, errno otherwise
 * NOTE: the buffer at *buffer_ptr must be xfreed by the caller
 * NOTE: if you make any changes here be sure to increment the value of 
 *	 	BUILD_STRUCT_VERSION and make the corresponding changes to 
 *		load_build in api/build_info.c
 */
void
slurm_rpc_dump_build ( slurm_msg_t * msg )
{
	clock_t start_time;
	slurm_msg_t response_msg ;
	last_update_msg_t * last_time_msg = ( last_update_msg_t * ) msg-> data ;
	build_info_msg_t build_tbl ;

	start_time = clock ();
	
	
	/* check to see if build_data has changed */	
	if ( last_time_msg -> last_update >= init_time )
	{
		info ("slurmctld_req: dump_build time=%ld", (long) (clock () - start_time));
		slurm_send_rc_msg ( msg , SLURM_NO_CHANGE_IN_DATA );
	}
	else
	{
		/* success */
		fill_build_table ( & build_tbl ) ;
		/* init response_msg structure */
		response_msg . address = msg -> address ;
		response_msg . msg_type = RESPONSE_BUILD_INFO ;
		response_msg . data = & build_tbl ;

		/* send message */
		info ("slurmctld_req: dump_build time=%ld", (long) (clock () - start_time));
		slurm_send_node_msg( msg -> conn_fd , &response_msg ) ;
	}
}

/* DumpJob - dump the Job configurations */
void
slurm_rpc_dump_jobs ( slurm_msg_t * msg )
{
	int error_code;
	clock_t start_time;
	char *dump;
	int dump_size;
	slurm_msg_t response_msg ;
	last_update_msg_t * last_time_msg = ( last_update_msg_t * ) msg-> data ;
	time_t last_update = last_time_msg -> last_update ;
	

	start_time = clock ();

	error_code = pack_all_jobs (&dump, &dump_size, &last_update);
	if (error_code)
		info ("slurmctld_req: pack_all_jobs error %d, time=%ld",
			 error_code, (long) (clock () - start_time));
	else
		info ("slurmctld_req: pack_all_jobs returning %d bytes, time=%ld",
			 dump_size, (long) (clock () - start_time));

	/* no changed data */
	if (dump_size == 0)
	{
		slurm_send_rc_msg ( msg , SLURM_NO_CHANGE_IN_DATA );
	}
	/* successful call */
	else if (error_code == 0)
	{
		/* success */
		/* init response_msg structure */
		response_msg . address = msg -> address ;
		response_msg . msg_type = RESPONSE_JOB_INFO ;
		response_msg . data = dump ;
		response_msg . data_size = dump_size ;

		/* send message */
		slurm_send_node_msg( msg -> conn_fd , &response_msg ) ;
	}
	/* error code returned */
	else
	{
		slurm_send_rc_msg ( msg , error_code );
	}
	if (dump)
		xfree (dump);
}

/* DumpNode - dump the node configurations */
void
slurm_rpc_dump_nodes ( slurm_msg_t * msg )
{
	int error_code;
	clock_t start_time;
	char *dump;
	int dump_size;
	slurm_msg_t response_msg ;
	last_update_msg_t * last_time_msg = ( last_update_msg_t * ) msg-> data ;
	time_t last_update = last_time_msg -> last_update ;

	start_time = clock ();

	error_code = pack_all_node (&dump, &dump_size, &last_update);
	if (error_code)
		info ("slurmctld_req: part_all_node error %d, time=%ld",
				error_code, (long) (clock () - start_time));
	else
		info ("slurmctld_req: part_all_node returning %d bytes, time=%ld",
				dump_size, (long) (clock () - start_time));

	/* no changed data */
	if (dump_size == 0)
	{
		slurm_send_rc_msg ( msg , SLURM_NO_CHANGE_IN_DATA );
	}
	/* successful call */
	else if (error_code == 0)
	{
		/* success */
		/* init response_msg structure */
		response_msg . address = msg -> address ;
		response_msg . msg_type = RESPONSE_NODE_INFO ;
		response_msg . data = dump ;
		response_msg . data_size = dump_size ;

		/* send message */
		slurm_send_node_msg( msg -> conn_fd , &response_msg ) ;
	}
	/* error code returned */
	else
	{
		slurm_send_rc_msg ( msg , error_code );
	}
	if (dump)
		xfree (dump);
}

/* DumpPart - dump the partition configurations */
void
slurm_rpc_dump_partitions ( slurm_msg_t * msg )
{
	int error_code;
	clock_t start_time;
	char *dump;
	int dump_size;
	slurm_msg_t response_msg ;
	last_update_msg_t * last_time_msg = ( last_update_msg_t * ) msg-> data ;
	time_t last_update = last_time_msg -> last_update ;

	start_time = clock ();

	error_code = pack_all_part (&dump, &dump_size, &last_update);
	if (error_code)
		info ("slurmctld_req: dump_part error %d, time=%ld",
				error_code, (long) (clock () - start_time));
	else
		info ("slurmctld_req: dump_part returning %d bytes, time=%ld",
				dump_size, (long) (clock () - start_time));

	/* no changed data */
	if (dump_size == 0)
	{
		slurm_send_rc_msg ( msg , SLURM_NO_CHANGE_IN_DATA );
	}
	/* successful call */
	else if (error_code == 0)
	{
		/* success */
		/* init response_msg structure */
		response_msg . address = msg -> address ;
		response_msg . msg_type = RESPONSE_PARTITION_INFO ;
		response_msg . data = dump ;
		response_msg . data_size = dump_size ;

		/* send message */
		slurm_send_node_msg( msg -> conn_fd , &response_msg ) ;
	}
	/* error code returned */
	else
	{
		slurm_send_rc_msg ( msg , error_code );
	}
	if (dump)
		xfree (dump);
}

/* JobCancel - cancel a slurm job or reservation */
void 
slurm_rpc_job_cancel ( slurm_msg_t * msg )
{
	/* init */
	int error_code;
	clock_t start_time;
	job_id_msg_t * job_id_msg = ( job_id_msg_t * ) msg-> data ;

	start_time = clock ();

	/* do RPC call */
	error_code = job_cancel ( job_id_msg->job_id );

	/* return result */
	if (error_code)
	{
		info ("slurmctld_req: job_cancel error %d, time=%ld",
				error_code, (long) (clock () - start_time));
		slurm_send_rc_msg ( msg , error_code );
	}
	else
	{
		info ("slurmctld_req: job_cancel success for %d, time=%ld",
				job_id_msg->job_id, (long) (clock () - start_time));
		slurm_send_rc_msg ( msg , SLURM_SUCCESS );
	}

}

/* JobWillRun - determine if job with given configuration can be initiated now */
void 
slurm_rpc_job_will_run ( slurm_msg_t * msg )
{
	/* init */
	int error_code;
	clock_t start_time;

	start_time = clock ();

	/* do RPC call */
	error_code = EINVAL;
	
	/* return result */
	if (error_code)
	{
		info ("slurmctld_req: job_will_run error %d, time=%ld",
				error_code, (long) (clock () - start_time));
		slurm_send_rc_msg ( msg , error_code );
	}
	else
	{
		info ("slurmctld_req: job_will_run success for , time=%ld",
				(long) (clock () - start_time));
		slurm_send_rc_msg ( msg , SLURM_SUCCESS );
	}

}
/* Reconfigure - re-initialized from configuration files */
void 
slurm_rpc_reconfigure_controller ( slurm_msg_t * msg )
{
	/* init */
	int error_code;
	clock_t start_time;

	start_time = clock ();

	/* do RPC call */
	error_code = init_slurm_conf ();
	if (error_code == 0)
		error_code = read_slurm_conf (SLURM_CONF);
	reset_job_bitmaps ();

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

/* NodeConfig - determine if a node's actual configuration satisfies the
 * configured specification */
void 
slurm_rpc_node_registration ( slurm_msg_t * msg )
{
	/* init */
	int error_code;
	clock_t start_time;
	node_registration_status_msg_t * node_reg_stat_msg = ( node_registration_status_msg_t * ) msg-> data ;

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
		error ("slurmctld_req: node_config error %d for %s, time=%ld",
				error_code, node_reg_stat_msg -> node_name, (long) (clock () - start_time));
		slurm_send_rc_msg ( msg , error_code );
	}
	else
	{
		info ("slurmctld_req: node_config for %s, time=%ld",
				node_reg_stat_msg -> node_name, (long) (clock () - start_time));
		slurm_send_rc_msg ( msg , SLURM_SUCCESS );
	}
}



void
fill_build_table ( struct build_table * build_ptr )
{
	build_ptr->last_update		= init_time ;
	build_ptr->backup_interval	= BACKUP_INTERVAL ;
	build_ptr->backup_location	= BACKUP_LOCATION ;
	build_ptr->backup_machine	= backup_controller ;
	build_ptr->control_daemon	= CONTROL_DAEMON ;
	build_ptr->control_machine	= control_machine ;
	build_ptr->controller_timeout	= CONTROLLER_TIMEOUT ;
	build_ptr->epilog		= EPILOG ;
	build_ptr->fast_schedule	= FAST_SCHEDULE ;
	build_ptr->hash_base		= HASH_BASE ;
	build_ptr->heartbeat_interval	= HEARTBEAT_INTERVAL;
	build_ptr->init_program		= INIT_PROGRAM ;
	build_ptr->kill_wait		= KILL_WAIT ;
	build_ptr->prioritize		= PRIORITIZE ;
	build_ptr->prolog		= PROLOG ;
	build_ptr->server_daemon	= SERVER_DAEMON ;
	build_ptr->server_timeout	= SERVER_TIMEOUT ;
	build_ptr->slurm_conf		= SLURM_CONF ;
	build_ptr->tmp_fs		= TMP_FS ;
}


/*
 * slurmctld_req - process a slurmctld request from the given socket
 * input: sockfd - the socket with a request to be processed
 */
void
slurmctld_req_old (int sockfd) {
	int error_code, in_size, i;
	char in_line[BUF_SIZE], node_name[MAX_NAME_LEN];
	int cpus, real_memory, tmp_disk;
	char *node_name_ptr, *part_name, *time_stamp;
	uint32_t job_id;
	time_t last_update;
	clock_t start_time;
	char *dump;
	int dump_size, dump_loc;

	in_size = recv (sockfd, in_line, sizeof (in_line), 0);
	start_time = clock ();

	/* Allocate:  allocate resources for a job */

	if (strncmp ("Allocate", in_line, 8) == 0) {
		node_name_ptr = NULL;
		error_code = job_allocate(&in_line[8], 	/* skip "Allocate" */
			&job_id, &node_name_ptr);
		if (error_code)
			info ("slurmctld_req: error %d allocating resources for %s, time=%ld",
				 error_code, &in_line[8], (long) (clock () - start_time));
		else
			info ("slurmctld_req: allocated nodes %s to %s, JobId=%u, time=%ld",
				 node_name_ptr, &in_line[8], job_id, 
				(long) (clock () - start_time));

		if (error_code == 0) {
			i = strlen(node_name_ptr) + 12;
			dump = xmalloc(i);
			sprintf(dump, "%s %u", node_name_ptr, job_id);
			send (sockfd, dump, i, 0);
			xfree(dump);
		}
		else if (error_code == EAGAIN)
			send (sockfd, "EAGAIN", 7, 0);
		else
			send (sockfd, "EINVAL", 7, 0);

		if (node_name_ptr)
			xfree (node_name_ptr);
	}


	/* JobSubmit - submit a job to the slurm queue */
	else if (strncmp ("JobSubmit", in_line, 9) == 0) {
		struct job_record *job_rec_ptr;
		error_code = job_create(&in_line[9], &job_id, 0, 0, 
				&job_rec_ptr);	/* skip "JobSubmit" */
		if (error_code)
			info ("slurmctld_req: job_submit error %d, time=%ld",
				 error_code, (long) (clock () - start_time));
		else
			info ("slurmctld_req: job_submit success for %s, id=%u, time=%ld",
				 &in_line[9], job_id, 
				(long) (clock () - start_time));
		if (error_code == 0) {
			dump = xmalloc(12);
			sprintf(dump, "%u", job_id);
			send (sockfd, dump, strlen(dump) + 1, 0);
			xfree (dump);
		}
		else
			send (sockfd, "EINVAL", 7, 0);
		schedule();
	}

	/* Update - modify node or partition configuration */
	else if (strncmp ("Update", in_line, 6) == 0) {
		node_name_ptr = part_name = NULL;
		error_code = load_string (&node_name_ptr, "NodeName=", in_line);
		if ((error_code == 0) && (node_name_ptr != NULL))
			error_code = update_node (node_name_ptr, &in_line[6]);	/* skip "Update" */
		else {
			error_code =
				load_string (&part_name, "PartitionName=", in_line);
			if ((error_code == 0) && (part_name != NULL))
				error_code = update_part (part_name, &in_line[6]); /* skip "Update" */
			else
				error_code = EINVAL;
		}		
		if (error_code) {
			if (node_name_ptr)
				error ("slurmctld_req: update error %d on node %s, time=%ld",
					 error_code, node_name_ptr, (long) (clock () - start_time));
			else if (part_name)
				error ("slurmctld_req: update error %d on partition %s, time=%ld",
					 error_code, part_name, (long) (clock () - start_time));
			else
				error ("slurmctld_req: update error %d on request %s, time=%ld",
					 error_code, in_line, (long) (clock () - start_time));

		}
		else {
			if (node_name_ptr)
				info ("slurmctld_req: updated node %s, time=%ld",
					 node_name_ptr, (long) (clock () - start_time));
			else
				info ("slurmctld_req: updated partition %s, time=%ld",
					 part_name, (long) (clock () - start_time));
		}
		sprintf (in_line, "%d", error_code);
		send (sockfd, in_line, strlen (in_line) + 1, 0);

		if (node_name_ptr)
			xfree (node_name_ptr);
		if (part_name)
			xfree (part_name);

	}
	else {
	}			
	return;
}
