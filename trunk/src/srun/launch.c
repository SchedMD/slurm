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
/*
static pthread_mutex_t active_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  active_cond  = PTHREAD_COND_INITIALIZER;
static int             active = 0;
static int             timeout = 0;
*/


/* array of nnodes launch threads initialize in launch() */
/* static launch_thr_t *thr; */

static void print_launch_msg(launch_tasks_request_msg_t *msg);
static int  envcount(char **env);

void *
launch(void *arg)
{
	slurm_msg_t req;
	launch_tasks_request_msg_t msg;
	job_t *job = (job_t *) arg;
	int i, j, k, taskid;
	char hostname[MAXHOSTNAMELEN];
	uint32_t **task_ids;

	update_job_state(job, SRUN_JOB_LAUNCHING);

	if (gethostname(hostname, MAXHOSTNAMELEN) < 0)
		error("gethostname: %m");

	debug("going to launch %d tasks on %d hosts", opt.nprocs, job->nhosts);

	/* thr = (launch_thr_t *) xmalloc(opt.nprocs * sizeof(*thr)); */

	req.msg_type = REQUEST_LAUNCH_TASKS;
	req.data     = &msg;

	msg.job_id = job->jobid;
	msg.uid = opt.uid;
	msg.argc = remote_argc;
	msg.argv = remote_argv;
	msg.credential = job->cred;
	msg.job_step_id = job->stepid;
	msg.envc = envcount(environ);
	msg.env = environ;
	msg.cwd = opt.cwd;
	msg.nnodes = job->nhosts;
	msg.nprocs = opt.nprocs;

#if HAVE_LIBELAN3
	msg.qsw_job = job->qsw_job;
#endif 
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

	for (i = 0; i < job->nhosts; i++) {

		msg.tasks_to_launch = job->ntask[i];
		msg.global_task_ids = task_ids[i];
		msg.srun_node_id    = (uint32_t)i;
		msg.io_port         = ntohs(job->ioport[i%job->niofds]);
		msg.resp_port       = ntohs(job->jaddr[i%job->njfds].sin_port);

		memcpy(&req.address, &job->slurmd_addr[i], sizeof(slurm_addr));

		debug2("launching on host %s", job->host[i]);
                print_launch_msg(&msg);
		if (slurm_send_only_node_msg(&req) < 0) {
			error("%s: %m", job->host[i]);
			job->host_state[i] = SRUN_HOST_UNREACHABLE;
		}
		xfree(task_ids[i]);

	}
	xfree(task_ids);

	update_job_state(job, SRUN_JOB_STARTING);

	return(void *)(0);

}


static void print_launch_msg(launch_tasks_request_msg_t *msg)
{
	int i;
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
