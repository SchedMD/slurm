/****************************************************************************\
 *  launch.c - initiate the user job's tasks.
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2002-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <grondona@llnl.gov>.
 *  UCRL-CODE-217948.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
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

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/param.h>

#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/hostlist.h"
#include "src/common/plugstack.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_interface.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/xsignal.h"
#include "src/common/forward.h"
#include "src/common/mpi.h"
#include "src/api/step_io.h"

#include "src/srun/srun_job.h"
#include "src/srun/launch.h"
#include "src/srun/opt.h"

#define MAX_RETRIES 3

extern char **environ;

/* number of active threads */
static pthread_mutex_t active_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  active_cond  = PTHREAD_COND_INITIALIZER;
static int             active = 0;
static int             joinable = 0;
static int             fail_launch_cnt = 0;

typedef enum {DSH_NEW, DSH_ACTIVE, DSH_DONE, DSH_FAILED, DSH_JOINED} state_t;

typedef struct launch_info {
	slurm_msg_t *req;
	srun_job_t *job;
} launch_info_t;

typedef struct thd {
        pthread_t	thread;			/* thread ID */
        state_t		state;      		/* thread state */
	time_t          tstart;			/* time thread started */
	launch_info_t   task;
} thd_t;

static int    _check_pending_threads(thd_t *thd, int count);
static void   _spawn_launch_thr(thd_t *th);
static int    _wait_on_active(thd_t *thd, srun_job_t *job);
static void   _p_launch(slurm_msg_t *req_array_ptr, srun_job_t *job);
static void * _p_launch_task(void *args);
static void   _print_launch_msg(launch_tasks_request_msg_t *msg, 
		                char * hostname, int nodeid);
static void   _update_failed_node(srun_job_t *j, int id);
static void   _update_contacted_node(srun_job_t *j, int id);

int 
launch_thr_create(srun_job_t *job)
{
	int e, retries = 0;
	pthread_attr_t attr;

	slurm_attr_init(&attr);
	while ((e = pthread_create(&job->lid, &attr, &launch, (void *) job))) {
		if (++retries > MAX_RETRIES) {
			error ("pthread_create error %m");
			slurm_attr_destroy(&attr);
			slurm_seterrno_ret(e);
		}
		sleep(1);	/* sleep and try again */
	}
	slurm_attr_destroy(&attr);

	debug("Started launch thread (%lu)", (unsigned long) job->lid);

	return SLURM_SUCCESS;
}

void *
launch(void *arg)
{
	launch_tasks_request_msg_t r;
	srun_job_t *job = (srun_job_t *) arg;
	int i, my_envc;
	slurm_msg_t msg;
	ret_data_info_t *ret_data = NULL;
	List ret_list = NULL;
	ListIterator ret_itr;
	int rc = SLURM_SUCCESS;

	update_job_state(job, SRUN_JOB_LAUNCHING);
	
	debug("going to launch %d tasks on %d hosts", 
	      opt.nprocs, job->step_layout->node_cnt);

	my_envc = envcount(environ);
	/* convert timeout from sec to milliseconds */
	opt.msg_timeout *= 1000;
	/* Common message contents */
	r.job_id          = job->jobid;
	r.uid             = opt.uid;
	r.gid             = opt.gid;
	r.argc            = remote_argc;
	r.argv            = remote_argv;
	r.cred            = job->cred;
	r.job_step_id     = job->stepid;
	r.envc            = my_envc;
	r.env             = environ;
	r.cwd             = opt.cwd;
	r.nnodes          = job->step_layout->node_cnt;
	r.nprocs          = opt.nprocs;
	r.slurmd_debug    = opt.slurmd_debug;
	r.switch_job      = job->switch_job;
	r.task_prolog     = opt.task_prolog;
	r.task_epilog     = opt.task_epilog;
	r.cpu_bind_type   = opt.cpu_bind_type;
	r.cpu_bind        = opt.cpu_bind;
	r.mem_bind_type   = opt.mem_bind_type;
	r.mem_bind        = opt.mem_bind;
	r.multi_prog      = opt.multi_prog;
	r.options         = job_options_create();

	spank_set_remote_options (r.options);

	r.ofname  = fname_remote_string (job->ofname);
	r.efname  = fname_remote_string (job->efname);
	r.ifname  = fname_remote_string (job->ifname);
	r.buffered_stdio = !opt.unbuffered;
	
	if (opt.parallel_debug)
		r.task_flags |= TASK_PARALLEL_DEBUG;
	
	/* Node specific message contents */
	if (slurm_mpi_single_task_per_node ()) {
		for (i = 0; i < job->step_layout->node_cnt; i++)
			job->step_layout->tasks[i] = 1;
	} 
	r.tasks_to_launch = job->step_layout->tasks;

	r.global_task_ids = job->step_layout->tids;
	r.cpus_allocated  = job->step_layout->tasks;
	
	r.num_resp_port = job->njfds;
	r.resp_port = xmalloc(sizeof(uint16_t) * r.num_resp_port);
	for (i = 0; i < r.num_resp_port; i++) {
		r.resp_port[i] = ntohs(job->jaddr[i].sin_port);
	}

	r.num_io_port = job->client_io->num_listen;
	r.io_port = xmalloc(sizeof(uint16_t) * r.num_io_port);
	for (i = 0; i < r.num_io_port; i++) {
		r.io_port[i] = job->client_io->listenport[i];
	}

	
	
	//hostlist = hostlist_create(job->nodelist);
	debug("sending to list %s", job->step_layout->node_list);
	
	slurm_msg_t_init(&msg);
	msg.msg_type        = REQUEST_LAUNCH_TASKS;
	msg.data            = &r;
	
	if (_verbose) {
		char *name = nodelist_nth_host(job->step_layout->node_list, 0);
		_print_launch_msg(&r, name, 0);
		free(name);
	}
	if(!(ret_list = slurm_send_recv_msgs(
		     job->step_layout->node_list,
		     &msg, 0, opt.msg_timeout))) {
		error("slurm_send_recv_msgs failed miserably: %m");
		return NULL;
	}

	ret_itr = list_iterator_create(ret_list);
	while ((ret_data = list_next(ret_itr))) {
		rc = slurm_get_return_code(ret_data->type, 
					   ret_data->data);
		debug("launch returned msg_rc=%d err=%d type=%d",
		      rc, ret_data->err, ret_data->type);
		if (rc != SLURM_SUCCESS) {
			errno = ret_data->err;
			error("Task launch failed on node %s(%d): %m",
			      ret_data->node_name, ret_data->nodeid);
			_update_failed_node(job, ret_data->nodeid);
			fail_launch_cnt++;
		} else {
#if 0 /* only for debugging, might want to make this a callback */
			errno = ret_data->err;
			info("Launch success on node %s(%d)",
			     ret_data->node_name, ret_data->nodeid);
#endif
			_update_contacted_node(job, 
					       ret_data->nodeid);
		}
	}
	list_iterator_destroy(ret_itr);
	list_destroy(ret_list);
	
	if (fail_launch_cnt) {
		srun_job_state_t jstate;

		slurm_mutex_lock(&job->state_mutex);
		jstate = job->state;
		slurm_mutex_unlock(&job->state_mutex);

		if (jstate < SRUN_JOB_TERMINATED) {
			error("%d launch request%s failed", 
			      fail_launch_cnt, fail_launch_cnt > 1 ? "s" : "");
			job->rc = 124;
			srun_job_kill(job);
		}

	} else {
		debug("All task launch requests sent");
		update_job_state(job, SRUN_JOB_STARTING);
	}
	xfree(r.io_port);
	xfree(r.resp_port);
		
	return(void *)(0);
}

static int _check_pending_threads(thd_t *thd, int count)
{
	int i;
	time_t now = time(NULL);

	for (i = 0; i < count; i++) {
		thd_t *tp = &thd[i];
		if ((tp->state == DSH_ACTIVE) && ((now - tp->tstart) >= 10) ) {
			debug2("sending SIGALRM to thread %lu", 
				(unsigned long) tp->thread);
			/*
			 * XXX: sending sigalrm to threads *seems* to
			 *  generate problems with the pthread_manager
			 *  thread. Disable this signal for now
			 * pthread_kill(tp->thread, SIGALRM);
			 */
		}
	}

	return 0;
}

/*
 * When running under parallel debugger, do not create threads in 
 *  detached state, as this seems to confuse TotalView specifically
 */
static void _set_attr_detached (pthread_attr_t *attr)
{
	int err;
	if (opt.parallel_debug) {
		return;
	}
	if ((err = pthread_attr_setdetachstate(attr, PTHREAD_CREATE_DETACHED)))
		error ("pthread_attr_setdetachstate: %s", slurm_strerror(err));
	return;
}


/*
 * Need to join with all attached threads if running
 *  under parallel debugger
 */
static void _join_attached_threads (int nthreads, thd_t *th)
{
	int i;
	void *retval;
	if (opt.parallel_debug) {
		for (i = 0; i < nthreads; i++) {
			if (th[i].thread != (pthread_t) NULL
			    && th[i].state == DSH_DONE) {
				pthread_join (th[i].thread, &retval);
				th[i].state = DSH_JOINED;
			}
		}
	}

	return;
}


static void _spawn_launch_thr(thd_t *th)
{
	pthread_attr_t attr;
	int err = 0;

	slurm_attr_init (&attr);
	_set_attr_detached (&attr);

	err = pthread_create(&th->thread, &attr, _p_launch_task, (void *)th);
	slurm_attr_destroy(&attr);
	if (err) {
		error ("pthread_create: %s", slurm_strerror(err));

		/* just run it under this thread */
		_p_launch_task((void *) th);
	}

	return;
}

static int _wait_on_active(thd_t *thd, srun_job_t *job)
{
	struct timeval now;
	struct timespec timeout;
	int rc;

	gettimeofday(&now, NULL);
	timeout.tv_sec  = now.tv_sec + 1;
	timeout.tv_nsec = now.tv_usec * 1000;

	rc = pthread_cond_timedwait( &active_cond, 
			             &active_mutex,
			             &timeout      );

	if (rc == ETIMEDOUT)
		_check_pending_threads(thd, job->thr_count);

	return rc;
}

/* _p_launch - parallel (multi-threaded) task launcher */
static void _p_launch(slurm_msg_t *req, srun_job_t *job)
{
	int i;
	thd_t *thd;
	int rc = 0;
	char *name = NULL;

	/*
	 * SigFunc *oldh;
	 * sigset_t set;
	 * 
	 * oldh = xsignal(SIGALRM, (SigFunc *) _alrm_handler);
	 * xsignal_save_mask(&set);
	 * xsignal_unblock(SIGALRM);
	 */

	/*
	 * Set job timeout to maximum launch time + current time
	 */
	job->ltimeout = time(NULL) + opt.max_launch_time;
	debug2("got thr_count of %d", job->thr_count);
	thd = xmalloc (job->thr_count * sizeof (thd_t));
	for (i = 0; i < job->thr_count; i++) {
		if (job->step_layout->tasks[i] == 0)	{
			/* No tasks for this node */
			name = nodelist_nth_host(job->step_layout->node_list,
						 i);
			debug("Node %s is unused",name);
			free(name);
			job->host_state[i] = SRUN_HOST_REPLIED;
			thd[i].thread = (pthread_t) NULL;
			continue;
		}

		if (job->state > SRUN_JOB_LAUNCHING)
			break;

		pthread_mutex_lock(&active_mutex);
		while (active >= opt.max_threads || rc < 0) 
			rc = _wait_on_active(thd, job);
		if (joinable >= (opt.max_threads/2))
			_join_attached_threads(job->thr_count, 
					       thd);
		active++;
		pthread_mutex_unlock(&active_mutex);

		thd[i].task.req = &req[i];
		thd[i].task.job = job;

		_spawn_launch_thr(&thd[i]);
	}
	for ( ; i < job->step_layout->node_cnt; i++)
		_update_failed_node(job, i);

	pthread_mutex_lock(&active_mutex);
	while (active > 0) 
		_wait_on_active(thd, job);
	pthread_mutex_unlock(&active_mutex);

	_join_attached_threads (job->thr_count, thd);

	/*
	 * xsignal_restore_mask(&set);
	 * xsignal(SIGALRM, oldh);
	 */

	xfree(thd);
}

static void
_update_failed_node(srun_job_t *j, int id)
{
	int i;
	pipe_enum_t pipe_enum = PIPE_HOST_STATE;
	
	pthread_mutex_lock(&j->task_mutex);
	if (j->host_state[id] == SRUN_HOST_INIT) {
		j->host_state[id] = SRUN_HOST_UNREACHABLE;

		if(message_thread) {
			safe_write(j->forked_msg->par_msg->msg_pipe[1],
			      &pipe_enum,sizeof(int));
			safe_write(j->forked_msg->par_msg->msg_pipe[1],
			      &id,sizeof(int));
			safe_write(j->forked_msg->par_msg->msg_pipe[1],
			      &j->host_state[id],sizeof(int));
		}
	}

	pipe_enum = PIPE_TASK_STATE;
	for (i = 0; i < j->step_layout->tasks[id]; i++) {
		j->task_state[j->step_layout->tids[id][i]] = SRUN_TASK_FAILED;

		if(message_thread) {
			safe_write(j->forked_msg->par_msg->msg_pipe[1],
				   &pipe_enum, sizeof(int));
			safe_write(j->forked_msg->par_msg->msg_pipe[1],
				   &j->step_layout->tids[id][i], sizeof(int));
			safe_write(j->forked_msg->par_msg->msg_pipe[1],
				   &j->task_state[j->step_layout->tids[id][i]],
				   sizeof(int));
		}
	}
	pthread_mutex_unlock(&j->task_mutex);

	/* update_failed_tasks(j, id); */
	return;
rwfail:
	pthread_mutex_unlock(&j->task_mutex);
	error("_update_failed_node: "
	      "write from srun message-handler process failed");
}

static void
_update_contacted_node(srun_job_t *j, int id)
{
	pipe_enum_t pipe_enum = PIPE_HOST_STATE;
	
	pthread_mutex_lock(&j->task_mutex);
	if (j->host_state[id] == SRUN_HOST_INIT) {
		j->host_state[id] = SRUN_HOST_CONTACTED;
		if(message_thread) {
			safe_write(j->forked_msg->par_msg->msg_pipe[1],
				   &pipe_enum, sizeof(int));
			safe_write(j->forked_msg->par_msg->msg_pipe[1],
				   &id, sizeof(int));
			safe_write(j->forked_msg->par_msg->msg_pipe[1],
				   &j->host_state[id], sizeof(int));
		}
	}
	pthread_mutex_unlock(&j->task_mutex);
	return;
rwfail:
	pthread_mutex_unlock(&j->task_mutex);
	error("_update_contacted_node: "
	      "write from srun message-handler process failed");
}


/* _p_launch_task - parallelized launch of a specific task */
static void * _p_launch_task(void *arg)
{
	thd_t                      *th     = (thd_t *)arg;
	launch_info_t              *tp     = &(th->task);
	slurm_msg_t                *req    = tp->req;
	launch_tasks_request_msg_t *msg    = req->data;
	srun_job_t                 *job    = tp->job;
	int                        nodeid  = req->srun_node_id;
	int                        retry   = 3; /* retry thrice */
	List ret_list = NULL;
	ListIterator itr;
	ret_data_info_t *ret_data_info = NULL;
	char *name = NULL;
	int rc = SLURM_SUCCESS;
	
	th->state  = DSH_ACTIVE;
	th->tstart = time(NULL);
	
	if (_verbose) {
		name = nodelist_nth_host(job->step_layout->node_list, nodeid);
		_print_launch_msg(msg, name, nodeid);
		free(name);
	}
again:
	//ret_list = slurm_send_recv_rc_msg(req, opt.msg_timeout);
	ret_list = slurm_send_recv_packed_msg(job->step_layout->node_list, req,
					      0, opt.msg_timeout);
	if(!ret_list) {
		th->state = DSH_FAILED;
			
		goto cleanup;
	}
	itr = list_iterator_create(ret_list);		
	while((ret_data_info = list_next(itr))) {
		rc = slurm_get_return_code(ret_data_info->type, 
					   ret_data_info->data);
		if(!ret_data_info->node_name) {
			name = nodelist_nth_host(
				job->step_layout->node_list, 
				ret_data_info->nodeid);
			ret_data_info->node_name = xstrdup(name);
			free(name);
		}
		if(rc == SLURM_SUCCESS) {
			_update_contacted_node(job, 
					       ret_data_info->nodeid);
			continue;
		}
		
		errno = ret_data_info->err;
		if (errno != EINTR) 
			verbose("first launch error on %s: %m",
				ret_data_info->node_name);
		
		if ((errno != ETIMEDOUT) 
		    && (job->state == SRUN_JOB_LAUNCHING)
		    && (errno != ESLURMD_INVALID_JOB_CREDENTIAL) 
		    &&  ((retry--) > 0)) {
			list_iterator_destroy(itr);
			list_destroy(ret_list);	
			sleep(1);
			goto again;
		}
		name = nodelist_nth_host(job->step_layout->node_list, 
					 ret_data_info->nodeid);
		
		if (errno == EINTR)
			verbose("launch on %s canceled", 
				ret_data_info->node_name);
		else
			error("second launch error on %s: %m", 
			      ret_data_info->node_name);
		
		_update_failed_node(job, ret_data_info->nodeid);
		
		th->state = DSH_FAILED;
		
		pthread_mutex_lock(&active_mutex);
		fail_launch_cnt++;
		pthread_mutex_unlock(&active_mutex);
	}
	list_iterator_destroy(itr);
	list_destroy(ret_list);	
cleanup:
	destroy_forward(&req->forward);
	pthread_mutex_lock(&active_mutex);
	th->state = DSH_DONE;
	active--;
	if (opt.parallel_debug)
		joinable++;
	pthread_cond_signal(&active_cond);
	pthread_mutex_unlock(&active_mutex);
	
	return NULL;
}


static void 
_print_launch_msg(launch_tasks_request_msg_t *msg, char * hostname, int nodeid)
{
	int i;
	char tmp_str[10], task_list[4096];

	if (opt.distribution == SLURM_DIST_BLOCK) {
		sprintf(task_list, "%u-%u", 
		        msg->global_task_ids[nodeid][0],
			msg->global_task_ids[nodeid]
			[(msg->tasks_to_launch[nodeid]-1)]);
	} else {
		for (i=0; i<msg->tasks_to_launch[nodeid]; i++) {
			sprintf(tmp_str, ",%u", 
				msg->global_task_ids[nodeid][i]);
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
	     msg->tasks_to_launch[nodeid], task_list);

	debug3("uid:%ld gid:%ld cwd:%s %d", (long) msg->uid,
		(long) msg->gid, msg->cwd, nodeid);
}
