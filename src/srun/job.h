/* an srun "job" */

#ifndef _HAVE_JOB_H
#define _HAVE_JOB_H

#include <pthread.h>

#include <netinet/in.h>

#include <src/common/macros.h>
#include <src/api/slurm.h>

typedef enum {
	SRUN_JOB_INIT = 0,
	SRUN_JOB_LAUNCHING,
	SRUN_JOB_STARTING,
	SRUN_JOB_RUNNING,
	SRUN_JOB_TERMINATING,
	SRUN_JOB_OVERDONE
} job_state_t;

typedef enum {
	SRUN_TASK_INIT = 0,
	SRUN_TASK_RUNNING,
	SRUN_TASK_FAILED,
	SRUN_TASK_EXITED
} task_state_t;


typedef struct srun_job {
	uint32_t jobid;		/* assigned job id 	*/
	uint32_t stepid;	/* assigned step id 	*/

	job_state_t state;	/* job state	   	*/
	pthread_mutex_t state_mutex; 
	pthread_cond_t  state_cond;

	slurm_job_credential_t *cred;
	char *nodelist;		/* nodelist in string form */
	int nhosts;
	char **host;		/* hostname vector */
	int *ntask; 		/* number of tasks to run on each host*/
        uint32_t *iaddr;	/* in_addr vector  */

	pthread_t sigid;	/* signals thread tid	*/

	pthread_t jtid;		/* job control thread id */
	slurm_fd jfd;		/* job control info fd   */
	slurm_addr jaddr;	/* job control info port */

	pthread_t ioid;		/* stdio thread id */
	int iofd;		/* stdio listen fd */
	int ioport;		/* stdio listen port */

	int *out;		/* ntask stdout fds */
	int *err;		/* ntask stderr fds */

	int *task_status;	/* ntask status (return codes) */
	task_state_t *task_state; /* ntask task states	*/
	pthread_mutex_t task_mutex;

#if HAVE_LIBELAN3
	qsw_jobinfo_t qsw_job;
#endif

} job_t;

job_t * job_create(resource_allocation_response_msg_t *resp);


#endif /* !_HAVE_JOB_H */
