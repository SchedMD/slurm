#include <stdlib.h>
#include <sys/types.h>
#include <pwd.h>
#include <grp.h>
#include <sys/wait.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

#include <src/common/log.h>
#include <src/common/list.h>
#include <src/common/xmalloc.h>
#include <src/common/slurm_protocol_api.h>
#include <src/common/slurm_errno.h>
#include <src/common/util_signals.h>

#include <src/slurmd/task_mgr.h>
#include <src/slurmd/shmem_struct.h>
#include <src/slurmd/circular_buffer.h>
#include <src/slurmd/io.h>
#include <src/slurmd/pipes.h>
#include <src/slurmd/reconnect_utils.h>

/* global variables */


/******************************************************************
 *task launch method call hierarchy
 *
 *launch_tasks()
 *	interconnect_init()
 *		fan_out_task_launch() (pthread_create)
 *			task_exec_thread() (fork) for task exec
 *			task_exec_thread() (pthread_create) for io piping 
 ******************************************************************/

int launch_task(task_start_t * task_start)
{
	pid_t pid = fork();
	switch (pid) {
	case -1:
		slurm_perror("fork");
		return SLURM_ERROR;
		break;
	case 0:
		task_exec_thread(task_start);
		_exit(0);
	default:
		task_start->pthread_id = pid;
		return SLURM_SUCCESS;
		break;

	}
	return SLURM_SUCCESS;
}

int wait_for_tasks(launch_tasks_request_msg_t * launch_msg,
		   task_start_t ** task_start)
{
	int i;
	int rc;
	for (i = 0; i < launch_msg->tasks_to_launch; i++) {
		rc = waitpid(task_start[i]->pthread_id, NULL, 0);
		debug3 ("fan_out_task_launch: thread %i pthread_id %i joined ",
		     i, task_start[i]->pthread_id);
	}
	return SLURM_SUCCESS;
}

int kill_launched_tasks(launch_tasks_request_msg_t * launch_msg,
			task_start_t ** task_start, int i)
{
	/*
	   int rc ;
	   for (  i-- ; i >= 0  ; i -- ) {
	           rc = kill(task_start[i]->pthread_id, SIGKILL);
	   }
	 */
	return SLURM_SUCCESS;
}
