/*
 *  $Id: shmemchkdev.c,v 1.1.1.1 1997/09/17 20:41:31 gropp Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      All rights reserved.  See COPYRIGHT in top-level directory.
 */


#include "mpid.h"
#include "mpiddev.h"
#include "../util/queue.h"

/***************************************************************************/
/* This is one of the main routines.  It checks for incoming messages and  */
/* dispatches them.  There is another such look in MPID_CH_blocking_recv   */
/* which is optimized for the important case of blocking receives for a    */
/* particular message.                                                     */
/*                                                                         */
/* This is a special version for shared memory.  It moves addresses of     */
/* packets, not packets, from one processor to another.                    */
/***************************************************************************/

/* Check for incoming messages.
    Input Parameter:
.   is_blocking - true if this routine should block until a message is
    available

    Returns -1 if nonblocking and no messages pending

    This routine makes use of a single dispatch routine to handle all
    incoming messages.  This makes the code a little lengthy, but each
    piece is relatively simple.

    This is the message-passing version.  The shared-memory version is
    in chchkshdev.c .
 */    
int MPID_CENJU3_Check_incoming( dev, is_blocking )
MPID_Device        *dev;
MPID_BLOCKING_TYPE is_blocking;
{
    MPID_PKT_T   *pkt;
    int          from_grank;
    MPIR_RHANDLE *rhandle;
    int          is_posted;
    int          err = MPI_SUCCESS;

    DEBUG_PRINT_MSG("Entering check_incoming");
    if (MPID_CENJU3_ReadControl( &pkt, sizeof(MPID_PKT_T), 
				&from_grank, is_blocking ) == 1) {
      DEBUG_PRINT_MSG("Leaving check_incoming (no messages)")
      return -1;
    }
    DEBUG_PRINT_PKT("R received message",pkt);
    DEBUG_PRINT_MSG("Message is available!");

    /* Separate the incoming messages from control messages */
    if (MPID_PKT_IS_MSG(pkt->head.mode)) {
	DEBUG_PRINT_RECV_PKT("R rcvd msg",pkt);

	/* Is the message expected or not? 
	   This routine RETURNS a rhandle, creating one if the message 
	   is unexpected (is_posted == 0) */
	MPID_Msg_arrived( pkt->head.lrank, pkt->head.tag, 
			  pkt->head.context_id, 
			  &rhandle, &is_posted );
#ifdef MPID_DEBUG_ALL   /* #DEBUG_START# */
	if (MPID_DebugFlag) {
	    fprintf( MPID_DEBUG_FILE, "[%d]R msg was %s (%s:%d)\n", 
		     MPID_MyWorldRank, 
		     is_posted ? "posted" : "unexpected", __FILE__, __LINE__ );
	}
#endif                  /* #DEBUG_END# */
	if (is_posted) {
	    /* We should check the size here for internal errors .... */
	    switch (pkt->head.mode) {
	    case MPID_PKT_SHORT:
		DEBUG_TEST_FCN(dev->short_msg->recv,"dev->short->recv");
		err = (*dev->short_msg->recv)( rhandle, from_grank, pkt );
		break;

	    case MPID_PKT_SEND_ADDRESS:
		DEBUG_TEST_FCN(dev->eager->recv,"dev->short->recv");
		err = (*dev->eager->recv)( rhandle, from_grank, pkt );
		break;

	    case MPID_PKT_REQUEST_SEND_GET:
		DEBUG_TEST_FCN(dev->rndv->irecv,"dev->rndv->irecv");
		err = (*dev->rndv->irecv)( rhandle, from_grank, pkt );
		break;

	    default:
		fprintf( stderr, 
			 "[%d] Internal error: msg packet discarded (%s:%d)\n",
			 MPID_MyWorldRank, __FILE__, __LINE__ );
	    }
	}
	else {
	    switch (pkt->head.mode) {
	    case MPID_PKT_SHORT:
		DEBUG_TEST_FCN(dev->short_msg->unex,"dev->short->unex");
		err = (*dev->short_msg->unex)( rhandle, from_grank, pkt );
		break;
	    case MPID_PKT_SEND_ADDRESS:
		DEBUG_TEST_FCN(dev->eager->unex,"dev->short->unex");
		err = (*dev->eager->unex)( rhandle, from_grank, pkt );
		break;
	    case MPID_PKT_REQUEST_SEND_GET:
		DEBUG_TEST_FCN(dev->rndv->unex,"dev->rndv->unex");
		err = (*dev->rndv->unex)( rhandle, from_grank, pkt );
		break;

	    default:
		fprintf( stderr, 
			 "[%d] Internal error: msg packet discarded (%s:%d)\n",
			 MPID_MyWorldRank, __FILE__, __LINE__ );
	    }
	}
    }
    else {
	switch (pkt->head.mode) {
	case MPID_PKT_CONT_GET:
	case MPID_PKT_OK_TO_SEND_GET:
	    DEBUG_TEST_FCN(dev->rndv->do_ack,"dev->rndv->do_ack");
	    err = (*dev->rndv->do_ack)( pkt, from_grank );
	    break;

	default:
	    fprintf( stdout, "[%d] Mode %d is unknown (internal error) %s:%d!\n", 
		     MPID_MyWorldRank, pkt->head.mode, __FILE__, __LINE__ );
	}
	/* Really should remember error in case subsequent events are 
	   successful */
    }
    DEBUG_PRINT_MSG("Exiting check_incoming");
    return err;
}
