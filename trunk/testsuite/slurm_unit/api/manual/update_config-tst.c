
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "src/slurm/slurm.h"
#include "testsuite/dejagnu.h"

#ifndef true
#define false 0
#define true  1
#endif

/* main is used here for module testing purposes only */
int
main (int argc, char *argv[]) {
	int error_code;
	update_part_msg_t	part_update1 ;
	update_part_msg_t	part_update2 ;
	update_node_msg_t	node_update1 ;
	update_node_msg_t	node_update2 ;

	slurm_init_part_desc_msg ( &part_update1 );
	slurm_init_part_desc_msg ( &part_update2 );
	part_update1 . name = "batch" ;
	part_update2 . name = "batch" ;
	part_update1 . state_up = false ;
	part_update2 . state_up = true ; 

	node_update1 . node_names = "lx1234" ;
	node_update2 . node_names = "lx1234" ;
	node_update1 . node_state = NODE_STATE_DRAINING ;
	node_update2 . node_state = NODE_STATE_IDLE ; 

	error_code = slurm_update_partition ( &part_update1);
	if (error_code)
		printf ("error %d for part_update1\n", errno);

	error_code = slurm_update_partition ( &part_update2);
	if (error_code)
		printf ("error %d for part_update2\n", errno);

	error_code = slurm_update_node ( &node_update1);
	if (error_code)
		printf ("error %d for node_update1\n", errno);

	error_code = slurm_update_node ( &node_update2);
	if (error_code)
		printf ("error %d for node_update2\n", errno);

	return (errno);
}


