#include "slurm_protocol_defs.h"
#include "slurm_protocol_util.h"
#include <stdint.h>
#include <stdio.h>

extern int debug ;

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

void init_header ( header_t * header , slurm_message_type_t message_type , uint16_t flags )
{
	header -> version = SLURM_PROTOCOL_VERSION ;
	header -> flags = flags ;
	header -> message_type = message_type ;
}

