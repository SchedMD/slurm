/*
 *  $Id: testany.c,v 1.19 2003/01/09 20:48:41 gropp Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Testany = PMPI_Testany
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Testany  MPI_Testany
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Testany as PMPI_Testany
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
    MPI_Testany - Tests for completion of any previdously initiated 
                  communication

Input Parameters:
+ count - list length (integer) 
- array_of_requests - array of requests (array of handles) 

Output Parameters:
+ index - index of operation that completed, or 'MPI_UNDEFINED'  if none 
  completed (integer) 
. flag - true if one of the operations is complete (logical) 
- status - status object (Status).  May be 'MPI_STATUS_IGNORE'.

.N waitstatus

.N fortran

.N Errors
.N MPI_SUCCESS
@*/
int MPI_Testany( 
	int count, 
	MPI_Request array_of_requests[], 
	int *index, int *flag, 
	MPI_Status *status )
{
    int i, found, mpi_errno = MPI_SUCCESS;
    MPI_Request request;
    int nnull = 0;
    static char myname[] = "MPI_TESTANY";

    TR_PUSH(myname);
    *index = MPI_UNDEFINED;

    MPID_DeviceCheck( MPID_NOTBLOCKING );
    found = 0;
    for (i = 0; i < count && !found; i++) {
	/* Skip over null handles.  We need this for handles generated
	   when MPI_PROC_NULL is the source or destination of an 
	   operation */
	request = array_of_requests[i];

	if (!request) {
	    nnull ++;
	    continue;
	    }
       
	switch (request->handle_type) {
	case MPIR_SEND:
	    if (MPID_SendRequestCancelled(request)) {
		if (status)
		    status->MPI_TAG = MPIR_MSG_CANCELLED; 
		*index = i;
		found = 1;
	    }
	    else {
		if (request->shandle.is_complete || 
		    MPID_SendIcomplete( request, &mpi_errno )) {
		    *index = i;
		    MPIR_FORGET_SEND(&request->shandle);
		    MPID_SendFree( request );
		    array_of_requests[i] = 0;
		    found = 1;
		}
	    }
	    break;
	case MPIR_RECV:
	    if (request->rhandle.s.MPI_TAG == MPIR_MSG_CANCELLED) {
		if (status) 
		    status->MPI_TAG = MPIR_MSG_CANCELLED; 
		*index = i;
		found = 1;
	    }
	    else {
		if (request->rhandle.is_complete || 
		    MPID_RecvIcomplete( request, (MPI_Status *)0, 
					&mpi_errno )) {
		    *index = i;
		    if (status)
			*status = request->rhandle.s;
		    MPID_RecvFree( request );
		    array_of_requests[i] = 0;
		    found = 1;
		}
	    }
	    break;
	case MPIR_PERSISTENT_SEND:
	    if (!request->persistent_shandle.active) {
		if (MPID_SendRequestCancelled(&request->persistent_shandle)) {
		    if (status)
			status->MPI_TAG = MPIR_MSG_CANCELLED;
		    *index = i;
		    found = 1;
		}
		else
		    nnull++;
	    }
	    else if (request->persistent_shandle.shandle.is_complete ||
		     MPID_SendIcomplete( request, &mpi_errno )) {
		*index = i;
		request->persistent_shandle.active = 0;
		found = 1;
	    }
	    break;
	case MPIR_PERSISTENT_RECV:
	    if (!request->persistent_rhandle.active) {
		if (request->persistent_rhandle.rhandle.s.MPI_TAG ==
		    MPIR_MSG_CANCELLED) {
		    if (status)
			status->MPI_TAG = MPIR_MSG_CANCELLED;
		    *index = i;
		    found = 1;
		}
		else
		    nnull++;
	    }
	    else if (request->persistent_rhandle.rhandle.is_complete ||
		     MPID_RecvIcomplete( request, (MPI_Status *)0, 
					 &mpi_errno )) {
		*index = i;
		if (status)
		    *status = request->persistent_rhandle.rhandle.s;
		request->persistent_rhandle.active = 0;
		found = 1;
	    }
	    break;
	}
    }
    if (nnull == count) {
	/* MPI Standard 1.1 requires an empty status in this case */
	if (status) {
	    status->MPI_TAG	   = MPI_ANY_TAG;
	    status->MPI_SOURCE = MPI_ANY_SOURCE;
	    status->MPI_ERROR  = MPI_SUCCESS;
	    MPID_ZERO_STATUS_COUNT(status);
	}
	*flag              = 1;
        *index             = MPI_UNDEFINED;
	TR_POP;
	return MPI_SUCCESS;
	}
    *flag = found;
    
    if (mpi_errno) {
	return MPIR_ERROR(MPIR_COMM_WORLD, mpi_errno, myname );
	}
    TR_POP;
    return mpi_errno;
}

