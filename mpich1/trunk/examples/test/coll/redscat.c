/* 
 * Test of reduce scatter.
 *
 * Each processor contributes its rank + the index to the reduction, 
 * then receives the ith sum
 *
 * Can be called with any number of processors.
 */

#include "mpi.h"
#include <stdio.h>
#include <stdlib.h>
#include "test.h"

int main( int argc, char **argv )
{
    int      err = 0, toterr;
    int      *sendbuf, recvbuf, *recvcounts;
    int      size, rank, i, sumval;
    MPI_Comm comm;


    MPI_Init( &argc, &argv );
    comm = MPI_COMM_WORLD;

    MPI_Comm_size( comm, &size );
    MPI_Comm_rank( comm, &rank );
    sendbuf = (int *) malloc( size * sizeof(int) );
    for (i=0; i<size; i++) 
	sendbuf[i] = rank + i;
    recvcounts = (int *)malloc( size * sizeof(int) );
    for (i=0; i<size; i++) 
	recvcounts[i] = 1;

    MPI_Reduce_scatter( sendbuf, &recvbuf, recvcounts, MPI_INT, MPI_SUM, comm );

    sumval = size * rank + ((size - 1) * size)/2;
/* recvbuf should be size * (rank + i) */
    if (recvbuf != sumval) {
	err++;
	fprintf( stdout, "Did not get expected value for reduce scatter\n" );
	fprintf( stdout, "[%d] Got %d expected %d\n", rank, recvbuf, sumval );
    }

    MPI_Allreduce( &err, &toterr, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD );
    if (rank == 0 && toterr == 0) {
	printf( " No Errors\n" );
    }
    MPI_Finalize( );

    return toterr;
}
