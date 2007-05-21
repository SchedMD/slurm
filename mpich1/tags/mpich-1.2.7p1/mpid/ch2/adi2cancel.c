/*
 *  $Id: adi2cancel.c,v 1.6 2001/11/12 23:00:40 ashton Exp $
 *
 *  (C) 1996 by Argonne National Laboratory and Mississipi State University.
 *      All rights reserved.  See COPYRIGHT in top-level directory.
 */

#include "mpid.h"
#include "mpiddev.h"
#include "mpimem.h"
#include "../util/queue.h"

/*
 * This file contains the routines to handle canceling a message
 *
 * Note: for now, cancel will probably only work on unmatched receives.
 * However, this code provides the hooks for supporting more complete
 * cancel implementations.
 */

void MPID_SendCancel( MPI_Request request, int *error_code )
{  
	/* begin MPID_SendCancel */

    MPIR_SHANDLE *shandle = &request->shandle;

    DEBUG_PRINT_MSG("S Starting SendCancel");

    shandle->is_cancelled = 0;
    shandle->cancel_complete = 0;

    MPID_SendCancelPacket(&request, error_code); 

    if (*error_code == MPI_SUCCESS) {   /* begin if !fail */
	DEBUG_PRINT_MSG("Entering while !shandle->cancel_complete");
	while (!shandle->cancel_complete) {  /* begin while */
	    MPID_DeviceCheck( MPID_BLOCKING );
	}
	DEBUG_PRINT_MSG("Leaving while !shandle->cancel_complete");

	if (shandle->is_cancelled) { 
	    if (shandle->finish)
		(shandle->finish)(shandle);  
	    if (shandle->handle_type == MPIR_PERSISTENT_SEND) {
		MPIR_PSHANDLE *pshandle = (MPIR_PSHANDLE *)request;
		pshandle->active = 0; 
	    }
	}

    }  /* end if !fail */

    DEBUG_PRINT_MSG("E Exiting SendCancel");
}  /* end MPID_SendCancel */

 
void MPID_RecvCancel( MPI_Request request, int *error_code )
{
    MPIR_RHANDLE *rhandle = &request->rhandle;

    DEBUG_PRINT_MSG("S Starting RecvCancel"); 

    /* First, try to find in pending receives */
    if (MPID_Dequeue( &MPID_recvs.posted, rhandle ) == 0) {
	/* Mark the request as cancelled */
	rhandle->s.MPI_TAG = MPIR_MSG_CANCELLED;
	/* Mark it as complete */
	rhandle->is_complete = 1;
	/* Should we call finish to free any space?  cancel? */
	if (rhandle->finish)
	    (rhandle->finish)( rhandle ); 
	/* Note that the request is still active until we complete it with
	   a wait/test operation */
    }
    if (rhandle->handle_type == MPIR_PERSISTENT_RECV) {
	MPIR_PRHANDLE *prhandle = (MPIR_PRHANDLE *)request;
	prhandle->active = 0; 
    }
    /* Mark the request as not cancelled */
    /* tag is already set >= 0 as part of receive */
    /* rhandle->s.tag = 0; */
    /* What to do about an inactive persistent receive? */

    /* In the case of a partly completed rendezvous receive, we might
       want to do something */
    *error_code = MPI_SUCCESS;
    DEBUG_PRINT_MSG("E Exiting RecvCancel");
}
