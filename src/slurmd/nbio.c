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
#include <src/slurmd/reconnect_utils.h>
#include <src/slurmd/io.h>
#include <src/slurmd/pipes.h>
#include <src/slurmd/nbio.h>

#define RECONNECT_TIMEOUT_SECONDS 1
#define RECONNECT_TIMEOUT_MICROSECONDS 0
typedef enum 
{
	IN_OUT_FD ,
	SIG_ERR_FD ,
	CHILD_IN_WR_FD ,
	CHILD_OUT_RD_FD ,
	CHILD_ERR_RD_FD 
} nbio_fd_t ;

typedef enum
{
	RD_SET ,
	WR_SET ,
	ER_SET 
} nbio_set_t ;

typedef enum
{
	CONNECTED ,
	RECONNECT ,
	DRAIN, 
	DRAINED
} reconnect_state_t ;

typedef struct nbio_attr
{
	task_start_t * task_start ;
	slurm_fd_set init_set[3] ;
	slurm_fd_set next_set[3] ;
	slurm_fd fd [5] ;
	circular_buffer_t * in_cir_buf ;
	circular_buffer_t * out_cir_buf ;
	circular_buffer_t * err_cir_buf ;
	int flush_flag ;
	int die ;
	int reconnect_flags [2] ;
	time_t reconnect_timers [2] ;
	int max_fd ;
	struct timeval select_timer ;
} nbio_attr_t ;

typedef struct io_debug
{
	char * name ;
	int local_task_id ;
	int global_task_id ;
} io_debug_t ;

/* TODO
 * timers on reconnect
 * line oriented code
 */
int nbio_set_init ( nbio_attr_t * nbio_attr , slurm_fd_set * set_ptr ) ;
int memcpy_sets ( slurm_fd_set * init_set , slurm_fd_set * next_set ) ;
int write_task_socket ( circular_buffer_t * cir_buf, slurm_fd write_fd , io_debug_t * dbg ) ;
int read_task_pipe ( circular_buffer_t * cir_buf, slurm_fd write_fd , io_debug_t * dbg ) ;
int write_task_pipe ( circular_buffer_t * cir_buf, slurm_fd write_fd , io_debug_t * dbg ) ;
int read_task_socket ( circular_buffer_t * cir_buf, slurm_fd read_fd , io_debug_t * dbg ) ;
int error_task_pipe ( nbio_attr_t * nbio_attr , int fd_index ) ;
int error_task_socket ( nbio_attr_t * nbio_attr , int fd_index ) ;
int set_max_fd ( nbio_attr_t * nbio_attr ) ;
int nbio_cleanup ( nbio_attr_t * nbio_attr ) ;
int reconnect (  nbio_attr_t * nbio_attr ) ;
int test_error_conditions (  nbio_attr_t * nbio_attr ) ;
int print_nbio_sets ( nbio_attr_t * nbio_attr , slurm_fd_set * set_ptr ) ;

int init_io_debug ( io_debug_t * io_dbg , task_start_t * task_start , char * name )
{
	io_dbg -> name = name ;
	io_dbg -> local_task_id = task_start -> local_task_id ;
	io_dbg -> global_task_id = task_start -> launch_msg -> global_task_ids[ task_start -> local_task_id ] ;
	return SLURM_SUCCESS ;
}

int init_nbio_attr ( nbio_attr_t * nbio_attr , task_start_t * task_start )
{
	int i;
	nbio_attr -> max_fd = 0 ; 
	nbio_attr -> flush_flag = false ; 
	nbio_attr -> die = false ; 
	nbio_attr -> task_start = task_start ;
	nbio_attr -> fd[IN_OUT_FD] = task_start -> sockets[STDIN_OUT_SOCK];
	nbio_attr -> fd[SIG_ERR_FD] = task_start -> sockets[SIG_STDERR_SOCK];
	nbio_attr -> fd[CHILD_IN_WR_FD] = task_start -> pipes[CHILD_IN_WR_PIPE];
	nbio_attr -> fd[CHILD_OUT_RD_FD] = task_start -> pipes[CHILD_OUT_RD_PIPE];
	nbio_attr -> fd[CHILD_ERR_RD_FD] = task_start -> pipes[CHILD_ERR_RD_PIPE];
	init_circular_buffer ( & nbio_attr -> in_cir_buf ) ;
	init_circular_buffer ( & nbio_attr -> out_cir_buf ) ;
	init_circular_buffer ( & nbio_attr -> err_cir_buf ) ;
	for ( i=0 ; i < 2 ; i ++ ) 
	{ 
		nbio_attr -> reconnect_flags[i] = RECONNECT ; 
		nbio_attr -> reconnect_timers[i] = 0 ;
	}
	nbio_set_init ( nbio_attr , nbio_attr -> init_set ) ;
	nbio_attr -> select_timer . tv_sec = RECONNECT_TIMEOUT_SECONDS ;
	nbio_attr -> select_timer . tv_usec = RECONNECT_TIMEOUT_MICROSECONDS ;
	return SLURM_SUCCESS ;
}

void * do_nbio ( void * arg )
{
	nbio_attr_t nbio_attr ;
	task_start_t * task_start = ( task_start_t * ) arg ;
	io_debug_t in_dbg ;
	io_debug_t out_dbg ;
	io_debug_t err_dbg ;

	init_io_debug ( & in_dbg , task_start , "stdin" ) ;
	init_io_debug ( & out_dbg , task_start , "stdout" ) ;
	init_io_debug ( & err_dbg , task_start , "stderr" ) ;
	init_nbio_attr ( & nbio_attr , task_start ) ;

	posix_signal_pipe_ignore ( ) ;
	
	reconnect ( & nbio_attr ) ;

	while ( true ) 
	{
		int rc ;

		set_max_fd ( & nbio_attr ) ;

		print_nbio_sets ( & nbio_attr , nbio_attr . init_set ) ;
		rc = slurm_select ( nbio_attr . max_fd , & nbio_attr . init_set[RD_SET] , & nbio_attr . init_set[WR_SET] , & nbio_attr . init_set[ER_SET] , & nbio_attr . select_timer ) ;
		debug3 ( "nbio select: rc: %i", rc ) ;
		print_nbio_sets ( & nbio_attr , nbio_attr . init_set ) ;
		if ( rc == SLURM_ERROR)
		{
			debug3 ( "select errror %m errno: %i", errno ) ;
			nbio_set_init ( & nbio_attr , nbio_attr . init_set ) ;
			continue ;
		}
		else if  ( rc == 0 )
		{
			reconnect ( & nbio_attr ) ;
			nbio_set_init ( & nbio_attr , nbio_attr .  init_set ) ;
			/* these are here to set the write set after the fd numbers could have changed in reconnect */
			if ( nbio_attr . out_cir_buf -> read_size > 0 ) { slurm_FD_SET ( nbio_attr . fd [IN_OUT_FD] , & nbio_attr . init_set [WR_SET] ); }
			if ( nbio_attr . err_cir_buf -> read_size > 0 ) { slurm_FD_SET ( nbio_attr . fd [SIG_ERR_FD] , & nbio_attr . init_set [WR_SET] ); }
			if ( test_error_conditions ( & nbio_attr ) ) break ;
			
			nbio_attr . select_timer . tv_sec = RECONNECT_TIMEOUT_SECONDS ;
			nbio_attr . select_timer . tv_usec = RECONNECT_TIMEOUT_MICROSECONDS ;
			continue ;
		}
		else if ( rc < 0 )
		{
			debug3 ( "select has unknown error: %i", rc ) ;
			break ;
		}
		
		if ( test_error_conditions ( & nbio_attr ) ) break ;
		
		nbio_set_init ( & nbio_attr , nbio_attr . next_set ) ;

		/* error fd set */
		if ( slurm_FD_ISSET (  nbio_attr . fd [CHILD_IN_WR_FD] , & nbio_attr . init_set [ER_SET] ) ) 
		{ 
			error_task_pipe ( &nbio_attr ,  CHILD_IN_WR_FD ) ;
		}
		if ( slurm_FD_ISSET ( nbio_attr . fd [CHILD_OUT_RD_FD] , & nbio_attr . init_set [ER_SET] ) ) 
		{ 
			error_task_pipe (  &nbio_attr , CHILD_OUT_RD_FD ) ;
		}
		if ( slurm_FD_ISSET ( nbio_attr . fd [CHILD_ERR_RD_FD] , & nbio_attr . init_set [ER_SET] ) ) 
		{
			error_task_pipe (  &nbio_attr , CHILD_ERR_RD_FD ) ;
		}
		if ( slurm_FD_ISSET ( nbio_attr . fd [IN_OUT_FD] , & nbio_attr . init_set [ER_SET] ) ) 
		{ 
			error_task_socket (  &nbio_attr , IN_OUT_FD ) ;
		}
		if ( slurm_FD_ISSET ( nbio_attr . fd [SIG_ERR_FD] , & nbio_attr . init_set [ER_SET] ) ) 
		{ 
			error_task_socket (  &nbio_attr , SIG_ERR_FD ) ;
		}

		/* read fd set */
		if ( slurm_FD_ISSET ( nbio_attr . fd [IN_OUT_FD] , & nbio_attr . init_set [RD_SET] ) 
				&& nbio_attr . reconnect_flags [IN_OUT_FD] == CONNECTED ) 
		{ 
			if ( read_task_socket ( nbio_attr . in_cir_buf , nbio_attr . fd [IN_OUT_FD] , & in_dbg ) )
			{
				error_task_socket (  &nbio_attr , IN_OUT_FD ) ;
			}
			else
			slurm_FD_SET ( nbio_attr . fd [CHILD_IN_WR_FD] , & nbio_attr . next_set [WR_SET] ); 
		}
		if ( slurm_FD_ISSET ( nbio_attr . fd [CHILD_OUT_RD_FD] , & nbio_attr . init_set [RD_SET] ) ) 
		{ 
			if ( read_task_pipe ( nbio_attr . out_cir_buf , nbio_attr . fd [CHILD_OUT_RD_FD] , & out_dbg ) )
			{
				error_task_pipe ( & nbio_attr , CHILD_OUT_RD_FD ) ;
			}
			else
			slurm_FD_SET ( nbio_attr . fd [IN_OUT_FD] , & nbio_attr . next_set [WR_SET] ); 
		}
		if ( slurm_FD_ISSET ( nbio_attr . fd [CHILD_ERR_RD_FD] , & nbio_attr . init_set [RD_SET] ) ) 
		{ 
			if ( read_task_pipe ( nbio_attr . err_cir_buf , nbio_attr . fd [CHILD_ERR_RD_FD] , & err_dbg ) )
			{
				error_task_pipe ( & nbio_attr , CHILD_ERR_RD_FD ) ;
			}
			else
			slurm_FD_SET ( nbio_attr .  fd [SIG_ERR_FD] , & nbio_attr . next_set [WR_SET] ); 
		}

		/* write fd set */
		if ( slurm_FD_ISSET ( nbio_attr . fd [CHILD_IN_WR_FD] , & nbio_attr . next_set [WR_SET] ) ) 
		{ 
			if ( write_task_pipe ( nbio_attr . in_cir_buf , nbio_attr . fd [CHILD_IN_WR_FD] , & in_dbg ) )
			{
				error_task_pipe ( & nbio_attr , CHILD_IN_WR_FD ) ;
			}
			else
			slurm_FD_CLR ( nbio_attr . fd [CHILD_IN_WR_FD] , & nbio_attr . next_set [WR_SET] ); 
		}
		if ( slurm_FD_ISSET ( nbio_attr . fd [IN_OUT_FD] , & nbio_attr . next_set [WR_SET] ) 
				&& nbio_attr . reconnect_flags [IN_OUT_FD] == CONNECTED ) 
		{ 
			if ( write_task_socket ( nbio_attr . out_cir_buf , nbio_attr . fd [IN_OUT_FD] , & out_dbg ) )
			{
				error_task_socket (  &nbio_attr , IN_OUT_FD ) ;
			}
			else
			slurm_FD_CLR ( nbio_attr . fd [IN_OUT_FD] , & nbio_attr . next_set [WR_SET] ); 
		}
		if ( slurm_FD_ISSET ( nbio_attr . fd [SIG_ERR_FD] , & nbio_attr . next_set [WR_SET] ) 
				&& nbio_attr . reconnect_flags [IN_OUT_FD] == CONNECTED ) 
		{ 
			if ( write_task_socket ( nbio_attr . err_cir_buf , nbio_attr . fd [SIG_ERR_FD] , & err_dbg ) )
			{
				error_task_socket (  &nbio_attr , SIG_ERR_FD ) ;
			}
			else
			slurm_FD_CLR ( nbio_attr .  fd [SIG_ERR_FD] , & nbio_attr . next_set [WR_SET] ); 
		}

		if ( nbio_attr . flush_flag )
		{
			nbio_set_init ( & nbio_attr , nbio_attr .  init_set ) ;
		}
		else
		{
			memcpy_sets ( nbio_attr . init_set , nbio_attr . next_set ) ;
		}
	}

	nbio_cleanup ( & nbio_attr ) ;
	return SLURM_SUCCESS ;
}

int memcpy_sets ( slurm_fd_set * init_set , slurm_fd_set * next_set )
{
	int i ;

	for ( i=0 ; i < 3 ; i++ )
	{
		memcpy ( & init_set[i] , & next_set[i] , sizeof ( slurm_fd_set ) ) ;
	}
	return SLURM_SUCCESS ;
}

int read_task_pipe ( circular_buffer_t * cir_buf, slurm_fd read_fd , io_debug_t * dbg )
{
	int bytes_read ;
	int local_errno ;

	/* test for wierd state */
	if ( ( cir_buf->write_size == 0 ) )
	{
		if ( dbg ) debug3 ( "%s cir_buf->write_size == 0 this shouldn't happen" , dbg -> name ) ;
		slurm_seterrno_ret ( ESLURMD_CIRBUF_POINTER_0 ) ;
	}

	/* read stdout code */
	if ( ( bytes_read = read_EINTR ( read_fd , cir_buf->tail , cir_buf->write_size ) ) <= 0 )
	{
		local_errno = errno ;
		if ( dbg ) debug3 ( "%i error reading %s pipe stream, %m errno: %i , bytes read %i ",
				dbg -> global_task_id , dbg -> name , local_errno , bytes_read ) ;
		slurm_seterrno_ret ( ESLURMD_PIPE_DISCONNECT ) ;
	}
	else
	{
		cir_buf_write_update ( cir_buf , bytes_read ) ;
		debug3 ( "read_task_pipe fd: %i bytes_read %i" , read_fd , bytes_read ) ;
		return SLURM_SUCCESS ;
	}
}


int write_task_pipe ( circular_buffer_t * cir_buf, slurm_fd write_fd , io_debug_t * dbg ) 
{
	int bytes_written ;
	int local_errno ;

	/* test for wierd state */
	if ( ( cir_buf->read_size == 0 ) )
	{
		if ( dbg ) debug3 ( "%s cir_buf->read_size == 0 this shouldn't happen" , dbg -> name ) ;
		slurm_seterrno_ret ( ESLURMD_CIRBUF_POINTER_0 ) ;
	}

	if ( ( bytes_written = write_EINTR ( write_fd , cir_buf->head , cir_buf->read_size ) ) <= 0 )
	{
		local_errno = errno ;
		if ( dbg ) debug3 ( "%i error sending %s pipe stream, %m errno: %i , bytes written %i ", 
				dbg -> global_task_id , dbg -> name , local_errno , bytes_written ) ;
		slurm_seterrno_ret ( ESLURMD_PIPE_DISCONNECT ) ;
	}
	else
	{
		cir_buf_read_update ( cir_buf , bytes_written ) ;
		//debug3 ( "write_task_pipe fd: %i bytes_written %i" , write_fd , bytes_written ) ;
		return SLURM_SUCCESS ;
	}
}

int read_task_socket ( circular_buffer_t * cir_buf, slurm_fd read_fd , io_debug_t * dbg ) 
{
	int bytes_read ;
	int local_errno ;

	/* test for wierd state */
	if ( ( cir_buf->write_size == 0 ) )
	{
		if ( dbg ) debug3 ( "%s cir_buf->write_size == 0 this shouldn't happen" , dbg -> name ) ;
		slurm_seterrno_ret ( ESLURMD_CIRBUF_POINTER_0 ) ;
	}

	if ( ( bytes_read = slurm_read_stream ( read_fd , cir_buf->tail , cir_buf->write_size ) ) <= 0 )
	{
		local_errno = errno ;	
		/* test for EOF on socket */
		if ( bytes_read == 0)
		{
			if ( dbg ) debug3 ( "%i 0 returned EOF on socket" , dbg -> global_task_id ) ;
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
					if ( dbg ) debug3 ("lost %s socket connection %m errno: %i" , dbg -> name , local_errno );
					slurm_seterrno_ret ( ESLURMD_SOCKET_DISCONNECT ) ;
					break ;
				default:
					if ( dbg ) debug3 ( "%i error reading %s sock stream, %m errno: %i , bytes read %i ", 
							dbg -> global_task_id , dbg -> name , local_errno , bytes_read ) ;
					slurm_seterrno_ret ( ESLURMD_UNKNOWN_SOCKET_ERROR ) ;
					break;
			}
		}
		else
		{
			debug3 ( "bytes_read: %i don't know what to do with this return code ", bytes_read ) ;
			slurm_seterrno_ret ( ESLURMD_UNKNOWN_SOCKET_ERROR ) ;
		}
	}
	else
	{
		cir_buf_write_update ( cir_buf , bytes_read ) ;
		//debug3 ( "read_task_socket fd: %i bytes_read %i" , read_fd , bytes_read ) ;
		return SLURM_SUCCESS ;
	}
}

int write_task_socket ( circular_buffer_t * cir_buf, slurm_fd write_fd , io_debug_t * dbg ) 
{
	int sock_bytes_written ;
	int local_errno ;

	/* test for wierd state */
	if ( ( cir_buf->read_size == 0 ) )
	{
		if ( dbg ) debug3 ( "%s cir_buf->read_size == 0 this shouldn't happen" , dbg -> name ) ;
		slurm_seterrno_ret ( ESLURMD_CIRBUF_POINTER_0 ) ;
	}

	if ( ( sock_bytes_written = slurm_write_stream ( write_fd , cir_buf->head , cir_buf->read_size ) ) <= 0 )
	{
		local_errno = errno ;
		/* test for EOF on socket */
		if ( sock_bytes_written == 0)
		{
			if ( dbg ) debug3 ( "%i 0 returned EOF on socket" , dbg -> global_task_id ) ;
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
					if ( dbg ) debug3 ( "lost %s socket connection %m errno: %i", dbg -> name , local_errno ) ;
					slurm_seterrno_ret ( ESLURMD_SOCKET_DISCONNECT ) ;
					break ;
				default:
					if ( dbg ) debug3 ( "%i error sending %s sock stream, %m errno %i, sock bytes written %i", 
							dbg -> global_task_id, dbg -> name , local_errno , sock_bytes_written ) ;
					slurm_seterrno_ret ( ESLURMD_UNKNOWN_SOCKET_ERROR ) ;
					break ;
			}
		}
		else
		{
			debug3 ( "bytes_read: %i don't know what to do with this return code ", sock_bytes_written ) ;
			slurm_seterrno_ret ( ESLURMD_UNKNOWN_SOCKET_ERROR ) ;
		}
	}
	else
	{
		cir_buf_read_update ( cir_buf , sock_bytes_written ) ;
		debug3 ( "write_task_socket fd: %i bytes_written %i" , write_fd , sock_bytes_written ) ;
		return SLURM_SUCCESS ;
	}
}

int error_task_pipe ( nbio_attr_t * nbio_attr , int fd_index )
{
	switch ( errno )
	{	
		case ESLURMD_CIRBUF_POINTER_0 :
			break ;
		case ESLURMD_PIPE_DISCONNECT :
			nbio_attr -> flush_flag = true ; 
			break ;
	}
	return SLURM_SUCCESS ;
}

int error_task_socket ( nbio_attr_t * nbio_attr , int fd_index )
{
	switch ( errno )
	{	
		case ESLURMD_CIRBUF_POINTER_0 :
			if ( nbio_attr -> flush_flag )
			{
				nbio_attr -> reconnect_flags[fd_index] = DRAINED ;
			}
			else
			{
				debug3 ( "ESLURMD_CIRBUF_POINTER_0 shouldn't have occured" ) ;
			}	
			break ;
		case ESLURMD_UNKNOWN_SOCKET_ERROR :
		case ESLURMD_SOCKET_DISCONNECT :
		case ESLURMD_EOF_ON_SOCKET :
			if ( !slurm_close_stream ( nbio_attr -> fd [fd_index] ) ) ;
			{
				nbio_attr -> fd [fd_index] = -1 ;
			}
			switch ( nbio_attr -> reconnect_flags[fd_index] )
			{
				case CONNECTED :
					nbio_attr -> reconnect_flags[fd_index] = RECONNECT ;
					break ;
				case DRAIN :
				case DRAINED :
					nbio_attr -> die = true;
					break ;
				case RECONNECT :
					break ;
				default :
					debug3 ( "Unknown case in error_task_socket:ESLURMD_EOF_ON_SOCKET: %i" , nbio_attr -> reconnect_flags[fd_index] ) ;
					break ;
			}
			break ;
		default :
			debug3 ( "Unknown case in error_task_socket: %i" , nbio_attr -> reconnect_flags[fd_index] ) ;
			break ;
	}
	return SLURM_SUCCESS ;
}

int nbio_set_init ( nbio_attr_t * nbio_attr , slurm_fd_set * set_ptr )
{
	int i ;

	for ( i=0 ; i < 3 ; i++ )
	{
		FD_ZERO ( & set_ptr[i] ) ;
	}

	if ( nbio_attr -> flush_flag )
	{
		/* write fds */
		slurm_FD_SET ( nbio_attr -> fd [IN_OUT_FD] , & set_ptr [WR_SET] ) ;
		slurm_FD_SET ( nbio_attr -> fd [SIG_ERR_FD] , & set_ptr [WR_SET] ) ;
		
		/* error fds */
		slurm_FD_SET ( nbio_attr -> fd [IN_OUT_FD] , & set_ptr [ER_SET] ) ;
		slurm_FD_SET ( nbio_attr -> fd [SIG_ERR_FD] , & set_ptr [ER_SET] ) ;
	}
	{
		/* read fds */
		slurm_FD_SET ( nbio_attr -> fd [IN_OUT_FD] , & set_ptr [RD_SET] ) ;
		slurm_FD_SET ( nbio_attr -> fd [CHILD_OUT_RD_FD] , & set_ptr [RD_SET] ) ;
		slurm_FD_SET ( nbio_attr -> fd [CHILD_ERR_RD_FD] , & set_ptr [RD_SET] ) ;

		/* error fds */
		for ( i=0 ; i < 5 ; i++ )
		{
			slurm_FD_SET ( nbio_attr -> fd [i] , & set_ptr[ER_SET] ) ;
		}

	}
	return SLURM_SUCCESS ;
}

int set_max_fd ( nbio_attr_t * nbio_attr )
{
	int i ;
	nbio_attr -> max_fd = 0 ;
	for ( i=0 ; i < 5 ; i++ )
	{
		nbio_attr -> max_fd = MAX ( nbio_attr -> max_fd , nbio_attr -> fd [ i ] ) ;
	}
	nbio_attr -> max_fd ++ ;
	return SLURM_SUCCESS ;
}

int nbio_cleanup ( nbio_attr_t * nbio_attr )
{
	free_circular_buffer ( nbio_attr -> in_cir_buf ) ;
	free_circular_buffer ( nbio_attr -> out_cir_buf ) ;
	free_circular_buffer ( nbio_attr -> err_cir_buf ) ;

	slurm_close_stream ( nbio_attr -> fd [IN_OUT_FD] ) ;
	slurm_close_stream ( nbio_attr -> fd [SIG_ERR_FD] ) ;
	close ( nbio_attr -> fd [CHILD_IN_WR_FD] ) ;
	close ( nbio_attr -> fd [CHILD_OUT_RD_FD] ) ;
	close ( nbio_attr -> fd [CHILD_ERR_RD_FD] ) ;

	return SLURM_SUCCESS ;
}

int reconnect (  nbio_attr_t * nbio_attr )
{
	if ( nbio_attr -> reconnect_flags[IN_OUT_FD] == RECONNECT )
	{
		if ( connect_io_stream ( nbio_attr -> task_start , STDIN_OUT_SOCK ) > 0 )
		{
			nbio_attr -> fd[IN_OUT_FD] = nbio_attr -> task_start -> sockets[STDIN_OUT_SOCK] ;
			slurm_set_stream_non_blocking ( nbio_attr -> fd [IN_OUT_FD] ) ;
			nbio_attr -> reconnect_flags[IN_OUT_FD] = CONNECTED ;
		}
	}
	if (  nbio_attr -> reconnect_flags[SIG_ERR_FD] == RECONNECT )
	{
		if ( connect_io_stream ( nbio_attr -> task_start , SIG_STDERR_SOCK ) > 0 )
		{
			nbio_attr -> fd[SIG_ERR_FD] = nbio_attr -> task_start -> sockets[SIG_STDERR_SOCK] ;
			slurm_set_stream_non_blocking ( nbio_attr -> fd [SIG_ERR_FD] ) ;
			nbio_attr -> reconnect_flags[SIG_ERR_FD] = CONNECTED ;
		}
	}
	return SLURM_SUCCESS ;
}

int test_error_conditions (  nbio_attr_t * nbio_attr )
{
	/* task has died and io is flushed */
	if ( nbio_attr -> out_cir_buf -> read_size == 0
			&& nbio_attr -> err_cir_buf -> read_size == 0
			&& nbio_attr -> flush_flag )
	{
		return SLURM_ERROR ;
	}

	if ( nbio_attr -> die ) 
	{
		return SLURM_ERROR ;
	}
/*
	if ( waitpid ( nbio_attr -> task_start -> exec_pid , NULL , WNOHANG ) > 0 )
	{
		return SLURM_ERROR ;
	}
	*/
	return SLURM_SUCCESS ;
}

int print_nbio_sets ( nbio_attr_t * nbio_attr , slurm_fd_set * set_ptr )
{
	int i ;
	printf ( "fds ");
	for ( i=0 ; i < 5 ; i ++ ) printf ( " %i " , nbio_attr -> fd[i] ) ;
	printf ( "\n");
	printf ( " %i %i %i %i %i %i \n", 
			nbio_attr -> in_cir_buf -> read_size ,
			nbio_attr -> in_cir_buf -> write_size ,
			nbio_attr -> out_cir_buf -> read_size ,
			nbio_attr -> out_cir_buf -> write_size ,
			nbio_attr -> err_cir_buf -> read_size ,
			nbio_attr -> err_cir_buf -> write_size
	       ) ;
	printf ( "--- 00000000001111111111222222222233\n") ;
	printf ( "--- 01234567890123456789012345678901\n") ;
	printf ( "rd  ");
	for ( i=0 ; i < 32 ; i ++ ) printf ( "%i" , slurm_FD_ISSET ( i , & set_ptr[RD_SET] ) ) ;
	printf ( "\n" ) ;
	printf ( "wr  ");
	for ( i=0 ; i < 32 ; i ++ ) printf ( "%i" , slurm_FD_ISSET ( i , & set_ptr[WR_SET] ) ) ;
	printf ( "\n" ) ;
	printf ( "er  ");
	for ( i=0 ; i < 32 ; i ++ ) printf ( "%i" , slurm_FD_ISSET ( i , & set_ptr[ER_SET] ) ) ;
	printf ( "\n" ) ;
	return SLURM_SUCCESS ;
}

