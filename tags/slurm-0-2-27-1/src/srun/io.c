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
#include "src/common/slurm_cred.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xsignal.h"
#include "src/common/io_hdr.h"

#include "src/srun/io.h"
#include "src/srun/job.h"
#include "src/srun/net.h"
#include "src/srun/opt.h"

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
static int	_do_task_output(int *fd, FILE *out, cbuf_t buf, int tasknum);
static int 	_do_task_output_poll(fd_info_t *info);
static int      _do_task_input(job_t *job, int taskid);
static int 	_do_task_input_poll(job_t *job, fd_info_t *info);
static inline bool _io_thr_done(job_t *job);
static int	_handle_pollerr(fd_info_t *info);
static ssize_t	_readx(int fd, char *buf, size_t maxbytes);
static int      _read_io_header(int fd, job_t *job, char *host);

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
static uint32_t nbytes    = 0;
static uint32_t nwritten  = 0;

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
	/* 
	 * Do not do this. Setting stdin nonblocking has the side
	 * effect of setting stdout/stderr nonblocking, which is
	 * not what we want. We should have similar functionality
	 * with blocking stdin.
	 */
	/* fd_set_nonblocking(job->stdinfd); */
}

static void
_update_task_io_state(job_t *job, int taskid)
{	
	slurm_mutex_lock(&job->task_mutex);
	if (job->task_state[taskid] == SRUN_TASK_IO_WAIT)
		job->task_state[taskid] = SRUN_TASK_EXITED;
	slurm_mutex_unlock(&job->task_mutex);
}


static void
_do_output_line(cbuf_t buf, FILE *out, int tasknum)
{
	int  len     = 0;
	int  tot     = 0;
	char line[4096];

	while ((len = cbuf_read_line(buf, line, sizeof(line), 1))) {
		int n = 0;

		if (opt.labelio)
			fprintf(out, "%0*d: ", fmt_width, tasknum);

		if ((n = fprintf(out, "%s", line)) < 0) { 
			error("Need to rewind %d bytes: %m", len);
			goto done;
		} else
			tot += n;
	}

    done:
	fflush(NULL);

	debug3("do_output: [%d %d %d]", tot, cbuf_used(buf), cbuf_size(buf));

	nwritten += tot;
	return;
}

static void
_do_output(cbuf_t buf, FILE *out, int tasknum)
{
	if (opt.unbuffered)
		cbuf_read_to_fd(buf, fileno(out), -1);
	else
		_do_output_line(buf, out, tasknum);
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

	debug3("Read %dB from tasks, wrote %dB", nbytes, nwritten);
}

static void
_io_thr_init(job_t *job, struct pollfd *fds) 
{
	int out_fd_state = WAITING_FOR_IO;
	int err_fd_state = WAITING_FOR_IO;
	int i;

	xassert(job != NULL);

	/*
	 * XXX: Handle job->ofname/efname == IO_ONE
	 */

	_set_iofds_nonblocking(job);

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
}

static void
_fd_info_init(fd_info_t *info, int taskid, int *pfd, FILE *fp, cbuf_t buf)
{
	info->taskid = taskid;
	info->fd     = pfd;
	info->fp     = fp;
	info->buf    = buf;
}

static int
_stdin_buffer_space (job_t *job)
{
	int i;
	int len = 0;
	for (i = 0; i < opt.nprocs; i++) {
		int nfree = cbuf_free(job->inbuf[i]);
		if (((len == 0) && (nfree > 0)) || (nfree < len))
			len = nfree;
	}

	return (len);
}

static nfds_t
_setup_pollfds(job_t *job, struct pollfd *fds, fd_info_t *map)
{
	int eofcnt = 0;
	int i;
	nfds_t nfds = job->niofds; /* already have n ioport fds + stdin */

	if ((job->stdinfd >= 0) && stdin_open && _stdin_buffer_space(job) > 0) {
		_poll_set_rd(fds[nfds], job->stdinfd);
		nfds++;
	}

	for (i = 0; i < opt.nprocs; i++) {

		if (job->task_state[i] == SRUN_TASK_FAILED) {
			job->out[i] = IO_DONE;
			if (job->err[i] == WAITING_FOR_IO)
				job->err[i] = IO_DONE;
		}

		if (job->out[i] >= 0) {

			_poll_set_rd(fds[nfds], job->out[i]);

			if (  (cbuf_used(job->inbuf[i]) > 0) 
			   || (stdin_got_eof && !job->stdin_eof[i]))
				fds[nfds].events |= POLLOUT;

			_fd_info_init( map + nfds, i, &job->out[i], 
			               job->outstream, job->outbuf[i] );
			nfds++;
		}

		if (job->err[i] >= 0) {
			_poll_set_rd(fds[nfds], job->err[i]);

			_fd_info_init( map + nfds, i, &job->err[i], 
			               job->errstream, job->errbuf[i] );
			nfds++;
		}


		if (   (job->out[i] == IO_DONE) 
		    && (job->err[i] == IO_DONE) ) {
			eofcnt++;
			_update_task_io_state(job, i);
		}


	}

	/* exit if we have received EOF on all streams */
	if (eofcnt) {
		if (eofcnt == opt.nprocs) {
			debug("got EOF on all streams");
			_flush_io(job);
			pthread_exit(0);
		} 
	}

	return nfds;
}

static void *
_io_thr_poll(void *job_arg)
{
	int i, rc;
	job_t *job  = (job_t *) job_arg;
	int numfds  = (opt.nprocs*2) + job->niofds + 2;
	nfds_t nfds = 0;
	struct pollfd fds[numfds];
	fd_info_t     map[numfds];	/* map fd in pollfd array to fd info */

	xassert(job != NULL);

	debug3("IO thread pid = %lu", (unsigned long) getpid());

	_io_thr_init(job, fds);

	while (!_io_thr_done(job)) {

		nfds = _setup_pollfds(job, fds, map);

		while ((!_io_thr_done(job)) && 
		       ((rc = poll(fds, nfds, -1)) <= 0)) {

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

		if ((fds[i].fd == job->stdinfd) && (job->stdinfd >= 0) && stdin_open) {
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

	debug("IO thread exiting");

	return NULL;
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

	/*
	 * Turn off buffering of output stream, since we're doing it 
	 * with our own buffers. (Also, stdio buffering seems to
	 * causing some problems with loss of output)
	 */
	 /* setvbuf(job->outstream, NULL, _IONBF, 0); */

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

	debug("Started IO server thread (%lu)", (unsigned long) job->ioid);

	return SLURM_SUCCESS;
}

static bool 
_is_fd_ready(int fd)
{
	struct pollfd pfd[1];
	int    rc;

	pfd[0].fd     = fd;
	pfd[0].events = POLLIN;

	rc = poll(pfd, 1, 10);

	return ((rc == 1) && (pfd[0].revents & POLLIN));
}


static int
_read_io_header(int fd, job_t *job, char *host)
{
	int      size = io_hdr_packed_size();
	cbuf_t   cb   = cbuf_create(size, size);
	char    *key  = NULL;
	int      len  = 0;
	io_hdr_t hdr;

	if (cbuf_write_from_fd(cb, fd, size, NULL) < 0) {
		error ("Bad stream header read: %m");
		goto fail;
	}

	if (io_hdr_read_cb(cb, &hdr) < 0) {
		error ("Unable to unpack io header: %m");
		goto fail;
	}

	if (slurm_cred_get_signature(job->cred, &key, &len) < 0) {
		error ("Couldn't get existing cred signature");
		goto fail;
	}

	if (io_hdr_validate(&hdr, key, len) < 0)  /* check key */
		goto fail;

	/* 
	 * validate reality of hdr.taskid
	 */
	if ((hdr.taskid < 0) || (hdr.taskid >= opt.nprocs)) {
		error ("Invalid taskid %d from %s", hdr.taskid, host);
		goto fail;
	}

	if (hdr.type == SLURM_IO_STDOUT)
		job->out[hdr.taskid] = fd;
	else
		job->err[hdr.taskid] = fd;

	debug2("accepted %s connection from %s task %u, sd=%d", 
	       (hdr.type == SLURM_IO_STDERR ? "stderr" : "stdout"), 
		host, hdr.taskid, fd                               );

	cbuf_destroy(cb);
	return SLURM_SUCCESS;

    fail:
	cbuf_destroy(cb);
	close(fd);
	return SLURM_ERROR;
}


static void
_accept_io_stream(job_t *job, int i)
{
	int j;
	int fd = job->iofd[i];
	debug2("Activity on IO server port %d fd %d", i, fd);

	for (j = 0; j < 15; i++) {
		int sd;
		struct sockaddr addr;
		struct sockaddr_in *sin;
		int size = sizeof(addr);
		char buf[INET_ADDRSTRLEN];

		/* 
		 * Return early if fd is not now ready
		 * This ensures that we never block when trying 
		 * to read the io header below.
		 *
		 * (XXX: This should eventually be fixed by making
		 *  reads of IO headers nonblocking)
		 */
		if (!_is_fd_ready(fd))
			return;

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

		debug3("Accepted IO connection: ip=%s sd=%d", buf, sd); 

		/*
		 * Read IO header and update job structure appropriately
		 */
		if (_read_io_header(sd, job, buf) < 0)
			continue;

		fd_set_nonblocking(sd);
	}

}

static int
_close_stream(int *fd, FILE *out, int tasknum)
{	
	int retval;
	debug2("%d: <%s disconnected>", tasknum, 
			out == stdout ? "stdout" : "stderr");
	retval = shutdown(*fd, SHUT_RDWR);
	if ((retval >= 0) || (errno != EBADF)) 
		close(*fd);
	*fd = IO_DONE;
	return retval;
}

static int
_do_task_output(int *fd, FILE *out, cbuf_t buf, int tasknum)
{
	int len = 0;
	int dropped = 0;

    again:
	if ((len = cbuf_write_from_fd(buf, *fd, -1, &dropped)) < 0) {

		/*
		 *  If output buffer is full, flush all output to
		 *   output stream
		 */
		if (errno == ENOSPC) {
			cbuf_read_to_fd(buf, fileno(out), -1);
			goto again;
		} 
		
		if (errno == EAGAIN)
			return 0;

		error("Error task %d IO: %m", tasknum);
		_close_stream(fd, out, tasknum);
		return len;

	} else if (len == 0) {
		_close_stream(fd, out, tasknum);
		return len;
	}

	nbytes += len;

	_do_output(buf, out, tasknum);

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

	verbose ("going to bcast %d bytes stdin", len);

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

