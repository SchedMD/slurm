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
int kill_task ( task_t * task ) ;
int interconnect_init ( launch_tasks_request_msg_t * launch_msg );
int fan_out_task_launch ( launch_tasks_request_msg_t * launch_msg );
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
	//slurmd_shmem_t * shmem_ptr = shmem_seg ;

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
	for (  i-- ; i >= 0  ; i -- )
	{
		rc = pthread_kill ( task_start[i]->pthread_id , SIGKILL ) ;
	}

	return_label:
		rel_shmem ( shmem_ptr ) ;
	return SLURM_SUCCESS ;
}

int forward_io ( task_start_t * task_arg ) 
{
	pthread_attr_t pthread_attr ;
	int local_errno;
	slurm_io_stream_header_t io_header ;
	
#define STDIN_OUT_SOCK 0
#define SIG_STDERR_SOCK 1 

	posix_signal_pipe_ignore ( ) ;

	/* open stdout & stderr sockets */
	if ( ( task_arg->sockets[STDIN_OUT_SOCK] = slurm_open_stream ( & ( task_arg -> io_streams_dest ) ) ) == SLURM_PROTOCOL_ERROR )
	{
		local_errno = errno ;	
		info ( "error opening socket to srun to pipe stdout errno %i" , local_errno ) ;
//		pthread_exit ( 0 ) ;
	}
	else
	{
		char buffer[sizeof(slurm_io_stream_header_t)] ;
		char * buf_ptr = buffer ;
		int buf_size = sizeof(slurm_io_stream_header_t) ;
		int size = sizeof(slurm_io_stream_header_t) ;
		
		init_io_stream_header ( & io_header , task_arg -> launch_msg -> credential -> signature , task_arg -> launch_msg -> global_task_ids[task_arg -> local_task_id ] , SLURM_IO_STREAM_INOUT ) ;
		pack_io_stream_header ( & io_header , & buf_ptr , & size ) ;
		slurm_write_stream (  task_arg->sockets[STDIN_OUT_SOCK] , buffer , buf_size - size ) ;
	}
	
	if ( ( task_arg->sockets[SIG_STDERR_SOCK] = slurm_open_stream ( &( task_arg -> io_streams_dest ) ) ) == SLURM_PROTOCOL_ERROR )
	{
		local_errno = errno ;	
		info ( "error opening socket to srun to pipe stdout errno %i" , local_errno ) ;
//		pthread_exit ( 0 ) ;
	}
	else
	{
		char buffer[sizeof(slurm_io_stream_header_t)] ;
		char * buf_ptr = buffer ;
		int buf_size = sizeof(slurm_io_stream_header_t) ;
		int size = sizeof(slurm_io_stream_header_t) ;
		
		init_io_stream_header ( & io_header , task_arg -> launch_msg -> credential -> signature , task_arg -> launch_msg -> global_task_ids[task_arg -> local_task_id ] , SLURM_IO_STREAM_SIGERR ) ;
		pack_io_stream_header ( & io_header , & buf_ptr , & size ) ;
		slurm_write_stream (  task_arg->sockets[SIG_STDERR_SOCK] , buffer , buf_size - size ) ;
	}
	
	/* spawn io pipe threads */
	pthread_attr_init( & pthread_attr ) ;
	//pthread_attr_setdetachstate ( & pthread_attr , PTHREAD_CREATE_DETACHED ) ;
	if ( pthread_create ( & task_arg->io_pthread_id[STDIN_FILENO] , NULL , stdin_io_pipe_thread , task_arg ) )
		goto return_label;
	if ( pthread_create ( & task_arg->io_pthread_id[STDOUT_FILENO] , NULL , stdout_io_pipe_thread , task_arg ) )
		goto kill_stdin_thread;
	if ( pthread_create ( & task_arg->io_pthread_id[STDERR_FILENO] , NULL , stderr_io_pipe_thread , task_arg ) )
		goto kill_stdout_thread;

	///* threads have been detatched*/
	
	pthread_join ( task_arg->io_pthread_id[STDERR_FILENO] , NULL ) ;
	info ( "errexit" ) ;
	pthread_join ( task_arg->io_pthread_id[STDOUT_FILENO] , NULL ) ;
	info ( "outexit" ) ;
	/* thread join on stderr or stdout signifies task termination we should kill the stdin thread */
	pthread_kill (  task_arg->io_pthread_id[STDIN_FILENO] , SIGKILL );
	
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
	//char buffer[SLURMD_IO_MAX_BUFFER_SIZE] ;
	int bytes_read ;
	int bytes_written ;
	int local_errno ;
	circular_buffer_t * cir_buf ;

	init_circular_buffer ( & cir_buf ) ;
	
	posix_signal_pipe_ignore ( ) ;
	
	while ( true )
	{
		//if ( ( bytes_read = slurm_read_stream ( io_arg->sockets[0] , buffer , SLURMD_IO_MAX_BUFFER_SIZE ) ) == SLURM_PROTOCOL_ERROR )
		if ( ( bytes_read = slurm_read_stream ( io_arg->sockets[STDIN_OUT_SOCK] , cir_buf->tail , cir_buf->write_size ) ) <= 0 )
		{
			local_errno = errno ;	
			switch ( local_errno )
			{
				case 0:
				case EBADF:
				case EPIPE:
				case ECONNREFUSED:
				case ECONNRESET:
				case ENOTCONN:
					break ;
				default:
					info ( "error reading stdin  stream for task %i, errno %i , bytes read %i ", 1 , local_errno , bytes_read ) ;
					error ( "uncaught errno %i", local_errno ) ;
			}
			continue ;
		}
		cir_buf_write_update ( cir_buf , bytes_read ) ;
		/* debug */
		//write ( 1 ,  "stdin-", 6 ) ;
		//write ( 1 ,  buffer , bytes_read ) ;
		//write ( 1 , cir_buf->head , cir_buf->read_size ) ;
		info ( "%i stdin bytes read", bytes_read ) ;
		/* debug */
		while ( true)
		{
			//if ( ( bytes_written = write ( io_arg->pipes[CHILD_IN_WR] , buffer , bytes_read ) ) <= 0 )
			if ( ( bytes_written = write ( io_arg->pipes[CHILD_IN_WR] , cir_buf->head , cir_buf->read_size ) ) <= 0 )
			{
				if ( bytes_written == SLURM_PROTOCOL_ERROR && errno == EINTR ) 
				{
					continue ;
				}
				else
				{

					local_errno = errno ;	
					info ( "error sending stdin  stream for task %i, errno %i , bytes read %i ", 1 , local_errno , bytes_read ) ;
					goto stdin_return ;
				}
			}
			else
			{
				cir_buf_read_update ( cir_buf , bytes_written ) ;
				break ;
			}
		}
	}
	stdin_return:
	free_circular_buffer ( cir_buf ) ;
	pthread_exit ( NULL ) ;
}

#define RECONNECT_RETRY_TIME 1
void * stdout_io_pipe_thread ( void * arg )
{
	task_start_t * io_arg = ( task_start_t * ) arg ;
	//char buffer[SLURMD_IO_MAX_BUFFER_SIZE] ;
	int bytes_read ;
	int sock_bytes_written ;
	int local_errno ;
	int attempt_reconnect = false ;
	time_t last_reconnect_try = 0 ;
	circular_buffer_t * cir_buf ;

	init_circular_buffer ( & cir_buf ) ;
	
	posix_signal_pipe_ignore ( ) ;

	while ( true )
	{
		/* read stderr code */
		//if ( ( bytes_read = read ( io_arg->pipes[CHILD_OUT_RD] , buffer , SLURMD_IO_MAX_BUFFER_SIZE ) ) <= 0 )
		if ( ( bytes_read = read ( io_arg->pipes[CHILD_OUT_RD] , cir_buf->tail , cir_buf->write_size ) ) <= 0 )
		{
			if ( bytes_read == SLURM_PROTOCOL_ERROR && errno == EINTR ) 
			{
				continue ;
			}
			else
			{

				local_errno = errno ;	
				info ( "error reading stdout stream for task %i, errno %i , bytes read %i ", 1 , local_errno , bytes_read ) ;
				goto stdout_return ;
			}
		}
		cir_buf_write_update ( cir_buf , bytes_read ) ;
		/* debug */
		//write ( 1 ,  buffer , bytes_read ) ;
		//write ( 1 , cir_buf->head , cir_buf->read_size ) ;
		//info ( "%i stdout bytes read", bytes_read ) ;
		/* debug */
		/* reconnect code */
		if ( attempt_reconnect )
		{
			time_t curr_time = time ( NULL ) ;
			if ( difftime ( curr_time , last_reconnect_try )  > RECONNECT_RETRY_TIME )
			{
				slurm_close_stream ( io_arg->sockets[STDIN_OUT_SOCK] ) ;
				if ( ( io_arg->sockets[STDIN_OUT_SOCK] = slurm_open_stream ( & ( io_arg -> io_streams_dest ) ) ) == SLURM_PROTOCOL_ERROR )
				{
					local_errno = errno ;	
					info ( "error reconnecting socket to srun to pipe stdout errno %i" , local_errno ) ;
					last_reconnect_try = time ( NULL ) ;
					continue ;
				}
				attempt_reconnect = false ;
			}
			else
			{
				continue ;
			}
		}
		/* write out socket code */
		//if ( ( sock_bytes_written = slurm_write_stream ( io_arg->sockets[0] , buffer , bytes_read ) ) == SLURM_PROTOCOL_ERROR )
		if ( ( sock_bytes_written = slurm_write_stream ( io_arg->sockets[STDIN_OUT_SOCK] , cir_buf->head , cir_buf->read_size ) ) == SLURM_PROTOCOL_ERROR )
		{
			local_errno = errno ;	
			switch ( local_errno )
			{
				case EBADF:
				case EPIPE:
				case ECONNREFUSED:
				case ECONNRESET:
				case ENOTCONN:
					info ( "std out connection lost" ) ;
					attempt_reconnect = true ;
					slurm_close_stream ( io_arg->sockets[STDIN_OUT_SOCK] ) ;
					break ;
				default:
					info ( "error sending stdout stream for task %i , errno %i", 1 , local_errno ) ;
					error ( "uncaught errno %i", local_errno ) ;
			}
			continue ;
		}
		cir_buf_read_update ( cir_buf , sock_bytes_written ) ;
	}

	stdout_return:
	free_circular_buffer ( cir_buf ) ;
	pthread_exit ( NULL ) ; 
}

void * stderr_io_pipe_thread ( void * arg )
{
	task_start_t * io_arg = ( task_start_t * ) arg ;
	//char buffer[SLURMD_IO_MAX_BUFFER_SIZE] ;
	int bytes_read ;
	int sock_bytes_written ;
	int local_errno ;
	int attempt_reconnect = false ;
	time_t last_reconnect_try = 0 ;
	circular_buffer_t * cir_buf ;

	init_circular_buffer ( & cir_buf ) ;
	
	while ( true )
	{
		/* read stderr code */
		//if ( ( bytes_read = read ( io_arg->pipes[CHILD_ERR_RD] , buffer , SLURMD_IO_MAX_BUFFER_SIZE ) ) <= 0 )
		if ( ( bytes_read = read ( io_arg->pipes[CHILD_ERR_RD] , cir_buf->tail , cir_buf->write_size ) ) <= 0 )
		{
			if ( bytes_read == SLURM_PROTOCOL_ERROR && errno == EINTR ) 
			{
				continue ;
			}
			else
			{

				local_errno = errno ;	
				info ( "error reading stderr stream for task %i, errno %i , bytes read %i ", 1 , local_errno , bytes_read ) ;
				goto stderr_return ;
			}
		}
		cir_buf_write_update ( cir_buf , bytes_read ) ;
		/* debug */
		//write ( 2 ,  buffer , bytes_read ) ;
		//info ( "%i stderr bytes read", bytes_read ) ;
		//write ( 2 , cir_buf->head , cir_buf->read_size ) ;
		/* debug */
		/* reconnect code */
		if ( attempt_reconnect )
		{
			time_t curr_time = time ( NULL ) ;
			if ( difftime ( curr_time , last_reconnect_try )  > RECONNECT_RETRY_TIME )
			{
				slurm_close_stream ( io_arg->sockets[SIG_STDERR_SOCK] ) ;
				if ( ( io_arg->sockets[SIG_STDERR_SOCK] = slurm_open_stream ( &( io_arg -> io_streams_dest ) ) ) == SLURM_PROTOCOL_ERROR )
				{
					local_errno = errno ;	
					info ( "error reconnecting socket to srun to pipe stderr errno %i" , local_errno ) ;
					last_reconnect_try = time ( NULL ) ;
					continue ;
				}
				attempt_reconnect = false ;
			}
			else
			{
				continue ;
			}
		}
		/* write out socket code */
		//if ( ( sock_bytes_written = slurm_write_stream ( io_arg->sockets[1] , buffer , bytes_read ) ) == SLURM_PROTOCOL_ERROR )
		if ( ( sock_bytes_written = slurm_write_stream ( io_arg->sockets[SIG_STDERR_SOCK] , cir_buf->head , cir_buf->read_size ) ) == SLURM_PROTOCOL_ERROR )
		{
			local_errno = errno ;	
			switch ( local_errno )
			{
				case EBADF:
				case EPIPE:
				case ECONNREFUSED:
				case ECONNRESET:
				case ENOTCONN:
					info ( "std err connection lost %s ", local_errno ) ;
					attempt_reconnect = true ;
					slurm_close_stream ( io_arg->sockets[SIG_STDERR_SOCK] ) ;
					break ;
				default:
					info ( "error sending stderr stream for task %i , errno %i", 1 , local_errno ) ;
					error ( "uncaught errno %i", local_errno ) ;
			}
			continue ;
		}
		cir_buf_read_update ( cir_buf , sock_bytes_written ) ;
	}

	stderr_return:
	free_circular_buffer ( cir_buf ) ;
	pthread_exit ( NULL ) ; 
}


void * task_exec_thread ( void * arg )
{
	task_start_t * task_start = ( task_start_t * ) arg ;
	launch_tasks_request_msg_t * launch_msg = task_start -> launch_msg ;
	int * pipes = task_start->pipes ;
	int rc ;
	int cpid ;
	struct passwd * pwd ;

	/* create pipes to read child stdin, stdout, sterr */
	init_parent_pipes ( task_start->pipes ) ;
	
	setup_parent_pipes ( task_start->pipes ) ;
	forward_io ( arg ) ;

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
			
			/* setuid and gid*/
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
			//execl ("/bin/bash", "bash", "-c", launch_msg->cmd_line, 0);
	
			execve ( launch_msg->argv[0], launch_msg->argv , launch_msg->env );
			close ( STDIN_FILENO );
			close ( STDOUT_FILENO );
			close ( STDERR_FILENO );
			_exit ( SLURM_SUCCESS ) ;
			
		default: /*parent proccess */
			task_start->exec_pid = cpid ;
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
		error ("dup failed on child standard in pipe, errno %i" , local_errno );
		//return error_code ;
	}
	close ( CHILD_IN_RD );
	close ( CHILD_IN_WR );

	/*dup stdout*/
	//close ( STDOUT_FILENO );
	if ( SLURM_ERROR == ( error_code |= dup2 ( pipes[CHILD_OUT_WR] , STDOUT_FILENO ) ) ) 
	{
		local_errno = errno ;
		error ("dup failed on child standard out pipe, errno %i" , local_errno );
		//return error_code ;
	}
	close ( CHILD_OUT_RD );
	close ( CHILD_OUT_WR );

	/*dup stderr*/
	//close ( STDERR_FILENO );
	if ( SLURM_ERROR == ( error_code |= dup2 ( pipes[CHILD_ERR_WR] , STDERR_FILENO ) ) ) 
	{
		local_errno = errno ;
		error ("dup failed on child standard err pipe, errno %i" , local_errno );
		//return error_code ;
	}
	close ( CHILD_ERR_RD );
	close ( CHILD_ERR_WR );

	return error_code ;
}

int kill_tasks ( kill_tasks_msg_t * kill_task_msg )
{
	int error_code = SLURM_SUCCESS ;
	slurmd_shmem_t * shmem_ptr = get_shmem ( ) ;
	job_step_t * job_step_ptr = find_job_step ( shmem_ptr , kill_task_msg -> job_id , kill_task_msg -> job_step_id ) ;
	task_t * task_ptr = job_step_ptr -> head_task ;
	while ( task_ptr != NULL )
	{
		kill_task ( task_ptr ) ;
		task_ptr = task_ptr -> next ;
	}
	return error_code ;
}


int kill_task ( task_t * task )
{
	kill ( task -> task_start . exec_pid , SIGKILL ) ;
	return SLURM_SUCCESS ;
}

int reattach_tasks_streams ( reattach_tasks_streams_msg_t * req_msg )
{
	int i;
	int error_code = SLURM_SUCCESS ;
	slurmd_shmem_t * shmem_ptr = get_shmem ( ) ;
	job_step_t * job_step_ptr = find_job_step ( shmem_ptr , req_msg->job_id , req_msg->job_step_id ) ;

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


