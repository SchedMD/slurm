/* 
 * update_config.c - request that slurmctld update its configuration
 *
 * author: moe jette, jette@llnl.gov
 */

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

#include "slurm.h"

#if DEBUG_MODULE
/* main is used here for module testing purposes only */
int
main (int argc, char *argv[]) {
	int error_code;
	char part_update1[] = "PartitionName=batch State=DOWN";
	char part_update2[] = "PartitionName=batch State=UP";
	char node_update1[] = "NodeName=lx1234 State=DOWN";
	char node_update2[] = "NodeName=lx1234 State=IDLE";

	error_code = update_config (part_update1);
	if (error_code)
		printf ("error %d for part_update1\n", error_code);
	error_code = update_config (part_update2);
	if (error_code)
		printf ("error %d for part_update2\n", error_code);
	error_code = update_config (node_update1);
	if (error_code)
		printf ("error %d for node_update1\n", error_code);
	error_code = update_config (node_update2);
	if (error_code)
		printf ("error %d for node_update2\n", error_code);

	exit (error_code);
}
#endif


/* 
 * update_config - _ request that slurmctld update its configuration per request
 * input: a line containing configuration information per the configuration file format
 * output: returns 0 on success, errno otherwise
 */
int
update_config (char *spec) {
	static int error_code;
	int buffer_offset, buffer_size, in_size;
	char *request_msg, *buffer;
	int sockfd;
	struct sockaddr_in serv_addr;

	if ((sockfd = socket (AF_INET, SOCK_STREAM, 0)) < 0)
		return EINVAL;
	serv_addr.sin_family = PF_INET;
	serv_addr.sin_addr.s_addr = inet_addr (SLURMCTLD_HOST);
	serv_addr.sin_port = htons (SLURMCTLD_PORT);
	if (connect
	    (sockfd, (struct sockaddr *) &serv_addr,
	     sizeof (serv_addr)) < 0) {
		close (sockfd);
		return EINVAL;
	}			
	request_msg = malloc (strlen (spec) + 10);
	if (request_msg == NULL) {
		close (sockfd);
		return ENOMEM;
	}			
	sprintf (request_msg, "Update %s", spec);
	if (send (sockfd, request_msg, strlen (request_msg) + 1, 0) <
	    strlen (request_msg)) {
		close (sockfd);
		free (request_msg);
		return EINVAL;
	}			
	free (request_msg);
	buffer = NULL;
	buffer_offset = 0;
	buffer_size = 1024;
	while (1) {
		buffer = realloc (buffer, buffer_size);
		if (buffer == NULL) {
			close (sockfd);
			return ENOMEM;
		}		
		in_size =
			recv (sockfd, &buffer[buffer_offset],
			      (buffer_size - buffer_offset), 0);
		if (in_size <= 0) {	/* end if input */
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
		return ENOMEM;
	error_code = atoi (buffer);
	free (buffer);
	return error_code;
}
