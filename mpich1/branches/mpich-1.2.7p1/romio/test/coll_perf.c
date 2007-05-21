/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*  
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
#include "mpi.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* The file name is taken as a command-line argument. */

/* Measures the I/O bandwidth for writing/reading a 3D
   block-distributed array to a file corresponding to the global array
   in row-major (C) order.
   Note that the file access pattern is noncontiguous.
  
   Array size 128^3. For other array sizes, change array_of_gsizes below.*/


int main(int argc, char **argv)
{
    MPI_Datatype newtype;
    int i, ndims, array_of_gsizes[3], array_of_distribs[3];
    int order, nprocs, len, *buf, bufcount, mynod;
    int array_of_dargs[3], array_of_psizes[3];
    MPI_File fh;
    MPI_Status status;
    double stim, write_tim, new_write_tim, write_bw;
    double read_tim, new_read_tim, read_bw;
    char *filename;

    MPI_Init(&argc,&argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &mynod);
    MPI_Comm_size(MPI_COMM_WORLD, &nprocs);

/* process 0 takes the file name as a command-line argument and 
   broadcasts it to other processes */
    if (!mynod) {
	i = 1;
	while ((i < argc) && strcmp("-fname", *argv)) {
	    i++;
	    argv++;
	}
	if (i >= argc) {
	    fprintf(stderr, "\n*#  Usage: coll_perf -fname filename\n\n");
	    MPI_Abort(MPI_COMM_WORLD, 1);
	}
	argv++;
	len = strlen(*argv);
	filename = (char *) malloc(len+1);
	strcpy(filename, *argv);
	MPI_Bcast(&len, 1, MPI_INT, 0, MPI_COMM_WORLD);
	MPI_Bcast(filename, len+1, MPI_CHAR, 0, MPI_COMM_WORLD);
    }
    else {
	MPI_Bcast(&len, 1, MPI_INT, 0, MPI_COMM_WORLD);
	filename = (char *) malloc(len+1);
	MPI_Bcast(filename, len+1, MPI_CHAR, 0, MPI_COMM_WORLD);
    }


    ndims = 3;
    order = MPI_ORDER_C;

    array_of_gsizes[0] = 128;
    array_of_gsizes[1] = 128;
    array_of_gsizes[2] = 128;

    array_of_distribs[0] = MPI_DISTRIBUTE_BLOCK;
    array_of_distribs[1] = MPI_DISTRIBUTE_BLOCK;
    array_of_distribs[2] = MPI_DISTRIBUTE_BLOCK;

    array_of_dargs[0] = MPI_DISTRIBUTE_DFLT_DARG;
    array_of_dargs[1] = MPI_DISTRIBUTE_DFLT_DARG;
    array_of_dargs[2] = MPI_DISTRIBUTE_DFLT_DARG;

    for (i=0; i<ndims; i++) array_of_psizes[i] = 0;
    MPI_Dims_create(nprocs, ndims, array_of_psizes);

    MPI_Type_create_darray(nprocs, mynod, ndims, array_of_gsizes,
			   array_of_distribs, array_of_dargs,
			   array_of_psizes, order, MPI_INT, &newtype);
    MPI_Type_commit(&newtype);

    MPI_Type_size(newtype, &bufcount);
    bufcount = bufcount/sizeof(int);
    buf = (int *) malloc(bufcount * sizeof(int));

/* to eliminate paging effects, do the operations once but don't time
   them */

    MPI_File_open(MPI_COMM_WORLD, filename, MPI_MODE_CREATE | MPI_MODE_RDWR, 
                  MPI_INFO_NULL, &fh);
    MPI_File_set_view(fh, 0, MPI_INT, newtype, "native", MPI_INFO_NULL);
    MPI_File_write_all(fh, buf, bufcount, MPI_INT, &status);
    MPI_File_seek(fh, 0, MPI_SEEK_SET);
    MPI_File_read_all(fh, buf, bufcount, MPI_INT, &status);
    MPI_File_close(&fh);

    MPI_Barrier(MPI_COMM_WORLD);
/* now time write_all */

    MPI_File_open(MPI_COMM_WORLD, filename, MPI_MODE_CREATE | MPI_MODE_RDWR, 
                  MPI_INFO_NULL, &fh);
    MPI_File_set_view(fh, 0, MPI_INT, newtype, "native", MPI_INFO_NULL);

    MPI_Barrier(MPI_COMM_WORLD);
    stim = MPI_Wtime();
    MPI_File_write_all(fh, buf, bufcount, MPI_INT, &status);
    write_tim = MPI_Wtime() - stim;
    MPI_File_close(&fh);

    MPI_Allreduce(&write_tim, &new_write_tim, 1, MPI_DOUBLE, MPI_MAX,
                    MPI_COMM_WORLD);

    if (mynod == 0) {
      write_bw = (array_of_gsizes[0]*array_of_gsizes[1]*array_of_gsizes[2]*sizeof(int))/(new_write_tim*1024.0*1024.0);
      fprintf(stderr, "Global array size %d x %d x %d integers\n", array_of_gsizes[0], array_of_gsizes[1], array_of_gsizes[2]);
      fprintf(stderr, "Collective write time = %f sec, Collective write bandwidth = %f Mbytes/sec\n", new_write_tim, write_bw);
    }

    MPI_Barrier(MPI_COMM_WORLD);
/* now time read_all */

    MPI_File_open(MPI_COMM_WORLD, filename, MPI_MODE_CREATE | MPI_MODE_RDWR, 
                  MPI_INFO_NULL, &fh); 
    MPI_File_set_view(fh, 0, MPI_INT, newtype, "native", MPI_INFO_NULL);

    MPI_Barrier(MPI_COMM_WORLD);
    stim = MPI_Wtime();
    MPI_File_read_all(fh, buf, bufcount, MPI_INT, &status);
    read_tim = MPI_Wtime() - stim;
    MPI_File_close(&fh);

    MPI_Allreduce(&read_tim, &new_read_tim, 1, MPI_DOUBLE, MPI_MAX,
                    MPI_COMM_WORLD);

    if (mynod == 0) {
      read_bw = (array_of_gsizes[0]*array_of_gsizes[1]*array_of_gsizes[2]*sizeof(int))/(new_read_tim*1024.0*1024.0);
      fprintf(stderr, "Collective read time = %f sec, Collective read bandwidth = %f Mbytes/sec\n", new_read_tim, read_bw);
    }

    MPI_Type_free(&newtype);
    free(buf);
    free(filename);

    MPI_Finalize();
    return 0;
}
