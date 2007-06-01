#include <stdio.h>
#include "mpi.h"
#include <stdlib.h>
#include "test.h"

/* Test request creation */

int main( int argc, char **argv )
{
    int i, n, n_goal = 2048, rc, len, buf[1];
    MPI_Request *req_array;
    MPI_Status status;
    char msg[MPI_MAX_ERROR_STRING];

    MPI_Init( &argc, &argv );
    MPI_Errhandler_set( MPI_COMM_WORLD, MPI_ERRORS_RETURN );

    n = n_goal;

    req_array = (MPI_Request *)malloc( n * sizeof(MPI_Request) );
    
    for (i=0; i<n; i++) {
	rc = MPI_Irecv( buf, 1, MPI_INT, 0, i, MPI_COMM_WORLD, req_array + i );
	if (rc) {
	    fprintf( stderr, "Error when creating request number %d\n", i );
	    MPI_Error_string( rc, msg, &len );
	    fprintf( stderr, "%s\n", msg );
	    n = i + 1;
	    break;
	}
    }
    for (i=0; i<n; i++) {
	rc = MPI_Cancel( req_array + i );
	if (rc) {
	    fprintf( stderr, "Error when canceling request number %d\n", i );
	    MPI_Error_string( rc, msg, &len );
	    fprintf( stderr, "%s\n", msg );
	    n = i + 1;
	    break;
	}
	rc = MPI_Request_free( req_array + i );
	if (rc) {
	    fprintf( stderr, "Error when freeing request number %d\n", i );
	    MPI_Error_string( rc, msg, &len );
	    fprintf( stderr, "%s\n", msg );
	    n = i + 1;
	    break;
	}
    }

    printf( "Completed test of %d request creations (with cancel)\n", n );

    for (i=0; i<n; i++) {
	rc = MPI_Irecv( buf, 1, MPI_INT, MPI_PROC_NULL, i, MPI_COMM_WORLD, 
			req_array + i );
	if (rc) {
	    fprintf( stderr, "Error when creating request number %d\n", i );
	    MPI_Error_string( rc, msg, &len );
	    fprintf( stderr, "%s\n", msg );
	    n = i + 1;
	    break;
	}
    }
    for (i=0; i<n; i++) {
	rc = MPI_Wait( req_array + i, &status );
	if (rc) {
	    fprintf( stderr, "Error when waiting on request number %d\n", i );
	    MPI_Error_string( rc, msg, &len );
	    fprintf( stderr, "%s\n", msg );
	    n = i + 1;
	    break;
	}
    }

    printf( "Completed test of %d request creations (with wait)\n", n );
    if (n != n_goal) {
	printf (
"This MPI implementation limits the number of request that can be created\n\
This is allowed by the standard and is not a bug, but is a limit on the\n\
implementation\n" );
    }
    free( req_array );
    MPI_Finalize( );
    return 0;
}
