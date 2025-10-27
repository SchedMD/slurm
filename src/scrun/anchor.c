/*****************************************************************************
 *  anchor.c - Slurm scrun anchor handlers
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
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

#include "config.h"

#define _GNU_SOURCE /* posix_openpt() */

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/param.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "slurm/slurm.h"

#include "src/common/daemonize.h"
#include "src/common/env.h"
#include "src/common/fd.h"
#include "src/common/http.h"
#include "src/common/log.h"
#include "src/common/net.h"
#include "src/common/pack.h"
#include "src/common/read_config.h"
#include "src/common/setproctitle.h"
#include "src/common/spank.h"
#include "src/common/uid.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/conmgr/conmgr.h"

#include "src/scrun/scrun.h"

#define THREAD_COUNT 3

static void _open_pty();
static int _kill_job(conmgr_fd_t *con, int signal);
static void _notify_started(void);
static void _try_start(void);
static pid_t _daemonize(bool new_session);

#define BLOCKING_REQ_MAGIC 0xa13ab9fa

typedef struct {
	int magic;
	conmgr_fd_t *con;
	slurm_msg_t *req_msg;
} blocking_req_t;

static void _free_block_req_t(void *x)
{
	blocking_req_t *args = x;

	xassert(args->magic == BLOCKING_REQ_MAGIC);
	args->magic = ~BLOCKING_REQ_MAGIC;

	/* con is owned by conmgr */
	/* state is owned by caller */
	slurm_free_msg(args->req_msg);
}

static int _queue_start_request(conmgr_fd_t *con, slurm_msg_t *req_msg)
{
	blocking_req_t *args = xmalloc(sizeof(*args));
	args->magic = BLOCKING_REQ_MAGIC;
	args->con = con;
	args->req_msg = req_msg;

	debug("%s: [%s] queued start request",
	      __func__, conmgr_fd_get_name(con));

	write_lock_state();
	if (!state.start_requests)
		state.start_requests = list_create(_free_block_req_t);

	list_append(state.start_requests, args);
	unlock_state();

	_try_start();

	return SLURM_SUCCESS;
}

static int _queue_delete_request(conmgr_fd_t *con, slurm_msg_t *req_msg)
{
	blocking_req_t *args = xmalloc(sizeof(*args));
	args->magic = BLOCKING_REQ_MAGIC;
	args->con = con;
	args->req_msg = req_msg;

	debug("%s: [%s] queued delete request",
	      __func__, conmgr_fd_get_name(con));

	write_lock_state();
	if (!state.delete_requests)
		state.delete_requests = list_create(_free_block_req_t);

	list_append(state.delete_requests, args);
	unlock_state();

	return SLURM_SUCCESS;
}

static void _on_pty_reply_sent(conmgr_callback_args_t conmgr_args, void *arg)
{
	conmgr_fd_t *con = conmgr_args.con;
	int fd;

	read_lock_state();
	fd = state.pts;
	unlock_state();

	debug("%s: [%s] sending fd:%u", __func__, conmgr_fd_get_name(con), fd);

	/* this is a blocking operation */
	send_fd_over_socket(conmgr_fd_get_output_fd(con), fd);
}

static int _send_pty(conmgr_fd_t *con, slurm_msg_t *req_msg)
{
	slurm_msg_t *msg;
	return_code_msg_t *rc_msg;
	int rc = SLURM_SUCCESS;

	msg = xmalloc(sizeof(*msg));
	rc_msg = xmalloc(sizeof(*rc_msg));
	slurm_msg_t_init(msg);
	slurm_msg_set_r_uid(msg, SLURM_AUTH_UID_ANY);
	msg->msg_type = RESPONSE_CONTAINER_PTY;
	msg->protocol_version = req_msg->protocol_version;
	msg->data = rc_msg;
	rc_msg->return_code = rc;
	rc = conmgr_queue_write_msg(con, msg);
	slurm_free_msg(msg);

	debug("%s: [%s] requested pty", __func__, conmgr_fd_get_name(con));

	conmgr_add_work_con_write_complete_fifo(con, _on_pty_reply_sent, NULL);

	return rc;
}

static void _daemonize_logs()
{
	/*
	 * Default to syslog since scrun anchor should only ever be run in
	 * foreground while debugging issues.
	 */
	xassert(!state.needs_lock);

	log_fac = SYSLOG_FACILITY_DAEMON;

	if (oci_conf->debug_flags) {
		debug("%s: overriding debugflags=0x%"PRIx64,
			      __func__, oci_conf->debug_flags);
		slurm_conf.debug_flags = oci_conf->debug_flags;
	}

	if (oci_conf->syslog_log_level) {
		log_opt.syslog_level = oci_conf->syslog_log_level;

		debug("%s: overriding syslog debug level=%s",
		      __func__, log_num2string(log_opt.syslog_level));
	}

	if (oci_conf->stdio_log_level) {
		log_opt.stderr_level = oci_conf->stdio_log_level;

		debug("%s: overriding stdio debug level=%s",
		      __func__, log_num2string(log_opt.stderr_level));
	}

	if (oci_conf->file_log_level) {
		log_opt.logfile_level = oci_conf->file_log_level;

		debug("%s: overriding logfile debug level=%s",
		      __func__, log_num2string(log_opt.logfile_level));
	}

	update_logging();
}

static void _tear_down(conmgr_callback_args_t conmgr_args, void *arg)
{
	bool need_kill = false, need_stop = false;
	int rc = SLURM_SUCCESS;

	xassert(!arg);

	read_lock_state();
	if (state.status >= CONTAINER_ST_STOPPED) {
		debug("%s: ignoring request", __func__);
		unlock_state();
		return;
	}

	need_kill = (state.status == CONTAINER_ST_RUNNING);
	need_stop = state.status < CONTAINER_ST_STOPPING;
	unlock_state();

	/* user has requested a tear down so assume success here */
	if (need_stop)
		stop_anchor(SLURM_SUCCESS);

	if (need_kill)
		rc = _kill_job(NULL, SIGKILL);

	if (!rc)
		stop_anchor(rc);
}

static int _send_delete_confirmation(void *x, void *arg)
{
	blocking_req_t *req = x;
	slurm_msg_t *msg;
	return_code_msg_t *rc_msg;

	xassert(req->magic == BLOCKING_REQ_MAGIC);

	debug("%s: [%s] sending delete confirmation",
	      __func__, conmgr_fd_get_name(req->con));

	/*
	 * either the container is already dead or kill will handle it.
	 * the process is async so there isn't much to do here.
	 */
	msg = xmalloc(sizeof(*msg));
	rc_msg = xmalloc(sizeof(*rc_msg));
	slurm_msg_t_init(msg);
	slurm_msg_set_r_uid(msg, SLURM_AUTH_UID_ANY);
	msg->msg_type = RESPONSE_CONTAINER_DELETE;
	msg->protocol_version = req->req_msg->protocol_version;
	msg->data = rc_msg;
	rc_msg->return_code = SLURM_SUCCESS;
	conmgr_queue_write_msg(req->con, msg);
	slurm_free_msg(msg);

	conmgr_queue_close_fd(req->con);

	return SLURM_SUCCESS;
}

/* stopping job is async: this is the final say if the job has stopped */
static void _check_if_stopped(conmgr_callback_args_t conmgr_args, void *arg)
{
	int ptm = -1;
	bool stopped = false;
	char *pid_file = NULL, *spool_dir = NULL, *anchor_socket = NULL;
	int pid_file_fd = -1;
	list_t *delete_requests;

	xassert(!arg);

	read_lock_state();
	debug2("%s: status=%s job_completed=%c staged_out=%c",
	       __func__, slurm_container_status_to_str(state.status),
	       (state.job_completed ? 'T' : 'F'),
	       (state.staged_out ? 'T' : 'F'));

	if (state.status >= CONTAINER_ST_STOPPED) {
		/* do nothing */
	} else if (state.job_completed && state.staged_out &&
		   (state.status >= CONTAINER_ST_STARTING)) {
		/* something else may have got past stopped */
		if (state.status == CONTAINER_ST_STOPPING)
			stopped = true;
	}
	unlock_state();

	if (!stopped)
		return;

	debug3("%s: I wish they'd just wipe out the container and get it over with. It's the waiting I can't stand.",
	       __func__);

	delete_requests = list_create(_free_block_req_t);

	write_lock_state();
	ptm = state.ptm;
	change_status_locked(CONTAINER_ST_STOPPED);
	if (state.delete_requests)
		list_transfer(delete_requests, state.delete_requests);
	unlock_state();

	list_for_each(delete_requests, _send_delete_confirmation, NULL);
	FREE_NULL_LIST(delete_requests);

	/* final cleanup from stopping */
	if (ptm != -1) {
		int tty;

		if ((ptm > STDERR_FILENO) && close(ptm))
			error("%s: PTM close(%d) failed: %m", __func__, ptm);

		if ((tty = open("/dev/tty", O_RDWR) >= 0)) {
			/* notify client's tty we are done */
			debug3("%s: calling TIOCNOTTY on /dev/tty", __func__);
			if (ioctl(tty, TIOCNOTTY, 0) == -1)
				debug("%s: TIOCNOTTY(%d) failed: %m",
				      __func__, tty);
			close(tty);
		}
	}

	debug2("%s: cleaning up temporary files", __func__);

	write_lock_state();
	/* pid file should already be cleared but make sure here */
	SWAP(pid_file, state.pid_file);
	SWAP(pid_file_fd, state.pid_file_fd);
	SWAP(spool_dir, state.spool_dir);
	SWAP(anchor_socket, state.anchor_socket);
	unlock_state();

	/* conmgr will unlink anchor_socket at shutdown */
	if (pid_file && unlink(pid_file))
		debug("%s: unable to unlink(%s): %m", __func__, pid_file);
	if ((pid_file_fd != -1) && ftruncate(pid_file_fd, 0))
		error("%s: unable to ftruncate(%d): %m", __func__, pid_file_fd);
	if ((pid_file_fd != -1) && close(pid_file_fd))
		debug("%s: unable to close(%d): %m", __func__, pid_file_fd);
	if (spool_dir && rmdir(spool_dir))
		debug("%s: unable to rmdir(%s): %m", __func__, spool_dir);

#ifdef MEMORY_LEAK_DEBUG
	xfree(anchor_socket);
	xfree(pid_file);
	xfree(spool_dir);
#endif /* MEMORY_LEAK_DEBUG */

	debug2("%s: Goodbye, cruel velvet drapes!", __func__);
	conmgr_request_shutdown();
}

static void _finish_job(conmgr_callback_args_t conmgr_args, void *arg)
{
	int rc;
	slurm_step_id_t step_id;
	bool existing_allocation;

	xassert(!arg);

	read_lock_state();
	xassert(state.status >= CONTAINER_ST_STOPPING);
	xassert(!state.job_completed);

	step_id = state.step_id;
	rc = state.srun_rc;
	existing_allocation = state.existing_allocation;
	unlock_state();

	if (existing_allocation) {
		debug("%s: skipping slurm_complete_job(%pI)",
		      __func__, &step_id);
		goto done;
	} else if (step_id.job_id == NO_VAL) {
		debug("%s: no Job to complete", __func__);
		return;
	}

	rc = slurm_complete_job(&step_id, rc);
	if ((rc == SLURM_ERROR) && errno)
		rc = errno;

	if (rc == ESLURM_ALREADY_DONE) {
		debug("%s: %pI already complete", __func__, &step_id);
	} else if (rc) {
		error("%s: slurm_complete_job(%pI) failed: %s",
		      __func__, &step_id, slurm_strerror(rc));
	} else {
		debug("%s: %pI released successfully", __func__, &step_id);
	}

done:
	write_lock_state();
	xassert(!state.job_completed);
	state.job_completed = true;
	unlock_state();

	conmgr_add_work_fifo(_check_if_stopped, NULL);
}

static void _stage_out(conmgr_callback_args_t conmgr_args, void *arg)
{
	int rc;
	bool staged_in;

	xassert(!arg);

	read_lock_state();
	xassert(state.status >= CONTAINER_ST_STOPPING);
	xassert(!state.staged_out);
	debug("%s: BEGIN container %s staging out", __func__, state.id);
	staged_in = state.staged_in;
	unlock_state();

	if (staged_in) {
		rc = stage_out();
	} else {
		rc = SLURM_SUCCESS;
		debug("%s: skipping stage_out() due to stage_in() never running",
		      __func__);
	}

	if (get_log_level() >= LOG_LEVEL_DEBUG) {
		read_lock_state();
		debug("%s: END container %s staging out: %s",
		      __func__, state.id, slurm_strerror(rc));
		unlock_state();
	}

	write_lock_state();
	xassert(!state.staged_out);
	state.staged_out = true;
	unlock_state();

	conmgr_add_work_fifo(_finish_job, NULL);
}

/* cleanup anchor and shutdown */
extern void stop_anchor(int status)
{
	debug2("%s: begin", __func__);

	write_lock_state();
	if (state.status > CONTAINER_ST_STOPPING) {
		unlock_state();
		debug2("%s: already stopped", __func__);
		return;
	}
	if (state.status == CONTAINER_ST_STOPPING) {
		unlock_state();
		debug2("%s: waiting for already running stop request",
		       __func__);
		return;
	}

	change_status_locked(CONTAINER_ST_STOPPING);

	xassert(!state.srun_exited);
	xassert(!state.srun_rc);
	state.srun_exited = true;
	state.srun_rc = status;

	xassert(!state.job_completed);
	xassert(!state.staged_out);

	if (state.startup_con) {
		int rc;

		debug4("%s: sending pid %"PRIu64" to parent due to container stopped in state %s",
		      __func__, (uint64_t) state.pid,
		      slurm_container_status_to_str(state.status));

		/* send the pid now since due to failure */
		if ((rc = conmgr_queue_write_data(state.startup_con, &state.pid,
						  sizeof(state.pid))))
			fatal("%s: unable to send pid: %s",
			      __func__, slurm_strerror(rc));

		conmgr_queue_close_fd(state.startup_con);
	}
	unlock_state();

	conmgr_add_work_fifo(_stage_out, NULL);

	debug2("%s: end", __func__);
}

static void _catch_sigchld(conmgr_callback_args_t conmgr_args, void *arg)
{
	pid_t pid;
	pid_t srun_pid;
	/* we are acting like this is atomic - it is only for logging */
	static uint32_t reaped = 0;

	xassert(arg == &state);

	debug("%s: caught SIGCHLD", __func__);

	write_lock_state();
	srun_pid = state.srun_pid;
	unlock_state();

	if (!srun_pid) {
		debug("%s: ignoring SIGCHLD before srun started",
		      __func__);
		return;
	}

	debug("%s: processing SIGCHLD: finding all anchor children (pid=%"PRIu64")",
	      __func__, (uint64_t) getpid());

	do {
		int status = 0;
		pid = waitpid(-1, &status, WNOHANG);

		if (pid < 0) {
			if (errno == ECHILD)
				debug("%s: got SIGCHLD with no child processes", __func__);
			else
				error("%s: waitpid(-1) failed: %m", __func__);

			break;
		}

		reaped++;

		if (pid == srun_pid) {
			if (WIFEXITED(status)) {
				debug("%s: srun[%d] exited with rc=0x%x",
				      __func__, pid, WEXITSTATUS(status));
			} else if (WIFSIGNALED(status)) {
				debug("%s: srun[%d] killed by signal %s[%d]",
				      __func__, pid,
				      strsignal(WTERMSIG(status)),
				      WTERMSIG(status));
			} else {
				debug("%s: srun[%d] exited rc=0x%x",
				      __func__, pid, status);
			}

			stop_anchor(status);
		} else if (get_log_level() >= LOG_LEVEL_DEBUG) {
			if (!pid) {
				debug("%s: done reaping %u child processes",
				      __func__, reaped);
			} else if (WIFEXITED(status)) {
				debug("%s; child[%d] exited with rc=0x%x",
				      __func__, pid, WEXITSTATUS(status));
			} else if (WIFSIGNALED(status)) {
				debug("%s: child[%d] killed by signal %s[%d]",
				      __func__, pid,
				      strsignal(WTERMSIG(status)),
				      WTERMSIG(status));
			}
		}
	} while (pid > 0);
}

static void *_on_cs_connection(conmgr_fd_t *con, void *arg)
{
	int tty;
	xassert(!arg);

	read_lock_state();
	/* containerd expects the PTM */
	tty = state.ptm;

	if (state.status >= CONTAINER_ST_STOPPED) {
		error("%s: skipping sending console_socket due container %s status %s",
		      __func__, state.id,
		      slurm_container_status_to_str(state.status));
		unlock_state();
		/* NULL return will close the connection */
		return NULL;
	}
	unlock_state();

	debug2("%s: [%s] sending fd:%d",
	       __func__, conmgr_fd_get_name(con), tty);

	xassert(tty != -1);
	xassert(isatty(tty));

	/* hand over pty to console_socket */
	/* WARNING: blocking call */
	errno = 0;
	send_fd_over_socket(conmgr_fd_get_output_fd(con), tty);
	debug2("%s: [%s] sent fd:%d rc:%m",
	       __func__, conmgr_fd_get_name(con), tty);

	/* only sending the fd */
	conmgr_queue_close_fd(con);

	return &state;
}

static int _on_cs_data(conmgr_fd_t *con, void *arg)
{
	xassert(!arg);

	debug3("%s", __func__);

	read_lock_state();
	error("%s: unexpectedly sent data via console_socket %s for container %s status=%s",
	      __func__, state.console_socket, state.id,
	      slurm_container_status_to_str(state.status));
	unlock_state();

	return EINVAL;
}

static void _on_cs_finish(conmgr_fd_t *con, void *arg)
{
	xassert(arg == &state);
	check_state();

	debug3("%s", __func__);
}

static void _queue_send_console_socket(void)
{
	int rc;
	static const conmgr_events_t events = {
		.on_connection = _on_cs_connection,
		.on_data = _on_cs_data,
		.on_finish = _on_cs_finish,
	};
	struct sockaddr_un addr = {
		.sun_family = AF_UNIX,
	};
	int fd = socket(AF_UNIX, SOCK_STREAM, 0);

	fd_set_nonblocking(fd);
	fd_set_close_on_exec(fd);

	if (strlcpy(addr.sun_path, state.console_socket,
		    sizeof(addr.sun_path)) != strlen(state.console_socket))
		fatal("console socket address too long: %s",
		      state.console_socket);

	if ((connect(fd, (struct sockaddr *) &addr, sizeof(addr))) < 0)
		fatal("%s: [%s] Unable to connect() to console socket: %m",
		      __func__, addr.sun_path);

	if ((rc = conmgr_process_fd(CON_TYPE_RAW, fd, fd, &events, CON_FLAG_NONE,
				    (slurm_addr_t *) &addr, sizeof(addr),
				    NULL, NULL)))
		fatal("%s: [%s] unable to initialize console socket: %s",
		      __func__, addr.sun_path, slurm_strerror(rc));

	debug("%s: queued up console socket %s to send pty",
	      __func__, addr.sun_path);
}

static int _send_start_response(conmgr_fd_t *con, slurm_msg_t *req_msg, int rc)
{
	slurm_msg_t *msg;
	container_started_msg_t *st_msg;

	/* respond with rc */
	msg = xmalloc(sizeof(*msg));
	st_msg = xmalloc(sizeof(*st_msg));
	slurm_msg_t_init(msg);
	slurm_msg_set_r_uid(msg, SLURM_AUTH_UID_ANY);
	msg->msg_type = RESPONSE_CONTAINER_START;
	msg->protocol_version = req_msg->protocol_version;
	msg->data = st_msg;
	st_msg->rc = rc;

	read_lock_state();
	st_msg->step_id = state.step_id;
	unlock_state();

	st_msg->step_id.step_id = 0;
	st_msg->step_id.step_het_comp = NO_VAL;
	rc = conmgr_queue_write_msg(con, msg);
	slurm_free_msg(msg);

	conmgr_queue_close_fd(con);
	return rc;
}

static int _finish_start_request(void *x, void *arg)
{
	int rc;
	blocking_req_t *req = x;

	check_state();
	xassert(!arg);
	xassert(req->magic == BLOCKING_REQ_MAGIC);

	debug("%s: [%s] sending start response",
	      __func__, conmgr_fd_get_name(req->con));

	rc = _send_start_response(req->con, req->req_msg, SLURM_SUCCESS);

	return (rc ? SLURM_ERROR : SLURM_SUCCESS);
}

static void _notify_started(void)
{
	list_t *l = NULL;

	write_lock_state();
	SWAP(l, state.start_requests);
	unlock_state();

	if (!l)
		return;

	list_for_each_ro(l, _finish_start_request, NULL);
	FREE_NULL_LIST(l);
}

static void _try_start(void)
{
	pid_t child = 0;

	write_lock_state();

	if (state.status >= CONTAINER_ST_RUNNING) {
		/* already started: report success */
		unlock_state();
		_notify_started();
		return;
	}

	if (state.status < CONTAINER_ST_CREATED) {
		if (!state.start_requests || !list_count(state.start_requests))
			fatal("%s: start request queue empty", __func__);

		debug("%s: deferring %d start requests while in status:%s",
		      __func__, list_count(state.start_requests),
		      slurm_container_status_to_str(state.status));

		unlock_state();
		return;
	}

	change_status_locked(CONTAINER_ST_STARTING);
	unlock_state();

	if ((child = _daemonize(false))) {
		write_lock_state();
		state.srun_pid = child;
		debug("%s: forked for srun of %s to pid:%"PRIu64,
			__func__, state.id, (uint64_t) state.srun_pid);
		change_status_locked(CONTAINER_ST_RUNNING);
		unlock_state();

		_notify_started();
	} else {
		exec_srun_container();
		fatal("should never execute this line");
	}
}

static int _start(conmgr_fd_t *con, slurm_msg_t *req_msg)
{
	container_state_msg_status_t status;

	read_lock_state();
	status = state.status;
	unlock_state();

	/* NOTE: explicitly listing all possible states here */
	switch (status) {
	case CONTAINER_ST_INVALID:
	case CONTAINER_ST_UNKNOWN:
	case CONTAINER_ST_MAX:
		fatal("%s: [%s] start request while in status:%s should never happen",
		      __func__, conmgr_fd_get_name(con),
		      slurm_container_status_to_str(status));
	case CONTAINER_ST_CREATING:
		debug("%s: [%s] start request while in status:%s. Deferring start request until CREATED state.",
		      __func__, conmgr_fd_get_name(con),
		      slurm_container_status_to_str(status));

		return _queue_start_request(con, req_msg);
	case CONTAINER_ST_CREATED:
	{
		debug("%s: [%s] queuing up start request in status:%s",
		      __func__, conmgr_fd_get_name(con),
		      slurm_container_status_to_str(status));

		/* at created and we just changed state to STARTING */
		return _queue_start_request(con, req_msg);
	}
	case CONTAINER_ST_STARTING:
	case CONTAINER_ST_RUNNING:
		debug("%s: [%s] ignoring duplicate start request while %s",
		      __func__, conmgr_fd_get_name(con),
		      slurm_container_status_to_str(status));

		return _send_start_response(con, req_msg, SLURM_SUCCESS);
	case CONTAINER_ST_STOPPING:
	case CONTAINER_ST_STOPPED:
		/* already ran? */
		debug("%s: [%s] start request while in status:%s rejected",
		      __func__, conmgr_fd_get_name(con),
		      slurm_container_status_to_str(status));

		/* TODO: maybe this should always be SUCCESS too? */
		return _send_start_response(con, req_msg, ESLURM_ALREADY_DONE);
	}

	fatal("%s: should never get past switch()", __func__);
}

static int _kill_job(conmgr_fd_t *con, int signal)
{
	int rc = SLURM_SUCCESS;
	slurm_step_id_t step_id;
	container_state_msg_status_t status;

	read_lock_state();
	step_id = state.step_id;
	status = state.status;
	unlock_state();

	if ((step_id.job_id != NO_VAL) && (status <= CONTAINER_ST_STOPPING)) {
		rc = slurm_kill_job(step_id.job_id, signal, KILL_FULL_JOB);

		debug("%s: [%s] slurm_kill_job(%pI, Signal[%d]=%s, 0) = %s",
		      __func__, (con ? conmgr_fd_get_name(con) : "self"),
		      &step_id, signal, strsignal(signal), slurm_strerror(rc));
	} else {
		debug("%s: [%s] job already dead",
		      __func__, conmgr_fd_get_name(con));
	}

	return rc;
}

static int _kill(conmgr_fd_t *con, slurm_msg_t *req_msg)
{
	slurm_msg_t *msg;
	return_code_msg_t *rc_msg;
	int rc;
	container_signal_msg_t *sig_msg = req_msg->data;

	xassert(req_msg->msg_type == REQUEST_CONTAINER_KILL);

	debug("%s: [%s] requested signal %s",
	      __func__, conmgr_fd_get_name(con), strsignal(sig_msg->signal));

	rc = _kill_job(con, sig_msg->signal);

	/* respond with rc */
	msg = xmalloc(sizeof(*msg));
	rc_msg = xmalloc(sizeof(*rc_msg));
	slurm_msg_t_init(msg);
	slurm_msg_set_r_uid(msg, SLURM_AUTH_UID_ANY);
	msg->msg_type = RESPONSE_CONTAINER_KILL;
	msg->protocol_version = req_msg->protocol_version;
	msg->data = rc_msg;
	rc_msg->return_code = rc;
	rc = conmgr_queue_write_msg(con, msg);
	slurm_free_msg(msg);

	return rc;
}

static int _delete(conmgr_fd_t *con, slurm_msg_t *req_msg)
{
	int rc = SLURM_SUCCESS;
	container_delete_msg_t *delete_msg = req_msg->data;

	debug("%s: [%s]%s delete requested: %s",
	      __func__, conmgr_fd_get_name(con),
	      (delete_msg->force ? " force" : ""), slurm_strerror(rc));

	rc = _queue_delete_request(con, req_msg);

	conmgr_add_work_fifo(_tear_down, NULL);

	return rc;
}

static void _set_proctitle()
{
	char *thread_name = NULL;

	xassert(!state.needs_lock);

	setproctitle("%s", state.id);
	xstrfmtcat(thread_name, "scrun:%s", state.id);
	if (prctl(PR_SET_NAME, thread_name, NULL, NULL, NULL) < 0) {
		fatal("Unable to set process name");
	}
	xfree(thread_name);
}

static pid_t _daemonize(bool new_session)
{
	int pipe_fd[2];
	pid_t pid;

	if (pipe(pipe_fd))
		fatal("pipe() failed: %m");
	xassert(pipe_fd[0] > STDERR_FILENO);
	xassert(pipe_fd[1] > STDERR_FILENO);

	if ((pid = fork()) == -1)
		fatal("cannot fork: %m");

	log_reinit();

	if (pid) {
		/* nothing else in parent */
		debug("%s: forked off child %"PRIu64,
		      __func__, (uint64_t) pid);

		if (close(pipe_fd[1]))
			fatal("close(%u) failed: %m", pipe_fd[1]);

		safe_read(pipe_fd[0], &pid, sizeof(pid));

		if (close(pipe_fd[0]))
			fatal("close(%u) failed: %m", pipe_fd[0]);

		return pid;
	}

	if (new_session) {
		/* explicitly not calling xdaemon() as it breaks stdio */
		switch (fork()) {
		case 0:
			break; /* child */
		case -1:
			return -1;
		default:
			_exit(0); /* exit parent */
		}

		if (setsid() < 0)
			fatal("setsid() failed: %m");

		switch (fork()) {
		case 0:
			break; /* child */
		case -1:
			return -1;
		default:
			_exit(0); /* exit parent */
		}
	}

	/* avoid deadlocks with logging threads */
	log_reinit();

	pid = getpid();

	if (close(pipe_fd[0]))
		fatal("close(%u) failed: %m", pipe_fd[0]);

	safe_write(pipe_fd[1], &pid, sizeof(pid));

	if (close(pipe_fd[1]))
		fatal("close(%u) failed: %m", pipe_fd[1]);

	return 0;

rwfail:
	fatal("Unable to send PID to parent: %m");
}

static void _cleanup_pidfile()
{
	xassert(!state.needs_lock);

	if (unlink(state.pid_file) == -1) {
		debug("%s: unable to remove pidfile `%s': %m",
		      __func__, state.pid_file);
	}

	if ((state.pid_file_fd != -1) && (close(state.pid_file_fd) == -1)) {
		debug("%s: unable to close pidfile `%s': %m", __func__,
		      state.pid_file);
	}
	state.pid_file_fd = -1;
}

/*
 * Based on create_pidfile() but does not place newline at end of file
 * to be compatible with docker pidfile parsing. Defers actually writing the
 * pidfile too.
 */
static void _open_pidfile()
{
	int rc = SLURM_SUCCESS;
	xassert(!state.needs_lock);

	if (!state.pid_file)
		return;

	xassert(state.pid_file_fd == -1);
	xassert(state.pid_file != NULL);
	xassert(state.pid_file[0] == '/');
	xassert(state.pid_file[1]);

	state.pid_file_fd = open(state.pid_file,
				  O_CREAT|O_WRONLY|O_TRUNC|O_CLOEXEC,
				  S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
	if (state.pid_file_fd == -1) {
		rc = errno;
		error("%s: unable to open pidfile `%s': %s",
		      __func__, state.pid_file, slurm_strerror(rc));
		/* avoid unlinking pidfile from another scrun */
		return;
	}
	fd_set_close_on_exec(state.pid_file_fd);

	if (fd_get_write_lock(state.pid_file_fd) == -1) {
		rc = errno;
		error("%s: unable to lock pidfile `%s': %s",
		      __func__, state.pid_file, slurm_strerror(rc));
		goto cleanup;
	}

	if (fchown(state.pid_file_fd, getuid(), -1) == -1) {
		rc = errno;
		error("%s: Unable to reset owner of pidfile `%s' to %u: %s",
		      __func__, state.pid_file, getuid(), slurm_strerror(rc));
		goto cleanup;
	}

	debug("%s: opened pid file: %s", __func__, state.pid_file);

	return;

cleanup:
	_cleanup_pidfile();
	fatal("%s: create pidfile %s failed: %s",
	      __func__, state.pid_file, slurm_strerror(rc));
}

static void _populate_pidfile()
{
	int rc = SLURM_SUCCESS;
	char *pid_str = NULL;
	xassert(!state.needs_lock);

	if (!state.pid_file)
		return;

	pid_str = xstrdup_printf("%lu", (unsigned long) state.pid);

	safe_write(state.pid_file_fd, pid_str, strlen(pid_str));
	xfree(pid_str);

	debug("%s: populated pid file: %s", __func__, state.pid_file);

	return;
rwfail:
	rc = errno;
	_cleanup_pidfile();
	xfree(pid_str);

	fatal("%s: populate pidfile %s failed: %s",
	      __func__, state.pid_file, slurm_strerror(rc));
}

extern void on_allocation(conmgr_callback_args_t conmgr_args, void *arg)
{
	bool queue_try_start = false;
	int rc;
	pid_t pid;

	xassert(!arg);

	write_lock_state();
	if (state.step_id.job_id == NO_VAL) {
		unlock_state();
		debug("%s: waiting for job allocation", __func__);
		return;
	}
	if (!state.startup_con) {
		unlock_state();
		debug("%s: waiting for create command connection", __func__);
		return;
	}
	if (state.status != CONTAINER_ST_CREATING) {
		error("%s: can only switch to CREATED from CREATING but current status=%s",
		      __func__, slurm_container_status_to_str(state.status));
		unlock_state();
		return;
	}

	/* created reqs completed */
	change_status_locked(CONTAINER_ST_CREATED);

	if (state.start_requests) {
		debug("%s: %s requesting start as user already requested start",
		      __func__, state.id);
		queue_try_start = true;
	}

	debug("%s: %s created successfully", __func__, state.id);

	pid = getpid();

	/* notify command_create() that container is now CREATED */
	xassert(state.startup_con);
	if ((rc = conmgr_queue_write_data(state.startup_con, &pid,
					  sizeof(pid))))
		fatal("%s: unable to send pid: %s",
		      __func__, slurm_strerror(rc));

	conmgr_queue_close_fd(state.startup_con);
	unlock_state();

	if (queue_try_start)
		_try_start();
}

static void *_on_connection(conmgr_fd_t *con, void *arg)
{
	/* may or may not need to be locked for this one */
	check_state();
	xassert(!arg);

	debug4("%s: [%s] new connection", __func__, conmgr_fd_get_name(con));

	return &state;
}

static void _on_connection_finish(conmgr_fd_t *con, void *arg)
{
	xassert(arg == &state);
	check_state();
}

static int _send_state(conmgr_fd_t *con, slurm_msg_t *req_msg)
{
	int rc;
	container_state_msg_t *state_msg;
	slurm_msg_t *msg;

	check_state();

	msg = xmalloc(sizeof(*msg));
	slurm_msg_t_init(msg);
	slurm_msg_set_r_uid(msg, SLURM_AUTH_UID_ANY);
	msg->msg_type = RESPONSE_CONTAINER_STATE;
	msg->protocol_version = req_msg->protocol_version;

	state_msg = slurm_create_container_state_msg();
	msg->data = state_msg;

	read_lock_state();
	state_msg->oci_version = xstrdup(state.oci_version);
	state_msg->id = xstrdup(state.id);
	state_msg->status = state.status;
	state_msg->pid = state.pid;
	state_msg->bundle = xstrdup(state.bundle);
	state_msg->annotations = list_shallow_copy(state.annotations);

	debug("%s: [%s] sent state with status=%s",
	      __func__, conmgr_fd_get_name(con),
	      slurm_container_status_to_str(state.status));

	rc = conmgr_queue_write_msg(con, msg);

	/* must hold read lock until annotations have been packed */
	unlock_state();

	slurm_free_msg(msg);
	return rc;
}

static int _on_connection_msg(conmgr_fd_t *con, slurm_msg_t *msg, int unpack_rc,
			      void *arg)
{
	int rc;
	uid_t user_id;

	xassert(arg == &state);

	if (unpack_rc || !msg->auth_ids_set) {
		error("%s: [%s] rejecting malformed RPC and closing connection: %s",
		      __func__, conmgr_fd_get_name(con),
		      slurm_strerror(unpack_rc));
		slurm_free_msg(msg);
		return unpack_rc;
	}

	read_lock_state();
	user_id = state.user_id;
	unlock_state();

	/*
	 * TODO: this only allows same user currently but maybe this should
	 * allow root/slurmuser?
	 *
	 * Note: containerd will start runc in a new username space running as
	 * the root user. We must check against the job user instead of
	 * getuid().
	 */
	if (!msg->auth_ids_set) {
		error("%s: [%s] rejecting %s RPC with missing user auth",
		      __func__, conmgr_fd_get_name(con),
		      rpc_num2string(msg->msg_type));
		return SLURM_PROTOCOL_AUTHENTICATION_ERROR;
	} else if (msg->auth_uid != user_id) {
		error("%s: [%s] rejecting %s RPC with user:%u != owner:%u",
		      __func__, conmgr_fd_get_name(con),
		      rpc_num2string(msg->msg_type), msg->auth_uid, user_id);
		return SLURM_PROTOCOL_AUTHENTICATION_ERROR;
	}

	switch (msg->msg_type) {
	case REQUEST_CONTAINER_STATE:
		rc = _send_state(con, msg);
		slurm_free_msg(msg);
		break;
	case REQUEST_CONTAINER_START:
		rc = _start(con, msg);
		/* msg is free'ed later */
		break;
	case REQUEST_CONTAINER_PTY:
		rc = _send_pty(con, msg);
		slurm_free_msg(msg);
		break;
	case REQUEST_CONTAINER_KILL:
		rc = _kill(con, msg);
		slurm_free_msg(msg);
		break;
	case REQUEST_CONTAINER_DELETE:
		rc = _delete(con, msg);
		/* msg is free'ed later */
		break;
	default:
		rc = SLURM_UNEXPECTED_MSG_ERROR;
		error("%s: [%s] unexpected message %u",
		      __func__, conmgr_fd_get_name(con), msg->msg_type);
		slurm_free_msg(msg);
	}

	return rc;
}

static void _adopt_tty(void)
{
	debug("STDIN_FILENO is a tty! requested_terminal=%c",
	      (state.requested_terminal ? 't' : 'f'));

	/* grab size of terminal screen */
	if (ioctl(STDIN_FILENO, TIOCGWINSZ, &state.tty_size))
		fatal("ioctl(TIOCGWINSZ): %m");

	state.pts = STDIN_FILENO;
}

static void _open_pty(void)
{
	int ptm; /* pseudoterminal master (PTM) */
	int pts; /* pseudoterminal slave (PTS) */

	xassert(!state.needs_lock);

	if ((ptm = posix_openpt(O_RDWR|O_NOCTTY)) < 0)
		fatal("posix_openpt() failed: %m");

	/*
	 * Per man pts:
	 * Before opening the pseudoterminal slave, you must pass the
	 * master's file descriptor to grantpt(3) and unlockpt(3).
	 */
	if (grantpt(ptm))
		fatal("%s: Unable to grantpt() pty: %m", __func__);
	if (unlockpt(ptm))
		fatal("%s: Unable to unlockpt() pty: %m", __func__);

	if ((pts = open(ptsname(ptm), O_RDWR)) < 0)
		fatal("%s: Unable to open %s: %m",
		      __func__, ptsname(ptm));

	debug("%s: created pty %s ptm:%d pts:%d",
	      __func__, ptsname(ptm), ptm, pts);

	xassert(state.ptm == -1);
	xassert(state.pts == -1);
	state.ptm = ptm;
	state.pts = pts;
}

static int _on_startup_con_data(conmgr_fd_t *con, void *arg)
{
	xassert(arg == &state);
	check_state();

	fatal("%s: unexpected data", __func__);
}

static void *_on_startup_con(conmgr_fd_t *con, void *arg)
{
	bool queue = false;

	xassert(!arg);

	debug4("%s: [%s] new startup connection",
	       __func__, conmgr_fd_get_name(con));

	write_lock_state();
	xassert(!state.startup_con);
	state.startup_con = con;

	/*
	 * job may already be allocated at this point so see if we need to mark
	 * as created
	 */
	if ((state.status == CONTAINER_ST_CREATING) &&
	    (state.step_id.job_id != NO_VAL) && !state.existing_allocation)
		queue = true;
	unlock_state();

	if (queue) {
		conmgr_add_work_fifo(on_allocation, NULL);
	}

	return &state;
}

static void _on_startup_con_fin(conmgr_fd_t *con, void *arg)
{
	xassert(arg == &state);

	write_lock_state();
	xassert(state.startup_con == con);
	debug4("%s: [%s] create command parent notified of start",
	       __func__, conmgr_fd_get_name(state.startup_con));
	xassert(state.startup_con);
	state.startup_con = NULL;
	unlock_state();

	_try_start();
}

static int _wait_create_pid(int fd, pid_t child)
{
	int rc, status;
	pid_t pid;

	debug("%s: waiting for anchor pid on fd %d from %"PRIu64,
	      __func__, fd, (uint64_t) child);

	safe_read(fd, &pid, sizeof(pid));

	if (close(fd))
		fatal("close(%u) failed: %m", fd);

	debug4("%s: goodbye cruel lamp", __func__);

	if (pid > 0) {
		debug("%s: anchor pid %"PRIu64" ready",
		      __func__, (uint64_t) pid);
		return SLURM_SUCCESS;
	} else {
		debug("%s: received failure signal pid %"PRIi64,
		      __func__, (int64_t) pid);
		goto check_pid;
	}
rwfail:
	rc = errno;
	debug("%s: pipe read(%d) error while waiting for pid from child process %"PRIu64" failed: %s",
	      __func__, fd, (uint64_t) child, slurm_strerror(rc));
check_pid:
	/* check what happened to the child process */
	debug("%s: waiting for anchor process %u to terminate",
	      __func__, child);

	while (((rc = waitpid(child, &status, 0)) < 0) && (errno == EINTR))
		debug("%s: waitpid(%" PRIu64 ") interrupted",
		      __func__, (uint64_t) child);

	if (rc == -1) {
		rc = errno;
		debug("%s: waitpid(%"PRIu64") failed[%d]: %s",
		      __func__, (uint64_t) child, rc, slurm_strerror(rc));
		return rc;
	}

	xassert(child == rc);
	rc = SLURM_SUCCESS;

	debug("anchor %d successfully left session", child);

	if (WIFEXITED(status)) {
		rc = WEXITSTATUS(status);
		debug("%s: anchor %"PRIu64" exited[%d]=%s",
		      __func__, (uint64_t) child, rc, slurm_strerror(rc));
	} else if (WIFSIGNALED(status)) {
		fatal("%s: anchor %"PRIu64" killed by signal %d",
		      __func__, (uint64_t) child, WTERMSIG(status));
	}

	return rc;
}

static int _anchor_child(int pipe_fd[2])
{
	static const conmgr_events_t conmgr_events = {
		.on_msg = _on_connection_msg,
		.on_connection = _on_connection,
		.on_finish = _on_connection_finish,
	};
	static const conmgr_events_t conmgr_startup_events = {
		.on_data = _on_startup_con_data,
		.on_connection = _on_startup_con,
		.on_finish = _on_startup_con_fin,
	};
	list_t *socket_listen = list_create(xfree_ptr);
	int rc, spank_rc;

	state.pid = getpid();
	_populate_pidfile();

	/* must init conmgr after calling fork() in _daemonize() */
	conmgr_init(0, THREAD_COUNT, 0);

	change_status_force(CONTAINER_ST_CREATING);

	if (mkdirpath(state.spool_dir, S_IRWXU, true)) {
		fatal("%s: unable to create spool directory %s: %m",
		      __func__, state.spool_dir);
	} else {
		debug("created: %s", state.spool_dir);
	}

	_daemonize_logs();

	_set_proctitle();

	/* setup new TTY*/
	if (isatty(STDIN_FILENO))
		_adopt_tty();
	else if (state.requested_terminal)
		_open_pty();

	if (state.console_socket && state.console_socket[0])
		_queue_send_console_socket();

	/* scrun anchor process */

	/* TODO: only 1 unix socket for now */
	list_append(socket_listen,
		    xstrdup_printf("unix:%s", state.anchor_socket));
	if ((rc = conmgr_create_listen_sockets(CON_TYPE_RPC, CON_FLAG_NONE,
					       socket_listen, &conmgr_events,
					       NULL)))
		fatal("%s: unable to initialize listeners: %s",
		      __func__, slurm_strerror(rc));
	debug("%s: listening on unix:%s", __func__, state.anchor_socket);

	conmgr_add_work_signal(SIGCHLD, _catch_sigchld, &state);

	if ((rc = conmgr_process_fd(CON_TYPE_RAW, pipe_fd[1], pipe_fd[1],
				    &conmgr_startup_events, CON_FLAG_NONE, NULL,
				    0, NULL, NULL)))
		fatal("%s: unable to initialize RPC listener: %s",
		      __func__, slurm_strerror(rc));

	conmgr_add_work_fifo(get_allocation, NULL);

	if ((spank_rc = spank_init_post_opt())) {
		fatal("%s: plugin stack post-option processing failed: %s",
		      __func__, slurm_strerror(spank_rc));
	}

	/* state must be rwlocked during conmgr_run() */
#ifndef NDEBUG
	slurm_mutex_lock(&state.debug_lock);
	debug4("%s: BEGIN conmgr_run()", __func__);
	xassert(!state.needs_lock);
	xassert(!state.locked);
	state.needs_lock = true;
	slurm_mutex_unlock(&state.debug_lock);
#endif

	rc = conmgr_run(true);

#ifndef NDEBUG
	slurm_mutex_lock(&state.debug_lock);
	xassert(!state.locked);
	xassert(state.needs_lock);
	state.needs_lock = false;
	debug4("%s: END conmgr_run()", __func__);
	slurm_mutex_unlock(&state.debug_lock);
#endif
	FREE_NULL_LIST(socket_listen);
	conmgr_fini();

	return rc;
}

extern int spawn_anchor(void)
{
	int pipe_fd[2] = { -1, -1 };
	pid_t child;
	int rc, spank_rc;

	check_state();

	init_lua();

	if ((rc = spank_init_allocator()))
		fatal("%s: failed to initialize plugin stack: %s",
		      __func__, slurm_strerror(rc));

	if (pipe(pipe_fd))
		fatal("pipe() failed: %m");
	xassert(pipe_fd[0] > STDERR_FILENO);
	xassert(pipe_fd[1] > STDERR_FILENO);

	_open_pidfile();

	if ((child = _daemonize(state.requested_terminal))) {
		if (close(pipe_fd[1]))
			fatal("%s: close pipe failed: %m", __func__);

		rc = _wait_create_pid(pipe_fd[0], child);
		goto done;
	} else
		rc = _anchor_child(pipe_fd);

done:
	spank_rc = spank_fini(NULL);
	destroy_lua();

	debug2("%s: rc[%d]=%s spank_rc[%d]=%s srun_rc[%d]=%s",
	      __func__,
	      rc, slurm_strerror(rc),
	      spank_rc, slurm_strerror(spank_rc),
	      state.srun_rc, slurm_strerror(state.srun_rc));

	if (rc)
		return rc;
	else if (spank_rc)
		return spank_rc;
	else if (state.srun_rc)
		return state.srun_rc;

	return SLURM_SUCCESS;
}
