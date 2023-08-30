/*****************************************************************************\
 *  run_command.c - run a command asynchronously and return output
 *****************************************************************************
 *  Copyright (C) 2014-2017 SchedMD LLC.
 *  Written by Morris Jette <jette@schedmd.com>
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
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <inttypes.h>		/* for uint16_t, uint32_t definitions */

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__)
#define POLLRDHUP POLLHUP
#endif

#include "src/common/fd.h"
#include "src/common/macros.h"
#include "src/common/timers.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/list.h"
#include "src/common/run_command.h"

static int command_shutdown = 0;
static int child_proc_count = 0;
static pthread_mutex_t proc_count_mutex = PTHREAD_MUTEX_INITIALIZER;

#define MAX_POLL_WAIT 500

extern void run_command_add_to_script(char **script_body, char *new_str)
{
	char *orig_script = *script_body;
	char *new_script, *sep, save_char;
	char *tmp_str = NULL;
	int i;

	if (!new_str || (new_str[0] == '\0'))
		return;	/* Nothing to prepend */

	if (!orig_script) {
		*script_body = xstrdup(new_str);
		return;
	}

	tmp_str = xstrdup(new_str);
	i = strlen(tmp_str) - 1;
	if (tmp_str[i] != '\n')	/* Append new line as needed */
		xstrcat(tmp_str, "\n");

	if (orig_script[0] != '#') {
		/* Prepend new lines */
		new_script = xstrdup(tmp_str);
		xstrcat(new_script, orig_script);
		xfree(*script_body);
		*script_body = new_script;
		xfree(tmp_str);
		return;
	}

	sep = strchr(orig_script, '\n');
	if (sep) {
		save_char = sep[1];
		sep[1] = '\0';
		new_script = xstrdup(orig_script);
		xstrcat(new_script, tmp_str);
		sep[1] = save_char;
		xstrcat(new_script, sep + 1);
		xfree(*script_body);
		*script_body = new_script;
		xfree(tmp_str);
		return;
	} else {
		new_script = xstrdup(orig_script);
		xstrcat(new_script, "\n");
		xstrcat(new_script, tmp_str);
		xfree(*script_body);
		*script_body = new_script;
		xfree(tmp_str);
		return;
	}
}

/* used to initialize run_command module */
extern void run_command_init(void)
{
	command_shutdown = 0;
}

/* used to terminate any outstanding commands */
extern void run_command_shutdown(void)
{
	command_shutdown = 1;
}

/* Return count of child processes */
extern int run_command_count(void)
{
	int cnt;

	slurm_mutex_lock(&proc_count_mutex);
	cnt = child_proc_count;
	slurm_mutex_unlock(&proc_count_mutex);

	return cnt;
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

static void _kill_pg(pid_t pid)
{
	killpg(pid, SIGTERM);
	usleep(10000);
	killpg(pid, SIGKILL);
}

extern char *run_command(run_command_args_t *args)
{
	pid_t cpid;
	char *resp = NULL;
	int pfd[2] = { -1, -1 };

	if ((args->script_path == NULL) || (args->script_path[0] == '\0')) {
		error("%s: no script specified", __func__);
		*(args->status) = 127;
		resp = xstrdup("Run command failed - configuration error");
		return resp;
	}
	if (args->script_path[0] != '/') {
		error("%s: %s is not fully qualified pathname (%s)",
		      __func__, args->script_type, args->script_path);
		*(args->status) = 127;
		resp = xstrdup("Run command failed - configuration error");
		return resp;
	}
	if (access(args->script_path, R_OK | X_OK) < 0) {
		error("%s: %s can not be executed (%s) %m",
		      __func__, args->script_type, args->script_path);
		*(args->status) = 127;
		resp = xstrdup("Run command failed - configuration error");
		return resp;
	}
	if (!args->turnoff_output) {
		if (pipe(pfd) != 0) {
			error("%s: pipe(): %m", __func__);
			*(args->status) = 127;
			resp = xstrdup("System error");
			return resp;
		}
	}
	slurm_mutex_lock(&proc_count_mutex);
	child_proc_count++;
	slurm_mutex_unlock(&proc_count_mutex);
	if ((cpid = fork()) == 0) {
		/*
		 * container_g_join() needs to be called in the child process
		 * to avoid a race condition if this process makes a file
		 * before we add the pid to the container in the parent.
		 */
		if (args->container_join &&
		    ((*(args->container_join))(args->job_id, getuid()) !=
		     SLURM_SUCCESS))
			error("container_g_join(%u): %m", args->job_id);

		if (!args->turnoff_output) {
			int devnull;
			if ((devnull = open("/dev/null", O_RDWR)) < 0) {
				error("%s: Unable to open /dev/null: %m",
				      __func__);
				_exit(127);
			}
			dup2(devnull, STDIN_FILENO);
			dup2(pfd[1], STDERR_FILENO);
			dup2(pfd[1], STDOUT_FILENO);
			closeall(3);
		} else {
			closeall(0);
		}
		setpgid(0, 0);
		/*
		 * sync euid -> ruid, egid -> rgid to avoid issues with fork'd
		 * processes using access() or similar calls.
		 */
		if (setresgid(getegid(), getegid(), -1)) {
			error("%s: Unable to setresgid()", __func__);
			_exit(127);
		}
		if (setresuid(geteuid(), geteuid(), -1)) {
			error("%s: Unable to setresuid()", __func__);
			_exit(127);
		}
		if (!args->env)
			execv(args->script_path, args->script_argv);
		else
			execve(args->script_path, args->script_argv, args->env);
		error("%s: execv(%s): %m", __func__, args->script_path);
		_exit(127);
	} else if (cpid < 0) {
		if (!args->turnoff_output) {
			close(pfd[0]);
			close(pfd[1]);
		}
		error("%s: fork(): %m", __func__);
		slurm_mutex_lock(&proc_count_mutex);
		child_proc_count--;
		slurm_mutex_unlock(&proc_count_mutex);
	} else if (!args->turnoff_output) {
		close(pfd[1]);
		if (args->tid)
			track_script_reset_cpid(args->tid, cpid);
		resp = run_command_poll_child(cpid,
					      args->max_wait,
					      args->orphan_on_shutdown,
					      pfd[0],
					      args->script_path,
					      args->script_type,
					      args->tid,
					      args->status,
					      args->timed_out);
		close(pfd[0]);
		slurm_mutex_lock(&proc_count_mutex);
		child_proc_count--;
		slurm_mutex_unlock(&proc_count_mutex);
	} else {
		if (args->tid)
			track_script_reset_cpid(args->tid, cpid);
		waitpid(cpid, args->status, 0);
	}

	return resp;
}

extern char *run_command_poll_child(int cpid,
				    int max_wait,
				    bool orphan_on_shutdown,
				    int read_fd,
				    const char *script_path,
				    const char *script_type,
				    pthread_t tid,
				    int *status,
				    bool *timed_out)
{
	bool send_terminate = true;
	struct pollfd fds;
	struct timeval tstart;
	int resp_size = 1024, resp_offset = 0;
	int new_wait;
	int i;
	char *resp;

	resp = xmalloc(resp_size);
	gettimeofday(&tstart, NULL);

	while (1) {
		if (command_shutdown) {
			error("%s: %s %s operation on shutdown",
			      __func__,
			      orphan_on_shutdown ?
			      "orphaning" : "killing",
			      script_type);
			break;
		}

		/*
		 * Pass zero as the status to just see if this script
		 * exists in track_script - if not, then we need to bail
		 * since this script was killed.
		 */
		if (tid &&
		    track_script_killed(tid, 0, false))
			break;

		fds.fd = read_fd;
		fds.events = POLLIN | POLLHUP | POLLRDHUP;
		fds.revents = 0;
		if (max_wait <= 0) {
			new_wait = MAX_POLL_WAIT;
		} else {
			new_wait = max_wait - _tot_wait(&tstart);
			if (new_wait <= 0) {
				error("%s: %s poll timeout @ %d msec",
				      __func__, script_type,
				      max_wait);
				if (timed_out)
					*(timed_out) = true;
				break;
			}
			new_wait = MIN(new_wait, MAX_POLL_WAIT);
		}
		i = poll(&fds, 1, new_wait);

		if (i == 0) {
			continue;
		} else if (i < 0) {
			if ((errno == EAGAIN) || (errno == EINTR))
				continue;
			error("%s: %s poll:%m",
			      __func__, script_type);
			break;
		}
		if ((fds.revents & POLLIN) == 0) {
			send_terminate = false;
			break;
		}
		i = read(read_fd, resp + resp_offset,
			 resp_size - resp_offset);
		if (i == 0) {
			send_terminate = false;
			break;
		} else if (i < 0) {
			if (errno == EAGAIN)
				continue;
			send_terminate = false;
			error("%s: read(%s): %m",
			      __func__,
			      script_path);
			break;
		} else {
			resp_offset += i;
			if (resp_offset + 1024 >= resp_size) {
				resp_size *= 2;
				resp = xrealloc(resp, resp_size);
			}
		}
	}
	if (command_shutdown && orphan_on_shutdown) {
		/* Don't kill the script on shutdown */
		*status = 0;
	} else if (send_terminate) {
		/*
		 * Kill immediately if the script isn't exiting
		 * normally.
		 */
		_kill_pg(cpid);
		waitpid(cpid, status, 0);
	} else {
		/*
		 * If the STDOUT is closed from the script we may reach
		 * this point without any input in read_fd, so just wait
		 * for the process here until max_wait.
		 */
		run_command_waitpid_timeout(script_type,
					    cpid, status,
					    max_wait,
					    _tot_wait(&tstart),
					    tid, timed_out);
	}

	return resp;
}

/*
 * run_command_waitpid_timeout()
 *
 *  Same as waitpid(2) but kill process group for pid after timeout millisecs.
 */
extern int run_command_waitpid_timeout(
	const char *name, pid_t pid, int *pstatus, int timeout_ms,
	int elapsed_ms, pthread_t tid, bool *timed_out)
{
	int max_delay = 1000;		 /* max delay between waitpid calls */
	int delay = 10;			 /* initial delay */
	int rc;
	int options = WNOHANG;
	int save_timeout_ms = timeout_ms;
	bool killed_pg = false;

	if (timeout_ms <= 0 || timeout_ms == NO_VAL16)
		options = 0;
	timeout_ms -= elapsed_ms;

	while ((rc = waitpid (pid, pstatus, options)) <= 0) {
		if (rc < 0) {
			if (errno == EINTR)
				continue;
			error("%s: waitpid(%d): %m", __func__, pid);
			return -1;
		} else if (command_shutdown) {
			error("%s: killing %s on shutdown",
			      __func__, name);
			_kill_pg(pid);
			killed_pg = true;
			options = 0;
		} else if (tid && track_script_killed(tid, 0, false)) {
			/*
			 * Pass zero as the status to track_script_killed() to
			 * know if this script exists in track_script and bail
			 * if it does not.
			 */
			_kill_pg(pid);
			killed_pg = true;
			options = 0;
		} else if (timeout_ms <= 0) {
			error("%s%stimeout after %d ms: killing pgid %d",
			      name != NULL ? name : "",
			      name != NULL ? ": " : "",
			      save_timeout_ms, pid);
			_kill_pg(pid);
			killed_pg = true;
			options = 0;
			if (timed_out)
				*timed_out = true;
		} else {
			(void) poll(NULL, 0, delay);
			timeout_ms -= delay;
			delay = MIN (timeout_ms, MIN(max_delay, delay*2));
		}
	}

	if (!killed_pg)
		_kill_pg(pid); /* kill children too */
	return rc;
}
