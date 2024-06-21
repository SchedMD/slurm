/*****************************************************************************\
 *  run_command.h - run a command asynchronously and return output
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

#ifndef __RUN_COMMAND_H__
#define __RUN_COMMAND_H__

#include "src/common/track_script.h"

#define RUN_COMMAND_LAUNCHER_MODE "slurm_script_launcher"
#define RUN_COMMAND_LAUNCHER_ARGC 3

typedef struct {
	void (*cb)(int write_fd, void *cb_arg);
	void *cb_arg;
	char **env;
	bool ignore_path_exec_check;
	uint32_t job_id;
	int max_wait;
	bool orphan_on_shutdown;
	char **script_argv;
	const char *script_path;
	const char *script_type;
	int *status;
	pthread_t tid;
	bool *timed_out;
	bool write_to_child;
} run_command_args_t;

/*
 * run_command_add_to_script
 *
 * Insert contents of "new_str" into "script_body"
 *
 * If new_str is NULL or empty, then this does nothing.
 * If *script_body is NULL, then this sets *script_body to new_str.
 * If *script_body begins with a '#' character (presumably the shebang line),
 * then this adds new_str to the line below.
 * Otherwise, this prepends *script_body with new_str.
 *
 * IN/OUT script_body - pointer to the string that represents the script.
 * IN new_str - the string to insert into *script_body
 */
extern void run_command_add_to_script(char **script_body, char *new_str);

/*
 * Used to initialize this run_command module.
 *
 * If run_command_shutdown() was previously called, this function must be
 * called to re-initialize this module and allow commands to run.
 *
 * IN argc - number of command line arguments
 * IN argv - the command line arguments or NULL to use the current running
 *      binary
 * IN binary - path to executable binary to use as the script launcher or NULL
 *	to use the current running binary or resolve using argc/argv
 * RET SLURM_SUCCESS if launcher resolved or SLURM_ERROR if launcher not
 *      resolved/set
 */
extern int run_command_init(int argc, char **argv, char *binary);

/*
 * Used to terminate any outstanding commands. Any future commands will be
 * immediately terminated until run_command_init() is called again.
 */
extern void run_command_shutdown(void);

/*
 * Return true if the caller is in RUN_COMMAND_LAUNCHER_MODE
 */
extern bool run_command_is_launcher(int argc, char **argv);

/* Return count of child processes */
extern int run_command_count(void);

/*
 * Execute a command, wait for termination and return its stdout.
 *
 * The following describes the variables in run_command_args_t:
 *
 * cb - If set, this callback function is called in the parent immediately after
 *      the child is launched. write_fd is set to a valid file descriptor if
 *      write_to_child is true; otherwise, write_fd is -1.
 * cb_arg - Optional argument to be passed to the callback function.
 * env IN - environment for the command, if NULL execv is used
 * max_wait IN - Maximum time to wait in milliseconds,
 *		 -1 for no limit (asynchronous)
 * orphan_on_shutdown IN - If true, then instead of killing the script on
 *                         shutdown, orphan the script instead.
 * script_argv IN - Arguments to the script
 * script_path IN - Fully qualified pathname of the program to execute
 * script_type IN - Type of program being run (e.g. "StartStageIn")
 * status OUT - Job exit code
 * tid IN - Thread we are calling from; zero if not using track_script.
 * timed_out OUT - If not NULL, then set to true if the command timed out.
 * write_to_child IN - If true, then open another pipe so the parent can
 *                     write data to the child.
 *
 * Return stdout+stderr of spawned program, value must be xfreed.
 */
extern char *run_command(run_command_args_t *run_command_args);

/*
 * Call this if a binary is running in script launcher mode.
 */
extern void run_command_launcher(int argc, char **argv);

/*
 * Read stdout of a child process and wait for the child process to terminate.
 * Kills the child's process group once the timeout is reached.
 *
 * IN cpid - child process id
 * IN max_wait - timeout in milliseconds
 * IN orphan_on_shutdown - if true, orphan instead of kill the child process
 *                         group when the daemon is shutting down
 * IN read_fd - file descriptor for reading stdout from the child process
 * IN script_path - path to script
 * IN script_type - description of script
 * IN tid - thread id of the calling thread; zero if not using track_script.
 * OUT status - exit status of the child process
 * OUT timed_out - true if the child process' run time hit the timeout max_wait
 *
 * Return the output of the child process.
 * Caller must xfree() returned value (even if no output was read).
 */
extern char *run_command_poll_child(int cpid,
				    int max_wait,
				    bool orphan_on_shutdown,
				    int read_fd,
				    const char *script_path,
				    const char *script_type,
				    pthread_t tid,
				    int *status,
				    bool *timed_out);

/*
 * run_command_waitpid_timeout()
 *
 *  Same as waitpid(2) but kill process group for pid after timeout millisecs.
 *
 *  name IN - name or class of program we're waiting on (for log messages)
 *  pid IN - child on which to call waitpid(2)
 *  pstatus OUT - pointer to integer status
 *  timeout_ms IN - timeout in milliseconds
 *  elapsed_ms IN - already elapsed time in milliseconds
 *  tid IN - thread ID of the calling process - only set if using track_script
 *  timed_out OUT - If not NULL, then set to true if waitpid() did not return
 *                  successfully after timeout_ms milliseconds.
 *
 *  Returns 0 for valid status in pstatus, -1 on failure of waitpid(2).
 */
extern int run_command_waitpid_timeout(
	const char *name, pid_t pid, int *pstatus, int timeout_ms,
	int elapsed_ms, pthread_t tid,
	bool *timed_out);

#endif	/* __RUN_COMMAND_H__ */
