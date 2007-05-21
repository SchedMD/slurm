#ifdef MPI_BUILD_PROFILING
#undef MPI_BUILD_PROFILING
#endif
#include <stdio.h>
#include "mpeconf.h"
#include "mpi.h"
#include "mpe_log.h"

#define S_SEND_EVENT      256
#define E_SEND_EVENT      257
#define S_RECV_EVENT      258
#define E_RECV_EVENT      259
#define S_BCAST_EVENT     260
#define E_BCAST_EVENT     261
#define S_REDUCE_EVENT    262
#define E_REDUCE_EVENT    263
#define S_BARRIER_EVENT   264
#define E_BARRIER_EVENT   265
#define S_ISEND_EVENT     266
#define E_ISEND_EVENT     267
#define S_IRECV_EVENT     268
#define E_IRECV_EVENT     269
#define S_WAIT_EVENT      270
#define E_WAIT_EVENT      271
#define S_TEST_EVENT      272
#define E_TEST_EVENT      273
#define S_WAITALL_EVENT   274
#define E_WAITALL_EVENT   275
#define S_SSEND_EVENT     276
#define E_SSEND_EVENT     277
#define S_WAITANY_EVENT   278
#define E_WAITANY_EVENT   279
#define S_SENDRECV_EVENT  280
#define E_SENDRECV_EVENT  281
#define S_ALLREDUCE_EVENT 282
#define E_ALLREDUCE_EVENT 283
#define S_ISSEND_EVENT    284
#define E_ISSEND_EVENT    285
#define S_PROBE_EVENT     286
#define E_PROBE_EVENT     287
#define S_IPROBE_EVENT    288
#define E_IPROBE_EVENT    289

static int Barrier_ncalls=0;
static int Bcast_ncalls=0;
static int Irecv_ncalls=0;
static int Isend_ncalls=0;
static int Recv_ncalls=0;
static int Reduce_ncalls=0;
static int Send_ncalls=0;
static int Sendrecv_ncalls=0;
static int Ssend_ncalls=0;
static int Test_ncalls=0;
static int Wait_ncalls=0;
static int Waitall_ncalls=0;
static int Waitany_ncalls=0;
static int Allreduce_ncalls=0;
static int Issend_ncalls=0;
static int Probe_ncalls=0;
static int Iprobe_ncalls=0;

static int procid;
static char logFileName[256];

/*
    MPI_Init - replacement for MPI_Init
*/
int MPI_Init( argc, argv )
int *argc;
char ***argv;
{
  int returnVal;

  fprintf( stderr, "Initializing MPI\n");

  returnVal = PMPI_Init( argc, argv );

  MPE_Init_log();
  MPI_Comm_rank( MPI_COMM_WORLD, &procid );
  if (procid == 0) {
    MPE_Describe_state( S_SEND_EVENT, E_SEND_EVENT, "Send", "blue:gray3" );
    MPE_Describe_state( S_RECV_EVENT, E_RECV_EVENT, "Recv", "green:light_gray" );
    MPE_Describe_state( S_BCAST_EVENT, E_BCAST_EVENT, "Bcast", "cyan:boxes" );
    MPE_Describe_state( S_REDUCE_EVENT, E_REDUCE_EVENT, "Reduce",
		       "purple:2x2" );
    MPE_Describe_state( S_ALLREDUCE_EVENT, E_ALLREDUCE_EVENT, 
		       "Allreduce", "purple:vlines3" );
    MPE_Describe_state( S_BARRIER_EVENT, E_BARRIER_EVENT, "Barrier",
		       "yellow:dimple3" );
    MPE_Describe_state( S_ISEND_EVENT, E_ISEND_EVENT, "Isend", "skyblue:gray" );
    MPE_Describe_state( S_IRECV_EVENT, E_IRECV_EVENT, 
		       "Irecv", "springgreen:gray" );
    MPE_Describe_state( S_WAIT_EVENT, E_WAIT_EVENT, "Wait", "red:black" );
    MPE_Describe_state( S_TEST_EVENT, E_TEST_EVENT, "Test", "orange:gray" );
    MPE_Describe_state( S_WAITALL_EVENT, E_WAITALL_EVENT, 
		       "Waitall", "OrangeRed:gray" );
    MPE_Describe_state( S_SSEND_EVENT, E_SSEND_EVENT, 
		       "Ssend", "deepskyblue:gray" );
    MPE_Describe_state( S_WAITANY_EVENT, E_WAITANY_EVENT, 
		       "Waitany", "coral:gray" );
    MPE_Describe_state( S_SENDRECV_EVENT, E_SENDRECV_EVENT, 
		       "Sendrecv", "seagreen:gray" );
    MPE_Describe_state( S_ALLREDUCE_EVENT, E_ALLREDUCE_EVENT, 
		       "Allreduce", "seagreen:gray" );
    MPE_Describe_state( S_ISSEND_EVENT, E_ISSEND_EVENT, 
		       "Issend", "seagreen:gray" );
    MPE_Describe_state( S_PROBE_EVENT, E_PROBE_EVENT, 
		       "Probe", "seagreen:gray" );
    MPE_Describe_state( S_IPROBE_EVENT, E_IPROBE_EVENT, 
		       "Iprobe", "seagreen:gray" );
  }
  /* sprintf( logFileName, "%s_profile.log", (*argv)[0] ); */
  sprintf( logFileName, "%s", (*argv)[0] );

  return returnVal;
}

/*
    MPI_Send - prototyping replacement for MPI_Send
*/
int MPI_Send( buf, count, datatype, dest, tag, comm )
void *buf;
int count, dest, tag;
MPI_Datatype datatype;
MPI_Comm comm;
{
  char mesgStr[100];
  int result;

  ++Send_ncalls;
  sprintf( mesgStr, "start send mesg %d sent from %d to %d", tag, procid, dest );
  MPE_Log_event( S_SEND_EVENT, Send_ncalls, mesgStr );
  result = PMPI_Send( buf, count, datatype, dest, tag, comm );
  sprintf( mesgStr, "end send from %d", procid );
  MPE_Log_event( E_SEND_EVENT, Send_ncalls, mesgStr );

  return result;
}

/*
    MPI_Recv - prototyping replacement for MPI_Recv
*/
int MPI_Recv( buf, count, datatype, source, tag, comm, status )
void *buf;
int count, source, tag;
MPI_Datatype datatype;
MPI_Comm comm;
MPI_Status *status;
{
  char mesgStr[100];
  int result;

  Recv_ncalls++;
  sprintf( mesgStr, "start recv by %d of mesg %d from %d", procid, tag, source );
  MPE_Log_event( S_RECV_EVENT, Recv_ncalls, mesgStr );
  result = PMPI_Recv( buf, count, datatype, source, tag, comm, status );
  sprintf( mesgStr, "end recv by %d", procid );
  MPE_Log_event( E_RECV_EVENT, Recv_ncalls, mesgStr );

  return result;
}

/*
    MPI_Bcast - prototyping replacement for MPI_Bcast
*/
int MPI_Bcast( buf, count, datatype, source, comm )
void *buf;
int count, source;
MPI_Datatype datatype;
MPI_Comm comm;
{
  int result;

  ++Bcast_ncalls;
  MPE_Log_event( S_BCAST_EVENT, Bcast_ncalls, 0 );
  result = PMPI_Bcast( buf, count, datatype, source, comm );
  MPE_Log_event( E_BCAST_EVENT, Bcast_ncalls, 0 );

  return result;
}


/*
    MPI_Reduce - prototyping replacement for MPI_Reduce
*/
int MPI_Reduce( sendbuf, recvbuf, count, datatype, op, root, comm )
void *sendbuf, *recvbuf;
int count, root;
MPI_Op op;
MPI_Datatype datatype;
MPI_Comm comm;
{
  int result;

  ++Reduce_ncalls;
  MPE_Log_event( S_REDUCE_EVENT, Reduce_ncalls, 0 );
  result = PMPI_Reduce( sendbuf, recvbuf, count, datatype, op, root, comm );
  MPE_Log_event( E_REDUCE_EVENT, Reduce_ncalls, 0 );

  return result;
}

/*
   MPI_Allreduce
 */
int MPI_Allreduce ( sendbuf, recvbuf, count, datatype, op, comm )
void             *sendbuf;
void             *recvbuf;
int               count;
MPI_Datatype      datatype;
MPI_Op            op;
MPI_Comm          comm;
{
  int result;

  ++Allreduce_ncalls;
  MPE_Log_event( S_ALLREDUCE_EVENT, Allreduce_ncalls, 0 );
  result = PMPI_Allreduce( sendbuf, recvbuf, count, datatype, op, comm );
  MPE_Log_event( E_ALLREDUCE_EVENT, Allreduce_ncalls, 0 );

  return result;
}

/*
    MPI_Barrier - prototyping replacement for MPI_Barrier
*/
int MPI_Barrier( comm )
MPI_Comm comm;
{
  int result;

  ++Barrier_ncalls;
  MPE_Log_event( S_BARRIER_EVENT, Barrier_ncalls, 0 );
  result = PMPI_Barrier( comm );
  MPE_Log_event( E_BARRIER_EVENT, Barrier_ncalls, 0 );

  return result;
}


/*
    MPI_Isend - prototyping replacement for MPI_Isend
*/
int MPI_Isend( buf, count, datatype, dest, tag, comm, request )
void *buf;
int count, dest, tag;
MPI_Datatype datatype;
MPI_Comm comm;
MPI_Request *request;
{
  int result;

  ++Isend_ncalls;
  MPE_Log_event( S_ISEND_EVENT, Isend_ncalls, 0 );
  result = PMPI_Isend( buf, count, datatype, dest, tag, comm, request );
  MPE_Log_event( E_ISEND_EVENT, Isend_ncalls, 0 );

  return result;
}


/*
    MPI_Irecv - prototyping replacement for MPI_Irecv
*/
int MPI_Irecv( buf, count, datatype, source, tag, comm, request )
void *buf;
int count, source, tag;
MPI_Datatype datatype;
MPI_Comm comm;
MPI_Request *request;
{
  int result;

  ++Irecv_ncalls;
  MPE_Log_event( S_IRECV_EVENT, Irecv_ncalls, 0 );
  result = PMPI_Irecv( buf, count, datatype, source, tag, comm, request );
  MPE_Log_event( E_IRECV_EVENT, Irecv_ncalls, 0 );

  return result;
}

/*
    MPI_Wait - prototyping replacement for MPI_Wait
*/
int MPI_Wait( request, status)
MPI_Request *request;
MPI_Status *status;
{
  int result;

  ++Wait_ncalls;
  MPE_Log_event( S_WAIT_EVENT, Wait_ncalls, 0 );
  result = PMPI_Wait( request, status );
  MPE_Log_event( E_WAIT_EVENT, Wait_ncalls, 0 );

  return result;
}

/*
    MPI_Test - prototyping replacement for MPI_Test
*/
int MPI_Test( request, flag, status )
MPI_Request *request;
int *flag;
MPI_Status *status;
{
  int result;

  ++Test_ncalls;
  MPE_Log_event( S_TEST_EVENT, Test_ncalls, 0 );
  result = PMPI_Test( request, flag, status );
  MPE_Log_event( E_TEST_EVENT, Test_ncalls, 0 );

  return result;
}



/*
    MPI_Waitall - prototyping replacement for MPI_Waitall
*/
int MPI_Waitall( count, requests, statuses )
int count;
MPI_Request *requests;
MPI_Status *statuses;
{
  int result;

  ++Waitall_ncalls;
  MPE_Log_event( S_WAITALL_EVENT, Waitall_ncalls, 0 );
  result = PMPI_Waitall( count, requests, statuses );
  MPE_Log_event( E_WAITALL_EVENT, Waitall_ncalls, 0 );

  return result;
}


/*
    MPI_Sendrecv - prototyping replacement for MPI_Sendrecv
*/
int MPI_Sendrecv( sendbuf, sendcount, sendtype, dest,   sendtag,
	      recvbuf, recvcount, recvtype, source, recvtag,
	      comm, status )
void *sendbuf, *recvbuf;
int sendcount, dest, sendtag, source, recvtag, recvcount;
MPI_Datatype sendtype, recvtype;
MPI_Comm comm;
MPI_Status *status;
{
  int result;

  ++Sendrecv_ncalls;
  MPE_Log_event( S_SENDRECV_EVENT, Sendrecv_ncalls, 0 );
  result = PMPI_Sendrecv( sendbuf, sendcount, sendtype, dest,   sendtag,
	      recvbuf, recvcount, recvtype, source, recvtag,
	      comm, status );
  MPE_Log_event( E_SENDRECV_EVENT, Sendrecv_ncalls, 0 );

  return result;
}

/*
    MPI_Waitany - prototyping replacement for MPI_Waitany
*/
int MPI_Waitany( count, array, index, status )
int count, *index;
MPI_Request *array;
MPI_Status *status;
{
  int result;

  ++Waitany_ncalls;
  MPE_Log_event( S_WAITANY_EVENT, Waitany_ncalls, 0 );
  result = PMPI_Waitany( count, array, index, status );
  MPE_Log_event( E_WAITANY_EVENT, Waitany_ncalls, 0 );

  return result;
}

/*
    MPI_Ssend - prototyping replacement for MPI_Ssend
*/
int MPI_Ssend( buf, count, datatype, dest, tag, comm )
void *buf;
int count, dest, tag;
MPI_Datatype datatype;
MPI_Comm comm;
{
  int result;

  ++Ssend_ncalls;
  MPE_Log_event( S_SSEND_EVENT, Ssend_ncalls, 0 );
  result = PMPI_Ssend( buf, count, datatype, dest, tag, comm );
  MPE_Log_event( E_SSEND_EVENT, Ssend_ncalls, 0 );

  return result;
}

/*
    MPI_Issend - prototyping replacement for MPI_Issend
*/
int MPI_Issend( buf, count, datatype, dest, tag, comm, request )
void *buf;
int count;
MPI_Datatype datatype;
int dest;
int tag;
MPI_Comm comm;
MPI_Request *request;
{
  int result;

  ++Issend_ncalls;
  MPE_Log_event( S_ISSEND_EVENT, Issend_ncalls, 0 );
  result = PMPI_Issend( buf, count, datatype, dest, tag, comm, request );
  MPE_Log_event( E_ISSEND_EVENT, Issend_ncalls, 0 );

  return result;
}

/*
   MPI_ Probe- prototyping replacement for MPI_Probe
*/
int MPI_Probe( source, tag, comm, status )
int source;
int tag;
MPI_Comm comm;
MPI_Status *status;
{
  int result;

  ++Probe_ncalls;
  MPE_Log_event( S_PROBE_EVENT, Probe_ncalls, 0 );
  result = PMPI_Probe( source, tag, comm, status );
  MPE_Log_event( E_PROBE_EVENT, Probe_ncalls, 0 );

  return result;
}

/*
    MPI_Iprobe - prototyping replacement for MPI_Iprobe
*/
int MPI_Iprobe( source, tag, comm, flag, status )
int source;
int tag;
MPI_Comm comm;
int *flag;
MPI_Status *status;
{
  int result;

  ++Iprobe_ncalls;
  MPE_Log_event( S_IPROBE_EVENT, Iprobe_ncalls, 0 );
  result = PMPI_Iprobe( source, tag, comm, flag, status );
  MPE_Log_event( E_IPROBE_EVENT, Iprobe_ncalls, 0 );

  return result;
}

/*
    MPI_Finalize - prototyping replacement for MPI_Finalize
*/
int MPI_Finalize()
{
  MPE_Finish_log( logFileName );
#ifdef DEBUG
  fprintf( stderr, "About to call system finalize\n" ); fflush(stderr);
#endif
  return PMPI_Finalize();
}



#if 0
/*
     MPI_ - prototyping replacement for MPI_
*/
int MPI_( )
{
  int result;

  ++_ncalls;
  MPE_Log_event( S__EVENT, Waitall_ncalls, 0 );
  result = PMPI_( );
  MPE_Log_event( E__EVENT, Waitall_ncalls, 0 );

  return result;
}

#endif
