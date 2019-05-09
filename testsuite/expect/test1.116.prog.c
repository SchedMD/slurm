/*****************************************************************************\
 *  test1.116.prog.c - Extended ping test of operation with Slurm.
 *****************************************************************************
 *  Copyright (C) 2019  SchedMD LLC.
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
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/utsname.h>

#define _DEBUG 0
#define ARRAY_LEN 4

int main(int argc, char * argv[])
{
	int i;
	int size, rank;
	int local_sum = 0, global_sum = 0;
	int array[ARRAY_LEN];

	MPI_Init(&argc, &argv);
	MPI_Comm_size(MPI_COMM_WORLD, &size);
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);

	if (rank == 0) {
		for (i = 0; i < ARRAY_LEN; i++)
			array[i] = rank + i;
	}
	MPI_Bcast(array, ARRAY_LEN, MPI_INT, 0, MPI_COMM_WORLD);
	for (i = 0; i < ARRAY_LEN; i++)
		local_sum += array[i];
#if _DEBUG
	for (i = 0; i < ARRAY_LEN; i++)
		printf("Rank[%d] Array[%d]=%d\n", rank, i, array[i]);
	printf("Rank[%d] LocalSum=%d\n", rank, local_sum);
#endif
	MPI_Reduce(&local_sum, &global_sum, 1, MPI_INT, MPI_SUM, 0,
		   MPI_COMM_WORLD);
	if (rank == 0)
		printf("Rank[%d] GlobalSum=%d\n", rank, global_sum);
	MPI_Finalize();
	return 0;
}
