/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*  
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
#include "mpi.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define VERBOSE 0
/* tests if error message is printed correctly */

int main(int argc, char **argv)
{
    int i, rank, len, err;
    int errs = 0;
    char *filename, *tmp;
    MPI_File fh;
    char string[MPI_MAX_ERROR_STRING];

    MPI_Init(&argc,&argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

#if VERBOSE
    if (!rank) {
	fprintf(stderr, "Tests if errors are reported correctly...\n");
	fprintf(stderr, "Should say \"Invalid displacement argument\"\n\n");
    }
#endif

/* process 0 takes the file name as a command-line argument and 
   broadcasts it to other processes */
    if (!rank) {
	i = 1;
	while ((i < argc) && strcmp("-fname", *argv)) {
	    i++;
	    argv++;
	}
	if (i >= argc) {
	    fprintf(stderr, "\n*#  Usage: simple -fname filename\n\n");
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
    
    /* each process opens a separate file called filename.'myrank' */
    tmp = (char *) malloc(len+10);
    strcpy(tmp, filename);
    sprintf(filename, "%s.%d", tmp, rank);

    err = MPI_File_open(MPI_COMM_SELF, filename, MPI_MODE_CREATE+MPI_MODE_RDWR,
		        MPI_INFO_NULL, &fh);
    err = MPI_File_set_view(fh, -1, MPI_BYTE, MPI_BYTE, "native", 
                            MPI_INFO_NULL);
    /* disp is deliberately passed as -1 */

    /* This test is designed for ROMIO specifically and tests for a 
       specific error message */
    if (err != MPI_SUCCESS) {
	MPI_Error_string(err, string, &len);
	if (!rank) {
#if VERBOSE
	    fprintf(stderr, "%s\n", string);
#else
	    /* check for the word "displacement" in the message.
	       This allows other formatting of the message */
	    if (strstr( string, "displacement" ) == 0) {
		fprintf( stderr, "Unexpected error message %s\n", string );
		errs++;
	    }
#endif
	}
    }
    else {
	errs++;
	fprintf( stderr, "File set view did not return an error\n" );
    }

    MPI_File_close(&fh);

    free(filename);
    free(tmp);

    if (!rank) {
	if (errs == 0) {
	    printf( " No Errors\n" );
	}
	else {
	    printf( " Found %d errors\n", errs );
	}
    }

    MPI_Finalize();
    return 0; 
}
