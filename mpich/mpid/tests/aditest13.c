#include <stdio.h>
#include <stdlib.h>
/* #include <memory.h> */
#include "mpid.h"

#include "aditest.h"

/* Define this global symbol */
MPI_Comm MPI_COMM_WORLD;

/* 
 * Simple ADI test for flow control.
 * The slave processes sends large numbers of relatively short
 * messages to the master
 */

int main(argc,argv)
int argc;
char **argv;
{
    char       *sbuf, *rbuf;
    int        ntest, i, j, len = 100000, err, msgrep = 0;
    int        master = 0, slave;
    MPI_Comm   comm = (MPI_Comm)0;
    MPI_Status status;


    MPID_Init( &argc, &argv, (void *)0, &err );

    SetupTests( argc, argv, &len, &master, &slave, &sbuf, &rbuf );

    if (MPID_MyWorldSize < 2) {
	fprintf( stderr, "%d\n", MPID_MyWorldSize );
	MPID_Abort( comm, 1, (char *)0, "Wrong number of processes" );
    }

    /* send 10MB of messages.  This is much larger than the memory
     * limit set below
     */
    ntest = 10000000/len;
    /* Use the memory tracing code to limit allocated memory to 2 MB */
    MPID_TrSetMaxMem( 2*1000000 );
    for (i=0; i<ntest; i++) {
	if (MPID_MyWorldRank == master) {
	    for (j=0; j<MPID_MyWorldSize; j++) {
		if (j == master) continue;
		MPID_RecvContig( comm, rbuf, len, j, 0, 0, &status, &err );
		if (err) {
		    printf( "Error (code %d) while receiving message\n", err );
		    MPID_Abort( comm, 1, (char *)0, (char *)0 );
		}
		(void) CheckStatus( &status, j, 0, len );
		(void) CheckData( sbuf, rbuf, len );
	    }
	    sleep(1);
	}
	else {
	    MPID_SendContig( comm, sbuf, len, MPID_MyWorldRank, 0, 0, 
			     master, msgrep, 
			     &err );
	}
    }

    EndTests( sbuf, rbuf );
    MPID_End();
    return 0;
}
