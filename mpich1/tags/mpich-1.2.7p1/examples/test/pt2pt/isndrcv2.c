#include <stdio.h>
#include "mpi.h"

int main( int argc, char **argv )
{
    int rank, size;
    MPI_Request r1, r2;
    MPI_Status  s;
    int         buf[10000], buf2[10000], count, tag1, tag2;

    count = 10000;
    tag1  = 100;
    tag2  = 1000;

    MPI_Init( &argc, &argv );
    MPI_Comm_rank( MPI_COMM_WORLD, &rank );
    MPI_Comm_size( MPI_COMM_WORLD, &size );

    if (rank == 0) {
	MPI_Isend( buf, count, MPI_INT, 1, tag1, MPI_COMM_WORLD, &r1 );
	MPI_Isend( buf2, count, MPI_INT, 1, tag2, MPI_COMM_WORLD, &r2 );
	MPI_Wait( &r1, &s );
	MPI_Wait( &r2, &s );
    }
    else if (rank == 1) {
	MPI_Irecv( buf2, count, MPI_INT, 0, tag2, MPI_COMM_WORLD, &r2 );
	MPI_Irecv( buf,  count, MPI_INT, 0, tag1, MPI_COMM_WORLD, &r1 );
	MPI_Wait( &r2, &s );
	if (s.MPI_TAG != tag2) {
	    printf( "Error in receive order\n" );
	}
	MPI_Wait( &r1, &s );
    }

    MPI_Barrier( MPI_COMM_WORLD );
    if (rank == 0) {
	printf( "Test completed\n" );
    }
    MPI_Finalize( );
    return 0;
}
