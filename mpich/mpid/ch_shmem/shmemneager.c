#include "mpid.h"
#include "mpiddev.h"
#include "mpimem.h"
#include "reqalloc.h"
/* flow.h includs the optional flow control for eager delivery */
#include "flow.h"
#include "chpackflow.h"

/*
   Nonblocking, eager shared-memory send/recv.
 */

/* Prototype definitions */
int MPID_SHMEM_Eagern_send ( void *, int, int, int, int, int, 
				       MPID_Msgrep_t );
int MPID_SHMEM_Eagern_isend ( void *, int, int, int, int, int, 
					MPID_Msgrep_t, MPIR_SHANDLE * );
int MPID_SHMEM_Eagern_cancel_send ( MPIR_SHANDLE * );
int MPID_SHMEM_Eagern_wait_send ( MPIR_SHANDLE * );
int MPID_SHMEM_Eagern_test_send ( MPIR_SHANDLE * );
int MPID_SHMEM_Eagern_save ( MPIR_RHANDLE *, int, void * );
int MPID_SHMEM_Eagern_unxrecv_start ( MPIR_RHANDLE *, void * );
void MPID_SHMEM_Eagern_delete ( MPID_Protocol * );
int MPID_SHMEM_Eagern_recv ( MPIR_RHANDLE *, int, void * );
int MPID_SHMEM_Eagern_irecv ( MPIR_RHANDLE *, int, void * );

extern int MPID_SHMEM_Eagerb_send ( void *, int, int, int, int, 
					   int, MPID_Msgrep_t );
extern int MPID_SHMEM_Eagerb_recv ( MPIR_RHANDLE *, int, void * );
extern int MPID_SHMEM_Eagerb_irecv ( MPIR_RHANDLE *, int, void * );
extern int MPID_SHMEM_Eagerb_save ( MPIR_RHANDLE *, int, void * );
extern int MPID_SHMEM_Eagerb_unxrecv_start ( MPIR_RHANDLE *, void * );

/* Used below */
int MPID_SHMEM_Rndvn_isend ( void *, int, int, int, int, int, 
				       MPID_Msgrep_t, MPIR_SHANDLE * );

/*
 * Definitions of the actual functions
 */

int MPID_SHMEM_Eagern_isend( buf, len, src_lrank, tag, context_id, dest,
			 msgrep, shandle )
void          *buf;
int           len, tag, context_id, src_lrank, dest;
MPID_Msgrep_t msgrep;
MPIR_SHANDLE *shandle;
{
    MPID_PKT_SEND_ADDRESS_T   *pkt;
    int                       in_len, pkt_len;
    
    pkt = (MPID_PKT_SEND_ADDRESS_T *)MPID_SHMEM_GetSendPkt(0);
    /* GetSendPkt hangs until successful */

    DEBUG_PRINT_MSG("S Starting Eagern_isend");
#ifdef MPID_FLOW_CONTROL
    while (!MPID_FLOW_MEM_OK(len,dest)) {  /* begin while !ok loop */
	/* Wait for a flow packet */
#ifdef MPID_DEBUG_ALL
	if (MPID_DebugFlag || MPID_DebugFlow) {
	    FPRINTF( MPID_DEBUG_FILE, 
		     "[%d] S Waiting for flow control packet from %d\n",
		     MPID_myid, dest );
	}
#endif
	MPID_DeviceCheck( MPID_BLOCKING );
    }  /* end while !ok loop */

    MPID_FLOW_MEM_SEND(len,dest); 
#endif

#ifdef MPID_PACK_CONTROL
    while (!MPID_PACKET_CHECK_OK(dest)) {  /* begin while !ok loop */
#ifdef MPID_DEBUG_ALL
	if (MPID_DebugFlag || MPID_DebugFlow) {
	    FPRINTF( MPID_DEBUG_FILE, 
	  "[%d] S Waiting for protocol ACK packet (in eagerb_send) from %d\n",
		     MPID_myid, dest );
	}
#endif
	MPID_DeviceCheck( MPID_BLOCKING );
    }  /* end while !ok loop */

    MPID_PACKET_ADD_SENT(MPID_myid, dest);
#endif

    pkt_len         = sizeof(MPID_PKT_SEND_ADDRESS_T) + len;
    pkt->mode	    = MPID_PKT_SEND_ADDRESS;
    pkt->context_id = context_id;
    pkt->lrank	    = src_lrank;
    pkt->to         = dest;
    pkt->seqnum     = pkt_len;
    pkt->tag	    = tag;
    pkt->len	    = len;
#ifdef MPID_FLOW_CONTROL
    MPID_FLOW_MEM_ADD(&pkt,dest);
#endif

    /* We save the address of the send handle in the packet; the receiver
       will return this to us */
    MPID_AINT_SET(pkt->send_id,shandle);
    
    /* Store partners rank in request in case message is cancelled */
    shandle->partner     = dest;
	
    DEBUG_PRINT_SEND_PKT("S Sending extra-long message",pkt);

    /* Place in shared memory */
    /* Better if setup fails if full memory not available */
    in_len = len;
    pkt->address = MPID_SetupGetAddress( buf, &len, dest );
    /* If len changed (out of memory, we have to switch to rendezvous */
    if (len != in_len) { 
	/* printf( "Switching to rendezvous because not enough space available\n"); */
	/* Return the resources that we allocated */
	MPID_FreeGetAddress( pkt->address );
	MPID_SHMEM_FreeRecvPkt( (MPID_PKT_T*)pkt );
	return MPID_SHMEM_Rndvn_isend( buf, in_len, src_lrank, tag, context_id,
				       dest, msgrep, shandle );
    }
    
    MEMCPY( pkt->address, buf, len );

    /* Send as packet only */
    MPID_SHMEM_SendControl( (MPID_PKT_T *)pkt, 
			    sizeof(MPID_PKT_SEND_ADDRESS_T), dest );

    shandle->wait	 = 0;
    shandle->test	 = 0;
    shandle->is_complete = 1;
    if (shandle->finish)
	(shandle->finish)(shandle);

    return MPI_SUCCESS;
}

int MPID_SHMEM_Eagern_send( buf, len, src_lrank, tag, context_id, dest,
			    msgrep )
void          *buf;
int           len, tag, context_id, src_lrank, dest;
MPID_Msgrep_t msgrep;
{
    MPIR_SHANDLE shandle;

    DEBUG_INIT_STRUCT(&shandle,sizeof(shandle));
    MPIR_SET_COOKIE((&shandle),MPIR_REQUEST_COOKIE)
    MPID_SendInit( &shandle );	
    shandle.finish = 0;  /* Just in case (e.g., Eagern_isend -> Rndvn_isend) */
    MPID_SHMEM_Eagern_isend( buf, len, src_lrank, tag, context_id, dest,
			     msgrep, &shandle );
    /* Note that isend is (probably) complete */
    if (!shandle.is_complete) {
	DEBUG_TEST_FCN(shandle.wait,"req->wait");
	shandle.wait( &shandle );
    }
    return MPI_SUCCESS;
}

int MPID_SHMEM_Eagern_cancel_send( shandle )
MPIR_SHANDLE *shandle;
{
    return 0;
}

int MPID_SHMEM_Eagern_test_send( shandle )
MPIR_SHANDLE *shandle;
{
    /* Test for completion */
    return MPI_SUCCESS;
}

int MPID_SHMEM_Eagern_wait_send( shandle )
MPIR_SHANDLE *shandle;
{
    return MPI_SUCCESS;
}

/*
 * This is the routine called when a packet of type MPID_PKT_SEND_ADDRESS is
 * seen.  It receives the data as shown (final interface not set yet)
 */
int MPID_SHMEM_Eagern_recv( rhandle, from, in_pkt )
MPIR_RHANDLE *rhandle;
int          from;
void         *in_pkt;
{
    MPID_PKT_SEND_ADDRESS_T   *pkt = (MPID_PKT_SEND_ADDRESS_T *)in_pkt;
    int    msglen, err = MPI_SUCCESS;

    msglen = pkt->len;

    DEBUG_PRINT_MSG("R Starting Eagern_recv");
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
    MEMCPY( rhandle->buf, pkt->address, msglen );
    MPID_FreeGetAddress( pkt->address );
    if (rhandle->finish) {
	(rhandle->finish)( rhandle );
    }
    MPID_SHMEM_FreeRecvPkt( (MPID_PKT_T *)pkt );
    rhandle->is_complete = 1;
    
    return err;
}

/* This routine is called when a message arrives and was expected */
int MPID_SHMEM_Eagern_irecv( rhandle, from, in_pkt )
MPIR_RHANDLE *rhandle;
int          from;
void         *in_pkt;
{
    MPID_PKT_SEND_ADDRESS_T *pkt = (MPID_PKT_SEND_ADDRESS_T *)in_pkt;
    int    msglen, err = MPI_SUCCESS;

    msglen = pkt->len;

    DEBUG_PRINT_MSG("R Starting Eagern_irecv");
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
    MEMCPY( rhandle->buf, pkt->address, msglen );
    MPID_FreeGetAddress( pkt->address );
    if (rhandle->finish)
	(rhandle->finish)( rhandle );
    rhandle->wait	 = 0;
    rhandle->test	 = 0;
    rhandle->push	 = 0;
    rhandle->is_complete = 1;

    MPID_SHMEM_FreeRecvPkt( (MPID_PKT_T *)pkt );
    
    return err;
}

/* Save an unexpected message in rhandle */
int MPID_SHMEM_Eagern_save( MPIR_RHANDLE *rhandle, int from, void *in_pkt )
{
    MPID_PKT_SEND_ADDRESS_T *pkt = (MPID_PKT_SEND_ADDRESS_T *)in_pkt;
    int tag, lrank, to, len, src;
    void *address;

    DEBUG_PRINT_MSG("R Starting Eagern_save");
    tag	    = pkt->tag;
    lrank   = pkt->lrank;
    to	    = pkt->to;
    len	    = pkt->len;
    src	    = pkt->src;
    address = pkt->address;


#ifdef MPID_PACK_CONTROL
    if (MPID_PACKET_RCVD_GET(src)) {
	MPID_SendProtoAckWithPacket(to, src, (MPID_PKT_T *)pkt);
    }
    else {
	MPID_SHMEM_FreeRecvPkt( (MPID_PKT_T *)pkt );
    }
    MPID_PACKET_ADD_RCVD(to, src);
#else
    MPID_SHMEM_FreeRecvPkt( (MPID_PKT_T *)pkt );
#endif
    rhandle->s.MPI_TAG	  = tag;
    rhandle->s.MPI_SOURCE = lrank;
    rhandle->s.MPI_ERROR  = 0;
    rhandle->partner      = to;
    rhandle->s.count      = len;
    rhandle->from         = from;
    rhandle->is_complete  = 0;
    /* Save the address */
#ifdef LEAVE_IN_SHARED_MEM
    rhandle->start        = address;
#else
    if (len > 0) {
	rhandle->start	  = (void *)MALLOC( len );
	rhandle->is_complete  = 1;
	if (!rhandle->start) {
	    rhandle->s.MPI_ERROR = MPI_ERR_INTERN;
	    /* This is really pretty fatal, because we haven't received
	       the actual message, leaving it in the system */
	    return 1;
	}
#ifdef MPID_FLOW_CONTROL
	MPID_FLOW_MEM_READ(len,from);
#endif
	MEMCPY( rhandle->start, address, len );
	MPID_FreeGetAddress( address );
    }
#endif
    rhandle->push = MPID_SHMEM_Eagern_unxrecv_start;
    return 0;
}
/* 
 * This routine is called when it is time to receive an unexpected
 * message
 */
int MPID_SHMEM_Eagern_unxrecv_start( rhandle, in_runex )
MPIR_RHANDLE *rhandle;
void         *in_runex;
{
    MPIR_RHANDLE *runex = (MPIR_RHANDLE *)in_runex;
    int          msglen, err = 0;

    msglen = runex->s.count;
    MPID_CHK_MSGLEN(rhandle,msglen,err);
    DEBUG_PRINT_MSG("R Starting unxrecv_start");
#ifdef MPID_PACK_CONTROL
    if (MPID_PACKET_RCVD_GET(runex->from)) {
	MPID_SendProtoAck(runex->partner, runex->from);
    }
    MPID_PACKET_ADD_RCVD(runex->partner, runex->from);
#endif
    /* Copy the data from the local area and free that area */
    if (runex->s.count > 0) {
	MEMCPY( rhandle->buf, runex->start, msglen );
#ifdef LEAVE_IN_SHARED_MEM
	MPID_FreeGetAddress( runex->start );
#else
	FREE( runex->start );
#endif
#ifdef MPID_FLOW_CONTROL
    MPID_FLOW_MEM_RECV(msglen,runex->from);
#endif
    }
    rhandle->s		 = runex->s;
    rhandle->wait	 = 0;
    rhandle->test	 = 0;
    rhandle->push	 = 0;
    rhandle->is_complete = 1;
    MPID_RecvFree( runex );
    if (rhandle->finish) 
	(rhandle->finish)( rhandle );

    return err;
}

void MPID_SHMEM_Eagern_delete( p )
MPID_Protocol *p;
{
    FREE( p );
}

MPID_Protocol *MPID_SHMEM_Eagern_setup()
{
    MPID_Protocol *p;

    p = (MPID_Protocol *) MALLOC( sizeof(MPID_Protocol) );
    if (!p) return 0;
    p->send	   = MPID_SHMEM_Eagern_send;
    p->recv	   = MPID_SHMEM_Eagern_recv;
    p->isend	   = MPID_SHMEM_Eagern_isend;
    p->wait_send   = 0;
    p->push_send   = 0;
    p->cancel_send = MPID_SHMEM_Eagern_cancel_send;
    p->irecv	   = MPID_SHMEM_Eagern_irecv;
    p->wait_recv   = 0;
    p->push_recv   = 0;
    p->cancel_recv = 0;
    p->do_ack      = 0;
    p->unex        = MPID_SHMEM_Eagern_save;
    p->delete      = MPID_SHMEM_Eagern_delete;

    return p;
}
