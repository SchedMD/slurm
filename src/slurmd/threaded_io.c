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
int forward_io ( task_start_t * task_start ) 
{
	pthread_attr_t pthread_attr ;

	//posix_signal_pipe_ignore ( ) ;

	/* open stdout*/
	connect_io_stream ( task_start , STDIN_OUT_SOCK ) ;
	/* open stderr*/
	connect_io_stream ( task_start , SIG_STDERR_SOCK ) ;
	
	/* spawn io pipe threads */
	/* set detatch state */
	pthread_attr_init( & pthread_attr ) ;
	/*pthread_attr_setdetachstate ( & pthread_attr , PTHREAD_CREATE_DETACHED ) ;*/
	if ( pthread_create ( & task_start->io_pthread_id[STDIN_FILENO] , NULL , stdin_io_pipe_thread , task_start ) )
		goto return_label;
	if ( pthread_create ( & task_start->io_pthread_id[STDOUT_FILENO] , NULL , stdout_io_pipe_thread , task_start ) )
		goto kill_stdin_thread;
	if ( pthread_create ( & task_start->io_pthread_id[STDERR_FILENO] , NULL , stderr_io_pipe_thread , task_start ) )
		goto kill_stdout_thread;

	
	
	goto return_label;

	kill_stdout_thread:
		pthread_kill ( task_start->io_pthread_id[STDOUT_FILENO] , SIGKILL );
	kill_stdin_thread:
		pthread_kill ( task_start->io_pthread_id[STDIN_FILENO] , SIGKILL );
	return_label:
	return SLURM_SUCCESS ;
}

int wait_on_io_threads ( task_start_t * task_start ) 
{
	/* threads have been detatched*/
	pthread_join ( task_start->io_pthread_id[STDERR_FILENO] , NULL ) ;
	info ( "%i: errexit" , task_start -> local_task_id ) ;
	pthread_join ( task_start->io_pthread_id[STDOUT_FILENO] , NULL ) ;
	info ( "%i: outexit" , task_start -> local_task_id ) ;
	/*pthread_join ( task_start->io_pthread_id[STDIN_FILENO] , NULL ) ;*/
	pthread_cancel ( task_start->io_pthread_id[STDIN_FILENO] );
	pthread_join ( task_start->io_pthread_id[STDIN_FILENO] , NULL ) ;
	info ( "%i: inexit" , task_start -> local_task_id ) ;
	/* thread join on stderr or stdout signifies task termination we should kill the stdin thread */
	return SLURM_SUCCESS ;
}

int launch_task ( task_start_t * task_start )
{
		return pthread_create ( & task_start -> pthread_id , NULL , task_exec_thread , ( void * ) task_start ) ;
}

int wait_for_tasks ( launch_tasks_request_msg_t * launch_msg , task_start_t ** task_start )
{
	int i ;
	int rc ;
	for ( i = 0 ; i < launch_msg->tasks_to_launch ; i ++ )
	{
		rc = pthread_join( task_start[i]->pthread_id , NULL )  ;
		debug3 ( "fan_out_task_launch: thread %i pthread_id %i joined " , i , task_start[i]->pthread_id ) ;
	}
	return SLURM_SUCCESS ;
}
	
int kill_launched_tasks ( launch_tasks_request_msg_t * launch_msg , task_start_t ** task_start , int i )
{
	/*
	int rc ;
	for (  i-- ; i >= 0  ; i -- )
	{
		rc = pthread_kill ( task_start[i]->pthread_id , SIGKILL ) ;
	}
	*/
	return SLURM_SUCCESS ;
}
