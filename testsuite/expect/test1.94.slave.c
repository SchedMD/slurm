/*****************************************************************************\
 *  test1.93.slave.c - Simple ping test of operation with SLURM.
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
#include <stdlib.h>
#include <unistd.h>
#include <sys/utsname.h>

#define COMM_TAG 1000

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
	fprintf(stdout, "Rank[%d] on %s just received msg from Rank %d\n",
		rank, uts.nodename, *buf);
}

int main(int argc, char *argv[]) 
{ 
	int buf, size, rank, rc = 0;
	MPI_Comm parent;

	MPI_Init(&argc, &argv);
	MPI_Comm_get_parent(&parent);
	if (parent == MPI_COMM_NULL) {
		printf("No parent!\n");
		rc = 1;
		goto fini;
	}
	MPI_Comm_remote_size(parent, &size);
	if (size != 1) {
		printf("Something's wrong with the parent\n");
		rc = 2;
		goto fini;
	}
	MPI_Comm_size(MPI_COMM_WORLD, &size);
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	//printf("slave rank:%d size:%d\n", rank, size);
 
	buf = rank;	/* we only pass rank */
	pass_its_neighbor(rank, size, &buf);
 
fini:	MPI_Finalize(); 
	exit(rc);
} 
