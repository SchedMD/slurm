/*****************************************************************************\
 *  reconnect_utils.c - 
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Kevin Tew <tew1@llnl.gov> et. al.
 *  UCRL-CODE-2002-040.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with ConMan; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

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

int connect_io_stream(task_start_t * task_start, int out_or_err)
{
	if ((task_start->sockets[out_or_err] =
	     slurm_open_stream(&(task_start->io_streams_dest))) ==
	    SLURM_PROTOCOL_ERROR) {
		error("connect_io_stream: <%s>: %m", 
				out_or_err ? "stdout" : "stderr");
		return SLURM_PROTOCOL_ERROR;
	} else 
		return send_io_stream_header(task_start, out_or_err);
}

int send_io_stream_header(task_start_t * task_start, int out_or_err)
{
	slurm_io_stream_header_t io_header;
	int buf_size = sizeof(slurm_io_stream_header_t);
	char buffer[buf_size];
	char *buf_ptr = buffer;
	int size = buf_size;

	if (out_or_err == STDIN_OUT_SOCK) {
		init_io_stream_header(&io_header,
				      task_start->launch_msg->credential->
				      signature,
				      task_start->launch_msg->
				      global_task_ids[task_start->local_task_id],
				      SLURM_IO_STREAM_INOUT);
		pack_io_stream_header(&io_header, (void **) &buf_ptr, &size);
		return slurm_write_stream(task_start->
					  sockets[STDIN_OUT_SOCK], buffer,
					  buf_size - size);
	} else {

		init_io_stream_header(&io_header,
				      task_start->launch_msg->credential->
				      signature,
				      task_start->launch_msg->
				      global_task_ids[task_start->
						      local_task_id],
				      SLURM_IO_STREAM_SIGERR);
		pack_io_stream_header(&io_header, (void **) &buf_ptr,
				      &size);
		return slurm_write_stream(task_start->
					  sockets[SIG_STDERR_SOCK], buffer,
					  buf_size - size);
	}
}

ssize_t read_EINTR(int fd, void *buf, size_t count)
{
	ssize_t bytes_read;
	while (true) {
		if ((bytes_read = read(fd, buf, count)) <= 0) {
			if ((bytes_read == SLURM_PROTOCOL_ERROR)
			    && (errno == EINTR)) {
				debug
				    ("read_EINTR: bytes_read: %i , fd: %i %m errno: %i",
				     bytes_read, fd, errno);
				continue;
			}
		}
		return bytes_read;
	}
}

ssize_t write_EINTR(int fd, void *buf, size_t count)
{
	ssize_t bytes_written;
	while (true) {
		if ((bytes_written = write(fd, buf, count)) <= 0) {
			if ((bytes_written == SLURM_PROTOCOL_ERROR)
			    && (errno == EINTR)) {
				debug
				    ("write_EINTR: bytes_written: %i , fd: %i %m errno: %i",
				     bytes_written, fd, errno);
				continue;
			}
		}
		return bytes_written;
	}
}

struct timeval timeval_diff(struct timeval *last, struct timeval *first)
{
	struct timeval temp;
	double lastd = last->tv_sec * 1000000 + last->tv_usec;
	double firstd = first->tv_sec * 1000000 + first->tv_usec;
	double diffd = lastd - firstd;
	temp.tv_sec = diffd / 1000000;
	temp.tv_usec = (long long) diffd % 1000000;
	return temp;
}

double timeval_diffd(struct timeval *last, struct timeval *first,
		     struct timeval *remaining)
{
	double lastd = last->tv_sec * 1000000 + last->tv_usec;
	double firstd = first->tv_sec * 1000000 + first->tv_usec;
	double diffd = lastd - firstd;
	remaining->tv_sec = diffd / 1000000;
	remaining->tv_usec = (long long) diffd % 1000000;
	return diffd;
}
