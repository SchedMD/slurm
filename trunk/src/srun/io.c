
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
#include <src/common/macros.h>
#include <src/common/slurm_protocol_defs.h>
#include <src/common/slurm_protocol_pack.h>

#include "opt.h"
#include "job.h"
#include "net.h"

#define IO_BUFSIZ	2048
#define IO_DONE		-9	/* signify that eof has been recvd on stream */

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
static int  _do_task_output(int *fd, FILE *out, int tasknum);
static void _bcast_stdin(int fd, job_t *job);	
static int  _readx(int fd, char *buf, size_t maxbytes);
static ssize_t _readn(int fd, void *buf, size_t nbytes);
static char * _next_line(char **str);

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

static void
_set_iofds_nonblocking(job_t *job)
{
	int i;
	for (i = 0; i < job->niofds; i++) {
		if (fcntl(job->iofd[i], F_SETFL, O_NONBLOCK) < 0)
			error("Unable to set nonblocking I/O on iofd %d", i);
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
	int i;

	xassert(job != NULL);

	debug3("IO thread pid = %ld", getpid());

	/* need ioport + msgport + stdin + 2*nprocs fds */
	fds = xmalloc(numfds*sizeof(*fds));

	_set_iofds_nonblocking(job);

	for (i = 0; i < opt.nprocs; i++) {
		job->out[i] = -1; 
		job->err[i] = -1;
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

			if (job->out[i] == IO_DONE && job->err[i] == IO_DONE)
				eofcnt++;
		}

		/* exit if we have recieved eof on all streams */
		if (eofcnt == opt.nprocs)
			pthread_exit(0);

		while (poll(fds, nfds, -1) < 0) {
			switch(errno) {
				case EINTR:
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
			if (fds[i].revents) 
				_accept_io_stream(job, i);
		}

		if (_poll_rd_isset(fds[i++]))
			_bcast_stdin(STDIN_FILENO, job);

		for (; i < nfds; i++) {
			unsigned short revents = fds[i].revents;
			if (revents & POLLERR)
				error("poll error on fd %d: %m", fds[i].fd);
			else if (revents & POLLIN) {
				_do_task_output_poll(&map[i]);
			} 
		}

	}
}

static void 
*_io_thr_select(void *job_arg)
{
	job_t *job = (job_t *) job_arg;
	fd_set rset, wset;
	int maxfd;
	int i, m;
	struct timeval tv;

	xassert(job != NULL);

	_set_iofds_nonblocking(job);

	for (i = 0; i < opt.nprocs; i++) {
		job->out[i] = -1; 
		job->err[i] = -1;
	}

	while (1) {
		unsigned long eofcnt = 0;

		FD_ZERO(&rset);
		FD_ZERO(&wset);

		FD_SET(STDIN_FILENO, &rset);
		maxfd = MAX(job->iofd[0], STDIN_FILENO);
		for (i = 0; i < job->niofds; i++) {
			FD_SET(job->iofd[i], &rset);
			maxfd = MAX(maxfd, job->iofd[i]);
		}

		for (i = 0; i < opt.nprocs; i++) {
			maxfd = MAX(maxfd, job->out[i]);
			maxfd = MAX(maxfd, job->err[i]);
			if (job->out[i] > 0) 
				FD_SET(job->out[i], &rset);
			if (job->err[i] > 0) 
				FD_SET(job->err[i], &rset);
			if (job->out[i] == IO_DONE && job->err[i] == IO_DONE)
				eofcnt++;
		}

		/* exit if we have recieved eof on all streams */
		if (eofcnt == opt.nprocs)
			pthread_exit(0);

		tv.tv_sec  = 0;
		tv.tv_usec = 500;
		while ((m = select(maxfd+1, &rset, NULL, NULL, &tv)) < 0) {
			if (errno != EINTR)
				fatal("Unable to handle I/O: %m", errno);
		}	

		for (i = 0; i < job->niofds; i++) {
			if (FD_ISSET(job->iofd[i], &rset)) 
				_accept_io_stream(job, i);
		}

		for (i = 0; i < opt.nprocs; i++) {
			if (job->err[i] > 0 && FD_ISSET(job->err[i], &rset)) 
				_do_task_output(&job->err[i], stderr, i);
			if (job->out[i] > 0 && FD_ISSET(job->out[i], &rset)) 
				_do_task_output(&job->out[i], stdout, i);
		}

		if (FD_ISSET(STDIN_FILENO, &rset))
			_bcast_stdin(STDIN_FILENO, job);
	}

	return (void *)(0);
}

void *
io_thr(void *arg)
{
	return _io_thr_poll(arg);
}

static void
_accept_io_stream(job_t *job, int i)
{
	verbose("Activity on IO server port %d", i);

	for (;;) {
		int sd;
		struct sockaddr addr;
		struct sockaddr_in *sin;
		int size = sizeof(addr);
		char buf[INET_ADDRSTRLEN];
		slurm_io_stream_header_t hdr;
		uint32_t len = sizeof(hdr) - 4;
		char msgbuf[len];
		char *bufptr = msgbuf;

		while ((sd = accept(job->iofd[i], &addr, (socklen_t *)&size)) < 0) {
			if (errno == EINTR)
				continue;
			if ((errno == EAGAIN) || (errno == EWOULDBLOCK))
				return;
			if (errno == ECONNABORTED)
				return;
			error("Unable to accept new connection: %m\n");
		}

		sin = (struct sockaddr_in *) &addr;
		inet_ntop(AF_INET, &sin->sin_addr, buf, INET_ADDRSTRLEN);

		_readn(sd, &msgbuf, len); 
		unpack_io_stream_header(&hdr, (void **) &bufptr, &len); 

		/* Assign new fds arbitrarily for now, until slurmd
		 * sends along some control information
		 */

		/* Do I even need this? */
		if (fcntl(sd, F_SETFL, O_NONBLOCK) < 0)
			error("Unable to set nonblocking I/O on new connection");

		if (hdr.type == SLURM_IO_STREAM_INOUT)
			job->out[hdr.task_id] = sd;
		else
			job->err[hdr.task_id] = sd;

		/* XXX: Need to check key */
		verbose("accepted %s connection from %s task %ld, sd=%d", 
				(hdr.type ? "stderr" : "stdout"), 
				buf, hdr.task_id, sd                   );
	}

}

static int
_do_task_output(int *fd, FILE *out, int tasknum)
{
	char buf[IO_BUFSIZ];
	char *line, *p;
	int len = 0;


	if ((len = _readx(*fd, buf, IO_BUFSIZ)) <= 0) {
		debug("%d: <%s disconnected>", tasknum, 
				out == stdout ? "stdout" : "stderr");
		fflush(out);
		shutdown(*fd, SHUT_RDWR);
		close(*fd);
		*fd = IO_DONE;
	}

	p = buf;
	while ((line = _next_line(&p)) != NULL) {
		if (opt.labelio)
			fprintf(out, "%d: ", tasknum);
		fprintf(out, "%s\n", line);
		fflush(out);
	}

	return len;
}

static int 
_readx(int fd, char *buf, size_t maxbytes)
{
	int n;

  again:
	if ((n = read(fd, (void *) buf, maxbytes)) < 0) {
		if (errno == EINTR)
			goto again;
		if ((errno == EAGAIN) || (errno == EWOULDBLOCK))
			return 0;
		error("Unable to read rc = %d: %m", n);
		return -1; /* shutdown socket, cleanup. */
	}
	/* null terminate */
	buf[n] = '\0';

	return n;
}	


/* return a pointer to the begging of the next line in *str and
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
	int pbuf = (int)buf;
	size_t nleft = nbytes;

	while(nleft > 0) {
		if ((n = read(fd, (void *)pbuf, nleft)) < 0 && errno != EINTR) {
			/* eof */
			return(0);
		}

		pbuf+=n;
		nleft-=n;
	}

	return(n);
}

static void
_bcast_stdin(int fd, job_t *job)
{
	int i;
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

	for (i = 0; i < opt.nprocs; i++)
		write(job->out[i], buf, len);
	return;
}

