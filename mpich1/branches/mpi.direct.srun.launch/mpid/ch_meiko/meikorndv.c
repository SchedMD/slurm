






/*
 *  $Id: meikorndv.c,v 1.1.1.1 1997/09/17 20:40:43 gropp Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      All rights reserved.  See COPYRIGHT in top-level directory.
 */


#ifndef lint
static char vcid[] = "$Id: meikorndv.c,v 1.1.1.1 1997/09/17 20:40:43 gropp Exp $";
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

#ifdef MPID_USE_RNDV
/* 
    In the Rendevous version of this, we must send a request back to the
    sender for the data. ???
 */
int MPID_MEIKO_Copy_body_long_rndv( dmpi_recv_handle, pkt, from )
MPIR_RHANDLE *dmpi_recv_handle;
MPID_PKT_T   *pkt;
int          from;
{
MPID_RHANDLE *mpid_recv_handle;
int          msglen, err = MPI_SUCCESS;

mpid_recv_handle = &dmpi_recv_handle->dev_rhandle;
msglen           = pkt->head.len;

MPID_MEIKOK_MSGLEN(dmpi_recv_handle,msglen,err)
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
   copies the message into the receiver's buffer, and generates a
   acknowledgement if the message has mode SYNC.

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
int MPID_MEIKO_Process_unexpected_rndv( dmpi_recv_handle, dmpi_unexpected )
MPIR_RHANDLE *dmpi_recv_handle, *dmpi_unexpected;
{
MPID_RHANDLE *mpid_recv_handle;
MPID_RHANDLE *mpid_recv_handle_unex;
int err = MPI_SUCCESS;

MPID_KEEP_STAT(MPID_n_unexpected++;)

DEBUG_PRINT_MSG("R Found message in unexpected queue");

/* Copy relevant data to recv_handle */
mpid_recv_handle	   = &dmpi_recv_handle->dev_rhandle;
mpid_recv_handle_unex	   = &dmpi_unexpected->dev_rhandle;
dmpi_recv_handle->source   = dmpi_unexpected->source;
dmpi_recv_handle->tag	   = dmpi_unexpected->tag;
dmpi_recv_handle->totallen = mpid_recv_handle_unex->bytes_as_contig;
#ifdef MPID_DEBUG_ALL   /* #DEBUG_START# */
if (MPID_DebugFlag) {
    fprintf( MPID_DEBUG_FILE,
	    "[%d]R Found unexpected message of %d bytes (%s:%d)...\n", 
	    MPID_MyWorldRank, mpid_recv_handle_unex->bytes_as_contig,
	    __FILE__, __LINE__ );
    fflush( MPID_DEBUG_FILE );
    }
#endif                  /* #DEBUG_END# */
/* Error test on length of message */
if (mpid_recv_handle->bytes_as_contig < dmpi_recv_handle->totallen) {
    mpid_recv_handle_unex->bytes_as_contig = mpid_recv_handle->bytes_as_contig;
    dmpi_recv_handle->totallen		   = mpid_recv_handle->bytes_as_contig;
    err					   = MPI_ERR_TRUNCATE;
    dmpi_recv_handle->errval               = MPI_ERR_TRUNCATE;
    /* Not a panic message */
    fprintf( stderr, "Truncated message"  );
    }

    /* We need to see if the message has already been delivered or not.
       If it was short, it should already be here; otherwise, we need to 
       send a request for it.  Note that we give mpid_recv_handle, not
       mpid_recv_handle_unex here, since we will be testing mpid_recv_handle,
       not mpid_recv_handle_unex for completion.
     */
if (mpid_recv_handle_unex->send_id) {
    MPID_MEIKO_Ack_Request( dmpi_recv_handle, mpid_recv_handle_unex->from,
			 mpid_recv_handle_unex->send_id, 
			 dmpi_unexpected->totallen );
    /* Now, wait for the message to arrive, processing other messages when
       possible */
    dmpi_recv_handle->completer = MPID_CMPL_RECV_RNDV;
    MPID_MEIKO_complete_recv( dmpi_recv_handle );
    }
else {
    if (mpid_recv_handle_unex->bytes_as_contig > 0) {
    /* Short message, all data is here */
	MEMCPY( mpid_recv_handle->start, mpid_recv_handle_unex->temp,
	        mpid_recv_handle_unex->bytes_as_contig );
	}
    DMPI_mark_recv_completed( dmpi_recv_handle );
    }

#ifdef MPID_DEBUG_ALL   /* #DEBUG_START# */
if (MPID_DebugFlag) {
    fprintf( MPID_DEBUG_FILE,
  "[%d]R Copied message out of temp area; send mode is %x (%s:%d)..\n", 
	    MPID_MyWorldRank, mpid_recv_handle_unex->mode, 
	    __FILE__, __LINE__ );
    fflush( MPID_DEBUG_FILE );
    }
#endif                  /* #DEBUG_END# */

if (mpid_recv_handle_unex->temp) {
    free(mpid_recv_handle_unex->temp );
    mpid_recv_handle_unex->temp = 0;      /* In case of a cancel */
    }

/* Return the synchronization message */
if (MPIR_MODE_IS_SYNC(mpid_recv_handle_unex)) {
#ifdef MPID_DEBUG_ALL   /* #DEBUG_START# */
    if (MPID_DebugFlag) {
	fprintf( MPID_DEBUG_FILE,
       "[%d]SYNC Returning sync for %x to %d for rcv of unxpcted (%s:%d)\n", 
	       MPID_MyWorldRank,
	        mpid_recv_handle_unex->mode, mpid_recv_handle_unex->from,
	        __FILE__, __LINE__ );
	fflush( MPID_DEBUG_FILE );
	}
#endif                  /* #DEBUG_END# */
    MPID_KEEP_STAT(MPID_n_syncack++;)
    MPID_SyncReturnAck( mpid_recv_handle_unex->send_id, 
		        mpid_recv_handle_unex->from );
    }

if (!mpid_recv_handle_unex->send_id)
    DMPI_mark_recv_completed(dmpi_recv_handle);

/* Recover dmpi_unexpected.  This is ok even for the rendevous protocol 
   since all of the information needed has been transfered into 
   dmpi_recv_handle. 
 */
DMPI_free_unexpected( dmpi_unexpected );

DEBUG_PRINT_MSG("R Leaving 'process unexpected'");
return err;
}

/*
   Copy the body of a message into the destination buffer for an
   unexpected message.  The information on the message is stored in the
   dmpi_recv_handle, which has allocated by the DMPI_msg_arrived routine.

   Again, just as for Copy_body, in the rendevous case, this may not 
   complete the transfer, just begin it.

   Unresolved to date is whether the "get" version should be aggressive or
   not.  We may want to use both algorithms: in the blocking case, 
   do NOT be aggressive (since the sender will be waiting); in the 
   non-blocking case, DO be aggressive, since the the sender may be busy
   doing other things (also note that in this case, if the single copy 
   get can be used, the data transfer exploits the case that the user's
   buffer can hold the data and wait for it to be read.
 */
int MPID_MEIKO_Save_unex_rndv( dmpi_recv_handle, pkt, from )
MPIR_RHANDLE *dmpi_recv_handle;
MPID_PKT_T   *pkt;
int          from;
{
dmpi_recv_handle->dev_rhandle.send_id = pkt->request_pkt.send_id;
dmpi_recv_handle->totallen		  = pkt->request_pkt.len;
}

/*
 Complete a rendevous receive
 */
int MPID_MEIKO_Complete_recv_rndv( dmpi_recv_handle ) 
MPIR_RHANDLE *dmpi_recv_handle;
{
DEBUG_PRINT_MSG("About to complete recv (possible rndv send)")
if (!MPID_Test_handle(dmpi_recv_handle) && dmpi_recv_handle->dev_rhandle.rid) {
    MPID_MEIKO_Complete_Rndv( &dmpi_recv_handle->dev_rhandle );
    DMPI_mark_recv_completed(dmpi_recv_handle);
    }
DEBUG_PRINT_MSG("Completed recv of rndv send")
return MPI_SUCCESS;
}

static int CurTag    = 1024;
static int TagsInUse = 0;

/* Respond to a request to send a message when the message is found to
   be posted */
int MPID_MEIKO_Ack_Request( dmpi_recv_handle, from, send_id, msglen )
MPIR_RHANDLE *dmpi_recv_handle;
int          from;
MPID_Aint    send_id;
int          msglen;
{
MPID_RNDV_T  recv_handle;
MPID_PKT_SEND_DECL(MPID_PKT_OK_TO_SEND_T,pkt);
MPID_RHANDLE *mpid_recv_handle = &dmpi_recv_handle->dev_rhandle;
int          err;

MPID_MEIKOK_MSGLEN(dmpi_recv_handle,msglen,err)
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

MPID_MEIKO_Complete_Rndv( mpid_recv_handle )
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
int MPID_MEIKO_Test_recv_rndv( dmpi_recv_handle )
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
int MPID_MEIKO_Do_Request( recv_handle, from, send_id )
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
int MPID_MEIKO_Test_send_rndv( dmpi_send_handle )
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
int MPID_MEIKO_post_send_long_rndv( dmpi_send_handle, mpid_send_handle, len ) 
MPIR_SHANDLE *dmpi_send_handle;
MPID_SHANDLE *mpid_send_handle;
int len;
{
char             *address;
int              pkt_len;
MPID_PKT_SEND_DECL(MPID_PKT_REQUEST_SEND_T,pkt);
int              dest;

MPID_PKT_SEND_ALLOC(MPID_PKT_LONG_T,pkt,0);
MPID_PKT_SEND_ALLOC_TEST(pkt,return MPI_ERR_EXHAUSTED)
MPID_PKT_SEND_SET(pkt,mode,MPID_PKT_REQUEST_SEND);
MPID_PKT_SEND_SET(pkt,send_id,(MPID_Aint) dmpi_send_handle);
pkt_len = sizeof(MPID_PKT_REQUEST_SEND_T);
mpid_send_handle->sid = 0;
MPID_PKT_SEND_SET(pkt,context_id,dmpi_send_handle->contextid);
MPID_PKT_SEND_SET(pkt,lrank,dmpi_send_handle->lrank);
MPID_PKT_SEND_SET(pkt,tag,dmpi_send_handle->tag);
MPID_PKT_SEND_SET(pkt,len,len);
MPID_PKT_SEND_SET_HETERO(pkt,dmpi_send_handle->msgrep)
dest           = dmpi_send_handle->dest;

DEBUG_PRINT_SEND_PKT("S Starting a send",pkt)

MPID_PKT_PACK( MPID_PKT_SEND_ADDR(pkt), sizeof(MPID_PKT_HEAD_T), dest );

#ifdef MPID_DEBUG_ALL   /* #DEBUG_START# */
if (MPID_DebugFlag) {
    fprintf( MPID_DEBUG_FILE, 
	   "[%d]S Getting data from mpid->start, first int is %d (%s:%d)\n",
	   MPID_MyWorldRank, *(int *)mpid_send_handle->start, 
	   __FILE__, __LINE__ );
    fprintf( MPID_DEBUG_FILE, "[%d]S Sending extra-long message (%s:%d)...\n", 
	    MPID_MyWorldRank, __FILE__, __LINE__ );
    MPID_Print_packet( MPID_DEBUG_FILE, (MPID_PKT_T*)MPID_PKT_SEND_ADDR(pkt) );
    fflush( MPID_DEBUG_FILE );
    }
#endif                  /* #DEBUG_END# */

/* Send as packet only */
MPID_DRAIN_INCOMING_FOR_TINY(mpid_send_handle->is_non_blocking)
MPID_SENDCONTROL( mpid_send_handle, MPID_PKT_SEND_ADDR(pkt), pkt_len, dest );

MPID_PKT_SEND_FREE(pkt);
dmpi_send_handle->completer = MPID_CMPL_SEND_RNDV;
return MPI_SUCCESS;
}

/*
    This routine is responsible for COMPLETING a rendevous send
 */
MPID_MEIKO_Cmpl_send_rndv( dmpi_send_handle )
MPIR_SHANDLE *dmpi_send_handle;
{
MPID_SHANDLE *mpid_send_handle;

DEBUG_PRINT_MSG("S Starting Send_rndv")

mpid_send_handle = &dmpi_send_handle->dev_shandle;
/* If we have rendevous send, then we may need to first wait until the
   message has been requested; then wait on the send to complete... */
while (!MPID_Test_handle(dmpi_send_handle) && mpid_send_handle->sid == 0)
    /* This can be a BLOCKING check because we must wait until
       an "ok to send" message arrives */
    (void) MPID_MEIKO_check_incoming( MPID_BLOCKING );

#ifndef PI_NO_NSEND
if (mpid_send_handle->sid)  {
    /* Before we do the wait, try to clear all pending messages */
    (void)MPID_MEIKO_check_incoming( MPID_NOTBLOCKING );
    MPID_MEIKO_isend_wait( dmpi_send_handle );
    }
#else
/* This test lets us 'complete' a rendevous send when there is no nonblocking
   send. */
if (mpid_send_handle->sid) {
    MPID_MEIKO_Test_send( dmpi_send_handle );
    }
#endif
DEBUG_PRINT_MSG("S Entering complete send while loop")
while (!MPID_Test_handle(dmpi_send_handle)) {
    /* This waits for the completion of a synchronous send, since at
       this point, we've finished waiting for the =_isend(,,,,0) to complete,
       or for a incremental get */
    (void)MPID_MEIKO_check_incoming( MPID_BLOCKING );
    }
DEBUG_PRINT_MSG("S Ending send_rndv")
}

int MPID_MEIKO_Cmpl_recv_rndv( dmpi_recv_handle )
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
    MPID_MEIKO_Complete_Rndv( &dmpi_recv_handle->dev_rhandle );
    DMPI_mark_recv_completed(dmpi_recv_handle);
    DEBUG_PRINT_MSG("Completed recv of rndv send")
    return MPI_SUCCESS;
    }
while (!MPID_Test_handle(dmpi_recv_handle)) {
    (void)MPID_MEIKO_check_incoming( MPID_BLOCKING );
    }
DEBUG_PRINT_MSG("Exiting cmpl_recv_rndv")
}

#endif


