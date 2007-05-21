/* -----------------------------------------------------------------------
 * Code:   mismatch.c
 * Lab:    Parallel Processing Performance Tools
 * Usage:  mismatch
 *         Run on two nodes
 *         You will need to stop the deadlocked program with <ctrl>\
 * Author: Roslyn Leibensperger  Last revised: 3/19/97 RYL
 *
 * Modified by Bill Gropp (ANL) to use Iprobe to detect the message and 
 * always produce output (no need to abort a deadlocked program).  
 * Unfortunately(?), the version of POE that had this bug is no longer
 * available, so we can't test whether using Iprobe would show the same
 * problem.
 * ------------------------------------------------------------------------ */
#include <stdio.h>
#include "mpi.h"
#define MSGLEN 100            /* length of message in elements */
#define TAG_A 100
#define TAG_B 200

int main( int argc, char *argv[] ) 
{
  float message1 [MSGLEN],    /* message buffers                      */
        message2 [MSGLEN],
        message3 [MSGLEN];
  int rank,                   /* rank of task in communicator         */
      dest=0, source=0,       /* rank in communicator of destination  */
                              /* and source tasks                     */
      send_tag=0, recv_tag=0, /* message tags                         */
      flag, size, i;
  int errs = 0, toterrs;
  MPI_Status status;          /* status of communication              */
  MPI_Status statuses[2];
  MPI_Request requests[2];

  MPI_Init( &argc, &argv );
  MPI_Comm_rank( MPI_COMM_WORLD, &rank );
  MPI_Comm_size( MPI_COMM_WORLD, &size );
  if (size != 2) {
      printf( "Must run with exactly 2 processes\n" );
      MPI_Abort( MPI_COMM_WORLD, 1 );
  }
  /* printf ( " Task %d initialized\n", rank ); */

  /* initialize message buffers */
  for ( i=0; i<MSGLEN; i++ )  
    {
      message1[i] = 100;
      message2[i] = -100;
    }

  /* ---------------------------------------------------------------
   * each task sets its message tags for the send and receive, plus
   * the destination for the send, and the source for the receive 
   * --------------------------------------------------------------- */
  if ( rank == 0 )  
    {
      dest = 1;
      source = 1;
      send_tag = TAG_B;
      recv_tag = TAG_A;
  }
  else if ( rank == 1)  
    {
      dest = 0;
      source = 0;
      send_tag = TAG_B;
      recv_tag = TAG_A;
    }

  /* ---------------------------------------------------------------
   * send and receive messages 
   * --------------------------------------------------------------- */
  /*  printf ( " Task %d has sent the message\n", rank ); */
  MPI_Isend ( message1, MSGLEN, MPI_FLOAT, dest, send_tag, MPI_COMM_WORLD, &requests[0] );
  MPI_Irecv ( message2, MSGLEN, MPI_FLOAT, source, recv_tag, MPI_COMM_WORLD, &requests[1] );

  /* See if we can receive the message on COMM_SELF...
   * This should *never* be possible, but if TV is to be believed may happen
   * with POE 2.4
   */
  MPI_Iprobe( MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_SELF, &flag, &status );
  if (flag) {
      errs++;
      printf ( " Task %d has received the message on COMM_SELF !\n", rank );
  }

  MPI_Recv( message3, MSGLEN, MPI_FLOAT, source, send_tag, MPI_COMM_WORLD, 
	    &status );
  MPI_Send( message3, MSGLEN, MPI_FLOAT, dest, recv_tag, MPI_COMM_WORLD );
  MPI_Waitall( 2, requests, statuses );
  MPI_Reduce( &errs, &toterrs, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD );
  if (rank == 0) {
      if (toterrs == 0) 
	  printf( "No errors\n" );
      else
	  printf( "Error in handling MPI_COMM_SELF\n" );
  }

  MPI_Finalize();
  return 0;
}


