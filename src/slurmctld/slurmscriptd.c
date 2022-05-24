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
#include "src/common/run_command.h"
#include "src/common/setproctitle.h"
#include "src/common/track_script.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/slurmctld/burst_buffer.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/slurmscriptd.h"
#include "src/slurmctld/slurmscriptd_protocol_defs.h"
#include "src/slurmctld/slurmscriptd_protocol_pack.h"

#define MAX_POLL_WAIT 500 /* in milliseconds */

/*
 *****************************************************************************
 * The following are meant to be used by both slurmscriptd and slurmctld
 *****************************************************************************
 */

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

static void _incr_script_cnt(void);

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

	if (!running_in_slurmctld()) { /* Only do this for slurmscriptd */
		run_command_shutdown();
		track_script_flush();
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
	_write_msg(slurmctld_writefd, msg.msg_type, buffer);

	if (wait) {
		_wait_for_script_resp(script_resp, &rc, resp_msg, signalled);
		_script_resp_map_remove(script_resp->key);
	}

cleanup:
	FREE_NULL_BUFFER(buffer);

	return rc;
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
	_write_msg(slurmscriptd_writefd, msg.msg_type, buffer);

cleanup:
	FREE_NULL_BUFFER(buffer);
	return rc;
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
 * Run a script with a given timeout (in seconds).
 * Return the status or SLURM_ERROR if fork() fails.
 */
static int _run_script(run_command_args_t *run_command_args, uint32_t job_id,
		       int timeout, char **resp_msg, bool *signalled)
{
	int status = SLURM_ERROR;
	int ms_timeout;
	char *resp = NULL;
	bool killed;

	if ((timeout <= 0) || (timeout == NO_VAL16))
		ms_timeout = -1; /* wait indefinitely in run_command() */
	else
		ms_timeout = timeout * 1000;

	run_command_args->max_wait = ms_timeout;
	run_command_args->status = &status;

	track_script_rec_add(job_id, 0, pthread_self());
	resp = run_command(run_command_args);
	if ((killed = track_script_killed(pthread_self(), status, true))) {
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
	track_script_remove(pthread_self());

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
	run_command_shutdown();
	track_script_flush();

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

static int _handle_reconfig(slurmscriptd_msg_t *recv_msg)
{
	slurmctld_lock_t config_write_lock =
		{ .conf = WRITE_LOCK };
	reconfig_msg_t *reconfig_msg = recv_msg->msg_data;

	log_flag(SCRIPT, "Handling %s", rpc_num2string(recv_msg->msg_type));

	lock_slurmctld(config_write_lock);
	slurm_conf.debug_flags = reconfig_msg->debug_flags;
	xfree(slurm_conf.slurmctld_logfile);
	slurm_conf.slurmctld_logfile = xstrdup(reconfig_msg->logfile);
	slurm_conf.log_fmt = reconfig_msg->log_fmt;
	slurm_conf.slurmctld_debug = reconfig_msg->slurmctld_debug;
	slurm_conf.slurmctld_syslog_debug = reconfig_msg->syslog_debug;
	update_logging();
	unlock_slurmctld(config_write_lock);

	return SLURM_SUCCESS;
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
	int status = 0;
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
			/*
			 * Pass zero as the status to just see if this script
			 * exists in track_script - if not, then we need to bail
			 * since this script was killed.
			 */
			if (track_script_killed(pthread_self(), 0, false))
				break;

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
			track_script_killed(pthread_self(), status, true);

		track_script_remove(pthread_self());
	}

	if (got_resp)
		*resp_msg = resp;
	else
		xfree(resp);

	return status;
}

static int _handle_shutdown(slurmscriptd_msg_t *recv_msg)
{
	log_flag(SCRIPT, "Handling %s", rpc_num2string(recv_msg->msg_type));
	/* Kill all running scripts. */
	run_command_shutdown();
	track_script_flush();

	eio_signal_shutdown(msg_handle);

	return SLURM_ERROR; /* Don't handle any more requests. */
}

static int _handle_run_script(slurmscriptd_msg_t *recv_msg)
{
	run_script_msg_t *script_msg = recv_msg->msg_data;
	int rc, status = 0;
	char *resp_msg = NULL;
	bool signalled = false;
	bool timed_out = false;
	pthread_t tid = pthread_self();
	run_command_args_t run_command_args = {
		.env = script_msg->env,
		.script_argv = script_msg->argv,
		.script_path = script_msg->script_path,
		.script_type = script_msg->script_name,
		.tid = tid,
		.timed_out = &timed_out,
	};

	switch (script_msg->script_type) {
	case SLURMSCRIPTD_BB_LUA:
		log_flag(SCRIPT, "Handling %s (burst_buffer.lua:%s) for JobId=%u: timeout=%u seconds, argc=%u, key=%s",
			 rpc_num2string(recv_msg->msg_type),
			 script_msg->script_name, script_msg->job_id,
			 script_msg->timeout, script_msg->argc, recv_msg->key);
		status = _run_bb_script(script_msg->script_name,
					script_msg->job_id,
					script_msg->timeout, script_msg->argc,
					script_msg->argv, &resp_msg,
					&signalled);
		break;
	case SLURMSCRIPTD_EPILOG: /* fall-through */
	case SLURMSCRIPTD_MAIL:
	case SLURMSCRIPTD_PROLOG:
		if (script_msg->job_id)
			log_flag(SCRIPT, "Handling %s (%s) for JobId=%u",
				 rpc_num2string(recv_msg->msg_type),
				 script_msg->script_name, script_msg->job_id);
		else
			log_flag(SCRIPT, "Handling %s (%s)",
				 rpc_num2string(recv_msg->msg_type),
				 script_msg->script_name);
		/*
		 * script_msg->timeout is in seconds but
		 * run_command_args.max_wait expects milliseconds.
		 * script_msg->timeout may also not be set (NO_VAL16).
		 * Let _run_script handle the conversion.
		 */
		status = _run_script(&run_command_args, script_msg->job_id,
				     script_msg->timeout,
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
	xfree(resp_msg);

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
		script_resp->resp_msg = script_complete->resp_msg;
		script_complete->resp_msg = NULL;
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

	switch (script_complete->script_type) {
	case SLURMSCRIPTD_BB_LUA:
	case SLURMSCRIPTD_MAIL:
		if (script_complete->job_id)
			log_flag(SCRIPT, "Handling %s (%s) for JobId=%u",
				 rpc_num2string(msg->msg_type),
				 script_complete->script_name,
				 script_complete->job_id);
		else
			log_flag(SCRIPT, "Handling %s (%s)",
				 rpc_num2string(msg->msg_type),
				 script_complete->script_name);
		break; /* Nothing more to do */
	case SLURMSCRIPTD_EPILOG:
		log_flag(SCRIPT, "Handling %s (%s) for JobId=%u",
			 rpc_num2string(msg->msg_type),
			 script_complete->script_name, script_complete->job_id);
		prep_epilog_slurmctld_callback(script_complete->status,
					       script_complete->job_id,
					       script_complete->timed_out);
		break;
	case SLURMSCRIPTD_PROLOG:
		log_flag(SCRIPT, "Handling %s (%s) for JobId=%u",
			 rpc_num2string(msg->msg_type),
			 script_complete->script_name, script_complete->job_id);
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
		log_flag(SCRIPT, "Handling %s: Received response from %s",
			 rpc_num2string(msg->msg_type),
			 script_complete->script_name);
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
		case SLURMSCRIPTD_REQUEST_RECONFIG:
			rc = _handle_reconfig(&recv_msg);
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
	run_command_init();
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
	_send_to_slurmscriptd(SLURMSCRIPTD_SHUTDOWN, NULL, false, NULL, NULL);
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
	_send_to_slurmscriptd(SLURMSCRIPTD_REQUEST_FLUSH, NULL, true, NULL,
			      NULL);
}

extern void slurmscriptd_flush_job(uint32_t job_id)
{
	flush_job_msg_t msg;

	msg.job_id = job_id;

	_send_to_slurmscriptd(SLURMSCRIPTD_REQUEST_FLUSH_JOB, &msg, false,
			      NULL, NULL);
}

extern void slurmscriptd_reconfig(void)
{
	reconfig_msg_t msg;
	slurmctld_lock_t config_read_lock =
		{ .conf = READ_LOCK };

	memset(&msg, 0, sizeof(msg));

	/*
	 * slurmscriptd only needs a minimal configuration, so only send what
	 * needs to be updated rather than sending the entire slurm_conf
	 * or having slurmscriptd read/parse the slurm.conf file.
	 */
	lock_slurmctld(config_read_lock);
	msg.debug_flags = slurm_conf.debug_flags;
	msg.logfile = slurm_conf.slurmctld_logfile;
	msg.log_fmt = slurm_conf.log_fmt;
	msg.slurmctld_debug = slurm_conf.slurmctld_debug;
	msg.syslog_debug = slurm_conf.slurmctld_syslog_debug;
	/*
	 * If we ever allow switching plugins on reconfig, then we will need to
	 * pass slurm_conf.bb_type to slurmscriptd, since a child/fork() of
	 * slurmscriptd calls bb_g_run_script().
	 */
	unlock_slurmctld(config_read_lock);

	_send_to_slurmscriptd(SLURMSCRIPTD_REQUEST_RECONFIG, &msg, false,
			      NULL, NULL);
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

extern int slurmscriptd_run_bb_lua(uint32_t job_id, char *function,
				   uint32_t argc, char **argv, uint32_t timeout,
				   char **resp, bool *track_script_signalled)
{
	int status, rc;
	run_script_msg_t run_script_msg;

	memset(&run_script_msg, 0, sizeof(run_script_msg));

	/* Init run_script_msg */
	run_script_msg.argc = argc;
	run_script_msg.argv = argv;
	run_script_msg.env = NULL;
	run_script_msg.job_id = job_id;
	run_script_msg.script_name = function; /* Shallow copy, do not free */
	run_script_msg.script_path = NULL;
	run_script_msg.script_type = SLURMSCRIPTD_BB_LUA;
	run_script_msg.timeout = timeout;

	/* Send message; wait for response */
	status = _send_to_slurmscriptd(SLURMSCRIPTD_REQUEST_RUN_SCRIPT,
				       &run_script_msg, true, resp,
				       track_script_signalled);

	/* Cleanup */
	if (WIFEXITED(status))
		rc = WEXITSTATUS(status);
	else
		rc = SLURM_ERROR;

	return rc;
}

extern void slurmscriptd_run_prepilog(uint32_t job_id, bool is_epilog,
				      char *script, char **env)
{
	run_script_msg_t run_script_msg;

	memset(&run_script_msg, 0, sizeof(run_script_msg));

	run_script_msg.argc = 1;
	run_script_msg.argv = xcalloc(2, sizeof(char *)); /* NULL terminated */
	run_script_msg.argv[0] = script;

	run_script_msg.env = env;
	run_script_msg.job_id = job_id;
	if (is_epilog) {
		run_script_msg.script_name = "EpilogSlurmctld";
		run_script_msg.script_type = SLURMSCRIPTD_EPILOG;
	} else {
		run_script_msg.script_name = "PrologSlurmctld";
		run_script_msg.script_type = SLURMSCRIPTD_PROLOG;
	}
	run_script_msg.script_path = script;
	run_script_msg.timeout = (uint32_t) slurm_conf.prolog_epilog_timeout;

	_send_to_slurmscriptd(SLURMSCRIPTD_REQUEST_RUN_SCRIPT, &run_script_msg,
			      false, NULL, NULL);

	/* Don't free argv[0], since we did not xstrdup that. */
	xfree(run_script_msg.argv);
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
