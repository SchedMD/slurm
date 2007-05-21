/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*  
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
#include "mpi.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* A simple performance test. The file name is taken as a 
   command-line argument. */

#define SIZE (1048576*4)       /* read/write size per node in bytes */

int main(int argc, char **argv)
{
    int *buf, i, j, mynod, nprocs, ntimes=5, len, err, flag;
    double stim, read_tim, write_tim, new_read_tim, new_write_tim;
    double min_read_tim=10000000.0, min_write_tim=10000000.0, read_bw, write_bw;
    MPI_File fh;
    MPI_Status status;
    char *filename;

    MPI_Init(&argc,&argv);
    MPI_Comm_size(MPI_COMM_WORLD, &nprocs);
    MPI_Comm_rank(MPI_COMM_WORLD, &mynod);

/* process 0 takes the file name as a command-line argument and 
   broadcasts it to other processes */
    if (!mynod) {
	i = 1;
	while ((i < argc) && strcmp("-fname", *argv)) {
	    i++;
	    argv++;
	}
	if (i >= argc) {
	    fprintf(stderr, "\n*#  Usage: perf -fname filename\n\n");
	    MPI_Abort(MPI_COMM_WORLD, 1);
	}
	argv++;
	len = strlen(*argv);
	filename = (char *) malloc(len+1);
	strcpy(filename, *argv);
	MPI_Bcast(&len, 1, MPI_INT, 0, MPI_COMM_WORLD);
	MPI_Bcast(filename, len+1, MPI_CHAR, 0, MPI_COMM_WORLD);
	fprintf(stderr, "Access size per process = %d bytes, ntimes = %d\n", SIZE, ntimes);
    }
    else {
	MPI_Bcast(&len, 1, MPI_INT, 0, MPI_COMM_WORLD);
	filename = (char *) malloc(len+1);
	MPI_Bcast(filename, len+1, MPI_CHAR, 0, MPI_COMM_WORLD);
    }


    buf = (int *) malloc(SIZE);

    for (j=0; j<ntimes; j++) {
	MPI_File_open(MPI_COMM_WORLD, filename, MPI_MODE_CREATE | 
             MPI_MODE_RDWR, MPI_INFO_NULL, &fh);
	MPI_File_seek(fh, mynod*SIZE, MPI_SEEK_SET);

	MPI_Barrier(MPI_COMM_WORLD);
	stim = MPI_Wtime();
	MPI_File_write(fh, buf, SIZE, MPI_BYTE, &status);
	write_tim = MPI_Wtime() - stim;
  
	MPI_File_close(&fh);

	MPI_Barrier(MPI_COMM_WORLD);

	MPI_File_open(MPI_COMM_WORLD, filename, MPI_MODE_CREATE | 
                   MPI_MODE_RDWR, MPI_INFO_NULL, &fh);
	MPI_File_seek(fh, mynod*SIZE, MPI_SEEK_SET);
      
	MPI_Barrier(MPI_COMM_WORLD);
	stim = MPI_Wtime();
	MPI_File_read(fh, buf, SIZE, MPI_BYTE, &status);
	read_tim = MPI_Wtime() - stim;
  
	MPI_File_close(&fh);
  
	MPI_Allreduce(&write_tim, &new_write_tim, 1, MPI_DOUBLE, MPI_MAX,
		      MPI_COMM_WORLD);
	MPI_Allreduce(&read_tim, &new_read_tim, 1, MPI_DOUBLE, MPI_MAX,
		    MPI_COMM_WORLD);

	min_read_tim = (new_read_tim < min_read_tim) ? 
	    new_read_tim : min_read_tim;
	min_write_tim = (new_write_tim < min_write_tim) ? 
	    new_write_tim : min_write_tim;
    }
    
    if (mynod == 0) {
	read_bw = (SIZE*nprocs)/(min_read_tim*1024.0*1024.0);
	write_bw = (SIZE*nprocs)/(min_write_tim*1024.0*1024.0);
	fprintf(stderr, "Write bandwidth without file sync = %f Mbytes/sec\n", write_bw);
	fprintf(stderr, "Read bandwidth without prior file sync = %f Mbytes/sec\n", read_bw);
    }

    min_write_tim=10000000.0;
    min_read_tim=10000000.0;

    flag = 0;
    for (j=0; j<ntimes; j++) {
	MPI_File_open(MPI_COMM_WORLD, filename, MPI_MODE_CREATE | 
                 MPI_MODE_RDWR, MPI_INFO_NULL, &fh);
	MPI_File_seek(fh, mynod*SIZE, MPI_SEEK_SET);

	MPI_Barrier(MPI_COMM_WORLD);
	stim = MPI_Wtime();
	MPI_File_write(fh, buf, SIZE, MPI_BYTE, &status);
	err = MPI_File_sync(fh);
	write_tim = MPI_Wtime() - stim;
	if (err == MPI_ERR_UNKNOWN) {
	    flag = 1;
	    break;
	}
  
	MPI_File_close(&fh);
  
	MPI_Barrier(MPI_COMM_WORLD);

	MPI_File_open(MPI_COMM_WORLD, filename, MPI_MODE_CREATE | 
                   MPI_MODE_RDWR, MPI_INFO_NULL, &fh);
	MPI_File_seek(fh, mynod*SIZE, MPI_SEEK_SET);
      
	MPI_Barrier(MPI_COMM_WORLD);
	stim = MPI_Wtime();
	MPI_File_read(fh, buf, SIZE, MPI_BYTE, &status);
	read_tim = MPI_Wtime() - stim;
  
	MPI_File_close(&fh);
  
	MPI_Allreduce(&write_tim, &new_write_tim, 1, MPI_DOUBLE, MPI_MAX,
		      MPI_COMM_WORLD);
	MPI_Allreduce(&read_tim, &new_read_tim, 1, MPI_DOUBLE, MPI_MAX,
		    MPI_COMM_WORLD);

	min_read_tim = (new_read_tim < min_read_tim) ? 
	    new_read_tim : min_read_tim;
	min_write_tim = (new_write_tim < min_write_tim) ? 
	    new_write_tim : min_write_tim;
    }

    if (mynod == 0) {
	if (flag) fprintf(stderr, "MPI_File_sync returns error.\n");
	else {
	    read_bw = (SIZE*nprocs)/(min_read_tim*1024.0*1024.0);
	    write_bw = (SIZE*nprocs)/(min_write_tim*1024.0*1024.0);
	    fprintf(stderr, "Write bandwidth including file sync = %f Mbytes/sec\n", write_bw);
	    fprintf(stderr, "Read bandwidth after file sync = %f Mbytes/sec\n", read_bw);
	}
    }

    free(buf);
    free(filename);
    MPI_Finalize();
    return 0;
}
