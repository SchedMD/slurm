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
#include "bits_bytes.h"

#if DEBUG_MODULE
/* main is used here for module testing purposes only */
int
main (int argc, char *argv[]) 
{
	static time_t last_update_time = (time_t) NULL;
	int error_code, i, j;
	struct part_buffer *part_buffer_ptr = NULL;
	struct part_table *part_ptr;

	error_code = slurm_load_part (last_update_time, &part_buffer_ptr);
	if (error_code) {
		printf ("slurm_load_part error %d\n", error_code);
		exit (error_code);
	}

	printf("Updated at %lx, record count %d\n",
		part_buffer_ptr->last_update, part_buffer_ptr->part_count);
	part_ptr = part_buffer_ptr->part_table_ptr;

	for (i = 0; i < part_buffer_ptr->part_count; i++) {
			printf ("PartitionName=%s MaxTime=%u ", 
				part_ptr[i].name, part_ptr[i].max_time);
			printf ("MaxNodes=%u TotalNodes=%u ", 
				part_ptr[i].max_nodes, part_ptr[i].total_nodes);
			printf ("TotalCPUs=%u Key=%u\n", 
				part_ptr[i].total_cpus, part_ptr[i].key);
			printf ("   Default=%u ", 
				part_ptr[i].default_part);
			printf ("Shared=%u StateUp=%u ", 
				part_ptr[i].shared, part_ptr[i].state_up);
			printf ("Nodes=%s AllowGroups=%s\n", 
				part_ptr[i].nodes, part_ptr[i].allow_groups);
			printf ("   NodeIndecies=");
			for (j = 0; part_ptr[i].node_inx; j++) {
				if (j > 0)
					printf(",%d", part_ptr[i].node_inx[j]);
				else
					printf("%d", part_ptr[i].node_inx[j]);
				if (part_ptr[i].node_inx[j] == -1)
					break;
			}
			printf("\n\n");
	}
	slurm_free_part_info (part_buffer_ptr);
	exit (0);
}
#endif


/*
 * slurm_free_part_info - free the partition information buffer (if allocated)
 * NOTE: buffer is loaded by load_part.
 */
void
slurm_free_part_info (struct part_buffer *part_buffer_ptr)
{
	int i;

	if (part_buffer_ptr == NULL)
		return;
	if (part_buffer_ptr->raw_buffer_ptr)
		free (part_buffer_ptr->raw_buffer_ptr);
	if (part_buffer_ptr->part_table_ptr) {
		for (i = 0; i < part_buffer_ptr->part_count; i++) {
			if (part_buffer_ptr->part_table_ptr[i].node_inx == NULL)
				continue;
			free (part_buffer_ptr->part_table_ptr[i].node_inx);
		}
		free (part_buffer_ptr->part_table_ptr);
	}
}



/*
 * slurm_load_part - load the supplied partition information buffer for use by info 
 *	gathering APIs if partition records have changed since the time specified. 
 * input: update_time - time of last update
 *	part_buffer_ptr - place to park part_buffer pointer
 * output: part_buffer_ptr - pointer to allocated part_buffer
 *	returns -1 if no update since update_time, 
 *		0 if update with no error, 
 *		EINVAL if the buffer (version or otherwise) is invalid, 
 *		ENOMEM if malloc failure
 * NOTE: the allocated memory at part_buffer_ptr freed by slurm_free_part_info.
 */
int
slurm_load_part (time_t update_time, struct part_buffer **part_buffer_ptr)
{
	int buffer_offset, buffer_size, in_size, i, sockfd;
	char request_msg[64], *buffer, *node_inx_str;
	void *buf_ptr;
	struct sockaddr_in serv_addr;
	uint16_t uint16_tmp;
	uint32_t uint32_tmp, uint32_time;
	struct part_table *part;

	*part_buffer_ptr = NULL;
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
	if (uint32_tmp != PART_STRUCT_VERSION) {
		free (buffer);
		return EINVAL;
	}
	unpack32 (&uint32_time, &buf_ptr, &buffer_size);

	/* load individual partition info */
	part = NULL;
	for (i = 0; buffer_size > 0; i++) {
		part = realloc (part, sizeof(struct part_table) * (i+1));
		if (part == NULL) {
			free (buffer);
			return ENOMEM;
		}
		unpackstr_ptr (&part[i].name, &uint16_tmp, 
			&buf_ptr, &buffer_size);
		if (part[i].name == NULL)
			part[i].name = "";
		unpack32  (&part[i].max_time, &buf_ptr, &buffer_size);
		unpack32  (&part[i].max_nodes, &buf_ptr, &buffer_size);
		unpack32  (&part[i].total_nodes, &buf_ptr, &buffer_size);

		unpack32  (&part[i].total_cpus, &buf_ptr, &buffer_size);
		unpack16  (&part[i].default_part, &buf_ptr, &buffer_size);
		unpack16  (&part[i].key, &buf_ptr, &buffer_size);
		unpack16  (&part[i].shared, &buf_ptr, &buffer_size);

		unpack16  (&part[i].state_up, &buf_ptr, &buffer_size);
		unpackstr_ptr (&part[i].allow_groups, &uint16_tmp, 
			&buf_ptr, &buffer_size);
		if (part[i].allow_groups == NULL)
			part[i].allow_groups = "";
		unpackstr_ptr (&part[i].nodes, &uint16_tmp, 
			&buf_ptr, &buffer_size);
		if (part[i].nodes == NULL)
			part[i].nodes = "";
		unpackstr_ptr (&node_inx_str, &uint16_tmp, 
			&buf_ptr, &buffer_size);
		if (node_inx_str == NULL)
			node_inx_str = "";
		part[i].node_inx = bitfmt2int(node_inx_str);
	}

	*part_buffer_ptr = malloc (sizeof (struct part_buffer));
	if (*part_buffer_ptr == NULL) {
		free (buffer);
		if (part) {
			int j;
			for (j = 0; j < i; j++) {
				if (part[j].node_inx)
					free (part[j].node_inx);
			}
			free (part);
		}
		return ENOMEM;
	}
	(*part_buffer_ptr)->last_update = (time_t) uint32_time;
	(*part_buffer_ptr)->part_count = i;
	(*part_buffer_ptr)->raw_buffer_ptr = buffer;
	(*part_buffer_ptr)->part_table_ptr = part;
	return 0;
}
