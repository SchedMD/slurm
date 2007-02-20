/****************************************************************************\
 *  msg.c - process message traffic between srun and slurm daemons
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>, et. al.
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
#  include "config.h"
#endif

#if HAVE_PTHREAD_H
#  include <pthread.h>
#endif

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/wait.h>
#include <time.h>

#include <slurm/slurm_errno.h>

#include "src/common/fd.h"
#include "src/common/hostlist.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/read_config.h"
#include "src/common/slurm_auth.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"

#include "src/srun/srun_job.h"
#include "src/srun/opt.h"
#include "src/srun/io.h"
#include "src/srun/msg.h"
#include "src/srun/sigstr.h"
#include "src/srun/attach.h"

#include "src/common/xstring.h"

#define LAUNCH_WAIT_SEC	 60	/* max wait to confirm launches, sec */
#define MAX_RETRIES 3		/* pthread_create retries */

static int    tasks_exited     = 0;
static uid_t  slurm_uid;
static slurm_fd slurmctld_fd   = (slurm_fd) NULL;

/*
 *  Static prototypes
 */
static void	_accept_msg_connection(srun_job_t *job, int fdnum);
static void	_confirm_launch_complete(srun_job_t *job);
static void	_dump_proctable(srun_job_t *job);
static void 	_exit_handler(srun_job_t *job, slurm_msg_t *exit_msg);
static void	_handle_msg(srun_job_t *job, slurm_msg_t *msg);
static inline bool _job_msg_done(srun_job_t *job);
static void	_launch_handler(srun_job_t *job, slurm_msg_t *resp);
static void     _do_poll_timeout(srun_job_t *job);
static int      _get_next_timeout(srun_job_t *job);
static void 	_msg_thr_poll(srun_job_t *job);
static void	_set_jfds_nonblocking(srun_job_t *job);
static void     _print_pid_list(const char *host, int ntasks, 
				uint32_t *pid, char *executable_name);
static void     _timeout_handler(time_t timeout);
static void     _node_fail_handler(char *nodelist, srun_job_t *job);

#define _poll_set_rd(_pfd, _fd) do {    \
	(_pfd).fd = _fd;                \
	(_pfd).events = POLLIN;         \
	} while (0)

#define _poll_set_wr(_pfd, _fd) do {    \
	(_pfd).fd = _fd;                \
	(_pfd).events = POLLOUT;        \
	} while (0)

#define _poll_rd_isset(pfd) ((pfd).revents & POLLIN )
#define _poll_wr_isset(pfd) ((pfd).revents & POLLOUT)
#define _poll_err(pfd)      ((pfd).revents & POLLERR)

/*
 * Install entry in the MPI_proctable for host with node id `nodeid'
 *  and the number of tasks `ntasks' with pid array `pid'
 */
static void
_build_proctable(srun_job_t *job, char *host, int nodeid, int ntasks, uint32_t *pid)
{
	int i;
	static int tasks_recorded = 0;
	pipe_enum_t pipe_enum = PIPE_MPIR_PROCTABLE_SIZE;
	
	if (MPIR_proctable_size == 0) {
		MPIR_proctable_size = opt.nprocs;
/* 		MPIR_proctable = xmalloc(sizeof(MPIR_PROCDESC) * opt.nprocs); */
/* 		totalview_jobid = NULL; */
/* 		xstrfmtcat(totalview_jobid, "%lu", job->jobid);		 */
		
		if(message_thread) {
			write(job->forked_msg->par_msg->msg_pipe[1],
			      &pipe_enum,sizeof(int));
			write(job->forked_msg->par_msg->msg_pipe[1],
			      &opt.nprocs,sizeof(int));
			
			pipe_enum = PIPE_MPIR_TOTALVIEW_JOBID;
			write(job->forked_msg->par_msg->msg_pipe[1],
			      &pipe_enum,sizeof(int));
			write(job->forked_msg->par_msg->msg_pipe[1],
			      &job->jobid,sizeof(int));	
		}
	}

	for (i = 0; i < ntasks; i++) {
		int taskid          = job->tids[nodeid][i];
		/* MPIR_PROCDESC *tv   = &MPIR_proctable[taskid]; */
/* 		tv->host_name       = job->host[nodeid]; */
/* 		tv->executable_name = remote_argv[0]; */
/* 		tv->pid             = pid[i]; */
		
		if(message_thread) {
			pipe_enum = PIPE_MPIR_PROCDESC;
			write(job->forked_msg->par_msg->msg_pipe[1],
			      &pipe_enum,sizeof(int));
			write(job->forked_msg->par_msg->msg_pipe[1],
			      &taskid,sizeof(int));	
			write(job->forked_msg->par_msg->msg_pipe[1],
			      &nodeid,sizeof(int));	
			write(job->forked_msg->par_msg->msg_pipe[1],
			      &pid[i],sizeof(int));
		}

		tasks_recorded++;
	}

	if (tasks_recorded == opt.nprocs) {
		/* MPIR_debug_state = MPIR_DEBUG_SPAWNED; */
/* 		MPIR_Breakpoint(); */
/* 		if (opt.debugger_test) */
/* 			_dump_proctable(job);  */
		
		if(message_thread) {
			i = MPIR_DEBUG_SPAWNED;
			pipe_enum = PIPE_MPIR_DEBUG_STATE;
			write(job->forked_msg->par_msg->msg_pipe[1],
			      &pipe_enum,sizeof(int));
			write(job->forked_msg->par_msg->msg_pipe[1],
			      &i,sizeof(int));
		}
	}
}

static void _dump_proctable(srun_job_t *job)
{
	int node_inx, task_inx, taskid;
	MPIR_PROCDESC *tv;

	for (node_inx=0; node_inx<job->nhosts; node_inx++) {
		for (task_inx=0; task_inx<job->ntask[node_inx]; task_inx++) {
			taskid = job->tids[node_inx][task_inx];
			tv = &MPIR_proctable[taskid];
			info("task:%d, host:%s, pid:%d",
				taskid, tv->host_name, tv->pid);
		}
	} 
}
	
void debugger_launch_failure(srun_job_t *job)
{
	int i;
	pipe_enum_t pipe_enum = PIPE_MPIR_DEBUG_STATE;
	
	if (opt.parallel_debug) {
		/* MPIR_debug_state = MPIR_DEBUG_ABORTING; */
/* 		MPIR_Breakpoint();  */
		if(message_thread && job) {
			i = MPIR_DEBUG_ABORTING;
			write(job->forked_msg->par_msg->msg_pipe[1],
			      &pipe_enum,sizeof(int));
			write(job->forked_msg->par_msg->msg_pipe[1],
			      &i,sizeof(int));
		} else if(!job) {
			error("Hey I don't have a job to write to on the "
			      "failure of the debugger launch.");
		}
	}
}

/*
 * Job has been notified of it's approaching time limit. 
 * Job will be killed shortly after timeout.
 * This RPC can arrive multiple times with the same or updated timeouts.
 * FIXME: We may want to signal the job or perform other action for this.
 * FIXME: How much lead time do we want for this message? Some jobs may 
 *	require tens of minutes to gracefully terminate.
 */
static void _timeout_handler(time_t timeout)
{
	static time_t last_timeout = 0;

	if (timeout != last_timeout) {
		last_timeout = timeout;
		verbose("job time limit to be reached at %s", 
				ctime(&timeout));
	}
}

/*
 * Job has been notified of a node's failure (at least the node's slurmd 
 * has stopped responding to slurmctld). It is possible that the user's 
 * job is continuing to execute on the specified nodes, but quite possibly 
 * not. The job will continue to execute given the --no-kill option. 
 * Otherwise all of the job's tasks and the job itself are killed..
 */
static void _node_fail_handler(char *nodelist, srun_job_t *job)
{
	if ( (opt.no_kill) &&
	     (io_node_fail(nodelist, job) == SLURM_SUCCESS) ) {
		error("Node failure on %s, eliminated that node", nodelist);
		return;
	}

	error("Node failure on %s, killing job", nodelist);
	update_job_state(job, SRUN_JOB_FORCETERM);
	info("sending Ctrl-C to remaining tasks");
	fwd_signal(job, SIGINT);
	if (job->ioid)
		io_thr_wake(job);
}

static bool _job_msg_done(srun_job_t *job)
{
	return (job->state >= SRUN_JOB_TERMINATED);
}

static void
_process_launch_resp(srun_job_t *job, launch_tasks_response_msg_t *msg)
{
	pipe_enum_t pipe_enum = PIPE_HOST_STATE;
	
	if ((msg->srun_node_id < 0) || (msg->srun_node_id >= job->nhosts)) {
		error ("Bad launch response from %s", msg->node_name);
		return;
	}

	pthread_mutex_lock(&job->task_mutex);
	job->host_state[msg->srun_node_id] = SRUN_HOST_REPLIED;
	pthread_mutex_unlock(&job->task_mutex);

	if(message_thread) {
		write(job->forked_msg->
		      par_msg->msg_pipe[1],&pipe_enum,sizeof(int));
		write(job->forked_msg->par_msg->msg_pipe[1],
		      &msg->srun_node_id,sizeof(int));
		write(job->forked_msg->par_msg->msg_pipe[1],
		      &job->host_state[msg->srun_node_id],sizeof(int));
		
	}
	_build_proctable( job, msg->node_name, msg->srun_node_id, 
			  msg->count_of_pids,  msg->local_pids   );
	_print_pid_list( msg->node_name, msg->count_of_pids, 
			 msg->local_pids, remote_argv[0]     );
	
}

static void
update_running_tasks(srun_job_t *job, uint32_t nodeid)
{
	int i;
	pipe_enum_t pipe_enum = PIPE_TASK_STATE;
	debug2("updating %d running tasks for node %d", 
			job->ntask[nodeid], nodeid);
	slurm_mutex_lock(&job->task_mutex);
	for (i = 0; i < job->ntask[nodeid]; i++) {
		uint32_t tid = job->tids[nodeid][i];
		job->task_state[tid] = SRUN_TASK_RUNNING;

		if(message_thread) {
			write(job->forked_msg->
			      par_msg->msg_pipe[1],&pipe_enum,sizeof(int));
			write(job->forked_msg->
			      par_msg->msg_pipe[1],&tid,sizeof(int));
			write(job->forked_msg->par_msg->msg_pipe[1],
			      &job->task_state[tid],sizeof(int));
		}
	}
	slurm_mutex_unlock(&job->task_mutex);
}

static void
update_failed_tasks(srun_job_t *job, uint32_t nodeid)
{
	int i;
	pipe_enum_t pipe_enum = PIPE_TASK_STATE;
	
	slurm_mutex_lock(&job->task_mutex);
	for (i = 0; i < job->ntask[nodeid]; i++) {
		uint32_t tid = job->tids[nodeid][i];
		job->task_state[tid] = SRUN_TASK_FAILED;

		if(message_thread) {
			write(job->forked_msg->par_msg->msg_pipe[1],
			      &pipe_enum,sizeof(int));
			write(job->forked_msg->
			      par_msg->msg_pipe[1],&tid,sizeof(int));
			write(job->forked_msg->par_msg->msg_pipe[1],
			      &job->task_state[tid],sizeof(int));
		}
		tasks_exited++;
	}
	slurm_mutex_unlock(&job->task_mutex);

	if (tasks_exited == opt.nprocs) {
		debug2("all tasks exited");
		update_job_state(job, SRUN_JOB_TERMINATED);
	}
}

static void
_launch_handler(srun_job_t *job, slurm_msg_t *resp)
{
	launch_tasks_response_msg_t *msg = resp->data;
	pipe_enum_t pipe_enum = PIPE_HOST_STATE;
	
	debug2("received launch resp from %s nodeid=%d", msg->node_name,
			msg->srun_node_id);
	
	if (msg->return_code != 0)  {

		error("%s: launch failed: %s", 
		       msg->node_name, slurm_strerror(msg->return_code));

		slurm_mutex_lock(&job->task_mutex);
		job->host_state[msg->srun_node_id] = SRUN_HOST_REPLIED;
		slurm_mutex_unlock(&job->task_mutex);
		
		if(message_thread) {
			write(job->forked_msg->par_msg->msg_pipe[1],
			      &pipe_enum,sizeof(int));
			write(job->forked_msg->par_msg->msg_pipe[1],
			      &msg->srun_node_id,sizeof(int));
			write(job->forked_msg->par_msg->msg_pipe[1],
			      &job->host_state[msg->srun_node_id],sizeof(int));
		}
		update_failed_tasks(job, msg->srun_node_id);

		/*
		if (!opt.no_kill) {
			job->rc = 124;
			update_job_state(job, SRUN_JOB_WAITING_ON_IO);
		} else 
			update_failed_tasks(job, msg->srun_node_id);
		*/
		debugger_launch_failure(job);
		return;
	} else {
		_process_launch_resp(job, msg);
		update_running_tasks(job, msg->srun_node_id);
	}
}

/* _confirm_launch_complete
 * confirm that all tasks registers a sucessful launch
 * pthread_exit with job kill on failure */
static void	
_confirm_launch_complete(srun_job_t *job)
{
	int i;

	for (i=0; i<job->nhosts; i++) {
		if (job->host_state[i] != SRUN_HOST_REPLIED) {
			error ("Node %s not responding, terminating job step",
			       job->host[i]);
			info("sending Ctrl-C to remaining tasks");
			fwd_signal(job, SIGINT);
			job->rc = 124;
			update_job_state(job, SRUN_JOB_FAILED);
			pthread_exit(0);
		}
	}

	/*
	 *  Reset launch timeout so timer will no longer go off
	 */
	job->ltimeout = 0;
}

static void
_reattach_handler(srun_job_t *job, slurm_msg_t *msg)
{
	int i;
	reattach_tasks_response_msg_t *resp = msg->data;
	pipe_enum_t pipe_enum = PIPE_HOST_STATE;
	
	if ((resp->srun_node_id < 0) || (resp->srun_node_id >= job->nhosts)) {
		error ("Invalid reattach response received");
		return;
	}

	slurm_mutex_lock(&job->task_mutex);
	job->host_state[resp->srun_node_id] = SRUN_HOST_REPLIED;
	slurm_mutex_unlock(&job->task_mutex);

	if(message_thread) {
		write(job->forked_msg->
		      par_msg->msg_pipe[1],&pipe_enum,sizeof(int));
		write(job->forked_msg->par_msg->msg_pipe[1],
		      &resp->srun_node_id,sizeof(int));
		write(job->forked_msg->par_msg->msg_pipe[1],
		      &job->host_state[resp->srun_node_id],sizeof(int));
	}

	if (resp->return_code != 0) {
		if (job->stepid == NO_VAL) { 
			error ("Unable to attach to job %d: %s", 
			       job->jobid, slurm_strerror(resp->return_code));
		} else {
			error ("Unable to attach to step %d.%d on node %d: %s",
			       job->jobid, job->stepid, resp->srun_node_id,
			       slurm_strerror(resp->return_code));
		}
		job->rc = 1;
		update_job_state(job, SRUN_JOB_FAILED);
		return;
	}

	/* 
	 * store global task id information as returned from slurmd
	 */
	job->tids[resp->srun_node_id]  = 
		xmalloc( resp->ntasks * sizeof(uint32_t) );

	job->ntask[resp->srun_node_id] = resp->ntasks;      

	for (i = 0; i < resp->ntasks; i++) {
		job->tids[resp->srun_node_id][i] = resp->gtids[i];
		job->hostid[resp->gtids[i]]      = resp->srun_node_id;
	}

	/* Build process table for any parallel debugger
         */
	if ((remote_argc == 0) && (resp->executable_name)) {
		remote_argc = 1;
		xrealloc(remote_argv, 2 * sizeof(char *));
		remote_argv[0] = resp->executable_name;
		resp->executable_name = NULL; /* nothing left to free */
		remote_argv[1] = NULL;
	}
	_build_proctable (job, resp->node_name, resp->srun_node_id,
	                  resp->ntasks, resp->local_pids);

	_print_pid_list(resp->node_name, resp->ntasks, resp->local_pids, 
			resp->executable_name);

	update_running_tasks(job, resp->srun_node_id);

}


static void 
_print_exit_status(srun_job_t *job, hostlist_t hl, char *host, int status)
{
	char buf[1024];
	char *corestr = "";
	bool signaled  = false;
	void (*print) (const char *, ...) = (void *) &error; 
		
	xassert(hl != NULL);

	slurm_mutex_lock(&job->state_mutex);
	signaled = job->signaled;
	slurm_mutex_unlock(&job->state_mutex);
	
	/*
	 *  Print message that task was signaled as verbose message
	 *    not error message if the user generated the signal.
	 */
	if (signaled) 
		print = &verbose;

	hostlist_ranged_string(hl, sizeof(buf), buf);

	if (status == 0) {
		verbose("%s: %s: Done", host, buf);
		return;
	}

#ifdef WCOREDUMP
	if (WCOREDUMP(status))
		corestr = " (core dumped)";
#endif

	if (WIFSIGNALED(status))
		(*print) ("%s: %s: %s%s", host, buf, sigstr(status), corestr); 
	else
		error ("%s: %s: Exited with exit code %d", 
		       host, buf, WEXITSTATUS(status));

	return;
}

static void
_die_if_signaled(srun_job_t *job, int status)
{
	bool signaled  = false;

	slurm_mutex_lock(&job->state_mutex);
	signaled = job->signaled;
	slurm_mutex_unlock(&job->state_mutex);

	if (WIFSIGNALED(status) && !signaled) {
		job->rc = 128 + WTERMSIG(status);
		update_job_state(job, SRUN_JOB_FAILED);
	}
}

static void
_update_task_exitcode(srun_job_t *job, int taskid)
{
	pipe_enum_t pipe_enum = PIPE_TASK_EXITCODE;

	if(message_thread) {
		write(job->forked_msg->par_msg->msg_pipe[1],
		      &pipe_enum, sizeof(int));
		write(job->forked_msg->par_msg->msg_pipe[1],
		      &taskid, sizeof(int));
		write(job->forked_msg->par_msg->msg_pipe[1],
		      &job->tstatus[taskid], sizeof(int));
	}
}

static void 
_exit_handler(srun_job_t *job, slurm_msg_t *exit_msg)
{
	task_exit_msg_t *msg       = (task_exit_msg_t *) exit_msg->data;
	hostlist_t       hl        = hostlist_create(NULL);
	int              hostid    = job->hostid[msg->task_id_list[0]]; 
	char            *host      = job->host[hostid];
	int              status    = msg->return_code;
	int              i;
	char             buf[1024];

	if (!job->etimeout && !tasks_exited) 
		job->etimeout = time(NULL) + opt.max_exit_timeout;

	for (i = 0; i < msg->num_tasks; i++) {
		uint32_t taskid = msg->task_id_list[i];

		if ((taskid < 0) || (taskid >= opt.nprocs)) {
			error("task exit resp has bad task id %d", taskid);
			continue;
		}

		snprintf(buf, sizeof(buf), "task%d", taskid);
		hostlist_push(hl, buf);

		slurm_mutex_lock(&job->task_mutex);
		job->tstatus[taskid] = status;
		_update_task_exitcode(job, taskid);
		if (status) 
			job->task_state[taskid] = SRUN_TASK_ABNORMAL_EXIT;
		else {
			if (   (job->err[taskid] != IO_DONE) 
			    || (job->out[taskid] != IO_DONE) )
				job->task_state[taskid] = SRUN_TASK_IO_WAIT;
			else
				job->task_state[taskid] = SRUN_TASK_EXITED;
		}

		slurm_mutex_unlock(&job->task_mutex);

		tasks_exited++;
		if ((tasks_exited == opt.nprocs) 
		  || (slurm_mpi_single_task_per_node () 
			&& (tasks_exited == job->nhosts))) {
			debug2("All tasks exited");
			update_job_state(job, SRUN_JOB_TERMINATED);
		}

	}

	_print_exit_status(job, hl, host, status);

	hostlist_destroy(hl);

	_die_if_signaled(job, status);

	/*
	 * When a task terminates with a non-zero exit code and the
	 * "--kill-on-bad-exit" option is set, terminate the entire job.
	 */
	if (status != 0 && opt.kill_bad_exit)
	{
		static int first_time = 1;

		/* Only kill the job once. */
		if (first_time)
		{
			debug("Terminating job due to a non-zero exit code");

			first_time = 0;

			srun_job_kill(job);
		}
	}
}

static void
_handle_msg(srun_job_t *job, slurm_msg_t *msg)
{
	uid_t req_uid = g_slurm_auth_get_uid(msg->cred);
	uid_t uid     = getuid();
	srun_timeout_msg_t *to;
	srun_node_fail_msg_t *nf;

	if ((req_uid != slurm_uid) && (req_uid != 0) && (req_uid != uid)) {
		error ("Security violation, slurm message from uid %u", 
		       (unsigned int) req_uid);
		return;
	}

	switch (msg->msg_type)
	{
		case RESPONSE_LAUNCH_TASKS:
			_launch_handler(job, msg);
			slurm_free_launch_tasks_response_msg(msg->data);
			break;
		case MESSAGE_TASK_EXIT:
			_exit_handler(job, msg);
			slurm_free_task_exit_msg(msg->data);
			break;
		case RESPONSE_REATTACH_TASKS:
			debug2("recvd reattach response");
			_reattach_handler(job, msg);
			slurm_free_reattach_tasks_response_msg(msg->data);
			break;
		case SRUN_PING:
			debug3("slurmctld ping received");
			slurm_send_rc_msg(msg, SLURM_SUCCESS);
			slurm_free_srun_ping_msg(msg->data);
			break;
		case SRUN_TIMEOUT:
			to = msg->data;
			_timeout_handler(to->timeout);
			slurm_send_rc_msg(msg, SLURM_SUCCESS);
			slurm_free_srun_timeout_msg(msg->data);
			break;
		case SRUN_NODE_FAIL:
			nf = msg->data;
			_node_fail_handler(nf->nodelist, job);
			slurm_send_rc_msg(msg, SLURM_SUCCESS);
			slurm_free_srun_node_fail_msg(msg->data);
			break;
		case RESPONSE_RESOURCE_ALLOCATION:
			debug3("resource allocation response received");
			slurm_send_rc_msg(msg, SLURM_SUCCESS);
			slurm_free_resource_allocation_response_msg(msg->data);
			break;
		default:
			error("received spurious message type: %d\n",
					msg->msg_type);
			break;
	}
	slurm_free_msg(msg);
	return;
}

/* NOTE: One extra FD for incoming slurmctld messages */
static void
_accept_msg_connection(srun_job_t *job, int fdnum)
{
	slurm_fd     fd = (slurm_fd) NULL;
	slurm_msg_t *msg = NULL;
	slurm_addr   cli_addr;
	unsigned char *uc;
	short        port;
	int          timeout = 0;	/* slurm default value */

	if (fdnum < job->njfds)
		fd = slurm_accept_msg_conn(job->jfd[fdnum], &cli_addr);
	else
		fd = slurm_accept_msg_conn(slurmctld_fd, &cli_addr);

	if (fd < 0) {
		error("Unable to accept connection: %m");
		return;
	}

	/* Should not call slurm_get_addr() because the IP may not be
	   in /etc/hosts. */
	uc = (unsigned char *)&cli_addr.sin_addr.s_addr;
	port = cli_addr.sin_port;
	debug2("got message connection from %u.%u.%u.%u:%d",
	       uc[0], uc[1], uc[2], uc[3], ntohs(port));

	msg = xmalloc(sizeof(*msg));

	/* multiple jobs (easily induced via no_alloc) sometimes result
	 * in slow message responses and timeouts. Raise the timeout
	 * to 5 seconds for no_alloc option only */
	if (opt.no_alloc)
		timeout = 5;
  again:
	if (slurm_receive_msg(fd, msg, timeout) < 0) {
		if (errno == EINTR)
			goto again;
		error("slurm_receive_msg[%u.%u.%u.%u]: %m",
		      uc[0],uc[1],uc[2],uc[3]);
		xfree(msg);
	} else {

		msg->conn_fd = fd;
		_handle_msg(job, msg); /* handle_msg frees msg */
	}

	slurm_close_accepted_conn(fd);
	return;
}


static void
_set_jfds_nonblocking(srun_job_t *job)
{
	int i;
	for (i = 0; i < job->njfds; i++) 
		fd_set_nonblocking(job->jfd[i]);
}

/*
 *  Call poll() with a timeout. (timeout argument is in seconds)
 * NOTE: One extra FD for incoming slurmctld messages
 */
static int
_do_poll(srun_job_t *job, struct pollfd *fds, int timeout)
{
	nfds_t nfds = (job->njfds + 1);
	int rc, to;

	if (timeout > 0)
		to = timeout * 1000;
	else
		to = timeout;

	while ((rc = poll(fds, nfds, to)) < 0) {
		switch (errno) {
		case EAGAIN:
		case EINTR:  continue;
		case ENOMEM:
		case EINVAL:
		case EFAULT: fatal("poll: %m");
		default:     error("poll: %m. Continuing...");
			     continue;
		}
	}

	return rc;
}


/*
 *  Get the next timeout in seconds from now.
 */
static int 
_get_next_timeout(srun_job_t *job)
{
	int timeout = -1;

	if (!job->ltimeout && !job->etimeout)
		return -1;

	if (!job->ltimeout)
		timeout = job->etimeout - time(NULL);
	else if (!job->etimeout)
		timeout = job->ltimeout - time(NULL);
	else 
		timeout = job->ltimeout < job->etimeout ? 
			  job->ltimeout - time(NULL) : 
			  job->etimeout - time(NULL);

	return timeout;
}

/*
 *  Handle the two poll timeout cases:
 *    1. Job launch timed out
 *    2. Exit timeout has expired (either print a message or kill job)
 */
static void
_do_poll_timeout(srun_job_t *job)
{
	time_t now = time(NULL);

	if ((job->ltimeout > 0) && (job->ltimeout <= now)) 
		_confirm_launch_complete(job);

	if ((job->etimeout > 0) && (job->etimeout <= now)) {
		if (!opt.max_wait)
			info("Warning: first task terminated %ds ago", 
			     opt.max_exit_timeout);
		else {
			error("First task exited %ds ago", opt.max_wait);
			report_task_status(job);
			update_job_state(job, SRUN_JOB_FAILED);
		}
		job->etimeout = 0;
	}
}

/* NOTE: One extra FD for incoming slurmctld messages */
static void 
_msg_thr_poll(srun_job_t *job)
{
	struct pollfd *fds;
	int i;

	fds = xmalloc((job->njfds + 1) * sizeof(*fds));

	_set_jfds_nonblocking(job);

	for (i = 0; i < job->njfds; i++)
		_poll_set_rd(fds[i], job->jfd[i]);
	_poll_set_rd(fds[i], slurmctld_fd);

	while (!_job_msg_done(job)) {
		if (_do_poll(job, fds, _get_next_timeout(job)) == 0) {
			_do_poll_timeout(job);
			continue;
		}

		for (i = 0; i < (job->njfds + 1) ; i++) {
			unsigned short revents = fds[i].revents;
			if ((revents & POLLERR) || 
			    (revents & POLLHUP) ||
			    (revents & POLLNVAL))
				error("poll error on jfd %d: %m", fds[i].fd);
			else if (revents & POLLIN) 
				_accept_msg_connection(job, i);
		}
		
	}
	xfree(fds);	/* if we were to break out of while loop */
}

void *
msg_thr(void *arg)
{
	srun_job_t *job = (srun_job_t *) arg;
	forked_msg_pipe_t *par_msg = job->forked_msg->par_msg;
	int done = 0;
	debug3("msg thread pid = %lu", (unsigned long) getpid());

	slurm_uid = (uid_t) slurm_get_slurm_user_id();

	_msg_thr_poll(job);

	close(par_msg->msg_pipe[1]); // close excess fildes
	debug3("msg thread done");	
	return (void *)1;
}

void *
par_thr(void *arg)
{
	srun_job_t *job = (srun_job_t *) arg;
	forked_msg_pipe_t *par_msg = job->forked_msg->par_msg;
	forked_msg_pipe_t *msg_par = job->forked_msg->msg_par;
	int c;
	pipe_enum_t type=0;
	int tid=-1;
	int nodeid=-1;
	int status;
	debug3("par thread pid = %lu", (unsigned long) getpid());

	//slurm_uid = (uid_t) slurm_get_slurm_user_id();
	close(msg_par->msg_pipe[0]); // close read end of pipe
	close(par_msg->msg_pipe[1]); // close write end of pipe 
	while(read(par_msg->msg_pipe[0], &c, sizeof(int)) == sizeof(int)) {
		// getting info from msg thread
		if(type == PIPE_NONE) {
			debug2("got type %d\n",c);
			type = c;
			continue;
		} 

		if(type == PIPE_JOB_STATE) {
			debug("PIPE_JOB_STATE, c = %d", c);
			update_job_state(job, c);
		} else if(type == PIPE_TASK_STATE) {
			debug("PIPE_TASK_STATE");
			if(tid == -1) {
				tid = c;
				continue;
			}
			slurm_mutex_lock(&job->task_mutex);
			job->task_state[tid] = c;
			if(c == SRUN_TASK_FAILED)
				tasks_exited++;
			slurm_mutex_unlock(&job->task_mutex);
			if (tasks_exited == opt.nprocs) {
				debug2("all tasks exited");
				update_job_state(job, SRUN_JOB_TERMINATED);
			}
			tid = -1;
		} else if(type == PIPE_TASK_EXITCODE) {
			debug("PIPE_TASK_EXITCODE");
			if(tid == -1) {
				debug("  setting tid");
				tid = c;
				continue;
			}
			slurm_mutex_lock(&job->task_mutex);
			debug("  setting task %d exitcode %d", tid, c);
			job->tstatus[tid] = c;
			slurm_mutex_unlock(&job->task_mutex);
			tid = -1;
		} else if(type == PIPE_HOST_STATE) {
			if(tid == -1) {
				tid = c;
				continue;
			}
			slurm_mutex_lock(&job->task_mutex);
			job->host_state[tid] = c;
			slurm_mutex_unlock(&job->task_mutex);
			tid = -1;
		} else if(type == PIPE_SIGNALED) {
			slurm_mutex_lock(&job->state_mutex);
			job->signaled = c;
			slurm_mutex_unlock(&job->state_mutex);
		} else if(type == PIPE_MPIR_PROCTABLE_SIZE) {
			if(MPIR_proctable_size == 0) {
				MPIR_proctable_size = c;
				MPIR_proctable = 
					xmalloc(sizeof(MPIR_PROCDESC) * c);
			}		
		} else if(type == PIPE_MPIR_TOTALVIEW_JOBID) {
			totalview_jobid = NULL;
			xstrfmtcat(totalview_jobid, "%lu", c);
		} else if(type == PIPE_MPIR_PROCDESC) {
			if(tid == -1) {
				tid = c;
				continue;
			}
			if(nodeid == -1) {
				nodeid = c;
				continue;
			}
			MPIR_PROCDESC *tv   = &MPIR_proctable[tid];
			tv->host_name       = job->host[nodeid];
			tv->executable_name = remote_argv[0];
			tv->pid             = c;
			tid = -1;
			nodeid = -1;
		} else if(type == PIPE_MPIR_DEBUG_STATE) {
			MPIR_debug_state = c;
			MPIR_Breakpoint();
			if (opt.debugger_test)
				_dump_proctable(job);
		}
		type = PIPE_NONE;
		
	}
	close(par_msg->msg_pipe[0]); // close excess fildes    
	close(msg_par->msg_pipe[1]); // close excess fildes
	if(waitpid(par_msg->pid,&status,0)<0) // wait for pid to finish
		return;// there was an error
	debug3("par thread done");
	return (void *)1;
}

int 
msg_thr_create(srun_job_t *job)
{
	int i, retries = 0;
	pthread_attr_t attr;
	int c;
	
	job->forked_msg = xmalloc(sizeof(forked_msg_t));
	job->forked_msg->par_msg = xmalloc(sizeof(forked_msg_pipe_t));
	job->forked_msg->msg_par = xmalloc(sizeof(forked_msg_pipe_t));
	
	set_allocate_job(job);

	for (i = 0; i < job->njfds; i++) {
		if ((job->jfd[i] = slurm_init_msg_engine_port(0)) < 0)
			fatal("init_msg_engine_port: %m");
		if (slurm_get_stream_addr(job->jfd[i], &job->jaddr[i]) 
		    < 0)
			fatal("slurm_get_stream_addr: %m");
		debug("initialized job control port %d\n",
		      ntohs(((struct sockaddr_in)
			     job->jaddr[i]).sin_port));
	}

	if (pipe(job->forked_msg->par_msg->msg_pipe) == -1) {
		error("pipe():  %m"); 
		return SLURM_ERROR;
	}
	if (pipe(job->forked_msg->msg_par->msg_pipe) == -1) {
		error("pipe():  %m"); 
		return SLURM_ERROR;
	}
	debug2("created the pipes for communication");

	/* retry fork for super-heavily loaded systems */
	for (i = 0; ; i++) {
		if((job->forked_msg->par_msg->pid = fork()) != -1)
			break;
		if (i < 3)
			usleep(1000);
		else {
			error("fork(): %m");
			return SLURM_ERROR;
		}
	}

	if (job->forked_msg->par_msg->pid == 0) {
		/* child */
#ifdef DISABLE_LOCALTIME
		disable_localtime();
#endif                   
		setsid();  
		message_thread = 1;
		close(job->forked_msg->
		      par_msg->msg_pipe[0]); // close read end of pipe
		close(job->forked_msg->
		      msg_par->msg_pipe[1]); // close write end of pipe
		slurm_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
		while ((errno = pthread_create(&job->jtid, &attr, &msg_thr,
					    (void *)job))) {
			if (++retries > MAX_RETRIES)
				fatal("Can't create pthread");
			sleep(1);
		}
		debug("Started msg to parent server thread (%lu)", 
		      (unsigned long) job->jtid);
		
		while(read(job->forked_msg->
			   msg_par->msg_pipe[0],&c,sizeof(int))>0)
			; // make sure my parent doesn't leave me hangin
		
		close(job->forked_msg->
		      msg_par->msg_pipe[0]); // close excess fildes    
		xfree(job->forked_msg->par_msg);	
		xfree(job->forked_msg->msg_par);	
		xfree(job->forked_msg);	
		_exit(0);
	} else {
		/* parent */

		slurm_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
		while ((errno = pthread_create(&job->jtid, &attr, &par_thr, 
					    (void *)job))) {
			if (++retries > MAX_RETRIES)
				fatal("Can't create pthread");
			sleep(1);	/* sleep and try again */
		}

		debug("Started parent to msg server thread (%lu)", 
		      (unsigned long) job->jtid);
	}

	
	return SLURM_SUCCESS;
}

static void
_print_pid_list(const char *host, int ntasks, uint32_t *pid, 
		char *executable_name)
{
	if (_verbose) {
		int i;
		hostlist_t pids = hostlist_create(NULL);
		char buf[1024];
		
		for (i = 0; i < ntasks; i++) {
			snprintf(buf, sizeof(buf), "pids:%d", pid[i]);
			hostlist_push(pids, buf);
		}
		
		hostlist_ranged_string(pids, sizeof(buf), buf);
		verbose("%s: %s %s", host, executable_name, buf);
	}
}

/* Set up port to handle messages from slurmctld */
extern slurm_fd slurmctld_msg_init(void)
{
	slurm_addr slurm_address;
	char hostname[64];
	uint16_t port;

	if (slurmctld_fd)	/* May set early for queued job allocation */
		return slurmctld_fd;

	if (opt.allocate && opt.noshell)
		return -1;

	slurmctld_fd = -1;
	slurmctld_comm_addr.hostname = NULL;
	slurmctld_comm_addr.port = 0;

	if ((slurmctld_fd = slurm_init_msg_engine_port(0)) < 0)
		fatal("slurm_init_msg_engine_port error %m");
	if (slurm_get_stream_addr(slurmctld_fd, &slurm_address) < 0)
		fatal("slurm_get_stream_addr error %m");
	fd_set_nonblocking(slurmctld_fd);
	/* hostname is not set, so slurm_get_addr fails
	slurm_get_addr(&slurm_address, &port, hostname, sizeof(hostname)); */
	port = slurm_address.sin_port;
	getnodename(hostname, sizeof(hostname));
	slurmctld_comm_addr.hostname = xstrdup(hostname);
	slurmctld_comm_addr.port     = ntohs(port);
	debug2("slurmctld messasges to host=%s,port=%u", 
			slurmctld_comm_addr.hostname, 
			slurmctld_comm_addr.port);

	return slurmctld_fd;
}


