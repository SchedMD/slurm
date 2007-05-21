
/*
> so, my second question:
> 
>   2. what is the output of that MPI program?
> 
> i think it should be 42 -1 42 -1.
> 
> but compiling with mpich-1.1.0 an running on solaris machines
> (ch_p4) writes : 42 -1 42 0.
> 
> thanks,
>     Holger
> 
> MPI code:
> -------------------------------------------------------
*/
#include "test.h"
#include <stdio.h>
#include <stdlib.h>
#include "mpi.h"

int main( int argc, char **argv )
{
  int my_rank, i, data[6];
  MPI_Status  status;
  MPI_Datatype  my_type;
  int errs = 0, toterrs;

  MPI_Init(&argc, &argv);
  MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);

  MPI_Type_vector(2, 1, 2, MPI_INT, &my_type);
  MPI_Type_commit(&my_type);

  if (my_rank == 0) {
    data[0]=42;data[1]=42;
    MPI_Send(&(data[0]), 2, MPI_INT, 1, 42, MPI_COMM_WORLD);
  } else {
    for (i=0; i<6; i++)
      data[i] = -1;
    MPI_Recv(&(data[0]), 2, my_type, 0, 42, MPI_COMM_WORLD, &status);
    /* Check for correct receipt */
    if (data[0] != 42 || data[1] != -1 || data[2] != 42 || data[3] != -1 
	|| data[4] != -1 || data[5] != -1) {
	errs++;
	for (i=0; i<4; i++)
	    printf("%i ",data[i]);
	printf("\n");
    }
  }
  MPI_Allreduce( &errs, &toterrs, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD );
  if (my_rank == 0) {
      if (toterrs > 0) printf( "Found %d errors\n", toterrs );
      else             printf( " No Errors\n" );
  }

  MPI_Type_free( &my_type );
  MPI_Finalize();
  return 0;
}
