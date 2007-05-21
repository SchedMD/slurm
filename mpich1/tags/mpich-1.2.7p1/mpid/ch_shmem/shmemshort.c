#include "mpid.h"
#include "mpiddev.h"
#include "mpimem.h"
#include "reqalloc.h"
#include "flow.h"
#include "chpackflow.h"

/*
 * This is almost exactly like chshort.c, except that packets are allocated
 * from the pool rather than on the call stack, and there is no heterogeneous
 * support.
 */

/* Prototype definitions */
int MPID_SHMEM_Eagerb_send_short ( void *, int, int, int, int, int, 
					  MPID_Msgrep_t );
int MPID_SHMEM_Eagerb_isend_short ( void *, int, int, int, int, int, 
					   MPID_Msgrep_t, MPIR_SHANDLE * );
int MPID_SHMEM_Eagerb_recv_short ( MPIR_RHANDLE *, int, void * );
int MPID_SHMEM_Eagerb_save_short ( MPIR_RHANDLE *, int, void *);
int MPID_SHMEM_Eagerb_unxrecv_start_short ( MPIR_RHANDLE *, void * );
void MPID_SHMEM_Eagerb_short_delete ( MPID_Protocol * );
/*
 * Definitions of the actual functions
 */

int MPID_SHMEM_Eagerb_send_short( buf, len, src_lrank, tag, context_id, dest,
			       msgrep )
void          *buf;
int           len, tag, context_id, src_lrank, dest;
MPID_Msgrep_t msgrep;
{
    int pkt_len;
    MPID_PKT_SHORT_T *pkt;

    /* These references are ordered to match the order they appear in the 
       structure */
    DEBUG_PRINT_MSG("S Getting a packet");
    pkt = (MPID_PKT_SHORT_T *)MPID_SHMEM_GetSendPkt(0);
    /* GetSendPkt hangs untill successful */
    DEBUG_PRINT_MSG("S Starting Eagerb_send_short");
#ifdef MPID_PACK_CONTROL
    while (!MPID_PACKET_CHECK_OK(dest)) {  /* begin while !ok loop */
	/* Wait for a protocol ACK packet */
#ifdef MPID_DEBUG_ALL
	if (MPID_DebugFlag || MPID_DebugFlow) {
		FPRINTF(MPID_DEBUG_FILE,
   "[%d] S Waiting for a protocol ACK packet (in eagerb_send_short) from %d\n",
			MPID_myid, dest);
	}
#endif
	MPID_DeviceCheck( MPID_BLOCKING );
    }  /* end while !ok loop */

    MPID_PACKET_ADD_SENT(MPID_myid, dest);
#endif

    pkt_len         = sizeof(MPID_PKT_HEAD_T) + sizeof(MPID_Aint);
    pkt->mode	    = MPID_PKT_SHORT;
    pkt->context_id = context_id;
    pkt->lrank	    = src_lrank;
    pkt->to         = dest;
    pkt->seqnum     = len + pkt_len;
    pkt->tag	    = tag;
    pkt->len	    = len;

    DEBUG_PRINT_SEND_PKT("S Sending",pkt);

    if (len > 0) {
	MEMCPY( pkt->buffer, buf, len );
	DEBUG_PRINT_PKT_DATA("S Getting data from buf",pkt);
    }
    /* Always use a blocking send for short messages.
       (May fail with systems that do not provide adequate
       buffering.  These systems should switch to non-blocking sends)
     */
    DEBUG_PRINT_SEND_PKT("S Sending message in a single packet",pkt);

    /* In case the message is marked as non-blocking, indicate that we don't
       need to wait on it.  We may also want to use nonblocking operations
       to send the envelopes.... */
    MPID_SHMEM_SendControl( (MPID_PKT_T*)pkt, len + pkt_len, dest );
    DEBUG_PRINT_MSG("S Sent message in a single packet");

    return MPI_SUCCESS;
}

int MPID_SHMEM_Eagerb_isend_short( buf, len, src_lrank, tag, context_id, dest,
			 msgrep, shandle )
void          *buf;
int           len, tag, context_id, src_lrank, dest;
MPID_Msgrep_t msgrep;
MPIR_SHANDLE *shandle;
{

    int pkt_len;
    MPID_PKT_SHORT_T *pkt;

    /* These references are ordered to match the order they appear in the 
       structure */
    DEBUG_PRINT_MSG("S Getting a packet");
    pkt = (MPID_PKT_SHORT_T *)MPID_SHMEM_GetSendPkt(0);
    /* GetSendPkt hangs untill successful */
    DEBUG_PRINT_MSG("S Starting Eagerb_isend_short");
#ifdef MPID_PACK_CONTROL
    while (!MPID_PACKET_CHECK_OK(dest)) {  /* begin while !ok loop */
	/* Wait for a protocol ACK packet */
#ifdef MPID_DEBUG_ALL
	if (MPID_DebugFlag || MPID_DebugFlow) {
		FPRINTF(MPID_DEBUG_FILE,
   "[%d] S Waiting for a protocol ACK packet (in eagerb_send_short) from %d\n",
			MPID_myid, dest);
	}
#endif
	MPID_DeviceCheck( MPID_BLOCKING );
    }  /* end while !ok loop */

    MPID_PACKET_ADD_SENT(MPID_myid, dest);
#endif

    pkt_len         = sizeof(MPID_PKT_HEAD_T) + sizeof(MPID_Aint);
    pkt->mode	    = MPID_PKT_SHORT;
    pkt->context_id = context_id;
    pkt->lrank	    = src_lrank;
    pkt->to         = dest;
    pkt->seqnum     = len + pkt_len;
    pkt->tag	    = tag;
    pkt->len	    = len;

    /* We save the address of the send handle in the packet; the receiver
       will return this to us */
    MPID_AINT_SET(pkt->send_id,shandle);
    
    /* Store partners rank in request in case message is cancelled */
    shandle->partner     = dest;
    shandle->is_complete = 1;

    DEBUG_PRINT_SEND_PKT("S Sending",pkt);

    if (len > 0) {
	MEMCPY( pkt->buffer, buf, len );
	DEBUG_PRINT_PKT_DATA("S Getting data from buf",pkt);
    }
    /* Always use a blocking send for short messages.
       (May fail with systems that do not provide adequate
       buffering.  These systems should switch to non-blocking sends)
     */
    DEBUG_PRINT_SEND_PKT("S Sending message in a single packet",pkt);

    /* In case the message is marked as non-blocking, indicate that we don't
       need to wait on it.  We may also want to use nonblocking operations
       to send the envelopes.... */
    MPID_SHMEM_SendControl( (MPID_PKT_T*)pkt, len + pkt_len, dest );
    DEBUG_PRINT_MSG("S Sent message in a single packet");

    return MPI_SUCCESS;
}

int MPID_SHMEM_Eagerb_recv_short( MPIR_RHANDLE *rhandle, int from_grank, 
				  void *in_pkt )
{
    MPID_PKT_SHORT_T *pkt = (MPID_PKT_SHORT_T *)in_pkt;
    int          msglen;
    int          err = MPI_SUCCESS;
    int          to, src, tag, lrank;

    DEBUG_PRINT_MSG("R Starting Eagerb_recv_short");
    /* Extract values from pkt (we need to free it or even better, use
       it to return the protocol ack).  This also *may* improve the
       local code, since pkt is read into local variables and then 
       freed. */
    msglen = pkt->len;
    to     = pkt->to;
    src    = pkt->src;
    tag    = pkt->tag;
    lrank  = pkt->lrank;

    rhandle->s.MPI_TAG	  = tag;
    rhandle->s.MPI_SOURCE = lrank;
    rhandle->s.count      = msglen;
    MPID_CHK_MSGLEN(rhandle,msglen,err);
    if (msglen > 0) {
	MEMCPY( rhandle->buf, pkt->buffer, msglen ); 
    }
    rhandle->s.MPI_ERROR = err;

    /* We must not free the packet until we have copied the data out of 
       it */
#ifdef MPID_PACK_CONTROL
    if (MPID_PACKET_RCVD_GET(src)) {
	MPID_SendProtoAckWithPacket(to, src, (MPID_PKT_T*)pkt);
    }
    else {
	MPID_SHMEM_FreeRecvPkt( (MPID_PKT_T *)pkt );
    }
    MPID_PACKET_ADD_RCVD(to, src);
#else     
    MPID_SHMEM_FreeRecvPkt( (MPID_PKT_T *)pkt );
#endif

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
int MPID_SHMEM_Eagerb_unxrecv_start_short( MPIR_RHANDLE *rhandle, 
					   void *in_runex )
{
    MPIR_RHANDLE *runex = (MPIR_RHANDLE *)in_runex;
    int          msglen, err = 0;

    msglen = runex->s.count;
    DEBUG_PRINT_MSG("R Starting Eagerb_unxrecv_start_short");
#ifdef MPID_PACK_CONTROL
    if (MPID_PACKET_RCVD_GET(runex->from)) {
	MPID_SendProtoAck(runex->partner, runex->from);
    }
    MPID_PACKET_ADD_RCVD(runex->partner, runex->from);
#endif
    MPID_CHK_MSGLEN(rhandle,msglen,err);
    /* Copy the data from the local area and free that area */
    if (runex->s.count > 0) {
	MEMCPY( rhandle->buf, runex->start, msglen );
	FREE( runex->start );
    }
    rhandle->s		 = runex->s;
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
int MPID_SHMEM_Eagerb_save_short( MPIR_RHANDLE *rhandle, int from, 
				  void *in_pkt )
{
    MPID_PKT_SHORT_T   *pkt = (MPID_PKT_SHORT_T *)in_pkt;
    int src, to, len, tag, lrank;
    void *buffer;

    DEBUG_PRINT_MSG("R Starting Eagerb_save_short");
    to  = pkt->to;
    len = pkt->len;
    buffer = pkt->buffer;
    src = pkt->src;
    tag = pkt->tag;
    lrank = pkt->lrank;

    rhandle->s.MPI_TAG	  = tag;
    rhandle->s.MPI_SOURCE = lrank;
    rhandle->s.MPI_ERROR  = 0;
    rhandle->from         = from;
    rhandle->partner      = to;
    rhandle->s.count      = len;

    /* Need to save msgrep for heterogeneous systems */
    if (len > 0) {
	rhandle->start	  = (void *)MALLOC( len );
	if (!rhandle->start) {
	    rhandle->s.MPI_ERROR = MPI_ERR_INTERN;
	    return 1;
	}
	MEMCPY( rhandle->start, buffer, len );
    }

    /* Don't free the packet until we have copied the data out of it */
#ifdef MPID_PACK_CONTROL
    if (MPID_PACKET_RCVD_GET(src)) {
	MPID_SendProtoAckWithPacket(to, src, (MPID_PKT_T*)pkt);
    }
    else {
	MPID_SHMEM_FreeRecvPkt( (MPID_PKT_T *)pkt );
    }
    MPID_PACKET_ADD_RCVD(to, src);
#else     
    MPID_SHMEM_FreeRecvPkt( (MPID_PKT_T *)pkt );
#endif

    rhandle->push = MPID_SHMEM_Eagerb_unxrecv_start_short;
    return 0;
}

void MPID_SHMEM_Eagerb_short_delete( MPID_Protocol *p )
{
    FREE( p );
}

MPID_Protocol *MPID_SHMEM_Short_setup( void )
{
    MPID_Protocol *p;

    p = (MPID_Protocol *) MALLOC( sizeof(MPID_Protocol) );
    if (!p) return 0;
    p->send	   = MPID_SHMEM_Eagerb_send_short;
    p->recv	   = MPID_SHMEM_Eagerb_recv_short;
    p->isend	   = MPID_SHMEM_Eagerb_isend_short;
    p->wait_send   = 0;
    p->push_send   = 0;
    p->cancel_send = 0;
    p->irecv	   = 0;
    p->wait_recv   = 0;
    p->push_recv   = 0;
    p->cancel_recv = 0;
    p->do_ack      = 0;
    p->unex        = MPID_SHMEM_Eagerb_save_short;
    p->delete      = MPID_SHMEM_Eagerb_short_delete;

    return p;
}

