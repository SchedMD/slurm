/****************************************************************************\
 *  slurm_protocol_pack.h - definitions for all pack and unpack functions
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

#ifndef _SLURM_PROTOCOL_PACK_H
#define _SLURM_PROTOCOL_PACK_H

#if HAVE_CONFIG_H
#  include <config.h>
#  if HAVE_INTTYPES_H
#    include <inttypes.h>
#  else
#    if HAVE_STDINT_H
#      include <stdint.h>
#    endif
#  endif  /* HAVE_INTTYPES_H */
#else   /* !HAVE_CONFIG_H */
#  include <inttypes.h>
#endif  /*  HAVE_CONFIG_H */

#include <src/common/pack.h>
#include <src/common/slurm_protocol_defs.h>

/* Pack / Unpack methods for slurm protocol header */
void pack_header ( header_t  * header , Buf buffer );
int  unpack_header ( header_t * header , Buf buffer );

/* Pack / Unpack methods for slurm io pipe streams header */
int size_io_stream_header (void);
void pack_io_stream_header ( slurm_io_stream_header_t * msg , Buf buffer ) ;
int unpack_io_stream_header ( slurm_io_stream_header_t * msg , Buf buffer ) ;

/* generic case statement Pack / Unpack methods for slurm protocol bodies */
int pack_msg ( slurm_msg_t const * msg , Buf buffer );
int unpack_msg ( slurm_msg_t * msgi , Buf buffer );

/* specific Pack / Unpack methods for slurm protocol bodies */
void pack_node_registration_status_msg ( slurm_node_registration_status_msg_t * msg , Buf buffer );

int unpack_node_registration_status_msg ( slurm_node_registration_status_msg_t ** msg ,  Buf buffer );

void pack_job_desc ( job_desc_msg_t *job_desc_msg_ptr, Buf buffer );
int unpack_job_desc ( job_desc_msg_t **job_desc_msg_ptr, Buf buffer );

void pack_old_job_desc ( old_job_alloc_msg_t * job_desc_ptr, Buf buffer );
int unpack_old_job_desc ( old_job_alloc_msg_t **job_desc_buffer_ptr, Buf buffer );

void pack_last_update ( last_update_msg_t * msg , Buf buffer );
int unpack_last_update ( last_update_msg_t ** msg , Buf buffer );

void pack_job_step_create_request_msg ( job_step_create_request_msg_t* msg , Buf buffer );
int unpack_job_step_create_request_msg ( job_step_create_request_msg_t** msg , Buf buffer );

void pack_job_step_create_response_msg (  job_step_create_response_msg_t* msg , Buf buffer );
int unpack_job_step_create_response_msg (job_step_create_response_msg_t** msg , Buf buffer );

void pack_return_code ( return_code_msg_t * msg , Buf buffer );
int unpack_return_code ( return_code_msg_t ** msg , Buf buffer );

void pack_slurm_ctl_conf ( slurm_ctl_conf_info_msg_t * build_ptr, Buf buffer );
int unpack_slurm_ctl_conf ( slurm_ctl_conf_info_msg_t **build_buffer_ptr, Buf buffer );

void pack_buffer_msg ( slurm_msg_t * msg , Buf buffer );

#define pack_job_info_msg(msg,buf)	pack_buffer_msg(msg,buf)
#define pack_job_step_info_msg(msg,buf)	pack_buffer_msg(msg,buf)

int unpack_job_info_msg ( job_info_msg_t ** msg , Buf buffer );
int unpack_job_info ( job_info_t ** job , Buf buffer );

int unpack_job_info_members ( job_info_t * job , Buf buffer );

/* job_step_info messages 
 * the pack_job_step_info_members is to be used programs such as slurmctld which do
 * not use the protocol structures internally.
 */
void pack_job_step_info ( job_step_info_t* step, Buf buffer );
void pack_job_step_info_members(   uint32_t job_id, uint16_t step_id, 
		uint32_t user_id, time_t start_time, char *partition, char *nodes,
		Buf buffer );
int unpack_job_step_info ( job_step_info_t ** step , Buf buffer );


void pack_partition_info_msg ( slurm_msg_t * msg, Buf buffer );
int unpack_partition_info_msg ( partition_info_msg_t ** , Buf buffer );
int unpack_partition_info ( partition_info_t ** part , Buf buffer );
int unpack_partition_info_members ( partition_info_t * part , Buf buffer );

void pack_node_info_msg ( slurm_msg_t * msg, Buf buffer );
int unpack_node_info_msg ( node_info_msg_t ** msg , Buf buffer );
int unpack_node_info ( node_info_t ** node , Buf buffer );
int unpack_node_info_members ( node_info_t * node , Buf buffer );

void pack_cancel_job_msg ( job_id_msg_t * msg , Buf buffer );
int unpack_cancel_job_msg ( job_id_msg_t ** msg_ptr , Buf buffer );

/* job_step_id functions */
void pack_job_step_id ( job_step_id_t * msg , Buf buffer );
int unpack_job_step_id ( job_step_id_t ** msg_ptr , Buf buffer );

/* job_step_id Macros for typedefs */
#define pack_cancel_job_step_msg(msg,buffer)		pack_job_step_id(msg,buffer)
#define unpack_cancel_job_step_msg(msg_ptr,buffer)	unpack_job_step_id(msg_ptr,buffer)
#define pack_job_step_id_msg(msg,buffer)		pack_job_step_id(msg,buffer)
#define unpack_job_step_id_msg(msg_ptr,buffer)		unpack_job_step_id(msg_ptr,buffer)
#define pack_job_step_info_request_msg(msg,buffer)	pack_job_step_id(msg,buffer)
#define unpack_job_step_info_request_msg(msg_ptr,buffer)unpack_job_step_id(msg_ptr,buffer)
#define pack_job_info_request_msg(msg,buffer)		pack_job_step_id(msg,buffer)
#define unpack_job_info_request_msg(msg_ptr,buffer)	unpack_job_step_id(msg_ptr,buffer)

int unpack_job_step_info ( job_step_info_t ** step , Buf buffer );
int unpack_job_step_info_members ( job_step_info_t * step , Buf buffer );
int unpack_job_step_info_response_msg ( job_step_info_response_msg_t** msg, Buf buffer );

void pack_cancel_tasks_msg ( kill_tasks_msg_t * msg , Buf buffer );
int unpack_cancel_tasks_msg ( kill_tasks_msg_t ** msg_ptr , Buf buffer );

void pack_resource_allocation_response_msg ( resource_allocation_response_msg_t * msg, Buf buffer );
int unpack_resource_allocation_response_msg ( resource_allocation_response_msg_t ** msg , Buf buffer );

void pack_resource_allocation_and_run_response_msg ( resource_allocation_and_run_response_msg_t * msg, Buf buffer );
int unpack_resource_allocation_and_run_response_msg ( resource_allocation_and_run_response_msg_t ** msg , Buf buffer );

void pack_submit_response_msg ( submit_response_msg_t * msg, Buf buffer );
int unpack_submit_response_msg ( submit_response_msg_t ** msg , Buf buffer );

void pack_update_node_msg ( update_node_msg_t * msg, Buf buffer );
int unpack_update_node_msg ( update_node_msg_t ** msg , Buf buffer );

void pack_partition_table_msg ( partition_desc_msg_t *  msg , Buf buffer );
int unpack_partition_table_msg ( partition_desc_msg_t **  msg_ptr , Buf buffer );

void pack_update_partition_msg ( update_part_msg_t * msg , Buf buffer );
int unpack_update_partition_msg ( update_part_msg_t ** msg_ptr , Buf buffer );

void pack_shutdown_msg ( shutdown_msg_t * msg , Buf buffer );
int unpack_shutdown_msg ( shutdown_msg_t ** msg_ptr , Buf buffer );

void pack_launch_tasks_request_msg ( launch_tasks_request_msg_t * msg , Buf buffer );
int unpack_launch_tasks_request_msg ( launch_tasks_request_msg_t ** msg_ptr , Buf buffer );

void pack_launch_tasks_response_msg ( launch_tasks_response_msg_t * msg , Buf buffer );
int unpack_launch_tasks_response_msg ( launch_tasks_response_msg_t ** msg_ptr , Buf buffer );

void pack_kill_tasks_msg ( kill_tasks_msg_t * msg , Buf buffer );
int unpack_kill_tasks_msg ( kill_tasks_msg_t ** msg_ptr , Buf buffer );

void pack_slurm_addr_array ( slurm_addr * slurm_address , uint16_t size_val, Buf buffer );
int  unpack_slurm_addr_array ( slurm_addr ** slurm_address , uint16_t * size_val , Buf buffer );


extern void pack_get_job_step_info ( job_step_info_request_msg_t * msg , Buf buffer );
extern int unpack_get_job_step_info ( job_step_info_request_msg_t ** msg , Buf buffer );

void pack_reattach_tasks_streams_msg ( reattach_tasks_streams_msg_t * msg , Buf buffer ) ;
int unpack_reattach_tasks_streams_msg ( reattach_tasks_streams_msg_t ** msg_ptr , Buf buffer ) ;

void pack_revoke_credential_msg ( revoke_credential_msg_t* msg , Buf buffer ) ;
int unpack_revoke_credential_msg ( revoke_credential_msg_t** msg , Buf buffer ) ;

void pack_task_exit_msg ( task_exit_msg_t * msg , Buf buffer ) ;
int unpack_task_exit_msg ( task_exit_msg_t ** msg_ptr , Buf buffer ) ;

void pack_job_credential ( slurm_job_credential_t* cred , Buf buffer ) ;
int unpack_job_credential( slurm_job_credential_t** msg , Buf buffer ) ;

void pack_batch_job_launch ( batch_job_launch_msg_t* cred , Buf buffer ) ;
int  unpack_batch_job_launch( batch_job_launch_msg_t** msg , Buf buffer ) ;

#endif
