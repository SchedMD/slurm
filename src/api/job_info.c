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
#include <syslog.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "slurm.h"
#include "slurmlib.h"

char *job_api_buffer = NULL;
int job_api_buffer_size = 0;

#if DEBUG_MODULE
/* main is used here for testing purposes only */
main (int argc, char *argv[]) {
	static time_t last_update_time = (time_t) NULL;
	int error_code, size, i;
	char *dump;
	int dump_size;
	time_t update_time;
	char req_name[MAX_ID_LEN];	/* name of the job_id */
	char next_name[MAX_ID_LEN];	/* name of the next job_id */
	char job_name[MAX_NAME_LEN], partition[MAX_NAME_LEN];
	char job_state[MAX_NAME_LEN], node_list[FEATURE_SIZE];
	int time_limit, user_id;
	time_t start_time, end_time;
	float priority;

	error_code = load_job (&last_update_time);
	if (error_code)
		printf ("load_job error %d\n", error_code);

	strcpy (req_name, "");	/* start at beginning of job list */
	for (i = 1;; i++) {
		error_code =
			load_job_config (req_name, next_name, job_name,
				partition, &user_id, job_state, node_list, 
				&time_limit, &start_time, &end_time, &priority);

		if (error_code != 0) {
			printf ("load_job_config error %d on %s\n",
				error_code, req_name);
			break;
		}		
		if ((i < 10) || (i % 10 == 0)) {
			printf ("found JobId=%s JobName=%s Partition=%s ", 
				req_name, job_name, partition);
			printf ("user_id=%d job_state=%s node_list=%s ", 
				user_id, job_state, node_list);
			printf ("time_limit=%d priority=%f ", 
				time_limit, priority);
			printf ("start_time=%lx end_time=%lx\n", 
				(long)start_time, (long)end_time);
		}
		else if ((i==10) || (i % 10 == 1))
			printf ("skipping...\n");

		if (strlen (next_name) == 0)
			break;
		strcpy (req_name, next_name);
	}			
	free_job_info ();
	exit (0);
}
#endif


/*
 * free_job_info - free the job information buffer (if allocated)
 * NOTE: buffer is loaded by load_job and used by load_job_name.
 */
void
free_job_info (void)
{
	if (job_api_buffer)
		free (job_api_buffer);
}


/*
 * load_job - load the supplied job information buffer for use by info gathering 
 *	APIs if job records have changed since the time specified. 
 * input: buffer - pointer to job information buffer
 *        buffer_size - size of buffer
 * output: returns 0 if no error, EINVAL if the buffer is invalid,
 *		 ENOMEM if malloc failure
 * NOTE: buffer is used by load_job_config and freed by free_job_info.
 */
int
load_job (time_t * last_update_time) {
	int buffer_offset, buffer_size, error_code, in_size, version;
	char request_msg[64], *buffer, *my_line;
	int sockfd;
	struct sockaddr_in serv_addr;
	unsigned long my_time;

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
		 (long) (*last_update_time));
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
		return 0;
	}			

	/* load buffer's header (data structure version and time) */
	buffer_offset = 0;
	error_code =
		read_buffer (buffer, &buffer_offset, buffer_size, &my_line);
	if ((error_code) || (strlen (my_line) < strlen (HEAD_FORMAT))) {
#if DEBUG_SYSTEM
		fprintf (stderr,
			 "load_job: job buffer lacks valid header\n");
#else
		syslog (LOG_ERR,
			"load_job: job buffer lacks valid header\n");
#endif
		free (buffer);
		return EINVAL;
	}			
	sscanf (my_line, HEAD_FORMAT, &my_time, &version);

	if (version != JOB_STRUCT_VERSION) {
#if DEBUG_SYSTEM
		fprintf (stderr, "load_part: expect version %d, read %d\n",
			 NODE_STRUCT_VERSION, version);
#else
		syslog (LOG_ERR, "load_part: expect version %d, read %d\n",
			NODE_STRUCT_VERSION, version);
#endif
		free (buffer);
		return EINVAL;
	}			

	*last_update_time = (time_t) my_time;
	job_api_buffer = buffer;
	job_api_buffer_size = buffer_size;
	return 0;
}


/* 
 * load_job_config - load the state information about the named job
 * input: req_name - job_id of the job for which information is requested
 *		     if "", then get info for the first job in list
 *        next_name - location into which the name of the next job_id is 
 *                   stored, "" if no more
 *        job_name, etc. - pointers into which the information is to be stored
 * output: next_name - job_id of the next job in the list
 *         job_name, etc. - the job's state information
 *         returns 0 on success, ENOENT if not found, or EINVAL if buffer is bad
 * NOTE:  req_name and next_name must be declared by the caller and 
 *		have length MAX_ID_LEN or larger
 * NOTE:  job_name, partition, and job_state must be declared by the caller and 
 *		have length MAX_NAME_LEN or larger
 * NOTE:  node_list must be declared by the caller and 
 *		have length FEATURE_SIZE or larger (NOT SUFFICIENT, TEMPORARY USE ONLY)
 * NOTE: buffer is loaded by load_job and freed by free_job_info.
 */
int
load_job_config (char *req_name, char *next_name, char *job_name,
		char *partition, int *user_id, char *job_state, 
		char *node_list, int *time_limit, time_t *start_time, 
		time_t *end_time, float *priority)

{
	int i, error_code, version, buffer_offset, my_user_id;
	static time_t last_update_time, update_time;
	struct job_record my_job;
	static char next_job_id_value[MAX_ID_LEN];
	static last_buffer_offset;
	char my_job_id[MAX_ID_LEN], *my_line;
	unsigned long my_time;
	long my_start_time, my_end_time;

	/* load buffer's header (data structure version and time) */
	buffer_offset = 0;
	error_code =
		read_buffer (job_api_buffer, &buffer_offset,
			     job_api_buffer_size, &my_line);
	if (error_code)
		return error_code;
	sscanf (my_line, HEAD_FORMAT, &my_time, &version);
	update_time = (time_t) my_time;

	if ((update_time == last_update_time)
	    && (strcmp (req_name, next_job_id_value) == 0)
	    && (strlen (req_name) != 0))
		buffer_offset = last_buffer_offset;
	last_update_time = update_time;

	while (1) {
		/* load all information for next job */
		error_code =
			read_buffer (job_api_buffer, &buffer_offset,
				     job_api_buffer_size, &my_line);
		if (error_code == EFAULT)
			break;	/* end of buffer */
		if (error_code)
			return error_code;
		sscanf (my_line, JOB_STRUCT_FORMAT1,
			 my_job_id, 
			 partition, 
			 job_name, 
			 &my_user_id, 
			 node_list, 
			 job_state, 
			 &my_job.time_limit, 
			 &my_start_time, 
			 &my_end_time, 
			 &my_job.priority);
		if (strlen (req_name) == 0)
			strncpy (req_name, my_job_id, MAX_ID_LEN);

		/* check if this is requested job */
		if (strcmp (req_name, my_job_id) != 0)
			continue;

		/*load values to be returned */
		*user_id = my_user_id;
		*time_limit = my_job.time_limit;
		*start_time = (time_t) my_start_time;
		*end_time = (time_t) my_end_time;
		*priority = my_job.priority;

		last_buffer_offset = buffer_offset;
		error_code =
			read_buffer (job_api_buffer, &buffer_offset,
				     job_api_buffer_size, &my_line);
		if (error_code) {	/* no more records */
			strcpy (next_job_id_value, "");
			strcpy (next_name, "");
		}
		else {
			sscanf (my_line, "JobId=%s", my_job_id);
			strncpy (next_job_id_value, my_job_id, MAX_ID_LEN);
			strncpy (next_name, my_job_id, MAX_ID_LEN);
		}
		return 0;
	}			
	return ENOENT;
}
