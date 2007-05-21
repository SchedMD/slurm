#include "mpi.h"
#include <stdio.h>

int main( int argc, char **argv)
{
    int blockcnt[2], size;
    MPI_Datatype tmptype, newtype, oldtypes[2];
    MPI_Aint offsets[2], extent, lb, ub;

    MPI_Init(&argc, &argv);

    blockcnt[0] = 1;
    offsets[0] = 1;
    oldtypes[0] = MPI_BYTE;
    blockcnt[1] = 1;	/* set upper bound to avoid padding */
    offsets[1] = 2;
    oldtypes[1] = MPI_UB;
    MPI_Type_struct(2, blockcnt, offsets, oldtypes, &tmptype);
    MPI_Type_commit(&tmptype);

    MPI_Type_size(tmptype, &size);
    MPI_Type_lb(tmptype, &lb);
    MPI_Type_ub(tmptype, &ub);
    MPI_Type_extent(tmptype, &extent);
#ifdef DEBUG
    printf("tmptype: size: %d lb: %d ub: %d ex: %d\n", size, lb, ub, 
	   extent);
#endif
	
    blockcnt[0] = 1;
    offsets[0] = 1;
    oldtypes[0] = tmptype;
    MPI_Type_struct(1, blockcnt, offsets, oldtypes, &newtype);
    MPI_Type_commit(&newtype);

    MPI_Type_size(newtype, &size);
    MPI_Type_lb(newtype, &lb);
    MPI_Type_ub(newtype, &ub);
    MPI_Type_extent(newtype, &extent);
#ifdef DEBUG
    printf("newtype: size: %d lb: %d ub: %d ex: %d\n", size, lb, ub, 
	   extent);
#endif	
    if (size != 1 || lb != 2 || ub != 3 || extent != 1) {
	    printf ("lb = %d (should be 2), ub = %d (should be 3) extent = %d should be 1, size = %d (should be 1)\n", lb, ub, extent, size) ;
    }
    else {
	printf( " No Errors\n" );
    }
    MPI_Type_free(&tmptype);
    MPI_Type_free(&newtype);
    MPI_Finalize();

    return 0;
}
