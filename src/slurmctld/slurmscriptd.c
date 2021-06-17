/*****************************************************************************\
 *  slurmscriptd.c - Slurm script functions.
 *****************************************************************************
 *  Copyright (C) 2021 SchedMD LLC.
 *  Written by Marshall Garey <marshall@schedmd.com>
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

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include "slurm/slurm_errno.h"

#include "src/common/eio.h"
#include "src/common/fd.h"
#include "src/common/log.h"
#include "src/common/xmalloc.h"
#include "src/slurmctld/slurmscriptd.h"

/* Constants */
enum {
	SLURMSCRIPTD_REQUEST_RUN_PROLOG,
	SLURMSCRIPTD_REQUEST_PROLOG_COMPLETE,
	SLURMSCRIPTD_SHUTDOWN,
};

/* Function prototypes */
static bool _msg_readable(eio_obj_t *obj);
static int _msg_accept(eio_obj_t *obj, List objs);

/* Global variables */
struct io_operations msg_ops = {
	.readable = _msg_readable,
	.handle_read = _msg_accept,
};

static int slurmctld_readfd = -1;
static int slurmctld_writefd = -1;
static int slurmscriptd_readfd = -1;
static int slurmscriptd_writefd = -1;
static pid_t slurmscriptd_pid;
static eio_handle_t *msg_handle = NULL;
static pthread_t slurmctld_listener_tid;
static pthread_mutex_t write_mutex = PTHREAD_MUTEX_INITIALIZER;

static bool _msg_readable(eio_obj_t *obj)
{
	debug3("Called %s", __func__);
	if (obj->shutdown) {
		debug2("%s: false, shutdown", __func__);
		return false;
	}
	return true;
}

static int _write_msg(int fd, int req, buf_t *buffer)
{
	int len;

	slurm_mutex_lock(&write_mutex);
	safe_write(fd, &req, sizeof(int));
	if (buffer) {
		len = get_buf_offset(buffer);
		safe_write(fd, &len, sizeof(int));
		safe_write(fd, get_buf_data(buffer), len);
	}
	slurm_mutex_unlock(&write_mutex);

	return SLURM_SUCCESS;

rwfail:
	error("%s: read/write op failed", __func__);
	slurm_mutex_unlock(&write_mutex);
	return SLURM_ERROR;
}

static int _handle_run_prolog(int fd)
{
	int rc, len;
	uint32_t job_id, tmp_size, env_cnt, i;
	char *script, *incoming_buffer;
	char **env;
	buf_t *buffer;

	safe_read(fd, &len, sizeof(int));
	incoming_buffer = xmalloc(len);
	safe_read(fd, incoming_buffer, len);
	buffer = create_buf(incoming_buffer, len);

	safe_unpack32(&job_id, buffer);
	safe_unpackstr_xmalloc(&script, &tmp_size, buffer);
	safe_unpack32(&env_cnt, buffer);
	safe_unpackstr_array(&env, &env_cnt, buffer);

	info("%s: got run prolog for job %u, script:%s",
	     __func__, job_id, script);
	for (i = 0; i < env_cnt; i++) {
		info("%s: env[%u]=%s", __func__, i, env[i]);
	}

	rc = _write_msg(slurmscriptd_writefd,
			SLURMSCRIPTD_REQUEST_PROLOG_COMPLETE, NULL);
	return rc;

unpack_error:
	error("%s: Failed to unpack message", __func__);
	return SLURM_ERROR;

rwfail:
	error("%s: read/write op failed", __func__);
	return SLURM_ERROR;
}

static int _handle_prolog_complete(void)
{
	int rc;

	rc = SLURM_SUCCESS;

	return rc;
}

static int _handle_shutdown(void)
{
	eio_signal_shutdown(msg_handle);

	return SLURM_ERROR; /* Don't handle any more requests. */
}

static int _handle_request(int fd)
{
	int req, rc = SLURM_SUCCESS;

	if ((rc = read(fd, &req, sizeof(int))) != sizeof(int)) {
		if (rc == 0) { /* EOF, normal */
			return -1;
		} else {
			debug3("%s: leaving on read error: %m", __func__);
			return SLURM_ERROR;
		}
	}

	switch (req) {
		case SLURMSCRIPTD_REQUEST_RUN_PROLOG:
			debug2("Handling SLURMSCRIPTD_REQUEST_RUN_PROLOG");
			rc = _handle_run_prolog(fd);
			break;
		case SLURMSCRIPTD_REQUEST_PROLOG_COMPLETE:
			debug2("Handling SLURMSCRIPTD_REQUEST_PROLOG_COMPLETE");
			rc = _handle_prolog_complete();
			break;
		case SLURMSCRIPTD_SHUTDOWN:
			debug2("Handling SLURMSCRIPTD_SHUTDOWN");
			rc = _handle_shutdown();
			break;
		default:
			error("%s: slurmscriptd: Unrecognied request: %d",
			      __func__, req);
			rc = SLURM_ERROR;
			break;
	}

	return rc;
}

static int _msg_accept(eio_obj_t *obj, List objs)
{
	int rc;

	while (true) {
		rc = _handle_request(obj->fd);
		if (rc != SLURM_SUCCESS)
			break;
	}

	return rc;
}

static void _setup_eio(int fd)
{
	eio_obj_t *eio_obj;

	fd_set_nonblocking(fd);

	eio_obj = eio_obj_create(fd, &msg_ops, NULL);
	msg_handle = eio_handle_create(0);
	eio_new_initial_obj(msg_handle, eio_obj);
}

static void _slurmscriptd_mainloop(void)
{
	_setup_eio(slurmscriptd_readfd);

	debug("%s: started", __func__);
	eio_handle_mainloop(msg_handle);
	debug("%s: finished", __func__);
}

static void *_slurmctld_listener_thread(void *x)
{
	debug("%s: started listening to slurmscriptd", __func__);
	eio_handle_mainloop(msg_handle);
	debug("%s: finished", __func__);

	return NULL;
}

static void _kill_slurmscriptd(void)
{
	int status;

	if (slurmscriptd_pid <= 0) {
		error("%s: slurmscriptd_pid < 0, we don't know the PID of slurmscriptd.",
		      __func__);
		return;
	}

	if (kill(slurmscriptd_pid, SIGTERM) < 0) {
		error("%s: kill failed when trying to SIGTERM slurmscriptd: %m",
		      __func__);
	}
	if (waitpid(slurmscriptd_pid, &status, 0) < 0) {
		if (WIFEXITED(status)) {
			/* Exited normally. */
		} else {
			error("%s: Unable to reap slurmscriptd child process", __func__);
		}
	}
}

extern void slurmscriptd_run_prepilog(uint32_t job_id, bool is_epilog,
				      char *script, char **env)
{
	buf_t *buffer;
	uint32_t env_var_cnt = 0;

	info("%s: jobid:%u, is_epilog:%u, script:%s",
	     __func__, job_id, is_epilog, script);
	if (is_epilog)
		return; // Not testing epilog right now

	buffer = init_buf(0);
	pack32(job_id, buffer);
	packstr(script, buffer);
	/*
	 * Pack the environment. We don't know how many environment variables
	 * there are, but we need to pack the number of environment variables
	 * so we know how to unpack. So we have to loop env twice: once
	 * to get the number of environment variables so we can pack that first,
	 * then again to pack the environment.
	 */
	while (env && env[env_var_cnt])
		env_var_cnt++;
	pack32(env_var_cnt, buffer);
	if (env_var_cnt)
		packstr_array(env, env_var_cnt, buffer);

	_write_msg(slurmctld_writefd, SLURMSCRIPTD_REQUEST_RUN_PROLOG, buffer);
	FREE_NULL_BUFFER(buffer);
}

extern int slurmscriptd_init(void)
{
	int to_slurmscriptd[2] = {-1, -1};
	int to_slurmctld[2] = {-1, -1};

	if ((pipe(to_slurmscriptd) < 0) || (pipe(to_slurmctld) < 0))
		fatal("%s: pipe failed: %m", __func__);

	slurmctld_readfd = to_slurmctld[0];
	slurmctld_writefd = to_slurmscriptd[1];
	slurmscriptd_readfd = to_slurmscriptd[0];
	slurmscriptd_writefd = to_slurmctld[1];

	slurmscriptd_pid = fork();
	if (slurmscriptd_pid < 0) { /* fork() failed */
		fatal("%s: fork() failed: %m", __func__);
	} else if (slurmscriptd_pid > 0) { /* parent (slurmctld) */
		ssize_t i;
		int rc = SLURM_ERROR, ack;

		/*
		 * Communication between slurmctld and slurmscriptd happens via
		 * the to_slurmscriptd and to_slurmctld pipes.
		 * slurmctld writes data to slurmscriptd with the
		 * to_slurmscriptd pipe and slurmscriptd writes data to
		 * slurmctld with the to_slurmctld pipe.
		 */
		if (close(to_slurmscriptd[0]) < 0) {
			_kill_slurmscriptd();
			fatal("%s: slurmctld: Unable to close read to_slurmscriptd in parent: %m",
			      __func__);
		}
		if (close(to_slurmctld[1]) < 0) {
			_kill_slurmscriptd();
			fatal("%s: slurmctld: Unable to close write to_slurmctld in parent: %m",
			      __func__);
		}

		/* Test communications with slurmscriptd. */
		i = read(slurmctld_readfd, &rc, sizeof(int));
		if (i < 0) {
			_kill_slurmscriptd();
			fatal("%s: slurmctld: Can not read return code from slurmscriptd: %m",
			      __func__);
		} else if (i != sizeof(int)) {
			_kill_slurmscriptd();
			fatal("%s: slurmctld: slurmscriptd failed to send return code: %m",
			      __func__);
		}
		if (rc != SLURM_SUCCESS) {
			_kill_slurmscriptd();
			fatal("%s: slurmctld: slurmscriptd did not initialize",
			      __func__);
		}
		ack = SLURM_SUCCESS;
		i = write(slurmctld_writefd, &ack, sizeof(int));
		if (i != sizeof(int)) {
			_kill_slurmscriptd();
			fatal("%s: slurmctld: failed to send ack to slurmscriptd: %m",
			      __func__);
		}

		slurm_mutex_init(&write_mutex);
		_setup_eio(slurmctld_readfd);
		slurm_thread_create(&slurmctld_listener_tid,
				    _slurmctld_listener_thread, NULL);
		debug("slurmctld: slurmscriptd fork()'d and initialized.");
	} else { /* child (slurmscriptd_pid == 0) */
		ssize_t i;
		int rc = SLURM_ERROR, ack;
		/* Close extra fd's. */
		if (close(to_slurmscriptd[1]) < 0) {
			error("%s: slurmscriptd: Unable to close write to_slurmscriptd in child: %m",
			      __func__);
			_exit(1);
		}
		if (close(to_slurmctld[0]) < 0) {
			error("%s: slurmscriptd: Unable to close read to_slurmctld in child: %m",
			      __func__);
			_exit(1);
		}
		/* Test communiations with slurmctld. */
		ack = SLURM_SUCCESS;
		i = write(slurmscriptd_writefd, &ack, sizeof(int));
		if (i != sizeof(int)) {
			error("%s: slurmscriptd: failed to send return code to slurmctld: %m",
			      __func__);
			_exit(1);
		}
		i = read(slurmscriptd_readfd, &rc, sizeof(int));
		if (i < 0) {
			error("%s: slurmscriptd: Can not read ack from slurmctld: %m",
			      __func__);
			_exit(1);
		} else if (i != sizeof(int)) {
			error("%s: slurmscriptd: slurmctld failed to send ack: %m",
			      __func__);
			_exit(1);
		}

		debug("slurmscriptd: Got ack from slurmctld, initialization successful");
		slurm_mutex_init(&write_mutex);
		_slurmscriptd_mainloop();
		/* We never want to return from here, only exit. */
		_exit(0);
	}

	return SLURM_SUCCESS;
}

extern int slurmscriptd_fini(void)
{
	debug("%s starting", __func__);
	eio_signal_shutdown(msg_handle);
	_write_msg(slurmctld_writefd, SLURMSCRIPTD_SHUTDOWN, NULL);
	_kill_slurmscriptd();
	pthread_join(slurmctld_listener_tid, NULL);
	slurm_mutex_destroy(&write_mutex);
	(void) close(slurmctld_writefd);
	(void) close(slurmctld_readfd);

	debug("%s complete", __func__);

	return SLURM_SUCCESS;
}
