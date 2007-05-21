#include "mpi.h"
#include "test.h"
#include <stdlib.h>
#include <stdio.h>

int main( int argc, char **argv )
{
    int              rank, size, i;
    int             *table;
    int              errors=0;
    MPI_Aint         address;
    MPI_Datatype     type, newtype;
    int              lens;

    MPI_Init( &argc, &argv );
    MPI_Comm_rank( MPI_COMM_WORLD, &rank );
    MPI_Comm_size( MPI_COMM_WORLD, &size );

    /* Make data table */
    table = (int *) calloc (size, sizeof(int));
    table[rank] = rank + 1;

    MPI_Barrier ( MPI_COMM_WORLD );
    /* Broadcast the data */
    for ( i=0; i<size; i++ ) 
      MPI_Bcast( &table[i], 1, MPI_INT, i, MPI_COMM_WORLD );

    /* See if we have the correct answers */
    for ( i=0; i<size; i++ )
      if (table[i] != i+1) errors++;

    MPI_Barrier ( MPI_COMM_WORLD );

    /* Try the same thing, but with a derived datatype */
    for ( i=0; i<size; i++ ) 
	table[i] = 0;
    table[rank] = rank + 1;
    for ( i=0; i<size; i++ ) {
	MPI_Address( &table[i], &address );
	type = MPI_INT;
	lens = 1;
	MPI_Type_struct( 1, &lens, &address, &type, &newtype );
	MPI_Type_commit( &newtype );
	MPI_Bcast( MPI_BOTTOM, 1, newtype, i, MPI_COMM_WORLD );
	MPI_Type_free( &newtype );
	}
    /* See if we have the correct answers */
    for ( i=0; i<size; i++ )
      if (table[i] != i+1) errors++;

    MPI_Barrier ( MPI_COMM_WORLD );

    Test_Waitforall( );
    MPI_Finalize();
    if (errors)
      printf( "[%d] done with ERRORS!\n", rank );
    return errors;
}


