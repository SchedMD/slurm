/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*  
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
#include "mpi.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define COUNT 1024

void handle_error(int errcode, char *str);

void handle_error(int errcode, char *str) 
{
	char msg[MPI_MAX_ERROR_STRING];
	int resultlen;
	MPI_Error_string(errcode, msg, &resultlen);
	fprintf(stderr, "%s: %s\n", str, msg);
	MPI_Abort(MPI_COMM_WORLD, 1);
}

/* tests shared file pointer functions */

int main(int argc, char **argv)
{
    int *buf, i, rank, nprocs, len, sum, global_sum;
    int errs=0, toterrs, errcode;
    char *filename;
    MPI_File fh;
    MPI_Status status;

    MPI_Init(&argc,&argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

/* process 0 takes the file name as a command-line argument and 
   broadcasts it to other processes */
    if (!rank) {
	i = 1;
	while ((i < argc) && strcmp("-fname", *argv)) {
	    i++;
	    argv++;
	}
	if (i >= argc) {
	    fprintf(stderr, "\n*#  Usage: shared_fp -fname filename\n\n");
	    MPI_Abort(MPI_COMM_WORLD, 1);
	}
	argv++;
	len = strlen(*argv);
	filename = (char *) malloc(len+10);
	strcpy(filename, *argv);
	MPI_Bcast(&len, 1, MPI_INT, 0, MPI_COMM_WORLD);
	MPI_Bcast(filename, len+10, MPI_CHAR, 0, MPI_COMM_WORLD);
    }
    else {
	MPI_Bcast(&len, 1, MPI_INT, 0, MPI_COMM_WORLD);
	filename = (char *) malloc(len+10);
	MPI_Bcast(filename, len+10, MPI_CHAR, 0, MPI_COMM_WORLD);
    }
    
    buf = (int *) malloc(COUNT * sizeof(int));

    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &nprocs);

    for (i=0; i<COUNT; i++) buf[i] = COUNT*rank + i;

    errcode = MPI_File_open(MPI_COMM_WORLD, filename, 
		    MPI_MODE_CREATE | MPI_MODE_RDWR, MPI_INFO_NULL, &fh);
    if (errcode != MPI_SUCCESS) {
	    handle_error(errcode, "MPI_File_open");
    }

    errcode = MPI_File_write_shared(fh, buf, COUNT, MPI_INT, &status);
    if (errcode != MPI_SUCCESS) {
	    handle_error(errcode, "MPI_File_write_shared");
    }

    for (i=0; i<COUNT; i++) buf[i] = 0;

    MPI_Barrier(MPI_COMM_WORLD);

    errcode = MPI_File_seek_shared(fh, 0, MPI_SEEK_SET);
    if (errcode != MPI_SUCCESS) {
	    handle_error(errcode, "MPI_File_seek_shared");
    }

    errcode = MPI_File_read_shared(fh, buf, COUNT, MPI_INT, &status);
    if (errcode != MPI_SUCCESS) {
	    handle_error(errcode, "MPI_File_read_shared");
    }

    MPI_File_close(&fh);

    sum = 0;
    for (i=0; i<COUNT; i++) sum += buf[i];

    MPI_Allreduce(&sum, &global_sum, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);

    if (global_sum != (((COUNT*nprocs - 1)*(COUNT*nprocs))/2)) {
	errs++;
	fprintf(stderr, "Error: sum %d, global_sum %d, %d\n", 
		sum, global_sum,(((COUNT*nprocs - 1)*(COUNT*nprocs))/2));
    }
    
    free(buf);
    free(filename);

    MPI_Allreduce( &errs, &toterrs, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD );
    if (rank == 0) {
	if( toterrs > 0) {
	    fprintf( stderr, "Found %d errors\n", toterrs );
	}
	else {
	    fprintf( stdout, " No Errors\n" );
	}
    }

    MPI_Finalize();
    return 0; 
}
