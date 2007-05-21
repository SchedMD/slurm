/*
 * This file shows a typical use of MPI_Cancel to free Persistent Recv's that
 * are not wanted.  We check for both successful and unsuccessful 
 * cancels
 */

/* On 10/27/99, a test for MPI_Waitsome/MPI_Testsome was added */

#include "mpi.h"
#include <stdio.h>

#if defined(NEEDS_STDLIB_PROTOTYPES)
#include "protofix.h"
#endif

int main( int argc, char **argv )
{
    MPI_Request r1;
    int         size, rank;
    int         err = 0;
    int         partner, buf[10], flag, idx, index;
    MPI_Status  status;

    MPI_Init( &argc, &argv );

    MPI_Comm_size( MPI_COMM_WORLD, &size );
    MPI_Comm_rank( MPI_COMM_WORLD, &rank );
    
    if (size < 2) {
	printf( "Cancel test requires at least 2 processes\n" );
	MPI_Abort( MPI_COMM_WORLD, 1 );
    }

    /* 
     * Here is the test.  First, we ensure an unsatisfied Irecv:
     *       process 0             process size-1
     *       Sendrecv              Sendrecv
     *       Irecv                    ----
     *       Cancel                   ----
     *       Sendrecv              Sendrecv
     * Next, we confirm receipt before canceling
     *       Irecv                 Send
     *       Sendrecv              Sendrecv
     *       Cancel
     */
    if (rank == 0) {
	partner = size - 1;
	/* Cancel succeeds for wait/waitall */
	MPI_Recv_init( buf, 10, MPI_INT, partner, 0, MPI_COMM_WORLD, &r1 );
	MPI_Sendrecv( MPI_BOTTOM, 0, MPI_INT, partner, 1,
		      MPI_BOTTOM, 0, MPI_INT, partner, 1,
		      MPI_COMM_WORLD, &status );
	MPI_Start( &r1 );
	MPI_Cancel( &r1 );
	MPI_Wait( &r1, &status );
	MPI_Test_cancelled( &status, &flag );
	MPI_Sendrecv( MPI_BOTTOM, 0, MPI_INT, partner, 1,
		      MPI_BOTTOM, 0, MPI_INT, partner, 1,
		      MPI_COMM_WORLD, &status );
	if (!flag) {
	    err++; 
	    printf( "Cancel of a receive failed where it should succeed (Wait).\n" );
	}

	MPI_Request_free( &r1 );

	/* Cancel fails for test/testall */
	buf[0] = -1;
	MPI_Recv_init( buf, 10, MPI_INT, partner, 2, MPI_COMM_WORLD, &r1 );
	MPI_Start( &r1 );
	MPI_Sendrecv( MPI_BOTTOM, 0, MPI_INT, partner, 1,
		      MPI_BOTTOM, 0, MPI_INT, partner, 1,
		      MPI_COMM_WORLD, &status );
	MPI_Cancel( &r1 );
	MPI_Test( &r1, &flag, &status );
	MPI_Test_cancelled( &status, &flag );
	if (flag) {
	    err++;
	    printf( "Cancel of a receive succeeded where it shouldn't (Test).\n" );
	    if (buf[0] != -1) {
		printf( "Receive buffer changed even though cancel suceeded! (Test).\n" );
	    }
	}
	MPI_Request_free( &r1 );

	/* Cancel succeeds for waitany */
	MPI_Recv_init( buf, 10, MPI_INT, partner, 0, MPI_COMM_WORLD, &r1 );
	MPI_Sendrecv( MPI_BOTTOM, 0, MPI_INT, partner, 1,
		      MPI_BOTTOM, 0, MPI_INT, partner, 1,
		      MPI_COMM_WORLD, &status );
	MPI_Start( &r1 );
	MPI_Cancel( &r1 );
	MPI_Waitany( 1, &r1, &idx, &status );
	MPI_Test_cancelled( &status, &flag );
	MPI_Sendrecv( MPI_BOTTOM, 0, MPI_INT, partner, 1,
		      MPI_BOTTOM, 0, MPI_INT, partner, 1,
		      MPI_COMM_WORLD, &status );
	if (!flag) {
	    err++;
	    printf( "Cancel of a receive failed where it should succeed (Waitany).\n" );
	}
	MPI_Request_free( &r1 );

	/* Cancel fails for testany */
        buf[0] = -1;
	MPI_Recv_init( buf, 10, MPI_INT, partner, 2, MPI_COMM_WORLD, &r1 );
	MPI_Start( &r1 );
	MPI_Sendrecv( MPI_BOTTOM, 0, MPI_INT, partner, 1,
		      MPI_BOTTOM, 0, MPI_INT, partner, 1,
		      MPI_COMM_WORLD, &status );
	MPI_Cancel( &r1 );
	MPI_Testany( 1, &r1, &idx, &flag, &status );
	MPI_Test_cancelled( &status, &flag );
	if (flag) {
	    err++;
	    printf( "Cancel of a receive succeeded where it shouldn't (Testany).\n" );
	    if (buf[0] != -1) {
		printf( "Receive buffer changed even though cancel suceeded! (Test).\n" );
	    }
	}
	MPI_Request_free( &r1 );

	/* Cancel succeeds for waitsome */
	MPI_Recv_init( buf, 10, MPI_INT, partner, 0, MPI_COMM_WORLD, &r1 );
	MPI_Sendrecv( MPI_BOTTOM, 0, MPI_INT, partner, 1,
		      MPI_BOTTOM, 0, MPI_INT, partner, 1,
		      MPI_COMM_WORLD, &status );
	MPI_Start( &r1 );
	MPI_Cancel( &r1 );
	MPI_Waitsome( 1, &r1, &idx, &index, &status );
	MPI_Test_cancelled( &status, &flag );
	MPI_Sendrecv( MPI_BOTTOM, 0, MPI_INT, partner, 1,
		      MPI_BOTTOM, 0, MPI_INT, partner, 1,
		      MPI_COMM_WORLD, &status );
	if (!flag) {
	    err++;
	    printf( "Cancel of a receive failed where it should succeed (Waitsome).\n" );
	}
	MPI_Request_free( &r1 );

	/* Cancel fails for testsome*/
        buf[0] = -1;
	MPI_Recv_init( buf, 10, MPI_INT, partner, 2, MPI_COMM_WORLD, &r1 );
	MPI_Start( &r1 );
	MPI_Sendrecv( MPI_BOTTOM, 0, MPI_INT, partner, 1,
		      MPI_BOTTOM, 0, MPI_INT, partner, 1,
		      MPI_COMM_WORLD, &status );
	MPI_Cancel( &r1 );
	MPI_Testsome( 1, &r1, &idx, &index, &status );
	MPI_Test_cancelled( &status, &flag );
	if (flag) {
	    err++;
	    printf( "Cancel of a receive succeeded where it shouldn't (Testsome).\n" );
	    if (buf[0] != -1) {
		printf( "Receive buffer changed even though cancel suceeded! (Testsome).\n" );
	    }
	}
	MPI_Request_free( &r1 );

	if (err) {
	    printf( "Test failed with %d errors.\n", err );
	}
	else {
	    printf( " No Errors\n" );
	}
    }

    else if (rank == size - 1) {
	partner = 0;
	/* Cancel succeeds for wait/waitall */
	MPI_Sendrecv( MPI_BOTTOM, 0, MPI_INT, partner, 1,
		      MPI_BOTTOM, 0, MPI_INT, partner, 1,
		      MPI_COMM_WORLD, &status );
	MPI_Sendrecv( MPI_BOTTOM, 0, MPI_INT, partner, 1,
		      MPI_BOTTOM, 0, MPI_INT, partner, 1,
		      MPI_COMM_WORLD, &status );
	/* Cancel fails for test/testall */
	buf[0] = 3;
	MPI_Send( buf, 3, MPI_INT, partner, 2, MPI_COMM_WORLD );
	MPI_Sendrecv( MPI_BOTTOM, 0, MPI_INT, partner, 1,
		      MPI_BOTTOM, 0, MPI_INT, partner, 1,
		      MPI_COMM_WORLD, &status );

	/* Cancel succeeds for waitany */
	MPI_Sendrecv( MPI_BOTTOM, 0, MPI_INT, partner, 1,
		      MPI_BOTTOM, 0, MPI_INT, partner, 1,
		      MPI_COMM_WORLD, &status );
	MPI_Sendrecv( MPI_BOTTOM, 0, MPI_INT, partner, 1,
		      MPI_BOTTOM, 0, MPI_INT, partner, 1,
		      MPI_COMM_WORLD, &status );
	/* Cancel fails  for testany */
	MPI_Send( buf, 3, MPI_INT, partner, 2, MPI_COMM_WORLD );
	MPI_Sendrecv( MPI_BOTTOM, 0, MPI_INT, partner, 1,
		      MPI_BOTTOM, 0, MPI_INT, partner, 1,
		      MPI_COMM_WORLD, &status );

	/* Cancel succeeds for waitsome */
	MPI_Sendrecv( MPI_BOTTOM, 0, MPI_INT, partner, 1,
		      MPI_BOTTOM, 0, MPI_INT, partner, 1,
		      MPI_COMM_WORLD, &status );
	MPI_Sendrecv( MPI_BOTTOM, 0, MPI_INT, partner, 1,
		      MPI_BOTTOM, 0, MPI_INT, partner, 1,
		      MPI_COMM_WORLD, &status );
	/* Cancel fails  for waitsome */
	MPI_Send( buf, 3, MPI_INT, partner, 2, MPI_COMM_WORLD );
	MPI_Sendrecv( MPI_BOTTOM, 0, MPI_INT, partner, 1,
		      MPI_BOTTOM, 0, MPI_INT, partner, 1,
		      MPI_COMM_WORLD, &status );

    /* 
       Next test - check that a cancel for a request receive from
       MPI_PROC_NULL succeeds (there is some suspicion that some
       systems can't handle this - also, MPI_REQUEST_NULL 
     */
    /* A null request is an error. (null objects are errors unless otherwise
       allowed)
    r1 = MPI_REQUEST_NULL;
    MPI_Cancel( &r1 );
    */
	MPI_Recv_init( buf, 10, MPI_INT, MPI_PROC_NULL, 0, MPI_COMM_WORLD, &r1 );
	MPI_Start( &r1 );
	MPI_Cancel( &r1 );
	MPI_Request_free( &r1 );    /* Must complete cancel.  We know that it 
				       won't complete, so we don't need to do
				       anything else */
    }

    MPI_Finalize();
    return 0;
}
