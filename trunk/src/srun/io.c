
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
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

static void _accept_io_stream(job_t *job);
static int  _handle_task_output(int *fd, FILE *out, int tasknum);
static void _bcast_stdin(int fd, job_t *job);	
static int  _readx(int fd, char *buf, size_t maxbytes);
static ssize_t _readn(int fd, void *buf, size_t nbytes);
static char * _next_line(char **str);

void *io_thr(void *job_arg)
{
	job_t *job = (job_t *) job_arg;
	fd_set rset, wset;
	int maxfd;
	int i, m;
	struct timeval tv;

	xassert(job != NULL);

	if (fcntl(job->iofd, F_SETFL, O_NONBLOCK) < 0)
		error("Unable to set nonblocking I/O on fd\n");

	for (i = 0; i < opt.nprocs; i++) {
		job->out[i] = -1; 
		job->err[i] = -1;
	}

	while (1) {

		FD_ZERO(&rset);
		FD_ZERO(&wset);

		maxfd = MAX(job->iofd, STDIN_FILENO);
		FD_SET(job->iofd, &rset);
		FD_SET(STDIN_FILENO, &rset);

		for (i = 0; i < opt.nprocs; i++) {
			maxfd = MAX(maxfd, job->out[i]);
			maxfd = MAX(maxfd, job->err[i]);
			if (job->out[i] > 0) 
				FD_SET(job->out[i], &rset);
			if (job->err[i] > 0) 
				FD_SET(job->err[i], &rset);
		}

		tv.tv_sec  = 0;
		tv.tv_usec = 500;
		while ((m = select(maxfd+1, &rset, NULL, NULL, NULL)) < 0) {
			if (errno != EINTR)
				fatal("Unable to handle I/O: %m", errno);
		}	

		if (FD_ISSET(job->iofd, &rset)) 
			_accept_io_stream(job);

		for (i = 0; i < opt.nprocs; i++) {
			if (job->err[i] > 0 && FD_ISSET(job->err[i], &rset)) 
				_handle_task_output(&job->err[i], stderr, i);
			if (job->out[i] > 0 && FD_ISSET(job->out[i], &rset)) 
				_handle_task_output(&job->out[i], stdout, i);
		}

		if (FD_ISSET(STDIN_FILENO, &rset))
			_bcast_stdin(STDIN_FILENO, job);
	}
	
	return (void *)(0);
}

static void
_accept_io_stream(job_t *job)
{
	int sd;
	struct sockaddr addr;
	struct sockaddr_in *sin;
	int size = sizeof(addr);
	char buf[INET_ADDRSTRLEN];
	slurm_io_stream_header_t hdr;
	uint32_t len = sizeof(hdr) - 4;
	char msgbuf[len];
	char *bufptr = msgbuf;


	while ((sd = accept(job->iofd, &addr, (socklen_t *) &size)) < 0) {
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
	debug("accepted %s connection from %s task %ld", 
			(hdr.type ? "stderr" : "stdout"), 
			buf, hdr.task_id               );

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

}

static int
_handle_task_output(int *fd, FILE *out, int tasknum)
{
	char buf[IO_BUFSIZ];
	char *line, *p;
	int len = 0;


	if ((len = _readx(*fd, buf, IO_BUFSIZ)) <= 0) {
		debug("%d: <%s disconnected>", tasknum, 
				out == stdout ? "stdout" : "stderr");
		fflush(stderr);
		shutdown(*fd, SHUT_RDWR);
		close(*fd);
		*fd = -1;
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

