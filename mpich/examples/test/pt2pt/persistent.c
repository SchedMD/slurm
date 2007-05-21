#include "mpi.h"
#include <stdio.h>

#if defined(NEEDS_STDLIB_PROTOTYPES)
#include "protofix.h"
#endif

int main( int argc, char **argv )
{
    int rank, size, i, len, actlen, expected_len;
    MPI_Request rq;
    MPI_Status status;
    double data[100];

    len = 100;
    MPI_Init( &argc, &argv );
    MPI_Comm_rank( MPI_COMM_WORLD, &rank );
    MPI_Comm_size( MPI_COMM_WORLD, &size );

    if (size < 3 ) {
	fprintf( stderr, "This test requires more than 2 proceses\n" );
	MPI_Finalize();
	return 1;
	}

    if (rank == 0) {
	MPI_Recv_init( data, len, MPI_DOUBLE, MPI_ANY_SOURCE, MPI_ANY_TAG, 
		       MPI_COMM_WORLD, &rq );
	for (i=1; i<size; i++) {
	    printf( "Receiving message %d\n", i );
	    MPI_Start( &rq );
	    MPI_Wait( &rq, &status );
	    if (status.MPI_SOURCE != status.MPI_TAG) {
		printf( "Error in received message (source and tag)\n" );
		printf( "Source was %d and tag was %d\n",
			status.MPI_SOURCE, status.MPI_TAG );
		}
	    MPI_Get_count( &status, MPI_DOUBLE, &actlen );
	    expected_len = (status.MPI_SOURCE < 10) ? status.MPI_SOURCE * 10 :
		100;
	    if (actlen != expected_len) {
		printf( "Got %d words, expected %d words\n", actlen, 
		       expected_len );
		}
	    printf( "Received message %d\n", i );
	    }
	MPI_Request_free( &rq );
	printf( "Completed all receives\n" );
	}
    else {
	MPI_Send( data, (rank < 10) ? rank * 10 : 100, 
		  MPI_DOUBLE, 0, rank, MPI_COMM_WORLD );
	}
MPI_Finalize();
return 0;
}
