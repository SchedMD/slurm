/*
 * $Id$
 * $Source$
 *
 * Demo the routines in common/qsw.c
 * This can run mping on the local node (uses shared memory comms).
 * ./runqsw /usr/lib/mpi-test/mping 1 1024
 */
#define HAVE_LIBELAN3 

#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include <src/common/bitstring.h>
#include <src/common/qsw.h>
#include <src/common/slurm_errno.h>
#include <src/common/slurm_protocol_api.h>
#include <src/slurmd/task_mgr.h>
#include <src/slurmd/interconnect.h>

static int setenvf(const char *fmt, ...) ;
static int do_env(int nodeid, int nnodes, int procid, int nprocs) ;
/* exported module funtion to launch tasks */
/*launch_tasks should really be named launch_job_step*/
int launch_tasks ( launch_tasks_request_msg_t * launch_msg )
{
	return interconnect_init ( launch_msg );
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
			slurm_perror("fork");
			return SLURM_ERROR ;
		case 0: /* child falls thru */
			break;
		default: /* parent */
			if (waitpid(pid, NULL, 0) < 0) 
			{
				slurm_perror("wait");
				return SLURM_ERROR ;
			}
			if (qsw_prgdestroy( launch_msg -> qsw_job ) < 0) {
				slurm_perror("qsw_prgdestroy");
				return SLURM_ERROR ;
			}
			return SLURM_SUCCESS ;
	}

	/* Process 2: */
	if (qsw_prog_init(launch_msg->qsw_job, launch_msg->uid) < 0) 
	{
		slurm_perror("qsw_prog_init");
		return SLURM_ERROR ;
	}
	
	return fan_out_task_launch ( launch_msg ) ;
}

int interconnect_set_capabilities ( task_start_t * task_start )
{
	pid_t pid;
	int nodeid, nnodes, nprocs, procid; 

	nodeid = task_start->launch_msg->srun_node_id;
	nnodes = task_start->launch_msg->nnodes;
	procid = task_start->local_task_id;
	nprocs = task_start->launch_msg->nprocs;

	if (qsw_setcap( task_start -> launch_msg -> qsw_job, procid) < 0) {
		slurm_perror("qsw_setcap");
		return SLURM_ERROR ;
	}
	if (do_env(nodeid, nnodes, procid, nprocs) < 0) {
		slurm_perror("do_env");
		return SLURM_ERROR ;
	}

	pid = fork();
	switch (pid) {
		case -1:        /* error */
			slurm_perror("fork");
			return SLURM_ERROR ;
		case 0:         /* child falls thru */
			return SLURM_SUCCESS ;
			break;
		default:        /* parent */
			if (waitpid(pid, NULL, 0) < 0) 
			{
				slurm_perror("waitpid");
				return SLURM_ERROR ;
			}
			return SLURM_SUCCESS ;
	}
}

/*
 * Set a variable in the callers environment.  Args are printf style.
 * XXX Space is allocated on the heap and will never be reclaimed.
 * Example: setenvf("RMS_RANK=%d", rank);
 */
static int setenvf(const char *fmt, ...) 
{
	va_list ap;
	char buf[BUFSIZ];
	char *bufcpy;
		    
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	bufcpy = strdup(buf);
	if (bufcpy == NULL)
		return -1;
	return putenv(bufcpy);
}

/*
 * Set environment variables needed by QSW MPICH / libelan.
 */
static int do_env(int nodeid, int nnodes, int procid, int nprocs)
{
	if (setenvf("RMS_RANK=%d", procid) < 0)
		return -1;
	if (setenvf("RMS_NODEID=%d", nodeid) < 0)
		return -1;
	if (setenvf("RMS_PROCID=%d", procid) < 0)
		return -1;
	if (setenvf("RMS_NNODES=%d", nnodes) < 0)
		return -1;
	if (setenvf("RMS_NPROCS=%d", nprocs) < 0)
		return -1;
	return 0;
}
