#include "mpi.h"
#include <stdio.h>

/*
 * intended to be run with at least 3 procs
 */
int main(int argc, char ** argv)
{
    MPI_Comm new_intercomm;
    MPI_Comm new_comm;
    int my_rank, my_size;
    int rrank;
    int procA, procB;

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);
    MPI_Comm_size( MPI_COMM_WORLD, &my_size );

    if (my_size < 3) {
	printf( "This test requires at least 3 processes: only %d provided\n",
		my_size );
	MPI_Abort( MPI_COMM_WORLD, 1 );
    }
#ifdef DBG
	printf("%d: Entering main()\n", my_rank); fflush(stdout);
#endif
    /* pick one of the following two settings for procA,procB */

	/* uncomment these and program will work */
	/* procA = 0; procB = 2; */

	/* uncomment these and program will hang */
	procA = 1; procB = 2;
    /* The SGI implementation of MPI fails this test */
    if (my_rank == procA || my_rank == procB)
    {
	if (my_rank == procA)
	{
	    rrank = procB;
	}
	else
	{
	    rrank = procA;
	}
#ifdef DBG
	printf("%d: Calling MPI_Intercomm_create()\n", my_rank); fflush(stdout);
#endif
	MPI_Intercomm_create(MPI_COMM_SELF, 0, 
			    MPI_COMM_WORLD, rrank, 
			    0, &new_intercomm);

#ifdef DBG
	printf("%d: Calling MPI_Comm_dup()\n", my_rank); fflush(stdout);
#endif
	MPI_Comm_dup(new_intercomm, &new_comm);

	/* Free these new communicators */
	MPI_Comm_free( &new_comm );
	MPI_Comm_free( &new_intercomm );
    }

    MPI_Barrier( MPI_COMM_WORLD );
    if (my_rank == 0) {
	printf( " No Errors\n" );
    }
#ifdef DBG
    printf("%d: Calling MPI_Finalize()\n", my_rank); fflush(stdout);
#endif
    MPI_Finalize();
    return 0;
}
