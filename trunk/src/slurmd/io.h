/*****************************************************************************\
 *  io.h -
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
