#ifndef REQUESTS
#define REQUESTS

typedef struct request_list_ {
    MPI_Request request;
    int         status, size, tag, otherParty;
    int         is_persistent;
    struct request_list_ *next;
} request_list;

#define RQ_SEND    0x1
#define RQ_RECV    0x2
#define RQ_CANCEL  0x4
/* if MPI_Cancel is called on a request, 'or' RQ_CANCEL into status.
** After a Wait* or Test* is called on that request, check for RQ_CANCEL.
** If the bit is set, check with MPI_Test_cancelled before registering
** the send/receive as 'happening'.
**
*/

#define rq_alloc( head_alloc, newrq ) {\
      if (head_alloc) {\
        newrq=head_alloc;head_alloc=newrq->next;\
	}else{\
      newrq = (request_list*) malloc(sizeof( request_list ));\
      }}

#define rq_remove_at( head, tail, head_alloc, ptr, last ) { \
  if (ptr) { \
    if (!last) { \
      head = ptr->next; \
    } else { \
      last->next = ptr->next; \
      if (tail == ptr) tail = last; \
    } \
	  ptr->next = head_alloc; head_alloc = ptr;}}

#define rq_remove( head, tail, head_alloc, rq ) { \
  request_list *ptr, *last; \
  ptr = head; \
  last = 0; \
  while (ptr && (ptr->request != rq)) { \
    last = ptr; \
    ptr = ptr->next; \
  } \
	rq_remove_at( head, tail, head_alloc, ptr, last );}

#define rq_add( head, tail, rq ) { \
  if (!head) { \
    head = tail = rq; \
  } else { \
    tail->next = rq; tail = rq; \
  }}

#define rq_find( head, req, rq ) { \
  rq = head; \
  while (rq && (rq->request != req)) rq = rq->next; }

#define rq_init( head_alloc ) {\
  int i; request_list *newrq; head_alloc = 0;\
  for (i=0;i<20;i++) {\
      newrq = (request_list*) malloc(sizeof( request_list ));\
      newrq->next = head_alloc;\
      head_alloc = newrq;\
  }}

#define rq_end( head_alloc ) {\
  request_list *rq; while (head_alloc) {\
	rq = head_alloc->next;free(head_alloc);head_alloc=rq;}}
#endif
