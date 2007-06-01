/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* ictest2.c 
   This is like ictest.c, but it creates communictors that are valid only
   at the "leaders"; other members of the local communicator are NOT
   in the remote communicator.  This is done by creating two communicators:
   0, + odd rank and even rank.  Only 0 is in in both communicators.

   This test originally tested the part of the standard that allowed the 
   leader to be in both groups.  This has been disallowed.  This test was
   recently changed to operate correctly under the new definition.

   Note that it generates unordered printf output, and is not suitable for
   automated testing.
 */
#include "mpi.h"
#include <stdio.h>
#include "test.h"

int verbose = 0;

int main( int argc, char **argv )
{
  int size, rank, key, lrank, rsize, result, remLeader = 0;
  MPI_Comm myComm;
  MPI_Comm myFirstComm;
  MPI_Comm mySecondComm;
  MPI_Comm evenComm, oddComm, remComm;
  int errors = 0, sum_errors;
  MPI_Status status;
  
  /* Initialization */
  MPI_Init ( &argc, &argv );
  MPI_Comm_rank ( MPI_COMM_WORLD, &rank);
  MPI_Comm_size ( MPI_COMM_WORLD, &size);

  /* Only works for 2 or more processes */
  if (size >= 2) {
    MPI_Comm merge1, merge2, merge3, merge4;

    /* Generate membership key in the range [0,1] */
    key = rank % 2;
    /* Create the even communicator */
    MPI_Comm_split ( MPI_COMM_WORLD, key, rank, &evenComm );
    if (key == 1) {
	/* Odd rank communicator discarded */
	MPI_Comm_free( &evenComm );
    }
    
    /* Create the odd communicator */
    MPI_Comm_split ( MPI_COMM_WORLD, key, rank, &oddComm );
    if (key == 0) {
	/* Even rank communicator discarded */
	MPI_Comm_free( &oddComm );
    }

    /* Create the odd + 0 communicator */
    if (rank == 0) key = 1;
    MPI_Comm_split( MPI_COMM_WORLD, key, rank, &remComm );
    if (key == 0) {
	/* Even rank communicator discarded */
	MPI_Comm_free( &remComm );
    }
    else {
	MPI_Comm_rank( remComm, &lrank );
	if (verbose) {
	    printf( "[%d] lrank in remComm is %d (color = %d, key=%d)\n", 
		    rank, lrank, rank, key );
	}
	remLeader = (lrank == 0) ? 1 : 0;
    }
    /* Now, choose the local and remote communicators */
    if (rank % 2) {
	/* Odd */
	myComm  = oddComm;
    }
    else {
	myComm  = evenComm;
    }

    /* Check that the leader is who we think he is */
    MPI_Comm_rank( myComm, &lrank );
    if (verbose) {
	printf( "[%d] local rank is %d\n", rank, lrank );
    }
    if (rank == 0) {
	int trank;
	MPI_Comm_rank( myComm, &trank );
	if (trank != 0) {
	    printf( "[%d] Comm split improperly ordered group (myComm)\n",
		    rank );
	    fflush(stdout);
	    errors++;
	}
	MPI_Comm_rank( remComm, &trank );
	if (trank != 0) {
	    printf( "[%d] Comm split improperly ordered group (remComm)\n",
		    rank );
	    fflush(stdout);
	    errors++;
	}
    }
    /* Perform the intercomm create and test it */
    /* local leader is first process in local_comm, i.e., has rank 0 */
    /* remote leader is process 0 (if odd) or 1 (if even) in remComm */
    MPI_Intercomm_create (myComm, 0, remComm, remLeader, 1, &myFirstComm );
/* temp */
    if (verbose) {
	printf( "[%d] through intercom create\n", rank );
	fflush( stdout );
    }
    MPI_Barrier( MPI_COMM_WORLD );
    if (verbose) {
	printf( "[%d] through barrier at end of intercom create\n", rank );
	fflush( stdout );
    }
/* temp */

    /* Try to dup this communicator */
    MPI_Comm_dup ( myFirstComm, &mySecondComm );

/* temp */
    if (verbose) {
	printf( "[%d] through comm dup\n", rank );
	fflush( stdout );
    }
    MPI_Barrier( MPI_COMM_WORLD );
    if (verbose) {
	printf( "[%d] through barrier at end of comm dup\n", rank );
	fflush( stdout );
    }
/* temp */

    /* Each member shares data with his "partner".  Note that process 0 in
       MPI_COMM_WORLD is sending to itself, since it is process 0 in both
       remote groups */
    MPI_Comm_rank( mySecondComm, &lrank );
    MPI_Comm_remote_size( mySecondComm, &rsize );

    if (verbose) {
	printf( "[%d] lrank in secondcomm is %d and remote size is %d\n", 
		rank, lrank, rsize );
	fflush( stdout );
    }

    /* Send key * size + rank in communicator */
    if (lrank < rsize) {
      int myval, hisval;
      key     = rank % 2;
      myval   = key * size + lrank;
      hisval  = -1;
      if (verbose) {
	  printf( "[%d] exchanging %d with %d in intercomm\n", 
		  rank, myval, lrank );
	  fflush( stdout );
      }
      MPI_Sendrecv (&myval,  1, MPI_INT, lrank, 0,
                    &hisval, 1, MPI_INT, lrank, 0, mySecondComm, &status);
      if (hisval != (lrank + (!key)*size)) {
	  printf( "[%d] expected %d but got %d\n", rank, lrank + (!key)*size,
		  hisval );
	  errors++;
	  }
      }
    
    if (errors) {
	printf("[%d] Failed!\n",rank);
	fflush(stdout);
    }

    /* Key is 1 for oddComm, 0 for evenComm (note both contain 0 in WORLD) */
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
    MPI_Comm_free( &myComm );
    /* remComm may have been freed above */
    if (remComm != MPI_COMM_NULL) 
	MPI_Comm_free( &remComm );
    MPI_Comm_free( &myFirstComm );
    MPI_Comm_free( &mySecondComm );
    MPI_Comm_free( &merge1 );
    MPI_Comm_free( &merge2 );
    MPI_Comm_free( &merge3 );
    MPI_Comm_free( &merge4 );
  }
  else {
      printf("[%d] Failed - at least 2 nodes must be used\n",rank);
  }

  MPI_Barrier( MPI_COMM_WORLD );
  MPI_Allreduce( &errors, &sum_errors, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD );
  if (sum_errors > 0) {
      printf( "%d errors on process %d\n", errors, rank );
      }
  else if (rank == 0) {
      printf( " No Errors\n" );
      }

  MPI_Finalize();
  return 0;
}
