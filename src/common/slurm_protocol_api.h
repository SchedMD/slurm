#ifndef _SLURM_PROTOCOL_API_H
#define _SLURM_PROTOCOL_API_H

#include <sys/time.h>
#include <stdint.h>
#include <src/common/slurm_protocol_common.h>
#include <src/common/slurm_protocol_util.h>
#include <src/common/slurm_protocol_defs.h>

/* high level routines */
/* message functions */
slurm_fd slurm_init_message_engine ( slurm_addr * slurm_address ) ;
uint32_t slurm_receive_message ( slurm_fd open_fd , slurm_addr * source_address , slurm_message_t ** message ) ; 
uint32_t slurm_shutdown_message_engine ( slurm_fd open_fd ) ;

/* send message functions */

/* stream functions */
slurm_fd slurm_listen_stream ( slurm_addr * slurm_address ) ;
slurm_fd slurm_accept_stream ( slurm_fd open_fd , slurm_addr * slurm_address ) ;
slurm_fd slurm_open_stream ( slurm_addr * slurm_address )	;
size_t slurm_write_stream ( slurm_fd open_fd , char * buffer , size_t size ) ;
size_t slurm_read_stream ( slurm_fd open_fd , char * buffer , size_t size ) ;
uint32_t slurm_close_stream ( slurm_fd open_fd ) ;
	


/* Low level routines */
/* message functions */

uint32_t slurm_receive_buffer ( slurm_fd open_fd , slurm_addr * source_address , slurm_message_type_t * message_type , char * data_buffer , size_t buf_len ) ;
uint32_t slurm_send_server_buffer ( slurm_fd open_fd , slurm_message_type_t message_type , char * data_buffer , size_t buf_len ) ;
uint32_t slurm_send_node_buffer ( slurm_fd open_fd , slurm_addr * destination_address , slurm_message_type_t message_type , char * data_buffer , size_t buf_len ) ;

uint32_t slurm_send_server_message ( slurm_fd open_fd , slurm_message_type_t message_type , slurm_message_t const * message ) ;
uint32_t slurm_send_node_message ( slurm_fd open_fd , slurm_addr * slurm_address , slurm_message_type_t message_type , slurm_message_t const * message ) ;
#endif
