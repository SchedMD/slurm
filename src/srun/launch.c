/****************************************************************************\
 *  launch.c - initiate the user job's tasks.
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2002-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <grondona@llnl.gov>.
 *  UCRL-CODE-226842.
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
static int             fail_launch_cnt = 0;

static void   _print_launch_msg(launch_tasks_request_msg_t *msg, 
		                char * hostname);
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
	int nodeid = NO_VAL;

	update_job_state(job, SRUN_JOB_LAUNCHING);
	
	debug("going to launch %d tasks on %d hosts", 
	      opt.nprocs, job->step_layout->node_cnt);

	my_envc = envcount(environ);
	/* convert timeout from sec to milliseconds */
	opt.msg_timeout *= 1000;
	memset(&r, 0, sizeof(r));

	/* Common message contents */
	r.job_id          = job->jobid;
	r.uid             = opt.uid;
	r.gid             = opt.gid;
	r.argc            = opt.argc;
	r.argv            = opt.argv;
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
	r.task_dist       = opt.distribution;
	r.plane_size      = opt.plane_size;
	r.cpu_bind_type   = opt.cpu_bind_type;
	r.cpu_bind        = opt.cpu_bind;
	r.mem_bind_type   = opt.mem_bind_type;
	r.mem_bind        = opt.mem_bind;
	r.multi_prog      = opt.multi_prog;
	r.options         = job_options_create();
	r.complete_nodelist = xstrdup(job->step_layout->node_list);
	spank_set_remote_options (r.options);

	r.ofname  = fname_remote_string (job->ofname);
	r.efname  = fname_remote_string (job->efname);
	r.ifname  = fname_remote_string (job->ifname);
	r.buffered_stdio = !opt.unbuffered;
	r.pty     = opt.pty;

	r.task_flags = 0;
	if (opt.parallel_debug)
		r.task_flags |= TASK_PARALLEL_DEBUG;
	
	/* Node specific message contents */
	if (mpi_hook_client_single_task_per_node ()) {
		for (i = 0; i < job->step_layout->node_cnt; i++)
			job->step_layout->tasks[i] = 1;
	} 
	r.tasks_to_launch = job->step_layout->tasks;

	r.global_task_ids = job->step_layout->tids;
	r.cpus_allocated  = job->step_layout->tasks;
	r.max_sockets     = opt.max_sockets_per_node;
	r.max_cores       = opt.max_cores_per_socket;
	r.max_threads     = opt.max_threads_per_core;
	r.cpus_per_task   = opt.cpus_per_task;

	r.ntasks_per_node   = opt.ntasks_per_node;
	r.ntasks_per_socket = opt.ntasks_per_socket;
	r.ntasks_per_core   = opt.ntasks_per_core;
	
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
		for(i=0; i<job->step_layout->node_cnt; i++) {
			char *name = nodelist_nth_host(
				job->step_layout->node_list, i);
			_print_launch_msg(&r, name);
			free(name);
		}
	}
	if(!(ret_list = slurm_send_recv_msgs(
		     job->step_layout->node_list,
		     &msg, opt.msg_timeout))) {
		error("slurm_send_recv_msgs failed miserably: %m");
		return NULL;
	}

	ret_itr = list_iterator_create(ret_list);
	while ((ret_data = list_next(ret_itr))) {
		rc = slurm_get_return_code(ret_data->type, 
					   ret_data->data);
		debug("launch returned msg_rc=%d err=%d type=%d",
		      rc, ret_data->err, ret_data->type);
		nodeid = nodelist_find(job->step_layout->node_list,
				       ret_data->node_name);
		
		if(nodeid >= job->step_layout->node_cnt) {
			/* Make sure we aren't trying to mark
			something we haven't requested but was
			included in the nodelist.  This should never
			happen */
			error("got a problem with a non requested "
			      "node %s(%d): %s",
			      ret_data->node_name, nodeid, 
			      slurm_strerror(rc));
		} else if (rc != SLURM_SUCCESS) {
			slurm_seterrno(rc);
			error("Task launch failed on node %s(%d): %s",
			      ret_data->node_name, nodeid, 
			      slurm_strerror(rc));
			_update_failed_node(job, nodeid);
			fail_launch_cnt++;
		} else {
#if 0 /* only for debugging, might want to make this a callback */
			slurm_seterrno(rc);
			info("Launch success on node %s(%d)",
			     ret_data->node_name, nodeid);
#endif
			_update_contacted_node(job, nodeid);
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
	xfree(r.complete_nodelist);
		
	return(void *)(0);
}

static void
_update_failed_node(srun_job_t *j, int id)
{
	int i;
	
	pthread_mutex_lock(&j->task_mutex);
	if (j->host_state[id] == SRUN_HOST_INIT) {
		j->host_state[id] = SRUN_HOST_UNREACHABLE;
	}

	for (i = 0; i < j->step_layout->tasks[id]; i++) {
		j->task_state[j->step_layout->tids[id][i]] = SRUN_TASK_FAILED;
	}
	pthread_mutex_unlock(&j->task_mutex);

	/* update_failed_tasks(j, id); */
}

static void
_update_contacted_node(srun_job_t *j, int id)
{
	pthread_mutex_lock(&j->task_mutex);
	if (j->host_state[id] == SRUN_HOST_INIT) {
		j->host_state[id] = SRUN_HOST_CONTACTED;
	}
	pthread_mutex_unlock(&j->task_mutex);
}

static void 
_print_launch_msg(launch_tasks_request_msg_t *msg, char * hostname)
{
	int i;
	char tmp_str[10], task_list[4096];
	int nodeid = nodelist_find(msg->complete_nodelist, hostname);
	
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
