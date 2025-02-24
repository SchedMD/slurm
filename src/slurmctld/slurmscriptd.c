/*****************************************************************************\
 *  slurmscriptd.c - Slurm script functions.
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

#define _GNU_SOURCE	/* For POLLRDHUP */

#if HAVE_SYS_PRCTL_H
#  include <sys/prctl.h>
#endif

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include "slurm/slurm_errno.h"

#include "src/common/eio.h"
#include "src/common/env.h"
#include "src/common/fd.h"
#include "src/common/fetch_config.h"
#include "src/common/log.h"
#include "src/common/msg_type.h"
#include "src/common/run_command.h"
#include "src/common/setproctitle.h"
#include "src/common/slurm_protocol_pack.h"
#include "src/common/track_script.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/conmgr/conmgr.h"

#include "src/interfaces/burst_buffer.h"
#include "src/interfaces/hash.h"

#include "src/slurmctld/locks.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/slurmscriptd.h"
#include "src/slurmctld/slurmscriptd_protocol_defs.h"
#include "src/slurmctld/slurmscriptd_protocol_pack.h"

#define MAX_POLL_WAIT 500 /* in milliseconds */
#define MAX_SHUTDOWN_DELAY 10

#ifndef POLLRDHUP
#define POLLRDHUP POLLHUP
#endif

/*
 *****************************************************************************
 * The following are meant to be used by both slurmscriptd and slurmctld
 *****************************************************************************
 */

static bool _msg_readable(eio_obj_t *obj);
static int _msg_accept(eio_obj_t *obj, list_t *objs);
static int _handle_close(eio_obj_t *obj, list_t *objs);

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

static void _incr_script_cnt(void);

static bool shutting_down = false;
static int slurmctld_readfd = -1;
static int slurmctld_writefd = -1;
static pid_t slurmscriptd_pid;
static pthread_t slurmctld_listener_tid;
static int script_count = 0;
static pthread_mutex_t script_count_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t script_count_cond = PTHREAD_COND_INITIALIZER;

static pthread_mutex_t script_resp_map_mutex = PTHREAD_MUTEX_INITIALIZER;
static xhash_t *script_resp_map = NULL;

/*
 *****************************************************************************
 * The following are meant to be used by only slurmscriptd
 *****************************************************************************
 */
static int slurmscriptd_readfd = -1;
static int slurmscriptd_writefd = -1;
static pthread_mutex_t powersave_script_count_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t powersave_script_cond = PTHREAD_COND_INITIALIZER;
static int powersave_script_count = 0;
static bool powersave_wait_called = false;


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
	script_resp->key = xstrdup_printf("%"PRIu64, (uint64_t) pthread_self());
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

static void _wait_for_powersave_scripts()
{
	int cnt = 0;
	struct timespec ts = {0, 0};
	time_t start;
	time_t now;
	bool first = true;

	/*
	 * Only do this wait once. Under normal operation, this is called twice:
	 * (1) _handle_shutdown()
	 * (2) _handle_close()
	 * We could just call this from _handle_shutdown(). However, if
	 * slurmctld fatal()'s or dies in some other way without sending
	 * SLURMSCRIPTD_SHUTDOWN, then only _handle_close() is called. So, we
	 * need this to be called from both places but only happen once.
	 */
	if (powersave_wait_called)
		return;
	powersave_wait_called = true;

	/*
	 * ResumeProgram has a temporary file open held in memory.
	 * Wait up to MAX_SHUTDOWN_DELAY seconds for powersave scripts to
	 * finish before shutting down (which will close the temporary file).
	 */
	slurm_mutex_lock(&powersave_script_count_mutex);
	start = now = time(NULL);
	while (now < (start + MAX_SHUTDOWN_DELAY)) {
		cnt = powersave_script_count;
		if (!cnt)
			break;
		if (first) {
			log_flag(SCRIPT, "Waiting up to %d seconds for %d powersave scripts to complete",
				 MAX_SHUTDOWN_DELAY, cnt);
			first = false;
		}

		ts.tv_sec = now + 2;
		slurm_cond_timedwait(&powersave_script_cond,
				     &powersave_script_count_mutex, &ts);
		now = time(NULL);
	}
	slurm_mutex_unlock(&powersave_script_count_mutex);

	/* Kill or orphan running scripts. */
	run_command_shutdown();
	if (cnt) {
		error("power_save: orphaning %d processes which are not terminating so slurmctld can exit",
		      cnt);

		/*
		 * Wait for the script completion messages to be processed and
		 * sent to slurmctld, otherwise slurmctld may wait forever for
		 * a message that won't come.
		 */
		slurm_mutex_lock(&powersave_script_count_mutex);
		while (cnt) {
			ts.tv_sec = time(NULL) + 2;
			slurm_cond_timedwait(&powersave_script_cond,
					     &powersave_script_count_mutex,
					     &ts);
			cnt = powersave_script_count;
		}
		slurm_mutex_unlock(&powersave_script_count_mutex);
	}

}

static int _handle_close(eio_obj_t *obj, list_t *objs)
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

	if (!running_in_slurmctld()) { /* Only do this for slurmscriptd */
		_wait_for_powersave_scripts();
		track_script_flush();
	} else {
		/* fd has been closed */
		slurmctld_readfd = -1;
	}

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

static int _write_msg(int fd, int req, buf_t *buffer, bool lock)
{
	int len = 0;

	if (lock)
		slurm_mutex_lock(&write_mutex);
	safe_write(fd, &req, sizeof(int));
	if (buffer) {
		len = get_buf_offset(buffer);
		safe_write(fd, &len, sizeof(int));
		safe_write(fd, get_buf_data(buffer), len);
	} else /* Write 0 length so the receiver knows not to read anymore */
		safe_write(fd, &len, sizeof(int));
	if (lock)
		slurm_mutex_unlock(&write_mutex);

	return SLURM_SUCCESS;

rwfail:
	if (running_in_slurmctld())
		error("%s: read/write op failed, restart slurmctld now: %m",
		      __func__);
	if (lock)
		slurm_mutex_unlock(&write_mutex);
	return SLURM_ERROR;
}

/*
 * Send an RPC from slurmctld to slurmscriptd.
 *
 * IN msg_type - type of message to send
 * IN msg_data - pointer to the message to send
 * IN wait - whether or not to wait for a response
 * OUT resp_msg - If not null, then this is set to the response string from
 *                the script. Caller is responsible to free.
 * OUT signalled - If not null, then this is set to true if the script was
 *                 signalled by track_script, false if not.
 *
 * RET SLURM_SUCCESS or SLURM_ERROR
 */
static int _send_to_slurmscriptd(uint32_t msg_type, void *msg_data, bool wait,
				 char **resp_msg, bool *signalled)
{
	slurmscriptd_msg_t msg;
	int rc = SLURM_SUCCESS;
	script_response_t *script_resp = NULL;
	buf_t *buffer = init_buf(0);

	xassert(running_in_slurmctld());
	memset(&msg, 0, sizeof(msg));

	if (wait) {
		script_resp = _script_resp_map_add();
		msg.key = script_resp->key;
	}
	msg.msg_data = msg_data;
	msg.msg_type = msg_type;

	if (slurmscriptd_pack_msg(&msg, buffer) != SLURM_SUCCESS) {
		rc = SLURM_ERROR;
		goto cleanup;
	}
	if (msg_type == SLURMSCRIPTD_REQUEST_RUN_SCRIPT)
		_incr_script_cnt();
	rc = _write_msg(slurmctld_writefd, msg.msg_type, buffer, true);

	if ((rc == SLURM_SUCCESS) && wait) {
		_wait_for_script_resp(script_resp, &rc, resp_msg, signalled);
		_script_resp_map_remove(script_resp->key);
	}

cleanup:
	FREE_NULL_BUFFER(buffer);

	return rc;
}

static void *_async_send_to_slurmscriptd(void *x)
{
	slurmscriptd_msg_t *send_args = x;

	_send_to_slurmscriptd(send_args->msg_type, send_args->msg_data, false,
			      NULL, NULL);
	slurmscriptd_free_msg(send_args);
	xfree(send_args);

	return NULL;
}

/*
 * This should only be called by slurmscriptd.
 */
static int _respond_to_slurmctld(char *key, uint32_t job_id, char *resp_msg,
				 char *script_name, script_type_t script_type,
				 bool signalled, int status, bool timed_out)
{
	int rc = SLURM_SUCCESS;
	slurmscriptd_msg_t msg;
	script_complete_t script_complete;
	buf_t *buffer = init_buf(0);

	/* Check that we're running in slurmscriptd. */
	xassert(!running_in_slurmctld());

	memset(&script_complete, 0, sizeof(script_complete));
	script_complete.job_id = job_id;
	/* Just point to strings, don't xstrdup, so don't free. */
	script_complete.resp_msg = resp_msg;
	script_complete.script_name = script_name;
	script_complete.script_type = script_type;
	script_complete.signalled = signalled;
	script_complete.status = status;
	script_complete.timed_out = timed_out;

	memset(&msg, 0, sizeof(msg));
	msg.key = key;
	msg.msg_data = &script_complete;
	msg.msg_type = SLURMSCRIPTD_REQUEST_SCRIPT_COMPLETE;

	if (slurmscriptd_pack_msg(&msg, buffer) != SLURM_SUCCESS) {
		rc = SLURM_ERROR;
		goto cleanup;
	}
	_write_msg(slurmscriptd_writefd, msg.msg_type, buffer, true);

cleanup:
	FREE_NULL_BUFFER(buffer);
	return rc;
}

static void _decr_script_cnt(void)
{
	slurm_mutex_lock(&script_count_mutex);
	script_count--;
	if (!script_count && shutting_down)
		slurm_cond_signal(&script_count_cond);
	slurm_mutex_unlock(&script_count_mutex);
}

static void _incr_script_cnt(void)
{
	slurm_mutex_lock(&script_count_mutex);
	script_count++;
	slurm_mutex_unlock(&script_count_mutex);
}

static void _change_proc_name(int argc, char **argv, char *proc_name)
{
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
	/* log_set_prefix takes control of an xmalloc()'d string */
	log_prefix = xstrdup_printf("%s: ", proc_name);
	log_set_prefix(&log_prefix);
}

static void _send_bb_script_msg(int write_fd, void *cb_arg)
{
	run_script_msg_t *script_msg = cb_arg;
	buf_t *buffer = init_buf(0);
	bb_script_info_msg_t bb_msg = {
		.cluster_name = slurm_conf.cluster_name,
		.extra_buf = script_msg->extra_buf,
		.extra_buf_size = script_msg->extra_buf_size,
		.function = script_msg->script_name,
		.job_id = script_msg->job_id,
		.slurmctld_debug = slurm_conf.slurmctld_debug,
		.slurmctld_logfile = slurm_conf.slurmctld_logfile,
		.log_fmt = slurm_conf.log_fmt,
		.plugindir = slurm_conf.plugindir,
		.slurm_user_name = slurm_conf.slurm_user_name,
		.slurm_user_id = slurm_conf.slurm_user_id,
	};
	slurmscriptd_msg_t msg = {
		.msg_data = &bb_msg,
		.msg_type = SLURMSCRIPTD_REQUEST_BB_SCRIPT_INFO,
	};

	/*
	 * Send bb_script_info_msg_t. The write_mutex controls writing on
	 * slurmctld_writefd or slurmscriptd_writefd. We are writing to a pipe
	 * to a running script, so we do not need to lock write_mutex.
	 */
	slurmscriptd_pack_msg(&msg, buffer);
	if (_write_msg(write_fd, SLURMSCRIPTD_REQUEST_BB_SCRIPT_INFO, buffer,
		       false) != SLURM_SUCCESS) {
		error("%s: Failed writing data to script: burst_buffer.lua:%s, JobId=%u",
		      __func__, script_msg->script_name, script_msg->job_id);
		goto fini;
	}

fini:
	FREE_NULL_BUFFER(buffer);
}

static int _recv_bb_script_msg(bb_script_info_msg_t **msg_pptr)
{
	int rc = SLURM_SUCCESS, req = 0, len = 0;
	int fd = STDIN_FILENO;
	char *data = NULL;
	buf_t *buffer = NULL;
	slurmscriptd_msg_t scriptd_msg = { 0 };

	safe_read(fd, &req, sizeof(req));
	scriptd_msg.msg_type = req;
	if (scriptd_msg.msg_type != SLURMSCRIPTD_REQUEST_BB_SCRIPT_INFO) {
		fatal("%s: Invalid msg_type=%u",
		      __func__, scriptd_msg.msg_type);
	}

	safe_read(fd, &len, sizeof(len));
	if (!len)
		fatal("%s: Invalid message length == 0", __func__);

	data = xmalloc(len);
	safe_read(fd, data, len);

	/* Unpack bb_script_info_msg_t */
	buffer = create_buf(data, len);
	rc = slurmscriptd_unpack_msg(&scriptd_msg, buffer);
	*msg_pptr = scriptd_msg.msg_data;
	FREE_NULL_BUFFER(buffer);

	return rc;

rwfail:
	error("%s Failed", __func__);
	return SLURM_ERROR;
}

/*
 * Run a script with a given timeout (in seconds).
 * Return the status or SLURM_ERROR if fork() fails.
 */
static int _run_script(run_command_args_t *run_command_args,
		       run_script_msg_t *script_msg,
		       char **resp_msg, bool *signalled)
{
	int status = SLURM_ERROR;
	int ms_timeout;
	char *resp = NULL;
	bool killed = false;
	int tmp_fd = 0;
	uint32_t job_id = script_msg->job_id;
	int timeout = script_msg->timeout;
	char *tmp_file_env_name = script_msg->tmp_file_env_name;
	char *tmp_file_str = script_msg->tmp_file_str;

	if ((timeout <= 0) || (timeout == NO_VAL16))
		ms_timeout = -1; /* wait indefinitely in run_command() */
	else
		ms_timeout = timeout * 1000;

	run_command_args->max_wait = ms_timeout;
	run_command_args->status = &status;

	if (tmp_file_str) {
		char *tmp_file = NULL;
		/*
		 * Open a file into which we dump tmp_file_str.
		 * Set an environment variable so the script will know how to
		 * read this file. We need to keep this file open for as long
		 * as the script is running.
		 */
		xassert(tmp_file_env_name);
		tmp_fd = dump_to_memfd((char*) run_command_args->script_type,
				       tmp_file_str, &tmp_file);
		if (tmp_fd == SLURM_ERROR) {
			error("Failed to create tmp file for %s",
			      run_command_args->script_type);
			tmp_fd = 0;
		} else {
			env_array_append(&run_command_args->env,
					 tmp_file_env_name, tmp_file);
		}
		xfree(tmp_file);
	}

	if (run_command_args->tid)
		track_script_rec_add(job_id, 0, pthread_self());
	resp = run_command(run_command_args);
	if (run_command_args->tid)
		killed = track_script_killed(pthread_self(), status, true);
	else if (WIFSIGNALED(status) && (WTERMSIG(status) == SIGKILL))
		killed = true;
	if (killed) {
		info("%s: JobId=%u %s killed by signal %u",
		     __func__, job_id, run_command_args->script_type,
		     WTERMSIG(status));
	} else if (status != 0) {
		error("%s: JobId=%u %s exit status %u:%u",
		      __func__, job_id, run_command_args->script_type,
		      WEXITSTATUS(status),
		      WTERMSIG(status));
	} else {
		if (job_id)
			log_flag(SCRIPT, "%s JobId=%u %s completed",
				 __func__, job_id,
				 run_command_args->script_type);
		else
			log_flag(SCRIPT, "%s %s completed",
				 __func__, run_command_args->script_type);
	}

	/*
	 * Use pthread_self here instead of track_script_rec->tid to avoid any
	 * potential for race.
	 */
	if (run_command_args->tid)
		track_script_remove(pthread_self());

	if (tmp_fd)
		close(tmp_fd);

	if (resp_msg)
		*resp_msg = resp;
	else
		xfree(resp);
	if (signalled)
		*signalled = killed;

	return status;
}

static int _handle_flush(slurmscriptd_msg_t *recv_msg)
{
	log_flag(SCRIPT, "Handling %s", rpc_num2string(recv_msg->msg_type));
	/* Kill all running scripts */
	track_script_flush();
	/*
	 * DO NOT CALL _wait_for_powersave_scripts HERE. That would result in
	 * reconfigure waiting for up to MAX_SHUTDOWN_DELAY seconds, which is
	 * an unacceptably long time for reconfigure.
	 */

	/* We need to respond to slurmctld that we are done */
	_respond_to_slurmctld(recv_msg->key, 0, NULL,
			      "SLURMSCRIPTD_REQUEST_FLUSH", SLURMSCRIPTD_NONE,
			      false, SLURM_SUCCESS, false);

	return SLURM_SUCCESS;
}

static int _handle_flush_job(slurmscriptd_msg_t *recv_msg)
{
	flush_job_msg_t *flush_msg = recv_msg->msg_data;

	log_flag(SCRIPT, "Handling %s for JobId=%u",
		 rpc_num2string(recv_msg->msg_type), flush_msg->job_id);

	track_script_flush_job(flush_msg->job_id);

	return SLURM_SUCCESS;
}

static int _handle_shutdown(slurmscriptd_msg_t *recv_msg)
{
	log_flag(SCRIPT, "Handling %s", rpc_num2string(recv_msg->msg_type));
	/* Kill or orphan all running scripts. */
	_wait_for_powersave_scripts();
	track_script_flush();

	conmgr_request_shutdown();
	eio_signal_shutdown(msg_handle);

	return SLURM_ERROR; /* Don't handle any more requests. */
}

static int _handle_run_script(slurmscriptd_msg_t *recv_msg)
{
	extern char **environ;
	run_script_msg_t *script_msg = recv_msg->msg_data;
	int rc, status = 0;
	char *resp_msg = NULL;
	bool signalled = false;
	bool timed_out = false;
	pthread_t tid = pthread_self();
	run_command_args_t run_command_args = {
		.env = env_array_copy((const char **) script_msg->env),
		.script_argv = script_msg->argv,
		.script_path = script_msg->script_path,
		.script_type = script_msg->script_name,
		.tid = tid,
		.timed_out = &timed_out,
	};

	log_flag(SCRIPT, "Handling %s (name=%s%s, JobId=%u, timeout=%u seconds, argc=%u, key=%s)",
		 rpc_num2string(recv_msg->msg_type),
		 script_msg->script_type == SLURMSCRIPTD_BB_LUA ?
		 "burst_buffer.lua:" : "",
		 script_msg->script_name,
		 script_msg->job_id,
		 script_msg->timeout,
		 script_msg->argc,
		 recv_msg->key);

	switch (script_msg->script_type) {
	case SLURMSCRIPTD_BB_LUA:
		/*
		 * Set SLURM_SCRIPT_CONTEXT in env for slurmctld, but we also
		 * need to preserve the parent's environment. There was not any
		 * env passed to us in script_msg.
		 */
		xassert(!run_command_args.env);
		run_command_args.env = env_array_copy((const char **) environ);
		env_array_append(&run_command_args.env, "SLURM_SCRIPT_CONTEXT",
				 "burst_buffer.lua");

		/* burst_buffer.lua is not exec'd, it is run directly from C */
		run_command_args.ignore_path_exec_check = true;

		/* Send needed script info and configs to slurmctld */
		run_command_args.write_to_child = true;
		run_command_args.cb = _send_bb_script_msg;
		run_command_args.cb_arg = script_msg;

		status = _run_script(&run_command_args, script_msg,
				     &resp_msg, &signalled);
		break;
	case SLURMSCRIPTD_EPILOG: /* fall-through */
	case SLURMSCRIPTD_MAIL:
	case SLURMSCRIPTD_PROLOG:
	case SLURMSCRIPTD_REBOOT:
	case SLURMSCRIPTD_RESV:
		/*
		 * script_msg->timeout is in seconds but
		 * run_command_args.max_wait expects milliseconds.
		 * script_msg->timeout may also not be set (NO_VAL16).
		 * Let _run_script handle the conversion.
		 */
		status = _run_script(&run_command_args, script_msg,
				     &resp_msg, &signalled);
		break;
	case SLURMSCRIPTD_POWER:
		slurm_mutex_lock(&powersave_script_count_mutex);
		powersave_script_count++;
		slurm_mutex_unlock(&powersave_script_count_mutex);

		/*
		 * We want these scripts to keep running even if slurmctld
		 * shuts down, so do not track these scripts with track_script
		 * so they don't get killed when slurmctld shuts down.
		 */
		run_command_args.tid = 0;
		run_command_args.orphan_on_shutdown = true;
		status = _run_script(&run_command_args, script_msg,
				     &resp_msg, &signalled);

		break;
	default:
		error("%s: Invalid script type=%d",
		      __func__, script_msg->script_type);
		status = SLURM_ERROR;
		break;
	}

	/* Send response */
	rc = _respond_to_slurmctld(recv_msg->key, script_msg->job_id,
				   resp_msg, script_msg->script_name,
				   script_msg->script_type, signalled, status,
				   timed_out);

	if (script_msg->script_type == SLURMSCRIPTD_POWER) {
		slurm_mutex_lock(&powersave_script_count_mutex);
		powersave_script_count--;
		if (!powersave_script_count && powersave_wait_called)
			slurm_cond_signal(&powersave_script_cond);
		slurm_mutex_unlock(&powersave_script_count_mutex);
	}
	xfree(resp_msg);
	env_array_free(run_command_args.env);

	return rc;
}

static int _notify_script_done(char *key, script_complete_t *script_complete)
{
	int rc = SLURM_SUCCESS;
	script_response_t *script_resp;

	slurm_mutex_lock(&script_resp_map_mutex);
	script_resp = xhash_get(script_resp_map, key, strlen(key));
	if (!script_resp) {
		/*
		 * This should never happen. We don't know how to notify
		 * whoever started this script that it is done.
		 */
		error("%s: We don't know who started this script (JobId=%u, func=%s, key=%s) so we can't notify them.",
		      __func__, script_complete->job_id,
		      script_complete->script_name, key);
		rc = SLURM_ERROR;
	} else {
		script_resp->resp_msg = xstrdup(script_complete->resp_msg);
		script_resp->rc = script_complete->status;
		script_resp->track_script_signalled =
			script_complete->signalled;
		slurm_mutex_lock(&script_resp->mutex);
		slurm_cond_signal(&script_resp->cond);
		slurm_mutex_unlock(&script_resp->mutex);
	}
	slurm_mutex_unlock(&script_resp_map_mutex);

	return rc;
}

static int _handle_script_complete(slurmscriptd_msg_t *msg)
{
	int rc = SLURM_SUCCESS;
	script_complete_t *script_complete = msg->msg_data;

	/* Notify the waiting thread that the script is done */
	if (msg->key)
		rc = _notify_script_done(msg->key, script_complete);

	log_flag(SCRIPT, "Handling %s (name=%s, JobId=%u, resp_msg=%s)",
		 rpc_num2string(msg->msg_type),
		 script_complete->script_name,
		 script_complete->job_id,
		 script_complete->resp_msg);

	switch (script_complete->script_type) {
	case SLURMSCRIPTD_BB_LUA:
	case SLURMSCRIPTD_MAIL:
	case SLURMSCRIPTD_REBOOT:
	case SLURMSCRIPTD_RESV:
		break; /* Nothing more to do */
	case SLURMSCRIPTD_EPILOG:
		prep_epilog_slurmctld_callback(script_complete->status,
					       script_complete->job_id,
					       script_complete->timed_out);
		break;
	case SLURMSCRIPTD_POWER:
		ping_nodes_now = true;
		break;
	case SLURMSCRIPTD_PROLOG:
		prep_prolog_slurmctld_callback(script_complete->status,
					       script_complete->job_id,
					       script_complete->timed_out);
		break;
	case SLURMSCRIPTD_NONE:
		/*
		 * Some other RPC (for example, SLURMSCRIPTD_REQUEST_FLUSH)
		 * completed and sent this back to notify a waiting thread of
		 * its completion. We do not want to call _decr_script_cnt()
		 * since it wasn't a script that ran, so we just return right
		 * now.
		 */
		return SLURM_SUCCESS;
	default:
		error("%s: unknown script type for script=%s, JobId=%u",
		      rpc_num2string(msg->msg_type),
		      script_complete->script_name, script_complete->job_id);
		break;
	}

	_decr_script_cnt();

	return rc;
}

static int _handle_update_debug_flags(slurmscriptd_msg_t *msg)
{
	slurmctld_lock_t config_write_lock =
		{ .conf = WRITE_LOCK };
	debug_flags_msg_t *debug_msg = msg->msg_data;
	char *flag_string;

	flag_string = debug_flags2str(debug_msg->debug_flags);
	log_flag(SCRIPT, "Handling %s; set DebugFlags to '%s'",
		 rpc_num2string(msg->msg_type),
		 flag_string ? flag_string : "none");
	xfree(flag_string);

	lock_slurmctld(config_write_lock);
	slurm_conf.debug_flags = debug_msg->debug_flags;
	slurm_conf.last_update = time(NULL);
	unlock_slurmctld(config_write_lock);

	return SLURM_SUCCESS;
}

static int _handle_update_log(slurmscriptd_msg_t *msg)
{
	slurmctld_lock_t config_write_lock =
		{ .conf = WRITE_LOCK };
	log_msg_t *log_msg = msg->msg_data;
	int debug_level = (int) log_msg->debug_level;
	bool log_rotate = log_msg->log_rotate;

	log_flag(SCRIPT, "Handling %s; set debug level to '%s'%s",
		 rpc_num2string(msg->msg_type),
		 log_num2string(debug_level),
		 log_rotate ? ", logrotate" : "");

	lock_slurmctld(config_write_lock);
	if (log_rotate) {
		update_logging();
	} else {
		update_log_levels(debug_level, debug_level);
		slurm_conf.slurmctld_debug = debug_level;
		slurm_conf.last_update = time(NULL);
	}
	unlock_slurmctld(config_write_lock);

	return SLURM_SUCCESS;
}

static int _handle_request(int req, buf_t *buffer)
{
	int rc;
	slurmscriptd_msg_t recv_msg;

	memset(&recv_msg, 0, sizeof(recv_msg));
	recv_msg.msg_type = (uint32_t)req;
	if (slurmscriptd_unpack_msg(&recv_msg, buffer) != SLURM_SUCCESS) {
		error("%s: Unable to handle message %d", __func__, req);
		rc = SLURM_ERROR;
		goto cleanup;
	}

	switch (req) {
		case SLURMSCRIPTD_REQUEST_FLUSH:
			rc = _handle_flush(&recv_msg);
			break;
		case SLURMSCRIPTD_REQUEST_FLUSH_JOB:
			rc = _handle_flush_job(&recv_msg);
			break;
		case SLURMSCRIPTD_REQUEST_RUN_SCRIPT:
			rc = _handle_run_script(&recv_msg);
			break;
		case SLURMSCRIPTD_REQUEST_SCRIPT_COMPLETE:
			rc = _handle_script_complete(&recv_msg);
			break;
		case SLURMSCRIPTD_REQUEST_UPDATE_DEBUG_FLAGS:
			rc = _handle_update_debug_flags(&recv_msg);
			break;
		case SLURMSCRIPTD_REQUEST_UPDATE_LOG:
			rc = _handle_update_log(&recv_msg);
			break;
		case SLURMSCRIPTD_SHUTDOWN:
			rc = _handle_shutdown(&recv_msg);
			break;
		default:
			error("%s: slurmscriptd: Unrecognied request: %d",
			      __func__, req);
			rc = SLURM_ERROR;
			break;
	}

cleanup:
	slurmscriptd_free_msg(&recv_msg);
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

static int _msg_accept(eio_obj_t *obj, list_t *objs)
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
		slurm_thread_create_detached(_handle_accept, req_args);

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

static void _on_sigint(conmgr_callback_args_t conmgr_args, void *arg)
{
	log_flag(SCRIPT, "Caught SIGINT. Ignoring.");
}

static void _on_sigterm(conmgr_callback_args_t conmgr_args, void *arg)
{
	log_flag(SCRIPT, "Caught SIGTERM. Ignoring.");
}

static void _on_sigchld(conmgr_callback_args_t conmgr_args, void *arg)
{
	log_flag(SCRIPT, "Caught SIGCHLD. Ignoring");
}

static void _on_sigquit(conmgr_callback_args_t conmgr_args, void *arg)
{
	log_flag(SCRIPT, "Caught SIGQUIT. Ignoring.");
}

static void _on_sigtstp(conmgr_callback_args_t conmgr_args, void *arg)
{
	log_flag(SCRIPT, "Caught SIGTSTP. Ignoring.");
}

static void _on_sighup(conmgr_callback_args_t conmgr_args, void *arg)
{
	log_flag(SCRIPT, "Caught SIGHUP. Ignoring.");
}

static void _on_sigusr1(conmgr_callback_args_t conmgr_args, void *arg)
{
	log_flag(SCRIPT, "Caught SIGUSR1. Ignoring.");
}

static void _on_sigusr2(conmgr_callback_args_t conmgr_args, void *arg)
{
	log_flag(SCRIPT, "Caught SIGUSR2. Ignoring.");
}

static void _on_sigpipe(conmgr_callback_args_t conmgr_args, void *arg)
{
	/* debug5 to avoid polluting the SCRIPT debug flag */
	debug5("Caught SIGPIPE. Ignoring.");
}

static void _on_sigxcpu(conmgr_callback_args_t conmgr_args, void *arg)
{
	log_flag(SCRIPT, "Caught SIGXCPU. Ignoring.");
}

static void _on_sigabrt(conmgr_callback_args_t conmgr_args, void *arg)
{
	log_flag(SCRIPT, "Caught SIGABRT. Ignoring.");
}

static void _on_sigalrm(conmgr_callback_args_t conmgr_args, void *arg)
{
	log_flag(SCRIPT, "Caught SIGALRM. Ignoring.");
}

static void _init_slurmscriptd_conmgr(void)
{
	conmgr_callbacks_t callbacks = { NULL, NULL };

	if (slurm_conf.slurmctld_params)
		conmgr_set_params(slurm_conf.slurmctld_params);

	conmgr_init(0, 0, callbacks);

	/*
	 * Ignore signals. slurmscriptd should only handle requests directly
	 * from slurmctld.
	 */
	conmgr_add_work_signal(SIGINT, _on_sigint, NULL);
	conmgr_add_work_signal(SIGTERM, _on_sigterm, NULL);
	conmgr_add_work_signal(SIGCHLD, _on_sigchld, NULL);
	conmgr_add_work_signal(SIGQUIT, _on_sigquit, NULL);
	conmgr_add_work_signal(SIGTSTP, _on_sigtstp, NULL);
	conmgr_add_work_signal(SIGHUP, _on_sighup, NULL);
	conmgr_add_work_signal(SIGUSR1, _on_sigusr1, NULL);
	conmgr_add_work_signal(SIGUSR2, _on_sigusr2, NULL);
	conmgr_add_work_signal(SIGPIPE, _on_sigpipe, NULL);
	conmgr_add_work_signal(SIGXCPU, _on_sigxcpu, NULL);
	conmgr_add_work_signal(SIGABRT, _on_sigabrt, NULL);
	conmgr_add_work_signal(SIGALRM, _on_sigalrm, NULL);

	conmgr_run(false);
}

__attribute__((noreturn))
extern void slurmscriptd_run_slurmscriptd(int argc, char **argv,
					  char *binary_path)
{
	ssize_t i;
	int rc = SLURM_ERROR, ack;

	slurmscriptd_writefd = SLURMSCRIPT_WRITE_FD;
	slurmscriptd_readfd = SLURMSCRIPT_READ_FD;

	_change_proc_name(argc, argv, "slurmscriptd");

	/* Test communications with slurmctld. */
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

	debug("slurmscriptd: Got ack from slurmctld");

	i = write(slurmscriptd_writefd, &ack, sizeof(int));
	if (i != sizeof(int))
		fatal("%s: Failed to send initialization code to slurmctld",
		      __func__);

	_init_slurmscriptd_conmgr();

	debug("Initialization successful");

	slurm_mutex_init(&powersave_script_count_mutex);
	slurm_mutex_init(&write_mutex);
	if ((run_command_init(0, NULL, binary_path) != SLURM_SUCCESS) &&
	    binary_path && binary_path[0])
		fatal("%s: Unable to reliably execute %s",
		      __func__, binary_path);

	_setup_eio(slurmscriptd_readfd);

	debug("%s: started", __func__);
	eio_handle_mainloop(msg_handle);
	debug("%s: finished", __func__);

#ifdef MEMORY_LEAK_DEBUG
	track_script_fini();
	slurm_mutex_destroy(&powersave_script_count_mutex);
#endif

	/* We never want to return from here, only exit. */
	_exit(0);
}

static void *_slurmctld_listener_thread(void *x)
{
	debug("%s: started listening to slurmscriptd", __func__);
	eio_handle_mainloop(msg_handle);
	debug("%s: finished", __func__);

	return NULL;
}

static void _wait_for_all_scripts(void)
{
	int last_pc = 0;
	struct timespec ts = {0, 0};

	/*
	 * Wait until all script complete messages have been processed or until
	 * the readfd is closed, in which case we know we'll never get more
	 * messages from slurmscriptd.
	 */
	slurm_mutex_lock(&script_count_mutex);
	while (slurmctld_readfd > 0) {
		if (!script_count)
			break;
		if (last_pc != script_count)
			info("waiting for %d running processes", script_count);
		last_pc = script_count;
		ts.tv_sec = time(NULL) + 2;
		slurm_cond_timedwait(&script_count_cond, &script_count_mutex,
				     &ts);
	}
	slurm_mutex_unlock(&script_count_mutex);
}

static void _kill_slurmscriptd(void)
{
	int status;
	int rc;

	if (slurmscriptd_pid <= 0) {
		error("%s: slurmscriptd_pid < 0, we don't know the PID of slurmscriptd.",
		      __func__);
		return;
	}

	shutting_down = true;
	slurmscriptd_flush();

	/* Tell slurmscriptd to shutdown, then wait for it to finish. */
	rc = _send_to_slurmscriptd(SLURMSCRIPTD_SHUTDOWN, NULL, false, NULL,
				   NULL);

	if (rc == SLURM_SUCCESS)
		_wait_for_all_scripts();

	if (rc != SLURM_SUCCESS) {
		/* Shutdown signal failed. Try to reap slurmscriptd now. */
		if (waitpid(slurmscriptd_pid, &status, WNOHANG) == 0) {
			/*
			 * slurmscriptd is not reaped and we cannot send a
			 * shutdown signal to slurmscriptd; kill it so we know
			 * that we won't wait forever.
			 */
			run_command_waitpid_timeout("slurmscriptd",
						    slurmscriptd_pid,
						    &status, 10 * MSEC_IN_SEC,
						    0, 0, NULL);
		}
	} else {
		while (waitpid(slurmscriptd_pid, &status, 0) < 0) {
			if (errno == EINTR)
				continue;
			error("%s: Unable to reap slurmscriptd child process",
			      __func__);
			break;
		}
	}
}

/*
 * Initialize a run_script_msg_t. This doesn't set all fields in
 * run_script_msg_t but sets the ones most likely to just be duplicate code
 * everywhere else. Use this when you need to allocate run_script_msg on the
 * heap.
 *
 * Return a heap allocated structure that must be free'd with
 * slurmscriptd_free_run_script_msg().
 */
static run_script_msg_t *_init_run_script_msg(char **env,
					      char *script_name,
					      char *script_path,
					      script_type_t script_type,
					      uint32_t timeout)
{
	run_script_msg_t *run_script_msg;

	run_script_msg = xmalloc(sizeof(*run_script_msg));
	run_script_msg->env = env_array_copy((const char **) env);
	run_script_msg->script_name = xstrdup(script_name);
	run_script_msg->script_path = xstrdup(script_path);
	run_script_msg->script_type = script_type;
	run_script_msg->timeout = timeout;

	return run_script_msg;
}

static job_info_msg_t *_unpack_bb_job_info(bb_script_info_msg_t *bb_msg)
{
	buf_t *extra_buf;
	slurm_msg_t *extra_msg;
	job_info_msg_t *job_info;

	if (!bb_msg->extra_buf_size)
		return NULL;

	extra_msg = xmalloc(sizeof *extra_msg);
	slurm_msg_t_init(extra_msg);
	extra_msg->protocol_version = SLURM_PROTOCOL_VERSION;
	extra_msg->msg_type = RESPONSE_JOB_INFO;
	extra_buf = create_buf(bb_msg->extra_buf, bb_msg->extra_buf_size);
	unpack_msg(extra_msg, extra_buf);
	job_info = extra_msg->data;
	extra_msg->data = NULL;

	/* create_buf() does not duplicate the data, it just points to it. */
	extra_buf->head = NULL;
	FREE_NULL_BUFFER(extra_buf);
	slurm_free_msg(extra_msg);

	return job_info;
}

static void _init_bb_script_config(char **function, uint32_t *job_id,
				   job_info_msg_t **job_info)
{
	bb_script_info_msg_t *bb_msg = NULL;

	if (_recv_bb_script_msg(&bb_msg) != SLURM_SUCCESS)
		fatal("Failed to receive burst buffer script msg");
	if (!bb_msg->function || !bb_msg->function[0])
		fatal_abort("%s: Invalid NULL function", __func__);

	*function = bb_msg->function;
	*job_id = bb_msg->job_id;
	*job_info = _unpack_bb_job_info(bb_msg);

	slurm_conf.cluster_name = bb_msg->cluster_name;
	slurm_conf.slurmctld_debug = bb_msg->slurmctld_debug;
	slurm_conf.slurmctld_logfile = bb_msg->slurmctld_logfile;
	slurm_conf.log_fmt = bb_msg->log_fmt;
	slurm_conf.plugindir = bb_msg->plugindir;
	slurm_conf.slurm_user_name = bb_msg->slurm_user_name;
	slurm_conf.slurm_user_id = bb_msg->slurm_user_id;

	/*
	 * We copied the pointers in bb_msg, so only free bb_msg, not its
	 * contents.
	 */
	xfree(bb_msg);
}

extern void slurmscriptd_flush(void)
{
	_send_to_slurmscriptd(SLURMSCRIPTD_REQUEST_FLUSH, NULL, true, NULL,
			      NULL);

	_wait_for_all_scripts();
}

extern void slurmscriptd_handle_bb_lua_mode(int argc, char **argv)
{
	int exit_code = 127;
	char *function = NULL;
	uint32_t job_id = 0;
	char *proc_name = "burst_buffer.lua";
	char *resp = NULL;
	char **script_argv = NULL;
	int script_argc = 0;
	/* The lock is required for update_logging() */
	slurmctld_lock_t config_write_lock = {
		.conf = WRITE_LOCK,
	};
	/*
	 * Only log errors until we read the config and update to the configured
	 * level.
	 */
	log_options_t log_opts = LOG_OPTS_STDERR_ONLY;
	job_info_msg_t *job_info = NULL;

	setpgid(0, 0);
	closeall(3); /* Do this before initializing logging */
	/* coverity[leaked_handle] */

	/* Logging will go to stdout/stderr until we call update_logging(). */
	log_init(proc_name, log_opts, LOG_DAEMON, NULL);

	/*
	 * Change our process name and make it so running_in_slurmctld() and
	 * run_in_daemon() return false.
	 */
	if (argc < RUN_COMMAND_LAUNCHER_ARGC) {
		fatal("%s: Unexpected argc=%d, it should be >= %d",
		      __func__, argc, RUN_COMMAND_LAUNCHER_ARGC);
	}

	script_argc = argc - RUN_COMMAND_LAUNCHER_ARGC;
	/* _change_proc_name overwrites argv. Copy the args that we need. */
	script_argv = slurm_char_array_copy(script_argc,
					    &argv[RUN_COMMAND_LAUNCHER_ARGC]);
	_change_proc_name(argc, argv, proc_name);

	/* Minimal config setup: */
	init_slurm_conf(&slurm_conf);
	_init_bb_script_config(&function, &job_id, &job_info);

	/*
	 * Initialize plugins.
	 */
	slurm_conf.bb_type = "burst_buffer/lua";
	if (bb_g_init() != SLURM_SUCCESS)
		fatal("failed to initialize burst_buffer plugin");

	/*
	 * update_logging() makes logs go to the slurmctld log file.
	 * Call update_logging() after initializing plugins to avoid seeing some
	 * extra debug logs about loading plugins.
	 */
	lock_slurmctld(config_write_lock);
	update_logging();
	unlock_slurmctld(config_write_lock);

	/* Run the script */
	exit_code = bb_g_run_script(function, job_id, script_argc, script_argv,
				    job_info, &resp);
	if (resp)
		safe_write(STDOUT_FILENO, resp, strlen(resp));

	/* Ignore memory leaks because we are calling exit() */
rwfail:
	exit(exit_code);
}

extern void slurmscriptd_flush_job(uint32_t job_id)
{
	flush_job_msg_t *msg = xmalloc(sizeof(*msg));
	slurmscriptd_msg_t *send_args = xmalloc(sizeof(*send_args));

	msg->job_id = job_id;
	send_args->msg_data = msg;
	send_args->msg_type = SLURMSCRIPTD_REQUEST_FLUSH_JOB;

	slurm_thread_create_detached(_async_send_to_slurmscriptd, send_args);
}

extern int slurmscriptd_run_mail(char *script_path, uint32_t argc, char **argv,
				 char **env, uint32_t timeout, char **resp)
{
	int status;
	run_script_msg_t run_script_msg;

	memset(&run_script_msg, 0, sizeof(run_script_msg));

	/* Init run_script_msg */
	run_script_msg.argc = argc;
	run_script_msg.argv = argv;
	run_script_msg.env = env;
	run_script_msg.script_name = "MailProg";
	run_script_msg.script_path = script_path;
	run_script_msg.script_type = SLURMSCRIPTD_MAIL;
	run_script_msg.timeout = timeout;

	/* Send message; wait for response */
	status = _send_to_slurmscriptd(SLURMSCRIPTD_REQUEST_RUN_SCRIPT,
				       &run_script_msg, true, resp, NULL);

	/* Cleanup */
	return status;
}

extern void slurmscriptd_run_power(char *script_path, char *hosts,
				   char *features, uint32_t job_id,
				   char *script_name, uint32_t timeout,
				   char *tmp_file_env_name, char *tmp_file_str)
{
	run_script_msg_t *run_script_msg;
	slurmscriptd_msg_t *send_args = xmalloc(sizeof(*send_args));
	int argc;
	char **env, **argv;

	argc = 3;
	argv = xcalloc(argc + 1, sizeof(char*)); /* Null terminated */
	argv[0] = xstrdup(script_path);
	argv[1] = xstrdup(hosts);
	argv[2] = xstrdup(features);

	env = env_array_create();
	env_array_append(&env, "SLURM_CONF", slurm_conf.slurm_conf);
	if (job_id)
		env_array_append_fmt(&env, "SLURM_JOB_ID", "%u", job_id);

	/* Init run_script_msg */
	run_script_msg = _init_run_script_msg(NULL, script_name, script_path,
					      SLURMSCRIPTD_POWER, timeout);

	run_script_msg->argc = argc;
	run_script_msg->argv = argv;
	run_script_msg->env = env;
	run_script_msg->job_id = job_id;
	run_script_msg->tmp_file_env_name = xstrdup(tmp_file_env_name);
	run_script_msg->tmp_file_str = xstrdup(tmp_file_str);

	/* Send message; don't wait for response */
	send_args->msg_data = run_script_msg;
	send_args->msg_type = SLURMSCRIPTD_REQUEST_RUN_SCRIPT;
	slurm_thread_create_detached(_async_send_to_slurmscriptd, send_args);
}

extern int slurmscriptd_run_bb_lua(uint32_t job_id, char *function,
				   uint32_t argc, char **argv, uint32_t timeout,
				   buf_t *job_buf, char **resp,
				   bool *track_script_signalled)
{
	int status, rc = SLURM_ERROR;
	uint32_t extra_buf_size = job_buf ? job_buf->processed : 0;
	run_script_msg_t run_script_msg;

	memset(&run_script_msg, 0, sizeof(run_script_msg));

	/* Init run_script_msg */
	run_script_msg.argc = argc;
	run_script_msg.argv = argv;
	run_script_msg.extra_buf = job_buf ? job_buf->head : NULL;
	run_script_msg.extra_buf_size = extra_buf_size;
	run_script_msg.job_id = job_id;
	run_script_msg.script_name = function; /* Shallow copy, do not free */
	run_script_msg.script_path = "burst_buffer.lua";
	run_script_msg.script_type = SLURMSCRIPTD_BB_LUA;
	run_script_msg.timeout = timeout;

	/* Send message; wait for response */
	status = _send_to_slurmscriptd(SLURMSCRIPTD_REQUEST_RUN_SCRIPT,
				       &run_script_msg, true, resp,
				       track_script_signalled);

	if (WIFEXITED(status))
		rc = WEXITSTATUS(status);
	else
		rc = SLURM_ERROR;

	return rc;
}

extern void slurmscriptd_run_prepilog(uint32_t job_id, bool is_epilog,
				      char *script, char **env)
{
	run_script_msg_t *run_script_msg;
	slurmscriptd_msg_t *send_args = xmalloc(sizeof(*send_args));
	char *script_name;
	script_type_t script_type;

	if (is_epilog) {
		script_name = "EpilogSlurmctld";
		script_type = SLURMSCRIPTD_EPILOG;
	} else {
		script_name = "PrologSlurmctld";
		script_type = SLURMSCRIPTD_PROLOG;
	}

	run_script_msg = _init_run_script_msg(env, script_name, script,
					      script_type,
					      slurm_conf.prolog_epilog_timeout);
	run_script_msg->argc = 1;
	run_script_msg->argv = xcalloc(2, sizeof(char *)); /* NULL terminated */
	run_script_msg->argv[0] = xstrdup(script);
	run_script_msg->job_id = job_id;

	/*
	 * Because this thread is holding the job write lock, do the write in
	 * a different detached thread so we do not lock up the slurmctld
	 * process if the write is blocked for some reason.
	 */
	send_args->msg_data = run_script_msg;
	send_args->msg_type = SLURMSCRIPTD_REQUEST_RUN_SCRIPT;
	slurm_thread_create_detached(_async_send_to_slurmscriptd, send_args);
}

extern int slurmscriptd_run_reboot(char *script_path, uint32_t argc,
				   char **argv)
{
	int status;

	run_script_msg_t run_script_msg;

	memset(&run_script_msg, 0, sizeof(run_script_msg));

	/* Init run_script_msg */
	run_script_msg.argc = argc;
	run_script_msg.argv = argv;
	run_script_msg.script_name = "RebootProgram";
	run_script_msg.script_path = script_path;
	run_script_msg.script_type = SLURMSCRIPTD_REBOOT;

	/* Send message; wait for response */
	status = _send_to_slurmscriptd(SLURMSCRIPTD_REQUEST_RUN_SCRIPT,
				       &run_script_msg, true, NULL, NULL);

	return status;
}

extern void slurmscriptd_run_resv(char *script_path, uint32_t argc, char **argv,
				  uint32_t timeout, char *script_name)
{
	run_script_msg_t *run_script_msg;
	slurmscriptd_msg_t *send_args = xmalloc(sizeof(*send_args));

	/* Init run_script_msg */
	run_script_msg = _init_run_script_msg(NULL, script_name, script_path,
					      SLURMSCRIPTD_RESV, timeout);
	run_script_msg->argc = argc;
	run_script_msg->argv = slurm_char_array_copy(argc, argv);

	/* Send message; don't wait for response */
	send_args->msg_data = run_script_msg;
	send_args->msg_type = SLURMSCRIPTD_REQUEST_RUN_SCRIPT;
	slurm_thread_create_detached(_async_send_to_slurmscriptd, send_args);
}

extern void slurmscriptd_update_debug_flags(uint64_t debug_flags)
{
	debug_flags_msg_t msg;

	memset(&msg, 0, sizeof(msg));

	msg.debug_flags = debug_flags;
	_send_to_slurmscriptd(SLURMSCRIPTD_REQUEST_UPDATE_DEBUG_FLAGS, &msg,
			      false, NULL, NULL);
}

extern void slurmscriptd_update_log_level(int debug_level, bool log_rotate)
{
	log_msg_t log_msg;

	memset(&log_msg, 0, sizeof(log_msg));

	log_msg.debug_level = (uint32_t) debug_level;
	log_msg.log_rotate = log_rotate;
	_send_to_slurmscriptd(SLURMSCRIPTD_REQUEST_UPDATE_LOG, &log_msg,
			      false, NULL, NULL);
}

extern int slurmscriptd_init(char **argv, char *binary_path)
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
		 * If there is a failure with startup, SIGKILL the slurmscriptd
		 * and then exit. The slurmscriptd pid will be adopted and then
		 * reaped by init, so we don't need to call waitpid().
		 */
		if (close(to_slurmscriptd[0]) < 0) {
			rc = errno;
			killpg(slurmscriptd_pid, SIGKILL);
			errno = rc;
			fatal("%s: slurmctld: Unable to close read to_slurmscriptd in parent: %m",
			      __func__);
		}
		if (close(to_slurmctld[1]) < 0) {
			rc = errno;
			killpg(slurmscriptd_pid, SIGKILL);
			errno = rc;
			fatal("%s: slurmctld: Unable to close write to_slurmctld in parent: %m",
			      __func__);
		}

		/* Test communications with slurmscriptd. */
		i = read(slurmctld_readfd, &rc, sizeof(int));
		if (i < 0) {
			rc = errno;
			killpg(slurmscriptd_pid, SIGKILL);
			errno = rc;
			fatal_abort("%s: slurmctld: Can not read return code from slurmscriptd: %m",
				    __func__);
		} else if (i != sizeof(int)) {
			rc = errno;
			killpg(slurmscriptd_pid, SIGKILL);
			errno = rc;
			fatal_abort("%s: slurmctld: slurmscriptd failed to send return code: %m",
				    __func__);
		}
		if (rc != SLURM_SUCCESS) {
			killpg(slurmscriptd_pid, SIGKILL);
			fatal_abort("%s: slurmctld: slurmscriptd did not initialize",
				    __func__);
		}
		ack = SLURM_SUCCESS;
		i = write(slurmctld_writefd, &ack, sizeof(int));
		if (i != sizeof(int)) {
			rc = errno;
			killpg(slurmscriptd_pid, SIGKILL);
			errno = rc;
			fatal_abort("%s: slurmctld: failed to send ack to slurmscriptd: %m",
				    __func__);
		}

		/* Get slurmscriptd initialization status */
		i = read(slurmctld_readfd, &rc, sizeof(int));
		if (i < 0)
			fatal("%s: Cannot read slurmscriptd initialization code",
			      __func__);
		if (rc != SLURM_SUCCESS)
			fatal("%s: slurmscriptd initialization failed",
			      __func__);


		slurm_mutex_init(&script_count_mutex);
		slurm_mutex_init(&write_mutex);
		slurm_mutex_init(&script_resp_map_mutex);
		script_resp_map = xhash_init(_resp_map_key_id, _resp_map_free);
		_setup_eio(slurmctld_readfd);
		slurm_thread_create(&slurmctld_listener_tid,
				    _slurmctld_listener_thread, NULL);
		debug("slurmctld: slurmscriptd fork()'d and initialized.");
	} else { /* child (slurmscriptd_pid == 0) */
		/*
		 * Dup needed file descriptors and re-exec self.
		 * We do not need to closeall() here because it will happen on
		 * the re-exec.
		 */
		dup2(slurmscriptd_readfd, SLURMSCRIPT_READ_FD);
		dup2(slurmscriptd_writefd, SLURMSCRIPT_WRITE_FD);
		setenv(SLURMSCRIPTD_MODE_ENV, "1", 1);
		execv(binary_path, argv);
		fatal("%s: execv() failed: %m", __func__);
		/* Never returns */
	}

	return SLURM_SUCCESS;
}

extern int slurmscriptd_fini(void)
{
	debug("%s starting", __func__);
	_kill_slurmscriptd();

	/* Now shutdown communications. */
	eio_signal_shutdown(msg_handle);
	slurm_thread_join(slurmctld_listener_tid);
	slurm_mutex_destroy(&script_resp_map_mutex);
	xhash_clear(script_resp_map);
	slurm_mutex_destroy(&write_mutex);
	(void) close(slurmctld_writefd);
	(void) close(slurmctld_readfd);

	debug("%s complete", __func__);

	return SLURM_SUCCESS;
}
