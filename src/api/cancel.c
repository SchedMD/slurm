/* 
 * cancel.c - cancel a slurm job
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
	int error_code = 0, i;

	if (argc < 2) {
		printf ("Usage: %s job_id\n", argv[0]);
		exit (1);
	}

	for (i=1; i<argc; i++) {
		error_code = slurm_cancel ((uint16_t) atoi(argv[i]));
		if (error_code != 0)
			printf ("slurm_cancel error %d for job %s\n", 
				error_code, argv[i]);
	}

	exit (error_code);
}
#endif


/*
 * slurm_cancel - cancel the specified job 
 * input: job_id - the job_id to be cancelled
 * output: returns 0 if no error, EINVAL if the request is invalid, 
 *			EAGAIN if the request can not be satisfied at present
 */
int
slurm_cancel (uint16_t job_id) 
{
	int buffer_offset, buffer_size, in_size;
	char *request_msg, *buffer, id_str[20];
	int sockfd;
	struct sockaddr_in serv_addr;

	sprintf (id_str, "%u", job_id);
	request_msg = malloc (strlen (id_str) + 11);
	if (request_msg == NULL)
		return EAGAIN;
	strcpy (request_msg, "JobCancel ");
	strcat (request_msg, id_str);

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
	printf ("%s\n", buffer);
	free (buffer);
	return 0;
}
