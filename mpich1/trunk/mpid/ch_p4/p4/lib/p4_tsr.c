#include "p4.h"
#include "p4_sys.h"

/*
 * search_p4_queue tries to locate a message of the desired type in the
 * local queue of messages already received.  If it finds one, it dequeues it 
 * if deq is true, and returns its address; otherwise it returns NULL.
 */
struct p4_msg *search_p4_queue( int req_type, int req_from, P4BOOL deq)
{
    struct p4_queued_msg *qpp, *qp;
    struct p4_msg *tqp;
    P4BOOL found;

    tqp = NULL;
    qpp = NULL;
    found = P4_FALSE;

    qp = p4_local->queued_messages->first_msg;
    while (qp)
    {
	if (qp->qmsg->ack_req & P4_BROADCAST_MASK)
	{
	    /* RMB p4_dprintfl(00, "subbrdcst type = %d, sender = %d\n",req_type,req_from); */
	    if (subtree_broadcast_p4(qp->qmsg->type, qp->qmsg->from,(char *) &qp->qmsg->msg,
				     qp->qmsg->len, qp->qmsg->data_type))
	    {
		p4_dprintf("search_p4_queue: failed\n");
		return(NULL);
	    }
	    qp->qmsg->ack_req &= ~P4_BROADCAST_MASK;	/* Unset broadcast bit */
	}
	qp = qp->next;
    }

    qp = p4_local->queued_messages->first_msg;
    while (qp && !found)
    {
	if (((qp->qmsg->type == req_type) || (req_type == -1)) &&
	    ((qp->qmsg->from == req_from) || (req_from == -1)))
	{
	    found = P4_TRUE;
	    if (deq)
	    {
		if (p4_local->queued_messages->first_msg ==
		    p4_local->queued_messages->last_msg)
		{
		    p4_local->queued_messages->first_msg = NULL;
		    p4_local->queued_messages->last_msg = NULL;
		}
		else
		{
		    if (qp == p4_local->queued_messages->first_msg)
		    {
			p4_local->queued_messages->first_msg = qp->next;
		    }
		    else if (qp == p4_local->queued_messages->last_msg)
		    {
			p4_local->queued_messages->last_msg = qpp;
			qpp->next = NULL;
		    }
		    else
		    {
			qpp->next = qp->next;
		    }
		}
	    }
	}
	else
	{
	    qpp = qp;
	    qp = qp->next;
	}
    }
    if (found)
    {
	p4_dprintfl(30,"extracted queued msg of type %d from %d\n",
		    qp->qmsg->type,qp->qmsg->from);
	tqp = qp->qmsg;
	if (deq)
	{
	    free_quel(qp);
	}
    }
    return (tqp);
}

/*
 * This is the top-level receive routine, called by the user.
 *   req_type is either a desired type or -1.  In the -1 case it will be
 *        modified  by p4_recv to indicate the type actually received.
 *   req_from is either a desired source or -1.  In the -1 case it will be
 *        modified by p4_recv to the source of the message actually received.
 *   msg will be set by p4_recv to point to a buffer containing the message.
 *   len_rcvd will be set by p4_recv to contain the length of the message.
 *
 *   returns 0 if successful; non-zero if error
 */
int p4_recv( int *req_type, int *req_from, char **msg, int *len_rcvd)
{
    struct p4_msg *tmsg, *tempmsg;
    P4BOOL good;

    p4_dprintfl(20, "receiving for type = %d, sender = %d\n",
		*req_type, *req_from);
    ALOG_LOG(p4_local->my_id,END_USER,0,"");
    ALOG_LOG(p4_local->my_id,BEGIN_RECV,*req_from,"");

    for (good = P4_FALSE; !good;)
    {
	ALOG_LOG(p4_local->my_id,END_RECV,0,"");
	ALOG_LOG(p4_local->my_id,BEGIN_WAIT,0,"");
	if (!(tmsg = search_p4_queue(*req_type, *req_from, 1)))
	{
	    tmsg = recv_message(req_type, req_from);
	    /*****
	    if (tmsg)
		p4_dprintfl(00, "received type = %d, sender = %d\n",
		            tmsg->type,tmsg->from);
	    *****/
	}
	ALOG_LOG(p4_local->my_id,END_WAIT,0,"");
	ALOG_LOG(p4_local->my_id,BEGIN_RECV,0,"");
	if (tmsg == NULL)
	{
	    p4_dprintfl(70,"p4_recv: got NULL back from recv_message\n");
	    continue;
	}
	if (((tmsg->type == *req_type) || (*req_type == -1)) &&
	    ((tmsg->from == *req_from) || (*req_from == -1)))
	{
	    good = P4_TRUE;
	}
	if (tmsg->ack_req & P4_BROADCAST_MASK)
	{
	    /* RMB p4_dprintfl(00, "subbrdcst type = %d, sender = %d\n",*req_type,*req_from); */
	    if (subtree_broadcast_p4(tmsg->type, tmsg->from,(char *) &tmsg->msg,
				     tmsg->len, tmsg->data_type))
	    {
		p4_dprintf("p4_recv: subtree_brdcst failed\n");
		return(-1);
	    }
	    tmsg->ack_req &= ~P4_BROADCAST_MASK;	/* Unset broadcast bit */
	}
	if (!good)
	    queue_p4_message(tmsg, p4_local->queued_messages);
    }

    *req_type = tmsg->type;
    *req_from = tmsg->from;

    p4_dprintfl(10, "received type=%d, from=%d\n",*req_type,*req_from);

    if (*msg == NULL)
    {
	*msg = (char *) &(tmsg->msg);
	*len_rcvd = tmsg->len;
    }
    else
    {
	tempmsg = (struct p4_msg *)
	    ((*msg) - (sizeof(struct p4_msg) - sizeof(char *)));
	/* copy into the user's buffer area, truncating if necessary */
	if (tmsg->len < tempmsg->orig_len)
	    *len_rcvd = tmsg->len;
	else
	    *len_rcvd = tempmsg->orig_len;
	bcopy((char *) &(tmsg->msg),*msg,*len_rcvd);
	tmsg->msg_id = (-1);
	free_p4_msg(tmsg);
    }
    ALOG_LOG(p4_local->my_id,END_RECV,*req_from,"");
    ALOG_LOG(p4_local->my_id,BEGIN_USER,0,"");

    return (0);
}

struct p4_msg *recv_message( int *req_type, int *req_from )
{
    p4_dprintfl( 99, "Starting recv_message for type = %d and sender = %d\n",
		 *req_type, *req_from );
#if  defined(CAN_DO_SOCKET_MSGS) && \
    !defined(CAN_DO_SHMEM_MSGS)  && \
    !defined(CAN_DO_CUBE_MSGS)   && \
    !defined(CAN_DO_SWITCH_MSGS) && \
    !defined(CAN_DO_TCMP_MSGS)

    return (socket_recv( P4_TRUE ));

#else
    {
	int i;
#ifdef USE_YIELD
	int cnt = 0;
#define BACKOFF_LIMIT 8
	/* If a yield method has been selected, yield after BACKOFF 
	   cycles.  
	   We also spin a few times on the shmem messages first,
	   since communication here will be faster and the
	   added latency of spinning on the shmem_msgs will
	   be neglible for the other devices
	*/
#endif
	while (P4_TRUE) {
#       if defined(CAN_DO_SHMEM_MSGS)
	    /* Optimally, this would spin for the round-trip time. 
	       Figure that at 5 us, and a very conservative .1us per
	       function call (in 2002, probably a gross overestimate),
	       that suggests at least 50 iterations.  We'll try 50
	       (that should be safe - shmem_msgs_available
	       is a short routine).   This could be tuned for
	       different platforms.
	    */
	    for (i=0; i<50; i++) {
		if (shmem_msgs_available())
		    {
			return (shmem_recv());
		    }
	    }
#       endif

#       if defined(CAN_DO_EUI_MSGS)
	    return (MD_eui_recv());
#       endif

#       if defined(CAN_DO_EUIH_MSGS)
	    if (MD_euih_msgs_available())
		{
		    return (MD_euih_recv());
		}
#       endif

#       if defined(CAN_DO_SOCKET_MSGS)
	    if (socket_msgs_available())
		{
		    return (socket_recv( P4_FALSE ));
		}
#       endif

#       if defined(CAN_DO_CUBE_MSGS)
	    if (MD_cube_msgs_available())
		{
		    return (MD_cube_recv());
		}
#       endif

#       if defined(CAN_DO_SWITCH_MSGS)
	    if (p4_global->proctable[p4_local->my_id].switch_port != -1)
		{
		    int rc, len;
		    if (rc = sw_probe(req_from, p4_local->my_id, req_type, &len))
			{
			    struct p4_msg *tmsg;
			    tmsg = alloc_p4_msg(len - sizeof(struct p4_msg) + sizeof(char *));
			    sw_recv(rc, tmsg);
			    p4_dprintfl(10, "p4_recv: received message from switch\n");
			    return (tmsg);
			}
		}
#       endif
#       if defined(CAN_DO_TCMP_MSGS)
	    if (MD_tcmp_msgs_available(req_type,req_from))
		{
		    return (MD_tcmp_recv());
		}
#       endif
#       ifdef USE_YIELD
	    if (cnt++ > BACKOFF_LIMIT) {
		cnt = 0;
		p4_yield();
	    }
#       endif
	}
    }


#endif
}

static int first_call = 1;
static struct p4_msg_queue *local_mq;
static struct p4_msg_queue *local_qp;

P4BOOL p4_any_messages_available()
/* sometimes want a simple call with little overhead  - SRM 
   did not make much difference - the main overhead is in the select within
   sock_msg_on_fd() */
{    
  int  qidx;

  if(first_call)
    {
      qidx = p4_local->my_id - p4_global->low_cluster_id;
      local_mq = &(p4_global->shmem_msg_queues[qidx]);
      local_qp = p4_local->queued_messages;
      first_call = 0;
    }

#if defined(CAN_DO_SOCKET_MSGS)
  return((local_qp->first_msg) || (local_mq->first_msg) ||
	 socket_msgs_available());
#else
  return((local_qp->first_msg) || (local_mq->first_msg));
#endif
}


P4BOOL p4_messages_available(req_type, req_from)
int *req_type, *req_from;
{
    int found;
    struct p4_msg *tmsg;

    ALOG_LOG(p4_local->my_id,END_USER,0,"");
    ALOG_LOG(p4_local->my_id,BEGIN_WAIT,1,"");

    found = P4_FALSE;
    if ((tmsg = search_p4_queue(*req_type, *req_from, 0)))
    {
	found = P4_TRUE;
	*req_type = tmsg->type;
	*req_from = tmsg->from;
    }

#   if defined(CAN_DO_SHMEM_MSGS)
    while (!found && shmem_msgs_available())
    {
	tmsg = shmem_recv();
	if (((tmsg->type == *req_type) || (*req_type == -1)) &&
	    ((tmsg->from == *req_from) || (*req_from == -1)))
	{
	    found = P4_TRUE;
	    *req_type = tmsg->type;
	    *req_from = tmsg->from;
	}
	queue_p4_message(tmsg, p4_local->queued_messages);
    }
#   endif

#   if defined(CAN_DO_SOCKET_MSGS)
    while (!found && socket_msgs_available())
    {
	tmsg = socket_recv( P4_FALSE );
	if (tmsg) {
	    if (((tmsg->type == *req_type) || (*req_type == -1)) &&
		((tmsg->from == *req_from) || (*req_from == -1)))
		{
		found	  = P4_TRUE;
		*req_type = tmsg->type;
		*req_from = tmsg->from;
		}
	    queue_p4_message(tmsg, p4_local->queued_messages);
	    }
    }
#   endif

#   if defined(CAN_DO_CUBE_MSGS)
    while (!found && MD_cube_msgs_available())
    {
	tmsg = MD_cube_recv();
	if (((tmsg->type == *req_type) || (*req_type == -1)) &&
	    ((tmsg->from == *req_from) || (*req_from == -1)))
	{
	    found = P4_TRUE;
	    (*req_type) = tmsg->type;
	    *req_from = tmsg->from;
	}
	queue_p4_message(tmsg, p4_local->queued_messages);
    }
#   endif


#if defined(CAN_DO_SWITCH_MSGS)
    if (!found && (p4_global->proctable[p4_local->my_id].switch_port != -1))
    {
	int len;
	if (sw_probe(req_from, p4_local->my_id, req_type, &len))
	    found = P4_TRUE;
    }
#endif

#if defined(CAN_DO_TCMP_MSGS)
    if (!found && MD_tcmp_msgs_available(req_from,req_type))
	found = P4_TRUE;
#endif

    if (!found) {
	int i;
	/* See if a connection has died ... */
	for (i = 0; i < p4_global->num_in_proctable; i++)
	    {
	    if (p4_local->conntab[i].type == CONN_REMOTE_DYING) {
		/* We need to detect that some are down... */
		p4_error( "Found a dead connection while looking for messages",
			   i );
		}
	    }
	}

    ALOG_LOG(p4_local->my_id,END_WAIT,1,"");
    ALOG_LOG(p4_local->my_id,BEGIN_USER,0,"");

    return (found);
}				/* p4_messages_available */

P4VOID queue_p4_message(msg, hdr)
struct p4_msg *msg;
struct p4_msg_queue *hdr;
{
    struct p4_queued_msg *q;

    q = alloc_quel();
    q->qmsg = msg;
    q->next = NULL;

    if (hdr->first_msg == NULL)
    {
	hdr->first_msg = q;
    }
    else
    {
	hdr->last_msg->next = q;
    }
    hdr->last_msg = q;
}


int send_message(type, from, to, msg, len, data_type, ack_req, p4_buff_ind)
char *msg;
int type, from, to, len, data_type;
P4BOOL ack_req, p4_buff_ind;
{
    struct p4_msg *tmsg;
    int conntype;

    if (to == 0xffff)		/* NCUBE broadcast */
	conntype = CONN_LOCAL;
    else
	conntype = p4_local->conntab[to].type;

    p4_dprintfl(90, "send_message: to = %d, conntype=%d conntype=%s\n",
		to, conntype, print_conn_type(conntype));
    ALOG_LOG(p4_local->my_id,END_USER,0,"");
    ALOG_LOG(p4_local->my_id,BEGIN_SEND,to,"");

    switch (conntype)
    {
      case CONN_ME:
	tmsg = get_tmsg(type,from,to,msg,len,data_type,ack_req,p4_buff_ind);
	p4_dprintfl(20, "sending msg of type %d to myself\n",type);
	queue_p4_message(tmsg, p4_local->queued_messages);
	p4_dprintfl(10, "sent msg of type %d to myself\n",type);
	break;

#ifdef CAN_DO_SHMEM_MSGS
      case CONN_SHMEM:
	tmsg = get_tmsg(type, from, to, msg, len, data_type, 
                        ack_req, p4_buff_ind);
	shmem_send(tmsg);
	break;
#endif

#ifdef CAN_DO_CUBE_MSGS
      case CONN_CUBE:
	tmsg = get_tmsg(type,from,to,msg,len,data_type,ack_req,p4_buff_ind);
	MD_cube_send(tmsg);
	if (!p4_buff_ind)
	    free_p4_msg(tmsg);
	break;
#endif

#ifdef CAN_DO_SOCKET_MSGS
      case CONN_REMOTE_OPENING:
      case CONN_REMOTE_NON_EST:
	if (establish_connection(to))
	{
	    p4_dprintfl(90, "send_message: conn just estabd to %d\n", to);
	}
	else
	{
	    p4_dprintf("send_message: unable to estab conn to %d\n", to);
	    ALOG_LOG(p4_local->my_id,END_SEND,to,"");
	    ALOG_LOG(p4_local->my_id,BEGIN_USER,0,"");
	    return (-1);
	}
	/* no break; - just fall into connected code */
      case CONN_REMOTE_EST:
	if (data_type == P4NOX || p4_local->conntab[to].same_data_rep)
	{
	    socket_send(type, from, to, msg, len, data_type, ack_req);
	}
	else
	{
#           ifdef CAN_DO_XDR
	    xdr_send(type, from, to, msg, len, data_type, ack_req);
#           else
	    p4_error("cannot do xdr sends\n",0);
#           endif
	}
	break;
#endif

#if defined(CAN_DO_SWITCH_MSGS)
      case CONN_REMOTE_SWITCH:
	tmsg = get_tmsg(type,from,to,msg,len,data_type,ack_req,p4_buff_ind);
	p4_dprintfl(20, "sending msg of type %d from %d to %d via switch_port %d\n",
	            tmsg->type,tmsg->from,to,p4_local->conntab[tmsg->to].switch_port,tmsg);
	sw_send(from, to,
		p4_local->conntab[tmsg->to].switch_port, tmsg,
		tmsg->len + sizeof(struct p4_msg) - sizeof(char *),
		type);
	p4_dprintfl(10, "sent msg of type %d from %d to %d via switch_port %d\n",
	            tmsg->type,tmsg->from,to,p4_local->conntab[tmsg->to].switch_port,tmsg);
	if (!p4_buff_ind)
	    free_p4_msg(tmsg);
	break;
#endif

#if defined(CAN_DO_TCMP_MSGS)
      case CONN_TCMP:
	tmsg = get_tmsg(type,from,to,msg,len,data_type,ack_req,p4_buff_ind);
	p4_dprintfl(20, "sending msg of type %d to %d via tcmp\n",type,to);
	MD_tcmp_send(type, from, to, tmsg, 
		     tmsg->len + sizeof(struct p4_msg) - sizeof(char *),
		     data_type, ack_req);
	p4_dprintfl(10, "sent msg of type %d to %d via tcmp\n",type,to);
	break;
#endif

      case CONN_REMOTE_DYING:
	p4_dprintfl(90, "send_message: proc %d is dying\n", to);
	ALOG_LOG(p4_local->my_id,END_SEND,to,"");
	ALOG_LOG(p4_local->my_id,BEGIN_USER,0,"");
	return (-1);

      default:
	p4_dprintf("send_message: to=%d; invalid conn type=%d\n",to,conntype);
	ALOG_LOG(p4_local->my_id,END_SEND,to,"");
	ALOG_LOG(p4_local->my_id,BEGIN_USER,0,"");
	return (-1);
    }

    ALOG_LOG(p4_local->my_id,END_SEND,to,"");
    ALOG_LOG(p4_local->my_id,BEGIN_USER,0,"");
    return (0);
}				/* send_message */

struct p4_msg *get_tmsg(type,from,to,msg,len,data_type,ack_req,p4_buff_ind)
char *msg;
int type, from, to, len, data_type, ack_req, p4_buff_ind;
{
    struct p4_msg *tmsg;

    if (p4_buff_ind)
    {
	tmsg = (struct p4_msg *) (msg - (sizeof(struct p4_msg) - sizeof(char *)));
    }
    else
    {
        tmsg = alloc_p4_msg(len);
	if (tmsg == NULL)
	{
	    p4_dprintf("OOPS! get_tmsg: could not alloc buff **\n");
	    return (NULL);
	}
	bcopy(msg, (char *) &(tmsg->msg), len);
    }
    tmsg->type	    = type;
    tmsg->from	    = from;
    tmsg->to	    = to;
    tmsg->len	    = len;
    tmsg->ack_req   = ack_req;
    tmsg->data_type = data_type;
    return (tmsg);
}


char *p4_msg_alloc(msglen)
int msglen;
{
    char *t;

    t = (char *) alloc_p4_msg(msglen);
    ((struct p4_msg *) t)->msg_id = -1;	/* msg not in use by IPSC isend */
    t = t + sizeof(struct p4_msg) - sizeof(char *);
    return(t);
}

P4VOID p4_msg_free(m)
char *m;
{
    char *t;

    t = m - (sizeof(struct p4_msg) - sizeof(char *));
    ((struct p4_msg *) t)->msg_id = -1;	/* msg not in use by IPSC isend */
    free_p4_msg((struct p4_msg *) t);
}


P4VOID initialize_msg_queue(mq)
struct p4_msg_queue *mq;
{
    mq->first_msg = NULL;
    mq->last_msg = NULL;
    (P4VOID) p4_moninit(&(mq->m), 1);
    p4_lock_init(&(mq->ack_lock));
    p4_lock(&(mq->ack_lock));
}


struct p4_queued_msg *alloc_quel()
{
    struct p4_queued_msg *q;

    p4_lock(&p4_global->avail_quel_lock);
    if (p4_global->avail_quel == NULL)
    {
	q = (struct p4_queued_msg *) p4_shmalloc(sizeof(struct p4_queued_msg));
	if (!q)
	    p4_error("alloc_quel:  could not allocate queue element",
		     sizeof(struct p4_queued_msg));
	 p4_dprintfl(50,"malloc'ed new quel at 0x%x\n",q);
    }
    else
    {
	q = p4_global->avail_quel;
	p4_global->avail_quel = q->next;
	p4_dprintfl(50,"reused quel at 0x%x\n",q);
    }
    p4_unlock(&p4_global->avail_quel_lock);
    p4_dprintfl(99,"Unlocked alloc_quel\n");
    return (q);
}

P4VOID free_quel(q)
struct p4_queued_msg *q;
{
    p4_lock(&p4_global->avail_quel_lock);
    q->next = p4_global->avail_quel;
    p4_global->avail_quel = q;
    p4_unlock(&p4_global->avail_quel_lock);
    p4_dprintfl(50,"freed quel at 0x%x to avail\n",q);
}

P4VOID free_avail_quels()
{
    struct p4_queued_msg *p,*q;

    p4_lock(&p4_global->avail_quel_lock);
    p = p4_global->avail_quel;
    while (p)
    {
	q = p->next;
	p4_dprintfl(50,"really freed quel at 0x%x\n",p);
	p4_shfree(p);
	p = q;
    }
    p4_unlock(&p4_global->avail_quel_lock);
}

/*
 * This yield code is taken from p2p_yield in ch_shmem/p2p.c
 */
/*
   Yield to other processes (rather than spinning in place)
 */
#ifdef USE_DYNAMIC_YIELD
#ifdef HAVE_SCHED_H
#include <sched.h>
#endif
void p4_yield( void )
{
    static int first_call = 1;
    static int kind = 1;

    if (first_call) {
	/* Get the yield style from the environment.  The default is 
	   sched_yield */
	char *name = getenv( "MPICH_YIELD" );
	if (name) {
	    if (strcmp( "sched_yield", name ) == 0) {
		kind = 1;
	    }
	    else if (strcmp( "select", name ) == 0) {
		kind = 2;
	    }
	    else if (strcmp( "none", name ) == 0) {
		kind = 0;
	    }
	}
    }
    switch (kind) {
    case 0: return;
    case 1:
	sched_yield();
	break;
    case 2: 
    {
	struct timeval tp;
	/*	fd_set readmask;  */
	tp.tv_sec  = 0;
	tp.tv_usec = 0;
	/*	FD_ZERO(&readmask);
		FD_SET(1,&readmask); */
	/*select( 2, (void *)&readmask, (void *)0, (void *)0, &tp ); */
	select( 0, (void *)0, (void *)0, (void *)0, &tp );
    }
    break;
    }
}
#else
#ifdef HAVE_SCHED_H
#include <sched.h>
#endif
void p4_yield( void )
{
#if defined(USE_SGINAP_YIELD)
    /* Multiprocessor IRIX machines may want to comment this out for lower
       latency */
    sginap(0);

#elif defined(USE_SCHED_YIELD)
    /* This is a POSIX function to yield the process */
    sched_yield();
#elif defined(USE_YIELD_YIELD)
    yield();
#elif defined(USE_SELECT_YIELD)
    /* Use a short select as a way to suggest to the OS to deschedule the 
       process.  Solaris select hangs if count is zero, so we check fd 1  
       This may not accomplish a process yield, depending on the OS.  
    */
    struct timeval tp;
    /* fd_set readmask; */
    tp.tv_sec  = 0;
    tp.tv_usec = 0;
    /* FD_ZERO(&readmask);
       FD_SET(1,&readmask); */
    /*select( 2, (void *)&readmask, (void *)0, (void *)0, &tp ); */
    select( 0, (void *)0, (void *)0, (void *)0, &tp );
#endif
}
#endif

/*
 * Check to see if ANY messages are available, without trying to receive them.
 * This is basically a "probe(anytag, anysource)", and provides the function
 * of a generalized Unix select.  In particular, we don't distinguish between
 * actual messages and EOF or error conditions.  The idea is that those
 * will be handled when the message is received.  The primary use of this 
 * routine is as a blocking call for *any* message activity, where the
 * application is receiving messages in different routines, determined by
 * the message tag.  This routine is basically a merge of p4_recv and 
 * recv_message, with all actual data transfers eliminated.
 */
int p4_waitformsg( void )
{
    struct p4_msg *tmsg;

    p4_dprintfl(20, "waiting for message" );

    ALOG_LOG(p4_local->my_id,END_USER,0,"");
    ALOG_LOG(p4_local->my_id,BEGIN_WAIT,0,"");
    if ((tmsg = search_p4_queue(-1,-1, 0)))
	return 1;
	/* If no message in the queues, wait for one to show up. */
#if  defined(CAN_DO_SOCKET_MSGS) && \
    !defined(CAN_DO_SHMEM_MSGS)  && \
    !defined(CAN_DO_CUBE_MSGS)   && \
    !defined(CAN_DO_SWITCH_MSGS) && \
    !defined(CAN_DO_TCMP_MSGS)
    /* Only socket messages */
    p4_wait_for_socket_msg( 1 );
#else
    {
	int i;
#ifdef USE_YIELD
	int cnt = 0;
#define BACKOFF_LIMIT 8
	/* If a yield method has been selected, yield after BACKOFF 
	   cycles.  
	   We also spin a few times on the shmem messages first,
	   since communication here will be faster and the
	   added latency of spinning on the shmem_msgs will
	   be neglible for the other devices
	*/
#endif
	while (P4_TRUE) {
#       if defined(CAN_DO_SHMEM_MSGS)
	    /* Optimally, this would spin for the round-trip time. 
	       Figure that at 5 us, and a very conservative .1us per
	       function call (in 2002, probably a gross overestimate),
	       that suggests at least 50 iterations.  We'll try 50
	       (that should be safe - shmem_msgs_available
	       is a short routine).   This could be tuned for
	       different platforms.
	    */
	    for (i=0; i<50; i++) {
		if (shmem_msgs_available()) {
		    /* Break out of while loop */
		    goto endwhile;
		}
	    }
#       endif

#       if defined(CAN_DO_EUI_MSGS) || defined(CAN_DO_EUIH_MSGS) || \
           defined(CAN_DO_CUBE_MSGS) || defined(CAN_DO_SWITCH_MSGS) || \
           defined(CAN_DO_TCMP_MSGS)
#          abort "Unsupported"
#       endif

#       if defined(CAN_DO_SOCKET_MSGS)
	    if (p4_wait_for_socket_msg( 0 )) {
		break;
	    }
#       endif

#       ifdef USE_YIELD
	    if (cnt++ > BACKOFF_LIMIT) {
		cnt = 0;
		p4_yield();
	    }
#       endif
	}
    }
#endif /* only socket msgs */
#ifdef CAN_DO_SHMEM_MSGS
 endwhile:
#endif
    ALOG_LOG(p4_local->my_id,END_WAIT,0,"");
    ALOG_LOG(p4_local->my_id,BEGIN_USER,0,"");

    return (1);
}
