/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*  
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
#include "mpi.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* A 32^3 array. For other array sizes, change array_of_gsizes below. */

/* Uses collective I/O. Writes a 3D block-distributed array to a file
   corresponding to the global array in row-major (C) order, reads it
   back, and checks that the data read is correct. */

/* The file name is taken as a command-line argument. */

/* Note that the file access pattern is noncontiguous. */
   
void handle_error(int errcode, char *str);

void handle_error(int errcode, char *str)
{
	char msg[MPI_MAX_ERROR_STRING];
	int resultlen;
	MPI_Error_string(errcode, msg, &resultlen);
	fprintf(stderr, "%s: %s\n", str, msg);
	MPI_Abort(MPI_COMM_WORLD, 1);
}

int main(int argc, char **argv)
{
    MPI_Datatype newtype;
    int i, ndims, array_of_gsizes[3], array_of_distribs[3];
    int order, nprocs, j, len;
    int array_of_dargs[3], array_of_psizes[3];
    int *readbuf, *writebuf, bufcount, mynod, *tmpbuf, array_size;
    char *filename;
    int errs=0, toterrs;
    MPI_File fh;
    MPI_Status status;
    MPI_Request request;
    MPI_Info info = MPI_INFO_NULL;
    int errcode;

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


/* create the distributed array filetype */
    ndims = 3;
    order = MPI_ORDER_C;

    array_of_gsizes[0] = 32;
    array_of_gsizes[1] = 32;
    array_of_gsizes[2] = 32;

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

/* initialize writebuf */

    MPI_Type_size(newtype, &bufcount);
    bufcount = bufcount/sizeof(int);
    writebuf = (int *) malloc(bufcount * sizeof(int));
    for (i=0; i<bufcount; i++) writebuf[i] = 1;

    array_size = array_of_gsizes[0]*array_of_gsizes[1]*array_of_gsizes[2];
    tmpbuf = (int *) calloc(array_size, sizeof(int));
    MPI_Irecv(tmpbuf, 1, newtype, mynod, 10, MPI_COMM_WORLD, &request);
    MPI_Send(writebuf, bufcount, MPI_INT, mynod, 10, MPI_COMM_WORLD);
    MPI_Wait(&request, &status);

    j = 0;
    for (i=0; i<array_size; i++)
	if (tmpbuf[i]) {
	    writebuf[j] = i;
	    j++;
	}
    free(tmpbuf);

    if (j != bufcount) {
	fprintf(stderr, "Error in initializing writebuf on process %d\n", mynod);
	MPI_Abort(MPI_COMM_WORLD, 1);
    }
/* end of initialization */

#if 0 
    /* XXX: make the romio testcases handle more than one command line 
     * argument.. like --aggregation  */
    
    /* for deferred open: hint stuff */
    MPI_Info_create(&info);
    MPI_Info_set(info, "romio_no_indep_rw", "true");
    MPI_Info_set(info, "romio_cb_read", "enable");
    MPI_Info_set(info, "romio_cb_write", "enable");
    MPI_Info_set(info, "cb_nodes", "1");
    MPI_Info_set(info, "cb_config_list", "schwinn.mcs.anl.gov:1");
#endif


    /* write the array to the file */
    errcode = MPI_File_open(MPI_COMM_WORLD, filename, 
		    MPI_MODE_CREATE | MPI_MODE_RDWR, info, &fh);
    if (errcode != MPI_SUCCESS) handle_error(errcode, "MPI_File_open");
   
    errcode = MPI_File_set_view(fh, 0, MPI_INT, newtype, "native", info);
    if (errcode != MPI_SUCCESS) handle_error(errcode, "MPI_File_set_view");

    errcode = MPI_File_write_all(fh, writebuf, bufcount, MPI_INT, &status);
    if (errcode != MPI_SUCCESS) handle_error(errcode, "MPI_File_write_all");
    errcode = MPI_File_close(&fh);
    if (errcode != MPI_SUCCESS) handle_error(errcode, "MPI_File_close");


    /* now read it back */
    readbuf = (int *) malloc(bufcount * sizeof(int));
    errcode = MPI_File_open(MPI_COMM_WORLD, filename, 
		    MPI_MODE_CREATE | MPI_MODE_RDWR, info, &fh);
    if (errcode != MPI_SUCCESS) handle_error(errcode, "MPI_File_open");

    errcode = MPI_File_set_view(fh, 0, MPI_INT, newtype, "native", info);
    if (errcode != MPI_SUCCESS) handle_error(errcode, "MPI_File_set_view");
    errcode = MPI_File_read_all(fh, readbuf, bufcount, MPI_INT, &status);
    if (errcode != MPI_SUCCESS) handle_error(errcode, "MPI_File_read_all");
    errcode = MPI_File_close(&fh);
    if (errcode != MPI_SUCCESS) handle_error(errcode, "MPI_File_close");

    /* check the data read */
    for (i=0; i<bufcount; i++) {
	if (readbuf[i] != writebuf[i]) {
	    errs++;
	    fprintf(stderr, "Process %d, readbuf %d, writebuf %d, i %d\n", mynod, readbuf[i], writebuf[i], i);
	}
    }

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
    free(readbuf);
    free(writebuf);
    free(filename);

    MPI_Finalize();
    return 0;
}
