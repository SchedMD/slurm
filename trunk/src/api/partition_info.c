/* 
 * partition_info.c - get the partition information of slurm
 * see slurm.h for documentation on external functions and data structures
 *
 * author: moe jette, jette@llnl.gov
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include <src/api/slurm.h>
#include <src/common/slurm_protocol_api.h>


void slurm_print_partition_info ( FILE* out, partition_info_msg_t * part_info_ptr )
{
	int i ;
	partition_table_t * part_ptr = part_info_ptr->partition_array ;

	for (i = 0; i < part_info_ptr->record_count; i++) {
		slurm_print_partition_table ( out, & part_ptr[i] ) ;
	}

}

void slurm_print_partition_table ( FILE* out, partition_table_t * part_ptr )
{
	int j ;

	fprintf ( out, "PartitionName=%s MaxTime=%u ", part_ptr->name, part_ptr->max_time);
	fprintf ( out, "MaxNodes=%u TotalNodes=%u ", part_ptr->max_nodes, part_ptr->total_nodes);
	fprintf ( out, "TotalCPUs=%u Key=%u\n", part_ptr->total_cpus, part_ptr->key);
	fprintf ( out, "   Default=%u ", part_ptr->default_part);
	fprintf ( out, "Shared=%u StateUp=%u ", part_ptr->shared, part_ptr->state_up);
	fprintf ( out, "Nodes=%s AllowGroups=%s\n", part_ptr->nodes, part_ptr->allow_groups);
	fprintf ( out, "   NodeIndecies=");
	for (j = 0; part_ptr->node_inx; j++) {
		if (j > 0)
			fprintf( out, ",%d", part_ptr->node_inx[j]);
		else
			fprintf( out, "%d", part_ptr->node_inx[j]);
		if (part_ptr->node_inx[j] == -1)
			break;
	}
	fprintf( out, "\n\n");
}



int
slurm_load_partitions (time_t update_time, partition_info_msg_t **partition_info_msg_pptr)
{
        int msg_size ;
        int rc ;
        slurm_fd sockfd ;
        slurm_msg_t request_msg ;
        slurm_msg_t response_msg ;
        last_update_msg_t last_time_msg ;
	return_code_msg_t * slurm_rc_msg ;

        /* init message connection for message communication with controller */
        if ( ( sockfd = slurm_open_controller_conn ( SLURM_PORT ) ) == SLURM_SOCKET_ERROR )
                return SLURM_SOCKET_ERROR ;

        /* send request message */
        last_time_msg . last_update = update_time ;
        request_msg . msg_type = REQUEST_PARTITION_INFO ;
        request_msg . data = &last_time_msg ;
        if ( ( rc = slurm_send_controller_msg ( sockfd , & request_msg ) ) == SLURM_SOCKET_ERROR )
                return SLURM_SOCKET_ERROR ;

        /* receive message */
        if ( ( msg_size = slurm_receive_msg ( sockfd , & response_msg ) ) == SLURM_SOCKET_ERROR )
                return SLURM_SOCKET_ERROR ;
        /* shutdown message connection */
        if ( ( rc = slurm_shutdown_msg_conn ( sockfd ) ) == SLURM_SOCKET_ERROR )
                return SLURM_SOCKET_ERROR ;

	switch ( response_msg . msg_type )
	{
		case RESPONSE_PARTITION_INFO:
        		 *partition_info_msg_pptr = ( partition_info_msg_t * ) response_msg . data ;
			break ;
		case RESPONSE_SLURM_RC:
			slurm_rc_msg = ( return_code_msg_t * ) response_msg . data ;
			break ;
		default:
			return SLURM_UNEXPECTED_MSG_ERROR ;
			break ;
	}

        return SLURM_SUCCESS ;
}
