#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include <src/api/slurm.h>
#include <testsuite/dejagnu.h>

/* main is used here for testing purposes only */
int 
main (int argc, char *argv[])
{
	int error_code;
	char *node_list;
	uint32_t job_id;

	error_code = slurm_allocate_resources
		("User=1500 JobName=job01 TotalNodes=400 TotalProcs=1000 ReqNodes=lx[3000-3003] Partition=batch MinRealMemory=1024 MinTmpDisk=2034 Groups=students,employee MinProcs=4 Contiguous=YES Key=1234 Immediate",
		 &node_list, &job_id);
	if (error_code)
		printf ("allocate error %d\n", error_code);
	else {
		printf ("allocate nodes %s to job %u\n", node_list, job_id);
		free (node_list);
	}

	while (1) {
		error_code = slurm_allocate_resources
			("User=1500 JobName=more TotalProcs=4000 Partition=batch Key=1234 Immediate",
			 &node_list, &job_id);
		if (error_code) {
			printf ("allocate error %d\n", error_code);
			break;
		}
		else {
			printf ("allocate nodes %s to job %u\n", node_list, job_id);
			free (node_list);
		}
	}

	while (1) {
		error_code = slurm_allocate_resources
			("User=1500 JobName=more TotalProcs=40 Partition=batch Key=1234 Immediate",
			 &node_list, &job_id);
		if (error_code) {
			printf ("allocate error %d\n", error_code);
			break;
		}
		else {
			printf ("allocate nodes %s to job %u\n", node_list, job_id);
			free (node_list);
		}
	}

	return (0);
}
