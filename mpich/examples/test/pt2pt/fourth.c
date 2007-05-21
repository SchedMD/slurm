#include <stdio.h>
#include "mpi.h"
#include "test.h"

int main( int argc, char **argv )
{
    int rank, np, data = 777;
    MPI_Request handle;
    MPI_Status status;

    MPI_Init( &argc, &argv );
    MPI_Comm_rank( MPI_COMM_WORLD, &rank );
    MPI_Comm_size( MPI_COMM_WORLD, &np );

    if (np < 4) {
      MPI_Finalize();
      printf( "4 processors or more required, %d done\n", rank );
      return(1);
    }

    if (rank == 0) {
      MPI_Isend( &data, 1, MPI_INT, 1, 0, MPI_COMM_WORLD, &handle );
      MPI_Wait( &handle, &status );
      MPI_Irecv( &data, 1, MPI_INT, 1, 0, MPI_COMM_WORLD, &handle );
      MPI_Wait( &handle, &status );
      MPI_Isend( &data, 1, MPI_INT, 2, 0, MPI_COMM_WORLD, &handle );
      MPI_Wait( &handle, &status );
      MPI_Irecv( &data, 1, MPI_INT, 2, 0, MPI_COMM_WORLD, &handle );
      MPI_Wait( &handle, &status );
    }
    else if (rank == 1) {
      MPI_Irecv( &data, 1, MPI_INT, 0, 0, MPI_COMM_WORLD, &handle );
      MPI_Wait( &handle, &status );
      MPI_Isend( &data, 1, MPI_INT, 0, 0, MPI_COMM_WORLD, &handle );
      MPI_Wait( &handle, &status );
      MPI_Isend( &data, 1, MPI_INT, 3, 0, MPI_COMM_WORLD, &handle );
      MPI_Wait( &handle, &status );
      MPI_Irecv( &data, 1, MPI_INT, 3, 0, MPI_COMM_WORLD, &handle );
      MPI_Wait( &handle, &status );
    }
    else if (rank == 2) {
      MPI_Isend( &data, 1, MPI_INT, 3, 0, MPI_COMM_WORLD, &handle );
      MPI_Wait( &handle, &status );
      MPI_Irecv( &data, 1, MPI_INT, 3, 0, MPI_COMM_WORLD, &handle );
      MPI_Wait( &handle, &status );
      MPI_Irecv( &data, 1, MPI_INT, 0, 0, MPI_COMM_WORLD, &handle );
      MPI_Wait( &handle, &status );
      MPI_Isend( &data, 1, MPI_INT, 0, 0, MPI_COMM_WORLD, &handle );
      MPI_Wait( &handle, &status );
    }
    else if (rank == 3) {
      MPI_Irecv( &data, 1, MPI_INT, 2, 0, MPI_COMM_WORLD, &handle );
      MPI_Wait( &handle, &status );
      MPI_Isend( &data, 1, MPI_INT, 2, 0, MPI_COMM_WORLD, &handle );
      MPI_Wait( &handle, &status );
      MPI_Irecv( &data, 1, MPI_INT, 1, 0, MPI_COMM_WORLD, &handle );
      MPI_Wait( &handle, &status );
      MPI_Isend( &data, 1, MPI_INT, 1, 0, MPI_COMM_WORLD, &handle );
      MPI_Wait( &handle, &status );
    }
    Test_Waitforall( );
    MPI_Finalize();
    return(0);
}
