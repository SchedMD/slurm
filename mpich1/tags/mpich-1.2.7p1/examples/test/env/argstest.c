#include <stdio.h>
#include "mpi.h"

int main( int argc, char **argv )
{
    int i;

    fprintf(stdout,"Before MPI_Init\n");
    for (i = 0; i < argc; i++)
	fprintf(stdout,"arg %d is %s\n", i, argv[i]);

    MPI_Init( &argc, &argv );

    fprintf(stdout,"After MPI_Init\n");
    for (i = 0; i < argc; i++)
	fprintf(stdout,"arg %d is %s\n", i, argv[i]);

    MPI_Finalize( );
}
