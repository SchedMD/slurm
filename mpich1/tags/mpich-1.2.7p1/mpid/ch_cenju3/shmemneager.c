#include "mpid.h"
#include "mpiddev.h"
#include "mpimem.h"
#include "reqalloc.h"

static int  ready = 0;
static char cready = 0;

/*
   Nonblocking, eager shared-memory send/recv.
 */

/* Prototype definitions */
int MPID_CENJU3_Eagern_send ANSI_ARGS(( void *, int, int, int, int, int, MPID_Msgrep_t ));
int MPID_CENJU3_Eagern_isend ANSI_ARGS(( void *, int, int, int, int, int, MPID_Msgrep_t,
				     MPIR_SHANDLE * ));
int MPID_CENJU3_Eagern_cancel_send ANSI_ARGS(( MPIR_SHANDLE * ));
int MPID_CENJU3_Eagern_wait_send ANSI_ARGS(( MPIR_SHANDLE * ));
int MPID_CENJU3_Eagern_test_send ANSI_ARGS(( MPIR_SHANDLE * ));
int MPID_CENJU3_Eagern_save ANSI_ARGS(( MPIR_RHANDLE *, int, void * ));
int MPID_CENJU3_Eagern_unxrecv_start ANSI_ARGS(( MPIR_RHANDLE *, void * ));
void MPID_CENJU3_Eagern_delete ANSI_ARGS(( MPID_Protocol * ));

/* 
 * Blocking operations come from chbeager.c
 */
extern int MPID_CENJU3_Eagerb_send ANSI_ARGS(( void *, int, int, int, int, 
					   int, int ));
extern int MPID_CENJU3_Eagerb_recv ANSI_ARGS(( MPIR_RHANDLE *, int, void * ));
extern int MPID_CENJU3_Eagerb_irecv ANSI_ARGS(( MPIR_RHANDLE *, int, void * ));
extern int MPID_CENJU3_Eagerb_save ANSI_ARGS(( MPIR_RHANDLE *, int, void * ));
extern int MPID_CENJU3_Eagerb_unxrecv_start ANSI_ARGS(( MPIR_RHANDLE *, void * ));

/*
 * Definitions of the actual functions
 */

int MPID_CENJU3_Eagern_isend( buf, len, src_lrank, tag, context_id, dest,
			 msgrep, shandle )
void *buf;
int  len, tag, context_id, src_lrank, dest;
MPID_Msgrep_t msgrep;
MPIR_SHANDLE *shandle;
{
    VOLATILE int *destready = &(MPID_destready[dest].buf_ready);
    int                       pkt_len;
    MPID_PKT_SEND_ADDRESS_T   *pkt, spkt;
    register void             *MPID_brk_pointer;
    
    pkt = &spkt;

    pkt->mode	    = MPID_PKT_SEND_ADDRESS;
    pkt->context_id = context_id;
    pkt->lrank	    = src_lrank;
    pkt->tag	    = tag;
    pkt->len	    = len;

    MPID_REMOTE_WRITE (dest, MPID_destready[dest].buf,
                             buf, len);

    MPID_BUF_READY_SET(destready);

    shandle->wait	 = 0;
    shandle->test	 = 0;
    shandle->is_complete = 1;

    DEBUG_PRINT_SEND_PKT("S Sending extra-long message",pkt);
	
    /* Send as packet only */

    MPID_CENJU3_SendControl( pkt, sizeof(MPID_PKT_SEND_ADDRESS_T), dest );

    return MPI_SUCCESS;
}

int MPID_CENJU3_Eagern_send( buf, len, src_lrank, tag, context_id, dest,
			 msgrep )
void *buf;
int  len, tag, context_id, src_lrank, dest;
MPID_Msgrep_t msgrep;
{
    MPIR_SHANDLE shandle;

    shandle.finish = 0;

    DEBUG_INIT_STRUCT(&shandle,sizeof(shandle));
    MPIR_SET_COOKIE((&shandle),MPIR_REQUEST_COOKIE)
    MPID_CENJU3_Eagern_isend( buf, len, src_lrank, tag, context_id, dest,
			     msgrep, &shandle );
    /* Note that isend is (probably) complete */
    if (!shandle.is_complete) {
	DEBUG_TEST_FCN(shandle.wait,"req->wait");
	shandle.wait( &shandle );
    }
    return MPI_SUCCESS;
}

int MPID_CENJU3_Eagern_cancel_send( shandle )
MPIR_SHANDLE *shandle;
{
    return 0;
}

int MPID_CENJU3_Eagern_test_send( shandle )
MPIR_SHANDLE *shandle;
{
    /* Test for completion */

    if (shandle->is_complete) {
       if (shandle->finish)
           (shandle->finish)( shandle );
    }

    return MPI_SUCCESS;
}

int MPID_CENJU3_Eagern_wait_send( shandle )
MPIR_SHANDLE *shandle;
{
    VOLATILE int *complete = &(shandle->is_complete);
    DEBUG_PRINT_MSG("R Starting Eagern_wait_send")

    /* Wait for completion */

    while (! *complete) {
       MPID_DeviceCheck( MPID_NOTBLOCKING );
    }

    if (shandle->finish)
          (shandle->finish)( shandle );

    DEBUG_PRINT_MSG("R Exiting Eagern_wait_send")
    return MPI_SUCCESS;
}

/*
 * This is the routine called when a packet of type MPID_PKT_SEND_ADDRESS is
 * seen.  It receives the data as shown (final interface not set yet)
   ???? WO IST DIE DIFFERENCE to IRECV ?????
 */
int MPID_CENJU3_Eagern_recv( rhandle, from, in_pkt )
MPIR_RHANDLE *rhandle;
int          from;
void         *in_pkt;
{
    MPID_PKT_SEND_ADDRESS_T   *pkt = (MPID_PKT_SEND_ADDRESS_T *)in_pkt;
    int    msglen, err = MPI_SUCCESS;

    DEBUG_PRINT_MSG("R Starting Eagern_recv")

    msglen = pkt->len;

    /* Check for truncation */

    MPID_CHK_MSGLEN(rhandle,msglen,err)

    rhandle->s.count	 = msglen;
    rhandle->s.MPI_TAG    = pkt->tag;
    rhandle->s.MPI_SOURCE = pkt->lrank;
    rhandle->s.MPI_ERROR = err;

    msglen = MPID_MIN (msglen, rhandle->len);

    if (msglen) MEMCPY( rhandle->buf, *(MPID_eager_pool+from), msglen );

#ifdef MPID_TWO_WRITES
    MPID_PKT_READY_CLR(&(pkt->ready));
#else
    memset ((void *) pkt, 0, (size_t) pkt->size);
#endif

    MPID_REMOTE_WRITE (from, &(MPID_destready[MPID_myid].buf_ready),
                             &ready, sizeof(int));
    MPID_REMOTE_WRITE (from, MPID_ready_pkt_to_clr,
                             &cready, sizeof(char));

    rhandle->is_complete = 1;

    if (rhandle->finish) {
	(rhandle->finish)( rhandle );
    }

    DEBUG_PRINT_MSG("R Exiting Eagern_recv")
    
    return err;
}

/* This routine is called when a message arrives and was expected */

int MPID_CENJU3_Eagern_irecv( rhandle, from, in_pkt )
MPIR_RHANDLE *rhandle;
int          from;
void         *in_pkt;
{
    MPID_PKT_SEND_ADDRESS_T *pkt = (MPID_PKT_SEND_ADDRESS_T *)in_pkt;
    int    msglen, err = MPI_SUCCESS;

    DEBUG_PRINT_MSG("R Starting Eagern_irecv")

    msglen = pkt->len;

    /* Check for truncation */

    MPID_CHK_MSGLEN(rhandle,msglen,err)

    rhandle->s.count	  = msglen;
    rhandle->s.MPI_TAG	  = pkt->tag;
    rhandle->s.MPI_SOURCE = pkt->lrank;
    rhandle->s.MPI_ERROR  = err;

    msglen = MPID_MIN (msglen, rhandle->len);
    if (msglen) MEMCPY( rhandle->buf, *(MPID_eager_pool+from), msglen );

#ifdef MPID_TWO_WRITES
    MPID_PKT_READY_CLR(&(pkt->ready));
#else
    memset ((void *) pkt, 0, (size_t) pkt->size);
#endif

    MPID_REMOTE_WRITE (from, &(MPID_destready[MPID_myid].buf_ready),
                             &ready, sizeof(int));
    MPID_REMOTE_WRITE (from, MPID_ready_pkt_to_clr,
                             &cready, sizeof(char));
   
    if (rhandle->finish)
	(rhandle->finish)( rhandle );

    rhandle->wait	 = 0;
    rhandle->test	 = 0;
    rhandle->push	 = 0;
    rhandle->is_complete = 1;

    DEBUG_PRINT_MSG("R Starting Exiting_irecv")
    return err;
}

/* Save an unexpected message in rhandle */
int MPID_CENJU3_Eagern_save( rhandle, from, in_pkt )
MPIR_RHANDLE *rhandle;
int          from;
void         *in_pkt;
{
    MPID_PKT_SEND_ADDRESS_T *pkt = (MPID_PKT_SEND_ADDRESS_T *)in_pkt;

    rhandle->s.MPI_TAG	  = pkt->tag;
    rhandle->s.MPI_SOURCE = pkt->lrank;
    rhandle->s.MPI_ERROR  = 0;
    rhandle->s.count      = pkt->len;
    rhandle->is_complete  = 0;
    rhandle->from         = from;

#ifdef MPID_TWO_WRITES
    MPID_PKT_READY_CLR(&(pkt->ready));
#else
    memset ((void *) pkt, 0, (size_t) pkt->size);
#endif

    MPID_REMOTE_WRITE(from, MPID_ready_pkt_to_clr,
                            &cready, sizeof(char));

    rhandle->push = MPID_CENJU3_Eagern_unxrecv_start;

    return 0;
}
/* 
 * This routine is called when it is time to receive an unexpected
 * message
 */
int MPID_CENJU3_Eagern_unxrecv_start( rhandle, in_runex )
MPIR_RHANDLE *rhandle;
void         *in_runex;
{
    MPIR_RHANDLE *runex = (MPIR_RHANDLE *)in_runex;
    int          msglen, err = 0;

    DEBUG_PRINT_MSG("R Starting Eagern_unxrecv_start")

    msglen = runex->s.count;

    MPID_CHK_MSGLEN(rhandle,msglen,err);

    msglen = MPID_MIN (msglen, rhandle->len);
    if (msglen) MEMCPY( rhandle->buf, *(MPID_eager_pool+runex->from), msglen );

    MPID_REMOTE_WRITE (runex->from, &(MPID_destready[MPID_myid].buf_ready),
                                    &ready, sizeof(int));
   
    rhandle->s		 = runex->s;
    rhandle->wait	 = 0;
    rhandle->test	 = 0;
    rhandle->push	 = 0;
    rhandle->is_complete = 1;

    if (rhandle->finish) 
	(rhandle->finish)( rhandle );

    MPID_RecvFree( runex );

    DEBUG_PRINT_MSG("R Exiting Eagern_unxrecv_start")
    return err;
}

void MPID_CENJU3_Eagern_delete( p )
MPID_Protocol *p;
{
    FREE( p );
}

MPID_Protocol *MPID_CENJU3_Eagern_setup()
{
    MPID_Protocol *p;

    p = (MPID_Protocol *) MALLOC( sizeof(MPID_Protocol) );
    if (!p) return 0;
    p->send	   = MPID_CENJU3_Eagern_send;
    p->recv	   = MPID_CENJU3_Eagern_recv;
    p->isend	   = MPID_CENJU3_Eagern_isend;
    p->wait_send   = 0;
    p->push_send   = 0;
    p->cancel_send = MPID_CENJU3_Eagern_cancel_send;
    p->irecv	   = MPID_CENJU3_Eagern_irecv;
    p->wait_recv   = 0;
    p->push_recv   = 0;
    p->cancel_recv = 0;
    p->do_ack      = 0;
    p->unex        = MPID_CENJU3_Eagern_save;
    p->delete      = MPID_CENJU3_Eagern_delete;

    return p;
}
