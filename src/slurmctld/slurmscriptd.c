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

#include "config.h"

#define _GNU_SOURCE	/* For POLLRDHUP */

#if HAVE_SYS_PRCTL_H
#  include <sys/prctl.h>
#endif

#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include "slurm/slurm_errno.h"

#include "src/common/eio.h"
#include "src/common/fd.h"
#include "src/common/log.h"
#include "src/common/setproctitle.h"
#include "src/common/track_script.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/slurmctld/burst_buffer.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/slurmscriptd.h"

#define MAX_POLL_WAIT 500 /* in milliseconds */

/*
 *****************************************************************************
 * The following are meant to be used by both slurmscriptd and slurmctld
 *****************************************************************************
 */

enum {
	SLURMSCRIPTD_REQUEST_RUN_PREPILOG,
	SLURMSCRIPTD_REQUEST_PROLOG_COMPLETE,
	SLURMSCRIPTD_REQUEST_EPILOG_COMPLETE,
	SLURMSCRIPTD_REQUEST_FLUSH,
	SLURMSCRIPTD_REQUEST_FLUSH_COMPLETE,
	SLURMSCRIPTD_REQUEST_FLUSH_JOB,
	SLURMSCRIPTD_REQUEST_RUN_BB_LUA,
	SLURMSCRIPTD_REQUEST_BB_LUA_COMPLETE,
	SLURMSCRIPTD_SHUTDOWN,
};

static bool _msg_readable(eio_obj_t *obj);
static int _msg_accept(eio_obj_t *obj, List objs);
static int _handle_close(eio_obj_t *obj, List objs);

struct io_operations msg_ops = {
	.readable = _msg_readable,
	.handle_read = _msg_accept,
	.handle_close = _handle_close,
};

typedef struct {
	buf_t *buffer;
	int req;
} req_args_t;

static eio_handle_t *msg_handle = NULL;
static pthread_mutex_t write_mutex = PTHREAD_MUTEX_INITIALIZER;

/*
 *****************************************************************************
 * The following are meant to be used by only slurmctld
 *****************************************************************************
 */
typedef struct {
	pthread_cond_t cond;
	char *key;
	pthread_mutex_t mutex;
	int rc;
	char *resp_msg;
	bool track_script_signalled;
} script_response_t;

static int slurmctld_readfd = -1;
static int slurmctld_writefd = -1;
static pid_t slurmscriptd_pid;
static pthread_t slurmctld_listener_tid;
static int script_count = 0;
static pthread_mutex_t script_count_mutex = PTHREAD_MUTEX_INITIALIZER;

static pthread_mutex_t script_resp_map_mutex = PTHREAD_MUTEX_INITIALIZER;
static xhash_t *script_resp_map = NULL;

/*
 *****************************************************************************
 * The following are meant to be used by only slurmscriptd
 *****************************************************************************
 */
static int slurmscriptd_readfd = -1;
static int slurmscriptd_writefd = -1;


/* Function definitions: */

/* Fetch key from xhash_t item. Called from function ptr */
static void _resp_map_key_id(void *item, const char **key, uint32_t *key_len)
{
	script_response_t *lua_resp = (script_response_t *)item;

	xassert(lua_resp);

	*key = lua_resp->key;
	*key_len = strlen(lua_resp->key);
}

/* Free item from xhash_t. Called from function ptr */
static void _resp_map_free(void *item)
{
	script_response_t *script_resp = (script_response_t *)item;

	if (!script_resp)
		return;

	slurm_cond_destroy(&script_resp->cond);
	xfree(script_resp->key);
	slurm_mutex_destroy(&script_resp->mutex);
	xfree(script_resp->resp_msg);
	xfree(script_resp);
}

/* Add an entry to script_resp_map */
static script_response_t *_script_resp_map_add(void)
{
	script_response_t *script_resp;

	script_resp = xmalloc(sizeof *script_resp);
	slurm_cond_init(&script_resp->cond, NULL);
	/*
	 * Use pthread_self() to create a unique identifier for the key.
	 * The caller must ensure that this thread does not end for the
	 * lifetime of the entry in the hashmap. The caller can do this by
	 * calling _wait_for_script_resp() which will block until the response
	 * RPC is received.
	 */
	script_resp->key = xstrdup_printf("%lu", (uint64_t) pthread_self());
	slurm_mutex_init(&script_resp->mutex);
	script_resp->resp_msg = NULL;

	slurm_mutex_lock(&script_resp_map_mutex);
	xhash_add(script_resp_map, script_resp);
	slurm_mutex_unlock(&script_resp_map_mutex);

	return script_resp;
}

static void _script_resp_map_remove(char *key)
{
	slurm_mutex_lock(&script_resp_map_mutex);
	xhash_delete(script_resp_map, key, strlen(key));
	slurm_mutex_unlock(&script_resp_map_mutex);
}

static void _wait_for_script_resp(script_response_t *script_resp,
				  int *status, char **resp_msg,
				  bool *track_script_signalled)
{
	slurm_mutex_lock(&script_resp->mutex);
	slurm_cond_wait(&script_resp->cond, &script_resp->mutex);
	/* The script is done now, and we should have the response */
	*status = script_resp->rc;
	if (resp_msg)
		*resp_msg = xstrdup(script_resp->resp_msg);
	if (track_script_signalled)
		*track_script_signalled = script_resp->track_script_signalled;
	slurm_mutex_unlock(&script_resp->mutex);
}

static int _handle_close(eio_obj_t *obj, List objs)
{
	debug3("Called %s", __func__);

	/*
	 * This happens on normal shutdown, but it also happens when either
	 * slurmctld or slurmscriptd are killed (e.g., by fatal(), SIGKILL)
	 * and then the pipe is closed because the process closed.
	 * If that happens then we want to shutdown instead of run forever.
	 * Also, if this is slurmscriptd, then we want to kill any running
	 * scripts.
	 */
	log_flag(SCRIPT, "close() on pipe");

	obj->shutdown = true;

	if (!running_in_slurmctld()) /* Only do this for slurmscriptd */
		track_script_flush();

	return SLURM_SUCCESS; /* Note: Return value is ignored by eio. */
}

static bool _msg_readable(eio_obj_t *obj)
{
	debug3("Called %s", __func__);
	if (obj->shutdown) {
		log_flag(SCRIPT, "%s: false, shutdown", __func__);
		return false;
	}
	return true;
}

static int _write_msg(int fd, int req, buf_t *buffer)
{
	int len = 0;

	slurm_mutex_lock(&write_mutex);
	safe_write(fd, &req, sizeof(int));
	if (buffer) {
		len = get_buf_offset(buffer);
		safe_write(fd, &len, sizeof(int));
		safe_write(fd, get_buf_data(buffer), len);
	} else /* Write 0 length so the receiver knows not to read anymore */
		safe_write(fd, &len, sizeof(int));
	slurm_mutex_unlock(&write_mutex);

	return SLURM_SUCCESS;

rwfail:
	error("%s: read/write op failed", __func__);
	slurm_mutex_unlock(&write_mutex);
	return SLURM_ERROR;
}

static void _decr_script_cnt(void)
{
	slurm_mutex_lock(&script_count_mutex);
	script_count--;
	slurm_mutex_unlock(&script_count_mutex);
}

static void _incr_script_cnt(void)
{
	slurm_mutex_lock(&script_count_mutex);
	script_count++;
	slurm_mutex_unlock(&script_count_mutex);
}

static int _tot_wait (struct timeval *start_time)
{
	struct timeval end_time;
	int msec_delay;

	gettimeofday(&end_time, NULL);
	msec_delay =   (end_time.tv_sec  - start_time->tv_sec ) * 1000;
	msec_delay += ((end_time.tv_usec - start_time->tv_usec + 500) / 1000);
	return msec_delay;
}

/*
 * Run a script with a given timeout.
 * Return the status or SLURM_ERROR if fork() fails.
 */
static int _run_script(char *script, char **env, uint32_t job_id,
		       char *script_name, int timeout)
{
	pid_t cpid;
	int status = SLURM_ERROR, wait_rc;
	char *argv[2];

	argv[0] = script;
	argv[1] = NULL;

	if ((cpid = fork()) < 0) {
		error("slurmctld_script fork error: %m");
		return status;
	} else if (cpid == 0) {
		/* child process */
		closeall(0);
		setpgid(0, 0);
		execve(argv[0], argv, env);
		_exit(127);
	}

	/* Start tracking this new process */
	track_script_rec_add(job_id, cpid, pthread_self());
	while (1) {
		wait_rc = waitpid_timeout(__func__, cpid, &status, timeout);

		if (wait_rc < 0) {
			if (errno == EINTR)
				continue;
			error("%s: waitpid error: %m", __func__);
			break;
		} else if (wait_rc > 0) {
			break;
		}
	}

	if (track_script_broadcast(pthread_self(), status)) {
		info("%s: slurmscriptd: JobId=%u %s killed by signal %u",
		     __func__, job_id, script_name, WTERMSIG(status));
	} else if (status != 0) {
		error("%s: slurmscriptd: JobId=%u %s exit status %u:%u",
		      __func__, job_id, script_name, WEXITSTATUS(status),
		      WTERMSIG(status));
	} else {
		log_flag(SCRIPT, "%s JobId=%u %s completed",
			 __func__, job_id, script_name);
	}

	/*
	 * Use pthread_self here instead of track_script_rec->tid to avoid any
	 * potential for race.
	 */
	track_script_remove(pthread_self());
	return status;
}

static int _handle_run_prepilog(buf_t *buffer)
{
	int rc, status, resp_rpc;
	uint32_t job_id, tmp_size, env_cnt;
	uint16_t timeout;
	bool is_epilog;
	char *script, *script_name;
	char **env;
	buf_t *resp_buffer;

	safe_unpack32(&job_id, buffer);
	safe_unpackbool(&is_epilog, buffer);
	safe_unpackstr_xmalloc(&script, &tmp_size, buffer);
	safe_unpack32(&env_cnt, buffer);
	safe_unpackstr_array(&env, &env_cnt, buffer);
	safe_unpack16(&timeout, buffer);

	if (is_epilog) {
		script_name = "epilog_slurmctld";
		resp_rpc = SLURMSCRIPTD_REQUEST_EPILOG_COMPLETE;
	} else {
		script_name = "prolog_slurmctld";
		resp_rpc = SLURMSCRIPTD_REQUEST_PROLOG_COMPLETE;
	}

	log_flag(SCRIPT, "Handling SLURMSCRIPTD_REQUEST_RUN_PREPILOG (%s) for JobId=%u",
		 script_name, job_id);
	status = _run_script(script, env, job_id, script_name, timeout);
	xfree(script);
	for (int i = 0; i < env_cnt; i++) {
		xfree(env[i]);
	}
	xfree(env);

	resp_buffer = init_buf(0);
	pack32(job_id, resp_buffer);
	pack32(status, resp_buffer);
	rc = _write_msg(slurmscriptd_writefd, resp_rpc, resp_buffer);
	FREE_NULL_BUFFER(resp_buffer);

	return rc;

unpack_error:
	error("%s: Failed to unpack message", __func__);
	return SLURM_ERROR;
}

static int _handle_prepilog_complete(buf_t *buffer, bool is_epilog)
{
	int rc;
	uint32_t status, job_id;

	safe_unpack32(&job_id, buffer);
	safe_unpack32(&status, buffer);

	if (is_epilog) {
		log_flag(SCRIPT, "Handling SLURMSCRIPTD_REQUEST_EPILOG_COMPLETE for JobId=%u",
			 job_id);
		prep_epilog_slurmctld_callback((int)status, job_id);
	} else {
		log_flag(SCRIPT, "Handling SLURMSCRIPTD_REQUEST_PROLOG_COMPLETE for JobId=%u",
			 job_id);
		prep_prolog_slurmctld_callback((int)status, job_id);
	}
	rc = SLURM_SUCCESS;
	_decr_script_cnt();

	return rc;

unpack_error:
	error("%s: Failed to unpack message", __func__);
	return SLURM_ERROR;
}

static int _handle_flush_complete(buf_t *buffer)
{
	script_response_t *script_resp;
	char *key = NULL;
	uint32_t tmp32;
	int rc = SLURM_SUCCESS;

	log_flag(SCRIPT, "Handling SLURMSCRIPTD_REQUEST_FLUSH_COMPLETE");
	safe_unpackstr_xmalloc(&key, &tmp32, buffer);

	slurm_mutex_lock(&script_resp_map_mutex);
	script_resp = xhash_get(script_resp_map, key, strlen(key));
	if (!script_resp) {
		/*
		 * This should never happen. We don't know how to notify
		 * whoever started this script that it is done.
		 */
		error("%s: Unable to notify thread waiting for SLURMSCRIPTD_FLUSH to complete, may have to SIGKILL slurmctld. (key=%s)",
		      __func__, key);
		rc = SLURM_ERROR;
	} else {
		script_resp->rc = SLURM_SUCCESS;
		slurm_mutex_lock(&script_resp->mutex);
		slurm_cond_signal(&script_resp->cond);
		slurm_mutex_unlock(&script_resp->mutex);
	}
	slurm_mutex_unlock(&script_resp_map_mutex);

	xfree(key);

	return rc;

unpack_error:
	error("%s: Failed to unpack message", __func__);
	return SLURM_ERROR;
}

static int _handle_flush(buf_t *buffer)
{
	buf_t *resp_buf;
	char *key = NULL;
	uint32_t tmp32;

	log_flag(SCRIPT, "Handling SLURMSCRIPTD_REQUEST_FLUSH");
	safe_unpackstr_xmalloc(&key, &tmp32, buffer);

	/* Kill all running scripts */
	track_script_flush();

	resp_buf = init_buf(0);
	packstr(key, resp_buf);
	_write_msg(slurmscriptd_writefd, SLURMSCRIPTD_REQUEST_FLUSH_COMPLETE,
		   resp_buf);
	FREE_NULL_BUFFER(resp_buf);
	xfree(key);

	return SLURM_SUCCESS;

unpack_error:
	error("%s: Failed to unpack message", __func__);
	return SLURM_ERROR;
}

static int _handle_flush_job(buf_t *buffer)
{
	uint32_t job_id;

	safe_unpack32(&job_id, buffer);
	log_flag(SCRIPT, "Handling SLURMSCRIPTD_REQUEST_FLUSH_JOB for JobId=%u",
		 job_id);

	track_script_flush_job(job_id);

	return SLURM_SUCCESS;

unpack_error:
	error("%s: Failed to unpack message", __func__);
	return SLURM_ERROR;
}

static void _run_bb_script_child(int fd, char *script_func, uint32_t job_id,
				 uint32_t argc, char **argv)
{
	int exit_code;
	char *resp = NULL;

	setpgid(0, 0);

	exit_code = bb_g_run_script(script_func, job_id, argc, argv, &resp);
	if (resp)
		safe_write(fd, resp, strlen(resp));

rwfail:
	_exit(exit_code);
}


/*
 * Run the burst buffer script in a fork()'d process so that if the script
 * runs for longer than the timeout, or if the script is cancelled, we can
 * SIGTERM/SIGKILL the process. This is based on the code in run_command(),
 * but instead of calling exec() in the child, we call a burst buffer plugin
 * API to run the script.
 *
 * Set the response of the script in resp_msg.
 * Return the exit code of the script.
 */
static int _run_bb_script(char *script_func, uint32_t job_id, uint32_t timeout,
			  uint32_t argc, char **argv, char **resp_msg,
			  bool *track_script_signalled)
{
	int pfd[2] = {-1, -1};
	bool got_resp = false;
	int status;
	char *resp = NULL;
	pid_t cpid;

	xassert(resp_msg);
	xassert(track_script_signalled);

	*track_script_signalled = false;

	if (pipe(pfd) != 0) {
		*resp_msg = xstrdup_printf("pipe(): %m");
		error("%s: Error running %s for JobId=%u: %s",
		      __func__, script_func, job_id, *resp_msg);
		return 127;
	}

	cpid = fork();
	if (cpid < 0) { /* fork() failed */
		*resp_msg = xstrdup_printf("fork(): %m");
		error("%s: Error running %s for JobId=%u: %s",
		      __func__, script_func, job_id, *resp_msg);
		close(pfd[0]);
		close(pfd[1]);
		return 127;
	} else if (cpid == 0) { /* child - run the script */
		close(pfd[0]); /* Close the read fd, we're only writing */
		_run_bb_script_child(pfd[1], script_func, job_id, argc, argv);
	} else { /* parent */
		int new_wait, max_wait;
		int resp_offset = 0, resp_size = 0;
		struct pollfd fds;
		struct timeval tstart;

		max_wait = timeout * 1000; /* convert to milliseconds */
		resp_size = 1024;
		resp = xmalloc(resp_size);
		close(pfd[1]); /* Close the write fd, we're only reading */
		gettimeofday(&tstart, NULL);
		track_script_rec_add(job_id, cpid, pthread_self());

		while (1) {
			int i;

			fds.fd = pfd[0];
			fds.events = POLLIN | POLLHUP | POLLRDHUP;
			fds.revents = 0;
			if (!max_wait) {
				new_wait = MAX_POLL_WAIT;
			} else {
				new_wait = max_wait - _tot_wait(&tstart);
				if (new_wait <= 0) {
					*resp_msg =
						xstrdup_printf("Timeout @ %d msec",
							       max_wait);
					error("%s: Error running %s for JobId=%u: %s",
					      __func__, script_func, job_id,
					      *resp_msg);
					got_resp = false;
					break;
				}
				new_wait = MIN(new_wait, MAX_POLL_WAIT);
			}
			i = poll(&fds, 1, new_wait);
			if (i == 0) {
				continue;
			} else if (i < 0) {
				*resp_msg = xstrdup_printf("poll():%m");
				error("%s: Error running %s for JobId=%u: %s",
				      __func__, script_func, job_id, *resp_msg);
				got_resp = false;
				break;
			}
			if ((fds.revents & POLLIN) == 0)
				break;
			i = read(pfd[0], resp + resp_offset,
				 resp_size - resp_offset);
			if (i == 0) {
				break;
			} else if (i < 0) {
				if (errno == EAGAIN)
					continue;
				*resp_msg = xstrdup_printf("read(): %m");
				error("%s: Error running %s for JobId=%u: %s",
				      __func__, script_func, job_id, *resp_msg);
				got_resp = false;
				break;
			} else {
				got_resp = true;
				resp_offset += i;
				if (resp_offset + 1024 >= resp_size) {
					resp_size *= 2;
					resp = xrealloc(resp, resp_size);
				}
			}
		}
		killpg(cpid, SIGTERM);
		usleep(10000);
		killpg(cpid, SIGKILL);
		waitpid(cpid, &status, 0);
		close(pfd[0]);

		/* If we were killed by track_script, let the caller know. */
		*track_script_signalled =
			track_script_broadcast(pthread_self(), status);

		track_script_remove(pthread_self());
	}

	if (got_resp)
		*resp_msg = resp;
	else
		xfree(resp);

	return status;
}

static int _handle_run_bb_lua(buf_t *buffer)
{
	bool track_script_signalled;
	uint32_t job_id, tmp_size, argc = 0, status, i, timeout, rc;
	char *script_func = NULL, *resp_msg = NULL, *key = NULL;
	char **argv = NULL;
	buf_t *resp_buffer;

	safe_unpackstr_xmalloc(&key, &tmp_size, buffer);
	safe_unpack32(&job_id, buffer);
	safe_unpackstr_xmalloc(&script_func, &tmp_size, buffer);
	safe_unpackstr_array(&argv, &argc, buffer);
	safe_unpack32(&timeout, buffer);
	log_flag(SCRIPT, "Handling SLURMSCRIPTD_REQUEST_RUN_BB_LUA for JobId=%u: func=%s, timeout=%u seconds, argc=%u, key=%s",
		 job_id, script_func, timeout, argc, key);

	/* Run the script */
	status = _run_bb_script(script_func, job_id, timeout, argc, argv,
				&resp_msg, &track_script_signalled);
	/* Extract return code from exit status. */
	if (WIFEXITED(status))
		rc = WEXITSTATUS(status);
	else
		rc = (uint32_t) SLURM_ERROR;

	/* Send complete message */
	resp_buffer = init_buf(0);
	packstr(key, resp_buffer);
	pack32(job_id, resp_buffer);
	packstr(script_func, resp_buffer);
	pack32(rc, resp_buffer);
	packstr(resp_msg, resp_buffer);
	packbool(track_script_signalled, resp_buffer);
	_write_msg(slurmscriptd_writefd, SLURMSCRIPTD_REQUEST_BB_LUA_COMPLETE,
		   resp_buffer);

	FREE_NULL_BUFFER(resp_buffer);
	xfree(key);
	xfree(script_func);
	xfree(resp_msg);
	for (i = 0; i < argc; i++)
		xfree(argv[i]);
	xfree(argv);

	return SLURM_SUCCESS;

unpack_error:
	error("%s: Failed to unpack message", __func__);

	xfree(key);
	xfree(script_func);
	xfree(resp_msg);
	for (i = 0; i < argc; i++)
		xfree(argv[i]);
	xfree(argv);

	return SLURM_ERROR;
}

static int _handle_bb_lua_complete(buf_t *buffer)
{
	int rc = SLURM_SUCCESS;
	bool track_script_signalled;
	uint32_t job_id, tmp_size, status;
	char *script_func = NULL, *resp_msg = NULL, *key = NULL;
	script_response_t *script_resp;

	safe_unpackstr_xmalloc(&key, &tmp_size, buffer);
	safe_unpack32(&job_id, buffer);
	safe_unpackstr_xmalloc(&script_func, &tmp_size, buffer);
	safe_unpack32(&status, buffer);
	safe_unpackstr_xmalloc(&resp_msg, &tmp_size, buffer);
	safe_unpackbool(&track_script_signalled, buffer);

	log_flag(SCRIPT, "Handling SLURMSCRIPTD_REQUEST_BB_LUA_COMPLETE for JobId=%u: func=%s, status=%u, resp=%s, track_script_signalled=%s, key=%s",
		 job_id, script_func, status, resp_msg,
		 track_script_signalled ? "true" : "false", key);

	_decr_script_cnt();

	slurm_mutex_lock(&script_resp_map_mutex);
	script_resp = xhash_get(script_resp_map, key, strlen(key));
	if (!script_resp) {
		/*
		 * This should never happen. We don't know how to notify
		 * whoever started this script that it is done.
		 */
		error("%s: We don't know who started this script (JobId=%u, func=%s, key=%s) so we can't notify them.",
		      __func__, job_id, script_func, key);
		rc = SLURM_ERROR;
		xfree(resp_msg);
	} else {
		script_resp->resp_msg = resp_msg;
		script_resp->rc = (int) status;
		script_resp->track_script_signalled = track_script_signalled;
		slurm_mutex_lock(&script_resp->mutex);
		slurm_cond_signal(&script_resp->cond);
		slurm_mutex_unlock(&script_resp->mutex);
	}
	slurm_mutex_unlock(&script_resp_map_mutex);

	xfree(key);
	xfree(script_func);

	return rc;

unpack_error:
	error("%s: Failed to unpack message", __func__);

	xfree(key);
	xfree(script_func);
	xfree(resp_msg);

	return SLURM_ERROR;
}

static int _handle_shutdown(void)
{
	log_flag(SCRIPT, "Handling SLURMSCRIPTD_SHUTDOWN");
	/* Kill all running scripts. */
	track_script_flush();

	eio_signal_shutdown(msg_handle);

	return SLURM_ERROR; /* Don't handle any more requests. */
}

static int _handle_request(int req, buf_t *buffer)
{
	int rc;

	switch (req) {
		case SLURMSCRIPTD_REQUEST_RUN_PREPILOG:
			rc = _handle_run_prepilog(buffer);
			break;
		case SLURMSCRIPTD_REQUEST_PROLOG_COMPLETE:
			rc = _handle_prepilog_complete(buffer, false);
			break;
		case SLURMSCRIPTD_REQUEST_EPILOG_COMPLETE:
			rc = _handle_prepilog_complete(buffer, true);
			break;
		case SLURMSCRIPTD_REQUEST_FLUSH:
			rc = _handle_flush(buffer);
			break;
		case SLURMSCRIPTD_REQUEST_FLUSH_COMPLETE:
			rc = _handle_flush_complete(buffer);
			break;
		case SLURMSCRIPTD_REQUEST_FLUSH_JOB:
			rc = _handle_flush_job(buffer);
			break;
		case SLURMSCRIPTD_REQUEST_RUN_BB_LUA:
			rc = _handle_run_bb_lua(buffer);
			break;
		case SLURMSCRIPTD_REQUEST_BB_LUA_COMPLETE:
			rc = _handle_bb_lua_complete(buffer);
			break;
		case SLURMSCRIPTD_SHUTDOWN:
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

static void *_handle_accept(void *args)
{
	req_args_t *req_args = (req_args_t *)args;

	_handle_request(req_args->req, req_args->buffer);
	FREE_NULL_BUFFER(req_args->buffer);
	xfree(req_args);

	return NULL;
}

static int _msg_accept(eio_obj_t *obj, List objs)
{
	int rc = SLURM_SUCCESS, req, buf_len = 0;
	char *incoming_buffer = NULL;
	buf_t *buffer = NULL;
	req_args_t *req_args;

	while (true) {
		if ((rc = read(obj->fd, &req, sizeof(int))) != sizeof(int)) {
			if (rc == 0) { /* EOF, normal */
				break;
			} else {
				debug3("%s: leaving on read error: %m", __func__);
				rc = SLURM_ERROR;
				break;
			}
		}
		/*
		 * We always write the length of the buffer so we can read
		 * the whole thing right here. We write a 0 for the length if
		 * no additional data was sent.
		 */
		safe_read(obj->fd, &buf_len, sizeof(int));
		if (buf_len) {
			incoming_buffer = xmalloc(buf_len);
			safe_read(obj->fd, incoming_buffer, buf_len);
			buffer = create_buf(incoming_buffer, buf_len);
		}

		req_args = xmalloc(sizeof *req_args);
		req_args->req = req;
		req_args->buffer = buffer;
		slurm_thread_create_detached(NULL, _handle_accept, req_args);

		/*
		 * xmalloc()'d data will be xfree()'d by _handle_accept()
		 */
		incoming_buffer = NULL;
		buffer = NULL;
		req_args = NULL;
	}

	return rc;
rwfail:
	error("%s: read/write op failed", __func__);
	return SLURM_ERROR;
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

static int _script_cnt(void)
{
	int cnt;

	slurm_mutex_lock(&script_count_mutex);
	cnt = script_count;
	slurm_mutex_unlock(&script_count_mutex);

	return cnt;
}

static void _kill_slurmscriptd(void)
{
	int status;

	if (slurmscriptd_pid <= 0) {
		error("%s: slurmscriptd_pid < 0, we don't know the PID of slurmscriptd.",
		      __func__);
		return;
	}

	/* Tell slurmscriptd to shutdown, then wait for it to finish. */
	_write_msg(slurmctld_writefd, SLURMSCRIPTD_SHUTDOWN, NULL);
	if (waitpid(slurmscriptd_pid, &status, 0) < 0) {
		if (WIFEXITED(status)) {
			/* Exited normally. */
		} else {
			error("%s: Unable to reap slurmscriptd child process", __func__);
		}
	}
}

extern void slurmscriptd_flush(void)
{
	int tmp;
	buf_t *buffer;
	script_response_t *script_resp;

	script_resp = _script_resp_map_add();
	buffer = init_buf(0);
	packstr(script_resp->key, buffer);
	_write_msg(slurmctld_writefd, SLURMSCRIPTD_REQUEST_FLUSH, buffer);
	FREE_NULL_BUFFER(buffer);

	_wait_for_script_resp(script_resp, &tmp, NULL, NULL);
	_script_resp_map_remove(script_resp->key);
}

extern void slurmscriptd_flush_job(uint32_t job_id)
{
	buf_t *buffer;

	buffer = init_buf(0);
	pack32(job_id, buffer);

	_write_msg(slurmctld_writefd, SLURMSCRIPTD_REQUEST_FLUSH_JOB, buffer);
	FREE_NULL_BUFFER(buffer);
}

extern int slurmscriptd_run_bb_lua(uint32_t job_id, char *function,
				   uint32_t argc, char **argv, uint32_t timeout,
				   char **resp, bool *track_script_signalled)
{
	int rc;
	buf_t *buffer;
	script_response_t *script_resp;

	/*
	 * Save this RPC in a hashmap so we can wait until it is done, get
	 * notified when it is done, and get the response.
	 */
	script_resp = _script_resp_map_add();

	/* Send the RPC. */
	buffer = init_buf(0);
	/*
	 * Pass the key to slurmscriptd, which will pass the key back to
	 * slurmctld when the lua script is done, and that can be used to
	 * notify us that the script is done and give us the rc and response.
	 */
	packstr(script_resp->key, buffer);
	pack32(job_id, buffer);
	packstr(function, buffer);
	packstr_array(argv, argc, buffer);
	pack32(timeout, buffer);

	_incr_script_cnt();
	_write_msg(slurmctld_writefd, SLURMSCRIPTD_REQUEST_RUN_BB_LUA, buffer);
	FREE_NULL_BUFFER(buffer);

	/*
	 * Block until the script is done. _wait_for_script_resp() sets rc,
	 * resp, and track_script_signalled.
	 */
	_wait_for_script_resp(script_resp, &rc, resp, track_script_signalled);
	_script_resp_map_remove(script_resp->key);

	return rc;
}

extern void slurmscriptd_run_prepilog(uint32_t job_id, bool is_epilog,
				      char *script, char **env)
{
	buf_t *buffer;
	uint32_t env_var_cnt = 0;

	buffer = init_buf(0);
	pack32(job_id, buffer);
	packbool(is_epilog, buffer);
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
	pack16(slurm_conf.prolog_epilog_timeout, buffer);

	_incr_script_cnt();
	_write_msg(slurmctld_writefd, SLURMSCRIPTD_REQUEST_RUN_PREPILOG,
		   buffer);
	FREE_NULL_BUFFER(buffer);
}

extern int slurmscriptd_init(int argc, char **argv)
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

		slurm_mutex_init(&script_count_mutex);
		slurm_mutex_init(&write_mutex);
		slurm_mutex_init(&script_resp_map_mutex);
		script_resp_map = xhash_init(_resp_map_key_id, _resp_map_free);
		_setup_eio(slurmctld_readfd);
		slurm_thread_create(&slurmctld_listener_tid,
				    _slurmctld_listener_thread, NULL);
		debug("slurmctld: slurmscriptd fork()'d and initialized.");
	} else { /* child (slurmscriptd_pid == 0) */
		ssize_t i;
		int rc = SLURM_ERROR, ack;
		char *proc_name = "slurmscriptd";
		char *log_prefix;

		/*
		 * Since running_in_slurmctld() is called before we fork()'d,
		 * the result is cached in static variables, so calling it now
		 * would return true even though we're now slurmscriptd.
		 * Reset those cached variables so running_in_slurmctld()
		 * returns false if called from slurmscriptd.
		 * But first change slurm_prog_name since that is
		 * read by run_in_daemon().
		 */
		xfree(slurm_prog_name);
		slurm_prog_name = xstrdup(proc_name);
		running_in_slurmctld_reset();

		/*
		 * Change the process name to slurmscriptd.
		 * Since slurmscriptd logs to the slurmctld log file, add a
		 * prefix to make it clear which daemon a log comes from.
		 */
		init_setproctitle(argc, argv);
		setproctitle("%s", proc_name);
#if HAVE_SYS_PRCTL_H
		if (prctl(PR_SET_NAME, proc_name, NULL, NULL, NULL) < 0) {
			error("%s: cannot set my name to %s %m",
			      __func__, proc_name);
		}
#endif
		/* log_set_fpfx takes control of an xmalloc()'d string */
		log_prefix = xstrdup_printf("%s: ", proc_name);
		log_set_fpfx(&log_prefix);

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

#ifdef MEMORY_LEAK_DEBUG
		track_script_fini();
#endif

		/* We never want to return from here, only exit. */
		_exit(0);
	}

	return SLURM_SUCCESS;
}

extern int slurmscriptd_fini(void)
{
	int pc, last_pc = 0;

	debug("%s starting", __func__);
	_kill_slurmscriptd();

	/* Wait until all script complete messages have been processed. */
	while ((pc = _script_cnt()) > 0) {
		if ((last_pc != 0) && (last_pc != pc)) {
			info("waiting for %d running processes", pc);
		}
		last_pc = pc;
		usleep(100000);
	}

	/* Now shutdown communications. */
	eio_signal_shutdown(msg_handle);
	pthread_join(slurmctld_listener_tid, NULL);
	slurm_mutex_destroy(&script_resp_map_mutex);
	xhash_clear(script_resp_map);
	slurm_mutex_destroy(&write_mutex);
	(void) close(slurmctld_writefd);
	(void) close(slurmctld_readfd);

	debug("%s complete", __func__);

	return SLURM_SUCCESS;
}
