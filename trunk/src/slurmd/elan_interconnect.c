/*****************************************************************************\
 *  elan_interconnect.c - Demo the routines in common/qsw.c
 *  This can run mping on the local node (uses shared memory comms).
 *  ./runqsw /usr/lib/mpi-test/mping 1 1024
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

#define HAVE_LIBELAN3 

#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include <src/common/xmalloc.h>
#include <src/common/xstring.h>
#include <src/common/bitstring.h>
#include <src/common/log.h>
#include <src/common/qsw.h>
#include <src/common/slurm_errno.h>
#include <src/common/slurm_protocol_api.h>
#include <src/slurmd/task_mgr.h>
#include <src/slurmd/interconnect.h>
#include <src/slurmd/setenvpf.h>



/* exported module funtion to launch tasks */
/*launch_tasks should really be named launch_job_step*/
int 
launch_tasks(launch_tasks_request_msg_t * launch_msg)
{
	pthread_atfork(NULL, NULL, pthread_fork_child_after);
	debug("launch_tasks: calling interconnect_init()");
	return interconnect_init(launch_msg);
}

static int 
_wait_and_destroy_prg(qsw_jobinfo_t qsw_job, pid_t pid)
{
	int i = 0;
	int sleeptime = 1;

	debug3("waiting to destory program description...");
	if (waitpid(pid, NULL, 0) < 0) {
		error("waitpid: %m");
		return SLURM_ERROR;
	}

	while(qsw_prgdestroy(qsw_job) < 0) {
		i++;
		error("qsw_prgdestroy: %m");
		if (errno == ESRCH)
			break;
		if (i == 1) {
			debug("sending SIGTERM to remaining tasks");
			qsw_prgsignal(qsw_job, SIGTERM);
		} else {
			debug("sending SIGKILL to remaining tasks");
			qsw_prgsignal(qsw_job, SIGKILL);
		}

		debug("going to sleep for %d seconds and try again", sleeptime);
		sleep(sleeptime*=2);
	}

	return SLURM_SUCCESS;
}

/* Contains interconnect specific setup instructions and then calls 
 * fan_out_task_launch */
int interconnect_init ( launch_tasks_request_msg_t * launch_msg )
{
	pid_t pid;

	/* Process 1: */
	switch ((pid = fork())) 
	{
		case -1:
			error ("elan_interconnect_init fork(): %m");
			return SLURM_ERROR ;
		case 0: /* child falls thru */
			break;
		default: /* parent */
			return _wait_and_destroy_prg(launch_msg->qsw_job, pid);
	}

	/* Process 2: */
	debug("calling qsw_prog_init from process %ld", getpid());
	if (qsw_prog_init(launch_msg->qsw_job, launch_msg->uid) < 0) {
		error ("elan interconnect_init: qsw_prog_init: %m");
		/* we may lose the following info if not logging to stderr */
		qsw_print_jobinfo(stderr, launch_msg->qsw_job);
		_exit(1) ;
	}
	
	fan_out_task_launch(launch_msg);
	_exit(0);

	return SLURM_ERROR; /* XXX: why? */
}

int interconnect_set_capabilities(task_start_t * task_start)
{
	pid_t pid;
	int nodeid, nnodes, nprocs, procid; 

	nodeid = task_start->launch_msg->srun_node_id;
	nnodes = task_start->launch_msg->nnodes;
	procid = task_start->local_task_id;
	nprocs = task_start->launch_msg->nprocs;

	debug3("nodeid=%d nnodes=%d procid=%d nprocs=%d", 
	       nodeid, nnodes, procid, nprocs);
	debug3("setting capability in process %ld", getpid());
	if (qsw_setcap(task_start->launch_msg->qsw_job, procid) < 0) {
		error("qsw_setcap: %m");
		return SLURM_ERROR ;
	}

	pid = fork();
	switch (pid) {
		case -1:        /* error */
			error("set_capabilities: fork: %m");
			return SLURM_ERROR ;
		case 0:         /* child falls thru */
			return SLURM_SUCCESS ;
		default:        /* parent */
			if (waitpid(pid, NULL, 0) < 0) {
				error("set_capabilities: waitpid: %m");
				return SLURM_ERROR;
			}
			_exit(0); /* XXX; why does parent exit here but return
				          above on an error from waitpid??     */
			return SLURM_SUCCESS; /* huh? */
	}
}

/*
 * Set environment variables needed by QSW MPICH / libelan.
 */
int interconnect_env(char ***env, uint16_t *envc, int nodeid, int nnodes, 
	             int procid, int nprocs)
{
	int cnt = *envc;

	if (setenvpf(env, &cnt, "RMS_RANK=%d", procid) < 0)
		return -1;
	if (setenvpf(env, &cnt, "RMS_NODEID=%d", nodeid) < 0)
		return -1;
	if (setenvpf(env, &cnt, "RMS_PROCID=%d", procid) < 0)
		return -1;
	if (setenvpf(env, &cnt, "RMS_NNODES=%d", nnodes) < 0)
		return -1;
	if (setenvpf(env, &cnt, "RMS_NPROCS=%d", nprocs) < 0)
		return -1;
	return 0;
}


void pthread_fork_child()
{
}
