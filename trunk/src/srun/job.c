/****************************************************************************\
 *  job.c - job data structure createion functions
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

#include <netdb.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>

#include "src/common/hostlist.h"
#include "src/common/log.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/cbuf.h"

#include "src/srun/job.h"
#include "src/srun/opt.h"
#include "src/srun/fname.h"

typedef struct allocation_info {
	uint32_t                jobid;
	uint32_t                stepid;
	char                   *nodelist;
	int                     nnodes;
	slurm_addr             *addrs;
	int                     num_cpu_groups;
	int                    *cpus_per_node;
	int                    *cpu_count_reps;
} allocation_info_t;


static inline int
_estimate_nports(int nclients, int cli_per_port)
{
	div_t d;
	d = div(nclients, cli_per_port);
	return d.rem > 0 ? d.quot + 1 : d.quot;
}

static int
_compute_task_count(allocation_info_t *info)
{
	int i, cnt = 0;

	if (opt.cpus_set) {
		for (i = 0; i < info->num_cpu_groups; i++)
			cnt += ( info->cpu_count_reps[i] *
				 (info->cpus_per_node[i]/opt.cpus_per_task));
	}

	return (cnt < info->nnodes) ? info->nnodes : cnt;
}

static void
_set_nprocs(allocation_info_t *info)
{
	if (!opt.nprocs_set)
		opt.nprocs = _compute_task_count(info);
}


static job_t *
_job_create_internal(allocation_info_t *info)
{
	int i;
	int cpu_cnt = 0;
	int cpu_inx = 0;
	int tph     = 0;
	hostlist_t hl;
	job_t *job;

	/* Reset nprocs if necessary 
	 */
	_set_nprocs(info);

	debug2("creating job with %d tasks", opt.nprocs);

	job = xmalloc(sizeof(*job));

	slurm_mutex_init(&job->state_mutex);
	pthread_cond_init(&job->state_cond, NULL);
	job->state = SRUN_JOB_INIT;

	job->nodelist = xstrdup(info->nodelist);
	hl = hostlist_create(job->nodelist);
	job->nhosts = hostlist_count(hl);

	job->jobid   = info->jobid;
	job->stepid  = info->stepid;
	job->old_job = false;

	job->slurmd_addr = xmalloc(job->nhosts * sizeof(slurm_addr));
	if (info->addrs)
		memcpy( job->slurmd_addr, info->addrs, 
			sizeof(slurm_addr)*job->nhosts);

	job->host  = (char **) xmalloc(job->nhosts * sizeof(char *));
	job->cpus  = (int *)   xmalloc(job->nhosts * sizeof(int) );
	job->ntask = (int *)   xmalloc(job->nhosts * sizeof(int) );

	/* Compute number of file descriptors / Ports needed for Job 
	 * control info server
	 */
	job->njfds = _estimate_nports(opt.nprocs, 48);
	job->jfd   = (slurm_fd *)   xmalloc(job->njfds * sizeof(slurm_fd));
	job->jaddr = (slurm_addr *) xmalloc(job->njfds * sizeof(slurm_addr));

	debug3("njfds = %d", job->njfds);

	/* Compute number of IO file descriptors needed and allocate 
	 * memory for them
	 */
	job->niofds = _estimate_nports(opt.nprocs, 64);
	job->iofd   = (int *) xmalloc(job->niofds * sizeof(int));
	job->ioport = (int *) xmalloc(job->niofds * sizeof(int));

	/* ntask stdout and stderr fds */
	job->out    = (int *)  xmalloc(opt.nprocs * sizeof(int));
	job->err    = (int *)  xmalloc(opt.nprocs * sizeof(int));

	/* ntask cbufs for stdout and stderr */
	job->outbuf     = (cbuf_t *) xmalloc(opt.nprocs * sizeof(cbuf_t));
	job->errbuf     = (cbuf_t *) xmalloc(opt.nprocs * sizeof(cbuf_t));
	job->inbuf      = (cbuf_t *) xmalloc(opt.nprocs * sizeof(cbuf_t));
	job->stdin_eof  = (bool *)   xmalloc(opt.nprocs * sizeof(bool));


	/* nhost host states */
	job->host_state =  xmalloc(job->nhosts * sizeof(host_state_t));

	/* ntask task states and statii*/
	job->task_state  =  xmalloc(opt.nprocs * sizeof(task_state_t));
	job->tstatus	 =  xmalloc(opt.nprocs * sizeof(int));

	for (i = 0; i < opt.nprocs; i++) {
		job->task_state[i] = SRUN_TASK_INIT;

		job->outbuf[i]     = cbuf_create(4096, 1048576);
		job->errbuf[i]     = cbuf_create(4096, 1048576);
		job->inbuf[i]      = cbuf_create(4096, 4096);

		cbuf_opt_set(job->outbuf[i], CBUF_OPT_OVERWRITE, CBUF_NO_DROP);
		cbuf_opt_set(job->errbuf[i], CBUF_OPT_OVERWRITE, CBUF_NO_DROP);
		cbuf_opt_set(job->inbuf[i],  CBUF_OPT_OVERWRITE, CBUF_NO_DROP);

		job->stdin_eof[i]  = false;
	}

	slurm_mutex_init(&job->task_mutex);

	/* tasks per host, round up */
	tph = (opt.nprocs+job->nhosts-1) / job->nhosts; 

	for(i = 0; i < job->nhosts; i++) {
		job->host[i]  = hostlist_shift(hl);

		if (opt.overcommit)
			job->cpus[i] = tph;
		else
			job->cpus[i] = info->cpus_per_node[cpu_inx];

		if ((++cpu_cnt) >= info->cpu_count_reps[cpu_inx]) {
			/* move to next record */
			cpu_inx++;
			cpu_cnt = 0;
		}
	}

	job->ifname = fname_create(job, opt.ifname);
	job->ofname = fname_create(job, opt.ofname);
	job->efname = opt.efname ? fname_create(job, opt.efname) : job->ofname;

	hostlist_destroy(hl);

	return job;
}

job_t *
job_create_allocation(resource_allocation_response_msg_t *resp)
{
	job_t *job;
	allocation_info_t *info = xmalloc(sizeof(*info));

	info->nodelist       = resp->node_list;
	info->nnodes	     = resp->node_cnt;
	info->jobid          = resp->job_id;
	info->stepid         = NO_VAL;
	info->num_cpu_groups = resp->num_cpu_groups;
	info->cpus_per_node  = resp->cpus_per_node;
	info->cpu_count_reps = resp->cpu_count_reps;
	info->addrs          = resp->node_addr;

	job = _job_create_internal(info);

	xfree(info);

	return (job);
}

static void
_job_fake_cred(job_t *job)
{
	int fd;
	if ((fd = open("/dev/random", O_RDONLY)) < 0)
		error ("unable to open /dev/random: %m");

	job->cred          = xmalloc(sizeof(*job->cred));
	job->cred->job_id  = job->jobid;
	job->cred->user_id = opt.uid;
	job->cred->expiration_time = 0x7fffffff;
	read(fd, job->cred->signature, SLURM_SSL_SIGNATURE_LENGTH);

	if (close(fd) < 0)
		error ("close(/dev/random): %m");
}

job_t *
job_create_noalloc(void)
{
	job_t *job = NULL;
	allocation_info_t *info = xmalloc(sizeof(*info));
	int cpn     = 1;
	int i;
	hostlist_t  hl = hostlist_create(opt.nodelist);

	if (!hl) {
		error("Invalid node list `%s' specified", opt.nodelist);
		goto error;
	}

	srand48(getpid());
	info->jobid          = (uint32_t) (lrand48() % 65550L + 1L);
	info->stepid         = 0;
	info->nodelist       = opt.nodelist;
	info->nnodes         = hostlist_count(hl);

	/* if (opt.nprocs < info->nnodes)
		opt.nprocs = hostlist_count(hl);
	*/
	hostlist_destroy(hl);

	info->cpus_per_node  = &cpn;
	info->cpu_count_reps = &opt.nprocs;
	info->addrs          = NULL; 

	/* 
	 * Create job, then fill in host addresses
	 */
	job = _job_create_internal(info);

	for (i = 0; i < job->nhosts; i++) {
		slurm_set_addr ( &job->slurmd_addr[i], 
				  slurm_get_slurmd_port(), 
				  job->host[i] );
	}

	_job_fake_cred(job);

   error:
	xfree(info);
	return (job);

}

void
update_job_state(job_t *job, job_state_t state)
{
	pthread_mutex_lock(&job->state_mutex);
	if (job->state < state) {
		job->state = state;
		pthread_cond_signal(&job->state_cond);
	}
	pthread_mutex_unlock(&job->state_mutex);
}

void 
job_force_termination(job_t *job)
{
	if (mode == MODE_ATTACH) {
		info ("forcing detach");
		update_job_state(job, SRUN_JOB_DETACHED); 	
	} else {
		info ("forcing job termination");
		update_job_state(job, SRUN_JOB_FORCETERM);
	}

	pthread_kill(job->ioid,  SIGHUP);
}

void job_fatal(job_t *job, const char *msg)
{
	if (msg) error(msg);

	job_destroy(job, errno);

	exit(1);
}

void 
job_destroy(job_t *job, int error)
{
	if (job->old_job) {
		debug("cancelling job step %u.%u", job->jobid, job->stepid);
		slurm_complete_job_step(job->jobid, job->stepid, 0, error);
	} else if (!opt.no_alloc) {
		debug("cancelling job %u", job->jobid);
		slurm_complete_job(job->jobid, 0, error);
	} else {
		debug("no allocation to cancel");
		return;
	}

#ifdef HAVE_TOTALVIEW
	if (error) tv_launch_failure();
#endif
}

void
job_kill(job_t *job)
{
	if (!opt.no_alloc) {
		if (slurm_kill_job_step(job->jobid, job->stepid, SIGKILL) < 0)
			error ("slurm_kill_job_step: %m");
	}
	update_job_state(job, SRUN_JOB_FAILED);
}

int
job_active_tasks_on_host(job_t *job, int hostid)
{
	int i;
	int retval = 0;

	slurm_mutex_lock(&job->task_mutex);
	for (i = 0; i < job->ntask[hostid]; i++) {
		uint32_t tid = job->tids[hostid][i];
		if (job->task_state[tid] == SRUN_TASK_RUNNING) 
			retval++;
	}
	slurm_mutex_unlock(&job->task_mutex);
	return retval;
}
