/****************************************************************************\
 *  io.c - process stdin, stdout, and stderr for parallel jobs.
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
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <src/common/xassert.h>
#include <src/common/xmalloc.h>
#include <src/common/log.h>
#include <src/common/fd.h>
#include <src/common/macros.h>
#include <src/common/pack.h>
#include <src/common/slurm_protocol_defs.h>
#include <src/common/slurm_protocol_pack.h>

#include <src/srun/io.h>
#include <src/srun/job.h>
#include <src/srun/net.h>
#include <src/srun/opt.h>

#define IO_BUFSIZ	2048

/* fd_info struct used in poll() loop to map fds back to task number,
 * appropriate output type (stdout/err), and original fd
 */
typedef struct fd_info {
	int taskid;	/* corresponding task id		*/
	int *fd; 	/* pointer to fd in job->out/err array 	*/
	FILE *fp;	/* fp on which to write output		*/
} fd_info_t;

static void _accept_io_stream(job_t *job, int i);
static int  _do_task_output_poll(fd_info_t *info);
static int  _handle_pollerr(fd_info_t *info);
static int  _close_stream(int *fd, FILE *out, int tasknum);
static int  _do_task_output(int *fd, FILE *out, int tasknum);
static void _bcast_stdin(int fd, job_t *job);	
static ssize_t _readx(int fd, char *buf, size_t maxbytes);
static ssize_t _readn(int fd, void *buf, size_t nbytes);
static char * _next_line(char **str);
static int _validate_header(slurm_io_stream_header_t *hdr, job_t *job);

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

static int 
_do_task_output_poll(fd_info_t *info)
{
	return _do_task_output(info->fd, info->fp, info->taskid);
}

static int
_handle_pollerr(fd_info_t *info)
{
	int fd = *info->fd;
	int err;
	socklen_t size = sizeof(int);
	if (getsockopt(fd, SOL_SOCKET, SO_ERROR, (void *)&err, &size) < 0)
		error("_handle_error_poll: getsockopt: %m");
	else if (err)
		error("poll error on fd %d: %s", fd, slurm_strerror(err));

	if (*info->fd > 0)
		return _do_task_output_poll(info);
	else 
		return 0;
}

static void
_set_iofds_nonblocking(job_t *job)
{
	int i;
	for (i = 0; i < job->niofds; i++) 
		fd_set_nonblocking(job->iofd[i]);
}

static void *
_io_thr_poll(void *job_arg)
{
	job_t *job = (job_t *) job_arg;
	struct pollfd *fds;
	nfds_t nfds = 0;
	int numfds = (opt.nprocs*2) + job->niofds + 2;
	fd_info_t map[numfds];	/* map fd in pollfd array to fd info */
	int i;

	xassert(job != NULL);

	debug3("IO thread pid = %ld", getpid());

	/* need ioport + msgport + stdin + 2*nprocs fds */
	fds = xmalloc(numfds*sizeof(*fds));

	_set_iofds_nonblocking(job);

	for (i = 0; i < opt.nprocs; i++) {
		job->out[i] = WAITING_FOR_IO; 
		job->err[i] = WAITING_FOR_IO;
	}

	for (i = 0; i < job->niofds; i++) {
		_poll_set_rd(fds[i], job->iofd[i]);
	}
	_poll_set_rd(fds[i], STDIN_FILENO);

	while (1) {
		int eofcnt = 0;
		nfds = job->niofds+1; /* already have n ioport fds + stdin */

		for (i = 0; i < opt.nprocs; i++) {
			if (job->out[i] > 0) {
				_poll_set_rd(fds[nfds], job->out[i]);
				map[nfds].taskid = i;
				map[nfds].fd     = &job->out[i];
				map[nfds].fp     = stdout;
				nfds++;
			}

			if (job->err[i] > 0) {
				_poll_set_rd(fds[nfds], job->err[i]);
				map[nfds].taskid = i;
				map[nfds].fd     = &job->err[i];
				map[nfds].fp     = stderr;
				nfds++;
			}

			if ((job->out[i] == IO_DONE) 
			   && (job->err[i] == IO_DONE))
				eofcnt++;
		}

		debug3("eofcnt == %d", eofcnt);

		/* exit if we have received eof on all streams */
		if (eofcnt == opt.nprocs)
			pthread_exit(0);

		while (poll(fds, nfds, -1) < 0) {
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
				if (fds[i].revents & POLLERR)
					error("poll error on io fd %d", i);
				else
					_accept_io_stream(job, i);
			}
		}

		if (_poll_rd_isset(fds[i++]))
			_bcast_stdin(STDIN_FILENO, job);

		for (; i < nfds; i++) {
			unsigned short revents = fds[i].revents;
			xassert(!(revents & POLLNVAL));
			if (revents & POLLERR || revents & POLLHUP)
				_handle_pollerr(&map[i]);

			if (revents & POLLIN && *map[i].fd > 0) 
				_do_task_output_poll(&map[i]);
		}
	}
	xfree(fds);
}

void *
io_thr(void *arg)
{
	return _io_thr_poll(arg);
}

static void
_accept_io_stream(job_t *job, int i)
{
	int len = size_io_stream_header();
	int j;
	int fd = job->iofd[i];
	verbose("Activity on IO server port %d fd %d", i, fd);

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
		if (_validate_header(&hdr, job))
			return;


		/* Assign new fds arbitrarily for now, until slurmd
		 * sends along some control information
		 */

		/* Do I even need this? */
		fd_set_nonblocking(sd);

		if ((hdr.task_id < 0) || (hdr.task_id >= opt.nprocs)) {
			error ("Invalid task_id %d from %s",
			       hdr.task_id, buf);
			continue;
		}

		if (hdr.type == SLURM_IO_STREAM_INOUT)
			job->out[hdr.task_id] = sd;
		else
			job->err[hdr.task_id] = sd;

		/* FIXME: Need to check key */
		verbose("accepted %s connection from %s task %ld, sd=%d", 
				(hdr.type ? "stderr" : "stdout"), 
				buf, hdr.task_id, sd                   );
	}

}

static int
_close_stream(int *fd, FILE *out, int tasknum)
{	
	int retval;
	debug("%d: <%s disconnected>", tasknum, 
			out == stdout ? "stdout" : "stderr");
	fflush(out);
	retval = shutdown(*fd, SHUT_RDWR);
	if (retval >= 0 || errno != EBADF) 
		close(*fd);
	*fd = IO_DONE;
	return retval;
}

static int
_do_task_output(int *fd, FILE *out, int tasknum)
{
	char buf[IO_BUFSIZ+1];
	char *line, *p;
	int len = 0;

	if ((len = _readx(*fd, buf, IO_BUFSIZ)) <= 0) {
		_close_stream(fd, out, tasknum);
		return len;
	}
	buf[IO_BUFSIZ] = '\0';

	p = buf;
	while ((line = _next_line(&p)) != NULL) {
		if (opt.labelio)
			fprintf(out, "%d: ", tasknum);
		fprintf(out, "%s\n", line);
		fflush(out);
	}

	return len;
}

static ssize_t 
_readx(int fd, char *buf, size_t maxbytes)
{
	size_t n;

  again:
	if ((n = read(fd, (void *) buf, maxbytes)) < 0) {
		if (errno == EINTR)
			goto again;
		if ((errno == EAGAIN) || (errno == EWOULDBLOCK))
			return 0;
		error("readx fd %d: %m", fd, n);
		return -1; /* shutdown socket, cleanup. */
	}
	/* null terminate */
	buf[n] = '\0';

	return n;
}	


/* return a pointer to the beginning of the next line in *str and
 * nullify newline character. *str will be pointed just past 
 * nullified '\n'
 */
static char *
_next_line(char **str)
{
	char *p, *line;
	xassert(*str != NULL);

	if (**str == '\0')
		return NULL;

	line = *str;
	if ((p = strchr(*str, '\n')) != NULL) {
		*p = '\0';
		*str = p+1;
	} else {
		*str += strlen(*str) - 1;
		**str = '\0';
	}

	return line;
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
_bcast_stdin(int fd, job_t *job)
{
	int i, disc=0;
	char buf[IO_BUFSIZ];
	size_t len;

	if ((len = _readx(fd, buf, IO_BUFSIZ)) <= 0) {
		if (len == 0) /* got EOF */
			buf[len++] = 4;
		else {
			error("error reading stdin. %m");
			return;
		}
	}

	/* broadcast to every connected task */
	for (i = 0; i < opt.nprocs; i++) {
		if ((job->out[i] == WAITING_FOR_IO) || 
		    (job->out[i] == IO_DONE)) 
			disc++;
		else
			write(job->out[i], buf, len);
	}
	if (disc)
		error("Stdin could not be sent to %d disconnected tasks", 
		      disc);
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
