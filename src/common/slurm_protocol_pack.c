/****************************************************************************\
 * slurm_protocol_pack.c - functions to pack and unpack structures for RPCs
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Kevin Tew <tew1@llnl.gov>.
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
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include <src/common/bitstring.h>
#include <src/common/slurm_protocol_pack.h>
#include <src/common/slurm_protocol_api.h>
#include <src/common/pack.h>
#include <src/common/log.h>
#include <src/common/xmalloc.h>


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
	pack16 ( (uint16_t)header -> msg_type , ( void ** ) buffer , length ) ;
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
	uint16_t tmp=0;
	unpack16 ( & header -> version , ( void ** ) buffer , length ) ;
	unpack16 ( & header -> flags , ( void ** ) buffer , length ) ;
	unpack16 ( & tmp , ( void ** ) buffer , length ) ;
	header -> msg_type = (slurm_msg_type_t )tmp;
	unpack32 ( & header -> body_length , ( void ** ) buffer , length ) ;
}

void pack_io_stream_header ( slurm_io_stream_header_t * msg , void ** buffer , uint32_t * length )
{
	assert ( msg != NULL );

	pack16( msg->version, buffer, length ) ;
	packmem( msg->key, SLURM_SSL_SIGNATURE_LENGTH , buffer, length ) ; 
	pack32( msg->task_id, buffer, length ) ;	
	pack16( msg->type, buffer, length ) ;
}

void unpack_io_stream_header ( slurm_io_stream_header_t * msg , void ** buffer , uint32_t * length )
{
	uint16_t uint16_tmp;
	
	unpack16( & msg->version, buffer, length ) ;
	unpackmem( msg->key, & uint16_tmp , buffer , length ) ; 
	unpack32( & msg->task_id, buffer, length ) ;	
	unpack16( & msg->type, buffer, length ) ;
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
			pack_slurm_ctl_conf ( ( slurm_ctl_conf_info_msg_t * ) msg -> data , (void ** ) buffer , buf_len ) ;
			break ;
		case RESPONSE_JOB_INFO:
			pack_job_info_msg ( ( slurm_msg_t * ) msg , (void ** ) buffer , buf_len ) ;
			break ;
		case RESPONSE_PARTITION_INFO:
			pack_partition_info_msg ( ( slurm_msg_t * ) msg , (void ** ) buffer , buf_len ) ;
			break ;
		case RESPONSE_NODE_INFO:
			pack_node_info_msg ( ( slurm_msg_t * ) msg , (void ** ) buffer , buf_len ) ;
			break ;
		case MESSAGE_NODE_REGISTRATION_STATUS :
			pack_node_registration_status_msg ( ( slurm_node_registration_status_msg_t * ) msg -> data , ( void ** ) buffer , buf_len );
			break ;
		case REQUEST_RESOURCE_ALLOCATION :
		case REQUEST_SUBMIT_BATCH_JOB :
		case REQUEST_IMMEDIATE_RESOURCE_ALLOCATION : 
		case REQUEST_JOB_WILL_RUN : 
			pack_job_desc ( (job_desc_msg_t * )  msg -> data , ( void ** ) buffer , buf_len )  ;
			break ;
		case REQUEST_NODE_REGISTRATION_STATUS :
		case REQUEST_RECONFIGURE :
			/* Message contains no body/information */
			break ;
		case RESPONSE_SUBMIT_BATCH_JOB:
			pack_submit_response_msg ( ( submit_response_msg_t * ) msg -> data , ( void ** ) buffer , buf_len ) ;
			break ;
		case RESPONSE_RESOURCE_ALLOCATION :
		case RESPONSE_IMMEDIATE_RESOURCE_ALLOCATION : 
		case RESPONSE_JOB_WILL_RUN :
			pack_resource_allocation_response_msg ( ( resource_allocation_response_msg_t * ) msg -> data , 
				( void ** ) buffer , buf_len ) ;
			break ;
		case REQUEST_UPDATE_JOB :
			pack_job_desc ( (job_desc_msg_t * )  msg -> data , 
				( void ** ) buffer , buf_len )  ;
			break ;
			break ;
		case REQUEST_UPDATE_NODE :
			pack_update_node_msg ( ( update_node_msg_t * ) msg-> data , 
				( void ** ) buffer , buf_len ) ;
			break ;
		case REQUEST_UPDATE_PARTITION :
			pack_update_partition_msg ( ( update_part_msg_t * ) msg->data , 
				( void ** ) buffer ,  buf_len ) ;
			break ;
		case REQUEST_REATTACH_TASKS_STREAMS :
			pack_reattach_tasks_streams_msg ( ( reattach_tasks_streams_msg_t * ) msg->data , ( void ** ) buffer , buf_len ) ;
			break ;
		case REQUEST_LAUNCH_TASKS :
			pack_launch_tasks_request_msg ( ( launch_tasks_request_msg_t * ) msg->data , ( void ** ) buffer , buf_len ) ;
			break ;
		case RESPONSE_LAUNCH_TASKS :
			pack_launch_tasks_response_msg ( ( launch_tasks_response_msg_t * ) msg->data , ( void ** ) buffer , buf_len ) ;
			break ;
		case REQUEST_KILL_TASKS :
			pack_cancel_tasks_msg ( ( kill_tasks_msg_t * ) msg->data , ( void ** ) buffer , buf_len ) ;
			break ;
		case REQUEST_CANCEL_JOB_STEP :
		case REQUEST_COMPLETE_JOB_STEP :
			pack_cancel_job_step_msg ( ( job_step_id_msg_t * ) msg->data , 
				( void ** ) buffer , buf_len ) ;
			break ;
		case REQUEST_REVOKE_JOB_CREDENTIAL :
			pack_revoke_credential_msg ( ( revoke_credential_msg_t * ) msg->data , ( void ** ) buffer , buf_len ) ;
			break ;
		case REQUEST_SIGNAL_JOB :
			break ;
		case REQUEST_SIGNAL_JOB_STEP :
			break ;
		case RESPONSE_RECONFIGURE :
		case RESPONSE_CANCEL_JOB_STEP :
		case RESPONSE_COMPLETE_JOB_STEP :
		case RESPONSE_SIGNAL_JOB :
		case RESPONSE_SIGNAL_JOB_STEP :
			break ;
		case REQUEST_JOB_ATTACH :
			break ;
		case RESPONSE_JOB_ATTACH :
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
		case RESPONSE_SLURM_RC:
			pack_return_code ( ( return_code_msg_t * ) msg -> data , ( void ** ) buffer , buf_len ) ;
			break;
		case RESPONSE_JOB_STEP_CREATE:
			pack_job_step_create_response_msg(( job_step_create_response_msg_t * ) msg -> data , ( void ** ) buffer , buf_len ) ;	
			break;
		case REQUEST_JOB_STEP_CREATE:
			pack_job_step_create_request_msg(( job_step_create_request_msg_t * ) msg -> data , ( void ** ) buffer , buf_len ) ;	
			break;
		default :
			error ( "No pack method for msg type %i",  msg -> msg_type ) ;
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
			unpack_slurm_ctl_conf ( ( slurm_ctl_conf_info_msg_t ** ) &(msg -> data) , (void ** ) buffer , buf_len ) ;
			break;
		case RESPONSE_JOB_INFO:
			unpack_job_info_msg ( ( job_info_msg_t ** ) &(msg -> data) , (void ** ) buffer , buf_len ) ;
			break;
		case RESPONSE_PARTITION_INFO:
			unpack_partition_info_msg ( ( partition_info_msg_t ** ) &(msg -> data) , (void ** ) buffer , buf_len ) ;
			break;
		case RESPONSE_NODE_INFO:
			unpack_node_info_msg ( ( node_info_msg_t ** ) &(msg -> data) , (void ** ) buffer , buf_len ) ;
			break;
		case MESSAGE_NODE_REGISTRATION_STATUS :
			unpack_node_registration_status_msg ( ( slurm_node_registration_status_msg_t ** ) &( msg -> data ), ( void ** ) buffer , buf_len );
			break ;
		case REQUEST_RESOURCE_ALLOCATION :
		case REQUEST_SUBMIT_BATCH_JOB :
		case REQUEST_IMMEDIATE_RESOURCE_ALLOCATION : 
		case REQUEST_JOB_WILL_RUN : 
			unpack_job_desc ( ( job_desc_msg_t **) & ( msg-> data ), ( void ** ) buffer , buf_len ) ;
			break ;
		case REQUEST_NODE_REGISTRATION_STATUS :
		case REQUEST_RECONFIGURE :
			/* Message contains no body/information */
			break ;
		case RESPONSE_SUBMIT_BATCH_JOB :
			unpack_submit_response_msg ( ( submit_response_msg_t ** ) & ( msg -> data ) , ( void ** ) buffer , buf_len ) ;
			break ;
		case RESPONSE_RESOURCE_ALLOCATION :
		case RESPONSE_IMMEDIATE_RESOURCE_ALLOCATION : 
		case RESPONSE_JOB_WILL_RUN :
			unpack_resource_allocation_response_msg ( ( resource_allocation_response_msg_t ** ) & ( msg -> data ) , ( void ** ) buffer , buf_len ) ;
			break ;
		case REQUEST_UPDATE_JOB :
			unpack_job_desc ( ( job_desc_msg_t **) & ( msg-> data ), 
				( void ** ) buffer , buf_len ) ;
			break ;
		case REQUEST_UPDATE_NODE :
			unpack_update_node_msg ( ( update_node_msg_t ** ) & ( msg-> data ) , 
				( void ** ) buffer , buf_len ) ;
			break ;
		case REQUEST_UPDATE_PARTITION :
			unpack_update_partition_msg ( ( update_part_msg_t ** ) & ( msg->data ) , 
				( void ** ) buffer ,  buf_len ) ;
			break ;
		case REQUEST_LAUNCH_TASKS :
			unpack_launch_tasks_request_msg ( ( launch_tasks_request_msg_t ** ) & ( msg->data ) , 
				( void ** ) buffer , buf_len ) ;
			break ;
		case RESPONSE_LAUNCH_TASKS :
			unpack_launch_tasks_response_msg ( ( launch_tasks_response_msg_t ** ) & ( msg->data ) , 
				( void ** ) buffer , buf_len ) ;
			break ; 
		case REQUEST_REATTACH_TASKS_STREAMS :
			unpack_reattach_tasks_streams_msg ( ( reattach_tasks_streams_msg_t ** ) & ( msg->data ) , 
				( void ** ) buffer , buf_len ) ;
			break ; 
		case REQUEST_KILL_TASKS :
			unpack_cancel_tasks_msg ( ( kill_tasks_msg_t ** ) & ( msg->data ) , 
				( void ** ) buffer , buf_len ) ;
			break ;
		case REQUEST_CANCEL_JOB_STEP :
		case REQUEST_COMPLETE_JOB_STEP :
			unpack_cancel_job_step_msg ( ( job_step_id_msg_t ** ) & ( msg->data ) , 
				( void ** ) buffer , buf_len ) ;
			break ;
		case REQUEST_REVOKE_JOB_CREDENTIAL :
			unpack_revoke_credential_msg ( ( revoke_credential_msg_t ** ) & ( msg->data ) , ( void ** ) buffer , buf_len ) ;
			break ;
		case REQUEST_SIGNAL_JOB :
			break ;
		case REQUEST_SIGNAL_JOB_STEP :
			break ;
		case RESPONSE_RECONFIGURE :
		case RESPONSE_CANCEL_JOB_STEP :
		case RESPONSE_COMPLETE_JOB_STEP :
		case RESPONSE_SIGNAL_JOB :
		case RESPONSE_SIGNAL_JOB_STEP :
			break ;
		case REQUEST_JOB_ATTACH :
			break ;
		case RESPONSE_JOB_ATTACH :
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
		case RESPONSE_SLURM_RC:
			unpack_return_code ( ( return_code_msg_t **) &(msg -> data)  , ( void ** ) buffer , buf_len ) ;
			break;
		case RESPONSE_JOB_STEP_CREATE:
			unpack_job_step_create_response_msg(( job_step_create_response_msg_t ** ) &msg -> data , ( void ** ) buffer , buf_len ) ;	
			break;
		case REQUEST_JOB_STEP_CREATE:
			unpack_job_step_create_request_msg(( job_step_create_request_msg_t ** ) &msg -> data , ( void ** ) buffer , buf_len ) ;	
			break;
			default :
				debug ( "No pack method for msg type %i",  msg -> msg_type ) ;
				return EINVAL ;
				break;
		
	}
	return 0 ;
}

void pack_update_node_msg ( update_node_msg_t * msg, void ** buffer , uint32_t * length )
{
	assert ( msg != NULL );

	packstr ( msg -> node_names , ( void ** ) buffer , length ) ;
	pack16 ( msg -> node_state , ( void ** ) buffer , length ) ;
}

int unpack_update_node_msg ( update_node_msg_t ** msg , void ** buffer , uint32_t * length )
{
	uint16_t uint16_tmp;
	update_node_msg_t * tmp_ptr ;
	/* alloc memory for structure */	
	
	assert ( msg != NULL );
	
	tmp_ptr = xmalloc ( sizeof ( update_node_msg_t ) ) ;
	if (tmp_ptr == NULL) 
		return ENOMEM;

	unpackstr_xmalloc ( & tmp_ptr -> node_names , &uint16_tmp,  ( void ** ) buffer , length ) ;
	unpack16 ( & tmp_ptr -> node_state , ( void ** ) buffer , length ) ;
	*msg = tmp_ptr ;
	return 0 ;
}

void pack_node_registration_status_msg ( slurm_node_registration_status_msg_t * msg, void ** buffer , uint32_t * length )
{
	assert ( msg != NULL );
	
	pack32 ( msg -> timestamp , ( void ** ) buffer , length ) ;
	packstr ( msg -> node_name , ( void ** ) buffer , length ) ;
	pack32 ( msg -> cpus , ( void ** ) buffer , length ) ;
	pack32 ( msg -> real_memory_size , ( void ** ) buffer , length ) ;
	pack32 ( msg -> temporary_disk_space , ( void ** ) buffer , length ) ;
}

int unpack_node_registration_status_msg ( slurm_node_registration_status_msg_t ** msg , void ** buffer , uint32_t * length )
{
	uint16_t uint16_tmp;
	slurm_node_registration_status_msg_t * node_reg_ptr ;
	/* alloc memory for structure */	

	assert ( msg != NULL );
	
	node_reg_ptr = xmalloc ( sizeof ( slurm_node_registration_status_msg_t ) ) ;
	if (node_reg_ptr == NULL) 
	{
		return ENOMEM;
	}

	/* load the data values */
	/* unpack timestamp of snapshot */
	unpack32 ( & node_reg_ptr -> timestamp , ( void ** ) buffer , length ) ;
	unpackstr_xmalloc ( & node_reg_ptr -> node_name , &uint16_tmp,  ( void ** ) buffer , length ) ;
	unpack32 ( & node_reg_ptr -> cpus , ( void ** ) buffer , length ) ;
	unpack32 ( & node_reg_ptr -> real_memory_size , ( void ** ) buffer , length ) ;
	unpack32 ( & node_reg_ptr -> temporary_disk_space , ( void ** ) buffer , length ) ;
	*msg = node_reg_ptr ;
	return 0 ;
}

void pack_resource_allocation_response_msg ( resource_allocation_response_msg_t * msg, void ** buffer , int * length )
{
	assert ( msg != NULL );
	
	pack32 ( msg->job_id , ( void ** ) buffer , length ) ;
	packstr ( msg->node_list , ( void ** ) buffer , length ) ;
	pack16 ( msg->num_cpu_groups , ( void ** ) buffer , length ) ;
	pack32_array ( msg->cpus_per_node, msg->num_cpu_groups , ( void ** ) buffer  , length ) ;
	pack32_array ( msg->cpu_count_reps, msg->num_cpu_groups, ( void ** ) buffer  , length ) ;
}

int unpack_resource_allocation_response_msg ( resource_allocation_response_msg_t ** msg , void ** buffer , int * length )
{
	uint16_t uint16_tmp;
	resource_allocation_response_msg_t * tmp_ptr ;

	assert ( msg != NULL );
	
	/* alloc memory for structure */	
	tmp_ptr = xmalloc ( sizeof ( resource_allocation_response_msg_t ) ) ;
	if (tmp_ptr == NULL) 
		return ENOMEM;

	/* load the data values */
	unpack32 ( & tmp_ptr -> job_id , ( void ** ) buffer , length ) ;
	unpackstr_xmalloc ( & tmp_ptr -> node_list , &uint16_tmp,  ( void ** ) buffer , length ) ;
	unpack16 ( & tmp_ptr -> num_cpu_groups , ( void ** ) buffer , length ) ;
	unpack32_array ( (uint32_t **) &(tmp_ptr->cpus_per_node), &uint16_tmp, ( void ** ) buffer  , length ) ;
	unpack32_array ( (uint32_t **) &(tmp_ptr->cpu_count_reps), &uint16_tmp,  ( void ** ) buffer  , length ) ;
	*msg = tmp_ptr ;
	return 0 ;
}

void pack_submit_response_msg ( submit_response_msg_t * msg, void ** buffer , int * length )
{
	assert ( msg != NULL );
	
	pack32 ( msg->job_id , ( void ** ) buffer , length ) ;
}

int unpack_submit_response_msg ( submit_response_msg_t ** msg , void ** buffer , int * length )
{
	submit_response_msg_t * tmp_ptr ;
	/* alloc memory for structure */	

	assert ( msg != NULL );
	
	tmp_ptr = xmalloc ( sizeof ( submit_response_msg_t ) ) ;
	if (tmp_ptr == NULL) 
		return ENOMEM;

	/* load the data values */
	unpack32 ( & tmp_ptr -> job_id , ( void ** ) buffer , length ) ;
	*msg = tmp_ptr ;
	return 0 ;
}
void pack_node_info_msg ( slurm_msg_t * msg, void ** buf_ptr , int * buffer_size )
{	
	assert ( msg != NULL );
	
	assert (  sizeof(*msg) == sizeof(slurm_msg_t) ) ;
	assert ( buf_ptr != NULL && (*buf_ptr) != NULL ) ;
	assert ( ( buffer_size ) != NULL ) ;
	assert ( *buffer_size >= msg->data_size );

	 memcpy ( *buf_ptr , msg->data , msg->data_size );
	((char*)*buf_ptr) += msg->data_size;
	(*buffer_size) -= msg->data_size;
}

int unpack_node_info_msg ( node_info_msg_t ** msg , void ** buf_ptr , int * buffer_size )
{
	int i;
	node_table_t *node;
	
	assert ( msg != NULL );
	
	*msg = xmalloc ( sizeof ( node_info_msg_t ) );
	if ( *msg == NULL )
		return ENOMEM ;

	/* load buffer's header (data structure version and time) */
	unpack32 (&((*msg) -> record_count), buf_ptr, buffer_size);
	unpack32 (&((*msg) -> last_update ) , buf_ptr, buffer_size);

	node = (*msg) -> node_array = xmalloc ( sizeof ( node_table_t ) * (*msg)->record_count ) ;

	/* load individual job info */
	for (i = 0; i < (*msg)->record_count ; i++) {
	unpack_node_table ( & node[i] , buf_ptr , buffer_size ) ;

	}
	return 0;
}


int unpack_node_table_msg ( node_table_msg_t ** node , void ** buf_ptr , int * buffer_size )
{
	assert ( node != NULL );

	*node = xmalloc ( sizeof(node_table_t) );
	if (node == NULL) {
		return ENOMEM;
	}
	unpack_node_table ( *node , buf_ptr , buffer_size ) ;
	return 0 ;
}

int unpack_node_table ( node_table_msg_t * node , void ** buf_ptr , int * buffer_size )
{
	uint16_t uint16_tmp;

	assert ( node != NULL );

        unpackstr_xmalloc (&node->name, &uint16_tmp, buf_ptr, buffer_size);
        unpack16  (&node->node_state, buf_ptr, buffer_size);
        unpack32  (&node->cpus, buf_ptr, buffer_size);
        unpack32  (&node->real_memory, buf_ptr, buffer_size);
        unpack32  (&node->tmp_disk, buf_ptr, buffer_size);
        unpack32  (&node->weight, buf_ptr, buffer_size);
        unpackstr_xmalloc (&node->features, &uint16_tmp, buf_ptr, buffer_size);
	unpackstr_xmalloc (&node->partition, &uint16_tmp, buf_ptr, buffer_size);

        return 0;
}


void
pack_update_partition_msg ( update_part_msg_t * msg , void ** buffer, uint32_t * length  )
{
	assert ( msg != NULL );

	packstr ( msg -> name, ( void ** ) buffer , length ) ;
	pack32 ( msg -> max_time, ( void ** ) buffer , length ) ;
	pack32 ( msg -> max_nodes, ( void ** ) buffer , length ) ;
	pack16 ( msg -> default_part, ( void ** ) buffer , length ) ;
	pack16 ( msg -> key, ( void ** ) buffer , length ) ;
	pack16 ( msg -> shared, ( void ** ) buffer , length ) ;
	pack16 ( msg -> state_up, ( void ** ) buffer , length ) ;
	packstr ( msg -> nodes, ( void ** ) buffer , length ) ;
	packstr ( msg -> allow_groups, ( void ** ) buffer , length ) ;
}

int 
unpack_update_partition_msg ( update_part_msg_t ** msg , void ** buffer, uint32_t * length  )
{
	uint16_t uint16_tmp;
	update_part_msg_t * tmp_ptr ;
	
	assert ( msg != NULL );

	/* alloc memory for structure */	
	tmp_ptr = xmalloc ( sizeof ( update_part_msg_t ) ) ;
	if (tmp_ptr == NULL) 
		return ENOMEM;

	unpackstr_xmalloc ( &tmp_ptr -> name, &uint16_tmp,  ( void ** ) buffer , length ) ;
	unpack32 ( &tmp_ptr -> max_time, ( void ** ) buffer , length ) ;
	unpack32 ( &tmp_ptr -> max_nodes, ( void ** ) buffer , length ) ;
	unpack16 ( &tmp_ptr -> default_part, ( void ** ) buffer , length ) ;
	unpack16 ( &tmp_ptr -> key, ( void ** ) buffer , length ) ;
	unpack16 ( &tmp_ptr -> shared, ( void ** ) buffer , length ) ;
	unpack16 ( &tmp_ptr -> state_up, ( void ** ) buffer , length ) ;
	unpackstr_xmalloc ( &tmp_ptr -> nodes, &uint16_tmp,  ( void ** ) buffer , length ) ;
	unpackstr_xmalloc ( &tmp_ptr -> allow_groups, &uint16_tmp,  ( void ** ) buffer , length ) ;
	*msg = tmp_ptr;
	return 0;
}

void pack_job_step_create_request_msg ( job_step_create_request_msg_t* msg , void ** buffer , uint32_t * length )
{
	assert ( msg != NULL );

	pack32 ( msg -> job_id, ( void ** ) buffer , length ) ;
	pack32 ( msg -> user_id, ( void ** ) buffer , length ) ;
	pack32 ( msg -> node_count, ( void ** ) buffer , length ) ;
	pack32 ( msg -> cpu_count, ( void ** ) buffer , length ) ;
	pack16 ( msg -> relative, ( void ** ) buffer , length ) ;
	packstr ( msg -> node_list, ( void ** ) buffer , length ) ;
}

int unpack_job_step_create_request_msg ( job_step_create_request_msg_t** msg , void ** buffer , uint32_t * length )
{
	uint16_t uint16_tmp;
	job_step_create_request_msg_t * tmp_ptr ;
	/* alloc memory for structure */	

	assert ( msg != NULL );

	tmp_ptr = xmalloc ( sizeof ( job_step_create_request_msg_t ) ) ;
	if (tmp_ptr == NULL) 
		return ENOMEM;

	unpack32 ( &( tmp_ptr -> job_id), ( void ** ) buffer , length ) ;
	unpack32 ( &( tmp_ptr -> user_id), ( void ** ) buffer , length ) ;
	unpack32 ( &( tmp_ptr -> node_count), ( void ** ) buffer , length ) ;
	unpack32 ( &( tmp_ptr -> cpu_count), ( void ** ) buffer , length ) ;
	unpack16 ( &( tmp_ptr -> relative), ( void ** ) buffer , length ) ;
	unpackstr_xmalloc ( &( tmp_ptr -> node_list ), &uint16_tmp,  ( void ** ) buffer , length ) ;

	*msg = tmp_ptr;
	return 0;
}

void pack_revoke_credential_msg ( revoke_credential_msg_t* msg , void ** buffer , uint32_t * length )
{
	assert ( msg != NULL );

	pack32( msg->job_id, buffer, length ) ;
	packmem( msg->signature, SLURM_SSL_SIGNATURE_LENGTH , buffer, length ) ; 
}

int unpack_revoke_credential_msg ( revoke_credential_msg_t** msg , void ** buffer , uint32_t * length )
{
	uint16_t uint16_tmp;
	revoke_credential_msg_t* tmp_ptr ;
	/* alloc memory for structure */	
	tmp_ptr = xmalloc ( sizeof ( slurm_job_credential_t ) ) ;
	if (tmp_ptr == NULL) 
		return ENOMEM;

	unpack32( &(tmp_ptr->job_id), buffer, length ) ;
	unpackmem( tmp_ptr->signature, & uint16_tmp , buffer, length ) ; 

	*msg = tmp_ptr;
	return 0;
}

void pack_job_credential ( slurm_job_credential_t* msg , void ** buffer , uint32_t * length )
{
	assert ( msg != NULL );

	pack32( msg->job_id, buffer, length ) ;
	pack16( (uint16_t) msg->user_id, buffer, length ) ;
	packstr( msg->node_list, buffer, length ) ;
	pack32( msg->experation_time, buffer, length ) ;	
	packmem( msg->signature, SLURM_SSL_SIGNATURE_LENGTH , buffer, length ) ; 
}

int unpack_job_credential( slurm_job_credential_t** msg , void ** buffer , uint32_t * length )
{
	uint16_t uint16_tmp;
	slurm_job_credential_t* tmp_ptr ;
	/* alloc memory for structure */	
	tmp_ptr = xmalloc ( sizeof ( slurm_job_credential_t ) ) ;
	if (tmp_ptr == NULL) 
		return ENOMEM;

	unpack32( &(tmp_ptr->job_id), buffer, length ) ;
	unpack16( (uint16_t*) &(tmp_ptr->user_id), buffer, length ) ;
	unpackstr_xmalloc ( &(tmp_ptr->node_list), &uint16_tmp,  ( void ** ) buffer , length ) ;
	unpack32( (uint32_t*) &(tmp_ptr->experation_time), buffer, length ) ;	/* What are we going to do about time_t ? */
	unpackmem( tmp_ptr->signature, & uint16_tmp , buffer, length ) ; 

	*msg = tmp_ptr;
	return 0;
}

#ifdef HAVE_LIBELAN3
/* I'm pretty sure this isn't how I want to do this... */
void pack_qsw_jobinfo( qsw_jobinfo *msg, void ** buffer , uint32_t * length )
{
	int len = qsw_pack_jobinfo ( msg , *buf_ptr, *buf_len);
	if (len > 0) {      /* Need to explicitly advance pointer and index here */
		*buffer = (void *) ((char *)*buf_ptr + len);
		*length += len;
	}

}

void unpack_qsw_jobinfo( qsw_jobinfo ** msg, void ** buffer , uint32_t * length )
{
	int len=0;
	qsw_jobinfo_t temp ;

	qsw_alloc_jobinfo( temp ) ;

	len = qsw_unpack_jobinfo ( *msg, *buf_ptr, *buf_len);
	if (len > 0) {      /* Need to explicitly advance pointer and index here */
		*buffer = (void *) ((char *)*buf_ptr - len);
		*length -= len;
	}
}
#endif

void pack_job_step_create_response_msg (  job_step_create_response_msg_t* msg , void ** buffer , uint32_t * length )
{
	assert ( msg != NULL );

	pack32 ( msg -> job_step_id , ( void ** ) buffer , length ) ;
	packstr ( msg -> node_list, ( void ** ) buffer , length ) ;
	pack_job_credential( msg->credentials, ( void ** ) buffer , length ) ;
#ifdef HAVE_LIBELAN3
	/* put the elan3 stuff here */	
#endif
 	
}

int unpack_job_step_create_response_msg (job_step_create_response_msg_t** msg , void ** buffer , uint32_t * length )
{
	uint16_t uint16_tmp;
	job_step_create_response_msg_t * tmp_ptr ;
	/* alloc memory for structure */	
	tmp_ptr = xmalloc ( sizeof ( job_step_create_response_msg_t ) ) ;
	if (tmp_ptr == NULL) 
		return ENOMEM;

	unpack32 ( &tmp_ptr -> job_step_id, ( void ** ) buffer , length ) ;
	unpackstr_xmalloc ( &tmp_ptr -> node_list, &uint16_tmp,  ( void ** ) buffer , length ) ;
	unpack_job_credential( &tmp_ptr->credentials, ( void ** ) buffer , length ) ;

	*msg = tmp_ptr;
#ifdef HAVE_LIBELAN3
	/* put the elan3 stuff here */	
#endif
	return 0;
}


void pack_partition_info_msg ( slurm_msg_t * msg, void ** buf_ptr , int * buffer_size )
{	
	assert ( msg != NULL );
	assert (  sizeof(*msg) == sizeof(slurm_msg_t) ) ;
	assert ( buf_ptr != NULL && (*buf_ptr) != NULL ) ;
	assert ( (buffer_size) != NULL ) ;
	assert ( *buffer_size >= msg->data_size );

	memcpy ( *buf_ptr , msg->data , msg->data_size );
	((char*)*buf_ptr) += msg->data_size;
	(*buffer_size) -= msg->data_size;
}

int unpack_partition_info_msg ( partition_info_msg_t ** msg , void ** buf_ptr , int * buffer_size )
{
        int i;
        partition_table_t *partition;

        *msg = xmalloc ( sizeof ( partition_info_msg_t ) );
        if ( *msg == NULL )
                return ENOMEM ;

        /* load buffer's header (data structure version and time) */
        unpack32 (&((*msg) -> record_count), buf_ptr, buffer_size);
        unpack32 (&((*msg) -> last_update ) , buf_ptr, buffer_size);

        partition = (*msg) -> partition_array = xmalloc ( sizeof ( partition_table_t ) * (*msg)->record_count ) ;

        /* load individual job info */
        for (i = 0; i < (*msg)->record_count ; i++) {
		unpack_partition_table ( & partition[i] , buf_ptr , buffer_size ) ;

        }
        return 0;
}




int unpack_partition_table_msg ( partition_table_msg_t ** part , void ** buf_ptr , int * buffer_size )
{
		*part = xmalloc ( sizeof(partition_table_t) );
		if (part == NULL) {
			return ENOMEM;
		}
		unpack_partition_table ( *part , buf_ptr , buffer_size ) ;
		return 0 ;
}

int unpack_partition_table ( partition_table_msg_t * part , void ** buf_ptr , int * buffer_size )
{
	uint16_t uint16_tmp;
	char * node_inx_str;

	unpackstr_xmalloc (&part->name, &uint16_tmp, buf_ptr, buffer_size);
	if (part->name == NULL)
		part->name = "";
	unpack32  (&part->max_time, buf_ptr, buffer_size);
	unpack32  (&part->max_nodes, buf_ptr, buffer_size);
	unpack32  (&part->total_nodes, buf_ptr, buffer_size);

	unpack32  (&part->total_cpus, buf_ptr, buffer_size);
	unpack16  (&part->default_part, buf_ptr, buffer_size);
	unpack16  (&part->key, buf_ptr, buffer_size);
	unpack16  (&part->shared, buf_ptr, buffer_size);

	unpack16  (&part->state_up, buf_ptr, buffer_size);
	unpackstr_xmalloc (&part->allow_groups, &uint16_tmp, buf_ptr, buffer_size);
	unpackstr_xmalloc (&part->nodes, &uint16_tmp, buf_ptr, buffer_size);
	unpackstr_xmalloc (&node_inx_str, &uint16_tmp, buf_ptr, buffer_size);
	if (node_inx_str == NULL)
		part->node_inx = bitfmt2int("");
	else {
		part->node_inx = bitfmt2int(node_inx_str);
		xfree ( node_inx_str );
	}
	return 0;
}

void pack_job_info_msg ( slurm_msg_t * msg, void ** buf_ptr , int * buffer_size )
{	
	assert ( msg != NULL );
	assert (  sizeof(*msg) == sizeof(slurm_msg_t) ) ;
	assert ( buf_ptr != NULL && (*buf_ptr) != NULL ) ;
	assert ( (buffer_size) != NULL ) ;
	assert ( *buffer_size >= msg->data_size );

	memcpy ( *buf_ptr , msg->data , msg->data_size );
	((char*)*buf_ptr) += msg->data_size;
	(*buffer_size) -= msg->data_size;
}

int unpack_job_info_msg ( job_info_msg_t ** msg , void ** buf_ptr , int * buffer_size )
{
	int i;
	job_table_t *job;
	
	*msg = xmalloc ( sizeof ( job_info_msg_t ) );
	if ( *msg == NULL )
		return ENOMEM ;

	/* load buffer's header (data structure version and time) */
	unpack32 (&((*msg) -> record_count), buf_ptr, buffer_size);
	unpack32 (&((*msg) -> last_update ) , buf_ptr, buffer_size);
	job = (*msg) -> job_array = xmalloc ( sizeof ( job_table_t ) * (*msg)->record_count ) ;

	/* load individual job info */
	for (i = 0; i < (*msg)->record_count ; i++) {
		unpack_job_table ( & job[i] , buf_ptr , buffer_size ) ;
	}
	return 0;
}

int unpack_job_table_msg ( job_table_msg_t ** msg , void ** buf_ptr , int * buffer_size )
{
	*msg = xmalloc ( sizeof ( job_table_t ) ) ;
	if ( *msg == NULL )
		return ENOMEM ;
	unpack_job_table ( *msg , buf_ptr , buffer_size ) ;
	return 0 ;
}

int unpack_job_table ( job_table_t * job , void ** buf_ptr , int * buffer_size )
{
	uint16_t uint16_tmp;
	uint32_t uint32_tmp;
	char * node_inx_str;

	unpack32  (&job->job_id, buf_ptr, buffer_size);
	unpack32  (&job->user_id, buf_ptr, buffer_size);
	unpack16  (&job->job_state, buf_ptr, buffer_size);
	unpack32  (&job->time_limit, buf_ptr, buffer_size);

	unpack32  (&uint32_tmp, buf_ptr, buffer_size);
	job->start_time = (time_t) uint32_tmp;
	unpack32  (&uint32_tmp, buf_ptr, buffer_size);
	job->end_time = (time_t) uint32_tmp;
	unpack32  (&job->priority, buf_ptr, buffer_size);

	unpackstr_xmalloc (&job->nodes, &uint16_tmp, buf_ptr, buffer_size);
	unpackstr_xmalloc (&job->partition, &uint16_tmp, buf_ptr, buffer_size);
	unpackstr_xmalloc (&job->name, &uint16_tmp, buf_ptr, buffer_size);
	unpackstr_xmalloc (&node_inx_str, &uint16_tmp, buf_ptr, buffer_size);
	if (node_inx_str == NULL)
		job->node_inx = bitfmt2int("");
	else {
		job->node_inx = bitfmt2int(node_inx_str);
		xfree ( node_inx_str );
	}

	unpack32  (&job->num_procs, buf_ptr, buffer_size);
	unpack32  (&job->num_nodes, buf_ptr, buffer_size);
	unpack16  (&job->shared, buf_ptr, buffer_size);
	unpack16  (&job->contiguous, buf_ptr, buffer_size);

	unpack32  (&job->min_procs, buf_ptr, buffer_size);
	unpack32  (&job->min_memory, buf_ptr, buffer_size);
	unpack32  (&job->min_tmp_disk, buf_ptr, buffer_size);

	unpackstr_xmalloc (&job->req_nodes, &uint16_tmp, buf_ptr, buffer_size);
	unpackstr_xmalloc (&node_inx_str, &uint16_tmp, buf_ptr, buffer_size);
	if (node_inx_str == NULL)
		job->req_node_inx = bitfmt2int("");
	else {
		job->req_node_inx = bitfmt2int(node_inx_str);
		xfree ( node_inx_str );
	}
	unpackstr_xmalloc (&job->features, &uint16_tmp, buf_ptr, buffer_size);
	return 0 ;
}

void pack_slurm_ctl_conf ( slurm_ctl_conf_info_msg_t * build_ptr, void ** buf_ptr , int * buffer_size )
{	
	pack32 (build_ptr->last_update, buf_ptr, buffer_size);
	packstr (build_ptr->backup_controller, buf_ptr, buffer_size);
	packstr (build_ptr->control_machine, buf_ptr, buffer_size);
	packstr (build_ptr->epilog, buf_ptr, buffer_size);
	pack16 (build_ptr->fast_schedule, buf_ptr, buffer_size);
	pack16 (build_ptr->hash_base, buf_ptr, buffer_size);
	pack16 (build_ptr->heartbeat_interval, buf_ptr, buffer_size);
	pack16 (build_ptr->kill_wait, buf_ptr, buffer_size);
	packstr (build_ptr->prioritize, buf_ptr, buffer_size);
	packstr (build_ptr->prolog, buf_ptr, buffer_size);
	pack16 (build_ptr->slurmctld_timeout, buf_ptr, buffer_size);
	pack16 (build_ptr->slurmd_timeout, buf_ptr, buffer_size);
	packstr (build_ptr->slurm_conf, buf_ptr, buffer_size);
	packstr (build_ptr->state_save_location, buf_ptr, buffer_size);
	packstr (build_ptr->tmp_fs, buf_ptr, buffer_size);
}

int unpack_slurm_ctl_conf ( slurm_ctl_conf_info_msg_t **build_buffer_ptr, void ** buffer , int * buffer_size )
{	
	uint16_t uint16_tmp;
	slurm_ctl_conf_info_msg_t * build_ptr ;
	/* alloc memory for structure */	
	build_ptr = xmalloc ( sizeof ( slurm_ctl_conf_t ) ) ;
	if (build_ptr == NULL) 
	{
		return ENOMEM;
	}

	/* load the data values */
	/* unpack timestamp of snapshot */
	unpack32 (&build_ptr->last_update, buffer, buffer_size);
	unpackstr_xmalloc (&build_ptr->backup_controller, &uint16_tmp, buffer, buffer_size);
	unpackstr_xmalloc (&build_ptr->control_machine, &uint16_tmp, buffer, buffer_size);
	unpackstr_xmalloc (&build_ptr->epilog, &uint16_tmp, buffer, buffer_size);
	unpack16 (&build_ptr->fast_schedule, buffer, buffer_size);
	unpack16 (&build_ptr->hash_base, buffer, buffer_size);
	unpack16 (&build_ptr->heartbeat_interval, buffer, buffer_size);
	unpack16 (&build_ptr->kill_wait, buffer, buffer_size);
	unpackstr_xmalloc (&build_ptr->prioritize, &uint16_tmp, buffer, buffer_size);
	unpackstr_xmalloc (&build_ptr->prolog, &uint16_tmp, buffer, buffer_size);
	unpack16 (&build_ptr->slurmctld_timeout, buffer, buffer_size);
	unpack16 (&build_ptr->slurmd_timeout, buffer, buffer_size);
	unpackstr_xmalloc (&build_ptr->slurm_conf, &uint16_tmp, buffer, buffer_size);
	unpackstr_xmalloc (&build_ptr->state_save_location, &uint16_tmp, buffer, buffer_size);
	unpackstr_xmalloc (&build_ptr->tmp_fs, &uint16_tmp, buffer, buffer_size);
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
	if (job_desc_ptr->partition_key)
		packmem (job_desc_ptr->partition_key, 32, buf_ptr, buffer_size);
	else {
		char *no_key;
		no_key = xmalloc (32);
		packmem (no_key, 32, buf_ptr, buffer_size);
		xfree (no_key);
	}
	
	pack32 (job_desc_ptr->min_procs, buf_ptr, buffer_size);
	pack32 (job_desc_ptr->min_memory, buf_ptr, buffer_size);
	pack32 (job_desc_ptr->min_tmp_disk, buf_ptr, buffer_size);
	
	packstr (job_desc_ptr->partition, buf_ptr, buffer_size);
	pack32 (job_desc_ptr->priority, buf_ptr, buffer_size);
	
	packstr (job_desc_ptr->req_nodes, buf_ptr, buffer_size);
	packstring_array (job_desc_ptr->environment, job_desc_ptr->env_size, buf_ptr, buffer_size);
	packstr (job_desc_ptr->script, buf_ptr, buffer_size);

	packstr (job_desc_ptr->stderr, buf_ptr, buffer_size);
	packstr (job_desc_ptr->stdin, buf_ptr, buffer_size);
	packstr (job_desc_ptr->stdout, buf_ptr, buffer_size);
	packstr (job_desc_ptr->work_dir, buf_ptr, buffer_size);

	pack16 (job_desc_ptr->shared, buf_ptr, buffer_size);
	pack32 (job_desc_ptr->time_limit, buf_ptr, buffer_size);
	
	pack32 (job_desc_ptr->num_procs, buf_ptr, buffer_size);
	pack32 (job_desc_ptr->num_nodes, buf_ptr, buffer_size);
	pack32 (job_desc_ptr->user_id, buf_ptr, buffer_size);

}

/* unpack_job_desc
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
	job_desc_ptr = xmalloc ( sizeof ( job_desc_msg_t ) ) ;
	if (job_desc_ptr== NULL) 
	{
		*job_desc_buffer_ptr = NULL ;
		return ENOMEM ;
	}

	/* load the data values */
	/* unpack timestamp of snapshot */

	unpack16 (&job_desc_ptr->contiguous, buf_ptr, buffer_size);
	unpackstr_xmalloc (&job_desc_ptr->features, &uint16_tmp, buf_ptr, buffer_size);
	unpackstr_xmalloc (&job_desc_ptr->groups, &uint16_tmp, buf_ptr, buffer_size);
	unpack32 (&job_desc_ptr->job_id, buf_ptr, buffer_size);
	unpackstr_xmalloc (&job_desc_ptr->name, &uint16_tmp, buf_ptr, buffer_size);
	unpackmem_xmalloc ( ( char ** ) &job_desc_ptr->partition_key, &uint16_tmp, 
				buf_ptr, buffer_size);
	
	unpack32 (&job_desc_ptr->min_procs, buf_ptr, buffer_size);
	unpack32 (&job_desc_ptr->min_memory, buf_ptr, buffer_size);
	unpack32 (&job_desc_ptr->min_tmp_disk, buf_ptr, buffer_size);
	
	unpackstr_xmalloc (&job_desc_ptr->partition, &uint16_tmp, buf_ptr, buffer_size);
	unpack32 (&job_desc_ptr->priority, buf_ptr, buffer_size);
	
	unpackstr_xmalloc (&job_desc_ptr->req_nodes, &uint16_tmp, buf_ptr, buffer_size);
	unpackstring_array (&job_desc_ptr->environment, &job_desc_ptr->env_size, buf_ptr, buffer_size);
	unpackstr_xmalloc (&job_desc_ptr->script, &uint16_tmp, buf_ptr, buffer_size);

	unpackstr_xmalloc (&job_desc_ptr->stderr, &uint16_tmp, buf_ptr, buffer_size);
	unpackstr_xmalloc (&job_desc_ptr->stdin, &uint16_tmp, buf_ptr, buffer_size);
	unpackstr_xmalloc (&job_desc_ptr->stdout, &uint16_tmp, buf_ptr, buffer_size);
	unpackstr_xmalloc (&job_desc_ptr->work_dir, &uint16_tmp, buf_ptr, buffer_size);

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

        last_update_msg = xmalloc ( sizeof ( last_update_msg_t ) ) ;
        if ( last_update_msg == NULL)
        {
                *msg = NULL ;
                return ENOMEM ;
        }

	unpack32 ( & last_update_msg -> last_update , buffer , length ) ;
	*msg = last_update_msg ;
	return 0 ;
}

void pack_return_code ( return_code_msg_t * msg , void ** buffer , uint32_t * length )
{
	pack32 ( msg -> return_code , buffer , length ) ;
}

int unpack_return_code ( return_code_msg_t ** msg , void ** buffer , uint32_t * length )
{
        return_code_msg_t * return_code_msg ;

        return_code_msg = xmalloc ( sizeof ( return_code_msg_t ) ) ;
        if ( return_code_msg == NULL)
        {
                *msg = NULL ;
                return ENOMEM ;
        }

	unpack32 ( & return_code_msg -> return_code , buffer , length ) ;
	*msg = return_code_msg ;
	return 0 ;
}

void pack_reattach_tasks_streams_msg ( reattach_tasks_streams_msg_t * msg , void ** buffer , uint32_t * length )
{
	pack32 ( msg -> job_id , buffer , length ) ;
	pack32 ( msg -> job_step_id , buffer , length ) ;
	pack32 ( msg -> uid , buffer , length ) ;
	pack_job_credential ( msg -> credential , buffer , length ) ;
	pack32 ( msg -> tasks_to_reattach , buffer , length ) ;
	slurm_pack_slurm_addr ( & msg -> streams , buffer , length ) ;
	pack32_array ( msg -> global_task_ids , ( uint16_t ) msg -> tasks_to_reattach , buffer , length ) ;
}

int unpack_reattach_tasks_streams_msg ( reattach_tasks_streams_msg_t ** msg_ptr , void ** buffer , uint32_t * length )
{
	uint16_t uint16_tmp;
	reattach_tasks_streams_msg_t * msg ;

	msg = xmalloc ( sizeof ( reattach_tasks_streams_msg_t ) ) ;
	if (msg == NULL) 
	{
		*msg_ptr = NULL ;
		return ENOMEM ;
	}

	unpack32 ( & msg -> job_id , buffer , length ) ;
	unpack32 ( & msg -> job_step_id , buffer , length ) ;
	unpack32 ( & msg -> uid , buffer , length ) ;
	unpack_job_credential( & msg -> credential ,  buffer , length ) ;
	unpack32 ( & msg -> tasks_to_reattach , buffer , length ) ;
	slurm_unpack_slurm_addr_no_alloc ( & msg -> streams , buffer , length ) ;
	unpack32_array ( & msg -> global_task_ids , & uint16_tmp , buffer , length ) ;
	*msg_ptr = msg ;
	return 0 ;
}

void pack_launch_tasks_response_msg ( launch_tasks_response_msg_t * msg , void ** buffer , uint32_t * length )
{
	pack32 ( msg -> return_code , buffer , length ) ;
	packstr ( msg -> node_name , buffer , length ) ;
}

int unpack_launch_tasks_response_msg ( launch_tasks_response_msg_t ** msg_ptr , void ** buffer , uint32_t * length )
{
	uint16_t uint16_tmp;
	launch_tasks_response_msg_t * msg ;

	msg = xmalloc ( sizeof ( launch_tasks_response_msg_t ) ) ;
	if (msg == NULL) 
	{
		*msg_ptr = NULL ;
		return ENOMEM ;
	}

	unpack32 ( & msg -> return_code , buffer , length ) ;
	unpackstr_xmalloc ( & msg -> node_name , & uint16_tmp , buffer , length ) ;
	*msg_ptr = msg ;
	return 0 ;
}

void pack_launch_tasks_request_msg ( launch_tasks_request_msg_t * msg , void ** buffer , uint32_t * length )
{
	pack32 ( msg -> job_id , buffer , length ) ;
	pack32 ( msg -> job_step_id , buffer , length ) ;
	pack32 ( msg -> uid , buffer , length ) ;
	pack_job_credential ( msg -> credential , buffer , length ) ;
	pack32 ( msg -> tasks_to_launch , buffer , length ) ;
	packstring_array ( msg -> env , msg -> envc , buffer , length ) ;
	packstr ( msg -> cwd , buffer , length ) ;
	packstring_array ( msg -> argv , msg -> argc , buffer , length ) ;
	slurm_pack_slurm_addr ( & msg -> response_addr , buffer , length ) ;
	slurm_pack_slurm_addr ( & msg -> streams , buffer , length ) ;
	pack32_array ( msg -> global_task_ids , ( uint16_t ) msg -> tasks_to_launch , buffer , length ) ;
}

int unpack_launch_tasks_request_msg ( launch_tasks_request_msg_t ** msg_ptr , void ** buffer , uint32_t * length )
{
	uint16_t uint16_tmp;
	launch_tasks_request_msg_t * msg ;

	msg = xmalloc ( sizeof ( launch_tasks_request_msg_t ) ) ;
	if (msg == NULL) 
	{
		*msg_ptr = NULL ;
		return ENOMEM ;
	}

	unpack32 ( & msg -> job_id , buffer , length ) ;
	unpack32 ( & msg -> job_step_id , buffer , length ) ;
	unpack32 ( & msg -> uid , buffer , length ) ;
	unpack_job_credential( & msg -> credential ,  buffer , length ) ;
	unpack32 ( & msg -> tasks_to_launch , buffer , length ) ;
	unpackstring_array ( & msg -> env , & msg -> envc , buffer , length ) ;
	unpackstr_xmalloc ( & msg -> cwd , & uint16_tmp , buffer , length ) ;
	unpackstring_array ( & msg -> argv , & msg->argc , buffer , length ) ;
	slurm_unpack_slurm_addr_no_alloc ( & msg -> response_addr , buffer , length ) ;
	slurm_unpack_slurm_addr_no_alloc ( & msg -> streams , buffer , length ) ;
	unpack32_array ( & msg -> global_task_ids , & uint16_tmp , buffer , length ) ;
	*msg_ptr = msg ;
	return 0 ;
}

void pack_cancel_tasks_msg ( kill_tasks_msg_t * msg , void ** buffer , uint32_t * length )
{
	pack32 ( msg -> job_id , buffer , length ) ;
	pack32 ( msg -> job_step_id , buffer , length ) ;
	pack32 ( msg -> signal , buffer , length ) ;
}

int unpack_cancel_tasks_msg ( kill_tasks_msg_t ** msg_ptr , void ** buffer , uint32_t * length )
{
	kill_tasks_msg_t * msg ;

	msg = xmalloc ( sizeof ( kill_tasks_msg_t ) ) ;
	if ( msg == NULL) 
	{
		*msg_ptr = NULL ;
		return ENOMEM ;
	}

	unpack32 ( & msg -> job_id , buffer , length ) ;
	unpack32 ( & msg -> job_step_id , buffer , length ) ;
	unpack32 ( & msg -> signal , buffer , length ) ;
	*msg_ptr = msg ;
	return 0 ;
}

void pack_cancel_job_step_msg ( job_step_id_msg_t * msg , void ** buffer , uint32_t * length )
{
	pack32 ( msg -> job_id , buffer , length ) ;
	pack32 ( msg -> job_step_id , buffer , length ) ;
}

int unpack_cancel_job_step_msg ( job_step_id_msg_t ** msg_ptr , void ** buffer , uint32_t * length )
{
	job_step_id_msg_t * msg ;

	msg = xmalloc ( sizeof ( job_step_id_msg_t ) ) ;
	if ( msg == NULL) 
	{
		*msg_ptr = NULL ;
		return ENOMEM ;
	}

	unpack32 ( & msg -> job_id , buffer , length ) ;
	unpack32 ( & msg -> job_step_id , buffer , length ) ;
	*msg_ptr = msg ;
	return 0 ;
}



/* template 
void pack_ ( * msg , void ** buffer , uint32_t * length )
{
	assert ( msg != NULL );

	pack16 ( msg -> , buffer , length ) ;
	pack32 ( msg -> , buffer , length ) ;
	packstr ( msg -> , buffer , length ) ;
}

void unpack_ ( ** msg_ptr , void ** buffer , uint32_t * length )
{
	uint16_t uint16_tmp;
	* msg ;

	assert ( msg_ptr != NULL );

	msg = xmalloc ( sizeof ( ) ) ;
	if (msg == NULL) 
	{
		*msg_ptr = NULL ;
		return ;
	}

	unpack16 ( & msg -> , buffer , length ) ;
	unpack32 ( & msg -> , buffer , length ) ;
	unpackstr_xmalloc ( & msg -> , & uint16_tmp , buffer , length ) ;
	*msg_ptr = msg ;
}
*/

void pack_slurm_addr_array ( slurm_addr * slurm_address , uint16_t size_val, void ** buffer , int * length )
{
	int i=0;
	uint16_t nl = htons(size_val);
	pack16( nl, buffer, length);

	for ( i=0; i < size_val; i++ ) 
	{
		slurm_pack_slurm_addr ( slurm_address + i , buffer , length ) ;
	}
	
}

void unpack_slurm_addr_array ( slurm_addr ** slurm_address , uint16_t * size_val , void ** buffer , int * length )
{
	int i=0;
	uint16_t nl ;
	unpack16( & nl , buffer , length );
	*size_val = ntohs ( nl ) ;
	*slurm_address = xmalloc( (*size_val) * sizeof( slurm_addr ) );

	for ( i=0; i < *size_val; i++ ) 
	{
		slurm_unpack_slurm_addr_no_alloc ( (*slurm_address) + i , buffer , length );
	}
}
