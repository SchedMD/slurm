/****************************************************************************\
 *  launch.c - initiate the user job's tasks.
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
#include <config.h>
#endif

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/param.h>

#include <src/common/xmalloc.h>
#include <src/common/log.h>
#include <src/common/slurm_protocol_api.h>

#include <src/srun/job.h>
#include <src/srun/launch.h>
#include <src/srun/opt.h>

extern char **environ;

/* number of active threads */
static pthread_mutex_t active_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  active_cond  = PTHREAD_COND_INITIALIZER;
static int             active = 0;

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

static void p_launch(slurm_msg_t *req_array_ptr, job_t *job);
static void * p_launch_task(void *args);
static void print_launch_msg(launch_tasks_request_msg_t *msg);
static int  envcount(char **env);

void *
launch(void *arg)
{
	slurm_msg_t *req_array_ptr;
	launch_tasks_request_msg_t *msg_array_ptr;
	job_t *job = (job_t *) arg;
	int i, j, k, my_envc, taskid;
	char hostname[MAXHOSTNAMELEN];
	uint32_t **task_ids;

	update_job_state(job, SRUN_JOB_LAUNCHING);

	if (gethostname(hostname, MAXHOSTNAMELEN) < 0)
		error("gethostname: %m");

	debug("going to launch %d tasks on %d hosts", opt.nprocs, job->nhosts);
	debug("sending to slurmd port %d", slurm_get_slurmd_port());

	/* Build task id list for each host */
	task_ids = (uint32_t **) xmalloc(job->nhosts * sizeof(uint32_t *));
	for (i = 0; i < job->nhosts; i++)
		task_ids[i] = (uint32_t *) xmalloc(job->cpus[i]*sizeof(uint32_t));
	taskid = 0;
	if (opt.distribution == SRUN_DIST_BLOCK) {
		for (i=0; ((i<job->nhosts) && (taskid<opt.nprocs)); i++) {
			for (j=0; (((j*opt.cpus_per_task)<job->cpus[i]) && 
			           (taskid<opt.nprocs)); j++) {
				task_ids[i][j] = taskid++;
				job->ntask[i]++;
			}
		}
	} else {	/*  (opt.distribution == SRUN_DIST_CYCLIC) */
		for (k=0; (taskid<opt.nprocs); k++) {	/* cycle counter */
			for (i=0; ((i<job->nhosts) && (taskid<opt.nprocs)); i++) {
				if (k < job->cpus[i]) {
					task_ids[i][k] = taskid++;
					job->ntask[i]++;
				}
			}
		}
	}

	msg_array_ptr = (launch_tasks_request_msg_t *) 
			xmalloc(sizeof(launch_tasks_request_msg_t) * job->nhosts);
	req_array_ptr = (slurm_msg_t *) 
			xmalloc(sizeof(slurm_msg_t) * job->nhosts);
	my_envc = envcount(environ);
	for (i = 0; i < job->nhosts; i++) {
		/* Common message contents */
		msg_array_ptr[i].job_id = job->jobid;
		msg_array_ptr[i].uid = opt.uid;
		msg_array_ptr[i].argc = remote_argc;
		msg_array_ptr[i].argv = remote_argv;
		msg_array_ptr[i].credential = job->cred;
		msg_array_ptr[i].job_step_id = job->stepid;
		msg_array_ptr[i].envc = my_envc;
		msg_array_ptr[i].env = environ;
		msg_array_ptr[i].cwd = opt.cwd;
		msg_array_ptr[i].nnodes = job->nhosts;
		msg_array_ptr[i].nprocs = opt.nprocs;
#if HAVE_LIBELAN3
		msg_array_ptr[i].qsw_job = job->qsw_job;
#endif 

		/* Node specific message contents */
		msg_array_ptr[i].tasks_to_launch = job->ntask[i];
		msg_array_ptr[i].global_task_ids = task_ids[i];
		msg_array_ptr[i].srun_node_id    = (uint32_t)i;
		msg_array_ptr[i].io_port         = ntohs(job->ioport[i%job->niofds]);
		msg_array_ptr[i].resp_port       = ntohs(job->jaddr[i%job->njfds].sin_port);

		req_array_ptr[i].msg_type = REQUEST_LAUNCH_TASKS;
		req_array_ptr[i].data     = &msg_array_ptr[i];
		memcpy(&req_array_ptr[i].address, &job->slurmd_addr[i], sizeof(slurm_addr));
	}

	p_launch(req_array_ptr, job);

	debug("All tasks have been launched");
	update_job_state(job, SRUN_JOB_STARTING);

	for (i = 0; i < job->nhosts; i++)
		xfree(task_ids[i]);
	xfree(task_ids);
	xfree(msg_array_ptr);
	xfree(req_array_ptr);

	return(void *)(0);

}

/* p_launch - parallel (multi-threaded) task launcher */
static void p_launch(slurm_msg_t *req_array_ptr, job_t *job)
{
	int i;
	task_info_t *task_info_ptr;
	thd_t *thread_ptr;

	if (opt.max_threads > job->nhosts)	/* don't need more threads than tasks */
		opt.max_threads = job->nhosts;

	thread_ptr = xmalloc (job->nhosts * sizeof (thd_t));
	for (i = 0; i < job->nhosts; i++) {
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
		if (pthread_attr_setdetachstate (&thread_ptr[i].attr, PTHREAD_CREATE_DETACHED))
			error ("pthread_attr_setdetachstate error %m");
#ifdef PTHREAD_SCOPE_SYSTEM
		if (pthread_attr_setscope (&thread_ptr[i].attr, PTHREAD_SCOPE_SYSTEM))
			error ("pthread_attr_setscope error %m");
#endif
		while ( pthread_create (&thread_ptr[i].thread, 
		                        &thread_ptr[i].attr, 
		                        p_launch_task, 
		                        (void *) task_info_ptr) ) {
			error ("pthread_create error %m");
			/* just run it under this thread */
			p_launch_task((void *) task_info_ptr);
		}
	}

	while (active > 0) {
		pthread_cond_wait(&active_cond, &active_mutex);
	}
	xfree(thread_ptr);
}

/* p_launch_task - parallelized launch of a specific task */
static void * p_launch_task(void *args)
{
	task_info_t *task_info_ptr = (task_info_t *)args;
	slurm_msg_t *req_ptr = task_info_ptr->req_ptr;
	launch_tasks_request_msg_t *msg_ptr = (launch_tasks_request_msg_t *) req_ptr->data;
	job_t *job_ptr = task_info_ptr->job_ptr;
	int host_inx = msg_ptr->srun_node_id;

	debug3("launching on host %s", job_ptr->host[host_inx]);
        print_launch_msg(msg_ptr);
	if (slurm_send_only_node_msg(req_ptr) < 0) {	/* Already handles timeout */
		error("launch %s: %m", job_ptr->host[host_inx]);
		job_ptr->host_state[host_inx] = SRUN_HOST_UNREACHABLE;
	}

	pthread_mutex_lock(&active_mutex);
	active--;
	pthread_cond_signal(&active_cond);
	pthread_mutex_unlock(&active_mutex);
	xfree(args);
	return NULL;
}


static void print_launch_msg(launch_tasks_request_msg_t *msg)
{
	char buf[4096];
	int len = 0;

	len = snprintf(buf, 4096, "%d.%d uid:%ld n:%ld `%s' %d [%d-%d]",
			msg->job_id, msg->job_step_id, (long) msg->uid, 
			(long) msg->tasks_to_launch, msg->cwd, 
			msg->srun_node_id, msg->global_task_ids[0],
			msg->global_task_ids[msg->tasks_to_launch-1]);
	debug3("%s", buf);
}

static int
envcount(char **environ)
{
	int envc = 0;
	while (environ[envc] != NULL)
		envc++;
	return envc;
}
