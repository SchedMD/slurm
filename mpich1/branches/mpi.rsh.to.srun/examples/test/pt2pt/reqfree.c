#include "test.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include "mpi.h"

#define MAX_REQ 10000

#define DEFAULT_REQ 100
#define DEFAULT_LEN 10000
#define DEFAULT_LOOP 10

int main( int argc, char **argv )
{
    int rank, size, loop, max_loop = DEFAULT_LOOP, max_req = DEFAULT_REQ;
    int buf_len = DEFAULT_LEN;
    int i, j, errs = 0, toterrs;
    MPI_Request r;
    MPI_Status  status;
    int *(b[MAX_REQ]);
    MPI_Datatype dtype;
    int sendrank = 0, recvrank = 1;

    MPI_Init( &argc, &argv );

    dtype = MPI_INT;

    MPI_Comm_rank( MPI_COMM_WORLD, &rank );

/* 
   The following test allows this test to run on small-memory systems
   that support the sysconf call interface.  This test keeps the test from
   becoming swap-bound.  For example, on an old Linux system or a
   Sony Playstation 2 (really!) 
 */
#if defined(HAVE_SYSCONF) && defined(_SC_PHYS_PAGES) && defined(_SC_PAGESIZE)
    if (rank == sendrank) 
    { 
	long n_pages, pagesize;
	int  msglen_max = max_req * buf_len * sizeof(int);
	n_pages  = sysconf( _SC_PHYS_PAGES );
	pagesize = sysconf( _SC_PAGESIZE );
	/* printf( "Total mem = %ld\n", n_pages * pagesize ); */
	/* We want to avoid integer overflow in the size calculation.
	   The best way is to avoid computing any products (such
	   as total memory = n_pages * pagesize) and instead
	   compute a msglen_max that fits within 1/4 of the available 
	   pages */
	if (n_pages > 0 && pagesize > 0) {
	    /* Recompute msglen_max */
	    int msgpages = 4 * ((msglen_max + pagesize - 1)/ pagesize);
	    while (n_pages < msgpages) { 
		msglen_max /= 2; msgpages /= 2; buf_len /= 2; 
	    }
	}
    }
#else
    /* printf( "No sysconf\n" ); */
#endif

    /* Check command line args (allow usage even with one processor */
    argv++;
    argc--;
    while (argc--) {
	if (strcmp( "-loop" , *argv ) == 0) {
	    argv++; argc--;
	    max_loop = atoi( *argv );
	}
	else if (strcmp( "-req", *argv ) == 0) {
	    argv++; argc--;
	    max_req = atoi( *argv );
	}
	else if (strcmp( "-len", *argv ) == 0) {
	    argv++; argc--;
	    buf_len = atoi( *argv );
	}
	else {
	    fprintf( stderr, 
		     "Usage: reqfree [ -loop n ] [ -req n ] [ -len n ]\n" );
	    MPI_Abort( MPI_COMM_WORLD, 1 );
	}
	argv++;
    }
    
    MPI_Comm_size( MPI_COMM_WORLD, &size );
    if (size != 2) {
	fprintf( stderr, "This program requires two processes\n" );
	MPI_Abort( MPI_COMM_WORLD, 1 );
    }

    /* Assume only processor 0 has the command line */
    MPI_Bcast( &max_loop, 1, MPI_INT, 0, MPI_COMM_WORLD );
    MPI_Bcast( &max_req, 1, MPI_INT, 0, MPI_COMM_WORLD );
    MPI_Bcast( &buf_len, 1, MPI_INT, 0, MPI_COMM_WORLD );

    /* Allocate buffers */
    for (i=0; i<max_req; i++) {
	b[i] = (int *) malloc(buf_len * sizeof(int) );
	if (!b[i]) {
	    fprintf( stderr, "Could not allocate %dth block of %d ints\n", 
		     i, buf_len );
	    MPI_Abort( MPI_COMM_WORLD, 2 );
	}
	if (rank != sendrank) break;
	for (j=0; j<buf_len; j++) {
	    b[i][j] = i * buf_len + j;
	}
    }

    /* Loop several times to capture resource leaks */
    for (loop=0; loop<max_loop; loop++) {
	if (rank == sendrank) {
	    for (i=0; i<max_req; i++) {
		MPI_Isend( b[i], buf_len, dtype, recvrank, 0, 
			   MPI_COMM_WORLD, &r );
		MPI_Request_free( &r ); 
	    }
	    MPI_Barrier( MPI_COMM_WORLD );
	    MPI_Barrier( MPI_COMM_WORLD );
	}
	else {
	    MPI_Barrier( MPI_COMM_WORLD );
	    for (i=0; i<max_req; i++) {
		MPI_Recv( b[0], buf_len, dtype, sendrank, 0, MPI_COMM_WORLD, 
			  &status );
		for (j=0; j<buf_len; j++) {
		    if (b[0][j] != i * buf_len + j) {
			errs++;
			fprintf( stdout, 
				 "at %d in %dth message, got %d expected %d\n",
				 j, i, b[0][j], i * buf_len + j );
			break;
		    }
		}
	    }
	    MPI_Barrier( MPI_COMM_WORLD );
	}
    }

    MPI_Allreduce( &errs, &toterrs, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD );
    if (rank == 0) {
	if (toterrs == 0) printf( " No Errors\n" );
	else              printf( "Found %d errors\n", toterrs );
    }

    MPI_Finalize( );
    return 0;
}

