/* 
   This is a test of MPI_Test to receive a message of known length (used as a
   server)
 */
#include "mpi.h"
#include <stdio.h>
#include <string.h>
#include "test.h"

int main( int argc, char **argv ) 
{
    int data, to, from, tag, maxlen, np, myid, flag, dest, src;
    MPI_Status status;
    MPI_Request request;

    MPI_Init( &argc, &argv );
    MPI_Comm_rank( MPI_COMM_WORLD, &myid );
    MPI_Comm_size( MPI_COMM_WORLD, &np );

/* dest writes out the received stats; for the output to be
   consistant (with the final check), it should be procees 0 */
    if (argc > 1 && argv[1] && strcmp( "-alt", argv[1] ) == 0) {
	dest = np - 1;
	src  = 0;
    }
    else {
	src  = np - 1;
	dest = 0;
    }

    if (myid == src) {
	to   = dest;
	tag = 2000;
	data = 100;
#ifdef VERBOSE
	printf( "About to send\n" );
#endif
	MPI_Send( &data, 1, MPI_INT, to, tag, MPI_COMM_WORLD );
	tag = 2001;
	data = 0;
#ifdef VERBOSE
	printf( "About to send 'done'\n" );
#endif
	MPI_Send( &data, 1, MPI_INT, to, tag, MPI_COMM_WORLD );
    }
    else {
	/* Server loop */
	while (1) {
	    tag    = MPI_ANY_TAG;
	    from   = MPI_ANY_SOURCE;
	    MPI_Irecv( &data, 1, MPI_INT, from, tag, MPI_COMM_WORLD,
		       &request );
	    /* Should really use MPI_Wait, but functionally this will work
	       (it is less efficient, however) */
	    do {		
		MPI_Test( &request, &flag, &status );
	    } while (!flag);
	    if (status.MPI_TAG == 2001) {
#ifdef VERBOSE
		printf( "Received terminate message\n" );
#endif
		break;
	    }
	    if (status.MPI_TAG == 2000) {
		MPI_Get_count( &status, MPI_INT, &maxlen );
		if (maxlen != 1) {
		    fprintf( stderr, "Should have received one integer; got %d\n",
			     maxlen );
		}
		/* Check data: */
		if (data != 100) {
		    fprintf( stderr, 
			     "Did not receive correct data: %d instead of %d\n", 
			     data, 100 );
		}
	    }
	}
    }
    MPI_Barrier( MPI_COMM_WORLD );
    Test_Waitforall( );
    MPI_Finalize();
    return 0;
}
