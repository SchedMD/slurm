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

ssize_t write_EINTR(int fd, void *buf, size_t count)
{
	ssize_t bytes_written ;
	while ( true )
	{
		if ( ( bytes_written = write ( fd, buf, count ) ) <= 0 )
		{
			debug ( "bytes_written: %i , %m errno: %i", bytes_written , errno ) ;
			if ( ( bytes_written == SLURM_PROTOCOL_ERROR ) && ( errno == EINTR ) )
			{
				continue ;
			}
		}
		return bytes_written ;
	}
}

