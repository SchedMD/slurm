/*
 * This is a test of Type_commit.  This checks to see if Type_commit
 * (or Type_struct) replaces a struct with a contiguous type, and
 * that that type is constructed correctly.
 */

#include "mpi.h"
#include <stdio.h>

#if defined(NEEDS_STDLIB_PROTOTYPES)
#include "protofix.h"
#endif

int main( int argc, char **argv )
{
    int          nsize, n2size;
    MPI_Aint     nlb, nub, n2lb, n2ub;
    MPI_Datatype ntype, n2type;
    MPI_Aint     displs[2];
    MPI_Datatype types[2];
    int          blockcounts[2];
    double       myarray[10];
    int          err = 0;

    MPI_Init( &argc, &argv );
    
    MPI_Address( &myarray[0], &displs[0] );
    MPI_Address( &myarray[3], &displs[1] );
    blockcounts[0] = 3;
    blockcounts[1] = 1;
    displs[1] = displs[1] - displs[0];
    displs[0] = 0;
    types[0] = MPI_DOUBLE;
    types[1] = MPI_DOUBLE;
    MPI_Type_struct( 2, blockcounts, displs, types, &ntype );
    MPI_Type_commit( &ntype );

    MPI_Type_size( ntype, &nsize );
    MPI_Type_lb( ntype, &nlb );
    MPI_Type_ub( ntype, &nub );

    if (nlb != 0) {
	err++;
	printf( "LB for struct is %d\n", (int)nlb );
    }
    if (nub != 4 * sizeof(double)) {
	err++;
	printf( "UB for struct is %d != %d\n", (int)nub, 
		4 * (int)sizeof(double) );
    }
    if (nsize != 4 * sizeof(double)) {
	err++;
	printf( "Size for struct %d != %d\n", nsize, 4 * (int)sizeof(double) );
    }

    MPI_Type_contiguous( 3, ntype, &n2type );
    MPI_Type_commit( &n2type );

    MPI_Type_size( n2type, &n2size );
    MPI_Type_lb( n2type, &n2lb );
    MPI_Type_ub( n2type, &n2ub );

    if (n2size != 3 * nsize) {
	err++;
	printf( "Size of contig type %d != %d\n", n2size, 3*nsize );
    }
    if (n2lb != 0) {
	err++;
	printf( "LB for contig is %d\n", (int)n2lb );
    }
    if (n2ub != 3 * nub) {
	err++;
	printf( "UB for contig %d != %d\n", (int)n2ub, 3 * (int)nub );
    }

    if (err) {
	printf( "Found %d errors\n", err );
    }
    else {
	printf( " No Errors\n" );
    }
    MPI_Type_free( &ntype );
    MPI_Type_free( &n2type );
    MPI_Finalize();
    return 0;
}
