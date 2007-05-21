#include "mpi.h"
#include <stdio.h>
#include <stdlib.h>

#if defined(NEEDS_STDLIB_PROTOTYPES)
#include "protofix.h"
#endif

/*
 * This needs to test long messages as well as short ones.
 * The most likely failure mode for this program is that it will
 * hang.  Sorry about that....
 *
 */
int main( int argc, char **argv )
{
int           sendbuf[10];
int           sendcount = 10;
int           recvbuf[10];
int           recvcount = 10;
int           source = 0, recvtag = 2;
int           dest = 0, sendtag = 2;
int           i, *longsend, *longrecv;

    int               mpi_errno = MPI_SUCCESS;
    MPI_Status        status_array[2];
    MPI_Request       req[2];

    MPI_Init( &argc, &argv );
    if ((mpi_errno = MPI_Irecv ( recvbuf, recvcount, MPI_INT,
			    source, recvtag, MPI_COMM_WORLD, &req[1] ))) 
	return mpi_errno;
    if ((mpi_errno = MPI_Isend ( sendbuf, sendcount, MPI_INT, dest,   
			    sendtag, MPI_COMM_WORLD, &req[0] ))) 
	return mpi_errno;

    fprintf( stdout, "[%d] Starting waitall\n", 0 );
    mpi_errno = MPI_Waitall ( 2, req, status_array );
    fprintf( stdout, "[%d] Ending waitall\n", 0 );

    for (i=16; i<257000; i *= 2) {
	longsend = (int *)malloc( i * sizeof(int) );
	longrecv = (int *)malloc( i * sizeof(int) );
	if (!longsend || !longrecv) {
	}
	if ((mpi_errno = MPI_Irecv ( longrecv, i, MPI_INT, source, recvtag, 
				     MPI_COMM_WORLD, &req[1] ))) 
	    return mpi_errno;
	if ((mpi_errno = MPI_Isend ( longsend, i, MPI_INT, dest,  sendtag, 
				     MPI_COMM_WORLD, &req[0] ))) 
	return mpi_errno;
	
	fprintf( stdout, "[%d] Starting waitall (%d)\n", 0, i );
	mpi_errno = MPI_Waitall ( 2, req, status_array );
	fprintf( stdout, "[%d] Ending waitall\n", 0 );

	free( longsend );
	free( longrecv );
    }

    MPI_Finalize();
    return (mpi_errno);
}
