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

#if DEBUG_MODULE
/* main is used here for module testing purposes only */
int
main (int argc, char *argv[]) 
{
	static time_t last_update_time = (time_t) NULL;
	int error_code ;
	partition_info_msg_t * part_info_ptr = NULL;

	error_code = slurm_load_partitions (last_update_time, &part_info_ptr);
	if (error_code) {
		printf ("slurm_load_part error %d\n", error_code);
		exit (error_code);
	}

	printf("Updated at %lx, record count %d\n",
		(time_t) part_info_ptr->last_update, part_info_ptr->record_count);
	slurm_print_partition_info ( part_info_ptr ) ;
	slurm_free_partition_info (part_info_ptr);
	exit (0);
}
#endif

void slurm_print_partition_info ( partition_info_msg_t * part_info_ptr )
{
	int i ;
	partition_table_t * part_ptr = part_info_ptr->partition_array ;

	for (i = 0; i < part_info_ptr->record_count; i++) {
		slurm_print_partition_table ( & part_ptr[i] ) ;
	}

}

void slurm_print_partition_table ( partition_table_t * part_ptr )
{
	int j ;

	printf ("PartitionName=%s MaxTime=%u ", part_ptr->name, part_ptr->max_time);
	printf ("MaxNodes=%u TotalNodes=%u ", part_ptr->max_nodes, part_ptr->total_nodes);
	printf ("TotalCPUs=%u Key=%u\n", part_ptr->total_cpus, part_ptr->key);
	printf ("   Default=%u ", part_ptr->default_part);
	printf ("Shared=%u StateUp=%u ", part_ptr->shared, part_ptr->state_up);
	printf ("Nodes=%s AllowGroups=%s\n", part_ptr->nodes, part_ptr->allow_groups);
	printf ("   NodeIndecies=");
	for (j = 0; part_ptr->node_inx; j++) {
		if (j > 0)
			printf(",%d", part_ptr->node_inx[j]);
		else
			printf("%d", part_ptr->node_inx[j]);
		if (part_ptr->node_inx[j] == -1)
			break;
	}
	printf("\n\n");
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
        /* pack32 ( update_time , &buf_ptr , &buffer_size ); */
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
