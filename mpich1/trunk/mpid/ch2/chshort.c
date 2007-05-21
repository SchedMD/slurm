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
#include "flow.h"
#include "chpackflow.h"

/* Prototype definitions */
int MPID_CH_Eagerb_send_short ( void *, int, int, int, int, int, 
					  MPID_Msgrep_t );
int MPID_CH_Eagerb_isend_short ( void *, int, int, int, int, int, 
					   MPID_Msgrep_t, MPIR_SHANDLE * );
int MPID_CH_Eagerb_recv_short ( MPIR_RHANDLE *, int, void * );
int MPID_CH_Eagerb_save_short ( MPIR_RHANDLE *, int, void *);
int MPID_CH_Eagerb_unxrecv_start_short ( MPIR_RHANDLE *, void * );
void MPID_CH_Eagerb_short_delete ( MPID_Protocol * );
/*
 * Definitions of the actual functions
 */

int MPID_CH_Eagerb_send_short( 
	void *buf, 
	int len, 
	int src_lrank, 
	int tag, 
	int context_id, 
	int dest,
	MPID_Msgrep_t msgrep )
{
    int pkt_len;
    MPID_PKT_SHORT_T pkt;

    DEBUG_PRINT_MSG("S Starting Eagerb_send_short");
#ifdef MPID_PACK_CONTROL
    DEBUG_PRINT_MSG("Entering while !MPID_PACKET_CHECK_OK"); 
    while (!MPID_PACKET_CHECK_OK(dest)) {  /* begin while !ok loop */
	/* Wait for a protocol ACK packet */
#ifdef MPID_DEBUG_ALL
	if (MPID_DebugFlag || MPID_DebugFlow) {
		FPRINTF(MPID_DEBUG_FILE,
   "[%d] S Waiting for a protocol ACK packet (in eagerb_send_short) from %d\n",
			MPID_MyWorldRank, dest);
	}
#endif
	MPID_DeviceCheck( MPID_BLOCKING );
    }  /* end while !ok loop */
    DEBUG_PRINT_MSG("Leaving while !MPID_PACKET_CHECK_OK"); 
    MPID_PACKET_ADD_SENT(MPID_MyWorldRank, dest);
#endif

    /* These references are ordered to match the order they appear in the 
       structure */
    /*pkt_len        = sizeof(MPID_PKT_HEAD_T) + sizeof(MPID_Aint); */
    /* The size is the full size of the packet, minus the size of the
       data area */
    pkt_len        = sizeof(MPID_PKT_SHORT_T) - MPID_PKT_MAX_DATA_SIZE;
    pkt.mode	   = MPID_PKT_SHORT;
    pkt.context_id = context_id;
    pkt.lrank	   = src_lrank;
    pkt.to         = dest;
    pkt.seqnum     = len + pkt_len; 
    pkt.src        = MPID_MyWorldRank;
    pkt.tag	   = tag;
    pkt.len	   = len;
    MPID_DO_HETERO(pkt.msgrep = (int)msgrep);

    DEBUG_PRINT_SEND_PKT("S Sending",&pkt);
    MPID_PKT_PACK( &pkt, pkt_len, dest );

    if (len > 0) {
	MEMCPY( pkt.buffer, buf, len );
	DEBUG_PRINT_PKT_DATA("S Getting data from buf",&pkt);
    }
    /* Always use a blocking send for short messages.
       (May fail with systems that do not provide adequate
       buffering.  These systems should switch to non-blocking sends)
     */
    DEBUG_PRINT_SEND_PKT("S Sending message in a single packet",&pkt);

    /* In case the message is marked as non-blocking, indicate that we don't
       need to wait on it.  We may also want to use nonblocking operations
       to send the envelopes.... */
    MPID_DRAIN_INCOMING_FOR_TINY(1);
    MPID_SendControlBlock( &pkt, len + pkt_len, dest );
    DEBUG_PRINT_MSG("S Sent message in a single packet");

    return MPI_SUCCESS;
}

int MPID_CH_Eagerb_isend_short( 
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
    MPID_PKT_SHORT_T pkt;

    DEBUG_PRINT_MSG("S Starting Eagerb_isend_short");
#ifdef MPID_PACK_CONTROL
    DEBUG_PRINT_MSG("Entering while !MPID_PACKET_CHECK_OK"); 
    while (!MPID_PACKET_CHECK_OK(dest)) {  /* begin while !ok loop */
	/* Wait for a protocol ACK packet */
#ifdef MPID_DEBUG_ALL
	if (MPID_DebugFlag || MPID_DebugFlow) {
		FPRINTF(MPID_DEBUG_FILE,
   "[%d] S Waiting for a protocol ACK packet (in eagerb_send_short) from %d\n",
			MPID_MyWorldRank, dest);
	}
#endif
	MPID_DeviceCheck( MPID_BLOCKING );
    }  /* end while !ok loop */
    DEBUG_PRINT_MSG("Leaving while !MPID_PACKET_CHECK_OK"); 
    MPID_PACKET_ADD_SENT(MPID_MyWorldRank, dest);
#endif

    /* These references are ordered to match the order they appear in the 
       structure */
    /*pkt_len        = sizeof(MPID_PKT_HEAD_T) + sizeof(MPID_Aint); */
    pkt_len        = sizeof(MPID_PKT_SHORT_T) - MPID_PKT_MAX_DATA_SIZE;
    pkt.mode	   = MPID_PKT_SHORT;
    pkt.context_id = context_id;
    pkt.lrank	   = src_lrank;
    pkt.to         = dest;
    pkt.seqnum     = len + pkt_len;
    pkt.src        = MPID_MyWorldRank;
    pkt.tag	   = tag;
    pkt.len	   = len;
    MPID_DO_HETERO(pkt.msgrep = (int)msgrep);

    /* We save the address of the send handle in the packet; the receiver
       will return this to us */
    MPID_AINT_SET(pkt.send_id,shandle);
    
    /* Store partners rank in request in case message is cancelled */
    shandle->partner     = dest;
    shandle->is_complete = 1;
    /* Instead of this, the calling code should test from not-complete,
       and set finish if needed */
/* #ifdef FOO
    if (shandle->finish) 
	(shandle->finish)( shandle );
#endif */
    
    DEBUG_PRINT_SEND_PKT("S Sending",&pkt);
    MPID_PKT_PACK( &pkt, sizeof(pkt), dest );

    if (len > 0) {
	MEMCPY( pkt.buffer, buf, len );
	DEBUG_PRINT_PKT_DATA("S Getting data from buf",&pkt);
    }
    /* Always use a blocking send for short messages.
       (May fail with systems that do not provide adequate
       buffering.  These systems should switch to non-blocking sends)
     */
    DEBUG_PRINT_SEND_PKT("S Sending message in a single packet",&pkt);

    /* In case the message is marked as non-blocking, indicate that we don't
       need to wait on it.  We may also want to use nonblocking operations
       to send the envelopes.... */
    MPID_DRAIN_INCOMING_FOR_TINY(1);
    MPID_SendControlBlock( &pkt, len + pkt_len, dest ); 

    DEBUG_PRINT_MSG("S Sent message in a single packet");
	
    return MPI_SUCCESS;

}

int MPID_CH_Eagerb_recv_short( 
	MPIR_RHANDLE *rhandle,
	int          from_grank,
	void         *in_pkt)
{
    MPID_PKT_SHORT_T *pkt = (MPID_PKT_SHORT_T *)in_pkt;
    int          msglen;
    int          err = MPI_SUCCESS;
    
    msglen		  = pkt->len;

    DEBUG_PRINT_MSG("R Starting Eagerb_recv_short");

#ifdef MPID_PACK_CONTROL
    if (MPID_PACKET_RCVD_GET(pkt->src)) {
	MPID_SendProtoAck(pkt->to, pkt->src);
    }
    MPID_PACKET_ADD_RCVD(pkt->to, pkt->src);
#endif

    rhandle->s.MPI_TAG	  = pkt->tag;
    rhandle->s.MPI_SOURCE = pkt->lrank;

    MPID_CHK_MSGLEN(rhandle,msglen,err);
    if (msglen > 0) {
	MEMCPY( rhandle->buf, pkt->buffer, msglen ); 
    }
    rhandle->s.count	  = msglen;
    rhandle->s.MPI_ERROR  = err;

    if (rhandle->finish) {
	MPID_DO_HETERO(rhandle->msgrep = (MPID_Msgrep_t)pkt->msgrep);
	(rhandle->finish)( rhandle );
    }
    rhandle->is_complete = 1;

    return err;
}

/* 
 * This routine is called when it is time to receive an unexpected
 * message
 */
int MPID_CH_Eagerb_unxrecv_start_short( 
	MPIR_RHANDLE *rhandle,
	void         *in_runex)
{
    MPIR_RHANDLE *runex = (MPIR_RHANDLE *)in_runex;
    int          msglen, err = 0;

    msglen = runex->s.count;
    DEBUG_PRINT_MSG("R Starting Eagerb_unxrecv_start_short");
    MPID_CHK_MSGLEN(rhandle,msglen,err);
    /* Copy the data from the local area and free that area */
    if (runex->s.count > 0) {
	MEMCPY( rhandle->buf, runex->start, msglen );
	FREE( runex->start );
    }
    MPID_DO_HETERO(rhandle->msgrep = runex->msgrep);
    rhandle->s		 = runex->s;
    rhandle->s.count     = msglen;
    rhandle->s.MPI_ERROR = err;
    rhandle->wait	 = 0;
    rhandle->test	 = 0;
    rhandle->push	 = 0;
    rhandle->is_complete = 1;
    if (rhandle->finish) 
	(rhandle->finish)( rhandle );
    MPID_RecvFree( runex );

    return err;
}

/* Save an unexpected message in rhandle */
int MPID_CH_Eagerb_save_short( 
	MPIR_RHANDLE *rhandle,
	int          from,
	void         *in_pkt)
{
    MPID_PKT_SHORT_T   *pkt = (MPID_PKT_SHORT_T *)in_pkt;

    DEBUG_PRINT_MSG("R Starting Eagerb_save_short");
#ifdef MPID_PACK_CONTROL
    if (MPID_PACKET_RCVD_GET(pkt->src)) {
	MPID_SendProtoAck(pkt->to, pkt->src);
    }
    MPID_PACKET_ADD_RCVD(pkt->to, pkt->src);
#endif

    rhandle->s.MPI_TAG	  = pkt->tag;
    rhandle->s.MPI_SOURCE = pkt->lrank;
    rhandle->s.MPI_ERROR  = 0;
    rhandle->from         = from;
    rhandle->partner      = pkt->to;
    rhandle->s.count      = pkt->len;
    /* rhandle->is_complete  = 1; */
    /* Need to save msgrep for heterogeneous systems */
    MPID_DO_HETERO(rhandle->msgrep = (MPID_Msgrep_t)pkt->msgrep);
    if (pkt->len > 0) {
	rhandle->start	  = (void *)MALLOC( pkt->len );
	if (!rhandle->start) {
	    rhandle->s.MPI_ERROR = MPI_ERR_INTERN;
	    return 1;
	}
	MEMCPY( rhandle->start, pkt->buffer, pkt->len );
    }
    rhandle->push = MPID_CH_Eagerb_unxrecv_start_short;
    return 0;
}

void MPID_CH_Eagerb_short_delete( 
	MPID_Protocol *p)
{
    FREE( p );
}

MPID_Protocol *MPID_CH_Short_setup()
{
    MPID_Protocol *p;

    p = (MPID_Protocol *) MALLOC( sizeof(MPID_Protocol) );
    if (!p) return 0;
    p->send	   = MPID_CH_Eagerb_send_short;
    p->recv	   = MPID_CH_Eagerb_recv_short;
    p->isend	   = MPID_CH_Eagerb_isend_short;
    p->wait_send   = 0;
    p->push_send   = 0;
    p->cancel_send = 0;
    p->irecv	   = 0;
    p->wait_recv   = 0;
    p->push_recv   = 0;
    p->cancel_recv = 0;
    p->do_ack      = 0;
    p->unex        = MPID_CH_Eagerb_save_short;
    p->delete      = MPID_CH_Eagerb_short_delete;

    return p;
}
