
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include <slurm/slurm.h>
#include "testsuite/dejagnu.h"

/* main is used here for testing purposes only */
int
main (int argc, char *argv[]) 
{
	static time_t last_update_time = (time_t) NULL;
	int error_code, i;
	node_info_msg_t * node_info_msg_ptr = NULL;

	error_code = slurm_load_node (last_update_time, &node_info_msg_ptr, 1);
	if (error_code) {
		slurm_perror ("last_update_time");
		return (error_code);
	}

	printf("Nodes updated at %ld, record count %d\n",
		(long)node_info_msg_ptr ->last_update, 
		node_info_msg_ptr->record_count);

	for (i = 0; i < node_info_msg_ptr-> record_count; i++) 
	{
		/* to limit output we print only the first 10 entries, 
		 * last 1 entry, and every 200th entry */
		if ((i < 10) || (i % 200 == 0) || 
		    ((i + 1)  == node_info_msg_ptr-> record_count)) {
			slurm_print_node_table ( stdout, & node_info_msg_ptr ->
							 node_array[i], 0 ) ;
		}
		else if ((i==10) || (i % 200 == 1))
			printf ("skipping...\n");
	}

	slurm_free_node_info_msg ( node_info_msg_ptr ) ;
	return (0);
}
