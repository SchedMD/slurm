/*

  Exercise attribute routines.
  This version checks for correct behavior of the copy and delete functions
  on an attribute, particularly the correct behavior when the routine returns
  failure.

 */
#include <stdio.h>
#include "mpi.h"
#include "test.h"

int test_communicators ( void );
void abort_msg ( char *, int );
int copybomb_fn ( MPI_Comm, int, void *, void *, void *, int * );
int deletebomb_fn ( MPI_Comm, int, void *, void * );

int main( int argc, char **argv )
{
    MPI_Init( &argc, &argv );
    test_communicators();
    Test_Waitforall( );
    MPI_Finalize();
    return 0;
}

/* 
 * MPI 1.2 Clarification: Clarification of Error Behavior of 
 *                        Attribute Callback Functions 
 * Any return value other than MPI_SUCCESS is erroneous.  The specific value
 * returned to the user is undefined (other than it can't be MPI_SUCCESS).
 * Proposals to specify particular values (e.g., user's value) failed.
 */
/* Return an error as the value */
int copybomb_fn( MPI_Comm oldcomm, int keyval, void *extra_state,
		 void *attribute_val_in, void *attribute_val_out, int *flag)
{
/* Note that if (sizeof(int) < sizeof(void *), just setting the int
   part of attribute_val_out may leave some dirty bits
 */
    *flag = 1;
    return MPI_ERR_OTHER;
}

/* Set delete flag to 1 to allow the attribute to be deleted */
static int delete_flag = 0;
int deletebomb_fn( MPI_Comm comm, int keyval, void *attribute_val, 
		   void *extra_state)
{
    if (delete_flag) return MPI_SUCCESS;
    return MPI_ERR_OTHER;
}

void abort_msg( char *str, int code )
{
    fprintf( stderr, "%s, err = %d\n", str, code );
    MPI_Abort( MPI_COMM_WORLD, code );
}

int test_communicators( void )
{
    MPI_Comm dup_comm_world, d2;
    int world_rank, world_size, key_1;
    int err;
    MPI_Aint value;

    MPI_Comm_rank( MPI_COMM_WORLD, &world_rank );
    MPI_Comm_size( MPI_COMM_WORLD, &world_size );
    if (world_rank == 0) {
	printf( "*** Attribute copy/delete return codes ***\n" );
    }

    MPI_Comm_dup( MPI_COMM_WORLD, &dup_comm_world );
    MPI_Barrier( dup_comm_world );

    MPI_Errhandler_set( dup_comm_world, MPI_ERRORS_RETURN );

    value = - 11;
    if ((err=MPI_Keyval_create( copybomb_fn, deletebomb_fn, &key_1, &value )))
	abort_msg( "Keyval_create", err );

    err = MPI_Attr_put( dup_comm_world, key_1, (void *)world_rank );
    if (err) {
	printf( "Error with first put\n" );
    }

    err = MPI_Attr_put( dup_comm_world, key_1, (void *)(2*world_rank) );
    if (err == MPI_SUCCESS) {
	printf( "delete function return code was MPI_SUCCESS in put\n" );
    }

    /* Because the attribute delete function should fail, the attribute
       should *not be removed* */
    err = MPI_Attr_delete( dup_comm_world, key_1 );
    if (err == MPI_SUCCESS) {
	printf( "delete function return code was MPI_SUCCESS in delete\n" );
    }
    
    err = MPI_Comm_dup( dup_comm_world, &d2 );
    if (err == MPI_SUCCESS) {
	printf( "copy function return code was MPI_SUCCESS in dup\n" );
    }
    if (err && d2 != MPI_COMM_NULL) {
	printf( "dup did not return MPI_COMM_NULL on error\n" );
    }

    delete_flag = 1;
    MPI_Comm_free( &dup_comm_world );

    return 0;
}

