/*****************************************************************************\
 *  job_info.c - get/print the job state information of slurm
 *  $Id$
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
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <time.h>

#include <slurm/slurm.h>

#include "src/common/slurm_protocol_api.h"

/*
 * slurm_print_job_info_msg - output information about all Slurm 
 *	jobs based upon message as loaded using slurm_load_jobs
 * IN out - file to write to
 * IN job_info_msg_ptr - job information message pointer
 * IN one_liner - print as a single line if true
 */
void 
slurm_print_job_info_msg ( FILE* out, job_info_msg_t *jinfo, int one_liner )
{
	int i;
	job_info_t *job_ptr = jinfo->job_array;
	char time_str[16];

	make_time_str ((time_t *)&jinfo->last_update, time_str);
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
void
slurm_print_job_info ( FILE* out, job_info_t * job_ptr, int one_liner )
{
	int j;
	char time_str[16];
	struct passwd *user_info = NULL;

	/****** Line 1 ******/
	fprintf ( out, "JobId=%u ", job_ptr->job_id);
	user_info = getpwuid((uid_t) job_ptr->user_id);
	if (user_info && user_info->pw_name[0])
		fprintf ( out, "UserId=%s(%u) ", 
			  user_info->pw_name, job_ptr->user_id);
	else
		fprintf ( out, "UserId=(%u) ", job_ptr->user_id);
	fprintf ( out, "Name=%s JobState=%s", 
		  job_ptr->name, job_state_string(job_ptr->job_state));
	if (one_liner)
		fprintf ( out, " ");
	else
		fprintf ( out, "\n   ");

	/****** Line 2 ******/
	fprintf ( out, "Priority=%u Partition=%s BatchFlag=%u", 
		  job_ptr->priority, job_ptr->partition, 
		  job_ptr->batch_flag);
	if (one_liner)
		fprintf ( out, " ");
	else
		fprintf ( out, "\n   ");

	/****** Line 3 ******/
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

	/****** Line 4 ******/
	make_time_str ((time_t *)&job_ptr->start_time, time_str);
	fprintf ( out, "StartTime=%s EndTime=", time_str);
	if ((job_ptr->time_limit == INFINITE) && 
	    (job_ptr->end_time > time(NULL)))
		fprintf ( out, "NONE");
	else {
		make_time_str ((time_t *)&job_ptr->end_time, time_str);
		fprintf ( out, "%s", time_str);
	}
	if (one_liner)
		fprintf ( out, " ");
	else
		fprintf ( out, "\n   ");

	/****** Line 5 ******/
	fprintf ( out, "NodeList=%s ", job_ptr->nodes);
	fprintf ( out, "NodeListIndecies=");
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

	/****** Line 6 ******/
	fprintf ( out, "ReqProcs=%u MinNodes=%u ", 
		job_ptr->num_procs, job_ptr->num_nodes);
	fprintf ( out, "Shared=%u Contiguous=%u",  
		job_ptr->shared, job_ptr->contiguous);
	if (one_liner)
		fprintf ( out, " ");
	else
		fprintf ( out, "\n   ");

	/****** Line 7 ******/
	fprintf ( out, "MinProcs=%u MinMemory=%u ",  
		job_ptr->min_procs, job_ptr->min_memory);
	fprintf ( out, "Features=%s MinTmpDisk=%u", 
		job_ptr->features, job_ptr->min_tmp_disk);
	if (one_liner)
		fprintf ( out, " ");
	else
		fprintf ( out, "\n   ");

	/****** Line 8 ******/
	fprintf ( out, "ReqNodeList=%s ", job_ptr->req_nodes);
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
slurm_load_jobs (time_t update_time, job_info_msg_t **resp)
{
	int rc;
	slurm_msg_t resp_msg;
	slurm_msg_t req_msg;
	job_info_request_msg_t req;

	req.last_update  = update_time;
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
 * IN job_pid - process_id of interest on this machine
 * OUT job_id_ptr - place to store a slurm job_id
 * RET 0 or a slurm error code
 */
int
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

