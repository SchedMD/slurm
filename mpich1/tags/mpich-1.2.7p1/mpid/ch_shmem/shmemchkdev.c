/*
 *  $Id: shmemchkdev.c,v 1.7 2002/04/11 20:48:51 gropp Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      All rights reserved.  See COPYRIGHT in top-level directory.
 */


#include "mpid.h"
#include "mpiddev.h"
#include "flow.h"
#include "../util/queue.h"
#include "chpackflow.h"

/***************************************************************************/
/* This is one of the main routines.  It checks for incoming messages and  */
/* dispatches them.  There is another such look in MPID_CH_blocking_recv   */
/* which is optimized for the important case of blocking receives for a    */
/* particular message.                                                     */
/*                                                                         */
/* This is a special version for shared memory.  It moves addresses of     */
/* packets, not packets, from one processor to another.                    */
/***************************************************************************/

#define MAX_CHECKDEVICE_NEST 10

/* Check for incoming messages.
    Input Parameter:
.   is_blocking - true if this routine should block until a message is
    available

    Returns -1 if nonblocking and no messages pending

    This routine makes use of a single dispatch routine to handle all
    incoming messages.  This makes the code a little lengthy, but each
    piece is relatively simple.
 */    
int MPID_SHMEM_Check_incoming( MPID_Device *dev, 
			       MPID_BLOCKING_TYPE is_blocking )
{
    MPID_PKT_T   *pkt;
    int          from_grank;
    MPIR_RHANDLE *rhandle;
    int          is_posted;
    int          err = MPI_SUCCESS;
    static int nest_level = 0;

    DEBUG_PRINT_MSG("Entering check_incoming");

    /* If nonblocking and no headers available, exit */
    if (is_blocking == MPID_NOTBLOCKING) {
	if (!(MPID_local || *MPID_incoming)) {
	    DEBUG_PRINT_MSG("Leaving check_incoming (no messages)")
		return -1;
	}
	DEBUG_PRINT_MSG("Message is available!");
    }
    if (nest_level++ > MAX_CHECKDEVICE_NEST) {
	MPID_Abort( 0, 1, "MPI Internal", "Deep nest in Check_incoming" );
    }
    
    DEBUG_PRINT_MSG("Waiting for message to arrive");
    MPID_SHMEM_ReadControl( &pkt, 0, &from_grank );
    DEBUG_PRINT_PKT("R received message",pkt);

    /* Separate the incoming messages from control messages */
    if (MPID_PKT_IS_MSG(pkt->head.mode)) {
	DEBUG_PRINT_RECV_PKT("R rcvd msg",pkt);

	/* Is the message expected or not? 
	   This routine RETURNS a rhandle, creating one if the message 
	   is unexpected (is_posted == 0) */
	MPID_Msg_arrived( pkt->head.lrank, pkt->head.tag, 
			  pkt->head.context_id, 
			  &rhandle, &is_posted );

	/* Need the send handle address in order to cancel a send */
	if (!is_posted) {  /* begin if !is_posted */
	    if (pkt->head.mode == MPID_PKT_SEND_ADDRESS) 
		rhandle->send_id = pkt->sendadd_pkt.send_id;
	    else if (pkt->head.mode == MPID_PKT_SHORT) 
		rhandle->send_id = pkt->short_pkt.send_id; 
	    else if (pkt->head.mode == MPID_PKT_OK_TO_SEND_GET)
		rhandle->send_id = pkt->get_pkt.send_id; 
	} 

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
		DEBUG_TEST_FCN(dev->eager->recv,"dev->eager->recv");
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
		DEBUG_TEST_FCN(dev->eager->unex,"dev->eager->unex");
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

	case MPID_PKT_ANTI_SEND:
	    MPID_SendCancelOkPacket( (MPID_PKT_T *)pkt, from_grank ); 
	    break;
	    
	case MPID_PKT_ANTI_SEND_OK:
	    MPID_RecvCancelOkPacket( (MPID_PKT_T *)pkt, from_grank ); 
	    break;

#ifdef MPID_FLOW_CONTROL
	case MPID_PKT_FLOW:
	    MPID_RecvFlowPacket( &pkt, from_grank );
	    break;
#endif
#ifdef MPID_PACK_CONTROL
	case MPID_PKT_PROTO_ACK:
	case MPID_PKT_ACK_PROTO:
	    MPID_RecvProtoAck( (MPID_PKT_T *)pkt, from_grank ); 
	    break;
#endif

	default:
	    fprintf( stdout, "[%d] Mode %d is unknown (internal error) %s:%d!\n", 
		     MPID_MyWorldRank, pkt->head.mode, __FILE__, __LINE__ );
	}
	/* Really should remember error in case subsequent events are 
	   successful */
    }
    nest_level--;
    DEBUG_PRINT_MSG("Exiting check_incoming");
    return err;
}
