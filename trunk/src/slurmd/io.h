#ifndef _SLURMD_IO_H_
#define _SLURMD_IO_H_
#include <src/slurmd/task_mgr.h>

/* file descriptor defines */

#define MAX_TASKS_PER_LAUNCH 64

enum {
	CHILD_IN_PIPE = 0,
	CHILD_IN_RD_PIPE = 0,
	CHILD_IN_WR_PIPE = 1,
	CHILD_OUT_PIPE = 2,
	CHILD_OUT_RD_PIPE = 2,
	CHILD_OUT_WR_PIPE = 3,
	CHILD_ERR_PIPE = 4,
	CHILD_ERR_RD_PIPE = 4,
	CHILD_ERR_WR_PIPE = 5
};

/* prototypes */
enum {
	STDIN_OUT_SOCK = 0,
	SIG_STDERR_SOCK = 1
};


/* forward_io
 * controlling thread for io forwarding or io piping
 * IN task_arg		- task_arg structure containing per task launch info
 * RET int		- return_code
 */
int forward_io(task_start_t * task_arg);

/* individual io piping threads called by forward_io */
void *stdin_io_pipe_thread(void *arg);
void *stdout_io_pipe_thread(void *arg);
void *stderr_io_pipe_thread(void *arg);

/* wait_on_io_threads
 * called by exec_task_thread parent proccess to insure streams have been flushed before returning task exit status
 * IN task_arg		- task_arg structure containing per task launch info
 * RET int		- return_code
 */
int wait_on_io_threads(task_start_t * task_start);

int launch_task(task_start_t * task_start);

int wait_for_tasks(launch_tasks_request_msg_t * launch_msg,
		   task_start_t ** task_start);

int kill_launched_tasks(launch_tasks_request_msg_t * launch_msg,
			task_start_t ** task_start, int i);
#endif
