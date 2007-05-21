/*
 *  $Id: adi2mpass.c,v 1.1.1.1 1997/09/17 20:39:24 gropp Exp $
 *
 *  (C) 1995 by Argonne National Laboratory and Mississipi State University.
 *      All rights reserved.  See COPYRIGHT in top-level directory.
 */


#ifndef lint
static char vcid[] = "$Id: adi2mpass.c,v 1.1.1.1 1997/09/17 20:39:24 gropp Exp $";
#endif /* lint */

#include "mpid.h"

/***************************************************************************/
/* This is one of the main routines.  It checks for incoming messages and  */
/* dispatches them.  There is another such look in MPID_CH_blocking_recv   */
/* which is optimized for the important case of blocking receives for a    */
/* particular message.  This is for message-passing-based systems, and     */
/* handles short, eager, and rendezvou messages.  A separate routine       */
/* handles shared memory transfers.  It is based on the "channel" interface*/
/***************************************************************************/

/* Check for incoming messages.
    Input Parameter:
.   is_blocking - true if this routine should block until a message is
    available

    Returns -1 if nonblocking and no messages pending

    This routine makes use of a single dispatch routine to handle all
    incoming messages.  This makes the code a little lengthy, but each
    piece is relatively simple.

    How does the routine decide which channel routines to call?  By 
    compile-time info?
 */    
int MPID_CH_DeviceCheckMsgPass( is_blocking )
MPID_BLOCKING_TYPE is_blocking;
{
    MPID_PKT_T   pkt;
    int          from_grank;
    MPIR_RHANDLE *rhandle;
    int          is_posted, msglen;
    int          err = MPI_SUCCESS;

    DEBUG_PRINT_MSG("Entering DeviceCheck")

    /* If nonblocking and no headers available, exit */
    if (is_blocking == MPID_NOTBLOCKING) {
	if (!MPID_PKT_CHECK()) {
	    DEBUG_PRINT_MSG("Leaving DeviceCheck (no messages)")
	    return -1;
	}
	DEBUG_PRINT_MSG("Message is available!")
    }
    DEBUG_PRINT_MSG("Waiting for message to arrive")
    MPID_PKT_WAIT();
    /* *** FIX ME **** */
    /* ??? Where does the rest of the packet get unpacked? Should the
       PKT_UNPACK use the size of the packet header rather than a "size" 
       field? */
    MPID_PKT_UNPACK( &pkt, sizeof(MPID_PKT_HEAD_T), from_grank );

    DEBUG_PRINT_PKT("R received message",pkt)

    /* Separate the incoming messages from control messages */
    if (MPID_PKT_IS_MSG(pkt.head.mode)) {
	DEBUG_PRINT_RECV_PKT("R rcvd msg",pkt)

    /* Is the message expected or not? 
       This routine RETURNS a rhandle, creating one if the message 
       is unexpected (is_posted == 0) */
    MPID_msg_arrived( pkt.head.lrank, pkt.head.tag, pkt.head.context_id, 
		      &rhandle, &is_posted );
    DEBUG_PRINT_MSG(is_posted?"R msg was posted":"R msg was unexpected")
    if (is_posted) {
	/* We should check the size here for internal errors .... */
	switch (pkt.head.mode) {
	    case MPID_PKT_SHORT:
	    msglen = ptk.head.len;
	    MPID_CHK_MSGLEN(MPI_COMM_WORLD,rhandle,msglen,&err)
	    rhandle->s.count = msglen;
	    if (msglen > 0) 
		MEMCPY( rhandle->buf, pkt.short_pkt.buffer, msglen ); 
	    rhandle->completer = 0;
	    break;

	case MPID_PKT_?: /* Eager */
	    break;
        case MPID_PKT_REQUEST_SEND:
	    /* Sets completer */
	    MPID_CH_DoRndvSend( rhandle->buf, rhandle->len, rhandle,
			        from_grank, 
			        pkt.head.len,
			        pkt.request_pkt.send_id);
	    break;

	default:
	    MPID_INVALID_PKT(pkt.head.mode);
	    break;
	}}
    else {
	/* There should be protocol dependent versions of this */
	MPID_CH_SaveUnexpected( rhandle, &pkt, from_grank );
	}
    }
else {
    /* Not an envelope; some other action to perform */
    switch (pkt.head.mode) {

	case MPID_PKT_OK_TO_SEND:
	MPID_CH_DoRndvAck( pkt.sendok_pkt.send_id, 
			   pkt.sendok_pkt.recv_handle, from_grank );
	break;

	default:
        MPID_INVALID_PKT(pkt.head.mode); 
        break;
	}
    /* Really should remember error incase subsequent events are successful */
    }
    DEBUG_PRINT_MSG("Exiting DeviceCheck")
    return err;
}

