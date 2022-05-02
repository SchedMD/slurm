/*****************************************************************************\
 *  run_command.h - run a command asynchronously and return output
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

#ifndef __RUN_COMMAND_H__
#define __RUN_COMMAND_H__

#include "src/common/track_script.h"

typedef struct {
	int (*container_join)(uint32_t job_id, uid_t uid);
	char **env;
	uint32_t job_id;
	int max_wait;
	char **script_argv;
	const char *script_path;
	const char *script_type;
	int *status;
	pthread_t tid;
	bool *timed_out;
	bool turnoff_output;
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
 * Needed in cases of high-availability when the backup controllers are
 * returning to function and must recover from a previously issued shutdown.
 */
extern void run_command_init(void);

/* used to terminate any outstanding commands */
extern void run_command_shutdown(void);

/* Return count of child processes */
extern int run_command_count(void);

/*
 * Execute a command, wait for termination and return its stdout.
 *
 * The following describes the variables in run_command_args_t:
 *
 * env IN - environment for the command, if NULL execv is used
 * max_wait IN - Maximum time to wait in milliseconds,
 *		 -1 for no limit (asynchronous)
 * script_argv IN - Arguments to the script
 * script_path IN - Fully qualified pathname of the program to execute
 * script_type IN - Type of program being run (e.g. "StartStageIn")
 * status OUT - Job exit code
 * tid IN - Thread we are calling from; zero if not using track_script.
 * timed_out OUT - If not NULL, then set to true if the command timed out.
 *
 * Return stdout+stderr of spawned program, value must be xfreed.
 */
extern char *run_command(run_command_args_t *run_command_args);

/*
 * run_command_waitpid_timeout()
 *
 *  Same as waitpid(2) but kill process group for pid after timeout millisecs.
 *
 *  name IN - name or class of program we're waiting on (for log messages)
 *  pid IN - child on which to call waitpid(2)
 *  pstatus OUT - pointer to integer status
 *  timeout_ms IN - timeout in milliseconds
 *  timed_out OUT - If not NULL, then set to true if waitpid() did not return
 *                  successfully after timeout_ms milliseconds.
 *
 *  Returns 0 for valid status in pstatus, -1 on failure of waitpid(2).
 */
extern int run_command_waitpid_timeout(
	const char *name, pid_t pid, int *pstatus, int timeout_ms,
	bool *timed_out);

#endif	/* __RUN_COMMAND_H__ */
