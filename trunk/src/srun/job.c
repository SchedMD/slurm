/****************************************************************************\
 *  job.c - job data structure createion functions
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

#include "src/common/hostlist.h"
#include "src/common/log.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/srun/job.h"
#include "src/srun/opt.h"

job_t *
job_create(resource_allocation_response_msg_t *resp)
{
	int i, cpu_cnt = 0, cpu_inx = 0; 
	int ntask, tph;		/* ntasks left to assign and tasks per host */
	int ncpu;
	div_t d;
	hostlist_t hl;
	job_t *job = (job_t *) xmalloc(sizeof(*job));


	pthread_mutex_init(&job->state_mutex, NULL);
	pthread_cond_init(&job->state_cond, NULL);
	job->state = SRUN_JOB_INIT;

	if (resp != NULL) {
		job->nodelist = xstrdup(resp->node_list);
	        hl = hostlist_create(resp->node_list);
		job->jobid = resp->job_id;
		ncpu  = *resp->cpus_per_node;
	} else {
		job->cred = 
		       (slurm_job_credential_t *) xmalloc(sizeof(*job->cred));
		job->cred->job_id = job->jobid;
		job->cred->user_id = opt.uid;
		job->cred->expiration_time = 0x7fffffff;
		job->cred->signature[0] = 'a';

		job->nodelist = xstrdup(opt.nodelist);
		hl = hostlist_create(opt.nodelist);
		srand48(getpid());
		job->jobid = (uint32_t) (lrand48() % 65550L + 1L);
		ncpu = 1;
		if (opt.nprocs <= 1)
			opt.nprocs = hostlist_count(hl);
	}


	job->nhosts = hostlist_count(hl);

	job->host  = (char **) xmalloc(job->nhosts * sizeof(char *));
	job->slurmd_addr = (slurm_addr *) xmalloc(job->nhosts * 
						sizeof(slurm_addr));
	job->cpus = (int *)   xmalloc(job->nhosts * sizeof(int) );
	job->ntask = (int *)   xmalloc(job->nhosts * sizeof(int) );

	/* Compute number of file descriptors / Ports needed for Job 
	 * control info server
	 */
	d = div(opt.nprocs, 48);
	job->njfds = d.rem > 0 ? d.quot+1 : d.quot;
	job->jfd   = (slurm_fd *)   xmalloc(job->njfds * sizeof(slurm_fd));
	job->jaddr = (slurm_addr *) xmalloc(job->njfds * sizeof(slurm_addr));

	debug3("njfds = %d", job->njfds);

	/* Compute number of IO file descriptors needed and allocate 
	 * memory for them
	 */
	d = div(opt.nprocs, 64);
	job->niofds = d.rem > 0 ? d.quot+1 : d.quot;
	job->iofd   = (int *) xmalloc(job->niofds * sizeof(int));
	job->ioport = (int *) xmalloc(job->niofds * sizeof(int));

	/* ntask stdout and stderr fds */
	job->out   = (int *)  xmalloc(opt.nprocs *  sizeof(int));
	job->err   = (int *)  xmalloc(opt.nprocs *  sizeof(int));

	/* nhost host states */
	job->host_state = 
		(host_state_t *) xmalloc(job->nhosts * sizeof(host_state_t));

	/* ntask task states and statii*/
	job->task_state  = 
		(task_state_t *) xmalloc(opt.nprocs * sizeof(task_state_t));
	job->tstatus	 = (int *) xmalloc(opt.nprocs * sizeof(int));

	pthread_mutex_init(&job->task_mutex, NULL);

	ntask = opt.nprocs;
	tph = (ntask+job->nhosts-1) / 
				job->nhosts; /* tasks per host, round up */

	for(i = 0; i < job->nhosts; i++) {
		job->host[i]  = hostlist_shift(hl);


		/* actual task counts and layouts performed in launch() */
		/* job->ntask[i] = 0; */

		if (resp) {
			job->cpus[i] = resp->cpus_per_node[cpu_inx];
			if ((++cpu_cnt) >= resp->cpu_count_reps[cpu_inx]) {
				/* move to next record */
				cpu_inx++;
				cpu_cnt = 0;
			}
			memcpy(&job->slurmd_addr[i], 
			       &resp->node_addr[i], 
			       sizeof(job->slurmd_addr[i]));
		} else {
			job->cpus[i] = tph;
			slurm_set_addr (&job->slurmd_addr[i], 
					slurm_get_slurmd_port(), job->host[i]);
		}
	}

	return job;
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

