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

void unpack_node_registration_status_msg (node_registration_status_msg_t * msg ,  char ** buffer , uint32_t * length ) ;

void pack_job_desc ( job_desc_t *job_desc_ptr, void ** buffer , int * buf_len ) ;
void unpack_job_desc ( job_desc_t **job_desc_buffer_ptr, void ** buffer , int * buffer_size ) ;
	/* template 
	   void pack_ ( char ** buffer , uint32_t * length , * msg )
	   {
	   pack16 ( msg -> , buffer , length ) ;
	   pack32 ( msg -> , buffer , length ) ;
	   }

	   void unpack_ ( char ** buffer , uint32_t * length , * messge )
	   {
	   unpack16 ( & msg -> , buffer , length ) ;
	   unpack32 ( & msg -> , buffer , length ) ;
	   }
	 */
#endif
