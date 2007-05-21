#include "mpi.h"
#include <stdio.h>
#include "test.h"

void addem ( int *, int *, int *, MPI_Datatype * );
void assoc ( int *, int *, int *, MPI_Datatype * );

void addem(invec, inoutvec, len, dtype)
int *invec, *inoutvec, *len;
MPI_Datatype *dtype;
{
  int i;
  for ( i=0; i<*len; i++ ) 
    inoutvec[i] += invec[i];
}

#define BAD_ANSWER 100000

/*
    The operation is inoutvec[i] = invec[i] op inoutvec[i] 
    (see 4.9.4).  The order is important.

    Note that the computation is in process rank (in the communicator)
    order, independant of the root.
 */
void assoc(invec, inoutvec, len, dtype)
int *invec, *inoutvec, *len;
MPI_Datatype *dtype;
{
  int i;
  for ( i=0; i<*len; i++ )  {
    if (inoutvec[i] <= invec[i] ) {
      int rank;
      MPI_Comm_rank( MPI_COMM_WORLD, &rank );
      fprintf( stderr, "[%d] inout[0] = %d, in[0] = %d\n", 
	      rank, inoutvec[0], invec[0] );
      inoutvec[i] = BAD_ANSWER;
      }
    else 
      inoutvec[i] = invec[i];
  }
}

int main( int argc, char **argv )
{
    int              rank, size, i;
    int              data;
    int              errors=0;
    int              result = -100;
    int              correct_result;
    MPI_Op           op_assoc, op_addem;

    MPI_Init( &argc, &argv );
    MPI_Comm_rank( MPI_COMM_WORLD, &rank );
    MPI_Comm_size( MPI_COMM_WORLD, &size );

    data = rank;

    correct_result = 0;
    for (i=0;i<=rank;i++)
      correct_result += i;

    MPI_Scan ( &data, &result, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD );
    if (result != correct_result) {
	fprintf( stderr, "[%d] Error suming ints with scan\n", rank );
	errors++;
	}

    MPI_Scan ( &data, &result, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD );
    if (result != correct_result) {
	fprintf( stderr, "[%d] Error summing ints with scan (2)\n", rank );
	errors++;
	}

    data = rank;
    result = -100;
    MPI_Op_create( (MPI_User_function *)assoc, 0, &op_assoc );
    MPI_Op_create( (MPI_User_function *)addem, 1, &op_addem );
    MPI_Scan ( &data, &result, 1, MPI_INT, op_addem, MPI_COMM_WORLD );
    if (result != correct_result) {
	fprintf( stderr, "[%d] Error summing ints with scan (userop)\n", 
		 rank );
	errors++;
	}

    MPI_Scan ( &data, &result, 1, MPI_INT, op_addem, MPI_COMM_WORLD );
    if (result != correct_result) {
	fprintf( stderr, "[%d] Error summing ints with scan (userop2)\n", 
		 rank );
	errors++;
	}
    result = -100;
    data = rank;
    MPI_Scan ( &data, &result, 1, MPI_INT, op_assoc, MPI_COMM_WORLD );
    if (result == BAD_ANSWER) {
	fprintf( stderr, "[%d] Error scanning with non-commutative op\n",
		 rank );
	errors++;
	}

    MPI_Op_free( &op_assoc );
    MPI_Op_free( &op_addem );

    if (errors)
      printf( "[%d] done with ERRORS(%d)!\n", rank, errors );

    Test_Waitforall( );
    MPI_Finalize();
    return errors;
}
