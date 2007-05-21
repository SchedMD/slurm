#include <stdio.h>
#include <stdlib.h>
/* #include <memory.h> */
#include "mpid.h"
#include "mpid_time.h"

#include "aditest.h"

/* Define this global symbol */
MPI_Comm MPI_COMM_WORLD;

/* 
 * Simple ADI test.  
 *
 * Still need to do - check error returns.
 */

int main(argc,argv)
int argc;
char **argv;
{
    int    err = 0;
    double t1, t2;
    double tick;
    int    i;

    MPID_Init( &argc, &argv, (void *)0, &err );
    MPID_Wtime( &t1 );
    MPID_Wtime( &t2 );
    if (t2 - t1 > 0.1 || t2 - t1 < 0.0) {
	err++;
	fprintf( stderr, 
		 "Two successive calls to MPID_Wtime gave strange results: (%f) (%f)\n", 
		 t1, t2 );
    }
/* Try several times to get a 1 second sleep */
    for (i = 0; i<10; i++) {
	MPID_Wtime( &t1 );
	sleep(1);
	MPID_Wtime( &t2 );
	if (t2 - t1 >= 1.0 && t2 - t1 <= 5.0) break;
	if (t2 - t1 > 5.0) i = 9;
    }
    if (i == 10) {
	fprintf( stderr, "Timer around sleep(1) did not give 1 second; gave %f\n",
		 t2 - t1 );
	fprintf( stderr, "If the sigchk check shows that SIGALRM is in use, \n\
this indicates only that user programs must NOT use any system call or\n\
library that uses SIGALRM.  SIGALRM is not used by MPICH but may be used\n\
by the software the MPICH uses to implement communication to other \n\
processes\n" );
	err++;
    } 
    MPID_Wtick( &tick );
    if (tick > 1.0 || tick < 0.0) {
	err++;
	fprintf( stderr, "MPID_Wtick gave a strange result: (%f)\n", tick );
    }
    MPID_End();

    return err;
}
