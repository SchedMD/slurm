#include "slurm_protocol_defs.h"
#include "slurm_protocol_util.h"
#include <stdint.h>
#include <stdio.h>

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

