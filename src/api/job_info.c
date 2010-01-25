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

static void _sprint_range(char *str, uint32_t str_size,
			  uint32_t lower, uint32_t upper)
{
	char tmp[128];

#ifdef HAVE_BG
	convert_num_unit((float)lower, tmp, sizeof(tmp), UNIT_NONE);
#else
	snprintf(tmp, sizeof(tmp), "%u", lower);
#endif
	if (upper > 0) {
    		char tmp2[128];
#ifdef HAVE_BG
		convert_num_unit((float)upper, tmp2, sizeof(tmp2), UNIT_NONE);
#else
		snprintf(tmp2, sizeof(tmp2), "%u", upper);
#endif
		snprintf(str, str_size, "%s-%s", tmp, tmp2);
	} else
		snprintf(str, str_size, "%s", tmp);

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
	fprintf(out, "%s", print_this);
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
	char time_str[32], *group_name, *user_name;
	char tmp1[128], tmp2[128], *tmp3_ptr;
	char tmp_line[512];
	char *ionodes = NULL;
	uint16_t exit_status = 0, term_sig = 0;
	job_resources_t *job_resrcs = job_ptr->job_resrcs;
	char *out = NULL;
	time_t run_time;
	uint32_t min_nodes, max_nodes = 0;

#ifdef HAVE_BG
	char select_buf[122];
	char *nodelist = "BP_List";
	select_g_select_jobinfo_get(job_ptr->select_jobinfo,
				    SELECT_JOBDATA_IONODES,
				    &ionodes);
#else
	bitstr_t *core_bitmap;
	char *host;
	int sock_inx, sock_reps, last;
	int abs_node_inx, rel_node_inx;
	int bit_inx, bit_reps;
	uint32_t *last_mem_alloc_ptr = NULL;
	uint32_t last_mem_alloc = NO_VAL;
	char last_hosts[128];
	hostlist_t hl, hl_last;
	char *nodelist = "NodeList";
#endif

	/****** Line 1 ******/
	snprintf(tmp_line, sizeof(tmp_line),
		 "JobId=%u Name=%s", job_ptr->job_id, job_ptr->name);
	out = xstrdup(tmp_line);
	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");

	/****** Line 2 ******/
	user_name = uid_to_string((uid_t) job_ptr->user_id);
	group_name = gid_to_string((gid_t) job_ptr->group_id);
	snprintf(tmp_line, sizeof(tmp_line),
		 "UserId=%s(%u) GroupId=%s(%u)",
		 user_name, job_ptr->user_id, group_name, job_ptr->group_id);
	xfree(user_name);
	xfree(group_name);
	xstrcat(out, tmp_line);
	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");

	/****** Line 3 ******/
	snprintf(tmp_line, sizeof(tmp_line),
		 "Priority=%u Account=%s QOS=%s",
		 job_ptr->priority, job_ptr->account, job_ptr->qos);
	xstrcat(out, tmp_line);
	if(slurm_get_track_wckey()) {
		snprintf(tmp_line, sizeof(tmp_line),
			 " WCKey=%s", job_ptr->wckey);
		xstrcat(out, tmp_line);
	}
	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");

	/****** Line 4 ******/
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
		 "JobState=%s Reason=%s Dependency=%s",
		 job_state_string(job_ptr->job_state), tmp3_ptr,
		 job_ptr->dependency);
	xstrcat(out, tmp_line);
	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");

	/****** Line 5 ******/
	snprintf(tmp_line, sizeof(tmp_line),
		 "Requeue=%u Restarts=%u BatchFlag=%u ",
		 job_ptr->requeue, job_ptr->restart_cnt, job_ptr->batch_flag);
	if (WIFSIGNALED(job_ptr->exit_code))
		term_sig = WTERMSIG(job_ptr->exit_code);
	else
		exit_status = WEXITSTATUS(job_ptr->exit_code);
	xstrcat(out, tmp_line);
	snprintf(tmp_line, sizeof(tmp_line),
		 "ExitCode=%u:%u", exit_status, term_sig);
	xstrcat(out, tmp_line);
	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");

	/****** Line 6 ******/
	snprintf(tmp_line, sizeof(tmp_line), "RunTime=");
	xstrcat(out, tmp_line);

	run_time = time(NULL);
	if (IS_JOB_SUSPENDED(job_ptr))
		run_time = job_ptr->pre_sus_time;
	else {
		if (!IS_JOB_RUNNING(job_ptr) && (job_ptr->end_time != 0))
			run_time = job_ptr->end_time;
		if (job_ptr->suspend_time) {
			run_time = (time_t)
				(difftime(run_time, job_ptr->suspend_time)
				 + job_ptr->pre_sus_time);
		} else
			run_time = (time_t)
				difftime(run_time, job_ptr->start_time);
	}
	secs2time_str(run_time, tmp1, sizeof(tmp1));
	sprintf(tmp_line, "%s ", tmp1);
	xstrcat(out, tmp_line);

	snprintf(tmp_line, sizeof(tmp_line), "TimeLimit=");
	xstrcat(out, tmp_line);
	if (job_ptr->time_limit == INFINITE)
		sprintf(tmp_line, "UNLIMITED");
	else if (job_ptr->time_limit == NO_VAL)
		sprintf(tmp_line, "Partition_Limit");
	else {
		secs2time_str(job_ptr->time_limit * 60, tmp_line,
			      sizeof(tmp_line));
	}
	xstrcat(out, tmp_line);
	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");

	/****** Line 7 ******/
	slurm_make_time_str((time_t *)&job_ptr->submit_time, time_str,
			    sizeof(time_str));
	snprintf(tmp_line, sizeof(tmp_line), "SubmitTime=%s ", time_str);
	xstrcat(out, tmp_line);

	slurm_make_time_str((time_t *)&job_ptr->eligible_time, time_str,
			    sizeof(time_str));
	snprintf(tmp_line, sizeof(tmp_line), "EligibleTime=%s", time_str);
	xstrcat(out, tmp_line);
	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");

	/****** Line 8 ******/
	slurm_make_time_str((time_t *)&job_ptr->start_time, time_str,
			    sizeof(time_str));
	snprintf(tmp_line, sizeof(tmp_line), "StartTime=%s ", time_str);
	xstrcat(out, tmp_line);

	snprintf(tmp_line, sizeof(tmp_line), "EndTime=");
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

	/****** Line 9 ******/
	if (job_ptr->suspend_time) {
		slurm_make_time_str ((time_t *)&job_ptr->suspend_time,
				     time_str, sizeof(time_str));
	} else {
		strncpy(time_str, "None", sizeof(time_str));
	}
	snprintf(tmp_line, sizeof(tmp_line),
		 "SuspendTime=%s SecsPreSuspend=%ld",
		 time_str, (long int)job_ptr->pre_sus_time);
	xstrcat(out, tmp_line);
	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");

	/****** Line 10 ******/
	snprintf(tmp_line, sizeof(tmp_line),
		 "Partition=%s AllocNode:Sid=%s:%u",
		 job_ptr->partition, job_ptr->alloc_node, job_ptr->alloc_sid);
	xstrcat(out, tmp_line);
	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");

	/****** Line 11 ******/
	snprintf(tmp_line, sizeof(tmp_line), "Req%s=%s Exc%s=%s",
		 nodelist, job_ptr->req_nodes, nodelist, job_ptr->exc_nodes);
	xstrcat(out, tmp_line);
	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");

	/****** Line 12 ******/
	xstrfmtcat(out, "%s=", nodelist);
	xstrcat(out, job_ptr->nodes);
	if(job_ptr->nodes && ionodes) {
		snprintf(tmp_line, sizeof(tmp_line), "[%s]", ionodes);
		xstrcat(out, tmp_line);
		xfree(ionodes);
	}
	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");

	/****** Line 13 ******/
#ifdef HAVE_BG
	select_g_select_jobinfo_get(job_ptr->select_jobinfo,
				    SELECT_JOBDATA_NODE_CNT,
				    &min_nodes);
	if ((min_nodes == 0) || (min_nodes == NO_VAL)) {
		min_nodes = job_ptr->num_nodes;
		max_nodes = job_ptr->max_nodes;
	} else if(job_ptr->max_nodes)
		max_nodes = min_nodes;
#else
	min_nodes = job_ptr->num_nodes;
	max_nodes = job_ptr->max_nodes;
#endif
	_sprint_range(tmp1, sizeof(tmp1), job_ptr->num_cpus, job_ptr->max_cpus);
	_sprint_range(tmp2, sizeof(tmp2), min_nodes, max_nodes);
	snprintf(tmp_line, sizeof(tmp_line),
		 "NumNodes=%s NumCPUs=%s CPUs/Task=%u ReqS:C:T=%u:%u:%u",
		 tmp2, tmp1, job_ptr->cpus_per_task,
		 job_ptr->min_sockets,
		 job_ptr->min_cores,
		 job_ptr->min_threads);
	xstrcat(out, tmp_line);
	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");

	if (!job_resrcs)
		goto line14;

#ifndef HAVE_BG
	if (!job_resrcs->core_bitmap)
		goto line14;

	last  = bit_fls(job_resrcs->core_bitmap);
	if (last == -1)
		goto line14;

	hl = hostlist_create(job_ptr->nodes);
	if (!hl) {
		error("slurm_sprint_job_info: hostlist_create: %s",
		      job_ptr->nodes);
		return NULL;
	}
	hl_last = hostlist_create(NULL);
	if (!hl_last) {
		error("slurm_sprint_job_info: hostlist_create: NULL");
		hostlist_destroy(hl);
		return NULL;
	}

	bit_inx = 0;
	i = sock_inx = sock_reps = 0;
	abs_node_inx = job_ptr->node_inx[i];

/*	tmp1[] stores the current cpu(s) allocated	*/
	tmp2[0] = '\0';	/* stores last cpu(s) allocated */
	for (rel_node_inx=0; rel_node_inx < job_resrcs->nhosts;
	     rel_node_inx++) {

		if (sock_reps >=
		    job_resrcs->sock_core_rep_count[sock_inx]) {
			sock_inx++;
			sock_reps = 0;
		}
		sock_reps++;

		bit_reps = job_resrcs->sockets_per_node[sock_inx] *
			job_resrcs->cores_per_socket[sock_inx];

		core_bitmap = bit_alloc(bit_reps);
		if (core_bitmap == NULL) {
			error("bit_alloc malloc failure");
			hostlist_destroy(hl_last);
			hostlist_destroy(hl);
			return NULL;
		}

		for (j=0; j < bit_reps; j++) {
			if (bit_test(job_resrcs->core_bitmap, bit_inx))
				bit_set(core_bitmap, j);
			bit_inx++;
		}

		bit_fmt(tmp1, sizeof(tmp1), core_bitmap);
		bit_free(core_bitmap);
		host = hostlist_shift(hl);
/*
 *		If the allocation values for this host are not the same as the
 *		last host, print the report of the last group of hosts that had
 *		identical allocation values.
 */
		if (strcmp(tmp1, tmp2) ||
		    (last_mem_alloc_ptr != job_resrcs->memory_allocated) ||
		    (job_resrcs->memory_allocated &&
		     (last_mem_alloc !=
		      job_resrcs->memory_allocated[rel_node_inx]))) {
			if (hostlist_count(hl_last)) {
				hostlist_ranged_string(hl_last,
						       sizeof(last_hosts),
						       last_hosts);
				snprintf(tmp_line, sizeof(tmp_line),
					 "  Nodes=%s CPU_IDs=%s Mem=%u",
					 last_hosts, tmp2, last_mem_alloc_ptr ?
					 last_mem_alloc : 0);
				xstrcat(out, tmp_line);
				if (one_liner)
					xstrcat(out, " ");
				else
					xstrcat(out, "\n   ");

				hostlist_destroy(hl_last);
				hl_last = hostlist_create(NULL);
			}
			strcpy(tmp2, tmp1);
			last_mem_alloc_ptr = job_resrcs->memory_allocated;
			if (last_mem_alloc_ptr)
				last_mem_alloc = job_resrcs->
					memory_allocated[rel_node_inx];
			else
				last_mem_alloc = NO_VAL;
		}
		hostlist_push_host(hl_last, host);
		free(host);

		if (bit_inx > last)
			break;

		if (abs_node_inx > job_ptr->node_inx[i+1]) {
			i += 2;
			abs_node_inx = job_ptr->node_inx[i];
		} else {
			abs_node_inx++;
		}
	}

	if (hostlist_count(hl_last)) {
		hostlist_ranged_string(hl_last, sizeof(last_hosts), last_hosts);
		snprintf(tmp_line, sizeof(tmp_line),
			 "  Nodes=%s CPU_IDs=%s Mem=%u", last_hosts, tmp2,
			 last_mem_alloc_ptr ? last_mem_alloc : 0);
		xstrcat(out, tmp_line);
		if (one_liner)
			xstrcat(out, " ");
		else
			xstrcat(out, "\n   ");
	}
	hostlist_destroy(hl);
	hostlist_destroy(hl_last);
#else
	if ((job_resrcs->cpu_array_cnt > 0) &&
	    (job_resrcs->cpu_array_value) &&
	    (job_resrcs->cpu_array_reps)) {
		int length = 0;
		xstrcat(out, "CPUs=");
		length += 10;
		for (i = 0; i < job_resrcs->cpu_array_cnt; i++) {
			if (length > 70) {
				/* skip to last CPU group entry */
			    	if (i < job_resrcs->cpu_array_cnt - 1) {
			    		continue;
				}
				/* add elipsis before last entry */
			    	xstrcat(out, "...,");
				length += 4;
			}

			snprintf(tmp_line, sizeof(tmp_line), "%d",
				 job_resrcs->cpu_array_value[i]);
			xstrcat(out, tmp_line);
			length += strlen(tmp_line);
		    	if (job_resrcs->cpu_array_reps[i] > 1) {
				snprintf(tmp_line, sizeof(tmp_line), "*%d",
					 job_resrcs->cpu_array_reps[i]);
				xstrcat(out, tmp_line);
				length += strlen(tmp_line);
			}
			if (i < job_resrcs->cpu_array_cnt - 1) {
				xstrcat(out, ",");
				length++;
			}
		}
		if (one_liner)
			xstrcat(out, " ");
		else
			xstrcat(out, "\n   ");
	}
#endif

	/****** Line 14 ******/
line14:
	if (job_ptr->pn_min_memory & MEM_PER_CPU) {
		job_ptr->pn_min_memory &= (~MEM_PER_CPU);
		tmp3_ptr = "CPU";
	} else
		tmp3_ptr = "Node";
#ifdef HAVE_BG
	convert_num_unit((float)job_ptr->pn_min_cpus, tmp1, sizeof(tmp1),
			 UNIT_NONE);
	snprintf(tmp_line, sizeof(tmp_line), "MinCPUsNode=%s",	tmp1);
#else
	snprintf(tmp_line, sizeof(tmp_line), "MinCPUsNode=%u",
		 job_ptr->pn_min_cpus);
#endif
	xstrcat(out, tmp_line);
	convert_num_unit((float)job_ptr->pn_min_memory, tmp1, sizeof(tmp1),
			 UNIT_MEGA);
	convert_num_unit((float)job_ptr->pn_min_tmp_disk, tmp2, sizeof(tmp2),
			 UNIT_MEGA);
	snprintf(tmp_line, sizeof(tmp_line),
		 " MinMemory%s=%s MinTmpDiskNode=%s",
		 tmp3_ptr, tmp1, tmp2);
	xstrcat(out, tmp_line);
	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");

	/****** Line 15 ******/
	snprintf(tmp_line, sizeof(tmp_line),
		 "Features=%s Reservation=%s",
		 job_ptr->features, job_ptr->resv_name);
	xstrcat(out, tmp_line);
	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");

	/****** Line 16 ******/
	snprintf(tmp_line, sizeof(tmp_line),
		 "Shared=%s Contiguous=%d Licenses=%s Network=%s",
		 (job_ptr->shared == 0 ? "0" :
		  job_ptr->shared == 1 ? "1" : "OK"),
		 job_ptr->contiguous, job_ptr->licenses, job_ptr->network);
	xstrcat(out, tmp_line);

	/****** Lines 17, 18 (optional, batch only) ******/
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

#ifdef HAVE_BG
	/****** Line 19 (optional) ******/
	select_g_select_jobinfo_sprint(job_ptr->select_jobinfo,
				       select_buf, sizeof(select_buf),
				       SELECT_PRINT_BG_ID);
	if (select_buf[0] != '\0') {
		if (one_liner)
			xstrcat(out, " ");
		else
			xstrcat(out, "\n   ");
		snprintf(tmp_line, sizeof(tmp_line),
			 "Block_ID=%s", select_buf);
		xstrcat(out, tmp_line);
	}

	/****** Line 20 (optional) ******/
	select_g_select_jobinfo_sprint(job_ptr->select_jobinfo,
				       select_buf, sizeof(select_buf),
				       SELECT_PRINT_MIXED_SHORT);
	if (select_buf[0] != '\0') {
		if (one_liner)
			xstrcat(out, " ");
		else
			xstrcat(out, "\n   ");
		xstrcat(out, select_buf);
	}

#ifdef HAVE_BGL
	/****** Line 21 (optional) ******/
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
#endif
	/****** Line 22 (optional) ******/
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
	/****** Line 23 (optional) ******/
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
	/****** Line 24 (optional) ******/
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

	/****** Line 25 (optional) ******/
	if (job_ptr->comment) {
		if (one_liner)
			xstrcat(out, " ");
		else
			xstrcat(out, "\n   ");
		snprintf(tmp_line, sizeof(tmp_line), "Comment=%s ",
			 job_ptr->comment);
		xstrcat(out, tmp_line);
	}

	if (one_liner)
		xstrcat(out, "\n");
	else
		xstrcat(out, "\n\n");

	return out;

}

/*
 * slurm_load_jobs - issue RPC to get all job configuration
 *	information if changed since update_time
 * IN update_time - time of current configuration data
 * IN job_info_msg_pptr - place to store a job configuration pointer
 * IN show_flags -  job filtering option: 0, SHOW_ALL or SHOW_DETAIL
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
 * IN show_flags -  job filtering option: 0, SHOW_ALL or SHOW_DETAIL
 * RET 0 or -1 on error
 * NOTE: free the response using slurm_free_job_info_msg
 */
extern int
slurm_load_job (job_info_msg_t **resp, uint32_t job_id, uint16_t show_flags)
{
	int rc;
	slurm_msg_t resp_msg;
	slurm_msg_t req_msg;
	job_id_msg_t req;

	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);

	req.job_id = job_id;
	req.show_flags = show_flags;
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
 * RET: READY_* values as defined in slurm.h
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

extern int slurm_job_cpus_allocated_on_node_id(
	job_resources_t *job_resrcs_ptr, int node_id)
{
	int i;
	int start_node=-1; /* start with -1 less so the array reps
			    * lines up correctly */

	if (!job_resrcs_ptr) {
		error("slurm_cpus_used_on_node_id: job_resources not set");
		return -1;
	}

	for (i = 0; i < job_resrcs_ptr->cpu_array_cnt; i++) {
		start_node += job_resrcs_ptr->cpu_array_reps[i];
		if(start_node >= node_id)
			break;
	}

	return job_resrcs_ptr->cpu_array_value[i];
}

extern int slurm_job_cpus_allocated_on_node(
	job_resources_t *job_resrcs_ptr, const char *node)
{
	int node_id;

	if (!job_resrcs_ptr) {
		error("slurm_cpus_used_on_node: job_resources not set");
		return -1;
	} else if(!node) {
		error("slurm_cpus_used_on_node: no node given");
		return -1;
	} else if(!job_resrcs_ptr->node_hl) {
		error("slurm_cpus_used_on_node: "
		      "hostlist not set in job_resources");
		return -1;
	} else if((node_id = hostlist_find(job_resrcs_ptr->node_hl, node))
		  == -1) {
		error("slurm_cpus_used_on_node: "
		      "node %s is not in this allocation", node);
		return -1;
	}

	return slurm_job_cpus_allocated_on_node_id(job_resrcs_ptr, node_id);
}

