#include "mpi.h"
#include <stdio.h>
#include "test.h"

/* This test makes sure that the ordering if reorder is FALSE is
   as specified in 6.2, virtual topologies 

   At the same time, it duplicates the tests in cart.c, but
   with reorder = 0.
*/

#define NUM_DIMS 2

int main( int argc, char **argv )
{
    int              rank, size, i;
    int              errors=0;
    int              dims[NUM_DIMS];
    int              periods[NUM_DIMS];
    int              coords[NUM_DIMS];
    int              new_coords[NUM_DIMS];
    int              reorder = 0;
    MPI_Comm         comm_temp, comm_cart, new_comm;
    int              topo_status;
    int              ndims;
    int              new_rank;
    int              remain_dims[NUM_DIMS];
    int              newnewrank;

    MPI_Init( &argc, &argv );

    MPI_Comm_rank( MPI_COMM_WORLD, &rank );
    MPI_Comm_size( MPI_COMM_WORLD, &size );

    /* Clear dims array and get dims for topology */
    for(i=0;i<NUM_DIMS;i++) { dims[i] = 0; periods[i] = 0; }
    MPI_Dims_create ( size, NUM_DIMS, dims );

    /* Make a new communicator with a topology */
    MPI_Cart_create ( MPI_COMM_WORLD, 2, dims, periods, reorder, &comm_temp );
    MPI_Comm_dup ( comm_temp, &comm_cart );

    /* Determine the status of the new communicator */
    MPI_Topo_test ( comm_cart, &topo_status );
    if (topo_status != MPI_CART) {
	printf( "topo_status of duped comm is not MPI_CART\n" );
	errors++;
    }

    /* How many dims do we have? */
    MPI_Cartdim_get( comm_cart, &ndims );
    if ( ndims != NUM_DIMS ) {
	printf( "Number of dims of duped comm (%d) should be %d\n", 
		ndims, NUM_DIMS );
	errors++;
    }

    /* Get the topology, does it agree with what we put in? */
    for(i=0;i<NUM_DIMS;i++) { dims[i] = 0; periods[i] = 0; }
    MPI_Cart_get ( comm_cart, NUM_DIMS, dims, periods, coords );

    /* Check that the coordinates are correct */
#if NUM_DIMS == 2
    if (rank != coords[1] + coords[0] * dims[1]) {
	errors++;
	fprintf( stderr, 
"Did not get expected coordinate (row major required by MPI standard 6.2)\n" );
    }
#endif
    /* Does the mapping from coords to rank work? */
    MPI_Cart_rank ( comm_cart, coords, &new_rank );
    if ( new_rank != rank ) {
	printf( "New rank of duped comm (%d) != old rank (%d)\n", 
		new_rank, rank );
	errors++;
    }

    /* Does the mapping from rank to coords work */
    MPI_Cart_coords ( comm_cart, rank, NUM_DIMS, new_coords );
    for (i=0;i<NUM_DIMS;i++) 
	if ( coords[i] != new_coords[i] ) {
	    printf( "Old coords[%d] of duped comm (%d) != new_coords (%d)\n", 
		    i, coords[i], new_coords[i] );
	    errors++;
	}

    /* Let's shift in each dimension and see how it works!   */
    /* Because it's late and I'm tired, I'm not making this  */
    /* automatically test itself.                            */
    for (i=0;i<NUM_DIMS;i++) {
      int source, dest;
      MPI_Cart_shift(comm_cart, i, 1, &source, &dest);
#ifdef VERBOSE      
      printf ("[%d] Shifting %d in the %d dimension\n",rank,1,i);
      printf ("[%d]    source = %d  dest = %d\n",rank,source,dest); 
#endif
    }

    /* Subdivide */
    remain_dims[0] = 0; 
    for (i=1; i<NUM_DIMS; i++) remain_dims[i] = 1;
    MPI_Cart_sub ( comm_cart, remain_dims, &new_comm );

    /* Determine the status of the new communicator */
    MPI_Topo_test ( new_comm, &topo_status );
    if (topo_status != MPI_CART) {
	printf( "topo_status of cartsub comm is not MPI_CART\n" );
	errors++;
    }

    /* How many dims do we have? */
    MPI_Cartdim_get( new_comm, &ndims );
    if ( ndims != NUM_DIMS-1 ) {
	printf( "Number of dims of cartsub comm (%d) should be %d\n", 
		ndims, NUM_DIMS-1 );
	errors++;
    }

    /* Get the topology, does it agree with what we put in? */
    for(i=0;i<NUM_DIMS-1;i++) { dims[i] = 0; periods[i] = 0; }
    MPI_Cart_get ( new_comm, ndims, dims, periods, coords );
    
    /* Does the mapping from coords to rank work? */
    MPI_Comm_rank ( new_comm, &newnewrank );
    MPI_Cart_rank ( new_comm, coords, &new_rank );
    if ( new_rank != newnewrank ) {
	printf( "New rank of cartsub comm (%d) != old rank (%d)\n", 
		new_rank, newnewrank );
	errors++;
    }
    /* Does the mapping from rank to coords work */
    MPI_Cart_coords ( new_comm, new_rank, NUM_DIMS -1, new_coords );
    for (i=0;i<NUM_DIMS-1;i++) 
	if ( coords[i] != new_coords[i] ) {
	    printf( "Old coords[%d] of cartsub comm (%d) != new_coords (%d)\n", 
		    i, coords[i], new_coords[i] );
	    errors++;
	}

    /* We're at the end */
    MPI_Comm_free( &new_comm );
    MPI_Comm_free( &comm_temp );
    MPI_Comm_free( &comm_cart );
    Test_Waitforall( );
    if (errors) printf( "[%d] done with %d ERRORS!\n", rank,errors );
    MPI_Finalize();
    return 0;
}
