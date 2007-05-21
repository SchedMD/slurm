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
    int    err = 0;
    double t1, t2;
    double tick;
    int    i;

    MPI_Init( &argc, &argv );
    t1 = MPI_Wtime();
    t2 = MPI_Wtime();
    if (t2 - t1 > 0.1 || t2 - t1 < 0.0) {
	err++;
	fprintf( stderr, 
		 "Two successive calls to MPI_Wtime gave strange results: (%f) (%f)\n", 
		 t1, t2 );
    }
/* Try several times to get a 1 second sleep */
    for (i = 0; i<10; i++) {
	t1 = MPI_Wtime();
	sleep(1);
	t2 = MPI_Wtime();
	if (t2 - t1 >= (1.0 - 0.01) && t2 - t1 <= 5.0) break;
	if (t2 - t1 > 5.0) i = 9;
    }
    if (i == 10) {
	fprintf( stderr, 
		 "Timer around sleep(1) did not give 1 second; gave %f\n",
             t2 - t1 );
	fprintf( stderr, "If the sigchk check shows that SIGALRM is in use, \n\
this indicates only that user programs must NOT use any system call or\n\
library that uses SIGALRM.  SIGALRM is not used by MPICH but may be used\n\
by the software the MPICH uses to implement communication to other \n\
processes\n" );
	err++;
    } 
    tick = MPI_Wtick();
    if (tick > 1.0 || tick <= 0.0) {
	err++;
	fprintf( stderr, "MPI_Wtick gave a strange result: (%f)\n", tick );
    }
    Test_Waitforall( );
    MPI_Finalize( );
    
    return err;
}
