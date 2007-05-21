#include "mpi.h"
#include <stdio.h>

int main( int argc, char *argv[] )
{
    int rank, size;
    MPI_Comm local_comm;
    MPI_Request r;
    MPI_Status status;
    double t0;


    MPI_Init( &argc, &argv );
    MPI_Comm_size( MPI_COMM_WORLD, &size );
    if (size < 3) {
	fprintf( stderr, "Need at least 3 processors\n" );
	MPI_Abort( MPI_COMM_WORLD, 1 );
    }
    
    MPI_Comm_rank( MPI_COMM_WORLD, &rank );
    MPI_Comm_split( MPI_COMM_WORLD, rank < 2, rank, &local_comm );

    MPI_Barrier( MPI_COMM_WORLD );
    if (rank == 0) {
	/* First, ensure ssend works */
	t0 = MPI_Wtime();
	MPI_Ssend( MPI_BOTTOM, 0, MPI_INT, 1, 1, MPI_COMM_WORLD );
	t0 = MPI_Wtime() - t0;
	if (t0 < 1.0) {
	    fprintf( stderr, "Ssend does not wait for recv!\n" );
	    fflush( stderr );
	    MPI_Abort( MPI_COMM_WORLD, 1 );
	}
	MPI_Barrier( MPI_COMM_WORLD );
	/* Start the ssend after process 1 is well into its barrier */
	t0 = MPI_Wtime();
	while (MPI_Wtime() - t0 < 1.0) ;
	MPI_Ssend( MPI_BOTTOM, 0, MPI_INT, 1, 0, MPI_COMM_WORLD );
	MPI_Barrier( local_comm );
	/* Send process 2 an alls well */
	MPI_Send( MPI_BOTTOM, 0, MPI_INT, 2, 0, MPI_COMM_WORLD );
    }
    else if (rank == 1) {
	t0 = MPI_Wtime();
	while (MPI_Wtime() - t0 < 2.0) ;
	MPI_Recv( MPI_BOTTOM, 0, MPI_INT, 0, 1, MPI_COMM_WORLD, &status );
	MPI_Barrier( MPI_COMM_WORLD );
	MPI_Irecv( MPI_BOTTOM, 0, MPI_INT, 0, 0, MPI_COMM_WORLD, &r );
	MPI_Barrier( local_comm );
	MPI_Wait( &r, &status );
    }
    else if (rank == 2) {
	int flag;

	MPI_Barrier( MPI_COMM_WORLD );
	MPI_Irecv( MPI_BOTTOM, 0, MPI_INT, 0, 0, MPI_COMM_WORLD, &r );
	t0 = MPI_Wtime();
	while (MPI_Wtime() - t0 < 3.0) ;
	MPI_Test( &r, &flag, &status );
	if (!flag) {
	    fprintf( stderr, "Test failed!\n" );
	    fflush( stderr );
	    MPI_Abort( MPI_COMM_WORLD, 1 );
	}
	else
	    fprintf( stderr, "Test succeeded\n" );
    }
    else {
	MPI_Barrier( MPI_COMM_WORLD );
    }

    MPI_Comm_free( &local_comm );
    MPI_Finalize();
    return 0;
}
