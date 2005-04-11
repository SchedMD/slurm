/*****************************************************************************\
 *  job_info.c - get/print the job state information of slurm
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov> et. al.
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
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <errno.h>
#include <pwd.h>
#include <grp.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <time.h>

#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>

#include "src/api/job_info.h"
#include "src/common/node_select.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/uid.h"

/*
 * slurm_print_job_info_msg - output information about all Slurm 
 *	jobs based upon message as loaded using slurm_load_jobs
 * IN out - file to write to
 * IN job_info_msg_ptr - job information message pointer
 * IN one_liner - print as a single line if true
 */
extern void 
slurm_print_job_info_msg ( FILE* out, job_info_msg_t *jinfo, int one_liner )
{
	int i;
	job_info_t *job_ptr = jinfo->job_array;
	char time_str[16];

	slurm_make_time_str ((time_t *)&jinfo->last_update, time_str);
	fprintf( out, "Job data as of %s, record count %d\n",
		 time_str, jinfo->record_count);

	for (i = 0; i < jinfo->record_count; i++) 
		slurm_print_job_info(out, &job_ptr[i], one_liner);
}

/*
 * slurm_print_job_info - output information about a specific Slurm 
 *	job based upon message as loaded using slurm_load_jobs
 * IN out - file to write to
 * IN job_ptr - an individual job information record pointer
 * IN one_liner - print as a single line if true
 */
extern void
slurm_print_job_info ( FILE* out, job_info_t * job_ptr, int one_liner )
{
	int j;
	char time_str[16], select_buf[128];
	struct group *group_info = NULL;

	/****** Line 1 ******/
	fprintf ( out, "JobId=%u ", job_ptr->job_id);
	fprintf ( out, "UserId=%s(%u) ", 
		uid_to_string((uid_t) job_ptr->user_id), job_ptr->user_id);
	group_info = getgrgid( (gid_t) job_ptr->group_id );
	if ( group_info && group_info->gr_name[ 0 ] )
		fprintf( out, "GroupId=%s(%u)",
			 group_info->gr_name, job_ptr->group_id );
	else
		fprintf( out, "GroupId=(%u)", job_ptr->group_id );
	if (one_liner)
		fprintf ( out, " ");
	else
		fprintf ( out, "\n   ");

	/****** Line 2 ******/
	fprintf ( out, "Name=%s JobState=%s", 
		  job_ptr->name, job_state_string(job_ptr->job_state));
	if (one_liner)
		fprintf ( out, " ");
	else
		fprintf ( out, "\n   ");

	/****** Line 3 ******/
	fprintf ( out, "Priority=%u Partition=%s BatchFlag=%u", 
		  job_ptr->priority, job_ptr->partition, 
		  job_ptr->batch_flag);
	if (one_liner)
		fprintf ( out, " ");
	else
		fprintf ( out, "\n   ");

	/****** Line 4 ******/
	fprintf ( out, "AllocNode:Sid=%s:%u TimeLimit=", 
		  job_ptr->alloc_node, job_ptr->alloc_sid);
	if (job_ptr->time_limit == INFINITE)
		fprintf ( out, "UNLIMITED");
	else if (job_ptr->time_limit == NO_VAL)
		fprintf ( out, "Partition_Limit");
	else
		fprintf ( out, "%u", job_ptr->time_limit);
	if (one_liner)
		fprintf ( out, " ");
	else
		fprintf ( out, "\n   ");

	/****** Line 5 ******/
	slurm_make_time_str ((time_t *)&job_ptr->start_time, time_str);
	fprintf ( out, "StartTime=%s EndTime=", time_str);
	if ((job_ptr->time_limit == INFINITE) && 
	    (job_ptr->end_time > time(NULL)))
		fprintf ( out, "NONE");
	else {
		slurm_make_time_str ((time_t *)&job_ptr->end_time, time_str);
		fprintf ( out, "%s", time_str);
	}
	if (one_liner)
		fprintf ( out, " ");
	else
		fprintf ( out, "\n   ");

	/****** Line 6 ******/
	fprintf ( out, "NodeList=%s ", job_ptr->nodes);
	fprintf ( out, "NodeListIndicies=");
	for (j = 0; job_ptr->node_inx; j++) {
		if (j > 0)
			fprintf( out, ",%d", job_ptr->node_inx[j]);
		else
			fprintf( out, "%d", job_ptr->node_inx[j]);
		if (job_ptr->node_inx[j] == -1)
			break;
	}
	if (one_liner)
		fprintf ( out, " ");
	else
		fprintf ( out, "\n   ");

	/****** Line 7 ******/
	fprintf ( out, "ReqProcs=%u MinNodes=%u ", 
		job_ptr->num_procs, job_ptr->num_nodes);
	fprintf ( out, "Shared=%u Contiguous=%u",  
		job_ptr->shared, job_ptr->contiguous);
	if (one_liner)
		fprintf ( out, " ");
	else
		fprintf ( out, "\n   ");

	/****** Line 8 ******/
	fprintf ( out, "MinProcs=%u MinMemory=%u ",  
		job_ptr->min_procs, job_ptr->min_memory);
	fprintf ( out, "Features=%s MinTmpDisk=%u", 
		job_ptr->features, job_ptr->min_tmp_disk);
	if (one_liner)
		fprintf ( out, " ");
	else
		fprintf ( out, "\n   ");

	/****** Line 9 ******/
	 fprintf ( out, "Dependency=%u Account=%s Reason=%s",
		job_ptr->dependency, job_ptr->account,
		job_reason_string(job_ptr->wait_reason));
	if (one_liner)
		fprintf ( out, " ");
	else
		fprintf ( out, "\n   ");


	/****** Line 10 ******/
	fprintf ( out, "ReqNodeList=%s ", job_ptr->req_nodes);
	fprintf ( out, "ReqNodeListIndicies=");
	for (j = 0; job_ptr->req_node_inx; j++) {
		if (j > 0)
			fprintf( out, ",%d", job_ptr->req_node_inx[j]);
		else
			fprintf( out, "%d", job_ptr->req_node_inx[j]);
		if (job_ptr->req_node_inx[j] == -1)
			break;
	}
	if (one_liner)
		fprintf ( out, " ");
	else
		fprintf ( out, "\n   ");

	/****** Line 11 ******/
	fprintf ( out, "ExcNodeList=%s ", job_ptr->exc_nodes);
	fprintf ( out, "ExcNodeListIndicies=");
	for (j = 0; job_ptr->exc_node_inx; j++) {
		if (j > 0)
			fprintf( out, ",%d", job_ptr->exc_node_inx[j]);
		else
			fprintf( out, "%d", job_ptr->exc_node_inx[j]);
		if (job_ptr->exc_node_inx[j] == -1)
			break;
	}


	/****** Line 12 (optional) ******/
	select_g_sprint_jobinfo(job_ptr->select_jobinfo,
		select_buf, sizeof(select_buf), SELECT_PRINT_MIXED);
	if (select_buf[0] != '\0') {
		if (one_liner)
			fprintf ( out, " ");
		else
			fprintf ( out, "\n   ");
		fprintf( out, "%s", select_buf);
	}

	fprintf( out, "\n\n");
}

/*
 * make_time_str - convert time_t to string with "month/date hour:min:sec" 
 * IN time - a time stamp
 * OUT string - pointer user defined buffer
 */
extern void
slurm_make_time_str (time_t *time, char *string)
{
	struct tm time_tm;

	localtime_r (time, &time_tm);
	if ( *time == (time_t) 0 ) {
		sprintf( string, "Unknown" );
	} else {
		sprintf ( string, "%2.2u/%2.2u-%2.2u:%2.2u:%2.2u", 
			  (time_tm.tm_mon+1), time_tm.tm_mday, 
			  time_tm.tm_hour, time_tm.tm_min, time_tm.tm_sec);
	}
}


/*
 * slurm_load_jobs - issue RPC to get slurm all job configuration  
 *	information if changed since update_time 
 * IN update_time - time of current configuration data
 * IN job_info_msg_pptr - place to store a job configuration pointer
 * IN show_flags -  job filtering options
 * RET 0 or -1 on error
 * NOTE: free the response using slurm_free_job_info_msg
 */
extern int
slurm_load_jobs (time_t update_time, job_info_msg_t **resp,
		uint16_t show_flags)
{
	int rc;
	slurm_msg_t resp_msg;
	slurm_msg_t req_msg;
	job_info_request_msg_t req;

	req.last_update  = update_time;
	req.show_flags = show_flags;
	req_msg.msg_type = REQUEST_JOB_INFO;
	req_msg.data     = &req;

	if (slurm_send_recv_controller_msg(&req_msg, &resp_msg) < 0)
		return SLURM_ERROR;

	slurm_free_cred(resp_msg.cred);
	switch (resp_msg.msg_type) {
	case RESPONSE_JOB_INFO:
		*resp = (job_info_msg_t *)resp_msg.data;
		break;
	case RESPONSE_SLURM_RC:
		rc = ((return_code_msg_t *) resp_msg.data)->return_code;
		slurm_free_return_code_msg(resp_msg.data);	
		if (rc) 
			slurm_seterrno_ret(rc);
		break;
	default:
		slurm_seterrno_ret(SLURM_UNEXPECTED_MSG_ERROR);
		break;
	}

	return SLURM_PROTOCOL_SUCCESS ;
}


/*
 * slurm_pid2jobid - issue RPC to get the slurm job_id given a process_id 
 *	on this machine
 * IN job_pid     - process_id of interest on this machine
 * OUT job_id_ptr - place to store a slurm job_id
 * RET 0 or -1 on error
 */
extern int
slurm_pid2jobid (pid_t job_pid, uint32_t *jobid)
{
	int rc;
	slurm_msg_t req_msg;
	slurm_msg_t resp_msg;
	job_id_request_msg_t req;

	/*
	 *  Set request message address to slurmd on localhost
	 */
	slurm_set_addr(&req_msg.address, (uint16_t)slurm_get_slurmd_port(), 
		       "localhost");

	req.job_pid      = job_pid;
	req_msg.msg_type = REQUEST_JOB_ID;
	req_msg.data     = &req;

	if (slurm_send_recv_node_msg(&req_msg, &resp_msg, 0) < 0)
		return SLURM_ERROR;

	slurm_free_cred(resp_msg.cred);
	switch (resp_msg.msg_type) {
	case RESPONSE_JOB_ID:
		*jobid = ((job_id_response_msg_t *) resp_msg.data)->job_id;
		slurm_free_job_id_response_msg(resp_msg.data);
		break;
	case RESPONSE_SLURM_RC:
	        rc = ((return_code_msg_t *) resp_msg.data)->return_code;
		slurm_free_return_code_msg(resp_msg.data);	
		if (rc) 
			slurm_seterrno_ret(rc);
		break;
	default:
		slurm_seterrno_ret(SLURM_UNEXPECTED_MSG_ERROR);
		break;
	}

	return SLURM_PROTOCOL_SUCCESS;
}

/*
 * slurm_get_end_time - get the expected end time for a given slurm job
 * IN jobid     - slurm job id
 * end_time_ptr - location in which to store scheduled end time for job 
 * RET 0 or -1 on error
 */
extern int
slurm_get_end_time(uint32_t jobid, time_t *end_time_ptr)
{
	int error_code, i;
	job_info_msg_t *jinfo;
	job_info_t *job_ptr;

	if ((error_code = slurm_load_jobs ((time_t) NULL, &jinfo, 1)))
		return error_code;

	error_code = SLURM_ERROR;	/* error until job found */
	job_ptr = jinfo->job_array;
	for (i = 0; i < jinfo->record_count; i++) {
		if (job_ptr[i].job_id != jobid)
			continue;
		*end_time_ptr = job_ptr[i].end_time;
		error_code = SLURM_SUCCESS;
		break;
	}
	slurm_free_job_info_msg(jinfo);

	if (error_code)
		slurm_seterrno(ESLURM_INVALID_JOB_ID);
	return error_code; 
}

/*
 * slurm_get_select_jobinfo - get data from a select job credential
 * IN jobinfo  - updated select job credential
 * IN data_type - type of data to enter into job credential
 * IN/OUT data - the data to enter into job credential
 * RET 0 or -1 on error
 */
extern int slurm_get_select_jobinfo (select_jobinfo_t jobinfo,
		enum select_data_type data_type, void *data)
{
	return select_g_get_jobinfo (jobinfo, data_type, data);
}

/*
 * slurm_job_node_ready - report if nodes are ready for job to execute now
 * IN job_id - slurm job id
 * RET: READY_* values as defined in api/job_info.h
 */
extern int slurm_job_node_ready(uint32_t job_id)
{
	slurm_msg_t req, resp;
	job_id_msg_t msg;
	int rc;

	req.msg_type = REQUEST_JOB_READY;
	req.data     = &msg;
	msg.job_id   = job_id;

	if (slurm_send_recv_controller_msg(&req, &resp) < 0)
		return -1;

	if (resp.msg_type == RESPONSE_JOB_READY) {
		rc = ((return_code_msg_t *) resp.data)->return_code;
		slurm_free_return_code_msg(resp.data);
	} else if (resp.msg_type == RESPONSE_SLURM_RC) {
		rc = READY_JOB_ERROR;
		slurm_free_return_code_msg(resp.data);
	} else
		rc = READY_JOB_ERROR;

	return rc;
}

