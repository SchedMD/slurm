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

#include <arpa/inet.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include "src/common/cpu_frequency.h"
#include "src/common/eio.h"
#include "src/common/fd.h"
#include "src/common/macros.h"
#include "src/common/net.h"
#include "src/common/parse_time.h"
#include "src/common/proc_args.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/slurm_protocol_pack.h"
#include "src/common/stepd_api.h"
#include "src/common/stepd_proxy.h"
#include "src/common/strlcpy.h"
#include "src/common/timers.h"
#include "src/common/tres_frequency.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/acct_gather.h"
#include "src/interfaces/auth.h"
#include "src/interfaces/cgroup.h"
#include "src/interfaces/jobacct_gather.h"
#include "src/interfaces/namespace.h"
#include "src/interfaces/proctrack.h"
#include "src/interfaces/switch.h"
#include "src/interfaces/task.h"

#include "src/slurmd/common/slurmstepd_init.h"
#include "src/slurmd/slurmd/slurmd.h"
#include "src/slurmd/slurmstepd/io.h"
#include "src/slurmd/slurmstepd/mgr.h"
#include "src/slurmd/slurmstepd/pdebug.h"
#include "src/slurmd/slurmstepd/req.h"
#include "src/slurmd/slurmstepd/slurmstepd.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"
#include "src/slurmd/slurmstepd/step_terminate_monitor.h"
#include "src/slurmd/slurmstepd/ulimits.h"

#include "src/stepmgr/srun_comm.h"
#include "src/stepmgr/stepmgr.h"

static void *_handle_accept(void *arg);
static int _handle_request(int fd, uid_t uid, pid_t remote_pid);
static void *_wait_extern_pid(void *args);
static int _handle_add_extern_pid_internal(pid_t pid);
static bool _msg_socket_readable(eio_obj_t *obj);
static int _msg_socket_accept(eio_obj_t *obj, list_t *objs);

struct io_operations msg_socket_ops = {
	.readable = &_msg_socket_readable,
	.handle_read = &_msg_socket_accept
};

static char *socket_name;
static pthread_mutex_t suspend_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool suspended = false;

static int extern_thread_cnt = 0;
static pthread_t *extern_threads = NULL;
static pthread_mutex_t extern_thread_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t extern_thread_cond = PTHREAD_COND_INITIALIZER;

pthread_mutex_t stepmgr_mutex = PTHREAD_MUTEX_INITIALIZER;

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
	return ((uid == (uid_t) 0) || (uid == slurm_conf.slurm_user_id));
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
	if ((fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0)) < 0)
		return -1;

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
		      slurm_step_id_t *step_id)
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
	xstrfmtcat(name, "%s/%s_%u.%u", dir, nodename, step_id->job_id,
		   step_id->step_id);

	if (step_id->step_het_comp != NO_VAL)
		xstrfmtcat(name, ".%u", step_id->step_het_comp);

	/*
	 * First check to see if the named socket already exists.
	 */
	if (stat(name, &stat_buf) == 0) {
		/* Vestigial from a slurmd crash or job requeue that did not
		 * happen properly (very rare conditions). Unlink the file
		 * and recreate it.
		 */
		if (unlink(name) != 0) {
			error("%s: failed unlink(%s): %m",
			      __func__, name);
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
static int _wait_for_job_running(void)
{
	struct timespec ts = {0, 0};
	int count = 0;
	int rc = SLURM_SUCCESS;

	slurm_mutex_lock(&step->state_mutex);

	/*
	 * SLURMSTEPD_STEP_RUNNING is 2 so we need loop at least that
	 * many times, but we don't want to loop any more than that.
	 */
	while ((step->state < SLURMSTEPD_STEP_RUNNING) && (count < 2)) {
		ts.tv_sec  = time(NULL) + 60;
		slurm_cond_timedwait(&step->state_cond,
				     &step->state_mutex, &ts);
		count++;
	}

	if (step->state < SLURMSTEPD_STEP_RUNNING) {
		debug("%ps not running yet %d [cont_id:%"PRIu64"]",
		      &step->step_id, step->state, step->cont_id);
		rc = ESLURMD_STEP_NOTRUNNING;
	}

	slurm_mutex_unlock(&step->state_mutex);
	return rc;
}

static void *_msg_thr_internal(void *ignored)
{
	debug("Message thread started pid = %lu", (unsigned long) getpid());
	eio_handle_mainloop(step->msg_handle);
	debug("Message thread exited");

	return NULL;
}

extern int msg_thr_create(void)
{
	int fd;
	eio_obj_t *eio_obj;
	errno = 0;
	fd = _domain_socket_create(conf->spooldir, conf->node_name,
				   &step->step_id);
	if (fd == -1)
		return SLURM_ERROR;

	fd_set_nonblocking(fd);

	eio_obj = eio_obj_create(fd, &msg_socket_ops, (void *)step);
	step->msg_handle = eio_handle_create(0);
	eio_new_initial_obj(step->msg_handle, eio_obj);

	slurm_thread_create(&step->msgid, _msg_thr_internal, NULL);

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

static int _msg_socket_accept(eio_obj_t *obj, list_t *objs)
{
	int fd, *param = NULL;
	struct sockaddr_un addr;
	int len = sizeof(addr);

	debug3("Called _msg_socket_accept");

	while ((fd = accept4(obj->fd, (struct sockaddr *) &addr,
			    (socklen_t *) &len, SOCK_CLOEXEC)) < 0) {
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

	fd_set_blocking(fd);

	param = xmalloc(sizeof(int));
	*param = fd;
	slurm_thread_create_detached(_handle_accept, param);

	debug3("Leaving _msg_socket_accept");
	return SLURM_SUCCESS;
}

static void *_handle_accept(void *arg)
{
	int fd = *(int *) arg;
	int req;
	int client_protocol_ver;
	buf_t *buffer = NULL;
	int rc;
	uid_t uid = SLURM_AUTH_NOBODY;
	gid_t gid = SLURM_AUTH_NOBODY;
	pid_t remote_pid = 0;

	debug3("%s: entering (new thread)", __func__);
	xfree(arg);

	safe_read(fd, &req, sizeof(int));
	if (req >= SLURM_MIN_PROTOCOL_VERSION) {
		if ((rc = net_get_peer(fd, &uid, &gid, &remote_pid))) {
			error("%s: [fd:%d] Unable to resolve socket peer process from kernel: %s",
			      __func__, fd, slurm_strerror(rc));
			goto fail;
		}

		client_protocol_ver = req;
	} else {
		error("%s: Invalid Protocol Version %d", __func__, req);
		goto fail;
	}

	debug3("%s: [fd:%d] Protocol Version %d from uid=%u gid=%u pid=%lu",
	       __func__, fd, client_protocol_ver, uid, gid,
	       (unsigned long) remote_pid);

	rc = SLURM_PROTOCOL_VERSION;
	safe_write(fd, &rc, sizeof(int));

	while (1) {
		rc = _handle_request(fd, uid, remote_pid);
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

/*
 * NOTE: reply must be in sync with corresponding rpc handling in slurmd.
 */
static int _handle_stepmgr_relay_msg(int fd,
				     uid_t uid,
				     slurm_msg_t *msg,
				     uint16_t msg_type,
				     bool reply)
{
	int rc;
	buf_t *buffer;
	char *data = NULL;
	uint16_t protocol_version;
	uint32_t data_size;
	return_code_msg_t rc_msg = { 0 };

	safe_read(fd, &protocol_version, sizeof(uint16_t));
	safe_read(fd, &data_size, sizeof(uint32_t));
	data = xmalloc(data_size);
	safe_read(fd, data, data_size);

	slurm_msg_t_init(msg);
	msg->msg_type = msg_type;
	msg->protocol_version = protocol_version;

	buffer = create_buf(data, data_size);
	rc = unpack_msg(msg, buffer);
	FREE_NULL_BUFFER(buffer);
	if (rc) {
		if (reply) {
			rc_msg.return_code = rc;
			stepd_proxy_send_resp_to_slurmd(fd, msg,
							RESPONSE_SLURM_RC,
							&rc_msg);
		}
		slurm_free_msg_members(msg);
	}

	return rc;

rwfail:
	xfree(data);
	return SLURM_ERROR;
}

static int _handle_step_create(int fd, uid_t uid, pid_t remote_pid)
{
	int rc;
	slurm_msg_t msg;
	job_step_create_request_msg_t *req_step_msg;

	if ((rc = _handle_stepmgr_relay_msg(fd, uid, &msg,
					    REQUEST_JOB_STEP_CREATE, true)))
		goto done;

	req_step_msg = msg.data;
	slurm_mutex_lock(&stepmgr_mutex);
	msg.auth_uid = req_step_msg->user_id = job_step_ptr->user_id;
	msg.auth_ids_set = true;

	/* step_create_from_msg responds to the client */
	step_create_from_msg(&msg, fd, NULL, NULL);

	slurm_mutex_unlock(&stepmgr_mutex);

	slurm_free_msg_members(&msg);

	return SLURM_SUCCESS;

done:
	return SLURM_ERROR;
}

static int _handle_job_step_get_info(int fd, uid_t uid, pid_t remote_pid)
{
	int rc;
	buf_t *buffer;
	slurm_msg_t msg;
	job_step_info_request_msg_t *request;
	pack_step_args_t args =  {0};

	if ((rc = _handle_stepmgr_relay_msg(fd, uid, &msg,
					    REQUEST_JOB_STEP_INFO, true)))
		goto done;

	request = msg.data;

	buffer = init_buf(BUF_SIZE);

	slurm_mutex_lock(&stepmgr_mutex);
	args.step_id = &request->step_id,
	args.steps_packed = 0,
	args.buffer = buffer,
	args.proto_version = msg.protocol_version,
	args.job_step_list = job_step_ptr->step_list,
	args.pack_job_step_list_func = pack_ctld_job_step_info,

	pack_job_step_info_response_msg(&args);
	slurm_mutex_unlock(&stepmgr_mutex);

	(void) stepd_proxy_send_resp_to_slurmd(fd, &msg, RESPONSE_JOB_STEP_INFO,
					       buffer);
	FREE_NULL_BUFFER(buffer);
	slurm_free_msg_members(&msg);

done:
	return rc;
}

static int _handle_cancel_job_step(int fd, uid_t uid, pid_t remote_pid)
{
	int rc;
	slurm_msg_t msg;
	job_step_kill_msg_t *request;
	return_code_msg_t rc_msg = { 0 };

	if ((rc = _handle_stepmgr_relay_msg(fd, uid, &msg,
					    REQUEST_CANCEL_JOB_STEP, true)))
		goto done;

	request = msg.data;

	slurm_mutex_lock(&stepmgr_mutex);
	rc = job_step_signal(&request->step_id, request->signal,
			     request->flags, uid);
	slurm_mutex_unlock(&stepmgr_mutex);

	rc_msg.return_code = rc;
	stepd_proxy_send_resp_to_slurmd(fd, &msg, RESPONSE_SLURM_RC, &rc_msg);
	slurm_free_msg_members(&msg);

done:
	return rc;
}

static int _handle_srun_job_complete(int fd, uid_t uid, pid_t remote_pid)
{
	int rc;
	slurm_msg_t msg;
	/*
	 * We currently don't need anything in the message
	 * srun_job_complete_msg_t *request;
	 */

	if ((rc = _handle_stepmgr_relay_msg(fd, uid, &msg, SRUN_JOB_COMPLETE,
					    false)))
		goto done;

	slurm_mutex_lock(&stepmgr_mutex);
	srun_job_complete(job_step_ptr);
	slurm_mutex_unlock(&stepmgr_mutex);

	slurm_free_msg_members(&msg);

done:
	return rc;
}

static int _handle_srun_node_fail(int fd, uid_t uid, pid_t remote_pid)
{
	int rc;
	slurm_msg_t msg;
	srun_node_fail_msg_t *request;

	if ((rc = _handle_stepmgr_relay_msg(fd, uid, &msg, SRUN_NODE_FAIL,
					    false)))
		goto done;

	request = msg.data;
	slurm_mutex_lock(&stepmgr_mutex);
	srun_node_fail(job_step_ptr, request->nodelist);
	slurm_mutex_unlock(&stepmgr_mutex);

	slurm_free_msg_members(&msg);

done:
	return rc;
}

static int _handle_srun_timeout(int fd, uid_t uid, pid_t remote_pid)
{
	int rc;
	slurm_msg_t msg;
	/*
	 * We currently don't need anything in the message
	 * srun_timeout_msg_t *request;
	 */

	if ((rc = _handle_stepmgr_relay_msg(fd, uid, &msg, SRUN_TIMEOUT,
					    false)))
		goto done;

	slurm_mutex_lock(&stepmgr_mutex);
	srun_timeout(job_step_ptr);
	slurm_mutex_unlock(&stepmgr_mutex);

	slurm_free_msg_members(&msg);

done:
	return rc;
}

static int _handle_update_step(int fd, uid_t uid, pid_t remote_pid)
{
	int rc;
	slurm_msg_t msg;
	step_update_request_msg_t *request;
	return_code_msg_t rc_msg = { 0 };

	if ((rc = _handle_stepmgr_relay_msg(fd, uid, &msg,
					    REQUEST_UPDATE_JOB_STEP, true)))
		goto done;

	request = msg.data;

	slurm_mutex_lock(&stepmgr_mutex);
	rc = update_step(request, uid);
	slurm_mutex_unlock(&stepmgr_mutex);

	rc_msg.return_code = rc;
	stepd_proxy_send_resp_to_slurmd(fd, &msg, RESPONSE_SLURM_RC, &rc_msg);
	slurm_free_msg_members(&msg);

done:
	return rc;
}

static int _handle_step_layout(int fd, uid_t uid, pid_t remote_pid)
{
	int rc;
	slurm_msg_t msg;
	slurm_step_id_t *request;
	slurm_step_layout_t *step_layout = NULL;
	return_code_msg_t rc_msg = { 0 };

	if ((rc = _handle_stepmgr_relay_msg(fd, uid, &msg, REQUEST_STEP_LAYOUT,
					    true)))
		goto done;

	request = msg.data;

	slurm_mutex_lock(&stepmgr_mutex);
	rc = stepmgr_get_step_layouts(job_step_ptr, request, &step_layout);
	slurm_mutex_unlock(&stepmgr_mutex);
	if (!rc) {
		(void) stepd_proxy_send_resp_to_slurmd(fd, &msg,
						       RESPONSE_STEP_LAYOUT,
						       step_layout);
		slurm_step_layout_destroy(step_layout);
	} else {
		rc_msg.return_code = rc;
		stepd_proxy_send_resp_to_slurmd(fd, &msg, RESPONSE_SLURM_RC,
						&rc_msg);
	}

	slurm_free_msg_members(&msg);

done:
	return rc;
}

static int _handle_job_sbcast_cred(int fd, uid_t uid, pid_t remote_pid)
{
	int rc;
	slurm_msg_t msg;
	step_alloc_info_msg_t *request;
	job_sbcast_cred_msg_t *job_info_resp_msg = NULL;
	return_code_msg_t rc_msg = { 0 };

	if ((rc = _handle_stepmgr_relay_msg(fd, uid, &msg,
					    REQUEST_JOB_SBCAST_CRED, true)))
		goto done;

	request = msg.data;

	slurm_mutex_lock(&stepmgr_mutex);
	rc = stepmgr_get_job_sbcast_cred_msg(job_step_ptr, &request->step_id,
					     msg.protocol_version,
					     &job_info_resp_msg);
	slurm_mutex_unlock(&stepmgr_mutex);
	if (rc)
		goto resp;

	(void) stepd_proxy_send_resp_to_slurmd(fd, &msg,
					       RESPONSE_JOB_SBCAST_CRED,
					       job_info_resp_msg);

	slurm_free_sbcast_cred_msg(job_info_resp_msg);
	slurm_free_msg_members(&msg);
	return rc;

resp:
	rc_msg.return_code = rc;
	stepd_proxy_send_resp_to_slurmd(fd, &msg, RESPONSE_SLURM_RC, &rc_msg);
	slurm_free_msg_members(&msg);

done:
	return rc;
}

static void _het_job_alloc_list_del(void *x)
{
	resource_allocation_response_msg_t *job_info_resp_msg = x;
	slurm_free_resource_allocation_response_msg(job_info_resp_msg);
}

static int _handle_het_job_alloc_info(int fd, uid_t uid, pid_t remote_pid)
{
	int rc;
	slurm_msg_t msg;
	job_alloc_info_msg_t *request;
	resource_allocation_response_msg_t *job_info_resp_msg = NULL;
	list_t *resp_list = NULL;
	return_code_msg_t rc_msg = { 0 };

	if ((rc = _handle_stepmgr_relay_msg(fd, uid, &msg,
					    REQUEST_HET_JOB_ALLOC_INFO, true)))
		goto done;

	request = msg.data;

	if (request->step_id.job_id != job_step_ptr->job_id) {
		error("attempting to get job information for %pI from a different stepmgr jobid %u: %s RPC from uid=%u",
		      &request->step_id, job_step_ptr->job_id,
		      rpc_num2string(msg.msg_type), uid);
		rc = ESLURM_INVALID_JOB_ID;
		goto resp;
	}

	slurm_mutex_lock(&stepmgr_mutex);

	resp_list = list_create(_het_job_alloc_list_del);
	job_info_resp_msg = build_job_info_resp(job_step_ptr);
	list_append(resp_list, job_info_resp_msg);

	slurm_mutex_unlock(&stepmgr_mutex);

	(void) stepd_proxy_send_resp_to_slurmd(fd, &msg,
					       RESPONSE_HET_JOB_ALLOCATION,
					       resp_list);

	FREE_NULL_LIST(resp_list);
	slurm_free_msg_members(&msg);
	return rc;

resp:
	rc_msg.return_code = rc;
	stepd_proxy_send_resp_to_slurmd(fd, &msg, RESPONSE_SLURM_RC, &rc_msg);
	slurm_free_msg_members(&msg);

done:
	return rc;
}

static int _handle_sluid(int fd, uid_t uid, pid_t remote_pid)
{
	safe_write(fd, &step->step_id.sluid, sizeof(sluid_t));

	return SLURM_SUCCESS;
rwfail:
	return SLURM_ERROR;
}

static int _handle_state(int fd, uid_t uid, pid_t remote_pid)
{
	safe_write(fd, &step->state, sizeof(slurmstepd_state_t));

	return SLURM_SUCCESS;
rwfail:
	return SLURM_ERROR;
}

static int _handle_mem_limits(int fd, uid_t uid, pid_t remote_pid)
{
	safe_write(fd, &step->job_mem, sizeof(uint64_t));
	safe_write(fd, &step->step_mem, sizeof(uint64_t));

	return SLURM_SUCCESS;
rwfail:
	return SLURM_ERROR;
}

static int _handle_uid(int fd, uid_t uid, pid_t remote_pid)
{
	safe_write(fd, &step->uid, sizeof(uid_t));

	return SLURM_SUCCESS;
rwfail:
	return SLURM_ERROR;
}

static int _handle_nodeid(int fd, uid_t uid, pid_t remote_pid)
{
	safe_write(fd, &step->nodeid, sizeof(uid_t));

	return SLURM_SUCCESS;
rwfail:
	return SLURM_ERROR;
}

static int _handle_signal_container(int fd, uid_t uid, pid_t remote_pid)
{
	int rc = SLURM_SUCCESS;
	int errnum = 0;
	int sig, flag, details_len;
	char *details = NULL;
	uid_t req_uid;
	static int msg_sent = 0;
	stepd_step_task_info_t *task;
	uint32_t i;

	safe_read(fd, &sig, sizeof(int));
	safe_read(fd, &flag, sizeof(int));
	safe_read(fd, &details_len, sizeof(int));
	if (details_len)
		details = xmalloc(details_len + 1);
	safe_read(fd, details, details_len);
	safe_read(fd, &req_uid, sizeof(uid_t));

	debug("_handle_signal_container for %ps uid=%u signal=%d flag=0x%x",
	      &step->step_id, req_uid, sig, flag);

	if (flag & KILL_NO_SIG_FAIL)
		step->flags |= LAUNCH_NO_SIG_FAIL;

	/*
	 * Sanity checks
	 */
	if ((errnum = _wait_for_job_running()) != SLURM_SUCCESS) {
		rc = -1;
		goto done;
	}

	if ((sig == SIGTERM) || (sig == SIGKILL)) {
		/* cycle thru the tasks and mark those that have not
		 * called abort and/or terminated as killed_by_cmd
		 */
		for (i = 0; i < step->node_tasks; i++) {
			if (NULL == (task = step->task[i])) {
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

	if ((step->step_id.step_id != SLURM_EXTERN_CONT) &&
	    (step->nodeid == msg_target_node_id) && (msg_sent == 0) &&
	    (step->state < SLURMSTEPD_STEP_ENDING)) {
		time_t now = time(NULL);
		char entity[45], time_str[256];

		if (step->step_id.step_id == SLURM_BATCH_SCRIPT) {
			snprintf(entity, sizeof(entity), "JOB %u",
				 step->step_id.job_id);
		} else {
			char tmp_char[33];
			log_build_step_id_str(&step->step_id, tmp_char,
					      sizeof(tmp_char),
					      STEP_ID_FLAG_NO_PREFIX);
			snprintf(entity, sizeof(entity), "STEP %s", tmp_char);
		}
		slurm_make_time_str(&now, time_str, sizeof(time_str));

		/*
		 * Not really errors,
		 * but we want messages displayed by default
		 */
		if (sig == SIG_TIME_LIMIT) {
			error("*** %s ON %s CANCELLED AT %s DUE TO TIME LIMIT ***",
			      entity, step->node_name, time_str);
			msg_sent = 1;
		} else if (sig == SIG_PREEMPTED) {
			error("*** %s ON %s CANCELLED AT %s DUE TO PREEMPTION ***",
			      entity, step->node_name, time_str);
			msg_sent = 1;
		} else if (sig == SIG_NODE_FAIL) {
			error("*** %s ON %s CANCELLED AT %s DUE TO NODE "
			      "FAILURE, SEE SLURMCTLD LOG FOR DETAILS ***",
			      entity, step->node_name, time_str);
			msg_sent = 1;
		} else if (sig == SIG_REQUEUED) {
			error("*** %s ON %s CANCELLED AT %s DUE TO JOB REQUEUE ***",
			      entity, step->node_name, time_str);
			msg_sent = 1;
		} else if (sig == SIG_FAILURE) {
			error("*** %s ON %s FAILED (non-zero exit code or other "
			      "failure mode) ***",
			      entity, step->node_name);
			msg_sent = 1;
		} else if ((sig == SIGTERM) || (sig == SIGKILL)) {
			error("*** %s ON %s CANCELLED AT %s DUE to SIGNAL %s ***",
			      entity, step->node_name, time_str,
			      strsignal(sig));
			msg_sent = 1;
		} else if (sig == SIG_TERM_KILL) {
			error("*** %s ON %s CANCELLED AT %s DUE TO TASK FAILURE ***",
			      entity, step->node_name, time_str);
			msg_sent = 1;
		}

		if (details)
			error("*** REASON: %s ***", details);
	}
	if ((sig == SIG_TIME_LIMIT) || (sig == SIG_NODE_FAIL) ||
	    (sig == SIG_PREEMPTED) || (sig == SIG_FAILURE) ||
	    (sig == SIG_REQUEUED))
		goto done;

	if (sig == SIG_ABORT) {
		sig = SIGKILL;
		step->aborted = true;
	}

	slurm_mutex_lock(&suspend_mutex);
	if (suspended && (sig != SIGKILL)) {
		rc = -1;
		errnum = ESLURMD_STEP_SUSPENDED;
		slurm_mutex_unlock(&suspend_mutex);
		goto done;
	}

	if (sig == SIG_DEBUG_WAKE) {
		for (int i = 0; i < step->node_tasks; i++)
			pdebug_wake_process(step->task[i]->pid);
		slurm_mutex_unlock(&suspend_mutex);
		goto done;
	}

	if (sig == SIG_TERM_KILL) {
		(void) proctrack_g_signal(step->cont_id, SIGCONT);
		(void) proctrack_g_signal(step->cont_id, SIGTERM);
		sleep(slurm_conf.kill_wait);
		sig = SIGKILL;
	}

	/*
	 * Specific handle for the batch container and some related flags.
	 */
	if (step->step_id.step_id == SLURM_BATCH_SCRIPT &&
	    ((flag & KILL_JOB_BATCH) || (flag & KILL_FULL_JOB))) {

		if (flag & KILL_FULL_JOB)
			rc = killpg(step->pgid, sig);
		else
			rc = kill(step->pgid, sig);
		if (rc < 0) {
			error("%s: failed signal %d pid %u %ps %m",
			      __func__, sig, step->pgid, &step->step_id);
			rc = SLURM_ERROR;
			errnum = errno;
			slurm_mutex_unlock(&suspend_mutex);
			goto done;
		}

		verbose("%s: sent signal %d to pid %u %ps",
			__func__, sig, step->pgid, &step->step_id);
		rc = SLURM_SUCCESS;
		errnum = 0;
		slurm_mutex_unlock(&suspend_mutex);
		goto done;
	}

	/*
	 * Signal the container
	 */
	if (proctrack_g_signal(step->cont_id, sig) < 0) {
		rc = -1;
		errnum = errno;
		verbose("Error sending signal %d to %ps: %m",
			sig, &step->step_id);
	} else {
		verbose("Sent signal %d to %ps", sig, &step->step_id);
	}
	slurm_mutex_unlock(&suspend_mutex);

	if ((sig == SIGTERM) || (sig == SIGKILL))
		set_job_state(SLURMSTEPD_STEP_CANCELLED);

done:
	xfree(details);

	/* Send the return code and errnum */
	safe_write(fd, &rc, sizeof(int));
	safe_write(fd, &errnum, sizeof(int));
	return SLURM_SUCCESS;
rwfail:
	xfree(details);
	return SLURM_ERROR;
}

static int _handle_notify_job(int fd, uid_t uid, pid_t remote_pid)
{
	int rc = SLURM_SUCCESS;
	int len;
	char *message = NULL;

	debug3("_handle_notify_job for %ps", &step->step_id);

	safe_read(fd, &len, sizeof(int));
	if (len) {
		message = xmalloc(len + 1);
		safe_read(fd, message, len);
	}

	error("%s", message);
	xfree(message);

	/* Send the return code */
	safe_write(fd, &rc, sizeof(int));
	xfree(message);
	return SLURM_SUCCESS;

rwfail:
	xfree(message);
	return SLURM_ERROR;
}

static int _handle_terminate(int fd, uid_t uid, pid_t remote_pid)
{
	int rc = SLURM_SUCCESS;
	int errnum = 0;
	stepd_step_task_info_t *task;
	uint32_t i;

	debug("_handle_terminate for %ps uid=%u", &step->step_id, uid);
	step_terminate_monitor_start();

	/*
	 * Sanity checks
	 */
	if ((errnum = _wait_for_job_running()) != SLURM_SUCCESS) {
		rc = -1;
		goto done;
	}

	/* cycle thru the tasks and mark those that have not
	 * called abort and/or terminated as killed_by_cmd
	 */
	for (i = 0; i < step->node_tasks; i++) {
		if (NULL == (task = step->task[i])) {
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
		debug("Terminating suspended %ps", &step->step_id);
		suspended = false;
	}

	if (proctrack_g_signal(step->cont_id, SIGKILL) < 0) {
		if (errno != ESRCH) {	/* No error if process already gone */
			rc = -1;
			errnum = errno;
		}
		verbose("Error sending SIGKILL signal to %ps: %m",
			&step->step_id);
	} else {
		verbose("Sent SIGKILL signal to %ps", &step->step_id);
	}
	slurm_mutex_unlock(&suspend_mutex);

	set_job_state(SLURMSTEPD_STEP_CANCELLED);

done:
	/* Send the return code and errnum */
	safe_write(fd, &rc, sizeof(int));
	safe_write(fd, &errnum, sizeof(int));
	return SLURM_SUCCESS;
rwfail:
	return SLURM_ERROR;
}

static int _handle_attach(int fd, uid_t uid, pid_t remote_pid)
{
	srun_info_t *srun;
	int rc = SLURM_SUCCESS;
	uint32_t *gtids = NULL, *pids = NULL;
	uint32_t key_len, cert_len;
	int len, i;

	debug("_handle_attach for %ps", &step->step_id);

	srun       = xmalloc(sizeof(srun_info_t));

	safe_read(fd, &cert_len, sizeof(uint32_t));
	if (cert_len) {
		srun->tls_cert = xmalloc(cert_len);
		safe_read(fd, srun->tls_cert, cert_len);
	}
	safe_read(fd, &srun->ioaddr, sizeof(slurm_addr_t));
	safe_read(fd, &srun->resp_addr, sizeof(slurm_addr_t));
	safe_read(fd, &key_len, sizeof(uint32_t));
	srun->key = xmalloc(key_len);
	safe_read(fd, srun->key, key_len);
	safe_read(fd, &srun->uid, sizeof(uid_t));
	safe_read(fd, &srun->protocol_version, sizeof(uint16_t));

	if (!srun->protocol_version)
		srun->protocol_version = NO_VAL16;

	/*
	 * Check if jobstep is actually running.
	 */
	if (step->state != SLURMSTEPD_STEP_RUNNING) {
		rc = ESLURMD_STEP_NOTRUNNING;
		goto done;
	}

	list_prepend(step->sruns, srun);
	rc = io_client_connect(srun);
	srun = NULL;
	debug("  back from io_client_connect, rc = %d", rc);
done:
	/* Send the return code */
	safe_write(fd, &rc, sizeof(int));

	debug("  in _handle_attach rc = %d", rc);
	if (rc == SLURM_SUCCESS) {
		/* Send response info */


		debug("  in _handle_attach sending response info");
		len = step->node_tasks * sizeof(uint32_t);
		pids = xmalloc(len);
		gtids = xmalloc(len);

		if (step->task != NULL) {
			for (i = 0; i < step->node_tasks; i++) {
				if (step->task[i] == NULL)
					continue;
				pids[i] = (uint32_t)step->task[i]->pid;
				gtids[i] = step->task[i]->gtid;
			}
		}

		safe_write(fd, &step->node_tasks, sizeof(uint32_t));
		safe_write(fd, pids, len);
		safe_write(fd, gtids, len);
		xfree(pids);
		xfree(gtids);

		for (i = 0; i < step->node_tasks; i++) {
			if (step->task && step->task[i] &&
			    step->task[i]->argv) {
				len = strlen(step->task[i]->argv[0]) + 1;
				safe_write(fd, &len, sizeof(int));
				safe_write(fd, step->task[i]->argv[0], len);
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

static int _handle_pid_in_container(int fd, uid_t uid, pid_t remote_pid)
{
	bool rc = false;
	pid_t pid;

	debug("_handle_pid_in_container for %ps", &step->step_id);

	safe_read(fd, &pid, sizeof(pid_t));

	rc = proctrack_g_has_pid(step->cont_id, pid);

	/* Send the return code */
	safe_write(fd, &rc, sizeof(bool));

	debug("Leaving _handle_pid_in_container");
	return SLURM_SUCCESS;
rwfail:
	return SLURM_ERROR;
}

static int _handle_get_ns_fd_helper(void *object, void *arg)
{
	ns_fd_map_t *entry = (ns_fd_map_t *) object;
	int *fd = (int *) arg;

#if defined(__linux__)
	if (entry->type != CLONE_NEWNS)
		return SLURM_SUCCESS;
#endif

	safe_write(*fd, &entry->fd, sizeof(entry->fd));
	send_fd_over_socket(*fd, entry->fd);

	debug("sent fd: %d", entry->fd);
	return SLURM_SUCCESS;

rwfail:
	return SLURM_ERROR;
}

static int _handle_get_ns_fd(int fd, uid_t uid, pid_t remote_pid)
{
	list_t *ns_map = list_create(NULL);

	debug("%s: for %pI %ps", __func__, &step->step_id, &step->step_id);

	if (namespace_g_join_external(&step->step_id, ns_map) < 0)
		goto rwfail;

	list_for_each_ro(ns_map, _handle_get_ns_fd_helper, &fd);

	debug("leaving %s", __func__);

	list_destroy(ns_map);
	return SLURM_SUCCESS;
rwfail:
	list_destroy(ns_map);
	return SLURM_ERROR;
}

static int _handle_get_ns_fds_helper(void *object, void *arg)
{
	ns_fd_map_t *entry = (ns_fd_map_t *) object;
	int *fd = (int *) arg;

	safe_write(*fd, &entry->type, sizeof(entry->type));
	send_fd_over_socket(*fd, entry->fd);

	debug("sent fd: %d", entry->fd);
	return SLURM_SUCCESS;

rwfail:
	return SLURM_ERROR;
}

static int _handle_get_ns_fds(int fd, uid_t uid, pid_t remote_pid)
{
	list_t *ns_map = list_create(NULL);
	int ns_count = 0;

	debug("%s: for %pI %ps", __func__, &step->step_id, &step->step_id);

	if (namespace_g_join_external(&step->step_id, ns_map) < 0)
		goto rwfail;

	ns_count = list_count(ns_map);
	safe_write(fd, &ns_count, sizeof(ns_count));
	list_for_each_ro(ns_map, _handle_get_ns_fds_helper, &fd);

	debug("leaving %s", __func__);

	list_destroy(ns_map);
	return SLURM_SUCCESS;
rwfail:
	list_destroy(ns_map);
	return SLURM_ERROR;
}

static int _handle_get_bpf_token(int fd, uid_t uid, pid_t remote_pid)
{
	int rc = SLURM_ERROR;
	int bpf_fd, token_fd = -1;

	/* If I am not the extern step do not reply */
	if (step->step_id.step_id != SLURM_EXTERN_CONT) {
		safe_write(fd, &rc, sizeof(int));
		return SLURM_ERROR;
	}

	token_fd = cgroup_g_bpf_get_token();

	/* BPF token is already generated, just send it */
	if (token_fd != -1) {
		rc = 0;
		safe_write(fd, &rc, sizeof(int));
		send_fd_over_socket(fd, token_fd);
	} else { /* Generate BPF token */
		rc = 1;
		safe_write(fd, &rc, sizeof(int));

		/* Receive fsopen rc*/
		safe_read(fd, &rc, sizeof(int));
		if (rc != SLURM_SUCCESS) {
			error("bpf fsopen failure");
			goto fini;
		}

		/* Receive the fd for fsopen */
		bpf_fd = receive_fd_over_socket(fd);
		if (bpf_fd < 0) {
			rc = SLURM_ERROR;
			error("Problems receiving the bpf fsopen fd");
			safe_write(fd, &rc, sizeof(int));
			goto fini;
		}

		/* Do the fsconfig for the bpf fs and send the rc */
		rc = cgroup_g_bpf_fsconfig(bpf_fd);
		close(bpf_fd);
		safe_write(fd, &rc, sizeof(int));
		if (rc != SLURM_SUCCESS) {
			error("bpf fsconfig failure");
			goto fini;
		}

		/* Receive token_creation rc*/
		safe_read(fd, &rc, sizeof(int));
		if (rc != SLURM_SUCCESS) {
			error("bpf token creation failure");
			goto fini;
		}

		/* BPF token fd reception*/
		token_fd = receive_fd_over_socket(fd);
		if (token_fd < 0) {
			rc = SLURM_ERROR;
			error("Problems receiving the bpf token fd");
		} else {
			rc = SLURM_SUCCESS;
			/* Save the token in the cgroup plugin */
			cgroup_g_bpf_set_token(token_fd);
		}
		/* Send rc for the reception of the token fd*/
		safe_write(fd, &rc, sizeof(int));
	}
fini:
	return SLURM_SUCCESS;
rwfail:
	return SLURM_ERROR;
}

static void _block_on_pid(pid_t pid)
{
	struct timespec ts = { 0, 0 };

	slurm_mutex_lock(&extern_thread_lock);
	while (kill(pid, 0) != -1) {
		if (step->state >= SLURMSTEPD_STEP_CANCELLED)
			break;
		clock_gettime(CLOCK_REALTIME, &ts);
		ts.tv_sec += 1;
		slurm_cond_timedwait(&extern_thread_cond, &extern_thread_lock,
				     &ts);
	}
	slurm_mutex_unlock(&extern_thread_lock);
}

/*
 * Wait for the given pid and when it ends, get any children that the pid might
 * have left behind. Then wait on these if so.
 */
static void *_wait_extern_pid(void *args)
{
	pid_t pid = *(pid_t *) args;
	jobacctinfo_t *jobacct = NULL;
	pid_t *pids = NULL;
	int npids = 0, i;
	char	proc_stat_file[256];	/* Allow ~20x extra length */
	FILE *stat_fp = NULL;
	int fd;
	char sbuf[256], *tmp, state[1];
	int num_read, ppid;

	xfree(args);

	//info("waiting on pid %d", pid);
	_block_on_pid(pid);
	//info("done with pid %d %d: %m", pid, rc);
	jobacct = jobacct_gather_remove_task(pid);
	if (jobacct) {
		step->jobacct->energy.consumed_energy = 0;
		jobacctinfo_aggregate(step->jobacct, jobacct);
		jobacctinfo_destroy(jobacct);
	}
	acct_gather_profile_g_task_end(pid);

	if (step->state >= SLURMSTEPD_STEP_CANCELLED)
		goto end;

	/*
	 * See if we have any children of the given pid left behind, and if
	 * found add them to track.
	 */
	proctrack_g_get_pids(step->cont_id, &pids, &npids);
	for (i = 0; i < npids; i++) {
		snprintf(proc_stat_file, 256, "/proc/%d/stat", pids[i]);
		if (!(stat_fp = fopen(proc_stat_file, "r")))
			continue;  /* Assume the process went away */

		/*
		 * If this pid is slurmstepd's pid (ourselves) or it is already
		 * tracked in the accounting, this is not an orphaned pid,
		 * so just ignore it.
		 */
		if ((getpid() == pids[i]) ||
		    jobacct_gather_stat_task(pids[i], false))
			goto next_pid;

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
				_handle_add_extern_pid_internal(pids[i]);
			}
		}
	next_pid:
		fclose(stat_fp);
	}
end:
	xfree(pids);
	return NULL;
}

static void _wait_extern_thr_create(pid_t *extern_pid)
{
	/* Lock as several RPC can write to the same variable. */
	slurm_mutex_lock(&extern_thread_lock);
	extern_thread_cnt++;
	xrecalloc(extern_threads, extern_thread_cnt, sizeof(pthread_t));
	slurm_thread_create(&extern_threads[extern_thread_cnt - 1],
			    _wait_extern_pid, extern_pid);
	slurm_mutex_unlock(&extern_thread_lock);
}

static int _handle_add_extern_pid_internal(pid_t pid)
{
	pid_t *extern_pid;
	jobacct_id_t jobacct_id;

	if (step->step_id.step_id != SLURM_EXTERN_CONT) {
		error("%s: non-extern step (%ps) given for %pI",
		      __func__, &step->step_id, &step->step_id);
		return SLURM_ERROR;
	}

	debug("%s: for %ps, pid %d", __func__, &step->step_id, pid);

	extern_pid = xmalloc(sizeof(*extern_pid));
	*extern_pid = pid;

	/* track pid: add outside of the below thread so that the pam module
	 * waits until the parent pid is added, before letting the parent spawn
	 * any children. */
	jobacct_id.taskid = step->nodeid; /* Treat node ID as global task ID */
	jobacct_id.nodeid = step->nodeid;
	jobacct_id.step = step;

	if (proctrack_g_add(step, pid) != SLURM_SUCCESS) {
		error("%s: %pI can't add pid %d to proctrack plugin in the extern_step.",
		      __func__, &step->step_id, pid);
		return SLURM_ERROR;
	}

	if (task_g_add_pid(pid) != SLURM_SUCCESS) {
		error("%s: %pI can't add pid %d to task plugin in the extern_step.",
		      __func__, &step->step_id, pid);
		return SLURM_ERROR;
	}

	if (jobacct_gather_add_task(pid, &jobacct_id, 1) != SLURM_SUCCESS) {
		error("%s: %pI can't add pid %d to jobacct_gather plugin in the extern_step.",
		      __func__, &step->step_id, pid);
		return SLURM_ERROR;
	}

	if (xstrcasestr(slurm_conf.launch_params, "ulimit_pam_adopt"))
		set_user_limits(pid);

	/* spawn a thread that will wait on the pid given */
	_wait_extern_thr_create(extern_pid);

	return SLURM_SUCCESS;
}

static int _handle_add_extern_pid(int fd, uid_t uid, pid_t remote_pid)
{
	int rc = SLURM_SUCCESS;
	pid_t pid;

	slurm_mutex_lock(&step->state_mutex);
	if (step->state >= SLURMSTEPD_STEP_CANCELLED) {
		error("Rejecting request to add extern pid from uid %u because step is ending",
		      uid);
		goto rwfail;
	}

	safe_read(fd, &pid, sizeof(pid_t));

	rc = _handle_add_extern_pid_internal(pid);

	/* Send the return code */
	safe_write(fd, &rc, sizeof(int));

	debug("Leaving _handle_add_extern_pid");
	slurm_mutex_unlock(&step->state_mutex);
	return SLURM_SUCCESS;
rwfail:
	slurm_mutex_unlock(&step->state_mutex);
	return SLURM_ERROR;
}

static int _handle_x11_display(int fd, uid_t uid, pid_t remote_pid)
{
	int len = 0;
	/* Send the display number. zero indicates no display setup */
	safe_write(fd, &step->x11_display, sizeof(int));
	if (step->x11_xauthority) {
		/* include NUL termination in length */
		len = strlen(step->x11_xauthority) + 1;
		safe_write(fd, &len, sizeof(int));
		safe_write(fd, step->x11_xauthority, len);
	} else {
		safe_write(fd, &len, sizeof(int));
	}

	debug("Leaving _handle_get_x11_display");
	return SLURM_SUCCESS;
rwfail:
	return SLURM_ERROR;
}

static int _handle_getpw(int fd, uid_t socket_uid, pid_t remote_pid)
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

	pid_match = proctrack_g_has_pid(step->cont_id, remote_pid);

	if (uid == step->uid)
		user_match = true;
	else if (!xstrcmp(name, step->user_name))
		user_match = true;

	if (mode == GETPW_MATCH_USER_AND_PID)
		found = (user_match && pid_match);
	else if (mode == GETPW_MATCH_PID)
		found = pid_match;
	else if (mode == GETPW_MATCH_ALWAYS)
		found = 1;

	if (!step->user_name || !step->pw_gecos ||
	    !step->pw_dir || !step->pw_shell) {
		error("%s: incomplete data, ignoring request", __func__);
		found = 0;
	}

	safe_write(fd, &found, sizeof(int));

	if (!found)
		return SLURM_SUCCESS;

	len = strlen(step->user_name);
	safe_write(fd, &len, sizeof(int));
	safe_write(fd, step->user_name, len);

	len = 1;
	safe_write(fd, &len, sizeof(int));
	safe_write(fd, "*", len);

	safe_write(fd, &step->uid, sizeof(uid_t));
	safe_write(fd, &step->gid, sizeof(gid_t));

	len = strlen(step->pw_gecos);
	safe_write(fd, &len, sizeof(int));
	safe_write(fd, step->pw_gecos, len);

	len = strlen(step->pw_dir);
	safe_write(fd, &len, sizeof(int));
	safe_write(fd, step->pw_dir, len);

	len = strlen(step->pw_shell);
	safe_write(fd, &len, sizeof(int));
	safe_write(fd, step->pw_shell, len);

	debug2("Leaving %s", __func__);
	return SLURM_SUCCESS;

rwfail:
	xfree(name);
	return SLURM_ERROR;
}

static int _send_one_struct_group(int fd, int offset)
{
	int len;

	if (!step->gr_names[offset])
		goto rwfail;
	len = strlen(step->gr_names[offset]);
	safe_write(fd, &len, sizeof(int));
	safe_write(fd, step->gr_names[offset], len);

	len = 1;
	safe_write(fd, &len, sizeof(int));
	safe_write(fd, "*", len);

	safe_write(fd, &step->gids[offset], sizeof(gid_t));

	len = strlen(step->user_name);
	safe_write(fd, &len, sizeof(int));
	safe_write(fd, step->user_name, len);

	return SLURM_SUCCESS;
rwfail:
	return SLURM_ERROR;
}

static int _handle_getgr(int fd, uid_t uid, pid_t remote_pid)
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

	pid_match = proctrack_g_has_pid(step->cont_id, remote_pid);

	if (!step->ngids || !step->gids || !step->gr_names) {
		error("%s: incomplete data, ignoring request", __func__);
	} else if ((mode == GETGR_MATCH_GROUP_AND_PID) && pid_match) {
		while (offset < step->ngids) {
			if (gid == step->gids[offset])
				break;
			if (!xstrcmp(name, step->gr_names[offset]))
				break;
			offset++;
		}
		if (offset < step->ngids)
			found = 1;
	} else if (mode == GETGR_MATCH_PID) {
		found = pid_match ? step->ngids : 0;
	} else if (mode == GETGR_MATCH_ALWAYS) {
		found = step->ngids;
	}

	safe_write(fd, &found, sizeof(int));

	if (!found)
		return SLURM_SUCCESS;

	if (mode == GETGR_MATCH_GROUP_AND_PID) {
		if (_send_one_struct_group(fd, offset))
			goto rwfail;
	} else {
		for (int i = 0; i < step->ngids; i++) {
			if (_send_one_struct_group(fd, i))
				goto rwfail;
		}
	}

	debug2("Leaving %s", __func__);
	return SLURM_SUCCESS;

rwfail:
	xfree(name);
	return SLURM_ERROR;
}

static int _handle_gethost(int fd, uid_t uid, pid_t remote_pid)
{
	int mode = 0;
	int len = 0;
	char *nodename = NULL;
	char *nodename_r = NULL;
	char *hostname = NULL;
	bool pid_match;
	int found = 0;
	unsigned char address[sizeof(struct in6_addr)];
	char *address_str = NULL;
	int af = AF_UNSPEC;
	slurm_addr_t addr;

	safe_read(fd, &mode, sizeof(int));
	safe_read(fd, &len, sizeof(int));
	if (len) {
		nodename = xmalloc(len + 1); /* add room for NULL */
		safe_read(fd, nodename, len);
	}

	pid_match = proctrack_g_has_pid(step->cont_id, remote_pid);

	if (!(mode & GETHOST_NOT_MATCH_PID) && !pid_match)
		debug("%s: no pid_match", __func__);
	else if (nodename && (!slurm_conf_get_addr(nodename, &addr, 0))) {
		char *tmp_str;

		found = 1;

		if (addr.ss_family == AF_INET)
			af = AF_INET;
		else if (addr.ss_family == AF_INET6)
			af = AF_INET6;

		nodename_r = xstrdup(nodename);
		hostname = xstrdup(nodename);

		slurm_get_ip_str(&addr, (char *)address, INET6_ADDRSTRLEN);
		tmp_str = xstrdup((char *)address);
		inet_pton(af, tmp_str, &address);
		xfree(tmp_str);
	} else if (nodename &&
		   (address_str = slurm_conf_get_address(nodename))) {
		if ((mode & GETHOST_IPV6) &&
		    (inet_pton(AF_INET6, address_str, &address) == 1)) {
			found = 1;
			af = AF_INET6;
		} else if ((mode & GETHOST_IPV4) &&
			   (inet_pton(AF_INET, address_str, &address) == 1)) {
			found = 1;
			af = AF_INET;
		}
		if (found) {
			if (!(nodename_r = slurm_conf_get_nodename(nodename)) ||
			    !(hostname = slurm_conf_get_hostname(nodename_r))) {
				xfree(nodename_r);
				xfree(hostname);
				found = 0;
			}
		}
	}
	xfree(nodename);

	safe_write(fd, &found, sizeof(int));

	if (!found)
		return SLURM_SUCCESS;

	len = strlen(hostname);
	safe_write(fd, &len, sizeof(int));
	safe_write(fd, hostname, len);

	len = 1;
	safe_write(fd, &len, sizeof(int));
	len = strlen(nodename_r);
	safe_write(fd, &len, sizeof(int));
	safe_write(fd, nodename_r, len);

	safe_write(fd, &af, sizeof(int));

	if (af == AF_INET6) {
		len = 16;
		safe_write(fd, &len, sizeof(int));
		safe_write(fd, &address, len);

	} else if (af == AF_INET) {
		len = 4;
		safe_write(fd, &len, sizeof(int));
		safe_write(fd, &address, len);
	} else {
		error("Not supported address type: %u", af);
		goto rwfail;
	}

	xfree(hostname);
	xfree(nodename_r);
	debug2("Leaving %s", __func__);
	return SLURM_SUCCESS;

rwfail:
	xfree(hostname);
	xfree(nodename_r);
	return SLURM_ERROR;
}

static int _handle_daemon_pid(int fd, uid_t uid, pid_t remote_pid)
{
	safe_write(fd, &step->jmgr_pid, sizeof(pid_t));

	return SLURM_SUCCESS;
rwfail:
	return SLURM_ERROR;
}

static int _handle_suspend(int fd, uid_t uid, pid_t remote_pid)
{
	int rc = SLURM_SUCCESS;
	int errnum = 0;
	char *tmp;
	static uint32_t suspend_grace_time = NO_VAL;

	debug("%s for %ps uid:%u", __func__, &step->step_id, uid);

	if ((errnum = _wait_for_job_running()) != SLURM_SUCCESS) {
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
		if (suspend_grace_time == NO_VAL) {
			char *suspend_grace_str = "suspend_grace_time=";

			/* Set default suspend_grace_time */
			suspend_grace_time = 2;

			/*
			 * Overwrite default suspend grace time if set in
			 * slurm_conf
			 */
			if ((tmp = xstrcasestr(slurm_conf.preempt_params,
					       suspend_grace_str))) {
				if (parse_uint32((tmp +
						  strlen(suspend_grace_str)),
						 &suspend_grace_time)) {
					error("Could not parse '%s' Using default instead.",
					      tmp);
				}
			}
		}

		/* SIGTSTP is sent first to let MPI daemons stop their tasks,
		 * then wait 2 seconds, then send SIGSTOP to the spawned
		 * process's container to stop everything else.
		 *
		 * In some cases, 1 second has proven insufficient. Longer
		 * delays may help ensure that all MPI tasks have been stopped
		 * (that depends upon the MPI implementation used), but will
		 * also permit longer time periods when more than one job can
		 * be running on each resource (not good). */
		if (proctrack_g_signal(step->cont_id, SIGTSTP) < 0) {
			verbose("Error suspending %ps (SIGTSTP): %m",
				&step->step_id);
		} else
			sleep(suspend_grace_time);

		if (proctrack_g_signal(step->cont_id, SIGSTOP) < 0) {
			verbose("Error suspending %ps (SIGSTOP): %m",
				&step->step_id);
		} else {
			verbose("Suspended %ps", &step->step_id);
		}
		suspended = true;
	}

	slurm_mutex_unlock(&suspend_mutex);

done:
	/* Send the return code and errno */
	safe_write(fd, &rc, sizeof(int));
	safe_write(fd, &errnum, sizeof(int));
	return SLURM_SUCCESS;
rwfail:
	return SLURM_ERROR;
}

static int _handle_resume(int fd, uid_t uid, pid_t remote_pid)
{
	int rc = SLURM_SUCCESS;
	int errnum = 0;

	debug("%s for %ps uid:%u", __func__, &step->step_id, uid);

	if ((errnum = _wait_for_job_running()) != SLURM_SUCCESS) {
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
		if (proctrack_g_signal(step->cont_id, SIGCONT) < 0) {
			verbose("Error resuming %ps: %m", &step->step_id);
		} else {
			verbose("Resumed %ps", &step->step_id);
		}
		suspended = false;
	}

	/*
	 * Reset CPU frequencies if changed
	 */
	if ((step->cpu_freq_min != NO_VAL) || (step->cpu_freq_max != NO_VAL) ||
	    (step->cpu_freq_gov != NO_VAL))
		cpu_freq_set(step);
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

static int _handle_completion(int fd, uid_t uid, pid_t remote_pid)
{
	int rc = SLURM_SUCCESS;
	int errnum = 0;
	int first;
	int last;
	jobacctinfo_t *jobacct = NULL;
	int step_rc;
	char *buf = NULL;
	int len;
	buf_t *buffer = NULL;
	bool lock_set = false, do_stepmgr = false;
	uint32_t step_id;

	debug("_handle_completion for %ps", &step->step_id);

	safe_read(fd, &first, sizeof(int));
	safe_read(fd, &last, sizeof(int));
	safe_read(fd, &step_rc, sizeof(int));
	safe_read(fd, &step_id, sizeof(uint32_t));
	safe_read(fd, &do_stepmgr, sizeof(bool));

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

	if (do_stepmgr) {
		slurm_mutex_lock(&stepmgr_mutex);
		if (job_step_ptr) {
			int rem = 0;
			uint32_t max_rc;
			slurm_step_id_t temp_id = {
				.job_id = job_step_ptr->job_id,
				.step_het_comp = NO_VAL,
				.step_id = step_id
			};

			step_complete_msg_t req = {
				.range_first = first,
				.range_last = last,
				.step_id = temp_id,
				.step_rc = step_rc,
				.jobacct = jobacct
			};

			step_partial_comp(&req, uid, true, &rem, &max_rc);

			safe_write(fd, &rc, sizeof(int));
			safe_write(fd, &errnum, sizeof(int));

			jobacctinfo_destroy(jobacct);

			rc = SLURM_SUCCESS;
		} else {
			error("Asked to complete a stepmgr step but we don't have a job_step_ptr. This should never happen.");
			rc = SLURM_ERROR;
		}
		slurm_mutex_unlock(&stepmgr_mutex);
		return rc;
	}

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
		int32_t set_bits;
		int32_t first_bit = first - (step_complete.rank + 1);
		int32_t last_bit = last - (step_complete.rank + 1);
		/* bit_set_count_range is [first, end) so +1 last_bit */
		int32_t last_bit_range = last_bit + 1;

#if 0
		char bits_string[128];
		debug2("Setting range %d (bit %d) through %d(bit %d)",
		       first, first_bit,
		       last, last_bit);
		bit_fmt(bits_string, sizeof(bits_string), step_complete.bits);
		debug2("  before bits: %s", bits_string);
#endif
		if (!(set_bits = bit_set_count_range(step_complete.bits,
						     first_bit,
						     last_bit_range))) {
			bit_nset(step_complete.bits, first_bit, last_bit);
		} else if (set_bits == (last_bit_range - first_bit)) {
			debug("Step complete from %d to %d was already processed on rank %d. Probably a RPC was resent from a child.",
			      first, last, step_complete.rank);
			goto timeout;
		} else {
			error("Step complete from %d to %d was half-way processed on rank %d. This should never happen.",
			      first, last, step_complete.rank);
			goto timeout;
		}

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

static int _handle_stat_jobacct(int fd, uid_t uid, pid_t remote_pid)
{
	bool update_data = true;
	jobacctinfo_t *jobacct = NULL;
	jobacctinfo_t *temp_jobacct = NULL;
	int num_tasks = 0;
	uint64_t msg_timeout_us;
	DEF_TIMERS;
	START_TIMER;

	debug("_handle_stat_jobacct for %ps", &step->step_id);

	jobacct = jobacctinfo_create(NULL);
	debug3("num tasks = %d", step->node_tasks);

	/*
	 * Extern step has pid = -1 so it would be skipped, deal with it
	 * differently
	 */
	if (step->step_id.step_id == SLURM_EXTERN_CONT) {
		/*
		 * We only have one task in the extern step on each node,
		 * despite many pids may have been adopted.
		 */
		jobacct_gather_stat_all_task(jobacct);
		jobacctinfo_aggregate(jobacct, step->jobacct);
		num_tasks = 1;
	} else {
		for (int i = 0; i < step->node_tasks; i++) {
			temp_jobacct =
				jobacct_gather_stat_task(step->task[i]->pid,
							 update_data);

			update_data = false;
			if (temp_jobacct) {
				jobacctinfo_aggregate(jobacct, temp_jobacct);
				jobacctinfo_destroy(temp_jobacct);
				num_tasks++;
			}
		}
	}

	jobacctinfo_setinfo(jobacct, JOBACCT_DATA_PIPE, &fd,
			    SLURM_PROTOCOL_VERSION);
	safe_write(fd, &num_tasks, sizeof(int));

	jobacctinfo_destroy(jobacct);
	END_TIMER;
	msg_timeout_us = ((uint64_t) slurm_conf.msg_timeout) * USEC_IN_SEC;
	if (DELTA_TIMER > msg_timeout_us)
		error("%s: Took %s, which is more than MessageTimeout (%us). The result won't be delivered",
		      __func__, TIME_STR, slurm_conf.msg_timeout);
	else
		debug("%s: Completed in %s", __func__, TIME_STR);

	return SLURM_SUCCESS;

rwfail:
	jobacctinfo_destroy(jobacct);
	END_TIMER;
	msg_timeout_us = ((uint64_t) slurm_conf.msg_timeout) * USEC_IN_SEC;
	if (DELTA_TIMER > msg_timeout_us)
		error("%s: Failed in %lus", __func__, DELTA_TIMER);

	return SLURM_ERROR;
}

/* We don't check the uid in this function, anyone may list the task info. */
static int _handle_task_info(int fd, uid_t uid, pid_t remote_pid)
{
	stepd_step_task_info_t *task;

	debug("_handle_task_info for %ps", &step->step_id);

	safe_write(fd, &step->node_tasks, sizeof(uint32_t));
	for (int i = 0; i < step->node_tasks; i++) {
		task = step->task[i];
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
static int _handle_list_pids(int fd, uid_t uid, pid_t remote_pid)
{
	pid_t *pids = NULL;
	int npids = 0;
	uint32_t pid;

	debug("_handle_list_pids for %ps", &step->step_id);
	proctrack_g_get_pids(step->cont_id, &pids, &npids);
	safe_write(fd, &npids, sizeof(uint32_t));
	for (int i = 0; i < npids; i++) {
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

static int _handle_reconfig(int fd, uid_t uid, pid_t remote_pid)
{
	int rc = SLURM_SUCCESS;
	int len;
	buf_t *buffer = NULL;
	int errnum = 0;

	/*
	 * Pull in any needed configuration changes.
	 * len = 0 indicates we're just going for a log rotate.
	 */
	safe_read(fd, &len, sizeof(int));
	if (len) {
		buffer = init_buf(len);
		safe_read(fd, buffer->head, len);
		unpack_stepd_reconf(buffer);
		FREE_NULL_BUFFER(buffer);
	}

	/*
	 * We just want to make sure the file handle is correct on a
	 * reconfigure since the file could had rolled thus making the
	 * current fd incorrect.
	 */
	log_alter(conf->log_opts, SYSLOG_FACILITY_DAEMON, conf->logfile);
	debug("_handle_reconfigure for %ps successful", &step->step_id);

	/* Send the return code and errno */
	safe_write(fd, &rc, sizeof(int));
	safe_write(fd, &errnum, sizeof(int));
	return SLURM_SUCCESS;
rwfail:
	FREE_NULL_BUFFER(buffer);
	return SLURM_ERROR;
}

extern void wait_for_resumed(uint16_t msg_type)
{
	for (int i = 0; ; i++) {
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

extern void set_msg_node_id(void)
{
	char *ptr = getenvp(step->env, "SLURM_STEP_KILLED_MSG_NODE_ID");
	if (ptr)
		msg_target_node_id = atoi(ptr);
}

extern void join_extern_threads()
{
	int thread_cnt;

	slurm_mutex_lock(&extern_thread_lock);
	slurm_cond_broadcast(&extern_thread_cond);
	thread_cnt = extern_thread_cnt;
	slurm_mutex_unlock(&extern_thread_lock);

	for (int i = 0; i < thread_cnt; i++) {
		debug2("Joining extern pid thread %d", i);
		slurm_thread_join(extern_threads[i]);
	}

	slurm_mutex_lock(&extern_thread_lock);
	xfree(extern_threads);
	extern_thread_cnt = 0;
	slurm_mutex_unlock(&extern_thread_lock);

	debug2("Done joining extern pid threads");
}

typedef struct {
	uint16_t msg_type;
	bool from_slurmd;
	bool from_job_owner;
	int (*func)(int fd, uid_t uid, pid_t remote_pid);
} slurmstepd_rpc_t;

slurmstepd_rpc_t stepd_rpcs[] = {
	{
		.msg_type = REQUEST_SLUID,
		.func = _handle_sluid,
	},
	{
		.msg_type = REQUEST_SIGNAL_CONTAINER,
		.from_job_owner = true,
		.func = _handle_signal_container,
	},
	{
		.msg_type = REQUEST_STATE,
		.func = _handle_state,
	},
	{
		.msg_type = REQUEST_STEP_MEM_LIMITS,
		.func = _handle_mem_limits,
	},
	{
		.msg_type = REQUEST_STEP_UID,
		.func = _handle_uid,
	},
	{
		.msg_type = REQUEST_STEP_NODEID,
		.func = _handle_nodeid,
	},
	{
		.msg_type = REQUEST_ATTACH,
		.from_slurmd = true,
		.func = _handle_attach,
	},
	{
		.msg_type = REQUEST_GET_BPF_TOKEN,
		.from_slurmd = true,
		.func = _handle_get_bpf_token,
	},
	{
		.msg_type = REQUEST_PID_IN_CONTAINER,
		.func = _handle_pid_in_container,
	},
	{
		.msg_type = REQUEST_DAEMON_PID,
		.func = _handle_daemon_pid,
	},
	{
		.msg_type = REQUEST_STEP_SUSPEND,
		.from_slurmd = true,
		.func = _handle_suspend,
	},
	{
		.msg_type = REQUEST_STEP_RESUME,
		.from_slurmd = true,
		.func = _handle_resume,
	},
	{
		.msg_type = REQUEST_STEP_TERMINATE,
		.from_job_owner = true,
		.func = _handle_terminate,
	},
	{
		.msg_type = REQUEST_STEP_COMPLETION,
		.from_slurmd = true,
		.func = _handle_completion,
	},
	{
		.msg_type = REQUEST_STEP_TASK_INFO,
		.func = _handle_task_info,
	},
	{
		.msg_type = REQUEST_STEP_STAT,
		.from_job_owner = true,
		.func = _handle_stat_jobacct,
	},
	{
		.msg_type = REQUEST_STEP_LIST_PIDS,
		.func = _handle_list_pids,
	},
	{
		.msg_type = REQUEST_STEP_RECONFIGURE,
		.from_slurmd = true,
		.func = _handle_reconfig,
	},
	{
		.msg_type = REQUEST_JOB_NOTIFY,
		.from_job_owner = true,
		.func = _handle_notify_job,
	},
	{
		.msg_type = REQUEST_ADD_EXTERN_PID,
		.from_slurmd = true,
		.func = _handle_add_extern_pid,
	},
	{
		.msg_type = REQUEST_X11_DISPLAY,
		.from_job_owner = true,
		.func = _handle_x11_display,
	},
	{
		.msg_type = REQUEST_GETPW,
		.func = _handle_getpw,
	},
	{
		.msg_type = REQUEST_GETGR,
		.func = _handle_getgr,
	},
	{
		.msg_type = REQUEST_GET_NS_FD,
		.from_job_owner = true,
		.func = _handle_get_ns_fd,
	},
	{
		.msg_type = REQUEST_GET_NS_FDS,
		.from_job_owner = true,
		.func = _handle_get_ns_fds,
	},
	{
		.msg_type = REQUEST_GETHOST,
		.func = _handle_gethost,
	},
	{
		/* terminate the array. this must be last. */
		.msg_type = 0,
		.func = NULL,
	}
};

slurmstepd_rpc_t stepd_proxy_rpcs[] = {
	{
		.msg_type = REQUEST_JOB_STEP_CREATE,
		.func = _handle_step_create,
	},
	{
		.msg_type = REQUEST_JOB_STEP_INFO,
		.func = _handle_job_step_get_info,
	},
	{
		.msg_type = REQUEST_CANCEL_JOB_STEP,
		.func = _handle_cancel_job_step,
	},
	{
		.msg_type = SRUN_JOB_COMPLETE,
		.func = _handle_srun_job_complete,
	},
	{
		.msg_type = SRUN_NODE_FAIL,
		.func = _handle_srun_node_fail,
	},
	{
		.msg_type = SRUN_TIMEOUT,
		.func = _handle_srun_timeout,
	},
	{
		.msg_type = REQUEST_UPDATE_JOB_STEP,
		.func = _handle_update_step,
	},
	{
		.msg_type = REQUEST_STEP_LAYOUT,
		.func = _handle_step_layout,
	},
	{
		.msg_type = REQUEST_JOB_SBCAST_CRED,
		.func = _handle_job_sbcast_cred,
	},
	{
		.msg_type = REQUEST_HET_JOB_ALLOC_INFO,
		.func = _handle_het_job_alloc_info,
	},
	{
		/* terminate the array. this must be last. */
		.msg_type = 0,
		.func = NULL,
	}
};

static int _handle_request(int fd, uid_t uid, pid_t remote_pid)
{
	slurmstepd_rpc_t *this_rpc = NULL;
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

	debug("Handling %s", rpc_num2string(req));

	for (this_rpc = stepd_rpcs; this_rpc->msg_type; this_rpc++) {
		if (this_rpc->msg_type == req)
			break;
	}

	/* Check through proxy RPCs if we're an extern step running stepmgr */
	if (!this_rpc->msg_type && job_step_ptr) {
		for (this_rpc = stepd_proxy_rpcs; this_rpc->msg_type;
		     this_rpc++) {
			if (this_rpc->msg_type == req)
				break;
		}

		/* all proxy rpcs must come through slurmd */
		if (this_rpc->msg_type && !_slurm_authorized_user(uid)) {
			error("Rejecting proxied %s from uid %u",
			      rpc_num2string(req), uid);
			return EPERM;
		}
	}

	if (!this_rpc->msg_type) {
		error("Unrecognized request: %d", req);
		return SLURM_ERROR;
	}

	if ((this_rpc->from_slurmd && !_slurm_authorized_user(uid)) ||
	    (this_rpc->from_job_owner && (uid != step->uid) &&
	     !_slurm_authorized_user(uid))) {
		error("Rejecting %s from uid %u", rpc_num2string(req), uid);
		return EPERM;
	}

	return this_rpc->func(fd, uid, remote_pid);
}
