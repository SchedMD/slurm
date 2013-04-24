/*****************************************************************************\
 *  test9.9.prog.c - Timing test for 5000 jobs.
 *
 *  Usage: test9.9.prog <sbatch_path> <exec_prog> <prog_name> <iterations>
 *
 *  NOTE: This is a variant of test9.9.bash. It seems to have the ability to
 *  run more jobs without problems running out of process IDs (it tests the
 *  fork call for errors and retries), but run slower than test9.9.bash.
 *****************************************************************************
 *  Copyright (C) 2012 SchedMD LLC.
 *  Written by Morris Jette <jette@schedmd.com>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static inline void _spawn_job(char *path, char **argv)
{
	int status;
	pid_t pid;

	while ((pid = fork()) < 0)
		usleep(100);
	if (pid == 0) {
		close(1);
		close(2);
		execv(path, argv);
		exit(0);
	}
	waitpid(-1, &status, WNOHANG);	/* reap processes when possible */

	//system(command);
}

int main (int argc, char **argv)
{
	char *command[10];
	int iterations, status;
	int i;

	if (argc != 5) {
		printf("FAILURE: Usage: test9.9.prog <sbatch_path> "
		       "<exec_prog> <prog_name> <iterations>\n");
		exit(1);
	}
	iterations = atoi(argv[4]);
	if (iterations < 1) {
		printf("FAILURE: Invalid iterations count (%s)\n", argv[4]);
		exit(1);
	}

	command[0] = "sbatch";
	command[1] = "-J";
	command[2] = argv[3];
	command[3] = "-o";
	command[4] = "/dev/null";
	command[5] = "--wrap";
	command[6] = argv[2];
	command[7] = NULL;

	for (i = 0; i < iterations; i++) {
		_spawn_job(argv[1], command);
	}

	while ((wait(&status) >= 0)) ;

	return 0;
}
