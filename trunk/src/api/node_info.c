/* 
 * node_info.c - get the node records of slurm
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
#include <arpa/inet.h>
#include <unistd.h>

#include "slurm.h"
#include "slurmlib.h"

char *node_api_buffer = NULL;
int node_api_buffer_size = 0;

#if DEBUG_MODULE
/* main is used here for testing purposes only */
int
main (int argc, char *argv[]) {
	static time_t last_update_time = (time_t) NULL;
	int error_code, i;
	char partition[MAX_NAME_LEN], node_state[MAX_NAME_LEN],
		features[FEATURE_SIZE];
	char req_name[MAX_NAME_LEN];	/* name of the partition */
	char next_name[MAX_NAME_LEN];	/* name of the next partition */
	int cpus, real_memory, tmp_disk, weight;

	error_code = load_node (&last_update_time);
	if (error_code)
		printf ("load_node error %d\n", error_code);

	strcpy (req_name, "");	/* start at beginning of node list */
	for (i = 1;; i++) {
		error_code =
			load_node_config (req_name, next_name, &cpus,
					  &real_memory, &tmp_disk, &weight,
					  features, partition, node_state);
		if (error_code != 0) {
			printf ("load_node_config error %d on %s\n",
				error_code, req_name);
			exit (1);
		}		
		if ((i < 10) || (i % 100 == 0)) {
			printf ("found NodeName=%s CPUs=%d RealMemory=%d TmpDisk=%d ", 
				req_name, cpus, real_memory, tmp_disk);
			printf ("State=%s Weight=%d Features=%s Partition=%s\n", 
				node_state, weight, features, partition);
		}
		else if ((i==10) || (i % 100 == 1))
			printf ("skipping...\n");

		if (strlen (next_name) == 0)
			break;
		strcpy (req_name, next_name);
	}			
	free_node_info ();
	exit (0);
}
#endif


/*
 * free_node_info - free the node information buffer (if allocated)
 * NOTE: buffer is loaded by load_node and used by load_node_name.
 */
void
free_node_info (void)
{
	if (node_api_buffer)
		free (node_api_buffer);
}


/*
 * load_node - load the supplied node information buffer for use by info gathering
 *	APIs if node records have changed since the time specified. 
 * input: buffer - pointer to node information buffer
 *        buffer_size - size of buffer
 * output: returns 0 if no error, EINVAL if the buffer is invalid, 
 *		ENOMEM if malloc failure
 * NOTE: buffer is used by load_node_config and freed by free_node_info.
 */
int
load_node (time_t * last_update_time) {
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
	sprintf (request_msg, "DumpNode LastUpdate=%lu",
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
			 "load_node: node buffer lacks valid header\n");
#else
		syslog (LOG_ERR,
			"load_node: node buffer lacks valid header\n");
#endif
		free (buffer);
		return EINVAL;
	}			
	sscanf (my_line, HEAD_FORMAT, &my_time, &version);

	if (version != NODE_STRUCT_VERSION) {
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
	node_api_buffer = buffer;
	node_api_buffer_size = buffer_size;
	return 0;
}


/* 
 * load_node_config - load the state information about the named node
 * input: req_name - name of the node for which information is requested
 *		     if "", then get info for the first node in list
 *        next_name - location into which the name of the next node is 
 *                   stored, "" if no more
 *        cpus, etc. - pointers into which the information is to be stored
 * output: next_name - name of the next node in the list
 *         cpus, etc. - the node's state information
 *         returns 0 on success, ENOENT if not found, or EINVAL if buffer is bad
 * NOTE:  req_name, next_name, partition, and node_state must be declared by the 
 *        caller and have length MAX_NAME_LEN or larger
 *        features must be declared by the caller and have length FEATURE_SIZE or larger.
 * NOTE: buffer is loaded by load_node and freed by free_node_info.
 */
int
load_node_config (char *req_name, char *next_name, int *cpus,
		  int *real_memory, int *tmp_disk, int *weight,
		  char *features, char *partition, char *node_state) {
	int error_code, version, buffer_offset, my_weight;
	static time_t last_update_time, update_time;
	struct node_record my_node;
	static char next_name_value[MAX_NAME_LEN];
	static int last_buffer_offset;
	char my_node_name[MAX_NAME_LEN], *my_line;
	unsigned long my_time;

	/* load buffer's header (data structure version and time) */
	buffer_offset = 0;
	error_code =
		read_buffer (node_api_buffer, &buffer_offset,
			     node_api_buffer_size, &my_line);
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
		/* load all information for next node */
		error_code =
			read_buffer (node_api_buffer, &buffer_offset,
				     node_api_buffer_size, &my_line);
		if (error_code == EFAULT)
			break;	/* end of buffer */
		if (error_code)
			return error_code;
		sscanf (my_line, NODE_STRUCT_FORMAT,
			my_node_name,
			node_state,
			&my_node.cpus,
			&my_node.real_memory,
			&my_node.tmp_disk, &my_weight, features, partition);
		if (strlen (req_name) == 0)
			strncpy (req_name, my_node_name, MAX_NAME_LEN);

		/* check if this is requested node */
		if (strcmp (req_name, my_node_name) != 0)
			continue;

		/*load values to be returned */
		*cpus = my_node.cpus;
		*real_memory = my_node.real_memory;
		*tmp_disk = my_node.tmp_disk;
		*weight = my_weight;

		last_buffer_offset = buffer_offset;
		error_code =
			read_buffer (node_api_buffer, &buffer_offset,
				     node_api_buffer_size, &my_line);
		if (error_code) {	/* no more records */
			strcpy (next_name_value, "");
			strcpy (next_name, "");
		}
		else {
			sscanf (my_line, "NodeName=%s", my_node_name);
			strncpy (next_name_value, my_node_name, MAX_NAME_LEN);
			strncpy (next_name, my_node_name, MAX_NAME_LEN);
		}
		return 0;
	}			
	return ENOENT;
}
