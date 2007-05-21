#include "test.h"
#include "mpi.h"
#include <stdio.h>
#include <stdlib.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#define MIN_MESSAGE_LENGTH 256
#define MAX_MESSAGE_LENGTH (16*1024*1024)
#define TAG1 1
#define TAG2 2
#define TAG3 3
#define TAG4 4
#define TAGSR 101

int verbose = 0;

void Resetbuf( char *, int );
void Checkbuf( char *, int, MPI_Status * );

void Resetbuf( char *buf, int len )
{
    int i;
    for (i=0; i<len; i++) 
	buf[i] = 0;
}

void Checkbuf( char *buf, int len, MPI_Status *status )
{
    int count, i;
    int err = 0;
    char ival;
    
    MPI_Get_count( status, MPI_CHAR, &count );
    if (count != len) {
	fprintf( stderr, "Got len of %d but expected %d\n", count, len );
	err++;
    }
    ival = 0;
    for (i=0; i<len; i++) {
	if (buf[i] != ival) {
	    err++;
	    fprintf( stderr, 
		     "Found wrong value in buffer[%d] = %d, expected %d\n",
		     i, buf[i], ival );
	    if (err > 10) break;
	}
	ival++;
    }
    if (err) MPI_Abort( MPI_COMM_WORLD, 1 );
}

int main( int argc, char *argv[] )
{
    int msglen, i;
    int msglen_min = MIN_MESSAGE_LENGTH;
    int msglen_max = MAX_MESSAGE_LENGTH;
    int rank,poolsize,Master;
    char *sendbuf,*recvbuf;
    char ival;
    MPI_Request request;
    MPI_Status status;
	
    MPI_Init(&argc,&argv);
    MPI_Comm_size(MPI_COMM_WORLD,&poolsize);
    MPI_Comm_rank(MPI_COMM_WORLD,&rank);

    if(poolsize != 2) {
	printf("Expected exactly 2 MPI processes\n");
	MPI_Abort( MPI_COMM_WORLD, 1 );
    }

/* 
   The following test allows this test to run on small-memory systems
   that support the sysconf call interface.  This test keeps the test from
   becoming swap-bound.  For example, on an old Linux system or a
   Sony Playstation 2 (really!) 
 */
#if defined(HAVE_SYSCONF) && defined(_SC_PHYS_PAGES) && defined(_SC_PAGESIZE)
    { 
	long n_pages, pagesize;
	int  actmsglen_max;
	n_pages  = sysconf( _SC_PHYS_PAGES );
	pagesize = sysconf( _SC_PAGESIZE );
	/* We want to avoid integer overflow in the size calculation.
	   The best way is to avoid computing any products (such
	   as total memory = n_pages * pagesize) and instead
	   compute a msglen_max that fits within 1/4 of the available 
	   pages */
	if (n_pages > 0 && pagesize > 0) {
	    /* Recompute msglen_max */
	    int msgpages = 4 * ((msglen_max + pagesize - 1)/ pagesize);
	    while (n_pages < msgpages) { msglen_max /= 2; msgpages /= 2; }
	}
	/* printf ( "before = %d\n", msglen_max ); */
	MPI_Allreduce( &msglen_max, &actmsglen_max, 1, MPI_INT, 
		       MPI_MIN, MPI_COMM_WORLD );
	msglen_max = actmsglen_max;
	/* printf ( "after = %d\n", msglen_max ); */
    }
#endif

    Master = (rank == 0);	

    if(Master && verbose)
	printf("Size (bytes)\n------------\n");
    for(msglen = msglen_min; msglen <= msglen_max; msglen *= 2) {

	sendbuf = malloc(msglen);
	recvbuf = malloc(msglen);
	if(sendbuf == NULL || recvbuf == NULL) {
	    printf("Can't allocate %d bytes\n",msglen);
	    MPI_Abort( MPI_COMM_WORLD, 1 );
	}

	ival = 0;
	for (i=0; i<msglen; i++) {
	    sendbuf[i] = ival++;
	    recvbuf[i] = 0;
	}


	if(Master && verbose) 
	    printf("%d\n",msglen);
	fflush(stdout);

	MPI_Barrier(MPI_COMM_WORLD);
		
	/* Send/Recv */
	if(Master) 
	    MPI_Send(sendbuf,msglen,MPI_CHAR,1,TAG1,MPI_COMM_WORLD);
	else {
	    Resetbuf( recvbuf, msglen );
	    MPI_Recv(recvbuf,msglen,MPI_CHAR,0,TAG1,MPI_COMM_WORLD,&status);
	    Checkbuf( recvbuf, msglen, &status );
	}

	MPI_Barrier(MPI_COMM_WORLD);

	/* Ssend/Recv */
	if(Master) 
	    MPI_Ssend(sendbuf,msglen,MPI_CHAR,1,TAG2,MPI_COMM_WORLD);
	else {
	    Resetbuf( recvbuf, msglen );
	    MPI_Recv(recvbuf,msglen,MPI_CHAR,0,TAG2,MPI_COMM_WORLD,&status);
	    Checkbuf( recvbuf, msglen, &status );
	}

	MPI_Barrier(MPI_COMM_WORLD);
		
	/* Rsend/Recv */
	if (Master) {
	    MPI_Sendrecv( MPI_BOTTOM, 0, MPI_INT, 1, TAGSR,
			  MPI_BOTTOM, 0, MPI_INT, 1, TAGSR,
			  MPI_COMM_WORLD, &status );
	    MPI_Rsend( sendbuf,msglen,MPI_CHAR,1,TAG3,MPI_COMM_WORLD );
	}
	else {
	    Resetbuf( recvbuf, msglen );
	    MPI_Irecv( recvbuf,msglen,MPI_CHAR,0,TAG3,MPI_COMM_WORLD,&request);
	    MPI_Sendrecv( MPI_BOTTOM, 0, MPI_INT, 0, TAGSR,
			  MPI_BOTTOM, 0, MPI_INT, 0, TAGSR,
			  MPI_COMM_WORLD, &status );
	    MPI_Wait( &request, &status );
	    Checkbuf( recvbuf, msglen, &status );
	}
	    
	MPI_Barrier(MPI_COMM_WORLD);

	/* Isend/Recv - receive not ready */
	if(Master) {
	    MPI_Sendrecv( MPI_BOTTOM, 0, MPI_INT, 1, TAGSR,
			  MPI_BOTTOM, 0, MPI_INT, 1, TAGSR,
			  MPI_COMM_WORLD, &status );
	    MPI_Isend(sendbuf,msglen,MPI_CHAR,1,TAG4,MPI_COMM_WORLD, &request);
	    MPI_Wait( &request, &status );
	}
	else {
	    Resetbuf( recvbuf, msglen );
	    MPI_Sendrecv( MPI_BOTTOM, 0, MPI_INT, 0, TAGSR,
			  MPI_BOTTOM, 0, MPI_INT, 0, TAGSR,
			  MPI_COMM_WORLD, &status );
	    MPI_Recv(recvbuf,msglen,MPI_CHAR,0,TAG4,MPI_COMM_WORLD,&status);
	    Checkbuf( recvbuf, msglen, &status );
	}

	MPI_Barrier(MPI_COMM_WORLD);

	free(sendbuf);
	free(recvbuf);
    }

    if (rank == 0) {
	/* If we do not abort, we saw no errors */
	printf( " No Errors\n" );
    }

    MPI_Finalize();
    return 0;
}
