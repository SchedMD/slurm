/*****************************************************************************\
 *  slurm_jobacct_gather.c - implementation-independent job accounting logging
 *  functions
 *****************************************************************************
 *  Copyright (C) 2003-2007/ The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Jay Windley <jwindley@lnxi.com>, Morris Jette <jette1@llnl.com>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  Copyright (C) 2005 Hewlett-Packard Development Company, L.P.
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

/*****************************************************************************\
 *  Modification history
 *
 *  19 Jan 2005 by Andy Riebs <andy.riebs@hp.com>
 *  	 This file is derived from the file slurm_jobcomp.c, written by
 *  	 Morris Jette, et al.
\*****************************************************************************/

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "src/common/macros.h"
#include "src/common/plugin.h"
#include "src/common/plugrack.h"
#include "src/common/read_config.h"
#include "src/common/slurm_jobacct_gather.h"
#include "src/common/slurmdbd_defs.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"

/*
** Define slurm-specific aliases for use by plugins, see slurm_xlator.h
** for details.
 */
strong_alias(jobacctinfo_pack, slurm_jobacctinfo_pack);
strong_alias(jobacctinfo_unpack, slurm_jobacctinfo_unpack);
strong_alias(jobacctinfo_create, slurm_jobacctinfo_create);
strong_alias(jobacctinfo_destroy, slurm_jobacctinfo_destroy);


/*
 * WARNING:  Do not change the order of these fields or add additional
 * fields at the beginning of the structure.  If you do, job accounting
 * plugins will stop working.  If you need to add fields, add them
 * at the end of the structure.
 */
typedef struct slurm_jobacct_gather_ops {
	void (*poll_data) (List task_list, bool pgid_plugin, uint64_t cont_id);
	int (*endpoll)    ();
	int (*add_task)   (pid_t pid, jobacct_id_t *jobacct_id);
} slurm_jobacct_gather_ops_t;

/*
 * These strings must be in the same order as the fields declared
 * for slurm_jobacct_gather_ops_t.
 */
static const char *syms[] = {
	"jobacct_gather_p_poll_data",
	"jobacct_gather_p_endpoll",
	"jobacct_gather_p_add_task",
};

static slurm_jobacct_gather_ops_t ops;
static plugin_context_t *g_context = NULL;
static pthread_mutex_t g_context_lock = PTHREAD_MUTEX_INITIALIZER;
static bool init_run = false;

static int freq = 0;
static bool pgid_plugin = false;
static List task_list = NULL;
static uint64_t cont_id = (uint64_t)NO_VAL;
static pthread_mutex_t task_list_lock = PTHREAD_MUTEX_INITIALIZER;

static bool jobacct_shutdown = true;
static bool jobacct_suspended = 0;
static bool plugin_polling = true;

static uint32_t jobacct_job_id     = 0;
static uint32_t jobacct_step_id    = 0;
static uint32_t jobacct_mem_limit  = 0;
static uint32_t jobacct_vmem_limit = 0;

/* _acct_kill_step() issue RPC to kill a slurm job step */
static void _acct_kill_step(void)
{
	slurm_msg_t msg;
	job_step_kill_msg_t req;
	job_notify_msg_t notify_req;

	slurm_msg_t_init(&msg);
	notify_req.job_id      = jobacct_job_id;
	notify_req.job_step_id = jobacct_step_id;
	notify_req.message     = "Exceeded job memory limit";
	msg.msg_type    = REQUEST_JOB_NOTIFY;
	msg.data        = &notify_req;
	slurm_send_only_controller_msg(&msg);

	/*
	 * Request message:
	 */
	req.job_id      = jobacct_job_id;
	req.job_step_id = jobacct_step_id;
	req.signal      = SIGKILL;
	req.batch_flag  = 0;
	msg.msg_type    = REQUEST_CANCEL_JOB_STEP;
	msg.data        = &req;

	slurm_send_only_controller_msg(&msg);
}

static void _pack_jobacct_id(jobacct_id_t *jobacct_id,
			     uint16_t rpc_version, Buf buffer)
{
	if (jobacct_id) {
		pack32((uint32_t) jobacct_id->nodeid, buffer);
		pack16((uint16_t) jobacct_id->taskid, buffer);
	} else {
		pack32((uint32_t) NO_VAL, buffer);
		pack16((uint16_t) NO_VAL, buffer);
	}
}

static int _unpack_jobacct_id(jobacct_id_t *jobacct_id,
			      uint16_t rpc_version, Buf buffer)
{
	safe_unpack32(&jobacct_id->nodeid, buffer);
	safe_unpack16(&jobacct_id->taskid, buffer);

	return SLURM_SUCCESS;
unpack_error:
	return SLURM_ERROR;
}

static void _poll_data(void)
{
	if (jobacct_suspended)
		return;

	/* Update the data */
	slurm_mutex_lock(&task_list_lock);
	(*(ops.poll_data))(task_list, pgid_plugin, cont_id);
	slurm_mutex_unlock(&task_list_lock);
}

static void _task_sleep(int rem)
{
	while (rem)
		rem = sleep(rem);	/* subject to interupt */
}


/* _watch_tasks() -- monitor slurm jobs and track their memory usage
 *
 * IN, OUT:	Irrelevant; this is invoked by pthread_create()
 */

static void *_watch_tasks(void *arg)
{
	/* Give chance for processes to spawn before starting
	 * the polling. This should largely eliminate the
	 * the chance of having /proc open when the tasks are
	 * spawned, which would prevent a valid checkpoint/restart
	 * with some systems */
	_task_sleep(1);

	while (!jobacct_shutdown) {  /* Do this until shutdown is requested */
		_poll_data();
		_task_sleep(freq);
	}
	return NULL;
}

extern int jobacct_gather_init(void)
{
	char    *plugin_type = "jobacct_gather";
	char	*type = NULL;
	int	retval=SLURM_SUCCESS;

	if (init_run && g_context)
		return retval;

	slurm_mutex_lock(&g_context_lock);
	if (g_context)
		goto done;

	type = slurm_get_jobacct_gather_type();

	g_context = plugin_context_create(
		plugin_type, type, (void **)&ops, syms, sizeof(syms));

	if (!g_context) {
		error("cannot create %s context for %s", plugin_type, type);
		retval = SLURM_ERROR;
		goto done;
	}

	if (!strcasecmp(type, "jobacct_gather/none")) {
		plugin_polling = false;
		goto done;
	}

	plugin_type = type;
	type = slurm_get_proctrack_type();
	if (!strcasecmp(type, "proctrack/pgid")) {
		info("WARNING: We will use a much slower algorithm with "
		     "proctrack/pgid, use Proctracktype=proctrack/linuxproc "
		     "or some other proctrack when using %s",
		     plugin_type);
		pgid_plugin = true;
	}
	xfree(type);
	xfree(plugin_type);

	type = slurm_get_accounting_storage_type();
	if (!strcasecmp(type, ACCOUNTING_STORAGE_TYPE_NONE)) {
		error("WARNING: Even though we are collecting accounting "
		      "information you have asked for it not to be stored "
		      "(%s) if this is not what you have in mind you will "
		      "need to change it.", ACCOUNTING_STORAGE_TYPE_NONE);
	}
	init_run = true;

done:
	slurm_mutex_unlock(&g_context_lock);
	xfree(type);

	return(retval);
}

extern int jobacct_gather_fini(void)
{
	int rc = SLURM_SUCCESS;

	slurm_mutex_lock(&g_context_lock);
	if (g_context) {
		init_run = false;
		rc = plugin_context_destroy(g_context);
		g_context = NULL;
	}
	slurm_mutex_unlock(&g_context_lock);
	return rc;
}

extern int jobacct_gather_startpoll(uint16_t frequency)
{
	int retval = SLURM_SUCCESS;
	pthread_attr_t attr;
	pthread_t _watch_tasks_thread_id;

	if (!plugin_polling)
		return SLURM_SUCCESS;

	if (jobacct_gather_init() < 0)
		return SLURM_ERROR;

	if (!jobacct_shutdown) {
		error("jobacct_gather_startpoll: poll already started!");
		return retval;
	}

	jobacct_shutdown = false;

	freq = frequency;

	task_list = list_create(jobacctinfo_destroy);
	if (frequency == 0) {	/* don't want dynamic monitoring? */
		debug2("jobacct_gather dynamic logging disabled");
		return retval;
	}

	/* create polling thread */
	slurm_attr_init(&attr);
	if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED))
		error("pthread_attr_setdetachstate error %m");

	if  (pthread_create(&_watch_tasks_thread_id, &attr,
			    &_watch_tasks, NULL)) {
		debug("jobacct_gather failed to create _watch_tasks "
		      "thread: %m");
		frequency = 0;
	} else
		debug3("jobacct_gather dynamic logging enabled");
	slurm_attr_destroy(&attr);

	return retval;
}

extern int jobacct_gather_endpoll(void)
{
	int retval = SLURM_SUCCESS;

	if (jobacct_gather_init() < 0)
		return SLURM_ERROR;

	jobacct_shutdown = true;
	slurm_mutex_lock(&task_list_lock);
	if(task_list)
		list_destroy(task_list);
	task_list = NULL;
	slurm_mutex_unlock(&task_list_lock);

	retval = (*(ops.endpoll))();

	return retval;
}

extern void jobacct_gather_change_poll(uint16_t frequency)
{
	if (jobacct_gather_init() < 0)
		return;

	if (plugin_polling && freq == 0 && frequency != 0) {
		pthread_attr_t attr;
		pthread_t _watch_tasks_thread_id;
		/* create polling thread */
		slurm_attr_init(&attr);
		if (pthread_attr_setdetachstate(&attr,
						PTHREAD_CREATE_DETACHED))
			error("pthread_attr_setdetachstate error %m");

		if  (pthread_create(&_watch_tasks_thread_id, &attr,
				    &_watch_tasks, NULL)) {
			debug("jobacct-gather failed to create _watch_tasks "
			      "thread: %m");
			frequency = 0;
		}
		else
			debug3("jobacct-gather LINUX dynamic logging enabled");
		slurm_attr_destroy(&attr);
		jobacct_shutdown = false;
	}

	freq = frequency;
	debug("jobacct-gather: frequency changed = %d", frequency);
	if (freq == 0)
		jobacct_shutdown = true;
	return;
}

extern void jobacct_gather_suspend_poll(void)
{
	jobacct_suspended = true;
}

extern void jobacct_gather_resume_poll(void)
{
	jobacct_suspended = false;
}

extern int jobacct_gather_add_task(pid_t pid, jobacct_id_t *jobacct_id,
				   int poll)
{
	struct jobacctinfo *jobacct;

	if (jobacct_gather_init() < 0)
		return SLURM_ERROR;

	if (!plugin_polling)
		return SLURM_SUCCESS;

	if (jobacct_shutdown)
		return SLURM_ERROR;

	jobacct = jobacctinfo_create(jobacct_id);

	slurm_mutex_lock(&task_list_lock);
	if (pid <= 0) {
		error("invalid pid given (%d) for task acct", pid);
		goto error;
	} else if (!task_list) {
		error("no task list created!");
		goto error;
	}

	jobacct->pid = pid;
	jobacct->min_cpu = 0;
	debug2("adding task %u pid %d on node %u to jobacct",
	       jobacct_id->taskid, pid, jobacct_id->nodeid);
	list_push(task_list, jobacct);
	slurm_mutex_unlock(&task_list_lock);

	(*(ops.add_task))(pid, jobacct_id);

	if (poll == 1)
		_poll_data();

	return SLURM_SUCCESS;
error:
	slurm_mutex_unlock(&task_list_lock);
	jobacctinfo_destroy(jobacct);
	return SLURM_ERROR;
}

extern jobacctinfo_t *jobacct_gather_stat_task(pid_t pid)
{
	if (!plugin_polling || jobacct_shutdown)
		return NULL;
	else if (pid) {
		struct jobacctinfo *jobacct = NULL;
		struct jobacctinfo *ret_jobacct = NULL;
		ListIterator itr = NULL;

		_poll_data();

		slurm_mutex_lock(&task_list_lock);
		if (!task_list) {
			error("no task list created!");
			goto error;
		}

		itr = list_iterator_create(task_list);
		while ((jobacct = list_next(itr))) {
			if(jobacct->pid == pid)
				break;
		}
		list_iterator_destroy(itr);
		if (jobacct == NULL)
			goto error;
		ret_jobacct = xmalloc(sizeof(struct jobacctinfo));
		memcpy(ret_jobacct, jobacct, sizeof(struct jobacctinfo));
	error:
		slurm_mutex_unlock(&task_list_lock);
		return ret_jobacct;
	} else {
		/* In this situation, we are just trying to get a
		 * basis of information since we are not pollng.  So
		 * we will give a chance for processes to spawn before we
		 * gather information. This should largely eliminate the
		 * the chance of having /proc open when the tasks are
		 * spawned, which would prevent a valid checkpoint/restart
		 * with some systems */
		_task_sleep(1);
		_poll_data();
		return NULL;
	}
}

extern jobacctinfo_t *jobacct_gather_remove_task(pid_t pid)
{
	struct jobacctinfo *jobacct = NULL;
	ListIterator itr = NULL;

	if (!plugin_polling)
		return NULL;

	/* poll data one last time before removing task
	 * mainly for updating energy consumption */
	_poll_data();

	if (jobacct_shutdown)
		return NULL;

	slurm_mutex_lock(&task_list_lock);
	if (!task_list) {
		error("no task list created!");
		goto error;
	}

	itr = list_iterator_create(task_list);
	while((jobacct = list_next(itr))) {
		if(jobacct->pid == pid) {
			list_remove(itr);
			break;
		}
	}
	list_iterator_destroy(itr);
	if(jobacct) {
		debug2("removing task %u pid %d from jobacct",
		       jobacct->max_vsize_id.taskid, jobacct->pid);
	} else {
		debug2("pid(%d) not being watched in jobacct!", pid);
	}
error:
	slurm_mutex_unlock(&task_list_lock);
	return jobacct;
}

extern int jobacct_gather_set_proctrack_container_id(uint64_t id)
{
	if (!plugin_polling || pgid_plugin)
		return SLURM_SUCCESS;

	if (cont_id != (uint64_t)NO_VAL)
		info("Warning: jobacct: set_proctrack_container_id: cont_id "
		     "is already set to %"PRIu64" you are setting it to "
		     "%"PRIu64"", cont_id, id);
	if (id <= 0) {
		error("jobacct: set_proctrack_container_id: "
		      "I was given most likely an unset cont_id %"PRIu64"",
		      id);
		return SLURM_ERROR;
	}
	cont_id = id;

	return SLURM_SUCCESS;
}

extern int jobacct_gather_set_mem_limit(uint32_t job_id, uint32_t step_id,
					uint32_t mem_limit)
{
	if (!plugin_polling)
		return SLURM_SUCCESS;

	if ((job_id == 0) || (mem_limit == 0)) {
		error("jobacct_gather_set_mem_limit: jobid:%u mem_limit:%u",
		      job_id, mem_limit);
		return SLURM_ERROR;
	}

	jobacct_job_id      = job_id;
	jobacct_step_id     = step_id;
	jobacct_mem_limit   = mem_limit * 1024;	/* MB to KB */
	jobacct_vmem_limit  = jobacct_mem_limit;
	jobacct_vmem_limit *= (slurm_get_vsize_factor() / 100.0);
	return SLURM_SUCCESS;
}

extern void jobacct_gather_handle_mem_limit(
	uint32_t total_job_mem, uint32_t total_job_vsize)
{
	if (!plugin_polling)
		return;

	if (jobacct_mem_limit) {
		if (jobacct_step_id == NO_VAL) {
			debug("Job %u memory used:%u limit:%u KB",
			      jobacct_job_id, total_job_mem, jobacct_mem_limit);
		} else {
			debug("Step %u.%u memory used:%u limit:%u KB",
			      jobacct_job_id, jobacct_step_id,
			      total_job_mem, jobacct_mem_limit);
		}
	}
	if (jobacct_job_id && jobacct_mem_limit &&
	    (total_job_mem > jobacct_mem_limit)) {
		if (jobacct_step_id == NO_VAL) {
			error("Job %u exceeded %u KB memory limit, being "
			      "killed", jobacct_job_id, jobacct_mem_limit);
		} else {
			error("Step %u.%u exceeded %u KB memory limit, being "
			      "killed", jobacct_job_id, jobacct_step_id,
			      jobacct_mem_limit);
		}
		_acct_kill_step();
	} else if (jobacct_job_id && jobacct_vmem_limit &&
		   (total_job_vsize > jobacct_vmem_limit)) {
		if (jobacct_step_id == NO_VAL) {
			error("Job %u exceeded %u KB virtual memory limit, "
			      "being killed", jobacct_job_id,
			      jobacct_vmem_limit);
		} else {
			error("Step %u.%u exceeded %u KB virtual memory "
			      "limit, being killed", jobacct_job_id,
			      jobacct_step_id, jobacct_vmem_limit);
		}
		_acct_kill_step();
	}
}

/********************* jobacctinfo functions ******************************/

extern jobacctinfo_t *jobacctinfo_create(jobacct_id_t *jobacct_id)
{
	struct jobacctinfo *jobacct;

	if (!plugin_polling)
		return NULL;

	jobacct = xmalloc(sizeof(struct jobacctinfo));

	if (!jobacct_id) {
		jobacct_id_t temp_id;
		temp_id.taskid = (uint16_t)NO_VAL;
		temp_id.nodeid = (uint32_t)NO_VAL;
		jobacct_id = &temp_id;
	}
	memset(jobacct, 0, sizeof(struct jobacctinfo));
	jobacct->sys_cpu_sec = 0;
	jobacct->sys_cpu_usec = 0;
	jobacct->user_cpu_sec = 0;
	jobacct->user_cpu_usec = 0;

	jobacct->max_vsize = 0;
	memcpy(&jobacct->max_vsize_id, jobacct_id, sizeof(jobacct_id_t));
	jobacct->tot_vsize = 0;
	jobacct->max_rss = 0;
	memcpy(&jobacct->max_rss_id, jobacct_id, sizeof(jobacct_id_t));
	jobacct->tot_rss = 0;
	jobacct->max_pages = 0;
	memcpy(&jobacct->max_pages_id, jobacct_id, sizeof(jobacct_id_t));
	jobacct->tot_pages = 0;
	jobacct->min_cpu = (uint32_t)NO_VAL;
	memcpy(&jobacct->min_cpu_id, jobacct_id, sizeof(jobacct_id_t));
	jobacct->tot_cpu = 0;
	jobacct->act_cpufreq = 0;
	memset(&jobacct->energy, 0, sizeof(acct_gather_energy_t));

	return jobacct;
}

extern void jobacctinfo_destroy(void *object)
{
	struct jobacctinfo *jobacct = (struct jobacctinfo *)object;
	xfree(jobacct);
}

extern int jobacctinfo_setinfo(jobacctinfo_t *jobacct,
			       enum jobacct_data_type type, void *data)
{
	int rc = SLURM_SUCCESS;
	int *fd = (int *)data;
	struct rusage *rusage = (struct rusage *)data;
	uint32_t *uint32 = (uint32_t *) data;
	jobacct_id_t *jobacct_id = (jobacct_id_t *) data;
	struct jobacctinfo *send = (struct jobacctinfo *) data;

	if (!plugin_polling)
		return SLURM_SUCCESS;

	switch (type) {
	case JOBACCT_DATA_TOTAL:
		memcpy(jobacct, send, sizeof(struct jobacctinfo));
		break;
	case JOBACCT_DATA_PIPE:
		safe_write(*fd, jobacct, sizeof(struct jobacctinfo));
		break;
	case JOBACCT_DATA_RUSAGE:
		jobacct->user_cpu_sec = rusage->ru_utime.tv_sec;
		jobacct->user_cpu_usec = rusage->ru_utime.tv_usec;
		jobacct->sys_cpu_sec = rusage->ru_stime.tv_sec;
		jobacct->sys_cpu_usec = rusage->ru_stime.tv_usec;
		break;
	case JOBACCT_DATA_MAX_RSS:
		jobacct->max_rss = *uint32;
		break;
	case JOBACCT_DATA_MAX_RSS_ID:
		jobacct->max_rss_id = *jobacct_id;
		break;
	case JOBACCT_DATA_TOT_RSS:
		jobacct->tot_rss = *uint32;
		break;
	case JOBACCT_DATA_MAX_VSIZE:
		jobacct->max_vsize = *uint32;
		break;
	case JOBACCT_DATA_MAX_VSIZE_ID:
		jobacct->max_vsize_id = *jobacct_id;
		break;
	case JOBACCT_DATA_TOT_VSIZE:
		jobacct->tot_vsize = *uint32;
		break;
	case JOBACCT_DATA_MAX_PAGES:
		jobacct->max_pages = *uint32;
		break;
	case JOBACCT_DATA_MAX_PAGES_ID:
		jobacct->max_pages_id = *jobacct_id;
		break;
	case JOBACCT_DATA_TOT_PAGES:
		jobacct->tot_pages = *uint32;
		break;
	case JOBACCT_DATA_MIN_CPU:
		jobacct->min_cpu = *uint32;
		break;
	case JOBACCT_DATA_MIN_CPU_ID:
		jobacct->min_cpu_id = *jobacct_id;
		break;
	case JOBACCT_DATA_TOT_CPU:
		jobacct->tot_cpu = *uint32;
		break;
	case JOBACCT_DATA_ACT_CPUFREQ:
		jobacct->act_cpufreq = *uint32;
		break;
	case JOBACCT_DATA_CONSUMED_ENERGY:
		jobacct->energy.consumed_energy = *uint32;
		break;
	default:
		debug("jobacct_g_set_setinfo data_type %d invalid", type);
	}

	return rc;
rwfail:
	return SLURM_ERROR;
}

extern int jobacctinfo_getinfo(
	jobacctinfo_t *jobacct, enum jobacct_data_type type, void *data)
{
	int rc = SLURM_SUCCESS;
	int *fd = (int *)data;
	uint32_t *uint32 = (uint32_t *) data;
	jobacct_id_t *jobacct_id = (jobacct_id_t *) data;
	struct rusage *rusage = (struct rusage *)data;
	struct jobacctinfo *send = (struct jobacctinfo *) data;

	if (!plugin_polling)
		return SLURM_SUCCESS;

	switch (type) {
	case JOBACCT_DATA_TOTAL:
		memcpy(send, jobacct, sizeof(struct jobacctinfo));
		break;
	case JOBACCT_DATA_PIPE:
		safe_read(*fd, jobacct, sizeof(struct jobacctinfo));
		break;
	case JOBACCT_DATA_RUSAGE:
		memset(rusage, 0, sizeof(struct rusage));
		rusage->ru_utime.tv_sec = jobacct->user_cpu_sec;
		rusage->ru_utime.tv_usec = jobacct->user_cpu_usec;
		rusage->ru_stime.tv_sec = jobacct->sys_cpu_sec;
		rusage->ru_stime.tv_usec = jobacct->sys_cpu_usec;
		break;
	case JOBACCT_DATA_MAX_RSS:
		*uint32 = jobacct->max_rss;
		break;
	case JOBACCT_DATA_MAX_RSS_ID:
		*jobacct_id = jobacct->max_rss_id;
		break;
	case JOBACCT_DATA_TOT_RSS:
		*uint32 = jobacct->tot_rss;
		break;
	case JOBACCT_DATA_MAX_VSIZE:
		*uint32 = jobacct->max_vsize;
		break;
	case JOBACCT_DATA_MAX_VSIZE_ID:
		*jobacct_id = jobacct->max_vsize_id;
		break;
	case JOBACCT_DATA_TOT_VSIZE:
		*uint32 = jobacct->tot_vsize;
		break;
	case JOBACCT_DATA_MAX_PAGES:
		*uint32 = jobacct->max_pages;
		break;
	case JOBACCT_DATA_MAX_PAGES_ID:
		*jobacct_id = jobacct->max_pages_id;
		break;
	case JOBACCT_DATA_TOT_PAGES:
		*uint32 = jobacct->tot_pages;
		break;
	case JOBACCT_DATA_MIN_CPU:
		*uint32 = jobacct->min_cpu;
		break;
	case JOBACCT_DATA_MIN_CPU_ID:
		*jobacct_id = jobacct->min_cpu_id;
		break;
	case JOBACCT_DATA_TOT_CPU:
		*uint32 = jobacct->tot_cpu;
		break;
	case JOBACCT_DATA_ACT_CPUFREQ:
		*uint32 = jobacct->act_cpufreq;
		break;
	case JOBACCT_DATA_CONSUMED_ENERGY:
		*uint32 = jobacct->energy.consumed_energy;
		break;
	default:
		debug("jobacct_g_set_getinfo data_type %d invalid", type);
	}
	return rc;
rwfail:
	return SLURM_ERROR;
}

extern void jobacctinfo_pack(jobacctinfo_t *jobacct,
			     uint16_t rpc_version, uint16_t protocol_type,
			     Buf buffer)
{
	int i = 0;

	if (!plugin_polling && (protocol_type != PROTOCOL_TYPE_DBD))
		return;

	/* The function can take calls from both DBD and from regular
	 * SLURM functions.  We choose to standardize on using the
	 * SLURM_PROTOCOL_VERSION here so if PROTOCOL_TYPE_DBD comes
	 * in we need to translate the DBD rpc_version to use the
	 * SLURM protocol_version.
	 *
	 * If this function ever changes make sure the
	 * slurmdbd_translate_rpc function has been updated with the
	 * new protocol version.
	 */
	if (protocol_type == PROTOCOL_TYPE_DBD)
		rpc_version = slurmdbd_translate_rpc(rpc_version);

	if (rpc_version >= SLURM_2_5_PROTOCOL_VERSION) {
		if (!jobacct) {
			for (i = 0; i < 14; i++)
				pack32((uint32_t) 0, buffer);
			for (i = 0; i < 4; i++)
				_pack_jobacct_id(NULL, rpc_version, buffer);
			return;
		}

		pack32((uint32_t)jobacct->user_cpu_sec, buffer);
		pack32((uint32_t)jobacct->user_cpu_usec, buffer);
		pack32((uint32_t)jobacct->sys_cpu_sec, buffer);
		pack32((uint32_t)jobacct->sys_cpu_usec, buffer);
		pack32((uint32_t)jobacct->max_vsize, buffer);
		pack32((uint32_t)jobacct->tot_vsize, buffer);
		pack32((uint32_t)jobacct->max_rss, buffer);
		pack32((uint32_t)jobacct->tot_rss, buffer);
		pack32((uint32_t)jobacct->max_pages, buffer);
		pack32((uint32_t)jobacct->tot_pages, buffer);
		pack32((uint32_t)jobacct->min_cpu, buffer);
		pack32((uint32_t)jobacct->tot_cpu, buffer);
		pack32((uint32_t)jobacct->act_cpufreq, buffer);
		pack32((uint32_t)jobacct->energy.consumed_energy, buffer);

		_pack_jobacct_id(&jobacct->max_vsize_id, rpc_version, buffer);
		_pack_jobacct_id(&jobacct->max_rss_id, rpc_version, buffer);
		_pack_jobacct_id(&jobacct->max_pages_id, rpc_version, buffer);
		_pack_jobacct_id(&jobacct->min_cpu_id, rpc_version, buffer);
	} else {
		if (!jobacct) {
			for (i = 0; i < 12; i++)
				pack32((uint32_t) 0, buffer);
			for (i = 0; i < 4; i++)
				_pack_jobacct_id(NULL, rpc_version, buffer);
			return;
		}

		pack32((uint32_t)jobacct->user_cpu_sec, buffer);
		pack32((uint32_t)jobacct->user_cpu_usec, buffer);
		pack32((uint32_t)jobacct->sys_cpu_sec, buffer);
		pack32((uint32_t)jobacct->sys_cpu_usec, buffer);
		pack32((uint32_t)jobacct->max_vsize, buffer);
		pack32((uint32_t)jobacct->tot_vsize, buffer);
		pack32((uint32_t)jobacct->max_rss, buffer);
		pack32((uint32_t)jobacct->tot_rss, buffer);
		pack32((uint32_t)jobacct->max_pages, buffer);
		pack32((uint32_t)jobacct->tot_pages, buffer);
		pack32((uint32_t)jobacct->min_cpu, buffer);
		pack32((uint32_t)jobacct->tot_cpu, buffer);

		_pack_jobacct_id(&jobacct->max_vsize_id, rpc_version, buffer);
		_pack_jobacct_id(&jobacct->max_rss_id, rpc_version, buffer);
		_pack_jobacct_id(&jobacct->max_pages_id, rpc_version, buffer);
		_pack_jobacct_id(&jobacct->min_cpu_id, rpc_version, buffer);

	}
}

extern int jobacctinfo_unpack(jobacctinfo_t **jobacct,
			      uint16_t rpc_version, uint16_t protocol_type,
			      Buf buffer)
{
	uint32_t uint32_tmp;

	if (!plugin_polling && (protocol_type != PROTOCOL_TYPE_DBD))
		return SLURM_SUCCESS;

	/* The function can take calls from both DBD and from regular
	 * SLURM functions.  We choose to standardize on using the
	 * SLURM_PROTOCOL_VERSION here so if PROTOCOL_TYPE_DBD comes
	 * in we need to translate the DBD rpc_version to use the
	 * SLURM protocol_version.
	 *
	 * If this function ever changes make sure the
	 * slurmdbd_translate_rpc function has been updated with the
	 * new protocol version.
	 */
	if (protocol_type == PROTOCOL_TYPE_DBD)
		rpc_version = slurmdbd_translate_rpc(rpc_version);

	if (rpc_version >= SLURM_2_5_PROTOCOL_VERSION) {
		*jobacct = xmalloc(sizeof(struct jobacctinfo));
		safe_unpack32(&uint32_tmp, buffer);
		(*jobacct)->user_cpu_sec = uint32_tmp;
		safe_unpack32(&uint32_tmp, buffer);
		(*jobacct)->user_cpu_usec = uint32_tmp;
		safe_unpack32(&uint32_tmp, buffer);
		(*jobacct)->sys_cpu_sec = uint32_tmp;
		safe_unpack32(&uint32_tmp, buffer);
		(*jobacct)->sys_cpu_usec = uint32_tmp;
		safe_unpack32(&(*jobacct)->max_vsize, buffer);
		safe_unpack32(&(*jobacct)->tot_vsize, buffer);
		safe_unpack32(&(*jobacct)->max_rss, buffer);
		safe_unpack32(&(*jobacct)->tot_rss, buffer);
		safe_unpack32(&(*jobacct)->max_pages, buffer);
		safe_unpack32(&(*jobacct)->tot_pages, buffer);
		safe_unpack32(&(*jobacct)->min_cpu, buffer);
		safe_unpack32(&(*jobacct)->tot_cpu, buffer);
		safe_unpack32(&(*jobacct)->act_cpufreq, buffer);
		safe_unpack32(&(*jobacct)->energy.consumed_energy, buffer);

		if (_unpack_jobacct_id(&(*jobacct)->max_vsize_id, rpc_version,
			buffer) != SLURM_SUCCESS)
			goto unpack_error;
		if (_unpack_jobacct_id(&(*jobacct)->max_rss_id, rpc_version,
			buffer) != SLURM_SUCCESS)
			goto unpack_error;
		if (_unpack_jobacct_id(&(*jobacct)->max_pages_id, rpc_version,
			buffer) != SLURM_SUCCESS)
			goto unpack_error;
		if (_unpack_jobacct_id(&(*jobacct)->min_cpu_id, rpc_version,
			buffer) != SLURM_SUCCESS)
			goto unpack_error;
	} else {
		*jobacct = xmalloc(sizeof(struct jobacctinfo));
		safe_unpack32(&uint32_tmp, buffer);
		(*jobacct)->user_cpu_sec = uint32_tmp;
		safe_unpack32(&uint32_tmp, buffer);
		(*jobacct)->user_cpu_usec = uint32_tmp;
		safe_unpack32(&uint32_tmp, buffer);
		(*jobacct)->sys_cpu_sec = uint32_tmp;
		safe_unpack32(&uint32_tmp, buffer);
		(*jobacct)->sys_cpu_usec = uint32_tmp;
		safe_unpack32(&(*jobacct)->max_vsize, buffer);
		safe_unpack32(&(*jobacct)->tot_vsize, buffer);
		safe_unpack32(&(*jobacct)->max_rss, buffer);
		safe_unpack32(&(*jobacct)->tot_rss, buffer);
		safe_unpack32(&(*jobacct)->max_pages, buffer);
		safe_unpack32(&(*jobacct)->tot_pages, buffer);
		safe_unpack32(&(*jobacct)->min_cpu, buffer);
		safe_unpack32(&(*jobacct)->tot_cpu, buffer);

		if (_unpack_jobacct_id(&(*jobacct)->max_vsize_id, rpc_version,
			buffer)!= SLURM_SUCCESS)
			goto unpack_error;
		if (_unpack_jobacct_id(&(*jobacct)->max_rss_id, rpc_version,
			buffer)!= SLURM_SUCCESS)
			goto unpack_error;
		if (_unpack_jobacct_id(&(*jobacct)->max_pages_id, rpc_version,
			buffer)!= SLURM_SUCCESS)
			goto unpack_error;
		if (_unpack_jobacct_id(&(*jobacct)->min_cpu_id, rpc_version,
			buffer)!= SLURM_SUCCESS)
			goto unpack_error;

	}

	return SLURM_SUCCESS;

unpack_error:
	debug2("jobacctinfo_unpack: unpack_error: size_buf(buffer) %u",
	       size_buf(buffer));
	xfree(*jobacct);
       	return SLURM_ERROR;
}

extern void jobacctinfo_aggregate(jobacctinfo_t *dest, jobacctinfo_t *from)
{
	if (!plugin_polling)
		return;

	xassert(dest);

	if (!from || (from->min_cpu == (uint32_t)NO_VAL))
		return;

	if (dest->max_vsize < from->max_vsize) {
		dest->max_vsize = from->max_vsize;
		dest->max_vsize_id = from->max_vsize_id;
	}
	dest->tot_vsize += from->tot_vsize;

	if (dest->max_rss < from->max_rss) {
		dest->max_rss = from->max_rss;
		dest->max_rss_id = from->max_rss_id;
	}
	dest->tot_rss += from->tot_rss;

	if (dest->max_pages < from->max_pages) {
		dest->max_pages = from->max_pages;
		dest->max_pages_id = from->max_pages_id;
	}
	dest->tot_pages += from->tot_pages;

	if ((dest->min_cpu > from->min_cpu)
	    || (dest->min_cpu == (uint32_t)NO_VAL)) {
		if (from->min_cpu == (uint32_t)NO_VAL)
			from->min_cpu = 0;
		dest->min_cpu = from->min_cpu;
		dest->min_cpu_id = from->min_cpu_id;
	}
	dest->tot_cpu += from->tot_cpu;

	if (dest->max_vsize_id.taskid == (uint16_t)NO_VAL)
		dest->max_vsize_id = from->max_vsize_id;

	if (dest->max_rss_id.taskid == (uint16_t)NO_VAL)
		dest->max_rss_id = from->max_rss_id;

	if (dest->max_pages_id.taskid == (uint16_t)NO_VAL)
		dest->max_pages_id = from->max_pages_id;

	if (dest->min_cpu_id.taskid == (uint16_t)NO_VAL)
		dest->min_cpu_id = from->min_cpu_id;

	dest->user_cpu_sec	+= from->user_cpu_sec;
	dest->user_cpu_usec	+= from->user_cpu_usec;
	while (dest->user_cpu_usec >= 1E6) {
		dest->user_cpu_sec++;
		dest->user_cpu_usec -= 1E6;
	}
	dest->sys_cpu_sec	+= from->sys_cpu_sec;
	dest->sys_cpu_usec	+= from->sys_cpu_usec;
	while (dest->sys_cpu_usec >= 1E6) {
		dest->sys_cpu_sec++;
		dest->sys_cpu_usec -= 1E6;
	}
	dest->act_cpufreq 	+= from->act_cpufreq;
	if (from->energy.consumed_energy == NO_VAL)
		dest->energy.consumed_energy = NO_VAL;
	else
		dest->energy.consumed_energy += from->energy.consumed_energy;
}

extern void jobacctinfo_2_stats(slurmdb_stats_t *stats, jobacctinfo_t *jobacct)
{
	xassert(jobacct);
	xassert(stats);

	stats->vsize_max = jobacct->max_vsize;
	stats->vsize_max_nodeid = jobacct->max_vsize_id.nodeid;
	stats->vsize_max_taskid = jobacct->max_vsize_id.taskid;
	stats->vsize_ave = (double)jobacct->tot_vsize;
	stats->rss_max = jobacct->max_rss;
	stats->rss_max_nodeid = jobacct->max_rss_id.nodeid;
	stats->rss_max_taskid = jobacct->max_rss_id.taskid;
	stats->rss_ave = (double)jobacct->tot_rss;
	stats->pages_max = jobacct->max_pages;
	stats->pages_max_nodeid = jobacct->max_pages_id.nodeid;
	stats->pages_max_taskid = jobacct->max_pages_id.taskid;
	stats->pages_ave = (double)jobacct->tot_pages;
	stats->cpu_min = jobacct->min_cpu;
	stats->cpu_min_nodeid = jobacct->min_cpu_id.nodeid;
	stats->cpu_min_taskid = jobacct->min_cpu_id.taskid;
	stats->cpu_ave = (double)jobacct->tot_cpu;
	stats->act_cpufreq = (double)jobacct->act_cpufreq;
	if (jobacct->energy.consumed_energy == NO_VAL)
		stats->consumed_energy = NO_VAL;
	else
		stats->consumed_energy = (double)jobacct->energy.consumed_energy;
}
