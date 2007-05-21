#include <stdio.h>
#include "mpi.h"
#include "test.h"

#ifdef USE_STDARG
void errfunc( MPI_Comm *, int *, ... );
#else
void errfunc( MPI_Comm *, int * );
#endif

/*
 * Test the reference count semantics of error handlers.
 */
int main( int argc, char *argv[] )
{
    MPI_Errhandler errhandler, olderrhandler;
    MPI_Comm       newcomm;
    int            rc, errcnt = 0;

    MPI_Init( &argc, &argv );

    MPI_Comm_dup( MPI_COMM_WORLD, &newcomm );
    MPI_Errhandler_create( errfunc, &errhandler );
    MPI_Errhandler_set( newcomm, errhandler );
    /* Once you set it, you should be able to free it */
    MPI_Errhandler_free( &errhandler );
    if (errhandler != MPI_ERRHANDLER_NULL) {
	printf( "Freed errhandler is not set to NULL\n" );
	errcnt++;
    }
    MPI_Errhandler_get( newcomm, &olderrhandler );
    MPI_Comm_free( &newcomm );

    /* olderrhandler should now be invalid.  Is it? */
    /* This test is based on an interpretation of the MPI standard that
       was subsequently overturned.  See the MPI-1.1 errata.  
       An Errhandler_get is similar to an MPI_Comm_group (having the 
       effect of creating a copy to the object). */
    MPI_Errhandler_set( MPI_COMM_WORLD, MPI_ERRORS_RETURN );
    rc = MPI_Errhandler_set( MPI_COMM_WORLD, olderrhandler );
    /* In the old interpretation, the test is !rc */
    if (rc) {
	printf( "Olderrhandler invalid after get and comm freed!\n" );
	errcnt ++;
    }

    if (errcnt) 
	printf( "Found %d errors!\n", errcnt );
    else
	printf( " No Errors\n" );

    MPI_Finalize( );
    return 0;
}

#if defined(USE_STDARG)
void errfunc( MPI_Comm *comm, int *err, ...)
#else
void errfunc( MPI_Comm *comm, int *err)
#endif
{
}
