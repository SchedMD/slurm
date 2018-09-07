/*****************************************************************************\
 *  test1.88.prog.c - Simple ping test of operation with Slurm.
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Dong Ang <dahn@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
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

#define BUF_SIZE   64
#define COMM_TAG   1000
#define ITERATIONS 1

typedef struct rank_info {
	char host[BUF_SIZE];
	int rank;
} rank_info_t;

static void pass_its_neighbor(const int rank, const int size)
{
	struct utsname uts;
	MPI_Request request[2];
	rank_info_t out_buf, in_buf;
	char *host_env = getenv("SLURMD_NODENAME");

	out_buf.rank = rank;
	if (host_env) {
		strncpy(out_buf.host, host_env, BUF_SIZE);
	} else {
		uname(&uts);
		strncpy(out_buf.host, uts.nodename, BUF_SIZE);
	}
	out_buf.host[BUF_SIZE - 1] = '\0';

	MPI_Irecv((void *)&in_buf, sizeof(rank_info_t), MPI_CHAR,
		  ((rank + size - 1) % size),
		  COMM_TAG, MPI_COMM_WORLD, &request[0]);
	MPI_Isend((void *)&out_buf, sizeof(rank_info_t), MPI_CHAR,
		  ((rank + 1) % size),
		  COMM_TAG, MPI_COMM_WORLD, &request[1]);
	MPI_Waitall(2, request, MPI_STATUS_IGNORE);

	printf("Rank[%d] on %s just received msg from Rank %d on %s\n",
	       rank, out_buf.host, in_buf.rank, in_buf.host);
}

int main(int argc, char * argv[])
{
	int i;
	int size, rank;
	time_t now;

	MPI_Init(&argc, &argv);
	MPI_Comm_size(MPI_COMM_WORLD, &size);
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);

	for (i = 0; i < ITERATIONS; i++) {
		if (i)
			sleep(1);
		pass_its_neighbor(rank, size);
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
