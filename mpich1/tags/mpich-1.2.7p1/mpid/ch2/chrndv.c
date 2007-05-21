/*
 *
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      All rights reserved.  See COPYRIGHT in top-level directory.
 */


#ifndef lint
static char vcid[] = "$Id: chrndv.c,v 1.3 2001/11/12 23:13:29 ashton Exp $";
#endif /* lint */

#include "mpid.h"

/* 
  This file contains the routines to send and receive messages using
  a rendevous protocol

 */

/* Here are some definitions to simplify debugging */
#include "mpiddebug.h"
/***************************************************************************/

/***************************************************************************/
/* These are used to keep track of the number and kinds of messages that   */
/* are received                                                            */
/***************************************************************************/
#include "mpidstat.h"
/***************************************************************************/

/***************************************************************************/
/* This is used to provide for a globally allocated message pkt in case
   we wish to preallocate or double buffer.  For example, the p4 device
   could use this to preallocate a message buffer; the Paragon could use
   this to use irecv's instead of recvs. 
 */
/***************************************************************************/
MPID_PKT_GALLOC

/***************************************************************************/
/* These routines copy data from an incoming message into the provided     */
/* buffer.                                                                 */
/***************************************************************************/

/* 
    In the Rendevous version of this, we must send a request back to the
    sender for the data. ???
 */
int MPID_CH_Copy_body_long_rndv( dmpi_recv_handle, pkt, from )
MPIR_RHANDLE *dmpi_recv_handle;
MPID_PKT_T   *pkt;
int          from;
{
MPID_RHANDLE *mpid_recv_handle;
int          msglen, err = MPI_SUCCESS;

mpid_recv_handle = &dmpi_recv_handle->dev_rhandle;
msglen           = pkt->head.len;

MPID_CHK_MSGLEN(dmpi_recv_handle,msglen,err)
dmpi_recv_handle->totallen = msglen;
MPID_KEEP_STAT(MPID_n_long++;)
MPID_RecvFromChannel( mpid_recv_handle->start, msglen, from );
DMPI_mark_recv_completed(dmpi_recv_handle);

return err;
}

/* 
   In the case of long synchronous messages, we do not need any special
   code for the synchronization because the rendevous code only delivers the
   message once the receive is posted.  To make this work, we need to
   make sure that the long sync SENDS don't activate the synchronous msg
   code 
 */

/*
   This code is called when a receive finds that the message has already 
   arrived and has been placed in the unexpected queue.  This code
   stores the information about the message (source, tag, length),
   copies the message into the receiver's buffer.

   dmpi_recv_handle is the API's receive handle that is to receive the
   data.

   dmpi_unexpected is the handle of the data found in the unexpected queue.

   In the case that the rendevous protocol is being used for long messages,
   we must begin the process of transfering the message.  Note that
   in this case, the message may not be completely transfered until
   we wait on the completion of the message.

   Note that in the Rendevous case, this routine may not set the
   completed field, since it the data may still be on its way.
   Because the Rendevous code is a rather different way of handling the
   processing of unexpected messages, there are two versions of this routine,
   one for MPID_USE_RNDV, and one without rendevous.  Make sure that you
   change the correct one (and both if there is a common problem!).
 */

/*
 Complete a rendevous receive
 */
int MPID_CH_Complete_recv_rndv( dmpi_recv_handle ) 
MPIR_RHANDLE *dmpi_recv_handle;
{
DEBUG_PRINT_MSG("About to complete recv (possible rndv send)")
if (!MPID_Test_handle(dmpi_recv_handle) && dmpi_recv_handle->dev_rhandle.rid) {
    MPID_CH_Complete_Rndv( &dmpi_recv_handle->dev_rhandle );
    DMPI_mark_recv_completed(dmpi_recv_handle);
    }
DEBUG_PRINT_MSG("Completed recv of rndv send")
return MPI_SUCCESS;
}

static int CurTag    = 1024;
static int TagsInUse = 0;

/* Respond to a request to send a message when the message is found to
   be posted */
int MPID_CH_Ack_Request( dmpi_recv_handle, from, send_id, msglen )
MPIR_RHANDLE *dmpi_recv_handle;
int          from;
MPID_Aint    send_id;
int          msglen;
{
MPID_RNDV_T  recv_handle;
MPID_PKT_SEND_DECL(MPID_PKT_OK_TO_SEND_T,pkt);
MPID_RHANDLE *mpid_recv_handle = &dmpi_recv_handle->dev_rhandle;
int          err;

MPID_CHK_MSGLEN(dmpi_recv_handle,msglen,err)
dmpi_recv_handle->totallen = msglen;

MPID_PKT_SEND_ALLOC(MPID_PKT_OK_TO_SEND,pkt,0);
MPID_PKT_SEND_ALLOC_TEST(pkt,return MPI_ERR_EXHAUSTED)
/* Generate a tag */
MPID_CreateRecvTransfer( mpid_recv_handle->start, msglen, from, &recv_handle );
mpid_recv_handle->recv_handle = recv_handle;
mpid_recv_handle->from	      = from;
/* Post the non-blocking receive */
MPID_StartRecvTransfer( mpid_recv_handle->start, msglen, from, recv_handle,
 		        mpid_recv_handle->rid );

MPID_PKT_SEND_SET(pkt,mode,MPID_PKT_OK_TO_SEND);
MPID_PKT_SEND_SET(pkt,send_id,send_id);
MPID_PKT_SEND_SET(pkt,recv_handle,recv_handle);

MPID_PKT_PACK( MPID_PKT_SEND_ADDR(pkt), sizeof(MPID_PKT_OK_TO_SEND_T), from );

/* Send a message back with the tag in it */
MPID_SendControl( MPID_PKT_SEND_ADDR(pkt), 
		  sizeof(MPID_PKT_OK_TO_SEND_T), from );

MPID_PKT_SEND_FREE(pkt);


return MPI_SUCCESS;
}

MPID_CH_Complete_Rndv( mpid_recv_handle )
MPID_RHANDLE *mpid_recv_handle;
{
DEBUG_PRINT_MSG("Starting completion of rndv by completing recv")
MPID_EndRecvTransfer( mpid_recv_handle->start, 
		      mpid_recv_handle->bytes_as_contig, 
		      mpid_recv_handle->from, mpid_recv_handle->recv_handle,
 		      mpid_recv_handle->rid );
mpid_recv_handle->rid = 0;
DEBUG_PRINT_MSG("Done receive rndv message data")
}

/* This is a test for received.  It must look to see if the transaction 
   has completed. */
int MPID_CH_Test_recv_rndv( dmpi_recv_handle )
MPIR_RHANDLE *dmpi_recv_handle;
{
int rcvready;
MPID_RHANDLE *mpid_recv_handle = &dmpi_recv_handle->dev_rhandle;

if (dmpi_recv_handle->completer == 0) return 1;
if (dmpi_recv_handle->completer == MPID_CMPL_RECV_RNDV) {
    rcvready = MPID_TestRecvTransfer( mpid_recv_handle->rid );
    if (rcvready) {
	MPID_CompleteRecvTransfer( 
		      mpid_recv_handle->start, 
		      mpid_recv_handle->bytes_as_contig, 
		      mpid_recv_handle->from, mpid_recv_handle->recv_handle,
 		      mpid_recv_handle->rid );
	}
    return rcvready;
    }
return 0;
}

/* Fullfill a request for a message */
int MPID_CH_Do_Request( recv_handle, from, send_id )
MPID_RNDV_T  recv_handle;
int          from;
MPID_Aint    send_id;
{
MPID_SHANDLE *mpid_send_handle;
MPIR_SHANDLE *dmpi_send_handle;

/* Find the send operation (check that it hasn't been cancelled!) */
dmpi_send_handle = (MPIR_SHANDLE *)send_id;
/* Should Look at cookie to make sure address is valid ... */
mpid_send_handle = &dmpi_send_handle->dev_shandle;
MPID_StartSendTransfer( dmpi_send_handle->dev_shandle.start,
	   dmpi_send_handle->dev_shandle.bytes_as_contig, from, recv_handle, 
	   mpid_send_handle->sid );
DEBUG_PRINT_MSG("Completed start of transfer")
return MPI_SUCCESS;
}

/* 
    Send-side routines for rendevous send
 */
int MPID_CH_Test_send_rndv( dmpi_send_handle )
MPIR_SHANDLE *dmpi_send_handle;
{
if (!(dmpi_send_handle->completer == 0) && dmpi_send_handle->dev_shandle.sid) {
    if (MPID_TestSendTransfer( dmpi_send_handle->dev_shandle.sid )) {
	/* If it is done, go ahead and mark the operation completed */
	/* Note that this is really not correct; in most systems, the
	   test also does the completion (just like MPI) */
	/*
	MPID_EndSendTransfer( dmpi_send_handle->dev_shandle.start, 
			dmpi_send_handle->dev_shandle.bytes_as_contig, 
			     dmpi_send_handle->dest,
			     dmpi_send_handle->dev_shandle.recv_handle, 
	   dmpi_send_handle->dev_shandle.sid );
	 */
	dmpi_send_handle->dev_shandle.sid = 0;
	DMPI_mark_send_completed(dmpi_send_handle);
	}
    }
return dmpi_send_handle->completer == 0;
}

/* Message-passing or channel version of send long message */

/*
    This routine is responsible for COMPLETING a rendevous send
 */
MPID_CH_Cmpl_send_rndv( dmpi_send_handle )
MPIR_SHANDLE *dmpi_send_handle;
{
MPID_SHANDLE *mpid_send_handle;

DEBUG_PRINT_MSG("S Starting Send_rndv")

mpid_send_handle = &dmpi_send_handle->dev_shandle;
/* If we have rendevous send, then we may need to first wait until the
   message has been requested; then wait on the send to complete... */
 DEBUG_PRINT_MSG("Entering while !MPID_Test_handle");    
while (!MPID_Test_handle(dmpi_send_handle) && mpid_send_handle->sid == 0)
    /* This can be a BLOCKING check because we must wait until
       an "ok to send" message arrives */
    (void) MPID_CH_check_incoming( MPID_BLOCKING );
 DEBUG_PRINT_MSG("Leaving while !MPID_Test_handle");    
#ifndef PI_NO_NSEND
if (mpid_send_handle->sid)  {
    /* Before we do the wait, try to clear all pending messages */
    (void)MPID_CH_check_incoming( MPID_NOTBLOCKING );
    MPID_CH_isend_wait( dmpi_send_handle );
    }
#else
/* This test lets us 'complete' a rendevous send when there is no nonblocking
   send. */
if (mpid_send_handle->sid) {
    MPID_CH_Test_send( dmpi_send_handle );
    }
#endif
 DEBUG_PRINT_MSG("Entering while !MPID_Test_handle")
while (!MPID_Test_handle(dmpi_send_handle)) {
    /* This waits for the completion of a synchronous send, since at
       this point, we've finished waiting for the PInsend to complete,
       or for a incremental get */
    (void)MPID_CH_check_incoming( MPID_BLOCKING );
    }
 DEBUG_PRINT_MSG("Leaving while !MPID_Test_handle")
}

int MPID_CH_Cmpl_recv_rndv( dmpi_recv_handle )
MPIR_RHANDLE *dmpi_recv_handle;
{
DEBUG_PRINT_MSG("Starting cmpl_recv_rndv")
 /*  && !defined(PI_NO_NRECV) */
/* This will not work on stream devices unless we can guarentee that this
   message is the next one in the pipe.  Otherwise, we need a loop that
   does a check_incoming, interleaved with status checks of this
   message */
/* This routine is ONLY called if 
   dmpi_recv_handle->completer == MPID_CMPL_RECV_RNDV */
DEBUG_PRINT_MSG("About to complete rndv recv")
if (!MPID_Test_handle(dmpi_recv_handle) && dmpi_recv_handle->dev_rhandle.rid) {
    MPID_CH_Complete_Rndv( &dmpi_recv_handle->dev_rhandle );
    DMPI_mark_recv_completed(dmpi_recv_handle);
    DEBUG_PRINT_MSG("Completed recv of rndv send")
    return MPI_SUCCESS;
    }
 DEBUG_PRINT_MSG("Entering while !MPID_Test_handle"); 
while (!MPID_Test_handle(dmpi_recv_handle)) {
    (void)MPID_CH_check_incoming( MPID_BLOCKING );
    }
 DEBUG_PRINT_MSG("Leaving while !MPID_Test_handle"); 
DEBUG_PRINT_MSG("Exiting cmpl_recv_rndv")
}

