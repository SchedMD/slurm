#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include <src/common/slurm_protocol_pack.h>
#include <src/common/pack.h>
#include <src/common/log.h>
#include <src/slurmctld/slurmctld.h>

/* pack_header
 * packs a slurm protocol header that proceeds every slurm message
 * header 	- the header structure to pack
 * buffer	- destination of the pack, note buffer will be incremented by underlying pack routines
 * length	- length of buffer, note length will be decremented by underlying pack routines
 */
void pack_header ( header_t * header, char ** buffer , uint32_t * length )
{
	pack16 ( header -> version , ( void ** ) buffer , length ) ;
	pack16 ( header -> flags , ( void ** ) buffer , length ) ;
	pack16 ( header -> msg_type , ( void ** ) buffer , length ) ;
	pack32 ( header -> body_length , ( void ** ) buffer , length ) ;
}

/* unpack_header
 * unpacks a slurm protocol header that proceeds every slurm message
 * header 	- the header structure to unpack
 * buffer	- destination of the pack, note buffer will be incremented by underlying unpack routines
 * length	- length of buffer, note length will be decremented by underlying unpack routines
 */
void unpack_header ( header_t * header , char ** buffer , uint32_t * length )
{
	unpack16 ( & header -> version , ( void ** ) buffer , length ) ;
	unpack16 ( & header -> flags , ( void ** ) buffer , length ) ;
	unpack16 ( & header -> msg_type , ( void ** ) buffer , length ) ;
	unpack32 ( & header -> body_length , ( void ** ) buffer , length ) ;
}

/* pack_msg
 * packs a slurm protocol mesg body
 * header 	- the body structure to pack
 * buffer	- destination of the pack, note buffer will be incremented by underlying pack routines
 * length	- length of buffer, note length will be decremented by underlying pack routines
 */
int pack_msg ( slurm_msg_t const * msg , char ** buffer , uint32_t * buf_len )
{
	switch ( msg -> msg_type )
	{
		case REQUEST_BUILD_INFO :
		case REQUEST_JOB_INFO :
		case REQUEST_JOB_STEP_INFO :
		case REQUEST_NODE_INFO :
		case REQUEST_PARTITION_INFO :
		case REQUEST_ACCTING_INFO :
			pack_last_update ( ( last_update_msg_t * ) msg -> data , ( void ** ) buffer , buf_len ) ;
			break;
		case RESPONSE_BUILD_INFO:
			pack_build_info ( ( build_info_msg_t * ) msg -> data , (void ** ) buffer , buf_len ) ;
			break ;
		case REQUEST_NODE_REGISRATION_STATUS :
			break ;
		case MESSAGE_NODE_REGISRATION_STATUS :
			break ;
		case REQUEST_RESOURCE_ALLOCATION :
		case REQUEST_SUBMIT_BATCH_JOB :
			pack_job_desc ( (job_desc_msg_t * )  msg -> data , ( void ** ) buffer , buf_len )  ;
			break ;
		case RESPONSE_RESOURCE_ALLOCATION :
			break ;
		case RESPONSE_SUBMIT_BATCH_JOB :
			break ;
		case REQUEST_CANCEL_JOB :
			break ;
		case REQUEST_CANCEL_JOB_STEP :
			break ;
		case REQUEST_SIGNAL_JOB :
			break ;
		case REQUEST_SIGNAL_JOB_STEP :
			break ;
		case REQUEST_RECONFIGURE :
			break ;
		case RESPONSE_CANCEL_JOB :
		case RESPONSE_RECONFIGURE :
		case RESPONSE_CANCEL_JOB_STEP :
		case RESPONSE_SIGNAL_JOB :
		case RESPONSE_SIGNAL_JOB_STEP :
			break ;
		case RESPONSE_JOB_INFO :
			break ;
		case REQUEST_JOB_ATTACH :
			break ;
		case RESPONSE_JOB_ATTACH :
			break ;
		case REQUEST_LAUNCH_TASKS :
			break ;
		case REQUEST_GET_JOB_STEP_INFO :
			break ;
		case RESPONSE_GET_JOB_STEP_INFO :
			break ;
		case REQUEST_JOB_RESOURCE :
			break ;
		case RESPONSE_JOB_RESOURCE :
			break ;
		case REQUEST_RUN_JOB_STEP :
			break ;
		case RESPONSE_RUN_JOB_STEP:
			break ;
		case REQUEST_GET_KEY :
			break ;
		case RESPONSE_GET_KEY :
			break ;
		case MESSAGE_TASK_EXIT :
			break ;
		case REQUEST_BATCH_JOB_LAUNCH :
			break ;
		case MESSAGE_UPLOAD_ACCOUNTING_INFO :
			break ;
		default :
			debug ( "No pack method for msg type %i",  msg -> msg_type ) ;
			return EINVAL ;
			break;
		
	}
	return 0 ;
}

/* unpack_msg
 * unpacks a slurm protocol msg body
 * header 	- the body structure to unpack
 * buffer	- destination of the pack, note buffer will be incremented by underlying unpack routines
 * length	- length of buffer, note length will be decremented by underlying unpack routines
 */
int unpack_msg ( slurm_msg_t * msg , char ** buffer , uint32_t * buf_len )
{
	switch ( msg-> msg_type )
	{
		case REQUEST_BUILD_INFO :
		case REQUEST_JOB_INFO :
		case REQUEST_JOB_STEP_INFO :
		case REQUEST_NODE_INFO :
		case REQUEST_PARTITION_INFO :
		case REQUEST_ACCTING_INFO :
			unpack_last_update ( ( last_update_msg_t **) &(msg -> data)  , ( void ** ) buffer , buf_len ) ;
			break;
		case RESPONSE_BUILD_INFO:
			unpack_build_info ( ( build_info_msg_t ** ) &(msg -> data) , (void ** ) buffer , buf_len ) ;
			break;
		case REQUEST_NODE_REGISRATION_STATUS :
			break ;
		case MESSAGE_NODE_REGISRATION_STATUS :
			break ;
		case REQUEST_RESOURCE_ALLOCATION :
		case REQUEST_SUBMIT_BATCH_JOB :
			unpack_job_desc ( ( job_desc_msg_t **) & ( msg-> data ), ( void ** ) buffer , buf_len ) ;
			break ;
		case RESPONSE_RESOURCE_ALLOCATION :
			break ;
		case RESPONSE_SUBMIT_BATCH_JOB :
			break ;
		case REQUEST_CANCEL_JOB :
			break ;
		case REQUEST_CANCEL_JOB_STEP :
			break ;
		case REQUEST_SIGNAL_JOB :
			break ;
		case REQUEST_SIGNAL_JOB_STEP :
			break ;
		case REQUEST_RECONFIGURE :
			break ;
		case RESPONSE_CANCEL_JOB :
		case RESPONSE_RECONFIGURE :
		case RESPONSE_CANCEL_JOB_STEP :
		case RESPONSE_SIGNAL_JOB :
		case RESPONSE_SIGNAL_JOB_STEP :
			break ;
		case REQUEST_JOB_ATTACH :
			break ;
		case RESPONSE_JOB_ATTACH :
			break ;
		case REQUEST_LAUNCH_TASKS :
			break ;
		case REQUEST_GET_JOB_STEP_INFO :
			break ;
		case RESPONSE_GET_JOB_STEP_INFO :
			break ;
		case REQUEST_JOB_RESOURCE :
			break ;
		case RESPONSE_JOB_RESOURCE :
			break ;
		case REQUEST_RUN_JOB_STEP :
			break ;
		case RESPONSE_RUN_JOB_STEP:
			break ;
		case REQUEST_GET_KEY :
			break ;
		case RESPONSE_GET_KEY :
			break ;
		case MESSAGE_TASK_EXIT :
			break ;
		case REQUEST_BATCH_JOB_LAUNCH :
			break ;
		case MESSAGE_UPLOAD_ACCOUNTING_INFO :
			break ;
		default :
			debug ( "No pack method for msg type %i",  msg -> msg_type ) ;
			return EINVAL ;
			break;
		
	}
	return 0 ;
}

void pack_node_registration_status_msg ( node_registration_status_msg_t * msg, char ** buffer , uint32_t * length )
{
	pack32 ( msg -> timestamp , ( void ** ) buffer , length ) ;
	packstr ( msg -> node_name , ( void ** ) buffer , length ) ;
	pack32 ( msg -> cpus , ( void ** ) buffer , length ) ;
	pack32 ( msg -> real_memory_size , ( void ** ) buffer , length ) ;
	pack32 ( msg -> temporary_disk_space , ( void ** ) buffer , length ) ;
}

int unpack_node_registration_status_msg ( node_registration_status_msg_t ** msg , char ** buffer , uint32_t * length )
{
	uint16_t uint16_tmp;
	node_registration_status_msg_t * node_reg_ptr ;
	/* alloc memory for structure */	
	node_reg_ptr = malloc ( sizeof ( node_registration_status_msg_t ) ) ;
	if (node_reg_ptr == NULL) 
	{
		return ENOMEM;
	}

	/* load the data values */
	/* unpack timestamp of snapshot */
	unpack32 ( & node_reg_ptr -> timestamp , ( void ** ) buffer , length ) ;
	unpackstr_ptr_malloc ( & node_reg_ptr -> node_name , &uint16_tmp,  ( void ** ) buffer , length ) ;
	unpack32 ( & node_reg_ptr -> cpus , ( void ** ) buffer , length ) ;
	unpack32 ( & node_reg_ptr -> real_memory_size , ( void ** ) buffer , length ) ;
	unpack32 ( & node_reg_ptr -> temporary_disk_space , ( void ** ) buffer , length ) ;
	*msg = node_reg_ptr ;
	return 0 ;
}

void pack_build_info ( build_info_msg_t * build_ptr, void ** buf_ptr , int * buffer_size )
{	
	pack32 (build_ptr->last_update, buf_ptr, buffer_size);
	pack16 (build_ptr->backup_interval, buf_ptr, buffer_size);
	packstr (build_ptr->backup_location, buf_ptr, buffer_size);
	packstr (build_ptr->backup_machine, buf_ptr, buffer_size);
	packstr (build_ptr->control_daemon, buf_ptr, buffer_size);
	packstr (build_ptr->control_machine, buf_ptr, buffer_size);
	pack16 (build_ptr->controller_timeout, buf_ptr, buffer_size);
	packstr (build_ptr->epilog, buf_ptr, buffer_size);
	pack16 (build_ptr->fast_schedule, buf_ptr, buffer_size);
	pack16 (build_ptr->hash_base, buf_ptr, buffer_size);
	pack16 (build_ptr->heartbeat_interval, buf_ptr, buffer_size);
	packstr (build_ptr->init_program, buf_ptr, buffer_size);
	pack16 (build_ptr->kill_wait, buf_ptr, buffer_size);
	packstr (build_ptr->prioritize, buf_ptr, buffer_size);
	packstr (build_ptr->prolog, buf_ptr, buffer_size);
	packstr (build_ptr->server_daemon, buf_ptr, buffer_size);
	pack16 (build_ptr->server_timeout, buf_ptr, buffer_size);
	packstr (build_ptr->slurm_conf, buf_ptr, buffer_size);
	packstr (build_ptr->tmp_fs, buf_ptr, buffer_size);
}

int unpack_build_info ( build_info_msg_t **build_buffer_ptr, void ** buffer , int * buffer_size )
{	
	uint16_t uint16_tmp;
	build_info_msg_t * build_ptr ;
	/* alloc memory for structure */	
	build_ptr = malloc ( sizeof ( struct build_table ) ) ;
	if (build_ptr == NULL) 
	{
		return ENOMEM;
	}

	/* load the data values */
	/* unpack timestamp of snapshot */
	unpack32 (&build_ptr->last_update, buffer, buffer_size);
	unpack16 (&build_ptr->backup_interval, buffer, buffer_size);
	unpackstr_ptr_malloc (&build_ptr->backup_location, &uint16_tmp, buffer, buffer_size);
	unpackstr_ptr_malloc (&build_ptr->backup_machine, &uint16_tmp, buffer, buffer_size);
	unpackstr_ptr_malloc (&build_ptr->control_daemon, &uint16_tmp, buffer, buffer_size);
	unpackstr_ptr_malloc (&build_ptr->control_machine, &uint16_tmp, buffer, buffer_size);
	unpack16 (&build_ptr->controller_timeout, buffer, buffer_size);
	unpackstr_ptr_malloc (&build_ptr->epilog, &uint16_tmp, buffer, buffer_size);
	unpack16 (&build_ptr->fast_schedule, buffer, buffer_size);
	unpack16 (&build_ptr->hash_base, buffer, buffer_size);
	unpack16 (&build_ptr->heartbeat_interval, buffer, buffer_size);
	unpackstr_ptr_malloc (&build_ptr->init_program, &uint16_tmp, buffer, buffer_size);
	unpack16 (&build_ptr->kill_wait, buffer, buffer_size);
	unpackstr_ptr_malloc (&build_ptr->prioritize, &uint16_tmp, buffer, buffer_size);
	unpackstr_ptr_malloc (&build_ptr->prolog, &uint16_tmp, buffer, buffer_size);
	unpackstr_ptr_malloc (&build_ptr->server_daemon, &uint16_tmp, buffer, buffer_size);
	unpack16 (&build_ptr->server_timeout, buffer, buffer_size);
	unpackstr_ptr_malloc (&build_ptr->slurm_conf, &uint16_tmp, buffer, buffer_size);
	unpackstr_ptr_malloc (&build_ptr->tmp_fs, &uint16_tmp, buffer, buffer_size);
	*build_buffer_ptr = build_ptr ;
	return 0 ;
}

/* pack_job_desc
 * packs a job_desc struct 
 * header 	- the body structure to pack
 * buf_ptr	- destination of the pack, note buffer will be incremented by underlying pack routines
 * buffer_size	- length of buffer, note length will be decremented by underlying pack routines
 */
void pack_job_desc ( job_desc_msg_t * job_desc_ptr, void ** buf_ptr , int * buffer_size )
{	
	/* load the data values */
	/* unpack timestamp of snapshot */

	pack16 (job_desc_ptr->contiguous, buf_ptr, buffer_size);
	packstr (job_desc_ptr->features, buf_ptr, buffer_size);
	packstr (job_desc_ptr->groups, buf_ptr, buffer_size);
	pack32 (job_desc_ptr->job_id, buf_ptr, buffer_size);
	packstr (job_desc_ptr->name, buf_ptr, buffer_size);
	packmem (job_desc_ptr->partition_key, 32, buf_ptr, buffer_size);
	
	pack32 (job_desc_ptr->min_procs, buf_ptr, buffer_size);
	pack32 (job_desc_ptr->min_memory, buf_ptr, buffer_size);
	pack32 (job_desc_ptr->min_tmp_disk, buf_ptr, buffer_size);
	
	packstr (job_desc_ptr->partition, buf_ptr, buffer_size);
	pack32 (job_desc_ptr->priority, buf_ptr, buffer_size);
	
	packstr (job_desc_ptr->partition, buf_ptr, buffer_size);
	packstr (job_desc_ptr->partition, buf_ptr, buffer_size);
	pack16 (job_desc_ptr->shared, buf_ptr, buffer_size);

	pack32 (job_desc_ptr->time_limit, buf_ptr, buffer_size);
	
	pack32 (job_desc_ptr->num_procs, buf_ptr, buffer_size);
	pack32 (job_desc_ptr->num_nodes, buf_ptr, buffer_size);
	pack32 (job_desc_ptr->user_id, buf_ptr, buffer_size);

}

/* unpack_msg
 * unpacks a job_desc struct
 * header 	- the body structure to unpack
 * buf_ptr	- destination of the pack, note buffer will be incremented by underlying unpack routines
 * buffer_size	- length of buffer, note length will be decremented by underlying unpack routines
 */
int unpack_job_desc ( job_desc_msg_t **job_desc_buffer_ptr, void ** buf_ptr , int * buffer_size )
{	
	uint16_t uint16_tmp;
	job_desc_msg_t * job_desc_ptr ;

	/* alloc memory for structure */
	job_desc_ptr = malloc ( sizeof ( job_desc_msg_t ) ) ;
	if (job_desc_ptr== NULL) 
	{
		*job_desc_buffer_ptr = NULL ;
		return ENOMEM ;
	}

	/* load the data values */
	/* unpack timestamp of snapshot */

	unpack16 (&job_desc_ptr->contiguous, buf_ptr, buffer_size);
	unpackstr_ptr_malloc (&job_desc_ptr->features, &uint16_tmp, buf_ptr, buffer_size);
	unpackstr_ptr_malloc (&job_desc_ptr->groups, &uint16_tmp, buf_ptr, buffer_size);
	unpack32 (&job_desc_ptr->job_id, buf_ptr, buffer_size);
	unpackstr_ptr_malloc (&job_desc_ptr->name, &uint16_tmp, buf_ptr, buffer_size);
	unpackmem_malloc ( ( char ** ) &job_desc_ptr->partition_key, &uint16_tmp, buf_ptr, buffer_size);
	
	unpack32 (&job_desc_ptr->min_procs, buf_ptr, buffer_size);
	unpack32 (&job_desc_ptr->min_memory, buf_ptr, buffer_size);
	unpack32 (&job_desc_ptr->min_tmp_disk, buf_ptr, buffer_size);
	
	unpackstr_ptr_malloc (&job_desc_ptr->partition, &uint16_tmp, buf_ptr, buffer_size);
	unpack32 (&job_desc_ptr->priority, buf_ptr, buffer_size);
	
	unpackstr_ptr_malloc (&job_desc_ptr->partition, &uint16_tmp, buf_ptr, buffer_size);
	unpackstr_ptr_malloc (&job_desc_ptr->partition, &uint16_tmp, buf_ptr, buffer_size);
	unpack16 (&job_desc_ptr->shared, buf_ptr, buffer_size);
	
	unpack32 (&job_desc_ptr->time_limit, buf_ptr, buffer_size);
	
	unpack32 (&job_desc_ptr->num_procs, buf_ptr, buffer_size);
	unpack32 (&job_desc_ptr->num_nodes, buf_ptr, buffer_size);
	unpack32 (&job_desc_ptr->user_id, buf_ptr, buffer_size);

	*job_desc_buffer_ptr = job_desc_ptr ;
	return 0 ;
}

void pack_last_update ( last_update_msg_t * msg , void ** buffer , uint32_t * length )
{
	pack32 ( msg -> last_update , buffer , length ) ;
}

int unpack_last_update ( last_update_msg_t ** msg , void ** buffer , uint32_t * length )
{
        last_update_msg_t * last_update_msg ;

        last_update_msg = malloc ( sizeof ( last_update_msg_t ) ) ;
        if ( last_update_msg == NULL)
        {
                *msg = NULL ;
                return ENOMEM ;
        }

	unpack32 ( & last_update_msg -> last_update , buffer , length ) ;
	*msg = last_update_msg ;
	return 0 ;
}

/* template 
void pack_ ( * msg , void ** buffer , uint32_t * length )
{
	pack16 ( msg -> , buffer , length ) ;
	pack32 ( msg -> , buffer , length ) ;
}

void unpack_ ( ** msg , void char ** buffer , uint32_t * length )
{
	uint16_t uint16_tmp;
	job_desc_msg_t * job_desc_ptr ;

	job_desc_ptr = malloc ( sizeof ( job_desc_msg_t ) ) ;
	if (job_desc_ptr== NULL) 
	{
		*msg = NULL ;
		return ;
	}

	unpack16 ( & msg -> , buffer , length ) ;
	unpack32 ( & msg -> , buffer , length ) ;
}
*/
