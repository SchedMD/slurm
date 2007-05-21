#include <stdio.h>
#include "mpi.h"
#include "test.h"

int main( int argc, char **argv)
{
    int    err = 0;
    void *v;
    int  flag;
    int  vval;
    int  rank, size;

    MPI_Init( &argc, &argv );
    MPI_Comm_size( MPI_COMM_WORLD, &size );
    MPI_Comm_rank( MPI_COMM_WORLD, &rank );
    MPI_Attr_get( MPI_COMM_WORLD, MPI_TAG_UB, &v, &flag );
    if (!flag || (vval = *(int*)v)< 32767) {
	err++;
	fprintf( stderr, "Could not get TAG_UB or got too-small value\n" );
    }
    MPI_Attr_get( MPI_COMM_WORLD, MPI_HOST, &v, &flag );
    vval = *(int*)v;
    if (!flag || ((vval < 0 || vval >= size) && vval != MPI_PROC_NULL)) {
	err++;
	fprintf( stderr, "Could not get HOST or got invalid value\n" );
    }
    MPI_Attr_get( MPI_COMM_WORLD, MPI_IO, &v, &flag );
    vval = *(int*)v;
    if (!flag || ((vval < 0 || vval >= size) && vval != MPI_ANY_SOURCE &&
		  vval != MPI_PROC_NULL)) {
	err++;
	fprintf( stderr, "Could not get IO or got invalid value\n" );
    }
    MPI_Attr_get( MPI_COMM_WORLD, MPI_WTIME_IS_GLOBAL, &v, &flag );
    if (flag) {
	/* Wtime need not be set */
	vval = *(int*)v;
	if (vval < 0 || vval > 1) {
	    err++;
	    fprintf( stderr, "Invalid value for WTIME_IS_GLOBAL (got %d)\n", 
		     vval );
	}
    }
    Test_Waitforall( );
    MPI_Finalize( );
    
    return err;
}
