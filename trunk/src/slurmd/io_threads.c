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
			debug3 ( "stdin cir_buf->write_size == 0 this shouldn't happen" ) ;
			continue ;
		}
		
		if ( ( bytes_read = slurm_read_stream ( task_start->sockets[STDIN_OUT_SOCK] , cir_buf->tail , cir_buf->write_size ) ) <= 0 )
		{
			local_errno = errno ;	
			if ( bytes_read == 0)
			{
				debug3 ( "0 returned EOF on socket ") ;
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
						debug3 ( "error reading stdin  stream for task %i, %m errno: %i , bytes read %i ", task_start -> launch_msg -> global_task_ids[ task_start -> local_task_id ] , local_errno , bytes_read ) ;
						debug3 ( "uncaught errno %i", local_errno ) ;
						break;
				}
			}
			else
			{
				debug3 ( "bytes_read: %i don't know what to do with this return code ", bytes_read ) ;
			}
		}
		else
		{
			cir_buf_write_update ( cir_buf , bytes_read ) ;
		}
		
		/* debug */
		/*
		write ( 1 ,  "stdin-", 6 ) ;
		write ( 1 , cir_buf->head , cir_buf->read_size ) ;
		debug3 ( "%i stdin bytes read", bytes_read ) ;
		*/
		/* debug */
		
		while ( true)
		{
		
			if ( ( bytes_written = write ( task_start->pipes[CHILD_IN_WR_PIPE] , cir_buf->head , cir_buf->read_size ) ) <= 0 )
			{
				if ( ( bytes_written == SLURM_PROTOCOL_ERROR ) && ( errno == EINTR ) ) 
				{
					continue ;
				}
				else
				{

					local_errno = errno ;	
					debug3 ( "error sending stdin  stream for task %i, %m errno: %i , bytes read %i ", task_start -> launch_msg -> global_task_ids[ task_start -> local_task_id ] , local_errno , bytes_read ) ;
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
	close ( task_start->pipes[CHILD_IN_WR_PIPE] ) ;
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
			debug3 ( "stdout cir_buf->write_size == 0 this shouldn't happen" ) ;
			continue ;
		}	

		/* read stdout code */
		if ( ( bytes_read = read_EINTR ( task_start->pipes[CHILD_OUT_RD_PIPE] , cir_buf->tail , cir_buf->write_size ) ) <= 0 )
		{
			local_errno = errno ;	
			debug3 ( "error reading stdout stream for task %i, %m errno: %i , bytes read %i ", 
					task_start -> launch_msg -> global_task_ids[ task_start -> local_task_id ] , local_errno , bytes_read ) ;
			goto stdout_return ;
		}
		else
		{
			cir_buf_write_update ( cir_buf , bytes_read ) ;
		}
		
		/* debug */
		write ( 1 , cir_buf->head , cir_buf->read_size ) ;
		debug3 ( "%i stdout bytes read", bytes_read ) ;
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
					debug3 ( "error reconnecting socket to srun to pipe stdout errno %i" , local_errno ) ;
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
					debug3 ( "std out connection losti %i", local_errno ) ;
					attempt_reconnect = true ;
					slurm_close_stream ( task_start->sockets[STDIN_OUT_SOCK] ) ;
					break ;
				default:
					debug3 ( "error sending stdout stream for task %i, errno %i", task_start -> launch_msg -> global_task_ids[ task_start -> local_task_id ] , local_errno ) ;
					debug3 ( "uncaught errno %i", local_errno ) ;
					break ;
			}
			continue ;
		}
		cir_buf_read_update ( cir_buf , sock_bytes_written ) ;
	}

	stdout_return:
	free_circular_buffer ( cir_buf ) ;
	slurm_close_stream ( task_start->sockets[STDIN_OUT_SOCK] ) ;
	close ( task_start->pipes[CHILD_OUT_RD_PIPE] ) ;
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
	
	posix_signal_pipe_ignore ( ) ;
	
	while ( true )
	{
		if ( ( cir_buf->write_size == 0 ) )
		{
			debug3 ( "stderr cir_buf->write_size == 0 this shouldn't happen" ) ;
			continue ;
		}	

		/* read stderr code */
		if ( ( bytes_read = read_EINTR ( task_start->pipes[CHILD_ERR_RD_PIPE] , cir_buf->tail , cir_buf->write_size ) ) <= 0 )
		{
			local_errno = errno ;	
				debug3 ( "error reading stderr stream for task %i, errno %i , bytes read %i ", 
						task_start -> launch_msg -> global_task_ids[ task_start -> local_task_id ] , local_errno , bytes_read ) ;
				goto stderr_return ;
		}
		else
		{
			cir_buf_write_update ( cir_buf , bytes_read ) ;
		}
		
		/* debug */
		/*
		debug3 ( "%i stderr bytes read", bytes_read ) ;
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
					debug3 ( "error reconnecting socket to srun to pipe stderr errno %i" , local_errno ) ;
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
					debug3 ( "std err connection lost %i ", local_errno ) ;
					attempt_reconnect = true ;
					slurm_close_stream ( task_start->sockets[SIG_STDERR_SOCK] ) ;
					break ;
				default:
					debug3 ( "error sending stderr stream for task %i , %m errno: %i", task_start -> launch_msg -> global_task_ids[ task_start -> local_task_id ] , local_errno ) ;
					debug3 ( "uncaught errno %i", local_errno ) ;
					break ;
			}
			continue ;
		}
		cir_buf_read_update ( cir_buf , sock_bytes_written ) ;
	}

	stderr_return:
	free_circular_buffer ( cir_buf ) ;
	slurm_close_stream ( task_start->sockets[SIG_STDERR_SOCK] ) ;
	close ( task_start->pipes[CHILD_ERR_RD_PIPE] ) ;
	pthread_exit ( NULL ) ; 
}

