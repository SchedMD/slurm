/*****************************************************************************\
 *  job_info.c - get/print the job state information of slurm
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov> et. al.
 *  CODE-OCEC-09-009. All rights reserved.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
 *  Please also read the included file: DISCLAIMER.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  In addition, as a special exception, the copyright holders give permission 
 *  to link the code of portions of this program with the OpenSSL library under
 *  certain conditions as described in each individual source file, and 
 *  distribute linked combinations including the two. You must obey the GNU 
 *  General Public License in all respects for all of the code used other than 
 *  OpenSSL. If you modify file(s) with this exception, you may extend this 
 *  exception to your version of the file(s), but you are not obligated to do 
 *  so. If you do not wish to do so, delete this exception statement from your
 *  version.  If you delete this exception statement from all source files in 
 *  the program, then also delete it here.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details. 
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <ctype.h>
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>

#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>

#include "src/api/job_info.h"
#include "src/common/forward.h"
#include "src/common/node_select.h"
#include "src/common/parse_time.h"
#include "src/common/slurm_auth.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/uid.h"
#include "src/common/xstring.h"

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
	char time_str[32];

	slurm_make_time_str ((time_t *)&jinfo->last_update, time_str, 
		sizeof(time_str));
	fprintf( out, "Job data as of %s, record count %d\n",
		 time_str, jinfo->record_count);

	for (i = 0; i < jinfo->record_count; i++) 
		slurm_print_job_info(out, &job_ptr[i], one_liner);
}

static void _sprint_range(char *str, uint16_t lower, uint16_t upper)
{
	/* Note: We don't have the size of str here */
	convert_num_unit((float)lower, str, 16, UNIT_NONE);
	if (upper > 0) {
    		char tmp[128];
		convert_num_unit((float)upper, tmp, sizeof(tmp), UNIT_NONE);
		strcat(str, "-");
		strcat(str, tmp);
	}
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
	char *print_this = slurm_sprint_job_info(job_ptr, one_liner);
	fprintf ( out, "%s", print_this);
	xfree(print_this);
}

/*
 * slurm_sprint_job_info - output information about a specific Slurm 
 *	job based upon message as loaded using slurm_load_jobs
 * IN job_ptr - an individual job information record pointer
 * IN one_liner - print as a single line if true
 * RET out - char * containing formatted output (must be freed after call)
 *           NULL is returned on failure.
 */
extern char *
slurm_sprint_job_info ( job_info_t * job_ptr, int one_liner )
{
	int i, j;
	char time_str[32], select_buf[122], *group_name, *user_name;
	char tmp1[128], tmp2[128], *tmp3_ptr;
	char tmp_line[512];
	char *ionodes = NULL;
	uint16_t exit_status = 0, term_sig = 0;
	char *out = NULL;
	
#ifdef HAVE_BG
	char *nodelist = "BP_List";
	select_g_select_jobinfo_get(job_ptr->select_jobinfo, 
			     SELECT_JOBDATA_IONODES, 
			     &ionodes);
#else
	char *nodelist = "NodeList";
#endif	

	/****** Line 1 ******/
	user_name = uid_to_string((uid_t) job_ptr->user_id);
	snprintf(tmp_line, sizeof(tmp_line), 
		"JobId=%u UserId=%s(%u) ", 
		job_ptr->job_id, user_name, job_ptr->user_id);
	xfree(user_name);
	out = xstrdup(tmp_line);
	group_name = gid_to_string((gid_t) job_ptr->group_id);
	snprintf(tmp_line, sizeof(tmp_line), "GroupId=%s(%u)",
		 group_name, job_ptr->group_id);
	xfree(group_name);
	xstrcat(out, tmp_line);
	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");

	/****** Line 2 ******/
	if(slurm_get_track_wckey())
		snprintf(tmp_line, sizeof(tmp_line), "Name=%s WCKey=%s",
			 job_ptr->name, job_ptr->wckey);
	else
		snprintf(tmp_line, sizeof(tmp_line), "Name=%s", job_ptr->name);
		
	xstrcat(out, tmp_line);
	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");

	/****** Line 3 ******/
	snprintf(tmp_line, sizeof(tmp_line), 
		 "Priority=%u Partition=%s BatchFlag=%u Reservation=%s", 
		 job_ptr->priority, job_ptr->partition, 
		 job_ptr->batch_flag, job_ptr->resv_name);
	xstrcat(out, tmp_line);
	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");

	/****** Line 4 ******/
	snprintf(tmp_line, sizeof(tmp_line),
		"AllocNode:Sid=%s:%u TimeLimit=", 
		job_ptr->alloc_node, job_ptr->alloc_sid);
	xstrcat(out, tmp_line);
	if (job_ptr->time_limit == INFINITE)
		sprintf(tmp_line, "UNLIMITED ");
	else if (job_ptr->time_limit == NO_VAL)
		sprintf(tmp_line, "Partition_Limit ");
	else {
		secs2time_str(job_ptr->time_limit * 60, tmp1,
			sizeof(tmp1));
		sprintf(tmp_line, "%s ", tmp1);
	}
	xstrcat(out, tmp_line);
	if (WIFSIGNALED(job_ptr->exit_code))
		term_sig = WTERMSIG(job_ptr->exit_code);
	else
		exit_status = WEXITSTATUS(job_ptr->exit_code);
	snprintf(tmp_line, sizeof(tmp_line),
		"ExitCode=%u:%u", 
		exit_status, term_sig);
	xstrcat(out, tmp_line);
	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");

	/****** Line 5 ******/
	if (IS_JOB_PENDING(job_ptr))
		tmp3_ptr = "EligibleTime";
	else
		tmp3_ptr = "StartTime";
	slurm_make_time_str((time_t *)&job_ptr->start_time, time_str,
		sizeof(time_str));
	snprintf(tmp_line, sizeof(tmp_line),
		"JobState=%s %s=%s EndTime=",
		job_state_string(job_ptr->job_state), 
		tmp3_ptr, time_str);
	xstrcat(out, tmp_line);
	if ((job_ptr->time_limit == INFINITE) && 
	    (job_ptr->end_time > time(NULL)))
		sprintf(tmp_line, "NONE");
	else {
		slurm_make_time_str ((time_t *)&job_ptr->end_time, time_str,
			sizeof(time_str));
		sprintf(tmp_line, "%s", time_str);
	}
	xstrcat(out, tmp_line);
	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");

	/****** Line 6 ******/
	xstrfmtcat(out, "%s=", nodelist);
	xstrcat(out, job_ptr->nodes);
	if(ionodes) {
		snprintf(tmp_line, sizeof(tmp_line), "[%s]", ionodes);
		xstrcat(out, tmp_line);
		xfree(ionodes);
	} 
	
	snprintf(tmp_line, sizeof(tmp_line), " %sIndices=", nodelist);
	xstrcat(out, tmp_line);
	for (j = 0;  (job_ptr->node_inx && (job_ptr->node_inx[j] != -1)); 
			j+=2) {
		if (j > 0)
			 xstrcat(out, ",");
		sprintf(tmp_line, "%d-%d", job_ptr->node_inx[j], 
			job_ptr->node_inx[j+1]);
		xstrcat(out, tmp_line);
	}
	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");

	/****** Line 6a (optional) ******/
#if 0
	/* mainly for debugging */ 
	convert_num_unit((float)job_ptr->num_cpu_groups, tmp1, sizeof(tmp1),
			 UNIT_NONE);
	snprintf(tmp_line, sizeof(tmp_line),
		"NumCPUGroups=%s ",
		 tmp1);
	xstrcat(out, tmp_line);
#endif

	if ((job_ptr->num_cpu_groups > 0) && 
	    (job_ptr->cpus_per_node) &&
	    (job_ptr->cpu_count_reps)) {
		int length = 0;
		xstrcat(out, "AllocCPUs=");
		length += 10;
		for (i = 0; i < job_ptr->num_cpu_groups; i++) {
			if (length > 70) {
				/* skip to last CPU group entry */
			    	if (i < job_ptr->num_cpu_groups - 1) {
			    		continue;
				}
				/* add elipsis before last entry */
			    	xstrcat(out, "...,");
				length += 4;
			}

			snprintf(tmp_line, sizeof(tmp_line),
				"%d",
				 job_ptr->cpus_per_node[i]);
			xstrcat(out, tmp_line);
			length += strlen(tmp_line);
		    	if (job_ptr->cpu_count_reps[i] > 1) {
				snprintf(tmp_line, sizeof(tmp_line),
					"*%d",
					 job_ptr->cpu_count_reps[i]);
				xstrcat(out, tmp_line);
				length += strlen(tmp_line);
			}
			if (i < job_ptr->num_cpu_groups - 1) {
				xstrcat(out, ",");
				length++;
			}
		}
		if (one_liner)
			xstrcat(out, " ");
		else
			xstrcat(out, "\n   ");
	}

	/****** Line 7 ******/
	convert_num_unit((float)job_ptr->num_procs, tmp1, sizeof(tmp1), 
			 UNIT_NONE);
#ifdef HAVE_BG
	convert_num_unit((float)job_ptr->num_nodes, tmp2, sizeof(tmp2),
			 UNIT_NONE);
	snprintf(tmp_line, sizeof(tmp_line), "ReqProcs=%s MinBPs=%s ", 
		 tmp1, tmp2);
#else
	_sprint_range(tmp2, job_ptr->num_nodes, job_ptr->max_nodes);
	snprintf(tmp_line, sizeof(tmp_line), "ReqProcs=%s ReqNodes=%s ", 
		 tmp1, tmp2);
#endif
	xstrcat(out, tmp_line);

	_sprint_range(tmp1, job_ptr->min_sockets, job_ptr->max_sockets);
	if (job_ptr->min_cores > 0) {
		_sprint_range(tmp2, job_ptr->min_cores, job_ptr->max_cores);
		strcat(tmp1, ":");
		strcat(tmp1, tmp2);
		if (job_ptr->min_threads > 0) {
			_sprint_range(tmp2, job_ptr->min_threads,
				      job_ptr->max_threads);
			strcat(tmp1, ":");
			strcat(tmp1, tmp2);
		}
	}
	snprintf(tmp_line, sizeof(tmp_line), 
		"ReqS:C:T=%s",
		tmp1);
	xstrcat(out, tmp_line);
	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");

	/****** Line 8 ******/
	snprintf(tmp_line, sizeof(tmp_line),
		"Shared=%s Contiguous=%d CPUs/task=%u Licenses=%s", 
		 (job_ptr->shared == 0 ? "0" :
		  job_ptr->shared == 1 ? "1" : "OK"),
		 job_ptr->contiguous, job_ptr->cpus_per_task,
		 job_ptr->licenses);
	xstrcat(out, tmp_line);

	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");

	/****** Line 9 ******/
	snprintf(tmp_line, sizeof(tmp_line), 
		"MinProcs=%u MinSockets=%u MinCores=%u MinThreads=%u", 
		job_ptr->job_min_procs, job_ptr->job_min_sockets, 
		job_ptr->job_min_cores, job_ptr->job_min_threads);
	xstrcat(out, tmp_line);
	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");

	/****** Line 10 ******/
	if (job_ptr->job_min_memory & MEM_PER_CPU) {
		job_ptr->job_min_memory &= (~MEM_PER_CPU);
		tmp3_ptr = "CPU";
	} else
		tmp3_ptr = "Node";
	convert_num_unit((float)job_ptr->job_min_memory, tmp1, sizeof(tmp1),
			 UNIT_NONE);
	convert_num_unit((float)job_ptr->job_min_tmp_disk, tmp2, sizeof(tmp2),
			 UNIT_NONE);
	snprintf(tmp_line, sizeof(tmp_line), 
		"MinMemory%s=%s MinTmpDisk=%s Features=%s",
		tmp3_ptr, tmp1, tmp2, job_ptr->features);
	xstrcat(out, tmp_line);
	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");

	/****** Line 11 ******/
	snprintf(tmp_line, sizeof(tmp_line), 
		"Dependency=%s Account=%s Requeue=%u Restarts=%u",
		job_ptr->dependency, job_ptr->account, job_ptr->requeue,
		job_ptr->restart_cnt);
	xstrcat(out, tmp_line);
	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");

	/****** Line 12 ******/
	if (job_ptr->state_desc) {
		/* Replace white space with underscore for easier parsing */
		for (j=0; job_ptr->state_desc[j]; j++) {
			if (isspace(job_ptr->state_desc[j]))
				job_ptr->state_desc[j] = '_';
		}
		tmp3_ptr = job_ptr->state_desc;
	} else
		tmp3_ptr = job_reason_string(job_ptr->state_reason);
	snprintf(tmp_line, sizeof(tmp_line), 
		"Reason=%s Network=%s",
		tmp3_ptr, job_ptr->network);
	xstrcat(out, tmp_line);
	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");

	/****** Line 13 ******/
	snprintf(tmp_line, sizeof(tmp_line), "Req%s=%s Req%sIndices=", 
		nodelist, job_ptr->req_nodes, nodelist);
	xstrcat(out, tmp_line);
	for (j = 0; (job_ptr->req_node_inx && (job_ptr->req_node_inx[j] != -1));
			j+=2) {
		if (j > 0)
			xstrcat(out, ",");
		sprintf(tmp_line, "%d-%d", job_ptr->req_node_inx[j],
			job_ptr->req_node_inx[j+1]);
		xstrcat(out, tmp_line);
	}
	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");

	/****** Line 14 ******/
	snprintf(tmp_line, sizeof(tmp_line), "Exc%s=%s Exc%sIndices=", 
		nodelist, job_ptr->exc_nodes, nodelist);
	xstrcat(out, tmp_line);
	for (j = 0; (job_ptr->exc_node_inx && (job_ptr->exc_node_inx[j] != -1)); 
			j+=2) {
		if (j > 0)
			xstrcat(out, ",");
		sprintf(tmp_line, "%d-%d", job_ptr->exc_node_inx[j],
			job_ptr->exc_node_inx[j+1]);
		xstrcat(out, tmp_line);
	}
	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");

	/****** Line 15 ******/
	slurm_make_time_str((time_t *)&job_ptr->submit_time, time_str, 
		sizeof(time_str));
	snprintf(tmp_line, sizeof(tmp_line), "SubmitTime=%s ", 
		 time_str);
	xstrcat(out, tmp_line);
	if (job_ptr->suspend_time) {
		slurm_make_time_str ((time_t *)&job_ptr->suspend_time, 
			time_str, sizeof(time_str));
	} else {
		strncpy(time_str, "None", sizeof(time_str));
	}
	sprintf(tmp_line, "SuspendTime=%s PreSusTime=%ld", 
		  time_str, (long int)job_ptr->pre_sus_time);
	xstrcat(out, tmp_line);

	/****** Lines 16, 17 (optional, batch only) ******/
	if (job_ptr->batch_flag) {
		if (one_liner)
			xstrcat(out, " ");
		else
			xstrcat(out, "\n   ");
		sprintf(tmp_line, "Command=%s", job_ptr->command);
		xstrcat(out, tmp_line);

		if (one_liner)
			xstrcat(out, " ");
		else
			xstrcat(out, "\n   ");
		sprintf(tmp_line, "WorkDir=%s", job_ptr->work_dir);
		xstrcat(out, tmp_line);
	}

	/****** Line 18 (optional) ******/
	if (job_ptr->comment) {
		if (one_liner)
			xstrcat(out, " ");
		else
			xstrcat(out, "\n   ");
		snprintf(tmp_line, sizeof(tmp_line), "Comment=%s ", 
			 job_ptr->comment);
		xstrcat(out, tmp_line);
	}

	/****** Line 19 (optional) ******/
	select_g_select_jobinfo_sprint(job_ptr->select_jobinfo,
				select_buf, sizeof(select_buf),
				SELECT_PRINT_MIXED);
	if (select_buf[0] != '\0') {
		if (one_liner)
			xstrcat(out, " ");
		else
			xstrcat(out, "\n   ");
		xstrcat(out, select_buf);
	}
#ifdef HAVE_BG
	/****** Line 20 (optional) ******/
	select_g_select_jobinfo_sprint(job_ptr->select_jobinfo,
				select_buf, sizeof(select_buf),
				SELECT_PRINT_BLRTS_IMAGE);
	if (select_buf[0] != '\0') {
		if (one_liner)
			xstrcat(out, " ");
		else
			xstrcat(out, "\n   ");
		snprintf(tmp_line, sizeof(tmp_line),
			 "BlrtsImage=%s", select_buf);
		xstrcat(out, tmp_line);
	}
	/****** Line 21 (optional) ******/
	select_g_select_jobinfo_sprint(job_ptr->select_jobinfo,
				select_buf, sizeof(select_buf),
				SELECT_PRINT_LINUX_IMAGE);
	if (select_buf[0] != '\0') {
		if (one_liner)
			xstrcat(out, " ");
		else
			xstrcat(out, "\n   ");
#ifdef HAVE_BGL
		snprintf(tmp_line, sizeof(tmp_line),
			 "LinuxImage=%s", select_buf);
#else
		snprintf(tmp_line, sizeof(tmp_line),
			 "CnloadImage=%s", select_buf);
#endif
		xstrcat(out, tmp_line);
	}
	/****** Line 22 (optional) ******/
	select_g_select_jobinfo_sprint(job_ptr->select_jobinfo,
				select_buf, sizeof(select_buf),
				SELECT_PRINT_MLOADER_IMAGE);
	if (select_buf[0] != '\0') {
		if (one_liner)
			xstrcat(out, " ");
		else
			xstrcat(out, "\n   ");
		snprintf(tmp_line, sizeof(tmp_line),
			 "MloaderImage=%s", select_buf);
		xstrcat(out, tmp_line);
	}
	/****** Line 23 (optional) ******/
	select_g_select_jobinfo_sprint(job_ptr->select_jobinfo,
				select_buf, sizeof(select_buf),
				SELECT_PRINT_RAMDISK_IMAGE);
	if (select_buf[0] != '\0') {
		if (one_liner)
			xstrcat(out, " ");
		else
			xstrcat(out, "\n   ");
#ifdef HAVE_BGL
		snprintf(tmp_line, sizeof(tmp_line),
			 "RamDiskImage=%s", select_buf);
#else
		snprintf(tmp_line, sizeof(tmp_line),
			 "IoloadImage=%s", select_buf);
#endif
		xstrcat(out, tmp_line);
	}
#endif
	xstrcat(out, "\n\n");

	return out;

}

/*
 * slurm_load_jobs - issue RPC to get all job configuration  
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

	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);

	req.last_update  = update_time;
	req.show_flags = show_flags;
	req_msg.msg_type = REQUEST_JOB_INFO;
	req_msg.data     = &req;

	if (slurm_send_recv_controller_msg(&req_msg, &resp_msg) < 0)
		return SLURM_ERROR;

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
 * slurm_load_job - issue RPC to get job information for one job ID
 * IN job_info_msg_pptr - place to store a job configuration pointer
 * IN job_id -  ID of job we want information about 
 * RET 0 or -1 on error
 * NOTE: free the response using slurm_free_job_info_msg
 */
extern int
slurm_load_job (job_info_msg_t **resp, uint32_t job_id)
{
	int rc;
	slurm_msg_t resp_msg;
	slurm_msg_t req_msg;
	job_id_msg_t req;

	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);

	req.job_id = job_id;
	req_msg.msg_type = REQUEST_JOB_INFO_SINGLE;
	req_msg.data     = &req;

	if (slurm_send_recv_controller_msg(&req_msg, &resp_msg) < 0)
		return SLURM_ERROR;

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
	char this_host[256], *this_addr;

	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);

	/*
	 *  Set request message address to slurmd on localhost
	 */
	gethostname_short(this_host, sizeof(this_host));
	this_addr = slurm_conf_get_nodeaddr(this_host);
	if (this_addr == NULL)
		this_addr = xstrdup("localhost");
	slurm_set_addr(&req_msg.address, (uint16_t)slurm_get_slurmd_port(), 
		       this_addr);
	xfree(this_addr);

	req.job_pid      = job_pid;
	req_msg.msg_type = REQUEST_JOB_ID;
	req_msg.data     = &req;
	
	rc = slurm_send_recv_node_msg(&req_msg, &resp_msg, 0);

	if(rc != 0 || !resp_msg.auth_cred) {
		error("slurm_pid2jobid: %m");
		if(resp_msg.auth_cred)
			g_slurm_auth_destroy(resp_msg.auth_cred);
		return SLURM_ERROR;
	}
	if(resp_msg.auth_cred)
		g_slurm_auth_destroy(resp_msg.auth_cred);	
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
 * slurm_get_rem_time - get the expected time remaining for a given job
 * IN jobid     - slurm job id
 * RET remaining time in seconds or -1 on error 
 */
extern long slurm_get_rem_time(uint32_t jobid)
{
	time_t now = time(NULL);
	time_t end_time;
	long rc;

	if (slurm_get_end_time(jobid, &end_time) != SLURM_SUCCESS)
		return -1L;

	rc = difftime(end_time, now);
	if (rc < 0)
		rc = 0L;
	return rc;
}

/* FORTRAN VERSIONS OF slurm_get_rem_time */
extern int32_t islurm_get_rem_time__(uint32_t *jobid)
{
	time_t now = time(NULL);
	time_t end_time;
	int32_t rc;

	if ((jobid == NULL)
	||  (slurm_get_end_time(*jobid, &end_time) != SLURM_SUCCESS))
		return 0;

	rc = difftime(end_time, now);
	if (rc < 0)
		rc = 0;
	return rc;
}
extern int32_t islurm_get_rem_time2__()
{
	uint32_t jobid;
	char *slurm_job_id = getenv("SLURM_JOB_ID");

	if (slurm_job_id == NULL)
		return 0;
	jobid = atol(slurm_job_id);
	return islurm_get_rem_time__(&jobid);
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
	int rc;
	slurm_msg_t resp_msg;
	slurm_msg_t req_msg;
	job_alloc_info_msg_t job_msg;
	srun_timeout_msg_t *timeout_msg;
	time_t now = time(NULL);
	static uint32_t jobid_cache = 0;
	static uint32_t jobid_env = 0;
	static time_t endtime_cache = 0;
	static time_t last_test_time = 0;

	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);

	if (!end_time_ptr)
		slurm_seterrno_ret(EINVAL);

	if (jobid == 0) {
		if (jobid_env) {
			jobid = jobid_env;
		} else {
			char *env = getenv("SLURM_JOB_ID");
			if (env) {
				jobid = (uint32_t) atol(env);
				jobid_env = jobid;
			}
		}
		if (jobid == 0) {
			slurm_seterrno(ESLURM_INVALID_JOB_ID);
			return SLURM_ERROR;
		}
	}

	/* Just use cached data if data less than 60 seconds old */
	if ((jobid == jobid_cache)
	&&  (difftime(now, last_test_time) < 60)) {
		*end_time_ptr  = endtime_cache;
		return SLURM_SUCCESS;
	}

	job_msg.job_id     = jobid;
	req_msg.msg_type   = REQUEST_JOB_END_TIME;
	req_msg.data       = &job_msg;

	if (slurm_send_recv_controller_msg(&req_msg, &resp_msg) < 0)
		return SLURM_ERROR;

	switch (resp_msg.msg_type) {
	case SRUN_TIMEOUT:
		timeout_msg = (srun_timeout_msg_t *) resp_msg.data;
		last_test_time = time(NULL);
		jobid_cache    = jobid;
		endtime_cache  = timeout_msg->timeout;
		*end_time_ptr  = endtime_cache;
		slurm_free_srun_timeout_msg(resp_msg.data);
		break;
	case RESPONSE_SLURM_RC:
		rc = ((return_code_msg_t *) resp_msg.data)->return_code;
		slurm_free_return_code_msg(resp_msg.data);
		if (endtime_cache)
			*end_time_ptr  = endtime_cache;
		else if (rc)
			slurm_seterrno_ret(rc);
		break;
	default:
		if (endtime_cache)
			*end_time_ptr  = endtime_cache;
		else
			slurm_seterrno_ret(SLURM_UNEXPECTED_MSG_ERROR);
		break;
	}

	return SLURM_SUCCESS;
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

	slurm_msg_t_init(&req);
	slurm_msg_t_init(&resp);

	req.msg_type = REQUEST_JOB_READY;
	req.data     = &msg;
	msg.job_id   = job_id;

	if (slurm_send_recv_controller_msg(&req, &resp) < 0)
		return -1;

	if (resp.msg_type == RESPONSE_JOB_READY) {
		rc = ((return_code_msg_t *) resp.data)->return_code;
		slurm_free_return_code_msg(resp.data);
	} else if (resp.msg_type == RESPONSE_SLURM_RC) {
		int job_rc = ((return_code_msg_t *) resp.data) -> 
				return_code;
		if ((job_rc == ESLURM_INVALID_PARTITION_NAME)
		||  (job_rc == ESLURM_INVALID_JOB_ID))
			rc = READY_JOB_FATAL;
		else	/* EAGAIN */
			rc = READY_JOB_ERROR;
		slurm_free_return_code_msg(resp.data);
	} else
		rc = READY_JOB_ERROR;

	return rc;
}

