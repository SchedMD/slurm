/****************************************************************************\
 *  slurm_protocol_pack.c - functions to pack and unpack structures for RPCs
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Kevin Tew <tew1@llnl.gov>, Moe Jette <jette1@llnl.gov>, et. al.
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
#include <src/common/slurm_protocol_defs.h>
#include <src/common/slurm_auth.h>
#include <src/common/pack.h>
#include <src/common/log.h>
#include <src/common/xmalloc.h>


void pack_job_credential ( slurm_job_credential_t* cred , Buf buffer );
int unpack_job_credential( slurm_job_credential_t** msg , Buf buffer );

/* pack_header
 * packs a slurm protocol header that proceeds every slurm message
 * header 	- the header structure to pack
 * buffer	- destination of the pack, contains pointers that are automatically updated
 */
void pack_header ( header_t * header, Buf buffer )
{
	pack16 ( header -> version , buffer ) ;
	pack16 ( header -> flags , buffer ) ;
        pack16 ( (uint16_t)header -> cred_type , buffer ) ;

	pack32 ( header -> cred_length , buffer ) ;
	pack16 ( (uint16_t)header -> msg_type , buffer ) ;
	pack32 ( header -> body_length , buffer ) ;
}

/* unpack_header
 * unpacks a slurm protocol header that proceeds every slurm message
 * header 	- the header structure to unpack
 * buffer	- destination of the pack, contains pointers that are automatically updated
 */
int unpack_header ( header_t * header , Buf buffer )
{
	uint16_t tmp=0;

	safe_unpack16 ( & header -> version , buffer ) ;
	safe_unpack16 ( & header -> flags , buffer ) ;
	safe_unpack16 ( & tmp , buffer ) ;
	header -> cred_type = (slurm_credential_type_t ) tmp ;

	safe_unpack32 ( & header -> cred_length , buffer ) ;
	safe_unpack16 ( & tmp , buffer ) ;
	header -> msg_type = (slurm_msg_type_t ) tmp ;
	safe_unpack32 ( & header -> body_length , buffer ) ;
	return SLURM_SUCCESS;

    unpack_error:
	return SLURM_ERROR;
}

int size_io_stream_header (void)
{	/* must match un/pack_io_stream_header and size_io_stream_header */
	return (SLURM_SSL_SIGNATURE_LENGTH + 2 * sizeof(uint16_t) + sizeof(uint32_t));
}

void pack_io_stream_header ( slurm_io_stream_header_t * msg , Buf buffer )
{	/* must match un/pack_io_stream_header and size_io_stream_header */

	assert ( msg != NULL );

	pack16( msg->version, buffer ) ;
	packmem_array( msg->key, (uint32_t) SLURM_SSL_SIGNATURE_LENGTH, buffer ) ; 
	pack32( msg->task_id, buffer ) ;	
	pack16( msg->type, buffer ) ;
}

int unpack_io_stream_header ( slurm_io_stream_header_t * msg , Buf buffer )
{	/* must match un/pack_io_stream_header and size_io_stream_header */

	safe_unpack16( & msg->version, buffer ) ;
	safe_unpackmem_array( msg->key, (uint32_t) SLURM_SSL_SIGNATURE_LENGTH , buffer ) ; 
	safe_unpack32( & msg->task_id, buffer ) ;	
	safe_unpack16( & msg->type, buffer ) ;
	return SLURM_SUCCESS;

    unpack_error:
	return SLURM_ERROR;
}


/* pack_msg
 * packs a slurm protocol mesg body
 * header 	- the body structure to pack
 * buffer	- destination of the pack, contains pointers that are automatically updated
 */
int pack_msg ( slurm_msg_t const * msg , Buf buffer )
{
	switch ( msg -> msg_type )
	{
		case REQUEST_BUILD_INFO :
		case REQUEST_NODE_INFO :
		case REQUEST_PARTITION_INFO :
		case REQUEST_ACCTING_INFO :
			pack_last_update ( ( last_update_msg_t * ) msg -> data , buffer ) ;
			break;
		case RESPONSE_BUILD_INFO:
			pack_slurm_ctl_conf ( ( slurm_ctl_conf_info_msg_t * ) msg -> data , buffer ) ;
			break ;
		case RESPONSE_JOB_INFO:
			pack_job_info_msg ( ( slurm_msg_t * ) msg , buffer ) ;
			break ;
		case RESPONSE_PARTITION_INFO:
			pack_partition_info_msg ( ( slurm_msg_t * ) msg , buffer ) ;
			break ;
		case RESPONSE_NODE_INFO:
			pack_node_info_msg ( ( slurm_msg_t * ) msg , buffer ) ;
			break ;
		case MESSAGE_NODE_REGISTRATION_STATUS :
			pack_node_registration_status_msg ( ( slurm_node_registration_status_msg_t * ) msg -> data , buffer );
			break ;
		case REQUEST_RESOURCE_ALLOCATION :
		case REQUEST_SUBMIT_BATCH_JOB :
		case REQUEST_IMMEDIATE_RESOURCE_ALLOCATION : 
		case REQUEST_JOB_WILL_RUN : 
		case REQUEST_ALLOCATION_AND_RUN_JOB_STEP :
			pack_job_desc ( (job_desc_msg_t * )  msg -> data , buffer )  ;
			break ;
		case REQUEST_OLD_JOB_RESOURCE_ALLOCATION :
			pack_old_job_desc ( (old_job_alloc_msg_t * )  msg -> data , buffer )  ;
			break ;
		case REQUEST_NODE_REGISTRATION_STATUS :
		case REQUEST_RECONFIGURE :
		case REQUEST_SHUTDOWN_IMMEDIATE :
		case REQUEST_PING :
		case REQUEST_CONTROL :
			/* Message contains no body/information */
			break ;
		case REQUEST_SHUTDOWN :
			pack_shutdown_msg ( (shutdown_msg_t *) msg -> data, buffer )  ;
			break;
		case RESPONSE_SUBMIT_BATCH_JOB:
			pack_submit_response_msg ( ( submit_response_msg_t * ) msg -> data , buffer ) ;
			break ;
		case RESPONSE_RESOURCE_ALLOCATION :
		case RESPONSE_IMMEDIATE_RESOURCE_ALLOCATION : 
		case RESPONSE_JOB_WILL_RUN :
			pack_resource_allocation_response_msg ( ( resource_allocation_response_msg_t * ) msg -> data , 
				buffer ) ;
			break ;
		case RESPONSE_ALLOCATION_AND_RUN_JOB_STEP :
			pack_resource_allocation_and_run_response_msg ( ( resource_allocation_and_run_response_msg_t * ) msg -> data , 
				buffer ) ;
			break ;
		case REQUEST_UPDATE_JOB :
			pack_job_desc ( (job_desc_msg_t * )  msg -> data , buffer )  ;
			break ;
			break ;
		case REQUEST_UPDATE_NODE :
			pack_update_node_msg ( ( update_node_msg_t * ) msg-> data , buffer ) ;
			break ;
		case REQUEST_UPDATE_PARTITION :
			pack_update_partition_msg ( ( update_part_msg_t * ) msg->data , buffer ) ;
			break ;
		case REQUEST_REATTACH_TASKS_STREAMS :
			pack_reattach_tasks_streams_msg ( ( reattach_tasks_streams_msg_t * ) msg->data , buffer ) ;
			break ;
		case REQUEST_LAUNCH_TASKS :
			pack_launch_tasks_request_msg ( ( launch_tasks_request_msg_t * ) msg->data , buffer ) ;
			break ;
		case RESPONSE_LAUNCH_TASKS :
			pack_launch_tasks_response_msg ( ( launch_tasks_response_msg_t * ) msg->data , buffer ) ;
			break ;
		case REQUEST_KILL_TASKS :
			pack_cancel_tasks_msg ( ( kill_tasks_msg_t * ) msg->data , buffer ) ;
			break ;
		case REQUEST_JOB_STEP_INFO :
			pack_get_job_step_info ( ( job_step_info_request_msg_t * ) msg->data , buffer ) ;
			break ;
		/********  job_step_id_t Messages  ********/
		case REQUEST_JOB_INFO :
		case REQUEST_CANCEL_JOB_STEP :
		case REQUEST_COMPLETE_JOB_STEP :
			pack_job_step_id ( ( job_step_id_t * ) msg->data , buffer ) ;
			break ;

		case REQUEST_REVOKE_JOB_CREDENTIAL :
			pack_revoke_credential_msg ( ( revoke_credential_msg_t * ) msg->data , buffer ) ;
			break ;
		case REQUEST_SIGNAL_JOB :
			break ;
		case REQUEST_SIGNAL_JOB_STEP :
			break ;
		case RESPONSE_RECONFIGURE :
		case RESPONSE_SHUTDOWN :
		case RESPONSE_CANCEL_JOB_STEP :
		case RESPONSE_COMPLETE_JOB_STEP :
		case RESPONSE_SIGNAL_JOB :
		case RESPONSE_SIGNAL_JOB_STEP :
			break ;
		case REQUEST_JOB_ATTACH :
			break ;
		case RESPONSE_JOB_ATTACH :
			break ;
		case RESPONSE_JOB_STEP_INFO :
			pack_job_step_info_msg( ( slurm_msg_t * ) msg , buffer ) ;
			break ;
		case REQUEST_JOB_RESOURCE :
			break ;
		case RESPONSE_JOB_RESOURCE :
			break ;
		case REQUEST_RUN_JOB_STEP :
			break ;
		case RESPONSE_RUN_JOB_STEP:
			break ;
		case MESSAGE_TASK_EXIT :
			pack_task_exit_msg ( ( task_exit_msg_t * ) msg -> data , buffer ) ;
			break ;
		case REQUEST_BATCH_JOB_LAUNCH :
			pack_batch_job_launch ( ( batch_job_launch_msg_t * ) msg -> data , buffer ) ;
			break ;
		case MESSAGE_UPLOAD_ACCOUNTING_INFO :
			break ;
		case RESPONSE_SLURM_RC:
			pack_return_code ( ( return_code_msg_t * ) msg -> data , buffer ) ;
			break;
		case RESPONSE_JOB_STEP_CREATE:
			pack_job_step_create_response_msg(( job_step_create_response_msg_t * ) msg -> data , buffer ) ;	
			break;
		case REQUEST_JOB_STEP_CREATE:
			pack_job_step_create_request_msg(( job_step_create_request_msg_t * ) msg -> data , buffer ) ;	
			break;
		default :
			debug ( "No pack method for msg type %i",  msg -> msg_type ) ;
			return EINVAL ;
			break;
		
	}
	return SLURM_SUCCESS ;
}

/* unpack_msg
 * unpacks a slurm protocol msg body
 * header 	- the body structure to unpack
 * buffer	- source of the unpack, contains pointers that are automatically updated
 */
int unpack_msg ( slurm_msg_t * msg , Buf buffer )
{
	int rc = SLURM_SUCCESS;

	switch ( msg-> msg_type )
	{
		case REQUEST_BUILD_INFO :
		case REQUEST_NODE_INFO :
		case REQUEST_PARTITION_INFO :
		case REQUEST_ACCTING_INFO :
			rc = unpack_last_update ( ( last_update_msg_t **) &(msg -> data)  , buffer ) ;
			break;
		case RESPONSE_BUILD_INFO:
			rc = unpack_slurm_ctl_conf ( ( slurm_ctl_conf_info_msg_t ** ) &(msg -> data) , buffer ) ;
			break;
		case RESPONSE_JOB_INFO:
			rc = unpack_job_info_msg ( ( job_info_msg_t ** ) &(msg -> data) , buffer ) ;
			break;
		case RESPONSE_PARTITION_INFO:
			rc = unpack_partition_info_msg ( ( partition_info_msg_t ** ) &(msg -> data) , buffer ) ;
			break;
		case RESPONSE_NODE_INFO:
			rc = unpack_node_info_msg ( ( node_info_msg_t ** ) &(msg -> data) , buffer) ;
			break;
		case MESSAGE_NODE_REGISTRATION_STATUS :
			rc = unpack_node_registration_status_msg ( ( slurm_node_registration_status_msg_t ** ) &( msg -> data ), buffer );
			break ;
		case REQUEST_RESOURCE_ALLOCATION :
		case REQUEST_SUBMIT_BATCH_JOB :
		case REQUEST_IMMEDIATE_RESOURCE_ALLOCATION : 
		case REQUEST_JOB_WILL_RUN : 
		case REQUEST_ALLOCATION_AND_RUN_JOB_STEP : 
			rc = unpack_job_desc ( ( job_desc_msg_t **) & ( msg-> data ), buffer ) ;
			break ;
		case REQUEST_OLD_JOB_RESOURCE_ALLOCATION :
			rc = unpack_old_job_desc ( (old_job_alloc_msg_t ** ) & ( msg -> data ), buffer )  ;
			break ;
		case REQUEST_NODE_REGISTRATION_STATUS :
		case REQUEST_RECONFIGURE :
		case REQUEST_SHUTDOWN_IMMEDIATE :
		case REQUEST_PING :
		case REQUEST_CONTROL :
			/* Message contains no body/information */
			break ;
		case REQUEST_SHUTDOWN :
			rc = unpack_shutdown_msg ( ( shutdown_msg_t **) & ( msg-> data ), buffer ) ;
			break ;
		case RESPONSE_SUBMIT_BATCH_JOB :
			rc = unpack_submit_response_msg ( ( submit_response_msg_t ** ) & ( msg -> data ) , buffer ) ;
			break ;
		case RESPONSE_RESOURCE_ALLOCATION :
		case RESPONSE_IMMEDIATE_RESOURCE_ALLOCATION : 
		case RESPONSE_JOB_WILL_RUN :
			rc = unpack_resource_allocation_response_msg ( ( resource_allocation_response_msg_t ** ) & ( msg -> data ) , buffer ) ;
			break ;
		case RESPONSE_ALLOCATION_AND_RUN_JOB_STEP :
			rc = unpack_resource_allocation_and_run_response_msg ( ( resource_allocation_and_run_response_msg_t ** ) & ( msg -> data ) , buffer ) ;
			break ;
		case REQUEST_UPDATE_JOB :
			rc = unpack_job_desc ( ( job_desc_msg_t **) & ( msg-> data ), buffer ) ;
			break ;
		case REQUEST_UPDATE_NODE :
			rc = unpack_update_node_msg ( ( update_node_msg_t ** ) & ( msg-> data ) , buffer ) ;
			break ;
		case REQUEST_UPDATE_PARTITION :
			rc = unpack_update_partition_msg ( ( update_part_msg_t ** ) & ( msg->data ) , buffer ) ;
			break ;
		case REQUEST_LAUNCH_TASKS :
			rc = unpack_launch_tasks_request_msg ( ( launch_tasks_request_msg_t ** ) & ( msg->data ) , buffer ) ;
			break ;
		case RESPONSE_LAUNCH_TASKS :
			rc = unpack_launch_tasks_response_msg ( ( launch_tasks_response_msg_t ** ) & ( msg->data ) , buffer ) ;
			break ; 
		case REQUEST_REATTACH_TASKS_STREAMS :
			rc = unpack_reattach_tasks_streams_msg ( ( reattach_tasks_streams_msg_t ** ) & ( msg->data ) , buffer ) ;
			break ; 
		case REQUEST_KILL_TASKS :
			rc = unpack_cancel_tasks_msg ( ( kill_tasks_msg_t ** ) & ( msg->data ) , buffer ) ;
			break ;
		case REQUEST_JOB_STEP_INFO :
			rc = unpack_get_job_step_info ( ( job_step_info_request_msg_t ** ) & ( msg->data ) , buffer ) ;
			break ;
		/********  job_step_id_t Messages  ********/
		case REQUEST_JOB_INFO :
		case REQUEST_CANCEL_JOB_STEP :
		case REQUEST_COMPLETE_JOB_STEP :
			rc = unpack_job_step_id ( ( job_step_id_t ** ) & ( msg->data ) , buffer ) ;
			break ;
		case REQUEST_REVOKE_JOB_CREDENTIAL :
			rc = unpack_revoke_credential_msg ( ( revoke_credential_msg_t ** ) & ( msg->data ) , buffer ) ;
			break ;
		case REQUEST_SIGNAL_JOB :
			break ;
		case REQUEST_SIGNAL_JOB_STEP :
			break ;
		case RESPONSE_RECONFIGURE :
		case RESPONSE_SHUTDOWN :
		case RESPONSE_CANCEL_JOB_STEP :
		case RESPONSE_COMPLETE_JOB_STEP :
		case RESPONSE_SIGNAL_JOB :
		case RESPONSE_SIGNAL_JOB_STEP :
			break ;
		case REQUEST_JOB_ATTACH :
			break ;
		case RESPONSE_JOB_ATTACH :
			break ;
		case RESPONSE_JOB_STEP_INFO :
			unpack_job_step_info_response_msg( ( job_step_info_response_msg_t ** ) & ( msg->data ) , buffer ) ;
			break ;
		case REQUEST_JOB_RESOURCE :
			break ;
		case RESPONSE_JOB_RESOURCE :
			break ;
		case REQUEST_RUN_JOB_STEP :
			break ;
		case RESPONSE_RUN_JOB_STEP:
			break ;
		case MESSAGE_TASK_EXIT :
			rc = unpack_task_exit_msg ( ( task_exit_msg_t ** ) & (msg->data )  , buffer ) ;
			break ;
		case REQUEST_BATCH_JOB_LAUNCH :
			rc = unpack_batch_job_launch ( ( batch_job_launch_msg_t **) &(msg -> data) , buffer) ;
			break ;
		case MESSAGE_UPLOAD_ACCOUNTING_INFO :
			break ;
		case RESPONSE_SLURM_RC:
			rc = unpack_return_code ( ( return_code_msg_t **) &(msg -> data) , buffer ) ;
			break;
		case RESPONSE_JOB_STEP_CREATE:
			rc = unpack_job_step_create_response_msg(( job_step_create_response_msg_t ** ) &msg -> data , buffer ) ;	
			break;
		case REQUEST_JOB_STEP_CREATE:
			rc = unpack_job_step_create_request_msg(( job_step_create_request_msg_t ** ) &msg -> data , buffer ) ;	
			break;
		default :
			debug ( "No unpack method for msg type %i",  msg -> msg_type ) ;
			return EINVAL ;
			break;
		
	}

	if (rc)
		error("Malformed RPC of type %u received", msg->msg_type);
	return rc ;
}

void pack_update_node_msg ( update_node_msg_t * msg, Buf buffer )
{
	assert ( msg != NULL );

	packstr ( msg -> node_names , buffer ) ;
	pack16 ( msg -> node_state , buffer ) ;
}

int unpack_update_node_msg ( update_node_msg_t ** msg , Buf buffer )
{
	uint16_t uint16_tmp;
	update_node_msg_t * tmp_ptr ;

	/* alloc memory for structure */	
	assert ( msg != NULL );
	tmp_ptr = xmalloc ( sizeof ( update_node_msg_t ) ) ;
	*msg = tmp_ptr ;

	safe_unpackstr_xmalloc ( & tmp_ptr -> node_names , &uint16_tmp,  buffer ) ;
	safe_unpack16 ( & tmp_ptr -> node_state , buffer ) ;
	return SLURM_SUCCESS ;

    unpack_error:
	if (tmp_ptr->node_names)
		xfree (tmp_ptr->node_names);
	xfree (tmp_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

void pack_node_registration_status_msg ( slurm_node_registration_status_msg_t * msg, Buf buffer )
{
	int i;
	assert ( msg != NULL );
	
	pack32 ( msg -> timestamp , buffer ) ;
	packstr ( msg -> node_name , buffer ) ;
	pack32 ( msg -> cpus , buffer ) ;
	pack32 ( msg -> real_memory_size , buffer ) ;
	pack32 ( msg -> temporary_disk_space , buffer ) ;
	pack32 ( msg -> job_count , buffer ) ;
	for (i = 0; i < msg->job_count ; i++) {
		pack32 ( msg -> job_id[i] , buffer ) ;
	}
	for (i = 0; i < msg->job_count ; i++) {
		pack16 ( msg -> step_id[i] , buffer ) ;
	}
}

int unpack_node_registration_status_msg ( slurm_node_registration_status_msg_t ** msg , Buf buffer )
{
	uint16_t uint16_tmp;
	int i;
	slurm_node_registration_status_msg_t * node_reg_ptr ;

	/* alloc memory for structure */	
	assert ( msg != NULL );
	node_reg_ptr = xmalloc ( sizeof ( slurm_node_registration_status_msg_t ) ) ;
	*msg = node_reg_ptr ;

	/* unpack timestamp of snapshot */
	safe_unpack32 ( & node_reg_ptr -> timestamp , buffer ) ;
	/* load the data values */
	safe_unpackstr_xmalloc ( & node_reg_ptr -> node_name , &uint16_tmp,  buffer ) ;
	safe_unpack32 ( & node_reg_ptr -> cpus , buffer ) ;
	safe_unpack32 ( & node_reg_ptr -> real_memory_size , buffer ) ;
	safe_unpack32 ( & node_reg_ptr -> temporary_disk_space , buffer ) ;
	safe_unpack32 ( & node_reg_ptr -> job_count , buffer ) ;
	node_reg_ptr -> job_id = xmalloc (sizeof (uint32_t) * node_reg_ptr->job_count);
	for (i = 0; i < node_reg_ptr->job_count ; i++) {
		safe_unpack32 ( & node_reg_ptr->job_id[i] , buffer ) ;
	}
	node_reg_ptr -> step_id = xmalloc (sizeof (uint16_t) * node_reg_ptr->job_count);
	for (i = 0; i < node_reg_ptr->job_count ; i++) {
		safe_unpack16 ( & node_reg_ptr->step_id[i] , buffer ) ;
	}
	return SLURM_SUCCESS ;

    unpack_error:
	if (node_reg_ptr->node_name)
		xfree (node_reg_ptr->node_name);
	if (node_reg_ptr->job_id)
		xfree (node_reg_ptr->job_id);
	if (node_reg_ptr->step_id)
		xfree (node_reg_ptr->step_id);
	xfree (node_reg_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

void pack_resource_allocation_response_msg ( resource_allocation_response_msg_t * msg, Buf buffer )
{
	assert ( msg != NULL );
	
	pack32 ( msg->job_id , buffer ) ;
	packstr ( msg->node_list , buffer ) ;

	pack16 ( msg->num_cpu_groups , buffer ) ;
	pack32_array ( msg->cpus_per_node, msg->num_cpu_groups , buffer ) ;
	pack32_array ( msg->cpu_count_reps, msg->num_cpu_groups, buffer ) ;

	pack16 ( msg->node_cnt , buffer ) ;
	pack_slurm_addr_array ( msg->node_addr, msg->node_cnt , buffer ) ;
}

int unpack_resource_allocation_response_msg ( resource_allocation_response_msg_t ** msg , Buf buffer )
{
	uint16_t uint16_tmp;
	resource_allocation_response_msg_t * tmp_ptr ;
	
	/* alloc memory for structure */	
	assert ( msg != NULL );
	tmp_ptr = xmalloc ( sizeof ( resource_allocation_response_msg_t ) ) ;
	*msg = tmp_ptr ;

	/* load the data values */
	safe_unpack32 ( & tmp_ptr -> job_id , buffer ) ;
	safe_unpackstr_xmalloc ( & tmp_ptr -> node_list , &uint16_tmp,  buffer ) ;

	safe_unpack16 ( & tmp_ptr -> num_cpu_groups , buffer ) ;
	if ( tmp_ptr -> num_cpu_groups > 0 ){ 
		tmp_ptr->cpus_per_node = (uint32_t*) xmalloc( sizeof(uint32_t) * tmp_ptr -> num_cpu_groups );
		tmp_ptr->cpu_count_reps = (uint32_t*) xmalloc( sizeof(uint32_t) * tmp_ptr -> num_cpu_groups );
		safe_unpack32_array ( (uint32_t**) &(tmp_ptr->cpus_per_node), &uint16_tmp, buffer ) ;
		safe_unpack32_array ( (uint32_t**) &(tmp_ptr->cpu_count_reps), &uint16_tmp,  buffer ) ;
	}
	else
	{
		tmp_ptr->cpus_per_node = NULL;
		tmp_ptr->cpu_count_reps = NULL;
	}

	safe_unpack16 ( & tmp_ptr -> node_cnt , buffer ) ;
	if ( tmp_ptr -> node_cnt > 0 ) {
		if (unpack_slurm_addr_array ( &(tmp_ptr->node_addr) , 
		                              &(tmp_ptr->node_cnt) , buffer ))
			goto unpack_error;
	} else
		tmp_ptr->node_addr = NULL;

	info ("job id is %u", tmp_ptr->job_id);
	return SLURM_SUCCESS ;

    unpack_error:
	if (tmp_ptr->node_list)
		xfree (tmp_ptr->node_list);
	if (tmp_ptr->cpus_per_node)
		xfree (tmp_ptr->cpus_per_node);
	if (tmp_ptr->cpu_count_reps)
		xfree (tmp_ptr->cpu_count_reps);
	xfree (tmp_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

void pack_resource_allocation_and_run_response_msg ( resource_allocation_and_run_response_msg_t * msg, Buf buffer )
{
	assert ( msg != NULL );
	
	pack32 ( msg->job_id , buffer ) ;
	packstr ( msg->node_list , buffer ) ;
	pack16 ( msg->num_cpu_groups , buffer ) ;
	pack32_array ( msg->cpus_per_node, msg->num_cpu_groups , buffer ) ;
	pack32_array ( msg->cpu_count_reps, msg->num_cpu_groups, buffer ) ;
	pack32 ( msg -> job_step_id , buffer ) ;

	pack16 ( msg->node_cnt , buffer ) ;
	pack_slurm_addr_array ( msg->node_addr, msg->node_cnt , buffer ) ;

	pack_job_credential( msg->credentials, buffer ) ;
#ifdef HAVE_LIBELAN3
	qsw_pack_jobinfo( msg -> qsw_job , buffer ) ;
#endif
}

int unpack_resource_allocation_and_run_response_msg ( resource_allocation_and_run_response_msg_t ** msg , Buf buffer )
{
	uint16_t uint16_tmp;
	resource_allocation_and_run_response_msg_t * tmp_ptr ;
	
	/* alloc memory for structure */	
	assert ( msg != NULL );
	tmp_ptr = xmalloc ( sizeof ( resource_allocation_and_run_response_msg_t ) ) ;
	*msg = tmp_ptr ;

	/* load the data values */
	safe_unpack32 ( & tmp_ptr -> job_id , buffer ) ;
	safe_unpackstr_xmalloc ( & tmp_ptr -> node_list , &uint16_tmp,  buffer ) ;
	safe_unpack16 ( & tmp_ptr -> num_cpu_groups , buffer ) ;

	if ( tmp_ptr -> num_cpu_groups > 0 ){ 
		tmp_ptr->cpus_per_node = (uint32_t*) xmalloc( sizeof(uint32_t) * tmp_ptr -> num_cpu_groups );
		tmp_ptr->cpu_count_reps = (uint32_t*) xmalloc( sizeof(uint32_t) * tmp_ptr -> num_cpu_groups );
		safe_unpack32_array ( (uint32_t**) &(tmp_ptr->cpus_per_node), &uint16_tmp, buffer ) ;
		safe_unpack32_array ( (uint32_t**) &(tmp_ptr->cpu_count_reps), &uint16_tmp, buffer ) ;
	}

	safe_unpack32 ( &tmp_ptr -> job_step_id, buffer ) ;
	safe_unpack16 ( & tmp_ptr -> node_cnt , buffer ) ;
	if ( tmp_ptr -> node_cnt > 0 ) {
		if (unpack_slurm_addr_array ( &(tmp_ptr->node_addr) , 
		                              &(tmp_ptr->node_cnt) , buffer ))
			goto unpack_error;
	} else
		tmp_ptr->node_addr = NULL;

	if (unpack_job_credential( &tmp_ptr->credentials, buffer ))
		goto unpack_error;
#ifdef HAVE_LIBELAN3
	qsw_alloc_jobinfo(&tmp_ptr->qsw_job);
	if (qsw_unpack_jobinfo(tmp_ptr->qsw_job, buffer) < 0) {
		error("qsw_unpack_jobinfo: %m");
		qsw_free_jobinfo (tmp_ptr->qsw_job);
		goto unpack_error;
	}
#endif
	return SLURM_SUCCESS ;

    unpack_error:
	if (tmp_ptr->node_list)
		xfree (tmp_ptr->node_list);
	if (tmp_ptr->cpus_per_node)
		xfree (tmp_ptr->cpus_per_node);
	if (tmp_ptr->cpu_count_reps)
		xfree (tmp_ptr->cpu_count_reps);
	xfree (tmp_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

void pack_submit_response_msg ( submit_response_msg_t * msg, Buf buffer )
{
	assert ( msg != NULL );
	
	pack32 ( msg->job_id , buffer ) ;
}

int unpack_submit_response_msg ( submit_response_msg_t ** msg , Buf buffer )
{
	submit_response_msg_t * tmp_ptr ;

	/* alloc memory for structure */	
	assert ( msg != NULL );
	tmp_ptr = xmalloc ( sizeof ( submit_response_msg_t ) ) ;
	*msg = tmp_ptr ;

	/* load the data values */
	safe_unpack32 ( & tmp_ptr -> job_id , buffer ) ;
	return SLURM_SUCCESS ;

    unpack_error:
	xfree (tmp_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}
void pack_node_info_msg ( slurm_msg_t * msg, Buf buffer )
{
	packmem_array ( msg->data , msg->data_size, buffer );
}

int unpack_node_info_msg ( node_info_msg_t ** msg , Buf buffer )
{
	int i;
	node_info_t *node = NULL;

	assert ( msg != NULL );
	*msg = xmalloc ( sizeof ( node_info_msg_t ) );

	/* load buffer's header (data structure version and time) */
	safe_unpack32 (&((*msg) -> record_count), buffer );
	safe_unpack_time (&((*msg) -> last_update ) , buffer );

	node = (*msg) -> node_array = xmalloc ( sizeof ( node_info_t ) * (*msg)->record_count ) ;

	/* load individual job info */
	for (i = 0; i < (*msg)->record_count ; i++) {
		if (unpack_node_info_members ( & node[i] , buffer ) )
			goto unpack_error;

	}
	return SLURM_SUCCESS;

    unpack_error:
	if (node)
		xfree (node);
	xfree (*msg);
	*msg = NULL;
	return SLURM_ERROR;
}


int unpack_node_info ( node_info_t ** node , Buf buffer )
{
	assert ( node != NULL );

	*node = xmalloc ( sizeof(node_info_t) );
	if (unpack_node_info_members ( *node , buffer )) {
		xfree (*node);
		*node = NULL;
		return SLURM_ERROR;
	} else
		return SLURM_SUCCESS ;
}

int unpack_node_info_members ( node_info_t * node , Buf buffer )
{
	uint16_t uint16_tmp;

	assert ( node != NULL );

	safe_unpackstr_xmalloc (&node->name, &uint16_tmp, buffer);
	safe_unpack16  (&node->node_state, buffer);
	safe_unpack32  (&node->cpus, buffer);
	safe_unpack32  (&node->real_memory, buffer);
	safe_unpack32  (&node->tmp_disk, buffer);
	safe_unpack32  (&node->weight, buffer);
	safe_unpackstr_xmalloc (&node->features, &uint16_tmp, buffer);
	safe_unpackstr_xmalloc (&node->partition, &uint16_tmp, buffer);

	return SLURM_SUCCESS;

    unpack_error:
	if (node->name) {
		xfree (node->name);
		node->name = NULL;
	}
	if (node->features) {
		xfree (node->features);
		node->features = NULL;
	}
	if (node->partition) {
		xfree (node->partition);
		node->partition = NULL;
	}
	return SLURM_ERROR;
}


void
pack_update_partition_msg ( update_part_msg_t * msg , Buf buffer )
{
	assert ( msg != NULL );

	packstr ( msg -> name, buffer ) ;
	pack32 ( msg -> max_time, buffer ) ;
	pack32 ( msg -> max_nodes, buffer ) ;
	pack16 ( msg -> default_part, buffer ) ;
	pack16 ( msg -> root_only, buffer ) ;
	pack16 ( msg -> shared, buffer ) ;
	pack16 ( msg -> state_up, buffer ) ;
	packstr ( msg -> nodes, buffer ) ;
	packstr ( msg -> allow_groups, buffer ) ;
}

int unpack_update_partition_msg ( update_part_msg_t ** msg , Buf buffer  )
{
	uint16_t uint16_tmp;
	update_part_msg_t * tmp_ptr ;
	
	assert ( msg != NULL );

	/* alloc memory for structure */	
	tmp_ptr = xmalloc ( sizeof ( update_part_msg_t ) ) ;
	*msg = tmp_ptr;

	safe_unpackstr_xmalloc ( &tmp_ptr -> name, &uint16_tmp,  buffer ) ;
	safe_unpack32 ( &tmp_ptr -> max_time, buffer ) ;
	safe_unpack32 ( &tmp_ptr -> max_nodes, buffer ) ;
	safe_unpack16 ( &tmp_ptr -> default_part, buffer ) ;
	safe_unpack16 ( &tmp_ptr -> root_only, buffer ) ;
	safe_unpack16 ( &tmp_ptr -> shared, buffer ) ;
	safe_unpack16 ( &tmp_ptr -> state_up, buffer ) ;
	safe_unpackstr_xmalloc ( &tmp_ptr -> nodes, &uint16_tmp, buffer ) ;
	safe_unpackstr_xmalloc ( &tmp_ptr -> allow_groups, &uint16_tmp, buffer ) ;
	return SLURM_SUCCESS;

    unpack_error:
	if (tmp_ptr -> name)
		xfree (tmp_ptr -> name);
	if (tmp_ptr -> nodes)
		xfree (tmp_ptr -> nodes);
	if (tmp_ptr -> allow_groups)
		xfree (tmp_ptr -> allow_groups);
	xfree (tmp_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

void pack_job_step_create_request_msg ( job_step_create_request_msg_t* msg , Buf buffer )
{
	assert ( msg != NULL );

	pack32 ( msg -> job_id, buffer ) ;
	pack32 ( msg -> user_id, buffer ) ;
	pack32 ( msg -> node_count, buffer ) ;
	pack32 ( msg -> cpu_count, buffer ) ;
	pack16 ( msg -> relative, buffer ) ;
	pack16 ( msg -> task_dist, buffer ) ;
	packstr ( msg -> node_list, buffer ) ;
}

int unpack_job_step_create_request_msg ( job_step_create_request_msg_t** msg , Buf buffer )
{
	uint16_t uint16_tmp;
	job_step_create_request_msg_t * tmp_ptr ;

	/* alloc memory for structure */	
	assert ( msg != NULL );
	tmp_ptr = xmalloc ( sizeof ( job_step_create_request_msg_t ) ) ;
	*msg = tmp_ptr;

	safe_unpack32 ( &( tmp_ptr -> job_id), buffer ) ;
	safe_unpack32 ( &( tmp_ptr -> user_id), buffer ) ;
	safe_unpack32 ( &( tmp_ptr -> node_count), buffer ) ;
	safe_unpack32 ( &( tmp_ptr -> cpu_count), buffer ) ;
	safe_unpack16 ( &( tmp_ptr -> relative), buffer ) ;
	safe_unpack16 ( &( tmp_ptr -> task_dist), buffer ) ;
	safe_unpackstr_xmalloc ( &( tmp_ptr -> node_list ), &uint16_tmp, buffer ) ;

	return SLURM_SUCCESS;

    unpack_error:
	if (tmp_ptr -> node_list)
		xfree (tmp_ptr -> node_list);
	xfree (tmp_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

void pack_revoke_credential_msg ( revoke_credential_msg_t* msg , Buf buffer )
{
	assert ( msg != NULL );

	pack32( msg->job_id, buffer ) ;
	pack32( ( uint32_t ) msg->expiration_time, buffer ) ;
	packmem_array( msg->signature, (uint32_t) SLURM_SSL_SIGNATURE_LENGTH , buffer ) ; 
}

int unpack_revoke_credential_msg ( revoke_credential_msg_t** msg , Buf buffer )
{
	revoke_credential_msg_t* tmp_ptr ;

	/* alloc memory for structure */	
	assert (msg);
	tmp_ptr = xmalloc ( sizeof ( slurm_job_credential_t ) ) ;
	*msg = tmp_ptr;

	safe_unpack32( &(tmp_ptr->job_id), buffer ) ;
	safe_unpack32( (uint32_t*) &(tmp_ptr->expiration_time), buffer ) ;
	safe_unpackmem_array( tmp_ptr->signature, 
	                      (uint32_t) SLURM_SSL_SIGNATURE_LENGTH , buffer ) ; 

	return SLURM_SUCCESS;

    unpack_error:
	xfree (tmp_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

void pack_job_credential ( slurm_job_credential_t* cred , Buf buffer )
{
	assert ( cred != NULL );

	pack32( cred->job_id, buffer ) ;
	pack16( (uint16_t) cred->user_id, buffer ) ;
	packstr( cred->node_list, buffer ) ;
	pack_time( cred->expiration_time, buffer ) ;
	packmem_array( cred->signature, (uint32_t) SLURM_SSL_SIGNATURE_LENGTH , buffer ) ; 
}

int unpack_job_credential( slurm_job_credential_t** msg , Buf buffer )
{
	uint16_t uint16_tmp;
	slurm_job_credential_t* tmp_ptr ;

	/* alloc memory for structure */	
	tmp_ptr = xmalloc ( sizeof ( slurm_job_credential_t ) ) ;
	*msg = tmp_ptr;

	safe_unpack32( &(tmp_ptr->job_id), buffer ) ;
	safe_unpack16( (uint16_t*) &(tmp_ptr->user_id), buffer ) ;
	safe_unpackstr_xmalloc ( &(tmp_ptr->node_list), &uint16_tmp,  buffer ) ;
	safe_unpack_time( &(tmp_ptr->expiration_time), buffer ) ;
	safe_unpackmem_array( tmp_ptr->signature, 
	                      (uint32_t) SLURM_SSL_SIGNATURE_LENGTH , buffer ) ; 

	return SLURM_SUCCESS;

    unpack_error:
	if (tmp_ptr->node_list)
		xfree (tmp_ptr->node_list);
	xfree (tmp_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

void pack_job_step_create_response_msg (  job_step_create_response_msg_t* msg , Buf buffer )
{
	assert ( msg != NULL );

	pack32 ( msg -> job_step_id , buffer ) ;
	packstr ( msg -> node_list, buffer ) ;
	pack_job_credential( msg->credentials, buffer ) ;
#ifdef HAVE_LIBELAN3
	qsw_pack_jobinfo( msg->qsw_job , buffer ) ;
#endif
 	
}

int unpack_job_step_create_response_msg (job_step_create_response_msg_t** msg , Buf buffer )
{
	uint16_t uint16_tmp;
	job_step_create_response_msg_t * tmp_ptr ;

	/* alloc memory for structure */	
	assert ( msg != NULL );
	tmp_ptr = xmalloc ( sizeof ( job_step_create_response_msg_t ) ) ;
	*msg = tmp_ptr;

	safe_unpack32 ( &tmp_ptr -> job_step_id, buffer ) ;
	safe_unpackstr_xmalloc ( &tmp_ptr -> node_list, &uint16_tmp,  buffer ) ;
	if (unpack_job_credential( &tmp_ptr->credentials, buffer ) )
		goto unpack_error;

#ifdef HAVE_LIBELAN3
	qsw_alloc_jobinfo(&tmp_ptr->qsw_job);
	if (qsw_unpack_jobinfo( tmp_ptr -> qsw_job , buffer ) ) {
		error("qsw_unpack_jobinfo: %m");
		qsw_free_jobinfo (tmp_ptr->qsw_job);
		goto unpack_error;
	}
#endif
	return SLURM_SUCCESS;

    unpack_error:
	if (tmp_ptr -> node_list)
		xfree (tmp_ptr -> node_list);
	xfree (tmp_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}


void pack_partition_info_msg ( slurm_msg_t * msg, Buf buffer )
{	
	packmem_array(msg->data, msg->data_size, buffer);
}

int unpack_partition_info_msg ( partition_info_msg_t ** msg , Buf buffer )
{
        int i;
        partition_info_t *partition = NULL;

        *msg = xmalloc ( sizeof ( partition_info_msg_t ) );

        /* load buffer's header (data structure version and time) */
        safe_unpack32 (&((*msg) -> record_count), buffer);
        safe_unpack_time (&((*msg) -> last_update ) , buffer);

        partition = (*msg) -> partition_array = 
			xmalloc ( sizeof ( partition_info_t ) * (*msg)->record_count ) ;

        /* load individual job info */
        for (i = 0; i < (*msg)->record_count ; i++) {
		if (unpack_partition_info_members ( & partition[i] , buffer ))
			goto unpack_error;
        }
        return SLURM_SUCCESS;

    unpack_error:
	xfree (*msg);
	*msg = NULL;
	if (partition)
		xfree (partition);
	return SLURM_ERROR;
}


int unpack_partition_info ( partition_info_t ** part , Buf buffer )
{
	*part = xmalloc ( sizeof(partition_info_t) );
	if (unpack_partition_info_members ( *part , buffer ) ) {
		xfree (*part);
		*part = NULL;
		return SLURM_ERROR;
	} else
		return SLURM_SUCCESS ;
}

int unpack_partition_info_members ( partition_info_t * part , Buf buffer )
{
	uint16_t uint16_tmp;
	char * node_inx_str = NULL;

	safe_unpackstr_xmalloc (&part->name, &uint16_tmp, buffer);
	if (part->name == NULL)
		part->name = xmalloc(1);	/* part->name = "" implicit set */
	safe_unpack32  (&part->max_time, buffer);
	safe_unpack32  (&part->max_nodes, buffer);
	safe_unpack32  (&part->total_nodes, buffer);

	safe_unpack32  (&part->total_cpus, buffer);
	safe_unpack16  (&part->default_part, buffer);
	safe_unpack16  (&part->root_only, buffer);
	safe_unpack16  (&part->shared, buffer);

	safe_unpack16  (&part->state_up, buffer);
	safe_unpackstr_xmalloc (&part->allow_groups, &uint16_tmp, buffer);
	safe_unpackstr_xmalloc (&part->nodes, &uint16_tmp, buffer);
	safe_unpackstr_xmalloc (&node_inx_str, &uint16_tmp, buffer);
	if (node_inx_str == NULL)
		part->node_inx = bitfmt2int("");
	else {
		part->node_inx = bitfmt2int(node_inx_str);
		xfree ( node_inx_str );
		node_inx_str = NULL;
	}
	return SLURM_SUCCESS;

    unpack_error:
	if (part->name)
		xfree (part->name);
	if (part->allow_groups)
		xfree (part->allow_groups);
	if (part->nodes)
		xfree (part->nodes);
	if (node_inx_str)
		xfree (node_inx_str);
	return SLURM_ERROR;
}

void pack_job_step_info_members(   uint32_t job_id, uint16_t step_id, 
		uint32_t user_id, time_t start_time, char *partition, char *nodes, 
		Buf buffer )
{
	pack32 ( job_id , buffer ) ;
	pack16 ( step_id , buffer ) ;
	pack32 ( user_id , buffer ) ;
	pack_time ( start_time , buffer ) ;
	packstr ( partition, buffer ) ;
	packstr ( nodes, buffer ) ;
	
}

void pack_job_step_info ( job_step_info_t* step, Buf buffer )
{
	pack_job_step_info_members( 
				step->job_id, 
				step->step_id, 
				step->user_id, 
				step->start_time, 
				step->partition ,
				step->nodes,
				buffer
			);
}

int unpack_job_step_info_members ( job_step_info_t * step , Buf buffer )
{
	uint16_t uint16_tmp = 0;

	safe_unpack32  (&step->job_id, buffer);
	safe_unpack16  (&step->step_id, buffer);
	safe_unpack32  (&step->user_id, buffer);
	safe_unpack_time (&step->start_time, buffer);
	safe_unpackstr_xmalloc (&step->partition, &uint16_tmp, buffer);
	safe_unpackstr_xmalloc (&step->nodes, &uint16_tmp, buffer);

	return SLURM_SUCCESS;

    unpack_error:
	if (step->partition)
		xfree (step->partition);
	if (step->nodes)
		xfree (step->nodes);
	return SLURM_ERROR;
}

int unpack_job_step_info ( job_step_info_t ** step , Buf buffer )
{
	*step = xmalloc( sizeof( job_step_info_t ) );
	if (unpack_job_step_info_members( *step, buffer )) {
		xfree (*step);
		*step = NULL;
		return SLURM_ERROR;
	} else
		return SLURM_SUCCESS;
}

int unpack_job_step_info_response_msg ( job_step_info_response_msg_t** msg, Buf buffer )
{
	int i=0;
	job_step_info_t * step;

	*msg = xmalloc( sizeof( job_step_info_response_msg_t ) );

	safe_unpack_time (&(*msg)->last_update , buffer);
	safe_unpack32 (&(*msg)->job_step_count , buffer);
	
	step = (*msg)->job_steps = xmalloc( sizeof(job_step_info_t) * (*msg)->job_step_count );

	for ( i=0; i < (*msg)->job_step_count; i++ )
		if (unpack_job_step_info_members ( &step[i] , buffer))
			goto unpack_error;

	return SLURM_SUCCESS;

    unpack_error:
	if (step)
		xfree (step);
	xfree (*msg);
	*msg = NULL;
	return SLURM_ERROR;
}

void pack_buffer_msg ( slurm_msg_t * msg, Buf buffer )
{
	packmem_array ( msg->data , msg->data_size, buffer );
}

int unpack_job_info_msg ( job_info_msg_t ** msg , Buf buffer )
{
	int i;
	job_info_t *job = NULL;
	
	*msg = xmalloc ( sizeof ( job_info_msg_t ) );

	/* load buffer's header (data structure version and time) */
	safe_unpack32 (&((*msg) -> record_count), buffer);
	safe_unpack_time (&((*msg) -> last_update ) , buffer);
	job = (*msg) -> job_array = xmalloc ( sizeof ( job_info_t ) * (*msg)->record_count ) ;

	/* load individual job info */
	for (i = 0; i < (*msg)->record_count ; i++) {
		if (unpack_job_info_members ( & job[i] , buffer ))
			goto unpack_error;
	}
	return SLURM_SUCCESS;

    unpack_error:
	if (job)
		xfree (job);
	xfree (*msg);
	*msg = NULL;
        return SLURM_ERROR;
}

int unpack_job_info ( job_info_t ** msg , Buf buffer )
{
	*msg = xmalloc ( sizeof ( job_info_t ) ) ;
	if (unpack_job_info_members ( *msg , buffer ) )
		goto unpack_error;
	return SLURM_SUCCESS ;

    unpack_error:
	xfree (*msg);
	*msg = NULL;
        return SLURM_ERROR;
}

int unpack_job_info_members ( job_info_t * job , Buf buffer )
{
	uint16_t uint16_tmp;
	char * node_inx_str;

	safe_unpack32  (&job->job_id, buffer);
	safe_unpack32  (&job->user_id, buffer);
	safe_unpack16  (&job->job_state, buffer);
	safe_unpack32  (&job->time_limit, buffer);

	safe_unpack_time  (&job->start_time, buffer);
	safe_unpack_time  (&job->end_time, buffer);
	safe_unpack32  (&job->priority, buffer);

	safe_unpackstr_xmalloc (&job->nodes, &uint16_tmp, buffer);
	safe_unpackstr_xmalloc (&job->partition, &uint16_tmp, buffer);
	safe_unpackstr_xmalloc (&job->name, &uint16_tmp, buffer);
	safe_unpackstr_xmalloc (&node_inx_str, &uint16_tmp, buffer);
	if (node_inx_str == NULL)
		job->node_inx = bitfmt2int("");
	else {
		job->node_inx = bitfmt2int(node_inx_str);
		xfree ( node_inx_str );
	}

	safe_unpack32  (&job->num_procs, buffer);
	safe_unpack32  (&job->num_nodes, buffer);
	safe_unpack16  (&job->shared, buffer);
	safe_unpack16  (&job->contiguous, buffer);

	safe_unpack32  (&job->min_procs, buffer);
	safe_unpack32  (&job->min_memory, buffer);
	safe_unpack32  (&job->min_tmp_disk, buffer);

	safe_unpackstr_xmalloc (&job->req_nodes, &uint16_tmp, buffer);
	safe_unpackstr_xmalloc (&node_inx_str, &uint16_tmp, buffer);
	if (node_inx_str == NULL)
		job->req_node_inx = bitfmt2int("");
	else {
		job->req_node_inx = bitfmt2int(node_inx_str);
		xfree ( node_inx_str );
	}
	safe_unpackstr_xmalloc (&job->features, &uint16_tmp, buffer);
	return SLURM_SUCCESS ;

    unpack_error:
	if (job->nodes)
		xfree (job->nodes);
	if (job->partition)
		xfree (job->partition);
	if (job->name)
		xfree (job->name);
	if (job->req_nodes)
		xfree (job->req_nodes);
	if (job->features)
		xfree (job->features);
        return SLURM_ERROR;
}

void pack_slurm_ctl_conf ( slurm_ctl_conf_info_msg_t * build_ptr, Buf buffer )
{	
	pack_time (build_ptr->last_update, buffer);
	packstr (build_ptr->backup_addr, buffer);
	packstr (build_ptr->backup_controller, buffer);
	packstr (build_ptr->control_addr, buffer);
	packstr (build_ptr->control_machine, buffer);
	packstr (build_ptr->epilog, buffer);
	pack16 (build_ptr->fast_schedule, buffer);
	pack16 (build_ptr->hash_base, buffer);
	pack16 (build_ptr->heartbeat_interval, buffer);
	pack16 (build_ptr->inactive_limit, buffer);
	pack16 (build_ptr->kill_wait, buffer);
	packstr (build_ptr->prioritize, buffer);
	packstr (build_ptr->prolog, buffer);
	pack16 (build_ptr->ret2service, buffer);
	packstr (build_ptr->slurmctld_logfile, buffer);
	pack16 (build_ptr->slurmctld_timeout, buffer);
	packstr (build_ptr->slurmd_logfile, buffer);
	packstr (build_ptr->slurmd_spooldir, buffer);
	pack16 (build_ptr->slurmd_timeout, buffer);
	packstr (build_ptr->slurm_conf, buffer);
	packstr (build_ptr->state_save_location, buffer);
	packstr (build_ptr->tmp_fs, buffer);
	packstr (build_ptr->job_credential_private_key, buffer);
	packstr (build_ptr->job_credential_public_certificate, buffer);
}

int unpack_slurm_ctl_conf ( slurm_ctl_conf_info_msg_t **build_buffer_ptr, Buf buffer )
{	
	uint16_t uint16_tmp;
	slurm_ctl_conf_info_msg_t * build_ptr ;

	/* alloc memory for structure */	
	build_ptr = xmalloc ( sizeof ( slurm_ctl_conf_t ) ) ;
	*build_buffer_ptr = build_ptr ;

	/* load the data values */
	/* unpack timestamp of snapshot */
	safe_unpack_time (&build_ptr->last_update, buffer);
	safe_unpackstr_xmalloc (&build_ptr->backup_addr, &uint16_tmp, buffer);
	safe_unpackstr_xmalloc (&build_ptr->backup_controller, &uint16_tmp, buffer);
	safe_unpackstr_xmalloc (&build_ptr->control_addr, &uint16_tmp, buffer);
	safe_unpackstr_xmalloc (&build_ptr->control_machine, &uint16_tmp, buffer);
	safe_unpackstr_xmalloc (&build_ptr->epilog, &uint16_tmp, buffer);
	safe_unpack16 (&build_ptr->fast_schedule, buffer);
	safe_unpack16 (&build_ptr->hash_base, buffer);
	safe_unpack16 (&build_ptr->heartbeat_interval, buffer);
	safe_unpack16 (&build_ptr->inactive_limit, buffer);
	safe_unpack16 (&build_ptr->kill_wait, buffer);
	safe_unpackstr_xmalloc (&build_ptr->prioritize, &uint16_tmp, buffer);
	safe_unpackstr_xmalloc (&build_ptr->prolog, &uint16_tmp, buffer);
	safe_unpack16 (&build_ptr->ret2service, buffer);
	safe_unpackstr_xmalloc (&build_ptr->slurmctld_logfile, &uint16_tmp, buffer);
	safe_unpack16 (&build_ptr->slurmctld_timeout, buffer);
	safe_unpackstr_xmalloc (&build_ptr->slurmd_logfile, &uint16_tmp, buffer);
	safe_unpackstr_xmalloc (&build_ptr->slurmd_spooldir, &uint16_tmp, buffer);
	safe_unpack16 (&build_ptr->slurmd_timeout, buffer);
	safe_unpackstr_xmalloc (&build_ptr->slurm_conf, &uint16_tmp, buffer);
	safe_unpackstr_xmalloc (&build_ptr->state_save_location, &uint16_tmp, buffer);
	safe_unpackstr_xmalloc (&build_ptr->tmp_fs, &uint16_tmp, buffer);
	safe_unpackstr_xmalloc (&build_ptr->job_credential_private_key, &uint16_tmp, buffer);
	safe_unpackstr_xmalloc (&build_ptr->job_credential_public_certificate, &uint16_tmp, buffer);
	return SLURM_SUCCESS ;

    unpack_error:
	if (build_ptr->backup_addr)
		xfree (build_ptr->backup_addr);
	if (build_ptr->control_addr)
		xfree (build_ptr->control_addr);
	if (build_ptr->control_addr)
		xfree (build_ptr->control_addr);
	if (build_ptr->control_machine)
		xfree (build_ptr->control_machine);
	if (build_ptr->epilog)
		xfree (build_ptr->epilog);
	if (build_ptr->prioritize)
		xfree (build_ptr->prioritize);
	if (build_ptr->prolog)
		xfree (build_ptr->prolog);
	if (build_ptr->slurmctld_logfile)
		xfree (build_ptr->slurmctld_logfile);
	if (build_ptr->slurmd_logfile)
		xfree (build_ptr->slurmd_logfile);
	if (build_ptr->slurmd_spooldir)
		xfree (build_ptr->slurmd_spooldir);
	if (build_ptr->slurm_conf)
		xfree (build_ptr->slurm_conf);
	if (build_ptr->state_save_location)
		xfree (build_ptr->state_save_location);
	if (build_ptr->tmp_fs)
		xfree (build_ptr->tmp_fs);
	if (build_ptr->job_credential_private_key)
		xfree (build_ptr->job_credential_private_key);
	if (build_ptr->job_credential_public_certificate)
		xfree (build_ptr->job_credential_public_certificate);
        xfree (build_ptr);
        *build_buffer_ptr = NULL;
        return SLURM_ERROR;
}

/* pack_job_desc
 * packs a job_desc struct 
 * header 	- the body structure to pack
 * buffer	- destination of the pack, contains pointers that are automatically updated
 */
void pack_job_desc ( job_desc_msg_t * job_desc_ptr, Buf buffer )
{	
	/* load the data values */
	pack16 (job_desc_ptr->contiguous, buffer);
	pack16 (job_desc_ptr->kill_on_node_fail, buffer);
	packstr (job_desc_ptr->features, buffer);
	pack32 (job_desc_ptr->job_id, buffer);
	packstr (job_desc_ptr->name, buffer);
	
	pack32 (job_desc_ptr->min_procs, buffer);
	pack32 (job_desc_ptr->min_memory, buffer);
	pack32 (job_desc_ptr->min_tmp_disk, buffer);
	
	packstr (job_desc_ptr->partition, buffer);
	pack32 (job_desc_ptr->priority, buffer);
	
	packstr (job_desc_ptr->req_nodes, buffer);
	packstr_array (job_desc_ptr->environment, job_desc_ptr->env_size, buffer);
	packstr (job_desc_ptr->script, buffer);

	packstr (job_desc_ptr->err, buffer);
	packstr (job_desc_ptr->in, buffer);
	packstr (job_desc_ptr->out, buffer);
	packstr (job_desc_ptr->work_dir, buffer);

	pack16 (job_desc_ptr->shared, buffer);
	pack32 (job_desc_ptr->time_limit, buffer);
	
	pack32 (job_desc_ptr->num_procs, buffer);
	pack32 (job_desc_ptr->num_nodes, buffer);
	pack32 (job_desc_ptr->user_id, buffer);

}

/* unpack_job_desc
 * unpacks a job_desc struct
 * header 	- the body structure to unpack
 * buffer	- source of the unpack, contains pointers that are automatically updated
 */
int unpack_job_desc ( job_desc_msg_t **job_desc_buffer_ptr, Buf buffer )
{	
	uint16_t uint16_tmp;
	job_desc_msg_t * job_desc_ptr ;

	/* alloc memory for structure */
	job_desc_ptr = xmalloc ( sizeof ( job_desc_msg_t ) ) ;
	*job_desc_buffer_ptr = job_desc_ptr ;

	/* load the data values */
	safe_unpack16 (&job_desc_ptr->contiguous, buffer);
	safe_unpack16 (&job_desc_ptr->kill_on_node_fail, buffer);
	safe_unpackstr_xmalloc (&job_desc_ptr->features, &uint16_tmp, buffer);
	safe_unpack32 (&job_desc_ptr->job_id, buffer);
	safe_unpackstr_xmalloc (&job_desc_ptr->name, &uint16_tmp, buffer);
	
	safe_unpack32 (&job_desc_ptr->min_procs, buffer);
	safe_unpack32 (&job_desc_ptr->min_memory, buffer);
	safe_unpack32 (&job_desc_ptr->min_tmp_disk, buffer);
	
	safe_unpackstr_xmalloc (&job_desc_ptr->partition, &uint16_tmp, buffer);
	safe_unpack32 (&job_desc_ptr->priority, buffer);

	safe_unpackstr_xmalloc (&job_desc_ptr->req_nodes, &uint16_tmp, buffer);
	safe_unpackstr_array (&job_desc_ptr->environment, &job_desc_ptr->env_size, buffer);
	safe_unpackstr_xmalloc (&job_desc_ptr->script, &uint16_tmp, buffer);

	safe_unpackstr_xmalloc (&job_desc_ptr->err, &uint16_tmp, buffer);
	safe_unpackstr_xmalloc (&job_desc_ptr->in, &uint16_tmp, buffer);
	safe_unpackstr_xmalloc (&job_desc_ptr->out, &uint16_tmp, buffer);
	safe_unpackstr_xmalloc (&job_desc_ptr->work_dir, &uint16_tmp, buffer);

	safe_unpack16 (&job_desc_ptr->shared, buffer);	
	safe_unpack32 (&job_desc_ptr->time_limit, buffer);
	
	safe_unpack32 (&job_desc_ptr->num_procs, buffer);
	safe_unpack32 (&job_desc_ptr->num_nodes, buffer);
	safe_unpack32 (&job_desc_ptr->user_id, buffer);

	return SLURM_SUCCESS ;

    unpack_error:
	if (job_desc_ptr->features)
		xfree (job_desc_ptr->features);
	if (job_desc_ptr->name)
		xfree (job_desc_ptr->name);
	if (job_desc_ptr->partition)
		xfree (job_desc_ptr->partition);
	if (job_desc_ptr->req_nodes)
		xfree (job_desc_ptr->req_nodes);
	if (job_desc_ptr->environment)
		xfree (job_desc_ptr->environment);
	if (job_desc_ptr->script)
		xfree (job_desc_ptr->script);
	if (job_desc_ptr->err)
		xfree (job_desc_ptr->err);
	if (job_desc_ptr->in)
		xfree (job_desc_ptr->in);
	if (job_desc_ptr->out)
		xfree (job_desc_ptr->out);
	if (job_desc_ptr->work_dir)
		xfree (job_desc_ptr->work_dir);
        xfree (job_desc_ptr);
        *job_desc_buffer_ptr = NULL;
        return SLURM_ERROR;
}

void pack_old_job_desc ( old_job_alloc_msg_t * job_desc_ptr, Buf buffer )
{	
	/* load the data values */
	pack32 (job_desc_ptr->job_id, buffer);
	pack32 (job_desc_ptr->uid, buffer);
}

int unpack_old_job_desc ( old_job_alloc_msg_t **job_desc_buffer_ptr, Buf buffer )
{	
	old_job_alloc_msg_t * job_desc_ptr ;

	/* alloc memory for structure */
	assert (job_desc_buffer_ptr != NULL);
	job_desc_ptr = xmalloc ( sizeof ( old_job_alloc_msg_t ) ) ;
	*job_desc_buffer_ptr = job_desc_ptr ;

	/* load the data values */
	safe_unpack32 (&job_desc_ptr->job_id, buffer);
	safe_unpack32 (&job_desc_ptr->uid, buffer);
	return SLURM_SUCCESS ;

    unpack_error:
        xfree (job_desc_ptr);
        *job_desc_buffer_ptr = NULL;
        return SLURM_ERROR;
}

void pack_last_update ( last_update_msg_t * msg , Buf buffer )
{
	pack_time ( msg -> last_update , buffer ) ;
}

int unpack_last_update ( last_update_msg_t ** msg , Buf buffer )
{
        last_update_msg_t * last_update_msg ;

        last_update_msg = xmalloc ( sizeof ( last_update_msg_t ) ) ;
	*msg = last_update_msg ;

	safe_unpack_time ( & last_update_msg -> last_update , buffer ) ;
	return SLURM_SUCCESS ;

    unpack_error:
        xfree (last_update_msg);
        *msg = NULL;
        return SLURM_ERROR;
}

void pack_return_code ( return_code_msg_t * msg , Buf buffer )
{
	pack32 ( msg -> return_code , buffer ) ;
}

int unpack_return_code ( return_code_msg_t ** msg , Buf buffer )
{
        return_code_msg_t * return_code_msg ;

        return_code_msg = xmalloc ( sizeof ( return_code_msg_t ) ) ;
	*msg = return_code_msg ;

	safe_unpack32 ( & return_code_msg -> return_code , buffer ) ;
	return SLURM_SUCCESS ;

    unpack_error:
        xfree (return_code_msg);
        *msg = NULL;
        return SLURM_ERROR;
}

void pack_reattach_tasks_streams_msg ( reattach_tasks_streams_msg_t * msg , Buf buffer )
{
	pack32 ( msg -> job_id , buffer ) ;
	pack32 ( msg -> job_step_id , buffer ) ;
	pack32 ( msg -> uid , buffer ) ;
	pack_job_credential ( msg -> credential , buffer ) ;
	pack32 ( msg -> tasks_to_reattach , buffer ) ;
	slurm_pack_slurm_addr ( & msg -> streams , buffer ) ;
	pack32_array ( msg -> global_task_ids , ( uint16_t ) msg -> tasks_to_reattach , buffer ) ;
}

int unpack_reattach_tasks_streams_msg ( reattach_tasks_streams_msg_t ** msg_ptr , Buf buffer )
{
	uint16_t uint16_tmp;
	reattach_tasks_streams_msg_t * msg ;

	msg = xmalloc ( sizeof ( reattach_tasks_streams_msg_t ) ) ;
	*msg_ptr = msg ;

	safe_unpack32 ( & msg -> job_id , buffer ) ;
	safe_unpack32 ( & msg -> job_step_id , buffer ) ;
	safe_unpack32 ( & msg -> uid , buffer ) ;
	if (unpack_job_credential( & msg -> credential ,  buffer ))
		goto unpack_error;
	safe_unpack32 ( & msg -> tasks_to_reattach , buffer ) ;
	if (slurm_unpack_slurm_addr_no_alloc ( & msg -> streams , buffer ))
		goto unpack_error;
	safe_unpack32_array ( & msg -> global_task_ids , & uint16_tmp , buffer ) ;
	return SLURM_SUCCESS ;

    unpack_error:
	if (msg -> global_task_ids)
		xfree (msg -> global_task_ids);
        xfree (msg);
        *msg_ptr = NULL;
        return SLURM_ERROR;
}

void pack_task_exit_msg ( task_exit_msg_t * msg , Buf buffer )
{
	pack32 ( msg -> task_id , buffer ) ;
	pack32 ( msg -> return_code , buffer ) ;
}

int unpack_task_exit_msg ( task_exit_msg_t ** msg_ptr , Buf buffer )
{
	task_exit_msg_t * msg ;

	msg = xmalloc ( sizeof ( task_exit_msg_t ) ) ;
	*msg_ptr = msg ;

	safe_unpack32 ( & msg -> task_id , buffer ) ;
	safe_unpack32 ( & msg -> return_code , buffer ) ;
	return SLURM_SUCCESS ;

    unpack_error:
        xfree (msg);
        *msg_ptr = NULL;
        return SLURM_ERROR;
}


void pack_launch_tasks_response_msg ( launch_tasks_response_msg_t * msg , Buf buffer )
{
	pack32  ( msg -> return_code , buffer ) ;
	packstr ( msg -> node_name , buffer ) ;
	pack32  ( msg -> srun_node_id , buffer ) ;
	pack32  ( msg -> local_pid , buffer ) ;
}

int unpack_launch_tasks_response_msg ( launch_tasks_response_msg_t ** msg_ptr , Buf buffer )
{
	uint16_t uint16_tmp;
	launch_tasks_response_msg_t * msg ;

	msg = xmalloc ( sizeof ( launch_tasks_response_msg_t ) ) ;
	*msg_ptr = msg ;

	safe_unpack32 ( & msg -> return_code , buffer ) ;
	safe_unpackstr_xmalloc ( & msg -> node_name , & uint16_tmp , buffer  ) ;
	safe_unpack32 ( & msg -> srun_node_id , buffer ) ;
	safe_unpack32 ( & msg -> local_pid , buffer ) ;
	return SLURM_SUCCESS ;

    unpack_error:
	if (msg -> node_name)
		xfree (msg -> node_name);
        xfree (msg);
        *msg_ptr = NULL;
        return SLURM_ERROR;
}

void pack_launch_tasks_request_msg ( launch_tasks_request_msg_t * msg , Buf buffer )
{
	pack32 ( msg -> job_id , buffer ) ;
	pack32 ( msg -> job_step_id , buffer ) ;
	pack32 ( msg -> nnodes, buffer ) ;
	pack32 ( msg -> nprocs, buffer ) ;
	pack32 ( msg -> uid , buffer ) ;
	pack32 ( msg -> srun_node_id , buffer ) ;
	pack_job_credential ( msg -> credential , buffer ) ;
	pack32 ( msg -> tasks_to_launch , buffer ) ;
	packstr_array ( msg -> env , msg -> envc , buffer ) ;
	packstr ( msg -> cwd , buffer ) ;
	packstr_array ( msg -> argv , msg -> argc , buffer ) ;
	pack16 (  msg -> resp_port , buffer ) ;
	pack16 (  msg -> io_port , buffer ) ;
	pack16 (  msg -> task_flags , buffer ) ;
	pack32_array ( msg -> global_task_ids , (uint16_t) msg -> tasks_to_launch , buffer ) ;
#ifdef HAVE_LIBELAN3
	qsw_pack_jobinfo( msg -> qsw_job , buffer ) ;
#endif
}

int unpack_launch_tasks_request_msg ( launch_tasks_request_msg_t ** msg_ptr , Buf buffer )
{
	uint16_t uint16_tmp;
	launch_tasks_request_msg_t * msg ;

	msg = xmalloc ( sizeof ( launch_tasks_request_msg_t ) ) ;
	*msg_ptr = msg ;

	safe_unpack32 ( & msg -> job_id , buffer ) ;
	safe_unpack32 ( & msg -> job_step_id , buffer ) ;
	safe_unpack32 ( & msg -> nnodes, buffer ) ;
	safe_unpack32 ( & msg -> nprocs, buffer ) ;
	safe_unpack32 ( & msg -> uid , buffer  ) ;
	safe_unpack32 ( & msg -> srun_node_id , buffer ) ;
	if (unpack_job_credential( & msg -> credential ,  buffer ))
		goto unpack_error;
	safe_unpack32 ( & msg -> tasks_to_launch , buffer ) ;
	safe_unpackstr_array ( & msg -> env , & msg -> envc , buffer ) ;
	safe_unpackstr_xmalloc ( & msg -> cwd , & uint16_tmp , buffer ) ;
	safe_unpackstr_array ( & msg -> argv , & msg->argc , buffer ) ;
	safe_unpack16 ( & msg -> resp_port , buffer  ) ;
	safe_unpack16 ( & msg -> io_port , buffer  ) ;
	safe_unpack16 ( & msg -> task_flags , buffer  ) ;
	safe_unpack32_array ( & msg -> global_task_ids , & uint16_tmp , buffer ) ;

#ifdef HAVE_LIBELAN3
	qsw_alloc_jobinfo(&msg->qsw_job);
	if (qsw_unpack_jobinfo(msg->qsw_job, buffer) < 0) {
		error("qsw_unpack_jobinfo: %m");
		goto unpack_error;
	}
#endif
	return SLURM_SUCCESS ;

    unpack_error:
	if (msg -> env)
		xfree (msg -> env);
	if (msg -> cwd)
		xfree (msg -> cwd);
	if (msg -> argv)
		xfree (msg -> argv);
	if (msg -> global_task_ids)
		xfree (msg -> global_task_ids);
        xfree (msg);
        *msg_ptr = NULL;
        return SLURM_ERROR;
}

void pack_cancel_tasks_msg ( kill_tasks_msg_t * msg , Buf buffer )
{
	pack32 ( msg -> job_id , buffer ) ;
	pack32 ( msg -> job_step_id , buffer ) ;
	pack32 ( msg -> signal , buffer ) ;
}

int unpack_cancel_tasks_msg ( kill_tasks_msg_t ** msg_ptr , Buf buffer )
{
	kill_tasks_msg_t * msg ;

	msg = xmalloc ( sizeof ( kill_tasks_msg_t ) ) ;
	*msg_ptr = msg ;

	safe_unpack32 ( & msg -> job_id , buffer ) ;
	safe_unpack32 ( & msg -> job_step_id , buffer ) ;
	safe_unpack32 ( & msg -> signal , buffer ) ;
	return SLURM_SUCCESS ;

    unpack_error:
        xfree (msg);
        *msg_ptr = NULL;
        return SLURM_ERROR ;
}

void pack_shutdown_msg ( shutdown_msg_t * msg , Buf buffer )
{
	pack16 ( msg -> core , buffer ) ;
}

int unpack_shutdown_msg ( shutdown_msg_t ** msg_ptr , Buf buffer )
{
	shutdown_msg_t * msg ;

	msg = xmalloc ( sizeof ( shutdown_msg_t ) ) ;
	*msg_ptr = msg ;

	safe_unpack16 ( & msg -> core , buffer ) ;
	return SLURM_SUCCESS ;

    unpack_error:
        xfree (msg);
        *msg_ptr = NULL;
        return SLURM_ERROR;
}

void pack_job_step_id ( job_step_id_t * msg , Buf buffer )
{
	pack_time ( msg -> last_update , buffer ) ;
	pack32 ( msg -> job_id , buffer ) ;
	pack32 ( msg -> job_step_id , buffer ) ;
}

int unpack_job_step_id ( job_step_id_t ** msg_ptr , Buf buffer )
{
	job_step_id_msg_t * msg ;

	msg = xmalloc ( sizeof ( job_step_id_msg_t ) ) ;
	*msg_ptr = msg ;

	safe_unpack_time ( & msg -> last_update , buffer ) ;
	safe_unpack32 ( & msg -> job_id , buffer ) ;
	safe_unpack32 ( & msg -> job_step_id , buffer ) ;
	return SLURM_SUCCESS ;

    unpack_error:
	xfree (msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}

void pack_get_job_step_info ( job_step_info_request_msg_t * msg , Buf buffer )
{
	pack_time ( msg -> last_update , buffer ) ;
	pack32 ( msg -> job_id , buffer ) ;
	pack32 ( msg -> step_id , buffer ) ;
}

int unpack_get_job_step_info ( job_step_info_request_msg_t ** msg , Buf buffer )
{
        job_step_info_request_msg_t * job_step_info ;

        job_step_info = xmalloc ( sizeof ( job_step_info_request_msg_t ) ) ;
	*msg = job_step_info;
 
	safe_unpack_time ( & job_step_info -> last_update , buffer ) ;
	safe_unpack32 ( & job_step_info -> job_id , buffer ) ;
	safe_unpack32 ( & job_step_info -> step_id , buffer ) ;
	return SLURM_SUCCESS ;

    unpack_error:
	xfree (job_step_info);
	*msg = NULL;
	return SLURM_ERROR;
}


void pack_slurm_addr_array ( slurm_addr * slurm_address , uint16_t size_val, Buf buffer )
{
	int i=0;
	uint16_t nl = htons(size_val);
	pack16( nl, buffer );

	for ( i=0; i < size_val; i++ ) 
	{
		slurm_pack_slurm_addr ( slurm_address + i , buffer ) ;
	}
	
}

int 
unpack_slurm_addr_array ( slurm_addr ** slurm_address , uint16_t * size_val , Buf buffer )
{
	int i=0;
	uint16_t nl ;

	*slurm_address = NULL;
	safe_unpack16( & nl , buffer );
	*size_val = ntohs ( nl ) ;
	*slurm_address = xmalloc( (*size_val) * sizeof( slurm_addr ) );

	for ( i=0; i < *size_val; i++ ) 
	{
		if (slurm_unpack_slurm_addr_no_alloc ( (*slurm_address) + i , buffer ))
			goto unpack_error;

	}
	return SLURM_SUCCESS;

    unpack_error:
	if (*slurm_address)
		xfree (*slurm_address);
	*slurm_address = NULL;
	return SLURM_ERROR;
}

void 
pack_batch_job_launch ( batch_job_launch_msg_t* msg , Buf buffer )
{
	assert ( msg != NULL );

	pack32 ( msg -> job_id, buffer ) ;
	pack32 ( msg -> user_id, buffer ) ;

	packstr ( msg -> nodes, buffer ) ;
	packstr ( msg -> script, buffer ) ;
	packstr ( msg -> work_dir, buffer ) ;

	packstr ( msg -> err, buffer ) ;
	packstr ( msg -> in, buffer ) ;
	packstr ( msg -> out, buffer ) ;

	pack16 ( msg -> argc, buffer ) ;
	packstr_array (msg -> argv, msg -> argc, buffer);

	pack16 ( msg -> env_size, buffer ) ;
	packstr_array (msg -> environment, msg -> env_size, buffer);
}

int 
unpack_batch_job_launch( batch_job_launch_msg_t** msg , Buf buffer )
{
	uint16_t uint16_tmp;
	batch_job_launch_msg_t *launch_msg_ptr ;

	assert ( msg != NULL );
	launch_msg_ptr = xmalloc ( sizeof (batch_job_launch_msg_t) ) ;
	*msg = launch_msg_ptr ;

	safe_unpack32 ( & launch_msg_ptr -> job_id, buffer ) ;
	safe_unpack32 ( & launch_msg_ptr -> user_id, buffer ) ;

	safe_unpackstr_xmalloc ( & launch_msg_ptr -> nodes, & uint16_tmp , buffer ) ;
	safe_unpackstr_xmalloc ( & launch_msg_ptr -> script, & uint16_tmp , buffer ) ;
	safe_unpackstr_xmalloc ( & launch_msg_ptr -> work_dir, & uint16_tmp , buffer ) ;

	safe_unpackstr_xmalloc ( & launch_msg_ptr -> err, & uint16_tmp , buffer ) ;
	safe_unpackstr_xmalloc ( & launch_msg_ptr -> in, & uint16_tmp , buffer ) ;
	safe_unpackstr_xmalloc ( & launch_msg_ptr -> out, & uint16_tmp , buffer ) ;

	safe_unpack16 ( & launch_msg_ptr -> argc, buffer ) ;
	safe_unpackstr_array (& launch_msg_ptr -> argv, &launch_msg_ptr -> argc, buffer);

	safe_unpack16 ( & launch_msg_ptr -> env_size, buffer ) ;
	safe_unpackstr_array (& launch_msg_ptr -> environment, &launch_msg_ptr -> env_size, buffer);

	return SLURM_SUCCESS;

    unpack_error:
	if (launch_msg_ptr -> nodes)
		xfree (launch_msg_ptr -> nodes);
	if (launch_msg_ptr -> script)
		xfree (launch_msg_ptr -> script);
	if (launch_msg_ptr -> work_dir)
		xfree (launch_msg_ptr -> work_dir);
	if (launch_msg_ptr -> err)
		xfree (launch_msg_ptr -> err);
	if (launch_msg_ptr -> in)
		xfree (launch_msg_ptr -> in);
	if (launch_msg_ptr -> out)
		xfree (launch_msg_ptr -> out);
	if (launch_msg_ptr -> argv)
		xfree (launch_msg_ptr -> argv);
	if (launch_msg_ptr -> environment)
		xfree (launch_msg_ptr -> environment);
	xfree (launch_msg_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

/* template 
void pack_ ( * msg , Buf buffer )
{
	assert ( msg != NULL );

	pack16 ( msg -> , buffer ) ;
	pack32 ( msg -> , buffer ) ;
	packstr ( msg -> , buffer ) ;
}

int unpack_ ( ** msg_ptr , Buf buffer )
{
	uint16_t uint16_tmp;
	* msg ;

	assert ( msg_ptr != NULL );

	msg = xmalloc ( sizeof ( ) ) ;
	*msg_ptr = msg;

	safe_unpack16 ( & msg -> , buffer ) ;
	safe_unpack32 ( & msg -> , buffer ) ;
	safe_unpackstr_xmalloc ( & msg -> x, & uint16_tmp , buffer ) ;
	return SLURM_SUCCESS;

    unpack_error:
	if (msg -> x)
		xfree (msg -> x);
	xfree (msg);
	*msg_ptr = NULL;
	return SLURM_ERROR;
}
*/
