/* 
 * reconfigure.c - request that slurmctld re-read the configuration files
 *
 * author: moe jette, jette@llnl.gov
 */

#ifdef have_config_h
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
#include "slurmlib.h"

#if DEBUG_MODULE
/* main is used here for module testing purposes only */
int 
main (int argc, char *argv[]) {
	int i, count, error_code;

	if (argc < 2)
		count = 1;
	else
		count = atoi (argv[1]);

	for (i = 0; i < count; i++) {
		error_code = reconfigure ();
		if (error_code != 0) {
			printf ("reconfigure error %d\n", error_code);
			exit (1);
		}
	}
	exit (0);
}
#endif


/* 
 * reconfigure - _ request that slurmctld re-read the configuration files
 * output: returns 0 on success, errno otherwise
 */
int
reconfigure () {
	int buffer_offset, buffer_size, error_code, in_size;
	char request_msg[64], *buffer;
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
	sprintf (request_msg, "Reconfigure");
	if (send (sockfd, request_msg, strlen (request_msg) + 1, 0) <
	    strlen (request_msg)) {
		close (sockfd);
		return EINVAL;
	}			
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
