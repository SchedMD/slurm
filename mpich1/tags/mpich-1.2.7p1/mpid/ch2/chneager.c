#include "mpid.h"
#include "mpiddev.h"
#include "mpimem.h"
#include "reqalloc.h"
/* flow.h includes the optional flow control for eager delivery */
#include "flow.h"
#include "chpackflow.h"

/*
   Nonblocking, eager send/recv.
   These are ALWAYS for long messages.  Short messages are always
   handled in blocking eager mode.  

   We COULD write the eager code to use nonblocking receives as well
   as sends, but that is too much like the rendezvous code.  Instead,
   this code RECEIVES using the eager blocking approach but SENDS with
   nonblocking sends.
 */

/* Prototype definitions */
int MPID_CH_Eagern_isend ( void *, int, int, int, int, int, 
				     MPID_Msgrep_t, MPIR_SHANDLE * );
int MPID_CH_Eagern_cancel_send ( MPIR_SHANDLE * );
int MPID_CH_Eagern_wait_send ( MPIR_SHANDLE * );
int MPID_CH_Eagern_test_send ( MPIR_SHANDLE * );
void MPID_CH_Eagern_delete ( MPID_Protocol * );

/* 
 * Blocking operations come from chbeager.c
 */
extern int MPID_CH_Eagerb_send ( void *, int, int, int, int, 
					   int, MPID_Msgrep_t );
extern int MPID_CH_Eagerb_recv ( MPIR_RHANDLE *, int, void * );
extern int MPID_CH_Eagerb_irecv ( MPIR_RHANDLE *, int, void * );
extern int MPID_CH_Eagerb_save ( MPIR_RHANDLE *, int, void * );
extern int MPID_CH_Eagerb_unxrecv_start ( MPIR_RHANDLE *, void * );

/*
 * Definitions of the actual functions
 */

int MPID_CH_Eagern_isend( buf, len, src_lrank, tag, context_id, dest,
			 msgrep, shandle )
void          *buf;
int           len, tag, context_id, src_lrank, dest;
MPID_Msgrep_t msgrep;
MPIR_SHANDLE  *shandle;
{
    int              pkt_len;
    MPID_PKT_LONG_T  pkt;
    
    DEBUG_PRINT_MSG("S Starting Eagern_isend");
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
    pkt.seqnum     = pkt_len + len;
    pkt.src        = MPID_MyWorldRank;
    pkt.tag	   = tag;
    pkt.len	   = len;
    MPID_DO_HETERO(pkt.msgrep = msgrep);
#ifdef MPID_FLOW_CONTROL
    MPID_FLOW_MEM_ADD(&pkt,dest);
#endif

    /* We save the address of the send handle in the packet; the receiver
       will return this to us */
    MPID_AINT_SET(pkt.send_id,shandle);
    
    /* Store partners rank in request in case message is cancelled */
    shandle->partner     = dest;
	
    DEBUG_PRINT_SEND_PKT("S Sending extra-long message",&pkt)

    MPID_PKT_PACK( &pkt, sizeof(pkt_len), dest );

    /* Send as packet only */
    MPID_DRAIN_INCOMING_FOR_TINY(1);
    MPID_SendControl( &pkt, pkt_len, dest );

    /* Send the body of the message */
    MPID_ISendChannel( buf, len, dest, shandle->sid );
    shandle->wait	 = MPID_CH_Eagern_wait_send;
    shandle->test	 = MPID_CH_Eagern_test_send;
/*     shandle->finish	 = 0; */
    shandle->is_complete = 0;

    return MPI_SUCCESS;
}

int MPID_CH_Eagern_cancel_send( shandle )
MPIR_SHANDLE *shandle;
{
    return 0;
}

int MPID_CH_Eagern_test_send( shandle )
MPIR_SHANDLE *shandle;
{
    /* Test for completion */

    if (!shandle->is_complete) {
	if (MPID_TSendChannel( shandle->sid )) {
	    shandle->is_complete = 1;
	    if (shandle->finish) 
		(shandle->finish)( shandle );
	}
    }
    return MPI_SUCCESS;
}

int MPID_CH_Eagern_wait_send( shandle )
MPIR_SHANDLE *shandle;
{
    if (!shandle->is_complete) {
#ifdef MPID_LIMITED_BUFFERS
	/* We do this to keep us from blocking in a wait in the event that
	   we must handle some incoming messages before we can execute the
	   wait. */
	while (!MPID_TestNBSendTransfer(shandle->sid))
	    (void) MPID_DeviceCheck( MPID_NOTBLOCKING );
	/* Once we have it, the message is completed */
#else
	MPID_WSendChannel( shandle->sid );
#endif
	shandle->is_complete = 1;
	if (shandle->finish) 
	    (shandle->finish)( shandle );
	}
    return MPI_SUCCESS;
}
void MPID_CH_Eagern_delete( p )
MPID_Protocol *p;
{
    FREE( p );
}

MPID_Protocol *MPID_CH_Eagern_setup()
{
    MPID_Protocol *p;

    p = (MPID_Protocol *) MALLOC( sizeof(MPID_Protocol) );
    if (!p) return 0;
    p->send	   = MPID_CH_Eagerb_send;
    p->recv	   = MPID_CH_Eagerb_recv;
    p->isend	   = MPID_CH_Eagern_isend;
    p->wait_send   = 0;
    p->push_send   = 0;
    p->cancel_send = MPID_CH_Eagern_cancel_send;
    p->irecv	   = MPID_CH_Eagerb_irecv;
    p->wait_recv   = 0;
    p->push_recv   = 0;
    p->cancel_recv = 0;
    p->do_ack      = 0;
    p->unex        = MPID_CH_Eagerb_save;
    p->delete      = MPID_CH_Eagern_delete;

    return p;
}
