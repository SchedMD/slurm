/*****************************************************************************\
 *  job_info.c - get/print the job state information of slurm
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Moe Jette <jette1@llnl.gov> et. al.
 *  UCRL-CODE-2002-040.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with ConMan; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <time.h>

#include "src/common/slurm_protocol_api.h"
#include "src/slurm/slurm.h"

/*
 * slurm_print_job_info_msg - output information about all Slurm 
 *	jobs based upon message as loaded using slurm_load_jobs
 * IN out - file to write to
 * IN job_info_msg_ptr - job information message pointer
 */
void 
slurm_print_job_info_msg ( FILE* out, job_info_msg_t * job_info_msg_ptr )
{
	int i;
	job_info_t * job_ptr = job_info_msg_ptr -> job_array ;
	char time_str[16];

	make_time_str ((time_t *)&job_info_msg_ptr->last_update, time_str);
	fprintf( out, "Job data as of %s, record count %d\n",
		time_str, job_info_msg_ptr->record_count);

	for (i = 0; i < job_info_msg_ptr-> record_count; i++) 
	{
		slurm_print_job_info ( out, & job_ptr[i] ) ;
	}
}

/*
 * slurm_print_job_info - output information about a specific Slurm 
 *	job based upon message as loaded using slurm_load_jobs
 * IN out - file to write to
 * IN job_ptr - an individual job information record pointer
 */
void
slurm_print_job_info ( FILE* out, job_info_t * job_ptr )
{
	int j;
	char time_str[16];

	fprintf ( out, "JobId=%u UserId=%u Name=%s ", 
		job_ptr->job_id, job_ptr->user_id, job_ptr->name);
	fprintf ( out, "JobState=%s TimeLimit=%u\n", 
		job_state_string(job_ptr->job_state), job_ptr->time_limit);

	fprintf ( out, "   Priority=%u Partition=%s BatchFlag:Sid=%u:%u\n", 
		job_ptr->priority, job_ptr->partition, 
		job_ptr->batch_flag, job_ptr->batch_sid);

	make_time_str ((time_t *)&job_ptr->start_time, time_str);
	fprintf ( out, "   StartTime=%s ", time_str);
	make_time_str ((time_t *)&job_ptr->end_time, time_str);
	fprintf ( out, "EndTime=%s\n", time_str);

	fprintf ( out, "   NodeList=%s ", job_ptr->nodes);
	fprintf ( out, "NodeListIndecies=");
	for (j = 0; job_ptr->node_inx; j++) {
		if (j > 0)
			fprintf( out, ",%d", job_ptr->node_inx[j]);
		else
			fprintf( out, "%d", job_ptr->node_inx[j]);
		if (job_ptr->node_inx[j] == -1)
			break;
	}
	fprintf( out, "\n");

	fprintf ( out, "   ReqProcs=%u MinNodes=%u ", 
		job_ptr->num_procs, job_ptr->num_nodes);
	fprintf ( out, "Shared=%u Contiguous=%u\n",  
		job_ptr->shared, job_ptr->contiguous);

	fprintf ( out, "   MinProcs=%u MinMemory=%u ",  
		job_ptr->min_procs, job_ptr->min_memory);
	fprintf ( out, "Features=%s MinTmpDisk=%u\n", 
		job_ptr->features, job_ptr->min_tmp_disk);

	fprintf ( out, "   ReqNodeList=%s ", job_ptr->req_nodes);
	fprintf ( out, "ReqNodeListIndecies=");
	for (j = 0; job_ptr->req_node_inx; j++) {
		if (j > 0)
			fprintf( out, ",%d", job_ptr->req_node_inx[j]);
		else
			fprintf( out, "%d", job_ptr->req_node_inx[j]);
		if (job_ptr->req_node_inx[j] == -1)
			break;
	}
	fprintf( out, "\n\n");
}

/*
 * make_time_str - convert time_t to string with "month/date hour:min:sec" 
 * IN time - a time stamp
 * OUT string - pointer user defined buffer
 */
void
make_time_str (time_t *time, char *string)
{
	struct tm time_tm;

	localtime_r (time, &time_tm);
	sprintf ( string, "%2.2u/%2.2u-%2.2u:%2.2u:%2.2u", 
		(time_tm.tm_mon+1), time_tm.tm_mday, 
		time_tm.tm_hour, time_tm.tm_min, time_tm.tm_sec);
}


/*
 * slurm_load_jobs - issue RPC to get slurm all job configuration  
 *	information if changed since update_time 
 * IN update_time - time of current configuration data
 * IN job_info_msg_pptr - place to store a job configuration pointer
 * RET 0 or a slurm error code
 * NOTE: free the response using slurm_free_job_info_msg
 */
int
slurm_load_jobs (time_t update_time, job_info_msg_t **job_info_msg_pptr)
{
	int msg_size ;
	int rc ;
	slurm_fd sockfd ;
	slurm_msg_t request_msg ;
	slurm_msg_t response_msg ;
	job_info_request_msg_t last_time_msg ;
	return_code_msg_t * slurm_rc_msg ;

	/* init message connection for communication with controller */
	if ( ( sockfd = slurm_open_controller_conn ( ) ) 
			== SLURM_SOCKET_ERROR ) {
		slurm_seterrno ( SLURM_COMMUNICATIONS_CONNECTION_ERROR );
		return SLURM_SOCKET_ERROR ;
	}

	/* send request message */
	last_time_msg . last_update = update_time ;
	request_msg . msg_type = REQUEST_JOB_INFO ;
	request_msg . data = &last_time_msg ;
	if ( ( rc = slurm_send_controller_msg ( sockfd , & request_msg ) ) 
			== SLURM_SOCKET_ERROR ) {
		slurm_seterrno ( SLURM_COMMUNICATIONS_SEND_ERROR );
		return SLURM_SOCKET_ERROR ;
	}

	/* receive message */
	if ( ( msg_size = slurm_receive_msg ( sockfd , & response_msg ) ) 
			== SLURM_SOCKET_ERROR ) {
		slurm_seterrno ( SLURM_COMMUNICATIONS_RECEIVE_ERROR );
		return SLURM_SOCKET_ERROR ;
	}

	/* shutdown message connection */
	if ( ( rc = slurm_shutdown_msg_conn ( sockfd ) ) 
			== SLURM_SOCKET_ERROR ) {
		slurm_seterrno ( SLURM_COMMUNICATIONS_SHUTDOWN_ERROR );
		return SLURM_SOCKET_ERROR ;
	}
	if ( msg_size )
		return msg_size;

	switch ( response_msg . msg_type )
	{
		case RESPONSE_JOB_INFO:
			*job_info_msg_pptr = 
				(job_info_msg_t *) response_msg.data ;
			return SLURM_PROTOCOL_SUCCESS ;
			break ;
		case RESPONSE_SLURM_RC:
			slurm_rc_msg = 
				(return_code_msg_t *) response_msg.data ;
			rc = slurm_rc_msg->return_code;
			slurm_free_return_code_msg ( slurm_rc_msg );	
			if (rc) {
				slurm_seterrno ( rc );
				return SLURM_PROTOCOL_ERROR;
			}
			break ;
		default:
			slurm_seterrno ( SLURM_UNEXPECTED_MSG_ERROR );
			return SLURM_PROTOCOL_ERROR;
			break ;
	}

	return SLURM_PROTOCOL_SUCCESS ;
}

/*
 * slurm_pid2jobid - issue RPC to get the slurm job_id given a process_id 
 *	on this machine
 * IN job_pid - process_id of interest on this machine
 * OUT job_id_ptr - place to store a slurm job_id
 * RET 0 or a slurm error code
 */
int
slurm_pid2jobid (pid_t job_pid, uint32_t *job_id_ptr)
{
	int msg_size ;
	int rc ;
	slurm_addr slurm_address;
	slurm_fd sockfd ;
	slurm_msg_t request_msg ;
	slurm_msg_t response_msg ;
	job_id_request_msg_t pid2jobid_msg ;
	return_code_msg_t * slurm_rc_msg ;
	job_id_response_msg_t * slurm_job_id_msg ;

	/* init message connection for communication with local slurmd */
	slurm_set_addr(&slurm_address, (uint16_t)slurm_get_slurmd_port(), 
		       "localhost");
	if ( ( sockfd = slurm_open_msg_conn ( &slurm_address ) ) 
			== SLURM_SOCKET_ERROR ) {
		slurm_seterrno ( SLURM_COMMUNICATIONS_CONNECTION_ERROR );
		return SLURM_SOCKET_ERROR ;
	}

	/* send request message */
	pid2jobid_msg . job_pid = job_pid ;
	request_msg . msg_type = REQUEST_JOB_ID ;
	request_msg . data = &pid2jobid_msg ;
	if ( ( rc = slurm_send_node_msg ( sockfd , & request_msg ) ) 
			== SLURM_SOCKET_ERROR ) {
		slurm_seterrno ( SLURM_COMMUNICATIONS_SEND_ERROR );
		return SLURM_SOCKET_ERROR ;
	}

	/* receive message */
	if ( ( msg_size = slurm_receive_msg ( sockfd , & response_msg ) ) 
			== SLURM_SOCKET_ERROR ) {
		slurm_seterrno ( SLURM_COMMUNICATIONS_RECEIVE_ERROR );
		return SLURM_SOCKET_ERROR ;
	}

	/* shutdown message connection */
	if ( ( rc = slurm_shutdown_msg_conn ( sockfd ) ) 
			== SLURM_SOCKET_ERROR ) {
		slurm_seterrno ( SLURM_COMMUNICATIONS_SHUTDOWN_ERROR );
		return SLURM_SOCKET_ERROR ;
	}
	if ( msg_size )
		return msg_size;

	switch ( response_msg . msg_type )
	{
		case RESPONSE_JOB_ID:
			slurm_job_id_msg = 
				(job_id_response_msg_t *) response_msg.data ;
			*job_id_ptr = slurm_job_id_msg -> job_id;
			slurm_free_job_id_response_msg ( slurm_job_id_msg );
			return SLURM_PROTOCOL_SUCCESS ;
			break ;
		case RESPONSE_SLURM_RC:
			slurm_rc_msg = 
				(return_code_msg_t *) response_msg.data ;
			rc = slurm_rc_msg->return_code;
			slurm_free_return_code_msg ( slurm_rc_msg );	
			if (rc) {
				slurm_seterrno ( rc );
				return SLURM_PROTOCOL_ERROR;
			}
			break ;
		default:
			slurm_seterrno ( SLURM_UNEXPECTED_MSG_ERROR );
			return SLURM_PROTOCOL_ERROR;
			break ;
	}

	return SLURM_PROTOCOL_SUCCESS ;
}

