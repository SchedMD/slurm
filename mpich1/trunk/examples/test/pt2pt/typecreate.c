#include <stdio.h>
#include "mpi.h"
#include "test.h"
#include <stdlib.h>

int main( int argc, char **argv )
{
    int i, n, n_goal = 2048, rc, len;
    MPI_Datatype *type_array;
    char msg[MPI_MAX_ERROR_STRING];

    MPI_Init( &argc, &argv );
    MPI_Errhandler_set( MPI_COMM_WORLD, MPI_ERRORS_RETURN );

    n = n_goal;

    type_array = (MPI_Datatype *)malloc( n * sizeof(MPI_Datatype) );
    
    for (i=0; i<n; i++) {
	int		blens[2];
	MPI_Aint	displ[2];
	MPI_Datatype    types[2];

	blens[0] = 2;
	blens[1] = 3;
	displ[0] = 0;
	displ[1] = (i+2) * sizeof(int);
	types[0] = MPI_INT;
	types[1] = MPI_DOUBLE;
	rc = MPI_Type_struct( 2, blens, displ, types, type_array + i );
 	if (rc) {
	    fprintf( stderr, "Error when creating type number %d\n", i );
	    MPI_Error_string( rc, msg, &len );
	    fprintf( stderr, "%s\n", msg );
	    n = i + 1;
	    break;
	}
	rc = MPI_Type_commit( type_array + i );
	if (rc) {
	    fprintf( stderr, "Error when commiting type number %d\n", i );
	    MPI_Error_string( rc, msg, &len );
	    fprintf( stderr, "%s\n", msg );
	    n = i + 1;
	    break;
	}
    }
    for (i=0; i<n; i++) {
	rc = MPI_Type_free( type_array + i );
	if (rc) {
	    fprintf( stderr, "Error when freeing type number %d\n", i );
	    MPI_Error_string( rc, msg, &len );
	    fprintf( stderr, "%s\n", msg );
	    break;
	}
    }
    free( type_array );
    printf( "Completed test of %d type creations\n", n );
    if (n != n_goal) {
	printf (
"This MPI implementation limits the number of datatypes that can be created\n\
This is allowed by the standard and is not a bug, but is a limit on the\n\
implementation\n" );
    }
    MPI_Finalize( );
    return 0;
}
