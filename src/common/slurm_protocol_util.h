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

uint32_t check_header_version( header_t * header) ;
void init_header ( header_t * header , slurm_msg_type_t msg_type , uint16_t flags ) ;
void set_slurm_addr_hton ( slurm_addr * slurm_address , uint16_t port , uint32_t ip_address ) ;
#endif
