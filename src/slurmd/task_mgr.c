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
#include <src/common/xerrno.h>
#include <src/common/xmalloc.h>
#include <src/common/slurm_protocol_api.h>
#include <src/common/slurm_errno.h>
#include <src/common/util_signals.h>

#include <src/slurmd/task_mgr.h>
#include <src/slurmd/shmem_struct.h>
#include <src/slurmd/circular_buffer.h>
#include <src/slurmd/pipes.h>
#include <src/slurmd/io.h>

/* global variables */

/* prototypes */
int kill_task ( task_t * task , int signal ) ;

int interconnect_init ( launch_tasks_request_msg_t * launch_msg );
int fan_out_task_launch ( launch_tasks_request_msg_t * launch_msg );
void * task_exec_thread ( void * arg ) ;
int send_task_exit_msg ( int task_return_code , task_start_t * task_start ) ;

/******************************************************************
 *task launch method call hierarchy
 *
 *launch_tasks()
 *	interconnect_init()
 *		fan_out_task_launch() (pthread_create)
 *			task_exec_thread() (fork) for task exec
 *			task_exec_thread() (pthread_create) for io piping 
 ******************************************************************/			

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
	return fan_out_task_launch ( launch_msg ) ;
}

int fan_out_task_launch ( launch_tasks_request_msg_t * launch_msg )
{
	int i ;
	int rc ;
	int session_id ;
	
	/* shmem work - see slurmd.c shmem_seg this is probably not needed*/
	slurmd_shmem_t * shmem_ptr = get_shmem ( ) ;
	/*slurmd_shmem_t * shmem_ptr = shmem_seg ;*/

	/*alloc a job_step objec in shmem for this launch_tasks request */
	/*launch_tasks should really be named launch_job_step*/
	job_step_t * curr_job_step = alloc_job_step ( shmem_ptr , launch_msg -> job_id , launch_msg -> job_step_id ) ;
	/* task pointer that will point to shmem task structures as the are allocated*/
	task_t * curr_task = NULL ;
	/* array of pointers used in this function to point to the task_start structure for each task to be
	 * launched*/
	task_start_t * task_start[launch_msg->tasks_to_launch];

	if ( ( session_id = setsid () ) == SLURM_ERROR )
	{
		info ( "set sid failed" );
		//if ( ( session_id = getsid (0) ) == SLURM_ERROR )
		{
			info ( "getsid also failed" ) ;
		}
	}
	curr_job_step -> session_id = session_id ;

		
	/* launch requested number of threads */
	for ( i = 0 ; i < launch_msg->tasks_to_launch ; i ++ )
	{
		curr_task = alloc_task ( shmem_ptr , curr_job_step );
		task_start[i] = & curr_task -> task_start ;
		curr_task -> task_id = launch_msg -> global_task_ids[i] ;
		
		/* fill in task_start struct */
		task_start[i] -> launch_msg = launch_msg ;
		task_start[i] -> local_task_id = i ; 
		task_start[i] -> io_streams_dest = launch_msg -> streams ; 

		if ( pthread_create ( & task_start[i]->pthread_id , NULL , task_exec_thread , ( void * ) task_start[i] ) )
			goto kill_threads;
	}
	
	/* wait for all the launched threads to finish */
	for ( i = 0 ; i < launch_msg->tasks_to_launch ; i ++ )
	{
		rc = pthread_join( task_start[i]->pthread_id , NULL )  ;
	}
	goto return_label;

	
	kill_threads:
	/*
	for (  i-- ; i >= 0  ; i -- )
	{
		rc = pthread_kill ( task_start[i]->pthread_id , SIGKILL ) ;
	}
	*/
	return_label:
		rel_shmem ( shmem_ptr ) ;
	return SLURM_SUCCESS ;
}



void * task_exec_thread ( void * arg )
{
	task_start_t * task_start = ( task_start_t * ) arg ;
	launch_tasks_request_msg_t * launch_msg = task_start -> launch_msg ;
	int * pipes = task_start->pipes ;
	int rc ;
	int cpid ;
	struct passwd * pwd ;
	int task_return_code ;
	int local_errno ;

	/* create pipes to read child stdin, stdout, sterr */
	init_parent_pipes ( task_start->pipes ) ;

#define FORK_ERROR -1
#define CHILD_PROCCESS 0
	switch ( ( cpid = fork ( ) ) )
	{
		case FORK_ERROR:
			break ;

		case CHILD_PROCCESS:
			debug ("CLIENT PROCESS");

			posix_signal_ignore (SIGTTOU); /* ignore tty output */
			posix_signal_ignore (SIGTTIN); /* ignore tty input */
			posix_signal_ignore (SIGTSTP); /* ignore user */
			
			/* setup std stream pipes */
			setup_child_pipes ( pipes ) ;

			/* get passwd file info */
			if ( ( pwd = getpwuid ( launch_msg->uid ) ) == NULL )
			{
				info ( "user id not found in passwd file" ) ;
				_exit ( SLURM_FAILURE ) ;
			}
			
			/* setgid and uid*/
			if ( ( rc = setgid ( pwd -> pw_gid ) ) == SLURM_ERROR )
			{
				info ( "set group id failed " ) ;
				_exit ( SLURM_FAILURE ) ;
			}

			if ( ( rc = setuid ( launch_msg->uid ) ) == SLURM_ERROR ) 
			{
				info ( "set user id failed " ) ;
				_exit ( SLURM_FAILURE ) ;
			}
			
			/* initgroups */
			/*if ( ( rc = initgroups ( pwd ->pw_name , pwd -> pw_gid ) ) == SLURM_ERROR )
			{
				info ( "init groups failed " ) ;
				_exit ( SLURM_FAILURE ) ;
			}
			*/

			/* run bash and cmdline */
			debug( "cwd %s", launch_msg->cwd ) ;
			chdir ( launch_msg->cwd ) ;
			execve ( launch_msg->argv[0], launch_msg->argv , launch_msg->env );

			/* error if execve returns
			 * clean up */
			error("execve(): %s: %m", launch_msg->argv[0]);
			close ( STDIN_FILENO );
			close ( STDOUT_FILENO );
			close ( STDERR_FILENO );
			local_errno = errno ;
			_exit ( local_errno ) ;
			break;
			
		default: /*parent proccess */
			task_start->exec_pid = cpid ;
			setup_parent_pipes ( task_start->pipes ) ;
			forward_io ( arg ) ;
			waitpid ( cpid , & task_return_code , 0 ) ;
			send_task_exit_msg ( task_return_code , task_start ) ;
			break;
	}
	return ( void * ) SLURM_SUCCESS ;
}


int send_task_exit_msg ( int task_return_code , task_start_t * task_start )
{
	slurm_msg_t resp_msg ;
	task_exit_msg_t task_exit ;

	/* init task_exit_message */
	task_exit . return_code = task_return_code ;
	task_exit . task_id = task_start -> launch_msg -> global_task_ids[ task_start -> local_task_id ] ;

	/* init slurm_msg_t */
	resp_msg . address = task_start -> launch_msg -> response_addr ;
	resp_msg . data = & task_exit ;
	resp_msg . msg_type = MESSAGE_TASK_EXIT;

	/* send message */
	return slurm_send_only_node_msg ( & resp_msg ) ;
}

int kill_tasks ( kill_tasks_msg_t * kill_task_msg )
{
	int error_code = SLURM_SUCCESS ;
	
	/* get shmemptr */
	slurmd_shmem_t * shmem_ptr = get_shmem ( ) ;
	
	/* find job step */
	job_step_t * job_step_ptr = find_job_step ( shmem_ptr , kill_task_msg -> job_id , kill_task_msg -> job_step_id ) ;
	
	/* cycle through job_step and kill tasks*/
	task_t * task_ptr = job_step_ptr -> head_task ;
	while ( task_ptr != NULL )
	{
		kill_task ( task_ptr , kill_task_msg -> signal ) ;
		task_ptr = task_ptr -> next ;
	}
	return error_code ;
}

int kill_task ( task_t * task , int signal )
{
	return kill ( task -> task_start . exec_pid , signal ) ;
}

int reattach_tasks_streams ( reattach_tasks_streams_msg_t * req_msg )
{
	int i;
	int error_code = SLURM_SUCCESS ;
	/* get shmemptr */
	slurmd_shmem_t * shmem_ptr = get_shmem ( ) ;

	/* find job step */
	job_step_t * job_step_ptr = find_job_step ( shmem_ptr , req_msg->job_id , req_msg->job_step_id ) ;

	/* cycle through tasks and set streams address */
	for ( i = 0  ; i < req_msg->tasks_to_reattach ; i ++ )
	{
		task_t * task = find_task ( job_step_ptr , req_msg->global_task_ids[i] ) ;
		if ( task != NULL )
		{
			task -> task_start . io_streams_dest =  req_msg -> streams ;
		}
		else
		{
			error ( "task id not found job_id %i job_step_id %i global_task_id %i" , req_msg->job_id , req_msg->job_step_id , req_msg->global_task_ids[i] ) ;
		}
	}
	return error_code ;
}


