#if HAVE_CONFIG_H
#include <config.h>
#endif

#include <unistd.h>
#include <errno.h>
#include <sys/param.h>

#include <src/common/xmalloc.h>
#include <src/common/log.h>
#include <src/common/slurm_protocol_api.h>

#include "launch.h"
#include "job.h"
#include "opt.h"

extern char **environ;

/* number of active threads */
/*
static pthread_mutex_t active_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  active_cond  = PTHREAD_COND_INITIALIZER;
static int             active = 0;
static int timeout;
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
	int i, j, taskid;
	char hostname[MAXHOSTNAMELEN];

	pthread_mutex_lock(&job->state_mutex);
	job->state = SRUN_JOB_LAUNCHING;
	pthread_cond_signal(&job->state_cond);
	pthread_mutex_unlock(&job->state_mutex);

	if (read_slurm_port_config() < 0)
		error("read_slurm_port_config: %d", slurm_strerror(errno));

	if (gethostname(hostname, MAXHOSTNAMELEN) < 0)
		error("gethostname: %m");

	debug("going to launch %d tasks on %d hosts\n", opt.nprocs, job->nhosts);

	/* thr = (launch_thr_t *) xmalloc(opt.nprocs * sizeof(*thr)); */

	req.msg_type = REQUEST_LAUNCH_TASKS;
	req.data     = &msg;

	msg.job_id = job->jobid;
	msg.uid = opt.uid;
	msg.argc = remote_argc;
	msg.argv = remote_argv;
	msg.credential = job->cred;
	msg.job_step_id = 0;
	msg.envc = envcount(environ);
	msg.env = environ;
	msg.cwd = opt.cwd;
	slurm_set_addr_char(&msg.response_addr, 
		 	    ntohs(job->jaddr.sin_port), hostname);

#if HAVE_LIBELAN3
	msg.qsw_job = job->qsw_job;
#endif
	debug("setting iopart to %s:%d", hostname, ntohs(job->ioport));
	slurm_set_addr_char(&msg.streams , ntohs(job->ioport), hostname); 
	debug("sending to slurmd port %d", slurm_get_slurmd_port());

	taskid = 0;
	for (i = 0; i < job->nhosts; i++) {
		msg.tasks_to_launch = job->ntask[i];
		msg.global_task_ids = 
			(uint32_t *) xmalloc(job->ntask[i]*sizeof(uint32_t));
		msg.srun_node_id = (uint32_t)i;

		for (j = 0; j < job->ntask[i]; j++)
			msg.global_task_ids[j] = taskid++;

		slurm_set_addr_uint(&req.address, slurm_get_slurmd_port(), 
				    ntohl(job->iaddr[i]));

		debug2("launching on host %s", job->host[i]);
                print_launch_msg(&msg);
		if (slurm_send_only_node_msg(&req) < 0) {
			error("%s: %m", job->host[i]);
			job->host_state[i] = SRUN_HOST_UNREACHABLE;
		}

		xfree(msg.global_task_ids);
	}

	pthread_mutex_lock(&job->state_mutex);
	job->state = SRUN_JOB_STARTING;
	pthread_cond_signal(&job->state_cond);
	pthread_mutex_unlock(&job->state_mutex);


	return(void *)(0);

}


static void print_launch_msg(launch_tasks_request_msg_t *msg)
{
	int i;
	debug("jobid  = %ld" , msg->job_id);
	debug("stepid = %ld" , msg->job_step_id);
	debug("uid    = %ld" , msg->uid);
	debug("ntasks = %ld" , msg->tasks_to_launch);
	debug("envc   = %d"  , msg->envc);
	debug("cwd    = `%s'", msg->cwd); 
	for (i = 0; i < msg->tasks_to_launch; i++)
		debug("global_task_id[%d] = %d\n", i, msg->global_task_ids[i]);
}

static int
envcount(char **environ)
{
	int envc = 0;
	while (environ[envc] != NULL)
		envc++;
	return envc;
}
