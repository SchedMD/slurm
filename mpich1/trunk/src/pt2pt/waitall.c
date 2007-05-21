/*
 *  $Id: waitall.c,v 1.25 2004/02/24 15:09:13 gropp Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */


#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Waitall = PMPI_Waitall
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Waitall  MPI_Waitall
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Waitall as PMPI_Waitall
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

/*@
    MPI_Waitall - Waits for all given communications to complete

Input Parameters:
+ count - lists length (integer) 
- array_of_requests - array of requests (array of handles) 

Output Parameter:
. array_of_statuses - array of status objects (array of Status).  May be
  'MPI_STATUSES_IGNORE'

.N waitstatus

.N fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_REQUEST
.N MPI_ERR_ARG
.N MPI_ERR_IN_STATUS
.N MPI_ERR_PENDING
@*/
int MPI_Waitall(
	int count, 
	MPI_Request array_of_requests[], 
	MPI_Status array_of_statuses[] )
{
    int i;
    MPI_Request request;
    int mpi_errno = MPI_SUCCESS, rc = MPI_SUCCESS;
    static char myname[] = "MPI_WAITALL";

/* There was some concern about waiting "inorder" here.  Since the underlying 
   ADI code processes all requests as they arrive, this should not matter.
   The code examples/test/pt2pt/waitall2.c tests for this. */
#define OLD_WAITALL
#ifdef OLD_WAITALL
    /* NOTE:
       This implementation will not work correctly if the device requires
       messages to be received in some particular order.  In that case, 
       this routine needs to try and complete the messages in ANY order.
       In particular, many systems need the sends completed before the
       receives.

       The same is true for testall.c .
     */
    /*
     * Note on the ordering of operations:
     * While this routine requests the device to complete the requests in 
     * a certain order (the order in which the requests are listed in the
     * array), requests must be completed in the order ISSUED.  The
     * test examples/test/pt2pt/waitall checks for this.
     */
    /* Process all pending sends... */
    for (i = 0; i < count; i++)
    {
        /* Skip over null handles.  We need this for handles generated
           when MPI_PROC_NULL is the source or destination of an operation */
        request = array_of_requests[i];
	/*FPRINTF( stderr, "[%d] processing request %d = %x\n", MPIR_tid, i, 
		 request ); */
        if (!request) {
	    /* See MPI Standard, 3.7 */
	    if (array_of_statuses) {
		/* This relies on using NULL for MPI_STATUS_IGNORE
		   and MPI_STATUSES_IGNORE */
		array_of_statuses[i].MPI_TAG    = MPI_ANY_TAG;
		array_of_statuses[i].MPI_SOURCE = MPI_ANY_SOURCE;
		array_of_statuses[i].MPI_ERROR  = MPI_SUCCESS;
		array_of_statuses[i].count	    = 0;
	    }
	    continue;
	    }

	if ( request->handle_type == MPIR_SEND ) {
	    if (MPID_SendRequestCancelled(request)) {
		if (array_of_statuses) {
		    array_of_statuses[i].MPI_TAG   = MPIR_MSG_CANCELLED; 
		    array_of_statuses[i].MPI_ERROR = MPI_SUCCESS;
		}
	    }
	    else {
		rc = MPI_SUCCESS;
		MPID_SendComplete( request, &rc );
		if (rc) {
		    if (array_of_statuses) {
			MPIR_Set_Status_error_array( array_of_requests, count, 
						    i, rc, array_of_statuses );
		    }
		    mpi_errno = MPI_ERR_IN_STATUS;
		    MPIR_RETURN(MPIR_COMM_WORLD, mpi_errno, myname );
		}
		MPIR_FORGET_SEND( &request->shandle );
		MPID_SendFree( array_of_requests[i] );
		array_of_requests[i]    = 0;
	    }
	}
	else if (request->handle_type == MPIR_PERSISTENT_SEND) {
	    if (!request->persistent_shandle.active) {
                /* See MPI Standard, 3.7 */
		if (MPID_SendRequestCancelled(&request->persistent_shandle)) {
		    if (array_of_statuses) {
			array_of_statuses[i].MPI_TAG = MPIR_MSG_CANCELLED;
		    }
		}
		else {
		    if (array_of_statuses) {
			array_of_statuses[i].MPI_TAG = MPI_ANY_TAG;
		    }
		}
		if (array_of_statuses) {
		    array_of_statuses[i].MPI_SOURCE	= MPI_ANY_SOURCE;
		    array_of_statuses[i].MPI_ERROR	= MPI_SUCCESS;
		    array_of_statuses[i].count	= 0;
		}
		continue;
	    }
	    rc = MPI_SUCCESS;
	    MPID_SendComplete( request, &rc );
	    if (rc) {
		if (array_of_statuses) {
		    MPIR_Set_Status_error_array( array_of_requests, count, i, 
						 rc, array_of_statuses );
		}
		mpi_errno = MPI_ERR_IN_STATUS;
		MPIR_RETURN(MPIR_COMM_WORLD, mpi_errno, myname );
	    }
	    request->persistent_shandle.active = 0;
	}
    }

    /* Process all pending receives... */
    for (i = 0; i < count; i++)
    {
        /* Skip over null handles.  We need this for handles generated
           when MPI_PROC_NULL is the source or destination of an operation
           Note that the send loop has already set the status array entries */
        request = array_of_requests[i];
	/*FPRINTF( stderr, "[%d] processing request %d = %x\n", MPIR_tid, i, 
		 request );*/
        if (!request) continue;

	if ( request->handle_type == MPIR_RECV ) {
	    /*FPRINTF( stderr, "[%d] receive request %d\n", MPIR_tid, i );*/
	    /* Old code does test first */
	    if (request->rhandle.s.MPI_TAG == MPIR_MSG_CANCELLED) {
		if (array_of_statuses) {
		    array_of_statuses[i].MPI_TAG = MPIR_MSG_CANCELLED;
		}
		MPID_RecvFree( array_of_requests[i] );
		array_of_requests[i] = 0; 
	    }
	    else {
		MPI_Status *status_p;
		rc = MPI_SUCCESS;

		if (array_of_statuses) status_p = &array_of_statuses[i];
		else                   status_p = 0;
		/* MPID_RecvComplete can handle a null status */
		MPID_RecvComplete( request, status_p, &rc );
		if (rc) {
		    if (array_of_statuses) {
			MPIR_Set_Status_error_array( array_of_requests, count,
						    i, rc, array_of_statuses );
		    }
		    mpi_errno = MPI_ERR_IN_STATUS;
		    MPIR_RETURN(MPIR_COMM_WORLD, mpi_errno, myname );
		}
		MPID_RecvFree( array_of_requests[i] );
		array_of_requests[i] = 0;
	    }
	}
	else if (request->handle_type == MPIR_PERSISTENT_RECV) {
	    MPI_Status *status_p;
	    if (!request->persistent_rhandle.active) {
		/* Thanks to mechi@terra.co.il for this fix */
		if (array_of_statuses) {
		    if (request->persistent_rhandle.rhandle.s.MPI_TAG ==
			MPIR_MSG_CANCELLED) 
			array_of_statuses[i].MPI_TAG = MPIR_MSG_CANCELLED;
		    else
			array_of_statuses[i].MPI_TAG    = MPI_ANY_TAG;
		    
		    array_of_statuses[i].MPI_SOURCE = MPI_ANY_SOURCE;
		    array_of_statuses[i].MPI_ERROR  = MPI_SUCCESS;
		    array_of_statuses[i].count	    = 0;
		}
		continue;
	    }
	    rc = MPI_SUCCESS;

	    if (array_of_statuses) status_p = &array_of_statuses[i];
	    else                   status_p = 0;
	    MPID_RecvComplete( request, status_p, &rc );
	    if (rc) {
		if (array_of_statuses) {
		    MPIR_Set_Status_error_array( array_of_requests, count, i, 
						 rc, array_of_statuses );
		}
		mpi_errno = MPI_ERR_IN_STATUS;
		MPIR_RETURN(MPIR_COMM_WORLD, mpi_errno, myname );
	    }
	    request->persistent_rhandle.active = 0;
	}
    }

#else
    /* The only thing that complicates using MPI_Waitany here is the
       error handling */
    {
	int i, index;
	MPI_Status status;

	for (i=0; i<count; i++) {
	    rc = MPI_Waitany( count, array_of_requests, &index, &status );
	    PRINTF( "Found index = %d\n", index );
	    /* If all requests are MPI_REQUEST_NULL, then index is
	       MPI_UNDEFINED */
	    if (array_of_statuses && index != MPI_UNDEFINED) {
		array_of_statuses[index] = status;
	    }
	    if (rc) {
		if (array_of_statuses) {
		    MPIR_Set_Status_error_array( array_of_requests, count, i, 
						 rc, array_of_statuses );
		}
		mpi_errno = MPI_ERR_IN_STATUS;
		MPIR_RETURN(MPIR_COMM_WORLD, mpi_errno, myname );
	    }
	}
    }
#endif
    
    if (mpi_errno) {
	return MPIR_ERROR(MPIR_COMM_WORLD, mpi_errno, myname);
	}
    return mpi_errno;
}
