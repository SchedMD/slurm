#ifndef _SLURM_PROTOCOL_UTIL_H
#define _SLURM_PROTOCOL_UTIL_H

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
#include <src/common/slurm_protocol_common.h>
#define SLURM_SSL_SIGNATURE_LENGTH 16
#define SLURM_IO_STREAM_INOUT 0
#define SLURM_IO_STREAM_SIGERR 1

uint32_t check_header_version( header_t * header) ;
void init_header ( header_t * header , slurm_msg_type_t msg_type , uint16_t flags ) ;

uint32_t check_io_stream_header_version( slurm_io_stream_header_t * header) ;
void init_io_stream_header ( slurm_io_stream_header_t * header , char * key , uint32_t task_id , uint16_t type ) ;
#endif
