#ifndef _SLURM_PROTOCOL_COMMON_H
#define _SLURM_PROTOCOL_COMMON_H

#include <src/common/slurm_protocol_errno.h>
/* for sendto and recvfrom commands */
#define SLURM_PROTOCOL_NO_SEND_RECV_FLAGS 0
/* for accpet commands */
#define SLURM_PROTOCOL_DEFAULT_LISTEN_BACKLOG 10
/* used in interface methods */
#define SLURM_PROTOCOL_FUNCTION_NOT_IMPLEMENTED -2 
/* max slurm message send and receive buffer size
this may need to be increased to 350k-512k */
#define SLURM_PROTOCOL_MAX_MESSAGE_BUFFER_SIZE 4096
/* slurm protocol header defines */ 
#define SLURM_PROTOCOL_VERSION 1
#define SLURM_PROTOCOL_NO_FLAGS 0 /* used in the header to set flags to empty */

typedef uint16_t slurm_msg_type_t ;

#if MONG_IMPLEMENTATION
#  include <src/common/slurm_protocol_mongo_common.h>
#else
#  include <src/common/slurm_protocol_socket_common.h>
#endif

#endif
