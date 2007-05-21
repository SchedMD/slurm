#include <stdio.h>
#include <stdlib.h>
#include "mpi.h"
#include "test.h"
#ifdef HAVE_WINDOWS_H
#define sleep(a_) Sleep((a_)*1000)
#include <windows.h>
#endif

int main( int argc, char **argv )
{
    double t1, t2;
    double tick;
    int    i;

    MPI_Init( &argc, &argv );
    t1 = MPI_Wtime();
    t2 = MPI_Wtime();
    fprintf( stdout, "Two successive calls to MPI_Wtime gave: (%f) (%f)\n", 
		t1, t2 );
    fprintf( stdout, "Five approximations to one second:\n");
    for (i = 0; i < 5; i++)
    {
	t1 = MPI_Wtime();
	sleep(1);
	t2 = MPI_Wtime();
	fprintf( stdout, "%f seconds\n", t2 - t1 );
    } 
    tick = MPI_Wtick();
    fprintf( stdout, "MPI_Wtick gave: (%10.8f)\n", tick );

    MPI_Finalize( );

    return 0;
}
