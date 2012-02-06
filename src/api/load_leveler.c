/*****************************************************************************\
 *  load_leveler.c - Present SLURM commands with the standard SLURM APIs, but
 *  interact with IBM's LoadLeveler rather than the SLURM daemons.
 *
 *  NOTES:
 *  LoadLeveler JobID is a string and not a number
 *  LoadLeveler ResourceRequirements are mapped to SLURM Generic Resources
 *****************************************************************************
 *  Copyright (C) 2011-2012 SchedMD <http://www.schedmd.com>.
 *  Written by Morris Jette <jette@schedmd.com>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.schedmd.com/slurmdocs/>.
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
#  ifdef HAVE_LLAPI_H
#    include <llapi.h>
#  endif
#endif

#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <utmp.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"
#include "src/common/hostlist.h"
#include "src/common/jobacct_common.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#ifdef USE_LOADLEVELER

#define PTY_MODE true

/* Set this to generate debugging information for salloc front-end/back-end
 *	program communications */
#define _DEBUG_SALLOC 1

/* Timeout for salloc front-end/back-end messages in usec */
#define MSG_TIMEOUT 5000000

#define OP_CODE_EXIT 0x0101
#define OP_CODE_EXEC 0x0102

static uint32_t   fe_auth_key = 0;
static slurm_fd_t fe_comm_socket = -1;
static char      *fe_job_id = NULL;
static bool       fe_job_killed = false;

#ifdef HAVE_LLAPI_H
/*****************************************************************************\
 * Local symbols
\*****************************************************************************/
static char *global_node_str = NULL;
static uint32_t global_cpu_cnt = 0, global_node_cnt = 0;

/*****************************************************************************\
 * Local helper functions
\*****************************************************************************/
static void _jobacct_del(void *x);
static void _load_adapter_info_job(LL_element *adapter, job_info_t *job_ptr);
static void _load_adapter_info_step(LL_element *adapter,
				    job_step_info_t *step_ptr);
static void _load_credential_info_job(LL_element *credential,
				      job_info_t *job_ptr);
static void _load_credential_info_step(LL_element *credential,
				       job_step_info_t *step_ptr);
static void _load_global_node_list(void);
static void _load_node_info_job(LL_element *node, job_info_t *job_ptr);
static void _load_node_info_step(LL_element *node, job_step_info_t *step_ptr);
static void _load_resource_info_job(LL_element *resource, job_info_t *job_ptr);
static void _load_resource_info_step(LL_element *resource,
				     job_step_info_t *step_ptr);
static void _load_step_info_job(LL_element *step, job_info_t *job_ptr,
				int step_inx);
static void _load_step_info_step(LL_element *step, job_step_info_t *step_ptr);
static void _load_task_info_job(LL_element *task, job_info_t *job_ptr);
static void _proc_disp_use_stat(LL_element *disp_use,
				jobacctinfo_t *job_acct_ptr,
			        int node_inx, int task_inx);
static int  _proc_mach_use_stat(LL_element *mach_use, List stats_list,
				int node_inx, int task_inx, char *node_name);
static void _proc_step_stat(LL_element *step, List stats_list);
static void _test_step_id (LL_element *step, char *job_id, uint32_t step_id,
			   bool *match_job_id, bool *match_step_id);

static void _jobacct_del(void *x)
{
	job_step_stat_t *step_stat_ptr = (job_step_stat_t *) x;

	xfree(step_stat_ptr->jobacct);
	xfree(step_stat_ptr->step_pids->pid);
	xfree(step_stat_ptr->step_pids);
	xfree(step_stat_ptr);
}

/* Load a adapter's information into a job record */
static void _load_adapter_info_job(LL_element *adapter, job_info_t *job_ptr)
{
	char *mode;
	int rc;

	rc = ll_get_data(adapter, LL_AdapterReqMode, &mode);
	if (!rc) {
		job_ptr->network = xstrdup(mode);
		free(mode);
	}
}

/* Load a adapter's information into a step record */
static void _load_adapter_info_step(LL_element *adapter,
				    job_step_info_t *step_ptr)
{
	char *mode;
	int rc;

	rc = ll_get_data(adapter, LL_AdapterReqMode, &mode);
	if (!rc) {
		step_ptr->network = xstrdup(mode);
		free(mode);
	}
}

/* Load a credential's information into a job record */
static void _load_credential_info_job(LL_element *credential,
				      job_info_t *job_ptr)
{
	int rc;
	int gid, uid;

	rc = ll_get_data(credential, LL_CredentialGid, &gid);
	if (!rc)
		job_ptr->group_id = (uint32_t) gid;
	rc = ll_get_data(credential, LL_CredentialUid, &uid);
	if (!rc)
		job_ptr->user_id = (uint32_t) uid;
}

/* Load a credential's information into a step record */
static void _load_credential_info_step(LL_element *credential,
				       job_step_info_t *step_ptr)
{
	int rc, uid;

	rc = ll_get_data(credential, LL_CredentialUid, &uid);
	if (!rc)
		step_ptr->user_id = (uint32_t) uid;
}

/* Load global information about nodes (names, node count and CPU count) */
static void _load_global_node_list(void)
{
	LL_element *query_object, *machine;
	char *name;
	hostlist_t hl;
	int cpus, err_code, len, obj_count, rc;

	if (global_node_str)
		return;

	query_object = ll_query(MACHINES);
	if (!query_object) {
		verbose("ll_query(MACHINES) failed");
		return;
	}

	rc = ll_set_request(query_object, QUERY_ALL, NULL, ALL_DATA);
	if (rc) {
		verbose("ll_set_request(MACHINES, ALL), error %d", rc);
		return;
	}

	machine = ll_get_objs(query_object, LL_CM, NULL, &obj_count, &err_code);
	if (!machine) {
		verbose("ll_get_objs(MACHINES), error %d", err_code);
		return;
	}

	hl = hostlist_create(NULL);
	while (machine) {
		if (ll_get_data(machine, LL_MachineName, &name)) {
			verbose("ll_get_data(LL_MachineName) failed");
		} else {
			char *sep = strchr(name, '.');
			if (sep)
				sep[0] = '\0';
			hostlist_push(hl, name);
			free(name);
		}

		if (ll_get_data(machine, LL_MachineCPUs, &cpus)) {
			verbose("ll_get_data(LL_MachineCPUs) failed");
		} else {
			global_cpu_cnt += cpus;
		}
		global_node_cnt++;

		machine = ll_next_obj(query_object);
	}

	len = (global_node_cnt + 1) * 16;
	global_node_str = xmalloc(len);
	while (hostlist_ranged_string(hl, len, global_node_str) < 0) {
		len *= 2;
		xrealloc(global_node_str, len);
	}
	hostlist_destroy(hl);

	ll_free_objs(machine);
	ll_deallocate(machine);
}

/* Load a node's information into a job record */
static void _load_node_info_job(LL_element *node, job_info_t *job_ptr)
{
	int rc;
	LL_element *resource = NULL, *task = NULL;

	rc = ll_get_data(node, LL_NodeGetFirstResourceRequirement, &resource);
	if (!rc && resource)
		_load_resource_info_job(resource, job_ptr);
	rc = ll_get_data(node, LL_NodeGetFirstTask, &task);
	if (!rc && task)
		_load_task_info_job(task, job_ptr);
}

/* Load a node's information into a step record */
static void _load_node_info_step(LL_element *node, job_step_info_t *step_ptr)
{
	int rc;
	LL_element *resource = NULL;

	rc = ll_get_data(node, LL_NodeGetFirstResourceRequirement, &resource);
	if (!rc && resource)
		_load_resource_info_step(resource, step_ptr);
}

/* Load a resource's information into a job record */
static void _load_resource_info_job(LL_element *resource, job_info_t *job_ptr)
{
	int rc;
	char *name, *sep;
	int value;

	rc = ll_get_data(resource, LL_ResourceRequirementName, &name);
	if (rc)
		return;
	rc = ll_get_data(resource, LL_ResourceRequirementValue, &value);
	if (!rc) {
		if (job_ptr->gres)
			sep = ",";
		else
			sep = "";
		xstrfmtcat(job_ptr->gres, "%s%s:%d", sep, name, value);
	}
	free(name);
}

/* Load a resource's information into a step record */
static void _load_resource_info_step(LL_element *resource,
				     job_step_info_t *step_ptr)
{
	int rc;
	char *name, *sep;
	int value;

	rc = ll_get_data(resource, LL_ResourceRequirementName, &name);
	if (rc)
		return;
	rc = ll_get_data(resource, LL_ResourceRequirementValue, &value);
	if (!rc) {
		if (step_ptr->gres)
			sep = ",";
		else
			sep = "";
		xstrfmtcat(step_ptr->gres, "%s%s:%d", sep, name, value);
	}
	free(name);
}

/* Load a step's information into a job record */
static void _load_step_info_job(LL_element *step, job_info_t *job_ptr,
				int step_inx)
{
	LL_element *adapter = NULL, *node = NULL;
	int rc;
	char *account, *class, *comment, *dependency, *end_ptr, *nodes_req;
	char *resv_name, *state_desc, *step_id, *work_dir;
	int cpus_core, exit_code, node_cnt, node_usage;
	int priority, restart, start_cnt, step_state, task_cnt, tasks_node;
	time_t fini_time, start_time;
	char **hosts;
	int64_t time_limit;

	/* This must be first, exit code of all steps must be tested */
	rc = ll_get_data(step, LL_StepCompletionCode, &exit_code);
	if (!rc) {
		if (step_inx == 0)
			job_ptr->exit_code = exit_code;
		job_ptr->derived_ec = MAX(job_ptr->derived_ec, exit_code);
	}
	if (step_inx > 0)
		return;
	/* The remaining fields only need to be read for the first step */

	rc = ll_get_data(step, LL_StepAccountNumber, &account);
	if (!rc) {
		job_ptr->account = xstrdup(account);
		free(account);
	}

	rc = ll_get_data(step, LL_StepJobClass, &class);
	if (!rc) {
		job_ptr->partition = xstrdup(class);
		free(class);
	}

	rc = ll_get_data(step, LL_StepComment, &comment);
	if (!rc) {
		job_ptr->comment = xstrdup(comment);
		free(comment);
	}

	rc = ll_get_data(step, LL_StepID, &step_id);
	if (!rc) {
		/* NOTE: JobID is a number: "<hostname>.<jobid>.<stepid>"
		 * For SLURM's job ID, use only short hostname and
		 * remove the trailing ".<stepid>" */
		char *sep1, *sep2, *sep3;
		sep3 = strrchr(step_id, '.');
		if (sep3 && (sep3 != step_id))
			sep3[0] = '\0';
		sep2 = strrchr(step_id, '.');
		if (sep2 && (sep2 != step_id))
			sep2[0] = '\0';
		sep1 = strchr(step_id, '.');
		if (sep1)
			sep1[0] = '\0';
		job_ptr->job_id = NULL;
		xstrfmtcat(job_ptr->job_id, "%s.%s", step_id, sep2+1);
		free(step_id);
	}

	rc = ll_get_data(step, LL_StepCompletionDate, &fini_time);
	if (!rc)
		job_ptr->end_time = fini_time;

	rc = ll_get_data(step, LL_StepCpusPerCore, &cpus_core);
	if (!rc)
		job_ptr->threads_per_core = cpus_core;

	/* NOTE: Dependency format differs from SLURM */
	rc = ll_get_data(step, LL_StepDependency, &dependency);
	if (!rc) {
		job_ptr->dependency = xstrdup(dependency);
		free(dependency);
	}

	rc = ll_get_data(step, LL_StepEstimatedStartTime, &start_time);
	if (!rc)
		job_ptr->start_time = start_time;

	rc = ll_get_data(step, LL_StepTotalNodesRequested, &nodes_req);
	if (!rc && nodes_req) {
		job_ptr->num_nodes = strtol(nodes_req, &end_ptr, 10);
		if (end_ptr && (end_ptr[0] == ','))
			job_ptr->max_nodes = strtol(end_ptr+1, &end_ptr, 10);
		free(nodes_req);
	}

	rc = ll_get_data(step, LL_StepTotalTasksRequested, &task_cnt);
	if (!rc) {
		job_ptr->max_cpus = task_cnt;
		job_ptr->num_cpus = task_cnt;
	}

	rc = ll_get_data(step, LL_StepState, &step_state);
	if (rc) {
		job_ptr->job_state = JOB_PENDING;	/* best guess */
	} else if ((step_state == STATE_RUNNING) ||
		   (step_state == STATE_STARTING)) {
		job_ptr->job_state = JOB_RUNNING;
		if (step_state == STATE_STARTING)
			job_ptr->job_state |= JOB_CONFIGURING;
		/* See LL_StepEstimatedStartTime above */
		rc = ll_get_data(step, LL_StepDispatchTime, &start_time);
		if (!rc)
			job_ptr->start_time = start_time;

		/* See LL_StepTotalNodesRequested above */
		rc = ll_get_data(step, LL_StepNodeCount, &node_cnt);
		if (!rc)
			job_ptr->num_nodes = node_cnt;

		/* See LL_StepTotalTasksRequested above*/
		rc = ll_get_data(step, LL_StepTaskInstanceCount, &task_cnt);
		if (!rc) {
			job_ptr->max_cpus = task_cnt;
			job_ptr->num_cpus = task_cnt;
		}
	} else if ((step_state == STATE_IDLE) ||
		   (step_state == STATE_PENDING)) {
		job_ptr->job_state = JOB_PENDING;
	} else if (step_state == STATE_CANCELED) {
		job_ptr->job_state = JOB_CANCELLED;
	} else if ((step_state == STATE_PREEMPTED) ||
		   (step_state == STATE_PREEMPT_PENDING)) {
		job_ptr->job_state = JOB_PREEMPTED;
	} else {
		job_ptr->job_state = JOB_COMPLETE;
	}

	rc = ll_get_data(step, LL_StepHostList, &hosts);
	if (!rc) {
		int i, len;
		hostlist_t hl;
		hl = hostlist_create(NULL);
		for (i = 0; hosts[i]; i++) {
			char *sep = strchr(hosts[i], '.');
			if (sep)
				sep[0] = '\0';
			if ((i == 0) && job_ptr->batch_flag)
				job_ptr->batch_host = xstrdup(hosts[i]);
			hostlist_push(hl, hosts[i]);
			free(hosts[i]);
		}
		len = (i + 1) * 16;
		job_ptr->nodes = xmalloc(len);
		while (hostlist_ranged_string(hl, len, job_ptr->nodes) < 0) {
			len *= 2;
			xrealloc(job_ptr->nodes, len);
		}
		hostlist_destroy(hl);
		free(hosts);
	}

	rc = ll_get_data(step, LL_StepIwd, &work_dir);
	if (!rc) {
		job_ptr->work_dir = xstrdup(work_dir);
		free(work_dir);
	}

	rc = ll_get_data(step, LL_StepMessages, &state_desc);
	if (!rc) {
		if (state_desc[0] != '\0')
			job_ptr->state_desc = xstrdup(state_desc);
		free(state_desc);
	}

	rc = ll_get_data(step, LL_StepNodeUsage, &node_usage);
	if (!rc && (node_usage == SHARED))
		job_ptr->shared = 1;

	rc = ll_get_data(step, LL_StepPriority, &priority);
	if (!rc)
		job_ptr->priority = priority;

	rc = ll_get_data(step, LL_StepReservationID, &resv_name);
	if (!rc) {
		job_ptr->resv_name = xstrdup(resv_name);
		free(resv_name);
	}

	rc = ll_get_data(step, LL_StepRestart, &restart);
	if (!rc && restart)
		job_ptr->requeue = 1;

	rc = ll_get_data(step, LL_StepStartCount, &start_cnt);
	if (!rc && start_cnt)
		job_ptr->restart_cnt = start_cnt - 1;

	rc = ll_get_data(step, LL_StepTasksPerNodeRequested, &tasks_node);
	if (!rc)
		job_ptr->ntasks_per_node = tasks_node;
	rc = ll_get_data(step, LL_StepWallClockLimitHard64, &time_limit);
	if (!rc) {
		if (time_limit == 0x7fffffff)
			job_ptr->time_limit = INFINITE;
		else
			job_ptr->time_limit = time_limit;
	}

	rc = ll_get_data(step, LL_StepWallClockLimitSoft64, &time_limit);
	if (!rc) {
		if (time_limit == 0x7fffffff)
			job_ptr->time_min = INFINITE;
		else
			job_ptr->time_min = time_limit;
	}

	rc = ll_get_data(step, LL_StepGetFirstNode, &node);
	if (!rc && node)
		_load_node_info_job(node, job_ptr);

	rc = ll_get_data(step, LL_StepGetFirstAdapterReq, &adapter);
	if (!rc && adapter)
		_load_adapter_info_job(node, job_ptr);

	/* Set some default values for other fields */
	job_ptr->state_reason = WAIT_NO_REASON;
}

/* Load a step's information into a step record */
static void _load_step_info_step(LL_element *step, job_step_info_t *step_ptr)
{
	LL_element *adapter = NULL, *node = NULL;
	int rc;
	char *ckpt_dir, *class, *name, *step_id;
	int step_state, task_cnt;
	time_t start_time;
	char **hosts;
	int64_t time_limit;

	rc = ll_get_data(step, LL_StepCkptExecuteDirectory, &ckpt_dir);
	if (!rc) {
		step_ptr->ckpt_dir = xstrdup(ckpt_dir);
		free(ckpt_dir);
	}

	rc = ll_get_data(step, LL_StepJobClass, &class);
	if (!rc) {
		step_ptr->partition = xstrdup(class);
		free(class);
	}

	rc = ll_get_data(step, LL_StepName, &name);
	if (!rc) {
		step_ptr->name = xstrdup(name);
		free(name);
	}

	rc = ll_get_data(step, LL_StepID, &step_id);
	if (!rc) {
		/* NOTE: JobID is a number: "<hostname>.<jobid>.<stepid>"
		 * For SLURM's job ID, use only short hostname and
		 * remove the trailing ".<stepid>" */
		char *sep1, *sep2, *sep3;
		sep3 = strrchr(step_id, '.');
		if (sep3 && (sep3 != step_id)) {
			step_ptr->step_id = strtol(sep3+1, NULL, 10);
			sep3[0] = '\0';
		}
		sep2 = strrchr(step_id, '.');
		if (sep2 && (sep2 != step_id))
			sep2[0] = '\0';
		sep1 = strchr(step_id, '.');
		if (sep1)
			sep1[0] = '\0';
		step_ptr->job_id = NULL;
		xstrfmtcat(step_ptr->job_id, "%s.%s", step_id, sep2+1);
		free(step_id);
	}

	rc = ll_get_data(step, LL_StepTotalTasksRequested, &task_cnt);
	if (!rc) {
		step_ptr->num_cpus = task_cnt;
		step_ptr->num_tasks = task_cnt;
	}

	rc = ll_get_data(step, LL_StepState, &step_state);
	if (!rc && (step_state == STATE_RUNNING)) {
		/* See LL_StepEstimatedStartTime above */
		rc = ll_get_data(step, LL_StepDispatchTime, &start_time);
		if (!rc) {
			step_ptr->start_time = start_time;
			step_ptr->run_time = difftime(time(NULL), start_time);
		}

		/* See LL_StepTotalTasksRequested above*/
		rc = ll_get_data(step, LL_StepTaskInstanceCount, &task_cnt);
		if (!rc) {
			step_ptr->num_cpus = task_cnt;
			step_ptr->num_tasks = task_cnt;
		}

		rc = ll_get_data(step, LL_StepHostList, &hosts);
		if (!rc) {
			char *sep;
			int i, len;
			hostlist_t hl;
			hl = hostlist_create(NULL);
			for (i = 0; hosts[i]; i++) {
				sep = strchr(hosts[i], '.');
				if (sep)
					sep[0] = '\0';
				hostlist_push(hl, hosts[i]);
				free(hosts[i]);
			}
			len = (i + 1) * 16;
			step_ptr->nodes = xmalloc(len);
			while (hostlist_ranged_string(hl, len,
						      step_ptr->nodes) < 0) {
				len *= 2;
				xrealloc(step_ptr->nodes, len);
			}
			hostlist_destroy(hl);
			free(hosts);
		} else {
			step_ptr->nodes = xstrdup("(UNKNOWN)");
		}
	} else {
		step_ptr->run_time = NO_VAL;
		step_ptr->nodes = xstrdup("(NOT_RUNNING)");
	}

	rc = ll_get_data(step, LL_StepWallClockLimitHard64, &time_limit);
	if (!rc) {
		if (time_limit == 0x7fffffff)
			step_ptr->time_limit = INFINITE;
		else
			step_ptr->time_limit = time_limit;
	}

	rc = ll_get_data(step, LL_StepGetFirstNode, &node);
	if (!rc && node)
		_load_node_info_step(node, step_ptr);

	rc = ll_get_data(step, LL_StepGetFirstAdapterReq, &adapter);
	if (!rc && adapter)
		_load_adapter_info_step(node, step_ptr);
}

/* Load a task's information into a job record */
static void _load_task_info_job(LL_element *task, job_info_t *job_ptr)
{
	int rc;
	char *command;

	rc = ll_get_data(task, LL_TaskExecutable, &command);
	if (!rc) {
		job_ptr->command = xstrdup(command);
		free(command);
	}
}

/* NOTE: Fields not set: max_pages, max_pages_id, tot_pages,
 *	 max_vsize, max_vsize_id, tot_vsize
 * NOTE: pid is always set to 1 (init) */
static void _proc_disp_use_stat(LL_element *disp_use,
				jobacctinfo_t *job_acct_ptr,
			        int node_inx, int task_inx)
{
	int64_t id_rss, is_rss, max_rss, tot_rss;
	int64_t sys_time, user_time, tot_time;
	bool first_pass = (job_acct_ptr->pid == 0);

/* FIXME: What are the units of these values */
	ll_get_data(disp_use, LL_DispUsageStepIdrss64, &id_rss);
	ll_get_data(disp_use, LL_DispUsageStepIsrss64, &is_rss);
	ll_get_data(disp_use, LL_DispUsageStepMaxrss64, &max_rss);
	ll_get_data(disp_use, LL_DispUsageStepSystemTime64, &sys_time);
	ll_get_data(disp_use, LL_DispUsageStepUserTime64, &user_time);

	job_acct_ptr->pid = 1;
	job_acct_ptr->sys_cpu_sec = sys_time;
	job_acct_ptr->sys_cpu_usec = 0;
	job_acct_ptr->user_cpu_sec = user_time;
	job_acct_ptr->user_cpu_usec = 0;
	tot_time = sys_time + user_time;
	job_acct_ptr->tot_cpu += tot_time;
	if (first_pass || (job_acct_ptr->min_cpu > tot_time)) {
		job_acct_ptr->min_cpu = tot_time;
		job_acct_ptr->min_cpu_id.nodeid = node_inx;
		job_acct_ptr->min_cpu_id.taskid = task_inx;
	}

	tot_rss = id_rss + is_rss;
	job_acct_ptr->tot_rss += tot_rss;
	if (first_pass || (job_acct_ptr->max_rss < tot_rss)) {
		job_acct_ptr->max_rss = tot_rss;
		job_acct_ptr->max_rss_id.nodeid = node_inx;
		job_acct_ptr->max_rss_id.taskid = task_inx;
	}
}

/* Return the number of tasks dispatched on this machine */
static int _proc_mach_use_stat(LL_element *mach_use, List stats_list,
			       int node_inx, int task_inx, char *node_name)
{
	job_step_stat_t *step_stat_ptr;
	jobacctinfo_t *job_acct_ptr;
	job_step_pids_t *job_pids_ptr;
	LL_element *disp_use = NULL;
	int task_cnt = 0;

	step_stat_ptr = xmalloc(sizeof(job_step_stat_t));
	job_acct_ptr = xmalloc(sizeof(jobacctinfo_t));
	step_stat_ptr->jobacct = job_acct_ptr;
	job_pids_ptr = xmalloc(sizeof(job_step_pids_t));
	step_stat_ptr->step_pids = job_pids_ptr;
	list_append(stats_list, step_stat_ptr);

	job_pids_ptr->pid_cnt = 1;
	job_pids_ptr->pid = xmalloc(sizeof(uint32_t));
	job_pids_ptr->pid[0] = 1;	/* sstat needs something here */
	job_pids_ptr->node_name = xstrdup(node_name);

	ll_get_data(mach_use, LL_MachUsageGetFirstDispUsage, &disp_use);
	while (disp_use) {
		_proc_disp_use_stat(mach_use, job_acct_ptr, node_inx, task_inx);
		task_cnt++;
		task_inx++;
		disp_use = NULL;
		ll_get_data(mach_use, LL_MachUsageGetNextDispUsage, &disp_use);
	}
	step_stat_ptr->num_tasks = task_cnt;

	return task_cnt;
}

static void _proc_step_stat(LL_element *step, List stats_list)
{
	LL_element *machine = NULL, *mach_use = NULL;
	char *node_name = NULL;
	int node_inx = 0, task_inx = 0, rc;

	ll_get_data(step, LL_StepGetFirstMachine, &machine);
	ll_get_data(step, LL_StepGetFirstMachUsage, &mach_use);
	while (mach_use) {
		node_name = NULL;
		if (machine) {
			ll_get_data(machine, LL_MachineName, &node_name);
			ll_get_data(step, LL_StepGetNextMachine, &machine);
		}
		rc = _proc_mach_use_stat(mach_use, stats_list, node_inx++,
					 task_inx, node_name);
		if (node_name)
			free(node_name);
		task_inx += rc;
		mach_use = NULL;
		ll_get_data(step, LL_StepGetNextMachUsage, &mach_use);
	}
}

/* Test if this step record is matches the input job_id and step_id.
 * Set match_job_id if job_ID matches, and
 * set match_step_id if step_id and job ID both match */
static void _test_step_id (LL_element *step, char *job_id, uint32_t step_id,
			   bool *match_job_id, bool *match_step_id)
{
	int rc;
	char *read_id = NULL;
	uint32_t this_step_id = NO_VAL;

	*match_job_id = false;
	*match_step_id = false;
	rc = ll_get_data(step, LL_StepID, &read_id);
	if (!rc) {
		/* NOTE: JobID is a number: "<hostname>.<jobid>.<stepid>"
		 * For SLURM's job ID, remove the trailing ".<stepid>" */
		char *sep1, *sep2, *sep3, *new_job_id = NULL;
		sep3 = strrchr(read_id, '.');
		if (sep3 && (sep3 != read_id)) {
			this_step_id = strtol(sep3+1, NULL, 10);
			sep3[0] = '\0';
		}
		sep2 = strrchr(read_id, '.');
		if (sep2)
			sep2[0] = '\0';
		sep1 = strchr(read_id, '.');
		if (sep1)
			sep1[0] = '\0';
		xstrfmtcat(new_job_id, "%s.%s", read_id, sep2+1);
		if (!job_id || !strcmp(job_id, new_job_id)) {
			*match_job_id = true;
			if ((step_id == NO_VAL) || (step_id == this_step_id))
				*match_step_id = true;
		}
		xfree(new_job_id);
		free(read_id);
	}
}
#endif	/* HAVE_LLAPI_H */

/*****************************************************************************\
 * Local helper functions for salloc front-end/back-end support
 * NOTE: These functions are needed for testing purposes even without llapi.h
\*****************************************************************************/

/* Generate and return a pseudo-random authentication key */
static uint32_t _gen_auth_key(void)
{
	struct timeval tv;
	uint32_t key;

	gettimeofday(&tv, NULL);
	key  = (tv.tv_sec % 1000) * 1000000;
	key += tv.tv_usec;

	return key;
}

#ifndef HAVE_LLAPI_H
/* Abort the back-end job
 * Return true if abort message send */
static bool _xmit_abort(void)
{
	char buf[32], *host, *sep;
	uint32_t resp_auth_key;
	uint16_t resp_port, op_code = OP_CODE_EXIT;
	char *auth_key  = getenv("SLURM_BE_KEY");
	char *sock_addr = getenv("SLURM_BE_SOCKET");
	slurm_fd_t resp_socket;
	slurm_addr_t resp_addr;
	int i;

	if (!auth_key || !sock_addr)
		return false;

	host = xstrdup(sock_addr);
	sep = strchr(host, ':');
	if (!sep) {
		xfree(host);
		return false;
	}
	sep[0] = '\0';
	resp_port = atoi(sep+1);
	slurm_set_addr(&resp_addr, resp_port, host);
	xfree(host);
	resp_socket = slurm_open_stream(&resp_addr);
	if (resp_socket < 0) {
		error("slurm_open_msg_conn(%s:%hu): %m", host, resp_port);
		return false;
	}

	resp_auth_key = atoi(auth_key);
	memcpy(buf+0, &resp_auth_key, 4);
	memcpy(buf+4, &op_code,       2);
	i = slurm_write_stream_timeout(resp_socket, buf, 6, MSG_TIMEOUT);
	if (i < 6) {
		error("xmit_resp abort: %m");
		return false;
	}
	slurm_shutdown_msg_engine(resp_socket);

	return true;
}
#endif

static bool _xmit_resp(slurm_fd_t socket_conn, uint32_t resp_auth_key,
		       uint32_t new_auth_key, uint16_t comm_port)
{
	int i;
	char buf[32];

	memcpy(buf+0, &resp_auth_key, 4);
	memcpy(buf+4, &new_auth_key,  4);
	memcpy(buf+8, &comm_port,     2);

	i = slurm_write_stream_timeout(socket_conn, buf, 10, MSG_TIMEOUT);
	if (i < 10) {
		error("xmit_resp write: %m");
		return false;
	}

	return true;
}

static void _read_be_key(slurm_fd_t socket_conn, char *hostname)
{
	char *sock_env = NULL;
	uint32_t read_key;
	uint16_t comm_port;
	char buf[32];
	int i;

	i = slurm_read_stream(socket_conn, buf, 6);
	if (i != 6) {
		error("_read_be_key: %d", i);
		return;
	}
	memcpy(&read_key,  buf+0, 4);
	memcpy(&comm_port, buf+4, 2);


	xstrfmtcat(sock_env, "%u", read_key);
	if (setenv("SLURM_BE_KEY", sock_env, 1))
		fatal("setenv(SLURM_BE_KEY): %m");
#ifdef _DEBUG_SALLOC
	info("SLURM_BE_KEY=%u", read_key);
#endif
	xfree(sock_env);

	xstrfmtcat(sock_env, "%s:%hu", hostname, comm_port);
	if (setenv("SLURM_BE_SOCKET", sock_env, 1))
		fatal("setenv(SLURM_BE_SOCKET): %m");
#ifdef _DEBUG_SALLOC
	info("SLURM_BE_SOCKET=%s", sock_env);
#endif
	xfree(sock_env);
}

/* Validate a message connection
 * Return: true=valid/authenticated */
static bool _validate_connect(slurm_fd_t socket_conn, uint32_t auth_key)
{
	struct timeval tv;
	fd_set read_fds;
	uint32_t read_key;
	bool valid = false;
	int i, n_fds;

	n_fds = socket_conn;
	while (1) {
		FD_ZERO(&read_fds);
		FD_SET(socket_conn, &read_fds);

		tv.tv_sec = 2;
		tv.tv_usec = 0;
		i = select((n_fds + 1), &read_fds, NULL, NULL, &tv);
		if (i == 0)
			break;
		if (i < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		i = slurm_read_stream(socket_conn, (char *)&read_key,
				      sizeof(read_key));
		if ((i == sizeof(read_key)) && (read_key == auth_key))
			valid = true;
		else {
			error("error validating incoming socket connection");
			sleep(1);	/* Help prevent brute force attack */
		}
		break;
	}

	return valid;
}

/* Spawn a child process */
static void *_wait_pid_thread(void *pid_buf)
{
	int status;
	pid_t pid;

	memcpy((char *) &pid, pid_buf, sizeof(pid_t));
	waitpid(pid, &status, 0);
	return NULL;
}
static void _wait_pid(pid_t pid)
{
	pthread_attr_t thread_attr;
	pthread_t thread_id = 0;
	char *pid_buf = xmalloc(sizeof(pid_t));

	memcpy(pid_buf, (char *) &pid, sizeof(pid_t));
	slurm_attr_init(&thread_attr);
	if (pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_DETACHED))
		error("pthread_attr_setdetachstate error %m");
	if (pthread_create(&thread_id, &thread_attr, _wait_pid_thread,
                           (void *) pid_buf)) {
		error("pthread_create: %m");
	}
	slurm_attr_destroy(&thread_attr);
}
static void _spawn_proc(char *exec_line)
{
	char **argv, *save_ptr = NULL, *tok;
	pid_t pid;
	int i;

	info("msg: %s", exec_line);
	pid = fork();
	if (pid < 0) {
		error("fork: %m");
		return;
	}
	if (pid > 0) {
		_wait_pid(pid);	/* Spawn thread to waid and avoid zombies */
		return;
	}

/* Need to deal with quoted and escaped spaces */
	i = 0;
	argv = xmalloc(sizeof(char *) * (strlen(exec_line) / 2 + 2));
	tok = strtok_r(exec_line, " ", &save_ptr);
	while (tok) {
		argv[i++] = tok;
		tok = strtok_r(NULL, " ", &save_ptr);
	}

	for (i = 0; i < 128; i++)
		(void) close(i);
	execvp(argv[0], argv);
}

/* Process incoming requests
 * comm_socket IN - socket to read from
 * auth_key IN - authentication key we are looking for
 * RETURN true to terminate
 */
static bool _be_proc_comm(slurm_fd_t comm_socket, uint32_t auth_key)
{
	uint16_t op_code, msg_size;
	slurm_fd_t be_comm_conn;
	slurm_addr_t be_addr;
	char *msg;
	int i;
	bool term_flag = false;

	be_comm_conn = slurm_accept_stream(comm_socket, &be_addr);
	if (be_comm_conn == SLURM_SOCKET_ERROR) {
		error("slurm_accept_stream: %m");
		return false;
	}

	if (!_validate_connect(be_comm_conn, auth_key))
		goto fini;

	i = slurm_read_stream(be_comm_conn, (char *)&op_code, sizeof(op_code));
	if (i != sizeof(op_code)) {
		error("socket read, bad op_code size: %d", i);
		goto fini;
	}
	if (op_code == OP_CODE_EXIT) {
		term_flag = true;
	} else if (op_code == OP_CODE_EXEC) {
		i = slurm_read_stream(be_comm_conn, (char *)&msg_size,
				      sizeof(msg_size));
		if (i != sizeof(msg_size)) {
			error("socket read, bad msg_size size: (%d != %u)",
			      i, (uint32_t) sizeof(msg_size));
			return false;
		}
		msg = xmalloc(msg_size);
		i = slurm_read_stream(be_comm_conn, msg, msg_size);
		if (i != msg_size) {
			error("socket read, bad message size: (%d != %u)",
			      i, msg_size);
			xfree(msg);
			return false;
		}
		_spawn_proc(msg);
		xfree(msg);
	} else {
		error("socket read, bad op_code: %hu", op_code);
	}

fini:	if (be_comm_conn != SLURM_SOCKET_ERROR)
		slurm_close_accepted_conn(be_comm_conn);
	return term_flag;
}

/* Test that the job still exists in LoadLeveler.
 * Return true if job is being killed or already finished. */
static bool _fe_test_job_fini(char *job_id)
{
	int i, rc;
	job_info_t *job_ptr;
	job_info_msg_t *job_info_msg;
	bool job_fini = false;

	if (fe_job_killed)	/* SIGINT processed in salloc */
		return true;

	rc = slurm_load_job(&job_info_msg, job_id, SHOW_ALL);
	if (rc != SLURM_SUCCESS)
		return false;	/* can't determine job state */

	for (i = 0; i < job_info_msg->record_count; i++) {
		if (strcmp(job_info_msg->job_array[i].job_id, job_id))
			continue;
		job_ptr = &job_info_msg->job_array[i];
		if (job_ptr->job_state >= JOB_COMPLETE)
			job_fini = true;
		break;
	}
	slurm_free_job_info_msg(job_info_msg);

	return job_fini;
}

/* Front-end processes connection from backed.
 * Return true if connect is successful. */
static bool _fe_proc_connect(slurm_fd_t fe_comm_socket)
{
	slurm_fd_t fe_comm_conn = -1;
	slurm_addr_t be_addr;
	bool be_connected = false;

	while (1) {
		fe_comm_conn = slurm_accept_stream(fe_comm_socket, &be_addr);
		if (fe_comm_conn != SLURM_SOCKET_ERROR) {
			if (_validate_connect(fe_comm_conn, fe_auth_key))
				be_connected = true;
			break;
		}
		if (errno != EINTR) {
			error("slurm_accept_stream: %m");
			goto fini;
		}
	}

fini:	if (be_connected) {
		char hostname[256];
		uint16_t comm_port;
		slurm_get_addr(&be_addr, &comm_port, hostname,
			       sizeof(hostname));
		_read_be_key(fe_comm_conn, hostname);
	}
	if (fe_comm_conn >= 0)
		slurm_close_accepted_conn(fe_comm_conn);
	return be_connected;
}

/*****************************************************************************\
 * LoadLeveler lacks the ability to spawn an interactive job like SLURM.
 * The following functions provide an interface between an salloc front-end
 * process and a back-end process spawned as a batch job.
\*****************************************************************************/

/*
 * salloc_front_end - Open socket connections to communicate with a remote
 *	node process and build a batch script to submit.
 *
 * RETURN - remote processes exit code or -1 if some internal error
 */
extern char *salloc_front_end (void)
{
	char hostname[256];
	uint16_t comm_port;
	slurm_addr_t comm_addr;
	char *exec_line = NULL;

	/* Open socket for back-end program to communicate with */
	if ((fe_comm_socket = slurm_init_msg_engine_port(0)) < 0) {
		error("init_msg_engine_port: %m");
		return NULL;
	}
	if (slurm_get_stream_addr(fe_comm_socket, &comm_addr) < 0) {
		error("slurm_get_stream_addr: %m");
		return NULL;
	}
	comm_port = ntohs(((struct sockaddr_in) comm_addr).sin_port);
	fe_auth_key = _gen_auth_key();

	exec_line = xstrdup("#!/bin/bash\n");
	if (gethostname_short(hostname, sizeof(hostname)))
		fatal("gethostname_short(): %m");
	xstrfmtcat(exec_line, "%s/bin/salloc --salloc-be %s %hu %u\n",
		   SLURM_PREFIX, hostname, comm_port, fe_auth_key);

	return exec_line;
}

/*
 * salloc_back_end - Open socket connections with the salloc or srun command
 *	that submitted this program as a LoadLeveler batch job and use that to
 *	spawn other jobs (specificially, spawn poe for srun wrapper)
 *
 * argc IN - Count of elements in argv
 * argv IN - [0]:  Our executable name (e.g. salloc)
 *	     [1]:  "--salloc-be" (argument to spawn salloc backend)
 *	     [2]:  Hostname or address of front-end
 *	     [3]:  Port number for communications
 *	     [4]:  Authentication key
 * RETURN - remote processes exit code
 */
extern int salloc_back_end (int argc, char **argv)
{
	char *host = NULL;
	uint16_t comm_port = 0, resp_port = 0;
	slurm_addr_t comm_addr, resp_addr;
	slurm_fd_t comm_socket = SLURM_SOCKET_ERROR;
	slurm_fd_t resp_socket = SLURM_SOCKET_ERROR;
	fd_set read_fds;
	int i, n_fds;
	uint32_t new_auth_key, resp_auth_key;

	if (argc >= 5) {
		host   = argv[2];
		resp_port = atoi(argv[3]);
		resp_auth_key = atoi(argv[4]);
	}
	if ((argc < 5) || (resp_port == 0)) {
		error("Usage: salloc --salloc-be <salloc_host> "
		      "<salloc_stdin/out_port> <auth_key>\n");
		return 1;
	}

	/* Open sockets for back-end program to communicate with */
	/* Socket for stdin/stdout */
	if ((comm_socket = slurm_init_msg_engine_port(0)) < 0) {
		error("init_msg_engine_port: %m");
		goto fini;
	}
	if (slurm_get_stream_addr(comm_socket, &comm_addr) < 0) {
		error("slurm_get_stream_addr: %m");
		goto fini;
	}
	comm_port = ntohs(((struct sockaddr_in) comm_addr).sin_port);
	new_auth_key = _gen_auth_key();

	slurm_set_addr(&resp_addr, resp_port, host);
	resp_socket = slurm_open_stream(&resp_addr);
	if (resp_socket < 0) {
		error("slurm_open_msg_conn(%s:%hu): %m", host, resp_port);
		return 1;
	}
	_xmit_resp(resp_socket, resp_auth_key, new_auth_key, comm_port);
	slurm_shutdown_msg_engine(resp_socket);

	n_fds = comm_socket;
	while (true) {
		FD_ZERO(&read_fds);
		FD_SET(comm_socket, &read_fds);
		i = select((n_fds + 1), &read_fds, NULL, NULL, NULL);
		if (i == -1) {
			if (errno == EINTR)
				continue;
			error("select: %m");
			break;
		}

		if (FD_ISSET(comm_socket, &read_fds) &&
		    _be_proc_comm(comm_socket, new_auth_key)) {
			/* Remote resp_socket closed */
			break;
		}
	}

fini:	if (comm_socket >= 0)
		slurm_shutdown_msg_engine(comm_socket);
	exit(0);
}

/* send spawn process request to salloc back-end
 * exec_line IN - process execute line
 * Return 0 on success, -1 on error */
extern int salloc_be_spawn(char *exec_line)
{
	char buf[32], *host, *sep;
	uint32_t resp_auth_key;
	uint16_t exec_len, resp_port, op_code = OP_CODE_EXEC;
	char *auth_key  = getenv("SLURM_BE_KEY");
	char *sock_addr = getenv("SLURM_BE_SOCKET");
	slurm_fd_t resp_socket;
	slurm_addr_t resp_addr;
	int i;

	if (!exec_line) {
		error("salloc_be_spawn(): exec_line is NULL");
		return -1;
	}
	if (!auth_key || !sock_addr) {
		error("salloc_be_spawn(): SLURM_BE_KEY snd/or SLURM_BE_SOCKET "
		      "are NULL");
		return -1;
	}

	host = xstrdup(sock_addr);
	sep = strchr(host, ':');
	if (!sep) {
		error("salloc_be_spawn(): SLURM_BE_SOCKET is invalid: %s",
		      sock_addr);
		xfree(host);
		return -1;
	}
	sep[0] = '\0';
	resp_port = atoi(sep+1);
	slurm_set_addr(&resp_addr, resp_port, host);
	xfree(host);
	resp_socket = slurm_open_stream(&resp_addr);
	if (resp_socket < 0) {
		error("slurm_open_msg_conn(%s:%hu): %m", host, resp_port);
		return -1;
	}

	resp_auth_key = atoi(auth_key);
	exec_len = strlen(exec_line) + 1;
	memcpy(buf+0,  &resp_auth_key, 4);
	memcpy(buf+4,  &op_code,       2);
	memcpy(buf+6,  &exec_len,      2);
	i = slurm_write_stream_timeout(resp_socket, buf, 8, MSG_TIMEOUT);
	if (i < 8) {
		error("salloc_be_spawn write: %m");
		return -1;
	}
	i = slurm_write_stream_timeout(resp_socket, exec_line, exec_len,
				       MSG_TIMEOUT);
	if (i < exec_len) {
		error("salloc_be_spawn write: %m");
		return -1;
	}
	slurm_shutdown_msg_engine(resp_socket);

	return 0;
}

/*****************************************************************************\
 * Replacement functions for src/api/cancel.c
\*****************************************************************************/

/*
 * slurm_kill_job - send the specified signal to all steps of an existing job
 * IN job_id     - the job's id
 * IN signal     - signal number
 * IN batch_flag - 1 to signal batch shell only, otherwise 0
 * RET 0 on success, otherwise return -1 and set errno to indicate the error
 */
extern int slurm_kill_job (char *job_id, uint16_t signal,
			   uint16_t batch_flag)
{
	if (signal != SIGKILL) {
		slurm_seterrno(ESLURM_NOT_SUPPORTED);
		return -1;
	}
	return slurm_terminate_job (job_id);
}

/*
 * Kill a job step with job id "job_id" and step id "step_id", optionally
 *	sending the processes in the job step a signal "signal"
 * IN job_id     - the job's id
 * IN step_id    - the job step's id
 * IN signal     - signal number
 * RET 0 on success, otherwise return -1 and set errno to indicate the error
 */
extern int slurm_kill_job_step (char *job_id, uint32_t step_id,
				uint16_t signal)
{
	if (signal != SIGKILL) {
		slurm_seterrno(ESLURM_NOT_SUPPORTED);
		return -1;
	}
	return slurm_terminate_job_step (job_id, step_id);
}

/*****************************************************************************\
 * Replacement functions for src/api/job_info.c
\*****************************************************************************/

/*
 * slurm_load_job - issue RPC to get job information for one job ID
 * IN job_info_msg_pptr - place to store a job configuration pointer
 * IN job_id -  ID of job we want information about
 * IN show_flags -  job filtering option: 0, SHOW_ALL or SHOW_DETAIL
 * RET 0 or -1 on error
 * NOTE: free the response using slurm_free_job_info_msg
 */
extern int slurm_load_job (job_info_msg_t **resp, char *job_id,
			   uint16_t show_flags)
{
#ifdef HAVE_LLAPI_H
	job_info_msg_t *job_info_ptr;
	job_info_t *job_ptr;
	LL_element *credential, *job, *query_object, *step;
	int err_code, job_type, obj_count, rc;
	char *name, *submit_host;
	bool match_job_id, match_step_id;
	time_t submit_time;

	query_object = ll_query(JOBS);
	if (!query_object) {
		verbose("ll_query(JOBS) failed");
		return SLURM_COMMUNICATIONS_CONNECTION_ERROR;
	}

	rc = ll_set_request(query_object, QUERY_ALL, NULL, ALL_DATA);
	if (rc) {
		verbose("ll_set_request(JOBS, ALL), error %d", rc);
		return SLURM_COMMUNICATIONS_CONNECTION_ERROR;
	}

	job = ll_get_objs(query_object, LL_CM, NULL, &obj_count, &err_code);
	if (!job) {
		verbose("ll_get_objs(JOBS), error %d", err_code);
		return SLURM_COMMUNICATIONS_CONNECTION_ERROR;
	}

	job_info_ptr = xmalloc(sizeof(job_info_msg_t));
	job_info_ptr->last_update = time(NULL);
	job_info_ptr->record_count = 0;
	job_info_ptr->job_array = xmalloc(sizeof(job_info_t) * 1);

	while (job) {
		step = NULL;
		match_job_id = match_step_id = false;
		rc = ll_get_data(job, LL_JobGetFirstStep, &step);
		if (!rc && step) {
			_test_step_id(step, job_id, NO_VAL,
				      &match_job_id, &match_step_id);
		}
		if (!match_job_id) {
			job = ll_next_obj(query_object);
			continue;	/* Try next job */
		}

		/* We found a match */
		job_info_ptr->record_count = 1;
		job_ptr = &job_info_ptr->job_array[0];
		_load_step_info_job(step, job_ptr, 0);

		submit_host = NULL;
		rc = ll_get_data(job, LL_JobSubmitHost, &submit_host);
		if (!rc) {
			char *sep = strchr(submit_host, '.');
			if (sep)
				sep[0] = '\0';
			job_ptr->alloc_node = xstrdup(submit_host);
			free(submit_host);
		}

		credential = NULL;
		rc = ll_get_data(job, LL_JobCredential, &credential);
		if (!rc && credential)
			_load_credential_info_job(credential, job_ptr);

		name = NULL;
		rc = ll_get_data(job, LL_JobName, &name);
		if (!rc) {
			job_ptr->name = xstrdup(name);
			free(name);
		}

		rc = ll_get_data(job, LL_JobSubmitTime, &submit_time);
		if (!rc)
			job_ptr->submit_time = submit_time;

		rc = ll_get_data(job, LL_JobStepType, &job_type);
		if (!rc && (job_type = BATCH_JOB))
			job_ptr->batch_flag = 1;
		break;
	}

	ll_free_objs(job);
	ll_deallocate(job);

	*resp = job_info_ptr;
	return SLURM_PROTOCOL_SUCCESS;
#else
	job_info_msg_t *job_info_ptr;
	verbose("running without loadleveler");
	job_info_ptr = xmalloc(sizeof(job_info_msg_t));
	job_info_ptr->last_update = time(NULL);
	job_info_ptr->record_count = 0;
	job_info_ptr->job_array = xmalloc(0);
	*resp = job_info_ptr;
	return SLURM_PROTOCOL_SUCCESS;
#endif
}

/*
 * slurm_job_node_ready - report if nodes are ready for job to execute now
 * IN job_id - slurm job id
 * RET: READY_* values as defined in slurm.h
 */
extern int slurm_job_node_ready(char *job_id)
{
/* FIXME: Add LoadLeveler test here */
	return (READY_NODE_STATE | READY_JOB_STATE);
}

/*
 * slurm_load_jobs - issue RPC to get slurm all job configuration
 *	information if changed since update_time
 * IN update_time - time of current configuration data
 * IN job_info_msg_pptr - place to store a job configuration pointer
 * IN show_flags - job filtering options
 * RET 0 or -1 on error
 * NOTE: free the response using slurm_free_job_info_msg
 */
extern int slurm_load_jobs (time_t update_time, job_info_msg_t **resp,
			    uint16_t show_flags)
{
#ifdef HAVE_LLAPI_H
	job_info_msg_t *job_info_ptr;
	job_info_t *job_ptr;
	LL_element *credential, *job, *query_object, *step;
	int job_inx = -1, step_inx = -1;
	int err_code, job_type, obj_count, rc;
	char *name, *submit_host;
	time_t submit_time;

	query_object = ll_query(JOBS);
	if (!query_object) {
		verbose("ll_query(JOBS) failed");
		return SLURM_COMMUNICATIONS_CONNECTION_ERROR;
	}

	rc = ll_set_request(query_object, QUERY_ALL, NULL, ALL_DATA);
	if (rc) {
		verbose("ll_set_request(JOBS, ALL), error %d", rc);
		return SLURM_COMMUNICATIONS_CONNECTION_ERROR;
	}

	job = ll_get_objs(query_object, LL_CM, NULL, &obj_count, &err_code);
	if (!job) {
		verbose("ll_get_objs(JOBS), error %d", err_code);
		return SLURM_COMMUNICATIONS_CONNECTION_ERROR;
	}

	job_info_ptr = xmalloc(sizeof(job_info_msg_t));
	job_info_ptr->last_update = time(NULL);
	job_info_ptr->record_count = obj_count;
	job_info_ptr->job_array = xmalloc(sizeof(job_info_t) * obj_count);

	while (job) {
		if (++job_inx >= obj_count) {
			/* error handled outside of loop */
			break;
		}
		job_ptr = &job_info_ptr->job_array[job_inx];

		/* The following SLURM fields have no equivalent available
		 * from LoadLeveler:
		 * alloc_sid, assoc_id, batch_script, contiguous
		 * features, licenses, nice, qos, show_flags
		 * exc_nodes, exc_node_inx, req_nodes, req_node_inx
		 * node_inx (this could be calculated, but requires reading
		 *	     and searching the machine table)
		 * cpus_per_task
		 * cores_per_socket, sockets_per_node
		 * ntasks_per_core, ntasks_per_socket
		 * pn_min_memory, pn_min_cpus, pn_min_tmp_disk
		 * eligible_time, preempt_time, pre_sus_time, resize_time
		 * suspend_time
		 * req_switch, wait4switch, wckey
		 * job_resrcs, select_jobinfo
		 */

		submit_host = NULL;
		rc = ll_get_data(job, LL_JobSubmitHost, &submit_host);
		if (!rc) {
			char *sep = strchr(submit_host, '.');
			if (sep)
				sep[0] = '\0';
			job_ptr->alloc_node = xstrdup(submit_host);
			free(submit_host);
		}

		credential = NULL;
		rc = ll_get_data(job, LL_JobCredential, &credential);
		if (!rc && credential)
			_load_credential_info_job(credential, job_ptr);

		name = NULL;
		rc = ll_get_data(job, LL_JobName, &name);
		if (!rc) {
			job_ptr->name = xstrdup(name);
			free(name);
		}

		rc = ll_get_data(job, LL_JobSubmitTime, &submit_time);
		if (!rc)
			job_ptr->submit_time = submit_time;

		rc = ll_get_data(job, LL_JobStepType, &job_type);
		if (!rc && (job_type = BATCH_JOB))
			job_ptr->batch_flag = 1;

		step = NULL;
		rc = ll_get_data(job, LL_JobGetFirstStep, &step);
		while (!rc && step) {
			step_inx++;
			_load_step_info_job(step, job_ptr, step_inx);
			step = NULL;
			rc = ll_get_data(job, LL_JobGetNextStep, &step);
		}

		job = ll_next_obj(query_object);
	}

	ll_free_objs(job);
	ll_deallocate(job);
	if (++job_inx != obj_count) {
		verbose("ll_get_objs(JOBS), bad obj_count %d", obj_count);
		slurm_free_job_info_msg(job_info_ptr);
		return SLURM_COMMUNICATIONS_CONNECTION_ERROR;
	}

	*resp = job_info_ptr;
	return SLURM_PROTOCOL_SUCCESS;
#else
	job_info_msg_t *job_info_ptr;
	verbose("running without loadleveler");
	job_info_ptr = xmalloc(sizeof(job_info_msg_t));
	job_info_ptr->last_update = time(NULL);
	job_info_ptr->record_count = 0;
	job_info_ptr->job_array = xmalloc(0);
	*resp = job_info_ptr;
	return SLURM_PROTOCOL_SUCCESS;
#endif
}

/*****************************************************************************\
 * Replacement functions for src/api/job_step_info.c
\*****************************************************************************/
extern int slurm_job_step_get_pids(char * job_id, uint32_t step_id,
				   char *node_list,
				   job_step_pids_response_msg_t **resp)
{
	slurm_seterrno(ESLURM_NOT_SUPPORTED);
	return -1;
}

/*
 * slurm_get_job_steps - issue RPC to get specific slurm job step
 *	configuration information if changed since update_time.
 *	a job_id value of NO_VAL implies all jobs, a step_id value of
 *	NO_VAL implies all steps
 * IN update_time - time of current configuration data
 * IN job_id - get information for specific job id, NULL for all jobs
 * IN step_id - get information for specific job step id, NO_VAL for all
 *	job steps
 * IN step_response_pptr - place to store a step response pointer
 * IN show_flags - job step filtering options
 * RET 0 on success, otherwise return -1 and set errno to indicate the error
 * NOTE: free the response using slurm_free_job_step_info_response_msg
 */
extern int slurm_get_job_steps (time_t update_time, char *job_id,
				uint32_t step_id,
				job_step_info_response_msg_t **resp,
				uint16_t show_flags)
{
#ifdef HAVE_LLAPI_H
	job_step_info_response_msg_t *step_info_ptr;
	job_step_info_t *step_ptr;
	LL_element *query_object, *credential, *job, *step;
	int job_inx = -1, step_inx = -1, step_buf_cnt;
	int err_code, obj_count, rc;
	bool match_job_id, match_step_id;

	query_object = ll_query(JOBS);
	if (!query_object) {
		verbose("ll_query(JOBS) failed");
		return SLURM_COMMUNICATIONS_CONNECTION_ERROR;
	}

	rc = ll_set_request(query_object, QUERY_ALL, NULL, ALL_DATA);
	if (rc) {
		verbose("ll_set_request(JOBS, ALL), error %d", rc);
		return SLURM_COMMUNICATIONS_CONNECTION_ERROR;
	}

	job = ll_get_objs(query_object, LL_CM, NULL, &obj_count, &err_code);
	if (!job) {
		verbose("ll_get_objs(JOBS), error %d", err_code);
		return SLURM_COMMUNICATIONS_CONNECTION_ERROR;
	}

	step_buf_cnt = obj_count * 2;
	step_info_ptr = xmalloc(sizeof(node_info_msg_t));
	step_info_ptr->last_update = time(NULL);
	step_info_ptr->job_steps = xmalloc(sizeof(job_step_info_t) *
					   step_buf_cnt);

	while (job) {
		if (++job_inx >= obj_count) {
			/* error handled outside of loop */
			break;
		}

		step = NULL;
		match_job_id = match_step_id = false;
		rc = ll_get_data(job, LL_JobGetFirstStep, &step);
		if (!rc && step) {
			_test_step_id(step, job_id, step_id,
				      &match_job_id, &match_step_id);
		}
		if (!match_job_id || !match_step_id)
			goto next_job;

		if (++step_inx >= step_buf_cnt) {
			/* Need to increase step buffer size */
			step_buf_cnt *= 2;
			xrealloc(step_info_ptr->job_steps,
				 sizeof(job_step_info_t) * step_buf_cnt);
		}
		step_ptr = &step_info_ptr->job_steps[step_inx];

		/* The following SLURM fields have no equivalent available
		 * from LoadLeveler:
		 * ckpt_interval, resv_ports, select_jobinfo
		 * node_inx (this could be calculated, but requires reading
		 *	     and searching the machine table)
		 */

		credential = NULL;
		rc = ll_get_data(job, LL_JobCredential, &credential);
		if (!rc && credential)
			_load_credential_info_step(credential, step_ptr);

		while (!rc && step) {
			_load_step_info_step(step, step_ptr);
			step = NULL;
			rc = ll_get_data(job, LL_JobGetNextStep, &step);
		}

next_job:	job = ll_next_obj(query_object);
	}

	ll_free_objs(job);
	ll_deallocate(job);
	if (++job_inx != obj_count) {
		verbose("ll_get_objs(JOBS), bad obj_count %d", obj_count);
		slurm_free_job_step_info_response_msg(step_info_ptr);
		return SLURM_COMMUNICATIONS_CONNECTION_ERROR;
	}

	step_info_ptr->job_step_count = step_inx + 1;
	*resp = step_info_ptr;
	return SLURM_PROTOCOL_SUCCESS;
#else
	job_step_info_response_msg_t *step_info_ptr;
	verbose("running without loadleveler");
	step_info_ptr = xmalloc(sizeof(node_info_msg_t));
	step_info_ptr->last_update = time(NULL);
	step_info_ptr->job_step_count = 0;
	step_info_ptr->job_steps = xmalloc(0);
	*resp = step_info_ptr;
	return SLURM_PROTOCOL_SUCCESS;
#endif
}

/*
 * slurm_job_step_stat - status a current step
 *
 * IN job_id
 * IN step_id
 * IN node_list, optional, if NULL then all nodes in step are returned.
 * OUT resp
 * RET SLURM_SUCCESS on success SLURM_ERROR else
 *
 * NOTE:
 * pid information is not available (alwasys set to 1)
 * vmem (virtual memory) information is not available
 * page count information is not available
 * task ID information assumes "block" distribution
 */
extern int slurm_job_step_stat(char *job_id, uint32_t step_id,
			       char *node_list,
			       job_step_stat_response_msg_t **resp)
{
#ifdef HAVE_LLAPI_H
	LL_element *query_object, *job, *step;
	bool match_job_id, match_step_id;
	int err_code, obj_count, rc;

	query_object = ll_query(JOBS);
	if (!query_object) {
		verbose("ll_query(JOBS) failed");
		return SLURM_COMMUNICATIONS_CONNECTION_ERROR;
	}

	rc = ll_set_request(query_object, QUERY_ALL, NULL, ALL_DATA);
	if (rc) {
		verbose("ll_set_request(JOBS, ALL), error %d", rc);
		return SLURM_COMMUNICATIONS_CONNECTION_ERROR;
	}

	job = ll_get_objs(query_object, LL_HISTORY_FILE, NULL, &obj_count,
			  &err_code);
	if (!job) {
		verbose("ll_get_objs(JOBS), error %d", err_code);
		return SLURM_COMMUNICATIONS_CONNECTION_ERROR;
	}

	*resp = NULL;
	while (job) {
		step = NULL;
		rc = ll_get_data(job, LL_JobGetFirstStep, &step);
		while (!rc && step) {
			match_job_id = match_step_id = false;
			_test_step_id(step, job_id, step_id,
				      &match_job_id, &match_step_id);
			if (!match_job_id)
				break;
			if (match_step_id) {
				step = NULL;
				rc = ll_get_data(job, LL_JobGetNextStep, &step);
				continue;
			}

			/* Process the match */
			*resp = xmalloc(sizeof(job_step_stat_response_msg_t));
			(*resp)->job_id = xstrdup(job_id);
			(*resp)->step_id = step_id;
			(*resp)->stats_list = list_create(_jobacct_del);
			_proc_step_stat(step, (*resp)->stats_list);
			goto fini;
		}
		job = ll_next_obj(query_object);
	}

fini:	ll_free_objs(job);
	ll_deallocate(job);

	if (match_job_id && match_step_id)
		return SLURM_PROTOCOL_SUCCESS;
	return SLURM_ERROR;
#else
	verbose("running without loadleveler");
	*resp = NULL;
	return SLURM_ERROR;
#endif
}

/*****************************************************************************\
 * Replacement functions for src/api/node_info.c
\*****************************************************************************/

/*
 * slurm_load_node - issue RPC to get slurm all node configuration information
 *	if changed since update_time
 * IN update_time - time of current configuration data
 * IN node_info_msg_pptr - place to store a node configuration pointer
 * IN show_flags - node filtering options
 * RET 0 or a slurm error code
 * NOTE: free the response using slurm_free_node_info_msg
 */
extern int slurm_load_node (time_t update_time,
			    node_info_msg_t **resp, uint16_t show_flags)
{
#ifdef HAVE_LLAPI_H
	node_info_msg_t *node_info_ptr;
	LL_element *query_object, *machine, *resource;
	int cpus, err_code, i, obj_count, rc;
	char *arch, *name, *os, *state;
	char **feature_list;
	int64_t disk, mem;
	int node_inx = -1;
	node_info_t *node_ptr;

	query_object = ll_query(MACHINES);
	if (!query_object) {
		verbose("ll_query(MACHINES) failed");
		return SLURM_COMMUNICATIONS_CONNECTION_ERROR;
	}

	rc = ll_set_request(query_object, QUERY_ALL, NULL, ALL_DATA);
	if (rc) {
		verbose("ll_set_request(MACHINES, ALL), error %d", rc);
		return SLURM_COMMUNICATIONS_CONNECTION_ERROR;
	}

	machine = ll_get_objs(query_object, LL_CM, NULL, &obj_count, &err_code);
	if (!machine) {
		verbose("ll_get_objs(MACHINES), error %d", err_code);
		return SLURM_COMMUNICATIONS_CONNECTION_ERROR;
	}

	node_info_ptr = xmalloc(sizeof(node_info_msg_t));
	node_info_ptr->last_update = time(NULL);
	node_info_ptr->node_scaling = 1;
	node_info_ptr->record_count = obj_count;
	node_info_ptr->node_array = xmalloc(sizeof(node_info_t) * obj_count);

	while (machine) {
		if (++node_inx >= obj_count) {
			/* error handled outside of loop */
			break;
		}
		node_ptr = &node_info_ptr->node_array[node_inx];

		/* The following SLURM fields have no equivalent available
		 * from LoadLeveler:
		 * boot_time, slurmd_start_time
		 * cores, sockets, threads (these might be set from "cpus")
		 * reason, reason_time, reason_uid
		 * select_nodeinfo
		 * weight
		 */

		if (ll_get_data(machine, LL_MachineArchitecture, &arch)) {
			verbose("ll_get_data(LL_MachineArchitecture) failed");
		} else {
			node_ptr->arch = xstrdup(arch);
			free(arch);
		}

		if (ll_get_data(machine, LL_MachineCPUs, &cpus)) {
			verbose("ll_get_data(LL_MachineCPUs) failed");
		} else {
			node_ptr->cpus = cpus;
		}

		if (ll_get_data(machine, LL_MachineDisk64, &disk)) {
			verbose("ll_get_data(LL_MachineDisk64) failed");
		} else {
			node_ptr->tmp_disk = disk * 1024;	/* KB -> MB */
		}

		if (ll_get_data(machine, LL_MachineFeatureList,&feature_list)){
			verbose("ll_get_data(LL_MachineFeatureList) failed");
		} else {
			for (i = 0; feature_list[i]; i++) {
				if (node_ptr->features)
					xstrcat(node_ptr->features, ",");
				xstrcat(node_ptr->features, feature_list[i]);
				free(feature_list[i]);
			}
			free(feature_list);
		}

		if (ll_get_data(machine, LL_MachineName, &name)) {
			verbose("ll_get_data(LL_MachineName) failed");
		} else {
			char *sep;
			node_ptr->node_addr = xstrdup(name);
			node_ptr->node_hostname = xstrdup(name);
			sep = strchr(name, '.');
			if (sep)
				sep[0] = '\0';
			node_ptr->name = xstrdup(name);
			free(name);
		}

		if (ll_get_data(machine, LL_MachineOperatingSystem, &os)) {
			verbose("ll_get_data(LL_MachineOperatingSystem) "
				"failed");
		} else {
			node_ptr->os = xstrdup(os);
			free(os);
		}

		if (ll_get_data(machine, LL_MachineRealMemory64, &mem)) {
			verbose("ll_get_data(LL_MachineRealMemory64) failed");
		} else {
			node_ptr->real_memory = mem;
		}

		if (ll_get_data(machine, LL_MachineStartdState, &state)) {
			verbose("ll_get_data(LL_MachineStartdState) failed");
		} else {
			if (!strcmp(state, "Down") ||
			    !strcmp(state, "None")) {
				node_ptr->node_state = NODE_STATE_DOWN;
			} else if (!strcmp(state, "Drained") ||
				   !strcmp(state, "Flush")) {
				node_ptr->node_state = NODE_STATE_IDLE |
						       NODE_STATE_DRAIN;
			} else if (!strcmp(state, "Draining") ||
				   !strcmp(state, "Suspend")) {
				node_ptr->node_state = NODE_STATE_ALLOCATED |
						       NODE_STATE_DRAIN;
			} else if (!strcmp(state, "Busy") ||
				   !strcmp(state, "Running")) {
				node_ptr->node_state = NODE_STATE_ALLOCATED;
			} else if (!strcmp(state, "Idle")) {
				node_ptr->node_state = NODE_STATE_IDLE;
			} else {
				node_ptr->node_state = NODE_STATE_UNKNOWN;
			}
			free(state);
		}

		resource = NULL;
		rc = ll_get_data(machine, LL_MachineGetFirstResource, &resource);
		while (resource) {
			char *name = NULL, value_str[64];
			int value;
			if (ll_get_data(resource, LL_ResourceName, &name) ||
			    ll_get_data(resource, LL_ResourceInitialValue,
					&value)) {
				verbose("ll_get_data(LL_LL_Resouce*) failed");
			} else {
				if (node_ptr->gres)
					xstrcat(node_ptr->gres, ",");
				xstrcat(node_ptr->gres, name);
				snprintf(value_str, sizeof(value_str), ":%d",
					 value);
				xstrcat(node_ptr->gres, value_str);
			}
			if (name)
				free(name);
			resource = NULL;
			if (ll_get_data(machine, LL_MachineGetNextResource,
					&resource)) {
				verbose("ll_get_data(LL_MachineGetNextResource"
					") failed");
			}
		}

		machine = ll_next_obj(query_object);
	}

	ll_free_objs(machine);
	ll_deallocate(machine);

	if (++node_inx != obj_count) {
		verbose("ll_get_objs(MACHINES), bad obj_count %d", obj_count);
		slurm_free_node_info_msg(node_info_ptr);
		return SLURM_COMMUNICATIONS_CONNECTION_ERROR;
	}

	*resp = node_info_ptr;
	return SLURM_PROTOCOL_SUCCESS;
#else
	node_info_msg_t *node_info_ptr;
	verbose("running without loadleveler");
	node_info_ptr = xmalloc(sizeof(node_info_msg_t));
	node_info_ptr->last_update = time(NULL);
	node_info_ptr->node_scaling = 1;
	node_info_ptr->record_count = 0;
	node_info_ptr->node_array = xmalloc(0);
	*resp = node_info_ptr;
	return SLURM_PROTOCOL_SUCCESS;
#endif
}

/*****************************************************************************\
 * Replacement functions for src/api/partition_info.c
\*****************************************************************************/

/*
 * slurm_load_partitions - issue RPC to get slurm all partition configuration
 *	information if changed since update_time
 * IN update_time - time of current configuration data
 * IN node_info_msg_pptr - place to store a node configuration pointer
 * IN show_flags - node filtering options
 * RET 0 or a slurm error code
 * NOTE: free the response using slurm_free_node_info_msg
 */
extern int slurm_load_partitions (time_t update_time,
				  partition_info_msg_t **resp,
				  uint16_t show_flags)
{
#ifdef HAVE_LLAPI_H
	partition_info_msg_t *part_info_ptr;
	partition_info_t *part_ptr;
	int obj_count, part_inx = -1;
	LL_element *query_object, *class;
	int err_code, i, rc;
	int priority;
	int64_t time_limit;
	char **groups, *name, *sep;


	_load_global_node_list();

	query_object = ll_query(CLASSES);
	if (!query_object) {
		verbose("ll_query(CLASSES) failed");
		return SLURM_COMMUNICATIONS_CONNECTION_ERROR;
	}

	rc = ll_set_request(query_object, QUERY_ALL, NULL, ALL_DATA);
	if (rc) {
		verbose("ll_set_request(CLASSES, ALL), error %d", rc);
		return SLURM_COMMUNICATIONS_CONNECTION_ERROR;
	}

	class = ll_get_objs(query_object, LL_CM, NULL, &obj_count, &err_code);
	if (!class) {
		verbose("ll_get_objs(CLASSES), error %d", err_code);
		return SLURM_COMMUNICATIONS_CONNECTION_ERROR;
	}

	part_info_ptr = xmalloc(sizeof(node_info_msg_t));
	part_info_ptr->last_update = time(NULL);
	part_info_ptr->record_count = obj_count;
	part_info_ptr->partition_array = xmalloc(sizeof(partition_info_t) *
						 obj_count);

	while (class) {
		if (++part_inx >= obj_count) {
			/* error handled outside of loop */
			break;
		}
		part_ptr = &part_info_ptr->partition_array[part_inx];

		/* The following SLURM fields have no equivalent available
		 * from LoadLeveler:
		 * allow_alloc_nodes, alternate, grace_time
		 * def_mem_per_cpu, max_mem_per_cpu
		 * max_nodes (this might be derived from LL_ClassMaxProcessors)
		 */

		rc = ll_get_data(class, LL_ClassDefWallClockLimitHard,
				 &time_limit);
		if (!rc) {
			if (time_limit == 0x7fffffff)
				part_ptr->default_time = INFINITE;
			else
				part_ptr->default_time = time_limit;
		}

		rc = ll_get_data(class, LL_ClassWallClockLimitHard,
				 &time_limit);
		if (!rc) {
			if (time_limit == 0x7fffffff)
				part_ptr->max_time = INFINITE;
			else
				part_ptr->max_time = time_limit;
		}

		rc = ll_get_data(class, LL_ClassIncludeGroups, &groups);
		if (!rc) {
			for (i = 0; groups[i]; i++) {
				if (part_ptr->allow_groups)
					sep = ",";
				else
					sep = "";
				xstrfmtcat(part_ptr->allow_groups, "%s%s",
					   sep, groups[i]);
				free(groups[i]);
			}
			free(groups);
		}

		rc = ll_get_data(class, LL_ClassName, &name);
		if (!rc) {
			part_ptr->name = xstrdup(name);
			free(name);
		}

		rc = ll_get_data(class, LL_ClassPriority, &priority);
		if (!rc)
			part_ptr->priority = priority;

		/* Set some default values for other fields */
		part_ptr->flags = 0;
		part_ptr->max_share = 1;
		part_ptr->min_nodes = 1;
		part_ptr->max_nodes = INFINITE;
		part_ptr->nodes = xstrdup(global_node_str);
		part_ptr->node_inx = xmalloc(sizeof(int) * 3);
		part_ptr->node_inx[0] = 0;
		part_ptr->node_inx[1] = global_node_cnt - 1;
		part_ptr->node_inx[2] = -1;
		part_ptr->total_cpus = global_cpu_cnt;
		part_ptr->total_nodes = global_node_cnt;
		part_ptr->preempt_mode = PREEMPT_MODE_SUSPEND; /* LL default */
		part_ptr->state_up = PARTITION_UP;

		class = ll_next_obj(query_object);
	}

	ll_free_objs(class);
	ll_deallocate(class);

	if (++part_inx != obj_count) {
		verbose("ll_get_objs(CLASSES), bad obj_count %d", obj_count);
		slurm_free_partition_info_msg(part_info_ptr);
		return SLURM_COMMUNICATIONS_CONNECTION_ERROR;
	}

	*resp = part_info_ptr;
	return SLURM_PROTOCOL_SUCCESS;
#else
	partition_info_msg_t *part_info_ptr;
	verbose("running without loadleveler");
	part_info_ptr = xmalloc(sizeof(node_info_msg_t));
	part_info_ptr->last_update = time(NULL);
	part_info_ptr->record_count = 0;
	part_info_ptr->partition_array = xmalloc(0);
	*resp = part_info_ptr;
	return SLURM_PROTOCOL_SUCCESS;
#endif
}

/*****************************************************************************\
 * Replacement functions for src/api/copmplete.c
\*****************************************************************************/
/*
 * slurm_complete_job - note the completion of a job allocation
 * IN job_id - the job's id
 * IN job_return_code - the highest exit code of any task of the job
 * RET 0 on success, otherwise return -1 and set errno to indicate the error
 */
extern int slurm_complete_job (char *job_id, uint32_t job_return_code)
{
	return slurm_terminate_job(job_id);
}

/*****************************************************************************\
 * Replacement functions for src/api/signal.c
\*****************************************************************************/
extern int slurm_notify_job (char *job_id, char *message)
{
	slurm_seterrno(ESLURM_NOT_SUPPORTED);
	return -1;
}

/*
 * slurm_signal_job - send the specified signal to all steps of an existing job
 * IN job_id     - the job's id
 * IN signal     - signal number
 * RET 0 on success, otherwise return -1 and set errno to indicate the error
 */
extern int slurm_signal_job (char *job_id, uint16_t signal)
{
	if (signal != SIGKILL) {
		slurm_seterrno(ESLURM_NOT_SUPPORTED);
		return -1;
	}
	return slurm_terminate_job (job_id);
}

/*
 * slurm_signal_job_step - send the specified signal to an existing job step
 * IN job_id  - the job's id
 * IN step_id - the job step's id - use SLURM_BATCH_SCRIPT as the step_id
 *              to send a signal to a job's batch script
 * IN signal  - signal number
 * RET 0 on success, otherwise return -1 and set errno to indicate the error
 */
extern int slurm_signal_job_step (char *job_id, uint32_t step_id,
				  uint16_t signal)
{
	if (signal != SIGKILL) {
		slurm_seterrno(ESLURM_NOT_SUPPORTED);
		return -1;
	}
	return slurm_terminate_job_step (job_id, step_id);
}

/*
 * slurm_terminate_job - terminates all steps of an existing job by sending
 *	a REQUEST_TERMINATE_JOB rpc to all slurmd in the job allocation.
 *	NOTE: No slurmd on LoadLeveler installations.
 * IN job_id     - the job's id
 * RET 0 on success, otherwise return -1 and set errno to indicate the error
 */
extern int slurm_terminate_job (char *job_id)
{
#ifndef HAVE_LLAPI_H
	if (fe_job_id && !strcmp(fe_job_id, job_id))
		fe_job_killed = true;
	(void) _xmit_abort();
	slurm_seterrno(ESLURM_NOT_SUPPORTED);
	return -1;
#else
	LL_element *job, *step, *query_object;
	int err_code, found, i, obj_count, rc = 0;
	bool match_job_id, match_step_id;
	char *step_id_str;
	uint32_t step_id;

	/* Make up to 4 passes through job list to capture job steps started
	 * while we are scanning and terminating job steps */
	for (i = 0; i < 4; i++) {
		found = 0;
		/* Scan list of job steps for match */
		query_object = ll_query(JOBS);
		if (!query_object) {
			verbose("ll_query(JOBS) failed");
			slurm_seterrno(SLURM_COMMUNICATIONS_CONNECTION_ERROR);
			return -1;
		}

		rc = ll_set_request(query_object, QUERY_ALL, NULL, ALL_DATA);
		if (rc) {
			verbose("ll_set_request(JOBS, ALL), error %d", rc);
			slurm_seterrno(SLURM_COMMUNICATIONS_CONNECTION_ERROR);
			return -1;
		}

		job = ll_get_objs(query_object, LL_CM, NULL, &obj_count,
				  &err_code);
		if (!job) {
			verbose("ll_get_objs(JOBS), error %d", err_code);
			slurm_seterrno(SLURM_COMMUNICATIONS_CONNECTION_ERROR);
			return -1;
		}
		while (job) {
			step = NULL;
			match_job_id = match_step_id = false;
			rc = ll_get_data(job, LL_JobGetFirstStep, &step);
			while (!rc && step) {
				_test_step_id(step, job_id, NO_VAL,
					      &match_job_id, &match_step_id);
				if (!match_job_id)
					break;
				step_id_str = NULL;
				rc = ll_get_data(step, LL_StepID, &step_id_str);
				if (rc || (step_id_str == NULL)) {
					verbose("ll_get_data(StepID), error %d",
						err_code);
					rc = -1;
				} else {
					char *sep = strrchr(step_id_str, '.');
					if (sep && (sep != step_id_str)) {
						step_id = strtol(sep+1, NULL,
								 10);
						if (slurm_terminate_job_step
								(job_id,
								 step_id)) {
							rc = -1;
						} else {
							found++;
						}
					}
				}
				step = NULL;
				rc = ll_get_data(job, LL_JobGetNextStep, &step);
			}

			job = ll_next_obj(query_object);
		}
		if (found == 0)
			break;
	}
	return rc;
#endif
}

/*
 * slurm_terminate_job_step - terminates a job step by sending a
 * 	REQUEST_TERMINATE_TASKS rpc to all slurmd of a job step.
 *	NOTE: No slurmd on LoadLeveler installations.
 * IN job_id  - the job's id
 * IN step_id - the job step's id - use SLURM_BATCH_SCRIPT as the step_id
 *              to terminate a job's batch script
 * RET 0 on success, otherwise return -1 and set errno to indicate the error
 */
extern int slurm_terminate_job_step (char *job_id, uint32_t step_id)
{
#ifdef HAVE_LLAPI_H
	int rc;
	LL_terminate_job_info *cancel_info;

	if (!job_id) {
		slurm_seterrno(ESLURM_INVALID_JOB_ID);
		return -1;
	}

/* FIXME: documentation not clear, probably needs work */
/* FIXME: we may need to fully expand job id with full hostname too */
	cancel_info = xmalloc(sizeof(LL_terminate_job_info));
	cancel_info->version_num =  LL_PROC_VERSION;
#if 0
	cancel_info->StepId.cluster =
		jobs_info.JobList[0]->step_list[0]->id.cluster;
	cancel_info->StepId.proc =
		jobs_info.JobList[0]->step_list[0]->id.proc;
	cancel_info->StepId.from_host =
		strdup(jobs_info.JobList[0]->step_list[0]->id.from_host);
#endif
	cancel_info->msg =  "Manually terminated";
	rc = ll_terminate_job(cancel_info);
	xfree(cancel_info);

	if (rc == API_OK)
		return 0;
	if (rc == -7)
		slurm_seterrno(ESLURM_ACCESS_DENIED);
	else
		slurm_seterrno(SLURM_ERROR);
	return -1;
#else
	slurm_seterrno(ESLURM_NOT_SUPPORTED);
	return -1;
#endif
}

/*****************************************************************************\
 * Replacement functions for src/api/signal.c
\*****************************************************************************/
/*
 * Translate a SLURM stdin/out/err filename to the LoadLeveler equivalent
 * slurm_fname IN - SLURM filename
 * RET: LoadLeveler equivalent file name, must xfree()
 */
static char *_massage_fname(char *slurm_fname)
{
	char *match, *tmp_fname = NULL, *work_fname;

	work_fname = xstrdup(slurm_fname);

	match = strstr(work_fname, "%j");
	if (match) {
		match[0] = '\0';
		xstrfmtcat(tmp_fname, "%s$(jobid)%s", work_fname, match + 2);
		xfree(work_fname);
		work_fname = tmp_fname;
		tmp_fname = NULL;
	}

	match = strstr(work_fname, "%J");
	if (match) {
		match[0] = '\0';
		xstrfmtcat(tmp_fname, "%s$(jobid).$(stepid)%s", work_fname,
			   match + 2);
		xfree(work_fname);
		work_fname = tmp_fname;
		tmp_fname = NULL;
	}

	match = strstr(work_fname, "%N");
	if (match) {
		match[0] = '\0';
		xstrfmtcat(tmp_fname, "%s$(schedd_host)%s", work_fname,
			   match + 2);
		xfree(work_fname);
		work_fname = tmp_fname;
		tmp_fname = NULL;
	}

	match = strstr(work_fname, "%s");
	if (match) {
		match[0] = '\0';
		xstrfmtcat(tmp_fname, "%s$(stepid)%s", work_fname, match + 2);
		xfree(work_fname);
		work_fname = tmp_fname;
		tmp_fname = NULL;
	}

	return work_fname;
}

/* NOTE: Important difference for LoadLeveler:
 * if req->spank_job_env_size >= 1 then
 *	spank_job_env[0] contains monitor_program for llsubmit()
 * if req->spank_job_env_size >= 2 then
 *	spank_job_env[1] contains monitor_arg for llsubmit()
 */
extern int slurm_submit_batch_job(job_desc_msg_t *req,
				  submit_response_msg_t **resp)
{
	char *first_line, *pathname, *slurm_cmd_file = NULL;
	int fd, i, rc = 0;
	size_t len, offset = 0, wrote;

	/* Move first line of script, e.g. "#!/bin/bash"
	 * also all subsequent lines that begin with "# @" */
	first_line = strchr(req->script, '\n');
	while (first_line && (first_line[1] == '#') &&
	       (first_line[2] == ' ') && (first_line[3] == '@')) {
		first_line = strchr(first_line+1, '\n');
	}
	if (first_line) {
		first_line[0] = '\0';
		xstrfmtcat(slurm_cmd_file, "%s\n", req->script);
		first_line[0] = '\n';
	} else
		xstrfmtcat(slurm_cmd_file, "%s\n", req->script);

	/* The following options are not supported with LoadLevler:
	 * uint16_t acctg_freq;		accounting polling interval (seconds)
	 * char *alloc_node;		node making resource allocation request
	 * uint16_t alloc_resp_port;	port to send allocation confirmation to
	 * uint32_t alloc_sid;		local sid making allocation request
	 * uint16_t contiguous;		set if job requires contiguous nodes
	 * uint16_t cores_per_socket;	cores per socket required by job
	 * char *dependency;		synchronize this job with other jobs
	 * time_t end_time;		time by which job must complete
	 * char *exc_nodes;		nodes excluded from allocation request
	 * uint32_t group_id;		group to assume, if run as root.
	 * uint16_t immediate;		allocate to run or fail immediately
	 * uint32_t job_id;		user-specified job ID
	 * uint16_t kill_on_node_fail;	set if node failure to kill job
	 * char *licenses;		licenses required by the job
	 * uint32_t max_cpus;		maximum number of processors required
	 * uint32_t min_cpus;		minimum number of processors required
	 * uint16_t ntasks_per_core;	number of tasks to invoke per core
	 * uint16_t ntasks_per_socket;	number of tasks to invoke per socket
	 * uint8_t open_mode;		stdout/err open mode truncate or append
	 * uint16_t other_port;		port to send notification msg to
	 * uint8_t overcommit;		over subscribe resources
	 * uint32_t priority;		only used for hold if priority==0
	 *				SLURM nice mapped to LL priority
	 * uint16_t pn_min_cpus;	minimum # CPUs per node
	 * char *qos;			Quality of Service
	 * uint16_t sockets_per_node;	sockets per node required by job
	 * char **spank_job_env;	env vars for prolog/epilog from SPANK
	 * uint32_t spank_job_env_size; element count in spank_env
	 * uint32_t time_min;		 minimum run time in minutes
	 * uint32_t user_id;		set only if different from current UID
	 * uint32_t wait4switch;	Maximum time to wait for switch count
	 * uint16_t wait_all_nodes;	set to wait for all nodes booted
	 * Nuint16_t warn_signal;	signal to send when near time limit
	 * uint16_t warn_time;		time before time limit to send signal
	 * char *wckey;			wckey for job
	 */

	if (req->account) {
		xstrfmtcat(slurm_cmd_file, "# @ account_no = %s\n",
			   req->account);
	}

	if (req->argc) {
		xstrfmtcat(slurm_cmd_file, "# @ arguments =");
		for (i = 0; i < req->argc; i++)
			xstrfmtcat(slurm_cmd_file, " %s", req->argv[i]);
		xstrfmtcat(slurm_cmd_file, "\n");
	}

	if (!req->num_tasks)
		;
	else if (req->plane_size != (uint16_t) NO_VAL) {
		xstrfmtcat(slurm_cmd_file, "# @ blocking = %u\n",
			   req->plane_size);
	} else if ((req->task_dist != (uint16_t) NO_VAL) &&
		   ((req->task_dist == SLURM_DIST_CYCLIC) ||
		    (req->task_dist == SLURM_DIST_CYCLIC_BLOCK) ||
		    (req->task_dist == SLURM_DIST_CYCLIC_CYCLIC))) {
		xstrfmtcat(slurm_cmd_file, "# @ blocking = 1\n");
	} else if ((req->task_dist != (uint16_t) NO_VAL) &&
		   ((req->task_dist == SLURM_DIST_BLOCK) ||
		    (req->task_dist == SLURM_DIST_BLOCK_BLOCK) ||
		    (req->task_dist == SLURM_DIST_BLOCK_CYCLIC))) {
		xstrfmtcat(slurm_cmd_file, "# @ blocking = unlimited\n");
	}

	if (req->ckpt_dir && req->ckpt_interval) {
		/* LoadLeveler checkpoint is only available with AIX */
		xstrfmtcat(slurm_cmd_file, "# @ ckpt_dir = %s\n",
			   req->ckpt_dir);
		/* System administrator must configure LoadLeveler with
		 * MIN_CKPT_INTERVAL and MAX_CKPT_INTERVAL */
		xstrfmtcat(slurm_cmd_file, "# @ checkpoint = interval\n");
	}

	if (req->partition)
		xstrfmtcat(slurm_cmd_file, "# @ class = %s\n", req->partition);

	if (req->comment)
		xstrfmtcat(slurm_cmd_file, "# @ comment = %s\n", req->comment);

	if (req->threads_per_core != (uint16_t) NO_VAL) {
		xstrfmtcat(slurm_cmd_file, "# @ cpus_per_core = %u\n",
			   req->threads_per_core);
	}

	/* Copy entire environment by default for SLURM */
	xstrfmtcat(slurm_cmd_file, "# @ env_copy = all\n");
	xstrfmtcat(slurm_cmd_file, "# @ environment = COPY_ALL\n");

	if (req->std_err) {
		char *fname = _massage_fname(req->std_err);
		xstrfmtcat(slurm_cmd_file, "# @ error = %s\n", fname);
		xfree(fname);
	} else {
/* FIXME: Will both stdout/err go to same file?, default stderr to /dev/null */
		xstrfmtcat(slurm_cmd_file, "# @ error = slurm.out.$(jobid)\n");
	}

#if 0
	/* SLURM's Linux group different from a LoadLeveler group and only
	 * user root can set the job's group ID */
	if (req->group_id != NO_VAL) {
		char *g_name;
		if ((g_name = gid_to_string((gid_t) req->group_id))) {
			xstrfmtcat(slurm_cmd_file, "# @ group = %s\n", g_name);
			xfree(g_name);
		}
	}
#endif

	if (req->work_dir) {
		xstrfmtcat(slurm_cmd_file, "# @ initialdir = %s\n",
			   req->work_dir);
	}

	if (req->std_in) {
		char *fname = _massage_fname(req->std_in);
		xstrfmtcat(slurm_cmd_file, "# @ input = %s\n", fname);
		xfree(fname);
	}

	if (req->priority == 0)
		xstrfmtcat(slurm_cmd_file, "# @ hold = user\n");

	if (req->name)
		xstrfmtcat(slurm_cmd_file, "# @ job_name = %s\n", req->name);

	xstrfmtcat(slurm_cmd_file, "# @ job_type = serial\n");

	if (req->reservation) {
		xstrfmtcat(slurm_cmd_file, "# @ ll_res_id = %s\n",
			   req->reservation);
	}

	if (req->mem_bind_type  == (uint16_t) NO_VAL)
		;
	else if (req->mem_bind_type &
	         (MEM_BIND_RANK | MEM_BIND_MAP |
		  MEM_BIND_MASK | MEM_BIND_LOCAL)) {
		xstrfmtcat(slurm_cmd_file,
			   "# @ mcm_affinity_options = mcm_mem_req\n");
	}

	if (req->network)
		xstrfmtcat(slurm_cmd_file, "# @ network = %s\n", req->network);

	if ((req->min_nodes != NO_VAL) && (req->max_nodes != NO_VAL)) {
		xstrfmtcat(slurm_cmd_file, "# @ node = %u,%u\n",
			  req->min_nodes, req->max_nodes);
	} else if (req->min_nodes != NO_VAL) {
		xstrfmtcat(slurm_cmd_file, "# @ node = %u\n", req->min_nodes);
	} else if (req->max_nodes != NO_VAL) {
		xstrfmtcat(slurm_cmd_file, "# @ node = ,%u\n", req->max_nodes);
	}

	if (req->gres) {
		bool gres_cnt = 0;
		char *gres, *save_ptr = NULL, *sep, *tok;
		int cnt;
		gres = xstrdup(req->gres);
		tok = strtok_r(gres, ",", &save_ptr);
		while (tok) {
			if (gres_cnt++ == 0) {
				xstrfmtcat(slurm_cmd_file,
					   "# @ node_resources =");
			}
			sep = strchr(tok, '*');
			if (sep) {
				sep[0] = '\0';
				cnt = atoi(sep + 1);
			} else {
				cnt = 1;
			}
			xstrfmtcat(slurm_cmd_file, " %s(%d)", tok, cnt);
			tok = strtok_r(NULL, ",", &save_ptr);
		}
		if (gres_cnt)
			xstrfmtcat(slurm_cmd_file, "\n");
		xfree(gres);
	}

	if (req->shared == 0)
		xstrfmtcat(slurm_cmd_file, "# @ node_usage = not_shared\n");
	else if (req->shared != (uint16_t) NO_VAL)
		xstrfmtcat(slurm_cmd_file, "# @ node_usage = shared\n");

	if (1) {
		int notify_cnt = 0;
		char *notify_str = NULL;
		if (req->mail_type & MAIL_JOB_BEGIN) {
			notify_str = "start";
			notify_cnt++;
		}
		if (req->mail_type & MAIL_JOB_END) {
			notify_str = "complete";
			notify_cnt++;
		}
		if (req->mail_type & MAIL_JOB_FAIL) {
			notify_str = "error";
			notify_cnt++;
		}
		if (req->mail_type & MAIL_JOB_REQUEUE) {
			notify_str = "complete";
			notify_cnt++;
		}
		if (notify_cnt == 0)
			notify_str = "never";
		else if (notify_cnt > 1)
			notify_str = "always";
		xstrfmtcat(slurm_cmd_file, "# @ notification = %s\n",
			   notify_str);
	}

	if (req->mail_user) {
		xstrfmtcat(slurm_cmd_file, "# @ notify_user = %s\n",
			   req->mail_user);
	}

	if (req->std_out) {
		char *fname = _massage_fname(req->std_out);
		xstrfmtcat(slurm_cmd_file, "# @ output = %s\n", fname);
		xfree(fname);
	} else {
		xstrfmtcat(slurm_cmd_file, "# @ output = slurm.out.$(jobid)\n");
	}

	if (req->features || req->req_nodes ||
	    ((req->pn_min_memory != NO_VAL) &&
	     ((req->pn_min_memory & MEM_PER_CPU) == 0)) ||
	    (req->pn_min_tmp_disk != NO_VAL)) {
		char *name = NULL, *join = "";
		hostlist_t hl = NULL;
		xstrfmtcat(slurm_cmd_file, "# @ requirements =");

		if (req->pn_min_tmp_disk != NO_VAL) {
			xstrfmtcat(slurm_cmd_file, "%s(Disk == %u)",
				   join, req->pn_min_tmp_disk);
			join = " && ";
		}
		if (req->features) {
			xstrfmtcat(slurm_cmd_file, "%s(Feature == %s)",
				   join, req->features);
			join = " && ";
		}
		if (req->req_nodes) {
			hl = hostlist_create(req->req_nodes);
			if (hl == NULL) {
				error("invalid required host list: %s",
				      req->req_nodes);
			}
		}
		if (req->req_nodes && hl) {
			xstrfmtcat(slurm_cmd_file, "%s(Machine == {", join);
			while ((name = hostlist_pop(hl))) {
				xstrfmtcat(slurm_cmd_file, " \"%s\" ", name);
				free(name);
			}
			hostlist_destroy(hl);
			xstrfmtcat(slurm_cmd_file, "}\n");
			join = " && ";
		}
		if ((req->pn_min_memory != NO_VAL) &&
		    ((req->pn_min_memory & MEM_PER_CPU) == 0)) {
			/* This might also be accomplished on a per _task_
			 * basis using "Resources=ConsumbleMemory(size)" */
			xstrfmtcat(slurm_cmd_file, "%s(TotalMemory == %u)",
				   join, req->pn_min_memory);
			join = " && ";
		}
	}

	if (req->requeue == 0)
		xstrfmtcat(slurm_cmd_file, "# @ restart = no\n");
	else if (req->requeue != (uint16_t) NO_VAL)
		xstrfmtcat(slurm_cmd_file, "# @ restart = yes\n");

	if (req->begin_time) {
		struct tm *begin_tm = localtime(&req->begin_time);
		xstrfmtcat(slurm_cmd_file,
			   "# @ startdate = %2d/%2d/%4d %2d:%2d:%2d\n",
			   begin_tm->tm_mon + 1, begin_tm->tm_mday,
			   begin_tm->tm_year + 1900, begin_tm->tm_hour,
			   begin_tm->tm_min, begin_tm->tm_sec);
	}

	if (req->cpus_per_task != (uint16_t) NO_VAL) {
		xstrfmtcat(slurm_cmd_file, "# @ task_affinity = cpu(%u)\n",
			   req->cpus_per_task);
	} else if (req->cpu_bind_type == (uint16_t) NO_VAL)
		;
	else if (req->cpu_bind_type & CPU_BIND_TO_CORES)
		xstrfmtcat(slurm_cmd_file, "# @ task_affinity = core\n");
	else if (req->cpu_bind_type & CPU_BIND_TO_THREADS)
		xstrfmtcat(slurm_cmd_file, "# @ task_affinity = cpu\n");

	if (req->ntasks_per_node != (uint16_t) NO_VAL) {
		xstrfmtcat(slurm_cmd_file, "# @ tasks_per_node = %u\n",
			   req->ntasks_per_node);
	}

	if (req->num_tasks != NO_VAL) {
		xstrfmtcat(slurm_cmd_file, "# @ total_tasks = %u\n",
			   req->num_tasks);
	}

	if (req->nice != (uint16_t) NO_VAL) {
		int prio = 50 + (req->nice - NICE_OFFSET);
		prio = MAX(prio, 0);
		prio = MIN(prio, 100);
		xstrfmtcat(slurm_cmd_file, "# @ user_priority = %d\n", prio);
	}

	if (req->time_limit != NO_VAL) {
		xstrfmtcat(slurm_cmd_file, "# @ wall_clock_limit = %u\n",
			   req->time_limit);
	}

	/* Copy all limits from current environment */
	xstrfmtcat(slurm_cmd_file, "# @ as_limit      = copy\n");
	xstrfmtcat(slurm_cmd_file, "# @ core_limit    = copy\n");
	xstrfmtcat(slurm_cmd_file, "# @ cpu_limit     = copy\n");
	xstrfmtcat(slurm_cmd_file, "# @ data_limit    = copy\n");
	xstrfmtcat(slurm_cmd_file, "# @ file_limit    = copy\n");
	xstrfmtcat(slurm_cmd_file, "# @ job_cpu_limit = copy\n");
	xstrfmtcat(slurm_cmd_file, "# @ locks_limit   = copy\n");
	xstrfmtcat(slurm_cmd_file, "# @ memlock_limit = copy\n");
	xstrfmtcat(slurm_cmd_file, "# @ nofile_limit  = copy\n");
	xstrfmtcat(slurm_cmd_file, "# @ nproc_limit   = copy\n");
	xstrfmtcat(slurm_cmd_file, "# @ stack_limit   = copy\n");

	xstrfmtcat(slurm_cmd_file, "# @ queue\n");

	/* Move the reset of the script */
	if (first_line)
		xstrfmtcat(slurm_cmd_file, "%s\n", first_line+1);

	pathname = xmalloc(PATH_MAX);
	if (!getcwd(pathname, PATH_MAX))
		fatal("getcwd: %m");
	xstrfmtcat(pathname, "/slurm.script.%u", (uint32_t)getpid());
	while (1) {
		fd = creat(pathname, 0700);
		if (fd >= 0)
			break;
		if (errno == EINTR)
			continue;
		fatal("creat(%s): %m", pathname);
	}
	len = strlen(slurm_cmd_file) + 1;
	while (offset < len) {
		wrote = write(fd, slurm_cmd_file + offset, len - offset);
		if (wrote < 0) {
			if ((errno == EAGAIN) || (errno == EINTR))
				continue;
			fatal("write(%s): %m", pathname);
		}
		offset += wrote;
	}

#ifdef HAVE_LLAPI_H
{
	LL_job *job_info = NULL;
	submit_response_msg_t *resp_ptr;
	char *monitor_program = NULL, *monitor_arg = NULL;

	if (req->spank_job_env_size >= 1)
		monitor_program = req->spank_job_env[0];
	if (req->spank_job_env_size >= 2)
		monitor_arg = req->spank_job_env[1];
	rc = llsubmit(pathname, monitor_program, monitor_arg, job_info,
		      LL_JOB_VERSION);
	if (rc == 0) {
		resp_ptr = xmalloc(sizeof(submit_response_msg_t));
		*resp = resp_ptr;
		if (job_info->steps > 0) {
			LL_job_step *step_ptr = job_info->step_list[0];
			/* Reduce to short hostname.<jobid> */
			xstrfmtcat(resp_ptr->job_id, "%s.%d",
				   step_ptr->id.from_host, step_ptr->id.proc);
		}
		resp_ptr->error_code = SLURM_SUCCESS;
		llfree_job_info(job_info, LL_JOB_VERSION);
	} else {
		*resp = NULL;
		slurm_seterrno(SLURM_ERROR);
	}
}
#else
	info("script:\n%s", slurm_cmd_file);
#if 0
	rc = -1;
	slurm_seterrno(ESLURM_NOT_SUPPORTED);
#else
{
	submit_response_msg_t *resp_ptr;
	resp_ptr = xmalloc(sizeof(submit_response_msg_t));
	*resp = resp_ptr;
	resp_ptr->job_id = xstrdup("jette.123");
}
#endif
#endif
	if (unlink(pathname))
		error("unlink(%s): %m", pathname);
	xfree(pathname);
	xfree(slurm_cmd_file);

	return rc;
}

/*****************************************************************************\
 * Replacement functions for src/api/step_ctx.c
\*****************************************************************************/
extern slurm_step_ctx_t *slurm_step_ctx_create (
		const slurm_step_ctx_params_t *step_params)
{ return NULL; }

extern slurm_step_ctx_t *slurm_step_ctx_create_no_alloc (
		const slurm_step_ctx_params_t *step_params, uint32_t step_id)
{ return NULL; }

extern int slurm_step_ctx_destroy (slurm_step_ctx_t *ctx)
{ return SLURM_ERROR; }

extern int slurm_step_ctx_get (slurm_step_ctx_t *ctx, int ctx_key, ...)
{ return SLURM_ERROR; }

extern void slurm_step_ctx_params_t_init (slurm_step_ctx_params_t *ptr)
{}

/*****************************************************************************\
 * Replacement functions for src/api/step_launch.c
\*****************************************************************************/
extern void record_ppid(void)
{}

extern int slurm_step_launch(slurm_step_ctx_t *ctx,
			     const slurm_step_launch_params_t *params,
			     const slurm_step_launch_callbacks_t *callbacks)
{ return SLURM_ERROR; }

extern void slurm_step_launch_abort(slurm_step_ctx_t *ctx)
{}

extern void slurm_step_launch_fwd_signal(slurm_step_ctx_t *ctx, int signo)
{}

extern int slurm_step_launch_wait_start(slurm_step_ctx_t *ctx)
{ return SLURM_ERROR; }

extern void slurm_step_launch_wait_finish(slurm_step_ctx_t *ctx)
{}

extern void slurm_step_launch_params_t_init (slurm_step_launch_params_t *ptr)
{}

/*****************************************************************************\
 * Replacement functions for src/api/allocate.c
\*****************************************************************************/

/*
 * slurm_allocate_resources_blocking
 *	allocate resources for a job request.  This call will block until
 *	the allocation is granted, or the specified timeout limit is reached.
 * IN req - description of resource allocation request
 * IN timeout - amount of time, in seconds, to wait for a response before
 * 	giving up.
 *	A timeout of zero will wait indefinitely.
 * IN pending_callback - If the allocation cannot be granted immediately,
 *      the controller will put the job in the PENDING state.  If
 *      pending callback is not NULL, it will be called with the job_id
 *      of the pending job as the sole parameter.
 *
 * RET allocation structure on success, NULL on error set errno to
 *	indicate the error (errno will be ETIMEDOUT if the timeout is reached
 *      with no allocation granted)
 * NOTE: free the response using slurm_free_resource_allocation_response_msg()
 */
extern resource_allocation_response_msg_t *
slurm_allocate_resources_blocking (job_desc_msg_t *user_req,
				   time_t timeout,
				   void(*pending_callback)(char *job_id))
{
	submit_response_msg_t *submit_resp;
	struct timeval tv;
	fd_set except_fds, read_fds;
	int i, n_fds;
	resource_allocation_response_msg_t *alloc_resp = NULL;

	if (fe_comm_socket < 0) {
		fatal("slurm_allocate_resources_blocking called without "
		      "establishing communications socket");
	}
	if (!user_req->script) {
		fatal("slurm_allocate_resources_blocking called without "
		      "script");
	}
	if (slurm_submit_batch_job(user_req, &submit_resp) != SLURM_SUCCESS)
		return NULL;
	xfree(fe_job_id);
	fe_job_id = xstrdup(submit_resp->job_id);
	if (pending_callback)
		pending_callback(fe_job_id);

	n_fds = fe_comm_socket;
	while (fe_comm_socket >= 0) {
		FD_ZERO(&except_fds);
		FD_SET(fe_comm_socket, &except_fds);
		FD_ZERO(&read_fds);
		FD_SET(fe_comm_socket, &read_fds);

		tv.tv_sec = 30;
		tv.tv_usec = 0;
		i = select((n_fds + 1), &read_fds, NULL, &except_fds, &tv);
		if ((i == 0) ||
		    ((i == -1) && (errno == EINTR))) {
			/* Test for abnormal job termination on timeout or
			 * many signals */
			if (_fe_test_job_fini(fe_job_id)) {
				slurm_shutdown_msg_engine(fe_comm_socket);
				fe_comm_socket = -1;
				break;
			}
			continue;
		} else if (i == -1) {
			error("select: %m");
			break;
		} else {	/* i > 0, ready for I/O */
			if (_fe_proc_connect(fe_comm_socket)) {
				slurm_shutdown_msg_engine(fe_comm_socket);
				fe_comm_socket = -1;
				slurm_allocation_lookup_lite(fe_job_id,
							     &alloc_resp);
				break;
			}
			continue;
		}
	}

	return alloc_resp;
}

/*
 * slurm_allocation_lookup - retrieve info for an existing resource allocation
 * IN jobid - job allocation identifier
 * OUT info - job allocation information
 * RET 0 on success, otherwise return -1 and set errno to indicate the error
 * NOTE: free the "resp" using slurm_free_resource_allocation_response_msg
 */
extern int
slurm_allocation_lookup(char *jobid,
			job_alloc_info_response_msg_t **info)
{
/* FIXME: Need to add code */
	return SLURM_ERROR;
}

/*
 * slurm_allocation_lookup_lite - retrieve info for an existing resource
 *                                allocation without the addrs and such
 * IN jobid - job allocation identifier
 * OUT info - job allocation information
 * RET 0 on success, otherwise return -1 and set errno to indicate the error
 * NOTE: free the response using slurm_free_resource_allocation_response_msg()
 */
extern int
slurm_allocation_lookup_lite(char *jobid,
			     resource_allocation_response_msg_t **info)
{
	int i, rc;
	job_info_t *job_ptr;
	job_info_msg_t *job_info_msg;
	resource_allocation_response_msg_t *alloc_resp;

	rc = slurm_load_job(&job_info_msg, jobid, SHOW_ALL);
	if (rc != SLURM_SUCCESS)
		return rc;

	for (i = 0; i < job_info_msg->record_count; i++) {
		if (!strcmp(job_info_msg->job_array[i].job_id, jobid))
			break;
	}
	job_ptr = &job_info_msg->job_array[i];
	if (i >= job_info_msg->record_count) {
		/* could not find this job */
#ifdef HAVE_LLAPI_H
		slurm_seterrno(ESLURM_INVALID_JOB_ID);
		rc = -1;
#else
		/* Simulate existance of job for srun testing purposes */
		char *sep;
		alloc_resp = xmalloc(sizeof(
				     resource_allocation_response_msg_t));
		alloc_resp->job_id = xstrdup(jobid);
		sep = strchr(jobid, '.');
		if (sep)
			sep[0] = '\0';
		alloc_resp->node_list = xstrdup(jobid);
		/* alloc_resp->alias_list = xstrdup(jobid); */
		if (sep)
			sep[0] = '.';
		alloc_resp->node_cnt = 1;
		alloc_resp->num_cpu_groups = 1;
		alloc_resp->cpus_per_node = xmalloc(sizeof(uint16_t) *
						    alloc_resp->num_cpu_groups);
		alloc_resp->cpus_per_node[0] = 1;
		alloc_resp->cpu_count_reps = xmalloc(sizeof(uint32_t) *
						    alloc_resp->num_cpu_groups);
		alloc_resp->cpu_count_reps[0] = alloc_resp->node_cnt;
		*info = alloc_resp;
#endif
	} else if (job_ptr->job_state >= JOB_COMPLETE) {
		slurm_seterrno(ESLURM_INVALID_JOB_ID);
		rc = -1;
	} else {
		alloc_resp = xmalloc(sizeof(
				     resource_allocation_response_msg_t));
		alloc_resp->job_id = xstrdup(jobid);
		alloc_resp->node_list = xstrdup(job_ptr->nodes);
		/* alloc_resp->alias_list = xstrdup(job_ptr->nodes); */
		alloc_resp->node_cnt = job_ptr->num_nodes;
		alloc_resp->num_cpu_groups = 1;
		alloc_resp->cpus_per_node = xmalloc(sizeof(uint16_t) *
						    alloc_resp->num_cpu_groups);
		alloc_resp->cpus_per_node[0] = 1;
		alloc_resp->cpu_count_reps = xmalloc(sizeof(uint32_t) *
						    alloc_resp->num_cpu_groups);
		alloc_resp->cpu_count_reps[0] = alloc_resp->node_cnt;
		/* alloc_resp->error_code = 0; */
		/* alloc_resp->select_jobinfo = NULL; */
		alloc_resp->pn_min_memory = job_ptr->pn_min_memory;
		*info = alloc_resp;
	}
	slurm_free_job_info_msg(job_info_msg);

	return rc;
}
#endif
