
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
	job_desc_msg_t job_mesg;
	submit_response_msg_t *resp_msg;
	
	slurm_init_job_desc_msg( &job_mesg );
	job_mesg. contiguous = 1; 
	job_mesg. groups = ("students,employee");
	job_mesg. name = ("job01");
	job_mesg. partition_key = NULL;
	job_mesg. min_procs = 4;
	job_mesg. min_memory = 1024;
	job_mesg. min_tmp_disk = 2034;
	job_mesg. partition = "batch";
	job_mesg. priority = 100;
	job_mesg. req_nodes = "lx[3000-3003]";
	job_mesg. shared = 0;
	job_mesg. time_limit = 100;
	job_mesg. num_procs = 1000;
	job_mesg. num_nodes = 400;
	job_mesg. user_id = 1500;
	job_mesg. script = "/bin/hostname\n/bin/date\n";
	job_mesg. stderr = "/tmp/slurm.stderr";
	job_mesg. stdin = "/tmp/slurm.stdin";
	job_mesg. stdout = "/tmp/slurm.stdout";
	job_mesg. work_dir = "/tmp\0";
	job_mesg. environment = "SLURM_ENV=looking_good\n";

	error_code = slurm_submit_batch_job( &job_mesg, &resp_msg );
	if (error_code) {
		printf ("submit error %d\n", errno);
		return (error_code);
	}
	else
		printf ("job %u submitted\n", resp_msg->job_id);
	slurm_free_submit_response_response_msg ( resp_msg );

	if (argc > 1) 
		count = atoi (argv[1]);
	else
		count = 1;

	for (i=1; i<count; i++) {
		slurm_init_job_desc_msg( &job_mesg );
		job_mesg. contiguous = 1; 
		job_mesg. groups = ("students,employee");
		job_mesg. name = ("job02+");
		job_mesg. partition_key = NULL;
		job_mesg. min_procs = 4;
		job_mesg. min_memory = 1024 + i;
		job_mesg. min_tmp_disk = 2034 + i;
		job_mesg. partition = "batch";
		job_mesg. priority = 100 + i;
		job_mesg. script = "/bin/hostname\n";
		job_mesg. shared = 0;
		job_mesg. time_limit = 100 + i;
		job_mesg. num_procs = 1000 + i;
		job_mesg. num_nodes = 400 + i;
		job_mesg. user_id = 1500;
		error_code = slurm_submit_batch_job( &job_mesg, &resp_msg );
		if (error_code) {
			printf ("submit error %d\n", errno);
			break;
		}
		else {
			printf ("job %u submitted\n", resp_msg->job_id);
		}
		slurm_free_submit_response_response_msg ( resp_msg );
	}

	exit (error_code);
}
