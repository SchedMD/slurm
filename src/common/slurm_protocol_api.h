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

#include <sys/types.h>

#include <src/common/slurm_protocol_common.h>
#include <src/common/slurm_protocol_util.h>
#include <src/common/slurm_protocol_defs.h>
#include <src/common/slurm_protocol_errno.h>

#define SLURM_PORT 7000
/*#define PRIMARY_SLURM_CONTROLLER	"pri_slrumctld.llnl.gov" */
/*#define SECONDARY_SLURM_CONTROLLER	"sec_slrumctld.llnl.gov" */
#define PRIMARY_SLURM_CONTROLLER	"localhost" 
#define SECONDARY_SLURM_CONTROLLER	"localhost" 
#define REQUEST_BUFFER_SIZE 64
#define RESPONSE_BUFFER_SIZE 1024

//WHAT ABOUT THESE INCLUDES

/* high level routines */
/* msg functions */
slurm_fd inline slurm_init_msg_engine_port ( uint16_t port ) ;
slurm_fd inline slurm_init_msg_engine ( slurm_addr * slurm_address ) ;
slurm_fd inline slurm_accept_msg_conn ( slurm_fd open_fd , slurm_addr * slurm_address ) ;
int inline slurm_close_accepted_conn ( slurm_fd open_fd ) ;
int inline slurm_shutdown_msg_engine ( slurm_fd open_fd ) ;

int slurm_receive_msg ( slurm_fd open_fd , slurm_msg_t * msg ) ; 
int slurm_send_controller_msg ( slurm_fd open_fd , slurm_msg_t * msg ) ;
int slurm_send_node_msg ( slurm_fd open_fd , slurm_msg_t * msg ) ;

slurm_fd inline slurm_open_controller_conn ( ) ;
slurm_fd inline slurm_open_msg_conn ( slurm_addr * slurm_address ) ;
int inline slurm_shutdown_msg_conn ( slurm_fd open_fd ) ;


/* send msg functions */

/* stream functions */
slurm_fd inline slurm_listen_stream ( slurm_addr * slurm_address ) ;
slurm_fd inline slurm_accept_stream ( slurm_fd open_fd , slurm_addr * slurm_address ) ;
slurm_fd inline slurm_open_stream ( slurm_addr * slurm_address ) ;

size_t inline slurm_write_stream ( slurm_fd open_fd , char * buffer , size_t size ) ;
size_t inline slurm_read_stream ( slurm_fd open_fd , char * buffer , size_t size ) ;
int inline slurm_close_stream ( slurm_fd open_fd ) ;
	

/* Low level routines */
/* msg functions */

int slurm_receive_buffer ( slurm_fd open_fd , slurm_addr * source_address , slurm_msg_type_t * msg_type , char * data_buffer , size_t buf_len ) ;
int slurm_send_controller_buffer ( slurm_fd open_fd , slurm_msg_type_t msg_type , char * data_buffer , size_t buf_len ) ;
int slurm_send_node_buffer ( slurm_fd open_fd , slurm_addr * destination_address , slurm_msg_type_t msg_type , char * data_buffer , size_t buf_len ) ;


/* Address Conversion Functions */

void inline slurm_set_addr_uint ( slurm_addr * slurm_address , uint16_t port , uint32_t ip_address ) ;
void inline slurm_set_addr ( slurm_addr * slurm_address , uint16_t port , char * host ) ;
void inline slurm_set_addr_any ( slurm_addr * slurm_address , uint16_t port ) ;
void inline slurm_set_addr_char ( slurm_addr * slurm_address , uint16_t port , char * host ) ;
void inline slurm_get_addr ( slurm_addr * slurm_address , uint16_t * port , char * host , uint32_t buf_len ) ;

/* Slurm message functions */
void slurm_free_msg ( slurm_msg_t * msg ) ;

/*******************************************/
/***** slurm send highlevel msg  functions */
/*******************************************/
void slurm_send_rc_msg ( slurm_msg_t * request_msg , int rc );
#endif
