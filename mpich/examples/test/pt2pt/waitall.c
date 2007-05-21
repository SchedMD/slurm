/*
 * This code tests waitall; in particular, the that ordering requirement
 * on nonblocking communication is observed.
 */

#include <stdio.h>
#include "mpi.h"

#if defined(NEEDS_STDLIB_PROTOTYPES)
#include "protofix.h"
#endif

#ifdef HAVE_UNISTD_H
/* For sleep */
#include <unistd.h>
#endif

#define MAX_REQ 32

#ifndef HAVE_SLEEP
void sleep( int secs )
{
#ifdef VX_WORKS
    /* Also needs include <time.h>? */
    struct timespec rqtp = { 10, 0 };
    nanosleep(&rqtp, NULL);
#else
    double t;
    t = MPI_Wtime();
    while (MPI_Wtime() - t < (double)secs) ;
#endif
}
#endif

int main( int argc, char **argv )
{
    int rank, size;
    int i, j, count, err = 0, toterr;
    MPI_Request r[MAX_REQ];
    MPI_Status  s[MAX_REQ];
    int         buf[MAX_REQ][MAX_REQ];

    MPI_Init( &argc, &argv );
    MPI_Comm_rank( MPI_COMM_WORLD, &rank );
    MPI_Comm_size( MPI_COMM_WORLD, &size );

    if (size < 2) {
	fprintf( stderr, "This test requires at least 2 processes\n" );
	MPI_Abort( MPI_COMM_WORLD, 1 );
    }
    /* First, cause the wait all to happen AFTER the Sends */
    if (rank == 0) {
	for (i=0; i<MAX_REQ; i++) {
	    MPI_Irecv( buf[i], i+1, MPI_INT, 1, 99, MPI_COMM_WORLD, 
		       &r[MAX_REQ-1-i] ); 
	}
	MPI_Waitall( MAX_REQ, r, s );
	/* Check that we've received the correct data */
	for (i=0; i<MAX_REQ; i++) {
	    MPI_Get_count( &s[MAX_REQ-1-i], MPI_INT, &count );
	    if (count != i) {
		err++;
		fprintf( stderr, "Wrong count (%d) for request %d\n", 
			 count, MAX_REQ-1-i );
	    }
	}
    }
    else if (rank == 1) {
	for (i=0; i<MAX_REQ; i++) {
	    for (j=0; j<=i; j++)
		buf[i][j] = i * MAX_REQ + j;
	    MPI_Send( buf[i], i, MPI_INT, 0, 99, MPI_COMM_WORLD );
	}
    }

    /* Second, cause the waitall to start BEFORE the Sends */
    if (rank == 0) {
	for (i=0; i<MAX_REQ; i++) {
	    MPI_Irecv( buf[i], i+1, MPI_INT, 1, 99, MPI_COMM_WORLD, 
		       &r[MAX_REQ-1-i] ); 
	}
	MPI_Send( MPI_BOTTOM, 0, MPI_INT, 1, 0, MPI_COMM_WORLD );
	MPI_Waitall( MAX_REQ, r, s );
	/* Check that we've received the correct data */
	for (i=0; i<MAX_REQ; i++) {
	    MPI_Get_count( &s[MAX_REQ-1-i], MPI_INT, &count );
	    if (count != i) {
		err++;
		fprintf( stderr, 
			 "Wrong count (%d) for request %d (waitall posted)\n", 
			 count, MAX_REQ-1-i );
	    }
	}
    }
    else if (rank == 1) {
	MPI_Recv( MPI_BOTTOM, 0, MPI_INT, 0, 0, MPI_COMM_WORLD, &s[0] );
	sleep( 2 );
	for (i=0; i<MAX_REQ; i++) {
	    for (j=0; j<=i; j++)
		buf[i][j] = i * MAX_REQ + j;
	    MPI_Send( buf[i], i, MPI_INT, 0, 99, MPI_COMM_WORLD );
	}
    }


    MPI_Barrier( MPI_COMM_WORLD );
    if (rank == 0) {
	toterr = err;
	if (toterr == 0) 
	    printf( "Test complete\n" );
	else
	    printf( "Found %d errors in test!\n", toterr );
    }
    
    MPI_Finalize();
    return 0;
}



