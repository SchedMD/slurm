#include <stdio.h>
#include <stdlib.h>
/* #include <memory.h> */
#include "mpid.h"

#include "aditest.h"

/* Define this global symbol */
struct MPIR_COMMUNICATOR *MPIR_COMM_WORLD;

/* 
 * Simple ADI test.  
 *
 * Still need to do - check error returns.
 */
int main(argc,argv)
int argc;
char **argv;
{
    char         *sbuf, *rbuf;
    int          ntest, i, len = 256, err, msgrep = 0;
    int          master = 1, slave = 0;
    struct MPIR_COMMUNICATOR *comm = 0;
    MPI_Status   status;
    MPIR_RHANDLE rhandle;
    MPI_Request  request = (MPI_Request)&rhandle;

    ntest = 100;
    err   = MPI_SUCCESS;

    MPID_Init( &argc, &argv, (void *)0, &err );

    SetupTests( argc, argv, &len, &master, &slave, &sbuf, &rbuf );

    if (MPID_MyWorldSize != 2) {
	fprintf( stderr, "%d\n", MPID_MyWorldSize );
	MPID_Abort( comm, 1, (char *)0, "Wrong number of processes" );
    }

    for (i=0; i<ntest; i++) {
	if (MPID_MyWorldRank == master) {
	    MPID_SendContig( comm, sbuf, len, master, 0, 0, slave, msgrep, 
			     &err );
	    MPID_IrecvContig( comm, rbuf, len, slave,  0, 0, request, &err );
	    MPID_RecvComplete( request, &status, &err );
	    (void)CheckStatus( &status, slave, 0, len );
	    (void)CheckData( sbuf, rbuf, len );
	}
	else {
	    MPID_RecvContig( comm, rbuf, len, master, 0, 0, &status, &err );
	    (void)CheckStatus( &status, master, 0, len );
	    (void)CheckData( sbuf, rbuf, len );
	    MPID_SendContig( comm, sbuf, len, slave, 0, 0, master, msgrep, 
			     &err );
	}
    }

    EndTests( sbuf, rbuf );
    MPID_End();
    return 0;
}

