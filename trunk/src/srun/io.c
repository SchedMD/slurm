/****************************************************************************\
 *  io.c - process stdin, stdout, and stderr for parallel jobs.
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <grondona@llnl.gov>, et. al.
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
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/poll.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <signal.h>

#include "src/common/fd.h"
#include "src/common/hostlist.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/pack.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/slurm_protocol_pack.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xsignal.h"

#include "src/srun/io.h"
#include "src/srun/job.h"
#include "src/srun/net.h"
#include "src/srun/opt.h"

#define IO_BUFSIZ		2048
#define MAX_TERM_WAIT_SEC	  60	/* max time since first task 
					 * terminated, secs, warning msg */
#define POLL_TIMEOUT_MSEC	 500	/* max wait for i/o poll, msec */

static time_t time_first_done = 0;
static int    fmt_width       = 0;

/* fd_info struct used in poll() loop to map fds back to task number,
 * appropriate output type (stdout/err), and original fd
 */
typedef struct fd_info {
	int taskid;	/* corresponding task id		*/
	int *fd; 	/* pointer to fd in job->out/err array 	*/
	FILE *fp;	/* fp on which to write output		*/
	cbuf_t buf;
} fd_info_t;

static void	_accept_io_stream(job_t *job, int i);
static void	_bcast_stdin(int fd, job_t *job);	
static int	_close_stream(int *fd, FILE *out, int tasknum);
static void	_do_poll_timeout(job_t *job);
static int	_do_task_output(int *fd, FILE *out, cbuf_t buf, int tasknum);
static int 	_do_task_output_poll(fd_info_t *info);
static int      _do_task_input(job_t *job, int taskid);
static int 	_do_task_input_poll(job_t *job, fd_info_t *info);
static inline bool _io_thr_done(job_t *job);
static int	_handle_pollerr(fd_info_t *info);
static char *	_host_state_name(host_state_t state_inx);
static ssize_t	_readn(int fd, void *buf, size_t nbytes);
static ssize_t	_readx(int fd, char *buf, size_t maxbytes);
static char *	_task_state_name(task_state_t state_inx);
static int	_validate_header(slurm_io_stream_header_t *hdr, job_t *job);

#define _poll_set_rd(_pfd, _fd) do { 	\
	(_pfd).fd = _fd;		\
	(_pfd).events = POLLIN; 	\
        } while (0)

#define _poll_set_wr(_pfd, _fd) do { 	\
	(_pfd).fd = _fd;		\
	(_pfd).events = POLLOUT;	\
        } while (0)

#define _poll_rd_isset(pfd) ((pfd).revents & POLLIN )
#define _poll_wr_isset(pfd) ((pfd).revents & POLLOUT)
#define _poll_err(pfd)      ((pfd).revents & POLLERR)
#define _poll_hup(pfd)      ((pfd).revents & POLLHUP)

/* True if an EOF needs to be broadcast to all tasks
 */
static bool stdin_got_eof = false;
static bool stdin_open    = true;

static int 
_do_task_output_poll(fd_info_t *info)
{
      	return _do_task_output(info->fd, info->fp, info->buf, info->taskid);
}

static int
_do_task_input_poll(job_t *job, fd_info_t *info)
{
	return _do_task_input(job, info->taskid);
}

static int
_handle_pollerr(fd_info_t *info)
{
	int fd = *info->fd;
	int err;
	socklen_t size = sizeof(int);
	if (getsockopt(fd, SOL_SOCKET, SO_ERROR, (void *)&err, &size) < 0)
		error("_handle_error_poll: getsockopt: %m");

	if (err)
		debug3("%d: poll error on fd %d: %s", 
			info->taskid, fd, slurm_strerror(err));
	else
		debug3("%d: fd %d got hangup", info->taskid, fd);

	/* _do_task_output() should read EOF and close output
	 * stream if necessary. This way, any remaining data
	 * is read.
	 */
	_do_task_output(info->fd, info->fp, info->buf, info->taskid);

	return 0;
}

static void
_set_iofds_nonblocking(job_t *job)
{
	int i;
	for (i = 0; i < job->niofds; i++) 
		fd_set_nonblocking(job->iofd[i]);
	fd_set_nonblocking(job->stdinfd);
}

static void
_update_task_state(job_t *job, int taskid)
{	
	slurm_mutex_lock(&job->task_mutex);
	if (job->task_state[taskid] == SRUN_TASK_IO_WAIT)
		job->task_state[taskid] = SRUN_TASK_EXITED;
	slurm_mutex_unlock(&job->task_mutex);
}

static void
_do_output(cbuf_t buf, FILE *out, int tasknum)
{
	int len;
	char line[4096];

	while ((len = cbuf_read_line(buf, line, sizeof(line), 1))) {
		if (opt.labelio)
			fprintf(out, "%0*d: ", fmt_width, tasknum);
		fputs(line, out);
		fflush(out);
	}

}

static void
_flush_io(job_t *job)
{
	int i;

	debug3("flushing all io");
	for (i = 0; i < opt.nprocs; i++) {
		/* 
		 * Ensure remaining output is written
		 */
		if (cbuf_used(job->outbuf[i]))
			cbuf_write(job->outbuf[i], "\n", 1, NULL);
		if (cbuf_used(job->errbuf[i]))
			cbuf_write(job->errbuf[i], "\n", 1, NULL);

		_do_output(job->outbuf[i], job->outstream, i);
		if (job->out[i] != IO_DONE)
			_close_stream(&job->out[i], stdout, i);

		_do_output(job->errbuf[i], job->errstream, i);
		if (job->err[i] != IO_DONE)
			_close_stream(&job->err[i], stderr, i);
	}
}

static void *
_io_thr_poll(void *job_arg)
{
	job_t *job = (job_t *) job_arg;
	struct pollfd *fds;
	nfds_t nfds = 0;
	int numfds = (opt.nprocs*2) + job->niofds + 2;
	fd_info_t map[numfds];	/* map fd in pollfd array to fd info */
	int i, rc, out_fd_state, err_fd_state;

	xassert(job != NULL);

	debug3("IO thread pid = %ld", getpid());

	/* need ioport + msgport + stdin + 2*nprocs fds */
	fds = xmalloc(numfds*sizeof(*fds));

	_set_iofds_nonblocking(job);

	out_fd_state = WAITING_FOR_IO;
	err_fd_state = WAITING_FOR_IO;

	if (job->ofname->type == IO_ALL)
		out_fd_state = WAITING_FOR_IO;
	else {
		if (job->ifname->type != IO_ALL)
			out_fd_state = IO_DONE;
		else
			out_fd_state = WAITING_FOR_IO;

		if (!opt.efname)
			err_fd_state = IO_DONE;
	}

	if ((job->efname->type == IO_ALL) && (err_fd_state != IO_DONE)) {
		err_fd_state = WAITING_FOR_IO;
	} else 
		err_fd_state = IO_DONE;

	for (i = 0; i < opt.nprocs; i++) {
		job->out[i] = out_fd_state; 
		job->err[i] = err_fd_state;
	}

	for (i = 0; i < job->niofds; i++) 
		_poll_set_rd(fds[i], job->iofd[i]);

	while (!_io_thr_done(job)) {
		int eofcnt = 0;
		nfds = job->niofds; /* already have n ioport fds + stdin */

		if ((job->stdinfd >= 0) && stdin_open) {
			_poll_set_rd(fds[nfds], job->stdinfd);
			nfds++;
		}

		for (i = 0; i < opt.nprocs; i++) {
			if (job->out[i] >= 0) {
				_poll_set_rd(fds[nfds], job->out[i]);

				if ( (cbuf_used(job->inbuf[i]) > 0) 
				    || (stdin_got_eof && !job->stdin_eof[i]))
					fds[nfds].events |= POLLOUT;

				map[nfds].taskid = i;
				map[nfds].fd     = &job->out[i];
				map[nfds].fp     = job->outstream;
				map[nfds].buf    = job->outbuf[i];
				nfds++;
			}

			if (job->err[i] >= 0) {
				_poll_set_rd(fds[nfds], job->err[i]);
				map[nfds].taskid = i;
				map[nfds].fd     = &job->err[i];
				map[nfds].fp     = job->errstream;
				map[nfds].buf    = job->errbuf[i];
				nfds++;
			}

			if (   (job->out[i] == IO_DONE) 
			    && (job->err[i] == IO_DONE) ) {
				eofcnt++;
				_update_task_state(job, i);
			}
		}

		/* exit if we have received EOF on all streams */
		if (eofcnt) {
			if (eofcnt == opt.nprocs) {
				_flush_io(job);
				pthread_exit(0);
			} if (time_first_done == 0)
				time_first_done = time(NULL);
		}

		if (job->state == SRUN_JOB_FAILED) {
			debug3("job state is failed");
			_flush_io(job);
			pthread_exit(0);
		}

		while ((!_io_thr_done(job)) && 
		       ((rc = poll(fds, nfds, POLL_TIMEOUT_MSEC)) <= 0)) {
			if (rc == 0) {	/* timeout */
				_do_poll_timeout(job);
				continue;
			}
			switch(errno) {
				case EINTR:
				case EAGAIN:
					continue;
					break;
				case ENOMEM:
				case EFAULT:
					fatal("poll: %m");
					break;
				default:
					error("poll: %m. trying again.");
					break;
			}
		}

		for (i = 0; i < job->niofds; i++) {
			if (fds[i].revents) {
				if (_poll_err(fds[i]))
					error("poll error on io fd %d", i);
				else
					_accept_io_stream(job, i);
			}
		}

		if ((job->stdinfd >= 0) && stdin_open) {
		       	if (fds[i].revents)
				_bcast_stdin(job->stdinfd, job);
			++i;
		}


		for ( ; i < nfds; i++) {
			unsigned short revents = fds[i].revents;
			xassert(!(revents & POLLNVAL));
			if ((revents & POLLERR) || (revents & POLLHUP)) 
				_handle_pollerr(&map[i]);

			if ((revents & POLLIN) && (*map[i].fd >= 0)) 
				_do_task_output_poll(&map[i]);

			if ((revents & POLLOUT) && (*map[i].fd >= 0))
				_do_task_input_poll(job, &map[i]);
		}
	}
	xfree(fds);
	return NULL;
}

static void _do_poll_timeout (job_t *job)
{
	int i, age, eofcnt = 0;
	static bool term_msg_sent = false;


	for (i = 0; ((i < opt.nprocs) && (time_first_done == 0)); i++) {
		if ((job->task_state[i] == SRUN_TASK_FAILED) || 
		    (job->task_state[i] == SRUN_TASK_EXITED))
			time_first_done = time(NULL);
	}

	for (i = 0; i < opt.nprocs; i++) {
		if ((job->err[i] == IO_DONE) && (job->out[i] == IO_DONE))
			eofcnt++;
	}

	if (eofcnt == opt.nprocs) {
		_flush_io(job);
		pthread_exit(0);
	}

	age = time(NULL) - time_first_done;
	if (job->state == SRUN_JOB_FAILED) {
		debug3("job failed, exiting IO thread");
		pthread_exit(0);
	} else if (time_first_done && opt.max_wait && 
		 (age >= opt.max_wait)
		) {
		error("First task terminated %d seconds ago", age);
		error("Terminating remaining tasks now");
		report_task_status(job);
		update_job_state(job, SRUN_JOB_FAILED);
		pthread_exit(0);
	} else if (time_first_done && (term_msg_sent == false) && 
		   (age >= MAX_TERM_WAIT_SEC)) {
		info("Warning: First task terminated %d seconds ago", age);
		term_msg_sent = true;
	}
}

void report_job_status(job_t *job)
{
	int i;

	for (i = 0; i < job->nhosts; i++) {
		info ("host:%s state:%s", job->host[i], 
		      _host_state_name(job->host_state[i]));
	}
}

static char *_host_state_name(host_state_t state_inx)
{
	switch (state_inx) {
		case SRUN_HOST_INIT:
			return "initial";
		case SRUN_HOST_CONTACTED:
			return "contacted";
		case SRUN_HOST_UNREACHABLE:
			return "unreachable";
		case SRUN_HOST_REPLIED:
			return "replied";
		default:
			return "unknown";
	}
}

static inline bool 
_io_thr_done(job_t *job)
{
	bool retval;
	slurm_mutex_lock(&job->state_mutex);
	retval = (job->state >= SRUN_JOB_FORCETERM);
	slurm_mutex_unlock(&job->state_mutex);
	return retval;
}

void report_task_status(job_t *job)
{
	int i;
	char buf[1024];
	hostlist_t hl[5];

	for (i = 0; i < 5; i++)
		hl[i] = hostlist_create(NULL);

	for (i = 0; i < opt.nprocs; i++) {
		int state = job->task_state[i];
		if ((state == SRUN_TASK_EXITED) 
		    && ((job->err[i] >= 0) || (job->out[i] >= 0)))
			state = 4;
		snprintf(buf, 256, "task%d", i);
		hostlist_push(hl[state], buf); 
	}

	for (i = 0; i< 5; i++) {
		if (hostlist_count(hl[i]) > 0) {
			hostlist_ranged_string(hl[i], 1024, buf);
			info("%s: %s", buf, _task_state_name(i));
		}

		hostlist_destroy(hl[i]);
	}

}

static char *_task_state_name(task_state_t state_inx)
{
	switch (state_inx) {
		case SRUN_TASK_INIT:
			return "initializing";
		case SRUN_TASK_RUNNING:
			return "running";
		case SRUN_TASK_FAILED:
			return "failed";
		case SRUN_TASK_EXITED:
			return "exited";
		case SRUN_TASK_IO_WAIT:
			return "waiting for io";
		default:
			return "unknown";
	}
}

static int
_stdin_open(char *filename)
{
	int fd;
	int flags = O_RDONLY;
	
	xassert(filename != NULL);

	if ((fd = open(filename, flags, 0644)) < 0) {
		error ("Unable to open `%s' for stdin: %m", filename);
		return -1;
	}
	fd_set_nonblocking(fd);
	fd_set_close_on_exec(fd);
	return fd;
}

static FILE *
_fopen(char *filename)
{
	FILE *fp;

	xassert(filename != NULL);

	if (!(fp = fopen(filename, "w"))) 
		error ("Unable to open `%s' for writing: %m", filename);

	return fp;
}

int
open_streams(job_t *job)
{
	if ((job->ifname->type != IO_PER_TASK) && job->ifname->name) 
		job->stdinfd = _stdin_open(job->ifname->name);
	else
		job->stdinfd = STDIN_FILENO;

	if ((job->ofname->type != IO_PER_TASK) && job->ofname->name)
		job->outstream = _fopen(job->ofname->name);
	else
		job->outstream = stdout;

	if ((job->efname->type != IO_PER_TASK) && job->efname->name)
		job->errstream = _fopen(job->efname->name);
	else
		job->errstream = stderr;

	if (!job->outstream || !job->errstream || (job->stdinfd < 0))
		return -1;

	return 0;
}


void *
io_thr(void *arg)
{
	return _io_thr_poll(arg);
}

static int
_wid(int n)
{
	int width = 1;
	n--;	/* For zero origin */
	while (n /= 10)
		width++;
	return width;
}

int
io_thr_create(job_t *job)
{
	int i;
	pthread_attr_t attr;

	if (opt.labelio)
		fmt_width = _wid(opt.nprocs);

	for (i = 0; i < job->niofds; i++) {
		if (net_stream_listen(&job->iofd[i], &job->ioport[i]) < 0)
			fatal("unable to initialize stdio server port: %m");
		debug("initialized stdio server port %d\n",
		      ntohs(job->ioport[i]));
		net_set_low_water(job->iofd[i], 140);
	}

	if (open_streams(job) < 0) {
		return SLURM_ERROR;
	}

	xsignal(SIGTTIN, SIG_IGN);

	pthread_attr_init(&attr);
	if ((errno = pthread_create(&job->ioid, &attr, &io_thr, (void *) job)))
		return SLURM_ERROR;

	debug("Started IO server thread (%d)", job->ioid);

	return SLURM_SUCCESS;
}


static void
_accept_io_stream(job_t *job, int i)
{
	int len = size_io_stream_header();
	int j;
	int fd = job->iofd[i];
	debug2("Activity on IO server port %d fd %d", i, fd);

	for (j = 0; j < 15; i++) {
		int sd, size_read;
		struct sockaddr addr;
		struct sockaddr_in *sin;
		int size = sizeof(addr);
		char buf[INET_ADDRSTRLEN];
		slurm_io_stream_header_t hdr;
		Buf buffer;

		while ((sd = accept(fd, &addr, &size)) < 0) {
			if (errno == EINTR)
				continue;
			if (errno == EAGAIN)	/* No more connections */
				return;
			if ((errno == ECONNABORTED) || 
			    (errno == EWOULDBLOCK)) {
				return;
			}
			error("Unable to accept new connection: %m\n");
			return;
		}

		sin = (struct sockaddr_in *) &addr;
		inet_ntop(AF_INET, &sin->sin_addr, buf, INET_ADDRSTRLEN);

		buffer = init_buf(len);
		size_read = _readn(sd, buffer->head, len); 
		if (size_read != len) {
			/* A fatal error for this stream */
			error("Incomplete stream header read");
			free_buf(buffer);
			return;
		}

		if (unpack_io_stream_header(&hdr, buffer)) {
			/* A fatal error for this stream */
			error ("Bad stream header read");
			free_buf(buffer); /* NOTE: this frees msgbuf */
			return;
		}
		free_buf(buffer); /* NOTE: this frees msgbuf */
		if (_validate_header(&hdr, job))	/* check key */
			return;


		/* Assign new fds arbitrarily for now, until slurmd
		 * sends along some control information
		 */
		if ((hdr.task_id < 0) || (hdr.task_id >= opt.nprocs)) {
			error ("Invalid task_id %d from %s", hdr.task_id, buf);
			continue;
		}

		/*
		 * IO connection from task may come after task exits,
		 * in which case, state should be waiting for IO.
		 * 
		 * Update to RUNNING now handled in msg.c
		 *
		 * slurm_mutex_lock(&job->task_mutex);
		 * job->task_state[hdr.task_id] = SRUN_TASK_RUNNING;
		 * slurm_mutex_unlock(&job->task_mutex);
		 */

		fd_set_nonblocking(sd);

		if (hdr.type == SLURM_IO_STREAM_INOUT)
			job->out[hdr.task_id] = sd;
		else
			job->err[hdr.task_id] = sd;

		debug2("accepted %s connection from %s task %ld, sd=%d", 
				(hdr.type ? "stderr" : "stdout"), 
				buf, hdr.task_id, sd                   );
	}

}

static int
_close_stream(int *fd, FILE *out, int tasknum)
{	
	int retval;
	debug2("%d: <%s disconnected>", tasknum, 
			out == stdout ? "stdout" : "stderr");
	fflush(out);
	retval = shutdown(*fd, SHUT_RDWR);
	if ((retval >= 0) || (errno != EBADF)) 
		close(*fd);
	*fd = IO_DONE;
	return retval;
}

static int
_do_task_output(int *fd, FILE *out, cbuf_t buf, int tasknum)
{
	char line[IO_BUFSIZ];
	int len = 0;
	int dropped = 0;

	if ((len = cbuf_write_from_fd(buf, *fd, -1, &dropped)) <= 0) {
		if ((len != 0)) 
			error("Error task %d IO: %m", tasknum);
		_close_stream(fd, out, tasknum);
		return len;
	}

	while ((len = cbuf_read_line(buf, line, sizeof(line), 1))) {
		if (opt.labelio)
			fprintf(out, "%0*d: ", fmt_width, tasknum);
		fputs(line, out);
		fflush(out);
	}

	return len;
}

static int
_do_task_input(job_t *job, int taskid)
{
	int len = 0;
	cbuf_t buf = job->inbuf[taskid];
	int    fd  = job->out[taskid];

	if ( stdin_got_eof 
	    && !job->stdin_eof[taskid] 
	    && (cbuf_used(buf) == 0)   ) {
		job->stdin_eof[taskid] = true;
		shutdown(job->out[taskid], SHUT_WR); 
		return 0;
	}

	if ((len = cbuf_read_to_fd(buf, fd, -1)) < 0) 
		error ("writing stdin data: %m");

	debug3("wrote %d bytes to task %d stdin", len, taskid);

	return len;
}

static ssize_t 
_readx(int fd, char *buf, size_t maxbytes)
{
	size_t n;

	if ((n = read(fd, (void *) buf, maxbytes)) < 0) {
		if (errno == EINTR)
			return -1;
		if ((errno == EAGAIN) || 
		    (errno == EWOULDBLOCK))
			return -1;
		error("readx fd %d: %m", fd, n);
		return -1; /* shutdown socket, cleanup. */
	}
	return n;
}	


ssize_t
_readn(int fd, void *buf, size_t nbytes) 
{
	int n;
	char *pbuf = (char *)buf;
	size_t nleft = nbytes;

	while (nleft > 0) {
		n = read(fd, (void *)pbuf, nleft);
		if (n > 0) {
			pbuf+=n;
			nleft-=n;
		} else if (n == 0) 	/* EOF */
			break;
		else if (errno == EINTR)
			continue;
		else {
			error("read error: %m");
			break;
		}
	}

	return(n);
}

static void
_write_all(job_t *job, cbuf_t cb, char *buf, size_t len, int taskid)
{
	int n = 0;
	int dropped = 0;

    again:
	n = cbuf_write(cb, buf, len, &dropped);
	if ((n < len) && (job->out[taskid] >= 0)) {
		error("cbuf_write returned %d", n);
		_do_task_input(job, taskid);
		goto again;
	}

	if (dropped)
		error ("Dropped %d bytes stdin data", dropped);
}

static void
_close_stdin(job_t *j)
{
	close(j->stdinfd); 
	j->stdinfd = IO_DONE;
	stdin_got_eof = true;
	stdin_open    = false;
}

static void
_bcast_stdin(int fd, job_t *job)
{
	int          i;
	char         buf[4096];
	ssize_t      len = sizeof(buf);
	ssize_t      n   = 0;

	if (job->ifname->type == IO_ONE) {
		i = job->ifname->taskid;
		if (cbuf_free(job->inbuf[i]) < len)
			len = cbuf_free(job->inbuf[i]);
	} else {
		for (i = 0; i < opt.nprocs; i++) {
			if (cbuf_free(job->inbuf[i]) < len)
				len = cbuf_free(job->inbuf[i]);
		}
	}

	if (len == 0)
		return;

	if ((n = _readx(fd, buf, len)) < 0) {
		if (errno == EIO) {
			stdin_open = false;
			debug2("disabling stdin");
		} else if (errno != EINTR)
			error("error reading stdin. %m");
		return;
	}

	if (n == 0) {
		_close_stdin(job);
		return;
	} 

	if (job->ifname->type == IO_ONE) {
		i = job->ifname->taskid;
		_write_all(job, job->inbuf[i], buf, n, i);
	} else {
		for (i = 0; i < opt.nprocs; i++) 
			_write_all(job, job->inbuf[i], buf, n, i);
	}

	return;
}

static int _validate_header(slurm_io_stream_header_t *hdr, job_t *job)
{
	if (hdr->version != SLURM_PROTOCOL_VERSION) {
		error("Invalid header version, notify administrators");
		return SLURM_ERROR;
	}

	if ((hdr->task_id < 0) && (hdr->task_id >= opt.nprocs)) {
		error("Invalid header client task_id, notify administrators");
		return SLURM_ERROR;
	}

	if ((hdr->type != SLURM_IO_STREAM_INOUT) && 
	    (hdr->type != SLURM_IO_STREAM_SIGERR)) {
		error("Invalid header client type, notify administrators");
		return SLURM_ERROR;
	}

	if (memcmp((void *)job->cred->signature,
	           (void *)hdr->key, SLURM_SSL_SIGNATURE_LENGTH)) {
		error("Invalid header signature, notify administrators");
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

