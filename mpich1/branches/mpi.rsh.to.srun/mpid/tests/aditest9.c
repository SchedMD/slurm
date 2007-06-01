#include <stdio.h>
#include <stdlib.h>
/* #include <memory.h> */
#include "mpid.h"
#include "mpimem.h"
#include "aditest.h"

/* Define this global symbol */
MPI_Comm MPI_COMM_WORLD;

/* 
 * Simple ADI test.  This uses the datatype routines and forces XDR
 * Only for systems that support XDR (this is expected to run
 * on a homogeneous system).  We pick "short" because it is 
 * represented as something other than short in XDR
 *
 * Still need to do - check error returns.
 */

int main(argc,argv)
int argc;
char **argv;
{
    short      *sbuf, *rbuf;
    int        ntest, i, len = 256, err;
    int        count;
    int        master = 1, slave = 0;
    MPI_Comm   comm;
    struct MPIR_COMMUNICATOR wcomm;
    MPI_Status status;

    ntest = 1;

    /* Must be called with -mpixdr option to get XDR test */
    MPID_Init( &argc, &argv, (void *)0, &err );

    /* Setup the datatypes.  Requires linking with the MPI library */
    MPIR_Init_dtes();

    /* Setup a communicator (needed for MPID_xxxDatatype routines) */
    comm = &wcomm;
    comm->np = MPID_MyWorldSize;
    comm->lrank_to_grank = (int *)MALLOC( MPID_MyWorldSize * sizeof(int) );
    for (i=0; i<MPID_MyWorldSize; i++)
	comm->lrank_to_grank[i] = i;
    /* MPID_Comm_init( (MPI_Comm)0, comm ); */
    MPID_CH_Comm_msgrep( comm ); 

    SetupTestsS( argc, argv, &len, &master, &slave, &sbuf, &rbuf );

    count = len / sizeof(short);

    if (MPID_MyWorldSize != 2) {
	fprintf( stderr, "%d\n", MPID_MyWorldSize );
	MPID_Abort( comm, 1, (char *)0, "Wrong number of processes" );
    }

    for (i=0; i<ntest; i++) {
	if (MPID_MyWorldRank == master) {
	    MPID_SendDatatype( comm, sbuf, count, MPI_SHORT, 
			       master, 0, 0, slave, &err );
	    MPID_RecvDatatype( comm, rbuf, count, MPI_SHORT, 
			       slave, 0, 0, &status, &err );
	    (void) CheckStatus( &status, slave, 0, count*sizeof(short) );
	    (void) CheckDataS( sbuf, rbuf, count, "master" );
	}
	else {
	    MPID_RecvDatatype( comm, rbuf, count, MPI_SHORT, 
			       master, 0, 0, &status, &err );
	    (void) CheckStatus( &status, master, 0, count*sizeof(short) );
	    (void) CheckDataS( sbuf, rbuf, count, "slave" );
	    MPID_SendDatatype( comm, sbuf, count, MPI_SHORT, 
			       slave, 0, 0, master, &err );
	}
    }

    EndTests( sbuf, rbuf );
    MPIR_Free_dtes();
    FREE(comm->lrank_to_grank);
    MPID_End();
    return 0;
}

