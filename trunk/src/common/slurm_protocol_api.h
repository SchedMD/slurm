#ifndef _SLURM_PROTOCOL_API_H
#define _SLURM_PROTOCOL_API_H

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

#include <src/common/slurm_protocol_common.h>
#include <src/common/slurm_protocol_util.h>
#include <src/common/slurm_protocol_defs.h>

//WHAT ABOUT THESE INCLUDES
#include <sys/time.h>

/* high level routines */
/* msg functions */
slurm_fd slurm_init_msg_engine ( slurm_addr * slurm_address ) ;
uint32_t slurm_receive_msg ( slurm_fd open_fd , slurm_addr * source_address , slurm_msg_t ** msg ) ; 
uint32_t slurm_shutdown_msg_engine ( slurm_fd open_fd ) ;

/* send msg functions */

/* stream functions */
slurm_fd slurm_listen_stream ( slurm_addr * slurm_address ) ;
slurm_fd slurm_accept_stream ( slurm_fd open_fd , slurm_addr * slurm_address ) ;
slurm_fd slurm_open_stream ( slurm_addr * slurm_address )	;
size_t slurm_write_stream ( slurm_fd open_fd , char * buffer , size_t size ) ;
size_t slurm_read_stream ( slurm_fd open_fd , char * buffer , size_t size ) ;
uint32_t slurm_close_stream ( slurm_fd open_fd ) ;
	


/* Low level routines */
/* msg functions */

uint32_t slurm_receive_buffer ( slurm_fd open_fd , slurm_addr * source_address , slurm_msg_type_t * msg_type , char * data_buffer , size_t buf_len ) ;
uint32_t slurm_send_controller_buffer ( slurm_fd open_fd , slurm_msg_type_t msg_type , char * data_buffer , size_t buf_len ) ;
uint32_t slurm_send_node_buffer ( slurm_fd open_fd , slurm_addr * destination_address , slurm_msg_type_t msg_type , char * data_buffer , size_t buf_len ) ;

uint32_t slurm_send_controller_msg ( slurm_fd open_fd , slurm_msg_type_t msg_type , slurm_msg_t const * msg ) ;
uint32_t slurm_send_node_msg ( slurm_fd open_fd , slurm_addr * slurm_address , slurm_msg_type_t msg_type , slurm_msg_t const * msg ) ;

void slurm_set_addr_uint ( slurm_addr * slurm_address , uint16_t port , uint32_t ip_address ) ;
void slurm_set_addr ( slurm_addr * slurm_address , uint16_t port , char * host ) ;
void slurm_set_addr_char ( slurm_addr * slurm_address , uint16_t port , char * host ) ;
void slurm_get_addr ( slurm_addr * slurm_address , uint16_t * port , char * host , uint32_t buf_len ) ;
#endif
