/* ictest3.c 
   This is like ictest2.c, but it creates communictors that are valid only
   at the "leaders"; other members of the local communicator are NOT
   in the remote communicator.  A peer communicator is constructed that
   contains both leaders.

   
 */
#include "mpi.h"
#include <stdio.h>
#include "test.h"

/* #define DEBUG */

int verbose = 0;

int main( int argc, char **argv )
{
  int size, rank, key, lrank, rsize, result;
  MPI_Comm myFirstComm;
  MPI_Comm mySecondComm;
  MPI_Comm newComm, peerComm;
  MPI_Group rgroup, lgroup, igroup;
  int errors = 0, sum_errors;
  int flag;
  MPI_Status status;
  
  /* Initialization */
  MPI_Init ( &argc, &argv );
  MPI_Comm_rank ( MPI_COMM_WORLD, &rank);
  MPI_Comm_size ( MPI_COMM_WORLD, &size);

  /* Only works for 2 or more processes */
  /* 
     We create an even and odd communicator, then create an
     intercommunicator out of them.  For this purpose, we use a 
     "peer" communicator valid only at one member of each of the odd and
     even communicators.
   */
  if (size >= 2) {
    MPI_Comm merge1, merge2, merge3, merge4;

    /* Generate membership key in the range [0,1] */
    key = rank % 2;
    /* Create the even communicator and odd communicators */
    MPI_Comm_split ( MPI_COMM_WORLD, key, rank, &newComm );

    MPI_Comm_test_inter( newComm, &flag );
    if (flag) {
	errors++;
	printf( "[%d] got test_inter gave true for intra comm\n", rank );
	}

    /* Create the "peer" communicator */
    key = 0;
    if (rank < 2) key = 1;
    MPI_Comm_split( MPI_COMM_WORLD, key, rank, &peerComm );
    if (key == 0) {
	MPI_Comm_free( &peerComm );
	}
#ifdef DEBUG
    else {
	MPI_Comm_rank( peerComm, &lrank );
	printf( "[%d] lrank in peerComm is %d (color = %d, key=%d)\n", 
	        rank, lrank, key, rank );
	}
#endif

    /* Check that the leader is who we think he is */
    MPI_Comm_rank( newComm, &lrank );
    /* printf( "[%d] local rank is %d\n", rank, lrank );
    fflush(stdout); */
    /* Perform the intercomm create and test it */
    /* Local leader is always the one at rank 0.  */
    /* If even, the remote leader is rank 1, if odd, the remote leader
       is rank 0 in the peercomm */
    MPI_Intercomm_create (newComm, 0, peerComm, !(rank % 2), 1, &myFirstComm );
#ifdef DEBUG
    printf( "[%d] through intercom create\n", rank );
    fflush( stdout );
    MPI_Barrier( MPI_COMM_WORLD );
    printf( "[%d] through barrier at end of intercom create\n", rank );
#endif
    MPI_Comm_test_inter( myFirstComm, &flag );
    if (!flag) {
	errors++;
	printf( "[%d] got test_inter gave false for inter comm\n", rank );
	}

    /* Try to dup this communicator */
    MPI_Comm_dup ( myFirstComm, &mySecondComm );
    MPI_Comm_test_inter( mySecondComm, &flag );
    if (!flag) {
	errors++;
	printf( "[%d] got test_inter gave false for dup of inter comm\n", 
	        rank );
	}

#ifdef DEBUG
    printf( "[%d] through comm dup\n", rank );
    fflush( stdout );
    MPI_Barrier( MPI_COMM_WORLD );
    printf( "[%d] through barrier at end of comm dup\n", rank );
#endif

    /* Each member shares data with his "partner".  */
    MPI_Comm_rank( mySecondComm, &lrank );
    MPI_Comm_remote_size( mySecondComm, &rsize );

#ifdef DEBUG
    printf( "[%d] lrank in secondcomm is %d and remote size is %d\n", 
	   rank, lrank, rsize );
    fflush( stdout ); 
#endif

    /* Check that the remote group is what we think */
    MPI_Comm_remote_group( mySecondComm, &rgroup );
    MPI_Comm_group( newComm, &lgroup );
    MPI_Group_intersection( rgroup, lgroup, &igroup );
    MPI_Group_compare( igroup, MPI_GROUP_EMPTY, &flag );
    if (flag != MPI_IDENT) {
	errors++;
	printf( "[%d] intersection of remote and local group is not empty\n",
	        rank );
	}
    MPI_Group_free( &rgroup );
    MPI_Group_free( &lgroup );
    MPI_Group_free( &igroup );

    /* Send key * size + rank in communicator */
    if (lrank < rsize) {
      int myval, hisval;
      key = rank % 2;
      myval   = key * size + lrank;
      hisval  = -1;
#ifdef DEBUG
      printf( "[%d] exchanging %d with %d in intercomm\n", 
	     rank, myval, lrank );
      fflush( stdout );
#endif
      MPI_Sendrecv (&myval,  1, MPI_INT, lrank, 0, 
                    &hisval, 1, MPI_INT, lrank, 0, mySecondComm, &status);
      if (hisval != (lrank + (!key)*size)) {
	  printf( "[%d] expected %d but got %d\n", rank, lrank + (!key)*size,
		  hisval );
	  errors++;
	  }
      }
    
    if (errors)
      printf("[%d] Failed!\n",rank);

    /* Key is 1 for oddComm, 0 for evenComm (note both contain 0 in WORLD) */
#ifdef DEBUG
    printf( "[%d] starting intercom merge\n", rank );
    fflush( stdout );
#endif
    MPI_Intercomm_merge ( mySecondComm, key, &merge1 );
    MPI_Intercomm_merge ( mySecondComm, (key+1)%2, &merge2 );
    MPI_Intercomm_merge ( mySecondComm, 0, &merge3 );
    MPI_Intercomm_merge ( mySecondComm, 1, &merge4 );

    MPI_Comm_compare( merge1, MPI_COMM_WORLD, &result );
    if (result != MPI_SIMILAR && size > 2) {
	printf( "[%d] comparision with merge1 failed\n", rank );
	errors++;
	}

    /* Free communicators */
    if (verbose) printf( "about to free communicators\n" );
    MPI_Comm_free( &newComm );
    if (peerComm != MPI_COMM_NULL) MPI_Comm_free( &peerComm );
    MPI_Comm_free( &myFirstComm );
    MPI_Comm_free( &mySecondComm );
    MPI_Comm_free( &merge1 );
    MPI_Comm_free( &merge2 );
    MPI_Comm_free( &merge3 );
    MPI_Comm_free( &merge4 );
  }
  else 
    printf("[%d] Failed - at least 2 nodes must be used\n",rank);

  MPI_Barrier( MPI_COMM_WORLD );
  MPI_Allreduce( &errors, &sum_errors, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD );
  if (sum_errors > 0) {
      printf( "%d errors on process %d\n", errors, rank );
      }
  else if (rank == 0) {
      printf( " No Errors\n" );
      }
  /* Finalize and end! */

  MPI_Finalize();
  return 0;
}
