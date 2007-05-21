/* -*- Mode: C; c-basic-offset:4 ; -*- */

#include "mpi.h"
#include <stdio.h>

int main( int argc, char *argv[] )
{
    int dims[10];
    int i, j, ndims, totnodes, err, errcnt = 0;

    MPI_Init( &argc, &argv );

    MPI_Errhandler_set( MPI_COMM_WORLD, MPI_ERRORS_RETURN );

    /* Try for error checks */
    dims[0] = 2;
    dims[1] = 2;
    dims[2] = 0;
    err = MPI_Dims_create( 26, 3, dims );
    if (err == MPI_SUCCESS) {
	printf( "The product of the specified dims does not divide the nnodes and MPI_Dims_create did not return an error\n" );
	for (i=0; i<3; i++) {
	    printf( "dims[%d] = %d\n", i, dims[i] );
	}
	errcnt++;
    }

    /* Check for a few reasonable decompositions */
    dims[0] = dims[1] = 0;
    err = MPI_Dims_create( 16, 2, dims );
    if (err) {
	char msg[MPI_MAX_ERROR_STRING];
	int result_len;
	MPI_Error_string( err, msg, &result_len );
	printf( "Unexpected error return from dims_create (16,2) %s\n", msg );
	errcnt++;
    }
    else {
	if (dims[0] * dims[1] != 16) {
	    printf( "Returned dimensions do not match request\n" );
	    errcnt++;
	}
#ifdef MPICH_NAME
	if (dims[0] != 4) {
	    errcnt++;
	    printf( "Expected 4 x 4, got %d x %d\n", dims[0],dims[1] );
	}
#endif
    }

    dims[0] = dims[1] = 0;
    /* 60 = 2 * 2 * 3 * 5 */
    err = MPI_Dims_create( 60, 2, dims );
    if (err) {
	char msg[MPI_MAX_ERROR_STRING];
	int result_len;
	MPI_Error_string( err, msg, &result_len );
	printf( "Unexpected error return from dims_create (16,2) %s\n", msg );
	errcnt++;
    }
    else {
	if (dims[0] * dims[1] != 60) {
	    printf( "Returned dimensions do not match request (%d)\n",
		    dims[0] * dims[1] );
	    errcnt++;
	}
#ifdef MPICH_NAME
	if (dims[0] == 1 || dims[1] == 1) {
	    errcnt++;
	    printf( "Expected rectangular decomp, got %d x %d\n", 
		    dims[0],dims[1] );
	}
#endif
    }

    /* Test a range of values */
    for (ndims=1; ndims<=4; ndims++) {
	for (i=2; i<64; i++) {
	    for (j=0; j<ndims; j++) {
		dims[j] = 0;
	    }
	    MPI_Dims_create( i, ndims, dims );
	    /* Check the results */
	    totnodes = 1;
	    for (j=0; j<ndims; j++) {
		totnodes *= dims[j];
		if (dims[j] <= 0) {
		    errcnt++;
		    printf( "Non positive dims[%d] = %d for %d nodes and %d ndims\n", 
			    j, dims[j], i, ndims );
		}
	    }
	    if (totnodes != i) {
		errcnt++;
		printf( "Did not correctly partition %d nodes among %d dims (got %d nodes)\n",
			i, ndims, totnodes );
		if (ndims > 1) {
		    printf( "Dims = " );
		    for (j=0; j<ndims; j++) {
			printf( " %d", dims[j] );
		    }
		    printf( "\n" );
		}
	    }
		
	}
    }
    /* Summarize the results */
    if (errcnt) {
	printf( " %d errors found\n", errcnt );
    }
    else {
	printf( " No Errors\n" );
    }
    MPI_Finalize( );
    return 0;
}
