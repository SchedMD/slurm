#include "mpi.h"
#include <stdio.h>
#include "test.h"

/* 
 * Test that receives are done by relative rank, and that the status value
 * contains the relative rank
 */
int main( int argc, char **argv )
{
    int rank, new_world_rank, size, order, errcnt = 0, i;
    int tmpint = 0;
    MPI_Comm new_world;
    MPI_Status s;

    MPI_Init(&argc,&argv);

    MPI_Comm_rank(MPI_COMM_WORLD,&rank);
    MPI_Comm_size(MPI_COMM_WORLD,&size);

    order = size - rank - 1;
    MPI_Comm_split(MPI_COMM_WORLD, 0, order, &new_world);
	
    MPI_Comm_rank ( new_world, &new_world_rank );

    /* Make sure that the split worked correctly */
    if (new_world_rank != order) {
	errcnt ++;
	fprintf( stderr, "Comm split did not properly order ranks!\n" );
    }
    if (new_world_rank==0) {
	MPI_Send(&tmpint, 1, MPI_INT, 1, 0, new_world);
	/* printf("%d(%d): Sent message to: %d\n", new_world_rank, rank, 1); */
    }
    else if (new_world_rank == 1) {
	MPI_Recv(&tmpint, 1, MPI_INT, 0, 0, new_world,&s);
	if (s.MPI_SOURCE != 0) {
	    errcnt++;
	    fprintf( stderr, 
		     "Source incorrect in recv status (%d should be %d)\n",
		     s.MPI_SOURCE, 0 );
	}
	/*
	  printf("%d(%d): Recv message from: -> %d(%d) <- these 2 should equal\n", 
	  new_world_rank, rank, 0, s.MPI_SOURCE); 
	  */
    }

    MPI_Comm_free( &new_world );
    i = errcnt;
    MPI_Allreduce( &i, &errcnt, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD );
    if (errcnt > 0) {
	printf( "Found %d errors in the run\n", errcnt );
    }
    Test_Waitforall( );
    MPI_Finalize();
    return 0;
}
