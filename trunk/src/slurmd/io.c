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
#include <src/slurmd/io.h>
#include <src/slurmd/pipes.h>

/* global variables */
int connect_io_stream (  task_start_t * task_start , int out_or_err ) ;
int send_io_stream_header ( task_start_t * task_start , int out_or_err ) ;
ssize_t read_EINTR(int fd, void *buf, size_t count) ;


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
	
#define STDIN_OUT_SOCK 0
#define SIG_STDERR_SOCK 1 

	posix_signal_pipe_ignore ( ) ;

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

	/* threads have been detatched*/
	
	pthread_join ( task_start->io_pthread_id[STDERR_FILENO] , NULL ) ;
	info ( "errexit" ) ;
	pthread_join ( task_start->io_pthread_id[STDOUT_FILENO] , NULL ) ;
	info ( "outexit" ) ;
	/*pthread_join ( task_start->io_pthread_id[STDIN_FILENO] , NULL ) ;*/
	pthread_cancel (  task_start->io_pthread_id[STDIN_FILENO] );
	info ( "inexit" ) ;
	/* thread join on stderr or stdout signifies task termination we should kill the stdin thread */
	
	goto return_label;

	kill_stdout_thread:
		pthread_kill ( task_start->io_pthread_id[STDOUT_FILENO] , SIGKILL );
	kill_stdin_thread:
		pthread_kill ( task_start->io_pthread_id[STDIN_FILENO] , SIGKILL );
	return_label:
	return SLURM_SUCCESS ;
}

void * stdin_io_pipe_thread ( void * arg )
{
	task_start_t * task_start = ( task_start_t * ) arg ;
	int bytes_read ;
	int bytes_written ;
	int local_errno ;
	circular_buffer_t * cir_buf ;

	init_circular_buffer ( & cir_buf ) ;
	
	posix_signal_pipe_ignore ( ) ;

	while ( true )
	{
		if ( ( cir_buf->write_size == 0 ) )
		{
			info ( "stdin cir_buf->write_size == 0 this shouldn't happen" ) ;
			continue ;
		}
		
		if ( ( bytes_read = slurm_read_stream ( task_start->sockets[STDIN_OUT_SOCK] , cir_buf->tail , cir_buf->write_size ) ) <= 0 )
		{
			local_errno = errno ;	
			if ( bytes_read == 0)
			{
				info ( "0 returned EOF on socket ") ;
				break ;
			}
			else if ( bytes_read == -1 )
			{
				switch ( local_errno )
				{
					case EBADF:
					case EPIPE:
					case ECONNREFUSED:
					case ECONNRESET:
					case ENOTCONN:
						break ;
					default:
						info ( "error reading stdin  stream for task %i, %m errno: %i , bytes read %i ", task_start -> launch_msg -> global_task_ids[ task_start -> local_task_id ] , local_errno , bytes_read ) ;
						error ( "uncaught errno %i", local_errno ) ;
						break;
				}
			}
			else
			{
				info ( "bytes_read: %i don't know what to do with this return code ", bytes_read ) ;
			}
		}
		else
		{
			cir_buf_write_update ( cir_buf , bytes_read ) ;
		}
		
		/* debug */
		//write ( 1 ,  "stdin-", 6 ) ;
		//write ( 1 , cir_buf->head , cir_buf->read_size ) ;
		info ( "%i stdin bytes read", bytes_read ) ;
		/* debug */
		
		while ( true)
		{
		
			if ( ( bytes_written = write ( task_start->pipes[CHILD_IN_WR] , cir_buf->head , cir_buf->read_size ) ) <= 0 )
			{
				if ( ( bytes_written == SLURM_PROTOCOL_ERROR ) && ( errno == EINTR ) ) 
				{
					continue ;
				}
				else
				{

					local_errno = errno ;	
					info ( "error sending stdin  stream for task %i, %m errno: %i , bytes read %i ", task_start -> launch_msg -> global_task_ids[ task_start -> local_task_id ] , local_errno , bytes_read ) ;
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
	task_start_t * task_start = ( task_start_t * ) arg ;
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
			if ( ( cir_buf->write_size == 0 ) )
			{
				info ( "stdout cir_buf->write_size == 0 this shouldn't happen" ) ;
				continue ;
			}	
		
		/* read stdout code */
		if ( ( bytes_read = read_EINTR ( task_start->pipes[CHILD_OUT_RD] , cir_buf->tail , cir_buf->write_size ) ) <= 0 )
		{
			local_errno = errno ;	
			info ( "error reading stdout stream for task %i, %m errno: %i , bytes read %i ", 
					task_start -> launch_msg -> global_task_ids[ task_start -> local_task_id ] , local_errno , bytes_read ) ;
			goto stdout_return ;
		}
		else
		{
			cir_buf_write_update ( cir_buf , bytes_read ) ;
		}
		
		/* debug */
		write ( 1 , cir_buf->head , cir_buf->read_size ) ;
		info ( "%i stdout bytes read", bytes_read ) ;
		/* debug */
		
		/* reconnect code */
		if ( attempt_reconnect )
		{
			time_t curr_time = time ( NULL ) ;
			if ( difftime ( curr_time , last_reconnect_try )  > RECONNECT_RETRY_TIME )
			{
				slurm_close_stream ( task_start->sockets[STDIN_OUT_SOCK] ) ;
				if ( ( task_start->sockets[STDIN_OUT_SOCK] = slurm_open_stream ( & ( task_start -> io_streams_dest ) ) ) == SLURM_PROTOCOL_ERROR )
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
		if ( ( sock_bytes_written = slurm_write_stream ( task_start->sockets[STDIN_OUT_SOCK] , cir_buf->head , cir_buf->read_size ) ) == SLURM_PROTOCOL_ERROR )
		{
			local_errno = errno ;	
			switch ( local_errno )
			{
				case EBADF:
				case EPIPE:
				case ECONNREFUSED:
				case ECONNRESET:
				case ENOTCONN:
					info ( "std out connection losti %i", local_errno ) ;
					attempt_reconnect = true ;
					slurm_close_stream ( task_start->sockets[STDIN_OUT_SOCK] ) ;
					break ;
				default:
					info ( "error sending stdout stream for task %i, errno %i", task_start -> launch_msg -> global_task_ids[ task_start -> local_task_id ] , local_errno ) ;
					error ( "uncaught errno %i", local_errno ) ;
					break ;
			}
			continue ;
		}
		cir_buf_read_update ( cir_buf , sock_bytes_written ) ;
	}

	stdout_return:
	free_circular_buffer ( cir_buf ) ;
	slurm_close_stream ( task_start->sockets[STDIN_OUT_SOCK] ) ;
	pthread_exit ( NULL ) ; 
}

void * stderr_io_pipe_thread ( void * arg )
{
	task_start_t * task_start = ( task_start_t * ) arg ;
	int bytes_read ;
	int sock_bytes_written ;
	int local_errno ;
	int attempt_reconnect = false ;
	time_t last_reconnect_try = 0 ;
	circular_buffer_t * cir_buf ;

	init_circular_buffer ( & cir_buf ) ;
	
	while ( true )
	{
			if ( ( cir_buf->write_size == 0 ) )
			{
				info ( "stderr cir_buf->write_size == 0 this shouldn't happen" ) ;
				continue ;
			}	
		
		/* read stderr code */
		if ( ( bytes_read = read ( task_start->pipes[CHILD_ERR_RD] , cir_buf->tail , cir_buf->write_size ) ) <= 0 )
		{
			debug ( "bytes_read: %i , errno: %i", bytes_read , errno ) ;
			if ( ( bytes_read == SLURM_PROTOCOL_ERROR ) && ( errno == EINTR ) ) 
			{
				continue ;
			}
			else
			{

				local_errno = errno ;	
				info ( "error reading stderr stream for task %i, errno %i , bytes read %i ", task_start -> launch_msg -> global_task_ids[ task_start -> local_task_id ] , local_errno , bytes_read ) ;
				goto stderr_return ;
			}
		}
		else
		{
			cir_buf_write_update ( cir_buf , bytes_read ) ;
		}
		
		/* debug */
		/*
		info ( "%i stderr bytes read", bytes_read ) ;
		write ( 2 , cir_buf->head , cir_buf->read_size ) ;
		*/
		/* debug */

		/* reconnect code */
		if ( attempt_reconnect )
		{
			time_t curr_time = time ( NULL ) ;
			if ( difftime ( curr_time , last_reconnect_try )  > RECONNECT_RETRY_TIME )
			{
				slurm_close_stream ( task_start->sockets[SIG_STDERR_SOCK] ) ;
				if ( ( task_start->sockets[SIG_STDERR_SOCK] = slurm_open_stream ( &( task_start -> io_streams_dest ) ) ) == SLURM_PROTOCOL_ERROR )
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
		if ( ( sock_bytes_written = slurm_write_stream ( task_start->sockets[SIG_STDERR_SOCK] , cir_buf->head , cir_buf->read_size ) ) == SLURM_PROTOCOL_ERROR )
		{
			local_errno = errno ;	
			switch ( local_errno )
			{
				case EBADF:
				case EPIPE:
				case ECONNREFUSED:
				case ECONNRESET:
				case ENOTCONN:
					info ( "std err connection lost %i ", local_errno ) ;
					attempt_reconnect = true ;
					slurm_close_stream ( task_start->sockets[SIG_STDERR_SOCK] ) ;
					break ;
				default:
					info ( "error sending stderr stream for task %i , %m errno: %i", task_start -> launch_msg -> global_task_ids[ task_start -> local_task_id ] , local_errno ) ;
					error ( "uncaught errno %i", local_errno ) ;
					break ;
			}
			continue ;
		}
		cir_buf_read_update ( cir_buf , sock_bytes_written ) ;
	}

	stderr_return:
	free_circular_buffer ( cir_buf ) ;
	slurm_close_stream ( task_start->sockets[SIG_STDERR_SOCK] ) ;
	pthread_exit ( NULL ) ; 
}

int connect_io_stream (  task_start_t * task_start , int out_or_err )
{
	int local_errno ;
	if ( ( task_start->sockets[out_or_err] = slurm_open_stream ( & ( task_start -> io_streams_dest ) ) ) == SLURM_PROTOCOL_ERROR )
	{
		local_errno = errno ;	
		info ( "error opening socket to srun to pipe %s %m errno: %i" , out_or_err ? "stdout" : "stderr" , local_errno ) ;
		return SLURM_PROTOCOL_ERROR ;
	}
	else
	{
		return send_io_stream_header ( task_start , out_or_err) ;
	}
	
}

int send_io_stream_header ( task_start_t * task_start , int out_or_err ) 
{
	slurm_io_stream_header_t io_header ;
	char buffer[sizeof(slurm_io_stream_header_t)] ;
	char * buf_ptr = buffer ;
	int buf_size = sizeof(slurm_io_stream_header_t) ;
	int size = sizeof(slurm_io_stream_header_t) ;


	if( out_or_err == STDIN_OUT_SOCK )
	{
		init_io_stream_header ( & io_header , 
				task_start -> launch_msg -> credential -> signature , 
				task_start -> launch_msg -> global_task_ids[task_start -> local_task_id ] , 
				SLURM_IO_STREAM_INOUT 
				) ;
		pack_io_stream_header ( & io_header , (void ** ) & buf_ptr , & size ) ;
		return slurm_write_stream (  task_start->sockets[STDIN_OUT_SOCK] , buffer , buf_size - size ) ;
	}
	else
	{

		init_io_stream_header ( & io_header , 
				task_start -> launch_msg -> credential -> signature , 
				task_start -> launch_msg -> global_task_ids[task_start -> local_task_id ] , 
				SLURM_IO_STREAM_SIGERR 
				) ;
		pack_io_stream_header ( & io_header , (void ** ) & buf_ptr , & size ) ;
		return slurm_write_stream (  task_start->sockets[SIG_STDERR_SOCK] , buffer , buf_size - size ) ;
	}
}


ssize_t read_EINTR(int fd, void *buf, size_t count)
{
	ssize_t bytes_read ;
	while ( true )
	{
		if ( ( bytes_read = read ( fd, buf, count ) ) <= 0 )
		{
			debug ( "bytes_read: %i , %m errno: %i", bytes_read , errno ) ;
			if ( ( bytes_read == SLURM_PROTOCOL_ERROR ) && ( errno == EINTR ) ) 
			{
				continue ;
			}
		}
		return bytes_read ;
	}
}
       
