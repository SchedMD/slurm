#ifdef MPI_BUILD_PROFILING
#undef MPI_BUILD_PROFILING
#endif
#include "mpi.h"
#include "stdio.h"

#ifdef __STDC__
extern int prof_send( int sender, int receiver, int tag, int size,
		       char *note );
extern int prof_recv( int receiver, int sender, int tag, int size,
			  char *note );
#else
extern int prof_send();
extern int prof_recv();
#endif

#include "requests.h"

static request_list *requests_head_{{fileno}}, *requests_tail_{{fileno}};
static int procid_{{fileno}};

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

{{fn fn_name MPI_Init}}
  {{callfn}}
  MPI_Comm_rank( MPI_COMM_WORLD, &procid_{{fileno}} );
  requests_head_{{fileno}} = requests_tail_{{fileno}} = 0;
{{endfn}}


{{fn fn_name MPI_Send MPI_Bsend MPI_Ssend MPI_Rsend}}
  {{vardecl int typesize}}
  {{callfn}}
  if ({{dest}} != MPI_PROC_NULL) {
    MPI_Type_size( {{datatype}}, (MPI_Aint *)&{{typesize}} );
    prof_send( procid_{{fileno}}, {{dest}}, {{tag}}, {{typesize}}*{{count}},
	       "{{fn_name}}" );
  }
{{endfn}}

{{fn fn_name MPI_Recv}}
  {{vardecl int size}}
  {{callfn}}
  if ({{source}} != MPI_PROC_NULL && returnVal == MPI_SUCCESS) {
    MPI_Get_count( {{status}}, MPI_BYTE, &{{size}} );
    prof_recv( procid_{{fileno}}, {{status}}->MPI_SOURCE,
	       {{status}}->MPI_TAG, {{size}}, "{{fn_name}}" );
  }
{{endfn}}


{{fn fn_name MPI_Sendrecv}}
  {{vardecl int typesize, count}}
  {{callfn}}
  if ({{dest}} != MPI_PROC_NULL && returnVal == MPI_SUCCESS) {
    MPI_Type_size( {{sendtype}}, (MPI_Aint *)&{{typesize}} );
    prof_send( procid_{{fileno}}, {{dest}}, {{sendtag}},
	       {{typesize}}*{{sendcount}}, "{{fn_name}}" );
    MPI_Get_count( {{status}}, MPI_BYTE, &{{count}} );
    prof_recv( {{dest}}, procid_{{fileno}}, {{recvtag}}, {{count}},
	       "{{fn_name}}" );
  }
{{endfn}}

  
{{fn fn_name MPI_Sendrecv_replace}}
  {{vardecl int size, typesize}}
  {{callfn}}
  if ({{dest}} != MPI_PROC_NULL && returnVal == MPI_SUCCESS) {
    MPI_Type_size( {{datatype}}, (MPI_Aint *)&{{typesize}} );
    prof_send( procid_{{fileno}}, {{dest}}, {{sendtag}},
	       {{typesize}}*{{count}}, "{{fn_name}}" );
    MPI_Get_count( {{status}}, MPI_BYTE, &{{size}} );
    prof_recv( {{dest}}, procid_{{fileno}}, {{recvtag}}, {{size}},
	       "{{fn_name}}" );
  }
{{endfn}}

{{fn fn_name MPI_Isend MPI_Ibsend MPI_Issend MPI_Irsend
             MPI_Send_init MPI_Bsend_init MPI_Ssend_init MPI_Rsend_init}}
  {{vardecl request_list *newrq}}
  {{vardecl int typesize}}
/* fprintf( stderr, "{{fn_name}} call on %d\n", procid_{{fileno}} ); */
  {{callfn}}
  if ({{dest}} != MPI_PROC_NULL) {
    if ({{newrq}} = (request_list*) malloc(sizeof( request_list ))) {
      MPI_Type_size( {{datatype}}, (MPI_Aint *)&{{typesize}} );
      {{newrq}}->request = {{request}};
      {{newrq}}->status = RQ_SEND;
      {{newrq}}->size = {{count}} * {{typesize}};
      {{newrq}}->tag = {{tag}};
      {{newrq}}->otherParty = {{dest}};
      {{newrq}}->next = 0;
      rq_add( requests_head_{{fileno}}, requests_tail_{{fileno}}, {{newrq}} );
    }
  }
{{endfn}}


{{fn fn_name MPI_Irecv MPI_Recv_init}}
  {{vardecl request_list *newrq}}
  {{callfn}}
  if ({{source}} != MPI_PROC_NULL && returnVal == MPI_SUCCESS) {
    if ({{newrq}} = (request_list*) malloc(sizeof( request_list ))) {
      {{newrq}}->request = {{request}};
      {{newrq}}->status = RQ_RECV;
      {{newrq}}->next = 0;
      rq_add( requests_head_{{fileno}}, requests_tail_{{fileno}}, {{newrq}} );
    }
  }
{{endfn}}


{{fn fn_name MPI_Request_free}}
  /* The request may have completed, may have not.  */
  /* We'll assume it didn't. */
  rq_remove( requests_head_{{fileno}}, {{request}} );
  {{callfn}}
{{endfn}}

{{fn fn_name MPI_Cancel}}
  {{vardecl request_list *rq}}
  rq_find( requests_head_{{fileno}}, {{request}}, {{rq}} );
  if ({{rq}}) {{rq}}->status |= RQ_CANCEL;
  /* be sure to check on the Test or Wait if it was really cancelled */
  {{callfn}}
{{endfn}}

void ProcessWaitTest_{{fileno}} ( request, status, note )
MPI_Request *request;
MPI_Status *status;
char *note;
{
  request_list *rq, *last;
  int flag, size;

  /* look for request */
  rq = requests_head_{{fileno}};
  last = 0;
  while (rq && (rq->request != request)) {
    last = rq;
    rq = rq->next;
  }

  if (!rq) {
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
      prof_send( procid_{{fileno}}, rq->otherParty, rq->tag, rq->size, note );
    } else {
      MPI_Get_count( status, MPI_BYTE, &size );
      prof_recv( procid_{{fileno}}, status->MPI_SOURCE, status->MPI_TAG,
		size, note );
    }
  }
  if (last) {
    last->next = rq->next;
  } else {
    requests_head_{{fileno}} = rq->next;
  }
  free( rq );
}

{{fn fn_name MPI_Wait}}
  {{callfn}}
  ProcessWaitTest_{{fileno}}( request, status, "{{fn_name}}" );
{{endfn}}




{{fn fn_name MPI_Waitany}}

  {{callfn}}
  ProcessWaitTest_{{fileno}}( &({{array_of_requests}}[*{{index}}]),
			{{status}}, "{{fn_name}}" );
{{endfn}}



{{fn fn_name MPI_Waitsome}}
  {{vardecl int i}}

  {{callfn}}
  for ({{i}}=0; {{i}} < *{{outcount}}; {{i}}++) {
    ProcessWaitTest_{{fileno}}( &({{array_of_requests}}
			          [{{array_of_indices}}[{{i}}]]),
			        &({{array_of_statuses}}
			          [{{array_of_indices}}[{{i}}]]),
			        "{{fn_name}}" );
  }
{{endfn}}


{{fn fn_name MPI_Waitall}}
  {{vardecl int i}}
/* fprintf( stderr, "{{fn_name}} call on %d\n", procid_{{fileno}} ); */
  {{callfn}}
  for ({{i}}=0; {{i}} < {{count}}; {{i}}++) {
    ProcessWaitTest_{{fileno}}( &({{array_of_requests}}[{{i}}]),
			        &({{array_of_statuses}}[{{i}}]),
			        "{{fn_name}}" );
  }
{{endfn}}


{{fn fn_name MPI_Test}}
  {{callfn}}
  if (*{{flag}}) 
    ProcessWaitTest_{{fileno}}( {{request}}, {{status}}, "{{fn_name}}" );
{{endfn}}

{{fn fn_name MPI_Testany}}
  {{callfn}}
  if (*{{flag}}) 
    ProcessWaitTest_{{fileno}}( &({{array_of_requests}}[*{{index}}]),
			        {{status}}, "{{fn_name}}" );
{{endfn}}

{{fn fn_name MPI_Testsome}}
  {{vardecl int i}}
  {{callfn}}
  for ({{i}}=0; {{i}} < *{{outcount}}; {{i}}++) {
    ProcessWaitTest_{{fileno}}( &({{array_of_requests}}
			          [{{array_of_indices}}[{{i}}]]),
			        &({{array_of_statuses}}
			          [{{array_of_indices}}[{{i}}]]),
			        "{{fn_name}}" );
  }
{{endfn}}


{{fn fn_name MPI_Testall}}
  {{vardecl int i}}
  {{callfn}}
  if (*{{flag}}) {
    for ({{i}}=0; {{i}} < {{count}}; {{i}}++) {
      ProcessWaitTest_{{fileno}}( &({{array_of_requests}}[{{i}}]),
				  &({{array_of_statuses}}[{{i}}]),
				  "{{fn_name}}" );
    }
  }
{{endfn}}









