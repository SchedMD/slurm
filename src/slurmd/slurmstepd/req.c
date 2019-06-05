/*****************************************************************************\
 *  src/slurmd/slurmstepd/req.c - slurmstepd domain socket request handling
 *****************************************************************************
 *  Copyright (C) 2005-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Christopher Morrone <morrone2@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  In addition, as a special exception, the copyright holders give permission
 *  to link the code of portions of this program with the OpenSSL library under
 *  certain conditions as described in each individual source file, and
 *  distribute linked combinations including the two. You must obey the GNU
 *  General Public License in all respects for all of the code used other than
 *  OpenSSL. If you modify file(s) with this exception, you may extend this
 *  exception to your version of the file(s), but you are not obligated to do
 *  so. If you do not wish to do so, delete this exception statement from your
 *  version.  If you delete this exception statement from all source files in
 *  the program, then also delete it here.
 *
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#define _GNU_SOURCE	/* needed for struct ucred definition */

#include <signal.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include "src/common/checkpoint.h"
#include "src/common/cpu_frequency.h"
#include "src/common/fd.h"
#include "src/common/eio.h"
#include "src/common/macros.h"
#include "src/common/parse_time.h"
#include "src/common/slurm_auth.h"
#include "src/common/slurm_jobacct_gather.h"
#include "src/common/slurm_acct_gather.h"
#include "src/common/stepd_api.h"
#include "src/common/strlcpy.h"
#include "src/common/switch.h"
#include "src/common/timers.h"
#include "src/common/tres_frequency.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmd/common/core_spec_plugin.h"
#include "src/slurmd/common/proctrack.h"
#include "src/slurmd/slurmd/slurmd.h"
#include "src/slurmd/slurmstepd/io.h"
#include "src/slurmd/slurmstepd/mgr.h"
#include "src/slurmd/slurmstepd/pdebug.h"
#include "src/slurmd/slurmstepd/req.h"
#include "src/slurmd/slurmstepd/slurmstepd.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"
#include "src/slurmd/slurmstepd/step_terminate_monitor.h"

#include "src/slurmd/common/task_plugin.h"

static void *_handle_accept(void *arg);
static int _handle_request(int fd, stepd_step_rec_t *job,
			   uid_t uid, pid_t remote_pid);
static int _handle_state(int fd, stepd_step_rec_t *job);
static int _handle_info(int fd, stepd_step_rec_t *job);
static int _handle_mem_limits(int fd, stepd_step_rec_t *job);
static int _handle_uid(int fd, stepd_step_rec_t *job);
static int _handle_nodeid(int fd, stepd_step_rec_t *job);
static int _handle_signal_container(int fd, stepd_step_rec_t *job, uid_t uid);
static int _handle_checkpoint_tasks(int fd, stepd_step_rec_t *job, uid_t uid);
static int _handle_attach(int fd, stepd_step_rec_t *job, uid_t uid);
static int _handle_pid_in_container(int fd, stepd_step_rec_t *job);
static void *_wait_extern_pid(void *args);
static int _handle_add_extern_pid_internal(stepd_step_rec_t *job, pid_t pid);
static int _handle_add_extern_pid(int fd, stepd_step_rec_t *job);
static int _handle_x11_display(int fd, stepd_step_rec_t *job);
static int _handle_getpw(int fd, stepd_step_rec_t *job, pid_t remote_pid);
static int _handle_getgr(int fd, stepd_step_rec_t *job, pid_t remote_pid);
static int _handle_daemon_pid(int fd, stepd_step_rec_t *job);
static int _handle_notify_job(int fd, stepd_step_rec_t *job, uid_t uid);
static int _handle_suspend(int fd, stepd_step_rec_t *job, uid_t uid);
static int _handle_resume(int fd, stepd_step_rec_t *job, uid_t uid);
static int _handle_terminate(int fd, stepd_step_rec_t *job, uid_t uid);
static int _handle_completion(int fd, stepd_step_rec_t *job, uid_t uid);
static int _handle_stat_jobacct(int fd, stepd_step_rec_t *job, uid_t uid);
static int _handle_task_info(int fd, stepd_step_rec_t *job);
static int _handle_list_pids(int fd, stepd_step_rec_t *job);
static int _handle_reconfig(int fd, stepd_step_rec_t *job, uid_t uid);
static bool _msg_socket_readable(eio_obj_t *obj);
static int _msg_socket_accept(eio_obj_t *obj, List objs);

struct io_operations msg_socket_ops = {
	.readable = &_msg_socket_readable,
	.handle_read = &_msg_socket_accept
};

static char *socket_name;
static pthread_mutex_t suspend_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool suspended = false;

struct request_params {
	int fd;
	stepd_step_rec_t *job;
};

typedef struct {
	stepd_step_rec_t *job;
	pid_t pid;
} extern_pid_t;


static pthread_mutex_t message_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t message_cond = PTHREAD_COND_INITIALIZER;
static int message_connections = 0;
static int msg_target_node_id = 0;

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

	/*
	 * If socket name would be truncated, emit error and exit
	 */
	if (strlen(name) > sizeof(addr.sun_path) - 1) {
		error("%s: Unix socket path '%s' is too long. (%ld > %ld)",
		      __func__, name, (long int)(strlen(name) + 1),
		      (long int)sizeof(addr.sun_path));
		errno = ESLURMD_INVALID_SOCKET_NAME_LEN;
		return -1;
	}

	/* create a unix domain stream socket */
	if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
		return -1;
	fd_set_close_on_exec(fd);

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strlcpy(addr.sun_path, name, sizeof(addr.sun_path));
	len = strlen(addr.sun_path)+1 + sizeof(addr.sun_family);

	/* bind the name to the descriptor */
	if (bind(fd, (struct sockaddr *) &addr, len) < 0) {
		(void) close(fd);
		return -2;
	}

	if (listen(fd, 32) < 0) {
		(void) close(fd);
		return -3;
	}

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
	if (stat(dir, &stat_buf) < 0) {
		error("Domain socket directory %s: %m", dir);
		return -1;
	} else if (!S_ISDIR(stat_buf.st_mode)) {
		error("%s is not a directory", dir);
		return -1;
	}

	/*
	 * Now build the name of socket, and create the socket.
	 */
	xstrfmtcat(name, "%s/%s_%u.%u", dir, nodename, jobid, stepid);

	/*
	 * First check to see if the named socket already exists.
	 */
	if (stat(name, &stat_buf) == 0) {
		/* Vestigial from a slurmd crash or job requeue that did not
		 * happen properly (very rare conditions). Unlink the file
		 * and recreate it.
		 */
		if (unlink(name) != 0) {
			error("%s: failed unlink(%s) job %u step %u %m",
			      __func__, name, jobid, stepid);
			xfree(name);
			errno = ESLURMD_STEP_EXISTS;
			return -1;
		}
	}

	fd = _create_socket(name);
	if (fd < 0)
		fatal("Could not create domain socket: %m");

	if (chmod(name, 0777) == -1)
		error("%s: chmod(%s): %m", __func__, name);
	socket_name = name;
	return fd;
}

static void
_domain_socket_destroy(int fd)
{
	if (close(fd) < 0)
		error("Unable to close domain socket: %m");

	if (unlink(socket_name) == -1)
		error("Unable to unlink domain socket `%s`: %m", socket_name);
}

/* Wait for the job to be running (pids added) before continuing. */
static int _wait_for_job_running(stepd_step_rec_t *job)
{
	struct timespec ts = {0, 0};
	int count = 0;
	int rc = SLURM_SUCCESS;

	slurm_mutex_lock(&job->state_mutex);

	/*
	 * SLURMSTEPD_STEP_RUNNING is 2 so we need loop at least that
	 * many times, but we don't want to loop any more than that.
	 */
	while ((job->state < SLURMSTEPD_STEP_RUNNING) && (count < 2)) {
		ts.tv_sec  = time(NULL) + 60;
		slurm_cond_timedwait(&job->state_cond, &job->state_mutex, &ts);
		count++;
	}

	if (job->state < SLURMSTEPD_STEP_RUNNING) {
		debug("step %u.%u not running yet %d [cont_id:%"PRIu64"]",
		      job->jobid, job->stepid, job->state, job->cont_id);
		rc = ESLURMD_JOB_NOTRUNNING;
	}

	slurm_mutex_unlock(&job->state_mutex);
	return rc;
}

static void *
_msg_thr_internal(void *job_arg)
{
	stepd_step_rec_t *job = (stepd_step_rec_t *) job_arg;

	debug("Message thread started pid = %lu", (unsigned long) getpid());
	eio_handle_mainloop(job->msg_handle);
	debug("Message thread exited");

	return NULL;
}

int
msg_thr_create(stepd_step_rec_t *job)
{
	int fd;
	eio_obj_t *eio_obj;
	errno = 0;
	fd = _domain_socket_create(conf->spooldir, conf->node_name,
				   job->jobid, job->stepid);
	if (fd == -1)
		return SLURM_ERROR;

	fd_set_nonblocking(fd);

	eio_obj = eio_obj_create(fd, &msg_socket_ops, (void *)job);
	job->msg_handle = eio_handle_create(0);
	eio_new_initial_obj(job->msg_handle, eio_obj);

	slurm_thread_create(&job->msgid, _msg_thr_internal, job);

	return SLURM_SUCCESS;
}

/*
 * Bounded wait for the connection count to drop to zero.
 * This gives connection threads a chance to complete any pending
 * RPCs before the slurmstepd exits.
 */
static void _wait_for_connections(void)
{
	struct timespec ts = {0, 0};
	int rc = 0;

	slurm_mutex_lock(&message_lock);
	ts.tv_sec = time(NULL) + STEPD_MESSAGE_COMP_WAIT;
	while (message_connections > 0 && rc == 0)
		rc = pthread_cond_timedwait(&message_cond, &message_lock, &ts);

	slurm_mutex_unlock(&message_lock);
}

static void _decrement_message_connections(void)
{
	slurm_mutex_lock(&message_lock);
	message_connections--;
	slurm_cond_signal(&message_cond);
	slurm_mutex_unlock(&message_lock);
}

static bool
_msg_socket_readable(eio_obj_t *obj)
{
	debug3("Called _msg_socket_readable");
	if (obj->shutdown == true) {
		/* All spawned tasks have been completed by this point */
		if (obj->fd != -1) {
			debug2("  false, shutdown");
			_domain_socket_destroy(obj->fd);
			/* slurmd considers the job step done now that
			 * the domain name socket is destroyed */
			obj->fd = -1;
			_wait_for_connections();
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
	stepd_step_rec_t *job = (stepd_step_rec_t *)obj->arg;
	int fd;
	struct sockaddr_un addr;
	int len = sizeof(addr);
	struct request_params *param = NULL;

	debug3("Called _msg_socket_accept");

	while ((fd = accept(obj->fd, (struct sockaddr *)&addr,
			    (socklen_t *)&len)) < 0) {
		if (errno == EINTR)
			continue;
		if ((errno == EAGAIN) ||
		    (errno == ECONNABORTED) ||
		    (errno == EWOULDBLOCK)) {
			return SLURM_SUCCESS;
		}
		error("Error on msg accept socket: %m");
		if ((errno == EMFILE)  ||
		    (errno == ENFILE)  ||
		    (errno == ENOBUFS) ||
		    (errno == ENOMEM)) {
			return SLURM_SUCCESS;
		}
		obj->shutdown = true;
		return SLURM_SUCCESS;
	}

	slurm_mutex_lock(&message_lock);
	message_connections++;
	slurm_mutex_unlock(&message_lock);

	fd_set_close_on_exec(fd);
	fd_set_blocking(fd);

	param = xmalloc(sizeof(struct request_params));
	param->fd = fd;
	param->job = job;
	slurm_thread_create_detached(NULL, _handle_accept, param);

	debug3("Leaving _msg_socket_accept");
	return SLURM_SUCCESS;
}

static void *_handle_accept(void *arg)
{
	/*struct request_params *param = (struct request_params *)arg;*/
	int fd = ((struct request_params *)arg)->fd;
	stepd_step_rec_t *job = ((struct request_params *)arg)->job;
	int req;
	int client_protocol_ver;
	Buf buffer = NULL;
	int rc;
	uid_t uid;
	pid_t remote_pid = NO_VAL;

	debug3("%s: entering (new thread)", __func__);
	xfree(arg);

	/*
	 * Prior to 19.05, REQUEST_CONNECT was the first thing written on
	 * the socket, followed by an auth_cred. In 19.05, this changes over
	 * to SLURM_PROTOCOL_VERSION from the client and the (unneeded)
	 * credential is no longer received. The REQUEST_CONNECT block should
	 * be removed two versions after 19.05.
	 */
	safe_read(fd, &req, sizeof(int));
	if (req == REQUEST_CONNECT) {
		int len;
		void *auth_cred;
		char *auth_info = slurm_get_auth_info();

		/* assume oldest we can talk to */
		client_protocol_ver = SLURM_MIN_PROTOCOL_VERSION;

		safe_read(fd, &len, sizeof(int));
		buffer = init_buf(len);
		safe_read(fd, get_buf_data(buffer), len);

		/* Unpack and verify the auth credential */
		auth_cred = g_slurm_auth_unpack(buffer, SLURM_PROTOCOL_VERSION);
		if (auth_cred == NULL) {
			error("Unpacking authentication credential: %m");
			FREE_NULL_BUFFER(buffer);
			goto fail;
		}
		rc = g_slurm_auth_verify(auth_cred, auth_info);
		if (rc != SLURM_SUCCESS) {
			error("Verifying authentication credential: %m");
			xfree(auth_info);
			(void) g_slurm_auth_destroy(auth_cred);
			FREE_NULL_BUFFER(buffer);
			goto fail;
		}
		xfree(auth_info);

		/* Get the uid from the credential, then destroy it. */
		uid = g_slurm_auth_get_uid(auth_cred);
		g_slurm_auth_destroy(auth_cred);
		FREE_NULL_BUFFER(buffer);
	} else if (req >= SLURM_MIN_PROTOCOL_VERSION) {
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__)
		gid_t tmp_gid;

		rc = getpeereid(fd, &uid, &tmp_gid);
#else
		struct ucred ucred;
		socklen_t len = sizeof(ucred);

		rc = getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &ucred, &len);
		uid = ucred.uid;
		remote_pid = ucred.pid;
#endif
		if (rc)
			goto fail;
		client_protocol_ver = req;
	} else {
		error("%s: Invalid Protocol Version %d", __func__, req);
		goto fail;
	}

	debug3("%s: Protocol Version %d from uid=%u",
	       __func__, client_protocol_ver, uid);

	rc = SLURM_PROTOCOL_VERSION;
	safe_write(fd, &rc, sizeof(int));

	while (1) {
		rc = _handle_request(fd, job, uid, remote_pid);
		if (rc != SLURM_SUCCESS)
			break;
	}

	if (close(fd) == -1)
		error("Closing accepted fd: %m");

	debug3("Leaving %s", __func__);
	_decrement_message_connections();
	return NULL;

fail:
	rc = SLURM_ERROR;
	safe_write(fd, &rc, sizeof(int));
rwfail:
	if (close(fd) == -1)
		error("Closing accepted fd after error: %m");
	debug("Leaving %s on an error", __func__);
	FREE_NULL_BUFFER(buffer);
	_decrement_message_connections();
	return NULL;
}


int _handle_request(int fd, stepd_step_rec_t *job, uid_t uid, pid_t remote_pid)
{
	int rc = SLURM_SUCCESS;
	int req;

	debug3("%s: entering", __func__);
	if ((rc = read(fd, &req, sizeof(int))) != sizeof(int)) {
		if (rc == 0) { /* EOF, normal */
			return -1;
		} else {
			debug3("%s: leaving on read error: %m", __func__);
			return SLURM_ERROR;
		}
	}

	switch (req) {
	case REQUEST_SIGNAL_TASK_LOCAL:		/* Defunct */
	case REQUEST_SIGNAL_TASK_GLOBAL:	/* Defunct */
	case REQUEST_SIGNAL_CONTAINER:
		debug("Handling REQUEST_SIGNAL_CONTAINER");
		rc = _handle_signal_container(fd, job, uid);
		break;
	case REQUEST_CHECKPOINT_TASKS:
		debug("Handling REQUEST_CHECKPOINT_TASKS");
		rc = _handle_checkpoint_tasks(fd, job, uid);
		break;
	case REQUEST_STATE:
		debug("Handling REQUEST_STATE");
		rc = _handle_state(fd, job);
		break;
	case REQUEST_INFO:
		debug("Handling REQUEST_INFO");
		rc = _handle_info(fd, job);
		break;
	case REQUEST_STEP_MEM_LIMITS:
		debug("Handling REQUEST_STEP_MEM_LIMITS");
		rc = _handle_mem_limits(fd, job);
		break;
	case REQUEST_STEP_UID:
		debug("Handling REQUEST_STEP_UID");
		rc = _handle_uid(fd, job);
		break;
	case REQUEST_STEP_NODEID:
		debug("Handling REQUEST_STEP_NODEID");
		rc = _handle_nodeid(fd, job);
		break;
	case REQUEST_ATTACH:
		debug("Handling REQUEST_ATTACH");
		rc = _handle_attach(fd, job, uid);
		break;
	case REQUEST_PID_IN_CONTAINER:
		debug("Handling REQUEST_PID_IN_CONTAINER");
		rc = _handle_pid_in_container(fd, job);
		break;
	case REQUEST_DAEMON_PID:
		debug("Handling REQUEST_DAEMON_PID");
		rc = _handle_daemon_pid(fd, job);
		break;
	case REQUEST_STEP_SUSPEND:
		debug("Handling REQUEST_STEP_SUSPEND");
		rc = _handle_suspend(fd, job, uid);
		break;
	case REQUEST_STEP_RESUME:
		debug("Handling REQUEST_STEP_RESUME");
		rc = _handle_resume(fd, job, uid);
		break;
	case REQUEST_STEP_TERMINATE:
		debug("Handling REQUEST_STEP_TERMINATE");
		rc = _handle_terminate(fd, job, uid);
		break;
	case REQUEST_STEP_COMPLETION_V2:
		debug("Handling REQUEST_STEP_COMPLETION_V2");
		rc = _handle_completion(fd, job, uid);
		break;
	case REQUEST_STEP_TASK_INFO:
		debug("Handling REQUEST_STEP_TASK_INFO");
		rc = _handle_task_info(fd, job);
		break;
	case REQUEST_STEP_STAT:
		debug("Handling REQUEST_STEP_STAT");
		rc = _handle_stat_jobacct(fd, job, uid);
		break;
	case REQUEST_STEP_LIST_PIDS:
		debug("Handling REQUEST_STEP_LIST_PIDS");
		rc = _handle_list_pids(fd, job);
		break;
	case REQUEST_STEP_RECONFIGURE:
		debug("Handling REQUEST_STEP_RECONFIGURE");
		rc = _handle_reconfig(fd, job, uid);
		break;
	case REQUEST_JOB_NOTIFY:
		debug("Handling REQUEST_JOB_NOTIFY");
		rc = _handle_notify_job(fd, job, uid);
		break;
	case REQUEST_ADD_EXTERN_PID:
		debug("Handling REQUEST_ADD_EXTERN_PID");
		rc = _handle_add_extern_pid(fd, job);
		break;
	case REQUEST_X11_DISPLAY:
		debug("Handling REQUEST_X11_DISPLAY");
		rc = _handle_x11_display(fd, job);
		break;
	case REQUEST_GETPW:
		debug("Handling REQUEST_GETPW");
		rc = _handle_getpw(fd, job, remote_pid);
		break;
	case REQUEST_GETGR:
		debug("Handling REQUEST_GETGR");
		rc = _handle_getgr(fd, job, remote_pid);
		break;
	default:
		error("Unrecognized request: %d", req);
		rc = SLURM_ERROR;
		break;
	}

	debug3("%s: leaving with rc: %d", __func__, rc);
	return rc;
}

static int
_handle_state(int fd, stepd_step_rec_t *job)
{
	safe_write(fd, &job->state, sizeof(slurmstepd_state_t));

	return SLURM_SUCCESS;
rwfail:
	return SLURM_ERROR;
}

static int
_handle_info(int fd, stepd_step_rec_t *job)
{
	uint16_t protocol_version = SLURM_PROTOCOL_VERSION;

	safe_write(fd, &job->uid, sizeof(uid_t));
	safe_write(fd, &job->jobid, sizeof(uint32_t));
	safe_write(fd, &job->stepid, sizeof(uint32_t));

	/* protocol_version was added in Slurm version 2.2,
	 * so it needed to be added later in the data sent
	 * for backward compatibility (so that it doesn't
	 * get confused for a huge UID, job ID or step ID;
	 * we should be save in avoiding huge node IDs). */
	safe_write(fd, &protocol_version, sizeof(uint16_t));
	safe_write(fd, &job->nodeid, sizeof(uint32_t));
	safe_write(fd, &job->job_mem, sizeof(uint64_t));
	safe_write(fd, &job->step_mem, sizeof(uint64_t));

	return SLURM_SUCCESS;
rwfail:
	return SLURM_ERROR;
}

static int
_handle_mem_limits(int fd, stepd_step_rec_t *job)
{
	safe_write(fd, &job->job_mem, sizeof(uint64_t));
	safe_write(fd, &job->step_mem, sizeof(uint64_t));

	return SLURM_SUCCESS;
rwfail:
	return SLURM_ERROR;
}

static int
_handle_uid(int fd, stepd_step_rec_t *job)
{
	safe_write(fd, &job->uid, sizeof(uid_t));

	return SLURM_SUCCESS;
rwfail:
	return SLURM_ERROR;
}

static int
_handle_nodeid(int fd, stepd_step_rec_t *job)
{
	safe_write(fd, &job->nodeid, sizeof(uid_t));

	return SLURM_SUCCESS;
rwfail:
	return SLURM_ERROR;
}

static int
_handle_signal_container(int fd, stepd_step_rec_t *job, uid_t uid)
{
	int rc = SLURM_SUCCESS;
	int errnum = 0;
	int sig, flag;
	uid_t req_uid;
	static int msg_sent = 0;
	stepd_step_task_info_t *task;
	uint32_t i;

	safe_read(fd, &sig, sizeof(int));
	safe_read(fd, &flag, sizeof(int));
	safe_read(fd, &req_uid, sizeof(uid_t));

	debug("_handle_signal_container for step=%u.%u uid=%d signal=%d",
	      job->jobid, job->stepid, (int) req_uid, sig);
	/* verify uid off uid instead of req_uid as we can trust that one */
	if ((uid != job->uid) && !_slurm_authorized_user(uid)) {
		error("signal container req from uid %ld for step=%u.%u "
		      "owned by uid %ld",
		      (long)req_uid, job->jobid, job->stepid, (long)job->uid);
		rc = -1;
		errnum = EPERM;
		goto done;
	}

	/*
	 * Sanity checks
	 */
	if ((errnum = _wait_for_job_running(job)) != SLURM_SUCCESS) {
		rc = -1;
		goto done;
	}

	if ((sig == SIGTERM) || (sig == SIGKILL)) {
		/* cycle thru the tasks and mark those that have not
		 * called abort and/or terminated as killed_by_cmd
		 */
		for (i = 0; i < job->node_tasks; i++) {
			if (NULL == (task = job->task[i])) {
				continue;
			}
			if (task->aborted || task->exited) {
				continue;
			}
			/* mark that this task is going to be killed by
			 * cmd so we ignore its exit status - otherwise,
			 * we will probably report the final exit status
			 * as SIGKILL
			 */
			task->killed_by_cmd = true;
		}
	}

	if ((job->stepid != SLURM_EXTERN_CONT) &&
	    (job->nodeid == msg_target_node_id) && (msg_sent == 0) &&
	    (job->state < SLURMSTEPD_STEP_ENDING)) {
		time_t now = time(NULL);
		char entity[24], time_str[24];

		if (job->stepid == SLURM_BATCH_SCRIPT) {
			snprintf(entity, sizeof(entity), "JOB %u", job->jobid);
		} else {
			snprintf(entity, sizeof(entity), "STEP %u.%u",
				 job->jobid, job->stepid);
		}
		slurm_make_time_str(&now, time_str, sizeof(time_str));

		/*
		 * Not really errors,
		 * but we want messages displayed by default
		 */
		if (sig == SIG_TIME_LIMIT) {
			error("*** %s ON %s CANCELLED AT %s DUE TO TIME LIMIT ***",
			      entity, job->node_name, time_str);
			msg_sent = 1;
		} else if (sig == SIG_PREEMPTED) {
			error("*** %s ON %s CANCELLED AT %s DUE TO PREEMPTION ***",
			      entity, job->node_name, time_str);
			msg_sent = 1;
		} else if (sig == SIG_NODE_FAIL) {
			error("*** %s ON %s CANCELLED AT %s DUE TO NODE "
			      "FAILURE, SEE SLURMCTLD LOG FOR DETAILS ***",
			      entity, job->node_name, time_str);
			msg_sent = 1;
		} else if (sig == SIG_REQUEUED) {
			error("*** %s ON %s CANCELLED AT %s DUE TO JOB REQUEUE ***",
			      entity, job->node_name, time_str);
			msg_sent = 1;
		} else if (sig == SIG_FAILURE) {
			error("*** %s ON %s FAILED (non-zero exit code or other "
			      "failure mode) ***",
			      entity, job->node_name);
			msg_sent = 1;
		} else if (sig == SIG_UME) {
			error("*** %s ON %s UNCORRECTABLE MEMORY ERROR AT %s ***",
			      entity, job->node_name, time_str);
		} else if ((sig == SIGTERM) || (sig == SIGKILL) ||
			   (sig == SIG_TERM_KILL)) {
			error("*** %s ON %s CANCELLED AT %s ***",
			      entity, job->node_name, time_str);
			msg_sent = 1;
		}
	}
	if ((sig == SIG_TIME_LIMIT) || (sig == SIG_NODE_FAIL) ||
	    (sig == SIG_PREEMPTED)  || (sig == SIG_FAILURE) ||
	    (sig == SIG_REQUEUED)   || (sig == SIG_UME))
		goto done;

	if (sig == SIG_ABORT) {
		sig = SIGKILL;
		job->aborted = true;
	}

	slurm_mutex_lock(&suspend_mutex);
	if (suspended && (sig != SIGKILL)) {
		rc = -1;
		errnum = ESLURMD_STEP_SUSPENDED;
		slurm_mutex_unlock(&suspend_mutex);
		goto done;
	}

	if (sig == SIG_DEBUG_WAKE) {
		int i;
		for (i = 0; i < job->node_tasks; i++)
			pdebug_wake_process(job, job->task[i]->pid);
		slurm_mutex_unlock(&suspend_mutex);
		goto done;
	}

	if (sig == SIG_TERM_KILL) {
		uint16_t kill_wait = slurm_get_kill_wait();
		(void) proctrack_g_signal(job->cont_id, SIGCONT);
		(void) proctrack_g_signal(job->cont_id, SIGTERM);
		sleep(kill_wait);
		sig = SIGKILL;
	}

	if (flag & KILL_JOB_BATCH
	    && job->stepid == SLURM_BATCH_SCRIPT) {
		/* We should only signal the batch script
		 * and nothing else, the job pgid is the
		 * equal to the pid of the batch script.
		 */
		if (kill(job->pgid, sig) < 0) {
			error("%s: failed signal %d container pid"
			      "%u job %u.%u %m",
			      __func__, sig, job->pgid,
			      job->jobid, job->stepid);
			rc = SLURM_ERROR;
			errnum = errno;
			slurm_mutex_unlock(&suspend_mutex);
			goto done;
		}
		rc = SLURM_SUCCESS;
		errnum = 0;
		verbose("%s: sent signal %d to container pid %u job %u.%u",
			__func__, sig, job->pgid,
			job->jobid, job->stepid);
		slurm_mutex_unlock(&suspend_mutex);
		goto done;
	}

	/*
	 * Signal the container
	 */
	if (proctrack_g_signal(job->cont_id, sig) < 0) {
		rc = -1;
		errnum = errno;
		verbose("Error sending signal %d to %u.%u: %m",
			sig, job->jobid, job->stepid);
	} else {
		verbose("Sent signal %d to %u.%u",
			sig, job->jobid, job->stepid);
	}
	slurm_mutex_unlock(&suspend_mutex);

done:
	/* Send the return code and errnum */
	safe_write(fd, &rc, sizeof(int));
	safe_write(fd, &errnum, sizeof(int));
	return SLURM_SUCCESS;
rwfail:
	return SLURM_ERROR;
}

static int
_handle_checkpoint_tasks(int fd, stepd_step_rec_t *job, uid_t uid)
{
	int rc = SLURM_SUCCESS;
	time_t timestamp;
	int len;
	char *image_dir = NULL;

	debug3("_handle_checkpoint_tasks for job %u.%u",
	       job->jobid, job->stepid);

	safe_read(fd, &timestamp, sizeof(time_t));
	safe_read(fd, &len, sizeof(int));
	if (len) {
		image_dir = xmalloc (len);
		safe_read(fd, image_dir, len); /* '\0' terminated */
	}

	debug3("  uid = %d", uid);
	if (uid != job->uid && !_slurm_authorized_user(uid)) {
		debug("checkpoint req from uid %ld for job %u.%u "
		      "owned by uid %ld",
		      (long)uid, job->jobid, job->stepid, (long)job->uid);
		rc = EPERM;
		goto done;
	}

	if (job->ckpt_timestamp &&
	    timestamp == job->ckpt_timestamp) {
		debug("duplicate checkpoint req for job %u.%u, "
		      "timestamp %ld. discarded.",
		      job->jobid, job->stepid, (long)timestamp);
		rc = ESLURM_ALREADY_DONE; /* EINPROGRESS? */
		goto done;
	}

	/*
	 * Sanity checks
	 */
	if (job->pgid <= (pid_t)1) {
		debug ("step %u.%u invalid [jmgr_pid:%d pgid:%u]",
		       job->jobid, job->stepid, job->jmgr_pid, job->pgid);
		rc = ESLURMD_JOB_NOTRUNNING;
		goto done;
	}

	slurm_mutex_lock(&suspend_mutex);
	if (suspended) {
		rc = ESLURMD_STEP_SUSPENDED;
		slurm_mutex_unlock(&suspend_mutex);
		goto done;
	}

	/* set timestamp in case another request comes */
	job->ckpt_timestamp = timestamp;

	/* TODO: do we need job->ckpt_dir any more,
	 *	except for checkpoint/xlch? */
/*	if (! image_dir) { */
/*		image_dir = xstrdup(job->ckpt_dir); */
/*	} */

	/* call the plugin to send the request */
	if (checkpoint_signal_tasks(job, image_dir) != SLURM_SUCCESS) {
		rc = -1;
		verbose("Error sending checkpoint request to %u.%u: %s",
			job->jobid, job->stepid, slurm_strerror(rc));
	} else {
		verbose("Sent checkpoint request to %u.%u",
			job->jobid, job->stepid);
	}

	slurm_mutex_unlock(&suspend_mutex);

done:
	/* Send the return code */
	safe_write(fd, &rc, sizeof(int));
	xfree(image_dir);
	return SLURM_SUCCESS;

rwfail:
	xfree(image_dir);
	return SLURM_ERROR;
}

static int
_handle_notify_job(int fd, stepd_step_rec_t *job, uid_t uid)
{
	int rc = SLURM_SUCCESS;
	int len;
	char *message = NULL;

	debug3("_handle_notify_job for job %u.%u",
	       job->jobid, job->stepid);

	safe_read(fd, &len, sizeof(int));
	if (len) {
		message = xmalloc (len);
		safe_read(fd, message, len); /* '\0' terminated */
	}

	debug3("  uid = %d", uid);
	if ((uid != job->uid) && !_slurm_authorized_user(uid)) {
		debug("notify req from uid %ld for job %u.%u "
		      "owned by uid %ld",
		      (long)uid, job->jobid, job->stepid, (long)job->uid);
		rc = EPERM;
		goto done;
	}
	error("%s", message);
	xfree(message);

done:
	/* Send the return code */
	safe_write(fd, &rc, sizeof(int));
	xfree(message);
	return SLURM_SUCCESS;

rwfail:
	xfree(message);
	return SLURM_ERROR;
}

static int
_handle_terminate(int fd, stepd_step_rec_t *job, uid_t uid)
{
	int rc = SLURM_SUCCESS;
	int errnum = 0;
	stepd_step_task_info_t *task;
	uint32_t i;

	if (uid != job->uid && !_slurm_authorized_user(uid)) {
		debug("terminate req from uid %ld for job %u.%u "
		      "owned by uid %ld",
		      (long)uid, job->jobid, job->stepid, (long)job->uid);
		rc = -1;
		errnum = EPERM;
		goto done;
	}
	debug("_handle_terminate for step=%u.%u uid=%d",
	      job->jobid, job->stepid, uid);
	step_terminate_monitor_start(job);

	/*
	 * Sanity checks
	 */
	if ((errnum = _wait_for_job_running(job)) != SLURM_SUCCESS) {
		rc = -1;
		goto done;
	}

	/* cycle thru the tasks and mark those that have not
	 * called abort and/or terminated as killed_by_cmd
	 */
	for (i = 0; i < job->node_tasks; i++) {
		if (NULL == (task = job->task[i])) {
			continue;
		}
		if (task->aborted || task->exited) {
			continue;
		}
		/* mark that this task is going to be killed by
		 * cmd so we ignore its exit status - otherwise,
		 * we will probably report the final exit status
		 * as SIGKILL
		 */
		task->killed_by_cmd = true;
	}

	/*
	 * Signal the container with SIGKILL
	 */
	slurm_mutex_lock(&suspend_mutex);
	if (suspended) {
		debug("Terminating suspended job step %u.%u",
		      job->jobid, job->stepid);
		suspended = false;
	}

	if (proctrack_g_signal(job->cont_id, SIGKILL) < 0) {
		if (errno != ESRCH) {	/* No error if process already gone */
			rc = -1;
			errnum = errno;
		}
		verbose("Error sending SIGKILL signal to %u.%u: %m",
			job->jobid, job->stepid);
	} else {
		verbose("Sent SIGKILL signal to %u.%u",
			job->jobid, job->stepid);
	}
	slurm_mutex_unlock(&suspend_mutex);

done:
	/* Send the return code and errnum */
	safe_write(fd, &rc, sizeof(int));
	safe_write(fd, &errnum, sizeof(int));
	return SLURM_SUCCESS;
rwfail:
	return SLURM_ERROR;
}

static int
_handle_attach(int fd, stepd_step_rec_t *job, uid_t uid)
{
	srun_info_t *srun;
	int rc = SLURM_SUCCESS;
	uint32_t *gtids = NULL, *pids = NULL;
	int len, i;

	debug("_handle_attach for job %u.%u", job->jobid, job->stepid);

	srun       = xmalloc(sizeof(srun_info_t));
	srun->key  = (srun_key_t *)xmalloc(SLURM_IO_KEY_SIZE);

	debug("sizeof(srun_info_t) = %d, sizeof(slurm_addr_t) = %d",
	      (int) sizeof(srun_info_t), (int) sizeof(slurm_addr_t));
	safe_read(fd, &srun->ioaddr, sizeof(slurm_addr_t));
	safe_read(fd, &srun->resp_addr, sizeof(slurm_addr_t));
	safe_read(fd, srun->key, SLURM_IO_KEY_SIZE);
	safe_read(fd, &srun->protocol_version, sizeof(uint16_t));

	if (!srun->protocol_version)
		srun->protocol_version = NO_VAL16;

	/*
	 * Check if jobstep is actually running.
	 */
	if (job->state != SLURMSTEPD_STEP_RUNNING) {
		rc = ESLURMD_JOB_NOTRUNNING;
		goto done;
	}

	/*
	 * At the moment, it only makes sense for the slurmd to make this
	 * call, so only _slurm_authorized_user is allowed.
	 */
	if (!_slurm_authorized_user(uid)) {
		error("uid %ld attempt to attach to job %u.%u owned by %ld",
		      (long) uid, job->jobid, job->stepid, (long)job->uid);
		rc = EPERM;
		goto done;
	}

	list_prepend(job->sruns, (void *) srun);
	rc = io_client_connect(srun, job);
	srun = NULL;
	debug("  back from io_client_connect, rc = %d", rc);
done:
	/* Send the return code */
	safe_write(fd, &rc, sizeof(int));

	debug("  in _handle_attach rc = %d", rc);
	if (rc == SLURM_SUCCESS) {
		/* Send response info */


		debug("  in _handle_attach sending response info");
		len = job->node_tasks * sizeof(uint32_t);
		pids = xmalloc(len);
		gtids = xmalloc(len);

		if (job->task != NULL) {
			for (i = 0; i < job->node_tasks; i++) {
				if (job->task[i] == NULL)
					continue;
				pids[i] = (uint32_t)job->task[i]->pid;
				gtids[i] = job->task[i]->gtid;
			}
		}

		safe_write(fd, &job->node_tasks, sizeof(uint32_t));
		safe_write(fd, pids, len);
		safe_write(fd, gtids, len);
		xfree(pids);
		xfree(gtids);

		for (i = 0; i < job->node_tasks; i++) {
			if (job->task && job->task[i] && job->task[i]->argv) {
				len = strlen(job->task[i]->argv[0]) + 1;
				safe_write(fd, &len, sizeof(int));
				safe_write(fd, job->task[i]->argv[0], len);
			} else {
				len = 0;
				safe_write(fd, &len, sizeof(int));
			}
		}
	}
	if (srun) {
		xfree(srun->key);
		xfree(srun);
	}
	return SLURM_SUCCESS;

rwfail:
	if (srun) {
		xfree(srun->key);
		xfree(srun);
	}
	xfree(pids);
	xfree(gtids);
	return SLURM_ERROR;
}

static int
_handle_pid_in_container(int fd, stepd_step_rec_t *job)
{
	bool rc = false;
	pid_t pid;

	debug("_handle_pid_in_container for job %u.%u",
	      job->jobid, job->stepid);

	safe_read(fd, &pid, sizeof(pid_t));

	rc = proctrack_g_has_pid(job->cont_id, pid);

	/* Send the return code */
	safe_write(fd, &rc, sizeof(bool));

	debug("Leaving _handle_pid_in_container");
	return SLURM_SUCCESS;
rwfail:
	return SLURM_ERROR;
}

static void _block_on_pid(pid_t pid)
{
	/* I wish there was another way to wait on a foreign pid, but
	 * I was unable to find one.
	 */
	while (kill(pid, 0) != -1)
		sleep(1);
}

/* Wait for the pid given and when it ends get and children it might
 * of left behind and wait on them instead.
 */
static void *_wait_extern_pid(void *args)
{
	extern_pid_t *extern_pid = (extern_pid_t *)args;

	stepd_step_rec_t *job = extern_pid->job;
	pid_t pid = extern_pid->pid;

	jobacctinfo_t *jobacct = NULL;
	pid_t *pids = NULL;
	int npids = 0, i;
	char	proc_stat_file[256];	/* Allow ~20x extra length */
	FILE *stat_fp = NULL;
	int fd;
	char sbuf[256], *tmp, state[1];
	int num_read, ppid;

	xfree(extern_pid);

	//info("waiting on pid %d", pid);
	_block_on_pid(pid);
	//info("done with pid %d %d: %m", pid, rc);
	jobacct = jobacct_gather_remove_task(pid);
	if (jobacct) {
		job->jobacct->energy.consumed_energy = 0;
		jobacctinfo_aggregate(job->jobacct, jobacct);
		jobacctinfo_destroy(jobacct);
	}
	acct_gather_profile_g_task_end(pid);

	/* See if we have any children of init left and add them to track. */
	proctrack_g_get_pids(job->cont_id, &pids, &npids);
	for (i = 0; i < npids; i++) {
		snprintf(proc_stat_file, 256, "/proc/%d/stat", pids[i]);
		if (!(stat_fp = fopen(proc_stat_file, "r")))
			continue;  /* Assume the process went away */
		fd = fileno(stat_fp);
		if (fcntl(fd, F_SETFD, FD_CLOEXEC) == -1)
			error("%s: fcntl(%s): %m", __func__, proc_stat_file);

		num_read = read(fd, sbuf, (sizeof(sbuf) - 1));

		if (num_read <= 0)
			goto next_pid;

		sbuf[num_read] = '\0';

		/* get to the end of cmd name */
		tmp = strrchr(sbuf, ')');
		if (tmp) {
			*tmp = '\0';	/* replace trailing ')' with NULL */
			/* skip space after ')' too */
			sscanf(tmp + 2,	"%c %d ", state, &ppid);

			if (ppid == 1) {
				debug2("adding tracking of orphaned process %d",
				       pids[i]);
				_handle_add_extern_pid_internal(job, pids[i]);
			}
		}
	next_pid:
		fclose(stat_fp);
	}

	return NULL;
}

static int _handle_add_extern_pid_internal(stepd_step_rec_t *job, pid_t pid)
{
	extern_pid_t *extern_pid;
	jobacct_id_t jobacct_id;

	if (job->stepid != SLURM_EXTERN_CONT) {
		error("%s: non-extern step (%u) given for job %u.",
		      __func__, job->stepid, job->jobid);
		return SLURM_ERROR;
	}

	debug("%s: for job %u.%u, pid %d",
	      __func__, job->jobid, job->stepid, pid);

	extern_pid = xmalloc(sizeof(extern_pid_t));
	extern_pid->job = job;
	extern_pid->pid = pid;

	/* track pid: add outside of the below thread so that the pam module
	 * waits until the parent pid is added, before letting the parent spawn
	 * any children. */
	jobacct_id.taskid = job->nodeid; /* Treat node ID as global task ID */
	jobacct_id.nodeid = job->nodeid;
	jobacct_id.job = job;

	if (proctrack_g_add(job, pid) != SLURM_SUCCESS) {
		error("%s: Job %u can't add pid %d to proctrack plugin in the extern_step.", __func__, job->jobid, pid);
		return SLURM_ERROR;
	}

	if (task_g_add_pid(pid) != SLURM_SUCCESS) {
		error("%s: Job %u can't add pid %d to task plugin in the extern_step.", __func__, job->jobid, pid);
		return SLURM_ERROR;
	}

	if (jobacct_gather_add_task(pid, &jobacct_id, 1) != SLURM_SUCCESS) {
		error("%s: Job %u can't add pid %d to jobacct_gather plugin in the extern_step.", __func__, job->jobid, pid);
		return SLURM_ERROR;
	}

	/* spawn a thread that will wait on the pid given */
	slurm_thread_create_detached(NULL, _wait_extern_pid, extern_pid);

	return SLURM_SUCCESS;
}

static int
_handle_add_extern_pid(int fd, stepd_step_rec_t *job)
{
	int rc = SLURM_SUCCESS;
	pid_t pid;

	safe_read(fd, &pid, sizeof(pid_t));

	rc = _handle_add_extern_pid_internal(job, pid);

	/* Send the return code */
	safe_write(fd, &rc, sizeof(int));

	debug("Leaving _handle_add_extern_pid");
	return SLURM_SUCCESS;
rwfail:
	return SLURM_ERROR;
}

static int _handle_x11_display(int fd, stepd_step_rec_t *job)
{
	int len = 0;
	/* Send the display number. zero indicates no display setup */
	safe_write(fd, &job->x11_display, sizeof(int));
	if (job->x11_xauthority) {
		/* include NUL termination in length */
		len = strlen(job->x11_xauthority) + 1;
		safe_write(fd, &len, sizeof(int));
		safe_write(fd, job->x11_xauthority, len);
	} else {
		safe_write(fd, &len, sizeof(int));
	}

	debug("Leaving _handle_get_x11_display");
	return SLURM_SUCCESS;
rwfail:
	return SLURM_ERROR;
}

static int _handle_getpw(int fd, stepd_step_rec_t *job, pid_t remote_pid)
{
	uid_t uid;
	int mode = 0;
	int len = 0;
	char *name = NULL;
	bool pid_match, user_match = false;
	int found = 0;

	safe_read(fd, &mode, sizeof(int));
	safe_read(fd, &uid, sizeof(uid_t));
	safe_read(fd, &len, sizeof(int));
	if (len) {
		name = xmalloc(len + 1); /* add room for NUL */
		safe_read(fd, name, len);
	}

	pid_match = proctrack_g_has_pid(job->cont_id, remote_pid);

	if (uid == job->uid)
		user_match = true;
	else if (!xstrcmp(name, job->user_name))
		user_match = true;

	if (mode == GETPW_MATCH_USER_AND_PID)
		found = (user_match && pid_match);
	else if (mode == GETPW_MATCH_PID)
		found = pid_match;
	else if (mode == GETPW_MATCH_ALWAYS)
		found = 1;

	if (!job->user_name || !job->pw_gecos ||
	    !job->pw_dir || !job->pw_shell) {
		error("%s: incomplete data, ignoring request", __func__);
		found = 0;
	}

	safe_write(fd, &found, sizeof(int));

	if (!found)
		return SLURM_SUCCESS;

	len = strlen(job->user_name);
	safe_write(fd, &len, sizeof(int));
	safe_write(fd, job->user_name, len);

	len = 1;
	safe_write(fd, &len, sizeof(int));
	safe_write(fd, "x", len);

	safe_write(fd, &job->uid, sizeof(uid_t));
	safe_write(fd, &job->gid, sizeof(gid_t));

	len = strlen(job->pw_gecos);
	safe_write(fd, &len, sizeof(int));
	safe_write(fd, job->pw_gecos, len);

	len = strlen(job->pw_dir);
	safe_write(fd, &len, sizeof(int));
	safe_write(fd, job->pw_dir, len);

	len = strlen(job->pw_shell);
	safe_write(fd, &len, sizeof(int));
	safe_write(fd, job->pw_shell, len);

	debug2("Leaving %s", __func__);
	return SLURM_SUCCESS;

rwfail:
	xfree(name);
	return SLURM_ERROR;
}

static int _send_one_struct_group(int fd, stepd_step_rec_t *job, int offset)
{
	int len;

	if (!job->gr_names[offset])
		goto rwfail;
	len = strlen(job->gr_names[offset]);
	safe_write(fd, &len, sizeof(int));
	safe_write(fd, job->gr_names[offset], len);

	len = 1;
	safe_write(fd, &len, sizeof(int));
	safe_write(fd, "x", len);

	safe_write(fd, &job->gids[offset], sizeof(gid_t));

	len = strlen(job->user_name);
	safe_write(fd, &len, sizeof(int));
	safe_write(fd, job->user_name, len);

	return SLURM_SUCCESS;
rwfail:
	return SLURM_ERROR;
}

static int _handle_getgr(int fd, stepd_step_rec_t *job, pid_t remote_pid)
{
	gid_t gid;
	int mode = 0;
	int len = 0;
	char *name = NULL;
	int offset = 0;
	bool pid_match;
	int found = 0;

	safe_read(fd, &mode, sizeof(int));
	safe_read(fd, &gid, sizeof(gid_t));
	safe_read(fd, &len, sizeof(int));
	if (len) {
		name = xmalloc(len + 1); /* add room for NUL */
		safe_read(fd, name, len);
	}

	pid_match = proctrack_g_has_pid(job->cont_id, remote_pid);

	if (!job->ngids || !job->gids || !job->gr_names) {
		error("%s: incomplete data, ignoring request", __func__);
	} else if (mode == GETGR_MATCH_GROUP_AND_PID ) {
		while (offset < job->ngids) {
			if (gid == job->gids[offset])
				break;
			if (!xstrcmp(name, job->gr_names[offset]))
				break;
		}
		if (offset < job->ngids)
			found = 1;
	} else if (mode == GETGR_MATCH_PID) {
		found = pid_match ? job->ngids : 0;
	} else if (mode == GETGR_MATCH_ALWAYS) {
		found = job->ngids;
	}

	safe_write(fd, &found, sizeof(int));

	if (!found)
		return SLURM_SUCCESS;

	if (mode == GETGR_MATCH_GROUP_AND_PID) {
		if (_send_one_struct_group(fd, job, offset))
			goto rwfail;
	} else {
		for (int i = 0; i < job->ngids; i++) {
			if (_send_one_struct_group(fd, job, i))
				goto rwfail;
		}
	}

	debug2("Leaving %s", __func__);
	return SLURM_SUCCESS;

rwfail:
	xfree(name);
	return SLURM_ERROR;
}

static int
_handle_daemon_pid(int fd, stepd_step_rec_t *job)
{
	safe_write(fd, &job->jmgr_pid, sizeof(pid_t));

	return SLURM_SUCCESS;
rwfail:
	return SLURM_ERROR;
}

static int
_handle_suspend(int fd, stepd_step_rec_t *job, uid_t uid)
{
	int rc = SLURM_SUCCESS;
	int errnum = 0;
	uint16_t job_core_spec = NO_VAL16;

	safe_read(fd, &job_core_spec, sizeof(uint16_t));

	debug("_handle_suspend for step:%u.%u uid:%ld core_spec:%u",
	      job->jobid, job->stepid, (long)uid, job_core_spec);

	if (!_slurm_authorized_user(uid)) {
		debug("job step suspend request from uid %ld for job %u.%u ",
		      (long)uid, job->jobid, job->stepid);
		rc = -1;
		errnum = EPERM;
		goto done;
	}

	if ((errnum = _wait_for_job_running(job)) != SLURM_SUCCESS) {
		rc = -1;
		goto done;
	}

	acct_gather_suspend_poll();

	/*
	 * Signal the container
	 */
	slurm_mutex_lock(&suspend_mutex);
	if (suspended) {
		rc = -1;
		errnum = ESLURMD_STEP_SUSPENDED;
		slurm_mutex_unlock(&suspend_mutex);
		goto done;
	} else {
		if (!job->batch && switch_g_job_step_pre_suspend(job))
			error("switch_g_job_step_pre_suspend: %m");

		/* SIGTSTP is sent first to let MPI daemons stop their tasks,
		 * then wait 2 seconds, then send SIGSTOP to the spawned
		 * process's container to stop everything else.
		 *
		 * In some cases, 1 second has proven insufficient. Longer
		 * delays may help ensure that all MPI tasks have been stopped
		 * (that depends upon the MPI implementation used), but will
		 * also permit longer time periods when more than one job can
		 * be running on each resource (not good). */
		if (proctrack_g_signal(job->cont_id, SIGTSTP) < 0) {
			verbose("Error suspending %u.%u (SIGTSTP): %m",
				job->jobid, job->stepid);
		} else
			sleep(2);

		if (proctrack_g_signal(job->cont_id, SIGSTOP) < 0) {
			verbose("Error suspending %u.%u (SIGSTOP): %m",
				job->jobid, job->stepid);
		} else {
			verbose("Suspended %u.%u", job->jobid, job->stepid);
		}
		suspended = true;
	}
	if (!job->batch && switch_g_job_step_post_suspend(job))
		error("switch_g_job_step_post_suspend: %m");
	if (!job->batch && core_spec_g_suspend(job->cont_id, job_core_spec))
		error("core_spec_g_suspend: %m");

	slurm_mutex_unlock(&suspend_mutex);

done:
	/* Send the return code and errno */
	safe_write(fd, &rc, sizeof(int));
	safe_write(fd, &errnum, sizeof(int));
	return SLURM_SUCCESS;
rwfail:
	return SLURM_ERROR;
}

static int
_handle_resume(int fd, stepd_step_rec_t *job, uid_t uid)
{
	int rc = SLURM_SUCCESS;
	int errnum = 0;
	uint16_t job_core_spec = NO_VAL16;

	safe_read(fd, &job_core_spec, sizeof(uint16_t));

	debug("_handle_resume for step:%u.%u uid:%ld core_spec:%u",
	      job->jobid, job->stepid, (long)uid, job_core_spec);

	if (!_slurm_authorized_user(uid)) {
		debug("job step resume request from uid %ld for job %u.%u ",
		      (long)uid, job->jobid, job->stepid);
		rc = -1;
		errnum = EPERM;
		goto done;
	}

	if ((errnum = _wait_for_job_running(job)) != SLURM_SUCCESS) {
		rc = -1;
		goto done;
	}

	acct_gather_resume_poll();
	/*
	 * Signal the container
	 */
	slurm_mutex_lock(&suspend_mutex);
	if (!suspended) {
		rc = -1;
		errnum = ESLURMD_STEP_NOTSUSPENDED;
		slurm_mutex_unlock(&suspend_mutex);
		goto done;
	} else {
		if (!job->batch && switch_g_job_step_pre_resume(job))
			error("switch_g_job_step_pre_resume: %m");
		if (!job->batch && core_spec_g_resume(job->cont_id,
						      job_core_spec))
			error("core_spec_g_resume: %m");
		if (proctrack_g_signal(job->cont_id, SIGCONT) < 0) {
			verbose("Error resuming %u.%u: %m",
				job->jobid, job->stepid);
		} else {
			verbose("Resumed %u.%u", job->jobid, job->stepid);
		}
		suspended = false;
	}
	if (!job->batch && switch_g_job_step_post_resume(job))
		error("switch_g_job_step_post_resume: %m");

	/*
	 * Reset CPU frequencies if changed
	 */
	if ((job->cpu_freq_min != NO_VAL) || (job->cpu_freq_max != NO_VAL) ||
	    (job->cpu_freq_gov != NO_VAL))
		cpu_freq_set(job);
	// TODO: Reset TRES frequencies?

	slurm_mutex_unlock(&suspend_mutex);

done:
	/* Send the return code and errno */
	safe_write(fd, &rc, sizeof(int));
	safe_write(fd, &errnum, sizeof(int));
	return SLURM_SUCCESS;
rwfail:
	return SLURM_ERROR;
}

static int
_handle_completion(int fd, stepd_step_rec_t *job, uid_t uid)
{
	int rc = SLURM_SUCCESS;
	int errnum = 0;
	int first;
	int last;
	jobacctinfo_t *jobacct = NULL;
	int step_rc;
	char *buf = NULL;
	int len;
	Buf buffer = NULL;
	bool lock_set = false;

	debug("_handle_completion for job %u.%u",
	      job->jobid, job->stepid);

	debug3("  uid = %d", uid);
	if (!_slurm_authorized_user(uid)) {
		debug("step completion message from uid %ld for job %u.%u ",
		      (long)uid, job->jobid, job->stepid);
		rc = -1;
		errnum = EPERM;
		/* Send the return code and errno */
		safe_write(fd, &rc, sizeof(int));
		safe_write(fd, &errnum, sizeof(int));
		return SLURM_SUCCESS;
	}

	safe_read(fd, &first, sizeof(int));
	safe_read(fd, &last, sizeof(int));
	safe_read(fd, &step_rc, sizeof(int));

	/*
	 * We must not use getinfo over a pipe with slurmd here
	 * Indeed, slurmstepd does a large use of setinfo over a pipe
	 * with slurmd and doing the reverse can result in a deadlock
	 * scenario with slurmd :
	 * slurmd(lockforread,write)/slurmstepd(write,lockforread)
	 * Do pack/unpack instead to be sure of independances of
	 * slurmd and slurmstepd
	 */
	safe_read(fd, &len, sizeof(int));
	buf = xmalloc(len);
	safe_read(fd, buf, len);
	buffer = create_buf(buf, len);
	buf = NULL;	/* Moved to data portion of "buffer", freed with that */
	if (jobacctinfo_unpack(&jobacct, SLURM_PROTOCOL_VERSION,
			       PROTOCOL_TYPE_SLURM, buffer, 1) != SLURM_SUCCESS)
		goto rwfail;
	FREE_NULL_BUFFER(buffer);

	/*
	 * Record the completed nodes
	 */
	slurm_mutex_lock(&step_complete.lock);
	lock_set = true;
	if (!step_complete.wait_children) {
		rc = -1;
		errnum = ETIMEDOUT; /* not used anyway */
		goto timeout;
	}

	/*
	 * SlurmUser or root can craft a launch without a valid credential
	 * ("srun --no-alloc ...") and no tree information can be built
	 *  without the hostlist from the credential.
	 */
	if (step_complete.bits && (step_complete.rank >= 0)) {
#if 0
		char bits_string[128];
		debug2("Setting range %d (bit %d) through %d(bit %d)",
		       first, first-(step_complete.rank+1),
		       last, last-(step_complete.rank+1));
		bit_fmt(bits_string, sizeof(bits_string), step_complete.bits);
		debug2("  before bits: %s", bits_string);
#endif
		bit_nset(step_complete.bits,
			 first - (step_complete.rank+1),
			 last - (step_complete.rank+1));
#if 0
		bit_fmt(bits_string, sizeof(bits_string), step_complete.bits);
		debug2("  after bits: %s", bits_string);
#endif
	}
	step_complete.step_rc = MAX(step_complete.step_rc, step_rc);

	/************* acct stuff ********************/
	jobacctinfo_aggregate(step_complete.jobacct, jobacct);
timeout:
	jobacctinfo_destroy(jobacct);
	/*********************************************/

	/*
	 * Send the return code and errno, we do this within the locked
	 * region to ensure that the stepd doesn't exit before we can
	 * perform this send.
	 */
	safe_write(fd, &rc, sizeof(int));
	safe_write(fd, &errnum, sizeof(int));
	slurm_cond_signal(&step_complete.cond);
	slurm_mutex_unlock(&step_complete.lock);

	return SLURM_SUCCESS;


rwfail:	if (lock_set) {
		slurm_cond_signal(&step_complete.cond);
		slurm_mutex_unlock(&step_complete.lock);
	}
	xfree(buf);	/* In case of failure before moving to "buffer" */
	FREE_NULL_BUFFER(buffer);
	return SLURM_ERROR;
}

static int
_handle_stat_jobacct(int fd, stepd_step_rec_t *job, uid_t uid)
{
	jobacctinfo_t *jobacct = NULL;
	jobacctinfo_t *temp_jobacct = NULL;
	int i = 0;
	int num_tasks = 0;
	debug("_handle_stat_jobacct for job %u.%u",
	      job->jobid, job->stepid);

	debug3("  uid = %d", uid);
	if (uid != job->uid && !_slurm_authorized_user(uid)) {
		debug("stat jobacct from uid %ld for job %u.%u "
		      "owned by uid %ld",
		      (long)uid, job->jobid, job->stepid, (long)job->uid);
		/* Send NULL */
		jobacctinfo_setinfo(jobacct, JOBACCT_DATA_PIPE, &fd,
				    SLURM_PROTOCOL_VERSION);
		return SLURM_ERROR;
	}

	jobacct = jobacctinfo_create(NULL);
	debug3("num tasks = %d", job->node_tasks);

	for (i = 0; i < job->node_tasks; i++) {
		temp_jobacct = jobacct_gather_stat_task(job->task[i]->pid);
		if (temp_jobacct) {
			jobacctinfo_aggregate(jobacct, temp_jobacct);
			jobacctinfo_destroy(temp_jobacct);
			num_tasks++;
		}
	}

	jobacctinfo_setinfo(jobacct, JOBACCT_DATA_PIPE, &fd,
			    SLURM_PROTOCOL_VERSION);
	safe_write(fd, &num_tasks, sizeof(int));

	jobacctinfo_destroy(jobacct);

	return SLURM_SUCCESS;

rwfail:
	jobacctinfo_destroy(jobacct);
	return SLURM_ERROR;
}

/* We don't check the uid in this function, anyone may list the task info. */
static int
_handle_task_info(int fd, stepd_step_rec_t *job)
{
	int i;
	stepd_step_task_info_t *task;

	debug("_handle_task_info for job %u.%u", job->jobid, job->stepid);

	safe_write(fd, &job->node_tasks, sizeof(uint32_t));
	for (i = 0; i < job->node_tasks; i++) {
		task = job->task[i];
		safe_write(fd, &task->id, sizeof(int));
		safe_write(fd, &task->gtid, sizeof(uint32_t));
		safe_write(fd, &task->pid, sizeof(pid_t));
		safe_write(fd, &task->exited, sizeof(bool));
		safe_write(fd, &task->estatus, sizeof(int));
	}

	return SLURM_SUCCESS;
rwfail:
	return SLURM_ERROR;
}

/* We don't check the uid in this function, anyone may list the task info. */
static int
_handle_list_pids(int fd, stepd_step_rec_t *job)
{
	int i;
	pid_t *pids = NULL;
	int npids = 0;
	uint32_t pid;

	debug("_handle_list_pids for job %u.%u", job->jobid, job->stepid);
	proctrack_g_get_pids(job->cont_id, &pids, &npids);
	safe_write(fd, &npids, sizeof(uint32_t));
	for (i = 0; i < npids; i++) {
		pid = (uint32_t)pids[i];
		safe_write(fd, &pid, sizeof(uint32_t));
	}
	if (npids > 0)
		xfree(pids);

	return SLURM_SUCCESS;
rwfail:
	if (npids > 0)
		xfree(pids);
	return SLURM_ERROR;
}

static int
_handle_reconfig(int fd, stepd_step_rec_t *job, uid_t uid)
{
	int rc = SLURM_SUCCESS;
	int errnum = 0;

	if (!_slurm_authorized_user(uid)) {
		debug("job step reconfigure request from uid %ld "
		      "for job %u.%u ",
		      (long)uid, job->jobid, job->stepid);
		rc = -1;
		errnum = EPERM;
		goto done;
	}

	/* We just want to make sure the file handle is correct on a
	   reconfigure since the file could had rolled thus making
	   the currect fd incorrect. */
	log_alter(conf->log_opts, SYSLOG_FACILITY_DAEMON, conf->logfile);
	debug("_handle_reconfigure for job %u.%u successful",
	      job->jobid, job->stepid);

done:
	/* Send the return code and errno */
	safe_write(fd, &rc, sizeof(int));
	safe_write(fd, &errnum, sizeof(int));
	return SLURM_SUCCESS;
rwfail:
	return SLURM_ERROR;
}

extern void wait_for_resumed(uint16_t msg_type)
{
	int i;

	for (i = 0; ; i++) {
		if (i)
			sleep(1);
		if (!suspended)
			return;
		if (i == 0) {
			info("defer sending msg_type %u to suspended job",
			     msg_type);
		}
	}
}

extern void set_msg_node_id(stepd_step_rec_t *job)
{
	char *ptr = getenvp(job->env, "SLURM_STEP_KILLED_MSG_NODE_ID");
	if (ptr)
		msg_target_node_id = atoi(ptr);
}
