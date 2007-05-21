






/*
 *  $Id: meikosend.c,v 1.1.1.1 1997/09/17 20:40:45 gropp Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      All rights reserved.  See COPYRIGHT in top-level directory.
 */

#ifndef lint
static char vcid[] = "$Id: meikosend.c,v 1.1.1.1 1997/09/17 20:40:45 gropp Exp $";
#endif

#include "mpid.h"

/* 
   Still need to do:  clean up the post_short, post_long to look more like the
   code in chrecv.c .  Complicated slightly because the chrecv has already
   received part of the message, where here, the message header needs to
   be sent with, possibly, some of the data.

   There are many other strategies for IMPLEMENTING the ADI than the one
   shown here.  For example, a more deliberate packetizing strategy could 
   be used.  For systems with interrupt-driven receives, we could send 
   messages only in responce to a request.  If access to lower levels of
   the transport is available, then the protocols for transmitting a message
   can be customized to the ADI.

   Also to be done:  allow the ADI to dynamically allocate packets and
   store them in the (void *pkt) field in dev_shandle, allowing the use
   of non-blocking operations to send the message packets.  This is needed
   on some systems (like TMC-CMMD and IBM-MPL) that do not provide much
   internal buffering for the user.
 */

/* Here are some definitions to simplify debugging */
#include "mpiddebug.h"

/***************************************************************************/
/* Some operations are completed in several stages.  To ensure that a      */
/* process does not exit from MPID_End while requests are pending, we keep */
/* track of how many are outstanding                                      */
/***************************************************************************/
extern int MPID_n_pending;  /* Number of uncompleted split requests */

/***************************************************************************/

/* This routine is a hook for eventually allowing pre-initialized packets */
void MPID_MEIKO_Init_send_code()
{
}

/* Nonblocking packet allocation for sending? */

/* 
   This file includes the routines to handle the device part of a send
   for Chameleon

   As a reminder, the first element is the device handle, the second is
   the (basically opaque) mpi handle
 */

#ifdef FOO
/* Send a short (single packet message) 
   dest is the GLOBAL rank of the destination (some of the debug 
   macros expect dest) 
*/
int MPID_MEIKO_send_short( buf, len, tag, context_id, lrank_sender, dest,
		        msgrep ) 
void *buf;
int  len, tag, context_id, lrank_sender, dest, msgrep;
{
MPID_PKT_SEND_DECL(MPID_PKT_SHORT_T,pkt);

MPID_PKT_SEND_ALLOC(MPID_PKT_SHORT_T,pkt,0);
/* If pkt is dynamically allocated, we need to check it here */
MPID_PKT_SEND_ALLOC_TEST(pkt,return MPI_ERR_EXHAUSTED)

/* These references are ordered to match the order they appear in the 
   structure */
MPID_PKT_SEND_SET(pkt,mode,MPID_PKT_SHORT);
MPID_PKT_SEND_SET(pkt,context_id,context_id);
MPID_PKT_SEND_SET(pkt,lrank,lrank_sender);
MPID_PKT_SEND_SET(pkt,tag,tag);
MPID_PKT_SEND_SET(pkt,len,len);
MPID_PKT_SEND_SET_HETERO(pkt,msgrep)

DEBUG_PRINT_SEND_PKT("S Sending contig",pkt)

MPID_PKT_PACK( MPID_PKT_SEND_ADDR(pkt), sizeof(MPID_PKT_HEAD_T), dest );

if (len > 0) {
    MEMCPY( MPID_PKT_SEND_GET(pkt,buffer), buf, len );
    DEBUG_PRINT_PKT_DATA("S Getting data from mpid->start",pkt)
    }
/* Always use a blocking send for short messages.
   (May fail with systems that do not provide adequate
   buffering.  These systems should switch to non-blocking sends)
 */
DEBUG_PRINT_SEND_PKT("S Sending message in a single packet",pkt)

MPID_SendControlBlock( MPID_PKT_SEND_ADDR(pkt), 
		       len + sizeof(MPID_PKT_HEAD_T), dest );

MPID_PKT_SEND_FREE(pkt);

DEBUG_PRINT_MSG("S Sent contig message in a single packet")

return MPI_SUCCESS;
}
#endif

int MPID_MEIKO_post_send_short( dmpi_send_handle, mpid_send_handle, len ) 
MPIR_SHANDLE *dmpi_send_handle;
MPID_SHANDLE *mpid_send_handle;
int len;
{
MPID_PKT_SEND_DECL(MPID_PKT_SHORT_T,pkt);
int              dest;

MPID_PKT_SEND_ALLOC(MPID_PKT_SHORT_T,pkt,0);
/* We depend on getting a packet */
/* 			dmpi_send_handle->dev_shandle.is_non_blocking); */
/* If pkt is dynamically allocated, we need to check it here */
MPID_PKT_SEND_ALLOC_TEST(pkt,return MPI_ERR_EXHAUSTED)

/* These references are ordered to match the order they appear in the 
   structure */
dest             = dmpi_send_handle->dest;
MPID_PKT_SEND_SET(pkt,mode,MPID_PKT_SHORT);
MPID_PKT_SEND_SET(pkt,context_id,dmpi_send_handle->contextid);
MPID_PKT_SEND_SET(pkt,lrank,dmpi_send_handle->lrank);
MPID_PKT_SEND_SET(pkt,tag,dmpi_send_handle->tag);
MPID_PKT_SEND_SET(pkt,len,len);
MPID_PKT_SEND_SET_HETERO(pkt,dmpi_send_handle->msgrep)

DEBUG_PRINT_SEND_PKT("S Sending",pkt)

MPID_PKT_PACK( MPID_PKT_SEND_ADDR(pkt), sizeof(MPID_PKT_HEAD_T), dest );

if (len > 0) {
    MEMCPY( MPID_PKT_SEND_GET(pkt,buffer), mpid_send_handle->start, len );
    DEBUG_PRINT_PKT_DATA("S Getting data from mpid->start",pkt)
    }
/* Always use a blocking send for short messages.
   (May fail with systems that do not provide adequate
   buffering.  These systems should switch to non-blocking sends)
 */
DEBUG_PRINT_SEND_PKT("S Sending message in a single packet",pkt)

/* In case the message is marked as non-blocking, indicate that we don't
   need to wait on it.  We may also want to use nonblocking operations
   to send the envelopes.... */
mpid_send_handle->sid = 0;
MPID_DRAIN_INCOMING_FOR_TINY(mpid_send_handle->is_non_blocking)
MPID_SENDCONTROL( mpid_send_handle, MPID_PKT_SEND_ADDR(pkt), 
		      len + sizeof(MPID_PKT_HEAD_T), dest );

DMPI_mark_send_completed( dmpi_send_handle );
MPID_PKT_SEND_FREE(pkt);

DEBUG_PRINT_MSG("S Sent message in a single packet")

return MPI_SUCCESS;
}

/* Long message */
#ifdef MPID_USE_GET               /*#NONGET_START#*/
#elif !defined(MPID_USE_RNDV)
/* Message-passing or channel version of send long message */
int MPID_MEIKO_post_send_long_eager( dmpi_send_handle, mpid_send_handle, len ) 
MPIR_SHANDLE *dmpi_send_handle;
MPID_SHANDLE *mpid_send_handle;
int len;
{
char             *address;
int              pkt_len;
MPID_PKT_SEND_DECL(MPID_PKT_LONG_T,pkt);
int              dest;

MPID_PKT_SEND_ALLOC(MPID_PKT_LONG_T,pkt,0);
/* We depend on getting a packet */
/* 			dmpi_send_handle->dev_shandle.is_non_blocking); */
/* If pkt is dynamically allocated, we need to check it here */
MPID_PKT_SEND_ALLOC_TEST(pkt,return MPI_ERR_EXHAUSTED)

MPID_PKT_SEND_SET(pkt,mode,MPID_PKT_LONG);
pkt_len = sizeof(MPID_PKT_LONG_T); 
MPID_PKT_SEND_SET(pkt,context_id,dmpi_send_handle->contextid);
MPID_PKT_SEND_SET(pkt,lrank,dmpi_send_handle->lrank);
MPID_PKT_SEND_SET(pkt,tag,dmpi_send_handle->tag);
MPID_PKT_SEND_SET(pkt,len,len);
MPID_PKT_SEND_SET_HETERO(pkt,dmpi_send_handle->msgrep)
dest           = dmpi_send_handle->dest;

DEBUG_PRINT_SEND_PKT("S Sending",pkt)

MPID_PKT_PACK( MPID_PKT_SEND_ADDR(pkt), sizeof(MPID_PKT_HEAD_T), dest );

DEBUG_PRINT_LONG_MSG("S Sending extra-long message",pkt)

/* Send as packet only */
MPID_DRAIN_INCOMING_FOR_TINY(mpid_send_handle->is_non_blocking)
MPID_SENDCONTROL( mpid_send_handle, MPID_PKT_SEND_ADDR(pkt), pkt_len, dest );

/* Send the body of the message */
address    = ((char*)mpid_send_handle->start);
/* This may be non-blocking */
MPID_SendData( address, len, dest, mpid_send_handle )

MPID_PKT_SEND_FREE(pkt);
return MPI_SUCCESS;
}
#endif                        /*#NONGET_END#*/

#ifndef PI_NO_NSEND           /*#NONGET_START#*/
MPID_MEIKO_Cmpl_send_nb( dmpi_send_handle )
MPIR_SHANDLE *dmpi_send_handle;
{
MPID_SHANDLE *mpid_send_handle = &dmpi_send_handle->dev_shandle;

DEBUG_PRINT_MSG("Starting Cmpl_send_nb")
if (mpid_send_handle->sid)  {
    /* Before we do the wait, try to clear all pending messages */
    (void)MPID_MEIKO_check_incoming( MPID_NOTBLOCKING );
    MPID_MEIKO_isend_wait( dmpi_send_handle );
    }
DEBUG_PRINT_MSG("Exiting Cmpl_send_nb")
}
#endif                        /*#NONGET_END#*/

/*
   We should really:

   a) remove the sync_send code
   b) ALWAYS use the rndv code

   This will require calling the appropriate test and unexpected
   message routines.  Note that this may fail for zero-length messages,
   unless force synchronous messages to deliver a message with no data
   (this may require a special message pkt).
 */
#ifndef MPID_USE_RNDV
int MPID_MEIKO_post_send_sync_short( dmpi_send_handle, mpid_send_handle, len ) 
MPIR_SHANDLE *dmpi_send_handle;
MPID_SHANDLE *mpid_send_handle;
int len;
{
MPID_PKT_SEND_DECL(MPID_PKT_SHORT_SYNC_T,pkt);
int                   dest;

MPID_PKT_SEND_ALLOC(MPID_PKT_SHORT_SYNC_T,pkt,0);
/* We depend on getting a packet */
/* 			dmpi_send_handle->dev_shandle.is_non_blocking); */
/* If pkt is dynamically allocated, we need to check it here */
MPID_PKT_SEND_ALLOC_TEST(pkt,return MPI_ERR_EXHAUSTED)

/* These references are ordered to match the order they appear in the 
   structure */
dest             = dmpi_send_handle->dest;
MPID_PKT_SEND_SET(pkt,mode,MPID_PKT_SHORT_SYNC); 
MPID_PKT_SEND_SET(pkt,context_id,dmpi_send_handle->contextid);
MPID_PKT_SEND_SET(pkt,lrank,dmpi_send_handle->lrank);
MPID_PKT_SEND_SET(pkt,tag,dmpi_send_handle->tag);
MPID_PKT_SEND_SET(pkt,len,len);
MPID_PKT_SEND_SET_HETERO(pkt,dmpi_send_handle->msgrep)
MPID_PKT_SEND_SET(pkt,sync_id,
		  MPID_MEIKO_Get_Sync_Id( dmpi_send_handle, mpid_send_handle ));

DEBUG_PRINT_SEND_PKT("S Sending",pkt)

MPID_PKT_PACK( MPID_PKT_SEND_ADDR(pkt), sizeof(MPID_PKT_HEAD_T), dest );

if (len > 0) {
    MEMCPY( MPID_PKT_SEND_GET(pkt,buffer), mpid_send_handle->start, len );
    DEBUG_PRINT_PKT_DATA("",pkt)
    }
/* Always use a blocking send for short messages.
   (May fail with systems that do not provide adequate
   buffering.  These systems should switch to non-blocking sends, or use
   blocking if the message itself is in blocking mode.)
 */
DEBUG_PRINT_SEND_PKT("S Sending message in a single packet",pkt)

/* In case the message is marked as non-blocking, indicate that we don't
   need to wait on it */
mpid_send_handle->sid = 0;
MPID_SendControlBlock( MPID_PKT_SEND_ADDR(pkt), 
                  len + (sizeof(MPID_PKT_SHORT_SYNC_T)-MPID_PKT_MAX_DATA_SIZE),
		  dest );
dmpi_send_handle->completer = MPID_CMPL_SEND_SYNC;
DEBUG_PRINT_MSG("S Sent message in a single packet")

return MPI_SUCCESS;
}

/* Long message */
#ifdef MPID_USE_GET               /*#NONGET_START#*/
#else
int MPID_MEIKO_post_send_sync_long_eager( dmpi_send_handle, mpid_send_handle, 
				       len ) 
MPIR_SHANDLE *dmpi_send_handle;
MPID_SHANDLE *mpid_send_handle;
int len;
{
char                 *address;
MPID_PKT_SEND_DECL(MPID_PKT_LONG_SYNC_T,pkt);
int                  dest;

MPID_PKT_SEND_SET(pkt,mode,MPID_PKT_LONG_SYNC); 
MPID_PKT_SEND_SET(pkt,context_id,dmpi_send_handle->contextid);
MPID_PKT_SEND_SET(pkt,lrank,dmpi_send_handle->lrank);
MPID_PKT_SEND_SET(pkt,tag,dmpi_send_handle->tag);
MPID_PKT_SEND_SET(pkt,len,len);
MPID_PKT_SEND_SET_HETERO(pkt,dmpi_send_handle->msgrep)
MPID_PKT_SEND_SET(pkt,sync_id,
		  MPID_MEIKO_Get_Sync_Id( dmpi_send_handle, mpid_send_handle ));
dest           = dmpi_send_handle->dest;

DEBUG_PRINT_SEND_PKT("S Sending ",pkt)
DEBUG_PRINT_LONG_MSG("S Sending extra-long message",pkt)

MPID_PKT_PACK( MPID_PKT_SEND_ADDR(pkt), sizeof(MPID_PKT_HEAD_T), dest );

/* Send as packet only */
MPID_SendControlBlock( MPID_PKT_SEND_ADDR(pkt), 
		       sizeof(MPID_PKT_LONG_SYNC_T), dest );

/* Send the body of the message */
address    = ((char*)mpid_send_handle->start);
MPID_SendData( address, len, dest, mpid_send_handle )
dmpi_send_handle->completer = MPID_CMPL_SEND_SYNC;

return MPI_SUCCESS;
}
#endif                          /*#NONGET_END#*/

void MPID_MEIKO_Cmpl_send_sync( dmpi_send_handle )
MPIR_SHANDLE *dmpi_send_handle;
{
MPID_SHANDLE *mpid_send_handle = &dmpi_send_handle->dev_shandle;

DEBUG_PRINT_MSG("S Starting send_sync")
#ifndef PI_NO_NSEND            /*#NONGET_START#*/
if (mpid_send_handle->sid)  {
    /* Before we do the wait, try to clear all pending messages */
    (void)MPID_MEIKO_check_incoming( MPID_NOTBLOCKING );
    MPID_MEIKO_isend_wait( dmpi_send_handle );
    }
#endif                         /*#NONGET_END#*/

DEBUG_PRINT_MSG("S Entering complete send while loop")
while (!MPID_Test_handle(dmpi_send_handle)) {
    /* This waits for the completion of a synchronous send, since at
       this point, we've finished waiting for the =_isend(,,,,0) to complete,
       or for a incremental get */
    (void)MPID_MEIKO_check_incoming( MPID_BLOCKING );
    }
DEBUG_PRINT_MSG("S Exiting complete send")
DEBUG_PRINT_MSG("S Exiting send_sync")
}
#else   /* non-rndv sync send */
                                          /*#NONGET_START#*/
int MPID_MEIKO_post_send_sync_long_eager( dmpi_send_handle, mpid_send_handle, 
				       len ) 
MPIR_SHANDLE *dmpi_send_handle;
MPID_SHANDLE *mpid_send_handle;
int len;
{
return MPID_MEIKO_post_send_long_rndv( dmpi_send_handle, mpid_send_handle, len );
}
                                           /*#NONGET_END#*/
int MPID_MEIKO_post_send_sync_short( dmpi_send_handle, mpid_send_handle, len ) 
MPIR_SHANDLE *dmpi_send_handle;
MPID_SHANDLE *mpid_send_handle;
int len;
{
return MPID_MEIKO_post_send_long_rndv( dmpi_send_handle, mpid_send_handle, len );
}
#endif  /* else of non-rndv sync send */

#ifdef MPID_ADI_MUST_SENDSELF
/****************************************************************************
  MPID_MEIKO_Post_send_local

  Description
    Some low-level devices do not support sending a message to yourself.  
    This function notifies the soft layer that a message has arrived,
    then copies the body of the message the dmpi handle.  Currently,
    we post (copy) the sent message directly to the unexpected message
    queue or the expected message queue.

  This code was taken from mpid/t3d/t3dsend.c 

  This code is relatively untested.  If the matching receive has not
  been posted, it copies the message rather than defering the copy.
  This may cause problems for some rendevous-based implementations.
 ***************************************************************************/
int MPID_MEIKO_Post_send_local( dmpi_send_handle, mpid_send_handle, len )
MPIR_SHANDLE *dmpi_send_handle;
MPID_SHANDLE *mpid_send_handle;
int           len;
{
    MPIR_RHANDLE    *dmpi_recv_handle;
    int              is_posted;
    int              err = MPI_SUCCESS;

    DEBUG_PRINT_MSG("S Send to self")

    DMPI_msg_arrived( dmpi_send_handle->lrank, dmpi_send_handle->tag, 
                      dmpi_send_handle->contextid, 
                      &dmpi_recv_handle, &is_posted );

    if (is_posted) {

        dmpi_recv_handle->totallen = len;
        
        /* Copy message if needed and mark the receive as completed */
        if (len > 0) 
            MEMCPY( dmpi_recv_handle->dev_rhandle.start, 
                    dmpi_send_handle->dev_shandle.start,
                    len ); 
        DMPI_mark_recv_completed(dmpi_recv_handle);

	/* Mark the send as completed. */
	DMPI_mark_send_completed( dmpi_send_handle );

        return (err);
    }
    else {

        MPID_RHANDLE *mpid_recv_handle;
        char         *address;
        
        /* initialize mpid handle */
        mpid_recv_handle                  = &dmpi_recv_handle->dev_rhandle;
        mpid_recv_handle->bytes_as_contig = len;
        mpid_recv_handle->mode            = 0;   
        /* This could be -1 to indicate message from self */
        mpid_recv_handle->from            = MPID_MyWorldRank; 
        
        /* copy the message */
        if (len > 0) {
            mpid_recv_handle->temp = (char *)malloc(len);
            if ( ! mpid_recv_handle->temp ) {
		(*MPID_ErrorHandler)( 1, 
			 "No more memory for storing unexpected messages"  );
			     return MPI_ERR_EXHAUSTED; 
		}
            MEMCPY( mpid_recv_handle->temp, 
                    dmpi_send_handle->dev_shandle.start, 
                    len );
        }
        DMPI_mark_recv_completed(dmpi_recv_handle);

	/* Mark the send as completed. */
	DMPI_mark_send_completed( dmpi_send_handle );

        return (err);
    }

} /* MPID_MEIKO_Post_send_local */
#endif

/*
   This sends the data.
   It takes advantage of being provided with the address of the user-buffer
   in the contiguous case.
 */
int MPID_MEIKO_post_send( dmpi_send_handle ) 
MPIR_SHANDLE *dmpi_send_handle;
{
MPID_SHANDLE *mpid_send_handle;
int         actual_len, rc;

DEBUG_PRINT_MSG("S Entering post send")

mpid_send_handle = &dmpi_send_handle->dev_shandle;
actual_len       = mpid_send_handle->bytes_as_contig;

#ifdef MPID_ADI_MUST_SENDSELF
{int dest;

dest = dmpi_send_handle->dest;
if (dest == MPID_MyWorldRank) {
    return MPID_MEIKO_Post_send_local( dmpi_send_handle, mpid_send_handle, 
				    actual_len );
    }
 }
#endif

/* Eventually, we'd like to make this more dynamic.  We'd need to
   with a multiprotocol channel interface, perhaps using some 
   "channel profile" in the description of that particular interface.
   If we can stick to ADI multiprotocol level, then we don't need to
   do anything here, since the "channel profile" will be part of the 
   's */
if (actual_len > MPID_PKT_DATA_SIZE) 
#ifdef MPID_USE_GET
    rc = MPID_MEIKO_post_send_long_get( dmpi_send_handle, mpid_send_handle, 
				   actual_len );
#elif defined(MPID_USE_RNDV)              /*#NONGET_START#*/
    rc = MPID_MEIKO_post_send_long_rndv( dmpi_send_handle, mpid_send_handle, 
				      actual_len );
#else
    rc = MPID_MEIKO_post_send_long_eager( dmpi_send_handle, mpid_send_handle, 
				       actual_len );
                                          /*#NONGET_END#*/
#endif
else
    rc = MPID_MEIKO_post_send_short( dmpi_send_handle, mpid_send_handle, 
				    actual_len );

/* Poke the device in case there is data ... */
DEBUG_PRINT_MSG("S Draining incoming...")
MPID_DRAIN_INCOMING;
DEBUG_PRINT_MSG("S Exiting post send")

return rc;
}

int MPID_MEIKO_post_send_sync( dmpi_send_handle ) 
MPIR_SHANDLE *dmpi_send_handle;
{
MPID_SHANDLE *mpid_send_handle;
int         actual_len, rc;

mpid_send_handle = &dmpi_send_handle->dev_shandle;
actual_len       = mpid_send_handle->bytes_as_contig;

if (actual_len > MPID_PKT_DATA_SIZE) 
#if defined(MPID_USE_GET)
    rc = MPID_MEIKO_post_send_sync_long_get( dmpi_send_handle, mpid_send_handle, 
					  actual_len );
#elif defined(MPID_USE_RNDV)      /*#NONGET_START#*/
    rc = MPID_MEIKO_post_send_long_rndv( dmpi_send_handle, mpid_send_handle, 
				      actual_len );
#else
    rc = MPID_MEIKO_post_send_sync_long_eager( dmpi_send_handle, 
					    mpid_send_handle, actual_len );
                                  /*#NONGET_END#*/
#endif
else
    rc = MPID_MEIKO_post_send_sync_short( dmpi_send_handle, mpid_send_handle, 
					 actual_len );

/* Poke the device in case there is data ... */
MPID_DRAIN_INCOMING;

return rc;
}

#ifdef FOO
/* 
   This version does NOT use the request handles; instead, it
   dispatches the data directly.  Intended for contiguous messages
   on homogeneous systems.
 */
int MPID_MEIKO_Contig_blocking_send( buf, len, tag, context_id, lrank_sender, 
				  grank_dest, msgrep )
void *buf;
int  len, tag, context_id, lrank_sender, grank_dest, msgrep;
{
int rc;

#ifdef MPID_ADI_MUST_SENDSELF
fprintf( stderr, "Send to self Unsupported at this time\n" );
#endif

if (len <= MPID_PKT_DATA_SIZE) 
    rc = MPID_MEIKO_send_short( buf, len, tag, context_id, lrank_sender, 
			     grank_dest, msgrep );
else 
#ifdef MPID_USE_GET
    rc = MPID_MEIKO_send_long_get( buf, len, tag, context_id, lrank_sender, 
			        grank_dest, msgrep );
#ifdef FOO
#elif defined(MPID_USE_RNDV)              /*#NONGET_START#*/
    rc = MPID_MEIKO_send_long_rndv( buf, len, tag, context_id, lrank_sender, 
			         grank_dest, msgrep);
#else
    rc = MPID_MEIKO_send_long_eager( buf, len, tag, context_id, lrank_sender, 
				  grank_dest, msgrep);
                                          /*#NONGET_END#*/
#endif
#else
.... unsupported ... (part of  FOO)
#endif

/* Poke the device in case there is data ... */
DEBUG_PRINT_MSG("S Draining incoming...")
MPID_DRAIN_INCOMING;

return rc;
}
#endif
/* Note that this routine is usually inlined by dm.h */
int MPID_MEIKO_Blocking_send( dmpi_send_handle )
MPIR_SHANDLE *dmpi_send_handle;
{
int err = MPI_SUCCESS;

DEBUG_PRINT_MSG("S Entering blocking send")
#ifdef MPID_LIMITED_BUFFERS
/* Force the use of non-blocking operations so that head-to-head operations
   can complete when there is an IRECV posted */
dmpi_send_handle->dev_shandle.is_non_blocking = 1;
err = MPID_MEIKO_post_send( dmpi_send_handle );
if (!err) MPID_MEIKO_complete_send( dmpi_send_handle );
dmpi_send_handle->dev_shandle.is_non_blocking = 0;
#else
err = MPID_MEIKO_post_send( dmpi_send_handle );
if (!err) MPID_MEIKO_complete_send( dmpi_send_handle );
#endif
DEBUG_PRINT_MSG("S Exiting blocking send")
return err;
}

/*#NONGET_START#*/
/*
  Chameleon gets no asynchronous notice that the message has been complete,
  so there is no asynchronous ref to DMPI_mark_send_completed.
 */
int MPID_MEIKO_isend_wait( dmpi_send_handle )
MPIR_SHANDLE *dmpi_send_handle;
{
MPID_SHANDLE *mpid_send_handle;

DEBUG_PRINT_MSG("S Starting isend_wait")

/* Wait on the message */
#ifndef PI_NO_NSEND
mpid_send_handle = &dmpi_send_handle->dev_shandle;
if (mpid_send_handle->sid) {
    /* We don't use non-blocking if the message is short enough... */
    /* We should probably ONLY do this in response to an explicit 
       note that the message has been received */
#ifdef MPID_LIMITED_BUFFERS
    /* We do this to keep us from blocking in a wait in the event that
       we must handle some incoming messages before we can execute the
       wait. */
    while (!MPID_TestSendTransfer(mpid_send_handle->sid))
	(void) MPID_MEIKO_check_incoming( MPID_NOTBLOCKING );
    /* Once we have it, the message is completed */
    mpid_send_handle->sid = 0;
#else
    MPID_WSendChannel( (void *)0, mpid_send_handle->bytes_as_contig, -1,
		      mpid_send_handle->sid );
    mpid_send_handle->sid = 0;
#endif
    }
#endif /* PI_NO_NSEND */
if (dmpi_send_handle->mode != MPIR_MODE_SYNCHRONOUS) {
    DMPI_mark_send_completed( dmpi_send_handle );
    }

DEBUG_PRINT_MSG("S Exiting isend_wait")

return MPI_SUCCESS;
}
/*#NONGET_END#*/

/*
  We have to be careful here.  If the wait would block because a matching
  receive has not yet been posted on the destination end, we could deadlock.

  The "solution" here is to first clear any incoming messages.  This allows
  us to post a matching receive that this send is supposed to complete.
  This solution is not complete; there are race conditions that can still
  cause it to fail.  In addition, the current code to handle incoming messages
  may try to force the receive to complete first; this will cause some systems
  to deadlock.  We probably need to packetize to guarentee reliable 
  behavior, and allow for partial completion.  

  Deferred to a later implementation (or better systems!)
 */
int MPID_MEIKO_complete_send( dmpi_send_handle ) 
MPIR_SHANDLE *dmpi_send_handle;
{
int err = MPI_SUCCESS;

/* Check to see if we need to complete the send. */
DEBUG_PRINT_MSG("S Entering complete send")

switch (dmpi_send_handle->completer) {
    case 0: 
         /* Message already completed */
         break;
#ifdef MPID_USE_RNDV                   /*#NONGET_START#*/
    case MPID_CMPL_SEND_RNDV:
	 MPID_MEIKO_Cmpl_send_rndv( dmpi_send_handle );
         break;
#endif                                 /*#NONGET_END#*/
#ifdef MPID_USE_GET                    /*#GET_START#*/
    case MPID_CMPL_SEND_GET:
	 MPID_MEIKO_Cmpl_send_get( dmpi_send_handle );
         break;
#endif                                 /*#GET_END#*/
#ifndef PI_NO_NSEND                    /*#NONGET_START#*/
    case MPID_CMPL_SEND_NB:
	 MPID_MEIKO_Cmpl_send_nb( dmpi_send_handle );
         break;
#endif                                 /*#NONGET_END#*/
#ifndef MPID_USE_RNDV
    case MPID_CMPL_SEND_SYNC:
	 /* Also does non-blocking sync sends */
	 MPID_MEIKO_Cmpl_send_sync( dmpi_send_handle );
	 break;
#endif
    default:
	 fprintf( stdout, "[%d]* Unexpected send completion mode %d\n", 
	          MPID_MyWorldRank, dmpi_send_handle->completer );
	 MPID_MEIKO_Print_Send_Handle( dmpi_send_handle );
	 fprintf( stdout, "[%d]* dmpi_send_contents:\n\
* dest	      = %d\n\
* tag	      = %d\n\
* contextid   = %d\n\
* buflen      = %d\n\
* count	      = %d\n\
* totallen    = %d\n\
* mode	      = %d\n\
* lrank	      = %d\n\
* recv_handle = %x\n", MPID_MyWorldRank, dmpi_send_handle->dest, 
		 dmpi_send_handle->tag, dmpi_send_handle->contextid, 
		 dmpi_send_handle->buflen, dmpi_send_handle->count,
		 dmpi_send_handle->totallen, dmpi_send_handle->mode, 
		 dmpi_send_handle->lrank, 
		 dmpi_send_handle->dev_shandle.recv_handle );
	 err = MPI_ERR_INTERN;
	 break;
    }
DEBUG_PRINT_MSG("S Exiting complete send")

return err;
}


/* 
   This routine tests for a send to be completed.  If non-blocking operations
   are used, it must check those operations...
 */
int MPID_MEIKO_Test_send( dmpi_send_handle )
MPIR_SHANDLE *dmpi_send_handle;
{
#ifdef MPID_USE_RNDV                           /*#NONGET_START#*/
MPID_MEIKO_Test_send_rndv( dmpi_send_handle );
#endif                                         
#ifndef PI_NO_NSEND
if (!MPID_Test_handle(dmpi_send_handle) &&
    dmpi_send_handle->dev_shandle.sid && 
    dmpi_send_handle->completer == MPID_CMPL_SEND_NB) {
    /* Note that if the test succeeds, the sid must be cleared; 
       otherwise we may attempt to wait on it later */
    if (MPID_TSendChannel( dmpi_send_handle->dev_shandle.sid )) {
	dmpi_send_handle->dev_shandle.sid = 0;
	return 1;
	}
    else 
	return 0;
    /* return MPID_TSendChannel( dmpi_send_handle->dev_shandle.sid ) ; */
    }
#endif                                         /*#NONGET_END#*/
/* Need code for GET? */
return MPID_Test_handle(dmpi_send_handle);
}

/* 
   This routine makes sure that we complete all pending requests

   Note: We should make it illegal here to receive anything put
   things like DONE_GET and COMPLETE_SEND.

   Something to fix: I've seen MPID_n_pending < 0!
 */
int MPID_MEIKO_Complete_pending()
{
DEBUG_PRINT_MSG( "Starting Complete_pending")
while (MPID_n_pending > 0) {
    (void)MPID_MEIKO_check_incoming( MPID_BLOCKING );
    }
DEBUG_PRINT_MSG( "Exiting Complete_pending")
return MPI_SUCCESS;
}
