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

#include "slurm.h"
#include <src/common/slurm_protocol_api.h>

#if DEBUG_MODULE
/* main is used here for testing purposes only */
int 
main (int argc, char *argv[]) 
{
	static time_t last_update_time = (time_t) NULL;
	int error_code;
	job_info_msg_t * job_info_msg_ptr = NULL;

	error_code = slurm_load_jobs (last_update_time, &job_info_msg_ptr);
	if (error_code) {
		printf ("slurm_load_job error %d\n", error_code);
		exit (error_code);
	}

	slurm_print_job_info_msg ( job_info_msg_ptr ) ;

	slurm_free_job_info ( job_info_msg_ptr ) ;
	exit (0);
}
#endif

/* print the entire job_info_msg */
void 
slurm_print_job_info_msg ( job_info_msg_t * job_info_msg_ptr )
{
	int i;
	job_table_t * job_ptr = job_info_msg_ptr -> job_array ;

	printf("Jobs updated at %d, record count %d\n",
		job_info_msg_ptr ->last_update, job_info_msg_ptr->record_count);

	for (i = 0; i < job_info_msg_ptr-> record_count; i++) 
	{
		slurm_print_job_table ( & job_ptr[i] ) ;
	}
}

/* print an individual job_table row */
void
slurm_print_job_table (job_table_t * job_ptr )
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
 * slurm_load_job - load the supplied job information buffer for use by info 
 *	gathering APIs if job records have changed since the time specified. 
 * input: update_time - time of last update
 *	job_buffer_ptr - place to park job_buffer pointer
 * output: job_buffer_ptr - pointer to allocated job_buffer
 *	returns -1 if no update since update_time, 
 *		0 if update with no error, 
 *		EINVAL if the buffer (version or otherwise) is invalid, 
 *		ENOMEM if malloc failure
 * NOTE: the allocated memory at job_info_msg_pptr freed by slurm_free_job_info.
 */
int
slurm_load_jobs (time_t update_time, job_info_msg_t **job_info_msg_pptr)
{
        int msg_size ;
        int rc ;
        slurm_fd sockfd ;
        slurm_msg_t request_msg ;
        slurm_msg_t response_msg ;
        last_update_msg_t last_time_msg ;
	return_code_msg_t * slurm_rc_msg ;

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
		case RESPONSE_JOB_INFO:
        		 *job_info_msg_pptr = ( job_info_msg_t * ) response_msg . data ;
			break ;
		case RESPONSE_SLURM_RC:
			slurm_rc_msg = ( return_code_msg_t * ) response_msg . data ;
			break ;
		default:
			return SLURM_UNEXPECTED_MSG_ERROR ;
			break ;
	}

        return SLURM_SUCCESS ;
}

