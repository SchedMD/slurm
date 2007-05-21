#include <stdio.h>

#include "mpi.h"
#include "mpptest.h"
#include "getopts.h"

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#if defined(HAVE_SHMALLOC) && !defined(HAVE_MPI_ALLOC_MEM)
#include <mpp/shmem.h>
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif

/*****************************************************************************

  Each collection of test routines contains:

  Initialization function (returns pointer to context to pass back to test
  functions

  Routine to change "distance"

  Routine to return test function (and set parameters) based on command-line
  arguments

  Routine to output "help" text

 *****************************************************************************/


/*****************************************************************************

 Here begin the test functions.  These all have the same format:

 double NAME(reps, len, ctx)
 
 Input Parameters:
. reps - number of times to perform operation
. len  - length of message (in bytes)
. ctx  - Pointer to structure containing ranks of participating processes

 Return value:
 Elapsed time for operation (not Elapsed time/reps), in seconds.

 These are organized as:
 head-to-head (each process sends to the other).  The blocking version
 can deadlock on systems with small amounts of buffering.

 round-trip (a single message is sent back and fourth between two nodes)

 In order to test both single and multiple senders and receivers, the 
 destination (partner) node is also set, and whether the node is a master or
 a slave (it may also be a bystander)
 *****************************************************************************/

#include "mpptest.h"

#if VARIABLE_TAG
#define MSG_TAG(iter) iter
#else
#define MSG_TAG(iter) 1
#endif

struct _PairData {
    int  proc1, proc2;
    int  source, destination,    /* Source and destination.  May be the
				    same as partner (for pair) or different
				    (for ring) */
         partner;                /* = source = destination if same */
    int  is_master, is_slave;
    };

static int CacheSize = 1048576;

void PairChange( int, PairData);
void ConfirmTest( int, int, PairData );

PairData PairInit( int proc1, int proc2 )
{
    PairData new;

    new	       = (PairData)malloc(sizeof(struct _PairData));   
    if (!new)return 0;;
    PairChange( 1, new );
    return new;
}

void PairChange( int distance, PairData ctx )
{
    int proc2;

    if (__MYPROCID == 0) {
	proc2 = GetNeighbor( 0, distance, 1 );
    }
    else {
	proc2 = GetNeighbor( __MYPROCID, distance, 0 );
	if (proc2 == 0) {
	    /* Then I'm the slave for the root */
	    proc2 = __MYPROCID;
	}
	else {
	    proc2 = NO_NBR;
	}
    }

    ctx->proc1     = 0;
    ctx->proc2     = proc2;
    ctx->is_master = __MYPROCID == ctx->proc1;
    ctx->is_slave  = __MYPROCID == proc2;
    if (ctx->is_master) {
	ctx->partner     = proc2;
	ctx->destination = proc2;
	ctx->source      = proc2;
    }
    else if (ctx->is_slave) {
	ctx->partner     = ctx->proc1;
	ctx->destination = ctx->proc1;
	ctx->source      = ctx->proc1;
    }
    else {
	ctx->partner     = NO_NBR;
	ctx->source	     = NO_NBR;
	ctx->destination = NO_NBR;
    }
}

/* Bisection test can be done by involving all processes in the 
   communication.

   In order to insure that we generate a valid pattern, I create an array
   with an entry for each processor.  Starting from position zero, I mark
   masters, slaves, and ununsed.  Each new entry is marked as a master, 
   with the destination partner marked as a slave.  
 */
PairData BisectInit( int distance )
{
    PairData new;

    new	       = (PairData)malloc(sizeof(struct _PairData));   
    if (!new)return 0;;

    BisectChange( distance, new );

    return new;
}

void BisectChange( int distance, PairData ctx )
{
    int      i, np;
    int      *marks, curpos;
    int      partner;

    np    = __NUMNODES;
    marks = (int *)malloc((unsigned)(np * sizeof(int) ));   
    if (!marks) MPI_Abort( MPI_COMM_WORLD, 1 );
    for (i=0; i<np; i++) {
	marks[i] = NO_NBR;
    }
    curpos = 0;
    while (curpos < np) {
	partner = GetNeighbor( curpos, distance, 1 );
	if (marks[curpos] == NO_NBR && marks[partner] == NO_NBR) {
	    marks[curpos]  = 1;
	    marks[partner] = 2;
	}
	curpos++;
    }

    ctx->proc1     = NO_NBR;
    ctx->proc2     = NO_NBR;
    ctx->is_master = marks[__MYPROCID] == 1;
    ctx->is_slave  = marks[__MYPROCID] == 2;
    if (ctx->is_master) {
	ctx->partner = GetNeighbor( __MYPROCID, distance, 1 );
	ctx->destination = ctx->partner;
	ctx->source      = ctx->partner;
    }
    else if (ctx->is_slave) {
	ctx->partner = GetNeighbor( __MYPROCID, distance, 0 );
	ctx->destination = ctx->partner;
	ctx->source      = ctx->partner;
    }
    else {
	ctx->partner     = NO_NBR;
	ctx->destination = NO_NBR;
	ctx->source      = NO_NBR;
    }
    free(marks);
}

/* Print information on the ctx */
void PrintPairInfo( PairData ctx )
{
    MPE_Seq_begin(MPI_COMM_WORLD,1 );
    fprintf( stdout, "[%d] sending to %d, %s\n", __MYPROCID, ctx->partner, 
	     (ctx->is_master || ctx->is_slave) ? 
	     ( ctx->is_master ? "Master" : "Slave" ) : "Bystander" );
    fflush( stdout );
    MPE_Seq_end(MPI_COMM_WORLD,1 );
}

typedef enum { HEADtoHEAD, ROUNDTRIP } CommType;
typedef enum { Blocking, NonBlocking, ReadyReceiver, MPISynchronous, 
	       Persistant, Vector, VectorType, Put, Get } 
               Protocol;
typedef enum { SpecifiedSource, AnySource } SourceType;
static SourceType source_type = AnySource;
static int MsgPending = 0;

double exchange_forcetype( int, int, PairData );
double exchange_async( int, int, PairData );
double exchange_sync( int, int, PairData );
double exchange_ssend( int, int, PairData );

double round_trip_sync( int, int, PairData );
double round_trip_force( int, int, PairData );
double round_trip_async( int, int, PairData );
double round_trip_ssend( int, int, PairData );
double round_trip_persis( int, int, PairData );
double round_trip_vector( int, int, PairData );
double round_trip_vectortype( int, int, PairData );

double round_trip_nc_sync( int, int, PairData );
double round_trip_nc_force( int, int, PairData );
double round_trip_nc_async( int, int, PairData );

#if ! defined(HAVE_MPI_PUT)
#define round_trip_put 0
#define round_trip_nc_put 0
#define exchange_put 0
#else
double exchange_put( int, int, PairData );
double round_trip_put( int, int, PairData );
double round_trip_nc_put( int, int, PairData );
#endif

#if ! defined(HAVE_MPI_GET)
#define round_trip_get 0
#define round_trip_nc_get 0
#define exchange_get 0
#else
double exchange_get( int, int, PairData );
double round_trip_get( int, int, PairData );
double round_trip_nc_get( int, int, PairData );
#endif

static void SetupTest( int );
static void FinishTest( void );

/* Determine the timing function */
double ((*GetPairFunction( int *argc, char *argv[], char *protocol_name )) ( int, int, void * ))
{
    CommType comm_type     = ROUNDTRIP;
    Protocol protocol      = Blocking;

    double (*f)(int,int,PairData);
    int      use_cache;

    f             = round_trip_sync;

    if (SYArgHasName( argc, argv, 1, "-force" )) {
	protocol      = ReadyReceiver;
	strcpy( protocol_name, "ready receiver" );
    }
    if (SYArgHasName( argc, argv, 1, "-async" )) {
	protocol      = NonBlocking;
	strcpy( protocol_name, "nonblocking" ); 
    }
    if (SYArgHasName( argc, argv, 1, "-sync"  )) {
	protocol      = Blocking;
	strcpy( protocol_name, "blocking" );
    }
    if (SYArgHasName( argc, argv, 1, "-ssend"  )) {
	protocol      = MPISynchronous;
	strcpy( protocol_name, "Ssend" );
    }
    if (SYArgHasName( argc, argv, 1, "-put"  )) {
	protocol      = Put;
	strcpy( protocol_name, "MPI_Put" );
    }
    if (SYArgHasName( argc, argv, 1, "-get"  )) {
	protocol      = Get;
	strcpy( protocol_name, "MPI_Get" );
    }
    if (SYArgHasName( argc, argv, 1, "-persistant"  )) {
	protocol      = Persistant;
	strcpy( protocol_name, "persistant" );
    }
    if (SYArgHasName( argc, argv, 1, "-vector"  )) {
	int stride;
	protocol      = Vector;
	strcpy( protocol_name, "vector" );
	if (SYArgGetInt( argc, argv, 1, "-vstride", &stride ))
	    set_vector_stride( stride );
    }
    if (SYArgHasName( argc, argv, 1, "-vectortype"  )) {
	int stride;
	protocol      = VectorType;
	strcpy( protocol_name, "type_vector" );
	if (SYArgGetInt( argc, argv, 1, "-vstride", &stride ))
	    set_vector_stride( stride );
    }
    if (SYArgHasName( argc, argv, 1, "-anysource" )) {
	source_type = AnySource;
    }
    if (SYArgHasName( argc, argv, 1, "-specified" )) {
	source_type = SpecifiedSource;
	strcat( protocol_name, "(specified source)" );
    }
    if (SYArgHasName( argc, argv, 1, "-pending" )) {
	MsgPending = 1;
	strcat( protocol_name, "(pending recvs)" );
    }
    use_cache = SYArgGetInt( argc, argv, 1, "-cachesize", &CacheSize );
    if (SYArgHasName( argc, argv, 1, "-head"  ))     comm_type = HEADtoHEAD;
    if (SYArgHasName( argc, argv, 1, "-roundtrip" )) comm_type = ROUNDTRIP;

    if (comm_type == ROUNDTRIP) {
	if (use_cache) {
	    switch( protocol ) {
	    case ReadyReceiver: f = round_trip_nc_force; break;
	    case NonBlocking:   f = round_trip_nc_async; break;
	    case Blocking:      f = round_trip_nc_sync;  break;
	    case Put:           f = round_trip_nc_put;   break;
	    case Get:           f = round_trip_nc_get;   break;
		/* Rolling through the cache means using different buffers
		   for each op; not doable with persistent requests */
	    case Persistant:    f = 0;                   break;
	    case Vector:        f = 0;                   break;
	    case VectorType:    f = 0;                   break;
	    default:            f = 0;                   break;
	    }
	}
	else {
	    switch( protocol ) {
	    case ReadyReceiver: f = round_trip_force;      break;
	    case NonBlocking:   f = round_trip_async;      break;
	    case Blocking:      f = round_trip_sync;       break;
	    case MPISynchronous:f = round_trip_ssend;      break;
	    case Put:           f = round_trip_put;        break;
	    case Get:           f = round_trip_get;        break;
	    case Persistant:    f = round_trip_persis;     break;
	    case Vector:        f = round_trip_vector;     break;
	    case VectorType:    f = round_trip_vectortype; break;
	    }
	}
    }
    else {
	switch( protocol ) {
	case ReadyReceiver: f = exchange_forcetype; break;
	case NonBlocking:   f = exchange_async;     break;
	case Blocking:      f = exchange_sync;      break;
	case MPISynchronous:f = exchange_ssend;     break;
	case Put:           f = exchange_put;       break;
	case Get:           f = exchange_get;       break;
	case Persistant:    f = 0;                  break;
	case Vector:        f = 0;                  break;
	case VectorType:    f = 0;                  break;
	}
    }
    if (!f) {
	fprintf( stderr, "Option %s not supported\n", protocol_name );
	MPI_Abort( MPI_COMM_WORLD, 1 );
    }
    return (double (*)(int,int,void*)) f;
}

/*****************************************************************************
 Here are the actual routines
 *****************************************************************************/
/* 
   Blocking exchange (head-to-head) 
*/
double exchange_sync( int reps, int len, PairData ctx)
{
  double elapsed_time;
  int  i, to = ctx->destination, from = ctx->source;
  int  recv_from;
  char *sbuffer, *rbuffer;
  double t0, t1;
  MPI_Status status;

  sbuffer = (char *)malloc(len);
  rbuffer = (char *)malloc(len);
  memset( sbuffer, 0, len );
  memset( rbuffer, 0, len );

  SetupTest( from );

  ConfirmTest( reps, len, ctx );

  elapsed_time = 0;
  if(ctx->is_master){
    recv_from = MPI_ANY_SOURCE;
    if (source_type == SpecifiedSource) recv_from = to;
    MPI_Recv(rbuffer,len,MPI_BYTE,recv_from,0,MPI_COMM_WORLD,&status);
    t0=MPI_Wtime();
    for(i=0;i<reps;i++){
      MPI_Send(sbuffer,len,MPI_BYTE,to,MSG_TAG(i),MPI_COMM_WORLD);
      MPI_Recv(rbuffer,len,MPI_BYTE,recv_from,MSG_TAG(i),
	       MPI_COMM_WORLD,&status);
    }
    t1 = MPI_Wtime();
    elapsed_time = t1-t0;
  }

  if(ctx->is_slave){
    recv_from = MPI_ANY_SOURCE;
    if (source_type == SpecifiedSource) recv_from = to;
    MPI_Send(sbuffer,len,MPI_BYTE,from,0,MPI_COMM_WORLD);
    for(i=0;i<reps;i++){
      MPI_Send(sbuffer,len,MPI_BYTE,to,MSG_TAG(i),MPI_COMM_WORLD);
      MPI_Recv(rbuffer,len,MPI_BYTE,recv_from,MSG_TAG(i),
	       MPI_COMM_WORLD,&status);
    }
  }

  FinishTest();
  free(sbuffer);
  free(rbuffer);
  return(elapsed_time);
}

/* 
   Nonblocking exchange (head-to-head) 
 */
double exchange_async( int reps, int len, PairData ctx)
{
  double         elapsed_time;
  int            i, to = ctx->destination, from = ctx->source;
  int            recv_from;
  MPI_Request  msg_id;
  char           *sbuffer,*rbuffer;
  double   t0, t1;
  MPI_Status status;

  sbuffer = (char *)malloc(len);
  rbuffer = (char *)malloc(len);
  memset( sbuffer, 0, len );
  memset( rbuffer, 0, len );

  SetupTest( from );
  ConfirmTest( reps, len, ctx );
  elapsed_time = 0;
  if(ctx->is_master){
    recv_from = MPI_ANY_SOURCE;
    if (source_type == SpecifiedSource) recv_from = to;
    MPI_Recv(rbuffer,len,MPI_BYTE,recv_from,0,MPI_COMM_WORLD,&status);  	
    t0=MPI_Wtime();
    for(i=0;i<reps;i++){
      MPI_Irecv(rbuffer,len,MPI_BYTE,recv_from,MSG_TAG(i),
		MPI_COMM_WORLD,&msg_id);
      MPI_Send(sbuffer,len,MPI_BYTE,to,MSG_TAG(i),MPI_COMM_WORLD);
      MPI_Wait(&(msg_id),&status);
    }
    t1=MPI_Wtime();
    elapsed_time = t1-t0;
  }

  if(ctx->is_slave){
    recv_from = MPI_ANY_SOURCE;
    if (source_type == SpecifiedSource) recv_from = to;
    MPI_Send(sbuffer,len,MPI_BYTE,from,0,MPI_COMM_WORLD);
    for(i=0;i<reps;i++){
      MPI_Irecv(rbuffer,len,MPI_BYTE,recv_from,MSG_TAG(i),
		MPI_COMM_WORLD,&msg_id);
      MPI_Send(sbuffer,len,MPI_BYTE,to,MSG_TAG(i),MPI_COMM_WORLD);
      MPI_Wait(&(msg_id),&status);
    }
  }

  FinishTest();
  free(sbuffer);
  free(rbuffer);
  return(elapsed_time);
}

/* 
   Synchronous send exchange (head-to-head) 
 */
double exchange_ssend( int reps, int len, PairData ctx)
{
  double         elapsed_time;
  int            i, to = ctx->destination, from = ctx->source;
  int            recv_from;
  MPI_Request  msg_id;
  char           *sbuffer,*rbuffer;
  double   t0, t1;
  MPI_Status status;

  sbuffer = (char *)malloc(len);
  rbuffer = (char *)malloc(len);
  memset( sbuffer, 0, len );
  memset( rbuffer, 0, len );

  SetupTest( from );
  ConfirmTest( reps, len, ctx );
  elapsed_time = 0;
  if(ctx->is_master){
    recv_from = MPI_ANY_SOURCE;
    if (source_type == SpecifiedSource) recv_from = to;
    MPI_Recv(rbuffer,len,MPI_BYTE,recv_from,0,MPI_COMM_WORLD,&status);  	
    t0=MPI_Wtime();
    for(i=0;i<reps;i++){
      MPI_Irecv(rbuffer,len,MPI_BYTE,recv_from,MSG_TAG(i),
		MPI_COMM_WORLD,&msg_id);
      MPI_Ssend(sbuffer,len,MPI_BYTE,to,MSG_TAG(i),MPI_COMM_WORLD);
      MPI_Wait(&(msg_id),&status);
    }
    t1=MPI_Wtime();
    elapsed_time = t1-t0;
  }

  if(ctx->is_slave){
    recv_from = MPI_ANY_SOURCE;
    if (source_type == SpecifiedSource) recv_from = to;
    MPI_Send(sbuffer,len,MPI_BYTE,from,0,MPI_COMM_WORLD);
    for(i=0;i<reps;i++){
      MPI_Irecv(rbuffer,len,MPI_BYTE,recv_from,MSG_TAG(i),
		MPI_COMM_WORLD,&msg_id);
      MPI_Ssend(sbuffer,len,MPI_BYTE,to,MSG_TAG(i),MPI_COMM_WORLD);
      MPI_Wait(&(msg_id),&status);
    }
  }

  FinishTest();
  free(sbuffer);
  free(rbuffer);
  return(elapsed_time);
}

/* 
   head-to-head exchange using forcetypes.  This uses null messages to
   let the sender know when the receive is ready 
 */
double exchange_forcetype( int reps, int len, PairData ctx)
{
  double         elapsed_time;
  int            i, d1, *dmy = &d1, 
                 to = ctx->destination, from = ctx->source;
  int            recv_from;
  MPI_Request  msg_id;
  MPI_Status   status;
  char           *sbuffer,*rbuffer;
  double   t0, t1;

  sbuffer = (char *)malloc(len);
  rbuffer = (char *)malloc(len);
  memset( sbuffer, 0, len );
  memset( rbuffer, 0, len );

  SetupTest( from );
  ConfirmTest( reps, len, ctx );
  elapsed_time = 0;
  if(ctx->is_master){
    recv_from = MPI_ANY_SOURCE;
    if (source_type == SpecifiedSource) recv_from = to;
    MPI_Recv(rbuffer,len,MPI_BYTE,recv_from,3,MPI_COMM_WORLD,&status);
    t0=MPI_Wtime();
    for(i=0;i<reps;i++){
      MPI_Irecv(rbuffer,len,MPI_BYTE,recv_from,0,MPI_COMM_WORLD,&(msg_id));
      MPI_Send(NULL,0,MPI_BYTE,to,2,MPI_COMM_WORLD);
      MPI_Recv(dmy,0,MPI_BYTE,recv_from,2,MPI_COMM_WORLD,&status);
      MPI_Rsend(sbuffer,len,MPI_BYTE,to,0,MPI_COMM_WORLD);
      MPI_Wait(&(msg_id),&status);
    }
    t1=MPI_Wtime();
    elapsed_time = t1-t0;
  }

  if(ctx->is_slave){
    recv_from = MPI_ANY_SOURCE;
    if (source_type == SpecifiedSource) recv_from = to;
    MPI_Send(sbuffer,len,MPI_BYTE,from,3,MPI_COMM_WORLD);
    for(i=0;i<reps;i++){
      MPI_Irecv(rbuffer,len,MPI_BYTE,recv_from,0,MPI_COMM_WORLD,&(msg_id));
      MPI_Send(NULL,0,MPI_BYTE,to,2,MPI_COMM_WORLD);
      MPI_Recv(dmy,0,MPI_BYTE,recv_from,2,MPI_COMM_WORLD,&status);
      MPI_Rsend(sbuffer,len,MPI_BYTE,to,0,MPI_COMM_WORLD);
      MPI_Wait(&(msg_id),&status);
    }
  }

  FinishTest();
  free(sbuffer);
  free(rbuffer);
  return(elapsed_time);
}

/* 
   Blocking round trip (always unidirectional) 
 */
double round_trip_sync( int reps, int len, PairData ctx)
{
  double elapsed_time;
  int  i, to = ctx->destination, from = ctx->source;
  int  recv_from;
  char *rbuffer,*sbuffer;
  MPI_Status status;
  double t0, t1;

  sbuffer = (char *)malloc(len);
  rbuffer = (char *)malloc(len);
  memset( sbuffer, 0, len );
  memset( rbuffer, 0, len );

  SetupTest( from );
  ConfirmTest( reps, len, ctx );
  elapsed_time = 0;
  if(ctx->is_master){
    recv_from = MPI_ANY_SOURCE;
    if (source_type == SpecifiedSource) recv_from = to;
    MPI_Recv(rbuffer,len,MPI_BYTE,recv_from,0,MPI_COMM_WORLD,&status);
    t0=MPI_Wtime();
    for(i=0;i<reps;i++){
      MPI_Send(sbuffer,len,MPI_BYTE,to,MSG_TAG(i),MPI_COMM_WORLD);
      MPI_Recv(rbuffer,len,MPI_BYTE,recv_from,MSG_TAG(i),
	       MPI_COMM_WORLD,&status);
    }
    t1=MPI_Wtime();
    elapsed_time = t1 -t0;
  }

  if(ctx->is_slave){
    recv_from = MPI_ANY_SOURCE;
    if (source_type == SpecifiedSource) recv_from = to;
    MPI_Send(sbuffer,len,MPI_BYTE,from,0,MPI_COMM_WORLD);
    for(i=0;i<reps;i++){
      MPI_Recv(rbuffer,len,MPI_BYTE,recv_from,MSG_TAG(i),
	       MPI_COMM_WORLD,&status);
      MPI_Send(sbuffer,len,MPI_BYTE,to,MSG_TAG(i),MPI_COMM_WORLD);
    }
  }

  FinishTest();
  free(sbuffer);
  free(rbuffer);
  return(elapsed_time);
}

/* 
   Synchrouous round trip (always unidirectional) 
 */
double round_trip_ssend( int reps, int len, PairData ctx)
{
  double elapsed_time;
  int  i, to = ctx->destination, from = ctx->source;
  int  recv_from;
  char *rbuffer,*sbuffer;
  MPI_Status status;
  double t0, t1;

  sbuffer = (char *)malloc(len);
  rbuffer = (char *)malloc(len);
  memset( sbuffer, 0, len );
  memset( rbuffer, 0, len );

  SetupTest( from );
  ConfirmTest( reps, len, ctx );
  elapsed_time = 0;
  if(ctx->is_master){
    recv_from = MPI_ANY_SOURCE;
    if (source_type == SpecifiedSource) recv_from = to;
    MPI_Recv(rbuffer,len,MPI_BYTE,recv_from,0,MPI_COMM_WORLD,&status);
    t0=MPI_Wtime();
    for(i=0;i<reps;i++){
      MPI_Ssend(sbuffer,len,MPI_BYTE,to,MSG_TAG(i),MPI_COMM_WORLD);
      MPI_Recv(rbuffer,len,MPI_BYTE,recv_from,MSG_TAG(i),
	       MPI_COMM_WORLD,&status);
    }
    t1=MPI_Wtime();
    elapsed_time = t1 -t0;
  }

  if(ctx->is_slave){
    recv_from = MPI_ANY_SOURCE;
    if (source_type == SpecifiedSource) recv_from = to;
    MPI_Send(sbuffer,len,MPI_BYTE,from,0,MPI_COMM_WORLD);
    for(i=0;i<reps;i++){
      MPI_Recv(rbuffer,len,MPI_BYTE,recv_from,MSG_TAG(i),
	       MPI_COMM_WORLD,&status);
      MPI_Ssend(sbuffer,len,MPI_BYTE,to,MSG_TAG(i),MPI_COMM_WORLD);
    }
  }

  FinishTest();
  free(sbuffer);
  free(rbuffer);
  return(elapsed_time);
}

/* 
   Ready-receiver round trip
 */
double round_trip_force( int reps, int len, PairData ctx)
{
  double elapsed_time;
  int  i, to = ctx->destination, from = ctx->source;
  int  recv_from;
  char *rbuffer,*sbuffer;
  double t0, t1;
  MPI_Request rid;
  MPI_Status  status;

  sbuffer = (char *)malloc(len);
  rbuffer = (char *)malloc(len);
  memset( sbuffer, 0, len );
  memset( rbuffer, 0, len );

  SetupTest( from );
  ConfirmTest( reps, len, ctx );
  elapsed_time = 0;
  if(ctx->is_master){
    recv_from = MPI_ANY_SOURCE;
    if (source_type == SpecifiedSource) recv_from = to;
    MPI_Recv(rbuffer,len,MPI_BYTE,recv_from,0,MPI_COMM_WORLD,&status);
    t0=MPI_Wtime();
    for(i=0;i<reps;i++){
      MPI_Irecv(rbuffer,len,MPI_BYTE,recv_from,MSG_TAG(i),
		MPI_COMM_WORLD,&(rid));
      MPI_Rsend(sbuffer,len,MPI_BYTE,to,MSG_TAG(i),MPI_COMM_WORLD);
      MPI_Wait(&(rid),&status);
    }
    t1=MPI_Wtime();
    elapsed_time = t1 -t0;
  }

  if(ctx->is_slave){
    recv_from = MPI_ANY_SOURCE;
    if (source_type == SpecifiedSource) recv_from = to;
    MPI_Irecv(rbuffer,len,MPI_BYTE,recv_from,MSG_TAG(i),
	      MPI_COMM_WORLD,&(rid));
    MPI_Send(sbuffer,len,MPI_BYTE,from,0,MPI_COMM_WORLD);
    for(i=0;i<reps-1;i++){
      MPI_Wait(&(rid),&status);
      MPI_Irecv(rbuffer,len,MPI_BYTE,recv_from,MSG_TAG(i),
		MPI_COMM_WORLD,&(rid));
      MPI_Rsend(sbuffer,len,MPI_BYTE,to,MSG_TAG(i),MPI_COMM_WORLD);
    }
    MPI_Wait(&(rid),&status);
    MPI_Rsend(sbuffer,len,MPI_BYTE,to,MSG_TAG(i),MPI_COMM_WORLD);
  }

  FinishTest();
  free(sbuffer);
  free(rbuffer);
  return(elapsed_time);
}

/* 
   Nonblocking round trip
 */
double round_trip_async( int reps, int len, PairData ctx)
{
  double elapsed_time;
  int  i, to = ctx->destination, from = ctx->source;
  int  recv_from;
  char *rbuffer,*sbuffer;
  MPI_Status status;
  double t0, t1;
  MPI_Request rid;

  sbuffer = (char *)malloc(len);
  rbuffer = (char *)malloc(len);
  memset( sbuffer, 0, len );
  memset( rbuffer, 0, len );

  SetupTest( from );
  ConfirmTest( reps, len, ctx );
  elapsed_time = 0;
  if(ctx->is_master){
    recv_from = MPI_ANY_SOURCE;
    if (source_type == SpecifiedSource) recv_from = to;
    MPI_Recv(rbuffer,len,MPI_BYTE,recv_from,0,MPI_COMM_WORLD,&status);
    t0=MPI_Wtime();
    for(i=0;i<reps;i++){
      MPI_Irecv(rbuffer,len,MPI_BYTE,recv_from,MSG_TAG(i),
		MPI_COMM_WORLD,&(rid));
      MPI_Send(sbuffer,len,MPI_BYTE,to,MSG_TAG(i),MPI_COMM_WORLD);
      MPI_Wait(&(rid),&status);
    }
    t1=MPI_Wtime();
    elapsed_time = t1 -t0;
  }

  if(ctx->is_slave){
    recv_from = MPI_ANY_SOURCE;
    if (source_type == SpecifiedSource) recv_from = to;
    MPI_Irecv(rbuffer,len,MPI_BYTE,recv_from,MSG_TAG(i),
	      MPI_COMM_WORLD,&(rid));
    MPI_Send(sbuffer,len,MPI_BYTE,from,0,MPI_COMM_WORLD);
    for(i=0;i<reps-1;i++){
      MPI_Wait(&(rid),&status);
      MPI_Irecv(rbuffer,len,MPI_BYTE,recv_from,MSG_TAG(i),
		MPI_COMM_WORLD,&(rid));
      MPI_Send(sbuffer,len,MPI_BYTE,to,MSG_TAG(i),MPI_COMM_WORLD);
    }
    MPI_Wait(&(rid),&status);
    MPI_Send(sbuffer,len,MPI_BYTE,to,1,MPI_COMM_WORLD);
  }

  FinishTest();
  free(sbuffer);
  free(rbuffer);
  return(elapsed_time);
}

/* 
   Persistant communication (only in MPI) 
 */
double round_trip_persis( int reps, int len, PairData ctx)
{
  double elapsed_time;
  int  i, to = ctx->destination, from = ctx->source;
  int  recv_from;
  char *rbuffer,*sbuffer;
  double t0, t1;
  MPI_Request sid, rid, rq[2];
  MPI_Status status, statuses[2];

  sbuffer = (char *)malloc(len);
  rbuffer = (char *)malloc(len);
  memset( sbuffer, 0, len );
  memset( rbuffer, 0, len );

  SetupTest( from );
  ConfirmTest( reps, len, ctx );
  elapsed_time = 0;
  if(ctx->is_master){
    recv_from = MPI_ANY_SOURCE;
    if (source_type == SpecifiedSource) recv_from = to;

    MPI_Send_init( sbuffer, len, MPI_BYTE, to, 1, MPI_COMM_WORLD, &sid );
    MPI_Recv_init( rbuffer, len, MPI_BYTE, recv_from, 1, MPI_COMM_WORLD, &rid ); 
    rq[0] = rid;
    rq[1] = sid;
    MPI_Recv(rbuffer,len,MPI_BYTE,recv_from,0,MPI_COMM_WORLD,&status);
    t0=MPI_Wtime();
    for(i=0;i<reps;i++){
      MPI_Startall( 2, rq );
      MPI_Waitall( 2, rq, statuses );
    }
    t1=MPI_Wtime();
    elapsed_time = t1 -t0;
    MPI_Request_free( &rid );
    MPI_Request_free( &sid );
  }

  if(ctx->is_slave){
    recv_from = MPI_ANY_SOURCE;
    if (source_type == SpecifiedSource) recv_from = from;
    MPI_Send_init( sbuffer, len, MPI_BYTE, from, 1, MPI_COMM_WORLD, &sid );
    MPI_Recv_init( rbuffer, len, MPI_BYTE, recv_from, 1, MPI_COMM_WORLD, &rid );
    rq[0] = rid;
    rq[1] = sid;
    MPI_Start( &rid );
    MPI_Send(sbuffer,len,MPI_BYTE,from,0,MPI_COMM_WORLD);
    for(i=0;i<reps-1;i++){
      MPI_Wait( &rid, &status );
      MPI_Startall( 2, rq );
      MPI_Wait( &sid, &status );
    }
    MPI_Wait( &rid, &status );
    MPI_Start( &sid );
    MPI_Wait( &sid, &status );
    MPI_Request_free( &rid );
    MPI_Request_free( &sid );
  }

  FinishTest();
  free(sbuffer);
  free(rbuffer);
  return(elapsed_time);
}

static int VectorStride = 10;
int set_vector_stride( int n )
{
    VectorStride = n;
    return 0;
}

double round_trip_vector(int reps, int len, PairData ctx)
{
  double elapsed_time;
  int  i, to = ctx->destination, from = ctx->source;
  int  recv_from;
  unsigned datalen;
  double *rbuffer,*sbuffer;
  double t0, t1;
  MPI_Datatype vec, types[2];
  int          blens[2];
  MPI_Aint     displs[2];
  MPI_Status   status;
  MPI_Comm     comm;
  int          alloc_len;

  /* Adjust len to be in bytes */
  len = len / sizeof(double);
  alloc_len = len;
  if (len == 0) alloc_len++;

  comm = MPI_COMM_WORLD;
  blens[0] = 1; displs[0] = 0; types[0] = MPI_DOUBLE;
  blens[1] = 1; displs[1] = VectorStride * sizeof(double); types[1] = MPI_UB;
  MPI_Type_struct( 2, blens, displs, types, &vec );
  MPI_Type_commit( &vec );

  datalen = VectorStride * alloc_len * sizeof(double);
  sbuffer = (double *)malloc(datalen);
  rbuffer = (double *)malloc(datalen);
  if (!sbuffer)return 0;
  if (!rbuffer)return 0;
  memset( sbuffer, 0, datalen );
  memset( rbuffer, 0, datalen );

  SetupTest( from );
  ConfirmTest( reps, len, ctx );
  elapsed_time = 0;
  if(ctx->is_master){
    recv_from = MPI_ANY_SOURCE;
    if (source_type == SpecifiedSource) recv_from = to;
    MPI_Recv( rbuffer, len, vec, recv_from, 0, comm, &status );
    t0=MPI_Wtime();
    for(i=0;i<reps;i++){
      MPI_Send( sbuffer, len, vec, to, MSG_TAG(i), comm );
      MPI_Recv( rbuffer, len, vec, recv_from, MSG_TAG(i), comm, &status );
      }
    t1=MPI_Wtime();
    elapsed_time = t1 - t0;
    }

  if(ctx->is_slave){
    recv_from = MPI_ANY_SOURCE;
    if (source_type == SpecifiedSource) recv_from = to;
    MPI_Send( sbuffer, len, vec, from, 0, comm );
    for(i=0;i<reps;i++){
	MPI_Recv( rbuffer, len, vec, recv_from, MSG_TAG(i), comm, &status );
	MPI_Send( sbuffer, len, vec, to, MSG_TAG(i), comm );
	}
    }

  FinishTest();
  free(sbuffer);
  free(rbuffer);
  MPI_Type_free( &vec );
  return(elapsed_time);
}

double round_trip_vectortype( int reps, int len, PairData ctx)
{
  double elapsed_time;
  int  i, to = ctx->destination, from = ctx->source;
  int  recv_from;
  unsigned datalen;
  double *rbuffer,*sbuffer;
  double t0, t1;
  MPI_Datatype vec;
  MPI_Status   status;
  MPI_Comm     comm;
  int          alloc_len;

  /* Adjust len to be in doubles */
  len = len / sizeof(double);
  alloc_len = len;
  if (len == 0) alloc_len++;

  comm = MPI_COMM_WORLD;
  MPI_Type_vector( len, 1, VectorStride, MPI_DOUBLE, &vec );
  MPI_Type_commit( &vec );

  datalen = VectorStride * alloc_len * sizeof(double);
  sbuffer = (double *)malloc(datalen);
  rbuffer = (double *)malloc(datalen);
  if (!sbuffer)return 0;
  if (!rbuffer)return 0;
  memset( sbuffer, 0, datalen );
  memset( rbuffer, 0, datalen );

  SetupTest( from );
  ConfirmTest( reps, len, ctx );
  elapsed_time = 0;
  if(ctx->is_master){
    recv_from = MPI_ANY_SOURCE;
    if (source_type == SpecifiedSource) recv_from = to;
    MPI_Recv( rbuffer, 1, vec, recv_from, 0, comm, &status );
    t0=MPI_Wtime();
    for(i=0;i<reps;i++){
      MPI_Send( sbuffer, 1, vec, to, MSG_TAG(i), comm );
      MPI_Recv( rbuffer, 1, vec, recv_from, MSG_TAG(i), comm, &status );
      }
    t1=MPI_Wtime();
    elapsed_time = t1 -t0;
    }

  if(ctx->is_slave){
    recv_from = MPI_ANY_SOURCE;
    if (source_type == SpecifiedSource) recv_from = to;
    MPI_Send( sbuffer, 1, vec, from, 0, comm );
    for(i=0;i<reps;i++){
	MPI_Recv( rbuffer, 1, vec, recv_from, MSG_TAG(i), comm, &status );
	MPI_Send( sbuffer, 1, vec, to, MSG_TAG(i), comm );
	}
    }

  FinishTest();
  free(sbuffer );
  free(rbuffer );
  MPI_Type_free( &vec );
  return(elapsed_time);
}
/*
    These versions try NOT to operate out of cache; rather, then send/receive
    into a moving window.
 */
/* 
   Blocking round trip (always unidirectional) 
 */
double round_trip_nc_sync( int reps, int len, PairData ctx)
{
    double elapsed_time;
    int  i, to = ctx->destination, from = ctx->source;
    int  recv_from;
    char *rbuffer,*sbuffer, *rp, *sp, *rlast, *slast;
    MPI_Status status;
    double t0, t1;

    sbuffer = (char *)malloc((unsigned)(2 * CacheSize ));
    slast   = sbuffer + 2 * CacheSize - len;
    rbuffer = (char *)malloc((unsigned)(2 * CacheSize ));
    rlast   = rbuffer + 2 * CacheSize - len;
    if (!sbuffer || !rbuffer) {
	fprintf( stderr, "Could not allocate %d bytes\n", 4 * CacheSize );
	exit(1 );
    }
    memset( sbuffer, 0, 2*CacheSize );
    memset( rbuffer, 0, 2*CacheSize );
    sp = sbuffer;
    rp = rbuffer;

    SetupTest( from );
    ConfirmTest( reps, len, ctx );
    elapsed_time = 0;
    if(ctx->is_master){
	recv_from = MPI_ANY_SOURCE;
	if (source_type == SpecifiedSource) recv_from = to;
	MPI_Recv(rbuffer,len,MPI_BYTE,recv_from,0,MPI_COMM_WORLD,&status);
	t0=MPI_Wtime();
	for(i=0;i<reps;i++){
	    MPI_Send(sp,len,MPI_BYTE,to,MSG_TAG(i),MPI_COMM_WORLD);
	    MPI_Recv(rp,len,MPI_BYTE,recv_from,MSG_TAG(i),
		     MPI_COMM_WORLD,&status);
	    sp += len;
	    rp += len;
	    if (sp > slast) sp = sbuffer;
	    if (rp > rlast) rp = rbuffer;
	}
	t1=MPI_Wtime();
	elapsed_time = t1 -t0;
    }

    if(ctx->is_slave){
	recv_from = MPI_ANY_SOURCE;
	if (source_type == SpecifiedSource) recv_from = to;
	MPI_Send(sbuffer,len,MPI_BYTE,from,0,MPI_COMM_WORLD);
	for(i=0;i<reps;i++){
	    MPI_Recv(rp,len,MPI_BYTE,recv_from,MSG_TAG(i),
		     MPI_COMM_WORLD,&status);
	    MPI_Send(sp,len,MPI_BYTE,to,MSG_TAG(i),MPI_COMM_WORLD);
	    sp += len;
	    rp += len;
	    if (sp > slast) sp = sbuffer;
	    if (rp > rlast) rp = rbuffer;
	}
    }

    FinishTest();
    free(sbuffer );
    free(rbuffer );
    return(elapsed_time);
}

/* 
   Ready-receiver round trip
 */
double round_trip_nc_force( int reps, int len, PairData ctx)
{
    double elapsed_time;
    int  i, to = ctx->destination, from = ctx->source;
    int  recv_from;
    char *rbuffer,*sbuffer, *rp, *sp, *rlast, *slast;
    double t0, t1;
    MPI_Request rid;
    MPI_Status  status;

    sbuffer = (char *)malloc((unsigned)(2 * CacheSize ));
    slast   = sbuffer + 2 * CacheSize - len;
    rbuffer = (char *)malloc((unsigned)(2 * CacheSize ));
    rlast   = rbuffer + 2 * CacheSize - len;
    if (!sbuffer || !rbuffer) {
	fprintf( stderr, "Could not allocate %d bytes\n", 4 * CacheSize );
	exit(1 );
    }
    sp = sbuffer;
    rp = rbuffer;
    memset( sbuffer, 0, 2*CacheSize );
    memset( rbuffer, 0, 2*CacheSize );

    SetupTest( from );
    ConfirmTest( reps, len, ctx );
    elapsed_time = 0;
    if(ctx->is_master){
	recv_from = MPI_ANY_SOURCE;
	if (source_type == SpecifiedSource) recv_from = to;
	MPI_Recv(rbuffer,len,MPI_BYTE,recv_from,0,MPI_COMM_WORLD,&status);
	t0=MPI_Wtime();
	for(i=0;i<reps;i++){
	    MPI_Irecv(rp,len,MPI_BYTE,recv_from,MSG_TAG(i),
		      MPI_COMM_WORLD,&(rid));
	    MPI_Rsend(sp,len,MPI_BYTE,to,MSG_TAG(i),MPI_COMM_WORLD);
	    MPI_Wait(&(rid),&status);
	    sp += len;
	    rp += len;
	    if (sp > slast) sp = sbuffer;
	    if (rp > rlast) rp = rbuffer;
	}
	t1=MPI_Wtime();
	elapsed_time = t1 -t0;
    }

    if(ctx->is_slave){
	recv_from = MPI_ANY_SOURCE;
	if (source_type == SpecifiedSource) recv_from = to;
	MPI_Irecv(rbuffer,len,MPI_BYTE,recv_from,MSG_TAG(i),
		  MPI_COMM_WORLD,&(rid));
	MPI_Send(sbuffer,len,MPI_BYTE,from,0,MPI_COMM_WORLD);
	for(i=0;i<reps-1;i++){
	    MPI_Wait(&(rid),&status);
	    rp += len;
	    if (rp > rlast) rp = rbuffer;
	    MPI_Irecv(rp,len,MPI_BYTE,recv_from,MSG_TAG(i),
		      MPI_COMM_WORLD,&(rid));
	    MPI_Rsend(sp,len,MPI_BYTE,to,MSG_TAG(i),MPI_COMM_WORLD);
	    sp += len;
	    if (sp > slast) sp = sbuffer;
	}
	MPI_Wait(&(rid),&status);
	MPI_Rsend(sp,len,MPI_BYTE,to,MSG_TAG(i),MPI_COMM_WORLD);
    }

    FinishTest();
    free(sbuffer );
    free(rbuffer );
    return(elapsed_time);
}

/* 
   Nonblocking round trip
 */
double round_trip_nc_async( int reps, int len, PairData ctx)
{
    double elapsed_time;
    int  i, to = ctx->destination, from = ctx->source;
    int  recv_from;
    char *rbuffer,*sbuffer, *rp, *sp, *rlast, *slast;
    double t0, t1;
    MPI_Request rid;
    MPI_Status  status;

    sbuffer = (char *)malloc((unsigned)(2 * CacheSize ));
    slast   = sbuffer + 2 * CacheSize - len;
    rbuffer = (char *)malloc((unsigned)(2 * CacheSize ));
    rlast   = rbuffer + 2 * CacheSize - len;
    if (!sbuffer || !rbuffer) {
	fprintf( stderr, "Could not allocate %d bytes\n", 4 * CacheSize );
	exit(1 );
    }
    sp = sbuffer;
    rp = rbuffer;
    memset( sbuffer, 0, 2*CacheSize );
    memset( rbuffer, 0, 2*CacheSize );

    SetupTest( from );
    ConfirmTest( reps, len, ctx );
    elapsed_time = 0;
    if(ctx->is_master){
	recv_from = MPI_ANY_SOURCE;
	if (source_type == SpecifiedSource) recv_from = to;
	MPI_Recv(rbuffer,len,MPI_BYTE,recv_from,0,MPI_COMM_WORLD,&status);
	t0=MPI_Wtime();
	for(i=0;i<reps;i++){
	    MPI_Irecv(rp,len,MPI_BYTE,recv_from,MSG_TAG(i),
		      MPI_COMM_WORLD,&(rid));
	    MPI_Send(sp,len,MPI_BYTE,to,MSG_TAG(i),MPI_COMM_WORLD);
	    MPI_Wait(&(rid),&status);
	    sp += len;
	    rp += len;
	    if (sp > slast) sp = sbuffer;
	    if (rp > rlast) rp = rbuffer;
	}
	t1=MPI_Wtime();
	elapsed_time = t1 -t0;
    }

    if(ctx->is_slave){
	recv_from = MPI_ANY_SOURCE;
	if (source_type == SpecifiedSource) recv_from = to;
	MPI_Irecv(rbuffer,len,MPI_BYTE,recv_from,MSG_TAG(i),
		  MPI_COMM_WORLD,&(rid));
	MPI_Send(sbuffer,len,MPI_BYTE,from,0,MPI_COMM_WORLD);
	for(i=0;i<reps-1;i++){
	    MPI_Wait(&(rid),&status);
	    rp += len;
	    if (rp > rlast) rp = rbuffer;
	    MPI_Irecv(rp,len,MPI_BYTE,recv_from,MSG_TAG(i),
		      MPI_COMM_WORLD,&(rid));
	    MPI_Send(sp,len,MPI_BYTE,to,MSG_TAG(i),MPI_COMM_WORLD);
	    sp += len;
	    if (sp > slast) sp = sbuffer;
	}
	MPI_Wait(&(rid),&status);
	MPI_Send(sp,len,MPI_BYTE,to,MSG_TAG(i),MPI_COMM_WORLD);
    }

    FinishTest();
    free(sbuffer );
    free(rbuffer );
    return(elapsed_time);
}

#ifdef HAVE_MPI_PUT
double exchange_put( int reps, int len, PairData ctx)
{
    double elapsed_time;
    int  i, to = ctx->destination, from = ctx->source;
    int  recv_from;
    char *sbuffer,*rbuffer;
    double t0, t1;
    MPI_Status status;
    MPI_Win    win;
    int        alloc_len;
  
    alloc_len = len;
    if (alloc_len == 0) alloc_len = sizeof(double);

#if defined(HAVE_SHMALLOC) && !defined(HAVE_MPI_ALLOC_MEM)
    sbuffer = (char *)shmalloc((unsigned)(alloc_len));
    rbuffer = (char *)shmalloc((unsigned)(alloc_len));
#else
    sbuffer = (char *)malloc((unsigned)(alloc_len));
    rbuffer = (char *)malloc((unsigned)(alloc_len));
#endif
    if (!sbuffer || !rbuffer) {
	fprintf( stderr, "Could not allocate %d bytes\n", alloc_len );
	exit(1 );
    }
    memset( sbuffer, 0, alloc_len );
    memset( rbuffer, 0, alloc_len );

    MPI_Win_create( rbuffer, len, 1, MPI_INFO_NULL, MPI_COMM_WORLD, &win );

    SetupTest( from );
    ConfirmTest( reps, len, ctx );

    elapsed_time = 0;
    if(ctx->is_master){
	recv_from = MPI_ANY_SOURCE;
	if (source_type == SpecifiedSource) recv_from = to;
	MPI_Recv(rbuffer,len,MPI_BYTE,recv_from,0,MPI_COMM_WORLD,&status);
	t0=MPI_Wtime();
	for(i=0;i<reps;i++){
	    MPI_Put( sbuffer, len, MPI_BYTE, to, 
		     0, len, MPI_BYTE, win );
	    MPI_Win_fence( 0, win );
	}
	t1 = MPI_Wtime();
	elapsed_time = t1-t0;
    }
    else if(ctx->is_slave){
	recv_from = MPI_ANY_SOURCE;
	if (source_type == SpecifiedSource) recv_from = to;
	MPI_Send(sbuffer,len,MPI_BYTE,from,0,MPI_COMM_WORLD);
	for(i=0;i<reps;i++){
	    MPI_Put( sbuffer, len, MPI_BYTE, from, 
		     0, len, MPI_BYTE, win );
	    MPI_Win_fence( 0, win );
	}
    }
    else {
	for(i=0;i<reps;i++){
	    MPI_Win_fence( 0, win );
	}
    }

    FinishTest();
    MPI_Win_free( &win );
#if defined(HAVE_SHMALLOC) && !defined(HAVE_MPI_ALLOC_MEM)
    shfree( sbuffer );
    shfree( rbuffer );
#else
    free(sbuffer );
    free(rbuffer );
#endif
    return(elapsed_time);
}
double round_trip_put( int reps, int len, PairData ctx)
{
    double elapsed_time;
    int  i, to = ctx->destination, from = ctx->source;
    int  recv_from;
    char *rbuffer,*sbuffer;
    double t0, t1;
    MPI_Win win;
    MPI_Status status;
    int alloc_len;

    alloc_len = len;
    if (alloc_len == 0) alloc_len = sizeof(double);

#if defined(HAVE_SHMALLOC) && !defined(HAVE_MPI_ALLOC_MEM)
    sbuffer = (char *)shmalloc((unsigned)(alloc_len));
    rbuffer = (char *)shmalloc((unsigned)(alloc_len));
#else
    sbuffer = (char *)malloc((unsigned)(alloc_len));
    rbuffer = (char *)malloc((unsigned)(alloc_len));
#endif
    if (!sbuffer || !rbuffer) {
	fprintf( stderr, "Could not allocate %d bytes\n", alloc_len );
	exit(1 );
    }
    memset( sbuffer, 0, alloc_len );
    memset( rbuffer, 0, alloc_len );

    MPI_Win_create( rbuffer, len, 1, MPI_INFO_NULL, MPI_COMM_WORLD, &win );

    SetupTest( from );
    ConfirmTest( reps, len, ctx );
    elapsed_time = 0;
    if(ctx->is_master){
	recv_from = MPI_ANY_SOURCE;
	if (source_type == SpecifiedSource) recv_from = to;
	MPI_Recv(rbuffer,len,MPI_BYTE,recv_from,0,MPI_COMM_WORLD,&status);
	t0=MPI_Wtime();
	for(i=0;i<reps;i++){
	    MPI_Put( sbuffer, len, MPI_BYTE, to, 
		     0, len, MPI_BYTE, win );
	    MPI_Win_fence( 0, win );
	    MPI_Win_fence( 0, win );
	}
	t1=MPI_Wtime();
	elapsed_time = t1 -t0;
    }

    else if(ctx->is_slave){
	recv_from = MPI_ANY_SOURCE;
	if (source_type == SpecifiedSource) recv_from = to;
	MPI_Send(sbuffer,len,MPI_BYTE,from,0,MPI_COMM_WORLD);
	for(i=0;i<reps;i++){
	    MPI_Win_fence( 0, win );
	    MPI_Put( sbuffer, len, MPI_BYTE, from, 
		     0, len, MPI_BYTE, win );
	    MPI_Win_fence( 0, win );
	}
    }
    else {
	for(i=0;i<reps;i++){
	    MPI_Win_fence( 0, win );
	    MPI_Win_fence( 0, win );
	}
    }

    FinishTest();
    MPI_Win_free( &win );
#if defined(HAVE_SHMALLOC) && !defined(HAVE_MPI_ALLOC_MEM)
    shfree( sbuffer );
    shfree( rbuffer );
#else
    free(sbuffer );
    free(rbuffer );
#endif
    return(elapsed_time);
}

double round_trip_nc_put( int reps, int len, PairData ctx)
{
    double elapsed_time;
    int  i, to = ctx->destination, from = ctx->source;
    int  recv_from;
    char *rbuffer,*sbuffer, *rp, *sp, *rlast, *slast;
    double t0, t1;
    MPI_Win win;
    MPI_Status status;

#if defined(HAVE_SHMALLOC) && !defined(HAVE_MPI_ALLOC_MEM)
    sbuffer = (char *)shmalloc((unsigned)(2 * CacheSize ));
    rbuffer = (char *)shmalloc((unsigned)(2 * CacheSize ));
#else
    sbuffer = (char *)malloc((unsigned)(2 * CacheSize ));
    rbuffer = (char *)malloc((unsigned)(2 * CacheSize ));
#endif
    slast   = sbuffer + 2 * CacheSize - len;
    rlast   = rbuffer + 2 * CacheSize - len;
    if (!sbuffer || !rbuffer) {
	fprintf( stderr, "Could not allocate %d bytes\n", 4 * CacheSize );
	exit(1 );
    }
    sp = sbuffer;
    rp = rbuffer;
    memset( sbuffer, 0, 2*CacheSize );
    memset( rbuffer, 0, 2*CacheSize );

    MPI_Win_create( rbuffer, len, 1, MPI_INFO_NULL, MPI_COMM_WORLD, &win );

    SetupTest( from );
    ConfirmTest( reps, len, ctx );
    elapsed_time = 0;
    if(ctx->is_master){
	recv_from = MPI_ANY_SOURCE;
	if (source_type == SpecifiedSource) recv_from = to;
	MPI_Recv(rbuffer,len,MPI_BYTE,recv_from,0,MPI_COMM_WORLD,&status);
	t0=MPI_Wtime();
	for(i=0;i<reps;i++){
	    MPI_Put( sp, len, MPI_BYTE, to, 
		     (int)(rp - rbuffer), len, MPI_BYTE, win );
	    MPI_Win_fence( 0, win );
	    MPI_Win_fence( 0, win );
	    sp += len;
	    rp += len;
	    if (sp > slast) sp = sbuffer;
	    if (rp > rlast) rp = rbuffer;
	}
	t1=MPI_Wtime();
	elapsed_time = t1 -t0;
    }

    else if(ctx->is_slave){
	recv_from = MPI_ANY_SOURCE;
	if (source_type == SpecifiedSource) recv_from = to;
	MPI_Send(sbuffer,len,MPI_BYTE,from,0,MPI_COMM_WORLD);
	for(i=0;i<reps;i++){
	    MPI_Win_fence( 0, win );
	    MPI_Put( sp, len, MPI_BYTE, from, 
		     (int)(rp - rbuffer), len, MPI_BYTE, win );
	    MPI_Win_fence( 0, win );
	    sp += len;
	    rp += len;
	    if (sp > slast) sp = sbuffer;
	    if (rp > rlast) rp = rbuffer;
	}
    }
    else {
	for(i=0;i<reps;i++){
	    MPI_Win_fence( 0, win );
	    MPI_Win_fence( 0, win );
	}
    }

    FinishTest();
    MPI_Win_free( &win );
#if defined(HAVE_SHMALLOC) && !defined(HAVE_MPI_ALLOC_MEM)
    shfree( sbuffer );
    shfree( rbuffer );
#else
    free(sbuffer );
    free(rbuffer );
#endif
    return(elapsed_time);
}
#endif

#ifdef HAVE_MPI_GET
double exchange_get( int reps, int len, PairData ctx)
{
    double elapsed_time;
    int  i, to = ctx->destination, from = ctx->source;
    int  recv_from;
    char *sbuffer,*rbuffer;
    double t0, t1;
    MPI_Status status;
    MPI_Win    win;
    int        alloc_len;
  
    alloc_len = len;
    if (alloc_len == 0) alloc_len = sizeof(double);

#if defined(HAVE_SHMALLOC) && !defined(HAVE_MPI_ALLOC_MEM)
    sbuffer = (char *)shmalloc((unsigned)(alloc_len));
    rbuffer = (char *)shmalloc((unsigned)(alloc_len));
#else
    sbuffer = (char *)malloc((unsigned)(alloc_len));
    rbuffer = (char *)malloc((unsigned)(alloc_len));
#endif
    if (!sbuffer || !rbuffer) {
	fprintf( stderr, "Could not allocate %d bytes\n", alloc_len );
	exit(1 );
    }
    memset( sbuffer, 0, alloc_len );
    memset( rbuffer, 0, alloc_len );

    MPI_Win_create( rbuffer, len, 1, MPI_INFO_NULL, MPI_COMM_WORLD, &win );

    SetupTest( from );
    ConfirmTest( reps, len, ctx );

    elapsed_time = 0;
    if(ctx->is_master){
	recv_from = MPI_ANY_SOURCE;
	if (source_type == SpecifiedSource) recv_from = to;
	MPI_Recv(rbuffer,len,MPI_BYTE,recv_from,0,MPI_COMM_WORLD,&status);
	t0=MPI_Wtime();
	for(i=0;i<reps;i++){
	    MPI_Get( rbuffer, len, MPI_BYTE, to, 
		     0, len, MPI_BYTE, win );
	    MPI_Win_fence( 0, win );
	}
	t1 = MPI_Wtime();
	elapsed_time = t1-t0;
    }
    else if(ctx->is_slave){
	recv_from = MPI_ANY_SOURCE;
	if (source_type == SpecifiedSource) recv_from = to;
	MPI_Send(sbuffer,len,MPI_BYTE,from,0,MPI_COMM_WORLD);
	for(i=0;i<reps;i++){
	    MPI_Get( rbuffer, len, MPI_BYTE, from, 
		     0, len, MPI_BYTE, win );
	    MPI_Win_fence( 0, win );
	}
    }
    else {
	for(i=0;i<reps;i++){
	    MPI_Win_fence( 0, win );
	}
    }

    FinishTest();
    MPI_Win_free( &win );
#if defined(HAVE_SHMALLOC) && !defined(HAVE_MPI_ALLOC_MEM)
    shfree( sbuffer );
    shfree( rbuffer );
#else
    free(sbuffer );
    free(rbuffer );
#endif
    return(elapsed_time);
}
double round_trip_get( int reps, int len, PairData ctx)
{
    double elapsed_time;
    int  i, to = ctx->destination, from = ctx->source;
    int  recv_from;
    char *rbuffer,*sbuffer;
    double t0, t1;
    MPI_Win win;
    MPI_Status status;
    int alloc_len;

    alloc_len = len;
    if (alloc_len == 0) alloc_len = sizeof(double);

#if defined(HAVE_SHMALLOC) && !defined(HAVE_MPI_ALLOC_MEM)
    sbuffer = (char *)shmalloc((unsigned)(alloc_len));
    rbuffer = (char *)shmalloc((unsigned)(alloc_len));
#else
    sbuffer = (char *)malloc((unsigned)(alloc_len));
    rbuffer = (char *)malloc((unsigned)(alloc_len));
#endif
    if (!sbuffer || !rbuffer) {
	fprintf( stderr, "Could not allocate %d bytes\n", alloc_len );
	exit(1 );
    }
    memset( sbuffer, 0, alloc_len );
    memset( rbuffer, 0, alloc_len );

    MPI_Win_create( rbuffer, len, 1, MPI_INFO_NULL, MPI_COMM_WORLD, &win );

    SetupTest( from );
    ConfirmTest( reps, len, ctx );
    elapsed_time = 0;
    if(ctx->is_master){
	recv_from = MPI_ANY_SOURCE;
	if (source_type == SpecifiedSource) recv_from = to;
	MPI_Recv(rbuffer,len,MPI_BYTE,recv_from,0,MPI_COMM_WORLD,&status);
	t0=MPI_Wtime();
	for(i=0;i<reps;i++){
	    MPI_Get( rbuffer, len, MPI_BYTE, to, 
		     0, len, MPI_BYTE, win );
	    MPI_Win_fence( 0, win );
	    MPI_Win_fence( 0, win );
	}
	t1=MPI_Wtime();
	elapsed_time = t1 -t0;
    }

    else if(ctx->is_slave){
	recv_from = MPI_ANY_SOURCE;
	if (source_type == SpecifiedSource) recv_from = to;
	MPI_Send(sbuffer,len,MPI_BYTE,from,0,MPI_COMM_WORLD);
	for(i=0;i<reps;i++){
	    MPI_Win_fence( 0, win );
	    MPI_Get( rbuffer, len, MPI_BYTE, from, 
		     0, len, MPI_BYTE, win );
	    MPI_Win_fence( 0, win );
	}
    }
    else {
	for(i=0;i<reps;i++){
	    MPI_Win_fence( 0, win );
	    MPI_Win_fence( 0, win );
	}
    }

    FinishTest();
    MPI_Win_free( &win );
#if defined(HAVE_SHMALLOC) && !defined(HAVE_MPI_ALLOC_MEM)
    shfree( sbuffer );
    shfree( rbuffer );
#else
    free(sbuffer );
    free(rbuffer );
#endif
    return(elapsed_time);
}

double round_trip_nc_get( int reps, int len, PairData ctx)
{
    double elapsed_time;
    int  i, to = ctx->destination, from = ctx->source;
    int  recv_from;
    char *rbuffer,*sbuffer, *rp, *sp, *rlast, *slast;
    double t0, t1;
    MPI_Win win;
    MPI_Status status;

#if defined(HAVE_SHMALLOC) && !defined(HAVE_MPI_ALLOC_MEM)
    sbuffer = (char *)shmalloc((unsigned)(2 * CacheSize ));
    rbuffer = (char *)shmalloc((unsigned)(2 * CacheSize ));
#else
    sbuffer = (char *)malloc((unsigned)(2 * CacheSize ));
    rbuffer = (char *)malloc((unsigned)(2 * CacheSize ));
#endif
    slast   = sbuffer + 2 * CacheSize - len;
    rlast   = rbuffer + 2 * CacheSize - len;
    if (!sbuffer || !rbuffer) {
	fprintf( stderr, "Could not allocate %d bytes\n", 4 * CacheSize );
	exit(1 );
    }
    sp = sbuffer;
    rp = rbuffer;
    memset( sbuffer, 0, 2*CacheSize );
    memset( rbuffer, 0, 2*CacheSize );

    MPI_Win_create( rbuffer, len, 1, MPI_INFO_NULL, MPI_COMM_WORLD, &win );

    SetupTest( from );
    ConfirmTest( reps, len, ctx );
    elapsed_time = 0;
    if(ctx->is_master){
	recv_from = MPI_ANY_SOURCE;
	if (source_type == SpecifiedSource) recv_from = to;
	MPI_Recv(rbuffer,len,MPI_BYTE,recv_from,0,MPI_COMM_WORLD,&status);
	t0=MPI_Wtime();
	for(i=0;i<reps;i++){
	    MPI_Get( rp, len, MPI_BYTE, to, 
		     (int)(sp - sbuffer), len, MPI_BYTE, win );
	    MPI_Win_fence( 0, win );
	    MPI_Win_fence( 0, win );
	    sp += len;
	    rp += len;
	    if (sp > slast) sp = sbuffer;
	    if (rp > rlast) rp = rbuffer;
	}
	t1=MPI_Wtime();
	elapsed_time = t1 -t0;
    }

    else if(ctx->is_slave){
	recv_from = MPI_ANY_SOURCE;
	if (source_type == SpecifiedSource) recv_from = to;
	MPI_Send(sbuffer,len,MPI_BYTE,from,0,MPI_COMM_WORLD);
	for(i=0;i<reps;i++){
	    MPI_Win_fence( 0, win );
	    MPI_Get( rp, len, MPI_BYTE, from, 
		     (int)(sp - sbuffer), len, MPI_BYTE, win );
	    MPI_Win_fence( 0, win );
	    sp += len;
	    rp += len;
	    if (sp > slast) sp = sbuffer;
	    if (rp > rlast) rp = rbuffer;
	}
    }
    else {
	for(i=0;i<reps;i++){
	    MPI_Win_fence( 0, win );
	    MPI_Win_fence( 0, win );
	}
    }

    FinishTest();
    MPI_Win_free( &win );
#if defined(HAVE_SHMALLOC) && !defined(HAVE_MPI_ALLOC_MEM)
    shfree( sbuffer );
    shfree( rbuffer );
#else
    free(sbuffer );
    free(rbuffer );
#endif
    return(elapsed_time);
}
#endif

/* 
   The following implements code to ensure that there is a pending receive
   that will never be satisfied
 */
#define NEVER_SENT_TAG  1000000000

static MPI_Request pending_req = MPI_REQUEST_NULL;
static void SetupTest( int from )
{
    static int dummy;
    if (MsgPending) {
	MPI_Irecv( &dummy, 1, MPI_INT, from, NEVER_SENT_TAG,
		   MPI_COMM_WORLD, &pending_req );
    }
}
static void FinishTest( void )
{
    if (MsgPending && pending_req != MPI_REQUEST_NULL) {
	MPI_Cancel( &pending_req );
	pending_req = MPI_REQUEST_NULL;
    }
}

/* 
   This routine confirms that all processes have consistent data.  If not,
   it generates an error message and invokes MPI_Abort
 */
void ConfirmTest( int reps, int len, PairData ctx )
{
    int msginfo[2], err=0;
    MPI_Status status;
    
    if (ctx->is_master) {
	MPI_Recv( msginfo, 2, MPI_INT, ctx->destination, 9999, 
		  MPI_COMM_WORLD, &status );
	if (msginfo[0] != reps) {
	    fprintf( stderr, "Expected %d but partner has %d for reps\n",
		     reps, msginfo[0] );
	    err++;
	}
	if (msginfo[1] != len) {
	    fprintf( stderr, "Expected %d but partner has %d for len\n",
		     len, msginfo[1] );
	    err++;
	}
	if (err) {
	    fflush( stderr );
	    MPI_Abort( MPI_COMM_WORLD, 1 );
	}
    }
    else if (ctx->is_slave) { 
	msginfo[0] = reps;
	msginfo[1] = len;
	MPI_Send( msginfo, 2, MPI_INT, ctx->source, 9999, MPI_COMM_WORLD );
    }
}
