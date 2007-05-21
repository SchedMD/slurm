#include <stdio.h>
#include "mpi.h"
#include "test.h"

/*
 * This example should be run with 2 processes and tests the ability of the
 * implementation to handle a flood of one-way messages.
 */

int main( int argc, char **argv )
{
  double wscale = 10.0, scale;
  int numprocs, myid,i,namelen;
  char processor_name[MPI_MAX_PROCESSOR_NAME];

  MPI_Init(&argc,&argv);
  MPI_Comm_size(MPI_COMM_WORLD,&numprocs);
  MPI_Comm_rank(MPI_COMM_WORLD,&myid);
  MPI_Get_processor_name(processor_name,&namelen);

  /* fprintf(stderr,"Process %d on %s\n",
          myid, processor_name); */
  for ( i=0; i<10000; i++) {
    MPI_Allreduce(&wscale,&scale,1,MPI_DOUBLE,MPI_SUM,MPI_COMM_WORLD);
  }
  MPI_Finalize();
  return 0;
}
