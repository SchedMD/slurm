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


/* number of active threads */
static pthread_mutex_t active_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  active_cond  = PTHREAD_COND_INITIALIZER;
static int             active = 0;


/* array of nnodes launch threads initialize in launch() */
static launch_thr_t *thr;

static int timeout;

static void print_launch_msg(launch_tasks_request_msg_t *msg);

void *
launch(void *arg)
{
	slurm_msg_t req;
	launch_tasks_request_msg_t msg;
	job_t *job = (job_t *) arg;
	int i, j, taskid;
	char hostname[MAXHOSTNAMELEN];

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
	msg.envc = 0;
	msg.env = NULL;
	msg.cwd = opt.cwd;
	slurm_set_addr_char(&msg.response_addr, 
		 	    ntohs(job->jaddr.sin_port), hostname);
	slurm_set_addr_char(&msg.streams , ntohs(job->ioport), hostname); 
	debug("sending to slurmd port %d", slurm_get_slurmd_port());

	taskid = 0;
	for (i = 0; i < job->nhosts; i++) {
		msg.tasks_to_launch = job->ntask[i];
		msg.global_task_ids = 
			(uint32_t *) xmalloc(job->ntask[i]*sizeof(uint32_t));

		for (j = 0; j < job->ntask[i]; j++)
			msg.global_task_ids[j] = taskid++;

		slurm_set_addr_uint(&req.address, slurm_get_slurmd_port(), 
				    ntohl(job->iaddr[i]));

                print_launch_msg(&msg);
		if (slurm_send_only_node_msg(&req) < 0)
			error("Unable to send launch request: %s",
					slurm_strerror(errno));

		xfree(msg.global_task_ids);
	}

	return 1;

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
