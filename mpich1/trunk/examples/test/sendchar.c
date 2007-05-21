#include <stdio.h>
#include "mpi.h"

#if defined(NEEDS_STDLIB_PROTOTYPES)
#include "protofix.h"
#endif


/*
   This is a simple test that can be used on heterogeneous systems that
   use XDR encoding to check for correct lengths. 

   Sends back and forth to check on one-sided conversion schemes
 */
int main( int argc, char **argv )
{
    int rank, c;
    MPI_Status status;
    char buf[10];

    MPI_Init( &argc, &argv );
    MPI_Comm_rank( MPI_COMM_WORLD, &rank );

    /* Receives from ANY check for common format */
    if (rank == 0) {
	printf( "Sending from 1 to 0\n" );
	MPI_Recv( buf, 10, MPI_CHAR, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, 
		  &status );
	MPI_Get_count( &status, MPI_CHAR, &c );
	if (c != 10) { 
	    printf( "(1)Did not get correct count; expected 10, got %d\n", c );
	    }
	}
    else if (rank == 1) {
	MPI_Send( buf, 10, MPI_CHAR, 0, 0, MPI_COMM_WORLD );
	}

    if (rank == 1) {
	MPI_Recv( buf, 10, MPI_CHAR, MPI_ANY_SOURCE, 1, MPI_COMM_WORLD, 
		  &status );
	MPI_Get_count( &status, MPI_CHAR, &c );
	if (c != 10) { 
	    printf( "(2)Did not get correct count; expected 10, got %d\n", c );
	    }
	}
    else if (rank == 0) {
	printf( "Sending from 0 to 1\n" );
	MPI_Send( buf, 10, MPI_CHAR, 1, 1, MPI_COMM_WORLD );
	}

    /* Receives from specific note check for special cases */
    if (rank == 0) {
	printf( "Sending from 1 to 0\n" );
	MPI_Recv( buf, 10, MPI_CHAR, 1, 0, MPI_COMM_WORLD, 
		  &status );
	MPI_Get_count( &status, MPI_CHAR, &c );
	if (c != 10) { 
	    printf( "(3)Did not get correct count; expected 10, got %d\n", c );
	    }
	}
    else if (rank == 1) {
	MPI_Send( buf, 10, MPI_CHAR, 0, 0, MPI_COMM_WORLD );
	}

    if (rank == 1) {
	MPI_Recv( buf, 10, MPI_CHAR, 0, 1, MPI_COMM_WORLD, 
		  &status );
	MPI_Get_count( &status, MPI_CHAR, &c );
	if (c != 10) { 
	    printf( "(4)Did not get correct count; expected 10, got %d\n", c );
	    }
	}
    else if (rank == 0) {
	printf( "Sending from 0 to 1\n" );
	MPI_Send( buf, 10, MPI_CHAR, 1, 1, MPI_COMM_WORLD );
	}
    MPI_Finalize();
return 0;
}
