#ifndef _SLURM_PROTOCOL_PACK_H
#define _SLURM_PROTOCOL_PACK_H
#include <stdint.h>
#include <src/common/slurm_protocol_defs.h>
#include <linux/types.h>

/* Pack / Unpack methods for slurm protocol header */
void pack_header ( char ** buffer , uint32_t * length , header_t * header ) ;

void unpack_header ( char ** buffer , uint32_t * length , header_t * header ) ;

/* generic case statement Pack / Unpack methods for slurm protocol bodies */
void pack_message ( char ** buffer , uint32_t * buf_len , slurm_message_t const * message ) ;

void unpack_message ( char ** buffer , uint32_t * buf_len , slurm_message_t * message ) ;

/* specific Pack / Unpack methods for slurm protocol bodies */
void pack_node_registration_status_message ( char ** buffer , uint32_t * length , node_registration_status_message_t * message ) ;

void unpack_node_registration_status_message ( char ** buffer , uint32_t * length , node_registration_status_message_t * messge ) ;

/* template 
void pack_ ( char ** buffer , uint32_t * length , * message )
{
	pack16 ( message -> , buffer , length ) ;
	pack32 ( message -> , buffer , length ) ;
}

void unpack_ ( char ** buffer , uint32_t * length , * messge )
{
	unpack16 ( & message -> , buffer , length ) ;
	unpack32 ( & message -> , buffer , length ) ;
}
*/
#endif
