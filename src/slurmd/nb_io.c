/*****************************************************************************\
 *  nb_io.c - Non-blocking I/O support
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
#include <src/slurmd/nbio.h>

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
int forward_io(task_start_t * task_start)
{
	//pthread_attr_t pthread_attr ;

	//pthread_attr_init( & pthread_attr ) ;
	/* set detatch state */
	/*pthread_attr_setdetachstate ( & pthread_attr , PTHREAD_CREATE_DETACHED ) ; */
	//if ( pthread_create ( & task_start->io_pthread_id[STDIN_FILENO] , NULL , do_nbio , task_start ) )
	//{
	//      return SLURM_FAILURE ;
	//}
	//return SLURM_SUCCESS ;
	return do_nbio(task_start);
}

int wait_on_io_threads(task_start_t * task_start)
{
	//pthread_join ( task_start->io_pthread_id[STDIN_FILENO] , NULL ) ;
	debug3("%i: nbio exit", task_start->local_task_id);
	/* thread join on stderr or stdout signifies task termination 
	 * we should kill the stdin thread 
	 */
	return SLURM_SUCCESS;
}

int iotype_init_pipes(int *pipes)
{
	int i;
	for (i = 0; i < 6; i++) {
		slurm_set_stream_non_blocking(pipes[0]);
	}
	return SLURM_SUCCESS;
}
