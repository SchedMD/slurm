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

typedef enum 
{
	IN_OUT_FD ;
	SIG_ERR_FD ;
	CHILD_IN_WR_FD ;
	CHILD_OUT_RD_FD ;
	CHILD_ERR_RD_RD ;
} nbio_fd_t ;

typedef enum
{
	RD_SET ;
	WR_SET ;
	ER_SET ;
} nbio_set_t ;

typedef struct nbio_attr
{
	task_start_t * task_start ;
	slurm_fd_set init_set[3] ;
	slurm_fd_set next_set[3] ;
	slurm_fd fd [5] ;
	circular_buffer_t * in_cir_buf ;
	circular_buffer_t * out_cir_buf ;
	circular_buffer_t * err_cir_buf ;
	int nbio_flags [5] ;
	int max_fd ;
} nbio_attr_t ;

int init_nbio_attr ( nbio_attr_t * nbio_attr , task_start_t * task_start )
{
	nbio_attr -> max_fd = 0 ; 
	nbio_attr -> task_start = task_start ;
	nbio_attr -> fd[IN_OUT_FD] = task_start -> sockets[STDIN_OUT_SOCK];
	nbio_attr -> fd[SIG_ERR_FD] = task_start -> sockets[SIG_STDERR_SOCK];
	nbio_attr -> fd[CHILD_IN_WR_FD] = task_start -> pipes[CHILD_IN_WR];
	nbio_attr -> fd[CHILD_OUT_RD_FD] = task_start -> pipes[CHILD_OUT_RD];
	nbio_attr -> fd[CHILD_ERR_RD_FD] = task_start -> pipes[CHILD_ERR_RD];
	init_circular_buffer ( & cir_buf ) ;
	init_circular_buffer ( & cir_buf ) ;
	init_circular_buffer ( & cir_buf ) ;
	nbio_set_init (  nbio_attr , nbio_attr -> init_set ) ;
}

void * do_nbio ( void * arg )
{
	nbio_attr_t nbio_attr ;
	task_start_t * task_start = ( task_start_t * ) arg ;

	posix_signal_pipe_ignore ( ) ;
	init_nbio_attr ( & nbio_attr , task_start ) ;

	while ( true ) 
	{
		int rc = slurm_select ( nbio_attr . max_fd , nbio_attr . init_set[RD_SET] , nbio_attr . init_set[WR_SET] , nbio_attr . init_set[ER_SET] , NULL ) ;

		nbio_set_init ( & nbio_attr , nbio_attr -> next_set )

		if ( slurm_FD_ISSET ( set_ptr[ER_SET] , nbio_attr . fd [CHILD_IN_WR_FD] ) ) { error_task_pipe ( ) }
		if ( slurm_FD_ISSET ( set_ptr[ER_SET] , nbio_attr . fd [CHILD_OUT_RD_FD] ) ) { error_task_pipe ( ) }
		if ( slurm_FD_ISSET ( set_ptr[ER_SET] , nbio_attr . fd [CHILD_ERR_RD_RD] ) ) { error_task_pipe ( ) }
		if ( slurm_FD_ISSET ( set_ptr[ER_SET] , nbio_attr . fd [IN_OUT_FD] ) ) { error_task_socket ( ) }
		if ( slurm_FD_ISSET ( set_ptr[ER_SET] , nbio_attr . fd [SIG_ERR_FD] ) ) { error_task_socket ( ) }

		if ( slurm_FD_ISSET ( init_set [RD_SET] , nbio_attr . fd [IN_OUT_FD] ) ) 
		{ 
			read_task_socket ( nbio_attr . in_cir_buf , nbio_attr . task_start , nbio_attr . fd [IN_OUT_FD] , "stdin" ); 
			slurm_FD_SET ( next_set [WR_SET] , nbio_attr . fd [CHILD_IN_WR_FD] ); 
		}
		if ( slurm_FD_ISSET ( init_set [RD_SET] , nbio_attr . fd [CHILD_OUT_RD_FD] ) ) 
		{ 
			read_task_pipe ( nbio_attr . out_cir_buf , nbio_attr . task_start , nbio_attr . fd [CHILD_OUT_RD_FD] , "stdout" ); 
			slurm_FD_SET ( next_set [WR_SET] , nbio_attr . fd [IN_OUT_FD] ); 
		}
		if ( slurm_FD_ISSET ( init_set [RD_SET] , nbio_attr . fd [CHILD_ERR_RD_RD] ) ) 
		{ 
			read_task_pipe ( nbio_attr . err_cir_buf , nbio_attr . task_start , nbio_attr . fd [CHILD_ERR_RD_FD] , "stderr" ); 
			slurm_FD_SET ( next_set [WR_SET] , nbio_attr .  fd [SIG_ERR_FD] ); 
		}

		if ( slurm_FD_ISSET ( init_set [WR_SET] , nbio_attr -> fd [CHILD_IN_WR_FD] ) ) 
		{ 
			write_task_pipe ( nbio_attr . in_cir_buf , nbio_attr . task_start , nbio_attr . fd [CHILD_IN_WR_FD] , "stdin" );
			slurm_FD_CLR ( next_set [WR_SET] , nbio_attr . fd [CHILD_IN_WR_FD] ); 
		}
		if ( slurm_FD_ISSET ( init_set [WR_SET] , nbio_attr -> fd [IN_OUT_FD] ) ) 
		{ 
			write_task_socket ( nbio_attr . out_cir_buf , nbio_attr . task_start , nbio_attr . fd [CHILD_OUT_WR_FD] , "stdout" );
			slurm_FD_SET ( next_set [WR_SET] , nbio_attr . fd [IN_OUT_FD] ); 
		}
		if ( slurm_FD_ISSET ( init_set [WR_SET] , nbio_attr -> fd [SIG_ERR_FD] ) ) 
		{ 
			write_task_socket ( nbio_attr . err_cir_buf , nbio_attr . task_start , nbio_attr . fd [CHILD_ERR_WR_FD] , "stderr" ); 
			slurm_FD_SET ( next_set [WR_SET] , nbio_attr .  fd [SIG_ERR_FD] ); 
		}

		memcpy_sets ( init_set , next_set ) ;
	}
}

int memcpy_sets ( slurm_fd_set * init_set , slurm_fd_set * next_set )
{
	int i ;
	
	for ( i=0 ; i < 3 ; i++ )
	{
		memcpy ( & init_set[i] , & next_set[i] , sizeof ( slurm_fd_set ) ) ;
	}
}

int read_task_pipe ( circular_buffer_t * cir_buf, task_start_t * task_start, slurm_fd write_fd , char * const name )
{
	int bytes_read ;
	int local_errno ;
	
	/* test for wierd state */
	if ( ( cir_buf->write_size == 0 ) )
	{
		info ( "%s cir_buf->write_size == 0 this shouldn't happen" , name ) ;
		slurm_seterrno_ret ( ESLURMD_CIRBUF_POINTER_0 ) ;
	}

	/* read stdout code */
	if ( ( bytes_read = read_EINTR ( read_fd , cir_buf->tail , cir_buf->write_size ) ) <= 0 )
	{
		local_errno = errno ;
		info ( "error reading %s stream for task %i, %m errno: %i , bytes read %i ",
				name , task_start -> launch_msg -> global_task_ids[ task_start -> local_task_id ] , local_errno , bytes_read ) ;
		slurm_seterrno_ret ( ESLURMD_PIPE_DISCONNECT ) ;
	}
	else
	{
		cir_buf_write_update ( cir_buf , bytes_read ) ;
		return SLURM_SUCCESS ;
	}
}


int write_task_pipe ( circular_buffer_t * cir_buf, task_start_t * task_start, slurm_fd write_fd , char * const name ) 
{
	int bytes_written ;
	int local_errno ;
	
	/* test for wierd state */
	if ( ( cir_buf->read_size == 0 ) )
	{
		info ( "%s cir_buf->read_size == 0 this shouldn't happen" , name ) ;
		slurm_seterrno_ret ( ESLURMD_CIRBUF_POINTER_0 ) ;
	}

	if ( ( bytes_written = write_EINTR ( write_fd , cir_buf->head , cir_buf->read_size ) ) <= 0 )
	{
		local_errno = errno ;
		info ( "error sending %s stream for task %i, %m errno: %i , bytes read %i ", 
				name , task_start -> launch_msg -> global_task_ids[ task_start -> local_task_id ] , local_errno , bytes_read ) ;
		slurm_seterrno_ret ( ESLURMD_PIPE_DISCONNECT ) ;
	}
	else
	{
		cir_buf_read_update ( cir_buf , bytes_written ) ;
		return SLURM_SUCCESS ;
	}
}

int read_task_socket ( circular_buffer_t * cir_buf, task_start_t * task_start, slurm_fd read_fd , char * const name ) 
{
	int bytes_read ;
	int local_errno ;
	
	/* test for wierd state */
	if ( ( cir_buf->write_size == 0 ) )
	{
		info ( "%s cir_buf->write_size == 0 this shouldn't happen" , name ) ;
		slurm_seterrno_ret ( ESLURMD_CIRBUF_POINTER_0 ) ;
	}

	if ( ( bytes_read = slurm_read_stream ( read_fd , cir_buf->tail , cir_buf->write_size ) ) <= 0 )
	{
		local_errno = errno ;	
		/* test for EOF on socket */
		if ( bytes_read == 0)
		{
			info ( "0 returned EOF on socket ") ;
			slurm_seterrno_ret ( ESLURMD_EOF_ON_SOCKET ) ;
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
					info ("lost %s socket connectiont" , name );
					slurm_seterrno_ret ( ESLURMD_SOCKET_DISCONNECT ) ;
					break ;
				default:
					info ( "error reading %s stream for task %i, %m errno: %i , bytes read %i ", 
							name , task_start -> launch_msg -> global_task_ids[ task_start -> local_task_id ] , local_errno , bytes_read ) ;
					error ( "uncaught errno %i", local_errno ) ;
					slurm_seterrno_ret ( ESLURMD_UNKNOWN_SOCKET_ERROR ) ;
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
	return SLURM_SUCCESS ;
}

int write_task_socket ( circular_buffer_t * cir_buf, task_start_t * task_start, slurm_fd write_fd , char * const name ) 
{
	int sock_bytes_written ;
	int local_errno ;
	
	/* test for wierd state */
	if ( ( cir_buf->read_size == 0 ) )
	{
		info ( "%s cir_buf->read_size == 0 this shouldn't happen" , name ) ;
		slurm_seterrno_ret ( ESLURMD_CIRBUF_POINTER_0 ) ;
	}

	if ( ( sock_bytes_written = slurm_write_stream ( write_fd , cir_buf->head , cir_buf->read_size ) ) <= 0 )
	{
		local_errno = errno ;
		/* test for EOF on socket */
		if ( sock_bytes_written == 0)
		{
			info ( "0 returned EOF on socket ") ;
			slurm_seterrno_ret ( ESLURMD_EOF_ON_SOCKET ) ;
		}
		else if ( sock_bytes_written == -1 )
		{
			switch ( local_errno )
			{
				case EBADF:
				case EPIPE:
				case ECONNREFUSED:
				case ECONNRESET:
				case ENOTCONN:
					info ( "%s connection losti %i", name , local_errno ) ;
					slurm_close_stream ( task_start->sockets[STDIN_OUT_SOCK] ) ;
					slurm_seterrno_ret ( ESLURMD_SOCKET_DISCONNECT ) ;
					break ;
				default:
					info ( "error sending %s stream for task %i, errno %i", 
							name , task_start -> launch_msg -> global_task_ids[ task_start -> local_task_id ] , local_errno ) ;
					error ( "uncaught errno %i", local_errno ) ;
					slurm_seterrno_ret ( ESLURMD_UNKNOWN_SOCKET_ERROR ) ;
					break ;
			}
		}
		else
		{
			info ( "bytes_read: %i don't know what to do with this return code ", bytes_read ) ;
		}
	}
	else
	{
		cir_buf_read_update ( cir_buf , sock_bytes_written ) ;
	}
	return SLURM_SUCCESS ;
}

int error_task_pipe ( ) 
{
}

int error_task_socket ( ) 
{
}

int nbio_set_init ( nbio_attr_t * nbio_attr , slurm_fd_set * set_ptr )
{
	int i ;
	for ( i=0 ; i < 3 ; i++ )
	{
		FD_ZERO ( nbio_attr -> init_set[i] ) ;
	}
	slurm_FD_SET ( set_ptr[RD_SET] , nbio_attr -> fd [IN_OUT_FD] ) ;
	slurm_FD_SET ( set_ptr[RD_SET] , nbio_attr -> fd [CHILD_OUT_RD_FD] ) ;
	slurm_FD_SET ( set_ptr[RD_SET] , nbio_attr -> fd [CHILD_ERR_RD_RD] ) ;
	
	for ( i=0 ; i < 5 ; i++ )
	{
		slurm_FD_SET ( set_ptr[ER_SET] , nbio_attr -> fd [i] ) ;
	}

	nbio_attr -> max_fd  = 0 ;

	for ( i=0 ; i < 5 ; i++ )
	{
		nbio_attr -> max_fd = MAX ( nbio_attr -> max_fd , nbio_attr -> fd [ i ] ) ;
	}
	return SLURM_SUCCESS ;
}
