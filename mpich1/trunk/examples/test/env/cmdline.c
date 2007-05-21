#include <stdio.h>
#include <stdlib.h>
#include "mpi.h"

/* 
   This is a test program to see if command line arguments are handled
   well.  Note that MPI doesn't *require* anything here, so this is 
   simply used to acess "quality of implementation"

   run with arguments
       a "b c" "d'e" 'f"g" h'
 */
int main( int argc, char *argv[] )
{
    int i, rank, toterr, err = 0;
    static char *eargv[5];

    eargv[1] = "a";
    eargv[2] = "b c";
    eargv[3] = "d'e";
    eargv[4] = "f\"g\" h";

    MPI_Init( &argc, &argv );
    
    for (i=1; i<=4; i++) {
	if (!argv[i]) {
	    printf( "Argument %d is null!\n", i );
	    err++;
	}
    }
    MPI_Allreduce( &err, &toterr, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD );
    if (toterr) {
	MPI_Abort( 1, MPI_COMM_WORLD );
	return 0;
    }

    /* a "b c" "d'e" 'f"g" h' */
    for (i=1; i<=4; i++) {
	if (strcmp( argv[i], eargv[i] ) != 0) {
	    err++;
	    printf( "Found %s but expected %s\n", argv[i], eargv[i] );
	}
    }
    MPI_Allreduce( &err, &toterr, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD );

    MPI_Comm_rank( MPI_COMM_WORLD, &rank );
    if (rank == 0) {
	if (toterr) printf( "Found %d errors\n", toterr );
	else        printf( " No Errors\n" );
    }
    
    MPI_Finalize();
    return 0;
}
