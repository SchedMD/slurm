
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include <slurm/slurm.h>
#include "testsuite/dejagnu.h"

/* main is used here for module testing purposes only */
int
main (int argc, char *argv[]) 
{
	static time_t last_update_time = (time_t) NULL;
	int error_code ;
	partition_info_msg_t * part_info_ptr = NULL;

	error_code = slurm_load_partitions (last_update_time, &part_info_ptr, 1);
	if (error_code) {
		slurm_perror ("slurm_load_partitions");
		return (error_code);
	}

	note("Updated at %ld, record count %d\n",
		(time_t) part_info_ptr->last_update, 
		part_info_ptr->record_count);

	slurm_print_partition_info_msg (stdout, part_info_ptr, 0);
	slurm_free_partition_info_msg (part_info_ptr);
	return (0);
}
