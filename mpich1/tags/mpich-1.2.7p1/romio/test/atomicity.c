/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*  
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
#include "mpi.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* tests whether atomicity semantics are satisfied for overlapping accesses
   in atomic mode. The probability of detecting errors is higher if you run 
   it on 8 or more processes. */

/* The file name is taken as a command-line argument. */

#define BUFSIZE 10000    /* no. of integers */
#define VERBOSE 0
int main(int argc, char **argv)
{
    int *writebuf, *readbuf, i, mynod, nprocs, len, err;
    char *filename;
    int errs=0, toterrs;
    MPI_Datatype newtype;
    MPI_File fh;
    MPI_Status status;
    MPI_Info info;

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
	    fprintf(stderr, "\n*#  Usage: coll_test -fname filename\n\n");
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

    writebuf = (int *) malloc(BUFSIZE*sizeof(int));
    readbuf = (int *) malloc(BUFSIZE*sizeof(int));

/* test atomicity of contiguous accesses */

/* initialize file to all zeros */
    if (!mynod) {
	MPI_File_delete(filename, MPI_INFO_NULL);
	MPI_File_open(MPI_COMM_SELF, filename, MPI_MODE_CREATE | 
             MPI_MODE_RDWR, MPI_INFO_NULL, &fh);
	for (i=0; i<BUFSIZE; i++) writebuf[i] = 0;
	MPI_File_write(fh, writebuf, BUFSIZE, MPI_INT, &status);
	MPI_File_close(&fh);
#if VERBOSE
	fprintf(stderr, "\ntesting contiguous accesses\n");
#endif
    }
    MPI_Barrier(MPI_COMM_WORLD);

    for (i=0; i<BUFSIZE; i++) writebuf[i] = 10;
    for (i=0; i<BUFSIZE; i++) readbuf[i] = 20;

    MPI_File_open(MPI_COMM_WORLD, filename, MPI_MODE_CREATE | 
             MPI_MODE_RDWR, MPI_INFO_NULL, &fh);

/* set atomicity to true */
    err = MPI_File_set_atomicity(fh, 1);
    if (err != MPI_SUCCESS) {
	fprintf(stderr, "Atomic mode not supported on this file system.\n");fflush(stderr);
	MPI_Abort(MPI_COMM_WORLD, 1);
    }

    MPI_Barrier(MPI_COMM_WORLD);
    
/* process 0 writes and others concurrently read. In atomic mode, 
   the data read must be either all old values or all new values; nothing
   in between. */ 

    if (!mynod) MPI_File_write(fh, writebuf, BUFSIZE, MPI_INT, &status);
    else {
	err = MPI_File_read(fh, readbuf, BUFSIZE, MPI_INT, &status);
	if (err == MPI_SUCCESS) {
	    if (readbuf[0] == 0) { /* the rest must also be 0 */
		for (i=1; i<BUFSIZE; i++) 
		    if (readbuf[i] != 0) {
			errs++;
			fprintf(stderr, "Process %d: readbuf[%d] is %d, should be 0\n", mynod, i, readbuf[i]);
			MPI_Abort(MPI_COMM_WORLD, 1);
		    }
	    }
	    else if (readbuf[0] == 10) { /* the rest must also be 10 */
		for (i=1; i<BUFSIZE; i++) 
		    if (readbuf[i] != 10) {
			errs++;
			fprintf(stderr, "Process %d: readbuf[%d] is %d, should be 10\n", mynod, i, readbuf[i]);
			MPI_Abort(MPI_COMM_WORLD, 1);
		    }
	    }
	    else {
		errs++;
		fprintf(stderr, "Process %d: readbuf[0] is %d, should be either 0 or 10\n", mynod, readbuf[0]); 	
	    }    
	}
    }

    MPI_File_close(&fh);
	
    MPI_Barrier(MPI_COMM_WORLD);


/* repeat the same test with a noncontiguous filetype */

    MPI_Type_vector(BUFSIZE, 1, 2, MPI_INT, &newtype);
    MPI_Type_commit(&newtype);

    MPI_Info_create(&info);
    /* I am setting these info values for testing purposes only. It is
       better to use the default values in practice. */
    MPI_Info_set(info, "ind_rd_buffer_size", "1209");
    MPI_Info_set(info, "ind_wr_buffer_size", "1107");
    
    if (!mynod) {
	MPI_File_delete(filename, MPI_INFO_NULL);
	MPI_File_open(MPI_COMM_SELF, filename, MPI_MODE_CREATE | 
             MPI_MODE_RDWR, info, &fh);
	for (i=0; i<BUFSIZE; i++) writebuf[i] = 0;
	MPI_File_set_view(fh, 0, MPI_INT, newtype, "native", info);
	MPI_File_write(fh, writebuf, BUFSIZE, MPI_INT, &status);
	MPI_File_close(&fh);
#if VERBOSE
	fprintf(stderr, "\ntesting noncontiguous accesses\n");
#endif
    }
    MPI_Barrier(MPI_COMM_WORLD);

    for (i=0; i<BUFSIZE; i++) writebuf[i] = 10;
    for (i=0; i<BUFSIZE; i++) readbuf[i] = 20;

    MPI_File_open(MPI_COMM_WORLD, filename, MPI_MODE_CREATE | 
             MPI_MODE_RDWR, info, &fh);
    MPI_File_set_atomicity(fh, 1);
    MPI_File_set_view(fh, 0, MPI_INT, newtype, "native", info);
    MPI_Barrier(MPI_COMM_WORLD);
    
    if (!mynod) MPI_File_write(fh, writebuf, BUFSIZE, MPI_INT, &status);
    else {
	err = MPI_File_read(fh, readbuf, BUFSIZE, MPI_INT, &status);
	if (err == MPI_SUCCESS) {
	    if (readbuf[0] == 0) {
		for (i=1; i<BUFSIZE; i++) 
		    if (readbuf[i] != 0) {
			errs++;
			fprintf(stderr, "Process %d: readbuf[%d] is %d, should be 0\n", mynod, i, readbuf[i]);
			MPI_Abort(MPI_COMM_WORLD, 1);
		    }
	    }
	    else if (readbuf[0] == 10) {
		for (i=1; i<BUFSIZE; i++) 
		    if (readbuf[i] != 10) {
			errs++;
			fprintf(stderr, "Process %d: readbuf[%d] is %d, should be 10\n", mynod, i, readbuf[i]);
			MPI_Abort(MPI_COMM_WORLD, 1);
		    }
	    }
	    else {
		errs++;
		fprintf(stderr, "Process %d: readbuf[0] is %d, should be either 0 or 10\n", mynod, readbuf[0]); 	    
	    }
	}
    }

    MPI_File_close(&fh);
	
    MPI_Barrier(MPI_COMM_WORLD);

    MPI_Allreduce( &errs, &toterrs, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD );
    if (mynod == 0) {
	if( toterrs > 0) {
	    fprintf( stderr, "Found %d errors\n", toterrs );
	}
	else {
	    fprintf( stdout, " No Errors\n" );
	}
    }
    MPI_Type_free(&newtype);
    MPI_Info_free(&info);
    free(writebuf);
    free(readbuf);
    free(filename);

    MPI_Finalize();
    return 0;
}
