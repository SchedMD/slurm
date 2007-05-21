#include "mpi.h"
#include <stdio.h>

#if defined(NEEDS_STDLIB_PROTOTYPES)
#include "protofix.h"
#endif
 
int main( int argc, char *argv[] )
{
    MPI_Datatype dt1, dt2, dt3;
    MPI_Aint     ex1, ex2, ex3;
    int          sz1, sz2, sz3;
    MPI_Aint     lb,ub;
    MPI_Aint     disp[3];
    MPI_Datatype types[3];
    int          blocklen[3];
 
    MPI_Init(&argc, &argv);
 
    blocklen[0] = 1;    blocklen[1] = 1;        blocklen[2] = 1;
    disp[0] = -3;       disp[1] = 0;            disp[2] = 6;
    types[0] = MPI_LB;  types[1] = MPI_INT;     types[2] = MPI_UB;
 
    MPI_Type_struct(3,blocklen,disp, types,&dt1);
    MPI_Type_commit(&dt1);
 
        MPI_Type_lb(dt1, &lb);          MPI_Type_ub(dt1, &ub);
        MPI_Type_extent(dt1,&ex1);      MPI_Type_size(dt1,&sz1);

	/* Values should be lb = -3, ub = 6 extent 9; 
	   size depends on implementation */
	if (lb != -3 || ub != 6 || ex1 != 9) {
	    printf("Example 3.26 type1 lb %d ub %d extent %d size %d\n",
		   (int)lb, (int)ub, (int)ex1, sz1);
	}
	else 
	    printf("Example 3.26 type1 correct\n" );
 
    MPI_Type_contiguous(2,dt1,&dt2);
        MPI_Type_lb(dt2, &lb);          MPI_Type_ub(dt2, &ub);
        MPI_Type_extent(dt2,&ex2);      MPI_Type_size(dt2,&sz2);
	/* Values should be lb = -3, ub = 15, extent = 18, size
	   depends on implementation */
	if (lb != -3 || ub != 15 || ex2 != 18) {
	    printf("Example 3.26 type2 lb %d ub %d extent %d size %d\n", 
		   (int)lb, (int)ub, (int)ex2, sz2);
	}
	else 
	    printf( "Example 3.26 type2 correct\n" );
 
    types[0]=dt1;               types[1]=dt1;
    blocklen[0]=1;              blocklen[1]=1;
    disp[0]=0;                  disp[1]=ex1;
 
    MPI_Type_struct(2, blocklen, disp, types, &dt3);
    MPI_Type_commit(&dt3);
 
        MPI_Type_lb(dt3, &lb);          MPI_Type_ub(dt3, &ub);
        MPI_Type_extent(dt3,&ex3);      MPI_Type_size(dt3,&sz3);
	/* Another way to express type2 */
	if (lb != -3 || ub != 15 || ex3 != 18) {
	    printf("type3 lb %d ub %d extent %d size %d\n", 
		   (int)lb, (int)ub, (int)ex3, sz2);
	}
	else 
	    printf( "type3 correct\n" );
 
    MPI_Type_free( &dt1 );
    MPI_Type_free( &dt2 );
    MPI_Type_free( &dt3 );
	
    MPI_Finalize();
    return 0;
}

