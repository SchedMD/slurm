#include "mpi.h"
#include <stdio.h>
/* stdlib.h needed for malloc declaration */
#include <stdlib.h>

#if defined(NEEDS_STDLIB_PROTOTYPES)
#include "protofix.h"
#endif

/*
 * This file tests MPI_Type_hindexed by describing parts of a triangular
 * matrix, stored in a square matrix, and sending sending it.
 * 
 * The matrix is stored in column-major, and the tests use
 * MPI_Type_vector or MPI_Type_struct to define the elements that are sent
 */

int main( int argc, char **argv )
{
    MPI_Datatype rowtype, mattype;
    int          *sbuf, *rbuf;
    int          rank, mat_n;
    static int   blens[2] = { 1, 1 };
    static MPI_Datatype types[2] = { MPI_INT, MPI_UB };
    int          *mat_blens, i ;
    MPI_Aint     *mat_displs;
    MPI_Aint     displs[2];
    MPI_Status   status;
    int          err, row, col;

    MPI_Init( &argc, &argv );

    err = 0;
    MPI_Comm_rank( MPI_COMM_WORLD, &rank );
    mat_n = 10;
    sbuf = (int *) malloc( mat_n * mat_n * sizeof(int) );
    rbuf = (int *) malloc( mat_n * mat_n * sizeof(int) );
    if (!sbuf || !rbuf) {
	MPI_Abort( MPI_COMM_WORLD, 1 );
    }

    /* Define a row type based on a strided struct type */
    displs[0] = 0;
    displs[1] = mat_n*sizeof(int);
    MPI_Type_struct( 2, blens, displs, types, &rowtype );
    /* MPI_Type_commit( &rowtype ); */

    /* Define an hindexed type that defines all of the rows of the
       triangular part of sbuf */
    
    mat_blens = (int *)malloc( mat_n * sizeof(int) );
    mat_displs = (MPI_Aint *)malloc( mat_n * sizeof(MPI_Aint) );
    for (i=0; i<mat_n; i++) {
	mat_blens[i] = mat_n - i;
	MPI_Address( &sbuf[i + i * mat_n], &mat_displs[i] );
	if (i != 0)
	    mat_displs[i] = mat_displs[i] - mat_displs[0];
    }
    mat_displs[0] = 0;
    MPI_Type_hindexed( mat_n, mat_blens, mat_displs, rowtype, &mattype );
    MPI_Type_commit( &mattype );
    MPI_Type_free( &rowtype );

    /* Load up the data */
    for (i=0; i<mat_n * mat_n; i++) {
	sbuf[i] = i;
	rbuf[i] = -i;
    }
    
    /* Send it and receive it in the same order */
    MPI_Sendrecv( sbuf, 1, mattype, rank, 0, rbuf, 1, mattype, rank, 0, 
		  MPI_COMM_WORLD, &status );

    for (row = 0; row<mat_n; row++) {
	for (col = row; col<mat_n; col++) {
	    if (rbuf[row + col*mat_n] != sbuf[row + col*mat_n]) {
		err++;
		fprintf( stderr, "mat(%d,%d) = %d, not %d\n",
			 row, col, rbuf[row+col*mat_n], sbuf[row+col*mat_n] );
	    }
	}
    }

    /* Send hindexed and receive contiguous */
    MPI_Sendrecv( sbuf, 1, mattype, rank, 1, 
		  rbuf, (mat_n * (mat_n + 1))/2, MPI_INT, rank, 1, 
		  MPI_COMM_WORLD, &status );
    i = 0;
    for (row = 0; row<mat_n; row++) {
	for (col = row; col<mat_n; col++) {
	    if (rbuf[i] != sbuf[row + col*mat_n]) {
		err++;
		fprintf( stderr, "rbuf(%d,%d) = %d, not %d\n",
			 row, col, rbuf[i], sbuf[row+col*mat_n] );
	    }
	    i++;
	}
    }

    MPI_Type_free( &mattype );
    if (err == 0) printf( "Test passed\n" );
    else          printf( "Test failed with %d errors\n", err );

    MPI_Finalize();
    return 0;
}
