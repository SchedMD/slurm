/*
 *  $Id: chevent.c,v 1.1.1.1 1997/09/17 20:39:19 gropp Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      All rights reserved.  See COPYRIGHT in top-level directory.
 */

#ifndef lint
static char vcid[] = "$Id: chevent.c,v 1.1.1.1 1997/09/17 20:39:19 gropp Exp $";
#endif

#include "mpid.h"
#include "mpiddebug.h"

/* 
   This file contains routines to see if the "device" wants to do anything
   (like receive some un-expected messages) 
 */
void MPID_CH_check_device( blocking )
int blocking;
{
DEBUG_PRINT_MSG("Entering check device")
if (blocking)
    (void) MPID_CH_check_incoming( MPID_BLOCKING );
else 
    while (MPID_CH_check_incoming( MPID_NOTBLOCKING ) != -1);
DEBUG_PRINT_MSG("Exiting check device")
}

/* 
   Cancel a message.  This is complicated by the fact that we must
   be able to say that a message HAS been cancelled or completed successfully
   given ONLY the status.

   Still to do:  Note that a cancelled message is COMPLETED for the purposes
   of the "completed" tests.
 */
int MPID_CH_Cancel( r )
MPIR_COMMON *r;
{
MPIR_SHANDLE *sh;
#ifndef PI_NO_NSEND
MPID_SHANDLE *sdh;
#endif
	
/* Once completed, the cancel "fails" because the message has already
   been delivered */
if (MPID_Test_handle( r )) return MPI_SUCCESS;

/* Otherwise, we can try to eliminate it.
   One potential problem: rendezvous sends.  We want a cancel to be
   a local operation, so we must make sure that, should we receive an
   acknowledgement that the now-canceled send has been received, that the
   code to process the rndv-ack discards it rather than acting on it.

   To cancel the operation, we need to 
    remove from queue
    mark as cancelled
    free any storage
   Each of these depends on whether it is a send/recv.

   Question:  Who is responsible for freeing the request itself?
   What if it is a persistant request?
 */
switch (r->handle_type) {
    case MPIR_SEND:
        sh = (MPIR_SHANDLE*)r;
#ifndef PI_NO_NSEND
        /* The send may be using a nonblocking send to deliver the
           message.  Cancel the send in that event. */
        sdh = &sh->dev_shandle;
        if (sdh->is_non_blocking && sdh->sid) {
	    MPID_CancelSendChannel( sdh->sid );
            sdh->sid = 0;
            }
#endif        
        /* All other free's are done by MPID_FREE_SEND_HANDLE */
    break;
    case MPIR_RECV:
        /* All other free's are done by MPID_FREE_RECV_HANDLE */
    break;
    }
return MPI_SUCCESS;
}
