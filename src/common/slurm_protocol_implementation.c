#ifndef _SLURM_PROTOCOL_IMPLEMENTATION_C
#define _SLURM_PROTOCOL_IMPLEMENTATION_C_

#if MONG_IMPLEMENTATION
#  include <src/common/slurm_protocol_mongo_implementation.h>
#else
#  include <src/common/slurm_protocol_socket_implementation.h>
#endif

#endif
