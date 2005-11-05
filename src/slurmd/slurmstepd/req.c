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

static void *_handle_request(void *arg);
static void _handle_state(int fd, slurmd_job_t *job);
static void _handle_signal_process_group(int fd, slurmd_job_t *job);
static void _handle_signal_task_local(int fd, slurmd_job_t *job);
static void _handle_signal_container(int fd, slurmd_job_t *job);
static void _handle_attach(int fd, slurmd_job_t *job);
static void _handle_pid_in_container(int fd, slurmd_job_t *job);
static void _handle_daemon_pid(int fd, slurmd_job_t *job);
static bool _msg_socket_readable(eio_obj_t *obj);
static int _msg_socket_accept(eio_obj_t *obj, List objs);

struct io_operations msg_socket_ops = {
	readable:	&_msg_socket_readable,
	handle_read:	&_msg_socket_accept
};

char *socket_name;

struct request_params {
	int fd;
	slurmd_job_t *job;
};

/*
 *  Returns true if "uid" is a "slurm authorized user" - i.e. uid == 0
 *   or uid == slurm user id at this time.
 */
static bool
_slurm_authorized_user(uid_t uid)
{
	return ((uid == (uid_t) 0) || (uid == conf->slurm_user_id));
}

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
	int len = sizeof(addr);
	struct request_params *param = NULL;
	pthread_attr_t attr;
	pthread_t id;

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

	slurm_attr_init(&attr);
	if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED) != 0) {
		close(fd);
		error("Unable to set detachstate on attr: %m");
		return SLURM_ERROR;
	}

	param = xmalloc(sizeof(struct request_params));
	param->fd = fd;
	param->job = job;
	if (pthread_create(&id, &attr, &_handle_request, (void *)param) != 0) {
		error("stepd_api message engine pthread_create: %m");
		_handle_request((void *)param);
	}

	return SLURM_SUCCESS;
}

static void *
_handle_request(void *arg)
{
	struct request_params *param = (struct request_params *)arg;
	int req;

	debug3("Entering _handle_message");

	if (read(param->fd, &req, sizeof(req)) != sizeof(req)) {
		error("Could not read request type: %m");
		goto fail;
	}

	switch (req) {
	case REQUEST_SIGNAL_PROCESS_GROUP:
		debug("Handling REQUEST_SIGNAL_PROCESS_GROUP");
		_handle_signal_process_group(param->fd, param->job);
		break;
	case REQUEST_SIGNAL_TASK_LOCAL:
		debug("Handling REQUEST_SIGNAL_TASK_LOCAL");
		_handle_signal_task_local(param->fd, param->job);
		break;
	case REQUEST_SIGNAL_TASK_GLOBAL:
		debug("Handling REQUEST_SIGNAL_TASK_LOCAL (not implemented)");
		break;
	case REQUEST_SIGNAL_CONTAINER:
		debug("Handling REQUEST_SIGNAL_CONTAINER");
		_handle_signal_container(param->fd, param->job);
		break;
	case REQUEST_STATE:
		debug("Handling REQUEST_STATE");
		_handle_state(param->fd, param->job);
		break;
	case REQUEST_ATTACH:
		debug("Handling REQUEST_ATTACH");
		_handle_attach(param->fd, param->job);
		break;
	case REQUEST_PID_IN_CONTAINER:
		debug("Handling REQUEST_PID_IN_CONTAINER");
		_handle_pid_in_container(param->fd, param->job);
		break;
	case REQUEST_DAEMON_PID:
		debug("Handling REQUEST_DAEMON_PID");
		_handle_daemon_pid(param->fd, param->job);
		break;
	default:
		error("Unrecognized request: %d", req);
		break;
	}

fail:
	close(param->fd);
	xfree(arg);
	debug3("Leaving  _handle_message");
}

static void
_handle_state(int fd, slurmd_job_t *job)
{
	int status = 0;

	safe_write(fd, &job->state, sizeof(slurmstepd_state_t));
rwfail:
	return;
}

static void
_handle_signal_process_group(int fd, slurmd_job_t *job)
{
	int rc = SLURM_SUCCESS;
	int signal;
	int buf_len = 0;
	Buf buf;
	void *auth_cred;
	int uid;

	debug("_handle_signal_process_group for job %u.%u",
	      job->jobid, job->stepid);

	safe_read(fd, &signal, sizeof(int));
	safe_read(fd, &buf_len, sizeof(int));
	buf = init_buf(buf_len);
	safe_read(fd, get_buf_data(buf), buf_len);

	debug3("  buf_len = %d", buf_len);
	auth_cred = g_slurm_auth_unpack(buf);
	free_buf(buf); /* takes care of xfree'ing data as well */

	/*
	 * Authenticate the user using the auth credential.
	 */
	uid = g_slurm_auth_get_uid(auth_cred);
	debug3("  uid = %d", uid);
	if (uid != job->uid && !_slurm_authorized_user(uid)) {
		debug("kill req from uid %ld for job %u.%u owned by uid %ld",
		      (long)uid, job->jobid, job->stepid, (long)job->uid);
		rc = EPERM;
		goto done;
	}

	/*
	 * Signal the process group
	 */
	if (job->pgid <= (pid_t)1) {
		debug ("step %u.%u invalid [jmgr_pid:%d pgid:%u]", 
                       job->jobid, job->stepid, job->jmgr_pid, job->pgid);
		rc = ESLURMD_JOB_NOTRUNNING;
		goto done;
	}

	if (killpg(job->pgid, signal) == -1) {
		rc = errno;
		verbose("Error sending signal %d to %u.%u, pgid %d: %s", 
			signal, job->jobid, job->stepid, job->pgid,
			slurm_strerror(rc));
	} else {
		verbose("Sent signal %d to %u.%u, pgid %d", 
			signal, job->jobid, job->stepid, job->pgid);
	}
	

done:
	/* Send the return code */
	safe_write(fd, &rc, sizeof(int));
rwfail:
	return;
}

static void
_handle_signal_task_local(int fd, slurmd_job_t *job)
{
	int rc = SLURM_SUCCESS;
	int signal;
	int ltaskid; /* local task index */
	int buf_len = 0;
	Buf buf;
	void *auth_cred;
	int uid;

	debug("_handle_signal_task_local for job %u.%u",
	      job->jobid, job->stepid);

	safe_read(fd, &signal, sizeof(int));
	safe_read(fd, &ltaskid, sizeof(int));
	safe_read(fd, &buf_len, sizeof(int));
	buf = init_buf(buf_len);
	safe_read(fd, get_buf_data(buf), buf_len);

	debug3("  buf_len = %d", buf_len);
	auth_cred = g_slurm_auth_unpack(buf);
	free_buf(buf); /* takes care of xfree'ing data as well */

	/*
	 * Authenticate the user using the auth credential.
	 */
	uid = g_slurm_auth_get_uid(auth_cred);
	debug3("  uid = %d", uid);
	if (uid != job->uid && !_slurm_authorized_user(uid)) {
		debug("kill req from uid %ld for job %u.%u owned by uid %ld",
		      (long)uid, job->jobid, job->stepid, (long)job->uid);
		rc = EPERM;
		goto done;
	}

	/*
	 * Sanity checks
	 */
	if (ltaskid < 0 || ltaskid >= job->ntasks) {
		debug("step %u.%u invalid local task id %d", 
		      job->jobid, job->stepid, ltaskid);
		rc = SLURM_ERROR;
		goto done;
	}
	if (!job->task
	    || !job->task[ltaskid]) {
		debug("step %u.%u no task info for task id %d",
		      job->jobid, job->stepid, ltaskid);
		rc = SLURM_ERROR;
		goto done;
	}
	if (job->task[ltaskid]->pid <= 1) {
		debug("step %u.%u invalid pid %d for task %d",
		      job->jobid, job->stepid,
		      job->task[ltaskid]->pid, ltaskid);
		rc = SLURM_ERROR;
		goto done;
	}

	/*
	 * Signal the task
	 */
	if (kill(job->task[ltaskid]->pid, signal) == -1) {
		rc = errno;
		verbose("Error sending signal %d to %u.%u, pid %d: %s", 
			signal, job->jobid, job->stepid,
			job->task[ltaskid]->pid, slurm_strerror(rc));
	} else {
		verbose("Sent signal %d to %u.%u, pid %d", 
			signal, job->jobid, job->stepid,
			job->task[ltaskid]->pid);
	}
	

done:
	/* Send the return code */
	safe_write(fd, &rc, sizeof(int));
rwfail:
	return;
}

static void
_handle_signal_container(int fd, slurmd_job_t *job)
{
	int rc = SLURM_SUCCESS;
	int signal;
	int buf_len = 0;
	Buf buf;
	void *auth_cred;
	int uid;

	debug("_handle_signal_container for job %u.%u",
	      job->jobid, job->stepid);

	safe_read(fd, &signal, sizeof(int));
	safe_read(fd, &buf_len, sizeof(int));
	buf = init_buf(buf_len);
	safe_read(fd, get_buf_data(buf), buf_len);

	debug3("  buf_len = %d", buf_len);
	auth_cred = g_slurm_auth_unpack(buf);
	free_buf(buf); /* takes care of xfree'ing data as well */

	/*
	 * Authenticate the user using the auth credential.
	 */
	uid = g_slurm_auth_get_uid(auth_cred);
	debug3("  uid = %d", uid);
	if (uid != job->uid && !_slurm_authorized_user(uid)) {
		debug("kill container req from uid %ld for job %u.%u "
		      "owned by uid %ld",
		      (long)uid, job->jobid, job->stepid, (long)job->uid);
		rc = EPERM;
		goto done;
	}

	/*
	 * Signal the container
	 */
	if (job->cont_id == 0) {
		debug ("step %u.%u invalid container [cont_id:%u]", 
			job->jobid, job->stepid, job->cont_id);
		rc = ESLURMD_JOB_NOTRUNNING;
		goto done;
	}

	if (slurm_container_signal(job->cont_id, signal) < 0) {
		rc = errno;
		verbose("Error sending signal %d to %u.%u: %s", 
			signal, job->jobid, job->stepid, 
			slurm_strerror(rc));
	} else {
		verbose("Sent signal %d to %u.%u", 
			signal, job->jobid, job->stepid);
	}

done:
	/* Send the return code */
	safe_write(fd, &rc, sizeof(int));
rwfail:
	return;
}

static void
_handle_attach(int fd, slurmd_job_t *job)
{
	srun_info_t *srun;
	int rc = SLURM_SUCCESS;
	int buf_len = 0;
	Buf buf;
	void *auth_cred;
	slurm_cred_t job_cred;
	int sig_len;
	int uid, gid;

	debug("_handle_request_attach for job %u.%u", job->jobid, job->stepid);

	srun       = xmalloc(sizeof(*srun));
	srun->key  = xmalloc(sizeof(*srun->key));

	safe_read(fd, &srun->ioaddr, sizeof(slurm_addr));
	safe_read(fd, &srun->resp_addr, sizeof(slurm_addr));
	safe_read(fd, &buf_len, sizeof(int));
	buf = init_buf(buf_len);
	safe_read(fd, get_buf_data(buf), buf_len);

	debug3("buf_len = %d", buf_len);
	auth_cred = g_slurm_auth_unpack(buf);
	job_cred = slurm_cred_unpack(buf);
	free_buf(buf); /* takes care of xfree'ing data as well */

	/*
	 * Check if jobstep is actually running.
	 */
	if (job->state != SLURMSTEPD_STEP_RUNNING) {
		rc = ESLURMD_JOB_NOTRUNNING;
		goto done;
	}

	/*
	 * Authenticate the user using the auth credential.
	 */
	uid = g_slurm_auth_get_uid(auth_cred);
	gid = g_slurm_auth_get_gid(auth_cred);
	debug3("  uid = %d, gid = %d", uid, gid);
	if (uid != job->uid && gid != job->gid) {
		error("uid %ld attempt to attach to job %u.%u owned by %ld",
				(long) uid, job->jobid, job->stepid,
				(long) job->uid);
		rc = EPERM;
		goto done;
	}

	/*
	 * Get the signature of the job credential to send back to srun.
	 */
	slurm_cred_get_signature(job_cred, (void *)&srun->key, &sig_len);
	xassert(sig_len <= SLURM_CRED_SIGLEN);

	list_prepend(job->sruns, (void *) srun);

	rc = io_client_connect(srun, job);
done:
	/* Send the return code */
	safe_write(fd, &rc, sizeof(int));

	debug("In _handle_attach rc = %d", rc);
	if (rc == SLURM_SUCCESS) {
		/* Send response info */
		uint32_t *pids, *gtids;
		int len, i;

		debug("In _handle_attach sending response info");
		len = job->ntasks * sizeof(uint32_t);
		pids = xmalloc(len);
		gtids = xmalloc(gtids);
		
		if (job->task != NULL) {
			for (i = 0; i < job->ntasks; i++) {
				if (job->task == NULL)
					continue;
				pids[i] = (uint32_t)job->task[i]->pid;
				gtids[i] = job->task[i]->gtid;
			}
		}

		safe_write(fd, &job->ntasks, sizeof(uint32_t));
		safe_write(fd, pids, len);
		safe_write(fd, gtids, len);
		xfree(pids);
		xfree(gtids);

		len = strlen(job->argv[0]) + 1; /* +1 to include the \0 */
		safe_write(fd, &len, sizeof(int));
		safe_write(fd, job->argv[0], len);
	}

rwfail:
	return;
}

static void
_handle_pid_in_container(int fd, slurmd_job_t *job)
{
	bool rc = false;
	pid_t pid;

	debug("_handle_pid_in_container for job %u.%u",
	      job->jobid, job->stepid);

	safe_read(fd, &pid, sizeof(pid_t));
	
	/*
	 * FIXME - we should add a new call in the proctrack API
	 *         that simply returns "true" if a pid is in the step
	 */
	if (job->cont_id == slurm_container_find(pid))
		rc = true;

	/* Send the return code */
	safe_write(fd, &rc, sizeof(bool));

rwfail:
	debug("Leaving _handle_pid_in_container");
}

static void
_handle_daemon_pid(int fd, slurmd_job_t *job)
{
	safe_write(fd, &job->jmgr_pid, sizeof(pid_t));
rwfail:
	return;
}

