#include <src/common/slurm_protocol_defs.h>
#include <src/common/slurm_protocol_common.h>
#include <src/common/slurm_protocol_util.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <string.h>


/* checks to see that the specified header was sent from a node running the same version of the protocol as the current node */
uint32_t check_header_version( header_t * header)
{
	if ( header -> version != SLURM_PROTOCOL_VERSION )
	{
		debug ( "Invalid Protocol Version from " ) ;
		return SLURM_PROTOCOL_VERSION_ERROR ;
	}
	return SLURM_PROTOCOL_SUCCESS ;
}

/* simple function to create a header, always insuring that an accurate version string is inserted */
void init_header ( header_t * header , slurm_msg_type_t msg_type , uint16_t flags )
{
	header -> version = SLURM_PROTOCOL_VERSION ;
	header -> flags = flags ;
	header -> msg_type = msg_type ;
}


