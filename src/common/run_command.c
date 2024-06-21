/*****************************************************************************\
 *  run_command.c - run a command asynchronously and return output
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
#include <fcntl.h>
#include <inttypes.h> /* for uint16_t, uint32_t definitions */
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef POLLRDHUP
#define POLLRDHUP POLLHUP
#endif

#include "src/common/fd.h"
#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/common/read_config.h"
#include "src/common/run_command.h"
#include "src/common/timers.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

static char *script_launcher = NULL;
static int script_launcher_fd = -1;
static int command_shutdown = 0;
static int child_proc_count = 0;
static pthread_mutex_t proc_count_mutex = PTHREAD_MUTEX_INITIALIZER;

#define MAX_POLL_WAIT 500

/* Function prototypes */
static void _run_command_child_exec(int fd, const char *path, char **argv,
				    char **env);
static void _run_command_child_pre_exec(void);

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
extern int run_command_init(int argc, char **argv, char *binary)
{
	command_shutdown = 0;

#if defined(__linux__)
	if (!binary && !script_launcher)
		binary = "/proc/self/exe";
#endif /* !__linux__ */

	/* Use argv[0] as fallback with absolute path */
	if (!binary && (argc > 0) && (argv[0][0] == '/'))
		binary = argv[0];

	if (!binary)
		return SLURM_ERROR;

	fd_close(&script_launcher_fd);
	xfree(script_launcher);

#if defined(__linux__)
	if ((script_launcher_fd = open(binary, (O_PATH|O_CLOEXEC))) >= 0) {
		char buf[PATH_MAX];
		ssize_t bytes = readlink(binary, buf, sizeof(buf));

		/*
		 * Because we are using script_launcher_fd to exec,
		 * script_launcher is just used for logging and thus we do not
		 * need script_launcher to be the full path. So, it is okay
		 * if readlink truncates the result; in that case, just use
		 * the truncated string.
		 */
		if (bytes > 0) {
			if (bytes >= sizeof(buf))
				bytes = sizeof(buf) - 1;

			buf[bytes] = '\0';

			script_launcher = xstrdup(buf);
		} else {
			script_launcher = xstrdup(binary);
		}

		return SLURM_SUCCESS;
	}
#endif /* !__linux__ */

	if (access(binary, R_OK | X_OK)) {
		error("%s: %s cannot be executed as an intermediate launcher, doing direct launch.",
		      __func__, binary);
		return SLURM_ERROR;
	} else {
		script_launcher = xstrdup(binary);
		return SLURM_SUCCESS;
	}
}

/* used to terminate any outstanding commands */
extern void run_command_shutdown(void)
{
	command_shutdown = 1;
}

extern bool run_command_is_launcher(int argc, char **argv)
{
	return (argc >= RUN_COMMAND_LAUNCHER_ARGC &&
		!xstrcmp(argv[1], RUN_COMMAND_LAUNCHER_MODE));
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

static void _run_command_child(run_command_args_t *args, int write_fd,
			       int read_fd, char **launcher_argv)
{
	int stdin_fd;

	if (read_fd > 0)
		stdin_fd = read_fd;
	else if ((stdin_fd = open("/dev/null", O_RDWR)) < 0) {
		/*
		 * We must avoid calling non-async-signal-safe functions at
		 * this point (like error() or similar), so we won't log
		 * anything now. If we want to log we could use write().
		 */
		_exit(127);
	}
	dup2(stdin_fd, STDIN_FILENO);
	dup2(write_fd, STDERR_FILENO);
	dup2(write_fd, STDOUT_FILENO);

	if (launcher_argv)
		_run_command_child_exec(script_launcher_fd, script_launcher,
					launcher_argv, args->env);

	_run_command_child_pre_exec();
	_run_command_child_exec(-1, args->script_path, args->script_argv,
				args->env);
}

static void _log_str_array(char *prefix, char **array)
{
	if (!(slurm_conf.debug_flags & DEBUG_FLAG_SCRIPT))
		return;

	if (!array)
		return;

	log_flag(SCRIPT, "%s: START", prefix);
	for (int i = 0; array[i]; i++)
		log_flag(SCRIPT, "%s[%d]=%s", prefix, i, array[i]);
	log_flag(SCRIPT, "%s: END", prefix);
}

static char **_setup_launcher_argv(run_command_args_t *args)
{
	char **launcher_argv = NULL;
	int extra = RUN_COMMAND_LAUNCHER_ARGC;
	int count = 0;

	xassert(script_launcher);

	_log_str_array("script_argv", args->script_argv);
	while (args->script_argv && args->script_argv[count])
		count++;

	count = count + extra + 1; /* Add one to NULL terminate the array. */
	launcher_argv = xcalloc(count, sizeof(launcher_argv[0]));

	/*
	 * args->script_argv[0] (launcher_argv[3]) is usually set to
	 * script_path, but that is not guaranteed (e.g. if args->script_argv
	 * == NULL). We want to guarantee that script_path is set, so we set
	 * it to launcher_argv[2].
	 */
	launcher_argv[0] = script_launcher;
	launcher_argv[1] = RUN_COMMAND_LAUNCHER_MODE;
	launcher_argv[2] = (char *) args->script_path;
	if (args->script_argv) {
		for (int i = 0; args->script_argv[i]; i++)
			launcher_argv[i + extra] = args->script_argv[i];
	}
	launcher_argv[count - 1] = NULL;

	_log_str_array("launcher_argv", launcher_argv);

	return launcher_argv;
}

/*
 * Wrapper for execv/execve. This should never return.
 */
static void _run_command_child_exec(int fd, const char *path, char **argv,
				    char **env)
{
	extern char **environ;

	if (!env || !env[0])
		env = environ;

	if (fd >= 0)
		fexecve(fd, argv, env);
	else
		execve(path, argv, env);
	error("%s: execv(%s): %m", __func__, path);
	_exit(127);
}

/*
 * Called in the child before exec. Do setup like closing unneeded files and
 * setting uid/gid.
 */
static void _run_command_child_pre_exec(void)
{
	closeall(3);
	/* coverity[leaked_handle] */
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
}

extern void run_command_launcher(int argc, char **argv)
{
	char *script_path = argv[RUN_COMMAND_LAUNCHER_ARGC - 1];
	char **script_argv = &argv[RUN_COMMAND_LAUNCHER_ARGC];

	xassert(script_path);
	_run_command_child_pre_exec();
	_run_command_child_exec(-1, script_path, script_argv, NULL);
	_exit(127);
}

extern char *run_command(run_command_args_t *args)
{
	pid_t cpid;
	char *resp = NULL;
	char **launcher_argv = NULL;
	int pfd_to_child[2] = { -1, -1 };
	int pfd[2] = { -1, -1 };
	bool free_argv = false;

	if ((args->script_path == NULL) || (args->script_path[0] == '\0')) {
		error("%s: no script specified", __func__);
		*(args->status) = 127;
		resp = xstrdup("Run command failed - configuration error");
		return resp;
	}
	if (!args->ignore_path_exec_check) {
		if (args->script_path[0] != '/') {
			error("%s: %s is not a fully qualified pathname (%s)",
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
	}
	if ((pipe(pfd) != 0) ||
	    (args->write_to_child && (pipe(pfd_to_child) != 0))) {
		error("%s: pipe(): %m", __func__);
		fd_close(&pfd[0]);
		fd_close(&pfd[1]);
		fd_close(&pfd_to_child[0]);
		fd_close(&pfd_to_child[1]);
		*(args->status) = 127;
		resp = xstrdup("System error");
		return resp;
	}
	if (!(args->script_argv)) {
		args->script_argv = xcalloc(2, sizeof(char *));
		args->script_argv[0] = xstrdup(args->script_path);
		free_argv = true;
	}
	slurm_mutex_lock(&proc_count_mutex);
	child_proc_count++;
	slurm_mutex_unlock(&proc_count_mutex);

	if (script_launcher)
		launcher_argv = _setup_launcher_argv(args);

	if ((cpid = fork()) == 0) {
		/* Child writes to pfd[1] and reads from pfd_to_child[0] */
		fd_close(&pfd_to_child[1]);
		fd_close(&pfd[0]);
		_run_command_child(args, pfd[1], pfd_to_child[0],
				   launcher_argv);
		/* We should never get here. */
	} else if (cpid < 0) {
		close(pfd[0]);
		close(pfd[1]);
		fd_close(&pfd_to_child[0]);
		fd_close(&pfd_to_child[1]);
		error("%s: fork(): %m", __func__);
		slurm_mutex_lock(&proc_count_mutex);
		child_proc_count--;
		slurm_mutex_unlock(&proc_count_mutex);
	} else {
		/* Parent writes to pfd_to_child[1] and reads from pfd[0] */
		close(pfd[1]);
		fd_close(&pfd_to_child[0]);
		if (args->tid)
			track_script_reset_cpid(args->tid, cpid);
		if (args->cb)
			args->cb(pfd_to_child[1], args->cb_arg);
		/*
		 * Close the write pipe to the child immediately after it is
		 * used, before calling run_command_poll_child(). This means
		 * that the pipe will be closed before waiting for the child
		 * to finish. If an error happened during the write, when the
		 * child tries to read the required data from the pipe, the
		 * pipe will be closed and the child can exit.
		 */
		fd_close(&pfd_to_child[1]);
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
	}
	if (free_argv) {
		xfree(args->script_argv[0]);
		xfree(args->script_argv);
	}

	log_flag(SCRIPT, "%s:script=%s, resp:\n%s",
		 __func__, args->script_path, resp);

	/* Array contents were not malloc'd, do not free */
	xfree(launcher_argv);

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
