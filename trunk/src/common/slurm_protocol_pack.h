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

#include <src/common/slurm_protocol_defs.h>

/* Pack / Unpack methods for slurm protocol header */
void pack_header ( header_t  * header , char ** buffer , uint32_t * length );
void unpack_header ( header_t * header , char ** buffer , uint32_t * length );

/* Pack / Unpack methods for slurm io pipe streams header */
void pack_io_stream_header ( slurm_io_stream_header_t * msg , void ** buffer , uint32_t * length ) ;
void unpack_io_stream_header ( slurm_io_stream_header_t * msg , void ** buffer , uint32_t * length ) ;

/* generic case statement Pack / Unpack methods for slurm protocol bodies */
int pack_msg ( slurm_msg_t const * msg , char ** buffer , uint32_t * buf_len );
int unpack_msg ( slurm_msg_t * msgi , char ** buffer , uint32_t * buf_len );

/* specific Pack / Unpack methods for slurm protocol bodies */
void pack_node_registration_status_msg ( slurm_node_registration_status_msg_t * msg , void ** buffer , uint32_t * length );

int unpack_node_registration_status_msg ( slurm_node_registration_status_msg_t ** msg ,  void ** buffer , uint32_t * length );

void pack_job_desc ( job_desc_msg_t *job_desc_msg_ptr, void ** buffer , int * buf_len );
int unpack_job_desc ( job_desc_msg_t **job_desc_msg_ptr, void ** buffer , int * buffer_size );

void pack_last_update ( last_update_msg_t * msg , void ** buffer , uint32_t * length );
int unpack_last_update ( last_update_msg_t ** msg , void ** buffer , uint32_t * length );

void pack_job_step_create_request_msg ( job_step_create_request_msg_t* msg , void ** buffer , uint32_t * length );
int unpack_job_step_create_request_msg ( job_step_create_request_msg_t** msg , void ** buffer , uint32_t * length );

void pack_job_step_create_response_msg (  job_step_create_response_msg_t* msg , void ** buffer , uint32_t * length );
int unpack_job_step_create_response_msg (job_step_create_response_msg_t** msg , void ** buffer , uint32_t * length );

void pack_return_code ( return_code_msg_t * msg , void ** buffer , uint32_t * length );
int unpack_return_code ( return_code_msg_t ** msg , void ** buffer , uint32_t * length );

void pack_slurm_ctl_conf ( slurm_ctl_conf_info_msg_t * build_ptr, void ** buffer , int * buffer_size );
int unpack_slurm_ctl_conf ( slurm_ctl_conf_info_msg_t **build_buffer_ptr, void ** buffer , int * buffer_size );

void pack_job_info_msg ( slurm_msg_t * msg , void ** buffer , int * buffer_size );
int unpack_job_info_msg ( job_info_msg_t ** msg , void ** buffer , int * buffer_size );
int unpack_job_table_msg ( job_table_t ** job , void ** buf_ptr , int * buffer_size );
int unpack_job_table ( job_table_t * job , void ** buf_ptr , int * buffer_size );

void pack_partition_info_msg ( slurm_msg_t * msg, void ** buf_ptr , int * buffer_size );
int unpack_partition_info_msg ( partition_info_msg_t ** , void ** buffer , int * buffer_size );
int unpack_partition_table_msg ( partition_table_msg_t ** part , void ** buf_ptr , int * buffer_size );
int unpack_partition_table ( partition_table_msg_t * part , void ** buf_ptr , int * buffer_size );

void pack_node_info_msg ( slurm_msg_t * msg, void ** buf_ptr , int * buffer_size );
int unpack_node_info_msg ( node_info_msg_t ** msg , void ** buf_ptr , int * buffer_size );
int unpack_node_table_msg ( node_table_msg_t ** node , void ** buf_ptr , int * buffer_size );
int unpack_node_table ( node_table_msg_t * node , void ** buf_ptr , int * buffer_size );

void pack_cancel_job_msg ( job_id_msg_t * msg , void ** buffer , uint32_t * length );
int unpack_cancel_job_msg ( job_id_msg_t ** msg_ptr , void ** buffer , uint32_t * length );

void pack_cancel_job_step_msg ( job_step_id_msg_t * msg , void ** buffer , uint32_t * length );
int unpack_cancel_job_step_msg ( job_step_id_msg_t ** msg_ptr , void ** buffer , uint32_t * length );

void pack_cancel_tasks_msg ( kill_tasks_msg_t * msg , void ** buffer , uint32_t * length );
int unpack_cancel_tasks_msg ( kill_tasks_msg_t ** msg_ptr , void ** buffer , uint32_t * length );

void pack_resource_allocation_response_msg ( resource_allocation_response_msg_t * msg, void ** buffer , int * length );
int unpack_resource_allocation_response_msg ( resource_allocation_response_msg_t ** msg , void ** buffer , int * length );

void pack_submit_response_msg ( submit_response_msg_t * msg, void ** buffer , int * length );
int unpack_submit_response_msg ( submit_response_msg_t ** msg , void ** buffer , int * length );

void pack_update_node_msg ( update_node_msg_t * msg, void ** buffer , uint32_t * length );
int unpack_update_node_msg ( update_node_msg_t ** msg , void ** buffer , uint32_t * length );

void pack_partition_table_msg ( partition_desc_msg_t *  msg , void ** buffer , int * buf_len );
int unpack_partition_table_msg ( partition_desc_msg_t **  msg_ptr , void ** buffer , int *  buf_len );

void pack_update_partition_msg ( update_part_msg_t * msg , void ** buffer, uint32_t * length  );
int unpack_update_partition_msg ( update_part_msg_t ** msg_ptr , void ** buffer, uint32_t * length  );

void pack_launch_tasks_request_msg ( launch_tasks_request_msg_t * msg , void ** buffer , uint32_t * length );
int unpack_launch_tasks_request_msg ( launch_tasks_request_msg_t ** msg_ptr , void ** buffer , uint32_t * length );

void pack_launch_tasks_response_msg ( launch_tasks_response_msg_t * msg , void ** buffer , uint32_t * length );
int unpack_launch_tasks_response_msg ( launch_tasks_response_msg_t ** msg_ptr , void ** buffer , uint32_t * length );

void pack_kill_tasks_msg ( kill_tasks_msg_t * msg , void ** buffer , uint32_t * length );
int unpack_kill_tasks_msg ( kill_tasks_msg_t ** msg_ptr , void ** buffer , uint32_t * length );

void pack_slurm_addr_array ( slurm_addr * slurm_address , uint16_t size_val, void ** buffer , int * length );
void unpack_slurm_addr_array ( slurm_addr ** slurm_address , uint16_t * size_val , void ** buffer , int * length );

void pack_reattach_tasks_streams_msg ( reattach_tasks_streams_msg_t * msg , void ** buffer , uint32_t * length ) ;
int unpack_reattach_tasks_streams_msg ( reattach_tasks_streams_msg_t ** msg_ptr , void ** buffer , uint32_t * length ) ;

void pack_revoke_credential_msg ( revoke_credential_msg_t* msg , void ** buffer , uint32_t * length ) ;
int unpack_revoke_credential_msg ( revoke_credential_msg_t** msg , void ** buffer , uint32_t * length ) ;
#endif
