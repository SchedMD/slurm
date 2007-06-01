#ifdef MPI_BUILD_PROFILING
#undef MPI_BUILD_PROFILING
#endif

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "mpeconf.h"

/* This is used to correct system header files without prototypes */
#if defined(NEEDS_STDLIB_PROTOTYPES)
#include "protofix.h"
#endif

/* This is the wrong test, but it will do for now ... */
#ifdef __STDC__
#include <stdarg.h>
#else
#include <varargs.h>
#endif

#include "mpi.h"
#include "mpe.h"
#include "point.h"

static int prof_send( int sender, int receiver, int tag, int size,
		       char *note );
static int prof_recv( int receiver, int sender, int tag, int size,
			  char *note );
/* static vector SubPoints_0( point, point ); */
static void DrawScreen_0( int, int );
static void MPE_Prof_DrawArrow_0( int, int );
static void ProcessWaitTest_1 ( MPI_Request, MPI_Status *, char * );

#define DEBUG_0 0

static int procid_0, np_0, readyToDraw_0=0;
static int xpos_0=-1, ypos_0=-1;
static point *procCoords_0;
static MPE_XGraph prof_graph_0;

#define PROC_RADIUS_0     10
#define PROC_SEPARATION_0 40
#define ARROW_LENGTH_0    12
#define ARROW_WIDTH_0      5
#define MARGIN_0           1.2

/*
static vector SubPoints_0(a, b)
point a, b;
{
  vector c;
  c.x = a.x - b.x;
  c.y = a.y - b.y;
  return c;
}
 */

#define UnitFromEndpoints_0( unit, start, end ) { \
  register double x, y, mag; \
  x = (end).x-(start).x; \
  y = (end).y-(start).y; \
  mag = sqrt( x*x + y*y ); \
  (unit).x = x/mag; \
  (unit).y = y/mag; }

#define NormVector_0( norm, vector ) { \
  (norm).x = -(vector).y; \
  (norm).y = vector.x; }

#define AddPointMultVector_0( newPt, pt, vec2, factor ) { \
  (newPt).x = (pt).x + (vec2).x*(factor); \
  (newPt).y = (pt).y + (vec2).y*(factor); }



static void DrawScreen_0( procid, np ) 
int procid, np;
{
  int width, procNum, radius;
  double angle;

  readyToDraw_0 = 0;

  procCoords_0 = (point *) malloc( sizeof( point ) * np );
  radius = (PROC_SEPARATION_0*np)/3.1416;
  width =  (radius + PROC_RADIUS_0) * 2 *
                      MARGIN_0;

  MPE_Open_graphics( &prof_graph_0, MPI_COMM_WORLD, 0,
		     xpos_0, ypos_0, width,
		     width, 0 );

  readyToDraw_0 = 1;

  if (procid == 0)
    MPE_Fill_rectangle( prof_graph_0, 0, 0, width,
		        width, MPE_WHITE );


  MPE_Draw_logic( prof_graph_0, MPE_LOGIC_INVERT );
  for (procNum=0; procNum < np; procNum++) {
    angle = (((double)procNum)/np)*3.1416*2 + 3.1416/2;
    procCoords_0[procNum].x = width/2 + radius
      * cos( angle );
    procCoords_0[procNum].y = width/2 - radius
      * sin( angle );
    if (procid_0 == 0) {
      MPE_Fill_circle( prof_graph_0,
		       procCoords_0[procNum].x,
		       procCoords_0[procNum].y,
		       PROC_RADIUS_0, MPE_GREEN );
    }
  }
  MPE_Update( prof_graph_0 );
}










static void MPE_Prof_DrawArrow_0( fromProc, toProc )
int fromProc, toProc;
{
  point start, end, a, b, c, d, e;
  vector unit, norm;

/*
                     D
                     | \
  A------------------B  E
                     | /
                     C

*/

  if (!readyToDraw_0) return;
  start = procCoords_0[fromProc];
  end = procCoords_0[toProc];
  UnitFromEndpoints_0( unit, start, end );
  NormVector_0( norm, unit );

  AddPointMultVector_0( a, start, unit,  PROC_RADIUS_0 );
  AddPointMultVector_0( e, end,   unit, -PROC_RADIUS_0 );
  AddPointMultVector_0( b, e,     unit, -ARROW_LENGTH_0 );
  AddPointMultVector_0( c, b,     norm,  ARROW_WIDTH_0 );
  AddPointMultVector_0( d, b,     norm, -ARROW_WIDTH_0 );

  MPE_Draw_line( prof_graph_0, a.x, a.y, b.x, b.y, MPE_BLACK );
  MPE_Draw_line( prof_graph_0, c.x, c.y, d.x, d.y, MPE_BLACK );
  MPE_Draw_line( prof_graph_0, d.x, d.y, e.x, e.y, MPE_BLACK );
  MPE_Draw_line( prof_graph_0, e.x, e.y, c.x, c.y, MPE_BLACK );
  MPE_Update( prof_graph_0 );
}

static int prof_send( sender, receiver, tag, size, note )
int sender, receiver, tag, size;
char *note;
{
  MPE_Prof_DrawArrow_0( sender, receiver );
  return 0;
}

static int prof_recv( receiver, sender, tag, size, note )
int receiver, sender, tag, size;
char *note;
{
  MPE_Prof_DrawArrow_0( sender, receiver );
  return 0;
}



#ifdef MPI_BUILD_PROFILING
#undef MPI_BUILD_PROFILING
#endif
#include "mpi.h"
#include "stdio.h"

#include "requests.h"

static request_list *requests_head_1, *requests_tail_1, *requests_avail_1=0;
static int procid_1;

/* Message_prof keeps track of when sends and receives 'happen'.  The
** time that each send or receive happens is different for each type of
** send or receive.
**
** Check for MPI_PROC_NULL
**
**   definitely a send:
**     After a call to MPI_Send.
**     After a call to MPI_Bsend.
**     After a call to MPI_Ssend.
**     After a call to MPI_Rsend.
**
**
**   definitely a receive:
**     After a call to MPI_Recv.
**
**   definitely a receive and a send:
**     After a call to MPI_Sendrecv
**     After a call to MPI_Sendrecv_replace
**
**   maybe a send, maybe a receive:
**     Before a call to MPI_Wait.
**     Before a call to MPI_Waitany.
**     Before a call to MPI_Waitsome.
**     Before a call to MPI_Waitall.
**     After a call to MPI_Probe
**   maybe neither:
**     Before a call to MPI_Test.
**     Before a call to MPI_Testany.
**     Before a call to MPI_Testsome.
**     Before a call to MPI_Testall.
**     After a call to MPI_Iprobe
**
**   start request for a send:
**     After a call to MPI_Isend.
**     After a call to MPI_Ibsend.
**     After a call to MPI_Issend.
**     After a call to MPI_Irsend.
**     After a call to MPI_Send_init.
**     After a call to MPI_Bsend_init.
**     After a call to MPI_Ssend_init.
**     After a call to MPI_Rsend_init.
**
**   start request for a recv:
**     After a call to MPI_Irecv.
**     After a call to MPI_Recv_init.
**
**   stop watching a request:
**     Before a call to MPI_Request_free
**
**   mark a request as possible cancelled:
**     After a call to MPI_Cancel
**
*/

static void ProcessWaitTest_1 ( MPI_Request request, MPI_Status *status, 
				char *note )
{
  request_list *rq, *last;
  int flag, size;

  /* look for request */
  rq = requests_head_1;
  last = 0;
  while (rq && (rq->request != request)) {
    last = rq;
    rq = rq->next;
  }

  if (!rq) {
#define PRINT_PROBLEMS
#ifdef PRINT_PROBLEMS
    fprintf( stderr, "Request not found in '%s'.\n", note );
#endif
    return;		/* request not found */
  }

  if (status->MPI_TAG != MPI_ANY_TAG) {
    /* if the request was not invalid */

    if (rq->status & RQ_CANCEL) {
      MPI_Test_cancelled( status, &flag );
      if (flag) return;		/* the request has been cancelled */
    }
    
    if (rq->status & RQ_SEND) {
      prof_send( procid_1, rq->otherParty, rq->tag, rq->size, note );
    } else {
      MPI_Get_count( status, MPI_BYTE, &size );
      prof_recv( procid_1, status->MPI_SOURCE, status->MPI_TAG,
		size, note );
    }
  }
  if (last) {
    last->next = rq->next;
  } else {
    requests_head_1 = rq->next;
  }
  free( rq );
}


int  MPI_Init( argc, argv )
int * argc;
char *** argv;
{
  int  returnVal;
 
  
  returnVal = PMPI_Init( argc, argv );

  MPI_Comm_rank( MPI_COMM_WORLD, &procid_1 );
  requests_head_1 = requests_tail_1 = 0;
  rq_init( requests_avail_1 );

  MPI_Comm_rank( MPI_COMM_WORLD, &procid_0 );
  MPI_Comm_size( MPI_COMM_WORLD, &np_0 );
  MPI_Barrier( MPI_COMM_WORLD );

  DrawScreen_0( procid_0, np_0 );

  return returnVal;
}

int MPI_Finalize()
{
    rq_end( requests_avail_1 );
    return PMPI_Finalize();
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
  int typesize;

  
  
  returnVal = PMPI_Bsend( buf, count, datatype, dest, tag, comm );

  if (dest != MPI_PROC_NULL) {
    MPI_Type_size( datatype, &typesize );
    prof_send( procid_1, dest, tag, typesize*count,
	       "MPI_Bsend" );
  }

  return returnVal;
}

int  MPI_Bsend_init( buf, count, datatype, dest, tag, comm, request )
void * buf;
int count;
MPI_Datatype datatype;
int dest;
int tag;
MPI_Comm comm;
MPI_Request * request;
{
  int  returnVal;
  request_list *newrq;
  int typesize3;

  
  
/* fprintf( stderr, "MPI_Bsend_init call on %d\n", procid_1 ); */
  
  returnVal = PMPI_Bsend_init( buf, count, datatype, dest, tag, comm, request );

  if (dest != MPI_PROC_NULL) {
      rq_alloc( requests_avail_1, newrq );
    if (newrq) {
      PMPI_Type_size( datatype, &typesize3 );
      newrq->request = *request;
      newrq->status = RQ_SEND;
      newrq->size = count * typesize3;
      newrq->tag = tag;
      newrq->otherParty = dest;
      newrq->next = 0;
      rq_add( requests_head_1, requests_tail_1, newrq );
    }
  }

  return returnVal;
}

int  MPI_Cancel( request )
MPI_Request * request;
{
  int  returnVal;
  request_list *rq;

  
  rq_find( requests_head_1, *request, rq );
  if (rq) rq->status |= RQ_CANCEL;
  /* be sure to check on the Test or Wait if it was really cancelled */
  
  returnVal = PMPI_Cancel( request );


  return returnVal;
}

int  MPI_Request_free( request )
MPI_Request * request;
{
  int  returnVal;

  /* The request may have completed, may have not.  */
  /* We'll assume it didn't. */
  rq_remove( requests_head_1, requests_tail_1, requests_avail_1, *request );
  
  returnVal = PMPI_Request_free( request );


  return returnVal;
}

int  MPI_Recv_init( buf, count, datatype, source, tag, comm, request )
void * buf;
int count;
MPI_Datatype datatype;
int source;
int tag;
MPI_Comm comm;
MPI_Request * request;
{
  int  returnVal;
  request_list *newrq1;

  
  
  returnVal = PMPI_Recv_init( buf, count, datatype, source, tag, comm, request );

  if (source != MPI_PROC_NULL && returnVal == MPI_SUCCESS) {
    if ((newrq1 = (request_list*) malloc(sizeof( request_list )))) {
      newrq1->request = *request;
      newrq1->status = RQ_RECV;
      newrq1->next = 0;
      rq_add( requests_head_1, requests_tail_1, newrq1 );
    }
  }

  return returnVal;
}

int  MPI_Send_init( buf, count, datatype, dest, tag, comm, request )
void * buf;
int count;
MPI_Datatype datatype;
int dest;
int tag;
MPI_Comm comm;
MPI_Request * request;
{
  int  returnVal;
  request_list *newrq;
  int typesize3;

  
  
/* fprintf( stderr, "MPI_Send_init call on %d\n", procid_1 ); */
  
  returnVal = PMPI_Send_init( buf, count, datatype, dest, tag, comm, request );

  if (dest != MPI_PROC_NULL) {
      rq_alloc( requests_avail_1, newrq );
      if (newrq) {
      PMPI_Type_size( datatype, &typesize3 );
      newrq->request = *request;
      newrq->status = RQ_SEND;
      newrq->size = count * typesize3;
      newrq->tag = tag;
      newrq->otherParty = dest;
      newrq->next = 0;
      rq_add( requests_head_1, requests_tail_1, newrq );
    }
  }

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
  request_list *newrq;
  int typesize3;

  
  
/* fprintf( stderr, "MPI_Ibsend call on %d\n", procid_1 ); */
  
  returnVal = PMPI_Ibsend( buf, count, datatype, dest, tag, comm, request );

  if (dest != MPI_PROC_NULL) {
      rq_alloc( requests_avail_1, newrq );
    if (newrq) {
      PMPI_Type_size( datatype, &typesize3 );
      newrq->request = *request;
      newrq->status = RQ_SEND;
      newrq->size = count * typesize3;
      newrq->tag = tag;
      newrq->otherParty = dest;
      newrq->next = 0;
      rq_add( requests_head_1, requests_tail_1, newrq );
    }
  }

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
  request_list *newrq1;

  
  
  returnVal = PMPI_Irecv( buf, count, datatype, source, tag, comm, request );

  if (source != MPI_PROC_NULL && returnVal == MPI_SUCCESS) {
      rq_alloc( requests_avail_1, newrq1 );
    if (newrq1) {
      newrq1->request = *request;
      newrq1->status = RQ_RECV;
      newrq1->next = 0;
      rq_add( requests_head_1, requests_tail_1, newrq1 );
    }
  }

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
  request_list *newrq;
  int typesize3;

  
  
/* fprintf( stderr, "MPI_Irsend call on %d\n", procid_1 ); */
  
  returnVal = PMPI_Irsend( buf, count, datatype, dest, tag, comm, request );

  if (dest != MPI_PROC_NULL) {
      rq_alloc( requests_avail_1, newrq );
    if (newrq) {
      PMPI_Type_size( datatype, &typesize3 );
      newrq->request = *request;
      newrq->status = RQ_SEND;
      newrq->size = count * typesize3;
      newrq->tag = tag;
      newrq->otherParty = dest;
      newrq->next = 0;
      rq_add( requests_head_1, requests_tail_1, newrq );
    }
  }

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
  request_list *newrq;
  int typesize3;

  
  
/* fprintf( stderr, "MPI_Isend call on %d\n", procid_1 ); */
  
  returnVal = PMPI_Isend( buf, count, datatype, dest, tag, comm, request );

  if (dest != MPI_PROC_NULL) {
      rq_alloc( requests_avail_1, newrq );
    if (newrq) {
      PMPI_Type_size( datatype, &typesize3 );
      newrq->request = *request;
      newrq->status = RQ_SEND;
      newrq->size = count * typesize3;
      newrq->tag = tag;
      newrq->otherParty = dest;
      newrq->next = 0;
      rq_add( requests_head_1, requests_tail_1, newrq );
    }
  }

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
  request_list *newrq;
  int typesize3;

  
  
/* fprintf( stderr, "MPI_Issend call on %d\n", procid_1 ); */
  
  returnVal = PMPI_Issend( buf, count, datatype, dest, tag, comm, request );

  if (dest != MPI_PROC_NULL) {
      rq_alloc( requests_avail_1, newrq );
    if (newrq) {
      PMPI_Type_size( datatype, &typesize3 );
      newrq->request = *request;
      newrq->status = RQ_SEND;
      newrq->size = count * typesize3;
      newrq->tag = tag;
      newrq->otherParty = dest;
      newrq->next = 0;
      rq_add( requests_head_1, requests_tail_1, newrq );
    }
  }

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
  int size;

  
  
  returnVal = PMPI_Recv( buf, count, datatype, source, tag, comm, status );

  if (source != MPI_PROC_NULL && returnVal == MPI_SUCCESS) {
    MPI_Get_count( status, MPI_BYTE, &size );
    prof_recv( procid_1, status->MPI_SOURCE,
	       status->MPI_TAG, size, "MPI_Recv" );
  }

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
  int typesize;

  
  
  returnVal = PMPI_Rsend( buf, count, datatype, dest, tag, comm );

  if (dest != MPI_PROC_NULL) {
    MPI_Type_size( datatype, &typesize );
    prof_send( procid_1, dest, tag, typesize*count,
	       "MPI_Rsend" );
  }

  return returnVal;
}

int  MPI_Rsend_init( buf, count, datatype, dest, tag, comm, request )
void * buf;
int count;
MPI_Datatype datatype;
int dest;
int tag;
MPI_Comm comm;
MPI_Request * request;
{
  int  returnVal;
  request_list *newrq;
  int typesize3;

  
  
/* fprintf( stderr, "MPI_Rsend_init call on %d\n", procid_1 ); */
  
  returnVal = PMPI_Rsend_init( buf, count, datatype, dest, tag, comm, request );

  if (dest != MPI_PROC_NULL) {
      rq_alloc( requests_avail_1, newrq );
    if (newrq) {
      PMPI_Type_size( datatype, &typesize3 );
      newrq->request = *request;
      newrq->status = RQ_SEND;
      newrq->size = count * typesize3;
      newrq->tag = tag;
      newrq->otherParty = dest;
      newrq->next = 0;
      rq_add( requests_head_1, requests_tail_1, newrq );
    }
  }

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
  int typesize;

  
  
  returnVal = PMPI_Send( buf, count, datatype, dest, tag, comm );

  if (dest != MPI_PROC_NULL) {
    MPI_Type_size( datatype, &typesize );
    prof_send( procid_1, dest, tag, typesize*count,
	       "MPI_Send" );
  }

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
  int typesize1;
  int count;

  
  
  returnVal = PMPI_Sendrecv( sendbuf, sendcount, sendtype, dest, sendtag, recvbuf, recvcount, recvtype, source, recvtag, comm, status );

  if (dest != MPI_PROC_NULL && returnVal == MPI_SUCCESS) {
    MPI_Type_size( sendtype, &typesize1 );
    prof_send( procid_1, dest, sendtag,
	       typesize1*sendcount, "MPI_Sendrecv" );
    MPI_Get_count( status, MPI_BYTE, &count );
    prof_recv( dest, procid_1, recvtag, count,
	       "MPI_Sendrecv" );
  }

  return returnVal;
}

int  MPI_Sendrecv_replace( buf, count, datatype, dest, sendtag, source, recvtag, comm, status )
void * buf;
int count;
MPI_Datatype datatype;
int dest;
int sendtag;
int source;
int recvtag;
MPI_Comm comm;
MPI_Status * status;
{
  int  returnVal;
  int size1;
  int typesize2;

  
  
  returnVal = PMPI_Sendrecv_replace( buf, count, datatype, dest, sendtag, source, recvtag, comm, status );

  if (dest != MPI_PROC_NULL && returnVal == MPI_SUCCESS) {
    MPI_Type_size( datatype, &typesize2 );
    prof_send( procid_1, dest, sendtag,
	       typesize2*count, "MPI_Sendrecv_replace" );
    MPI_Get_count( status, MPI_BYTE, &size1 );
    prof_recv( dest, procid_1, recvtag, size1,
	       "MPI_Sendrecv_replace" );
  }

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
  int typesize;

  
  
  returnVal = PMPI_Ssend( buf, count, datatype, dest, tag, comm );

  if (dest != MPI_PROC_NULL) {
    MPI_Type_size( datatype, &typesize );
    prof_send( procid_1, dest, tag, typesize*count,
	       "MPI_Ssend" );
  }

  return returnVal;
}

int  MPI_Ssend_init( buf, count, datatype, dest, tag, comm, request )
void * buf;
int count;
MPI_Datatype datatype;
int dest;
int tag;
MPI_Comm comm;
MPI_Request * request;
{
  int  returnVal;
  request_list *newrq;
  int typesize3;

  
  
/* fprintf( stderr, "MPI_Ssend_init call on %d\n", procid_1 ); */
  
  returnVal = PMPI_Ssend_init( buf, count, datatype, dest, tag, comm, request );

  if (dest != MPI_PROC_NULL) {
      rq_alloc( requests_avail_1, newrq );
    if (newrq) {
      PMPI_Type_size( datatype, &typesize3 );
      newrq->request = *request;
      newrq->status = RQ_SEND;
      newrq->size = count * typesize3;
      newrq->tag = tag;
      newrq->otherParty = dest;
      newrq->next = 0;
      rq_add( requests_head_1, requests_tail_1, newrq );
    }
  }

  return returnVal;
}

int   MPI_Test( request, flag, status )
MPI_Request * request;
int * flag;
MPI_Status * status;
{
  int   returnVal;
  MPI_Request lreq = *request;

  
  returnVal = PMPI_Test( request, flag, status );

  if (*flag) 
    ProcessWaitTest_1( lreq, status, "MPI_Test" );

  return returnVal;
}

int  MPI_Testall( count, array_of_requests, flag, array_of_statuses )
int count;
MPI_Request * array_of_requests;
int * flag;
MPI_Status * array_of_statuses;
{
  int  returnVal;
  int i3;

  /* NEEDS WORK */
  
  returnVal = PMPI_Testall( count, array_of_requests, flag, array_of_statuses );

  if (*flag) {
    for (i3=0; i3 < count; i3++) {
      ProcessWaitTest_1( array_of_requests[i3], /* WRONG */
				  &array_of_statuses[i3],  
				  "MPI_Testall" );
    }
  }

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

  /* NEEDS WORK */
  
  returnVal = PMPI_Testany( count, array_of_requests, index, flag, status );

  if (*flag) 
    ProcessWaitTest_1( array_of_requests[*index], /* WRONG */
			        status, "MPI_Testany" );

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
  int i2;

    /* NEEDS WORK */
  
  returnVal = PMPI_Testsome( incount, array_of_requests, outcount, array_of_indices, array_of_statuses );

  for (i2=0; i2 < *outcount; i2++) {
    ProcessWaitTest_1( array_of_requests
			          [array_of_indices[i2]], /* WRONG */
			        &array_of_statuses
			          [array_of_indices[i2]],
			        "MPI_Testsome" );
  }

  return returnVal;
}

int   MPI_Wait( request, status )
MPI_Request * request;
MPI_Status * status;
{
  int   returnVal;
  MPI_Request lreq = *request;
  
  returnVal = PMPI_Wait( request, status );

  ProcessWaitTest_1( lreq, status, "MPI_Wait" );

  return returnVal;
}

int  MPI_Waitall( count, array_of_requests, array_of_statuses )
int count;
MPI_Request * array_of_requests;
MPI_Status * array_of_statuses;
{
  int  returnVal;
  int i1;

  /* NEEDS WORK */
  
/* fprintf( stderr, "MPI_Waitall call on %d\n", procid_1 ); */
  
  returnVal = PMPI_Waitall( count, array_of_requests, array_of_statuses );

  for (i1=0; i1 < count; i1++) {
    ProcessWaitTest_1( array_of_requests[i1], /* WRONG */
			        &array_of_statuses[i1],
			        "MPI_Waitall" );
  }

  return returnVal;
}

int  MPI_Waitany( count, array_of_requests, index, status )
int count;
MPI_Request * array_of_requests;
int * index;
MPI_Status * status;
{
  int  returnVal;

  /* NEEDS WORK */
 
  returnVal = PMPI_Waitany( count, array_of_requests, index, status );

  ProcessWaitTest_1( array_of_requests[*index] /* WRONG */, status,
			      "MPI_Waitany" );

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
  int i;

  /* NEEDS WORK */
  
  returnVal = PMPI_Waitsome( incount, array_of_requests, outcount, array_of_indices, array_of_statuses );

  for (i=0; i < *outcount; i++) {
    ProcessWaitTest_1( array_of_requests
			          [array_of_indices[i]], /* WRONG */
			        &array_of_statuses
			          [array_of_indices[i]],
			        "MPI_Waitsome" );
  }

  return returnVal;
}
