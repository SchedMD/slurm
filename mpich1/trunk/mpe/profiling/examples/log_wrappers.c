#ifdef MPI_BUILD_PROFILING
#undef MPI_BUILD_PROFILING
#endif
#include <stdio.h>
#include "mpi.h"
#include "mpe.h"

static int MPI_Allreduce_stateid_0,MPI_Allreduce_ncalls_0=0;
static int MPI_Barrier_stateid_0,MPI_Barrier_ncalls_0=0;
static int MPI_Bcast_stateid_0,MPI_Bcast_ncalls_0=0;
static int MPI_Reduce_stateid_0,MPI_Reduce_ncalls_0=0;
static int MPI_Bsend_stateid_0,MPI_Bsend_ncalls_0=0;
static int MPI_Ibsend_stateid_0,MPI_Ibsend_ncalls_0=0;
static int MPI_Iprobe_stateid_0,MPI_Iprobe_ncalls_0=0;
static int MPI_Irecv_stateid_0,MPI_Irecv_ncalls_0=0;
static int MPI_Irsend_stateid_0,MPI_Irsend_ncalls_0=0;
static int MPI_Isend_stateid_0,MPI_Isend_ncalls_0=0;
static int MPI_Issend_stateid_0,MPI_Issend_ncalls_0=0;
static int MPI_Probe_stateid_0,MPI_Probe_ncalls_0=0;
static int MPI_Recv_stateid_0,MPI_Recv_ncalls_0=0;
static int MPI_Rsend_stateid_0,MPI_Rsend_ncalls_0=0;
static int MPI_Send_stateid_0,MPI_Send_ncalls_0=0;
static int MPI_Sendrecv_stateid_0,MPI_Sendrecv_ncalls_0=0;
static int MPI_Ssend_stateid_0,MPI_Ssend_ncalls_0=0;
static int MPI_Test_stateid_0,MPI_Test_ncalls_0=0;
static int MPI_Testall_stateid_0,MPI_Testall_ncalls_0=0;
static int MPI_Testany_stateid_0,MPI_Testany_ncalls_0=0;
static int MPI_Testsome_stateid_0,MPI_Testsome_ncalls_0=0;
static int MPI_Wait_stateid_0,MPI_Wait_ncalls_0=0;
static int MPI_Waitall_stateid_0,MPI_Waitall_ncalls_0=0;
static int MPI_Waitany_stateid_0,MPI_Waitany_ncalls_0=0;
static int MPI_Waitsome_stateid_0,MPI_Waitsome_ncalls_0=0;


static int procid_0;
static char logFileName_0[256];










int   MPI_Allreduce( sendbuf, recvbuf, count, datatype, op, comm )
void * sendbuf;
void * recvbuf;
int count;
MPI_Datatype datatype;
MPI_Op op;
MPI_Comm comm;
{
  int   returnVal;

/*
    MPI_Allreduce - prototyping replacement for MPI_Allreduce
    Log the beginning and ending of the time spent in MPI_Allreduce calls.
*/

  ++MPI_Allreduce_ncalls_0;
  MPE_Log_event( MPI_Allreduce_stateid_0*2,
	         MPI_Allreduce_ncalls_0, (char *)0 );
  
  returnVal = PMPI_Allreduce( sendbuf, recvbuf, count, datatype, op, comm );

  MPE_Log_event( MPI_Allreduce_stateid_0*2+1,
	         MPI_Allreduce_ncalls_0, (char *)0 );


  return returnVal;
}

int   MPI_Barrier( comm )
MPI_Comm comm;
{
  int   returnVal;

/*
    MPI_Barrier - prototyping replacement for MPI_Barrier
    Log the beginning and ending of the time spent in MPI_Barrier calls.
*/

  ++MPI_Barrier_ncalls_0;
  MPE_Log_event( MPI_Barrier_stateid_0*2,
	         MPI_Barrier_ncalls_0, (char *)0 );
  
  returnVal = PMPI_Barrier( comm );

  MPE_Log_event( MPI_Barrier_stateid_0*2+1,
	         MPI_Barrier_ncalls_0, (char *)0 );


  return returnVal;
}

int   MPI_Bcast( buffer, count, datatype, root, comm )
void * buffer;
int count;
MPI_Datatype datatype;
int root;
MPI_Comm comm;
{
  int   returnVal;

/*
    MPI_Bcast - prototyping replacement for MPI_Bcast
    Log the beginning and ending of the time spent in MPI_Bcast calls.
*/

  ++MPI_Bcast_ncalls_0;
  MPE_Log_event( MPI_Bcast_stateid_0*2,
	         MPI_Bcast_ncalls_0, (char *)0 );
  
  returnVal = PMPI_Bcast( buffer, count, datatype, root, comm );

  MPE_Log_event( MPI_Bcast_stateid_0*2+1,
	         MPI_Bcast_ncalls_0, (char *)0 );


  return returnVal;
}

int   MPI_Reduce( sendbuf, recvbuf, count, datatype, op, root, comm )
void * sendbuf;
void * recvbuf;
int count;
MPI_Datatype datatype;
MPI_Op op;
int root;
MPI_Comm comm;
{
  int   returnVal;

/*
    MPI_Reduce - prototyping replacement for MPI_Reduce
    Log the beginning and ending of the time spent in MPI_Reduce calls.
*/

  ++MPI_Reduce_ncalls_0;
  MPE_Log_event( MPI_Reduce_stateid_0*2,
	         MPI_Reduce_ncalls_0, (char *)0 );
  
  returnVal = PMPI_Reduce( sendbuf, recvbuf, count, datatype, op, root, comm );

  MPE_Log_event( MPI_Reduce_stateid_0*2+1,
	         MPI_Reduce_ncalls_0, (char *)0 );


  return returnVal;
}

int  MPI_Finalize(  )
{
  int  returnVal;

/*
    MPI_Finalize - prototyping replacement for MPI_Finalize
*/

  if (procid_0 == 0) {
    fprintf( stderr, "Writing logfile.\n");
    MPE_Describe_state( MPI_Allreduce_stateid_0*2,
	                            MPI_Allreduce_stateid_0*2+1,
      				    "MPI_Allreduce", ":" );
    MPE_Describe_state( MPI_Barrier_stateid_0*2,
	                            MPI_Barrier_stateid_0*2+1,
      				    "MPI_Barrier", ":" );
    MPE_Describe_state( MPI_Bcast_stateid_0*2,
	                            MPI_Bcast_stateid_0*2+1,
      				    "MPI_Bcast", ":" );
    MPE_Describe_state( MPI_Reduce_stateid_0*2,
	                            MPI_Reduce_stateid_0*2+1,
      				    "MPI_Reduce", ":" );
    MPE_Describe_state( MPI_Bsend_stateid_0*2,
	                            MPI_Bsend_stateid_0*2+1,
      				    "MPI_Bsend", ":" );
    MPE_Describe_state( MPI_Ibsend_stateid_0*2,
	                            MPI_Ibsend_stateid_0*2+1,
      				    "MPI_Ibsend", ":" );
    MPE_Describe_state( MPI_Iprobe_stateid_0*2,
	                            MPI_Iprobe_stateid_0*2+1,
      				    "MPI_Iprobe", ":" );
    MPE_Describe_state( MPI_Irecv_stateid_0*2,
	                            MPI_Irecv_stateid_0*2+1,
      				    "MPI_Irecv", ":" );
    MPE_Describe_state( MPI_Irsend_stateid_0*2,
	                            MPI_Irsend_stateid_0*2+1,
      				    "MPI_Irsend", ":" );
    MPE_Describe_state( MPI_Isend_stateid_0*2,
	                            MPI_Isend_stateid_0*2+1,
      				    "MPI_Isend", ":" );
    MPE_Describe_state( MPI_Issend_stateid_0*2,
	                            MPI_Issend_stateid_0*2+1,
      				    "MPI_Issend", ":" );
    MPE_Describe_state( MPI_Probe_stateid_0*2,
	                            MPI_Probe_stateid_0*2+1,
      				    "MPI_Probe", ":" );
    MPE_Describe_state( MPI_Recv_stateid_0*2,
	                            MPI_Recv_stateid_0*2+1,
      				    "MPI_Recv", ":" );
    MPE_Describe_state( MPI_Rsend_stateid_0*2,
	                            MPI_Rsend_stateid_0*2+1,
      				    "MPI_Rsend", ":" );
    MPE_Describe_state( MPI_Send_stateid_0*2,
	                            MPI_Send_stateid_0*2+1,
      				    "MPI_Send", ":" );
    MPE_Describe_state( MPI_Sendrecv_stateid_0*2,
	                            MPI_Sendrecv_stateid_0*2+1,
      				    "MPI_Sendrecv", ":" );
    MPE_Describe_state( MPI_Ssend_stateid_0*2,
	                            MPI_Ssend_stateid_0*2+1,
      				    "MPI_Ssend", ":" );
    MPE_Describe_state( MPI_Test_stateid_0*2,
	                            MPI_Test_stateid_0*2+1,
      				    "MPI_Test", ":" );
    MPE_Describe_state( MPI_Testall_stateid_0*2,
	                            MPI_Testall_stateid_0*2+1,
      				    "MPI_Testall", ":" );
    MPE_Describe_state( MPI_Testany_stateid_0*2,
	                            MPI_Testany_stateid_0*2+1,
      				    "MPI_Testany", ":" );
    MPE_Describe_state( MPI_Testsome_stateid_0*2,
	                            MPI_Testsome_stateid_0*2+1,
      				    "MPI_Testsome", ":" );
    MPE_Describe_state( MPI_Wait_stateid_0*2,
	                            MPI_Wait_stateid_0*2+1,
      				    "MPI_Wait", ":" );
    MPE_Describe_state( MPI_Waitall_stateid_0*2,
	                            MPI_Waitall_stateid_0*2+1,
      				    "MPI_Waitall", ":" );
    MPE_Describe_state( MPI_Waitany_stateid_0*2,
	                            MPI_Waitany_stateid_0*2+1,
      				    "MPI_Waitany", ":" );
    MPE_Describe_state( MPI_Waitsome_stateid_0*2,
	                            MPI_Waitsome_stateid_0*2+1,
      				    "MPI_Waitsome", ":" );
    
  }
  MPE_Finish_log( logFileName_0 );
  if (procid_0 == 0)
    fprintf( stderr, "Finished writing logfile.\n");

  
  returnVal = PMPI_Finalize(  );


  return returnVal;
}

int  MPI_Init( argc, argv )
int * argc;
char *** argv;
{
  int  returnVal;
  int stateid;

  
  
  returnVal = PMPI_Init( argc, argv );


  MPE_Init_log();
  MPI_Comm_rank( MPI_COMM_WORLD, &procid_0 );
  stateid=1;
  MPI_Allreduce_stateid_0 = stateid++;
  MPI_Barrier_stateid_0 = stateid++;
  MPI_Bcast_stateid_0 = stateid++;
  MPI_Reduce_stateid_0 = stateid++;
  MPI_Bsend_stateid_0 = stateid++;
  MPI_Ibsend_stateid_0 = stateid++;
  MPI_Iprobe_stateid_0 = stateid++;
  MPI_Irecv_stateid_0 = stateid++;
  MPI_Irsend_stateid_0 = stateid++;
  MPI_Isend_stateid_0 = stateid++;
  MPI_Issend_stateid_0 = stateid++;
  MPI_Probe_stateid_0 = stateid++;
  MPI_Recv_stateid_0 = stateid++;
  MPI_Rsend_stateid_0 = stateid++;
  MPI_Send_stateid_0 = stateid++;
  MPI_Sendrecv_stateid_0 = stateid++;
  MPI_Ssend_stateid_0 = stateid++;
  MPI_Test_stateid_0 = stateid++;
  MPI_Testall_stateid_0 = stateid++;
  MPI_Testany_stateid_0 = stateid++;
  MPI_Testsome_stateid_0 = stateid++;
  MPI_Wait_stateid_0 = stateid++;
  MPI_Waitall_stateid_0 = stateid++;
  MPI_Waitany_stateid_0 = stateid++;
  MPI_Waitsome_stateid_0 = stateid++;
  
  sprintf( logFileName_0, "%s_profile.log", (*argv)[0] );

  MPE_Start_log();

  return returnVal;
}

int  MPI_Bsend( buf, count, datatype, dest, tag, comm )
void * buf;
int count;
MPI_Datatype datatype;
int dest;
int tag;
MPI_Comm comm;
{
  int  returnVal;

/*
    MPI_Bsend - prototyping replacement for MPI_Bsend
    Log the beginning and ending of the time spent in MPI_Bsend calls.
*/

  ++MPI_Bsend_ncalls_0;
  MPE_Log_event( MPI_Bsend_stateid_0*2,
	         MPI_Bsend_ncalls_0, (char *)0 );
  
  returnVal = PMPI_Bsend( buf, count, datatype, dest, tag, comm );

  MPE_Log_event( MPI_Bsend_stateid_0*2+1,
	         MPI_Bsend_ncalls_0, (char *)0 );


  return returnVal;
}

int  MPI_Ibsend( buf, count, datatype, dest, tag, comm, request )
void * buf;
int count;
MPI_Datatype datatype;
int dest;
int tag;
MPI_Comm comm;
MPI_Request * request;
{
  int  returnVal;

/*
    MPI_Ibsend - prototyping replacement for MPI_Ibsend
    Log the beginning and ending of the time spent in MPI_Ibsend calls.
*/

  ++MPI_Ibsend_ncalls_0;
  MPE_Log_event( MPI_Ibsend_stateid_0*2,
	         MPI_Ibsend_ncalls_0, (char *)0 );
  
  returnVal = PMPI_Ibsend( buf, count, datatype, dest, tag, comm, request );

  MPE_Log_event( MPI_Ibsend_stateid_0*2+1,
	         MPI_Ibsend_ncalls_0, (char *)0 );


  return returnVal;
}

int  MPI_Iprobe( source, tag, comm, flag, status )
int source;
int tag;
MPI_Comm comm;
int * flag;
MPI_Status * status;
{
  int  returnVal;

/*
    MPI_Iprobe - prototyping replacement for MPI_Iprobe
    Log the beginning and ending of the time spent in MPI_Iprobe calls.
*/

  ++MPI_Iprobe_ncalls_0;
  MPE_Log_event( MPI_Iprobe_stateid_0*2,
	         MPI_Iprobe_ncalls_0, (char *)0 );
  
  returnVal = PMPI_Iprobe( source, tag, comm, flag, status );

  MPE_Log_event( MPI_Iprobe_stateid_0*2+1,
	         MPI_Iprobe_ncalls_0, (char *)0 );


  return returnVal;
}

int  MPI_Irecv( buf, count, datatype, source, tag, comm, request )
void * buf;
int count;
MPI_Datatype datatype;
int source;
int tag;
MPI_Comm comm;
MPI_Request * request;
{
  int  returnVal;

/*
    MPI_Irecv - prototyping replacement for MPI_Irecv
    Log the beginning and ending of the time spent in MPI_Irecv calls.
*/

  ++MPI_Irecv_ncalls_0;
  MPE_Log_event( MPI_Irecv_stateid_0*2,
	         MPI_Irecv_ncalls_0, (char *)0 );
  
  returnVal = PMPI_Irecv( buf, count, datatype, source, tag, comm, request );

  MPE_Log_event( MPI_Irecv_stateid_0*2+1,
	         MPI_Irecv_ncalls_0, (char *)0 );


  return returnVal;
}

int  MPI_Irsend( buf, count, datatype, dest, tag, comm, request )
void * buf;
int count;
MPI_Datatype datatype;
int dest;
int tag;
MPI_Comm comm;
MPI_Request * request;
{
  int  returnVal;

/*
    MPI_Irsend - prototyping replacement for MPI_Irsend
    Log the beginning and ending of the time spent in MPI_Irsend calls.
*/

  ++MPI_Irsend_ncalls_0;
  MPE_Log_event( MPI_Irsend_stateid_0*2,
	         MPI_Irsend_ncalls_0, (char *)0 );
  
  returnVal = PMPI_Irsend( buf, count, datatype, dest, tag, comm, request );

  MPE_Log_event( MPI_Irsend_stateid_0*2+1,
	         MPI_Irsend_ncalls_0, (char *)0 );


  return returnVal;
}

int  MPI_Isend( buf, count, datatype, dest, tag, comm, request )
void * buf;
int count;
MPI_Datatype datatype;
int dest;
int tag;
MPI_Comm comm;
MPI_Request * request;
{
  int  returnVal;

/*
    MPI_Isend - prototyping replacement for MPI_Isend
    Log the beginning and ending of the time spent in MPI_Isend calls.
*/

  ++MPI_Isend_ncalls_0;
  MPE_Log_event( MPI_Isend_stateid_0*2,
	         MPI_Isend_ncalls_0, (char *)0 );
  
  returnVal = PMPI_Isend( buf, count, datatype, dest, tag, comm, request );

  MPE_Log_event( MPI_Isend_stateid_0*2+1,
	         MPI_Isend_ncalls_0, (char *)0 );


  return returnVal;
}

int  MPI_Issend( buf, count, datatype, dest, tag, comm, request )
void * buf;
int count;
MPI_Datatype datatype;
int dest;
int tag;
MPI_Comm comm;
MPI_Request * request;
{
  int  returnVal;

/*
    MPI_Issend - prototyping replacement for MPI_Issend
    Log the beginning and ending of the time spent in MPI_Issend calls.
*/

  ++MPI_Issend_ncalls_0;
  MPE_Log_event( MPI_Issend_stateid_0*2,
	         MPI_Issend_ncalls_0, (char *)0 );
  
  returnVal = PMPI_Issend( buf, count, datatype, dest, tag, comm, request );

  MPE_Log_event( MPI_Issend_stateid_0*2+1,
	         MPI_Issend_ncalls_0, (char *)0 );


  return returnVal;
}

int  MPI_Probe( source, tag, comm, status )
int source;
int tag;
MPI_Comm comm;
MPI_Status * status;
{
  int  returnVal;

/*
    MPI_Probe - prototyping replacement for MPI_Probe
    Log the beginning and ending of the time spent in MPI_Probe calls.
*/

  ++MPI_Probe_ncalls_0;
  MPE_Log_event( MPI_Probe_stateid_0*2,
	         MPI_Probe_ncalls_0, (char *)0 );
  
  returnVal = PMPI_Probe( source, tag, comm, status );

  MPE_Log_event( MPI_Probe_stateid_0*2+1,
	         MPI_Probe_ncalls_0, (char *)0 );


  return returnVal;
}

int  MPI_Recv( buf, count, datatype, source, tag, comm, status )
void * buf;
int count;
MPI_Datatype datatype;
int source;
int tag;
MPI_Comm comm;
MPI_Status * status;
{
  int  returnVal;

/*
    MPI_Recv - prototyping replacement for MPI_Recv
    Log the beginning and ending of the time spent in MPI_Recv calls.
*/

  ++MPI_Recv_ncalls_0;
  MPE_Log_event( MPI_Recv_stateid_0*2,
	         MPI_Recv_ncalls_0, (char *)0 );
  
  returnVal = PMPI_Recv( buf, count, datatype, source, tag, comm, status );

  MPE_Log_event( MPI_Recv_stateid_0*2+1,
	         MPI_Recv_ncalls_0, (char *)0 );


  return returnVal;
}

int  MPI_Rsend( buf, count, datatype, dest, tag, comm )
void * buf;
int count;
MPI_Datatype datatype;
int dest;
int tag;
MPI_Comm comm;
{
  int  returnVal;

/*
    MPI_Rsend - prototyping replacement for MPI_Rsend
    Log the beginning and ending of the time spent in MPI_Rsend calls.
*/

  ++MPI_Rsend_ncalls_0;
  MPE_Log_event( MPI_Rsend_stateid_0*2,
	         MPI_Rsend_ncalls_0, (char *)0 );
  
  returnVal = PMPI_Rsend( buf, count, datatype, dest, tag, comm );

  MPE_Log_event( MPI_Rsend_stateid_0*2+1,
	         MPI_Rsend_ncalls_0, (char *)0 );


  return returnVal;
}

int  MPI_Send( buf, count, datatype, dest, tag, comm )
void * buf;
int count;
MPI_Datatype datatype;
int dest;
int tag;
MPI_Comm comm;
{
  int  returnVal;

/*
    MPI_Send - prototyping replacement for MPI_Send
    Log the beginning and ending of the time spent in MPI_Send calls.
*/

  ++MPI_Send_ncalls_0;
  MPE_Log_event( MPI_Send_stateid_0*2,
	         MPI_Send_ncalls_0, (char *)0 );
  
  returnVal = PMPI_Send( buf, count, datatype, dest, tag, comm );

  MPE_Log_event( MPI_Send_stateid_0*2+1,
	         MPI_Send_ncalls_0, (char *)0 );


  return returnVal;
}

int  MPI_Sendrecv( sendbuf, sendcount, sendtype, dest, sendtag, recvbuf, recvcount, recvtype, source, recvtag, comm, status )
void * sendbuf;
int sendcount;
MPI_Datatype sendtype;
int dest;
int sendtag;
void * recvbuf;
int recvcount;
MPI_Datatype recvtype;
int source;
int recvtag;
MPI_Comm comm;
MPI_Status * status;
{
  int  returnVal;

/*
    MPI_Sendrecv - prototyping replacement for MPI_Sendrecv
    Log the beginning and ending of the time spent in MPI_Sendrecv calls.
*/

  ++MPI_Sendrecv_ncalls_0;
  MPE_Log_event( MPI_Sendrecv_stateid_0*2,
	         MPI_Sendrecv_ncalls_0, (char *)0 );
  
  returnVal = PMPI_Sendrecv( sendbuf, sendcount, sendtype, dest, sendtag, recvbuf, recvcount, recvtype, source, recvtag, comm, status );

  MPE_Log_event( MPI_Sendrecv_stateid_0*2+1,
	         MPI_Sendrecv_ncalls_0, (char *)0 );


  return returnVal;
}

int  MPI_Ssend( buf, count, datatype, dest, tag, comm )
void * buf;
int count;
MPI_Datatype datatype;
int dest;
int tag;
MPI_Comm comm;
{
  int  returnVal;

/*
    MPI_Ssend - prototyping replacement for MPI_Ssend
    Log the beginning and ending of the time spent in MPI_Ssend calls.
*/

  ++MPI_Ssend_ncalls_0;
  MPE_Log_event( MPI_Ssend_stateid_0*2,
	         MPI_Ssend_ncalls_0, (char *)0 );
  
  returnVal = PMPI_Ssend( buf, count, datatype, dest, tag, comm );

  MPE_Log_event( MPI_Ssend_stateid_0*2+1,
	         MPI_Ssend_ncalls_0, (char *)0 );


  return returnVal;
}

int   MPI_Test( request, flag, status )
MPI_Request * request;
int * flag;
MPI_Status * status;
{
  int   returnVal;

/*
    MPI_Test - prototyping replacement for MPI_Test
    Log the beginning and ending of the time spent in MPI_Test calls.
*/

  ++MPI_Test_ncalls_0;
  MPE_Log_event( MPI_Test_stateid_0*2,
	         MPI_Test_ncalls_0, (char *)0 );
  
  returnVal = PMPI_Test( request, flag, status );

  MPE_Log_event( MPI_Test_stateid_0*2+1,
	         MPI_Test_ncalls_0, (char *)0 );


  return returnVal;
}

int  MPI_Testall( count, array_of_requests, flag, array_of_statuses )
int count;
MPI_Request * array_of_requests;
int * flag;
MPI_Status * array_of_statuses;
{
  int  returnVal;

/*
    MPI_Testall - prototyping replacement for MPI_Testall
    Log the beginning and ending of the time spent in MPI_Testall calls.
*/

  ++MPI_Testall_ncalls_0;
  MPE_Log_event( MPI_Testall_stateid_0*2,
	         MPI_Testall_ncalls_0, (char *)0 );
  
  returnVal = PMPI_Testall( count, array_of_requests, flag, array_of_statuses );

  MPE_Log_event( MPI_Testall_stateid_0*2+1,
	         MPI_Testall_ncalls_0, (char *)0 );


  return returnVal;
}

int  MPI_Testany( count, array_of_requests, index, flag, status )
int count;
MPI_Request * array_of_requests;
int * index;
int * flag;
MPI_Status * status;
{
  int  returnVal;

/*
    MPI_Testany - prototyping replacement for MPI_Testany
    Log the beginning and ending of the time spent in MPI_Testany calls.
*/

  ++MPI_Testany_ncalls_0;
  MPE_Log_event( MPI_Testany_stateid_0*2,
	         MPI_Testany_ncalls_0, (char *)0 );
  
  returnVal = PMPI_Testany( count, array_of_requests, index, flag, status );

  MPE_Log_event( MPI_Testany_stateid_0*2+1,
	         MPI_Testany_ncalls_0, (char *)0 );


  return returnVal;
}

int  MPI_Testsome( incount, array_of_requests, outcount, array_of_indices, array_of_statuses )
int incount;
MPI_Request * array_of_requests;
int * outcount;
int * array_of_indices;
MPI_Status * array_of_statuses;
{
  int  returnVal;

/*
    MPI_Testsome - prototyping replacement for MPI_Testsome
    Log the beginning and ending of the time spent in MPI_Testsome calls.
*/

  ++MPI_Testsome_ncalls_0;
  MPE_Log_event( MPI_Testsome_stateid_0*2,
	         MPI_Testsome_ncalls_0, (char *)0 );
  
  returnVal = PMPI_Testsome( incount, array_of_requests, outcount, array_of_indices, array_of_statuses );

  MPE_Log_event( MPI_Testsome_stateid_0*2+1,
	         MPI_Testsome_ncalls_0, (char *)0 );


  return returnVal;
}

int   MPI_Wait( request, status )
MPI_Request * request;
MPI_Status * status;
{
  int   returnVal;

/*
    MPI_Wait - prototyping replacement for MPI_Wait
    Log the beginning and ending of the time spent in MPI_Wait calls.
*/

  ++MPI_Wait_ncalls_0;
  MPE_Log_event( MPI_Wait_stateid_0*2,
	         MPI_Wait_ncalls_0, (char *)0 );
  
  returnVal = PMPI_Wait( request, status );

  MPE_Log_event( MPI_Wait_stateid_0*2+1,
	         MPI_Wait_ncalls_0, (char *)0 );


  return returnVal;
}

int  MPI_Waitall( count, array_of_requests, array_of_statuses )
int count;
MPI_Request * array_of_requests;
MPI_Status * array_of_statuses;
{
  int  returnVal;

/*
    MPI_Waitall - prototyping replacement for MPI_Waitall
    Log the beginning and ending of the time spent in MPI_Waitall calls.
*/

  ++MPI_Waitall_ncalls_0;
  MPE_Log_event( MPI_Waitall_stateid_0*2,
	         MPI_Waitall_ncalls_0, (char *)0 );
  
  returnVal = PMPI_Waitall( count, array_of_requests, array_of_statuses );

  MPE_Log_event( MPI_Waitall_stateid_0*2+1,
	         MPI_Waitall_ncalls_0, (char *)0 );


  return returnVal;
}

int  MPI_Waitany( count, array_of_requests, index, status )
int count;
MPI_Request * array_of_requests;
int * index;
MPI_Status * status;
{
  int  returnVal;

/*
    MPI_Waitany - prototyping replacement for MPI_Waitany
    Log the beginning and ending of the time spent in MPI_Waitany calls.
*/

  ++MPI_Waitany_ncalls_0;
  MPE_Log_event( MPI_Waitany_stateid_0*2,
	         MPI_Waitany_ncalls_0, (char *)0 );
  
  returnVal = PMPI_Waitany( count, array_of_requests, index, status );

  MPE_Log_event( MPI_Waitany_stateid_0*2+1,
	         MPI_Waitany_ncalls_0, (char *)0 );


  return returnVal;
}

int  MPI_Waitsome( incount, array_of_requests, outcount, array_of_indices, array_of_statuses )
int incount;
MPI_Request * array_of_requests;
int * outcount;
int * array_of_indices;
MPI_Status * array_of_statuses;
{
  int  returnVal;

/*
    MPI_Waitsome - prototyping replacement for MPI_Waitsome
    Log the beginning and ending of the time spent in MPI_Waitsome calls.
*/

  ++MPI_Waitsome_ncalls_0;
  MPE_Log_event( MPI_Waitsome_stateid_0*2,
	         MPI_Waitsome_ncalls_0, (char *)0 );
  
  returnVal = PMPI_Waitsome( incount, array_of_requests, outcount, array_of_indices, array_of_statuses );

  MPE_Log_event( MPI_Waitsome_stateid_0*2+1,
	         MPI_Waitsome_ncalls_0, (char *)0 );


  return returnVal;
}
