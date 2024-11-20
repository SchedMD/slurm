/*****************************************************************************\
 *  info_job.c - job information functions for scontrol.
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include <arpa/inet.h>
#include <grp.h>
#include <fcntl.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "scontrol.h"
#include "src/common/bitstring.h"
#include "src/common/cpu_frequency.h"
#include "src/common/proc_args.h"
#include "src/common/slurm_time.h"
#include "src/common/stepd_api.h"

#include "src/interfaces/data_parser.h"
#include "src/common/openapi.h"

#define CONTAINER_ID_TAG "containerid="
#define POLL_SLEEP	3	/* retry interval in seconds  */

typedef struct add_to_listjobs_list_args {
	list_t *jobs_seen;
	list_t *listjobs_list;
} add_to_listjobs_list_args_t;

static node_info_msg_t *_get_node_info_for_jobs(void)
{
	int error_code;
	node_info_msg_t *node_info_msg = NULL;
	uint16_t show_flags = 0;

	if (old_node_info_ptr)
		return old_node_info_ptr;

	/* Must load all nodes including hidden for cross-index
	 * from job's node_inx to node table to work */
	/*if (all_flag)		Always set this flag */
	show_flags |= SHOW_ALL;
	if (federation_flag)
		show_flags |= SHOW_FEDERATION;
	if (local_flag)
		show_flags |= SHOW_LOCAL;
	error_code = scontrol_load_nodes(&node_info_msg, show_flags);
	if (error_code) {
		exit_code = 1;
		if (quiet_flag != 1)
			slurm_perror ("slurm_load_nodes error");
		return NULL;
	}

	return node_info_msg;
}

/* This set of functions loads/free node information so that we can map a job's
 * core bitmap to it's CPU IDs based upon the thread count on each node. */

static uint32_t _threads_per_core(char *host)
{
	node_info_msg_t *node_info_msg = NULL;
	uint32_t i, threads = 1;

	if (!host)
		return threads;

	if (!(node_info_msg = _get_node_info_for_jobs()))
		return threads;

	for (i = 0; i < node_info_msg->record_count; i++) {
		if (node_info_msg->node_array[i].name &&
		    !xstrcmp(host, node_info_msg->node_array[i].name)) {
			threads = node_info_msg->node_array[i].threads;
			break;
		}
	}

	return threads;
}

static void _sprint_range(char *str, uint32_t str_size,
			  uint32_t lower, uint32_t upper)
{
	if (upper > 0)
		snprintf(str, str_size, "%u-%u", lower, upper);
	else
		snprintf(str, str_size, "%u", lower);
}

/*
 * _sprint_job_info - output information about a specific Slurm
 *	job based upon message as loaded using slurm_load_jobs
 * IN job_ptr - an individual job information record pointer
 * RET out - char * containing formatted output (must be freed after call)
 *           NULL is returned on failure.
 */
static char *_sprint_job_info(job_info_t *job_ptr)
{
	int i, j, k;
	char time_str[256], *group_name, *user_name;
	char *gres_last = "", tmp1[128], tmp2[128];
	char *tmp6_ptr;
	char tmp_line[1024 * 128];
	char tmp_path[PATH_MAX];
	uint16_t exit_status = 0, term_sig = 0;
	job_resources_t *job_resrcs = job_ptr->job_resrcs;
	char *job_size_str = NULL;
	char *out = NULL;
	time_t run_time;
	uint32_t min_nodes, max_nodes = 0;
	char *nodelist = "NodeList";
	char *sorted_nodelist = NULL;
	bitstr_t *cpu_bitmap;
	char *host;
	int sock_inx, sock_reps, last;
	int abs_node_inx, rel_node_inx;
	int64_t nice;
	int bit_inx, bit_reps;
	uint64_t *last_mem_alloc_ptr = NULL;
	uint64_t last_mem_alloc = NO_VAL64;
	char *last_hosts;
	hostlist_t *hl, *hl_last;
	uint32_t threads;
	char *line_end = (one_liner) ? " " : "\n   ";

	if (job_ptr->job_id == 0)	/* Duplicated sibling job record */
		return NULL;

	/****** Line 1 ******/
	xstrfmtcat(out, "JobId=%u ", job_ptr->job_id);

	if (job_ptr->array_job_id) {
		if (job_ptr->array_task_str) {
			xstrfmtcat(out, "ArrayJobId=%u ArrayTaskId=%s ",
				   job_ptr->array_job_id,
				   job_ptr->array_task_str);
		} else {
			xstrfmtcat(out, "ArrayJobId=%u ArrayTaskId=%u ",
				   job_ptr->array_job_id,
				   job_ptr->array_task_id);
		}
		if (job_ptr->array_max_tasks) {
			xstrfmtcat(out, "ArrayTaskThrottle=%u ",
				   job_ptr->array_max_tasks);
		}
	} else if (job_ptr->het_job_id) {
		xstrfmtcat(out, "HetJobId=%u HetJobOffset=%u ",
			   job_ptr->het_job_id, job_ptr->het_job_offset);
	}
	xstrfmtcat(out, "JobName=%s", job_ptr->name);
	xstrcat(out, line_end);

	/****** Line ******/
	if (job_ptr->het_job_id_set) {
		xstrfmtcat(out, "HetJobIdSet=%s", job_ptr->het_job_id_set);
		xstrcat(out, line_end);
	}

	/****** Line ******/
	user_name = uid_to_string((uid_t) job_ptr->user_id);
	group_name = gid_to_string((gid_t) job_ptr->group_id);
	xstrfmtcat(out, "UserId=%s(%u) GroupId=%s(%u) MCS_label=%s",
		   user_name, job_ptr->user_id, group_name, job_ptr->group_id,
		   (job_ptr->mcs_label==NULL) ? "N/A" : job_ptr->mcs_label);
	xfree(user_name);
	xfree(group_name);
	xstrcat(out, line_end);

	/****** Line 3 ******/
	nice = ((int64_t)job_ptr->nice) - NICE_OFFSET;
	xstrfmtcat(out, "Priority=%u Nice=%"PRIi64" Account=%s QOS=%s",
		   job_ptr->priority, nice, job_ptr->account, job_ptr->qos);
	if (slurm_get_track_wckey())
		xstrfmtcat(out, " WCKey=%s", job_ptr->wckey);
	xstrcat(out, line_end);

	/****** Line 4 ******/
	xstrfmtcat(out, "JobState=%s ", job_state_string(job_ptr->job_state));

	if (job_ptr->state_desc) {
		/* Replace white space with underscore for easier parsing */
		for (j=0; job_ptr->state_desc[j]; j++) {
			if (isspace((int)job_ptr->state_desc[j]))
				job_ptr->state_desc[j] = '_';
		}
		xstrfmtcat(out, "Reason=%s ", job_ptr->state_desc);
	} else
		xstrfmtcat(out, "Reason=%s ",
			   job_state_reason_string(job_ptr->state_reason));

	if (job_ptr->failed_node)
		xstrfmtcat(out, "FailedNode=%s ", job_ptr->failed_node);

	xstrfmtcat(out, "Dependency=%s", job_ptr->dependency);
	xstrcat(out, line_end);

	/****** Line 5 ******/
	xstrfmtcat(out, "Requeue=%u Restarts=%u BatchFlag=%u Reboot=%u ",
		 job_ptr->requeue, job_ptr->restart_cnt, job_ptr->batch_flag,
		 job_ptr->reboot);
	exit_status = term_sig = 0;
	if (WIFSIGNALED(job_ptr->exit_code))
		term_sig = WTERMSIG(job_ptr->exit_code);
	else if (WIFEXITED(job_ptr->exit_code))
		exit_status = WEXITSTATUS(job_ptr->exit_code);
	xstrfmtcat(out, "ExitCode=%u:%u", exit_status, term_sig);
	xstrcat(out, line_end);

	/****** Line 5a (optional) ******/
	if (detail_flag) {
		exit_status = term_sig = 0;
		if (WIFSIGNALED(job_ptr->derived_ec))
			term_sig = WTERMSIG(job_ptr->derived_ec);
		else if (WIFEXITED(job_ptr->derived_ec))
			exit_status = WEXITSTATUS(job_ptr->derived_ec);
		xstrfmtcat(out, "DerivedExitCode=%u:%u", exit_status, term_sig);
		xstrcat(out, line_end);
	}

	/****** Line 6 ******/
	if (IS_JOB_PENDING(job_ptr) || !job_ptr->start_time)
		run_time = 0;
	else if (IS_JOB_SUSPENDED(job_ptr))
		run_time = job_ptr->pre_sus_time;
	else {
		time_t end_time;
		if (IS_JOB_RUNNING(job_ptr) || (job_ptr->end_time == 0))
			end_time = time(NULL);
		else
			end_time = job_ptr->end_time;
		if (job_ptr->suspend_time) {
			run_time = (time_t)
				(difftime(end_time, job_ptr->suspend_time)
				 + job_ptr->pre_sus_time);
		} else
			run_time = (time_t)
				difftime(end_time, job_ptr->start_time);
	}
	secs2time_str(run_time, time_str, sizeof(time_str));
	xstrfmtcat(out, "RunTime=%s ", time_str);

	if (job_ptr->time_limit == NO_VAL)
		xstrcat(out, "TimeLimit=Partition_Limit ");
	else {
		mins2time_str(job_ptr->time_limit, time_str, sizeof(time_str));
		xstrfmtcat(out, "TimeLimit=%s ", time_str);
	}

	if (job_ptr->time_min == 0)
		xstrcat(out, "TimeMin=N/A");
	else {
		mins2time_str(job_ptr->time_min, time_str, sizeof(time_str));
		xstrfmtcat(out, "TimeMin=%s", time_str);
	}
	xstrcat(out, line_end);

	/****** Line 7 ******/
	slurm_make_time_str(&job_ptr->submit_time, time_str, sizeof(time_str));
	xstrfmtcat(out, "SubmitTime=%s ", time_str);

	slurm_make_time_str(&job_ptr->eligible_time, time_str, sizeof(time_str));
	xstrfmtcat(out, "EligibleTime=%s", time_str);

	xstrcat(out, line_end);

	/****** Line 7.5 ******/
	slurm_make_time_str(&job_ptr->accrue_time, time_str, sizeof(time_str));
	xstrfmtcat(out, "AccrueTime=%s", time_str);

	xstrcat(out, line_end);

	/****** Line 8 (optional) ******/
	if (job_ptr->resize_time) {
		slurm_make_time_str(&job_ptr->resize_time, time_str,
				    sizeof(time_str));
		xstrfmtcat(out, "ResizeTime=%s", time_str);
		xstrcat(out, line_end);
	}

	/****** Line 9 ******/
	slurm_make_time_str(&job_ptr->start_time, time_str, sizeof(time_str));
	xstrfmtcat(out, "StartTime=%s ", time_str);

	if ((job_ptr->time_limit == INFINITE) &&
	    (job_ptr->end_time > time(NULL)))
		xstrcat(out, "EndTime=Unknown ");
	else {
		slurm_make_time_str(&job_ptr->end_time, time_str,
				    sizeof(time_str));
		xstrfmtcat(out, "EndTime=%s ", time_str);
	}

	if (job_ptr->deadline) {
		slurm_make_time_str(&job_ptr->deadline, time_str,
				    sizeof(time_str));
		xstrfmtcat(out, "Deadline=%s", time_str);
	} else {
		xstrcat(out, "Deadline=N/A");
	}

	xstrcat(out, line_end);

	/****** Line ******/

	if (job_ptr->bitflags & CRON_JOB || job_ptr->cronspec) {
		if (job_ptr->bitflags & CRON_JOB)
			xstrcat(out, "CronJob=Yes ");
		xstrfmtcat(out, "CrontabSpec=\"%s\"", job_ptr->cronspec);
		xstrcat(out, line_end);
	}

	/****** Line ******/
	/*
	 * only print this line if preemption is enabled and job started
	 * 	see src/slurmctld/job_mgr.c:pack_job, 'preemptable'
	 */
	if (job_ptr->preemptable_time) {
		slurm_make_time_str(&job_ptr->preemptable_time,
				    time_str, sizeof(time_str));
		xstrfmtcat(out, "PreemptEligibleTime=%s ", time_str);

		if (job_ptr->preempt_time == 0)
			xstrcat(out, "PreemptTime=None");
		else {
			slurm_make_time_str(&job_ptr->preempt_time, time_str,
					    sizeof(time_str));
			xstrfmtcat(out, "PreemptTime=%s", time_str);
		}

		xstrcat(out, line_end);
	}

	/****** Line 10 ******/
	if (job_ptr->suspend_time) {
		slurm_make_time_str(&job_ptr->suspend_time, time_str,
				    sizeof(time_str));
		xstrfmtcat(out, "SuspendTime=%s ", time_str);
	} else
		xstrcat(out, "SuspendTime=None ");

	xstrfmtcat(out, "SecsPreSuspend=%ld ", (long int)job_ptr->pre_sus_time);

	slurm_make_time_str(&job_ptr->last_sched_eval, time_str,
			    sizeof(time_str));
	xstrfmtcat(out, "LastSchedEval=%s Scheduler=%s%s", time_str,
		   job_ptr->bitflags & BACKFILL_SCHED ? "Backfill" : "Main",
		   job_ptr->bitflags & BACKFILL_LAST ? ":*" : "");
	xstrcat(out, line_end);

	/****** Line 11 ******/
	xstrfmtcat(out, "Partition=%s AllocNode:Sid=%s:%u",
		   job_ptr->partition, job_ptr->alloc_node, job_ptr->alloc_sid);
	xstrcat(out, line_end);

	/****** Line 12 ******/
	xstrfmtcat(out, "Req%s=%s Exc%s=%s", nodelist, job_ptr->req_nodes,
		   nodelist, job_ptr->exc_nodes);
	xstrcat(out, line_end);

	/****** Line 13 ******/
	sorted_nodelist = slurm_sort_node_list_str(job_ptr->nodes);
	xstrfmtcat(out, "%s=%s", nodelist, sorted_nodelist);
	xfree(sorted_nodelist);

	if (job_ptr->sched_nodes)
		xstrfmtcat(out, " Sched%s=%s", nodelist, job_ptr->sched_nodes);

	xstrcat(out, line_end);

	/****** Line 14 (optional) ******/
	if (job_ptr->batch_features)
		xstrfmtcat(out, "BatchFeatures=%s", job_ptr->batch_features);
	if (job_ptr->batch_host) {
		char *sep = "";
		if (job_ptr->batch_features)
			sep = " ";
		xstrfmtcat(out, "%sBatchHost=%s", sep, job_ptr->batch_host);
	}
	if (job_ptr->batch_features || job_ptr->batch_host)
		xstrcat(out, line_end);

	/****** Line 14 (optional) ******/
	if (job_ptr->bitflags & STEPMGR_ENABLED) {
		xstrfmtcat(out, "StepMgrEnabled=Yes");
		xstrcat(out, line_end);
	}

	/****** Line 14a (optional) ******/
	if (job_ptr->fed_siblings_active || job_ptr->fed_siblings_viable) {
		xstrfmtcat(out, "FedOrigin=%s FedViableSiblings=%s FedActiveSiblings=%s",
			   job_ptr->fed_origin_str,
			   job_ptr->fed_siblings_viable_str,
			   job_ptr->fed_siblings_active_str);
		xstrcat(out, line_end);
	}

	/****** Line 15 ******/
	if (IS_JOB_PENDING(job_ptr)) {
		min_nodes = job_ptr->num_nodes;
		max_nodes = job_ptr->max_nodes;
		job_size_str = job_ptr->job_size_str;
		if (max_nodes && (max_nodes < min_nodes))
			min_nodes = max_nodes;
	} else {
		min_nodes = job_ptr->num_nodes;
		max_nodes = 0;
	}

	if (job_size_str)
		snprintf(tmp_line, sizeof(tmp_line), "%s", job_size_str);
	else
		_sprint_range(tmp_line, sizeof(tmp_line), min_nodes, max_nodes);
	xstrfmtcat(out, "NumNodes=%s ", tmp_line);
	_sprint_range(tmp_line, sizeof(tmp_line), job_ptr->num_cpus,
		      job_ptr->max_cpus);
	xstrfmtcat(out, "NumCPUs=%s ", tmp_line);

	if (job_ptr->num_tasks == NO_VAL)
		xstrcat(out, "NumTasks=N/A ");
	else
		xstrfmtcat(out, "NumTasks=%u ", job_ptr->num_tasks);

	if (job_ptr->cpus_per_task == NO_VAL16)
		xstrfmtcat(out, "CPUs/Task=N/A ");
	else
		xstrfmtcat(out, "CPUs/Task=%u ", job_ptr->cpus_per_task);

	if (job_ptr->boards_per_node == NO_VAL16)
		xstrcat(out, "ReqB:S:C:T=*:");
	else
		xstrfmtcat(out, "ReqB:S:C:T=%u:", job_ptr->boards_per_node);

	if (job_ptr->sockets_per_board == NO_VAL16)
		xstrcat(out, "*:");
	else
		xstrfmtcat(out, "%u:", job_ptr->sockets_per_board);

	if (job_ptr->cores_per_socket == NO_VAL16)
		xstrcat(out, "*:");
	else
		xstrfmtcat(out, "%u:", job_ptr->cores_per_socket);

	if (job_ptr->threads_per_core == NO_VAL16)
		xstrcat(out, "*");
	else
		xstrfmtcat(out, "%u", job_ptr->threads_per_core);

	xstrcat(out, line_end);

	/****** Line 16 ******/
	/* Tres should already of been converted at this point from simple */
	xstrfmtcat(out, "ReqTRES=%s", job_ptr->tres_req_str);
	xstrcat(out, line_end);

	/****** Line ******/
	xstrfmtcat(out, "AllocTRES=%s", job_ptr->tres_alloc_str);
	xstrcat(out, line_end);

	/****** Line 17 ******/
	if (job_ptr->sockets_per_node == NO_VAL16)
		xstrcat(out, "Socks/Node=* ");
	else
		xstrfmtcat(out, "Socks/Node=%u ", job_ptr->sockets_per_node);

	if (job_ptr->ntasks_per_node == NO_VAL16)
		xstrcat(out, "NtasksPerN:B:S:C=*:");
	else
		xstrfmtcat(out, "NtasksPerN:B:S:C=%u:",
			   job_ptr->ntasks_per_node);

	if (job_ptr->ntasks_per_board == NO_VAL16)
		xstrcat(out, "*:");
	else
		xstrfmtcat(out, "%u:", job_ptr->ntasks_per_board);

	if ((job_ptr->ntasks_per_socket == NO_VAL16) ||
	    (job_ptr->ntasks_per_socket == INFINITE16))
		xstrcat(out, "*:");
	else
		xstrfmtcat(out, "%u:", job_ptr->ntasks_per_socket);

	if ((job_ptr->ntasks_per_core == NO_VAL16) ||
	    (job_ptr->ntasks_per_core == INFINITE16))
		xstrcat(out, "* ");
	else
		xstrfmtcat(out, "%u ", job_ptr->ntasks_per_core);

	if (job_ptr->core_spec == NO_VAL16)
		xstrcat(out, "CoreSpec=*");
	else if (job_ptr->core_spec & CORE_SPEC_THREAD)
		xstrfmtcat(out, "ThreadSpec=%d",
			   (job_ptr->core_spec & (~CORE_SPEC_THREAD)));
	else
		xstrfmtcat(out, "CoreSpec=%u", job_ptr->core_spec);

	xstrcat(out, line_end);

	if (job_resrcs && job_resrcs->core_bitmap &&
	    ((last = bit_fls(job_resrcs->core_bitmap)) != -1)) {

		xstrfmtcat(out, "JOB_GRES=%s", job_ptr->gres_total);
		xstrcat(out, line_end);

		hl = hostlist_create(job_resrcs->nodes);
		if (!hl) {
			error("%s: hostlist_create: %s",
			      __func__, job_resrcs->nodes);
			return NULL;
		}
		hl_last = hostlist_create(NULL);
		if (!hl_last) {
			error("%s: hostlist_create: NULL", __func__);
			hostlist_destroy(hl);
			return NULL;
		}

		bit_inx = 0;
		i = sock_inx = sock_reps = 0;
		abs_node_inx = job_ptr->node_inx[i];

		gres_last = "";
		/* tmp1[] stores the current cpu(s) allocated */
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
			host = hostlist_shift(hl);
			threads = _threads_per_core(host);
			cpu_bitmap = bit_alloc(bit_reps * threads);
			for (j = 0; j < bit_reps; j++) {
				if (bit_test(job_resrcs->core_bitmap, bit_inx)){
					for (k = 0; k < threads; k++)
						bit_set(cpu_bitmap,
							(j * threads) + k);
				}
				bit_inx++;
			}
			bit_fmt(tmp1, sizeof(tmp1), cpu_bitmap);
			FREE_NULL_BITMAP(cpu_bitmap);
			/*
			 * If the allocation values for this host are not the
			 * same as the last host, print the report of the last
			 * group of hosts that had identical allocation values.
			 */
			if (xstrcmp(tmp1, tmp2) ||
			    ((rel_node_inx < job_ptr->gres_detail_cnt) &&
			     xstrcmp(job_ptr->gres_detail_str[rel_node_inx],
				     gres_last)) ||
			    (last_mem_alloc_ptr !=
			     job_resrcs->memory_allocated) ||
			    (job_resrcs->memory_allocated &&
			     (last_mem_alloc !=
			      job_resrcs->memory_allocated[rel_node_inx]))) {
				if (hostlist_count(hl_last)) {
					last_hosts =
						hostlist_ranged_string_xmalloc(
						hl_last);
					xstrfmtcat(out,
						   "  Nodes=%s CPU_IDs=%s "
						   "Mem=%"PRIu64" GRES=%s",
						   last_hosts, tmp2,
						   last_mem_alloc_ptr ?
						   last_mem_alloc : 0,
						   gres_last);
					xfree(last_hosts);
					xstrcat(out, line_end);

					hostlist_destroy(hl_last);
					hl_last = hostlist_create(NULL);
				}

				strcpy(tmp2, tmp1);
				if (rel_node_inx < job_ptr->gres_detail_cnt) {
					gres_last = job_ptr->
						    gres_detail_str[rel_node_inx];
				} else {
					gres_last = "";
				}
				last_mem_alloc_ptr =
					job_resrcs->memory_allocated;
				if (last_mem_alloc_ptr)
					last_mem_alloc = job_resrcs->
						memory_allocated[rel_node_inx];
				else
					last_mem_alloc = NO_VAL64;
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
			last_hosts = hostlist_ranged_string_xmalloc(hl_last);
			xstrfmtcat(out, "  Nodes=%s CPU_IDs=%s Mem=%"PRIu64" GRES=%s",
				 last_hosts, tmp2,
				 last_mem_alloc_ptr ? last_mem_alloc : 0,
				 gres_last);
			xfree(last_hosts);
			xstrcat(out, line_end);
		}
		hostlist_destroy(hl);
		hostlist_destroy(hl_last);
	}
	/****** Line 18 ******/

	/*
	 * If there is a mem_per_tres job->pn_min_memory will not be
	 * set, let's figure it from the first tres there.
	 */
	if (job_ptr->mem_per_tres) {
		tmp6_ptr = "TRES";
	} else if (job_ptr->pn_min_memory & MEM_PER_CPU) {
		job_ptr->pn_min_memory &= (~MEM_PER_CPU);
		tmp6_ptr = "CPU";
	} else
		tmp6_ptr = "Node";

	xstrfmtcat(out, "MinCPUsNode=%u ", job_ptr->pn_min_cpus);

	convert_num_unit((float)job_ptr->pn_min_memory, tmp1, sizeof(tmp1),
			 UNIT_MEGA, NO_VAL, CONVERT_NUM_UNIT_EXACT);
	convert_num_unit((float)job_ptr->pn_min_tmp_disk, tmp2, sizeof(tmp2),
			 UNIT_MEGA, NO_VAL, CONVERT_NUM_UNIT_EXACT);
	xstrfmtcat(out, "MinMemory%s=%s MinTmpDiskNode=%s",
		   tmp6_ptr, tmp1, tmp2);
	xstrcat(out, line_end);

	/****** Line ******/
	secs2time_str((time_t)job_ptr->delay_boot, tmp1, sizeof(tmp1));
	xstrfmtcat(out, "Features=%s DelayBoot=%s", job_ptr->features, tmp1);
	xstrcat(out, line_end);

	/****** Line (optional) ******/
	if (job_ptr->cluster_features) {
		xstrfmtcat(out, "ClusterFeatures=%s",
			   job_ptr->cluster_features);
		xstrcat(out, line_end);
	}

	/****** Line (optional) ******/
	if (job_ptr->prefer) {
		xstrfmtcat(out, "Prefer=%s", job_ptr->prefer);
		xstrcat(out, line_end);
	}

	/****** Line (optional) ******/
	if (job_ptr->resv_name) {
		xstrfmtcat(out, "Reservation=%s", job_ptr->resv_name);
		xstrcat(out, line_end);
	}

	/****** Line 20 ******/
	xstrfmtcat(out, "OverSubscribe=%s Contiguous=%d Licenses=%s Network=%s",
		   job_share_string(job_ptr->shared), job_ptr->contiguous,
		   job_ptr->licenses, job_ptr->network);
	xstrcat(out, line_end);

	/****** Line 21 ******/
	xstrfmtcat(out, "Command=%s", job_ptr->command);
	xstrcat(out, line_end);

	/****** Line 22 ******/
	xstrfmtcat(out, "WorkDir=%s", job_ptr->work_dir);

	/****** Line (optional) ******/
	if (job_ptr->admin_comment) {
		xstrcat(out, line_end);
		xstrfmtcat(out, "AdminComment=%s ", job_ptr->admin_comment);
	}

	/****** Line (optional) ******/
	if (job_ptr->system_comment) {
		xstrcat(out, line_end);
		xstrfmtcat(out, "SystemComment=%s ", job_ptr->system_comment);
	}

	/****** Line (optional) ******/
	if (job_ptr->comment) {
		xstrcat(out, line_end);
		xstrfmtcat(out, "Comment=%s ", job_ptr->comment);
	}

	/****** Line (optional) ******/
	if (job_ptr->extra) {
		xstrcat(out, line_end);
		xstrfmtcat(out, "Extra=%s ", job_ptr->extra);
	}

	/****** Line 30 (optional) ******/
	if (job_ptr->batch_flag) {
		xstrcat(out, line_end);
		slurm_get_job_stderr(tmp_path, sizeof(tmp_path), job_ptr);
		xstrfmtcat(out, "StdErr=%s", tmp_path);
	}

	/****** Line 31 (optional) ******/
	if (job_ptr->batch_flag) {
		xstrcat(out, line_end);
		slurm_get_job_stdin(tmp_path, sizeof(tmp_path), job_ptr);
		xstrfmtcat(out, "StdIn=%s", tmp_path);
	}

	/****** Line 32 (optional) ******/
	if (job_ptr->batch_flag) {
		xstrcat(out, line_end);
		slurm_get_job_stdout(tmp_path, sizeof(tmp_path), job_ptr);
		xstrfmtcat(out, "StdOut=%s", tmp_path);
	}

	/****** Line 34 (optional) ******/
	if (job_ptr->req_switch) {
		char time_buf[32];
		xstrcat(out, line_end);
		secs2time_str((time_t) job_ptr->wait4switch, time_buf,
			      sizeof(time_buf));
		xstrfmtcat(out, "Switches=%u@%s",
			   job_ptr->req_switch, time_buf);
	}

	/****** Line 35 (optional) ******/
	if (job_ptr->burst_buffer) {
		xstrcat(out, line_end);
		xstrfmtcat(out, "BurstBuffer=%s", job_ptr->burst_buffer);
	}

	/****** Line (optional) ******/
	if (job_ptr->burst_buffer_state) {
		xstrcat(out, line_end);
		xstrfmtcat(out, "BurstBufferState=%s",
			   job_ptr->burst_buffer_state);
	}

	/****** Line 36 (optional) ******/
	if (cpu_freq_debug(NULL, NULL, tmp1, sizeof(tmp1),
			   job_ptr->cpu_freq_gov, job_ptr->cpu_freq_min,
			   job_ptr->cpu_freq_max, NO_VAL) != 0) {
		xstrcat(out, line_end);
		xstrcat(out, tmp1);
	}

	/****** Line 38 (optional) ******/
	if (job_ptr->bitflags &
	    (GRES_DISABLE_BIND | GRES_ENFORCE_BIND |
	     GRES_MULT_TASKS_PER_SHARING |
	     GRES_ONE_TASK_PER_SHARING | KILL_INV_DEP | NO_KILL_INV_DEP |
	     SPREAD_JOB)) {
		xstrcat(out, line_end);
		if (job_ptr->bitflags & GRES_ALLOW_TASK_SHARING)
			xstrcat(out, "GresAllowTaskSharing=Yes,");
		if (job_ptr->bitflags & GRES_DISABLE_BIND)
			xstrcat(out, "GresEnforceBind=No,");
		if (job_ptr->bitflags & GRES_ENFORCE_BIND)
			xstrcat(out, "GresEnforceBind=Yes,");
		if (job_ptr->bitflags & GRES_MULT_TASKS_PER_SHARING)
			xstrcat(out, "GresOneTaskPerSharing=No,");
		if (job_ptr->bitflags & GRES_ONE_TASK_PER_SHARING)
			xstrcat(out, "GresOneTaskPerSharing=Yes,");
		if (job_ptr->bitflags & KILL_INV_DEP)
			xstrcat(out, "KillOInInvalidDependent=Yes,");
		if (job_ptr->bitflags & NO_KILL_INV_DEP)
			xstrcat(out, "KillOInInvalidDependent=No,");
		if (job_ptr->bitflags & SPREAD_JOB)
			xstrcat(out, "SpreadJob=Yes,");

		out[strlen(out)-1] = '\0'; /* remove trailing ',' */
	}

	/****** Line (optional) ******/
	if (job_ptr->oom_kill_step != NO_VAL16) {
		xstrcat(out, line_end);
		xstrfmtcat(out, "OOMKillStep=%u", job_ptr->oom_kill_step);
	}

	/****** Line (optional) ******/
	if (job_ptr->cpus_per_tres) {
		xstrcat(out, line_end);
		xstrfmtcat(out, "CpusPerTres=%s", job_ptr->cpus_per_tres);
	}

	/****** Line (optional) ******/
	if (job_ptr->mem_per_tres) {
		xstrcat(out, line_end);
		xstrfmtcat(out, "MemPerTres=%s", job_ptr->mem_per_tres);
	}

	/****** Line (optional) ******/
	if (job_ptr->tres_bind) {
		xstrcat(out, line_end);
		xstrfmtcat(out, "TresBind=%s", job_ptr->tres_bind);
	}

	/****** Line (optional) ******/
	if (job_ptr->tres_freq) {
		xstrcat(out, line_end);
		xstrfmtcat(out, "TresFreq=%s", job_ptr->tres_freq);
	}

	/****** Line (optional) ******/
	if (job_ptr->tres_per_job) {
		xstrcat(out, line_end);
		xstrfmtcat(out, "TresPerJob=%s", job_ptr->tres_per_job);
	}

	/****** Line (optional) ******/
	if (job_ptr->tres_per_node) {
		xstrcat(out, line_end);
		xstrfmtcat(out, "TresPerNode=%s", job_ptr->tres_per_node);
	}

	/****** Line (optional) ******/
	if (job_ptr->tres_per_socket) {
		xstrcat(out, line_end);
		xstrfmtcat(out, "TresPerSocket=%s", job_ptr->tres_per_socket);
	}

	/****** Line (optional) ******/
	if (job_ptr->tres_per_task) {
		xstrcat(out, line_end);
		xstrfmtcat(out, "TresPerTask=%s", job_ptr->tres_per_task);
	}

	/****** Line (optional) ******/
	if (job_ptr->mail_type && job_ptr->mail_user) {
		xstrcat(out, line_end);
		xstrfmtcat(out, "MailUser=%s MailType=%s",
			   job_ptr->mail_user,
			   print_mail_type(job_ptr->mail_type));
	}

	/****** Line (optional) ******/
	if ((job_ptr->ntasks_per_tres) &&
	    (job_ptr->ntasks_per_tres != NO_VAL16) &&
	    (job_ptr->ntasks_per_tres != INFINITE16)) {
		xstrcat(out, line_end);
		xstrfmtcat(out, "NtasksPerTRES=%u", job_ptr->ntasks_per_tres);
	}

	/****** Line (optional) ******/
	if (job_ptr->container || job_ptr->container_id) {
		xstrcat(out, line_end);
		xstrfmtcat(out, "Container=%s ContainerID=%s",
			   job_ptr->container, job_ptr->container_id);
	}

	/****** Line (optional) ******/
	if (job_ptr->selinux_context) {
		xstrcat(out, line_end);
		xstrfmtcat(out, "SELinuxContext=%s", job_ptr->selinux_context);
	}

	/****** Line (optional) ******/
	if (job_ptr->resv_ports) {
		xstrcat(out, line_end);
		xstrfmtcat(out, "ResvPorts=%s", job_ptr->resv_ports);
	}

	xstrcat(out, line_end);

	/****** END OF JOB RECORD ******/
	if (one_liner)
		xstrcat(out, "\n");
	else
		xstrcat(out, "\n\n");

	return out;
}

/*
 * _print_job_info - output information about a specific Slurm
 *	job based upon message as loaded using slurm_load_jobs
 * IN out - file to write to
 * IN job_ptr - an individual job information record pointer
 */
static void _print_job_info(FILE *out, job_info_t *job_ptr)
{
	char *print_this;

	if ((print_this = _sprint_job_info(job_ptr))) {
		fprintf(out, "%s", print_this);
		xfree(print_this);
	}
}

/* Load current job table information into *job_buffer_pptr */
extern int
scontrol_load_job(job_info_msg_t ** job_buffer_pptr, uint32_t job_id)
{
	int error_code;
	static uint16_t last_show_flags = 0xffff;
	uint16_t show_flags = 0;
	job_info_msg_t * job_info_ptr = NULL;

	if (all_flag)
		show_flags |= SHOW_ALL;

	if (detail_flag)
		show_flags |= SHOW_DETAIL;
	if (federation_flag)
		show_flags |= SHOW_FEDERATION;
	if (local_flag)
		show_flags |= SHOW_LOCAL;
	if (sibling_flag)
		show_flags |= SHOW_FEDERATION | SHOW_SIBLING;

	if (old_job_info_ptr) {
		if (last_show_flags != show_flags)
			old_job_info_ptr->last_update = (time_t) 0;
		if (job_id) {
			error_code = slurm_load_job(&job_info_ptr, job_id,
						    show_flags);
		} else {
			error_code = slurm_load_jobs(
				old_job_info_ptr->last_update,
				&job_info_ptr, show_flags);
		}
		if (error_code == SLURM_SUCCESS)
			slurm_free_job_info_msg (old_job_info_ptr);
		else if (errno == SLURM_NO_CHANGE_IN_DATA) {
			job_info_ptr = old_job_info_ptr;
			error_code = SLURM_SUCCESS;
			if (quiet_flag == -1)
 				printf ("slurm_load_jobs no change in data\n");
		}
	} else if (job_id) {
		error_code = slurm_load_job(&job_info_ptr, job_id, show_flags);
	} else {
		error_code = slurm_load_jobs((time_t) NULL, &job_info_ptr,
					     show_flags);
	}

	if (error_code == SLURM_SUCCESS) {
		old_job_info_ptr = job_info_ptr;
		if (job_id)
			old_job_info_ptr->last_update = (time_t) 0;
		last_show_flags  = show_flags;
		*job_buffer_pptr = job_info_ptr;
	}

	return error_code;
}

/*
 * scontrol_pid_info - given a local process id, print the corresponding
 *	slurm job id and its expected end time
 * IN job_pid - the local process id of interest
 */
extern void
scontrol_pid_info(pid_t job_pid)
{
	int error_code;
	uint32_t job_id = 0;
	time_t end_time;
	long rem_time;

	error_code = slurm_pid2jobid(job_pid, &job_id);
	if (error_code) {
		exit_code = 1;
		if (quiet_flag != 1)
			fprintf(stderr,
				"Failed to locate job for requested pid\n");
		return;
	}

	error_code = slurm_get_end_time(job_id, &end_time);
	if (error_code) {
		exit_code = 1;
		if (quiet_flag != 1)
			slurm_perror("Failed to get job end time");
		return;
	}
	printf("Slurm JobId=%u ends at %s\n", job_id, slurm_ctime2(&end_time));

	rem_time = slurm_get_rem_time(job_id);
	printf("Job remaining time is %ld seconds\n", rem_time);
	return;
}

/*
 * scontrol_print_completing - print jobs in completing state and
 *	associated nodes in COMPLETING or DOWN state
 */
extern void
scontrol_print_completing (void)
{
	int error_code, i;
	job_info_msg_t  *job_info_msg;
	job_info_t      *job_info;
	node_info_msg_t *node_info_msg;

	error_code = scontrol_load_job (&job_info_msg, 0);
	if (error_code) {
		exit_code = 1;
		if (quiet_flag != 1)
			slurm_perror ("slurm_load_jobs error");
		return;
	}

	if (!(node_info_msg = _get_node_info_for_jobs()))
		return;

	/* Scan the jobs for completing state */
	job_info = job_info_msg->job_array;
	for (i = 0; i < job_info_msg->record_count; i++) {
		if (job_info[i].job_state & JOB_COMPLETING)
			scontrol_print_completing_job(&job_info[i],
						      node_info_msg);
	}
}

extern void
scontrol_print_completing_job(job_info_t *job_ptr,
			      node_info_msg_t *node_info_msg)
{
	int i, c_offset = 0;
	node_info_t *node_info;
	hostlist_t *comp_nodes, *down_nodes;
	char *node_buf;
	char time_str[256];
	time_t completing_time = 0;

	comp_nodes = hostlist_create(NULL);
	down_nodes = hostlist_create(NULL);

	if (job_ptr->cluster && federation_flag && !local_flag)
		c_offset = get_cluster_node_offset(job_ptr->cluster,
						   node_info_msg);

	for (i = 0; job_ptr->node_inx[i] != -1; i+=2) {
		int j = job_ptr->node_inx[i];
		for (; j <= job_ptr->node_inx[i+1]; j++) {
			int node_inx = j + c_offset;
			if (node_inx >= node_info_msg->record_count)
				break;
			node_info = &(node_info_msg->node_array[node_inx]);
			if (IS_NODE_COMPLETING(node_info))
				hostlist_push_host(comp_nodes, node_info->name);
			else if (IS_NODE_DOWN(node_info))
				hostlist_push_host(down_nodes, node_info->name);
		}
	}

	fprintf(stdout, "JobId=%u ", job_ptr->job_id);

	slurm_make_time_str(&job_ptr->end_time, time_str, sizeof(time_str));
	fprintf(stdout, "EndTime=%s ", time_str);

	completing_time = time(NULL) - job_ptr->end_time;
	secs2time_str(completing_time, time_str, sizeof(time_str));
	fprintf(stdout, "CompletingTime=%s ", time_str);

	/* Sort the hostlists */
	hostlist_sort(comp_nodes);
	hostlist_sort(down_nodes);
	node_buf = hostlist_ranged_string_xmalloc(comp_nodes);
	if (node_buf && node_buf[0])
		fprintf(stdout, "Nodes(COMPLETING)=%s ", node_buf);
	xfree(node_buf);

	node_buf = hostlist_ranged_string_xmalloc(down_nodes);
	if (node_buf && node_buf[0])
		fprintf(stdout, "Nodes(DOWN)=%s ", node_buf);
	xfree(node_buf);
	fprintf(stdout, "\n");

	hostlist_destroy(comp_nodes);
	hostlist_destroy(down_nodes);
}

static bool _het_job_offset_match(job_info_t *job_ptr, uint32_t het_job_offset)
{
	if ((het_job_offset == NO_VAL) ||
	    (het_job_offset == job_ptr->het_job_offset))
		return true;
	return false;
}

static bool _task_id_in_job(job_info_t *job_ptr, uint32_t array_id)
{
	uint32_t array_len;

	if ((array_id == NO_VAL) ||
	    (array_id == job_ptr->array_task_id))
		return true;

	if (!job_ptr->array_bitmap)
		return false;
	array_len = bit_size(job_ptr->array_bitmap);
	if (array_id >= array_len)
		return false;
	if (bit_test(job_ptr->array_bitmap, array_id))
		return true;
	return false;
}

/*
 * scontrol_print_job - print the specified job's information
 * IN job_id - job's id or NULL to print information about all jobs
 */
extern void scontrol_print_job(char *job_id_str, int argc, char **argv)
{
	int error_code = SLURM_SUCCESS, i, print_cnt = 0;
	uint32_t job_id = 0;
	uint32_t array_id = NO_VAL, het_job_offset = NO_VAL;
	job_info_msg_t * job_buffer_ptr = NULL;
	job_info_t *job_ptr = NULL;
	char *end_ptr = NULL;

	if (job_id_str) {
		char *tmp_job_ptr = job_id_str;
		/*
		 * Check that the input is a valid job id (i.e. 123 or 123_456).
		 */
		while (*tmp_job_ptr) {
			if (!isdigit(*tmp_job_ptr) &&
			    (*tmp_job_ptr != '_') && (*tmp_job_ptr != '+')) {
				exit_code = 1;
				errno = ESLURM_INVALID_JOB_ID;
				if (quiet_flag != 1)
					slurm_perror("scontrol_print_job error");
				return;
			}
			++tmp_job_ptr;
		}
		job_id = (uint32_t) strtol (job_id_str, &end_ptr, 10);
		if (end_ptr[0] == '_')
			array_id = strtol(end_ptr + 1, &end_ptr, 10);
		if (end_ptr[0] == '+')
			het_job_offset = strtol(end_ptr + 1, &end_ptr, 10);
	}

	error_code = scontrol_load_job(&job_buffer_ptr, job_id);

	if (mime_type) {
		openapi_resp_job_info_msg_t resp = {
			.jobs = job_buffer_ptr,
		};

		if (job_buffer_ptr) {
			resp.last_update = job_buffer_ptr->last_update;
			resp.last_backfill = job_buffer_ptr->last_backfill;
		}

		DATA_DUMP_CLI(OPENAPI_JOB_INFO_RESP, resp, argc, argv, NULL,
			      mime_type, data_parser, error_code);

		if (error_code)
			exit_code = 1;
		return;
	}

	if (error_code) {
		exit_code = 1;
		if (quiet_flag != 1)
			slurm_perror("slurm_load_jobs error");
		return;
	}

	if (quiet_flag == -1) {
		char time_str[256];
		slurm_make_time_str ((time_t *)&job_buffer_ptr->last_update,
				     time_str, sizeof(time_str));
		printf ("last_update_time=%s, records=%d\n",
			time_str, job_buffer_ptr->record_count);
	}

	for (i = 0, job_ptr = job_buffer_ptr->job_array;
	     i < job_buffer_ptr->record_count; i++, job_ptr++) {
		char *save_array_str = NULL;
		uint32_t save_task_id = 0;
		if (!_het_job_offset_match(job_ptr, het_job_offset))
			continue;
		if (!_task_id_in_job(job_ptr, array_id))
			continue;
		if ((array_id != NO_VAL) && job_ptr->array_task_str) {
			save_array_str = job_ptr->array_task_str;
			job_ptr->array_task_str = NULL;
			save_task_id = job_ptr->array_task_id;
			job_ptr->array_task_id = array_id;
		}
		_print_job_info(stdout, job_ptr);
		if (save_array_str) {
			job_ptr->array_task_str = save_array_str;
			job_ptr->array_task_id = save_task_id;
		}
		print_cnt++;
	}

	if (print_cnt == 0) {
		if (job_id_str) {
			exit_code = 1;
			if (quiet_flag != 1) {
				if (array_id != NO_VAL) {
					printf("Job %u_%u not found\n",
					       job_id, array_id);
				} else if (het_job_offset != NO_VAL) {
					printf("Job %u+%u not found\n",
					       job_id, het_job_offset);
				} else {
					printf("Job %u not found\n", job_id);
				}
			}
		} else if (quiet_flag != 1)
			printf ("No jobs in the system\n");
	}
}

/*
 * scontrol_print_step - print the specified job step's information
 * IN job_step_id_str - job step's id or NULL to print information
 *	about all job steps
 */
extern void scontrol_print_step(char *job_step_id_str, int argc, char **argv)
{
	int error_code = 0, print_cnt = 0;
	slurm_step_id_t step_id = {
		.job_id = NO_VAL,
		.step_het_comp = NO_VAL,
		.step_id = NO_VAL,
	};
	uint32_t array_id = NO_VAL;
	job_step_info_response_msg_t *job_step_info_ptr = NULL;
	static uint32_t last_job_id = 0, last_array_id, last_step_id = 0;
	static job_step_info_response_msg_t *old_job_step_info_ptr = NULL;
	static uint16_t last_show_flags = 0xffff;
	uint16_t show_flags = 0;
	job_step_info_t **steps = NULL;

	if (!job_step_id_str) {
		/* do nothing */
	} else if (!xstrncasecmp(job_step_id_str, CONTAINER_ID_TAG,
				 strlen(CONTAINER_ID_TAG))) {
		uid_t uid = SLURM_AUTH_NOBODY;
		list_t *step_list = list_create((ListDelF) slurm_free_step_id);
		char *cid = job_step_id_str + strlen(CONTAINER_ID_TAG);

		error_code = slurm_find_step_ids_by_container_id(
			SHOW_ALL, uid, cid, step_list);

		if (error_code || list_is_empty(step_list)) {
			step_id.job_id = 0;
		} else {
			/* just clone out the first step id details */
			step_id = *(slurm_step_id_t *) list_peek(step_list);
			job_step_id_str = NULL;
		}

		FREE_NULL_LIST(step_list);
	} else {
		slurm_selected_step_t id = {0};
		if (!(error_code = unfmt_job_id_string(job_step_id_str, &id,
						       NO_VAL))) {
			if (id.array_task_id != NO_VAL)
				array_id = id.array_task_id;

			step_id = id.step_id;
		}
	}

	if (all_flag)
		show_flags |= SHOW_ALL;
	if (local_flag)
		show_flags |= SHOW_LOCAL;

	if (!step_id.job_id || error_code) {
		/* step lookup failed already - skip trying again */
	} else if ((old_job_step_info_ptr) && (last_job_id == step_id.job_id) &&
	    (last_array_id == array_id) && (last_step_id == step_id.step_id)) {
		if (last_show_flags != show_flags)
			old_job_step_info_ptr->last_update = (time_t) 0;
		error_code = slurm_get_job_steps(
			old_job_step_info_ptr->last_update,
			step_id.job_id, step_id.step_id, &job_step_info_ptr,
			show_flags);
		if (error_code == SLURM_SUCCESS)
			slurm_free_job_step_info_response_msg (
				old_job_step_info_ptr);
		else if (errno == SLURM_NO_CHANGE_IN_DATA) {
			job_step_info_ptr = old_job_step_info_ptr;
			error_code = SLURM_SUCCESS;
			if (quiet_flag == -1)
				printf("slurm_get_job_steps no change in data\n");
		} else {
			error_code = errno;
		}
	} else {
		if (old_job_step_info_ptr) {
			slurm_free_job_step_info_response_msg (
				old_job_step_info_ptr);
			old_job_step_info_ptr = NULL;
		}
		error_code = slurm_get_job_steps ( (time_t) 0, step_id.job_id,
						   step_id.step_id,
						   &job_step_info_ptr,
						   show_flags);
		if ((error_code == SLURM_ERROR) && errno)
			error_code = errno;
	}

	if (error_code || !job_step_info_ptr) {
		if (mime_type) {
			openapi_resp_job_step_info_msg_t resp = {
				.steps = job_step_info_ptr,
			};

			if (job_step_info_ptr)
				resp.last_update =
					job_step_info_ptr->last_update;

			DATA_DUMP_CLI(OPENAPI_STEP_INFO_MSG, resp, argc, argv,
				      NULL, mime_type, data_parser, error_code);

			if (error_code)
				exit_code = 1;
			return;
		}

		exit_code = 1;
		if (quiet_flag != 1) {
			if (!step_id.job_id)
				printf("No job steps found\n");
			else
				error("%s: slurm_get_job_steps(%s) failed: %s",
				      __func__, job_step_id_str,
				      slurm_strerror(error_code));
		}
		return;
	}

	old_job_step_info_ptr = job_step_info_ptr;
	last_show_flags = show_flags;
	last_job_id = step_id.job_id;
	last_step_id = step_id.step_id;

	if (!mime_type && (quiet_flag == -1)) {
		char time_str[256];
		slurm_make_time_str ((time_t *)&job_step_info_ptr->last_update,
			             time_str, sizeof(time_str));
		printf ("last_update_time=%s, records=%d\n",
			time_str, job_step_info_ptr->job_step_count);
	}

	if (job_step_info_ptr->job_step_count) {
		int s = 0;
		steps = xcalloc(job_step_info_ptr->job_step_count + 1,
				sizeof(*steps));

		for (int i = 0; i < job_step_info_ptr->job_step_count; i++) {
			job_step_info_t *step =
				&job_step_info_ptr->job_steps[i];

			if ((array_id != NO_VAL) &&
			    (array_id != step->array_task_id))
				continue;

			steps[s] = step;
			s++;
		}
	}

	if (mime_type) {
		openapi_resp_job_step_info_msg_t resp = {
			.steps = job_step_info_ptr,
		};

		if (job_step_info_ptr)
			resp.last_update = job_step_info_ptr->last_update;

		DATA_DUMP_CLI(OPENAPI_STEP_INFO_MSG, resp, argc, argv, NULL,
			      mime_type, data_parser, error_code);
	} else if (steps) {
		int i = 0;

		for (; steps[i]; i++)
			slurm_print_job_step_info(stdout, steps[i], one_liner);

		print_cnt = i;
	}

	if (!mime_type && !print_cnt) {
		if (job_step_id_str) {
			exit_code = 1;
			if (quiet_flag != 1) {
				char tmp_char[45];
				log_build_step_id_str(&step_id, tmp_char,
						      sizeof(tmp_char),
						      (STEP_ID_FLAG_NO_PREFIX |
						       STEP_ID_FLAG_NO_JOB));
				if (array_id == NO_VAL) {
					printf("Job step %u.%s not found\n",
					       step_id.job_id, tmp_char);
				} else {
					printf("Job step %u_%u.%s not found\n",
					       step_id.job_id, array_id,
					       tmp_char);
				}
			}
		} else if (quiet_flag != 1)
			printf ("No job steps in the system\n");
	}

	xfree(steps);
}

static int _add_to_listjobs_list(void *x, void *arg)
{
	step_loc_t *step_loc = x;
	slurm_step_id_t step_id = step_loc->step_id;
	listjobs_info_t *listjobs_info;
	uint32_t *job_id;

	add_to_listjobs_list_args_t *args = arg;
	list_t *listjobs_list = args->listjobs_list;
	list_t *jobs_seen = args->jobs_seen;

	/* Don't add duplicate job ids to the list */
	if (list_find_first(jobs_seen, slurm_find_uint32_in_list,
			    &step_id.job_id))
		return 0;

	job_id = xmalloc(sizeof(*job_id));
	*job_id = step_id.job_id;
	list_append(jobs_seen, job_id);

	listjobs_info = xmalloc(sizeof(*listjobs_info));
	listjobs_info->job_id = step_id.job_id;
	list_append(listjobs_list, listjobs_info);

	return 0;
}

static int _print_listjobs_info(void *x, void *arg)
{
	uint32_t *job_id = x;

	printf("%-8d\n", *job_id);

	return 0;
}

static void _dump_listjobs(list_t *listjobs_list, int argc, char **argv)
{
	int rc;

	openapi_resp_listjobs_info_t resp = {
		.listjobs_list = listjobs_list,
	};

	DATA_DUMP_CLI(OPENAPI_LISTJOBS_INFO_RESP, resp, argc, argv, NULL,
		      mime_type, data_parser, rc);

	if (rc != SLURM_SUCCESS)
		exit_code = 1;
}

/*
 * scontrol_list_jobs - Print jobs on node.
 *
 * IN node_name - query this node for any jobs
 */
extern void scontrol_list_jobs(int argc, char **argv)
{
	char *node_name = NULL;
	list_t *steps = NULL;
	list_t *listjobs_list = NULL;
	list_t *jobs_seen = NULL;
	add_to_listjobs_list_args_t for_each_args = { 0 };

	if (argc)
		node_name = argv[1];

	steps = stepd_available(NULL, node_name);

	if (!steps || !list_count(steps)) {
		if (mime_type)
			_dump_listjobs(NULL, argc, argv);
		else
			fprintf(stderr, "No slurmstepd's found on this node\n");

		goto cleanup;
	}

	listjobs_list = list_create(xfree_ptr);
	jobs_seen = list_create(xfree_ptr);

	for_each_args.listjobs_list = listjobs_list;
	for_each_args.jobs_seen = jobs_seen;

	list_for_each(steps, _add_to_listjobs_list, &for_each_args);

	if (mime_type) {
		_dump_listjobs(listjobs_list, argc, argv);
		goto cleanup;
	}

	printf("JOBID\n");
	list_for_each(listjobs_list, _print_listjobs_info, NULL);

cleanup:
	FREE_NULL_LIST(listjobs_list);
	FREE_NULL_LIST(jobs_seen);
	FREE_NULL_LIST(steps);
}

/* Return 1 on success, 0 on failure to find a jobid in the string */
static int _parse_jobid(const char *jobid_str, uint32_t *out_jobid)
{
	char *ptr, *job;
	long jobid;

	job = xstrdup(jobid_str);
	ptr = xstrchr(job, '.');
	if (ptr != NULL) {
		*ptr = '\0';
	}

	jobid = strtol(job, &ptr, 10);
	if (!xstring_is_whitespace(ptr)) {
		fprintf(stderr, "\"%s\" does not look like a jobid\n", job);
		xfree(job);
		return 0;
	}

	*out_jobid = (uint32_t) jobid;
	xfree(job);
	return 1;
}

/* Return 1 on success, 0 on failure to find a stepid in the string */
static int _parse_stepid(const char *jobid_str, slurm_step_id_t *step_id)
{
	char *ptr, *job, *step;
	int rc = 1;

	job = xstrdup(jobid_str);
	ptr = xstrchr(job, '.');
	if (ptr == NULL) {
		/* did not find a period, so no step ID in this string */
		xfree(job);
		return rc;
	} else {
		step = ptr + 1;
	}

	step_id->step_id = (uint32_t)strtol(step, &ptr, 10);

	step = xstrchr(ptr, '+');
	if (step) {
		/* het step */
		step++;
		step_id->step_het_comp = (uint32_t)strtol(step, &ptr, 10);
	} else
		step_id->step_het_comp = NO_VAL;

	if (!xstring_is_whitespace(ptr)) {
		fprintf(stderr, "\"%s\" does not look like a stepid\n",
			jobid_str);
		rc = 0;
	}

	xfree(job);
	return rc;
}


static bool
_in_task_array(pid_t pid, slurmstepd_task_info_t *task_array,
	       uint32_t task_array_count)
{
	int i;

	for (i = 0; i < task_array_count; i++) {
		if (pid == task_array[i].pid)
			return true;
	}

	return false;
}

static void _list_pids_one_step(const char *node_name, slurm_step_id_t *step_id,
				list_t *listpids_list)
{
	int fd;
	slurmstepd_task_info_t *task_info = NULL;
	uint32_t *pids = NULL;
	uint32_t count = 0;
	uint32_t tcount = 0;
	int i;
	uint16_t protocol_version;
	char tmp_char[64];

	fd = stepd_connect(NULL, node_name, step_id, &protocol_version);
	if (fd == -1) {
		exit_code = 1;
		if (errno == ENOENT) {
			fprintf(stderr,
				"%s does not exist on this node.\n",
				log_build_step_id_str(step_id, tmp_char,
						      sizeof(tmp_char),
						      STEP_ID_FLAG_NONE));
			exit_code = 1;
		} else {
			perror("Unable to connect to slurmstepd");
		}
		return;
	}

	log_build_step_id_str(step_id, tmp_char, sizeof(tmp_char),
			      STEP_ID_FLAG_NO_JOB | STEP_ID_FLAG_NO_PREFIX);

	/* Get all task pids */
	stepd_task_info(fd, protocol_version, &task_info, &tcount);
	for (i = 0; i < (int)tcount; i++) {
		if (task_info[i].exited)
			continue;
		listpids_info_t *listpids_info = xmalloc(
			sizeof(*listpids_info));

		listpids_info->global_task_id = task_info[i].gtid;
		listpids_info->job_id = step_id->job_id;
		listpids_info->local_task_id = task_info[i].id;
		listpids_info->pid = task_info[i].pid;
		listpids_info->step_id = xstrdup(tmp_char);

		list_append(listpids_list, listpids_info);
	}

	/* Get pids in proctrack container (slurmstepd, srun, etc.) */
	stepd_list_pids(fd, protocol_version, &pids, &count);
	for (i = 0; i < count; i++) {
		if (_in_task_array((pid_t) pids[i], task_info, tcount))
			continue;
		listpids_info_t *listpids_info = xmalloc(
			sizeof(*listpids_info));

		listpids_info->global_task_id = NO_VAL;
		listpids_info->job_id = step_id->job_id;
		listpids_info->local_task_id = NO_VAL;
		listpids_info->pid = pids[i];
		listpids_info->step_id = xstrdup(tmp_char);

		list_append(listpids_list, listpids_info);
	}

	xfree(pids);
	xfree(task_info);
	close(fd);
}

static void _dump_listpids(list_t *listpids_list, int argc, char **argv)
{
	int rc;

	openapi_resp_listpids_info_t resp = {
		.listpids_list = listpids_list,
	};

	DATA_DUMP_CLI(OPENAPI_LISTPIDS_INFO_RESP, resp, argc, argv, NULL,
		      mime_type, data_parser, rc);

	if (rc != SLURM_SUCCESS)
		exit_code = 1;
}

static void _list_pids_all_steps(const char *node_name,
				 slurm_step_id_t *step_id,
				 list_t* listpids_list,
				 int argc, char **argv)
{
	list_t *steps;
	list_itr_t *itr;
	step_loc_t *stepd;
	int count = 0;
	char tmp_char[64];

	if (step_id->step_het_comp != NO_VAL) {
		_list_pids_one_step(node_name, step_id, listpids_list);
		return;
	}

	steps = stepd_available(NULL, node_name);
	if (!steps || list_count(steps) == 0) {
		if (mime_type) {
			_dump_listpids(NULL, argc, argv);
		} else {
			fprintf(stderr, "%s does not exist on node %s.\n",
				log_build_step_id_str(step_id, tmp_char,
						      sizeof(tmp_char),
						      STEP_ID_FLAG_NONE),
				node_name);
		}
		FREE_NULL_LIST(steps);
		exit_code = 1;
		return;
	}

	itr = list_iterator_create(steps);
	while ((stepd = list_next(itr))) {
		if (step_id->job_id != stepd->step_id.job_id)
			continue;

		if ((step_id->step_id != NO_VAL) &&
		    (step_id->step_id != stepd->step_id.step_id))
			continue;

		_list_pids_one_step(stepd->nodename, &stepd->step_id,
				    listpids_list);
		count++;
	}
	list_iterator_destroy(itr);
	FREE_NULL_LIST(steps);

	if (count == 0) {
		if (step_id->step_id != NO_VAL) {
			fprintf(stderr, "%s does not exist on node %s.\n",
				log_build_step_id_str(step_id, tmp_char,
						      sizeof(tmp_char),
						      STEP_ID_FLAG_NONE),
				node_name);
		} else
			fprintf(stderr, "There are no steps for job %u on node %s.\n",
				step_id->job_id, node_name);
		exit_code = 1;
	}
}

static void _list_pids_all_jobs(const char *node_name, list_t *listpids_list,
				int argc, char **argv)
{
	list_t *steps;
	list_itr_t *itr;
	step_loc_t *stepd;

	steps = stepd_available(NULL, node_name);
	if (!steps || list_count(steps) == 0) {
		if (mime_type)
			_dump_listpids(NULL, argc, argv);
		else
			fprintf(stderr, "No job steps exist on this node.\n");
		FREE_NULL_LIST(steps);
		exit_code = 1;
		return;
	}

	itr = list_iterator_create(steps);
	while((stepd = list_next(itr))) {
		_list_pids_one_step(stepd->nodename, &stepd->step_id,
				    listpids_list);
	}
	list_iterator_destroy(itr);
	FREE_NULL_LIST(steps);
}

static int _print_listpids_info(void *x, void *arg)
{
	listpids_info_t *listpids_info = x;

	printf("%-8d %-8d %-8s ",
	       listpids_info->pid, listpids_info->job_id,
	       listpids_info->step_id);

	if (listpids_info->local_task_id != NO_VAL) {
		printf("%-7d ", listpids_info->local_task_id);
	} else {
		printf("%-7s ", "-");
	}

	if (listpids_info->global_task_id != NO_VAL) {
		printf("%-8d ", listpids_info->global_task_id);
	} else {
		printf("%-8s ", "-");
	}

	printf("\n");
	return 0;
}

static void _free_listpids_info(void *x)
{
	listpids_info_t *listpids_info = x;

	if (listpids_info) {
		xfree(listpids_info->step_id);
	}

	xfree(listpids_info);
}

/*
 * scontrol_list_pids - given a slurmd job ID or job ID + step ID,
 *	print the process IDs of the processes each job step (or
 *	just the specified step ID).
 * IN jobid_str - string representing a jobid: jobid[.stepid]
 * IN node_name - May be NULL, in which case it will attempt to
 *	determine the NodeName of the local host on its own.
 *	This is mostly of use when multiple-slurmd support is in use,
 *	because if NULL is used when there are multiple slurmd on the
 *	node, one of them will be selected more-or-less at random.
 */
extern void scontrol_list_pids(int argc, char **argv)
{
	char *jobid_str = NULL;
	char *node_name = NULL;
	list_t *listpids_list = NULL;
	slurm_step_id_t step_id = {
		.job_id = 0,
		.step_id = NO_VAL,
		.step_het_comp = NO_VAL,
	};

	if (argc >= 2)
		jobid_str = argv[1];
	if (argc >= 3)
		node_name = argv[2];

	/* Job ID is optional */
	if (jobid_str != NULL
	    && jobid_str[0] != '*'
	    && !_parse_jobid(jobid_str, &step_id.job_id)) {
		exit_code = 1;
		return;
	}

	listpids_list = list_create(_free_listpids_info);

	/* Step ID is optional */
	if (jobid_str == NULL || jobid_str[0] == '*') {
		_list_pids_all_jobs(node_name, listpids_list, argc, argv);
	} else if (_parse_stepid(jobid_str, &step_id))
		_list_pids_all_steps(node_name, &step_id, listpids_list, argc,
				     argv);
	if (exit_code)
		goto cleanup;

	if (mime_type) {
		_dump_listpids(listpids_list, argc, argv);
		goto cleanup;
	}

	printf("%-8s %-8s %-8s %-7s %-8s\n",
	       "PID", "JOBID", "STEPID", "LOCALID", "GLOBALID");
	list_for_each(listpids_list, _print_listpids_info, NULL);

cleanup:
	FREE_NULL_LIST(listpids_list);
}

static int _add_to_liststeps_list(void *x, void *arg)
{
	liststeps_info_t *liststeps_info;
	step_loc_t *step_loc = x;
	list_t *liststeps_list = arg;
	char step_id_str[32];
	slurm_step_id_t step_id = step_loc->step_id;

	log_build_step_id_str(&step_id, step_id_str, sizeof(step_id_str),
			      STEP_ID_FLAG_NO_JOB | STEP_ID_FLAG_NO_PREFIX);

	liststeps_info = xmalloc(sizeof(*liststeps_info));

	liststeps_info->step_id = xstrdup(step_id_str);
	liststeps_info->job_id = step_id.job_id;

	list_append(liststeps_list, liststeps_info);

	return 0;
}

static int _print_liststeps_info(void *x, void *arg)
{
	liststeps_info_t *liststeps_info = x;

	printf("%-8d %-8s\n", liststeps_info->job_id, liststeps_info->step_id);

	return 0;
}

static void _free_liststeps_info(void *x)
{
	liststeps_info_t *liststeps_info = x;

	if (liststeps_info) {
		xfree(liststeps_info->step_id);
	}

	xfree(liststeps_info);
}

static void _dump_liststeps(list_t *liststeps_list, int argc, char **argv)
{
	int rc;

	openapi_resp_liststeps_info_t resp = {
		.liststeps_list = liststeps_list,
	};

	DATA_DUMP_CLI(OPENAPI_LISTSTEPS_INFO_RESP, resp, argc, argv, NULL,
		      mime_type, data_parser, rc);

	if (rc != SLURM_SUCCESS)
		exit_code = 1;
}

/*
 * scontrol_list_steps - Print steps on node.
 *
 * IN node_name - query this node for any steps
 */
extern void scontrol_list_steps(int argc, char **argv)
{
	list_t *liststeps_list = NULL;
	char *node_name = NULL;
	list_t *steps;

	if (argc)
		node_name = argv[1];

	steps = stepd_available(NULL, node_name);

	if (!steps || !list_count(steps)) {
		if (mime_type)
			_dump_liststeps(NULL, argc, argv);
		else
			fprintf(stderr, "No slurmstepd's found on this node\n");

		goto cleanup;
	}

	liststeps_list = list_create(_free_liststeps_info);
	list_for_each(steps, _add_to_liststeps_list, liststeps_list);

	if (mime_type) {
		_dump_liststeps(liststeps_list, argc, argv);
		goto cleanup;
	}

	printf("%-8s %-8s\n", "JOBID", "STEPID");
	list_for_each(liststeps_list, _print_liststeps_info, NULL);

cleanup:
	FREE_NULL_LIST(liststeps_list);
	FREE_NULL_LIST(steps);
}

extern void scontrol_getent(const char *node_name)
{
	list_t *steps = NULL;
	list_itr_t *itr = NULL;
	step_loc_t *stepd;
	int fd;
	struct passwd *pwd = NULL;
	struct group **grps = NULL;

	if (!(steps = stepd_available(NULL, node_name))) {
		fprintf(stderr, "No steps found on this node\n");
		return;
	}

	itr = list_iterator_create(steps);
	while ((stepd = list_next(itr))) {
		char tmp_char[45];
		fd = stepd_connect(NULL, node_name, &stepd->step_id,
				   &stepd->protocol_version);

		if (fd < 0)
			continue;
		pwd = stepd_getpw(fd, stepd->protocol_version,
				  GETPW_MATCH_ALWAYS, 0, NULL);

		if (!pwd) {
			close(fd);
			continue;
		}
		log_build_step_id_str(&stepd->step_id, tmp_char,
				      sizeof(tmp_char), STEP_ID_FLAG_NO_PREFIX);
		printf("JobId=%s:\nUser:\n", tmp_char);

		printf("%s:%s:%u:%u:%s:%s:%s\nGroups:\n",
		       pwd->pw_name, pwd->pw_passwd, pwd->pw_uid, pwd->pw_gid,
		       pwd->pw_gecos, pwd->pw_dir, pwd->pw_shell);

		xfree_struct_passwd(pwd);

		grps = stepd_getgr(fd, stepd->protocol_version,
				   GETGR_MATCH_ALWAYS, 0, NULL);
		if (!grps) {
			close(fd);
			printf("\n");
			continue;
		}

		for (int i = 0; grps[i]; i++) {
			printf("%s:%s:%u:%s\n",
			       grps[i]->gr_name, grps[i]->gr_passwd,
			       grps[i]->gr_gid,
			       (grps[i]->gr_mem) ? grps[i]->gr_mem[0] : "");
		}
		close(fd);
		xfree_struct_group_array(grps);
		printf("\n");
	}
	list_iterator_destroy(itr);
	FREE_NULL_LIST(steps);
}

extern void scontrol_gethost(const char *stepd_node, const char *node_name)
{
	list_t *steps = NULL;
	list_itr_t *itr = NULL;
	step_loc_t *stepd;
	int fd;

	if (!(steps = stepd_available(NULL, stepd_node))) {
		fprintf(stderr, "No steps found on this node\n");
		return;
	}

	itr = list_iterator_create(steps);
	while ((stepd = list_next(itr))) {
		char tmp_char[45], buf[INET6_ADDRSTRLEN];
		struct hostent *host = NULL;
		const char *ip;
		int i, j;

		fd = stepd_connect(NULL, stepd_node, &stepd->step_id,
				   &stepd->protocol_version);

		if (fd < 0)
			continue;
		host = stepd_gethostbyname(fd, stepd->protocol_version,
					   (GETHOST_IPV4 | GETHOST_IPV6 |
					    GETHOST_NOT_MATCH_PID), node_name);
		log_build_step_id_str(&stepd->step_id, tmp_char,
				      sizeof(tmp_char), STEP_ID_FLAG_NO_PREFIX);
		printf("JobId=%s:\nHost:\n", tmp_char);
		for (i = 0; host && host->h_addr_list[i] != NULL; ++i) {
			ip = inet_ntop(host->h_addrtype, host->h_addr_list[i],
				       buf, sizeof (buf));
			printf("%-15s %s", ip, host->h_name);
			for (j = 0; host->h_aliases[j] != NULL; ++j) {
				printf(" %s", host->h_aliases[i]);
			}
			printf("\n");
		}

		xfree_struct_hostent(host);
		close(fd);
		printf("\n");
	}
	list_iterator_destroy(itr);
	FREE_NULL_LIST(steps);
}

/*
 * scontrol_print_hosts - given a node list expression, return
 *	a list of nodes, one per line
 */
extern void
scontrol_print_hosts (char * node_list)
{
	hostlist_t *hl;
	char *host;

	if (!node_list) {
		error("host list is empty");
		return;
	}
	hl = hostlist_create_dims(node_list, 0);
	if (!hl) {
		fprintf(stderr, "Invalid hostlist: %s\n", node_list);
		return;
	}
	while ((host = hostlist_shift_dims(hl, 0))) {
		printf("%s\n", host);
		free(host);
	}
	hostlist_destroy(hl);
}

/* Replace '\n' with ',', remove duplicate comma */
static void
_reformat_hostlist(char *hostlist)
{
	int i, o;
	for (i=0; (hostlist[i] != '\0'); i++) {
		if (hostlist[i] == '\n')
			hostlist[i] = ',';
	}

	o = 0;
	for (i=0; (hostlist[i] != '\0'); i++) {
		while ((hostlist[i] == ',') && (hostlist[i+1] == ','))
			i++;
		hostlist[o++] = hostlist[i];
	}
	hostlist[o] = '\0';
}

/*
 * scontrol_encode_hostlist - given a list of hostnames or the pathname
 *	of a file containing hostnames, translate them into a hostlist
 *	expression
 */
extern int scontrol_encode_hostlist(char *arg_hostlist, bool sorted)
{
	char *io_buf = NULL, *tmp_list, *ranged_string, *hostlist;
	int buf_size = 1024 * 1024;
	int data_read = 0;
	hostlist_t *hl;

	if (!arg_hostlist) {
		fprintf(stderr, "Hostlist is NULL\n");
		return SLURM_ERROR;
	}

	if (!xstrcmp(arg_hostlist, "-"))
		hostlist = "/dev/stdin";
	else
		hostlist = arg_hostlist;

	if (hostlist[0] == '/') {
		ssize_t buf_read;
		int fd = open(hostlist, O_RDONLY);
		if (fd < 0) {
			fprintf(stderr, "Can not open %s\n", hostlist);
			return SLURM_ERROR;
		}
		io_buf = xmalloc(buf_size + 1);
		while ((buf_read = read(fd, &io_buf[data_read],
					buf_size - data_read)) > 0) {
			data_read += buf_read;
		}

		close(fd);

		if (buf_read < 0) {
			xfree(io_buf);
			fprintf(stderr, "Error reading %s\n", hostlist);
			return SLURM_ERROR;
		}

		if (data_read >= buf_size) {
			/* If over 1MB, the file is almost certainly invalid */
			fprintf(stderr, "File %s is too large\n", hostlist);
			xfree(io_buf);
			return SLURM_ERROR;
		}
		io_buf[data_read] = '\0';
		_reformat_hostlist(io_buf);
		tmp_list = io_buf;
	} else
		tmp_list = hostlist;

	hl = hostlist_create(tmp_list);
	if (hl == NULL) {
		fprintf(stderr, "Invalid hostlist: %s\n", tmp_list);
		xfree(io_buf);
		return SLURM_ERROR;
	}
	if (sorted)
		hostlist_sort(hl);
	ranged_string = hostlist_ranged_string_xmalloc(hl);
	printf("%s\n", ranged_string);
	hostlist_destroy(hl);
	xfree(ranged_string);
	xfree(io_buf);
	return SLURM_SUCCESS;
}

static int _wait_nodes_ready(uint32_t job_id)
{
	int is_ready = SLURM_ERROR, i, rc = 0;
	int cur_delay = 0;
	int max_delay;

	if (!slurm_conf.suspend_timeout || !slurm_conf.resume_timeout)
		return SLURM_SUCCESS;	/* Power save mode disabled */
	max_delay = slurm_conf.suspend_timeout + slurm_conf.resume_timeout;
	max_delay *= 5;		/* Allow for ResumeRate support */

	for (i=0; (cur_delay < max_delay); i++) {
		if (i) {
			if (i == 1)
				info("Waiting for nodes to boot");
			sleep(POLL_SLEEP);
			cur_delay += POLL_SLEEP;
		}

		rc = slurm_job_node_ready(job_id);

		if (rc == READY_JOB_FATAL)
			break;				/* fatal error */
		if ((rc == READY_JOB_ERROR) || (rc == EAGAIN))
			continue;			/* retry */
		if ((rc & READY_JOB_STATE) == 0)	/* job killed */
			break;
		if ((rc & READY_NODE_STATE) &&
		    (rc & READY_PROLOG_STATE)) {
			is_ready = SLURM_SUCCESS;
			break;
		}
	}
	if (is_ready == SLURM_SUCCESS)
     		info("Nodes are ready for job %u", job_id);
	else if ((rc & READY_JOB_STATE) == 0)
		info("Job %u no longer running", job_id);
	else
		info("Problem running job %u", job_id);

	return is_ready;
}

/*
 * Wait until a job is ready to execute or enters some failed state
 * RET 1: job ready to run
 *     0: job can't run (cancelled, failure state, timeout, etc.)
 */
extern int scontrol_job_ready(char *job_id_str)
{
	uint32_t job_id;

	job_id = atoi(job_id_str);
	if (job_id <= 0) {
		fprintf(stderr, "Invalid job_id %s", job_id_str);
		return SLURM_ERROR;
	}

	return _wait_nodes_ready(job_id);
}

extern int scontrol_callerid(int argc, char **argv)
{
	int af, ver = 4;
	unsigned char ip_src[sizeof(struct in6_addr)],
		      ip_dst[sizeof(struct in6_addr)];
	uint32_t port_src, port_dst, job_id;
	network_callerid_msg_t req;
	char node_name[HOST_NAME_MAX], *ptr;

	if (argc == 5) {
		ver = strtoul(argv[4], &ptr, 0);
		if (ptr && ptr[0]) {
			error("Address family not an integer");
			return SLURM_ERROR;
		}
	}

	if (ver != 4 && ver != 6) {
		error("Invalid address family: %d", ver);
		return SLURM_ERROR;
	}

	af = ver == 4 ? AF_INET : AF_INET6;
	if (!inet_pton(af, argv[0], ip_src)) {
		error("inet_pton failed for '%s'", argv[0]);
		return SLURM_ERROR;
	}

	port_src = strtoul(argv[1], &ptr, 0);
	if (ptr && ptr[0]) {
		error("Source port not an integer");
		return SLURM_ERROR;
	}

	if (!inet_pton(af, argv[2], ip_dst)) {
		error("scontrol_callerid: inet_pton failed for '%s'", argv[2]);
		return SLURM_ERROR;
	}

	port_dst = strtoul(argv[3], &ptr, 0);
	if (ptr && ptr[0]) {
		error("Destination port not an integer");
		return SLURM_ERROR;
	}

	memcpy(req.ip_src, ip_src, 16);
	memcpy(req.ip_dst, ip_dst, 16);
	req.port_src = port_src;
	req.port_dst = port_dst;
	req.af = af;

	if (slurm_network_callerid(req, &job_id, node_name, HOST_NAME_MAX)
			!= SLURM_SUCCESS) {
		fprintf(stderr,
			"slurm_network_callerid: unable to retrieve callerid data from remote slurmd\n");
		return SLURM_ERROR;
	} else if (job_id == NO_VAL) {
		fprintf(stderr,
			"slurm_network_callerid: remote job id indeterminate\n");
		return SLURM_ERROR;
	} else {
		printf("%u %s\n", job_id, node_name);
		return SLURM_SUCCESS;
	}
}

extern int scontrol_batch_script(int argc, char **argv)
{
	char *filename;
	FILE *out;
	int exit_code;
	uint32_t jobid;

	if (argc < 1)
		return SLURM_ERROR;

	jobid = atoll(argv[0]);

	if (argc > 1)
		filename = xstrdup(argv[1]);
	else
		filename = xstrdup_printf("slurm-%u.sh", jobid);

	if (!xstrcmp(filename, "-")) {
		out = stdout;
	} else {
		if (!(out = fopen(filename, "w"))) {
			fprintf(stderr, "failed to open file `%s`: %m\n",
				filename);
			xfree(filename);
			return errno;
		}
	}

	exit_code = slurm_job_batch_script(out, jobid);

	if (out != stdout)
		fclose(out);

	if (exit_code != SLURM_SUCCESS) {
		if (out != stdout)
			unlink(filename);
		slurm_perror("job script retrieval failed");
	} else if ((out != stdout) && (quiet_flag != 1)) {
		printf("batch script for job %u written to %s\n",
		       jobid, filename);
	}

	xfree(filename);
	return exit_code;
}
