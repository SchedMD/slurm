/*
 * This file checks to see if the collective routine MPI_Allreduce uses
 * MPI_Send or MPI_Isend to implement the operation.  It should use either
 * a PMPI routine or a non-MPI routine.  
 */

#include "mpi.h"
#include <stdio.h>

static int used_send = 0,
           used_isend = 0,
           used_sendrecv = 0;
int main( int argc, char *argv[] )
{
    int in, out;
    int rank;
    int in_sends[3], out_sends[3];

    MPI_Init( &argc, &argv );
    
    MPI_Comm_rank( MPI_COMM_WORLD, &rank );
    in = 1;
    MPI_Allreduce( &in, &out, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD );

    /* Now, see whether MPI routines were used */
    in_sends[0] = used_send;
    in_sends[1] = used_isend;
    in_sends[2] = used_sendrecv;
    MPI_Reduce( in_sends, out_sends, 3, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD );
    if (rank == 0) {
	int errs = 0;
	if (in_sends[0] > 0) {
	    printf( " Allreduce used MPI_SEND (%d)\n", in_sends[0] );
	    errs++;
	}
	if (in_sends[1] > 0) {
	    printf( " Allreduce used MPI_ISEND (%d)\n", in_sends[1] );
	    errs++;
	}
	if (in_sends[2] > 0) {
	    printf( " Allreduce used MPI_SENDRECV (%d)\n", in_sends[2] );
	    errs++;
	}
	if (!errs) {
	    printf( " No Errors\n" );
	}
    }

    MPI_Finalize( );
    return 0;
}

/* 
 * Replacements for MPI_Send, Isend, and Sendrecv that detect their use
 */

int MPI_Send( void *buf, int count, MPI_Datatype datatype, int dest, 
	      int tag, MPI_Comm comm )
{
    used_send++;
    return PMPI_Send( buf, count, datatype, dest, tag, comm );
}

int MPI_Sendrecv( void *sendbuf, int sendcount, MPI_Datatype sendtype, 
		  int dest, int sendtag, 
                  void *recvbuf, int recvcount, MPI_Datatype recvtype, 
		  int source, int recvtag, MPI_Comm comm, MPI_Status *status )
{
    used_sendrecv++;
    return PMPI_Sendrecv( sendbuf, sendcount, sendtype, dest, sendtag, 
			  recvbuf, recvcount, recvtype, source, recvtag, 
			  comm, status ); 
}

int MPI_Isend( void *buf, int count, MPI_Datatype datatype, int dest, int tag,
	       MPI_Comm comm, MPI_Request *request )
{
    used_isend++;
    return PMPI_Isend( buf, count, datatype, dest, tag, comm, request );
}

