#include <src/common/slurm_protocol_defs.h>
#include <stdlib.h>
#include <src/slurmctld/slurmctld.h>
/* short messages*/
void slurm_free_last_update_msg ( last_update_msg_t * msg )
{
	free ( msg ) ;
}

void slurm_free_job_id_msg ( job_id_msg_t * msg )
{
	free ( msg ) ;
}

void slurm_free_return_code_msg ( return_code_msg_t * msg )
{
	free ( msg ) ;
}




void slurm_free_build_info ( build_info_msg_t * build_ptr )
{
	if ( build_ptr ) 
	{
		free ( build_ptr->backup_location ) ;
		free ( build_ptr->backup_machine ) ;
		free ( build_ptr->control_daemon ) ;
		free ( build_ptr->control_machine ) ;
		free ( build_ptr->epilog ) ;
		free ( build_ptr->init_program ) ;
		free ( build_ptr->prolog ) ;
		free ( build_ptr->server_daemon ) ;
		free ( build_ptr->slurm_conf ) ;
		free ( build_ptr->tmp_fs ) ;
		free ( build_ptr ) ;
	}
}

void slurm_free_job_desc_msg ( job_desc_msg_t * msg )
{
	if ( msg )
	{
		free ( msg->features ) ;
		free ( msg->groups ) ;
		free ( msg->name ) ;
		free ( msg->partition_key ) ;
		free ( msg->partition ) ;
		free ( msg->req_nodes ) ;
		free ( msg->job_script ) ;
		free ( msg ) ;
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
		free ( msg );
	}
}

void slurm_free_partition_table_msg ( partition_table_t * part )
{
	if ( part )
	{
		slurm_free_partition_table ( part ) ;
		free ( part ) ;
	}
}

void slurm_free_partition_table ( partition_table_t * part )
{
	if ( part )
	{
		free ( part->name ) ;
		free ( part->allow_groups ) ;
		free ( part->nodes ) ;
		free ( part->node_inx ) ;
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
		free ( msg );
	}
}

void slurm_free_job_table_msg ( job_table_t * job )
{
	if ( job )
	{
		slurm_free_job_table ( job ) ;
		free ( job ) ;
	}
}

void slurm_free_job_table ( job_table_t * job )
{
	if ( job )
	{
		free (job->nodes) ;
		free (job->partition) ;
		free (job->name) ;
		free (job->node_inx) ;
		free (job->req_nodes) ;
		free (job->features) ;
		free (job->job_script) ;
		free (job->req_node_inx) ;
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
		free ( msg );
	}
}

void slurm_free_node_table_msg ( node_table_msg_t * node )
{
	if ( node )
	{
		slurm_free_node_table ( node ) ;
		free ( node ) ;
	}
}

void slurm_free_node_table ( node_table_t * node )
{
	if ( node )
	{
		free (node->name) ;
		free (node->features) ;
		free (node->partition) ;
	}
}


#define SLURM_JOB_DESC_NONCONTIGUOUS		0
#define SLURM_JOB_DESC_CONTIGUOUS		1 
#define SLURM_JOB_DESC_DEFAULT_FEATURES		"NONE"
#define SLURM_JOB_DESC_DEFAULT_GROUPS		"SET_BY_API"
#define SLURM_JOB_DESC_DEFAULT_JOB_ID		0
#define SLURM_JOB_DESC_DEFAULT_JOB_NAME 	NULL
#define SLURM_JOB_DESC_DEFAULT_PARITION_KEY	NULL
#define SLURM_JOB_DESC_DEFAULT_MIN_PROCS	0
#define SLURM_JOB_DESC_DEFAULT_MIN_MEMORY	0
#define SLURM_JOB_DESC_DEFAULT_MIN_TMP_DISK	0
#define SLURM_JOB_DESC_DEFAULT_PARTITION	NULL
#define SLURM_JOB_DESC_DEFAULT_PRIORITY		0xfffffffe
#define SLURM_JOB_DESC_DEFAULT_REQ_NODES	NULL
#define SLURM_JOB_DESC_DEFAULT_JOB_SCRIPT	NULL
#define SLURM_JOB_DESC_DEFAULT_SHARED		0
#define SLURM_JOB_DESC_DEFAULT_TIME_LIMIT	0xfffffffe
#define SLURM_JOB_DESC_DEFAULT_NUM_PROCS	0
#define SLURM_JOB_DESC_DEFAULT_NUM_NODES	0
#define SLURM_JOB_DESC_DEFAULT_USER_ID		0 

void slurm_init_job_desc_msg ( job_desc_msg_t * job_desc_msg )
{
	job_desc_msg -> contiguous = SLURM_JOB_DESC_NONCONTIGUOUS ;
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
	job_desc_msg -> req_nodes = SLURM_JOB_DESC_DEFAULT_REQ_NODES ;
	job_desc_msg -> job_script = SLURM_JOB_DESC_DEFAULT_JOB_SCRIPT ;
	job_desc_msg -> shared = SLURM_JOB_DESC_DEFAULT_SHARED ;
	job_desc_msg -> time_limit = SLURM_JOB_DESC_DEFAULT_TIME_LIMIT ;
	job_desc_msg -> num_procs = SLURM_JOB_DESC_DEFAULT_NUM_PROCS ;
	job_desc_msg -> num_nodes = SLURM_JOB_DESC_DEFAULT_NUM_NODES ;
	job_desc_msg -> user_id = SLURM_JOB_DESC_DEFAULT_USER_ID ;
}
