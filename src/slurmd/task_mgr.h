/*****************************************************************************\
 *  task_mgr.h - 
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

#ifndef _TASK_MGR_H
#define _TASK_MGR_H

#if HAVE_CONFIG_H
#  include <config.h>
#  if HAVE_INTTYPES_H
#    include <inttypes.h>
#  else
#    if HAVE_STDINT_H
#      include <stdint.h>
#    endif
#  endif			/* HAVE_INTTYPES_H */
#else				/* !HAVE_CONFIG_H */
#  include <inttypes.h>
#endif				/*  HAVE_CONFIG_H */

#include <src/common/slurm_protocol_api.h>

#define STDIN_IO_THREAD 0
#define STDOUT_IO_THREAD 1
#define STDERR_IO_THREAD 2
#define STDSIG_IO_THREAD 3
#define SLURMD_NUMBER_OF_IO_THREADS 4
#define SLURMD_IO_MAX_BUFFER_SIZE 4096

/* function prototypes */
/* launch_tasks
 * called by the rpc method to initiate task launch
 * IN launch_msg	- launch task messge
 * RET int		- return_code
 */
int launch_tasks(launch_tasks_request_msg_t * launch_msg);

/* kill_tasks
 * called by the rpc method to kill a job_step or set of task launches
 * IN 			- kill task message
 * RET int 		- return_code
 */
int kill_tasks(kill_tasks_msg_t * kill_task_msg);

/* kill_all_tasks
 * kills all the currently running tasks used by shutdown code 
 * RET 		- return_code
 */
int kill_all_tasks();

/* reattach_tasks_streams
 * called by the reattach tasks rpc method to change the shmem task structs to point to a new destination for streams
 * IN req_msg		- reattach tasks streams message
 */
int reattach_tasks_streams(reattach_tasks_streams_msg_t * req_msg);

void *task_exec_thread(void *arg);

void pthread_fork_before(void);
void pthread_fork_parent_after(void);
void pthread_fork_child_after(void);

typedef struct task_start {
	/*task control thread id */
	pthread_t pthread_id;
	int thread_return;
	/*actual exec thread id */
	int exec_pid;
	int exec_thread_return;
	/*io threads ids */
	pthread_t io_pthread_id[SLURMD_NUMBER_OF_IO_THREADS];
	int io_thread_return[SLURMD_NUMBER_OF_IO_THREADS];
	launch_tasks_request_msg_t *launch_msg;
	int pipes[6];
	int sockets[2];
	int local_task_id;
	char addr_update;
	slurm_addr io_streams_dest;
} task_start_t;
#endif
