/* 
 * partition_info.c - get the partition information of slurm
 * see slurm.h for documentation on external functions and data structures
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

#include "slurmlib.h"
#include "pack.h"

#if DEBUG_MODULE
/* main is used here for module testing purposes only */
int
main (int argc, char *argv[]) 
{
	static time_t last_update_time = (time_t) NULL;
	int error_code;
	struct build_buffer *build_buffer_ptr = NULL;
	struct build_table  *build_table_ptr = NULL;

	error_code = slurm_load_build (last_update_time, &build_buffer_ptr);
	if (error_code) {
		printf ("slurm_load_build error %d\n", error_code);
		exit (1);
	}

	printf("Updated at %lx\n", build_buffer_ptr->last_update);
	build_table_ptr = build_buffer_ptr->build_table_ptr;
	printf("backup_interval	= %u\n", build_table_ptr->backup_interval);
	printf("backup_location	= %s\n", build_table_ptr->backup_location);
	printf("backup_machine	= %s\n", build_table_ptr->backup_machine);
	printf("control_daemon	= %s\n", build_table_ptr->control_daemon);
	printf("control_machine	= %s\n", build_table_ptr->control_machine);
	printf("epilog		= %s\n", build_table_ptr->epilog);
	printf("fast_schedule	= %u\n", build_table_ptr->fast_schedule);
	printf("hash_base	= %u\n", build_table_ptr->hash_base);
	printf("heartbeat_interval	= %u\n", 
				build_table_ptr->heartbeat_interval);
	printf("init_program	= %s\n", build_table_ptr->init_program);
	printf("kill_wait	= %u\n", build_table_ptr->kill_wait);
	printf("prioritize	= %s\n", build_table_ptr->prioritize);
	printf("prolog		= %s\n", build_table_ptr->prolog);
	printf("server_daemon	= %s\n", build_table_ptr->server_daemon);
	printf("server_timeout	= %u\n", build_table_ptr->server_timeout);
	printf("slurm_conf	= %s\n", build_table_ptr->slurm_conf);
	printf("tmp_fs		= %s\n", build_table_ptr->tmp_fs);

	slurm_free_build_info (build_buffer_ptr);
	exit (0);
}
#endif


/*
 * slurm_free_build_info - free the build information buffer (if allocated)
 * NOTE: buffer is loaded by slurm_load_build.
 */
void
slurm_free_build_info (struct build_buffer *build_buffer_ptr)
{
	if (build_buffer_ptr == NULL)
		return;
	if (build_buffer_ptr->raw_buffer_ptr)
		free (build_buffer_ptr->raw_buffer_ptr);
	if (build_buffer_ptr->build_table_ptr)
		free (build_buffer_ptr->build_table_ptr);
}


/*
 * slurm_load_build - load the slurm build information buffer for use by info 
 *	gathering APIs if build info has changed since the time specified. 
 * input: update_time - time of last update
 *	build_buffer_ptr - place to park build_buffer pointer
 * output: build_buffer_ptr - pointer to allocated build_buffer
 *	returns -1 if no update since update_time, 
 *		0 if update with no error, 
 *		EINVAL if the buffer (version or otherwise) is invalid, 
 *		ENOMEM if malloc failure
 * NOTE: the allocated memory at build_buffer_ptr freed by slurm_free_node_info.
 */
int
slurm_load_build (time_t update_time, struct build_buffer **build_buffer_ptr)
{
	int buffer_offset, buffer_size, in_size, sockfd;
	char request_msg[64], *buffer;
	void *buf_ptr;
	struct sockaddr_in serv_addr;
	uint32_t uint32_tmp, uint32_time;
	uint16_t uint16_tmp;
	struct build_table *build_ptr;

	*build_buffer_ptr = NULL;
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
	sprintf (request_msg, "DumpBuild LastUpdate=%lu",
		 (long) (update_time));
	if (send (sockfd, request_msg, strlen (request_msg) + 1, 0) <
	    strlen (request_msg)) {
		close (sockfd);
		return EINVAL;
	}			
	buffer = NULL;
	buffer_offset = 0;
	buffer_size = 8 * 1024;
	while (1) {
		buffer = realloc (buffer, buffer_size);
		if (buffer == NULL) {
			close (sockfd);
			return ENOMEM;
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
		return ENOMEM;
	if (strcmp (buffer, "nochange") == 0) {
		free (buffer);
		return -1;
	}

	/* load buffer's header (data structure version and time) */
	buf_ptr = buffer;
	unpack32 (&uint32_tmp, &buf_ptr, &buffer_size);
	if (uint32_tmp != BUILD_STRUCT_VERSION) {
		free (buffer);
		return EINVAL;
	}
	unpack32 (&uint32_time, &buf_ptr, &buffer_size);

	/* load the data values */
	build_ptr = malloc (sizeof  (struct build_table));
	if (build_ptr == NULL) {
		free (buffer);
		return ENOMEM;
	}
	unpack16 (&build_ptr->backup_interval, &buf_ptr, &buffer_size);
	unpackstr_ptr (&build_ptr->backup_location, &uint16_tmp, 
		&buf_ptr, &buffer_size);
	unpackstr_ptr (&build_ptr->backup_machine, &uint16_tmp, 
		&buf_ptr, &buffer_size);
	unpackstr_ptr (&build_ptr->control_daemon, &uint16_tmp, 
		&buf_ptr, &buffer_size);
	unpackstr_ptr (&build_ptr->control_machine, &uint16_tmp, 
		&buf_ptr, &buffer_size);
	unpack16 (&build_ptr->controller_timeout, &buf_ptr, &buffer_size);
	unpackstr_ptr (&build_ptr->epilog, &uint16_tmp, 
		&buf_ptr, &buffer_size);
	unpack16 (&build_ptr->fast_schedule, &buf_ptr, &buffer_size);
	unpack16 (&build_ptr->hash_base, &buf_ptr, &buffer_size);
	unpack16 (&build_ptr->heartbeat_interval, &buf_ptr, &buffer_size);
	unpackstr_ptr (&build_ptr->init_program, &uint16_tmp, 
		&buf_ptr, &buffer_size);
	unpack16 (&build_ptr->kill_wait, &buf_ptr, &buffer_size);
	unpackstr_ptr (&build_ptr->prioritize, &uint16_tmp, 
		&buf_ptr, &buffer_size);
	unpackstr_ptr (&build_ptr->prolog, &uint16_tmp, 
		&buf_ptr, &buffer_size);
	unpackstr_ptr (&build_ptr->server_daemon, &uint16_tmp, 
		&buf_ptr, &buffer_size);
	unpack16 (&build_ptr->server_timeout, &buf_ptr, &buffer_size);
	unpackstr_ptr (&build_ptr->slurm_conf, &uint16_tmp, 
		&buf_ptr, &buffer_size);
	unpackstr_ptr (&build_ptr->tmp_fs, &uint16_tmp, 
		&buf_ptr, &buffer_size);

	*build_buffer_ptr = malloc (sizeof (struct build_buffer));
	if (*build_buffer_ptr == NULL) {
		free (buffer);
		free (build_ptr);
		return ENOMEM;
	}
	(*build_buffer_ptr)->last_update = (time_t) uint32_time;
	(*build_buffer_ptr)->raw_buffer_ptr = buffer;
	(*build_buffer_ptr)->build_table_ptr = build_ptr;
	return 0;
}
