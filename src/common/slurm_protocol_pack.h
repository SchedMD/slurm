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
void pack_header ( header_t  * header , char ** buffer , uint32_t * length ) ;

void unpack_header ( header_t * header , char ** buffer , uint32_t * length ) ;

/* generic case statement Pack / Unpack methods for slurm protocol bodies */
int pack_msg ( slurm_msg_t const * msg , char ** buffer , uint32_t * buf_len ) ;

int unpack_msg ( slurm_msg_t * msgi , char ** buffer , uint32_t * buf_len ) ;

/* specific Pack / Unpack methods for slurm protocol bodies */
void pack_node_registration_status_msg ( node_registration_status_msg_t * msg , char ** buffer , uint32_t * length ) ;

int unpack_node_registration_status_msg (node_registration_status_msg_t ** msg ,  char ** buffer , uint32_t * length ) ;

void pack_job_desc ( job_desc_msg_t *job_desc_msg_ptr, void ** buffer , int * buf_len ) ;
int unpack_job_desc ( job_desc_msg_t **job_desc_msg_ptr, void ** buffer , int * buffer_size ) ;

void pack_last_update ( last_update_msg_t * msg , void ** buffer , uint32_t * length ) ;
int unpack_last_update ( last_update_msg_t ** msg , void ** buffer , uint32_t * length ) ;

void pack_build_info ( build_info_msg_t * build_ptr, void ** buffer , int * buffer_size ) ;
int unpack_build_info ( build_info_msg_t **build_buffer_ptr, void ** buffer , int * buffer_size ) ;

void pack_job_info_msg ( slurm_msg_t * msg , void ** buffer , int * buffer_size ) ;
int unpack_job_info_msg ( job_info_msg_t ** msg , void ** buffer , int * buffer_size ) ;
int unpack_job_table ( job_table_t * job , void ** buf_ptr , int * buffer_size ) ;

#endif
