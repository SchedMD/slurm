
#include <stdio.h>
#include "mpi.h"
#include "test.h"

#define TABLE_SIZE 2

int main( int argc, char **argv )
{
  int    rank, size;
  double a[TABLE_SIZE];
  struct { double a; int b; } in[TABLE_SIZE], out[TABLE_SIZE];
  int    i;
  int    errors = 0, toterrors;

  /* Initialize the environment and some variables */
  MPI_Init( &argc, &argv );
  MPI_Comm_rank( MPI_COMM_WORLD, &rank );
  MPI_Comm_size( MPI_COMM_WORLD, &size );

  /* Initialize the maxloc data */
  for ( i=0; i<TABLE_SIZE; i++ ) a[i] = 0;
  for ( i=rank; i<TABLE_SIZE; i++ ) a[i] = (double)rank + 1.0;

  /* Copy data to the "in" buffer */
  for (i=0; i<TABLE_SIZE; i++) { 
	in[i].a = a[i];
	in[i].b = rank;
  }

  /* Reduce it! */
  MPI_Reduce( in, out, TABLE_SIZE, MPI_DOUBLE_INT, MPI_MAXLOC, 0, MPI_COMM_WORLD );
  MPI_Bcast ( out, TABLE_SIZE, MPI_DOUBLE_INT, 0, MPI_COMM_WORLD );

  /* Check to see that we got the right answers */
  for (i=0; i<TABLE_SIZE; i++) 
	if (i % size == rank)
	  if (out[i].b != rank) {
        printf("MAX (ranks[%d] = %d != %d\n", i, out[i].b, rank );
		errors++;
      }

  /* Initialize the minloc data */
  for ( i=0; i<TABLE_SIZE; i++ ) a[i] = 0;
  for ( i=rank; i<TABLE_SIZE; i++ ) a[i] = -(double)rank - 1.0;

  /* Copy data to the "in" buffer */
  for (i=0; i<TABLE_SIZE; i++)  {
	in[i].a = a[i];
	in[i].b = rank;
  }

  /* Reduce it! */
  MPI_Allreduce( in, out, TABLE_SIZE, MPI_DOUBLE_INT, MPI_MINLOC, MPI_COMM_WORLD );

  /* Check to see that we got the right answers */
  for (i=0; i<TABLE_SIZE; i++) 
	if (i % size == rank)
	  if (out[i].b != rank) {
        printf("MIN (ranks[%d] = %d != %d\n", i, out[i].b, rank );
		errors++;
      }

  /* Finish up! */
  MPI_Allreduce( &errors, &toterrors, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD );
  if (toterrors) {
      if (errors)
	  printf( "[%d] done with ERRORS(%d)!\n", rank, errors );
  }
  else {
      if (rank == 0) printf( " No Errors\n" );
  }
      
  MPI_Finalize();
  return errors;
}
