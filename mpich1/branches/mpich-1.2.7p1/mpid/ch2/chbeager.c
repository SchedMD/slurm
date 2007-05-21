/*
 *
 *
 *  (C) 1995 by Argonne National Laboratory and Mississipi State University.
 *      All rights reserved.  See COPYRIGHT in top-level directory.
 */

#include "mpid.h"
#include "mpiddev.h"
#include "mpimem.h"
#include "reqalloc.h"
/* flow.h includs the optional flow control for eager delivery */
#include "flow.h"
#include "chpackflow.h"

/*
   Blocking, eager send/recv.
   These are ALWAYS for long messages.  Short messages are always
   handled in eager mode.
 */

/* Prototype definitions */
int MPID_CH_Eagerb_send ( void *, int, int, int, int, int, MPID_Msgrep_t );
int MPID_CH_Eagerb_isend ( void *, int, int, int, int, int, 
				     MPID_Msgrep_t, MPIR_SHANDLE * );
int MPID_CH_Eagerb_recv ( MPIR_RHANDLE *, int, void * );
int MPID_CH_Eagerb_irecv ( MPIR_RHANDLE *, int, void * );
int MPID_CH_Eagerb_save ( MPIR_RHANDLE *, int, void * );
int MPID_CH_Eagerb_unxrecv_start ( MPIR_RHANDLE *, void * );
int MPID_CH_Eagerb_cancel_send ( MPIR_SHANDLE * );
void MPID_CH_Eagerb_delete ( MPID_Protocol * );
/*
 * Definitions of the actual functions
 */
int MPID_CH_Eagerb_send( 
	void *buf, 
	int len, 
	int src_lrank, 
	int tag, 
	int context_id, 
	int dest,
	MPID_Msgrep_t msgrep )
{
    int              pkt_len;
    MPID_PKT_LONG_T  pkt;
    
    DEBUG_PRINT_MSG("S Starting Eagerb_send");
#ifdef MPID_FLOW_CONTROL
    DEBUG_PRINT_MSG("Entering while !MPID_FLOW_MEM_OK");
    while (!MPID_FLOW_MEM_OK(len,dest)) {
	/* Wait for a flow packet */
#ifdef MPID_DEBUG_ALL
	if (MPID_DebugFlag || MPID_DebugFlow) {
	    FPRINTF( MPID_DEBUG_FILE, 
		     "[%d] S Waiting for flow control packet from %d\n",
		     MPID_MyWorldRank, dest );
	}
#endif
	MPID_DeviceCheck( MPID_BLOCKING );
    }
    DEBUG_PRINT_MSG("Leaving while !MPID_FLOW_MEM_OK");
    MPID_FLOW_MEM_SEND(len,dest);
#endif

#ifdef MPID_PACK_CONTROL
    DEBUG_PRINT_MSG("Entering while !MPID_PACKET_CHECK_OK");
    while (!MPID_PACKET_CHECK_OK(dest)) {  /* begin while !ok loop */
#ifdef MPID_DEBUG_ALL
	if (MPID_DebugFlag || MPID_DebugFlow) {
	    FPRINTF( MPID_DEBUG_FILE, 
	  "[%d] S Waiting for protocol ACK packet (in eagerb_send) from %d\n",
		     MPID_MyWorldRank, dest );
	}
#endif
	MPID_DeviceCheck( MPID_BLOCKING );
    }  /* end while !ok loop */
    DEBUG_PRINT_MSG("Leaving while !MPID_PACKET_CHECK_OK");
    MPID_PACKET_ADD_SENT(MPID_MyWorldRank, dest) 
#endif

    pkt.mode	   = MPID_PKT_LONG;
    pkt_len	   = sizeof(MPID_PKT_LONG_T); 
    pkt.context_id = context_id;
    pkt.lrank	   = src_lrank;
    pkt.to         = dest;
    pkt.src        = MPID_MyWorldRank;
    pkt.seqnum     = pkt_len + len;
    pkt.tag	   = tag;
    pkt.len	   = len;
    MPID_DO_HETERO(pkt.msgrep = (int)msgrep);
#ifdef MPID_FLOW_CONTROL
    MPID_FLOW_MEM_ADD(&pkt,dest);
#endif

    DEBUG_PRINT_SEND_PKT("S Sending extra-long message",&pkt)

    MPID_PKT_PACK( &pkt, pkt_len, dest );

    /* Send as packet only */
    /* It is important to call drain_tiny(1) because, even though this is
       a blocking send, it might still block if some other process is
       not receiving.  In general, we really need a way to force 
       processes to unclog their channels.  On some systems, like the
       IBM SPx, this is impossible (without an unacceptable 
       performance burden). */
    MPID_DRAIN_INCOMING_FOR_TINY(1);
    MPID_SendControlBlock( &pkt, pkt_len, dest );

    /* Send the body of the message */
    MPID_SendChannel( buf, len, dest );

    return MPI_SUCCESS;
}

/*
 * This is the routine called when a packet of type MPID_PKT_LONG is
 * seen.  It receives the data as shown (final interface not set yet)
 */
int MPID_CH_Eagerb_recv( 
	MPIR_RHANDLE *rhandle,
	int          from,
	void         *in_pkt)
{
    MPID_PKT_LONG_T   *pkt = (MPID_PKT_LONG_T *)in_pkt;
    int    msglen, err = MPI_SUCCESS;

    msglen = pkt->len;

    MPID_DO_HETERO(rhandle->msgrep = (MPID_Msgrep_t)pkt->msgrep);
    DEBUG_PRINT_MSG("R Starting Eagerb_recv");
#ifdef MPID_FLOW_CONTROL
    MPID_FLOW_MEM_GET(pkt,from);
#endif

#ifdef MPID_PACK_CONTROL
    if (MPID_PACKET_RCVD_GET(pkt->src)) {
	MPID_SendProtoAck(pkt->to, pkt->src);
    }
    MPID_PACKET_ADD_RCVD(pkt->to, pkt->src);
#endif
    /* Check for truncation */
    MPID_CHK_MSGLEN(rhandle,msglen,err)
    /* Note that if we truncate, We really must receive the message in two 
       parts; the part that we can store, and the part that we discard.
       This case is not yet handled. */
#ifdef MPID_FLOW_CONTROL
    MPID_FLOW_MEM_READ(msglen,from);
    MPID_FLOW_MEM_RECV(msglen,from);
#endif
    rhandle->s.count	 = msglen;
    rhandle->s.MPI_ERROR = err;
    /* source/tag? */
    MPID_RecvFromChannel( rhandle->buf, msglen, from );
    if (rhandle->finish) {
	(rhandle->finish)( rhandle );
    }
    rhandle->is_complete = 1;
    
    return err;
}

/* 
 * This routine is called when it is time to receive an unexpected
 * message
 */
int MPID_CH_Eagerb_unxrecv_start( 
	MPIR_RHANDLE *rhandle,
	void         *in_runex)
{
    MPIR_RHANDLE *runex = (MPIR_RHANDLE *)in_runex;
    int          msglen, err = 0;

    msglen = runex->s.count;

    MPID_CHK_MSGLEN(rhandle,msglen,err);
    DEBUG_PRINT_MSG("R Starting unxrecv_start");
    /* Copy the data from the local area and free that area */
    if (runex->s.count > 0) {
	MEMCPY( rhandle->buf, runex->start, msglen );
	FREE( runex->start );
    }
#ifdef MPID_FLOW_CONTROL
    MPID_FLOW_MEM_RECV(msglen,runex->from);
#endif
    MPID_DO_HETERO(rhandle->msgrep = runex->msgrep);
    rhandle->s		 = runex->s;
    rhandle->s.count     = msglen;
    rhandle->s.MPI_ERROR = err;
    MPID_RecvFree( runex );
    rhandle->wait	 = 0;
    rhandle->test	 = 0;
    rhandle->push	 = 0;
    rhandle->is_complete = 1;
    if (rhandle->finish) 
	(rhandle->finish)( rhandle );

    return err;
}

/* Save an unexpected message in rhandle */
int MPID_CH_Eagerb_save( 
	MPIR_RHANDLE *rhandle,
	int          from,
	void         *in_pkt)
{
    MPID_PKT_T *pkt = (MPID_PKT_T *)in_pkt;

    DEBUG_PRINT_MSG("R Starting Eagerb_save");
#ifdef MPID_PACK_CONTROL
    if (MPID_PACKET_RCVD_GET(pkt->head.src)) {
	MPID_SendProtoAck(pkt->head.to, pkt->head.src);
    }
    MPID_PACKET_ADD_RCVD(pkt->head.to, pkt->head.src);
#endif
    rhandle->s.MPI_TAG	  = pkt->head.tag;
    rhandle->s.MPI_SOURCE = pkt->head.lrank;
    rhandle->s.MPI_ERROR  = 0;
    rhandle->partner      = pkt->head.to;
    rhandle->s.count      = pkt->head.len;
    rhandle->from         = from; /* Needed for flow control */
    rhandle->is_complete  = 1;
    /* Need to save msgrep for heterogeneous systems */
    MPID_DO_HETERO(rhandle->msgrep = (MPID_Msgrep_t)pkt->head.msgrep);
    if (pkt->head.len > 0) {
	rhandle->start	  = (void *)MALLOC( pkt->head.len );
	rhandle->is_complete  = 1;
	if (!rhandle->start) {
	    rhandle->s.MPI_ERROR = MPI_ERR_INTERN;
	    /* This is really pretty fatal, because we haven't received
	       the actual message, leaving it in the system */
	    return 1;
	}
#ifdef MPID_FLOW_CONTROL
	MPID_FLOW_MEM_READ(pkt->head.len,from);
#endif
	MPID_RecvFromChannel( rhandle->start, pkt->head.len, from );
    }
    rhandle->push = MPID_CH_Eagerb_unxrecv_start;
    return 0;
}

int MPID_CH_Eagerb_isend( 
	void *buf, 
	int len, 
	int src_lrank, 
	int tag, 
	int context_id, 
	int dest,
	MPID_Msgrep_t msgrep, 
	MPIR_SHANDLE *shandle )
{
    int pkt_len; 
    MPID_PKT_LONG_T  pkt;

    DEBUG_PRINT_MSG("S Starting Eagerb_isend");
#ifdef MPID_FLOW_CONTROL
    DEBUG_PRINT_MSG("Entering while !MPID_FLOW_MEM_OK");
    while (!MPID_FLOW_MEM_OK(len,dest)) {  /* begin while !ok loop */
	/* Wait for a flow packet */
#ifdef MPID_DEBUG_ALL
	if (MPID_DebugFlag || MPID_DebugFlow) {
	    FPRINTF( MPID_DEBUG_FILE, 
		     "[%d] S Waiting for flow control packet from %d\n",
		     MPID_MyWorldRank, dest );
	}
#endif
	MPID_DeviceCheck( MPID_BLOCKING );
    }  /* end while !ok loop */
    DEBUG_PRINT_MSG("Leaving while !MPID_FLOW_MEM_OK");
    MPID_FLOW_MEM_SEND(len,dest); 
#endif

#ifdef MPID_PACK_CONTROL
    DEBUG_PRINT_MSG("Entering while !MPID_PACKET_CHECK_OK");
    while (!MPID_PACKET_CHECK_OK(dest)) {  /* begin while !ok loop */
#ifdef MPID_DEBUG_ALL
	if (MPID_DebugFlag || MPID_DebugFlow) {
	    FPRINTF( MPID_DEBUG_FILE, 
	  "[%d] S Waiting for protocol ACK packet (in eagerb_isend) from %d\n",
		     MPID_MyWorldRank, dest );
	}
#endif
	MPID_DeviceCheck( MPID_BLOCKING );
    }  /* end while !ok loop */
    DEBUG_PRINT_MSG("Leaving while !MPID_PACKET_CHECK_OK");
    MPID_PACKET_ADD_SENT(MPID_MyWorldRank, dest) 
#endif

    pkt.mode	   = MPID_PKT_LONG;
    pkt_len	   = sizeof(MPID_PKT_LONG_T); 
    pkt.context_id = context_id;
    pkt.lrank	   = src_lrank;
    pkt.to         = dest;
    pkt.seqnum     = pkt_len + len; 
    pkt.src        = MPID_MyWorldRank;
    pkt.tag	   = tag;
    pkt.len	   = len;
    MPID_DO_HETERO(pkt.msgrep = (int)msgrep);
#ifdef MPID_FLOW_CONTROL
    MPID_FLOW_MEM_ADD(&pkt,dest);
#endif

    /* We save the address of the send handle in the packet; the receiver
       will return this to us */
    MPID_AINT_SET(pkt.send_id,shandle);
    
    /* Store partners rank in request in case message is cancelled */
    shandle->partner     = dest;

    DEBUG_PRINT_SEND_PKT("S Sending extra-long message",&pkt)

    MPID_PKT_PACK( &pkt, pkt_len, dest );

    /* Send as packet only */
    /* It is important to call drain_tiny(1) because, even though this is
       a blocking send, it might still block if some other process is
       not receiving.  In general, we really need a way to force 
       processes to unclog their channels.  On some systems, like the
       IBM SPx, this is impossible (without an unacceptable 
       performance burden). */
    MPID_DRAIN_INCOMING_FOR_TINY(1);

    /* send the header */
    MPID_SendControlBlock( &pkt, pkt_len, dest ); 

    /* Send the body of the message */
    MPID_SendChannel( buf, len, dest ); 

    shandle->is_complete = 1;
    if (shandle->finish) 
	(shandle->finish)( shandle );

    return MPI_SUCCESS;
}

int MPID_CH_Eagerb_cancel_send( 
	MPIR_SHANDLE *shandle)
{
    return 0;
}

/* This routine is called when a message arrives and was expected */
int MPID_CH_Eagerb_irecv( 
	MPIR_RHANDLE *rhandle,
	int          from,
	void         *in_pkt)
{
    MPID_PKT_LONG_T *pkt = (MPID_PKT_LONG_T *)in_pkt;
    int    msglen, err = MPI_SUCCESS;

    DEBUG_PRINT_MSG("R Starting Eagerb_irecv");

    msglen = pkt->len;
    /* Check for truncation */
    MPID_CHK_MSGLEN(rhandle,msglen,err)
#ifdef MPID_FLOW_CONTROL
    MPID_FLOW_MEM_GET(pkt,from);
    MPID_FLOW_MEM_READ(msglen,from);
    MPID_FLOW_MEM_RECV(msglen,from);
#endif

#ifdef MPID_PACK_CONTROL
    if (MPID_PACKET_RCVD_GET(pkt->src)) {
	MPID_SendProtoAck(pkt->to, pkt->src);
    }
    MPID_PACKET_ADD_RCVD(pkt->to, pkt->src);
#endif

    /* Note that if we truncate, We really must receive the message in two 
       parts; the part that we can store, and the part that we discard.
       This case is not yet handled. */
    rhandle->s.count	  = msglen;
    rhandle->s.MPI_TAG	  = pkt->tag;
    rhandle->s.MPI_SOURCE = pkt->lrank;
    rhandle->s.MPI_ERROR  = err;

    MPID_RecvFromChannel( rhandle->buf, msglen, from );

    if (rhandle->finish)
	(rhandle->finish)( rhandle );
    rhandle->wait	 = 0;
    rhandle->test	 = 0;
    rhandle->push	 = 0;
    rhandle->is_complete = 1;
    
    return err;
}


#ifdef FOO
/* There is common code to de-list an unmatched message */
int MPID_CH_Eagerb_cancel_recv( )
{
return 0;
}

int MPID_CH_Eagerb_test_send( )
{
    return 1;
}

int MPID_CH_Eagerb_wait_send( )
{
    return 1;
}

/* Either it is already present, or it isn't here */
int MPID_CH_Eagerb_test_recv( )
{
    return 0;
}

/* This code should do what ? */
int MPID_CH_Eagerb_wait_recv( )
{
    return 0;
}

#endif

void MPID_CH_Eagerb_delete( 
	MPID_Protocol *p)
{
    FREE( p );
}

MPID_Protocol *MPID_CH_Eagerb_setup()
{
    MPID_Protocol *p;

    p = (MPID_Protocol *) MALLOC( sizeof(MPID_Protocol) );
    if (!p) return 0;
    p->send	   = MPID_CH_Eagerb_send;
    p->recv	   = MPID_CH_Eagerb_recv;
    p->isend	   = MPID_CH_Eagerb_isend;
    p->wait_send   = 0;
    p->push_send   = 0;
    p->cancel_send = MPID_CH_Eagerb_cancel_send;
    p->irecv	   = MPID_CH_Eagerb_irecv;
    p->wait_recv   = 0;
    p->push_recv   = 0;
    p->cancel_recv = 0;
    p->do_ack      = 0;
    p->unex        = MPID_CH_Eagerb_save;
    p->delete      = MPID_CH_Eagerb_delete;

    return p;
}
