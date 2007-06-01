#include <stdio.h>
#include "mpi.h"

int main( int argc, char **argv )
{
  int i_size;
  MPI_Aint i_extent;
    
  MPI_Init(&argc, &argv);


  MPI_Type_extent(MPI_INT, &i_extent);
  printf("MPI_Type_extent (MPI_INT) = %d\n", i_extent);

  MPI_Type_size(MPI_INT, &i_size);
  printf("MPI_Type_size (MPI_INT) = %d\n", i_size);


  MPI_Type_extent(MPI_UNSIGNED, &i_extent);
  printf("MPI_Type_extent (MPI_UNSIGNED) = %d\n", i_extent);

  MPI_Type_size(MPI_UNSIGNED, &i_size);
  printf("MPI_Type_size (MPI_UNSIGNED) = %d\n", i_size);

#if defined(HAVE_LONG_DOUBLE)
  MPI_Type_extent(MPI_LONG_DOUBLE, &i_extent);
  printf("MPI_Type_extent (MPI_LONG_DOUBLE) = %d\n", i_extent);

  MPI_Type_size(MPI_LONG_DOUBLE, &i_size);
  printf("MPI_Type_size (MPI_LONG_DOUBLE) = %d\n", i_size);
#endif

  Test_Waitforall( );
  MPI_Finalize();
  return 0;
}
