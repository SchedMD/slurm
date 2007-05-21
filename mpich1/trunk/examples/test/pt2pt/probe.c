/* 
   This is a test of probe to receive a message of unknown length
 */

#include <stdio.h>
#include <string.h>
#include "mpi.h"
#include "test.h"

int main( int argc, char **argv ) 
{
int data, to, from, tag, maxlen, np, myid, src, dest;
MPI_Status status;

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
    tag  = 2000;
#ifdef VERBOSE    
    printf( "About to send\n" );
#endif
    MPI_Send( &data, 1, MPI_INT, to, tag, MPI_COMM_WORLD );
    }
else {
    tag    = 2000;
    from   = MPI_ANY_SOURCE;
    MPI_Probe( from, tag, MPI_COMM_WORLD, &status );
    MPI_Get_count( &status, MPI_INT, &maxlen );
    /* Here I'd normally allocate space; I'll just check that it is ok */
    if (maxlen > 1)
	printf( "Error; size = %d\n", maxlen );
#ifdef VERBOSE
    printf( "About to receive\n" );
#endif
    MPI_Recv( &data, 1, MPI_INT, status.MPI_SOURCE, status.MPI_TAG, 
	      MPI_COMM_WORLD, &status );
    }
MPI_Barrier( MPI_COMM_WORLD );
Test_Waitforall( );
MPI_Finalize();
return 0;
}
