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

#include "slurm.h"
#include "slurmlib.h"

char *part_api_buffer = NULL;
int part_api_buffer_size = 0;

#if DEBUG_MODULE
/* main is used here for module testing purposes only */
main (int argc, char *argv[]) {
	static time_t last_update_time = (time_t) NULL;
	int error_code;
	char req_name[MAX_NAME_LEN];	/* name of the partition */
	char next_name[MAX_NAME_LEN];	/* name of the next partition */
	int max_time;		/* -1 if unlimited */
	int max_nodes;		/* -1 if unlimited */
	int total_nodes;	/* total number of nodes in the partition */
	int total_cpus;		/* total number of cpus in the partition */
	char nodes[FEATURE_SIZE];	/* names of nodes in partition */
	char allow_groups[FEATURE_SIZE];	/* NULL indicates all */
	int key;		/* 1 if slurm distributed key is required for use of partition */
	int state_up;		/* 1 if state is up */
	int shared;		/* 1 if partition can be shared */
	int default_flag;	/* 1 if default partition */

	error_code = load_part (&last_update_time);
	if (error_code) {
		printf ("load_part error %d\n", error_code);
		exit (1);
	}			
	strcpy (req_name, "");	/* start at beginning of partition list */
	while (error_code == 0) {
		error_code =
			load_part_name (req_name, next_name, &max_time,
					&max_nodes, &total_nodes, &total_cpus,
					&key, &state_up, &shared, &default_flag,
					nodes, allow_groups);
		if (error_code != 0) {
			printf ("load_part_name error %d finding %s\n",
				error_code, req_name);
			break;
		}		

		printf ("found partition NodeName=%s Nodes=%s MaxTime=%d MaxNodes=%d\n", 
			req_name, nodes, max_time, max_nodes, default_flag);
		printf ("  Default=%d TotalNodes=%d TotalCPUs=%d Key=%d StateUp=%d\n", 
			default_flag, total_nodes, total_cpus, key, state_up);
		printf ("  Shared=%d AllowGroups=%s\n", shared, allow_groups);
		if (strlen (next_name) == 0)
			break;
		strcpy (req_name, next_name);
	}			
	free_part_info ();
	exit (0);
}
#endif


/*
 * free_part_info - free the partition information buffer (if allocated)
 * NOTE: buffer is loaded by load_part and used by load_part_name.
 */
void
free_part_info (void) {
	if (part_api_buffer)
		free (part_api_buffer);
}


/*
 * load_part - update the partition information buffer for use by info gathering apis if 
 *	partition records have changed since the time specified. 
 * input: last_update_time - pointer to time of last buffer
 * output: last_update_time - time reset if buffer is updated
 *         returns 0 if no error, EINVAL if the buffer is invalid, ENOMEM if malloc failure
 * NOTE: buffer is used by load_part_name and free by free_part_info.
 */
int
load_part (time_t * last_update_time) {
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
	sprintf (request_msg, "DumpPart LastUpdate=%lu",
		 (long) (*last_update_time));
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
	if (strcmp (buffer, "nochange") == 0) {
		free (buffer);
		return 0;
	}			

	buffer_offset = 0;
	error_code =
		read_buffer (buffer, &buffer_offset, buffer_size, &my_line);
	if ((error_code) || (strlen (my_line) < strlen (HEAD_FORMAT))) {
#if DEBUG_SYSTEM
		fprintf (stderr,
			 "load_part: node buffer lacks valid header\n");
#else
		syslog (LOG_ERR,
			"load_part: node buffer lacks valid header\n");
#endif
		free (buffer);
		return EINVAL;
	}			
	sscanf (my_line, HEAD_FORMAT, &my_time, &version);
	if (version != PART_STRUCT_VERSION) {
#if DEBUG_SYSTEM
		fprintf (stderr, "load_part: expect version %d, read %d\n",
			 PART_STRUCT_VERSION, version);
#else
		syslog (LOG_ERR, "load_part: expect version %d, read %d\n",
			PART_STRUCT_VERSION, version);
#endif
		free (buffer);
		return EINVAL;
	}			

	*last_update_time = (time_t) my_time;
	part_api_buffer = buffer;
	part_api_buffer_size = buffer_size;
	return 0;
}


/* 
 * load_part_name - load the state information about the named partition
 * input: req_name - name of the partition for which information is requested
 *		     if "", then get info for the first partition in list
 *        next_name - location into which the name of the next partition is 
 *                   stored, "" if no more
 *        max_time, etc. - pointers into which the information is to be stored
 * output: req_name - the partition's name is stored here
 *         next_name - the name of the next partition in the list is stored here
 *         max_time, etc. - the partition's state information
 *         returns 0 on success, ENOENT if not found, or EINVAL if buffer is bad
 * NOTE:  req_name and next_name must be declared by caller with have length MAX_NAME_LEN or larger.
 *        nodes and allow_groups must be declared by caller with length of FEATURE_SIZE or larger.
 * NOTE: buffer is loaded by load_part and free by free_part_info.
 */
int
load_part_name (char *req_name, char *next_name, int *max_time,
		int *max_nodes, int *total_nodes, int *total_cpus, int *key,
		int *state_up, int *shared, int *default_flag, char *nodes,
		char *allow_groups) {
	int i, error_code, version, buffer_offset;
	static time_t last_update_time, update_time;
	struct part_record my_part;
	static char next_name_value[MAX_NAME_LEN];
	char my_part_name[MAX_NAME_LEN], my_key[MAX_NAME_LEN],
		my_default[MAX_NAME_LEN];
	char my_shared[MAX_NAME_LEN], my_state[MAX_NAME_LEN], *my_line;
	static last_buffer_offset;
	unsigned long my_time;

	/* load buffer's header (data structure version and time) */
	buffer_offset = 0;
	error_code =
		read_buffer (part_api_buffer, &buffer_offset,
			     part_api_buffer_size, &my_line);
	if (error_code)
		return error_code;
	sscanf (my_line, HEAD_FORMAT, &my_time, &version);
	update_time = (time_t) my_time;

	if ((update_time == last_update_time)
	    && (strcmp (req_name, next_name_value) == 0)
	    && (strlen (req_name) != 0))
		buffer_offset = last_buffer_offset;
	last_update_time = update_time;

	while (1) {
		/* load all info for next partition */
		error_code =
			read_buffer (part_api_buffer, &buffer_offset,
				     part_api_buffer_size, &my_line);
		if (error_code == EFAULT)
			break;	/* end of buffer */
		if (error_code)
			return error_code;

		sscanf (my_line, PART_STRUCT_FORMAT,
			my_part_name,
			&my_part.max_nodes,
			&my_part.max_time,
			nodes,
			my_key,
			my_default,
			allow_groups,
			my_shared,
			my_state, &my_part.total_nodes, &my_part.total_cpus);

		if (strlen (req_name) == 0)
			strcpy (req_name, my_part_name);

		/* check if this is requested partition */
		if (strcmp (req_name, my_part_name) != 0)
			continue;

		/*load values to be returned */
		*max_time = my_part.max_time;
		*max_nodes = my_part.max_nodes;
		*total_nodes = my_part.total_nodes;
		*total_cpus = my_part.total_cpus;
		if (strcmp (my_key, "YES") == 0)
			*key = 1;
		else
			*key = 0;
		if (strcmp (my_default, "YES") == 0)
			*default_flag = 1;
		else
			*default_flag = 0;
		if (strcmp (my_state, "UP") == 0)
			*state_up = 1;
		else
			*state_up = 0;
		if (strcmp (my_shared, "YES") == 0)
			*shared = 1;
		else if (strcmp (my_shared, "NO") == 0)
			*shared = 0;
		else	/* FORCE */
			*shared = 2;

		last_buffer_offset = buffer_offset;
		error_code =
			read_buffer (part_api_buffer, &buffer_offset,
				     part_api_buffer_size, &my_line);
		if (error_code) {	/* no more records */
			strcpy (next_name_value, "");
			strcpy (next_name, "");
		}
		else {
			sscanf (my_line, "PartitionName=%s", my_part_name);
			strncpy (next_name_value, my_part_name, MAX_NAME_LEN);
			strncpy (next_name, my_part_name, MAX_NAME_LEN);
		}		/* else */
		return 0;
	}			
	return ENOENT;
}
