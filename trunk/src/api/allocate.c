/* 
 * allocate.c - allocate nodes for a job with supplied contraints
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
#include <string.h>
#include <syslog.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "slurm.h"
#include "slurmlib.h"

#if DEBUG_MODULE
/* main is used here for testing purposes only */
main (int argc, char *argv[])
{
	int error_code;
	char *node_list, *job_id;

	error_code = slurm_allocate
		("User=1500 Script=/bin/hostname JobName=job01 TotalNodes=400 TotalProcs=1000 ReqNodes=lx[3000-3003] Partition=batch MinRealMemory=1024 MinTmpDisk=2034 Groups=students,employee MinProcs=4 Contiguous=YES Key=1234",
		 &node_list, &job_id);
	if (error_code)
		printf ("allocate error %d\n", error_code);
	else {
		printf ("allocate nodes %s to job %s\n", node_list, job_id);
		free (node_list);
		free (job_id);
	}

	while (1) {
		error_code = slurm_allocate
			("User=1500 Script=/bin/hostname JobName=more TotalProcs=4000 Partition=batch Key=1234 ",
			 &node_list, &job_id);
		if (error_code) {
			printf ("allocate error %d\n", error_code);
			break;
		}
		else {
			printf ("allocate nodes %s to job %s\n", node_list, job_id);
			free (node_list);
			free (job_id);
		}
	}

	while (1) {
		error_code = slurm_allocate
			("User=1500 Script=/bin/hostname JobName=more TotalProcs=40 Partition=batch Key=1234 ",
			 &node_list, &job_id);
		if (error_code) {
			printf ("allocate error %d\n", error_code);
			break;
		}
		else {
			printf ("allocate nodes %s to job %s\n", node_list, job_id);
			free (node_list);
			free (job_id);
		}
	}

	exit (0);
}
#endif


/*
 * slurm_allocate - allocate nodes for a job with supplied contraints. 
 * input: spec - specification of the job's constraints
 *        job_id - place into which a job_id pointer can be placed
 * output: job_id - node_list - list of allocated nodes
 *         returns 0 if no error, EINVAL if the request is invalid, 
 *			EAGAIN if the request can not be satisfied at present
 * NOTE: required specifications include: User=<uid> Script=<pathname>
 *	optional specifications include: Contiguous=<YES|NO> 
 *	Distribution=<BLOCK|CYCLE> Features=<features> Groups=<groups>
 *	JobId=<id> JobName=<name> Key=<credential> MinProcs=<count>
 *	MinRealMemory=<MB> MinTmpDisk=<MB> Partition=<part_name>
 *	Priority=<integer> ProcsPerTask=<count> ReqNodes=<node_list>
 *	Shared=<YES|NO> TimeLimit=<minutes> TotalNodes=<count>
 *	TotalProcs=<count>
 * NOTE: the calling function must free the allocated storage at node_list[0]
 *	and job_id[0]
 */
int
slurm_allocate (char *spec, char **node_list, char **job_id) {
	int buffer_offset, buffer_size, error_code, in_size;
	char *request_msg, *buffer, *job_id_ptr;
	int sockfd;
	struct sockaddr_in serv_addr;

	node_list[0] = job_id[0] = NULL;
	if ((spec == NULL) || (node_list == (char **) NULL))
		return EINVAL;
	request_msg = malloc (strlen (spec) + 10);
	if (request_msg == NULL)
		return EAGAIN;
	strcpy (request_msg, "Allocate ");
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
	}			
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
		}		
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
	job_id_ptr = strchr(buffer, (int) ' ');
	if (job_id_ptr != NULL) {
		job_id[0] = malloc(strlen(job_id_ptr));
		job_id_ptr[0] = (char) NULL;
		if (job_id[0] != NULL)
			strcpy(job_id[0], &job_id_ptr[1]);
	}
	node_list[0] = buffer;
	return 0;
}
