/* */

#include <netdb.h>
#include <string.h>
#include <sys/socket.h>

#include "job.h"
#include "opt.h"

#include <src/common/xmalloc.h>
#include <src/common/xstring.h>
#include <src/common/hostlist.h>
#include <src/common/log.h>

job_t *
job_create(resource_allocation_response_msg_t *resp)
{
	int i; 
	int ntask, tph;		/* ntasks left to assign and tasks per host */
	int ncpu;
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
		job->jobid = 1;
		ncpu = 1;
		if (opt.nprocs <= 1)
			opt.nprocs = hostlist_count(hl);
	}


	job->nhosts = hostlist_count(hl);

	job->host  = (char **) xmalloc(job->nhosts * sizeof(char *));
	job->iaddr = (uint32_t *) xmalloc(job->nhosts * sizeof(uint32_t));
	job->ntask = (int *)   xmalloc(job->nhosts * sizeof(int *) );

	/* ntask stdout and stderr fds */
	job->out   = (int *)   xmalloc(opt.nprocs * sizeof(int)   );
	job->err   = (int *)   xmalloc(opt.nprocs * sizeof(int)   );

	/* nhost host states */
	job->host_state = 
		(host_state_t *) xmalloc(job->nhosts * sizeof(host_state_t));

	/* ntask task states and statii*/
	job->task_state  = 
		(task_state_t *) xmalloc(opt.nprocs * sizeof(task_state_t));
	job->tstatus	 = (int *) xmalloc(opt.nprocs * sizeof(int));

	pthread_mutex_init(&job->task_mutex, NULL);

	ntask = opt.nprocs;
	tph   = ntask / job->nhosts; /* expect trucation of result here */

	for(i = 0; i < job->nhosts; i++) {
		struct hostent *he;
		job->host[i]  = hostlist_shift(hl);

		he = gethostbyname(job->host[i]);
		if (he == NULL) {
			error("Unable to resolve host `%s'\n", job->host[i]);
			continue;
		}
		memcpy(&job->iaddr[i], *he->h_addr_list, sizeof(job->iaddr[i]));

		/* XXX: temporary, need function to lay out tasks later */
		job->ntask[i] = (ntask - tph) > 0 ? tph : ntask; 
	}

	return job;
}
