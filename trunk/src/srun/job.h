/* an srun "job" */

#ifndef _HAVE_JOB_H
#define _HAVE_JOB_H

#ifdef WITH_PTHREADS
#  include <pthread.h>
#endif

#include <netinet/in.h>

#include "src/common/macros.h"
#include "src/common/cbuf.h"
#include "src/api/slurm.h"
#include "src/common/slurm_protocol_defs.h"

#include "src/srun/fname.h"

typedef enum {
	SRUN_JOB_INIT = 0,         /* Job's initial state                   */
	SRUN_JOB_LAUNCHING,        /* Launch thread is running              */
	SRUN_JOB_STARTING,         /* Launch thread is complete             */
	SRUN_JOB_RUNNING,          /* Launch thread complete                */
	SRUN_JOB_TERMINATING,      /* Once first task terminates            */
	SRUN_JOB_TERMINATED,       /* All tasks terminated (may have IO)    */
	SRUN_JOB_WAITING_ON_IO,    /* All tasks terminated; waiting for IO  */
	SRUN_JOB_FORCETERM,        /* Forced termination of IO thread       */
	SRUN_JOB_DONE,             /* tasks and IO complete                 */
	SRUN_JOB_DETACHED,         /* Detached IO from job (Not used now)   */
	SRUN_JOB_FAILED,           /* Job failed for some reason            */
} job_state_t;

typedef enum {
	SRUN_HOST_INIT = 0,
	SRUN_HOST_CONTACTED,
	SRUN_HOST_UNREACHABLE,
	SRUN_HOST_REPLIED
} host_state_t;

typedef enum {
	SRUN_TASK_INIT = 0,
	SRUN_TASK_RUNNING,
	SRUN_TASK_FAILED,
	SRUN_TASK_IO_WAIT,
	SRUN_TASK_EXITED
} task_state_t;


typedef struct srun_job {
	uint32_t jobid;		/* assigned job id 	                  */
	uint32_t stepid;	/* assigned step id 	                  */
	bool old_job;           /* run job step under previous allocation */

	job_state_t state;	/* job state	   	                  */
	pthread_mutex_t state_mutex; 
	pthread_cond_t  state_cond;

	slurm_job_credential_t *cred;
	char *nodelist;		/* nodelist in string form */
	int nhosts;
	char **host;		/* hostname vector */
	int *cpus; 		/* number of processors on each host */
	int *ntask; 		/* number of tasks to run on each host */
	uint32_t **tids;	/* host id => task ids mapping    */

        slurm_addr *slurmd_addr;/* slurm_addr vector to slurmd's */

	pthread_t sigid;	/* signals thread tid		  */

	pthread_t jtid;		/* job control thread id 	  */
	int njfds;		/* number of job control info fds */
	slurm_fd *jfd;		/* job control info fd   	  */
	slurm_addr *jaddr;	/* job control info ports 	  */

	pthread_t ioid;		/* stdio thread id 		  */
	int niofds;		/* Number of IO fds  		  */
	int *iofd;		/* stdio listen fds 		  */
	int *ioport;		/* stdio listen ports 		  */

	int *out;		/* ntask stdout fds */
	int *err;		/* ntask stderr fds */

	/* XXX Need long term solution here:
	 * Quickfix: ntask*2 cbufs for buffering job output
	 */
	cbuf_t *outbuf;
	cbuf_t *errbuf;
	cbuf_t *inbuf;            /* buffer for stdin data */

	pthread_t lid;		  /* launch thread id */

	host_state_t *host_state; /* nhost host states */

	int *tstatus;	          /* ntask exit statii */
	task_state_t *task_state; /* ntask task states */
	pthread_mutex_t task_mutex;

#ifdef HAVE_LIBELAN3
	qsw_jobinfo_t qsw_job;
#endif
	io_filename_t *ifname;
	io_filename_t *ofname;
	io_filename_t *efname;

	/* Output streams and stdin fileno */
	FILE *outstream;
	FILE *errstream;
	int   stdinfd;
	bool *stdin_eof;  /* true if task i processed stdin eof */

} job_t;

void    update_job_state(job_t *job, job_state_t newstate);
void    job_force_termination(job_t *job);

job_t * job_create_noalloc(void);
job_t * job_create_allocation(resource_allocation_response_msg_t *resp);

void    job_fatal(job_t *job, const char *msg);
void    job_destroy(job_t *job);

#endif /* !_HAVE_JOB_H */
