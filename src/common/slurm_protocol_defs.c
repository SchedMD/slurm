#include <src/common/slurm_protocol_defs.h>
#include <src/common/xmalloc.h>
#include <stdlib.h>
/* short messages*/
void slurm_free_last_update_msg ( last_update_msg_t * msg )
{
	xfree ( msg ) ;
}

void slurm_free_job_id_msg ( job_id_msg_t * msg )
{
	xfree ( msg ) ;
}

void slurm_free_return_code_msg ( return_code_msg_t * msg )
{
	xfree ( msg ) ;
}




void slurm_free_ctl_conf ( slurm_ctl_conf_info_msg_t * build_ptr )
{
	if ( build_ptr ) 
	{
		if ( build_ptr->backup_location )
			xfree ( build_ptr->backup_location ) ;
		if ( build_ptr->backup_machine )
			xfree ( build_ptr->backup_machine ) ;
		if ( build_ptr->control_daemon )
			xfree ( build_ptr->control_daemon ) ;
		if ( build_ptr->control_machine )
			xfree ( build_ptr->control_machine ) ;
		if ( build_ptr->epilog )
			xfree ( build_ptr->epilog ) ;
		if ( build_ptr->init_program )
			xfree ( build_ptr->init_program ) ;
		if ( build_ptr->prolog )
			xfree ( build_ptr->prolog ) ;
		if ( build_ptr->server_daemon )
			xfree ( build_ptr->server_daemon ) ;
		if ( build_ptr->slurm_conf )
			xfree ( build_ptr->slurm_conf ) ;
		if ( build_ptr->tmp_fs )
			xfree ( build_ptr->tmp_fs ) ;
		if ( build_ptr->server_daemon )
			xfree ( build_ptr->server_daemon ) ;
		xfree ( build_ptr ) ;
	}
}

void slurm_free_job_desc_msg ( job_desc_msg_t * msg )
{
	if ( msg )
	{
		if ( msg->features )
			xfree ( msg->features ) ;
		if ( msg->groups )
			xfree ( msg->groups ) ;
		if ( msg->name )
			xfree ( msg->name ) ;
		if ( msg->partition_key )
			xfree ( msg->partition_key ) ;
		if ( msg->partition )
			xfree ( msg->partition ) ;
		if ( msg->req_nodes )
			xfree ( msg->req_nodes ) ;
		if ( msg->job_script )
			xfree ( msg->job_script ) ;
		xfree ( msg ) ;
	}
}

void slurm_free_partition_info (partition_info_msg_t * msg )
{
	int i; 
	if ( msg )
	{
		if ( msg -> partition_array )
		{
			for (i = 0; i < msg -> record_count; i++) {
				slurm_free_partition_table ( & ( msg->partition_array[i] ) ) ;
			}
		}
		xfree ( msg );
	}
}

void slurm_free_partition_table_msg ( partition_table_t * part )
{
	if ( part )
	{
		slurm_free_partition_table ( part ) ;
		xfree ( part ) ;
	}
}

void slurm_free_partition_table ( partition_table_t * part )
{
	if ( part )
	{
		if ( part->name )
			xfree ( part->name ) ;
		if ( part->allow_groups )
			xfree ( part->allow_groups ) ;
		if ( part->nodes )
			xfree ( part->nodes ) ;
		if ( part->node_inx )
			xfree ( part->node_inx ) ;
	}
}

void slurm_free_job_info ( job_info_msg_t * msg )
{
	int i; 
	if ( msg )
	{
		if ( msg -> job_array )
		{
			for (i = 0; i < msg -> record_count; i++) {
				slurm_free_job_table ( & ( msg->job_array[i] ) ) ;
			}
		}
		xfree ( msg );
	}
}

void slurm_free_job_table_msg ( job_table_t * job )
{
	if ( job )
	{
		slurm_free_job_table ( job ) ;
		xfree ( job ) ;
	}
}

void slurm_free_job_table ( job_table_t * job )
{
	if ( job )
	{
		if ( job->nodes )
			xfree (job->nodes) ;
		if ( job->partition )
			xfree (job->partition) ;
		if ( job->name )
			xfree (job->name) ;
		if ( job->node_inx )
			xfree (job->node_inx) ;
		if ( job->req_nodes )
			xfree (job->req_nodes) ;
		if ( job->features )
			xfree (job->features) ;
		if ( job->job_script )
			xfree (job->job_script) ;
		xfree (job->req_node_inx) ;
	}
}

void slurm_free_node_info ( node_info_msg_t * msg )
{
	int i; 
	if ( msg )
	{
		if ( msg -> node_array )
		{
			for (i = 0; i < msg -> record_count; i++) {
				slurm_free_node_table ( & ( msg->node_array[i] ) ) ;
			}
		}
		xfree ( msg );
	}
}

void slurm_free_node_table_msg ( node_table_msg_t * node )
{
	if ( node )
	{
		slurm_free_node_table ( node ) ;
		xfree ( node ) ;
	}
}

void slurm_free_node_table ( node_table_t * node )
{
	if ( node )
	{
		if ( node->name )
			xfree ( node->name ) ;
		if ( node->features )
			xfree ( node->features ) ;
		if ( node->partition )
			xfree ( node->partition ) ;
	}
}

void slurm_free_resource_allocation_response_msg ( resource_allocation_response_msg_t * msg )
{
	if ( msg )
	{
		if ( msg->node_list )
			xfree ( msg->node_list ) ;
		if ( msg->cpus_per_node )
			xfree ( msg->cpus_per_node ) ;
		if ( msg->cpu_count_reps )
			xfree ( msg->cpu_count_reps ) ;
		xfree ( msg ) ;
	}
}

void slurm_free_node_registration_status_msg ( slurm_node_registration_status_msg_t * msg )
{
	if ( msg )
	{
		if ( msg -> node_name )
			xfree ( msg -> node_name ) ;
		xfree ( msg ) ;
	}
}


void slurm_free_update_node_msg ( update_node_msg_t * msg )
{
	if ( msg )
	{
		if ( msg -> node_names )
			xfree ( msg -> node_names ) ;
		xfree ( msg ) ;
	}
}

void slurm_free_launch_tasks_msg ( launch_tasks_msg_t * msg )
{
	if ( msg )
	{
		if ( msg -> credentials )
			xfree ( msg -> credentials );
		if ( msg -> env )
			xfree ( msg -> env );
		if ( msg -> cwd )
			xfree ( msg -> cwd );
		if ( msg -> cmd_line )
			xfree ( msg -> cmd_line );
		xfree ( msg ) ;
	}
	/*stdin location*/
	/*stdout location*/
	/*stderr location*/
	/*task completion location*/
} 

void slurm_free_kill_tasks_msg ( kill_tasks_msg_t * msg )
{
	if ( msg )
	{
		xfree ( msg ) ;
	}
}

/**********************
 ***********************
 Init functions
 ***********************
 **********************/


void slurm_init_job_desc_msg ( job_desc_msg_t * job_desc_msg )
{
	job_desc_msg -> contiguous = SLURM_JOB_DESC_DEFAULT_CONTIGUOUS ;
	job_desc_msg -> dist = SLURM_JOB_DESC_DEFAULT_DIST ;
	job_desc_msg -> features = SLURM_JOB_DESC_DEFAULT_FEATURES ;
	job_desc_msg -> groups = SLURM_JOB_DESC_DEFAULT_GROUPS ; /* will be set by api */
	job_desc_msg -> job_id = SLURM_JOB_DESC_DEFAULT_JOB_ID ; /* will be set by api */
	job_desc_msg -> name = SLURM_JOB_DESC_DEFAULT_JOB_NAME  ;
	job_desc_msg -> partition_key = SLURM_JOB_DESC_DEFAULT_PARITION_KEY ;
	job_desc_msg -> min_procs = SLURM_JOB_DESC_DEFAULT_MIN_PROCS ;
	job_desc_msg -> min_memory = SLURM_JOB_DESC_DEFAULT_MIN_MEMORY ;
	job_desc_msg -> min_tmp_disk = SLURM_JOB_DESC_DEFAULT_MIN_TMP_DISK ;
	job_desc_msg -> partition = SLURM_JOB_DESC_DEFAULT_PARTITION ;
	job_desc_msg -> priority = SLURM_JOB_DESC_DEFAULT_PRIORITY ;
        job_desc_msg -> procs_per_task = SLURM_JOB_DESC_DEFAULT_PROCS_PER_TASK ;
	job_desc_msg -> req_nodes = SLURM_JOB_DESC_DEFAULT_REQ_NODES ;
	job_desc_msg -> job_script = SLURM_JOB_DESC_DEFAULT_JOB_SCRIPT ;
	job_desc_msg -> shared = SLURM_JOB_DESC_DEFAULT_SHARED ;
	job_desc_msg -> time_limit = SLURM_JOB_DESC_DEFAULT_TIME_LIMIT ;
	job_desc_msg -> num_procs = SLURM_JOB_DESC_DEFAULT_NUM_PROCS ;
	job_desc_msg -> num_nodes = SLURM_JOB_DESC_DEFAULT_NUM_NODES ;
	job_desc_msg -> user_id = SLURM_JOB_DESC_DEFAULT_USER_ID ;
}
