/*
 * This file tests that message truncation errors are properly detected and
 * handled (in particular, that data is NOT overwritten).
 */

#include "mpi.h"
#include <stdio.h>
#include <stdlib.h>
#include "test.h"
/* Prototypes for picky compilers */
int SetupRecvBuf ( int * );
int CheckRecvErr ( int, MPI_Status *, int *, char * );

int main( int argc, char **argv )
{
    int         err = 0, toterr;
    int         world_rank;
    MPI_Comm    comm, dupcomm;
    int         rank, size;
    int         partner, merr, flag;
    MPI_Status  status;
    MPI_Request request;
    int         i, sendbuf[10], recvbuf[10];

    MPI_Init( &argc, &argv );
    MPI_Comm_rank( MPI_COMM_WORLD, &world_rank );

    comm = MPI_COMM_WORLD;
    MPI_Comm_dup( comm, &dupcomm );
    MPI_Comm_rank( comm, &rank );
    MPI_Comm_size( comm, &size );

/* We'll RECEIVE into rank 0, just to simplify any debugging.  The tests are
   sender                                     receiver
   send( count = 10 )                         recv(count = 1)
   isend( count = 10 )
   sendrecv                                   sendrecv
   wait                                       recv(count=1) (unexpected recv)
                                              irecv( count = 1)
   sendrecv                                   sendrecv               
   send( count = 10)                          wait (expected/err trunc)
                                              irecv( count = 1)
   sendrecv                                   sendrecv
   send( count = 10)                          test (expected/err trunc)
 */
   
    if (size < 2) {
	fprintf( stderr, "This test requires at least 2 processes\n" );
	MPI_Abort( MPI_COMM_WORLD, 1 );
    }
    if (rank == 0) {
	/* Only return on the RECEIVERS side */
	MPI_Errhandler_set( comm, MPI_ERRORS_RETURN );
	partner = size - 1;
	SetupRecvBuf( recvbuf );
	merr = MPI_Recv( recvbuf, 1, MPI_INT, partner, 1, comm, &status );
	err += CheckRecvErr( merr, &status, recvbuf, "Recv" );
	MPI_Sendrecv( MPI_BOTTOM, 0, MPI_INT, partner, 0,
		      MPI_BOTTOM, 0, MPI_INT, partner, 0,
		      dupcomm, &status );

	SetupRecvBuf( recvbuf );
	merr = MPI_Recv( recvbuf, 1, MPI_INT, partner, 2, comm, &status );
	err += CheckRecvErr( merr, &status, recvbuf, "Unexpected Recv" );

	SetupRecvBuf( recvbuf );
	merr = MPI_Irecv( recvbuf, 1, MPI_INT, partner, 3, comm, &request );
    
	MPI_Sendrecv( MPI_BOTTOM, 0, MPI_INT, partner, 0,
		      MPI_BOTTOM, 0, MPI_INT, partner, 0,
		      dupcomm, &status );
	merr = MPI_Wait( &request, &status );
	err += CheckRecvErr( merr, &status, recvbuf, "Irecv/Wait" );

	SetupRecvBuf( recvbuf );
	merr = MPI_Irecv( recvbuf, 1, MPI_INT, partner, 4, comm, &request );
	MPI_Sendrecv( MPI_BOTTOM, 0, MPI_INT, partner, 0,
		      MPI_BOTTOM, 0, MPI_INT, partner, 0,
		      dupcomm, &status );
	do { 
	    merr = MPI_Test( &request, &flag, &status );
	} while (merr == 0 && flag == 0);
	err += CheckRecvErr( merr, &status, recvbuf, "Irecv/Test" );
    }
    else if (rank == size - 1) {
	partner = 0;
	for (i=0; i<10; i++) 
	    sendbuf[i] = 100 + i;
	MPI_Send( sendbuf, 10, MPI_INT, partner, 1, comm );
	MPI_Isend( sendbuf, 10, MPI_INT, partner, 2, comm, &request );
	MPI_Sendrecv( MPI_BOTTOM, 0, MPI_INT, partner, 0,
		      MPI_BOTTOM, 0, MPI_INT, partner, 0,
		      dupcomm, &status );
	MPI_Wait( &request, &status );

	MPI_Sendrecv( MPI_BOTTOM, 0, MPI_INT, partner, 0,
		      MPI_BOTTOM, 0, MPI_INT, partner, 0,
		      dupcomm, &status );
	MPI_Send( sendbuf, 10, MPI_INT, partner, 3, comm );
	MPI_Sendrecv( MPI_BOTTOM, 0, MPI_INT, partner, 0,
		      MPI_BOTTOM, 0, MPI_INT, partner, 0,
		      dupcomm, &status );
	MPI_Send( sendbuf, 10, MPI_INT, partner, 4, comm );
    }
    MPI_Comm_free( &dupcomm );

    MPI_Allreduce( &err, &toterr, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD );
    if (world_rank == 0) {
	if (toterr == 0) 
	    printf( " No Errors\n" );
	else
	    printf( "Found %d errors in Truncated Message test\n", toterr );
    }
    MPI_Finalize( );
    return toterr;
}

int SetupRecvBuf( recvbuf )
int *recvbuf;
{
    int i;
    for (i=0; i<10; i++) 
	recvbuf[i] = i+1;
    return 0;
}

int CheckRecvErr( merr, status, recvbuf, msg )
int        merr, *recvbuf;
MPI_Status *status;
char       *msg;
{
    int  class;
    int  err = 0, rlen;
    char buf[MPI_MAX_ERROR_STRING];

/* Get the MPI Error class from merr */
    MPI_Error_class( merr, &class );
    switch (class) {
    case MPI_ERR_TRUNCATE:
	/* Check that data buf is ok */
	if (recvbuf[1] != 2) {
	    err++;
	    fprintf( stderr, 
		     "Receive buffer overwritten!  Found %d in 2nd pos.\n",
		     recvbuf[1] );
	}
	break;

    case MPI_ERR_IN_STATUS:
	/* Check for correct message */
	/* ERR IN STATUS is correct ONLY for multiple completion routines */
/*	if (status->MPI_ERROR == MPI_ERR_TRUNCATE) 
	    break; */
	/* Else, fall through into default... */
    default:
	/* Wrong error; get message and print */
	MPI_Error_string( merr, buf, &rlen );
	fprintf( stderr, 
		 "Got unexpected error message from %s: %s\n", msg, buf );
	err++;
    }
    return err;
}
