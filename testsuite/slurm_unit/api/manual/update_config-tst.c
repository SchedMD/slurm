
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include <src/api/slurm.h>
#include <testsuite/dejagnu.h>

/* main is used here for module testing purposes only */
int
main (int argc, char *argv[]) {
	int error_code;
	char part_update1[] = "PartitionName=batch State=DOWN";
	char part_update2[] = "PartitionName=batch State=UP";
	char node_update1[] = "NodeName=lx1234 State=DOWN";
	char node_update2[] = "NodeName=lx1234 State=IDLE";

	error_code = slurm_update_config (part_update1);
	if (error_code)
		printf ("error %d for part_update1\n", error_code);
	error_code = slurm_update_config (part_update2);
	if (error_code)
		printf ("error %d for part_update2\n", error_code);
	error_code = slurm_update_config (node_update1);
	if (error_code)
		printf ("error %d for node_update1\n", error_code);
	error_code = slurm_update_config (node_update2);
	if (error_code)
		printf ("error %d for node_update2\n", error_code);

	return (error_code);
}


