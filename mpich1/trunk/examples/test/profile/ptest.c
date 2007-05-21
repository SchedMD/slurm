#include <stdio.h>
#include "mpi.h"

/* Header for testing procedures */

#if defined(NEEDS_STDLIB_PROTOTYPES)
#include "protofix.h"
#endif

/*
 * This tests for the existence of MPI_Pcontrol; nothing more.
 */
int main( int argc, char **argv )
{
    MPI_Init( &argc, &argv );
    
    MPI_Pcontrol( 0 );
    printf( "Pcontrol test passed\n" );
    MPI_Finalize();
    return 0;
}
