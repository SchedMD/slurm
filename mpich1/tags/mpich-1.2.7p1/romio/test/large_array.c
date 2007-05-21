/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*  
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
#include "mpi.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Writes a 4-Gbyte distributed array, reads it back, and then deletes the 
   file. Uses collective I/O. */
/* The file name is taken as a command-line argument. */
/* Run it only on a machine with sufficient memory and a file system
   on which ROMIO supports large files, i.e., PIOFS, XFS, SFS, and HFS */

/* This program will work only if the MPI implementation defines MPI_Aint 
   as a 64-bit integer. */
   
int main(int argc, char **argv)
{
    MPI_Datatype newtype;
    int i, ndims, array_of_gsizes[3], array_of_distribs[3];
    int order, nprocs, len, flag, err;
    int array_of_dargs[3], array_of_psizes[3];
    int *readbuf, *writebuf, bufcount, mynod;
    char filename[1024];
    MPI_File fh;
    MPI_Status status;
    MPI_Aint size_with_aint;
    MPI_Offset size_with_offset;

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
	    fprintf(stderr, "\n*#  Usage: large_array -fname filename\n\n");
	    MPI_Abort(MPI_COMM_WORLD, 1);
	}
	argv++;
	len = strlen(*argv);
	strcpy(filename, *argv);
	MPI_Bcast(&len, 1, MPI_INT, 0, MPI_COMM_WORLD);
	MPI_Bcast(filename, len+1, MPI_CHAR, 0, MPI_COMM_WORLD);
	fprintf(stderr, "This program creates a 4 Gbyte file. Don't run it if you don't have that much disk space!\n");
    }
    else {
	MPI_Bcast(&len, 1, MPI_INT, 0, MPI_COMM_WORLD);
	MPI_Bcast(filename, len+1, MPI_CHAR, 0, MPI_COMM_WORLD);
    }

/* create the distributed array filetype */
    ndims = 3;
    order = MPI_ORDER_C;

    array_of_gsizes[0] = 1024;
    array_of_gsizes[1] = 1024;
    array_of_gsizes[2] = 4*1024/sizeof(int);

    array_of_distribs[0] = MPI_DISTRIBUTE_BLOCK;
    array_of_distribs[1] = MPI_DISTRIBUTE_BLOCK;
    array_of_distribs[2] = MPI_DISTRIBUTE_BLOCK;

    array_of_dargs[0] = MPI_DISTRIBUTE_DFLT_DARG;
    array_of_dargs[1] = MPI_DISTRIBUTE_DFLT_DARG;
    array_of_dargs[2] = MPI_DISTRIBUTE_DFLT_DARG;

    for (i=0; i<ndims; i++) array_of_psizes[i] = 0;
    MPI_Dims_create(nprocs, ndims, array_of_psizes);

/* check if MPI_Aint is large enough for size of global array. 
   if not, complain. */

    size_with_aint = sizeof(int);
    for (i=0; i<ndims; i++) size_with_aint *= array_of_gsizes[i];
    size_with_offset = sizeof(int);
    for (i=0; i<ndims; i++) size_with_offset *= array_of_gsizes[i];
    if (size_with_aint != size_with_offset) {
        fprintf(stderr, "Can't use an array of this size unless the MPI implementation defines a 64-bit MPI_Aint\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    MPI_Type_create_darray(nprocs, mynod, ndims, array_of_gsizes, 
			   array_of_distribs, array_of_dargs,
			   array_of_psizes, order, MPI_INT, &newtype);
    MPI_Type_commit(&newtype);

/* initialize writebuf */

    MPI_Type_size(newtype, &bufcount);
    bufcount = bufcount/sizeof(int);
    writebuf = (int *) malloc(bufcount * sizeof(int));
    if (!writebuf) fprintf(stderr, "Process %d, not enough memory for writebuf\n", mynod);
    for (i=0; i<bufcount; i++) writebuf[i] = mynod*1024 + i;

    /* write the array to the file */
    MPI_File_open(MPI_COMM_WORLD, filename, MPI_MODE_CREATE | MPI_MODE_RDWR, 
                  MPI_INFO_NULL, &fh);
    MPI_File_set_view(fh, 0, MPI_INT, newtype, "native", MPI_INFO_NULL);
    MPI_File_write_all(fh, writebuf, bufcount, MPI_INT, &status);
    MPI_File_close(&fh);

    free(writebuf);

    /* now read it back */
    readbuf = (int *) calloc(bufcount, sizeof(int));
    if (!readbuf) fprintf(stderr, "Process %d, not enough memory for readbuf\n", mynod);

    MPI_File_open(MPI_COMM_WORLD, filename, MPI_MODE_CREATE | MPI_MODE_RDWR, 
                  MPI_INFO_NULL, &fh);
    MPI_File_set_view(fh, 0, MPI_INT, newtype, "native", MPI_INFO_NULL);
    MPI_File_read_all(fh, readbuf, bufcount, MPI_INT, &status);
    MPI_File_close(&fh);

    /* check the data read */
    flag = 0;
    for (i=0; i<bufcount; i++) 
	if (readbuf[i] != mynod*1024 + i) {
	    fprintf(stderr, "Process %d, readbuf=%d, writebuf=%d\n", mynod, readbuf[i], mynod*1024 + i);
            flag = 1;
	}
    if (!flag) fprintf(stderr, "Process %d: data read back is correct\n", mynod);

    MPI_Type_free(&newtype);
    free(readbuf);

    MPI_Barrier(MPI_COMM_WORLD);
    if (!mynod) {
	err = MPI_File_delete(filename, MPI_INFO_NULL);
	if (err == MPI_SUCCESS) fprintf(stderr, "file deleted\n");
    }

    MPI_Finalize();
    return 0;
}
