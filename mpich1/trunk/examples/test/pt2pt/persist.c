#include "mpi.h"
#include <stdio.h>
#include <stdlib.h>

#if defined(NEEDS_STDLIB_PROTOTYPES)
#include "protofix.h"
#endif

/*
 * This example causes the IBM SP2 MPI version to generate the message
 * ERROR: 0032-158 Persistent request already active  (2) in MPI_Startall, task 0
 * in the SECOND set of MPI_Startall (after the MPI_Request_free).
 */
int main( int argc, char **argv )
{
    MPI_Request r[4];
    MPI_Status  statuses[4];
    double sbuf1[10], sbuf2[10];
    double rbuf1[10], rbuf2[10];
    int size, rank, up_nbr, down_nbr, i;

    MPI_Init( &argc, &argv );
    MPI_Comm_size( MPI_COMM_WORLD, &size );
    MPI_Comm_rank( MPI_COMM_WORLD, &rank );

    up_nbr = (rank + 1) % size;
    down_nbr = (size + rank - 1) % size;

    MPI_Recv_init( rbuf1, 10, MPI_DOUBLE, down_nbr, 0, MPI_COMM_WORLD, &r[0] );
    MPI_Recv_init( rbuf2, 10, MPI_DOUBLE, up_nbr, 1, MPI_COMM_WORLD, &r[1] );
    MPI_Send_init( sbuf1, 10, MPI_DOUBLE, up_nbr, 0, MPI_COMM_WORLD, &r[2] );
    MPI_Send_init( sbuf2, 10, MPI_DOUBLE, down_nbr, 1, MPI_COMM_WORLD, &r[3] );
    MPI_Startall( 4, r );
    MPI_Waitall( 4, r, statuses );

    for (i=0; i<4; i++) {
	MPI_Request_free( &r[i] );
	}

    MPI_Recv_init( rbuf1, 10, MPI_DOUBLE, down_nbr, 0, MPI_COMM_WORLD, &r[0] );
    MPI_Recv_init( rbuf2, 10, MPI_DOUBLE, up_nbr, 1, MPI_COMM_WORLD, &r[1] );
    MPI_Send_init( sbuf1, 10, MPI_DOUBLE, up_nbr, 0, MPI_COMM_WORLD, &r[2] );
    MPI_Send_init( sbuf2, 10, MPI_DOUBLE, down_nbr, 1, MPI_COMM_WORLD, &r[3] );
    MPI_Startall( 4, r );
    MPI_Waitall( 4, r, statuses );

    for (i=0; i<4; i++) {
	MPI_Request_free( &r[i] );
	}

    if (rank == 0) printf( "No errors\n" );
    MPI_Finalize();
    return 0;
}
