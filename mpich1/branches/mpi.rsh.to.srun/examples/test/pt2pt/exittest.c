
/*
  A report was made that this program hung on a 2 processor LINUX cluster.
  We haven't seen that problem, but since this does test whether process 0
  waits for the other processes to complete before exiting, it is a good
  test to have.
 */
#include <stdio.h>
#include "mpi.h"
#define MAX_NUM_PROCS 10

int main( int argc, char *argv[])
{
  int idx;
  int num_procs,my_id;
  int s;
  int r;
  MPI_Status status;

  MPI_Init(&argc,&argv);
  MPI_Comm_size(MPI_COMM_WORLD,&num_procs);
  MPI_Comm_rank(MPI_COMM_WORLD,&my_id);
  
  if (num_procs < 3)
    {
    fprintf(stderr, "Need at least 3 processes for this bug\n");
    MPI_Finalize();
    return 0;
    }

#ifdef DEBUG  
  fprintf(stderr, "%d Starting ....\n", my_id);
  fflush(stderr);
#endif
  
  if (my_id == 1)
    {
    idx = 2;
    s = 333;
#ifdef DEBUG
    fprintf(stdout, "%d start send (%d) to %d\n", my_id, s, idx);
    fflush(stdout);
#endif
    MPI_Send(&s, 1, MPI_INT, idx, 0, MPI_COMM_WORLD);
#ifdef DEBUG
    fprintf(stdout, "%d finished send to %d\n", my_id, idx);
    fflush(stdout);
#endif
    }
  
  if (my_id == 2)
    {
    idx = 1;
#ifdef DEBUG
    fprintf(stdout, "%d start recv from %d\n", my_id, idx);
    fflush(stdout);
#endif
    MPI_Recv (&r, 1, MPI_INT, idx, 0, MPI_COMM_WORLD, &status );
#ifdef DEBUG
    fprintf(stdout, "%d finished recv (%d) from %d\n", my_id, r, idx);
    fflush(stdout);
#endif
    }

#ifdef DBUG  
  fprintf(stdout, "%d Done ....\n",my_id);
  fflush(stdout);
#endif  
  MPI_Barrier( MPI_COMM_WORLD );
  if (my_id == 0) {
      /* If we reach here, we're done */
      printf( " No Errors\n" );
  }

  MPI_Finalize();
  return 0;
}

