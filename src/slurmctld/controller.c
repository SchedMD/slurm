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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <unistd.h>

#include <src/common/pack.h>
#include <src/common/slurm_protocol_api.h>
#include <src/common/macros.h>
#include <src/common/xstring.h>
#include <src/slurmctld/locks.h>
#include <src/slurmctld/slurmctld.h>

#define BUF_SIZE 1024

log_options_t log_opts = LOG_OPTS_STDERR_ONLY ;
slurm_ctl_conf_t slurmctld_conf;

int getnodename (char *name, size_t len);
int msg_from_root (void);
void slurmctld_req ( slurm_msg_t * msg );
void fill_ctld_conf ( slurm_ctl_conf_t * build_ptr );
void init_ctld_conf ( slurm_ctl_conf_t * build_ptr );
void parse_commandline( int argc, char* argv[], slurm_ctl_conf_t * );
inline static void slurm_rpc_dump_build ( slurm_msg_t * msg ) ;
inline static void slurm_rpc_dump_nodes ( slurm_msg_t * msg ) ;
inline static void slurm_rpc_dump_partitions ( slurm_msg_t * msg ) ;
inline static void slurm_rpc_dump_jobs ( slurm_msg_t * msg ) ;
inline static void slurm_rpc_job_step_cancel ( slurm_msg_t * msg ) ;
inline static void slurm_rpc_submit_batch_job ( slurm_msg_t * msg ) ;
inline static void slurm_rpc_reconfigure_controller ( slurm_msg_t * msg ) ;
inline static void slurm_rpc_node_registration ( slurm_msg_t * msg ) ;
inline static void slurm_rpc_update_job ( slurm_msg_t * msg ) ;
inline static void slurm_rpc_update_node ( slurm_msg_t * msg ) ;
inline static void slurm_rpc_update_partition ( slurm_msg_t * msg ) ;
inline static void slurm_rpc_job_will_run ( slurm_msg_t * msg ) ;
inline static void slurm_rpc_job_step_create( slurm_msg_t* msg ) ;	
inline static void slurm_rpc_allocate_resources ( slurm_msg_t * msg , uint8_t immediate ) ;
void usage (char *prog_name);

/* main - slurmctld main function, start various threads and process RPCs */
int 
main (int argc, char *argv[]) 
{
	int error_code ;
	slurm_fd newsockfd;
        slurm_fd sockfd;
	slurm_msg_t * msg = NULL ;
	slurm_addr cli_addr ;
	char node_name[MAX_NAME_LEN];

	/*
	 * Establish initial configuration
	 */
	log_init(argv[0], log_opts, SYSLOG_FACILITY_DAEMON, NULL);

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
			node_name, slurmctld_conf.control_machine, slurmctld_conf.backup_controller);

	/* initialize port for RPCs */
	if ( ( sockfd = slurm_init_msg_engine_port ( slurmctld_conf . slurmctld_port ) ) 
			== SLURM_SOCKET_ERROR )
		fatal ("slurm_init_msg_engine_port error %d \n", errno);

	/*
	 * Procss incoming RPCs indefinitely
	 */
	while (1) 
	{
		/* accept needed for stream implementation 
		 * is a no-op in message implementation that just passes sockfd to newsockfd
		 */
		if ( ( newsockfd = slurm_accept_msg_conn ( sockfd , & cli_addr ) ) == SLURM_SOCKET_ERROR )
		{
			error ("slurm_accept_msg_conn error %d", errno) ;
			break ;
		}
		
		/* receive message call that must occur before thread spawn because in message 
		 * implementation their is no connection and the message is the sign of a new connection */
		msg = xmalloc ( sizeof ( slurm_msg_t ) ) ;	
		if (msg == NULL)
			return ENOMEM;
		
		if ( ( error_code = slurm_receive_msg ( newsockfd , msg ) ) == SLURM_SOCKET_ERROR )
		{
			error ("slurm_receive_msg error %d", errno);
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
			slurm_free_last_update_msg ( msg -> data ) ;
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
		case REQUEST_JOB_WILL_RUN :
			slurm_rpc_job_will_run ( msg -> data ) ;
			slurm_free_job_desc_msg ( msg -> data ) ;
			break;
		case REQUEST_CANCEL_JOB_STEP:
			slurm_rpc_job_step_cancel ( msg ) ;
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
	last_update_msg_t * last_time_msg = ( last_update_msg_t * ) msg-> data ;
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
	}
	schedule();
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
		slurm_job_credential_t cred = { 1,1,"test",start_time,0} ;
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
	conf_ptr->tmp_fs            	= NULL ;

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

/* getnodename - equivalent to gethostname, but return only the first component of the fully 
 *	qualified name (e.g. "linux123.foo.bar" becomes "linux123") */
int
getnodename (char *name, size_t len)
{
	int error_code, name_len;
	char *dot_ptr, path_name[1024];

	error_code = gethostname (path_name, sizeof(path_name));
	if (error_code)
		return error_code;

	dot_ptr = strchr (path_name, '.');
	if (dot_ptr == NULL)
		dot_ptr = path_name + strlen(path_name);
	else
		dot_ptr[0] = '\0';

	name_len = (dot_ptr - path_name);
	if (name_len > len)
		return ENAMETOOLONG;

	strcpy (name, path_name);
	return 0;
}
