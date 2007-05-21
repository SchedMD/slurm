#include <stdio.h>
#include <stdlib.h>
/* #include <memory.h> */
#include "mpid.h"

#include "aditest.h"

/* Define this global symbol */
MPI_Comm MPI_COMM_WORLD;

/* 
 * Simple ADI test.  This uses Irecv to receive messages "out of order"
 * Use test instead of wait to complete the receives.
 *
 * Still need to do - check error returns.
 */
#define MAX_RECVS 4
int main(argc,argv)
int argc;
char **argv;
{
    char       *sbuf, *rbuf;
    int        ntest, i, j, len = 256, err, msgrep = 0, flag, ndone;
    int        master = 1, slave = 0;
    MPI_Comm   comm = (MPI_Comm)0;
    int        nmsgs = MAX_RECVS;
    int        base_tag;
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

    base_tag = 0;
    for (i=0; i<ntest; i++) {
	if (MPID_MyWorldRank == master) {
	    for (j=0; j<nmsgs; j++) {
		MPID_SendContig( comm, sbuf, len, master, base_tag + j, 0, 
				 slave, msgrep, &err );
	    }
	    MPID_RecvContig( comm, rbuf, len, slave, 0, 0, &status, &err );
	    (void) CheckStatus( &status, slave, 0, len );
	    (void) CheckData( sbuf, rbuf, len );
	}
	else {
	    for (j=0; j<MAX_RECVS; j++) {
		req[j] = (MPI_Request)&rhandle[j];
		MPID_Request_init( &rhandle[j], MPIR_RECV );
	    }

	    for (j=0; j<nmsgs; j++) {
		MPID_IrecvContig( comm, rbuf, len, master, 
				  base_tag + nmsgs-j-1, 0, req[nmsgs-j-1], 
				  &err );
	    }
	    ndone = 0;
	    while (ndone != nmsgs) {
		for (j=0; j<nmsgs; j++) {
		    if (req[j] == 0) continue;
		    flag = MPID_RecvIcomplete( req[j], &status, &err);
		    if (flag) {
			ndone++;
			(void) CheckStatus( &status, master, j+base_tag, len );
			(void) CheckData( sbuf, rbuf, len );
			req[j] = 0;
		    }
		}
	    }
	    MPID_SsendContig( comm, sbuf, len, slave, 0, 0, master,
			      msgrep, &err );
	}
	base_tag += nmsgs;
    }

    EndTests( sbuf, rbuf );
    MPID_End();
    return 0;
}

