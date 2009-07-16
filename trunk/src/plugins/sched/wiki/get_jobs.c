/*****************************************************************************\
 *  get_jobs.c - Process Wiki get job info request
 *****************************************************************************
 *  Copyright (C) 2006-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
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

#include <grp.h>
#include <sys/types.h>

#include "./msg.h"
#include "src/common/hostlist.h"
#include "src/common/list.h"
#include "src/common/uid.h"
#include "src/slurmctld/licenses.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/slurmctld.h"

static char *	_dump_all_jobs(int *job_cnt, time_t update_time);
static char *	_dump_job(struct job_record *job_ptr, time_t update_time);
static uint16_t _get_job_cpus_per_task(struct job_record *job_ptr);
static uint16_t _get_job_tasks_per_node(struct job_record *job_ptr);
static uint32_t	_get_job_end_time(struct job_record *job_ptr);
static char *	_get_job_features(struct job_record *job_ptr);
static uint32_t	_get_job_min_disk(struct job_record *job_ptr);
static uint32_t	_get_job_min_mem(struct job_record *job_ptr);
static uint32_t	_get_job_min_nodes(struct job_record *job_ptr);
static char *	_get_job_state(struct job_record *job_ptr);
static uint32_t	_get_job_submit_time(struct job_record *job_ptr);
static uint32_t	_get_job_suspend_time(struct job_record *job_ptr);
static uint32_t	_get_job_tasks(struct job_record *job_ptr);
static uint32_t	_get_job_time_limit(struct job_record *job_ptr);
static int	_hidden_job(struct job_record *job_ptr);
static char *	_task_list(struct job_record *job_ptr);

/*
 * get_jobs - get information on specific job(s) changed since some time
 * cmd_ptr IN - CMD=GETJOBS ARG=[<UPDATETIME>:<JOBID>[:<JOBID>]...]
 *                              [<UPDATETIME>:ALL]
 * RET 0 on success, -1 on failure
 *
 * Response format
 * ARG=<cnt>#<JOBID>;
 *	STATE=<state>;			Moab equivalent job state
 *	[HOSTLIST=<node1:node2>;]	list of required nodes, if any
 *	[STARTDATE=<uts>;]		earliest start time, if any
 *	[TASKLIST=<node1:node2>;]	nodes in use, if running or completing
 *	[RFEATURES=<features>;]		required features, if any, 
 *					NOTE: OR operator not supported
 *	[REJMESSAGE=<str>;]		reason job is not running, if any
 *	UPDATETIME=<uts>;		time last active
 *	WCLIMIT=<secs>;			wall clock time limit, seconds
 *	TASKS=<cpus>;			CPUs required
 *	[NODES=<nodes>;]		count of nodes required
 *	[TASKSPERNODE=<cnt>;]		tasks required per node
 *	DPROCS=<cpus_per_task>;		count of CPUs required per task
 *	QUEUETIME=<uts>;		submission time
 *	STARTTIME=<uts>;		time execution started
 *	PARTITIONMASK=<partition>;	partition name
 *	[DMEM=<mbytes>;]		MB of memory required per cpu
 *	RMEM=<MB>;			MB of memory required
 *	RDISK=<MB>;			MB of disk space required
 *	[COMPLETETIME=<uts>;]		termination time
 *	[SUSPENDTIME=<secs>;]		seconds that job has been suspended
 *	[ACCOUNT=<bank_account>;]	bank account name
 *	[QOS=<quality_of_service>;]	quality of service
 *	[RCLASS=<resource_class>;]	resource class
 *	[COMMENT=<whatever>;]		job dependency or account number
 *	UNAME=<user_name>;		user name
 *	GNAME=<group_name>;		group name
 * [#<JOBID>;...];			additional jobs, if any
 */
extern int	get_jobs(char *cmd_ptr, int *err_code, char **err_msg)
{
	char *arg_ptr, *tmp_char, *tmp_buf, *buf = NULL;
	time_t update_time;
	/* Locks: read job, partition */
	slurmctld_lock_t job_read_lock = {
		NO_LOCK, READ_LOCK, NO_LOCK, READ_LOCK };
	int job_rec_cnt = 0, buf_size = 0;

	arg_ptr = strstr(cmd_ptr, "ARG=");
	if (arg_ptr == NULL) {
		*err_code = -300;
		*err_msg = "GETJOBS lacks ARG";
		error("wiki: GETJOBS lacks ARG");
		return -1;
	}
	update_time = (time_t) strtoul(arg_ptr+4, &tmp_char, 10);
	if (tmp_char[0] != ':') {
		*err_code = -300;
		*err_msg = "Invalid ARG value";
		error("wiki: GETJOBS has invalid ARG value");
		return -1;
	}
	if (job_list == NULL) {
		*err_code = -140;
		*err_msg = "Still performing initialization";
		error("wiki: job_list not yet initilized");
		return -1;
	}
	tmp_char++;
	lock_slurmctld(job_read_lock);
	if (strncmp(tmp_char, "ALL", 3) == 0) {
		/* report all jobs */
		buf = _dump_all_jobs(&job_rec_cnt, update_time);
	} else {
		struct job_record *job_ptr = NULL;
		char *job_name = NULL, *tmp2_char = NULL;
		uint32_t job_id;

		job_name = strtok_r(tmp_char, ":", &tmp2_char);
		while (job_name) {
			job_id = (uint32_t) strtoul(job_name, NULL, 10);
			job_ptr = find_job_record(job_id);
			tmp_buf = _dump_job(job_ptr, update_time);
			if (job_rec_cnt > 0)
				xstrcat(buf, "#");
			xstrcat(buf, tmp_buf);
			xfree(tmp_buf);
			job_rec_cnt++;
			job_name = strtok_r(NULL, ":", &tmp2_char);
		}
	}
	unlock_slurmctld(job_read_lock);

	/* Prepend ("ARG=%d", job_rec_cnt) to reply message */
	if (buf)
		buf_size = strlen(buf);
	tmp_buf = xmalloc(buf_size + 32);
	if (job_rec_cnt)
		sprintf(tmp_buf, "SC=0 ARG=%d#%s", job_rec_cnt, buf);
	else
		sprintf(tmp_buf, "SC=0 ARG=0#");
	xfree(buf);
	*err_code = 0;
	*err_msg = tmp_buf;
	return 0;
}

static int	_hidden_job(struct job_record *job_ptr)
{
	int i;

	for (i=0; i<HIDE_PART_CNT; i++) {
		if (hide_part_ptr[i] == NULL)
			break;
		if (hide_part_ptr[i] == job_ptr->part_ptr)
			return 1;
	}
	return 0;
}

static char *   _dump_all_jobs(int *job_cnt, time_t update_time)
{
	int cnt = 0;
	struct job_record *job_ptr;
	ListIterator job_iterator;
	char *tmp_buf, *buf = NULL;

	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		if (_hidden_job(job_ptr))
			continue;
		tmp_buf = _dump_job(job_ptr, update_time);
		if (cnt > 0)
			xstrcat(buf, "#");
		xstrcat(buf, tmp_buf);
		xfree(tmp_buf);
		cnt++;
	}
	*job_cnt = cnt;
	return buf;
}

static char *	_dump_job(struct job_record *job_ptr, time_t update_time)
{
	char tmp[16384], *buf = NULL;
	char *uname, *gname;
	uint32_t end_time, suspend_time, min_mem;

	if (!job_ptr)
		return NULL;

	snprintf(tmp, sizeof(tmp), "%u:STATE=%s;",
		job_ptr->job_id, _get_job_state(job_ptr));
	xstrcat(buf, tmp);

	if (update_time > last_job_update)
		return buf;

	if (IS_JOB_PENDING(job_ptr) && (job_ptr->details)) {
		if ((job_ptr->details->req_nodes)
		    &&  (job_ptr->details->req_nodes[0])) {
			char *hosts = bitmap2wiki_node_name(
				job_ptr->details->req_node_bitmap);
			snprintf(tmp, sizeof(tmp),
				 "HOSTLIST=%s;", hosts);
			xstrcat(buf, tmp);
			xfree(hosts);
		}
		if (job_ptr->details->begin_time) {
			snprintf(tmp, sizeof(tmp),
				 "STARTDATE=%u;", (uint32_t)
				 job_ptr->details->begin_time);
			xstrcat(buf, tmp);
		}
	} else if (!IS_JOB_FINISHED(job_ptr)) {
		char *hosts = _task_list(job_ptr);
		snprintf(tmp, sizeof(tmp),
			"TASKLIST=%s;", hosts);
		xstrcat(buf, tmp);
		xfree(hosts);
	}

	if (IS_JOB_PENDING(job_ptr)) {
		char *req_features = _get_job_features(job_ptr);
		if (req_features) {
			snprintf(tmp, sizeof(tmp),
				"RFEATURES=%s;", req_features);
			xstrcat(buf, tmp);
			xfree(req_features);
		}
	}

	if (IS_JOB_FAILED(job_ptr)) {
		snprintf(tmp, sizeof(tmp),
			"REJMESSAGE=\"%s\";",
			job_reason_string(job_ptr->state_reason));
		xstrcat(buf, tmp);
	}

	snprintf(tmp, sizeof(tmp), 
		"UPDATETIME=%u;WCLIMIT=%u;TASKS=%u;",
		(uint32_t) job_ptr->time_last_active,
		(uint32_t) _get_job_time_limit(job_ptr),
		_get_job_tasks(job_ptr));
	xstrcat(buf, tmp);

	if (!IS_JOB_FINISHED(job_ptr)) {
	        uint16_t tpn;
		snprintf(tmp, sizeof(tmp),
			"NODES=%u;",
			_get_job_min_nodes(job_ptr));
		xstrcat(buf, tmp);
		tpn = _get_job_tasks_per_node(job_ptr);
		if (tpn > 0) {
			snprintf(tmp, sizeof(tmp),
				 "TASKPERNODE=%u;",
				 tpn);
			xstrcat(buf, tmp);
		}
	}

	snprintf(tmp, sizeof(tmp),
		"DPROCS=%u;",
		_get_job_cpus_per_task(job_ptr));
	xstrcat(buf, tmp);

	snprintf(tmp, sizeof(tmp),
		"QUEUETIME=%u;STARTTIME=%u;PARTITIONMASK=%s;",
		_get_job_submit_time(job_ptr),
		(uint32_t) job_ptr->start_time,
		job_ptr->partition);
	xstrcat(buf, tmp);

	min_mem = _get_job_min_mem(job_ptr);
	if (min_mem & MEM_PER_CPU) {
		snprintf(tmp, sizeof(tmp),
			"DMEM=%u;", min_mem & (~MEM_PER_CPU));
		xstrcat(buf, tmp);
	}

	snprintf(tmp, sizeof(tmp),
		"RMEM=%u;RDISK=%u;",
		_get_job_min_mem(job_ptr),
		_get_job_min_disk(job_ptr));
	xstrcat(buf, tmp);

	end_time = _get_job_end_time(job_ptr);
	if (end_time) {
		snprintf(tmp, sizeof(tmp),
			"COMPLETETIME=%u;", end_time);
		xstrcat(buf, tmp);
	}

	suspend_time = _get_job_suspend_time(job_ptr);
	if (suspend_time) {
		snprintf(tmp, sizeof(tmp),
			"SUSPENDTIME=%u;", suspend_time);
		xstrcat(buf, tmp);
	}

	if (job_ptr->account) {
		/* allow QOS spec in form "qos-name" */
		if (!strncmp(job_ptr->account, "qos-", 4)) {
			snprintf(tmp, sizeof(tmp),
				 "QOS=%s;", job_ptr->account + 4);
		} else {
			snprintf(tmp, sizeof(tmp),
				"ACCOUNT=%s;", job_ptr->account);
		}
		xstrcat(buf, tmp);
	}

	if (job_ptr->comment && job_ptr->comment[0]) {
		/* Parse comment for class/qos spec */
		char *copy;
		char *cred, *value;
		copy = xstrdup(job_ptr->comment);
		cred = strtok(copy, ",");
		while (cred != NULL) {
			if (!strncmp(cred, "qos:", 4)) {
				value = &cred[4];
				if (value[0] != '\0') {
					snprintf(tmp, sizeof(tmp),
						 "QOS=%s;", value);
					xstrcat(buf, tmp);
				}
			} else if (!strncmp(cred, "class:", 6)) {
				value = &cred[6];
				if (value[0] != '\0') {
					snprintf(tmp, sizeof(tmp),
						"RCLASS=%s;", value);
					xstrcat(buf, tmp);
				}
			}
			cred = strtok(NULL, ",");
		}
		xfree(copy);
		snprintf(tmp, sizeof(tmp),
			 "COMMENT=%s;", job_ptr->comment);
		xstrcat(buf, tmp);
	}

	if (job_ptr->details &&
	    (update_time > job_ptr->details->submit_time))
		return buf;

	uname = uid_to_string((uid_t) job_ptr->user_id);
	gname = gid_to_string(job_ptr->group_id);
	snprintf(tmp, sizeof(tmp),
		"UNAME=%s;GNAME=%s;", uname, gname);
	xstrcat(buf, tmp);
	xfree(uname);
	xfree(gname);

	return buf;
}

static uint16_t _get_job_cpus_per_task(struct job_record *job_ptr)
{
	uint16_t cpus_per_task = 1;

	if (job_ptr->details && job_ptr->details->cpus_per_task)
		cpus_per_task = job_ptr->details->cpus_per_task;
	return cpus_per_task;
}


static uint16_t _get_job_tasks_per_node(struct job_record *job_ptr)
{
	uint16_t tasks_per_node = 0;

	if (job_ptr->details && job_ptr->details->ntasks_per_node)
		tasks_per_node = job_ptr->details->ntasks_per_node;
	return tasks_per_node;
}

static uint32_t _get_job_min_mem(struct job_record *job_ptr)
{
	if (job_ptr->details)
		return job_ptr->details->job_min_memory;
	return (uint32_t) 0;
}

static uint32_t _get_job_min_disk(struct job_record *job_ptr)
	
{
	if (job_ptr->details)
		return job_ptr->details->job_min_tmp_disk;
	return (uint32_t) 0;
}

static uint32_t	_get_job_min_nodes(struct job_record *job_ptr)
{
	if (IS_JOB_STARTED(job_ptr)) {
		/* return actual count of currently allocated nodes.
		 * NOTE: gets decremented to zero while job is completing */
		return job_ptr->node_cnt;
	}

	if (job_ptr->details)
		return job_ptr->details->min_nodes;
	return (uint32_t) 1;
}

static uint32_t _get_job_submit_time(struct job_record *job_ptr)
{
	if (job_ptr->details)
		return (uint32_t) job_ptr->details->submit_time;
	return (uint32_t) 0;
}

static uint32_t _get_job_tasks(struct job_record *job_ptr)
{
	uint32_t task_cnt;

	if (IS_JOB_STARTED(job_ptr)) {
		task_cnt = job_ptr->total_procs;
	} else {
		if (job_ptr->num_procs)
			task_cnt = job_ptr->num_procs;
		else
			task_cnt = 1;
		if (job_ptr->details) {
			task_cnt = MAX(task_cnt,
				       (_get_job_min_nodes(job_ptr) * 
				        job_ptr->details->
					ntasks_per_node));
		}
	}

	return task_cnt / _get_job_cpus_per_task(job_ptr);
}

static uint32_t	_get_job_time_limit(struct job_record *job_ptr)
{
	uint32_t limit = job_ptr->time_limit;

	if ((limit == NO_VAL) || (limit == INFINITE))
		return (uint32_t) (365 * 24 * 60 * 60);	/* one year */
	else
		return (limit * 60);	/* seconds, not minutes */
}

/* NOTE: if job has already completed, we append "EXITCODE=#" to 
 * the state name */
static char *	_get_job_state(struct job_record *job_ptr)
{
	if (IS_JOB_COMPLETING(job_ptr)) {
		/* Give configured KillWait+10 for job
		 * to clear out, then then consider job 
		 * done. Moab will allocate jobs to 
		 * nodes that are already Idle. */ 
		int age = (int) difftime(time(NULL), 
			job_ptr->end_time);
		if (age < (kill_wait+10))
			return "Running";
	}

	if (IS_JOB_RUNNING(job_ptr))
		return "Running";
	if (IS_JOB_SUSPENDED(job_ptr))
		return "Suspended";
	if (IS_JOB_PENDING(job_ptr))
		return "Idle";

	if (IS_JOB_COMPLETE(job_ptr))
		return "Completed";
	else /* JOB_CANCELLED, JOB_FAILED, JOB_TIMEOUT, JOB_NODE_FAIL */
		return "Removed";
}

/* Return a job's required features, if any joined with AND.
 * If required features are joined by OR, then return NULL.
 * Returned string must be xfreed. */
static char * _get_job_features(struct job_record *job_ptr)
{
	int i;
	char *rfeatures;

	if ((job_ptr->details == NULL)
	||  (job_ptr->details->features == NULL)
	||  (job_ptr->details->features[0] == '\0'))
		return NULL;

	rfeatures = xstrdup(job_ptr->details->features);
	/* Translate "&" to ":" */
	for (i=0; ; i++) {
		if (rfeatures[i] == '\0')
			return rfeatures;
		if (rfeatures[i] == '|')
			break;
		if (rfeatures[i] == '&')
			rfeatures[i] = ':';
	}

	/* Found '|' (OR), which is not supported by Moab */
	xfree(rfeatures);
	return NULL;
}

static uint32_t	_get_job_end_time(struct job_record *job_ptr)
{
	if (IS_JOB_FINISHED(job_ptr))
		return (uint32_t) job_ptr->end_time;
	return (uint32_t) 0;
}

/* returns how long job has been suspended, in seconds */
static uint32_t	_get_job_suspend_time(struct job_record *job_ptr)
{
	if (IS_JOB_SUSPENDED(job_ptr)) {
		time_t now = time(NULL);
		return (uint32_t) difftime(now, 
				job_ptr->suspend_time);
	}
	return (uint32_t) 0;
}

/*
 * bitmap2wiki_node_name  - given a bitmap, build a list of colon separated
 *	node names (if we can't use node range expressions), or the
 *	normal slurm node name expression
 *
 * IN bitmap - bitmap pointer
 * RET pointer to node list or NULL on error
 * globals: node_record_table_ptr - pointer to node table
 * NOTE: the caller must xfree the memory at node_list when no longer required
 */
extern char *   bitmap2wiki_node_name(bitstr_t *bitmap)
{
	int i, first = 1;
	char *buf = NULL;

	if (bitmap == NULL)
		return xstrdup("");

	for (i = 0; i < node_record_count; i++) {
		if (bit_test (bitmap, i) == 0)
			continue;
		if (first == 0)
			xstrcat(buf, ":");
		first = 0;
		xstrcat(buf, node_record_table_ptr[i].name);
	}
	return buf;
}


/* Return task list in Maui format: tux0:tux0:tux1:tux1:tux2 */
static char * _task_list(struct job_record *job_ptr)
{
	int i, j, task_cnt;
	char *buf = NULL, *host;
	hostlist_t hl = hostlist_create(job_ptr->nodes);
	select_job_res_t *select_ptr = job_ptr->select_job;

	xassert(select_ptr && select_ptr->cpus);
	buf = xstrdup("");
	if (hl == NULL)
		return buf;

	for (i=0; i<select_ptr->nhosts; i++) {
		host = hostlist_shift(hl);
		if (host == NULL) {
			error("bad node_cnt for job %u (%s, %d)", 
				job_ptr->job_id, job_ptr->nodes,
				job_ptr->node_cnt);
			break;
		}
		task_cnt = select_ptr->cpus[i];
		if (job_ptr->details && job_ptr->details->cpus_per_task)
			task_cnt /= job_ptr->details->cpus_per_task;
		for (j=0; j<task_cnt; j++) {
			if (buf)
				xstrcat(buf, ":");
			xstrcat(buf, host);
		}
		free(host);
	}
	hostlist_destroy(hl);
	return buf;
}
