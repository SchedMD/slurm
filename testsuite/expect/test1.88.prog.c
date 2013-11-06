/*****************************************************************************\
 *  test1.88.prog.c - Simple ping test of operation with SLURM.
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Dong Ang <dahn@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
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

#include <mpi.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <sys/utsname.h>

#define COMM_TAG   1000
#define ITERATIONS 1

static void pass_its_neighbor(const int rank, const int size, const int* buf)
{
	struct utsname uts;
	MPI_Request request[2];
	MPI_Status status[2];

	MPI_Irecv((void *)buf, 1, MPI_INT, ((rank+size-1)%size), COMM_TAG,
		MPI_COMM_WORLD, &request[0]);
	MPI_Isend((void *)&rank, 1, MPI_INT, ((rank+1)%size), COMM_TAG,
		MPI_COMM_WORLD, &request[1]);
	MPI_Waitall(2, request, status);

	uname(&uts);
	printf("Rank[%d] on %s just received msg from Rank %d\n",
	       rank, uts.nodename, *buf);
}

int main(int argc, char * argv[])
{
	int i;
	int size, rank,buf;
	time_t now;

	MPI_Init(&argc, &argv);
	MPI_Comm_size(MPI_COMM_WORLD, &size);
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);

	buf = rank;	/* we only pass rank */

	for (i = 0; i < ITERATIONS; i++) {
		if (i)
			sleep(1);
		pass_its_neighbor(rank, size, &buf);
		if ((ITERATIONS > 1) && (rank == 0)) {
			static time_t last_time = 0;
			now = time(NULL);
			printf("Iteration:%d Time:%s", i, ctime(&now));
			if (last_time && (last_time < (now - 2)))
				printf("Woke from suspend\n");
			last_time = now;
		}
	}

	MPI_Finalize();
	return 0;
}

