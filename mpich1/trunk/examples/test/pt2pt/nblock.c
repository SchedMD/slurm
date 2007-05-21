#include <stdio.h>
#include <stdlib.h>
#include "mpi.h"

#if defined(NEEDS_STDLIB_PROTOTYPES)
#include "protofix.h"
#endif

#ifndef MAXNP
#define MAXNP 16
#endif

/*
   Test to make sure that nonblocking routines actually work.  This
   stresses them by sending large numbers of requests and receiving them
   piecemeal.
 */
int main( int argc, char **argv )
{
    int count, tag, nsend, myid, np, rcnt, scnt, i, j;
    int *(sbuf[MAXNP]), *(rbuf[MAXNP]);
    MPI_Status status;
    MPI_Request *rsend, *rrecv;

    MPI_Init( &argc, &argv );
    MPI_Comm_rank( MPI_COMM_WORLD, &myid );
    MPI_Comm_size( MPI_COMM_WORLD, &np );

    if (np > MAXNP) {
	fprintf( stderr, 
		 "This test must run with at most %d processes\n", MAXNP );
	MPI_Abort( MPI_COMM_WORLD, 1 );
    }

    nsend = 3 * np;
    rsend = (MPI_Request *) malloc ( nsend * sizeof(MPI_Request) );
    rrecv = (MPI_Request *) malloc ( nsend * sizeof(MPI_Request) );
    if (!rsend || !rrecv) {
	fprintf( stderr, "Failed to allocate space for requests\n" );
	MPI_Abort( MPI_COMM_WORLD, 1 );
    }

    for (count = 1; count < 10000; count *= 2) {
	for (i=0; i<nsend; i++) {
	    sbuf[i] = (int *)calloc( count, sizeof(int) );
	    rbuf[i] = (int *)malloc( count * sizeof(int) );
	    if (!sbuf[i] || !rbuf[i]) {
		fprintf( stderr, "Unable to allocate %d ints\n", count );
		MPI_Abort( MPI_COMM_WORLD, 1 );
	    }
	}
	
	/* We'll send/recv from everyone */
	scnt = 0;
	rcnt = 0;
	/* The MPI standard requires that active buffers be distinct
	   in nonblocking calls */
	for (j=0; j<3; j++) {
	    tag = j;
	    for (i=0; i<np; i++) {
		if (i != myid) {
		    MPI_Isend( sbuf[scnt], count, MPI_INT, i, 
			       tag, MPI_COMM_WORLD, &rsend[scnt] );
		    scnt++;
		}
		
	    }
	    for (i=0; i<np; i++) {
		if (i != myid) {
		    MPI_Irecv( rbuf[rcnt], count, MPI_INT, i, 
			       tag, MPI_COMM_WORLD, &rrecv[rcnt] );
		    rcnt++;
		}
	    }
	}
	/* In general, it would be better to use MPI_Waitall, but this should
	   work as well */
	for (i=0; i<rcnt; i++) {
	    MPI_Wait( &rrecv[i], &status );
	}
	for (i=0; i<scnt; i++) {
	    MPI_Wait( &rsend[i], &status );
	}

	for (i=0; i<nsend; i++) {
	    free( sbuf[i] );
	    free( rbuf[i] );
	}

	MPI_Barrier( MPI_COMM_WORLD );
	if (myid == 0 && (count % 64) == 0) {
	    printf( "All processes completed for count = %ld ints of data\n", 
		    (long)count );
	    fflush(stdout);
	}
    }
    MPI_Finalize();
    return 0;
}


