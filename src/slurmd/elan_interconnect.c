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
int launch_tasks ( launch_tasks_request_msg_t * launch_msg )
{
	pthread_atfork ( NULL , NULL , pthread_fork_child_after ) ;
	return interconnect_init ( launch_msg );
}

/* Contains interconnect specific setup instructions and then calls 
 * fan_out_task_launch */
int interconnect_init ( launch_tasks_request_msg_t * launch_msg )
{
	pid_t pid;
	int i=0;

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
			while(true)
			{
			if (qsw_prgdestroy( launch_msg -> qsw_job ) < 0) {
				pid_t pids[256];
				int npids;
				int i;
				
				slurm_perror("qsw_prgdestroy");
				debug ("qsw_prgdestroy iteration %i, %m errno: %i", i , errno);
				sleep (1);
				i++ ;

				if (rms_prginfo( launch_msg -> qsw_job -> j_prognum , sizeof(pids)/sizeof(pid_t), pids, &npids) < 0) {
					perror("rms_prginfo");
				}
				printf("pids");
				for (i = 0; i < npids; i++)
					printf("%d\n", pids[i]);
				printf("\n");
				continue ;
				return SLURM_ERROR ;
			}
			break ;
			debug ("successfull qsw_prgdestroy");
			}
			return SLURM_SUCCESS ;
	}

	/* Process 2: */
	info("qsw_prog_init called from process %ld", getpid());
	if (qsw_prog_init(launch_msg->qsw_job, launch_msg->uid) < 0) 
	{
		slurm_perror("qsw_prog_init");
		_exit(1) ;
	}
	
	fan_out_task_launch ( launch_msg ) ;
	_exit(0) ;
	return SLURM_ERROR ;
}

int interconnect_set_capabilities ( task_start_t * task_start )
{
	pid_t pid;
	int nodeid, nnodes, nprocs, procid; 

	nodeid = task_start->launch_msg->srun_node_id;
	nnodes = task_start->launch_msg->nnodes;
	procid = task_start->local_task_id;
	nprocs = task_start->launch_msg->nprocs;

	info("nodeid=%d nnodes=%d procid=%d nprocs=%d", 
			nodeid, nnodes, procid, nprocs);
	info("setting capability in process %ld", getpid());

	if (qsw_setcap( task_start -> launch_msg -> qsw_job, procid) < 0) {
		slurm_perror("qsw_setcap");
		return SLURM_ERROR ;
	}

	pid = fork();
	switch (pid) {
		case -1:        /* error */
			slurm_perror("fork");
			return SLURM_ERROR ;
		case 0:         /* child falls thru */
			return SLURM_SUCCESS ;
		default:        /* parent */
			if (waitpid(pid, NULL, 0) < 0) 
			{
				slurm_perror("waitpid");
				return SLURM_ERROR ;
			}
			_exit ( 0 ) ;
			return SLURM_SUCCESS ;
	}
}

/*
 * Set environment variables needed by QSW MPICH / libelan.
 */
int interconnect_env(char ***env, int *envc, int nodeid, int nnodes, 
	             int procid, int nprocs)
{
	if (setenvpf(env, envc, "RMS_RANK=%d", procid) < 0)
		return -1;
	if (setenvpf(env, envc, "RMS_NODEID=%d", nodeid) < 0)
		return -1;
	if (setenvpf(env, envc, "RMS_PROCID=%d", procid) < 0)
		return -1;
	if (setenvpf(env, envc, "RMS_NNODES=%d", nnodes) < 0)
		return -1;
	if (setenvpf(env, envc, "RMS_NPROCS=%d", nprocs) < 0)
		return -1;
	return 0;
}


void pthread_fork_child ( )
{
}
