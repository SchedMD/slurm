
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include <src/api/slurm.h>
#include <testsuite/dejagnu.h>

/* main is used here for testing purposes only */
int
main (int argc, char *argv[]) 
{
	static time_t last_update_time = (time_t) NULL;
	int error_code, i;
	node_info_msg_t * node_info_msg_ptr = NULL;
	node_info_t * node_ptr ;

	error_code = slurm_load_node (last_update_time, &node_info_msg_ptr);
	if (error_code) {
		printf ("slurm_load_node error %d\n", errno);
		return (error_code);
	}

	printf("Nodes updated at %d, record count %d\n",
		node_info_msg_ptr ->last_update, node_info_msg_ptr->record_count);
	node_ptr = node_info_msg_ptr -> node_array ;

	for (i = 0; i < node_info_msg_ptr-> record_count; i++) 
	{
		/* to limit output we print only the first 10 entries, 
		 * last 1 entry, and every 200th entry */
		if ((i < 10) || (i % 200 == 0) || 
		    ((i + 1)  == node_info_msg_ptr-> record_count)) {
			slurm_print_node_table ( stdout, & node_ptr[i] ) ;
		}
		else if ((i==10) || (i % 200 == 1))
			printf ("skipping...\n");
	}

	slurm_free_node_info_msg ( node_info_msg_ptr ) ;
	return (0);
}
