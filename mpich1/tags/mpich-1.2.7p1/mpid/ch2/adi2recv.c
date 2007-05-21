/*
 *
 *
 *  (C) 1995 by Argonne National Laboratory and Mississipi State University.
 *      All rights reserved.  See COPYRIGHT in top-level directory.
 */

#include "mpid.h"
#include "mpiddev.h"
#include "reqalloc.h"
#include "../util/queue.h"

/* 
  This file contains the routines to handle receiving a message
 */

/***************************************************************************/
/*
 * Despite the apparent symmetry, receives are fundamentally different from
 * sends.  All receives happen by processing an incoming item of information
 * and checking it against known receives.
 *
 * Eventually, we may want to make RecvContig a special case (as in the
 * first generation ADI) to avoid the routine calls.
 */
/***************************************************************************/
/* Does this need to return msgrep if heterogeneous? */

/* 
 * Error handling needs additional work.  Is a request that has detected an
 * error complete?  Who sets it?  What if the error is ERR_TRUNCATE (where
 * we'd like to make everything work to the limit of the buffer)?
 */

void MPID_RecvContig( 
	struct MPIR_COMMUNICATOR *comm_ptr, 
	void *buf, 
	int maxlen, 
	int src_lrank, 
	int tag, 
	int context_id, 
	MPI_Status *status, 
	int *error_code )
{
    MPIR_RHANDLE rhandle;
    MPI_Request  request = (MPI_Request)&rhandle;

    DEBUG_INIT_STRUCT(request,sizeof(rhandle));
    MPID_RecvInit( &rhandle );

    /* Just in case; make sure that finish is 0 */
    rhandle.finish = 0;

    *error_code = 0;
    MPID_IrecvContig( comm_ptr, buf, maxlen, src_lrank, tag, context_id, 
		      request, error_code );
    if (!*error_code) {
	MPID_RecvComplete( request, status, error_code );
    }
}

void MPID_IrecvContig( 
	struct MPIR_COMMUNICATOR *comm_ptr, 
	void *buf, 
	int maxlen, 
	int src_lrank, 
	int tag, 
	int context_id, 
	MPI_Request request, 
	int *error_code )
{
    MPIR_RHANDLE *dmpi_unexpected, *rhandle = &request->rhandle;

    DEBUG_PRINT_ARGS("R starting IrecvContig");

    /* The one error test that makes sense here */
    if (buf == 0 && maxlen > 0) {
	*error_code = MPI_ERR_BUFFER;
	return;
    }

    /* 
       At this time, we check to see if the message has already been received.
       Note that we cannot have any thread receiving a message while 
       checking the queues.   In case we do enqueue the message, we set
       the fields that will need to be valid BEFORE calling this routine
       (this is extra overhead ONLY in the case that the message was
       unexpected, which is already the higher-overhead case).
       */
    rhandle->len	 = maxlen;
    rhandle->buf	 = buf;
    rhandle->is_complete = 0;
    rhandle->wait        = 0;
    rhandle->test        = 0;
    rhandle->finish      = 0;
    MPID_Search_unexpected_queue_and_post( src_lrank, tag, context_id,  
					   rhandle, &dmpi_unexpected );
    if (dmpi_unexpected) {
	DEBUG_PRINT_MSG("R Found in unexpected queue");
	DEBUG_TEST_FCN(dmpi_unexpected->push,"req->push");
	*error_code = (*dmpi_unexpected->push)( rhandle, dmpi_unexpected );
	DEBUG_PRINT_MSG("R Exiting IrecvContig");
	/* This may or may not complete the message */
	return;
    }

    /* If we got here, the message is not yet available */
    /*    MPID_DRAIN_INCOMING */

    DEBUG_PRINT_MSG("R Exiting IrecvContig");
}

int MPID_RecvIcomplete( 
	MPI_Request request,
	MPI_Status  *status,
	int         *error_code)
{
    MPIR_RHANDLE *rhandle = &request->rhandle;
    MPID_Device *dev;
    int         lerr;
	    
    if (rhandle->is_complete) {
	if (rhandle->finish) 
	    (rhandle->finish)( rhandle );
	if (status) 
	    *status = rhandle->s;
	*error_code = rhandle->s.MPI_ERROR;
	return 1;
    }

    DEBUG_PRINT_MSG("R Entering RecvIcomplete");
    if (rhandle->test) 
	*error_code = 
	    (*rhandle->test)( rhandle );
    else {
	/* The most common case is a check device loop */
	dev = MPID_devset->dev_list;
	while (dev) {
	    lerr = (*dev->check_device)( dev, MPID_NOTBLOCKING );
	    if (lerr > 0) {
		*error_code = lerr;
		break;
	    }
	    dev = dev->next;
	}
    }
    DEBUG_PRINT_MSG("R Exiting RecvIcomplete");

    if (rhandle->is_complete) {
	if (rhandle->finish) 
	    (rhandle->finish)( rhandle );
	if (status) 
	    *status = rhandle->s;
	*error_code = rhandle->s.MPI_ERROR;
	return 1;
    }
    return 0;
}

void MPID_RecvComplete( 
	MPI_Request request,
	MPI_Status  *status,
	int         *error_code)
{
    MPIR_RHANDLE *rhandle = &request->rhandle;
    int          lerr;

    /* The 'while' is at the top in case the 'wait' routine is changed
       by one of the steps.  This happens, for example, in the Rendezvous
       Protocol */
    DEBUG_PRINT_MSG( "Entering while !rhandle->is_complete" );
    while (!rhandle->is_complete) {
	if (rhandle->wait) {
	    *error_code = 
		(*rhandle->wait)( rhandle );
	}
	else {
	    /* The most common case is a check device loop until it is
	       complete. */
	    MPID_Device *dev;

	    if (MPID_devset->ndev_list == 1) {
		dev = MPID_devset->dev_list;
		if (!rhandle->is_complete) {
		    lerr = (*dev->check_device)( dev, MPID_BLOCKING );
		    /* An error return from check_device might apply
		       to a different request.  We break only if
		       the request that encountered the error is the one
		       we detected */
		    if (lerr > 0 && rhandle->s.MPI_ERROR) {
			*error_code = lerr;
			break;
		    }
		}
	    }
	    else {
		if (!rhandle->is_complete) {
		    dev = MPID_devset->dev_list;
		    while (dev) {
			lerr = (*dev->check_device)( dev, MPID_NOTBLOCKING );
			if (lerr > 0 && rhandle->s.MPI_ERROR) {
			    *error_code = lerr;
			    break;
			}
			dev = dev->next;
		    }
		    /* Could still loose the error code if some OTHER
		       request generated the error.  Not sure how 
		       best to handle.  Call error handler here? */
		}
	    }
	}
    }
    DEBUG_PRINT_MSG( "Leaving while !rhandle->is_complete" );
    if (rhandle->finish) 
	(rhandle->finish)( rhandle );
    if (status) *status = rhandle->s;
    *error_code = rhandle->s.MPI_ERROR;
}

/* Temp fix for MPI_Status_set_elements, needed in Romio */
void MPID_Status_set_bytes( MPI_Status *status, int bytes )
{
    status->count = bytes;
}

#ifdef LAPI
/*\ JN: added to suck incoming messages when waiting for send to complete
\*/
int MPID_Check_Receives_While_Waiting()
{
int     lerr=0;
MPID_Device *dev = MPID_devset->dev_list;

      while (dev) {
           lerr = (*dev->check_device)( dev, MPID_NOTBLOCKING );
           if (lerr > 0) break;
           dev = dev->next;
      }
      return(lerr);
}
#endif
