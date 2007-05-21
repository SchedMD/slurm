/*
 * Test from oertel@ZIB-Berlin.DE 
 */

/*
 * Test of MPI_Ssend on MPI implementation on Cray T3D
 *
 * Process dest should receive numbers 1,...,10 but
 * receives 274878030344 instead !!!
 *
 * Test program works correctly with MPI_Ssend replaced by MPI_Send!
 *
 *
 * Compiler options: /mpp/bin/cc -Tcray-t3d -g -X2 -I"directory of mpi.h"
 *
 * Output of run with option -mpiversion:

ssendt3d -mpiversion
MPI model implementation 1.00.11., T3D Device Driver, Version 0.0
MPI model implementation 1.00.11., T3D Device Driver, Version 0.0
Configured with -arch=cray_t3d -device=t3d -opt=-g -ar_nolocal -make=gmake
Configured with -arch=cray_t3d -device=t3d -opt=-g -ar_nolocal -make=gmake
Received 274878008072
Received 274878008072
Received 274878008072
Received 274878008072
Received 274878008072
Received 274878008072
Received 274878008072
Received 274878008072
Received 274878008072
Received 274878008072

 */

#include <stdio.h>
#include "mpi.h"


#if defined(NEEDS_STDLIB_PROTOTYPES)
#include "protofix.h"
#endif

#define SIZE 10

static int src  = 0;
static int dest = 1;

int main( int argc, char **argv )
{
    int rank; /* My Rank (0 or 1) */
    int i, ivalue;
    MPI_Status Stat;

    MPI_Init(&argc, &argv);
    MPI_Comm_rank( MPI_COMM_WORLD, &rank);

    if (rank == src) {

        for (i=1; i<=SIZE; i++)
        {
            MPI_Ssend( &i, 1, MPI_INT, dest, 2000, MPI_COMM_WORLD);
        }

    } else if (rank == dest) {
 
        for (i=1; i<=SIZE; i++)
        {
            MPI_Recv( &ivalue, 1, MPI_INT, src, 2000, MPI_COMM_WORLD, &Stat);
            printf("Received %d\n", ivalue); fflush(stdout);
        }
    }
 
    MPI_Barrier( MPI_COMM_WORLD);
    MPI_Finalize();
 
    return 0;
}
