#include "mpi.h"
/* This  simple test checks that MPI_Abort kills all processes 
 * There are two interesting cases:
 * masternode == 0
 * masternode != 0
 */
int main( int argc, char **argv )
{
  int node, size, i;
  int masternode = 0;

  MPI_Init(&argc, &argv);

  MPI_Comm_rank(MPI_COMM_WORLD, &node);
  MPI_Comm_size(MPI_COMM_WORLD, &size);

  /* Check for -altmaster */
  for (i=1; i<argc; i++) {
      if (argv[i] && strcmp( "-altmaster", argv[i] ) == 0) {
	  masternode = size-1;
      }
  }

  if(node == masternode) {
    MPI_Abort(MPI_COMM_WORLD, 99);
  }
  else {
    /* barrier will hang since masternode never calls */
    MPI_Barrier(MPI_COMM_WORLD);
  }

  MPI_Finalize();
  return 0;
}
