#include <stdio.h>
#include <stdlib.h>
/* #include <memory.h> */
#include "mpid.h"

#include "aditest.h"

/* Define this global symbol */
MPI_Comm MPI_COMM_WORLD;

/* 
 * Simple ADI test.  This uses Isend to send messages "out of order"
 * We use tests to complete the sends
 *
 * Still need to do - check error returns.
 */
#define MAX_SENDS 4
int main(argc,argv)
int argc;
char **argv;
{
    char       *sbuf, *rbuf;
    int        ntest, i, j, len = 256, err, ndone, msgrep = 0;
    int        master = 1, slave = 0;
    MPI_Comm   comm = (MPI_Comm)0;
    int        nmsgs = MAX_SENDS;
    MPI_Status status;
    MPIR_SHANDLE shandle[MAX_SENDS];
    MPI_Request  req[MAX_SENDS];

    ntest = 100;

    MPID_Init( &argc, &argv, (void *)0, &err );

    SetupTests( argc, argv, &len, &master, &slave, &sbuf, &rbuf );

    if (MPID_MyWorldSize != 2) {
	fprintf( stderr, "%d\n", MPID_MyWorldSize );
	MPID_Abort( comm, 1, (char *)0, "Wrong number of processes" );
    }

    for (i=0; i<ntest; i++) {
	if (MPID_MyWorldRank == master) {
	    for (j=0; j<nmsgs; j++) {
		req[j] = (MPI_Request)&shandle[j];
		MPID_Request_init( &shandle[j], MPIR_SEND );
		MPID_IsendContig( comm, sbuf, len, master, j, 0, slave, 
				  msgrep, req[j], &err );
	    }
	    ndone = 0;
	    while (ndone != nmsgs) {
		for (j=0; j<nmsgs; j++) {
		    if (!req[j]) continue;
		    if (MPID_SendIcomplete( req[j], &err )) {
			ndone++;
			req[j] = 0;
		    }
		}
	    }
	    MPID_RecvContig( comm, rbuf, len, slave, 0, 0, &status, &err );
	    (void) CheckStatus( &status, slave, 0, len );
	    (void) CheckData( sbuf, rbuf, len );
	}
	else {
	    for (j=nmsgs-1; j>=0; j--) {
		MPID_RecvContig( comm, rbuf, len, master, j, 0, &status, 
				 &err );
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

