#include "mpi.h"
#include <stdio.h>
#include "test.h"

int main( int argc, char **argv )
{
  int rank, value, result;

  MPI_Init (&argc, &argv);
  MPI_Comm_rank (MPI_COMM_WORLD, &rank);

  value = (rank == 0) ? 3 : 6;
  MPI_Allreduce (&value, &result, 1, MPI_INT, MPI_BOR, MPI_COMM_WORLD);
  if (rank == 0) printf ("Result of 3 BOR 6 is %d, result of 3|6 is %d\n", 
                         result, 3|6);

  Test_Waitforall( );
  MPI_Finalize ();

  return 0;
}
