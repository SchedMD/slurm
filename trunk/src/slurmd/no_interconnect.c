#include <src/common/slurm_protocol_api.h>
#include <src/slurmd/task_mgr.h>
#include <src/slurmd/interconnect.h>

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
	return fan_out_task_launch ( launch_msg ) ;
}

int interconnect_set_capabilities ( task_start_t * task_start ) 
{
	return SLURM_SUCCESS ;
}

/*
 * Set environment variables needed by QSW MPICH / libelan.
 */
int interconnect_env(char ***env, int *envc, int nodeid, int nnodes, 
	             int procid, int nprocs)
{
	return SLURM_SUCCESS ;
}


