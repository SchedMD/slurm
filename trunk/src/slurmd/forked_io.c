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

#define FORK_ERROR -1

	//posix_signal_pipe_ignore ( ) ;

	/* open stdout*/
	connect_io_stream ( task_start , STDIN_OUT_SOCK ) ;
	/* open stderr*/
	connect_io_stream ( task_start , SIG_STDERR_SOCK ) ;

	task_start->io_pthread_id[STDIN_FILENO]	= fork () ;
	switch ( task_start->io_pthread_id[STDIN_FILENO] )
	{
		case FORK_ERROR :
			goto return_label;
			break;
		case 0 :
			break;
		default :
			stdin_io_pipe_thread ( task_start ) ;
			_exit( 0 ) ;
			break ;
	}
	
	task_start->io_pthread_id[STDOUT_FILENO] = fork () ;
	switch ( task_start->io_pthread_id[STDOUT_FILENO] )
	{
		case FORK_ERROR :
			goto kill_stdin_thread;
			break;
		case 0 :
			break;
		default :
			stdout_io_pipe_thread ( task_start ) ;
			_exit( 0 ) ;
			break ;
	}
	
	task_start->io_pthread_id[STDERR_FILENO] = fork () ;
	switch ( task_start->io_pthread_id[STDERR_FILENO] )
	{
		case FORK_ERROR :
			goto kill_stdout_thread;
			break;
		case 0 :
			break;
		default :
			stderr_io_pipe_thread ( task_start ) ;
			_exit( 0 ) ;
			break ;
	}
	
	goto return_label;

	kill_stdout_thread:
		kill ( task_start->io_pthread_id[STDOUT_FILENO] , SIGKILL );
	kill_stdin_thread:
		kill ( task_start->io_pthread_id[STDIN_FILENO] , SIGKILL );
	return_label:
	return SLURM_SUCCESS ;
}

int wait_on_io_threads ( task_start_t * task_start ) 
{
	/* threads have been detatched*/
	waitpid (  task_start->io_pthread_id[STDERR_FILENO] , NULL , 0 ) ;
	info ( "errexit" ) ;
	waitpid ( task_start->io_pthread_id[STDOUT_FILENO] , NULL , 0 ) ;
	info ( "outexit" ) ;
	/* waitpid ( task_start->io_pthread_id[STDIN_FILENO] , NULL ) ;*/
	kill ( task_start->io_pthread_id[STDIN_FILENO] , SIGKILL );
	info ( "inexit" ) ;
	/* thread join on stderr or stdout signifies task termination we should kill the stdin thread */
	return SLURM_SUCCESS ;
}

