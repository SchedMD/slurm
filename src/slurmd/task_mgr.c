#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <src/common/log.h>
#include <src/common/list.h>
#include <src/common/xerrno.h>
#include <src/common/xmalloc.h>
#include <src/common/slurm_protocol_api.h>
#include <src/common/slurm_errno.h>
#include <src/slurmd/task_mgr.h>
#include <src/slurmd/shmem_struct.h>

/*
global variables 
*/

/* file descriptor defines */

#define STDIN_FILENO 0
#define STDOUT_FILENO 1 
#define STDERR_FILENO 2
#define MAX_TASKS_PER_LAUNCH 64
#define CHILD_IN 0
#define CHILD_IN_RD 0
#define CHILD_IN_WR 1
#define CHILD_OUT 2
#define CHILD_OUT_RD 2
#define CHILD_OUT_WR 3
#define CHILD_ERR 4
#define CHILD_ERR_RD 4
#define CHILD_ERR_WR 5

//extern slurmd_shmem_t * shmem_seg ;

/* prototypes */
void slurm_free_task ( void * _task ) ;
void * iowatch_launch_thread ( void * arg ) ;
int kill_task ( task_t * task ) ;
int interconnect_init ( launch_tasks_msg_t * launch_msg );
int fan_out_task_launch ( launch_tasks_msg_t * launch_msg );
void * task_exec_thread ( void * arg ) ;
int init_parent_pipes ( int * pipes ) ;
void setup_parent_pipes ( int * pipes ) ; 
int setup_child_pipes ( int * pipes ) ;
int forward_io ( task_start_t * task_arg ) ;
void * stdin_io_pipe_thread ( void * arg ) ;
void * stdout_io_pipe_thread ( void * arg ) ;
void * stderr_io_pipe_thread ( void * arg ) ;
int setup_task_env  (task_start_t * task_start ) ;
/******************************************************************
 *task launch method call hierarchy
 *
 *launch_tasks()
 *	interconnect_init()
 *		fan_out_task_launch() (pthread_create)
 *			iowatch_launch_thread() (pthread_create)
 *				task_exec_thread() (pthread_create)
 ******************************************************************/			

/* exported module funtion to launch tasks */
/*launch_tasks should really be named launch_job_step*/
int launch_tasks ( launch_tasks_msg_t * launch_msg )
{
	return interconnect_init ( launch_msg );
}

/* Contains interconnect specific setup instructions and then calls 
 * fan_out_task_launch */
int interconnect_init ( launch_tasks_msg_t * launch_msg )
{
	return fan_out_task_launch ( launch_msg ) ;
}

int fan_out_task_launch ( launch_tasks_msg_t * launch_msg )
{
	int i ;
	int rc ;
	
	/* shmem work - see slurmd.c shmem_seg this is probably not needed*/
	slurmd_shmem_t * shmem_ptr = get_shmem ( ) ;
	//slurmd_shmem_t * shmem_ptr = shmem_seg ;

	/*alloc a job_step objec in shmem for this launch_tasks request */
	/*launch_tasks should really be named launch_job_step*/
	job_step_t * curr_job_step = alloc_job_step ( shmem_ptr , launch_msg -> job_id , launch_msg -> job_step_id ) ;
	/* task pointer that will point to shmem task structures as the are allocated*/
	task_t * curr_task = NULL ;
	/* array of pointers used in this function to point to the task_start structure for each task to be
	 * launched*/
	task_start_t * task_start[launch_msg->tasks_to_launch];

		
	/* launch requested number of threads */
	for ( i = 0 ; i < launch_msg->tasks_to_launch ; i ++ )
	{
		curr_task = alloc_task ( shmem_ptr , curr_job_step );
		task_start[i] = & curr_task -> task_start ;
		task_start[i] -> launch_msg = launch_msg ;

		if ( pthread_create ( & task_start[i]->pthread_id , NULL , iowatch_launch_thread , ( void * ) task_start[i] ) )
			goto kill_threads;
	}
	
	/* wait for all the launched threads to finish */
	for ( i = 0 ; i < launch_msg->tasks_to_launch ; i ++ )
	{
		rc = pthread_join( task_start[i]->pthread_id , NULL )  ;
	}
	goto return_label;

	
	kill_threads:
	for (  i-- ; i >= 0  ; i -- )
	{
		rc = pthread_kill ( task_start[i]->pthread_id , SIGKILL ) ;
	}

	return_label:
		rel_shmem ( shmem_ptr ) ;
	return SLURM_SUCCESS ;
}

void * iowatch_launch_thread ( void * arg ) 
{
	task_start_t * task_start = ( task_start_t * ) arg ;

	/* create pipes to read child stdin, stdout, sterr */
	init_parent_pipes ( task_start->pipes ) ;

	/* create task thread */
	if ( pthread_create ( & task_start->exec_pthread_id , NULL , task_exec_thread , ( void * ) arg ) )
		return ( void * ) SLURM_ERROR ;

	/* pipe output from child task to srun over sockets */
	setup_parent_pipes ( task_start->pipes ) ;
	//forward_io ( arg ) ;

	/* wait for thread to die */
	pthread_join ( task_start->exec_pthread_id , NULL ) ;

	return ( void * ) SLURM_SUCCESS ;
}

int forward_io ( task_start_t * task_arg ) 
{
	pthread_attr_t pthread_attr ;
	slurm_addr dest_addr ;
	int local_errno;

	/* open stdout & stderr sockets */
	if ( ( task_arg->sockets[0] = slurm_open_stream ( & dest_addr ) ) == SLURM_PROTOCOL_ERROR )
	{
		local_errno = errno ;	
		info ( "error opening socket to srun to pipe stdout errno %i" , local_errno ) ;
		pthread_exit ( 0 ) ;
	}
	
	if ( ( task_arg->sockets[1] = slurm_open_stream ( & dest_addr ) ) == SLURM_PROTOCOL_ERROR )
	{
		local_errno = errno ;	
		info ( "error opening socket to srun to pipe stdout errno %i" , local_errno ) ;
		pthread_exit ( 0 ) ;
	}
	
	/* spawn io pipe threads */
	pthread_attr_init( & pthread_attr ) ;
	pthread_attr_setdetachstate ( & pthread_attr , PTHREAD_CREATE_DETACHED ) ;
	if ( pthread_create ( & task_arg->io_pthread_id[STDIN_FILENO] , NULL , stdin_io_pipe_thread , task_arg ) )
		goto return_label;
	if ( pthread_create ( & task_arg->io_pthread_id[STDOUT_FILENO] , NULL , stdout_io_pipe_thread , task_arg ) )
		goto kill_stdin_thread;
	if ( pthread_create ( & task_arg->io_pthread_id[STDERR_FILENO] , NULL , stderr_io_pipe_thread , task_arg ) )
		goto kill_stdout_thread;

	/* threads have been detatched*/
	//pthread_join ( task_arg->io_pthread_id[STDERR_FILENO] , NULL ) ;
	//pthread_join ( task_arg->io_pthread_id[STDOUT_FILENO] , NULL ) ;
	//pthread_join ( task_arg->io_pthread_id[STDIN_FILENO] , NULL ) ;
	
	goto return_label;

	kill_stdout_thread:
		pthread_kill ( task_arg->io_pthread_id[STDOUT_FILENO] , SIGKILL );
	kill_stdin_thread:
		pthread_kill ( task_arg->io_pthread_id[STDIN_FILENO] , SIGKILL );
	return_label:
	return SLURM_SUCCESS ;
}

void * stdin_io_pipe_thread ( void * arg )
{
	task_start_t * io_arg = ( task_start_t * ) arg ;
	char buffer[SLURMD_IO_MAX_BUFFER_SIZE] ;
	int bytes_read ;
	int sock_bytes_written ;
	
	while ( true )
	{
		if ( ( sock_bytes_written = slurm_read_stream ( io_arg->sockets[0] , buffer , bytes_read ) ) == SLURM_PROTOCOL_ERROR )
		{
			info ( "error sending stdout stream for task %i", 1 ) ;
			pthread_exit ( NULL ) ; 
		}
		if ( ( bytes_read = write ( io_arg->pipes[CHILD_IN_WR] , buffer , SLURMD_IO_MAX_BUFFER_SIZE ) ) <= 0 )
		{
			info ( "error reading stdout stream for task %i", 1 ) ;
			pthread_exit ( NULL ) ;
		}
	}
}

void * stdout_io_pipe_thread ( void * arg )
{
	task_start_t * io_arg = ( task_start_t * ) arg ;
	char buffer[SLURMD_IO_MAX_BUFFER_SIZE] ;
	int bytes_read ;
	int sock_bytes_written ;
	
	while ( true )
	{
		if ( ( bytes_read = read ( io_arg->pipes[CHILD_OUT_RD] , buffer , SLURMD_IO_MAX_BUFFER_SIZE ) ) <= 0 )
		{
			info ( "error reading stdout stream for task %i", 1 ) ;
			pthread_exit ( NULL ) ;
		}
		if ( ( sock_bytes_written = slurm_write_stream ( io_arg->sockets[0] , buffer , bytes_read ) ) == SLURM_PROTOCOL_ERROR )
		{
			info ( "error sending stdout stream for task %i", 1 ) ;
			pthread_exit ( NULL ) ; 
		}
	}
}

void * stderr_io_pipe_thread ( void * arg )
{
	task_start_t * io_arg = ( task_start_t * ) arg ;
	char buffer[SLURMD_IO_MAX_BUFFER_SIZE] ;
	int bytes_read ;
	int sock_bytes_written ;
	
	/* dest_addr = somethiong from arg */


	while ( true )
	{
		if ( ( bytes_read = read ( io_arg->pipes[CHILD_ERR_RD] , buffer , SLURMD_IO_MAX_BUFFER_SIZE ) ) <= 0 )
		{
			info ( "error reading stderr stream for task %i", 1 ) ;
			pthread_exit ( NULL ) ;
		}
		if ( ( sock_bytes_written = slurm_write_stream ( io_arg->sockets[1] , buffer , bytes_read ) ) == SLURM_PROTOCOL_ERROR )
		{
			info ( "error sending stderr stream for task %i", 1 ) ;
			pthread_exit ( NULL ) ; 
		}
	}
}
#define FORK_ERROR -1
#define CHILD_PROCCESS 0

void * task_exec_thread ( void * arg )
{
	task_start_t * task_arg = ( task_start_t * ) arg ;
	launch_tasks_msg_t * launch_msg = task_arg -> launch_msg ;
	int * pipes = task_arg->pipes ;
	int rc ;
	int cpid ;

	switch ( ( cpid = fork ( ) ) )
	{
		case FORK_ERROR:
			break ;
		case CHILD_PROCCESS:

			/* setup std stream pipes */
			setup_child_pipes ( pipes ) ;

			rc ++ ;
			/* setuid and gid*/
			//if ( ( rc = setuid ( launch_msg->uid ) ) == SLURM_ERROR ) ;

			//if ( ( rc = setgid ( launch_msg->gid ) ) == SLURM_ERROR ) ;

			/* setup requested env */
			//setup_task_env ( task_arg ) ;

			/* run bash and cmdline */
			chdir ( launch_msg->cwd ) ;
			execl ( "/bin/bash" , "bash" , "-c" , launch_msg->cmd_line );
		default: /*parent proccess */
			waitpid ( cpid , NULL , 0 ) ;
	}
	return ( void * ) SLURM_SUCCESS ;
}

void setup_parent_pipes ( int * pipes )
{
	close ( pipes[CHILD_IN_RD] ) ;
	close ( pipes[CHILD_OUT_WR] ) ;
	close ( pipes[CHILD_ERR_WR] ) ;
}

int init_parent_pipes ( int * pipes )
{
	int rc ;
	/* open pipes to be used in dup after fork */
	if( ( rc = pipe ( & pipes[CHILD_IN] ) ) ) 
	{
		return ESLRUMD_PIPE_ERROR_ON_TASK_SPAWN ;
	}
	if( ( rc = pipe ( & pipes[CHILD_OUT] ) ) ) 
	{
		return ESLRUMD_PIPE_ERROR_ON_TASK_SPAWN ;
	}
	if( ( rc = pipe ( & pipes[CHILD_ERR] ) ) ) 
	{
		return ESLRUMD_PIPE_ERROR_ON_TASK_SPAWN ;
	}
	return SLURM_SUCCESS ;
}

int setup_child_pipes ( int * pipes )
{
	int error_code = 0 ;
	int local_errno;

	/*dup stdin*/
	//close ( STDIN_FILENO );
	if ( SLURM_ERROR == ( error_code |= dup2 ( pipes[CHILD_IN_RD] , STDIN_FILENO ) ) ) 
	{
		local_errno = errno ;
		info ("dup failed on child standard in pipe, errno %i" , local_errno );
		//return error_code ;
	}
	close ( CHILD_IN_RD );
	close ( CHILD_IN_WR );

	/*dup stdout*/
	//close ( STDOUT_FILENO );
	if ( SLURM_ERROR == ( error_code |= dup2 ( pipes[CHILD_OUT_WR] , STDOUT_FILENO ) ) ) 
	{
		local_errno = errno ;
		info ("dup failed on child standard out pipe, errno %i" , local_errno );
		//return error_code ;
	}
	close ( CHILD_OUT_RD );
	close ( CHILD_OUT_WR );

	/*dup stderr*/
	//close ( STDERR_FILENO );
	if ( SLURM_ERROR == ( error_code |= dup2 ( pipes[CHILD_ERR_WR] , STDERR_FILENO ) ) ) 
	{
		local_errno = errno ;
		info ("dup failed on child standard err pipe, errno %i" , local_errno );
		//return error_code ;
	}
	close ( CHILD_ERR_RD );
	close ( CHILD_ERR_WR );

	return error_code ;
}

int kill_tasks ( kill_tasks_msg_t * kill_task_msg )
{
	int error_code = SLURM_SUCCESS ;

	return error_code ;
}


int kill_task ( task_t * task )
{
	return SLURM_SUCCESS ;
}

int setup_task_env  (task_start_t * task_start )
{
	int i ;
	for ( i = 0 ; i < task_start -> launch_msg -> envc ; i ++ )
	{
		char * env_var = xmalloc ( strlen (  task_start -> launch_msg -> env[i] ) ) ;
		memcpy ( env_var , task_start -> launch_msg -> env[i] , strlen (  task_start -> launch_msg -> env[i] ) ) ;
		putenv ( env_var ) ;
	}
	return SLURM_SUCCESS ;
}
