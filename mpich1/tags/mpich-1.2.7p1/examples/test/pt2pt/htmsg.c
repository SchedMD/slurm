#include "mpi.h"
#include <stdio.h>
#include <string.h>

#if defined(NEEDS_STDLIB_PROTOTYPES)
#include "protofix.h"
#endif

/* 
   This is a very simple MPI program which can be used to check things
   like the behavior of the ADI or heterogeneous code
 */
int main( int argc, char **argv )
{
char msg[10];
char smsg[10];
int  rank, size;
int  src, dest;
int  count;
MPI_Status status;

MPI_Init( &argc, &argv );
MPI_Comm_size( MPI_COMM_WORLD, &size );
MPI_Comm_rank( MPI_COMM_WORLD, &rank );
if (size != 2) {
    MPI_Abort( MPI_COMM_WORLD, 1 );
    return 1;
    }
src  = 1;
dest = 0;
if (rank == src) {
    strcpy( msg, "MPICH!" );
    MPI_Send( msg, 7, MPI_CHAR, dest, 10, MPI_COMM_WORLD );
    }
else {
    MPI_Recv( smsg, 10, MPI_CHAR, src, 10, MPI_COMM_WORLD, &status );
    if (status.MPI_TAG != 10) {
	fprintf( stderr, "Error in status tag!\n" );
	}
    if (status.MPI_SOURCE != src) {
	fprintf( stderr, "Error in status source!\n" );
	}
    MPI_Get_count( &status, MPI_CHAR, &count );
    if (count != 7) {
	fprintf( stderr, "Error in count, got %d expected 7\n", count );
	}
    if (strcmp( smsg, "MPICH!" )) {
	fprintf( stderr, "Got wrong msg (%s), expected \"MPICH!\"\n", smsg );
	}
    }

MPI_Finalize();
return 0;
}
