#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mpi.h"

#if defined(NEEDS_STDLIB_PROTOTYPES)
#include "protofix.h"
#endif

/*
   Test to make sure that nonblocking routines actually work
   In this example, we assume that we do not know the message
   sizes ahead of time.

   Just like nblock, but with the probe test.
*/

int main( int argc, char **argv )
{
    int count, tag, nsend, myid, np, rcnt, scnt, i, j, *send_buf;
    int length, finished;
    int baselen = 1;
    int **recv_buf;
    MPI_Status status, rtn_status;
    MPI_Request *rsend, *rrecv;

    MPI_Init( &argc, &argv );
    MPI_Comm_rank( MPI_COMM_WORLD, &myid );
    MPI_Comm_size( MPI_COMM_WORLD, &np );
/*
  MPE_Errors_call_dbx_in_xterm( (argv)[0], (char *)0 ); 
  MPE_Signals_call_debugger();
  */
    if (argc > 2 && argv[1] && strcmp( argv[1], "-first" ) == 0) 
	baselen = atoi(argv[2]);

/* malloc buffers */
    nsend = 3 * np;
    rsend = (MPI_Request *) malloc ( nsend * sizeof(MPI_Request) );
    rrecv = (MPI_Request *) malloc ( nsend * sizeof(MPI_Request) );
    recv_buf = (int **) malloc ( nsend * sizeof(int *) );
    if (!rsend || !rrecv) {
	fprintf( stderr, "Failed to allocate space for requests\n" );
	MPI_Abort( MPI_COMM_WORLD, 1 );
    }

    for (count = baselen; count < 10000; count *= 2) {
	/* We'll send/recv from everyone */
	scnt = 0;
	rcnt = 0;

	/* do sends */
	send_buf   = (int *)malloc( count * sizeof(int) );
	for (j=0; j<3; j++) {
	    tag = j;
	    for (i=0; i<np; i++) {
		if (i != myid) 
		    MPI_Isend( send_buf, count, MPI_INT, i, tag, 
			       MPI_COMM_WORLD, &rsend[scnt++] );
	    }
	    /* Check sends, one could free memory here if they are done */
	    for (i=0; i<scnt; i++) {
		MPI_Test( &rsend[i], &finished, &status );
	    }
	}

	/* do recvs */
	for (j=0; j<3; j++) {
	    tag = j;
	    for (i=0; i<np; i++) {
		if (i != myid)  {
		    MPI_Probe(MPI_ANY_SOURCE,tag,MPI_COMM_WORLD,&status);
		    MPI_Get_count(&status,MPI_INT,&length); 
		    /* printf("[%d] length = %d\n",myid,length); 
		       fflush(stdout); */
		    recv_buf[rcnt] = (int *)malloc(length * sizeof(int));
		    MPI_Recv(recv_buf[rcnt],length,MPI_INT,status.MPI_SOURCE, 
			     status.MPI_TAG,MPI_COMM_WORLD,&rtn_status);
		    rcnt++;
		}
	    }
	}

	/* Wait on sends */
   	for (i=0; i<scnt; i++) {
	    MPI_Wait( &rsend[i], &status );
	}

	/* free buffers */
	for (i=0; i<rcnt; i++) free(recv_buf[i]);
	free( send_buf );
	
	MPI_Barrier( MPI_COMM_WORLD );
	if (myid == 0 && (count % 64) == 0) {
	    printf( "All processes completed for count = %ld ints of data\n", 
		    (long)count ); fflush(stdout);
	}
    }

    MPI_Finalize();
    return 0;
}

