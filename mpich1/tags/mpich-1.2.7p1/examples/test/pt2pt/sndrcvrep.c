#include "mpi.h"
#include <stdio.h>
#include <stdlib.h>

#if defined(NEEDS_STDLIB_PROTOTYPES)
#include "protofix.h"
#endif

int main( int argc, char **argv )
{
    MPI_Status   status;
    int          count, dest, source, sendtag, recvtag, len, rc;
    int          rank, size, errcnt = 0;
    MPI_Comm     comm;
    int          *buf;
    MPI_Datatype dtype;
    MPI_Init( &argc, &argv );

    MPI_Comm_dup( MPI_COMM_WORLD, &comm );
    MPI_Errhandler_set( comm, MPI_ERRORS_RETURN );

    MPI_Comm_rank( MPI_COMM_WORLD, &rank );
    MPI_Comm_size( MPI_COMM_WORLD, &size );
    /* Check recoverable errors */
    if (rank == 0) {
	rc = MPI_Sendrecv_replace( (char *)0, 1, MPI_INT, 0, 0,
				   0, 0, comm, &status );
	if (!rc) {
	    errcnt++;
	    fprintf( stderr, "Failed to detect null buffer\n" );
	}
	buf = 0; /* Give buf a value before use */
	rc = MPI_Sendrecv_replace( buf, 1, MPI_DATATYPE_NULL, 0,
				   0, 0, 0, comm, &status );
	if (!rc) {
	    errcnt++;
	    fprintf( stderr, "Failed to detect null datatype\n" );
	}
	/* Could be others */
    }

    /* Check non-contiguous datatypes */
    MPI_Type_vector( 1, 1, 10, MPI_INT, &dtype );
    MPI_Type_commit( &dtype );

    buf    = (int *)malloc( 10 * 10 * sizeof(int) );
    dest   = (rank + 1) % size;
    source = (rank + size - 1) % size;

    count   = 0;
    sendtag = 1;
    recvtag = 1;
    MPI_Sendrecv_replace( buf, count, dtype, dest, 
			  sendtag, source, recvtag, MPI_COMM_WORLD, &status );
    MPI_Get_count( &status, dtype, &len );
    if (len != 0) {
	errcnt ++;
	fprintf( stderr, "Computed %d for count, should be %d\n", len, 0 );
    }
    
    MPI_Type_free( &dtype );
    MPI_Comm_free( &comm );
    if (rank == 0) {
	printf( "Completed test of MPI_Sendrecv_replace\n" );
    }
    MPI_Finalize();
    return 0;
}
