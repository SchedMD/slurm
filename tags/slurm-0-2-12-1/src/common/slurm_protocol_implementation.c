#ifndef _SLURM_PROTOCOL_IMPLEMENTATION_C
#define _SLURM_PROTOCOL_IMPLEMENTATION_C

#if HAVE_CONFIG_H
#  include "config.h"
#endif  /*  HAVE_CONFIG_H */

/* Just pick the apprpriate communications definitions */

#if MONGO_IMPLEMENTATION
#  include "src/common/slurm_protocol_mongo_implementation.h"
#else
#  include "src/common/slurm_protocol_socket_implementation.h"
#endif

#endif
