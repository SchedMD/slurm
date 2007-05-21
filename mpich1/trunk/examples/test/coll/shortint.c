#include "mpi.h"
#include <stdio.h>
typedef struct { short a; int b } s1;

main( int argc, char **argv )
{
s1 s[10], sout[10];
int i, rank;
MPI_Status status;

MPI_Init( &argc, &argv );
MPI_Comm_rank( MPI_COMM_WORLD, &rank );
for (i=0; i<10; i++) {
    s[i].a = rank + i;
    s[i].b = rank;
    sout[i].a = -1;
    sout[i].b = -1;
    }
/* MPI_Allreduce( s, sout, 10, MPI_SHORT_INT, MPI_MINLOC, MPI_COMM_WORLD ); */
/* if (rank == 1) 
    for (i=0; i<10; i++) 
	sout[i] = s[i];
 */
MPI_Reduce( s, sout, 10, MPI_SHORT_INT, MPI_MINLOC, 1, MPI_COMM_WORLD );
if (rank == 1)
for (i=0; i<10; i++) {
    printf( "[%d] (%x,%x)\n", rank, (int)sout[i].a, sout[i].b );
    }
if (rank == 1) 
    MPI_Send( sout, 10, MPI_SHORT_INT, 0, 0, MPI_COMM_WORLD );
else if (rank == 0)
    MPI_Recv( sout, 10, MPI_SHORT_INT, 1, 0, MPI_COMM_WORLD, &status );
/* MPI_Bcast( sout, 10, MPI_SHORT_INT, 1, MPI_COMM_WORLD ); */
for (i=0; i<10; i++) {
    printf( "[%d] (%x,%x)\n", rank, (int)sout[i].a, sout[i].b );
    }
MPI_Finalize();
return 0;
}
