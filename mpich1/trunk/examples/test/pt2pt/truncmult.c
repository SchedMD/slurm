/*
 * This file tests that message truncation errors are properly detected and
 * handled (in particular, that data is NOT overwritten).
 * 
 * This version checks the multiple completion routines
 */

#include "mpi.h"
#include <stdio.h>
#include <stdlib.h>
#include "test.h"
/* Prototypes for picky compilers */
int SetupRecvBuf ( int * );
int CheckRecvErr ( int, MPI_Status *, int *, char * );
int CheckRecvOk  ( MPI_Status *, int *, int, char * );

int main( int argc, char **argv )
{
    int         err = 0, toterr;
    int         world_rank;
    MPI_Comm    comm, dupcomm;
    int         rank, size;
    int         partner, merr;
    MPI_Status  statuses[4], status;
    MPI_Request requests[4];
    int         i, sendbuf[10],
	        recvbuf1[10], recvbuf2[10], recvbuf3[10], recvbuf4[10];

    MPI_Init( &argc, &argv );
    MPI_Comm_rank( MPI_COMM_WORLD, &world_rank );

    comm = MPI_COMM_WORLD;
    MPI_Comm_dup( comm, &dupcomm );
    MPI_Comm_rank( comm, &rank );
    MPI_Comm_size( comm, &size );

/* We'll RECEIVE into rank 0, just to simplify any debugging.  Just in 
   case the MPI implementation tests for errors when the irecv is issued,
   we make sure that the matching sends don't occur until the receives
   are posted.

   sender                                     receiver
                                              irecv(tag=1,count=1)
					      irecv(tag=2,count=1)
   sendrecv                                   sendrecv
   send(tag=1,count=1)                        
   send(tag=2,count=10)
                                              waitall()
                                                error in status, err trunc
                                                wait for tag = 1 if necessary
   sendrecv                                   sendrecv
   Ditto, but with 2 truncated messages
   Ditto, but with testall. (not done yet)
   All of the above, but with waitsome/testsome (not done yet)
 */
   
    if (rank == 0) {
	/* Only return on the RECEIVERS side */
	MPI_Errhandler_set( comm, MPI_ERRORS_RETURN );
	partner = size - 1;

	SetupRecvBuf( recvbuf1 );
	SetupRecvBuf( recvbuf2 );
	merr = MPI_Irecv( recvbuf1, 1, MPI_INT, partner, 1, comm, 
			  &requests[0] );  /* this will succeed */
	merr = MPI_Irecv( recvbuf2, 1, MPI_INT, partner, 2, comm, 
			  &requests[1] );  /* this will fail */
	MPI_Sendrecv( MPI_BOTTOM, 0, MPI_INT, partner, 0,
		      MPI_BOTTOM, 0, MPI_INT, partner, 0,
		      dupcomm, &status );
	merr = MPI_Waitall( 2, requests, statuses );
	if (merr != MPI_ERR_IN_STATUS) {
	    err++;
	    fprintf( stderr, "Did not return MPI_ERR_IN_STATUS\n" );
	    MPI_Abort( MPI_COMM_WORLD, 1 );
	}
	if (statuses[0].MPI_ERROR == MPI_ERR_PENDING) {
	    /* information - first send is not yet complete */
	    if ((statuses[0].MPI_ERROR = MPI_Wait( &requests[0], &statuses[0] )) == MPI_SUCCESS) {
		err++;
		fprintf( stderr, "failed to complete legal request (1)\n" );
	    }
	}
	if (statuses[0].MPI_ERROR != MPI_SUCCESS) {
	    err ++;
	    fprintf( stderr, "Could not complete legal send-receive\n" );
	    MPI_Abort( MPI_COMM_WORLD, 1 );
	}
	err += CheckRecvErr( merr, &statuses[1], recvbuf2, "Irecv" );

	SetupRecvBuf( recvbuf1 );
	SetupRecvBuf( recvbuf2 );
	SetupRecvBuf( recvbuf3 );
	SetupRecvBuf( recvbuf4 );
	merr = MPI_Irecv( recvbuf1, 1, MPI_INT, partner, 1, comm, 
			  &requests[0] );  /* this will succeed */
	merr = MPI_Irecv( recvbuf2, 1, MPI_INT, partner, 2, comm, 
			  &requests[1] );  /* this will fail */
	merr = MPI_Irecv( recvbuf3, 1, MPI_INT, partner, 3, comm, 
			  &requests[2] );  /* this will fail */
	merr = MPI_Irecv( recvbuf4, 1, MPI_INT, partner, 4, comm, 
			  &requests[3] );  /* this will succeed */
	MPI_Sendrecv( MPI_BOTTOM, 0, MPI_INT, partner, 0,
		      MPI_BOTTOM, 0, MPI_INT, partner, 0,
		      dupcomm, &status );
	merr = MPI_Waitall( 4, requests, statuses );
	if (merr != MPI_ERR_IN_STATUS) {
	    err++;
	    fprintf( stderr, "Did not return MPI_ERR_IN_STATUS (4)\n" );
	    MPI_Abort( MPI_COMM_WORLD, 1 );
	}
	if (statuses[0].MPI_ERROR == MPI_ERR_PENDING) {
	    /* information - first send is not yet complete */
	    if ((statuses[0].MPI_ERROR = MPI_Wait( &requests[0], &statuses[0] )) != MPI_SUCCESS) {
		err++;
		fprintf( stderr, "failed to complete legal request (1a)\n" );
	    }
	}
	/* Check for correct completion */
	err += CheckRecvOk( &statuses[0], recvbuf1, 1, "4-1" );

	if (statuses[3].MPI_ERROR == MPI_ERR_PENDING) {
	    /* information - first send is not yet complete */
	    if ((statuses[3].MPI_ERROR = MPI_Wait( &requests[3], &statuses[3] )) != MPI_SUCCESS) {
		err++;
		fprintf( stderr, "failed to complete legal request (3a)\n" );
	    }
	}
	/* Check for correct completion */
	err += CheckRecvOk( &statuses[3], recvbuf4, 4, "4-4" );

	if (statuses[0].MPI_ERROR != MPI_SUCCESS) {
	    err ++;
	    fprintf( stderr, "Could not complete legal send-receive-0\n" );
	    MPI_Abort( MPI_COMM_WORLD, 1 );
	}
	if (statuses[3].MPI_ERROR != MPI_SUCCESS) {
	    err ++;
	    fprintf( stderr, "Could not complete legal send-receive-3\n" );
	    MPI_Abort( MPI_COMM_WORLD, 1 );
	}
	
	if (statuses[1].MPI_ERROR == MPI_ERR_PENDING) {
	    statuses[1].MPI_ERROR = MPI_Wait( &requests[1], &statuses[1] );
	}
	err += CheckRecvErr( merr, &statuses[1], recvbuf2, "Irecv-2" );
	if (statuses[2].MPI_ERROR == MPI_ERR_PENDING) {
	    statuses[2].MPI_ERROR = MPI_Wait( &requests[2], &statuses[2] );
	}
	err += CheckRecvErr( merr, &statuses[2], recvbuf3, "Irecv-3" );
	MPI_Sendrecv( MPI_BOTTOM, 0, MPI_INT, partner, 0,
		      MPI_BOTTOM, 0, MPI_INT, partner, 0,
		      dupcomm, &status );
    }
    else if (rank == size - 1) {
	partner = 0;
	for (i=0; i<10; i++) 
	    sendbuf[i] = 100 + i;
	MPI_Sendrecv( MPI_BOTTOM, 0, MPI_INT, partner, 0,
		      MPI_BOTTOM, 0, MPI_INT, partner, 0,
		      dupcomm, &status );
	MPI_Send( sendbuf, 1, MPI_INT, partner, 1, comm );
	MPI_Send( sendbuf, 10, MPI_INT, partner, 2, comm );

	MPI_Sendrecv( MPI_BOTTOM, 0, MPI_INT, partner, 0,
		      MPI_BOTTOM, 0, MPI_INT, partner, 0,
		      dupcomm, &status );
	MPI_Send( sendbuf, 1, MPI_INT, partner, 1, comm );
	MPI_Send( sendbuf, 10, MPI_INT, partner, 2, comm );
	MPI_Send( sendbuf, 10, MPI_INT, partner, 3, comm );
	MPI_Send( sendbuf, 1, MPI_INT, partner, 4, comm );
	MPI_Sendrecv( MPI_BOTTOM, 0, MPI_INT, partner, 0,
		      MPI_BOTTOM, 0, MPI_INT, partner, 0,
		      dupcomm, &status );
    }
    MPI_Comm_free( &dupcomm );

    MPI_Allreduce( &err, &toterr, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD );
    if (world_rank == 0) {
	if (toterr == 0) 
	    printf( " No Errors\n" );
	else
	    printf( "Found %d errors in Truncated Message Multiple Completion test\n", toterr );
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

int CheckRecvOk( status, recvbuf, tag, msg )
int        *recvbuf, tag;
MPI_Status *status;
char       *msg;
{
    int err = 0, count;

    if (status->MPI_TAG != tag) {
	err++;
	fprintf( stderr, "Wrong tag; was %d should be %d (%s)\n", 
		 status->MPI_TAG, tag, msg );
    }
    MPI_Get_count( status, MPI_INT, &count );
    if (count != 1) {
	err++;
	fprintf( stderr, "Wrong count; was %d expected 1 (%s)\n", count, msg );
    }
    return err;
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
        MPI_Error_class(status->MPI_ERROR, &class);
	if (class != MPI_ERR_TRUNCATE) {
	    MPI_Error_string( status->MPI_ERROR, buf, &rlen );
	    fprintf( stderr, 
		 "Unexpected error message for err in status for %s: %s\n", 
		 msg, buf );
	}
	break;
    default:
	/* Wrong error; get message and print */
	MPI_Error_string( merr, buf, &rlen );
	fprintf( stderr, 
		 "Got unexpected error message from %s: %s\n", msg, buf );
	err++;
    }
    return err;
}
