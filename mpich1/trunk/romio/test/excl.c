/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*  
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
#include "mpi.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* tests MPI_MODE_EXCL */

int main(int argc, char **argv)
{
    MPI_File fh;
    int rank, len, err, i;
    int errs=0, toterrs;
    char *filename;

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
            fprintf(stderr, "\n*#  Usage: excl -fname filename\n\n");
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
    

    if (!rank) MPI_File_delete(filename, MPI_INFO_NULL);
    MPI_Barrier(MPI_COMM_WORLD);

    /* this open should succeed */
    err = MPI_File_open(MPI_COMM_WORLD, filename, 
         MPI_MODE_CREATE | MPI_MODE_EXCL | MPI_MODE_RDWR, MPI_INFO_NULL , &fh);
    if (err != MPI_SUCCESS) {
	errs++;
	fprintf(stderr, "Process %d: open failed when it should have succeeded\n", rank);
    }
    else MPI_File_close(&fh);

    MPI_Barrier(MPI_COMM_WORLD);

    /* this open should fail */
    err = MPI_File_open(MPI_COMM_WORLD, filename, 
         MPI_MODE_CREATE | MPI_MODE_EXCL | MPI_MODE_RDWR, MPI_INFO_NULL , &fh);
    if (err == MPI_SUCCESS) {
	errs++;
	fprintf(stderr, "Process %d: open succeeded when it should have failed\n", rank);
    }

    MPI_Allreduce( &errs, &toterrs, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD );
    if (rank == 0) {
	if( toterrs > 0) {
	    fprintf( stderr, "Found %d errors\n", toterrs );
	}
	else {
	    fprintf( stdout, " No Errors\n" );
	}
    }

    free(filename);
    MPI_Finalize();
    return 0; 
}
