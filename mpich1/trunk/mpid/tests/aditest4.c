#include <stdio.h>
#include <stdlib.h>
/* #include <memory.h> */
#include "mpid.h"

#include "aditest.h"

/* Define this global symbol */
MPI_Comm MPI_COMM_WORLD;

/* 
 * Simple ADI test.  This uses Irecv to receive messages "out of order"
 *
 * Still need to do - check error returns.
 */
#define MAX_RECVS 4
int main(argc,argv)
int argc;
char **argv;
{
    char       *sbuf, *rbuf;
    int        ntest, i, j, len = 256, err, msgrep = 0;
    int        master = 1, slave = 0;
    MPI_Comm   comm = (MPI_Comm)0;
    int        nmsgs = MAX_RECVS;
    MPI_Status status;
    MPIR_RHANDLE rhandle[MAX_RECVS];
    MPI_Request  req[MAX_RECVS];

    ntest = 100;

    MPID_Init( &argc, &argv, (void *)0, &err );

    SetupTests( argc, argv, &len, &master, &slave, &sbuf, &rbuf );

    if (MPID_MyWorldSize != 2) {
	fprintf( stderr, "%d\n", MPID_MyWorldSize );
	MPID_Abort( comm, 1, (char *)0, "Wrong number of processes" );
    }

    for (i=0; i<MAX_RECVS; i++) {
	req[i] = (MPI_Request)&rhandle[i];
	MPID_Request_init( &rhandle[i], MPIR_RECV );
    }

    for (i=0; i<ntest; i++) {
	if (MPID_MyWorldRank == master) {
	    for (j=0; j<nmsgs; j++) {
		MPID_SendContig( comm, sbuf, len, master, j, 0, slave, msgrep, 
				 &err );
	    }
	    MPID_RecvContig( comm, rbuf, len, slave, 0, 0, &status, &err );
	    (void) CheckStatus( &status, slave, 0, len );
	    (void) CheckData( sbuf, rbuf, len );
	}
	else {
	    for (j=0; j<nmsgs; j++) {
		MPID_IrecvContig( comm, rbuf, len, master, nmsgs-j-1, 0, 
				  req[nmsgs-j-1], &err );
	    }
	    /* By waiting on the second FIRST, we ensure that we get these
	       out-of-order */
	    for (j=nmsgs-1; j>=0; j--) {
		MPID_RecvComplete( req[j], &status, &err );
		(void) CheckStatus( &status, master, j, len );
		(void) CheckData( sbuf, rbuf, len );
	    }
	    MPID_SsendContig( comm, sbuf, len, slave, 0, 0, master,
			      msgrep, &err );
	}
    }

    EndTests( sbuf, rbuf );
    MPID_End();
    return 0;
}

