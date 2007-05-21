/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* This program comes from Bert Still, bert@h4p.llnl.gov 
   It caused problems for the T3D implementation.
 */
#include <stdio.h>
#include "mpi.h"
#include "test.h"

#define MESSAGE_TAG 8
#define MESSAGE_VALUE 6
#define MESSAGE_TYPE MPI_BYTE
#define MESSAGE_CTYPE char
static MESSAGE_CTYPE recv_msg[8];
static MESSAGE_CTYPE send_msg[8];

static MPI_Status recv_status;
static MPI_Status send_status[2];
static MPI_Request request[2];
static int complete[2];

/*------------------------------------------------------------------------*/

void fatal ( int, char * );

void fatal(rank, msg)
int rank;
char *msg;
{
  printf("***FATAL** rank %d: %s\n", rank, msg);
  MPI_Abort(MPI_COMM_WORLD, 1);
  exit(1);
}

int verbose = 0;
int main( int argc, char *argv[] )
{
  int size, rank;
  int err=0, toterr;

  if (MPI_Init(&argc, &argv)!=MPI_SUCCESS) fatal(-1, "MPI_Init failed");

  if (MPI_Comm_size(MPI_COMM_WORLD, &size)!=MPI_SUCCESS)
    fatal(-1, "MPI_Comm_size failed");
  if (MPI_Comm_rank(MPI_COMM_WORLD, &rank)!=MPI_SUCCESS)
    fatal(-1, "MPI_Comm_rank failed");
  if (size!=2) fatal(rank, "issend2 test requires -np 2\n");

  if (rank) {
    if (MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD,
                  &recv_status)!=MPI_SUCCESS)
      fatal(rank, "MPI_Probe failed");
    if (recv_status.MPI_SOURCE!=0 || recv_status.MPI_TAG!=MESSAGE_TAG)
      fatal(rank, "message source or tag wrong");
    if (MPI_Recv(recv_msg, 8, MESSAGE_TYPE,
                 recv_status.MPI_SOURCE, recv_status.MPI_TAG, MPI_COMM_WORLD,
                 &recv_status)!=MPI_SUCCESS)
      fatal(rank, "MPI_Recv failed");
    if (recv_msg[0] == MESSAGE_VALUE) {
	if (verbose) printf( "test completed successfully\n" );
    }
    else {
	printf("test failed: rank %d: got %d but expected %d\n", 
	       rank, recv_msg[0], MESSAGE_VALUE );
	err++;
    }

    fflush(stdout);

    if (recv_msg[0]!=MESSAGE_VALUE)
      fatal(rank, "received message doesn't match sent message");

  } else {
    int n_complete;

    send_msg[0]= MESSAGE_VALUE;

    if (MPI_Issend(send_msg, 1, MESSAGE_TYPE, /*rank*/1, MESSAGE_TAG,
                   MPI_COMM_WORLD, request) != MPI_SUCCESS)
          fatal(rank, "MPI_Issend failed");
    if (MPI_Waitsome(1, request, &n_complete, complete,send_status) != 
        MPI_SUCCESS) 
	fatal(rank, "MPI_Waitsome failed");
    if (request[0]!=MPI_REQUEST_NULL || n_complete!=1 || complete[0]!=0) 
	fatal(rank, "Waitsome result is wrong");
  }

  MPI_Allreduce( &err, &toterr, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD );
  if (rank == 0) {
      if (toterr == 0) {
	  printf( " No Errors\n" );
      }
      else {
	  printf (" Found %d errors\n", toterr );
      }
  }
  /* printf("rank %d: about to finalize\n", rank); */
  fflush(stdout);
  MPI_Finalize();
  /*  printf("rank %d: finalize completed\n", rank); */
  fflush(stdout);
  return 0;
}
