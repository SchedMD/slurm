

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#include "src/slurm/slurm.h"
#include "src/common/slurm_protocol_api.h"

#include "get_resp.h"

#define DEBUG_MODULE
/* report results of successful job allocation */
void report_results(resource_allocation_response_msg_t* resp_msg);

int
main( int argc, char* argv[])
{
	int error_code;	
	int job_id = atoi( argv[1] );
	error_code = slurm_kill_job ( job_id, SIGKILL ); 

	if (error_code)
		slurm_perror( "slurm_cancel_job faile: ");
	else
		printf("Job %d canceled\n", job_id );	

	return SLURM_SUCCESS ;
}


