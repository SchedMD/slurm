#include "mpi.h"
#include <stdio.h>
#include <stdlib.h>
#include "test.h"

#define MAX_REQ 16
#define DEF_MAX_MSG 2000000
/* 
   This program tests a flood of data for both unexpected and expected messages   to test any internal message fragmentation or protocol shifts

   An optional argument can change the maximum message size.  For example, use
      flood 9000000
   to stress the memory system (the size is the number of ints, not bytes)
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
    int msgsize, maxmsg, root, i, size, rank, err = 0, toterr;
    int max_msg_size = DEF_MAX_MSG;
    int *sbuf, *rbuf;

    MPI_Init( &argc, &argv );

    comm = MPI_COMM_WORLD;

    MPI_Comm_size( comm, &size );
    MPI_Comm_rank( comm, &rank );

    if (size < 2) {
	printf( "This test requires at least 2 processors\n" );
	MPI_Abort( comm, 1 );
    }

    /* Check for a max message argument */
    if (rank == 0) {
	if (argc > 1) {
	    max_msg_size = atoi( argv[1] );
	    /* Correct if unrecognized argument */
	    if (max_msg_size <= 0) max_msg_size = DEF_MAX_MSG;
	}
    }
    MPI_Bcast( &max_msg_size, 1, MPI_INT, 0, MPI_COMM_WORLD );

    /* First, try large blocking sends to root */
    root = 0;
    
    msgsize = 128;
    maxmsg  = max_msg_size;
    if (rank == root && verbose) printf( "Blocking sends: " );
    while (msgsize < maxmsg) {
	if (rank == root) {
	    if (verbose) { printf( "%d ", msgsize ); fflush( stdout ); }
	    rbuf = (int *)malloc( msgsize * sizeof(int) );
	    if (!rbuf) {
		printf( "Could not allocate %d words\n", msgsize );
		MPI_Abort( comm, 1 );
	    }
	    for (i=0; i<size; i++) {
		if (i == rank) continue;
		SetupRdata( rbuf, msgsize );
		MPI_Recv( rbuf, msgsize, MPI_INT, i, 2*i, comm, s );
		err += CheckData( rbuf, msgsize, 2*i, s );
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
	    MPI_Send( sbuf, msgsize, MPI_INT, root, 2*rank, comm );
	    free( sbuf );
	}
	msgsize *= 4;
    }
    if (rank == 0 && verbose) { printf( "\n" ); fflush( stdout ); }

    /* Next, try unexpected messages with Isends */
    msgsize = 128;
    maxmsg  = max_msg_size;
    if (rank == root && verbose) printf( "Unexpected recvs: " );
    while (msgsize < maxmsg) {
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
		SetupRdata( rbuf, msgsize );
		MPI_Recv( rbuf, msgsize, MPI_INT, i, 2*i, comm, s );
		err += CheckData( rbuf, msgsize, 2*i, s );
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
	    MPI_Isend( sbuf, msgsize, MPI_INT, root, 2*rank, comm, r );
	    MPI_Barrier( comm );
	    MPI_Wait( r, s );
	    free( sbuf );
	}
	msgsize *= 4;
    }
    if (rank == 0 && verbose) { printf( "\n" ); fflush( stdout ); }

    /* Try large synchronous blocking sends to root */
    root = 0;
    
    msgsize = 128;
    maxmsg  = max_msg_size;
    if (rank == root && verbose) printf( "Synchronous sends: " );
    while (msgsize < maxmsg) {
	if (rank == root) {
	    if (verbose) { printf( "%d ", msgsize ); fflush( stdout ); }
	    rbuf = (int *)malloc( msgsize * sizeof(int) );
	    if (!rbuf) {
		printf( "Could not allocate %d words\n", msgsize );
		MPI_Abort( comm, 1 );
	    }
	    for (i=0; i<size; i++) {
		if (i == rank) continue;
		SetupRdata( rbuf, msgsize );
		MPI_Recv( rbuf, msgsize, MPI_INT, i, 2*i, comm, s );
		err += CheckData( rbuf, msgsize, 2*i, s );
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
	    MPI_Ssend( sbuf, msgsize, MPI_INT, root, 2*rank, comm );
	    free( sbuf );
	}
	msgsize *= 4;
    }
    if (rank == 0 && verbose) { printf( "\n" ); fflush( stdout ); }

    /* Next, try expected messages with Rsend */
    msgsize = 128;
    maxmsg  = max_msg_size;
    if (rank == root && verbose) printf( "Expected recvs and Rsend: " );
    while (msgsize < maxmsg) {
	if (rank == root) {
	    if (verbose) { printf( "%d ", msgsize ); fflush( stdout ); }
	    rbuf = (int *)malloc( msgsize * sizeof(int) );
	    if (!rbuf) {
		printf( "Could not allocate %d words\n", msgsize );
		MPI_Abort( comm, 1 );
	    }
	    for (i=0; i<size; i++) {
		if (i == rank) continue;
		SetupRdata( rbuf, msgsize );
		MPI_Irecv( rbuf, msgsize, MPI_INT, i, 2*i, comm, r );
		MPI_Send( MPI_BOTTOM, 0, MPI_INT, i, 2*i+1, comm );
		MPI_Wait( r, s );
		err += CheckData( rbuf, msgsize, 2*i, s );
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
	    MPI_Recv( MPI_BOTTOM, 0, MPI_INT, root, 2*rank+1, comm, s );
	    MPI_Rsend( sbuf, msgsize, MPI_INT, root, 2*rank, comm );
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
	    printf( "rbuf[%d] is 0x%x, should be 0x%x\n", i, rbuf[i], i );
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
