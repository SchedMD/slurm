/*****************************************************************************\
 *  test1.94.server.c - Test of MPICH2 task spawn logic
 *****************************************************************************
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
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
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
	int world_size, universe_size = 0, *universe_sizep, rank, flag, rc;
	MPI_Comm everyone;	/* intercommunicator */

	if (argc < 3) {
		printf("FAILURE: Usage %s <client_program> <ntasks>\n",
		       argv[0]);
		exit(1);
	}

	MPI_Init(&argc, &argv);
	MPI_Comm_size(MPI_COMM_WORLD, &world_size);
	if (world_size != 1) {
		printf("FAILURE: Started %d server processes\n", world_size);
		exit(1);
	}

	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	//printf("server rank:%d\n",rank);

	/*
	 * NOTE: Slurm reports MPI_UNIVERSE_SIZE as the task count of the
	 * current step. The server runs as a single-task step, so this is 1
	 * and we fall back below. We pick 4 to match the job allocation.
	 */
	MPI_Comm_get_attr(MPI_COMM_WORLD, MPI_UNIVERSE_SIZE,
			  &universe_sizep, &flag);
	if (flag) {
		universe_size = *universe_sizep;
		printf("MPI_UNIVERSE_SIZE is %d\n", universe_size);
	}
	if (universe_size < 2) {
		universe_size = atoi(argv[2]);
		printf("Manually setting MPI_UNIVERSE_SIZE to %d\n",
		       universe_size);
	}

	rc = MPI_Comm_spawn(argv[1], MPI_ARGV_NULL, universe_size,
			    MPI_INFO_NULL, 0, MPI_COMM_SELF, &everyone,
			    MPI_ERRCODES_IGNORE);
	if (rc != MPI_SUCCESS) {
		printf("FAILURE: MPI_Comm_spawn(): %d\n", rc);
		exit(1);
	}

	MPI_Finalize();
	exit(0);
}
