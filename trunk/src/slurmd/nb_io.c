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
int forward_io ( task_start_t * task_start ) 
{
	//pthread_attr_t pthread_attr ;

	//pthread_attr_init( & pthread_attr ) ;
	/* set detatch state */
	/*pthread_attr_setdetachstate ( & pthread_attr , PTHREAD_CREATE_DETACHED ) ;*/
	//if ( pthread_create ( & task_start->io_pthread_id[STDIN_FILENO] , NULL , do_nbio , task_start ) )
	//{
	//	return SLURM_FAILURE ;
	//}
	//return SLURM_SUCCESS ;
	return do_nbio ( task_start ) ;
}

int wait_on_io_threads ( task_start_t * task_start ) 
{
	//pthread_join ( task_start->io_pthread_id[STDIN_FILENO] , NULL ) ;
	info ( "%i: nbio exit" , task_start -> local_task_id ) ;
	/* thread join on stderr or stdout signifies task termination we should kill the stdin thread */
	return SLURM_SUCCESS ;
}

int iotype_init_pipes ( int * pipes )
{
	int i ;
	for ( i=0 ; i < 6 ; i ++ ) 
	{
		slurm_set_stream_non_blocking ( pipes[0] ) ;
	}
	return SLURM_SUCCESS ;
}

