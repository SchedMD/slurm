/*****************************************************************************\
 * allocate-tst.c - exercise the SLURM allocate API
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Moe Jette <jette1@llnl.gov> and Joey Ekstrom <ekstrom1@llnl.gov>
 *  UCRL-CODE-2002-040.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include <src/api/slurm.h>
#include <testsuite/dejagnu.h>

void report_results(resource_allocation_response_msg_t* resp_msg);

/* main is used here for testing purposes only */
int 
main (int argc, char *argv[])
{
	int error_code, job_count, max_jobs;
	job_desc_msg_t job_mesg;
	resource_allocation_response_msg_t* resp_msg ;

	if (argc > 1) 
		max_jobs = atoi (argv[1]);
	else
		max_jobs = 1;

	slurm_init_job_desc_msg( &job_mesg );
	job_mesg. contiguous = 1;
	job_mesg. groups = ("students,employee\0");
	job_mesg. name = ("job01\0");
	job_mesg. partition_key = "1234";
	job_mesg. min_procs = 4;
	job_mesg. min_memory = 1024;
	job_mesg. min_tmp_disk = 2034;
	job_mesg. partition = "batch\0";
	job_mesg. priority = 100;
	job_mesg. req_nodes = "lx[3000-3003]\0";
	job_mesg. shared = 0;
	job_mesg. time_limit = 200;
	job_mesg. num_procs = 1000;
	job_mesg. num_nodes = 400;
	job_mesg. user_id = 1500;


	error_code = slurm_allocate_resources ( &job_mesg , &resp_msg , false ); 
	if (error_code)
		printf ("allocate error %d\n", errno);
	else
		report_results(resp_msg);
	slurm_free_resource_allocation_response_msg ( resp_msg );

	for (job_count = 1 ; job_count <max_jobs;  job_count++) {
		slurm_init_job_desc_msg( &job_mesg );
		job_mesg. contiguous = 1;
		job_mesg. groups = ("students,employee\0");
		job_mesg. name = ("more.big\0");
		job_mesg. partition_key = "1234";
		job_mesg. min_procs = 4;
		job_mesg. min_memory = 1024;
		job_mesg. min_tmp_disk = 2034;
		job_mesg. partition = "batch\0";
		job_mesg. priority = 100;
		job_mesg. shared = 0;
		job_mesg. time_limit = 200;
		job_mesg. num_procs = 4000;
		job_mesg. user_id = 1500;

		/* the string also had Immediate */
		error_code = slurm_allocate_resources ( &job_mesg , &resp_msg , true ); 
		if (error_code) {
			printf ("allocate error %d\n", errno);
			break;
		}
		else 
			report_results(resp_msg);
		slurm_free_resource_allocation_response_msg ( resp_msg );
	}

	for ( ; job_count <max_jobs;  job_count++) {
		slurm_init_job_desc_msg( &job_mesg );
		job_mesg. name = ("more.tiny\0");
		job_mesg. num_procs = 40;
 	        job_mesg. user_id = 1500;

		error_code = slurm_allocate_resources ( &job_mesg , &resp_msg , true ); 
		if (error_code) {
			printf ("allocate error %d\n", errno);
			break;
		}
		else 
			report_results(resp_msg);
		slurm_free_resource_allocation_response_msg ( resp_msg );
	}

	for ( ; job_count <max_jobs;  job_count++) {
		slurm_init_job_desc_msg( &job_mesg );
		job_mesg. name = ("more.queue\0");
		job_mesg. num_procs = 40;
 	        job_mesg. user_id = 1500;

		error_code = slurm_allocate_resources ( &job_mesg , &resp_msg , false ); 
		if (error_code) {
			printf ("allocate error %d\n", errno);
			break;
		}
		else 
			report_results(resp_msg);
		slurm_free_resource_allocation_response_msg ( resp_msg );
	}

	return (0);
}

/* report results of successful job allocation */
void
report_results(resource_allocation_response_msg_t* resp_msg)
{
	int i;

	printf ("allocate nodes %s to job %u\n", resp_msg->node_list, resp_msg->job_id);
	if (resp_msg->num_cpu_groups > 0) {
		printf ("processor counts: ");
		for (i=0; i<resp_msg->num_cpu_groups; i++) {
			if (i > 0)
				printf(", ");
			printf ("%u(x%u)", resp_msg->cpus_per_node[i], resp_msg->cpu_count_reps[i]);
		}
		printf ("\n");
	}
}
