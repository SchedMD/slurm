#include <src/common/slurm_protocol_defs.h>
#include <stdlib.h>
#include <src/slurmctld/slurmctld.h>

void slurm_free_last_update_msg ( last_update_msg_t * msg )
{
	free ( msg ) ;
}

void slurm_free_job_id_msg ( job_id_msg_t * msg )
{
	free ( msg ) ;
}

void slurm_free_job_desc_msg ( job_desc_msg_t * msg )
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

void slurm_free_job_info_msg ( job_info_msg_t * msg )
{
	int i; 
	for (i = 0; i < msg -> record_count; i++) {
		slurm_free_job_table ( & ( msg->job_array[i] ) ) ;
	}
	free ( msg );
}

void slurm_free_job_table ( job_table_t * job )
{
	if ( job )
	{
		if (job->nodes) free (job->nodes) ;
		if (job->partition) free (job->partition) ;
		if (job->name) free (job->name) ;
		if (job->node_inx) free (job->node_inx) ;
		if (job->req_nodes) free (job->req_nodes) ;
		if (job->features) free (job->features) ;
		if (job->job_script) free (job->job_script) ;
		if (job->req_node_inx) free (job->req_node_inx) ;
		free ( job ) ;
	}
}
