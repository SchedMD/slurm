/* an srun "job" */

#ifndef _HAVE_JOB_H
#define _HAVE_JOB_H

#include <pthread.h>

#include <netinet/in.h>

#include <src/common/macros.h>
#include <src/api/slurm.h>

typedef struct in_addr IA;

typedef struct srun_job {
	uint32_t jobid;		/* assigned job id */
	uint32_t stepid;	/* assigned step id */
	slurm_job_credential_t *cred;
	char *nodelist;		/* nodelist in string form */
	int nhosts;
	char **host;		/* hostname vector */
	int *ntask; 		/* number of tasks to run on each host*/
        uint32_t *iaddr;	/* in_addr vector  */

	pthread_t jtid;		/* job control thread id */
	slurm_fd jfd;		/* job control info fd   */
	slurm_addr jaddr;	/* job control info port */

	pthread_t ioid;		/* stdio thread id */
	int iofd;		/* stdio listen fd */
	int ioport;		/* stdio listen port */

	int *out;		/* ntask stdout fds */
	int *err;		/* ntask stderr fds */

	int lastfd;		/* temporary help for assigning io fds */
} job_t;

job_t * job_create(resource_allocation_response_msg_t *resp);


#endif /* !_HAVE_JOB_H */
