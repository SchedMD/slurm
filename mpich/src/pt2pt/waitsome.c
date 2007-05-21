/*
 *  $Id: waitsome.c,v 1.15 2005/08/15 17:20:47 gropp Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */


#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Waitsome = PMPI_Waitsome
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Waitsome  MPI_Waitsome
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Waitsome as PMPI_Waitsome
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
    MPI_Waitsome - Waits for some given communications to complete

Input Parameters:
+ incount - length of array_of_requests (integer) 
- array_of_requests - array of requests (array of handles) 

Output Parameters:
+ outcount - number of completed requests (integer) 
. array_of_indices - array of indices of operations that 
completed (array of integers) 
- array_of_statuses - array of status objects for 
    operations that completed (array of Status).  May be 'MPI_STATUSES_IGNORE'.

Notes:
  The array of indicies are in the range '0' to 'incount - 1' for C and 
in the range '1' to 'incount' for Fortran.  

Null requests are ignored; if all requests are null, then the routine
returns with 'outcount' set to 'MPI_UNDEFINED'.

.N waitstatus

.N fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_REQUEST
.N MPI_ERR_ARG
.N MPI_ERR_IN_STATUS
@*/
int MPI_Waitsome( 
	int incount, 
	MPI_Request array_of_requests[], 
	int *outcount, 
	int array_of_indices[], 
	MPI_Status array_of_statuses[] )
{
    int i, j, mpi_errno = MPI_SUCCESS;
    MPI_Request request;
    int nnull, mpi_lerr;
    int nfound = 0;
    static char myname[] = "MPI_WAITSOME";

    TR_PUSH(myname);

    /* NOTE:
       This implementation will not work correctly if the device requires
       messages to be received in some particular order.  In that case, 
       this routine needs to try and complete the messages in ANY order.
       
       The same is true for waitall.c .
     */
    nnull = 0;
    MPID_DeviceCheck( MPID_NOTBLOCKING );
    while (nfound == 0 && nnull < incount ) {
	nnull = 0;
	for (i = 0; i < incount; i++) {
	    /* Skip over null handles.  We need this for handles generated
	       when MPI_PROC_NULL is the source or destination of an 
	       operation */
	    request = array_of_requests[i];

	    if (!request) {/*  || !request->chandle.active) { */
		nnull ++;
		continue;
	    }

	    mpi_lerr = 0;
	    switch (request->handle_type) {
	    case MPIR_SEND:
		if (MPID_SendRequestCancelled(request)) {
		    if (array_of_statuses) {
			array_of_statuses[i].MPI_TAG = MPIR_MSG_CANCELLED; 
			array_of_statuses[i].MPI_ERROR = MPI_SUCCESS;
		    }
		    nfound++;
		}
		else {
		    if (request->shandle.is_complete || 
			MPID_SendIcomplete( request, &mpi_lerr )) {
			array_of_indices[nfound] = i;
			if (mpi_lerr) {
			    if (mpi_errno == MPI_SUCCESS) {
				if (array_of_statuses) {
				    for (j=0; j<incount; j++) 
					array_of_statuses[j].MPI_ERROR = MPI_SUCCESS;
				}
				mpi_errno = MPI_ERR_IN_STATUS;
			    }
			    if (array_of_statuses)
				array_of_statuses[nfound].MPI_ERROR = mpi_lerr;
			}
			MPIR_FORGET_SEND( &request->shandle );
			MPID_SendFree( request );
			array_of_requests[i] = 0;
			nfound++;
		    }
		}
		break;
	    case MPIR_RECV:
		if (request->rhandle.s.MPI_TAG == MPIR_MSG_CANCELLED) {
		    if (array_of_statuses) 
			array_of_statuses[i].MPI_TAG = MPIR_MSG_CANCELLED;
		    MPID_RecvFree( array_of_requests[i] );
		    array_of_requests[i] = 0; 
		    nfound++;
		}
		else {
		    if (request->rhandle.is_complete || 
			MPID_RecvIcomplete( request, (MPI_Status *)0, 
					    &mpi_lerr )) {
			array_of_indices[nfound]  = i;
			if (request->rhandle.s.MPI_ERROR) {
			    if (mpi_errno == MPI_SUCCESS) {
				if (array_of_statuses) {
				    for (j=0; j<incount; j++) 
					array_of_statuses[j].MPI_ERROR = MPI_SUCCESS;
				}
				mpi_errno = MPI_ERR_IN_STATUS;
			    }
			}
			if (array_of_statuses) 
			    array_of_statuses[nfound] = request->rhandle.s;
			MPID_RecvFree( request );
			array_of_requests[i] = 0;
			nfound++;
		    }
		}
		break;
	    case MPIR_PERSISTENT_SEND:
		if (!request->persistent_shandle.active) {
		    if (MPID_SendRequestCancelled(&request->persistent_shandle)) {
			if (array_of_statuses) 
			    array_of_statuses[i].MPI_TAG = MPIR_MSG_CANCELLED;
			nfound++;
		    }
		    else
			nnull++;
		}
		else if (request->persistent_shandle.shandle.is_complete ||
			 MPID_SendIcomplete( request, &mpi_lerr )) {
		    array_of_indices[nfound] = i;
		    if (mpi_lerr) {
			if (mpi_errno == MPI_SUCCESS) {
			    if (array_of_statuses) {
				for (j=0; j<incount; j++) 
				    array_of_statuses[j].MPI_ERROR = MPI_SUCCESS;
			    }
			    mpi_errno = MPI_ERR_IN_STATUS;
			}
			if (array_of_statuses) 
			    array_of_statuses[nfound].MPI_ERROR = mpi_lerr;
		    }
		    request->persistent_shandle.active = 0;
		    nfound++;
		}
		break;
	    case MPIR_PERSISTENT_RECV:
		if (!request->persistent_rhandle.active) {
		    if (request->persistent_rhandle.rhandle.s.MPI_TAG ==
			MPIR_MSG_CANCELLED) {
			if (array_of_statuses) 
			    array_of_statuses[i].MPI_TAG = MPIR_MSG_CANCELLED;
			nfound++;
		    }
		    else
			nnull++;
		}
		else if (request->persistent_rhandle.rhandle.is_complete ||
			 MPID_RecvIcomplete( request, (MPI_Status *)0, 
					     &mpi_lerr )) {
		    array_of_indices[nfound] = i;
		    if (mpi_lerr) {
			if (mpi_errno == MPI_SUCCESS) {
			    if (array_of_statuses) {
				for (j=0; j<incount; j++)
				    array_of_statuses[j].MPI_ERROR = MPI_SUCCESS;
			    }
			    mpi_errno = MPI_ERR_IN_STATUS;
			}
		    }
		    if (array_of_statuses)
			array_of_statuses[nfound] = 
			    request->persistent_rhandle.rhandle.s;
		    request->persistent_rhandle.active = 0;
		    nfound++;
		}
		break;
	    }
	}
	if (nfound == 0 && nnull < incount) 
	    MPID_DeviceCheck( MPID_BLOCKING );
    }
    if (nnull == incount)
	*outcount = MPI_UNDEFINED;
    else
	*outcount = nfound;
    if (mpi_errno) {
	return MPIR_ERROR(MPIR_COMM_WORLD, mpi_errno, myname);
	}
    TR_POP;
    return mpi_errno;
}

