#include "mpi.h"
#include <stdlib.h>
#include <stdio.h>
#include "test.h"

/*
  This program tests MPI_Alltoallv by having processor i send different
  amounts of data to each processor.

  Because there are separate send and receive types to alltoallv,
  there need to be tests to rearrange data on the fly.  Not done yet.
  
  The first test sends i items to processor i from all processors.

  Currently, the test uses only MPI_INT; this is adequate for testing systems
  that use point-to-point operations
 */

int main( int argc, char **argv )
{

    MPI_Comm comm;
    int      *sbuf, *rbuf;
    int      rank, size;
    int      *sendcounts, *recvcounts, *rdispls, *sdispls;
    int      i, j, *p, err, toterr;
    
    MPI_Init( &argc, &argv );
    err = 0;
    
    comm = MPI_COMM_WORLD;

    /* Create the buffer */
    MPI_Comm_size( comm, &size );
    MPI_Comm_rank( comm, &rank );
    sbuf = (int *)malloc( size * size * sizeof(int) );
    rbuf = (int *)malloc( size * size * sizeof(int) );
    if (!sbuf || !rbuf) {
	fprintf( stderr, "Could not allocated buffers!\n" );
	MPI_Abort( comm, 1 );
    }
    
    /* Load up the buffers */
    for (i=0; i<size*size; i++) {
	sbuf[i] = i + 100*rank;
	rbuf[i] = -i;
    }

    /* Create and load the arguments to alltoallv */
    sendcounts = (int *)malloc( size * sizeof(int) );
    recvcounts = (int *)malloc( size * sizeof(int) );
    rdispls    = (int *)malloc( size * sizeof(int) );
    sdispls    = (int *)malloc( size * sizeof(int) );
    if (!sendcounts || !recvcounts || !rdispls || !sdispls) {
	fprintf( stderr, "Could not allocate arg items!\n" );
	MPI_Abort( comm, 1 );
    }
    for (i=0; i<size; i++) {
	sendcounts[i] = i;
	recvcounts[i] = rank;
	rdispls[i]    = i * rank;
	sdispls[i]    = (i * (i+1))/2;
    }
    MPI_Alltoallv( sbuf, sendcounts, sdispls, MPI_INT,
		   rbuf, recvcounts, rdispls, MPI_INT, comm );

    /* Check rbuf */
    for (i=0; i<size; i++) {
	p = rbuf + rdispls[i];
	for (j=0; j<rank; j++) {
	    if (p[j] != i * 100 + (rank*(rank+1))/2 + j) {
		fprintf( stderr, "[%d] got %d expected %d for %dth\n",
			 rank, p[j],(i*(i+1))/2 + j, j );
		err++;
	    }
	}
    }

    free( sdispls );
    free( rdispls );
    free( recvcounts );
    free( sendcounts );
    free( rbuf );
    free( sbuf );

    MPI_Allreduce( &err, &toterr, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD );
    MPI_Comm_rank( MPI_COMM_WORLD, &rank );
    if (rank == 0) {
	if (toterr > 0) 
	    fprintf( stderr, "Test FAILED with %d errors\n", toterr );
	else
	    fprintf( stderr, " No Errors\n" );
    }
	
    MPI_Finalize();
    return 0;
}
