/* 
   Test ordering of messages that differ only in data

   sendorder [ -n number-of-sends ] [ -m length-of-long-sends ]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mpi.h"

/* Prototypes */
void delay( int );
void CheckStatus( MPI_Status *, int, int, int, int * );

/* 
   This is a delay to make sure that several messages are in the queue when 
   the MPI_Recv is called

   10ms delay for now.
*/
void delay( int ms )
{
  double t, deltat = ms * 0.001;
  t = MPI_Wtime();
  while (MPI_Wtime() - t < deltat) ;
}

void CheckStatus( MPI_Status *status, int tag, int src, int cnt, int *err )
{
  int n;
  
  if (status->MPI_TAG != tag && status->MPI_SOURCE != src) { 
    if (*err < 10) {
      fprintf( stdout, 
       "Error in message status! tag = %d and source = %d\n", status->MPI_TAG, 
		   status->MPI_SOURCE );
	}
    (void)*err++;
  }
  MPI_Get_count( status, MPI_INT, &n );
  if (n != cnt) {
    if (*err < 10) {
      fprintf( stdout, 
       "Error in message status!  length is %d and should be %d\n", n, cnt );
    }
    (void)*err++;
  }
}

int main( int argc, char *argv[] )
{
  int i, n, m, val, *buf;
  MPI_Status status;
  int src, dest, tag, err = 0, toterr;
  int rank, size;
  MPI_Comm comm;

  MPI_Init( &argc, &argv );

  n    = 1000;    /* Number of tests */
  comm = MPI_COMM_WORLD;
  tag  = 3;
  m    = 1000;    /* Size in ints of longer buffer */

  /* Check for options
   */
  argc--; argv++;
  while (argc > 0) {
    if (argv[0] && strcmp( argv[0], "-n" ) == 0) {
      argc++;
      n = atoi( argv[0] );
    }
    else if (argv[0] && strcmp( argv[0], "-m" ) == 0) {
      argc++;
      m = atoi( argv[0] );
    }
    argc--; argv++;
  }
  /* Ensure that everyone has the values */
  MPI_Bcast( &n, 1, MPI_INT, 0, MPI_COMM_WORLD );
  MPI_Bcast( &m, 1, MPI_INT, 0, MPI_COMM_WORLD );

  MPI_Comm_rank( comm, &rank );
  MPI_Comm_size( comm, &size );
  if (size < 2) {
    fprintf( stderr, "This program requires at least 2 processes\n" );
    MPI_Abort( MPI_COMM_WORLD, 1 );
  }
  src  = 0;
  dest = size - 1;

  /* Single Int */
  MPI_Barrier( comm );
  if (rank == src) {
    for (i=0; i<n; i++) {
      MPI_Send( &i, 1, MPI_INT, dest, tag, comm );
    }
  }
  else if (rank == dest) {
    for (i=0; i<n; i++) {
      delay( 10 );
      MPI_Recv( &val, 1, MPI_INT, src, tag, comm, &status );
      /* The messages are sent in order that matches the value of i; 
	 if they are not received in order, this will show up in the
	 value here */
      if (val != i) { 
	if (err < 10) {
	  fprintf( stdout, 
   "Error in message order (single int): message %d received when %d expected\n", val, i );
	}
	err++;
      }
      CheckStatus( &status, tag, src, 1, &err );
    }
  }

  /* Alternating message sizes */
  buf = (int *)malloc( m * sizeof(int) );
  if (!buf) {
    fprintf( stdout, "Could not allocate %d ints\n", m );
    MPI_Abort( MPI_COMM_WORLD, 1 );
  }
  for (i=0; i<m; i++) buf[i] = - i;

  MPI_Barrier( comm );
  if (rank == src) {
    for (i=0; i<n; i++) {
      buf[0] = i;
      MPI_Send( &i, 1, MPI_INT, dest, tag, comm );
      MPI_Send( buf, m, MPI_INT, dest, tag, comm );
    }
  }
  else if (rank == dest) {
    for (i=0; i<n; i++) {
      delay( 10 );
      MPI_Recv( &val, 1, MPI_INT, src, tag, comm, &status );
      if (val != i) { 
	if (err < 10) {
	  fprintf( stdout, 
   "Error in message order: message %d received when %d expected\n", val, i );
	}
	err++;
      }
      CheckStatus( &status, tag, src, 1, &err );

      MPI_Recv( buf, m, MPI_INT, src, tag, comm, &status );
      if (buf[0] != i) { 
	if (err < 10) {
	  fprintf( stdout, 
   "Error in message order: message buf[] %d received when %d expected\n", 
		   buf[0], i );
	}
	err++;
      }
      CheckStatus( &status, tag, src, m, &err );
    }
  }
  
  /* Finally error reporting: make sure that rank 0 reports the message */
  MPI_Allreduce( &err, &toterr, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD );
  MPI_Comm_rank( MPI_COMM_WORLD, &rank );
  if (rank == 0) {
    if (toterr) printf( "Found %d errors\n", toterr );
    else        printf( " No Errors\n" );
  }

  MPI_Barrier( MPI_COMM_WORLD );
  MPI_Finalize();
  return 0;
}

