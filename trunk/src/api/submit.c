/* 
 * submit.c - submit a job with supplied contraints
 * see slurm.h for documentation on external functions and data structures
 *
 * author: moe jette, jette@llnl.gov
 */

#define DEBUG_SYSTEM 1

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
#include <arpa/inet.h>
#include <unistd.h>

#include "slurmlib.h"

#if DEBUG_MODULE
/* main is used here for testing purposes only */
int 
main (int argc, char *argv[]) 
{
	int error_code, i, count;
	uint16_t job_id;

	error_code = slurm_submit
		("User=1500 Script=/bin/hostname JobName=job01 TotalNodes=400 TotalProcs=1000 ReqNodes=lx[3000-3003] Partition=batch MinRealMemory=1024 MinTmpDisk=2034 Groups=students,employee MinProcs=4 Contiguous=YES Key=1234",
		 &job_id);
	if (error_code) {
		printf ("submit error %d\n", error_code);
		exit (error_code);
	}
	else
		printf ("job %u submitted\n", job_id);

	if (argc > 1) 
		count = atoi (argv[1]);
	else
		count = 5;

	for (i=0; i<count; i++) {
		error_code = slurm_submit
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

	exit (error_code);
}
#endif


/*
 * slurm_submit - submit/queue a job with supplied contraints. 
 * input: spec - specification of the job's constraints
 *	job_id - place to store id of submitted job
 * output: job_id - the job's id
 *	returns 0 if no error, EINVAL if the request is invalid
 * NOTE: required specification include: Script=<script_path_name>
 *	User=<uid>
 * NOTE: optional specifications include: Contiguous=<YES|NO> 
 *	Distribution=<BLOCK|CYCLE> Features=<features> Groups=<groups>
 *	JobId=<id> JobName=<name> Key=<key> MinProcs=<count> 
 *	MinRealMemory=<MB> MinTmpDisk=<MB> Partition=<part_name>
 *	Priority=<integer> ProcsPerTask=<count> ReqNodes=<node_list>
 *	Shared=<YES|NO> TimeLimit=<minutes> TotalNodes=<count>
 *	TotalProcs=<count> Immediate=<YES|NO>
 */
int
slurm_submit (char *spec, uint16_t *job_id)
{
	int buffer_offset, buffer_size, in_size;
	char *request_msg, *buffer;
	int sockfd;
	struct sockaddr_in serv_addr;

	if (spec == NULL)
		return EINVAL;
	request_msg = malloc (strlen (spec) + 10);
	if (request_msg == NULL)
		return EAGAIN;
	strcpy (request_msg, "JobSubmit ");
	strcat (request_msg, spec);

	if ((sockfd = socket (AF_INET, SOCK_STREAM, 0)) < 0)
		return EINVAL;
	serv_addr.sin_family = PF_INET;
	serv_addr.sin_addr.s_addr = inet_addr (SLURMCTLD_HOST);
	serv_addr.sin_port = htons (SLURMCTLD_PORT);
	if (connect
	    (sockfd, (struct sockaddr *) &serv_addr,
	     sizeof (serv_addr)) < 0) {
		close (sockfd);
		return EAGAIN;
	}			/* if */
	if (send (sockfd, request_msg, strlen (request_msg) + 1, 0) <
	    strlen (request_msg)) {
		close (sockfd);
		return EAGAIN;
	}

	buffer = NULL;
	buffer_offset = 0;
	buffer_size = 8 * 1024;
	while (1) {
		buffer = realloc (buffer, buffer_size);
		if (buffer == NULL) {
			close (sockfd);
			return EAGAIN;
		}
		in_size =
			recv (sockfd, &buffer[buffer_offset],
			      (buffer_size - buffer_offset), 0);
		if (in_size <= 0) {	/* end of input */
			in_size = 0;
			break;
		}		/* if */
		buffer_offset += in_size;
		buffer_size += in_size;
	}
	close (sockfd);
	buffer_size = buffer_offset + in_size;
	buffer = realloc (buffer, buffer_size);
	if (buffer == NULL)
		return EAGAIN;

	if (strcmp (buffer, "EAGAIN") == 0) {
		free (buffer);
		return EAGAIN;
	}
	if (strcmp (buffer, "EINVAL") == 0) {
		free (buffer);
		return EINVAL;
	}
	*job_id = (uint16_t) atoi (buffer);
	return 0;
}
