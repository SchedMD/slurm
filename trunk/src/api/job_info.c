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
#include "slurm.h"
#include "nodelist.h"

#include <src/common/slurm_protocol_api.h>
void slurm_print_job_table (struct job_table * job_ptr );
int unpack_job_table ( job_table_t * job , void ** buf_ptr , int * buffer_size ) ;

#if DEBUG_MODULE
/* main is used here for testing purposes only */
int 
main (int argc, char *argv[]) 
{
	static time_t last_update_time = (time_t) NULL;
	int error_code, i, j;
	struct job_buffer *job_buffer_ptr = NULL;
	struct job_table *job_ptr = NULL;

	error_code = slurm_load_job (last_update_time, &job_buffer_ptr);
	if (error_code) {
		printf ("slurm_load_job error %d\n", error_code);
		exit (error_code);
	}

	printf("Jobs updated at %lx, record count %d\n",
		job_buffer_ptr->last_update, job_buffer_ptr->job_count);
	job_ptr = job_buffer_ptr->job_table_ptr;

	slurm_print_job_info ( job_ptr ) ;

	slurm_free_job_info (job_buffer_ptr);
	exit (0);
}
#endif

void 
slurm_print_job_info_msg ( job_info_msg_t * job_info_msg_ptr )
{
	int i;
	job_table_t * job_ptr = job_info_msg_ptr -> job_array ;
	for (i = 0; i < job_info_msg_ptr-> record_count; i++) 
	{
		slurm_print_job_table ( & job_ptr[i] ) ;
	}
}

void
slurm_print_job_table (struct job_table * job_ptr )
{
	int j;
	printf ("JobId=%u UserId=%u ", job_ptr->job_id, job_ptr->user_id);
	printf ("JobState=%u TimeLimit=%u ", job_ptr->job_state, job_ptr->time_limit);
	printf ("Priority=%u Partition=%s\n", job_ptr->priority, job_ptr->partition);
	printf ("   Name=%s NodeList=%s ", job_ptr->name, job_ptr->nodes);
	printf ("StartTime=%x EndTime=%x\n", (uint32_t) job_ptr->start_time, (uint32_t) job_ptr->end_time);

	printf ("   NodeListIndecies=");
	for (j = 0; job_ptr->node_inx; j++) {
		if (j > 0)
			printf(",%d", job_ptr->node_inx[j]);
		else
			printf("%d", job_ptr->node_inx[j]);
		if (job_ptr->node_inx[j] == -1)
			break;
	}
	printf("\n");

	printf ("   ReqProcs=%u ReqNodes=%u ", job_ptr->num_procs, job_ptr->num_nodes);
	printf ("Shared=%u Contiguous=%u ", job_ptr->shared, job_ptr->contiguous);
	printf ("MinProcs=%u MinMemory=%u ", job_ptr->min_procs, job_ptr->min_memory);
	printf ("MinTmpDisk=%u\n", job_ptr->min_tmp_disk);
	printf ("   ReqNodeList=%s Features=%s ", job_ptr->req_nodes, job_ptr->features);
	printf ("JobScript=%s\n", job_ptr->job_script);
	printf ("   ReqNodeListIndecies=");
	for (j = 0; job_ptr->req_node_inx; j++) {
		if (j > 0)
			printf(",%d", job_ptr->req_node_inx[j]);
		else
			printf("%d", job_ptr->req_node_inx[j]);
		if (job_ptr->req_node_inx[j] == -1)
			break;
	}
	printf("\n\n");
}

/*
 * slurm_free_job_info - free the job information buffer (if allocated)
 * NOTE: buffer is loaded by load_job.
 */
void
slurm_free_job_table (job_table_t * job)
{
}

void
slurm_free_job_info (job_info_msg_t * job_info_ptr)
{
	int i;
	if (job_info_ptr)
	{
		if (job_info_ptr -> job_array ) {
			for (i = 0; i < job_info_ptr-> record_count; i++) {
				slurm_free_job_table ( & (job_info_ptr -> job_array[i] ) ) ;
			}
		}
		free (job_info_ptr);
	}
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
        int msg_size ;
        int rc ;
        slurm_fd sockfd ;
        slurm_msg_t request_msg ;
        slurm_msg_t response_msg ;
        last_update_msg_t last_time_msg ;

        /* init message connection for message communication with controller */
        if ( ( sockfd = slurm_open_controller_conn ( SLURM_PORT ) ) == SLURM_SOCKET_ERROR )
                return SLURM_SOCKET_ERROR ;

        /* send request message */
        /* pack32 ( update_time , &buf_ptr , &buffer_size ); */
        last_time_msg . last_update = update_time ;
        request_msg . msg_type = REQUEST_JOB_INFO ;
        request_msg . data = &last_time_msg ;
        if ( ( rc = slurm_send_controller_msg ( sockfd , & request_msg ) ) == SLURM_SOCKET_ERROR )
                return SLURM_SOCKET_ERROR ;

        /* receive message */
        if ( ( msg_size = slurm_receive_msg ( sockfd , & response_msg ) ) == SLURM_SOCKET_ERROR )
                return SLURM_SOCKET_ERROR ;
        /* shutdown message connection */
        if ( ( rc = slurm_shutdown_msg_conn ( sockfd ) ) == SLURM_SOCKET_ERROR )
                return SLURM_SOCKET_ERROR ;

	switch ( response_msg . msg_type )
	{
		case REQUEST_JOB_INFO:
        		/** *build_table_ptr = ( build_table_t * ) response_msg . data ; */
			break ;
		case RESPONSE_SLURM_RC:
			break ;
		default:
			return SLURM_UNEXPECTED_MSG_ERROR ;
			break ;
	}

        return SLURM_SUCCESS ;
}

int unpack_job_info_msg ( job_info_msg_t ** msg , void ** buf_ptr , int * buffer_size )
{
	int uint32_record_count ;
	int i;
	job_table_t *job;
	
	*msg = malloc ( sizeof ( job_info_msg_t ) );
	if ( *msg == NULL )
		return ENOMEM ;


	/* load buffer's header (data structure version and time) */
	unpack32 (&((*msg) -> last_update ) , buf_ptr, buffer_size);
	unpack32 (&((*msg) -> record_count), buf_ptr, buffer_size);

	job = (*msg) -> job_array = malloc ( sizeof ( job_table_t ) * uint32_record_count ) ;

	/* load individual job info */
	job = NULL;
	for (i = 0; buffer_size > 0; i++) {
	unpack_job_table ( & job[i] , buf_ptr , buffer_size ) ;

	}
	return 0;
}

int unpack_job_table_msg ( job_table_msg_t ** msg , void ** buf_ptr , int * buffer_size )
{
	*msg = malloc ( sizeof ( job_table_t ) ) ;
	if ( *msg == NULL )
		return ENOMEM ;
	unpack_job_table ( *msg , buf_ptr , buffer_size ) ;
	return 0 ;
}

int unpack_job_table ( job_table_t * job , void ** buf_ptr , int * buffer_size )
{
	uint16_t uint16_tmp;
	uint32_t uint32_tmp;
	char * node_inx_str;

	unpack32  (&job->job_id, buf_ptr, buffer_size);
	unpack32  (&job->user_id, buf_ptr, buffer_size);
	unpack16  (&job->job_state, buf_ptr, buffer_size);
	unpack32  (&job->time_limit, buf_ptr, buffer_size);

	unpack32  (&uint32_tmp, buf_ptr, buffer_size);
	job->start_time = (time_t) uint32_tmp;
	unpack32  (&uint32_tmp, buf_ptr, buffer_size);
	job->end_time = (time_t) uint32_tmp;
	unpack32  (&job->priority, buf_ptr, buffer_size);

	unpackstr_ptr_malloc (&job->nodes, &uint16_tmp, buf_ptr, buffer_size);
	if (job->nodes == NULL)
		job->nodes = "";
	unpackstr_ptr_malloc (&job->partition, &uint16_tmp, buf_ptr, buffer_size);
	if (job->partition == NULL)
		job->partition = "";
	unpackstr_ptr_malloc (&job->name, &uint16_tmp, buf_ptr, buffer_size);
	if (job->name == NULL)
		job->name = "";
	unpackstr_ptr (&node_inx_str, &uint16_tmp, buf_ptr, buffer_size);
	if (node_inx_str == NULL)
		node_inx_str = "";
	job->node_inx = bitfmt2int(node_inx_str);

	unpack32  (&job->num_procs, buf_ptr, buffer_size);
	unpack32  (&job->num_nodes, buf_ptr, buffer_size);
	unpack16  (&job->shared, buf_ptr, buffer_size);
	unpack16  (&job->contiguous, buf_ptr, buffer_size);

	unpack32  (&job->min_procs, buf_ptr, buffer_size);
	unpack32  (&job->min_memory, buf_ptr, buffer_size);
	unpack32  (&job->min_tmp_disk, buf_ptr, buffer_size);

	unpackstr_ptr_malloc (&job->req_nodes, &uint16_tmp, buf_ptr, buffer_size);
	if (job->req_nodes == NULL)
		job->req_nodes = "";
	unpackstr_ptr (&node_inx_str, &uint16_tmp, buf_ptr, buffer_size);
	if (node_inx_str == NULL)
		node_inx_str = "";
	job->req_node_inx = bitfmt2int(node_inx_str);
	unpackstr_ptr_malloc (&job->features, &uint16_tmp, buf_ptr, buffer_size);
	if (job->features == NULL)
		job->features = "";
	unpackstr_ptr_malloc (&job->job_script, &uint16_tmp, buf_ptr, buffer_size);
	if (job->job_script == NULL)
		job->job_script = "";
	return 0 ;
}
