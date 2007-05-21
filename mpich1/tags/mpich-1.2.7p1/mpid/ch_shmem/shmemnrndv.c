#include "mpid.h"
#include "mpiddev.h"
#include "mpimem.h"
#include "reqalloc.h"
#include "sendq.h"	/* For MPIR_FORGET_SEND */
#include "flow.h"
#include "chpackflow.h"

/* Shared memory by rendezvous.  Messages are sent in one of two ways 
   (not counting the short in packet way):

   All of the data is copied into shared memory, the address is sent,
   and the receiver returns the shared memory.

   Only some of the data is copied into shared memory, and the address
   for THAT is sent.  Getting the rest of the message is done by sending
   the original sender a message (or possibly many messages) to 
   provide the rest of the data.  Not yet implemented.

 */
   

/* Prototype definitions */
int MPID_SHMEM_Rndvn_send ( void *, int, int, int, int, int, 
					  MPID_Msgrep_t );
int MPID_SHMEM_Rndvn_isend ( void *, int, int, int, int, int, 
				       MPID_Msgrep_t, MPIR_SHANDLE * );
int MPID_SHMEM_Rndvn_irecv ( MPIR_RHANDLE *, int, void * );
int MPID_SHMEM_Rndvn_save ( MPIR_RHANDLE *, int, void *);

int MPID_SHMEM_Rndvn_unxrecv_start ( MPIR_RHANDLE *, void * );
int MPID_SHMEM_Rndvn_unxrecv_end ( MPIR_RHANDLE * );
int MPID_SHMEM_Rndvn_unxrecv_test_end ( MPIR_RHANDLE * );
int MPID_SHMEM_Rndvn_ok_to_send  ( MPID_Aint, MPID_RNDV_T, int );
int MPID_SHMEM_Rndvn_ack ( void *, int );
int MPID_SHMEM_Rndvn_send_wait ( MPIR_SHANDLE * );
int MPID_SHMEM_Rndvn_send_test_ack ( MPIR_SHANDLE * );
int MPID_SHMEM_Rndvn_send_wait_ack ( MPIR_SHANDLE * );
void MPID_SHMEM_Rndvn_delete ( MPID_Protocol * );

/* Globals for this protocol */

/*
 * Definitions of the actual functions
 */

/*
 * Send a message anouncing the availablility of data.  An "ack" must be
 * sent by the receiver to initiate data transfers (the ack type is
 * MPID_PKT_OK_TO_SEND_GET).
 */
int MPID_SHMEM_Rndvn_isend( buf, len, src_lrank, tag, context_id, dest,
			    msgrep, shandle )
void          *buf;
int           len, tag, context_id, src_lrank, dest;
MPID_Msgrep_t msgrep;
MPIR_SHANDLE *shandle;
{
    MPID_PKT_GET_T *pkt;
    
    DEBUG_PRINT_MSG("S About to get pkt for request to send");
    pkt = (MPID_PKT_GET_T *) MPID_SHMEM_GetSendPkt(0);
    /* GetSendPkt hangs until successful */
    DEBUG_PRINT_MSG("S Starting Rndvb_isend");
#ifdef MPID_PACK_CONTROL
    while (!MPID_PACKET_CHECK_OK(dest)) {  /* begin while !ok loop */
	/* Wait for a protocol ACK packet */
#ifdef MPID_DEBUG_ALL
	if (MPID_DebugFlag || MPID_DebugFlow)
	    FPRINTF(MPID_DEBUG_FILE,
	 "[%d] S Waiting for a protocol ACK packet (in rndvb isend) from %d\n",
		    MPID_myid, dest);
#endif
	MPID_DeviceCheck( MPID_BLOCKING );
    }  /* end while !ok loop */

    MPID_PACKET_ADD_SENT(MPID_myid, dest);
#endif

    pkt->mode	    = MPID_PKT_REQUEST_SEND_GET;
    pkt->context_id = context_id;
    pkt->lrank	    = src_lrank;
    pkt->to         = dest;
    pkt->seqnum     = sizeof(MPID_PKT_GET_T);
    pkt->tag	    = tag;
    pkt->len	    = len;

    /* We save the address of the send handle in the packet; the
       receiver will return this to us */
    MPID_AINT_SET(pkt->send_id,shandle);
	
    /* Store info in the request for completing the message */
    shandle->is_complete     = 0;
    shandle->start	     = buf;
    shandle->bytes_as_contig = len;
#ifdef MPID_DEBUG_ALL
    { char sbuf[150];
    sprintf( sbuf, "S Saving buf = %lx of %d bytes", (long)buf, len );
    DEBUG_PRINT_MSG(sbuf);
    }
#endif
    /* Set the test/wait functions */
    shandle->wait	     = MPID_SHMEM_Rndvn_send_wait_ack;
    shandle->test            = MPID_SHMEM_Rndvn_send_test_ack;
    /* shandle->finish must NOT be set here; it must be cleared/set
       when the request is created */
    /* Store partners rank in request in case message is cancelled */
    shandle->partner         = dest;
    DEBUG_PRINT_BASIC_SEND_PKT("S Sending rndv-get message",pkt)
    MPID_n_pending++;
    MPID_SHMEM_SendControl( (MPID_PKT_T *)pkt, sizeof(MPID_PKT_GET_T), dest );

    return MPI_SUCCESS;
}

/*
 * This is just isend/wait
 */
int MPID_SHMEM_Rndvn_send( buf, len, src_lrank, tag, context_id, dest,
			   msgrep )
void          *buf;
int           len, tag, context_id, src_lrank, dest;
MPID_Msgrep_t msgrep;
{
    MPIR_SHANDLE shandle;

    DEBUG_INIT_STRUCT(&shandle,sizeof(shandle));
    MPIR_SET_COOKIE((&shandle),MPIR_REQUEST_COOKIE);
    MPID_SendInit( &shandle );
    shandle.finish = 0;
    MPID_SHMEM_Rndvn_isend( buf, len, src_lrank, tag, context_id, dest,
			 msgrep, &shandle );
    DEBUG_TEST_FCN(shandle.wait,"req->wait");
    shandle.wait( &shandle );
    return MPI_SUCCESS;
}

/* 
 *  Send ack routines.  When a receive is ready for data, it sends
 *  a message of type MPID_PKT_OK_TO_SEND_GET.  The sending side
 *  responds to this by calling the "do_ack" function; the
 *  shandle is looked up (from pkt->send_id), a shared area is created,
 *  data is placed there, and the packet is returned.  If the data will
 *  not fit in a single packet, then the receiver sends additional
 *  MPID_PKT_CONT_GET packets.  Once all of the data is available in
 *  shared memory, then the send side is complete.   
 *  Note that the it is possible to send zero data; this is how 
 *  Ssend(count=0) is implemented 
 */
/* 
 * This is the routine that is called when an "ok to send" packet is
 * received OR when an "cont get" packet is received.  (one ack entry
 * in the check-device routine)
 */
int MPID_SHMEM_Rndvn_ack( void *in_pkt, int from_grank )
{  /* begin MPID_SHMEM_Rndvn_ack */

    MPID_PKT_GET_T *pkt = (MPID_PKT_GET_T *)in_pkt;
    int            len;
    int            is_done;
    int            temp_to, temp_lrank;

    DEBUG_PRINT_MSG("R Starting Rndvb_ack");
#ifdef MPID_PACK_CONTROL
    if (MPID_PACKET_RCVD_GET(pkt->src)) {
	MPID_SendProtoAck(pkt->to, pkt->src);
    }
    MPID_PACKET_ADD_RCVD(pkt->to, pkt->src);
#endif

    if (pkt->mode == MPID_PKT_OK_TO_SEND_GET) {  
	/* begin if mode == OK_TO_SEND_GET */

	MPIR_SHANDLE *shandle=0;
	MPID_AINT_GET(shandle,pkt->send_id);
#ifdef MPIR_HAS_COOKIES
	if (shandle->cookie != MPIR_REQUEST_COOKIE) {
	    fprintf( stderr, "shandle is %lx\n", (long)shandle );
	    fprintf( stderr, "shandle cookie is %lx\n", shandle->cookie );
	    MPID_Print_shandle( stderr, shandle );
	    MPID_Abort( (MPI_Comm)0, 1, "MPI internal", 
			"Bad address in Rendezvous send" );
	}
#endif	
#ifdef MPID_PACK_CONTROL
    while (!MPID_PACKET_CHECK_OK(pkt->src)) {  /* begin while !ok loop */
	/* Wait for a protocol ACK packet */
#ifdef MPID_DEBUG_ALL
	if (MPID_DebugFlag || MPID_DebugFlow)
	    FPRINTF(MPID_DEBUG_FILE,
	 "[%d] S Waiting for a protocol ACK packet (in rndvb isend) from %d\n",
		    MPID_myid, pkt->src);
#endif
	MPID_DeviceCheck( MPID_BLOCKING );
    }  /* end while !ok loop */

    MPID_PACKET_ADD_SENT(pkt->to, pkt->src);
#endif

	/* Continuing data */
	DEBUG_PRINT_MSG("Sending incremental cont get");
        temp_to     = pkt->to;
	temp_lrank  = pkt->lrank;
	pkt->mode   = MPID_PKT_CONT_GET;
	pkt->to     = temp_lrank;
        pkt->lrank  = temp_to;
	pkt->seqnum = sizeof(MPID_PKT_GET_T) + shandle->bytes_as_contig;

	if (pkt->len_avail == 0) {  /* begin if len_avail */
	    /* Need to get initial memory */
	    /* Handle the special case of 0-length data */
	    /* This results in one more message but simplifies the 
	       rest of the code */
	    pkt->len_avail = shandle->bytes_as_contig;
	    if (pkt->len_avail > 0) {  /* begin if len_avail > 0 */
		pkt->address = MPID_SetupGetAddress( shandle->start, 
					    &pkt->len_avail, from_grank );
	    }  /* end if len_avail > 0 */
	    else 
		pkt->address = 0;
	    pkt->cur_offset = 0;
	}  /* end if len_avail == 0 */

	else {  /* begin else */
	    pkt->cur_offset	+= pkt->len_avail;
	}  /* end else */

	/* Compute length available to send.  If this is it,
	   remember so that we can mark the operation as complete */
	len		 = shandle->bytes_as_contig - pkt->cur_offset;
	if (len > pkt->len_avail) {  /* begin if len > len_avail */
	    len = pkt->len_avail;
	    is_done = 0;
	}  /* end if len > len_avail */

	else {  /* begin else */
	    pkt->len_avail	 = len;
	    is_done = 1;
	}  /* end else */
	    
	if (len > 0) {  /* begin if len > 0 */
	    MEMCPY( pkt->address, ((char *)shandle->start) + pkt->cur_offset,
		    len );
	}  /* end if len > 0 */

	DEBUG_PRINT_BASIC_SEND_PKT("S Sending cont-get message",pkt)
	MPID_SHMEM_SendControl( (MPID_PKT_T *)pkt, 
				sizeof(MPID_PKT_GET_T), from_grank );

	if (is_done) {  /* begin if is_done */
	    MPID_n_pending--;
	    shandle->is_complete = 1;
	    if (shandle->finish)
		(shandle->finish)( shandle );
	    /* ? clear wait/test */
	    /* If the corresponding send request is orphaned, delete it */
	    /* printf( "Completed rendezvous send shandle = %x (ref=%d)\n", 
		    (long)shandle, shandle->ref_count ); */
	    if (shandle->ref_count == 0) {  /* begin if ref_count == 0 */
		/* ? persistent requests? */
	        MPIR_FORGET_SEND( shandle );
		MPID_SendFree( shandle );
	    }  /* begin if ref_count == 0 */
	    
	}  /* end if is_done */
    }	/* end if mode == OK_TO_SEND_GET */

    else if (pkt->mode == MPID_PKT_CONT_GET) {  /* begin if mode == CONT_GET */

	/* Data is available */
	MPIR_RHANDLE *rhandle=0;
	MPID_AINT_GET(rhandle,pkt->recv_id);
#ifdef MPIR_HAS_COOKIES
	if (rhandle->cookie != MPIR_REQUEST_COOKIE) {
	    fprintf( stderr, "rhandle is %lx\n", (long)rhandle );
	    fprintf( stderr, "rhandle cookie is %lx\n", rhandle->cookie );
	    MPID_Print_rhandle( stderr, rhandle );
	    MPID_Abort( (MPI_Comm)0, 1, "MPI internal", 
		    "Bad address in Rendezvous send" );
    }
#endif	
	if (pkt->len_avail > 0) {  /* begin if len_avail > 0 */
	    MEMCPY( ((char *)rhandle->buf) + pkt->cur_offset, pkt->address, 
		    pkt->len_avail );
	}  /* end if len_avail > 0 */

	if (pkt->len_avail + pkt->cur_offset >= rhandle->s.count) {
	    /* begin if len_avail + cur_offset > s.count */

	    /* We have all the data; the transfer is complete and
	       we can release the packet and the memory */
	    rhandle->is_complete = 1;
	    if (rhandle->finish) 
		(rhandle->finish)( rhandle );
	    MPID_FreeGetAddress( pkt->address );
	    MPID_SHMEM_FreeRecvPkt( (MPID_PKT_T*)pkt );
	}  /* end if len_avail + cur_offset > s.count */

	else {  /* begin else */
	    pkt->mode = MPID_PKT_OK_TO_SEND_GET;
	    DEBUG_PRINT_BASIC_SEND_PKT("R Sending ok-to-send message",pkt)
#ifdef MPID_PACK_CONTROL
    while (!MPID_PACKET_CHECK_OK(pkt->src)) {  /* begin while !ok loop */
	/* Wait for a protocol ACK packet */
#ifdef MPID_DEBUG_ALL
	if (MPID_DebugFlag || MPID_DebugFlow)
	    FPRINTF(MPID_DEBUG_FILE,
	 "[%d] S Waiting for a protocol ACK packet (in rndvb isend) from %d\n",
		    MPID_myid, pkt->src);
#endif
	MPID_DeviceCheck( MPID_BLOCKING );
    }  /* end while !ok loop */

    MPID_PACKET_ADD_SENT(pkt->to, pkt->src);
#endif

	MPID_SHMEM_SendControl( (MPID_PKT_T *)pkt, 
				sizeof(MPID_PKT_GET_T), from_grank );

	}  /* end else */

    }  /* begin if mode == CONT_GET */

    return MPI_SUCCESS;

}  /* begin MPID_SHMEM_Rndvn_ack */

/*
 * This is the routine called when a packet of type MPID_PKT_REQUEST_SEND is
 * seen and the receive has been posted.  Note the use of a nonblocking
 * receiver BEFORE sending the ack.
 */
int MPID_SHMEM_Rndvn_irecv( MPIR_RHANDLE *rhandle, int from_grank, 
			    void *in_pkt )
{
    MPID_PKT_GET_T *pkt = (MPID_PKT_GET_T *)in_pkt;
    int    msglen, err = MPI_SUCCESS;

    msglen = pkt->len;
#ifdef MPID_PACK_CONTROL
    if (MPID_PACKET_RCVD_GET(pkt->src)) {
	MPID_SendProtoAck(pkt->to, pkt->src);
    }
    MPID_PACKET_ADD_RCVD(pkt->to, pkt->src);
#endif

    DEBUG_PRINT_MSG("R Starting rndvb irecv");

    /* Check for truncation */
    MPID_CHK_MSGLEN(rhandle,msglen,err)
    /* Note that if we truncate, We really must receive the message in two 
       parts; the part that we can store, and the part that we discard.
       This case is not yet handled. */
    MPIR_SET_COOKIE((rhandle),MPIR_REQUEST_COOKIE);
    rhandle->s.count	  = msglen;
    rhandle->s.MPI_TAG	  = pkt->tag;
    rhandle->s.MPI_SOURCE = pkt->lrank;
    rhandle->s.MPI_ERROR  = err;
    rhandle->from         = from_grank;
    rhandle->send_id	  = pkt->send_id;
    rhandle->wait	  = MPID_SHMEM_Rndvn_unxrecv_end;
    rhandle->test	  = MPID_SHMEM_Rndvn_unxrecv_test_end;
    rhandle->push	  = 0;
    rhandle->is_complete  = 0;

#ifdef MPID_PACK_CONTROL
    while (!MPID_PACKET_CHECK_OK(from_grank)) {  /* begin while !ok loop */
	/* Wait for a protocol ACK packet */
#ifdef MPID_DEBUG_ALL
	if (MPID_DebugFlag || MPID_DebugFlow)
	    FPRINTF(MPID_DEBUG_FILE,
	 "[%d] S Waiting for a protocol ACK packet (in rndvb isend) from %d\n",
		    MPID_myid, from_grank);
#endif
	MPID_DeviceCheck( MPID_BLOCKING );
    }  /* end while !ok loop */

    MPID_PACKET_ADD_SENT(pkt->to, from_grank);
#endif

    /* Send back an "ok to proceed" packet */
    pkt->mode = MPID_PKT_OK_TO_SEND_GET;
    pkt->to         = from_grank;
    pkt->lrank      = MPID_myid;
    pkt->cur_offset = 0;
    pkt->len_avail  = 0;
    pkt->address    = 0;
    MPID_AINT_SET(pkt->recv_id,rhandle);
    
    DEBUG_PRINT_BASIC_SEND_PKT("R Sending ok-to-send message",pkt)
    MPID_SHMEM_SendControl( (MPID_PKT_T *)pkt, 
			    sizeof(MPID_PKT_GET_T), from_grank );

    /* Note that since we are returning the control packet, we don't
       delete it */
    return err;
}

/* Save an unexpected message in rhandle.  This is the same as
   MPID_SHMEM_Rndvb_save except for the "push" function */
int MPID_SHMEM_Rndvn_save( MPIR_RHANDLE *rhandle, int from_grank, 
			   void *in_pkt )
{
    MPID_PKT_GET_T   *pkt = (MPID_PKT_GET_T *)in_pkt;
    int to, src;

    to = pkt->to;
    src = pkt->src;
    DEBUG_PRINT_MSG("Saving info on unexpected message");
    rhandle->s.MPI_TAG	  = pkt->tag;
    rhandle->s.MPI_SOURCE = pkt->lrank;
    rhandle->s.MPI_ERROR  = 0;
    rhandle->s.count      = pkt->len;
    rhandle->is_complete  = 0;
    rhandle->from         = from_grank;
    rhandle->partner      = to;
    rhandle->send_id      = pkt->send_id;
    MPID_SHMEM_FreeRecvPkt( (MPID_PKT_T *)pkt );
#ifdef MPID_PACK_CONTROL
    /* This needs to happen after the FreeRecvPkt incase we need that
       packet to send the ProtoAck back */
    if (MPID_PACKET_RCVD_GET(src)) {
	MPID_SendProtoAck(to, src);
    }
    MPID_PACKET_ADD_RCVD(to, src);
#endif
    /* Need to set the push etc routine to complete this transfer */
    rhandle->push         = MPID_SHMEM_Rndvn_unxrecv_start;
    return 0;
}

#ifdef FOO
/*
 * This is an internal routine to return an OK TO SEND packet.
 * It is the same as the Rndvb version
 */
int MPID_SHMEM_Rndvn_ok_to_send( send_id, rtag, from_grank )
MPID_Aint   send_id;
MPID_RNDV_T rtag;
int         from_grank;
{
    MPID_PKT_GET_T *pkt;

    DEBUG_PRINT_MSG("Starting rndvn ok to send");
    pkt = (MPID_PKT_GET_T *)MPID_SHMEM_GetSendPkt(0);
    /* GetSendPkt hangs untill successful */
    pkt->mode = MPID_PKT_CONT_GET;
    MPID_AINT_SET(pkt->send_id,send_id);
    /* pkt->recv_handle = rtag; */
    DEBUG_PRINT_BASIC_SEND_PKT("S Ok send", pkt);
    MPID_SHMEM_SendControl( pkt, sizeof(MPID_PKT_GET_T), from_grank );
    return MPI_SUCCESS;
}
#endif

/* 
 * This routine is called when it is time to receive an unexpected
 * message.
 */
int MPID_SHMEM_Rndvn_unxrecv_start( rhandle, in_runex )
MPIR_RHANDLE *rhandle;
void         *in_runex;
{
    MPID_PKT_GET_T *pkt;
    MPIR_RHANDLE   *runex = (MPIR_RHANDLE *)in_runex;

    /* Tell the sender to make the data available */
    DEBUG_PRINT_MSG("R about to get packet for ok to send");
    pkt = (MPID_PKT_GET_T *) MPID_SHMEM_GetSendPkt(0);
    /* ? test for length ? */
#ifdef MPID_PACK_CONTROL
    if (MPID_PACKET_RCVD_GET(runex->from)) {
	MPID_SendProtoAck(runex->partner, runex->from);
    }
    MPID_PACKET_ADD_RCVD(runex->partner, runex->from);
#endif

    MPIR_SET_COOKIE((rhandle),MPIR_REQUEST_COOKIE);
    rhandle->s		  = runex->s;
    rhandle->send_id	  = runex->send_id;
    rhandle->wait	  = MPID_SHMEM_Rndvn_unxrecv_end;
    rhandle->test	  = MPID_SHMEM_Rndvn_unxrecv_test_end;
    rhandle->push	  = 0;
    rhandle->is_complete  = 0;

    /* Send back an "ok to proceed" packet */
    pkt->mode	    = MPID_PKT_OK_TO_SEND_GET;
    pkt->lrank      = runex->partner;
    pkt->to         = runex->s.MPI_SOURCE;
    pkt->seqnum     = sizeof(MPID_PKT_GET_T);
    pkt->cur_offset = 0;
    pkt->len_avail  = 0;
    pkt->address    = 0;
    pkt->send_id    = runex->send_id;
    MPID_AINT_SET(pkt->recv_id,rhandle);
    
    DEBUG_PRINT_BASIC_SEND_PKT("R Sending ok-to-send message",pkt)
#ifdef MPID_PACK_CONTROL
    while (!MPID_PACKET_CHECK_OK(runex->from)) {  /* begin while !ok loop */
	/* Wait for a protocol ACK packet */
#ifdef MPID_DEBUG_ALL
	if (MPID_DebugFlag || MPID_DebugFlow)
	    FPRINTF(MPID_DEBUG_FILE,
	 "[%d] S Waiting for a protocol ACK packet (in rndvb isend) from %d\n",
		    MPID_myid, runex->from);
#endif
	MPID_DeviceCheck( MPID_BLOCKING );
    }  /* end while !ok loop */

    MPID_PACKET_ADD_SENT(runex->partner, runex->from);
#endif

    MPID_SHMEM_SendControl( (MPID_PKT_T *)pkt, 
			    sizeof(MPID_PKT_GET_T), runex->from );

    MPID_RecvFree( runex );

    return 0;
}

/* 
   This is the wait routine for a rendezvous message that was unexpected.
   A request for the message has already been sent and the receive 
   transfer has been started.  We wait for the "continue get" packets
   to set the completed bit.
 */
int MPID_SHMEM_Rndvn_unxrecv_end( rhandle )
MPIR_RHANDLE *rhandle;
{
    while (!rhandle->is_complete) {
	MPID_DeviceCheck( MPID_BLOCKING );
    }
    if (rhandle->finish) 
	(rhandle->finish)( rhandle );

    return MPI_SUCCESS;
}

/* 
   This is the test routine for a rendezvous message that was unexpected.
   A request for the message has already been sent, and the receive has been
   started.
 */
int MPID_SHMEM_Rndvn_unxrecv_test_end( rhandle )
MPIR_RHANDLE *rhandle;
{
    if (rhandle->is_complete == 1) {
	if (rhandle->finish) 
	    (rhandle->finish)( rhandle );
    }
    else 
	MPID_DeviceCheck( MPID_NOTBLOCKING );

    return MPI_SUCCESS;
}

#ifdef FOO
int MPID_SHMEM_Rndvn_send_wait( shandle )
MPIR_SHANDLE *shandle;
{
    DEBUG_PRINT_MSG("Ending send transfer");
#ifdef FOO
    MPID_EndNBSendTransfer( shandle, shandle->recv_handle, shandle->sid );
#endif
    shandle->is_complete = 1;
    if (shandle->finish) 
	(shandle->finish)( shandle );
    return 0;
}
#endif

#ifdef FOO
int MPID_SHMEM_Rndvn_send_test( shandle )
MPIR_SHANDLE *shandle;
{
    DEBUG_PRINT_MSG("Testing for end send transfer" );
#ifdef FOO
    if (MPID_TestNBSendTransfer( shandle->sid )) {
	shandle->is_complete = 1;
	if (shandle->finish) 
	    (shandle->finish)( shandle );
    }
#endif
    return 0;
}
#endif

/* These wait for the "ack" and then change the wait routine on the
   handle */
int MPID_SHMEM_Rndvn_send_wait_ack( shandle )
MPIR_SHANDLE *shandle;
{
    DEBUG_PRINT_MSG("Waiting for Rndvn ack");
    while (!shandle->is_complete && 
	   shandle->wait == MPID_SHMEM_Rndvn_send_wait_ack) {
	MPID_DeviceCheck( MPID_BLOCKING );
    }
    if (!shandle->is_complete) {
	DEBUG_TEST_FCN(shandle->wait,"shandle->wait");
	return (shandle->wait)( shandle );
    }
    return 0;
}

int MPID_SHMEM_Rndvn_send_test_ack( shandle )
MPIR_SHANDLE *shandle;
{
    DEBUG_PRINT_MSG("Testing for Rndvn ack" );
    if (!shandle->is_complete &&
	shandle->test == MPID_SHMEM_Rndvn_send_test_ack) {
	MPID_DeviceCheck( MPID_NOTBLOCKING );
    }

    return 0;
}

/* 
 * CancelSend 
 * This is fairly hard.  We need to send a "please_cancel_send", 
 * which, if the message is found in the unexpected queue, removes it.
 * However, if the message is being received at the "same" moment, the
 * ok_to_send and cancel_send messages could cross.  To handle this, the
 * receiver must ack the cancel_send message (making the success of the
 * cancel non-local).  There are even more complex protocols, but we won't
 * bother.
 * 
 * Don't forget to update MPID_n_pending as needed.
 */

void MPID_SHMEM_Rndvn_delete( p )
MPID_Protocol *p;
{
    FREE( p );
}

/*
 * The only routing really visable outside this file; it defines the
 * Blocking Rendezvous protocol.
 */
MPID_Protocol *MPID_SHMEM_Rndvn_setup()
{
    MPID_Protocol *p;

    p = (MPID_Protocol *) MALLOC( sizeof(MPID_Protocol) );
    if (!p) return 0;
    p->send	   = MPID_SHMEM_Rndvn_send;
    p->recv	   = 0;
    p->isend	   = MPID_SHMEM_Rndvn_isend;
    p->wait_send   = 0;
    p->push_send   = 0;
    p->cancel_send = 0;
    p->irecv	   = MPID_SHMEM_Rndvn_irecv;
    p->wait_recv   = 0;
    p->push_recv   = 0;
    p->cancel_recv = 0;
    p->do_ack      = MPID_SHMEM_Rndvn_ack;
    p->unex        = MPID_SHMEM_Rndvn_save;
    p->delete      = MPID_SHMEM_Rndvn_delete;

    return p;
}
