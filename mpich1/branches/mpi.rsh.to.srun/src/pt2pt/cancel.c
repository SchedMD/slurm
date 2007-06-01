/*
 *  $Id: cancel.c,v 1.10 2001/11/14 20:09:56 ashton Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Cancel = PMPI_Cancel
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Cancel  MPI_Cancel
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Cancel as PMPI_Cancel
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPI_BUILD_PROFILING
#include "mpiprof.h"
/* Insert the prototypes for the PMPI routines */
#undef __MPI_BINDINGS
#include "binding.h"
#endif

/*@
    MPI_Cancel - Cancels a communication request

Input Parameter:
. request - communication request (handle) 

Note:
Cancel has only been implemented for receive requests; it is a no-op for
send requests.  The primary expected use of MPI_Cancel is in multi-buffering
schemes, where speculative MPI_Irecvs are made.  When the computation 
completes, some of these receive requests may remain; using MPI_Cancel allows
the user to cancel these unsatisfied requests.  

Cancelling a send operation is much more difficult, in large part because the 
send will usually be at least partially complete (the information on the tag,
size, and source are usually sent immediately to the destination).  As of
version 1.2.0, MPICH supports cancelling of sends.  Users are
advised that cancelling a send, while a local operation (as defined by the MPI
standard), is likely to be expensive (usually generating one or more internal
messages). 

.N fortran

.N NULL

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_REQUEST
.N MPI_ERR_ARG
@*/
int MPI_Cancel( MPI_Request *request )
{
    static char myname[] = "MPI_CANCEL";
    int mpi_errno = MPI_SUCCESS;

    TR_PUSH(myname);

    MPIR_TEST_ARG(request);
    if (mpi_errno)
	return MPIR_ERROR(MPIR_COMM_WORLD,mpi_errno,myname );
    
    /* A null request requires no effort to cancel.  However, it
       is an error. */
    if (*request == MPI_REQUEST_NULL) 
	return MPIR_ERROR(MPIR_COMM_WORLD,
	  MPIR_ERRCLASS_TO_CODE(MPI_ERR_REQUEST,MPIR_ERR_REQUEST_NULL),myname);

    /* Check that the request is actually a request */
    if (MPIR_TEST_REQUEST(MPI_COMM_WORLD,*request))
	return MPIR_ERROR(MPIR_COMM_WORLD,mpi_errno,myname);
    
    switch ((*request)->handle_type) {
    case MPIR_SEND:
	MPID_SendCancel( *request, &mpi_errno );
	break;
    case MPIR_RECV:
	MPID_RecvCancel( *request, &mpi_errno );
	break;
    case MPIR_PERSISTENT_SEND:
	/* Only active persistent operations can be cancelled */
	if (!(*request)->persistent_shandle.active)
	    return MPIR_ERROR( MPIR_COMM_WORLD, MPI_ERR_REQUEST, myname );
	MPID_SendCancel( *request, &mpi_errno );
	break;
    case MPIR_PERSISTENT_RECV:
	/* Only active persistent operations can be cancelled */
	if (!(*request)->persistent_rhandle.active)
	    return MPIR_ERROR( MPIR_COMM_WORLD, MPI_ERR_REQUEST, myname );
	MPID_RecvCancel( *request, &mpi_errno );
	break;
    /* For user request, cast and call user cancel function */
    }

    TR_POP;
    /* Note that we should really use the communicator in the request,
       if available! */
    MPIR_RETURN( MPIR_COMM_WORLD, mpi_errno, myname );
}

