/*****************************************************************************\
 *  task_mgr.c - 
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Kevin Tew <tew1@llnl.gov> et. al.
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
 *  with ConMan; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#include <stdlib.h>
#include <sys/types.h>
#include <pwd.h>
#include <grp.h>
#include <sys/wait.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

#include <src/common/log.h>
#include <src/common/list.h>
#include <src/common/xmalloc.h>
#include <src/common/slurm_protocol_api.h>
#include <src/common/slurm_errno.h>
#include <src/common/util_signals.h>

#include <src/slurmd/task_mgr.h>
#include <src/slurmd/shmem_struct.h>
#include <src/slurmd/circular_buffer.h>
#include <src/slurmd/pipes.h>
#include <src/slurmd/io.h>
#include <src/slurmd/interconnect.h>

/* global variables */

/* prototypes */
int kill_task(task_t * task, int signal);
extern pid_t getsid(pid_t pid);

int send_task_exit_msg(int task_return_code, task_start_t * task_start);

/******************************************************************
 *task launch method call hierarchy
 *
 *launch_tasks()
 *	interconnect_init()
 *		fan_out_task_launch() (pthread_create)
 *			task_exec_thread() (fork) for task exec
 *			task_exec_thread() (pthread_create) for io piping 
 ******************************************************************/

int fan_out_task_launch(launch_tasks_request_msg_t * launch_msg)
{
	int i;
	int session_id;

	/* shmem work - see slurmd.c shmem_seg this is probably not needed */
	slurmd_shmem_t *shmem_ptr = get_shmem();

	/* alloc a job_step object in shmem for this launch_tasks request 
	 * launch_tasks should really be named launch_job_step             
	 */
	job_step_t *curr_job_step = 
		alloc_job_step(shmem_ptr, launch_msg->job_id, 
			       launch_msg->job_step_id);

	/* task pointer that will point to shmem task structures as they 
	 * are allocated 
	 */
	task_t *curr_task = NULL;

	/* array of pointers used in this function to point to the 
	 * task_start structure for each task to be launched
	 */
	task_start_t *task_start[launch_msg->tasks_to_launch];

	debug3("entered fan_out_task_launch()");
	debug("msg->job_step_id = %d", launch_msg->job_step_id);

	if ((session_id = setsid()) == SLURM_ERROR) {
		error("set sid failed: %m");
		if ((session_id = getsid(0)) == SLURM_ERROR) {
			error("getsid also failed");
		}
	}

	curr_job_step->session_id = session_id;


	debug3("going to launch %d tasks", launch_msg->tasks_to_launch);
	/* launch requested number of threads 
	 */
	for (i = 0; i < launch_msg->tasks_to_launch; i++) {
		curr_task = alloc_task(shmem_ptr, curr_job_step);
		task_start[i] = &curr_task->task_start;
		curr_task->task_id = launch_msg->global_task_ids[i];

		/* fill in task_start struct */
		task_start[i]->launch_msg = launch_msg;
		task_start[i]->local_task_id = i;
		task_start[i]->io_streams_dest = launch_msg->streams;

		debug("going to launch task %d", i);
		if (launch_task(task_start[i])) {
			error("launch_task error ");
			goto kill_tasks_label;
		}
		debug("task %i launched", i);
	}

	/* wait for all the launched threads to finish 
	 */
	wait_for_tasks(launch_msg, task_start);

	goto return_label;

      kill_tasks_label:
	/* kill_launched_tasks(launch_msg, task_start, i); */
      return_label:
	/* can't release if this is the same process as the main daemon ie threads
	 * this is needed if we use forks
	 * rel_shmem ( shmem_ptr ) ; */
	deallocate_job_step(curr_job_step);
	return SLURM_SUCCESS;
}



void *task_exec_thread(void *arg)
{
	task_start_t *task_start = (task_start_t *) arg;
	launch_tasks_request_msg_t *launch_msg = task_start->launch_msg;
	int *pipes = task_start->pipes;
	int rc;
	int cpid;
	struct passwd *pwd;
	int task_return_code;
	int local_errno;
	log_options_t log_opts_def = LOG_OPTS_STDERR_ONLY;

	interconnect_set_capabilities(task_start);

	/* create pipes to read child stdin, stdout, sterr */
	init_parent_pipes(task_start->pipes);

#define FORK_ERROR -1
#define CHILD_PROCCESS 0
	switch ((cpid = fork())) {
	case FORK_ERROR:
		break;

	case CHILD_PROCCESS:
		/* log init stuff */
		log_init("slurmd", log_opts_def, 0, NULL);

		unblock_all_signals();

		posix_signal_ignore(SIGTTOU);	/* ignore tty output */
		posix_signal_ignore(SIGTTIN);	/* ignore tty input */
		posix_signal_ignore(SIGTSTP);	/* ignore user */

		/* setup interconnect specific environment variables
		 */
		interconnect_env(&launch_msg->env, &launch_msg->envc, 
				 launch_msg->srun_node_id,
				 launch_msg->nnodes,
				 launch_msg->global_task_ids[task_start->local_task_id],
				 launch_msg->nprocs);

		/* setup std stream pipes */
		setup_child_pipes(pipes);

		/* get passwd file info */
		if ((pwd = getpwuid(launch_msg->uid)) == NULL) {
			error("user id not found in passwd file");
			_exit(SLURM_FAILURE);
		}

		/* setgid and uid */
		if ((rc = setgid(pwd->pw_gid)) < 0) {
			error("setgid failed: %m ");
			_exit(SLURM_FAILURE);
		}

		/* initgroups */
		if (( getuid() == (uid_t)0 ) &&
		    ( initgroups(pwd->pw_name, pwd->pw_gid) ) < 0) {
			error("initgroups() failed: %m");
			//_exit(SLURM_FAILURE);
		}

		if ((rc = setuid(launch_msg->uid)) < 0) {
			error("setuid() failed: %m");
			_exit(SLURM_FAILURE);
		}

		/* run bash and cmdline */
		if ((chdir(launch_msg->cwd)) < 0) {
			error("cannot chdir to `%s,' going to /tmp instead",
					launch_msg->cwd);
			if ((chdir("/tmp")) < 0) {
				error("couldn't chdir to `/tmp' either. dying.");
				_exit(SLURM_FAILURE);
			}
		}

		execve(launch_msg->argv[0], launch_msg->argv, launch_msg->env);

		/* error if execve returns
		 * clean up */
		error("execve(): %s: %m", launch_msg->argv[0]);
		close(STDIN_FILENO);
		close(STDOUT_FILENO);
		close(STDERR_FILENO);
		local_errno = errno;
		_exit(local_errno);
		break;

	default:		/*parent proccess */
		debug("forked pid %ld", cpid);
		task_start->exec_pid = cpid;
		/* order below is very important 
		 * deadlock can occur if you mess with it - ask me how I know :)
		 */

		debug3("calling setup_parent_pipes");
		/* 1   */ setup_parent_pipes(task_start->pipes);
		debug3("calling forward_io");
		/* 1.5 */ forward_io(arg);
		debug3("calling waitpid(%ld)", cpid);
		/* 2   */ waitpid(cpid, &task_return_code, 0);
		/* 3   */ wait_on_io_threads(task_start);

		send_task_exit_msg(task_return_code, task_start);

		break;
	}
	return (void *) SLURM_SUCCESS; /* XXX: I think this is wrong */
}


int send_task_exit_msg(int task_return_code, task_start_t * task_start)
{
	slurm_msg_t resp_msg;
	task_exit_msg_t task_exit;

	/* init task_exit_message */
	task_exit.return_code = task_return_code;
	task_exit.task_id =
	    task_start->launch_msg->global_task_ids[task_start->local_task_id];

	/* init slurm_msg_t */
	resp_msg.address = task_start->launch_msg->response_addr;
	resp_msg.data = &task_exit;
	resp_msg.msg_type = MESSAGE_TASK_EXIT;

	debug("sending task exit code %d", task_return_code);

	/* send message */
	return slurm_send_only_node_msg(&resp_msg);
}

int kill_tasks(kill_tasks_msg_t * kill_task_msg)
{
	int i = 0;
	int error_code = SLURM_SUCCESS;

	/* get shmemptr 
	 */
	slurmd_shmem_t *shmem_ptr = get_shmem();
	
	task_t *task_ptr;

	/* find job step 
	 */
	job_step_t *job_step_ptr = 
		find_job_step(shmem_ptr, kill_task_msg->job_id,
			      kill_task_msg->job_step_id);

	debug("request to kill step %d.%d with signal %d", 
	      kill_task_msg->job_id,
	      kill_task_msg->job_step_id, 
	      kill_task_msg->signal);

	if (job_step_ptr == (void *) SLURM_ERROR) 
		slurm_seterrno_ret(ESLURMD_ERROR_FINDING_JOB_STEP_IN_SHMEM);

	/* cycle through job_step and kill tasks */
	task_ptr = job_step_ptr->head_task;

	while (task_ptr != NULL) {
		debug3("killing task %i of jobid %i , of job_step %i ", i,
		       kill_task_msg->job_id, kill_task_msg->job_step_id);
		kill_task(task_ptr, kill_task_msg->signal);
		task_ptr = task_ptr->next;
		i++;
		debug3("next task_ptr %i ", task_ptr);
	}
	debug3("leaving kill_tasks");
	return error_code;
}

int kill_all_tasks()
{
	int error_code = SLURM_SUCCESS;

	/* get shmemptr */
	slurmd_shmem_t *shmem = get_shmem();

	int i;
	pthread_mutex_lock(&shmem->mutex);
	for (i = 0; i < MAX_JOB_STEPS; i++) {
		if (shmem->job_steps[i].used == true) {
			/* cycle through job_step and kill tasks */
			task_t *task_ptr = shmem->job_steps[i].head_task;
			while (task_ptr != NULL) {
				kill_task(task_ptr, SIGKILL);
				task_ptr = task_ptr->next;
			}
		}
	}
	pthread_mutex_unlock(&shmem->mutex);
	return error_code;

}

int kill_task(task_t * task, int signal)
{
	debug3("killing proccess %i, with signal %i",
	       task->task_start.exec_pid, signal);
	return kill(task->task_start.exec_pid, signal);
}

int reattach_tasks_streams(reattach_tasks_streams_msg_t * req_msg)
{
	int i;
	int error_code = SLURM_SUCCESS;
	/* get shmemptr */
	slurmd_shmem_t *shmem_ptr = get_shmem();

	/* find job step */
	job_step_t *job_step_ptr =
	    find_job_step(shmem_ptr, req_msg->job_id,
			  req_msg->job_step_id);

	/* cycle through tasks and set streams address */
	for (i = 0; i < req_msg->tasks_to_reattach; i++) {
		task_t *task =
		    find_task(job_step_ptr, req_msg->global_task_ids[i]);
		if (task != NULL) {
			task->task_start.io_streams_dest =
			    req_msg->streams;
		} else {
			error("task id not found job_id %i "
			      "job_step_id %i global_task_id %i",
			      req_msg->job_id, req_msg->job_step_id,
			      req_msg->global_task_ids[i]);
		}
	}
	return error_code;
}

void pthread_fork_child_after(void)
{
	log_reinit();
}
