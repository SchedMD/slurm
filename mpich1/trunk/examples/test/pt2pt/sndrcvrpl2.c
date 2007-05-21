
/* 
 * Based on a program from James Clippinger (james@cs.dartmouth.edu), 
 * http://www.cs.dartmouth.edu/~james/.
 *
 */
#include "test.h"
#include <stdlib.h>
#include <stdio.h>
#include "mpi.h"

int main( int argc, char **argv )
{
    MPI_Status status;
    int count, rank, size,  dest, source, i, err = 0, toterr;
    long *buf;

    /* Initialize MPI and get my rank and total number of
       processors */
    
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    /* Send-receive-replace the buffer */
    count  = 1 << 14;
    buf    = (long *)malloc( count * sizeof(long) );
    for (i=0; i<count; i++)
	buf[i] = rank + size*i;
    dest   = (rank + 1) % size;
    source = (rank + size - 1) % size;

/*
    fprintf(stderr, "Proc %d: About to SRR, dest proc %d, source proc 
%d\n",
	    rank, dest, source);
 */
    MPI_Sendrecv_replace( buf, count, MPI_LONG, dest, 
                          1, source, 1, MPI_COMM_WORLD, &status );

    for (i=0; i<count; i++) {
	if (buf[i] != source + size*i) {
	    if (err++ > 10) break;
	    printf( "Received %ld in buf[%d]; expected %d\n",
		    buf[i], i, source + size*i );
	}
    }
/*
    fprintf(stderr, "Done with SRR on proc %d\n", rank);
 */

    /* Finalize everything */
    MPI_Allreduce( &err, &toterr, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD );
    if (rank == 0) {
	if (toterr == 0) 
	    printf( " No Errors\n" );
	else
	    printf( "Test failed with %d errors!\n", toterr );
    }
    free( buf );
    
    MPI_Finalize();
    return 0;
}
