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
/* main is used here for testing purposes only */
int
main (int argc, char *argv[]) 
{
	static time_t last_update_time = (time_t) NULL;
	int error_code, i;
	struct node_buffer *node_buffer_ptr = NULL;
	struct node_table *node_ptr;

	error_code = slurm_load_node (last_update_time, &node_buffer_ptr);
	if (error_code) {
		printf ("slurm_load_node error %d\n", error_code);
		exit (error_code);
	}

	printf("Updated at %lx, record count %d\n",
		node_buffer_ptr->last_update, node_buffer_ptr->node_count);
	node_ptr = node_buffer_ptr->node_table_ptr;

	for (i = 0; i < node_buffer_ptr->node_count; i++) {
		if ((i < 10) || (i % 200 == 0) || 
		    ((i + 1)  == node_buffer_ptr->node_count)) {
			printf ("NodeName=%s CPUs=%u ", 
				node_ptr[i].name, node_ptr[i].cpus);
			printf ("RealMemory=%u TmpDisk=%u ", 
				node_ptr[i].real_memory, node_ptr[i].tmp_disk);
			printf ("State=%u Weight=%u ", 
				node_ptr[i].node_state, node_ptr[i].weight);
			printf ("Features=%s Partition=%s\n", 
				node_ptr[i].features, node_ptr[i].partition);
		}
		else if ((i==10) || (i % 200 == 1))
			printf ("skipping...\n");
	}			
	slurm_free_node_info (node_buffer_ptr);
	exit (0);
}
#endif


/*
 * slurm_free_node_info - free the node information buffer (if allocated)
 * NOTE: buffer is loaded by slurm_load_node.
 */
void
slurm_free_node_info (struct node_buffer *node_buffer_ptr)
{
	if (node_buffer_ptr == NULL)
		return;
	if (node_buffer_ptr->raw_buffer_ptr)
		free (node_buffer_ptr->raw_buffer_ptr);
	if (node_buffer_ptr->node_table_ptr)
		free (node_buffer_ptr->node_table_ptr);
}


/*
 * slurm_load_node - load the supplied node information buffer for use by info 
 *	gathering APIs if node records have changed since the time specified. 
 * input: update_time - time of last update
 *	node_buffer_ptr - place to park node_buffer pointer
 * output: node_buffer_ptr - pointer to allocated node_buffer
 *	returns -1 if no update since update_time, 
 *		0 if update with no error, 
 *		EINVAL if the buffer (version or otherwise) is invalid, 
 *		ENOMEM if malloc failure
 * NOTE: the allocated memory at node_buffer_ptr freed by slurm_free_node_info.
 */
int
slurm_load_node (time_t update_time, struct node_buffer **node_buffer_ptr)
{
	int buffer_offset, buffer_size, in_size, i, sockfd;
	char request_msg[64], *buffer;
	void *buf_ptr;
	struct sockaddr_in serv_addr;
	uint32_t uint32_tmp, uint32_time;
	uint16_t uint16_tmp;
	struct node_table *node;

	*node_buffer_ptr = NULL;
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
	if (uint32_tmp != NODE_STRUCT_VERSION) {
		free (buffer);
		return EINVAL;
	}
	unpack32 (&uint32_time, &buf_ptr, &buffer_size);

	/* load individual node info */
	node = NULL;
	for (i = 0; buffer_size > 0; i++) {
		node = realloc (node, sizeof(struct node_table) * (i+1));
		if (node == NULL) {
			free (buffer);
			return ENOMEM;
		}
		unpackstr_ptr (&node[i].name, &uint16_tmp, &buf_ptr, &buffer_size);
		unpack32  (&node[i].node_state, &buf_ptr, &buffer_size);
		unpack32  (&node[i].cpus, &buf_ptr, &buffer_size);
		unpack32  (&node[i].real_memory, &buf_ptr, &buffer_size);
		unpack32  (&node[i].tmp_disk, &buf_ptr, &buffer_size);
		unpack32  (&node[i].weight, &buf_ptr, &buffer_size);
		unpackstr_ptr (&node[i].features, &uint16_tmp, 
			&buf_ptr, &buffer_size);
		unpackstr_ptr (&node[i].partition, &uint16_tmp,
			&buf_ptr, &buffer_size);
	}

	*node_buffer_ptr = malloc (sizeof (struct node_buffer));
	if (*node_buffer_ptr == NULL) {
		free (buffer);
		free (node);
		return ENOMEM;
	}
	(*node_buffer_ptr)->last_update = (time_t) uint32_time;
	(*node_buffer_ptr)->node_count = i;
	(*node_buffer_ptr)->raw_buffer_ptr = buffer;
	(*node_buffer_ptr)->node_table_ptr = node;
	return 0;
}
