#include <stdio.h>
#include <stdlib.h>
/* #include <memory.h> */
#include "mpid.h"
#include "mpimem.h"
#include "aditest.h"

/* Define this global symbol */
MPI_Comm MPI_COMM_WORLD;

/* 
 * Simple ADI test.  This uses the datatype routines.
 * This creates a type that skips every other value.
 * Tests both RecvComplete and RecvIcomplete .
 *
 * Still need to do - check error returns.
 */

int CheckDataStruct( sbuf, rbuf, len, msg )
short *sbuf, *rbuf;
int   len;
char  *msg;
{
    int i;
    int errcnt = 0;
    for (i=0; i<len; i++) {
	if (sbuf[2*i] != rbuf[2*i]) {
	    fprintf( stderr, "[%d] Expected %d but saw %d at rbuf[%d] %s\n",
		     MPID_MyWorldRank, (int)sbuf[2*i], (int)rbuf[2*i], 
		     2*i, msg );
	    if (errcnt++ > 10) break;
	}
	if (sbuf[2*i] != i) {
	    fprintf( stderr, "[%d] sbuf[%d] is %d, should be %d\n", 
		     MPID_MyWorldRank, 2*i, (int)sbuf[2*i], i );
	    if (errcnt++ > 10) break;
	}
	if (rbuf[2*i+1] != -i) {
	    fprintf( stderr, "[%d] rbuf[%d] is %d, should be %d\n",
		     MPID_MyWorldRank, 2*i+1, rbuf[2*i+1], -i );
	    if (errcnt++ > 10) break;
	}
    }
    return errcnt;
}

int main(argc,argv)
int argc;
char **argv;
{
    short      *sbuf, *rbuf;
    int        ntest, i, len = 256, err, flag;
    int        count;
    int        master = 1, slave = 0;
    MPI_Comm   comm;
    struct MPIR_COMMUNICATOR wcomm;
    MPI_Status status;
    MPI_Datatype dtype;
    static int blens[2] = { 1, 1 };
    static MPI_Datatype types[2] = { MPI_SHORT, MPI_UB };
    static MPI_Aint displs[2] = { 0, 2*sizeof(short) };
    MPIR_RHANDLE rhandle;
    MPI_Request  req;

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

    SetupArgs( argc, argv, &len, &master, &slave );
    count = len / sizeof(short);

    sbuf = (short *)MALLOC( 2 * sizeof(short) * count );
    rbuf = (short *)MALLOC( 2 * sizeof(short) * count );
    if (!sbuf || !rbuf) {
	fprintf( stderr, "Could not allocate buffers!\n" );
	return 1;
    }
    for (i=0; i<count; i++) {
	sbuf[i*2]   = i;
	sbuf[i*2+1] = 0;
	rbuf[i*2+1] = -i;
    }

    if (MPID_MyWorldSize != 2) {
	fprintf( stderr, "%d\n", MPID_MyWorldSize );
	MPID_Abort( comm, 1, (char *)0, "Wrong number of processes" );
    }

    MPI_Type_struct( 2, blens, displs, types, &dtype );
    MPI_Type_commit( &dtype );
    for (i=0; i<ntest; i++) {
	if (MPID_MyWorldRank == master) {
	    MPID_SendDatatype( comm, sbuf, count, dtype, 
			       master, 0, 0, slave, &err );
	    req = (MPI_Request)&rhandle;
	    MPID_Request_init( &rhandle, MPIR_RECV );
	    MPID_IrecvDatatype( comm, rbuf, count, dtype, 
			       slave, 0, 0, req, &err );
	    do {
		flag = MPID_RecvIcomplete( req, &status, &err );
	    } while (flag == 0);
	    (void) CheckStatus( &status, slave, 0, count*sizeof(short) );
	    (void) CheckDataStruct( sbuf, rbuf, count, "master" );
	}
	else {
	    req = (MPI_Request)&rhandle;
	    MPID_Request_init( &rhandle, MPIR_RECV );
	    MPID_IrecvDatatype( comm, rbuf, count, dtype, 
			       master, 0, 0, req, &err );
	    MPID_RecvComplete( req, &status, &err );
	    (void) CheckStatus( &status, master, 0, count*sizeof(short) );
	    (void) CheckDataStruct( sbuf, rbuf, count, "slave" );
	    MPID_SsendDatatype( comm, sbuf, count, dtype, 
			       slave, 0, 0, master, &err );
	}
    }

    MPI_Type_free( &dtype );
    FREE( sbuf );
    FREE( rbuf );

    MPIR_Free_dtes();
    FREE(comm->lrank_to_grank);

    MPID_End();
    return 0;
}

