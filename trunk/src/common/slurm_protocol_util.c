#include <stdlib.h>
#include <assert.h>

#include <src/common/slurm_protocol_defs.h>
#include <src/common/slurm_protocol_common.h>
#include <src/common/slurm_protocol_util.h>
#include <src/common/log.h>

/* checks to see that the specified header was sent from a node running the same version of the protocol as the current node */
uint32_t check_header_version( header_t * header)
{
	if ( header -> version != SLURM_PROTOCOL_VERSION )
	{
		debug ( "Invalid Protocol Version %d ", header -> version ) ;
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

/* checks to see that the specified header was sent from a node running the same version of the protocol as the current node */
uint32_t check_io_stream_header_version( slurm_io_stream_header_t * header)
{
	if ( header -> version != SLURM_PROTOCOL_VERSION )
	{
		debug ( "Invalid IO Stream Protocol Version %d ", header -> version ) ;
		return SLURM_PROTOCOL_IO_STREAM_VERSION_ERROR ;
	}
	return SLURM_PROTOCOL_SUCCESS ;
}

/* simple function to create a header, always insuring that an accurate version string is inserted */
void init_io_stream_header ( slurm_io_stream_header_t * header , char * key , uint32_t task_id , uint16_t type )
{

	assert ( key != NULL );
	header -> version = SLURM_PROTOCOL_VERSION ;
	memcpy ( header -> key , key , SLURM_SSL_SIGNATURE_LENGTH ) ;
	header -> task_id = task_id ;
	header -> type = type ;
}
