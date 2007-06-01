/*
 * This program performs some simple tests of the MPI_Bcast broadcast
 * functionality.
 */

#include "test.h"
#include "mpi.h"
#include <stdlib.h>

int
main( int argc, char **argv)
{
    int rank, size, ret, passed, i, *test_array;

    /* Set up MPI */
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    /* Setup the tests */
    Test_Init("bcast", rank);
    test_array = (int *)malloc(size*sizeof(int));

    /* Perform the test - this operation should really be done
       with an allgather, but it makes a good test... */
    passed = 1;
    for (i=0; i < size; i++) {
	if (i == rank)
	    test_array[i] = i;
	MPI_Bcast(test_array, size, MPI_INT, i, MPI_COMM_WORLD);
	if (test_array[i] != i)
	    passed = 0;
    }
    if (!passed)
	Test_Failed("Simple Broadcast test");
    else {
	if (rank == 0)
	    Test_Passed("Simple Broadcast test");
	}

    /* Close down the tests */
    free(test_array);
    if (rank == 0)
	ret = Summarize_Test_Results();
    else
	ret = 0;
    Test_Finalize();

    /* Close down MPI */
    Test_Waitforall( );
    MPI_Finalize();
    return ret;
}
