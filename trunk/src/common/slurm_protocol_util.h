#ifndef _SLURM_PROTOCOL_UTIL_H

#include <src/common/slurm_protocol_defs.h>
#include <src/common/slurm_protocol_common.h>
#include <stdint.h>
uint32_t check_header_version( header_t * header) ;
void init_header ( header_t * header , slurm_message_type_t message_type , uint16_t flags ) ;
void set_slurm_addr_hton ( slurm_addr * slurm_address , uint16_t port , uint32_t ip_address ) ;
#endif
