#include "mpi.h"
#include <stdio.h>
#include <stdlib.h>
#include "test.h"

#define MAX_REQ 32
#define MAX_MSG_CNT 32000
#define MAX_MSG 2048
/* 
   This program tests a flood of data of short messages to test handling
   of both incoming messages and internal message queues
 */

void SetupData ( int *, int, int );
void SetupRdata ( int *, int );
int  CheckData ( int *, int, int, MPI_Status * );

#ifdef VERBOSE
static int verbose = 1;
#else
static int verbose = 0;
#endif


int main( int argc, char **argv )
{
    MPI_Comm comm;
    MPI_Request r[MAX_REQ];
    MPI_Status  s[MAX_REQ];
    int msgsize, maxmsg, root, i, j, size, rank, err = 0, msgcnt, toterr;
    int *sbuf, *rbuf;

    MPI_Init( &argc, &argv );
    
    comm = MPI_COMM_WORLD;

    MPI_Comm_size( comm, &size );
    MPI_Comm_rank( comm, &rank );

    if (size < 2) {
	printf( "This test requires at least 2 processors\n" );
	MPI_Abort( comm, 1 );
    }

    /* First, try large blocking sends to root */
    root = 0;
    
    maxmsg =  MAX_MSG;
    msgsize = 128;
    msgcnt  = MAX_MSG_CNT;
    if (rank == root && verbose) printf( "Blocking sends: " );
    while (msgsize <= maxmsg) {
	if (rank == root) {
	    if (verbose) { printf( "%d ", msgsize ); fflush( stdout ); }
	    rbuf = (int *)malloc( msgsize * sizeof(int) );
	    if (!rbuf) {
		printf( "Could not allocate %d words\n", msgsize );
		MPI_Abort( comm, 1 );
	    }
	    for (i=0; i<size; i++) {
		if (i == rank) continue;
		for (j=0; j<msgcnt; j++) {
		    SetupRdata( rbuf, msgsize );
		    MPI_Recv( rbuf, msgsize, MPI_INT, i, 2*i, comm, s );
		    err += CheckData( rbuf, msgsize, 2*i, s );
		}
	    }
	    free( rbuf );
	}
	else {
	    sbuf = (int *)malloc( msgsize * sizeof(int) );
	    if (!sbuf) {
		printf( "Could not allocate %d words\n", msgsize );
		MPI_Abort( comm, 1 );
	    }
	    SetupData( sbuf, msgsize, 2*rank );
	    for (j=0; j<msgcnt; j++) 
		MPI_Send( sbuf, msgsize, MPI_INT, root, 2*rank, comm );
	    free( sbuf );
	}
	msgsize *= 4;
    }
    if (rank == 0 && verbose) { printf( "\n" ); fflush( stdout ); }

    /* Next, try unexpected messages with Isends */
    msgsize = 128;
    maxmsg  = MAX_MSG;
    msgcnt  = MAX_REQ;
    if (rank == root && verbose) printf( "Unexpected recvs: " );
    while (msgsize <= maxmsg) {
	if (rank == root) {
	    if (verbose) { printf( "%d ", msgsize ); fflush( stdout ); }
	    rbuf = (int *)malloc( msgsize * sizeof(int) );
	    if (!rbuf) {
		printf( "Could not allocate %d words\n", msgsize );
		MPI_Abort( comm, 1 );
	    }
	    MPI_Barrier( comm );
	    for (i=0; i<size; i++) {
		if (i == rank) continue;
		for (j=0; j<msgcnt; j++) {
		    SetupRdata( rbuf, msgsize );
		    MPI_Recv( rbuf, msgsize, MPI_INT, i, 2*i, comm, s );
		    err += CheckData( rbuf, msgsize, 2*i, s );
		}
	    }
	    free( rbuf );
	}
	else {
	    sbuf = (int *)malloc( msgsize * sizeof(int) );
	    if (!sbuf) {
		printf( "Could not allocate %d words\n", msgsize );
		MPI_Abort( comm, 1 );
	    }
	    SetupData( sbuf, msgsize, 2*rank );
	    for (j=0; j<msgcnt; j++) {
		MPI_Isend( sbuf, msgsize, MPI_INT, root, 2*rank, comm, &r[j] );
	    }
	    MPI_Barrier( comm );
	    MPI_Waitall( msgcnt, r, s );
	    free( sbuf );
	}
	msgsize *= 4;
    }
    if (rank == 0 && verbose) { printf( "\n" ); fflush( stdout ); }

    /* Try large synchronous blocking sends to root */
    root = 0;
    
    msgsize = 128;
    maxmsg  = MAX_MSG;
    if (rank == root && verbose) printf( "Synchronous sends: " );
    while (msgsize <= maxmsg) {
	if (rank == root) {
	    if (verbose) { printf( "%d ", msgsize ); fflush( stdout ); }
	    rbuf = (int *)malloc( msgsize * sizeof(int) );
	    if (!rbuf) {
		printf( "Could not allocate %d words\n", msgsize );
		MPI_Abort( comm, 1 );
	    }
	    for (i=0; i<size; i++) {
		if (i == rank) continue;
		for (j=0; j<msgcnt; j++) {
		    SetupRdata( rbuf, msgsize );
		    MPI_Recv( rbuf, msgsize, MPI_INT, i, 2*i, comm, s );
		    err += CheckData( rbuf, msgsize, 2*i, s );
		}
	    }
	    free( rbuf );
	}
	else {
	    sbuf = (int *)malloc( msgsize * sizeof(int) );
	    if (!sbuf) {
		printf( "Could not allocate %d words\n", msgsize );
		MPI_Abort( comm, 1 );
	    }
	    SetupData( sbuf, msgsize, 2*rank );
	    for (j=0; j<msgcnt; j++) 
		MPI_Ssend( sbuf, msgsize, MPI_INT, root, 2*rank, comm );
	    free( sbuf );
	}
	msgsize *= 4;
    }
    if (rank == 0 && verbose) { printf( "\n" ); fflush( stdout ); }

    MPI_Allreduce( &err, &toterr, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD );
    
    if (rank == 0) {
	if (toterr == 0) printf( " No Errors\n" );
	else printf( "!! found %d errors\n", toterr );
    }
    if (toterr) {
	printf( "!! found %d errors on processor %d\n", err, rank );
    }

    MPI_Finalize( );
    return 0;
}

void SetupData( sbuf, n, tag )
int *sbuf, n, tag;
{
    int i;

    for (i=0; i<n; i++) 
	sbuf[i] = i;
}

int CheckData( rbuf, n, tag, s )
int *rbuf, n, tag;
MPI_Status *s;
{
    int act_n, i;

    MPI_Get_count( s, MPI_INT, &act_n );
    if (act_n != n) {
	printf( "Received %d instead of %d ints\n", act_n, n );
	return 1;
    }
    for (i=0; i<n; i++) {
	if (rbuf[i] != i) {
	    printf( "rbuf[%d] is %d, should be %d\n", i, rbuf[i], i );
	    return 1;
	}
    }
    return 0;
}

void SetupRdata( rbuf, n )
int *rbuf, n;
{
    int i;
    
    for (i=0; i<n; i++) rbuf[i] = -(i+1);
}
