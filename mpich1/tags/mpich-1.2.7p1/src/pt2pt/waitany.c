/*
 *  $Id: waitany.c,v 1.20 2003/01/09 20:48:42 gropp Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Waitany = PMPI_Waitany
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Waitany  MPI_Waitany
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Waitany as PMPI_Waitany
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPI_BUILD_PROFILING
#include "mpiprof.h"
/* Insert the prototypes for the PMPI routines */
#undef __MPI_BINDINGS
#include "binding.h"
#endif
#include "reqalloc.h"

/* index is a function in string.h.  Define this to suppress warnings about
   shadowed symbols from the C compiler */
#ifndef index
#define index idx
#endif
/*@
    MPI_Waitany - Waits for any specified send or receive to complete

Input Parameters:
+ count - list length (integer) 
- array_of_requests - array of requests (array of handles) 

Output Parameters:
+ index - index of handle for operation that completed (integer).  In the
range '0' to 'count-1'.  In Fortran, the range is '1' to 'count'.
- status - status object (Status).  May be 'MPI_STATUS_IGNORE'.

Notes:
If all of the requests are 'MPI_REQUEST_NULL', then 'index' is returned as 
'MPI_UNDEFINED', and 'status' is returned as an empty status.

.N waitstatus

.N fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_REQUEST
.N MPI_ERR_ARG
@*/
int MPI_Waitany(
	int count, 
	MPI_Request array_of_requests[], 
	int *index, 
	MPI_Status *status )
{
    int i, mpi_errno = MPI_SUCCESS;
    int done;
    MPI_Request request;
    static char myname[] = "MPI_WAITANY";

    TR_PUSH(myname);
    *index = MPI_UNDEFINED;

    /* Check for all requests either null or inactive persistent */
    for (i=0; i < count; i++) {
	request = array_of_requests[i];
	if (!request) continue;
	if (request->handle_type == MPIR_PERSISTENT_SEND) {
	    if (request->persistent_shandle.active) break;
	    if (MPID_SendRequestCancelled(&request->persistent_shandle))
		break;
	}
	else if (request->handle_type == MPIR_PERSISTENT_RECV) {
	    if (request->persistent_rhandle.active) break;
	    if (request->persistent_rhandle.rhandle.s.MPI_TAG ==
		MPIR_MSG_CANCELLED) break;
	}
	else 
	    break;
    }

    if (i == count) {
	/* MPI Standard 1.1 requires an empty status in this case */
        if (status) {
	    status->MPI_TAG	   = MPI_ANY_TAG;
	    status->MPI_SOURCE = MPI_ANY_SOURCE;
	    status->MPI_ERROR  = MPI_SUCCESS;
	    MPID_ZERO_STATUS_COUNT(status);
	}
        *index             = MPI_UNDEFINED;  /* Also required in 1.1 */
	TR_POP;
	return mpi_errno;
	}
    done = 0;
    while (!done) {
	for (i=0; !done && i<count; i++) {
	    request = array_of_requests[i];
	    if (!request) continue;
	    switch (request->handle_type) {
	    case MPIR_SEND:
		if (MPID_SendRequestCancelled(request)) {
		    if (status) 
			status->MPI_TAG = MPIR_MSG_CANCELLED; 
		    *index = i;
		    done = 1;
		}
		else {
		    if (MPID_SendIcomplete( request, &mpi_errno )) {
			if (mpi_errno) 
			    MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
			MPIR_FORGET_SEND( &request->shandle );
			MPID_SendFree( array_of_requests[i] );
			*index = i;
			array_of_requests[i] = 0;
			done = 1;
		    }
		}
		break;
	    case MPIR_RECV:
		if (request->rhandle.s.MPI_TAG == MPIR_MSG_CANCELLED) {
		    if (status) 
			status->MPI_TAG = MPIR_MSG_CANCELLED;
		    MPID_RecvFree( array_of_requests[i] );
		    *index = i;
		    array_of_requests[i] = 0; 
		    done = 1;
		}
		else {
		    /* MPID_RecvIcomplete accepts null status */
		    if (MPID_RecvIcomplete( request, status, &mpi_errno )) {
			if (mpi_errno) 
			    MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
			MPID_RecvFree( array_of_requests[i] );
			*index = i;
			array_of_requests[i] = 0;
			done = 1;
		    }
		}
		break;
	    case MPIR_PERSISTENT_SEND:
		if (request->persistent_shandle.active) {
		    if (MPID_SendIcomplete( request, &mpi_errno )) {
			if (mpi_errno) 
			    MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
			request->persistent_shandle.active = 0;
			*index = i;
			done = 1;
		    }
		}
		else {
		    if (MPID_SendRequestCancelled(&request->persistent_shandle)) {
			if (status) 
			    status->MPI_TAG = MPIR_MSG_CANCELLED; 
			*index = i;
			done = 1;
		    }
		}
		break;
	    case MPIR_PERSISTENT_RECV:
		if (request->persistent_rhandle.active) {
		    if (MPID_RecvIcomplete( request, status, &mpi_errno )) {
			if (mpi_errno) 
			    MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
			request->persistent_rhandle.active = 0;
			*index = i;
			done   = 1;
		    }
		}
		else {
		    if (request->persistent_rhandle.rhandle.s.MPI_TAG ==
			MPIR_MSG_CANCELLED) {
			if (status) 
			    status->MPI_TAG = MPIR_MSG_CANCELLED; 
			*index = i;
			done = 1;
		    }
		}
		break;
	    }
	}
	if (!done) {
	    /* Do a NON blocking check */
	    MPID_DeviceCheck( MPID_NOTBLOCKING );
	}
	else 
	    break;
    }
    TR_POP;
    return mpi_errno;
}
