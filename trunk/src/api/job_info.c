/* 
 * job_info.c - get the job records of slurm
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

#include "pack.h"
#include "slurmlib.h"

#if DEBUG_MODULE
/* main is used here for testing purposes only */
int 
main (int argc, char *argv[]) 
{
	static time_t last_update_time = (time_t) NULL;
	int error_code, i;
	struct job_buffer *job_buffer_ptr = NULL;
	struct job_table *job_ptr;

	error_code = slurm_load_job (last_update_time, &job_buffer_ptr);
	if (error_code) {
		printf ("slurm_load_job error %d\n", error_code);
		exit (error_code);
	}

	printf("Updated at %lx, record count %d\n",
		job_buffer_ptr->last_update, job_buffer_ptr->job_count);
	job_ptr = job_buffer_ptr->job_table_ptr;

	for (i = 0; i < job_buffer_ptr->job_count; i++) {
			printf ("JobId=%s UserId=%u ", 
				job_ptr[i].job_id, job_ptr[i].user_id);
			printf ("JobState=%u TimeLimit=%u ", 
				job_ptr[i].job_state, job_ptr[i].time_limit);
			printf ("Priority=%u Partition=%s\n", 
				job_ptr[i].priority, job_ptr[i].partition);

			printf ("   Name=%s Nodes=%s ", 
				job_ptr[i].name, job_ptr[i].nodes);
			printf ("StartTime=%x EndTime=%x\n", 
				(uint32_t) job_ptr[i].start_time, 
				(uint32_t) job_ptr[i].end_time);

			printf ("   ReqProcs=%u ReqNodes=%u ",
				job_ptr[i].num_procs, job_ptr[i].num_nodes);
			printf ("Shared=%u Contiguous=%u\n",
				job_ptr[i].shared, job_ptr[i].contiguous);

			printf ("   MinProcs=%u MinMemory=%u ",
				job_ptr[i].min_procs, job_ptr[i].min_memory);
			printf ("MinTmpDisk=%u TotalProcs=%u\n",
				job_ptr[i].min_tmp_disk, job_ptr[i].total_procs);

			printf ("   ReqNodes=%s Features=%s ",
				job_ptr[i].req_nodes, job_ptr[i].features);
			printf ("JobScript=%s\n\n",
				job_ptr[i].job_script);

	}			
	slurm_free_job_info (job_buffer_ptr);
	exit (0);
}
#endif


/*
 * slurm_free_job_info - free the job information buffer (if allocated)
 * NOTE: buffer is loaded by load_job.
 */
void
slurm_free_job_info (struct job_buffer *job_buffer_ptr)
{
	if (job_buffer_ptr == NULL)
		return;
	if (job_buffer_ptr->raw_buffer_ptr)
		free (job_buffer_ptr->raw_buffer_ptr);
	if (job_buffer_ptr->job_table_ptr)
		free (job_buffer_ptr->job_table_ptr);
}


/*
 * slurm_load_job - load the supplied job information buffer for use by info 
 *	gathering APIs if job records have changed since the time specified. 
 * input: update_time - time of last update
 *	job_buffer_ptr - place to park job_buffer pointer
 * output: job_buffer_ptr - pointer to allocated job_buffer
 *	returns -1 if no update since update_time, 
 *		0 if update with no error, 
 *		EINVAL if the buffer (version or otherwise) is invalid, 
 *		ENOMEM if malloc failure
 * NOTE: the allocated memory at job_buffer_ptr freed by slurm_free_job_info.
 */
int
slurm_load_job (time_t update_time, struct job_buffer **job_buffer_ptr)
{
	int buffer_offset, buffer_size, in_size, i, sockfd;
	char request_msg[64], *buffer;
	void *buf_ptr;
	struct sockaddr_in serv_addr;
	uint16_t uint16_tmp;
	uint32_t uint32_tmp, uint32_time;
	struct job_table *job;

	*job_buffer_ptr = NULL;
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
	sprintf (request_msg, "DumpJob LastUpdate=%lu",
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
	if (uint32_tmp != JOB_STRUCT_VERSION) {
		free (buffer);
		return EINVAL;
	}
	unpack32 (&uint32_time, &buf_ptr, &buffer_size);

	/* load individual job info */
	job = NULL;
	for (i = 0; buffer_size > 0; i++) {
		job = realloc (job, sizeof(struct job_table) * (i+1));
		if (job == NULL) {
			free (buffer);
			return ENOMEM;
		}
		unpackstr_ptr (&job[i].job_id, &uint16_tmp, 
			&buf_ptr, &buffer_size);
		unpack32  (&job[i].user_id, &buf_ptr, &buffer_size);
		unpack16  (&job[i].job_state, &buf_ptr, &buffer_size);
		unpack32  (&job[i].time_limit, &buf_ptr, &buffer_size);

		unpack32  (&uint32_tmp, &buf_ptr, &buffer_size);
		job[i].start_time = (time_t) uint32_tmp;
		unpack32  (&uint32_tmp, &buf_ptr, &buffer_size);
		job[i].end_time = (time_t) uint32_tmp;
		unpack32  (&job[i].priority, &buf_ptr, &buffer_size);

		unpackstr_ptr (&job[i].nodes, &uint16_tmp, 
			&buf_ptr, &buffer_size);
		unpackstr_ptr (&job[i].partition, &uint16_tmp, 
			&buf_ptr, &buffer_size);
		unpackstr_ptr (&job[i].name, &uint16_tmp, 
			&buf_ptr, &buffer_size);

		unpack32  (&job[i].num_procs, &buf_ptr, &buffer_size);
		unpack32  (&job[i].num_nodes, &buf_ptr, &buffer_size);
		unpack16  (&job[i].shared, &buf_ptr, &buffer_size);
		unpack16  (&job[i].contiguous, &buf_ptr, &buffer_size);

		unpack32  (&job[i].min_procs, &buf_ptr, &buffer_size);
		unpack32  (&job[i].min_memory, &buf_ptr, &buffer_size);
		unpack32  (&job[i].min_tmp_disk, &buf_ptr, &buffer_size);
		unpack32  (&job[i].total_procs, &buf_ptr, &buffer_size);

		unpackstr_ptr (&job[i].req_nodes, &uint16_tmp, 
			&buf_ptr, &buffer_size);
		unpackstr_ptr (&job[i].features, &uint16_tmp, 
			&buf_ptr, &buffer_size);
		unpackstr_ptr (&job[i].job_script, &uint16_tmp, 
			&buf_ptr, &buffer_size);
	}

	*job_buffer_ptr = malloc (sizeof (struct job_buffer));
	if (*job_buffer_ptr == NULL) {
		free (buffer);
		free (job);
		return ENOMEM;
	}
	(*job_buffer_ptr)->last_update = (time_t) uint32_time;
	(*job_buffer_ptr)->job_count = i;
	(*job_buffer_ptr)->raw_buffer_ptr = buffer;
	(*job_buffer_ptr)->job_table_ptr = job;
	return 0;
}
