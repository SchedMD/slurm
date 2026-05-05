/*****************************************************************************\
 *  mpi_signal_test.c - MPI test program for PMIx task failure signal behavior.
 *****************************************************************************
 *  Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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
#include <mpi.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define EXIT_CODE 42

static void sigterm_handler(int sig)
{
	static const char msg[] = "rank_got_sigterm\n";
	(void) write(STDOUT_FILENO, msg, sizeof(msg) - 1);
}

int main(int argc, char *argv[])
{
	int rank;
	int trap = 0, abort_mode = 0;

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "trap"))
			trap = 1;
		else if (!strcmp(argv[i], "abort"))
			abort_mode = 1;
	}

	MPI_Init(&argc, &argv);
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);

	if (rank == 0) {
		if (abort_mode) {
			printf("rank0_calling_abort\n");
			fflush(stdout);
			MPI_Abort(MPI_COMM_WORLD, EXIT_CODE);
		} else {
			printf("rank0_exiting_%d\n", EXIT_CODE);
			fflush(stdout);
			_exit(EXIT_CODE);
		}
	}

	if (trap) {
		struct sigaction sa = { .sa_handler = sigterm_handler };
		sigemptyset(&sa.sa_mask);
		sigaction(SIGTERM, &sa, NULL);
	}

	while (1)
		pause();

	return 0;
}
