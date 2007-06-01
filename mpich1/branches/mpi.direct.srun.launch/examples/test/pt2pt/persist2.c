#include "mpi.h"
#include <stdio.h>
#include <stdlib.h>

#if defined(NEEDS_STDLIB_PROTOTYPES)
#include "protofix.h"
#endif

/*
 */
int main( int argc, char **argv )
{
    MPI_Request r[4];
    MPI_Status  statuses[4];
    double sbuf1[10], sbuf2[10];
    double rbuf1[10], rbuf2[10];
    double userbuf[40+4*MPI_BSEND_OVERHEAD];
    int size, rank, up_nbr, down_nbr, i, err, toterr;

    MPI_Init( &argc, &argv );
    MPI_Comm_size( MPI_COMM_WORLD, &size );
    MPI_Comm_rank( MPI_COMM_WORLD, &rank );

    up_nbr = (rank + 1) % size;
    down_nbr = (size + rank - 1) % size;

    for (i=0; i<10; i++) {
	sbuf1[i] = (double)i;
	sbuf2[i] = (double)(i+20);
    }
    MPI_Buffer_attach( userbuf, 40*sizeof(double) + 4 * MPI_BSEND_OVERHEAD );

    MPI_Recv_init( rbuf1, 10, MPI_DOUBLE, down_nbr, 0, MPI_COMM_WORLD, &r[0] );
    MPI_Recv_init( rbuf2, 10, MPI_DOUBLE, up_nbr, 1, MPI_COMM_WORLD, &r[1] );
    MPI_Bsend_init( sbuf1, 10, MPI_DOUBLE, up_nbr, 0, MPI_COMM_WORLD, &r[2] );
    MPI_Bsend_init( sbuf2, 10, MPI_DOUBLE, down_nbr, 1, MPI_COMM_WORLD, &r[3] );
    MPI_Startall( 4, r );
    MPI_Waitall( 4, r, statuses );

    for (i=0; i<4; i++) {
	MPI_Request_free( &r[i] );
	}

    MPI_Recv_init( rbuf1, 10, MPI_DOUBLE, down_nbr, 0, MPI_COMM_WORLD, &r[0] );
    MPI_Recv_init( rbuf2, 10, MPI_DOUBLE, up_nbr, 1, MPI_COMM_WORLD, &r[1] );
    MPI_Bsend_init( sbuf1, 10, MPI_DOUBLE, up_nbr, 0, MPI_COMM_WORLD, &r[2] );
    MPI_Bsend_init( sbuf2, 10, MPI_DOUBLE, down_nbr, 1, MPI_COMM_WORLD, &r[3] );
    MPI_Startall( 4, r );
    MPI_Waitall( 4, r, statuses );

    for (i=0; i<4; i++) {
	MPI_Request_free( &r[i] );
	}

    /* Check data */
    err = 0;
    for (i=0; i<10;i++) {
	if (rbuf1[i] != i) {
	    err++;
	    if (err < 10) 
		fprintf( stderr, "Expected %d, rbuf1[%d] = %f\n", i, i, 
			 rbuf1[i] );
	}
	if (rbuf2[i] != i+20) {
	    err++;
	    if (err < 10) 
		fprintf( stderr, "Expected %d, rbuf2[%d] = %f\n", i+20, i, 
			 rbuf2[i] );
	}
    }

    MPI_Allreduce( &err, &toterr, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD );
    if (rank == 0) {
	if (toterr == 0) printf( "No errors\n" );
	else             printf( "Found %d errors\n", toterr );
    }

    MPI_Finalize();
    return 0;
}
