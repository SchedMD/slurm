

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#include <src/api/slurm.h>
#include <src/common/slurm_protocol_api.h>

#include "get_resp.h"
#define DEBUG_MODULE
/* report results of successful job allocation */
void report_results(resource_allocation_response_msg_t* resp_msg);
int
main( int argc, char* argv[])
{
	int error_code;	
	job_desc_msg_t job_mesg;
	resource_allocation_response_msg_t* resp_msg ;

	/* Create a job/resource allocation */
	slurm_init_job_desc_msg( &job_mesg );
	printf("Creating Job Message\n");
	
	job_mesg. contiguous = get_tf_resp( "contiguous", 0 );
	job_mesg. groups = get_string_resp( "groups", "students,employee\0");
	job_mesg. name = get_string_resp( "job_name", "job01\0");
	job_mesg. min_procs = get_int_resp( "min_procs", 4 );
	job_mesg. min_memory = get_int_resp( "min_memory", 1024);
	job_mesg. min_tmp_disk = get_int_resp( "min_tmp_disk", 2034 );
	job_mesg. partition = get_string_resp("string_resp", "batch\0" );
	job_mesg. priority = get_int_resp( "priority", 100 );
	job_mesg. req_nodes = get_string_resp( "req_nodes", "lx[3000-3003]\0" );
	job_mesg. shared = get_int_resp( "shared", 0 );
	job_mesg. time_limit = get_int_resp( "time_limit", 200 );
	job_mesg. num_procs = get_int_resp( "num_procs", 1000) ;
	job_mesg. num_nodes = get_int_resp( "num_nodes", 400);
	job_mesg. user_id = get_int_resp( "user_id", 1500 );


	error_code = slurm_allocate_resources ( &job_mesg , &resp_msg , false ); 
	if (error_code)
		printf ("allocate error %s\n", slurm_strerror( error_code ));
	else
		report_results(resp_msg);

	return SLURM_SUCCESS ;
}


void
report_results(resource_allocation_response_msg_t* resp_msg)
{
	int i;

	printf ("NODES ALLOCATED\n\t JOB_ID = %u\n\tnodes = %s\n",  resp_msg->job_id, resp_msg->node_list);
	if (resp_msg->num_cpu_groups > 0) {
		printf ("\tprocessor counts: ");
		for (i=0; i<resp_msg->num_cpu_groups; i++) {
			if (i > 0)
				printf(", ");
			printf ("%u(x%u)", resp_msg->cpus_per_node[i], resp_msg->cpu_count_reps[i]);
		}
		printf ("\n");
	}
}
