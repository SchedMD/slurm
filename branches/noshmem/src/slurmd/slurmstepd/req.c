/*****************************************************************************\
 *  src/slurmd/slurmstepd/req.c - slurmstepd domain socket request handling
 *  $Id: $
 *****************************************************************************
 *  Copyright (C) 2005 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Christopher Morrone <morrone2@llnl.gov>
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

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <unistd.h>

#include "src/common/xstring.h"
#include "src/common/xmalloc.h"
#include "src/common/fd.h"
#include "src/common/eio.h"

#include "src/slurmd/slurmd/slurmd.h"
#include "src/slurmd/common/stepd_api.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"
#include "src/slurmd/slurmstepd/req.h"

static void _handle_request(int fd, slurmd_job_t *job);
static int _handle_request_status(int fd);
static int _handle_request_attach(int fd, slurmd_job_t *job);
static bool _msg_socket_readable(eio_obj_t *obj);
static int _msg_socket_accept(eio_obj_t *obj, List objs);

struct io_operations msg_socket_ops = {
	readable:	&_msg_socket_readable,
	handle_read:	&_msg_socket_accept
};

char *socket_name;

/*
 * Create a named unix domain listening socket.
 * (cf, Stevens APUE 1st ed., section 15.5.2)
 */
static int
_create_socket(const char *name)
{
	int fd;
	int len;
	struct sockaddr_un addr;

	/* create a unix domain stream socket */
	if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
		return -1;
	fd_set_close_on_exec(fd);

	unlink(name);  /* in case it already exists */

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strcpy(addr.sun_path, name);
	len = strlen(addr.sun_path) + sizeof(addr.sun_family);

	/* bind the name to the descriptor */
	if (bind(fd, (struct sockaddr *) &addr, len) < 0)
		return -2;

	if (listen(fd, 5) < 0)
		return -3;

	return fd;
}

static int
_domain_socket_create(const char *dir, const char *nodename,
		     uint32_t jobid, uint32_t stepid)
{
	int fd;
	char *name = NULL;
	struct stat stat_buf;

	/*
	 * Make sure that "dir" exists and is a directory.
	 */
	if (stat(dir, &stat_buf) < 0)
		fatal("Domain socket directory %s: %m", dir);
	else if (!S_ISDIR(stat_buf.st_mode))
		fatal("%s is not a directory", dir);

	/*
	 * Now build the the name of socket, and create the socket.
	 */
	xstrfmtcat(name, "%s/%s_%u.%u", dir, nodename, jobid, stepid);
	fd = _create_socket(name);
	if (fd < 0)
		fatal("Could not create domain socket: %m");

	chmod(name, 0777);
	socket_name = name;
	return fd;
}

static void
_domain_socket_destroy(int fd)
{
	if (close(fd) < 0)
		error("Unable to close domain socket");

	unlink(socket_name);
}


static void *
_msg_thr_internal(void *job_arg)
{
	slurmd_job_t *job = (slurmd_job_t *) job_arg;

	debug("Message thread started pid = %lu", (unsigned long) getpid());
	eio_handle_mainloop(job->msg_handle);
	debug("Message thread exited");
}

void
msg_thr_create(slurmd_job_t *job)
{
	int fd;
	eio_obj_t *eio_obj;
	pthread_attr_t attr;

	fd = _domain_socket_create(conf->spooldir, "nodename",
				  job->jobid, job->stepid);
	fd_set_nonblocking(fd);

	eio_obj = eio_obj_create(fd, &msg_socket_ops, (void *)job);
	job->msg_handle = eio_handle_create();
	eio_new_initial_obj(job->msg_handle, eio_obj);

	slurm_attr_init(&attr);
	if (pthread_create(&job->msgid, &attr,
			   &_msg_thr_internal, (void *)job) != 0)
		fatal("pthread_create: %m");
}

static bool 
_msg_socket_readable(eio_obj_t *obj)
{
	debug3("Called _msg_socket_readable");
	if (obj->shutdown == true) {
		if (obj->fd != -1) {
			debug2("  false, shutdown");
			_domain_socket_destroy(obj->fd);
			obj->fd = -1;
		} else {
			debug2("  false");
		}
		return false;
	}
	return true;
}

static int
_msg_socket_accept(eio_obj_t *obj, List objs)
{
	slurmd_job_t *job = (slurmd_job_t *)obj->arg;
	int fd;
	struct sockaddr_un addr;
	struct stat statbuf;
	int len = sizeof(addr);

	debug3("Called _msg_socket_read");

	while ((fd = accept(obj->fd, (struct sockaddr *)&addr,
			    (socklen_t *)&len)) < 0) {
		if (errno == EINTR)
			continue;
		if (errno == EAGAIN
		    || errno == ECONNABORTED
		    || errno == EWOULDBLOCK) {
			return SLURM_SUCCESS;
		}
		error("Error on msg accept socket: %m");
		obj->shutdown = true;
		return SLURM_SUCCESS;
	}

	/* FIXME should really create a pthread to handle the message */

	fd_set_blocking(fd);
	_handle_request(fd, job);
}

static void
_handle_request(int fd, slurmd_job_t *job)
{
	int req;

	debug3("Entering _handle_message");

	if (read(fd, &req, sizeof(req)) != sizeof(req)) {
		error("Could not read request type: %m");
		goto fail;
	}

	switch (req) {
	case REQUEST_SIGNAL:
		debug("Handling REQUEST_SIGNAL");
		break;
	case REQUEST_TERMINATE:
		debug("Handling REQUEST_TERMINATE");
		break;
	case REQUEST_STATUS:
		debug("Handling REQUEST_STATUS");
		_handle_request_status(fd);
		break;
	case REQUEST_ATTACH:
		debug("Handling REQUEST_ATTACH");
		_handle_request_attach(fd, job);
		break;
	default:
		error("Unrecognized request: %d", req);
		break;
	}

fail:
	close(fd);
	debug3("Leaving  _handle_message");
}

static int
_handle_request_status(int fd)
{
	static int status = 1;

	write(fd, &status, sizeof(status));
	status++;
}

static int
_handle_request_attach(int fd, slurmd_job_t *job)
{
	srun_info_t *srun;
	int rc;

	debug("_handle_request_attach for job %u.%u", job->jobid, job->stepid);

	xassert(sizeof(*srun->key) <= SLURM_CRED_SIGLEN);
	srun       = xmalloc(sizeof(*srun));
	srun->key  = xmalloc(sizeof(*srun->key));

	read(fd, &srun->ioaddr, sizeof(slurm_addr));
	read(fd, &srun->resp_addr, sizeof(slurm_addr));
	read(fd, srun->key, SLURM_CRED_SIGLEN);

	list_prepend(job->sruns, (void *) srun);

	rc = io_client_connect(srun, job);
	if (rc == SLURM_ERROR)
		error("Failed attaching new stdio client");
}
