/*
 * This program performs some simple tests of the MPI_Bcast broadcast
 * functionality.
 *
 * It checks the handling of different datatypes by different participants
 * (with matching type signatures, of course), as well as different
 * roots and communicators.
 */

#include "test.h"
#include "mpi.h"
#include <stdlib.h>

int main( int argc, char **argv )
{
    int rank, size, ret, passed, i, *test_array;
    int stride, count, root;
    MPI_Datatype newtype;
    MPI_Comm     comm = MPI_COMM_WORLD;

    /* Set up MPI */
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(comm, &rank);

    /* Setup the tests */
    Test_Init("bcastvec", rank);

    /* Allow for additional communicators */
    MPI_Comm_size(comm, &size);
    /* MPI_Comm_rank(comm, &rank); */
    stride = (rank + 1);
    test_array = (int *)malloc(size*stride*sizeof(int));

    /* Create the vector datatype EXCEPT for process 0 (vector of
       stride 1 is contiguous) */
    if (rank > 0) {
	count = 1;
        MPI_Type_vector( size, 1, stride, MPI_INT, &newtype);
        MPI_Type_commit( &newtype );
    }
    else {
	count = size;
	newtype = MPI_INT;
    }

    /* Perform the test.  Each process in turn becomes the root.
       After each operation, check that nothing has gone wrong */
    passed = 1;
    for (root = 0; root < size; root++) {
	/* Fill the array with -1 for unset, rank + i * size for set */
	for (i=0; i<size*stride; i++) test_array[i] = -1;
	if (rank == root) 
	    for (i=0; i<size; i++) test_array[i*stride] = rank + i * size;
	MPI_Bcast( test_array, count, newtype, root, comm );
	for (i=0; i<size; i++) {
	    if (test_array[i*stride] != root + i * size) {
		passed = 0;
	    }
	}
    }
    free(test_array);
    if (rank != 0) MPI_Type_free( &newtype );

    if (!passed)
	Test_Failed("Simple Broadcast test with datatypes");
    else {
	if (rank == 0)
	    Test_Passed("Simple Broadcast test with datatypes");
	}

    /* Close down the tests */
    if (rank == 0)
	ret = Summarize_Test_Results();
    else {
	ret = 0;
    }
    Test_Finalize();

    /* Close down MPI */
    Test_Waitforall( );
    MPI_Finalize();
    return ret;
}
