#include "mpi.h"
#include <stdio.h>
/* stdlib.h Needed for malloc declaration */
#include <stdlib.h>
#include "test.h"

int main( int argc, char **argv )
{
    int i, n, n_goal = 2048, n_all, rc, n_ranks, *ranks, rank, size, len;
    MPI_Group *group_array, world_group;
    char msg[MPI_MAX_ERROR_STRING];

    MPI_Init( &argc, &argv );
    MPI_Errhandler_set( MPI_COMM_WORLD, MPI_ERRORS_RETURN );
    MPI_Comm_size( MPI_COMM_WORLD, &size );
    MPI_Comm_rank( MPI_COMM_WORLD, &rank );
    n = n_goal;
    
    group_array = (MPI_Group *)malloc( n * sizeof(MPI_Group) );

    MPI_Comm_group( MPI_COMM_WORLD, &world_group );

    n_ranks = size;
    ranks = (int *)malloc( size * sizeof(int) );
    for (i=0; i<size; i++) ranks[i] = i;

    for (i=0; i<n; i++) {
	rc = MPI_Group_incl( world_group, n_ranks, ranks, group_array + i );
 	if (rc) {
	    fprintf( stderr, "Error when creating group number %d\n", i );
	    MPI_Error_string( rc, msg, &len );
	    fprintf( stderr, "%s\n", msg );
	    n = i + 1;
	    break;
	}
	
    }

    for (i=0; i<n; i++) {
	rc = MPI_Group_free( group_array + i );
	if (rc) {
	    fprintf( stderr, "Error when freeing group number %d\n", i );
	    MPI_Error_string( rc, msg, &len );
	    fprintf( stderr, "%s\n", msg );
	    break;
	}
    }
    
    MPI_Group_free( &world_group );

    MPI_Reduce( &n, &n_all, 1, MPI_INT, MPI_MIN, 0, MPI_COMM_WORLD );
    if (rank == 0) {
	/* printf( "Completed test of %d type creations\n", n_all ); */
	if (n_all != n_goal) {
	    printf (
"This MPI implementation limits the number of groups that can be created\n\
This is allowed by the standard and is not a bug, but is a limit on the\n\
implementation\n" );
	}
	else {
	    printf( " No Errors\n" );
	}
    }

    MPI_Finalize( );
    return 0;
}
