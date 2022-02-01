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

extern char *run_command(run_command_args_t *args)
{
	int i, new_wait, resp_size = 0, resp_offset = 0;
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
	if (args->max_wait != -1) {
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
		if (args->max_wait != -1) {
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
			if ((cpid = fork()) < 0)
				_exit(127);
			else if (cpid > 0)
				_exit(0);
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
		if (args->max_wait != -1) {
			close(pfd[0]);
			close(pfd[1]);
		}
		error("%s: fork(): %m", __func__);
		slurm_mutex_lock(&proc_count_mutex);
		child_proc_count--;
		slurm_mutex_unlock(&proc_count_mutex);
	} else if (args->max_wait != -1) {
		struct pollfd fds;
		struct timeval tstart;
		resp_size = 1024;
		resp = xmalloc(resp_size);
		close(pfd[1]);
		gettimeofday(&tstart, NULL);
		if (args->tid)
			track_script_reset_cpid(args->tid, cpid);
		while (1) {
			if (command_shutdown) {
				error("%s: killing %s operation on shutdown",
				      __func__, args->script_type);
				break;
			}

			if (args->tid &&
			    track_script_broadcast(args->tid, *(args->status)))
				break;

			fds.fd = pfd[0];
			fds.events = POLLIN | POLLHUP | POLLRDHUP;
			fds.revents = 0;
			if (args->max_wait <= 0) {
				new_wait = MAX_POLL_WAIT;
			} else {
				new_wait = args->max_wait - _tot_wait(&tstart);
				if (new_wait <= 0) {
					error("%s: %s poll timeout @ %d msec",
					      __func__, args->script_type,
					      args->max_wait);
					if (args->timed_out)
						*(args->timed_out) = true;
					break;
				}
				new_wait = MIN(new_wait, MAX_POLL_WAIT);
			}
			i = poll(&fds, 1, new_wait);
			if (i == 0) {
				continue;
			} else if (i < 0) {
				error("%s: %s poll:%m",
				      __func__, args->script_type);
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
				error("%s: read(%s): %m", __func__,
				      args->script_path);
				break;
			} else {
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
		waitpid(cpid, args->status, 0);
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
