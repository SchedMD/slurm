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
void pack_header ( char ** buffer , uint32_t * length , header_t * header ) ;

void unpack_header ( char ** buffer , uint32_t * length , header_t * header ) ;

/* generic case statement Pack / Unpack methods for slurm protocol bodies */
void pack_msg ( char ** buffer , uint32_t * buf_len , slurm_msg_t const * msg ) ;

void unpack_msg ( char ** buffer , uint32_t * buf_len , slurm_msg_t * msg ) ;

/* specific Pack / Unpack methods for slurm protocol bodies */
void pack_node_registration_status_msg ( char ** buffer , uint32_t * length , node_registration_status_msg_t * msg ) ;

void unpack_node_registration_status_msg ( char ** buffer , uint32_t * length , node_registration_status_msg_t * messge ) ;

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
