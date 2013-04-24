/*****************************************************************************\
 *  test1.94.master.c - Test of MPICH2 task spawn logic
 *****************************************************************************
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
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char *argv[]) 
{ 
	int world_size, universe_size = 0, *universe_sizep, rank, flag, rc;
	MPI_Comm everyone;	/* intercommunicator */

	if (argc < 2) {
		printf("FAILURE: Usage %s <slave_program>\n", argv[0]);
		exit(1);
	}

	MPI_Init(&argc, &argv);
	MPI_Comm_size(MPI_COMM_WORLD, &world_size);
	if (world_size != 1) {
		printf("FAILURE: Started %d master processes\n", world_size);
		exit(1);
	}

	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	//printf("master rank:%d\n",rank);

	/* NOTE: Ideally MPI_UNIVERSE_SIZE would be the size of the job
	 * allocation. Presently it is the size of the job step allocation.
	 * In any case, additional tasks can be spawned */
	MPI_Comm_get_attr(MPI_COMM_WORLD, MPI_UNIVERSE_SIZE,
			  &universe_sizep, &flag);
	if (flag) {
		universe_size = *universe_sizep;
		//printf("MPI_UNIVERSE_SIZE is %d\n", universe_size);
	}
	if (universe_size < 2)
		universe_size = 5;

	rc = MPI_Comm_spawn(argv[1], MPI_ARGV_NULL, universe_size-1,  
			    MPI_INFO_NULL, 0, MPI_COMM_SELF, &everyone,  
			    MPI_ERRCODES_IGNORE);
	if (rc != MPI_SUCCESS) {
		printf("FAILURE: MPI_Comm_spawn(): %d\n", rc);
		exit(1);
	}

	MPI_Finalize();
	exit(0);
} 
