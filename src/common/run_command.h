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

/* Execute a command, wait for termination and return its stdout.
 * script_type IN - Type of program being run (e.g. "StartStageIn")
 * script_path IN - Fully qualified pathname of the program to execute
 * script_args IN - Arguments to the script
 * max_wait IN - Maximum time to wait in milliseconds,
 *		 -1 for no limit (asynchronous)
 * tid IN - Thread we are calling from.
 * status OUT - Job exit code
 * env - environment for the command, if NULL execv is used
 * Return stdout+stderr of spawned program, value must be xfreed. */
extern char *run_command(const char *script_type, const char *script_path,
			 char **script_argv, char **env, int max_wait,
			 pthread_t tid,
			 int *status);

/* Free an array of xmalloced records. The array must be NULL terminated. */
extern void free_command_argv(char **script_argv);

#endif	/* __RUN_COMMAND_H__ */
