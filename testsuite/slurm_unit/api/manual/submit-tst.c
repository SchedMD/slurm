
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include <src/api/slurm.h>
#include <testsuite/dejagnu.h>

/* main is used here for testing purposes only */
int 
main (int argc, char *argv[]) 
{
	int error_code, i, count;
	uint32_t job_id;

	error_code = slurm_submit_batch_job
		("User=1500 Script=/bin/hostname JobName=job01 TotalNodes=400 TotalProcs=1000 ReqNodes=lx[3000-3003] Partition=batch MinRealMemory=1024 MinTmpDisk=2034 Groups=students,employee MinProcs=4 Contiguous=YES Key=1234",
		 &job_id);
	if (error_code) {
		printf ("submit error %d\n", error_code);
		return (error_code);
	}
	else
		printf ("job %u submitted\n", job_id);

	if (argc > 1) 
		count = atoi (argv[1]);
	else
		count = 5;

	for (i=0; i<count; i++) {
		error_code = slurm_submit_batch_job
			("User=1500 Script=/bin/hostname JobName=more TotalProcs=4000 Partition=batch Key=1234 ",
			 &job_id);
		if (error_code) {
			printf ("submit error %d\n", error_code);
			break;
		}
		else {
			printf ("job %u submitted\n", job_id);
		}
	}

	return (error_code);
}
