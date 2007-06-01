/*
 * This file tests that message truncation errors are properly detected and
 * handled (in particular, that data is NOT overwritten).
 */

#include <stdio.h>
#include "mpid.h"

/* Define this global symbol */
MPI_Comm MPI_COMM_WORLD;

void fence( comm, partner )
MPI_Comm comm;
int      partner;
{
    MPIR_RHANDLE rhandle;
    MPI_Request  request = (MPI_Request)&rhandle;
    MPI_Status   status;
    int          err, msgrep = 0;

    MPID_IrecvContig( comm, MPI_BOTTOM, 0, partner, 0, 1, request, &err );
    MPID_SendContig( comm, MPI_BOTTOM, 0, MPID_MyWorldRank, 0, 1, partner, 
		     msgrep, &err );
    MPID_RecvComplete( request, &status, &err );
}

int main( argc, argv )
int  argc;
char **argv;
{
    int         err = 0;
    int         rank, size;
    int         partner, merr, flag, msgrep = 0;
    MPI_Status  status;
    MPIR_RHANDLE rhandle;
    MPI_Request  request = (MPI_Request)&rhandle;
    MPI_Comm   comm = (MPI_Comm)0;
    int         i, sendbuf[10], recvbuf[10];

    MPID_Init( &argc, &argv, (void *)0, &err );

    rank = MPID_MyWorldRank;
    size = MPID_MyWorldSize;

/* We'll RECEIVE into rank 0, just to simplify any debugging.  The tests are
   sender                                     receiver
   send( count = 10 )                         recv(count = 1)
   isend( count = 10 )
   sendrecv                                   sendrecv
   wait                                       recv(count=1) (unexpected recv)
                                              irecv( count = 1)
   sendrecv                                   sendrecv               
   send( count = 10)                          wait (expected/error in status)
                                              irecv( count = 1)
   sendrecv                                   sendrecv
   send( count = 10)                          test (expected/error in status)
 */
   
    if (rank == 0) {
	/* Only return on the RECEIVERS side */
	partner = size - 1;
	SetupRecvBuf( recvbuf );
	merr = 0;
	MPID_RecvContig( comm, recvbuf, sizeof(int), partner, 1, 0, &status, 
			   &merr );
	err += CheckRecvErr( merr, &status, recvbuf, "Recv" );
	fence( comm, partner );

	SetupRecvBuf( recvbuf );
	merr = 0;
	MPID_RecvContig( comm, recvbuf, sizeof(int), partner, 2, 0, &status,
			   &merr );
	err += CheckRecvErr( merr, &status, recvbuf, "Unexpected Recv" );

	SetupRecvBuf( recvbuf );
	merr = 0;
	MPID_IrecvContig( comm, recvbuf, sizeof(int), partner, 3, 0, request, 
			    &merr );
    
	fence( comm, partner );

	merr = 0;
	MPID_RecvComplete( request, &status, &merr );
	err += CheckRecvErr( merr, &status, recvbuf, "Irecv/Wait" );

	SetupRecvBuf( recvbuf );
	merr = 0;
	MPID_IrecvContig( comm, recvbuf, sizeof(int), partner, 4, 0, request,
			    &merr );
	fence( comm, partner );

	merr = 0;
	do { 
	    flag = MPID_RecvIcomplete( request, &status, &merr );
	} while (merr == 0 && flag == 0);
	err += CheckRecvErr( merr, &status, recvbuf, "Irecv/Test" );
    }
    else if (rank == size - 1) {
	partner = 0;
	for (i=0; i<10; i++) 
	    sendbuf[i] = 100 + i;
	MPID_SendContig( comm, sendbuf, 10*sizeof(int), rank, 1, 0, partner, 
			 msgrep, &merr );
	MPID_IsendContig( comm, sendbuf, 10*sizeof(int), rank, 2, 0, 
			  partner, msgrep, request, &merr );
	fence( comm, partner );

	MPID_SendComplete( request, &merr );

	fence( comm, partner );

	MPID_SendContig( comm, sendbuf, 10*sizeof(int), rank, 3, 0, 
			 partner, msgrep, &merr );
	fence( comm, partner );

	MPID_SendContig( comm, sendbuf, 10*sizeof(int), rank, 4, 0, partner, 
			 msgrep, &merr );
    }

    if (rank == 0) {
	if (err == 0) 
	    printf( "No errors in Truncated Message test\n" );
	else
	    printf( "Found %d errors in Truncated Message test\n", err );
    }

    MPID_End();
    return 0;
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
	if (status->MPI_ERROR == MPI_ERR_TRUNCATE)
	    break;
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
