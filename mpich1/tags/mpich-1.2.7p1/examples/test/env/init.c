#include "mpi.h"
#include <stdio.h>
#include "test.h"

int main( int argc, char **argv )
{
    int        flag;
    MPI_Status status;
    int        size, rank, partner, i;

    for (i=0; i<2; i++ ) {
	MPI_Initialized(&flag);
	if(flag == 0)
	    MPI_Init(&argc,&argv);
    }

    MPI_Comm_size( MPI_COMM_WORLD, &size );
    if (size != 2) {
	printf( "Test must be run with 2 processes\n" );
	MPI_Abort( MPI_COMM_WORLD, 1 );
    }
    MPI_Comm_rank( MPI_COMM_WORLD, &rank );
    partner = (rank + 1) % size;
    MPI_Sendrecv( MPI_BOTTOM, 0, MPI_INT, partner, 0,
		  MPI_BOTTOM, 0, MPI_INT, partner, 0, 
		  MPI_COMM_WORLD, &status );
    if (rank == 0) printf( " No Errors\n" );
    MPI_Finalize();
    return 0;
}
