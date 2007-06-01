/*
 *  $Id: chsend.c,v 1.1.1.1 1997/09/17 20:39:20 gropp Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      All rights reserved.  See COPYRIGHT in top-level directory.
 */

#ifndef lint
static char vcid[] = "$Id: chsend.c,v 1.1.1.1 1997/09/17 20:39:20 gropp Exp $";
#endif

#include "mpid.h"

/* Here are some definitions to simplify debugging */
#include "mpiddebug.h"

/***************************************************************************/
/* Some operations are completed in several stages.  To ensure that a      */
/* process does not exit from MPID_End while requests are pending, we keep */
/* track of how many are outstanding                                      */
/***************************************************************************/
extern int MPID_n_pending;  /* Number of uncompleted split requests */

/***************************************************************************/

/* 
   This file includes the routines to handle the device part of a send
   for Chameleon

   As a reminder, the first element is the device handle, the second is
   the (basically opaque) mpi handle
 */

int MPID_CH_post_send_short( dmpi_send_handle, mpid_send_handle, len ) 
MPIR_SHANDLE *dmpi_send_handle;
MPID_SHANDLE *mpid_send_handle;
int len;
{
return (send_short)( mpid_send_handle->start, len, dmpi_send_handle->tag, 
		     dmpi_send_handle->contextid, dmpi_send_handle->lrank,
		     dmpi_send_handle->dest, dmpi_send_handle->msgrep );
}

MPID_CH_Cmpl_send_nb( dmpi_send_handle )
MPIR_SHANDLE *dmpi_send_handle;
{
MPID_SHANDLE *mpid_send_handle = &dmpi_send_handle->dev_shandle;

DEBUG_PRINT_MSG("Starting Cmpl_send_nb")
if (mpid_send_handle->sid)  {
    /* Before we do the wait, try to clear all pending messages */
    (void)MPID_CH_check_incoming( MPID_NOTBLOCKING );
    (isend_wait)( dmpi_send_handle );
    }
DEBUG_PRINT_MSG("Exiting Cmpl_send_nb")
}

/* Always use rndv code for sync send/recv */
                                          /*#NONGET_START#*/
/*
   This sends the data.
   It takes advantage of being provided with the address of the user-buffer
   in the contiguous case.
 */
int MPID_CH_post_send( dmpi_send_handle ) 
MPIR_SHANDLE *dmpi_send_handle;
{
MPID_SHANDLE *mpid_send_handle;
int         len, rc;

DEBUG_PRINT_MSG("S Entering post send")

mpid_send_handle = &dmpi_send_handle->dev_shandle;
len       = mpid_send_handle->bytes_as_contig;

#ifdef MPID_ADI_MUST_SENDSELF
{int dest;

dest = dmpi_send_handle->dest;
if (dest == MPID_MyWorldRank) {
    return MPID_CH_Post_send_local( dmpi_send_handle, mpid_send_handle, len );
    }
 }
#endif

if (len > MPID_PKT_DATA_SIZE) {
    if (mpid_send_handle->is_non_blocking) {
	rc = (isend)( mpid_send_handle->start, len, dmpi_send_handle->tag,
		   dmpi_send_handle->contextid, dmpi_send_handle->lrank,
	           dmpi_send_handle->dest, dmpi_send_handle->msgrep,
		   &dmpi_send_handle->dev_shandle.sid );
	}
    else {
	rc = (send)( mpid_send_handle->start, len, dmpi_send_handle->tag,
		   dmpi_send_handle->contextid, dmpi_send_handle->lrank,
	           dmpi_send_handle->dest, dmpi_send_handle->msgrep );
	}
    }
else
    rc = (send_short)( dmpi_send_handle, mpid_send_handle, len );

/* Poke the device in case there is data ... */
DEBUG_PRINT_MSG("S Draining incoming...")
MPID_DRAIN_INCOMING;
DEBUG_PRINT_MSG("S Exiting post send")

return rc;
}

int MPID_CH_post_send_sync( dmpi_send_handle ) 
MPIR_SHANDLE *dmpi_send_handle;
{
MPID_SHANDLE *mpid_send_handle;
int         actual_len, rc;

mpid_send_handle = &dmpi_send_handle->dev_shandle;
actual_len       = mpid_send_handle->bytes_as_contig;

 no test for short ---

/* Poke the device in case there is data ... */
MPID_DRAIN_INCOMING;

return rc;
}

/* Note that this routine is usually inlined by dm.h */
int MPID_CH_Blocking_send( dmpi_send_handle )
MPIR_SHANDLE *dmpi_send_handle;
{
    int err = MPI_SUCCESS;
    
    DEBUG_PRINT_MSG("S Entering blocking send")
#ifdef MPID_LIMITED_BUFFERS
    /* Force the use of non-blocking operations so that head-to-head operations
       can complete when there is an IRECV posted */
    dmpi_send_handle->dev_shandle.is_non_blocking = 1;
    err = MPID_CH_post_send( dmpi_send_handle );
    if (!err) MPID_CH_complete_send( dmpi_send_handle );
    dmpi_send_handle->dev_shandle.is_non_blocking = 0;
#else
    err = MPID_CH_post_send( dmpi_send_handle );
    if (!err) MPID_CH_complete_send( dmpi_send_handle );
#endif
    DEBUG_PRINT_MSG("S Exiting blocking send")
	return err;
}

/*
  Chameleon gets no asynchronous notice that the message has been complete,
  so there is no asynchronous ref to DMPI_mark_send_completed.
 */
int MPID_CH_isend_wait( dmpi_send_handle )
MPIR_SHANDLE *dmpi_send_handle;
{
    int err;

    DEBUG_PRINT_MSG("S Starting isend_wait")
    err = (wait_send)( &dmpi_send_handle->dev_shandle.sid );
    DMPI_mark_send_completed( dmpi_send_handle );
    DEBUG_PRINT_MSG("S Exiting isend_wait")

    return err;
}

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
int MPID_CH_complete_send( dmpi_send_handle ) 
MPIR_SHANDLE *dmpi_send_handle;
{
int err = MPI_SUCCESS;

/* Check to see if we need to complete the send. */
DEBUG_PRINT_MSG("S Entering complete send")

// should just call completion routine stored in request!
if (dmpi_send_handle->completer) 
    (*dmpi_send_handle->completer)( dmpi_send_handle );
// These are things like Cmpl_send_rndv etc.

DEBUG_PRINT_MSG("S Exiting complete send")

return err;
}

/* 
   This routine tests for a send to be completed.  If non-blocking operations
   are used, it must check those operations...
 */
int MPID_CH_Test_send( dmpi_send_handle )
MPIR_SHANDLE *dmpi_send_handle;
{
int flag;
if (!MPID_Test_handle( dmpi_send_handle ) {
    /* Completion routine for send should be stored with send? */
    flag = (dmpi_send_handle->test_send)( &dmpi_send_handle->dev_shandle.sid );
    if (flag) dmpi_send_handle->completer = 0;
    }
/* Need code for GET? */
return MPID_Test_handle(dmpi_send_handle);
}

/* 
   This routine makes sure that we complete all pending requests

   Note: We should make it illegal here to receive anything put
   things like DONE_GET and COMPLETE_SEND.

   Something to fix: I've seen MPID_n_pending < 0!
 */
int MPID_CH_Complete_pending()
{
DEBUG_PRINT_MSG( "Starting Complete_pending")
while (MPID_n_pending > 0) {
    (void)MPID_CH_check_incoming( MPID_BLOCKING );
    }
DEBUG_PRINT_MSG( "Exiting Complete_pending")
return MPI_SUCCESS;
}
