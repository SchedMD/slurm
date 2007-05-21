/* 
   This test checks for possible interference between 
   successive calls to MPI_Allreduce.  Some users, on some MPI implementations
   and platforms, have had to add MPI_Barrier before MPI_Allreduce calls.
   */
#include "mpi.h"
#include <stdio.h>

#define MAX_LOOP 1000

int main( int argc, char *argv[] )
{
    int i, in_val, out_val;
    int rank, size;
    int errs = 0, toterrs;

    MPI_Init( &argc, &argv );

    MPI_Comm_size( MPI_COMM_WORLD, &size );
    MPI_Comm_rank( MPI_COMM_WORLD, &rank );
    for (i=0; i<MAX_LOOP; i++) {
	in_val = (i & 0x1) ? 10 : -10;
	MPI_Allreduce( &in_val, &out_val, 1, MPI_INT, MPI_SUM, 
		       MPI_COMM_WORLD );
	if (i & 0x1) {
	    if (out_val != 10 * size) {
		errs++;
		printf( "[%d] Error in out_val = %d\n", rank, out_val );
	    }
	}
	else {
	    if (-out_val != 10 * size) {
		errs++;
		printf( "[%d] Error in out_val = %d\n", rank, out_val );
	    }
	}
    }
    MPI_Barrier( MPI_COMM_WORLD );
    MPI_Allreduce( &errs, &toterrs, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD );
    
    if (rank == 0) {
	if (toterrs) 
	    printf( " Found %d errors\n", toterrs );
	else
	    printf( " No Errors\n" );
    }

    MPI_Finalize( );
    return 0;
}
