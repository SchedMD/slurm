/****************************************************************************\
 *  launch.c - initiate the user job's tasks.
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <grondona@llnl.gov>.
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

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <sys/param.h>

#include "src/common/log.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"

#include "src/srun/job.h"
#include "src/srun/launch.h"
#include "src/srun/opt.h"

extern char **environ;

/* number of active threads */
static pthread_mutex_t active_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  active_cond  = PTHREAD_COND_INITIALIZER;
static int             active = 0;
static int             fail_launch_cnt = 0;

typedef enum {DSH_NEW, DSH_ACTIVE, DSH_DONE, DSH_FAILED} state_t;

typedef struct thd {
        pthread_t	thread;			/* thread ID */
        pthread_attr_t	attr;			/* thread attributes */
        state_t		state;      		/* thread state */
} thd_t;

typedef struct task_info {
	slurm_msg_t *req_ptr;
	job_t *job_ptr;
} task_info_t;

static void   _dist_block(job_t *job);
static void   _dist_cyclic(job_t *job);
static void   _p_launch(slurm_msg_t *req_array_ptr, job_t *job);
static void * _p_launch_task(void *args);
static void   _print_launch_msg(launch_tasks_request_msg_t *msg, 
		                char * hostname);
static int    _envcount(char **env);

static void
_dist_block(job_t *job)
{
	int i, j, taskid = 0;
	for (i=0; ((i<job->nhosts) && (taskid<opt.nprocs)); i++) {
		for (j=0; (((j*opt.cpus_per_task)<job->cpus[i]) && 
					(taskid<opt.nprocs)); j++) {
			job->hostid[taskid] = i;
			job->tids[i][j]     = taskid++;
			job->ntask[i]++;
		}
	}
}

static void
_dist_cyclic(job_t *job)
{
	int i, j, taskid = 0;
	for (j=0; (taskid<opt.nprocs); j++) {	/* cycle counter */
		for (i=0; ((i<job->nhosts) && (taskid<opt.nprocs)); i++) {
			if (j < job->cpus[i]) {
				job->hostid[taskid] = i;
				job->tids[i][j]     = taskid++;
				job->ntask[i]++;
			}
		}
	}
}

int 
launch_thr_create(job_t *job)
{
	int e;
	pthread_attr_t attr;

	pthread_attr_init(&attr);
	if ((e = pthread_create(&job->lid, &attr, &launch, (void *) job))) 
		slurm_seterrno_ret(e);

	debug("Started launch thread (%d)", job->lid);

	return SLURM_SUCCESS;
}

void *
launch(void *arg)
{
	slurm_msg_t *req_array_ptr;
	launch_tasks_request_msg_t *msg_array_ptr;
	job_t *job = (job_t *) arg;
	int i, my_envc;
	char hostname[MAXHOSTNAMELEN];

	update_job_state(job, SRUN_JOB_LAUNCHING);
	if (gethostname(hostname, MAXHOSTNAMELEN) < 0)
		error("gethostname: %m");

	debug("going to launch %d tasks on %d hosts", opt.nprocs, job->nhosts);
	debug("sending to slurmd port %d", slurm_get_slurmd_port());

	/* Build task id list for each host */
	job->tids   = xmalloc(job->nhosts * sizeof(uint32_t *));
	job->hostid = xmalloc(opt.nprocs  * sizeof(uint32_t));
	for (i = 0; i < job->nhosts; i++)
		job->tids[i] = xmalloc(job->cpus[i] * sizeof(uint32_t));

	if (opt.distribution == SRUN_DIST_UNKNOWN) {
		if (opt.nprocs <= job->nhosts)
			opt.distribution = SRUN_DIST_CYCLIC;
		else
			opt.distribution = SRUN_DIST_BLOCK;
	}

	if (opt.distribution == SRUN_DIST_BLOCK)
		_dist_block(job);
	else  
		_dist_cyclic(job);

	msg_array_ptr = 
		xmalloc(sizeof(launch_tasks_request_msg_t)*job->nhosts);
	req_array_ptr = xmalloc(sizeof(slurm_msg_t) * job->nhosts);
	my_envc = _envcount(environ);
	for (i = 0; i < job->nhosts; i++) {
		launch_tasks_request_msg_t *r = &msg_array_ptr[i];
		slurm_msg_t                *m = &req_array_ptr[i];

		/* Common message contents */
		r->job_id          = job->jobid;
		r->uid             = opt.uid;
		r->argc            = remote_argc;
		r->argv            = remote_argv;
		r->cred            = job->cred;
		r->job_step_id     = job->stepid;
		r->envc            = my_envc;
		r->env             = environ;
		r->cwd             = opt.cwd;
		r->nnodes          = job->nhosts;
		r->nprocs          = opt.nprocs;
		r->slurmd_debug    = opt.slurmd_debug;

		if (job->ofname->type == IO_PER_TASK)
			r->ofname  = job->ofname->name;
		if (job->efname->type == IO_PER_TASK)
			r->efname  = job->efname->name;
		if (job->ifname->type == IO_PER_TASK)
			r->ifname  = job->ifname->name;

		/* Node specific message contents */
		r->tasks_to_launch = job->ntask[i];
		r->global_task_ids = job->tids[i];
		r->srun_node_id    = (uint32_t)i;
		r->io_port         = ntohs(job->ioport[i%job->niofds]);
		r->resp_port       = ntohs(job->jaddr[i%job->njfds].sin_port);
		m->msg_type        = REQUEST_LAUNCH_TASKS;
		m->data            = &msg_array_ptr[i];
		memcpy(&m->address, &job->slurmd_addr[i], sizeof(slurm_addr));

#ifdef HAVE_LIBELAN3
		r->qsw_job = job->qsw_job;
#endif

#ifdef HAVE_TOTALVIEW
		if (opt.totalview)
			r->task_flags |= TASK_TOTALVIEW_DEBUG;
#endif

	}

	_p_launch(req_array_ptr, job);

	xfree(msg_array_ptr);
	xfree(req_array_ptr);

	if (fail_launch_cnt) {
		error("%d task launch requests failed, terminating job step", 
		      fail_launch_cnt);
		job->rc = 124;
		job_kill(job);
	} else {
		debug("All task launch requests sent");
		update_job_state(job, SRUN_JOB_STARTING);
	}

	return(void *)(0);
}

/* _p_launch - parallel (multi-threaded) task launcher */
static void _p_launch(slurm_msg_t *req_array_ptr, job_t *job)
{
	int i;
	task_info_t *task_info_ptr;
	thd_t *thread_ptr;

	thread_ptr = xmalloc (job->nhosts * sizeof (thd_t));
	for (i = 0; i < job->nhosts; i++) {
		if (job->ntask[i] == 0)	{	/* No tasks for this node */
			debug("Node %s is unused",job->host[i]);
			continue;
		}

		pthread_mutex_lock(&active_mutex);
		while (active >= opt.max_threads) {
			pthread_cond_wait(&active_cond, &active_mutex);
		}
		active++;
		pthread_mutex_unlock(&active_mutex);

		task_info_ptr = (task_info_t *)xmalloc(sizeof(task_info_t));
		task_info_ptr->req_ptr = &req_array_ptr[i];
		task_info_ptr->job_ptr = job;

		if (pthread_attr_init (&thread_ptr[i].attr))
			fatal ("pthread_attr_init error %m");
		if (pthread_attr_setdetachstate (&thread_ptr[i].attr, 
						 PTHREAD_CREATE_DETACHED))
			error ("pthread_attr_setdetachstate error %m");
#ifdef PTHREAD_SCOPE_SYSTEM
		if (pthread_attr_setscope (&thread_ptr[i].attr, 
					   PTHREAD_SCOPE_SYSTEM))
			error ("pthread_attr_setscope error %m");
#endif
		if ( pthread_create (	&thread_ptr[i].thread, 
		                        &thread_ptr[i].attr, 
		                        _p_launch_task, 
		                        (void *) task_info_ptr) ) {
			error ("pthread_create error %m");
			/* just run it under this thread */
			_p_launch_task((void *) task_info_ptr);
		}

	}

	pthread_mutex_lock(&active_mutex);
	while (active > 0) {
		pthread_cond_wait(&active_cond, &active_mutex);
	}
	pthread_mutex_unlock(&active_mutex);
	xfree(thread_ptr);
}

static int
_send_msg_rc(slurm_msg_t *msg)
{
	slurm_msg_t        resp;
	return_code_msg_t *rcmsg   = NULL;
	int		   rc      = 0;

       	if ((rc = slurm_send_recv_node_msg(msg, &resp)) < 0) 
		return rc;

	switch (resp.msg_type) {
	case RESPONSE_SLURM_RC:
		rcmsg = resp.data;
		rc = rcmsg->return_code;
		slurm_free_return_code_msg(rcmsg);
		break;
	default:
		error("recvd msg type %d. expected %d", resp.msg_type, 
				RESPONSE_SLURM_RC);
		rc = SLURM_UNEXPECTED_MSG_ERROR;
	}

	slurm_seterrno_ret (rc);
}

static void
_update_failed_node(job_t *j, int id)
{
	pthread_mutex_lock(&j->task_mutex);
	if (j->host_state[id] == SRUN_HOST_INIT)
		j->host_state[id] = SRUN_HOST_UNREACHABLE;
	pthread_mutex_unlock(&j->task_mutex);

	/* update_failed_tasks(j, id); */
}

static void
_update_contacted_node(job_t *j, int id)
{
	pthread_mutex_lock(&j->task_mutex);
	if (j->host_state[id] == SRUN_HOST_INIT)
		j->host_state[id] = SRUN_HOST_CONTACTED;
	pthread_mutex_unlock(&j->task_mutex);
}


/* _p_launch_task - parallelized launch of a specific task */
static void * _p_launch_task(void *arg)
{
	task_info_t                *tp     = (task_info_t *)arg;
	slurm_msg_t                *req    = tp->req_ptr;
	launch_tasks_request_msg_t *msg    = req->data;
	job_t                      *job    = tp->job_ptr;
	int                        nodeid  = msg->srun_node_id;
	int                        failure = 0;
	int                        retry   = 3; /* retry thrice */

	if (_verbose)
	        _print_launch_msg(msg, job->host[nodeid]);

    again:
	if  (_send_msg_rc(req) < 0) {	/* Has timeout */

		error("launch error on %s: %m", job->host[nodeid]);
		if ((errno != ETIMEDOUT) && retry--) {
			sleep(1);
			goto again;
		}

		_update_failed_node(job, nodeid);
		failure = 1;

	} else 
		_update_contacted_node(job, nodeid);


	pthread_mutex_lock(&active_mutex);
	active--;
	fail_launch_cnt += failure;
	pthread_cond_signal(&active_cond);
	pthread_mutex_unlock(&active_mutex);

	xfree(arg);
	return NULL;
}


static void 
_print_launch_msg(launch_tasks_request_msg_t *msg, char * hostname)
{
	int i;
	char tmp_str[10], task_list[4096];

	if (opt.distribution == SRUN_DIST_BLOCK) {
		sprintf(task_list, "%u-%u", 
		        msg->global_task_ids[0],
			msg->global_task_ids[(msg->tasks_to_launch-1)]);
	} else {
		for (i=0; i<msg->tasks_to_launch; i++) {
			sprintf(tmp_str, ",%u", msg->global_task_ids[i]);
			if (i == 0)
				strcpy(task_list, &tmp_str[1]);
			else if ((strlen(tmp_str) + strlen(task_list)) < 
			         sizeof(task_list))
				strcat(task_list, tmp_str);
			else
				break;
		}
	}

	info("launching %u.%u on host %s, %u tasks: %s", 
	     msg->job_id, msg->job_step_id, hostname, 
	     msg->tasks_to_launch, task_list);

	debug3("uid:%ld cwd:%s %d",
		(long) msg->uid, msg->cwd, msg->srun_node_id);
}

static int
_envcount(char **environ)
{
	int envc = 0;
	while (environ[envc] != NULL)
		envc++;
	return envc;
}
