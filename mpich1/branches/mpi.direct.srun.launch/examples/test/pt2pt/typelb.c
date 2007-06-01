#include "mpi.h"
#include <stdio.h>

int
main( int argc, char **argv)
{
    int blockcnt[2], rank;
    MPI_Aint offsets[2], lb, ub, extent;
    MPI_Datatype tmp_type, newtype;

    MPI_Init(&argc, &argv);

    MPI_Comm_rank( MPI_COMM_WORLD, &rank );
    if (rank == 0) {
	blockcnt[0] = 1;
	offsets[0] = 3;
	MPI_Type_hindexed(1, blockcnt, offsets, MPI_BYTE, &tmp_type);
	blockcnt[0] = 1;
	offsets[0] = 1;
	MPI_Type_hindexed(1, blockcnt, offsets, tmp_type, &newtype);
	MPI_Type_commit(&newtype);
	
	MPI_Type_lb(newtype, &lb);
	MPI_Type_extent(newtype, &extent);
	MPI_Type_ub(newtype, &ub);
	
	/* Check that the results are correct */
#ifdef DEBUG
	printf("lb=%ld, ub=%ld, extent=%ld\n", lb, ub, extent);
	printf("Should be lb=4, ub=5, extent=1\n");
#endif
	if (lb != 4 || ub != 5 || extent != 1) {
	    printf ("lb = %d (should be 4), ub = %d (should be 5) extent = %d should be 1\n", lb, ub, extent) ;
	}
	else {
	    printf( " No Errors\n" );
	}

	MPI_Type_free(&tmp_type);
	MPI_Type_free(&newtype);

    }
    MPI_Finalize();
    return 0;
}
