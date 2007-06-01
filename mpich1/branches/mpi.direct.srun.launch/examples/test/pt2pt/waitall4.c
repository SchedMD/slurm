/*
  Test of waitall.  This makes sure that the requests in a wait can occur
  in any order.

  Run with 4 processes.  This checks for code that listens to a specified
  process.  This is similar to the test in waitall3, except that the 
  wait is on sends instead of receives.  Messages are sent by process 2 to
  processes 0 and 1.  Process 3 is uninvolved.
  */

#include <stdio.h>
#include <stdlib.h>
#include "mpi.h"

#if defined(NEEDS_STDLIB_PROTOTYPES)
#include "protofix.h"
#endif

void Pause( double );

void Pause( double sec )
{
  double t1 = MPI_Wtime();
  while (MPI_Wtime() - t1 < sec) ;
}

int main( int argc, char **argv )
{
    int size, rank, flag, i;
    int *buf1, *buf2, cnt;
    double t0;
    MPI_Status statuses[2];
    MPI_Request req[2];
    
    MPI_Init( &argc, &argv );
    MPI_Comm_size( MPI_COMM_WORLD, &size );
    MPI_Comm_rank( MPI_COMM_WORLD, &rank );

    if (size < 3) {
	printf( "This test requires at least 3 processors\n" );
	MPI_Abort( MPI_COMM_WORLD, 1 );
	return 1;
    }
  
    /* Large enough that almost certainly a rendezvous algorithm will be used
       by Issend.  buflimit.c will give you a more reliable value */
    cnt = 35000;

    /* Test:
     process 0                    process 1               process 2
                                                          Issend1
                                                          Issend0
     Barrier                      Barrier                 Barrier
     pause(2 sec)                 pause(2 sec)            pause(1 sec)
     irecv2                                               waitall
     test(2) for 5 secs
     sendrecv (process 1)         sendrecv(process0)
                                  recv2
     wait(2) if necessary

     If the test for Irecv2 never succeeds, then the waitall appears to be
     waiting for req1 first.  By using Issend, we can keep the program from
     hanging.
    */
    buf1 = (int *)malloc( cnt * sizeof(int) );
    buf2 = (int *)malloc( cnt * sizeof(int) );
    if (!buf1 || !buf2) {
	printf( "Could not allocate buffers of size %d\n", cnt );
	MPI_Abort( MPI_COMM_WORLD, 1 );
	return 1;
    }
    
    for (i=0; i<cnt; i++) {
	buf1[i] = i;
	buf2[i] = i;
    }

    MPI_Barrier( MPI_COMM_WORLD );
    if (rank == 0) {
	MPI_Barrier( MPI_COMM_WORLD );
	Pause( 2.0 );
	MPI_Irecv( buf2, cnt, MPI_INT, 2, 2, MPI_COMM_WORLD, &req[0] );
	t0 = MPI_Wtime();
	flag = 0;
	while (t0 + 5.0 > MPI_Wtime() && !flag) 
	    MPI_Test( &req[0], &flag, &statuses[0] );
	/* printf( "Test succeeded at %f with flag %d\n", MPI_Wtime()-t0, flag ); */
	/* Tell process 2 to go ahead */
	MPI_Sendrecv( MPI_BOTTOM, 0, MPI_BYTE, 1, 3, 
		      MPI_BOTTOM, 0, MPI_BYTE, 1, 3, MPI_COMM_WORLD, &statuses[0] );
	if (!flag) {
	    printf( 
    "*ERROR: MPI_Waitall appears to be waiting for requests in the order\n\
they appear in the request list\n" );
	    /* We can wait now since process 2 should have allowed the wait
	       to proceed */
	    MPI_Wait( &req[0], &statuses[0] );
	}
	else {
	    printf( " No Errors\n" ) ;
	}
    }
    else if (rank == 2) {
	MPI_Issend( buf1, cnt, MPI_INT, 1, 1, MPI_COMM_WORLD, &req[0] );
	MPI_Issend( buf2, cnt, MPI_INT, 0, 2, MPI_COMM_WORLD, &req[1] );
	MPI_Barrier( MPI_COMM_WORLD );
	Pause( 1.0 );
	MPI_Waitall( 2, req, statuses );
    }
    else if (rank == 1) {
	MPI_Status status;
	MPI_Barrier( MPI_COMM_WORLD );
	/* Wait for process 0 to tell us to go ahead */
	MPI_Sendrecv( MPI_BOTTOM, 0, MPI_BYTE, 0, 3, 
		      MPI_BOTTOM, 0, MPI_BYTE, 0, 3, MPI_COMM_WORLD, &statuses[0] );
	MPI_Recv( buf1, cnt, MPI_INT, 2, 1, MPI_COMM_WORLD, &status );
    }
    else {
	MPI_Barrier( MPI_COMM_WORLD );
    }
    
    free( buf1 );
    free( buf2 );
    MPI_Finalize();
    return 0;
}
