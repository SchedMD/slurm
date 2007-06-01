#include <stdio.h>

#include "mpi.h"

/*
   This is a simple test that can be used on heterogeneous systems that
   use XDR or bytes swap encoding to check for correct lengths. 

   Sends back and forth to check on one-sided conversion schemes
 */
int main( int argc, char **argv )
{
    int rank, c;
    MPI_Status status;
    MPI_Datatype s1;
    static MPI_Datatype oldtypes[2] = { MPI_INT, MPI_CHAR };
    static int blens[2]             = { 1, 10 };
    MPI_Aint displs[2];
    struct {
	int len;
	char b[10];
	} buf;

    MPI_Init( &argc, &argv );
    MPI_Comm_rank( MPI_COMM_WORLD, &rank );

    MPI_Address( &buf.len,  &displs[0] );
    MPI_Address( &buf.b[0], &displs[1] );
    displs[1] = displs[1] - displs[0];
    displs[0] = 0;
    MPI_Type_struct( 2, blens, displs, oldtypes, &s1 );
    MPI_Type_commit( &s1 );

    /* Receives from ANY check for common format */
    if (rank == 0) {
	printf( "Sending from 1 to 0\n" );
	MPI_Recv( &buf, 1, s1, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, 
		  &status );
	MPI_Get_count( &status, s1, &c );
	if (c != 1) { 
	    printf( "(1)Did not get correct count; expected 1, got %d\n", c );
	    }
	}
    else if (rank == 1) {
	MPI_Send( &buf, 1, s1, 0, 0, MPI_COMM_WORLD );
	}

    if (rank == 1) {
	MPI_Recv( &buf, 1, s1, MPI_ANY_SOURCE, 1, MPI_COMM_WORLD, 
		  &status );
	MPI_Get_count( &status, s1, &c );
	if (c != 1) { 
	    printf( "(2)Did not get correct count; expected 1, got %d\n", c );
	    }
	}
    else if (rank == 0) {
	printf( "Sending from 0 to 1\n" );
	MPI_Send( &buf, 1, s1, 1, 1, MPI_COMM_WORLD );
	}

    /* Receives from specific note check for special cases */
    if (rank == 0) {
	printf( "Sending from 1 to 0\n" );
	MPI_Recv( &buf, 1, s1, 1, 0, MPI_COMM_WORLD, &status );
	MPI_Get_count( &status, s1, &c );
	if (c != 1) { 
	    printf( "(3)Did not get correct count; expected 1, got %d\n", c );
	    }
	}
    else if (rank == 1) {
	MPI_Send( &buf, 1, s1, 0, 0, MPI_COMM_WORLD );
	}

    if (rank == 1) {
	MPI_Recv( &buf, 1, s1, 0, 1, MPI_COMM_WORLD, &status );
	MPI_Get_count( &status, s1, &c );
	if (c != 1) { 
	    printf( "(4)Did not get correct count; expected 1, got %d\n", c );
	    }
	}
    else if (rank == 0) {
	printf( "Sending from 0 to 1\n" );
	MPI_Send( &buf, 1, s1, 1, 1, MPI_COMM_WORLD );
	}
    MPI_Type_free( &s1 );
    MPI_Finalize();
return 0;
}
