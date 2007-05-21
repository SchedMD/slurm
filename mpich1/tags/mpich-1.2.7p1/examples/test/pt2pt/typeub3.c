/* This test checks that all of the MPI Type routines correctly compute 
   the UB and LB of a datatype from the greatest/least instance */

#include "mpi.h"
#include <stdio.h>

#if defined(NEEDS_STDLIB_PROTOTYPES)
#include "protofix.h"
#endif
 
int main( int argc, char *argv[] )
{
    MPI_Datatype dt1, dt2, dt3, dt4, dt5;
    MPI_Aint     ex;
    int          sz;
    MPI_Aint     lb,ub;
    MPI_Aint     disp[3];
    MPI_Datatype types[3];
    int          blocklen[3];
    int          idisp[3];

    MPI_Init(&argc, &argv);

    /* Create a datatype with explicit LB and UB */
    blocklen[0] = 1;    blocklen[1] = 1;        blocklen[2] = 1;
    disp[0] = -3;       disp[1] = 0;            disp[2] = 6;
    types[0] = MPI_LB;  types[1] = MPI_INT;     types[2] = MPI_UB;
 
    /* Generate samples for contiguous, hindexed, hvector, indexed, 
       and vector (struct and contiguous tested in typeub2) */

    MPI_Type_struct(3,blocklen,disp, types,&dt1);
    MPI_Type_commit(&dt1);

    /* This type is the same as in typeub2, and is tested there */
    
    types[0]=dt1;               types[1]=dt1;
    blocklen[0]=1;              blocklen[1]=1;
    disp[0]=-4;                 disp[1]=7;
    idisp[0]=-4;                idisp[1]=7;

    MPI_Type_hindexed( 2, blocklen, disp, dt1, &dt2 );
    MPI_Type_commit( &dt2 );

        MPI_Type_lb( dt2, &lb );       MPI_Type_ub( dt2, &ub );
	MPI_Type_extent( dt2, &ex );   MPI_Type_size( dt2, &sz );

	if (lb != -7 || ub != 13 || ex != 20) {
	    printf("hindexed lb %d ub %d extent %d size %d\n", 
		   (int)lb, (int)ub, (int)ex, sz);
	}
	else 
	    printf( "hindexed ok\n" );

    MPI_Type_indexed( 2, blocklen, idisp, dt1, &dt3 );
    MPI_Type_commit( &dt3 );

        MPI_Type_lb( dt3, &lb );       MPI_Type_ub( dt3, &ub );
	MPI_Type_extent( dt3, &ex );   MPI_Type_size( dt3, &sz );

	if (lb != -39 || ub != 69 || ex != 108) {
	    printf("indexed lb %d ub %d extent %d size %d\n", 
		   (int)lb, (int)ub, (int)ex, sz);
	}
	else 
	    printf( "indexed ok\n" );

    MPI_Type_hvector( 2, 1, 14, dt1, &dt4 );
    MPI_Type_commit( &dt4 );

        MPI_Type_lb( dt4, &lb );       MPI_Type_ub( dt4, &ub );
	MPI_Type_extent( dt4, &ex );   MPI_Type_size( dt4, &sz );

	if (lb != -3 || ub != 20 || ex != 23) {
	    printf("hvector lb %d ub %d extent %d size %d\n", 
		   (int)lb, (int)ub, (int)ex, sz);
	}
	else 
	    printf( "hvector ok\n" );

    MPI_Type_vector( 2, 1, 14, dt1, &dt5 );
    MPI_Type_commit( &dt5 );
    
        MPI_Type_lb( dt5, &lb );       MPI_Type_ub( dt5, &ub );
	MPI_Type_extent( dt5, &ex );   MPI_Type_size( dt5, &sz );

 
	if (lb != -3 || ub != 132 || ex != 135) {
	    printf("vector lb %d ub %d extent %d size %d\n", 
		   (int)lb, (int)ub, (int)ex, sz);
	}
	else 
	    printf( "vector ok\n" );

    MPI_Type_free( &dt1 );
    MPI_Type_free( &dt2 );
    MPI_Type_free( &dt3 );
    MPI_Type_free( &dt4 );
    MPI_Type_free( &dt5 );

    MPI_Finalize();
    return 0;
}

