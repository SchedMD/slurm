/*****************************************************************************\
 *  test7.6.prog.c - Test of TotalView operation with SLURM.
 *****************************************************************************
 *  Copyright (C) 2004-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Dong Ang <dahn@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
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
#include <mpi.h>

#define COMM_TAG 1000

static void pass_its_neighbor(const int rank, const int size, const int* buf)
{
	MPI_Request request[2];
	MPI_Status status[2];

	MPI_Irecv((void *)buf, 1, MPI_INT, ((rank+size-1)%size), COMM_TAG,
		MPI_COMM_WORLD, &request[0]);
	MPI_Isend((void *)&rank, 1, MPI_INT, ((rank+1)%size), COMM_TAG,
		MPI_COMM_WORLD, &request[1]);
	MPI_Waitall(2, request, status);

	fprintf(stdout, "Rank[%d] I just received msg from Rank %d\n",
		rank, *buf);
}

int main(int argc, char * argv[])
{
	int size, rank,buf;

	MPI_Init(&argc, &argv);
	MPI_Comm_size(MPI_COMM_WORLD, &size);
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	sleep(6);

	buf = rank;	/* we only pass rank */

	pass_its_neighbor(rank, size, &buf);

	MPI_Finalize();
	return 0;
}

