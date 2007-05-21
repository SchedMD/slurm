#include <stdio.h>
#include <stdlib.h>
/* #include <memory.h> */
#include "mpid.h"

#include "aditest.h"

/* Define this global symbol */
MPI_Comm MPI_COMM_WORLD;

/* 
 * Simple ADI test.  This uses Irecv to receive messages "out of order"
 * in the special send-to-self case.
 *
 */
#define MAX_RECVS 4
#define MAX_SENDS 4
int main(argc,argv)
int argc;
char **argv;
{
    char       *sbuf, *rbuf;
    int        ntest, i, j, len = 256, err, msgrep = 0;
    int        master = 0, slave = 0;
    MPI_Comm   comm = (MPI_Comm)0;
    int        nmsgs = MAX_RECVS;
    MPI_Status status;
    MPIR_RHANDLE rhandle[MAX_RECVS];
    MPIR_SHANDLE shandle[MAX_SENDS];
    MPI_Request  req[MAX_RECVS];

    ntest = 100;

    MPID_Init( &argc, &argv, (void *)0, &err );

    SetupTests( argc, argv, &len, &master, &slave, &sbuf, &rbuf );
    if (master != 0 || slave != 0) {
	fprintf( stderr, "Send to self test requires 1 process only\n" );
	exit(1);
    }

    if (MPID_MyWorldSize != 1) {
	fprintf( stderr, "%d\n", MPID_MyWorldSize );
	MPID_Abort( comm, 1, (char *)0, "Wrong number of processes" );
    }

    for (i=0; i<MAX_RECVS; i++) {
	req[i] = (MPI_Request)&rhandle[i];
	MPID_Request_init( &rhandle[i], MPIR_RECV );
    }

    for (j=0; j<nmsgs; j++) {
	MPID_IrecvContig( comm, rbuf, len, master, nmsgs-j-1, 0, 
			  req[nmsgs-j-1], &err );
    }
    for (j=0; j<nmsgs; j++) {
	MPID_SsendContig( comm, sbuf, len, master, j, 0, slave, msgrep, 
			  &err );
    }
    /* By waiting on the second FIRST, we ensure that we get these
       out-of-order */
    for (j=nmsgs-1; j>=0; j--) {
	MPID_RecvComplete( req[j], &status, &err );
	(void) CheckStatus( &status, master, j, len );
	(void) CheckData( sbuf, rbuf, len );
    }

    /* Now, reverse the test and post send and complete with receive */
    for (i=0; i<MAX_SENDS; i++) {
	req[i] = (MPI_Request)&shandle[i];
	MPID_Request_init( &shandle[i], MPIR_SEND );
    }
    for (j=0; j<nmsgs; j++) {
	MPID_IssendContig( comm, sbuf, len, master, j+10, 0, slave, 
			   msgrep, req[j], &err );
    }
    /* We must wait on them in RECEIVER order */
    /* Is this a design bug in the ADI? */
    for (j=nmsgs-1; j>=0; j--) {
	MPID_RecvContig( comm, rbuf, len, master, j+10, 0, &status, 
			 &err );
	(void) CheckStatus( &status, master, j, len );
	(void) CheckData( sbuf, rbuf, len );
    }
    for (j=nmsgs-1; j>=0; j--) {
	MPID_SendComplete( req[j], &err );
    }


    EndTests( sbuf, rbuf );
    MPID_End();
    return 0;
}

