#include <src/common/slurm_protocol_defs.h>
#include <src/common/slurm_protocol_common.h>
#include <src/common/slurm_protocol_util.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>

extern int debug ;

/* checks to see that the specified header was sent from a node running the same version of the protocol as the current node */
uint32_t check_header_version( header_t * header)
{
	if ( header -> version != SLURM_PROTOCOL_VERSION )
	{
		if ( debug ) 
		{
			fprintf ( stderr , "Invalid Protocol Version from " ) ;
		}
		return SLURM_PROTOCOL_VERSION_ERROR ;
	}
	return SLURM_PROTOCOL_SUCCESS ;
}

/* simple function to create a header, always insuring that an accurate version string is inserted */
void init_header ( header_t * header , slurm_message_type_t message_type , uint16_t flags )
{
	header -> version = SLURM_PROTOCOL_VERSION ;
	header -> flags = flags ;
	header -> message_type = message_type ;
}

/* sets the fields of a slurm_addr */
void set_slurm_addr_hton ( slurm_addr * slurm_address , uint16_t port , uint32_t ip_address )
{
	slurm_address -> family = AF_SLURM ;
	slurm_address -> port = htons ( port ) ;
	slurm_address -> address = htons ( ip_address ) ;
}
