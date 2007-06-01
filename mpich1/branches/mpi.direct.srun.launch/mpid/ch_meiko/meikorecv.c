






/*
 *  $Id: meikorecv.c,v 1.1.1.1 1997/09/17 20:40:44 gropp Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      All rights reserved.  See COPYRIGHT in top-level directory.
 */


#ifndef lint
static char vcid[] = "$Id: meikorecv.c,v 1.1.1.1 1997/09/17 20:40:44 gropp Exp $";
#endif /* lint */

#include "mpid.h"

/* 
  This file contains the routines to handle receiving a message

  Because we don't know the length of messages (at least the long ones with
  tag MPID_PT2PT2_TAG(src) tags), we never post a receive.  Rather, we have
  a MPID_MEIKO_check_incoming routine that looks for headers.  Note that
  messages sent from a source to destination with the MPID_PT2PT2_TAG(src)
  are ordered (we assume that the message-passing system preserves order).

  Possible Improvements:
  This current system does not "prepost" Irecv's, and so, on systems with
  aggressive delivery (like the Paragon), can suffer performance penalties.
  Obvious places for improvements are
     On blocking receives for long messages, post the irecv FIRST.
     If the message is actually short, cancel the receive.
         (potential problem - if the next message from the same source is
         long, it might match (incorrectly) with the posted irecv.  
         Possible fix: use sequence number for each processor pair, with
         the sequence numbers incremented for each message, short or long, 
         and place the sequence number into the tag field)

     For tags/sources/contexts in a specified range, post the irecv with
     at tag synthesized from all of the tag/source/context(/sequence number)
     May need to use cancel if the message is shorter than expected.
     This can be done with both blocking and non-blocking messages.

     Another approach is to generate "go-ahead" messages, to be handled in 
     chsend.c, perhaps by an interrupt-driven receive.  


     This file is changing to a system where each message kind is handled
     by its own routine (after the receive-queue matching).  This makes the
     number of lines of code larger, and generates some duplication of code,
     but also makes is significantly easier to tune each type (short, 
     synchronous, etc) to a particular protocol.
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

/* This routine is called by the initialization code to preform any 
   receiver initializations, such as preallocating or pre-posting a 
   control-message buffer
 */
void MPID_MEIKO_Init_recv_code()
{
MPID_PKT_INIT();
}

/***************************************************************************/
/* These routines copy data from an incoming message into the provided     */
/* buffer.  We use static (local) routines to allow the compiler to inline */
/* them.                                                                   */
/***************************************************************************/
static int MPID_MEIKO_Copy_body_short( dmpi_recv_handle, pkt, pktbuf )
MPIR_RHANDLE *dmpi_recv_handle;
MPID_PKT_T   *pkt;
void         *pktbuf;
{
int          msglen;
int          err = MPI_SUCCESS;

MPID_KEEP_STAT(MPID_n_short++;)

msglen = pkt->head.len;
MPID_MEIKOK_MSGLEN(dmpi_recv_handle,msglen,err)
dmpi_recv_handle->totallen = msglen;
if (msglen > 0) 
    MEMCPY( dmpi_recv_handle->dev_rhandle.start, pktbuf, msglen ); 
DMPI_mark_recv_completed(dmpi_recv_handle);

return err;
}

static int MPID_MEIKO_Copy_body_sync_short( dmpi_recv_handle, pkt, from )
MPIR_RHANDLE *dmpi_recv_handle;
MPID_PKT_T   *pkt;
int          from;
{
int err;

err = MPID_MEIKO_Copy_body_short( dmpi_recv_handle, pkt, 
			       pkt->short_sync_pkt.buffer );

DEBUG_PRINT_SYNCACK(0,pkt)
MPID_KEEP_STAT(MPID_n_syncack++;)
MPID_SyncReturnAck( pkt->short_sync_pkt.sync_id, from );

return err;
}

/* Now the long messages.  Only if not using the Rendevous protocol (
   actually, this is eager only) */
#ifndef MPID_USE_RNDV                            /*#NONGET_START#*/
/* 
    In the Rendevous version of this, it sends a request back to the
    sender for the data...
 */
static int MPID_MEIKO_Copy_body_long( dmpi_recv_handle, pkt, from )
MPIR_RHANDLE *dmpi_recv_handle;
MPID_PKT_T   *pkt;
int          from;
{
MPID_RHANDLE *mpid_recv_handle;
int          msglen, err = MPI_SUCCESS;

mpid_recv_handle = &dmpi_recv_handle->dev_rhandle;
msglen           = pkt->head.len;

/* Check for truncation */
MPID_MEIKOK_MSGLEN(dmpi_recv_handle,msglen,err)
/* Note that if we truncate, We really must receive the message in two parts; 
   the part that we can store, and the part that we discard.
   This case is not yet handled. */
dmpi_recv_handle->totallen = msglen;
MPID_KEEP_STAT(MPID_n_long++;)
MPID_RecvFromChannel( mpid_recv_handle->start, msglen, from );
DMPI_mark_recv_completed(dmpi_recv_handle);

return err;
}

/* For the eventual case of non-blocking recv */
int MPID_MEIKO_Cmpl_recv_nb( dmpi_recv_handle )
MPIR_RHANDLE *dmpi_recv_handle;
{
return MPI_SUCCESS;
}  

int MPID_MEIKO_Cmpl_recv_sync( dmpi_recv_handle )
MPIR_RHANDLE *dmpi_recv_handle;
{
/* ??? */
DEBUG_PRINT_MSG("Entering Cmpl_recv_sync")
while (!MPID_Test_handle(dmpi_recv_handle)) {
    (void)MPID_MEIKO_check_incoming( MPID_BLOCKING );
    }
DEBUG_PRINT_MSG("Exiting Cmpl_recv_sync")
return MPI_SUCCESS;
}

static int MPID_MEIKO_Copy_body_sync_long( dmpi_recv_handle, pkt, from )
MPIR_RHANDLE *dmpi_recv_handle;
MPID_PKT_T   *pkt;
int          from;
{
int err;

err = MPID_MEIKO_Copy_body_long( dmpi_recv_handle, pkt, from );

DEBUG_PRINT_SYNCACK(0,pkt)
MPID_KEEP_STAT(MPID_n_syncack++;)
MPID_SyncReturnAck( pkt->long_sync_pkt.sync_id, from );

return err;
}
#endif                                        /*#NONGET_END#*/

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
#if !defined(MPID_USE_RNDV) && !defined(MPID_USE_GET)   /*#NONGET_START#*/
/* This is the unexpected receive code for NON RENDEVOUS.
   The message has been received but is still in the unexpected message heap 
 */
int MPID_MEIKO_Process_unexpected( dmpi_recv_handle, dmpi_unexpected )
MPIR_RHANDLE *dmpi_recv_handle, *dmpi_unexpected;
{
MPID_RHANDLE *mpid_recv_handle;
MPID_RHANDLE *mpid_recv_handle_unex;
int err = MPI_SUCCESS;

MPID_KEEP_STAT(MPID_n_unexpected++;)

DEBUG_PRINT_MSG("R Found message in unexpected queue")

/* It is possible that the message has not yet arrived.  We may even want
   to go get it.  Test for that case */
MPID_MEIKO_complete_recv( dmpi_unexpected );

/* Copy relevant data to recv_handle */
mpid_recv_handle	   = &dmpi_recv_handle->dev_rhandle;
mpid_recv_handle_unex	   = &dmpi_unexpected->dev_rhandle;
dmpi_recv_handle->source   = dmpi_unexpected->source;
dmpi_recv_handle->tag	   = dmpi_unexpected->tag;
dmpi_recv_handle->totallen = mpid_recv_handle_unex->bytes_as_contig;
#ifdef MPID_DEBUG_ALL   /* #DEBUG_START# */
if (MPID_DebugFlag) {
    fprintf( MPID_DEBUG_FILE,
	    "[%d]R Found message in temp area of %d bytes (%s:%d)...\n", 
	    MPID_MyWorldRank, mpid_recv_handle_unex->bytes_as_contig,
	    __FILE__, __LINE__ );
    fflush( MPID_DEBUG_FILE );
    }
#endif                  /* #DEBUG_END# */
/* Error test on length of message */
if (mpid_recv_handle->bytes_as_contig < dmpi_recv_handle->totallen) {
    mpid_recv_handle_unex->bytes_as_contig = mpid_recv_handle->bytes_as_contig;
    dmpi_recv_handle->totallen = mpid_recv_handle->bytes_as_contig;
    err = MPI_ERR_TRUNCATE;
    dmpi_recv_handle->errval = MPI_ERR_TRUNCATE;
    /* This is a non-fatal error */
    fprintf( stderr, "Truncated message (in processing unexpected)\n"  );
    }

/* 
   At this point, this routine should use the general "completion" logic
   to obtain the rest of the message, with the "eager" completion just doing
   a memcpy .
 */
if (mpid_recv_handle_unex->bytes_as_contig > 0) {
    MEMCPY( mpid_recv_handle->start, mpid_recv_handle_unex->temp,
	   mpid_recv_handle_unex->bytes_as_contig );
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

DMPI_mark_recv_completed(dmpi_recv_handle);

/* Recover dmpi_unexpected.  This is ok even for the rendevous protocol 
   since all of the information needed has been transfered into 
   dmpi_recv_handle. 
 */
DMPI_free_unexpected( dmpi_unexpected );

DEBUG_PRINT_MSG("R Leaving 'process unexpected'")

return err;
}
#endif                                         /*#NONGET_END#*/

/*
   Post a receive.

   Since the Chameleon implementation lets the underlying message transport
   layer handle this, there isn't much to do.  Note that this is for 
   NONBLOCKING receives; there is a separate call for blocking receives.

   Otherwise, we simply try to handle any receives that are ready for
   processing.

 */
int MPID_MEIKO_post_recv( dmpi_recv_handle ) 
MPIR_RHANDLE *dmpi_recv_handle;
{
MPIR_RHANDLE *dmpi_unexpected;
int          found, err;

/* If this is really a blocking receive, make the blocking receive code 
   do it... */
if (!dmpi_recv_handle->dev_rhandle.is_non_blocking) {
    return MPID_MEIKO_blocking_recv( dmpi_recv_handle );
    }

#ifdef MPID_DEBUG_ALL   /* #DEBUG_START# */
if (MPID_DebugFlag) {
    fprintf( MPID_DEBUG_FILE,
   "[%d]R starting recv for tag = %d, source = %d, ctx = %d, (%s:%d)\n", 
	    MPID_MyWorldRank, dmpi_recv_handle->tag, dmpi_recv_handle->source,
	    dmpi_recv_handle->contextid, __FILE__, __LINE__ );
    fflush( MPID_DEBUG_FILE );
    }
#endif                  /* #DEBUG_END# */
/* At this time, we check to see if the message has already been received.
   (this is a macro that checks first to see if the queue is empty)
   Note that we can not have any thread receiving a message while 
   checking the queues.  Thus, the general thread-locks here are needed.
   Note that the queues have their own locks (whcih are redundent in this
   case).   */
MPID_THREAD_LOCK(0,0)
DMPI_search_unexpected_queue( dmpi_recv_handle->source, 
		   dmpi_recv_handle->tag, dmpi_recv_handle->contextid, 
		   &found, 1, &dmpi_unexpected );
if (found) {
    MPID_THREAD_UNLOCK(0,0)
    DEBUG_PRINT_MSG("R found in unexpected queue")
#if defined(MPID_USE_GET)
    return MPID_MEIKO_Process_unexpected_get( dmpi_recv_handle, 
					    dmpi_unexpected );
#elif defined(MPID_USE_RNDV)                  /*#NONGET_START#*/
    return MPID_MEIKO_Process_unexpected_rndv( dmpi_recv_handle, 
					    dmpi_unexpected );
#else
    return MPID_MEIKO_Process_unexpected( dmpi_recv_handle, dmpi_unexpected );
                                              /*#NONGET_END#*/
#endif
    }

/* Add to the posted receive queue */
MPIR_enqueue( &MPIR_posted_recvs, (MPIR_COMMON *)dmpi_recv_handle, 
	      MPIR_QRHANDLE );
MPID_THREAD_UNLOCK(0,0)

/* If we got here, the message is not yet available */
DEBUG_PRINT_MSG("R About to do a non-blocking check of incoming messages")

MPID_DRAIN_INCOMING

DEBUG_PRINT_MSG("R Exiting post receive")

return MPI_SUCCESS;
}

/*
   Copy the body of a message into the destination buffer for a posted
   receive.  This is used only when the matching receive exists and
   is described by dmpi_recv_handle.
 */
int MPID_MEIKO_Copy_body( dmpi_recv_handle, pkt, from )
MPIR_RHANDLE *dmpi_recv_handle;
MPID_PKT_T   *pkt;
int          from;
{
int err = MPI_SUCCESS;
switch (pkt->head.mode) {
    case MPID_PKT_SHORT_READY:
    case MPID_PKT_SHORT:
    err = MPID_MEIKO_Copy_body_short( dmpi_recv_handle, pkt, 
				   pkt->short_pkt.buffer );
    DMPI_mark_recv_completed(dmpi_recv_handle);
    break;

    case MPID_PKT_SHORT_SYNC:
    /* sync_id  = pkt.short_sync_pkt.sync_id; */
    err = MPID_MEIKO_Copy_body_sync_short( dmpi_recv_handle, pkt, from );
    DMPI_mark_recv_completed(dmpi_recv_handle);
    break;

#ifdef MPID_USE_RNDV                         /*#NONGET_START#*/
    case MPID_PKT_REQUEST_SEND:
    case MPID_PKT_REQUEST_SEND_READY:
    /* Send back an OK to send */
    DEBUG_PRINT_MSG("Acking request to send")
    MPID_MEIKO_Ack_Request( dmpi_recv_handle, from, pkt->request_pkt.send_id,
			 pkt->head.len );
    /* Note that in this case we do not mark the transfer as completed */
    dmpi_recv_handle->completer = MPID_CMPL_RECV_RNDV;
    break;
#endif                                       /*#NONGET_END#*/
#if defined(MPID_USE_GET)                    /*#GET_START#*/
    case MPID_PKT_DO_GET:
    MPID_MEIKO_Do_get( dmpi_recv_handle, from, (MPID_PKT_GET_T *)pkt );
    /* We can't clear the packet here, since the packet address was 
       passed in.  See coments on get */
    /* The completer field is set in Do_get, incase the message is complete */
    /* MPID_PKT_RECV_CLR(pkt); */
    break;

    case MPID_PKT_DO_GET_SYNC:
    MPID_MEIKO_Do_get( dmpi_recv_handle, from, (MPID_PKT_GET_T *)pkt );
    /* Do the sync ack (if necessary - not needed for some get protocols) */
    MPID_SyncReturnAck( ((MPID_PKT_GET_T*)pkt)->sync_id, from );
    /* Note that this needs a completer for SYNC; not set here */
    break;

    case MPID_PKT_DONE_GET:
    /* This means that the send that this is a reply to has completed */
    MPID_MEIKO_Done_get( pkt, from );
    /* Done_get sets the completer field */
    break;

#else                                        /*#GET_END#*/
                                             /*#NONGET_START#*/
    case MPID_PKT_LONG_READY:
    case MPID_PKT_LONG:
#ifdef MPID_USE_RNDV                         
    err = MPID_MEIKO_Copy_body_long_rndv( dmpi_recv_handle, pkt, from );
#else                                        
    err = MPID_MEIKO_Copy_body_long( dmpi_recv_handle, pkt, from );
#endif
    DMPI_mark_recv_completed(dmpi_recv_handle);
    break;

#ifndef MPID_USE_RNDV
    case MPID_PKT_LONG_SYNC:
    /* sync_id  = pkt.long_sync_pkt.sync_id; */
    err = MPID_MEIKO_Copy_body_sync_long( dmpi_recv_handle, pkt, from );
    DMPI_mark_recv_completed(dmpi_recv_handle);
    break;
#endif
                                             /*#NONGET_END#*/
#endif                                       /*#GET_START#*/
                                             /*#GET_END#*/
    default:
    fprintf( stderr, "Internal Error! Unrecognized packet type %d\n", 
	    pkt->head.mode );
    }

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
#define MPIDGETMEM(len) \
            address = (char *)malloc(len);if(!address){\
	    (*MPID_ErrorHandler)( 1, \
			 "No more memory for storing unexpected messages"  );\
	    return MPI_ERR_EXHAUSTED; }

/* 
   This should probably be labeled the "eager" algorithm, and split into
   parts (one to chrndv, one to chget, etc.).
 */
int MPID_MEIKO_Copy_body_unex( dmpi_recv_handle, pkt, from )
MPIR_RHANDLE *dmpi_recv_handle;
MPID_PKT_T   *pkt;
int          from;
{
MPID_RHANDLE *mpid_recv_handle;
char *address;
int  msglen;

mpid_recv_handle = &dmpi_recv_handle->dev_rhandle;
msglen           = pkt->head.len;

mpid_recv_handle->bytes_as_contig = msglen;
mpid_recv_handle->mode		  = 0;   
mpid_recv_handle->from		  = from;
mpid_recv_handle->send_id         = 0;
mpid_recv_handle->start           = 0;
DMPI_Clr_recv_completed( dmpi_recv_handle );
address				  = 0;
switch (pkt->head.mode) {
    case MPID_PKT_SHORT_READY:
    case MPID_PKT_SHORT:
	MPID_KEEP_STAT(MPID_n_short++;)
	if (msglen > 0) {
	    MPIDGETMEM(msglen);
	    MEMCPY( address, pkt->short_pkt.buffer, msglen );
	    }
	DMPI_mark_recv_completed(dmpi_recv_handle);
	break;

    case MPID_PKT_SHORT_SYNC:
	/* Note that the sync_id may be a full address */
	mpid_recv_handle->mode	  = (int)MPIR_MODE_SYNCHRONOUS;
	mpid_recv_handle->send_id = pkt->short_sync_pkt.sync_id;
        MPID_KEEP_STAT(MPID_n_short++;)
	if (msglen > 0) {
	    MPIDGETMEM(msglen);
	    MEMCPY( address, pkt->short_sync_pkt.buffer, msglen );
	    }
	/* completed means that the data is available  */
	DMPI_mark_recv_completed(dmpi_recv_handle);
	break;

#ifdef MPID_USE_RNDV                             
	                                      /*#NONGET_START#*/
    case MPID_PKT_REQUEST_SEND:
    case MPID_PKT_REQUEST_SEND_READY:
	/* Save the send id.  In this case, there is no data. */
	DEBUG_PRINT_MSG("Save request to send id")
	dmpi_recv_handle->dev_rhandle.send_id = pkt->request_pkt.send_id;
	dmpi_recv_handle->totallen	      = pkt->request_pkt.len;
	break;
                                              /*#NONGET_END#*/
#elif defined(MPID_USE_GET)
                                              /*#GET_START#*/
    case MPID_PKT_DO_GET_SYNC:
	mpid_recv_handle->mode	  = (int)MPIR_MODE_SYNCHRONOUS;
	mpid_recv_handle->send_id = pkt->get_pkt.sync_id;
    case MPID_PKT_DO_GET:
	/* We could just save the address, but to start with, we'll
	   copy the message */
	MPIDGETMEM(msglen);
	MPID_KEEP_STAT(MPID_n_long++);
	pkt->get_pkt.recv_id = (MPID_Aint) dmpi_recv_handle;
	MPID_MEIKO_Do_get_to_mem( address, from, (MPID_PKT_GET_T *)pkt );
	/* This isn't correct for sync mode.  */
	if (pkt->get_pkt.cur_offset >= pkt->get_pkt.len) {
	    DMPI_mark_recv_completed(dmpi_recv_handle);
	    }
	else
	    dmpi_recv_handle->completer = MPID_CMPL_RECV_GET;
	    
	/* Can't do the clear here, since the packet isn't given back */
	/* MPID_PKT_RECV_CLR(pkt); */
	break;
                                              /*#GET_END#*/
#else                                         /*#NONGET_START#*/
	/* Eager receive */
    case MPID_PKT_LONG_SYNC:
	/* Note that the sync_id may be a full address */
	mpid_recv_handle->mode	  = (int)MPIR_MODE_SYNCHRONOUS;
	mpid_recv_handle->send_id = pkt->long_sync_pkt.sync_id;
	MPIDGETMEM(msglen);
	MPID_KEEP_STAT(MPID_n_long++;)
	MPID_RecvFromChannel( address, msglen, from );
	/* completed means that the data is available  */
	DMPI_mark_recv_completed(dmpi_recv_handle);
	break;
    case MPID_PKT_LONG_READY:
    case MPID_PKT_LONG:
	MPIDGETMEM(msglen);
	MPID_KEEP_STAT(MPID_n_long++;)
	MPID_RecvFromChannel( address, msglen, from );
	DMPI_mark_recv_completed(dmpi_recv_handle);
	break;
                                              /*#NONGET_END#*/
#endif
    }
mpid_recv_handle->temp            = address;

#ifdef MPID_DEBUG_ALL   /* #DEBUG_START# */
if (MPID_DebugFlag && (pkt->head.mode == MPID_PKT_SHORT_SYNC ||
    pkt->head.mode == MPID_PKT_LONG_SYNC)) {
    fprintf( MPID_DEBUG_FILE,
   "[%d]R setting mode of unexpected message to sync (%s:%d)\n", 
	   MPID_MyWorldRank, __FILE__, __LINE__ );
    }
#endif                  /* #DEBUG_END# */


#ifdef DEBUG_READY
if (MPID_MODE_IS_READY(pkt)) {
    (*MPID_ErrorHandler)( 1, 
			 "Received ready message without matching receive"  );
    return MPI_ERR_NOMATCH;
    }
#endif
return MPI_SUCCESS;
}

/***************************************************************************/
/* This is one of the main routines.  It checks for incoming messages and  */
/* dispatches them.  There is another such look in MPID_MEIKO_blocking_recv   */
/* which is optimized for the important case of blocking receives for a    */
/* particular message.                                                     */
/***************************************************************************/

/* Check for incoming messages.
    Input Parameter:
.   is_blocking - true if this routine should block until a message is
    available

    Returns -1 if nonblocking and no messages pending

    This routine makes use of a single dispatch routine to handle all
    incoming messages.  This makes the code a little lengthy, but each
    piece is relatively simple.
 */    
int MPID_MEIKO_check_incoming( is_blocking )
MPID_BLOCKING_TYPE is_blocking;
{
MPID_PKT_RECV_DECL(MPID_PKT_T,pkt);
int          from;
MPIR_RHANDLE *dmpi_recv_handle;
int          is_posted;
int          err = MPI_SUCCESS;

DEBUG_PRINT_MSG("Entering check_incoming")

/* If nonblocking and no headers available, exit */
#ifndef pvm3
if (is_blocking == MPID_NOTBLOCKING) {
    if (!MPID_PKT_CHECK()) {
	DEBUG_PRINT_MSG("Leaving check_incoming (no messages)")
	return -1;
	}
    DEBUG_PRINT_MSG("Message is available!")
    }
DEBUG_PRINT_MSG("Waiting for message to arrive")
MPID_PKT_WAIT();
#else   /* #PVM3_START# */
/* pvm3.0 doesn't have a real probe, but what they do have meets the 
   semantics that we need here, though it is somewhat painful... 
   All this to save  the user a single routine call in the case where
   a probe is immediately followed by a recv.  Heaven help you if you
   use the probe to decide to call some other code to process the 
   message... 

   Later versions of PVM 3 may have a proper probe; if someone needs it,
   please send mail to mpi-bugs@mcs.anl.gov
*/
{
int bufid, bytes, msgtype; 
if (is_blocking == MPID_NOTBLOCKING) {
    if ((bufid = pvm_nrecv( -1, MPID_PT2PT_TAG )) <= 0) return -1;
    /* If we found a message, we now have to receive it */
    pvm_bufinfo( bufid, &bytes, &msgtype, &__PVMFROMTID );
    pvm_upkint( (int *)&pkt, bytes / sizeof(int), 1 );
    __PVMFROM = -1;
    }
else {
    /* For the blocking case, we can use the existing code ... */
    MPID_PKT_WAIT();
    }
}       /* #PVM3_END# */
#endif
/* 
   This unpacks ONLY the head of the message.
   Note that the payload is handled separately (MPIR_Unpack etc) and
   most of the other data can be considered just bits to return uninterpreted. 

   There are exceptions (see rendevous code); 
 */
MPID_PKT_UNPACK( MPID_PKT_RECV_ADDR(pkt), sizeof(MPID_PKT_HEAD_T), from );

DEBUG_PRINT_PKT("R received message",pkt)

/* Separate the incoming messages from control messages */
if (MPID_PKT_IS_MSG(MPID_PKT_RECV_GET(pkt,head.mode))) {
    DEBUG_PRINT_RECV_PKT("R rcvd msg",pkt)

/* Is the message expected or not? 
   This routine RETURNS a dmpi_recv_handle, creating one if the message 
   is unexpected (is_posted == 0) */
	DMPI_msg_arrived( MPID_PKT_RECV_GET(pkt,head.lrank), 
			  MPID_PKT_RECV_GET(pkt,head.tag), 
			  MPID_PKT_RECV_GET(pkt,head.context_id), 
			  &dmpi_recv_handle, &is_posted );
#ifdef MPID_DEBUG_ALL   /* #DEBUG_START# */
    if (MPID_DebugFlag) {
	fprintf( MPID_DEBUG_FILE, "[%d]R msg was %s (%s:%d)\n", 
		MPID_MyWorldRank, 
		is_posted ? "posted" : "unexpected", __FILE__, __LINE__ );
	}
#endif                  /* #DEBUG_END# */
    if (is_posted) {
	/* We should check the size here for internal errors .... */
	switch (MPID_PKT_RECV_GET(pkt,head.mode)) {
	    case MPID_PKT_SHORT_READY:
	    case MPID_PKT_SHORT:
	    err = MPID_MEIKO_Copy_body_short( dmpi_recv_handle, 
					  MPID_PKT_RECV_ADDR(pkt), 
				     MPID_PKT_RECV_GET(pkt,short_pkt.buffer) );
	    break;
                                              /*#NONGET_START#*/
#ifdef MPID_USE_RNDV                    
	case MPID_PKT_REQUEST_SEND:
	case MPID_PKT_REQUEST_SEND_READY:
	    /* Send back an OK to send, with a tag value and 
	       a posted recv */
	    DEBUG_PRINT_MSG("Acking request to send for posted msg")
	    MPID_MEIKO_Ack_Request( dmpi_recv_handle, from, 
				 MPID_PKT_RECV_GET(pkt,request_pkt.send_id),
				 MPID_PKT_RECV_GET(pkt,head.len) );
	    dmpi_recv_handle->completer = MPID_CMPL_RECV_RNDV;
	break;
#endif
	case MPID_PKT_LONG_READY:
	case MPID_PKT_LONG:
#ifdef MPID_USE_RNDV                   
	    err = MPID_MEIKO_Copy_body_long_rndv( dmpi_recv_handle, 
					       MPID_PKT_RECV_ADDR(pkt), from );
#else                                  
	    err = MPID_MEIKO_Copy_body_long( dmpi_recv_handle, 
					  MPID_PKT_RECV_ADDR(pkt), from );
#endif                                 

	    break;
#ifndef MPID_USE_RNDV
	case MPID_PKT_LONG_SYNC:
	    err = MPID_MEIKO_Copy_body_sync_long( dmpi_recv_handle, 
					       MPID_PKT_RECV_ADDR(pkt), 
					       from );
	    break;
#endif
	                                      /*#NONGET_END#*/
	case MPID_PKT_SHORT_SYNC:
	    err = MPID_MEIKO_Copy_body_sync_short( dmpi_recv_handle, 
					        MPID_PKT_RECV_ADDR(pkt), 
					        from );
	    break;
#ifdef MPID_USE_GET                    /*#GET_START#*/
        case MPID_PKT_DO_GET:
	    MPID_MEIKO_Do_get( dmpi_recv_handle, from, 
			    (MPID_PKT_GET_T *)MPID_PKT_RECV_ADDR(pkt) );
	    /* We no longer send the packet back ... */
	    /* MPID_PKT_RECV_CLR(pkt); */
	    break;

	    case MPID_PKT_DO_GET_SYNC:
	    MPID_MEIKO_Do_get( dmpi_recv_handle, from, 
			    (MPID_PKT_GET_T *)MPID_PKT_RECV_ADDR(pkt) );
	    /* Do the sync ack (if necessary - not needed for some 
	       get protocols) */
	    MPID_SyncReturnAck( 
	       ((MPID_PKT_GET_T*)MPID_PKT_RECV_ADDR(pkt))->sync_id, from );
	    break;
#endif                                    /*#GET_END#*/
	default:
	    fprintf( stderr, 
		    "[%d] Internal error: msg packet discarded (%s:%d)\n",
		    MPID_MyWorldRank, __FILE__, __LINE__ );
		     
	}}
    else {
	MPID_MEIKO_Copy_body_unex( dmpi_recv_handle, MPID_PKT_RECV_ADDR(pkt), 
			        from );
	}
    }
else {
    switch (MPID_PKT_RECV_GET(pkt,head.mode)) {
	case MPID_PKT_SYNC_ACK:
        MPID_SyncAck( MPID_PKT_RECV_GET(pkt,sync_ack_pkt.sync_id), from );
	break;
	case MPID_PKT_COMPLETE_SEND:
	break;
	case MPID_PKT_COMPLETE_RECV:
	break;
#ifdef MPID_USE_RNDV                           /*#NONGET_START#*/
	case MPID_PKT_OK_TO_SEND:
	DEBUG_PRINT_MSG("Responding to Ack for request to send")
	MPID_PKT_UNPACK( &(MPID_PKT_RECV_GET(ptk,sendok_pkt.send_id), 8, from);
	MPID_MEIKO_Do_Request( MPID_PKT_RECV_GET(pkt,sendok_pkt.recv_handle), 
			    from, MPID_PKT_RECV_GET(pkt,sendok_pkt.send_id) );
	break;
#endif                                         /*#NONGET_END#*/
	case MPID_PKT_READY_ERROR:
	break;

#ifdef MPID_USE_GET                            /*#GET_START#*/
        case MPID_PKT_DONE_GET:
        MPID_MEIKO_Done_get( MPID_PKT_RECV_ADDR(pkt), from );
        break;
	case MPID_PKT_CONT_GET:
	MPID_MEIKO_Cont_get( MPID_PKT_RECV_ADDR(pkt), from );
	break;
#endif                                         /*#GET_END#*/

	default:
	fprintf( stdout, "[%d] Mode %d is unknown (internal error) %s:%d!\n", 
		    MPID_MyWorldRank, MPID_PKT_RECV_GET(pkt,head.mode), 
		    __FILE__, __LINE__ );
	}
    /* Really should remember error incase subsequent events are successful */
    }
MPID_PKT_RECV_FREE(pkt);
DEBUG_PRINT_MSG("Exiting check_incoming")
return err;
}


/*
    This routine completes a particular receive.  It does this by processing
    incoming messages until the indicated message is received.

    For fairness, we may want a version with an array of handles.

    In the case of a rendevous send, it may need to wait on a nonblocking
    receive.

    NOTE: MANY MPI_TESTxxx routines are calling this when they
    should be calling MPID_Test_recv instead.  NEED TO FIX....
 */
int MPID_MEIKO_complete_recv( dmpi_recv_handle ) 
MPIR_RHANDLE *dmpi_recv_handle;
{
DEBUG_PRINT_MSG("Starting complete recv")
/* If the 'completer' is still 1, it is because the message hasn't been
   received at all.  Wait for it */
while (dmpi_recv_handle->completer == 1) {
    (void)MPID_MEIKO_check_incoming( MPID_BLOCKING );
    }
DEBUG_PRINT_MSG("Switching on completer")
switch (dmpi_recv_handle->completer) {
    case 0:
    break;
#ifdef MPID_USE_RNDV                                /*#NONGET_START#*/
    case MPID_CMPL_RECV_RNDV:
    DEBUG_PRINT_MSG("Complete rendevous")
    MPID_MEIKO_Cmpl_recv_rndv( dmpi_recv_handle );
    break;
#endif                                              /*#NONGET_END#*/
#ifndef MPID_USE_RNDV                               /*#NONGET_START#*/
    case MPID_CMPL_RECV_NB:
    DEBUG_PRINT_MSG("Complete nonblocking")
    MPID_MEIKO_Cmpl_recv_nb( dmpi_recv_handle );
    break;
#endif                                              /*#NONGET_END#*/
#ifdef MPID_USE_GET                                 /*#GET_START#*/
    case MPID_CMPL_RECV_GET:
    /* Process messages until the message completes */
    DEBUG_PRINT_MSG("Complete get")
    while (dmpi_recv_handle->completer) {
	(void)MPID_MEIKO_check_incoming( MPID_BLOCKING );
	}
    break;
#endif                                              /*#GET_END#*/
    default:
    /* Eventually, should be stderr */
    fprintf( stdout, "[%d]* Unknown recv completion mode of %d, tag = %d\n", 
	     MPID_MyWorldRank, dmpi_recv_handle->completer, 
	     dmpi_recv_handle->tag );
    break;
    }
DEBUG_PRINT_MSG("Completed recv (exiting complete recv)")
return MPI_SUCCESS;
}

int MPID_MEIKO_Test_recv_push( dmpi_recv_handle )
MPIR_RHANDLE *dmpi_recv_handle;
{
#ifdef MPID_USE_RNDV                              /*#NONGET_START#*/
if (dmpi_recv_handle->completer == MPID_CMPL_RECV_RNDV) {
    return MPID_MEIKO_Test_recv_rndv( dmpi_recv_handle );
    }
#endif                                            /*#NONGET_END#*/
/* If completer is 0, the message is complete.  */
return (dmpi_recv_handle->completer == 0);
}

/*
   Special case code for blocking receive.  The "common" case is handled with
   straight-through code; uncommon cases call routines.
   Note that this code never enqueues the request into the posted receive 
   queue.

   This routine is NOT thread-safe; it should not be used in a multi-threaded
   implementation (instead, use the nonblocking code and then do a 
   complete-recv).
 */
int MPID_MEIKO_blocking_recv( dmpi_recv_handle ) 
MPIR_RHANDLE *dmpi_recv_handle;
{
MPID_PKT_RECV_DECL(MPID_PKT_T,pkt);
MPIR_RHANDLE *dmpi_unexpected, *dmpi_save_recv_handle;
int          found, from, is_posted, tag, source, context_id;
int          tagmask, srcmask;
int          ptag, pcid, plrk;   /* Values from packet */
int          err = MPI_SUCCESS;

#ifdef MPID_DEBUG_ALL   /* #DEBUG_START# */
if (MPID_DebugFlag) {
    fprintf( MPID_DEBUG_FILE,
 "[%d]R starting blocking recv for tag = %d, source = %d, ctx = %d, len = %d (%s:%d)\n", 
	    MPID_MyWorldRank, dmpi_recv_handle->tag, dmpi_recv_handle->source,
	    dmpi_recv_handle->contextid, 
	    dmpi_recv_handle->dev_rhandle.bytes_as_contig, 
	    __FILE__, __LINE__ );
    fflush( MPID_DEBUG_FILE );
    }
#endif                  /* #DEBUG_END# */
/* At this time, we check to see if the message has already been received */
tag	   = dmpi_recv_handle->tag;
context_id = dmpi_recv_handle->contextid;
source	   = dmpi_recv_handle->source;

DMPI_search_unexpected_queue( source, tag, context_id, 
		   &found, 1, &dmpi_unexpected );
if (found) {
#if defined(MPID_USE_GET)
    return MPID_MEIKO_Process_unexpected_get( dmpi_recv_handle, 
					    dmpi_unexpected );
#elif defined(MPID_USE_RNDV)                   /*#NONGET_START#*/
    return MPID_MEIKO_Process_unexpected_rndv( dmpi_recv_handle, 
					    dmpi_unexpected );
#else
    return MPID_MEIKO_Process_unexpected( dmpi_recv_handle, dmpi_unexpected );
                                               /*#NONGET_END#*/
#endif
    }

dmpi_save_recv_handle = dmpi_recv_handle;
/* If we got here, the message is not yet available */
DEBUG_PRINT_MSG("R Blocking recv; starting wait loop")
if (tag == MPI_ANY_TAG) {
    tagmask = 0;
    tag     = 0;
    }
else
    tagmask = ~0;
if (source == MPI_ANY_SOURCE) {
    srcmask = 0;
    source  = 0;
    }
else
    srcmask = ~0;
while (!MPID_Test_handle(dmpi_save_recv_handle)) {
    MPID_PKT_POST_AND_WAIT();
    MPID_PKT_UNPACK( MPID_PKT_RECV_ADDR(pkt), sizeof(MPID_PKT_HEAD_T), from );
    if (MPID_PKT_IS_MSG(MPID_PKT_RECV_GET(pkt,head.mode))) {
	ptag = MPID_PKT_RECV_GET(pkt,head.tag);
	plrk = MPID_PKT_RECV_GET(pkt,head.lrank);
	pcid = MPID_PKT_RECV_GET(pkt,head.context_id);
	/* We should check the size here for internal errors .... */
	DEBUG_PRINT_FULL_RECV_PKT("R received message",pkt)
	if (pcid == context_id        && 
	    (ptag & tagmask) == tag   &&
	    (plrk & srcmask) == source) {
	    /* Found the message that I'm waiting for (it was never queued) */
	    is_posted                = 1;
	    dmpi_recv_handle	     = dmpi_save_recv_handle;
	    dmpi_recv_handle->tag    = ptag;
	    dmpi_recv_handle->source = plrk;
	    }
	else {
	    /* Message other than the one we're waiting for... */
	    DMPI_msg_arrived( plrk, ptag, pcid, 
			     &dmpi_recv_handle, &is_posted );
	    }
#ifdef MPID_DEBUG_ALL   /* #DEBUG_START# */
	if (MPID_DebugFlag) {
	    fprintf( MPID_DEBUG_FILE,
		    "[%d]R msg was %s (%s:%d)\n", MPID_MyWorldRank, 
		   is_posted ? "posted" : "unexpected", __FILE__, __LINE__ );
	    }
#endif                  /* #DEBUG_END# */
	if (is_posted) {
	    err = MPID_MEIKO_Copy_body( dmpi_recv_handle, 
				     MPID_PKT_RECV_ADDR(pkt), from );
	    if (dmpi_recv_handle == dmpi_save_recv_handle) {
		MPID_PKT_RECV_FREE(pkt);
		
#ifdef MPID_USE_RNDV                              /*#NONGET_START#*/
	    /* In the special case that we have received the message that
	       we are looking for, but it was sent with the Rendevous
	       send, we need to wait for the message to complete */
		if (!MPID_Test_handle(dmpi_recv_handle) && 
		    dmpi_recv_handle->dev_rhandle.rid) 
		    MPID_MEIKO_complete_recv( dmpi_recv_handle );
#endif                                            /*#NONGET_END#*/
#ifdef MPID_USE_GET                               /*#GET_START#*/
		MPID_MEIKO_complete_recv( dmpi_recv_handle );
#endif                                            /*#GET_END#*/
		return err;
		}
	    }
	else {
	    MPID_MEIKO_Copy_body_unex( dmpi_recv_handle, MPID_PKT_RECV_ADDR(pkt),
				    from );
	    }
	}
    else {
	switch (MPID_PKT_RECV_GET(pkt,head.mode)) {
	    case MPID_PKT_SYNC_ACK:
	    MPID_SyncAck( MPID_PKT_RECV_GET(pkt,sync_ack_pkt.sync_id), from );
	    break;
	    case MPID_PKT_COMPLETE_SEND:
	    break;
	    case MPID_PKT_COMPLETE_RECV:
	    break;
#ifdef MPID_USE_RNDV                            /*#NONGET_START#*/
	    case MPID_PKT_OK_TO_SEND:
	    /* Lookup send handle, respond with data */
	    MPID_MEIKO_Do_Request( MPID_PKT_RECV_GET(pkt,sendok_pkt.recv_handle), 
			        from, 
			        MPID_PKT_RECV_GET(pkt,sendok_pkt.send_id) );
	    break;
#endif                                          /*#NONGET_END#*/
#ifdef MPID_USE_GET                             /*#GET_START#*/
	    case MPID_PKT_DONE_GET:
	    /* This means that the send that this is a 
	       reply to has completed */
	    MPID_MEIKO_Done_get( MPID_PKT_RECV_ADDR(pkt), from );
	    break;
	    case MPID_PKT_CONT_GET:
	    MPID_MEIKO_Cont_get( MPID_PKT_RECV_ADDR(pkt), from );
	    break;
#endif                                          /*#GET_END#*/
	    case MPID_PKT_READY_ERROR:
	    break;
	    default:
	    fprintf( stdout, 
		     "[%d] Mode %d is unknown (internal error) %s:%d!\n", 
		    MPID_MyWorldRank, MPID_PKT_RECV_GET(pkt,head.mode), 
		    __FILE__, __LINE__ );
	    }
	}
    MPID_PKT_RECV_FREE(pkt);
    }

return err;
}
