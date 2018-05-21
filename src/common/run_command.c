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

#if defined(__FreeBSD__) || defined(__NetBSD__)
#define POLLRDHUP POLLHUP
#endif

#include "src/common/macros.h"
#include "src/common/timers.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/common/run_command.h"

static int shutdown = 0;
static int child_proc_count = 0;
static pthread_mutex_t proc_count_mutex = PTHREAD_MUTEX_INITIALIZER;

#define MAX_POLL_WAIT 500

/* used to terminate any outstanding commands */
extern void run_command_shutdown(void)
{
	shutdown = 1;
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

/* Execute a script, wait for termination and return its stdout.
 * script_type IN - Type of program being run (e.g. "StartStageIn")
 * script_path IN - Fully qualified pathname of the program to execute
 * script_args IN - Arguments to the script
 * max_wait IN - Maximum time to wait in milliseconds,
 *		 -1 for no limit (asynchronous)
 * status OUT - Job exit code
 * Return stdout+stderr of spawned program, value must be xfreed. */
extern char *run_command(char *script_type, char *script_path,
			   char **script_argv, int max_wait, int *status)
{
	int i, new_wait, resp_size = 0, resp_offset = 0;
	pid_t cpid;
	char *resp = NULL;
	int pfd[2] = { -1, -1 };

	if ((script_path == NULL) || (script_path[0] == '\0')) {
		error("%s: no script specified", __func__);
		*status = 127;
		resp = xstrdup("Run command failed - configuration error");
		return resp;
	}
	if (script_path[0] != '/') {
		error("%s: %s is not fully qualified pathname (%s)",
		      __func__, script_type, script_path);
		*status = 127;
		resp = xstrdup("Run command failed - configuration error");
		return resp;
	}
	if (access(script_path, R_OK | X_OK) < 0) {
		error("%s: %s can not be executed (%s) %m",
		      __func__, script_type, script_path);
		*status = 127;
		resp = xstrdup("Run command failed - configuration error");
		return resp;
	}
	if (max_wait != -1) {
		if (pipe(pfd) != 0) {
			error("%s: pipe(): %m", __func__);
			*status = 127;
			resp = xstrdup("System error");
			return resp;
		}
	}
	slurm_mutex_lock(&proc_count_mutex);
	child_proc_count++;
	slurm_mutex_unlock(&proc_count_mutex);
	if ((cpid = fork()) == 0) {
		int cc;

		cc = sysconf(_SC_OPEN_MAX);
		if (max_wait != -1) {
			dup2(pfd[1], STDERR_FILENO);
			dup2(pfd[1], STDOUT_FILENO);
			for (i = 0; i < cc; i++) {
				if ((i != STDERR_FILENO) &&
				    (i != STDOUT_FILENO))
					close(i);
			}
		} else {
			for (i = 0; i < cc; i++)
				close(i);
			if ((cpid = fork()) < 0)
				exit(127);
			else if (cpid > 0)
				exit(0);
		}
		setpgid(0, 0);
		execv(script_path, script_argv);
		error("%s: execv(%s): %m", __func__, script_path);
		exit(127);
	} else if (cpid < 0) {
		if (max_wait != -1) {
			close(pfd[0]);
			close(pfd[1]);
		}
		error("%s: fork(): %m", __func__);
		slurm_mutex_lock(&proc_count_mutex);
		child_proc_count--;
		slurm_mutex_unlock(&proc_count_mutex);
	} else if (max_wait != -1) {
		struct pollfd fds;
		struct timeval tstart;
		resp_size = 1024;
		resp = xmalloc(resp_size);
		close(pfd[1]);
		gettimeofday(&tstart, NULL);
		while (1) {
			if (shutdown) {
				error("%s: killing %s operation on shutdown",
				      __func__, script_type);
				break;
			}
			fds.fd = pfd[0];
			fds.events = POLLIN | POLLHUP | POLLRDHUP;
			fds.revents = 0;
			if (max_wait <= 0) {
				new_wait = MAX_POLL_WAIT;
			} else {
				new_wait = max_wait - _tot_wait(&tstart);
				if (new_wait <= 0) {
					error("%s: %s poll timeout @ %d msec",
					      __func__, script_type, max_wait);
					break;
				}
				new_wait = MIN(new_wait, MAX_POLL_WAIT);
			}
			i = poll(&fds, 1, new_wait);
			if (i == 0) {
				continue;
			} else if (i < 0) {
				error("%s: %s poll:%m", __func__, script_type);
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
		killpg(cpid, SIGTERM);
		usleep(10000);
		killpg(cpid, SIGKILL);
		waitpid(cpid, status, 0);
		close(pfd[0]);
		slurm_mutex_lock(&proc_count_mutex);
		child_proc_count--;
		slurm_mutex_unlock(&proc_count_mutex);
	} else {
		waitpid(cpid, status, 0);
	}
	return resp;
}

extern void free_command_argv(char **script_argv)
{
	int i;

	for (i = 0; script_argv[i]; i++)
	        xfree(script_argv[i]);
	xfree(script_argv);
}
