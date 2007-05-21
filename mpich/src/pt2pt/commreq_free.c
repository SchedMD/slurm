/*
 *  $Id: commreq_free.c,v 1.9 2001/11/14 20:09:56 ashton Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Request_free = PMPI_Request_free
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Request_free  MPI_Request_free
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Request_free as PMPI_Request_free
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
/* pt2pt for MPIR_Type_free */
#include "mpipt2pt.h"

/*@
    MPI_Request_free - Frees a communication request object

Input Parameter:
. request - communication request (handle) 

Notes:
This routine is normally used to free persistent requests created with 
either 'MPI_Recv_init' or 'MPI_Send_init' and friends.  However, it can be 
used to free a request created with 'MPI_Irecv' or 'MPI_Isend' and friends;
in that case the use can not use the test/wait routines on the request.

It `is` permitted to free an active request.  However, once freed, you can not
use the request in a wait or test routine (e.g., 'MPI_Wait').

.N fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_REQUEST
.N MPI_ERR_ARG

.see also: MPI_Isend, MPI_Irecv, MPI_Issend, MPI_Ibsend, MPI_Irsend,
MPI_Recv_init, MPI_Send_init, MPI_Ssend_init, MPI_Rsend_init, MPI_Wait,
MPI_Test, MPI_Waitall, MPI_Waitany, MPI_Waitsome, MPI_Testall, MPI_Testany,
MPI_Testsome
@*/
int MPI_Request_free( MPI_Request *request )
{
    int mpi_errno = MPI_SUCCESS;
    MPI_Request rq;
    static char myname[] = "MPI_REQUEST_FREE";

    TR_PUSH(myname);

    MPIR_TEST_ARG(request);
    if (mpi_errno) 
	return MPIR_ERROR(MPIR_COMM_WORLD,mpi_errno, myname );
    if (MPIR_TEST_REQUEST(MPI_COMM_WORLD,*request))
	return MPIR_ERROR(MPIR_COMM_WORLD,mpi_errno, myname );

    rq = *request;
    switch( rq->handle_type) {
    case MPIR_SEND:
	if (rq->shandle.is_complete) {
	    MPIR_FORGET_SEND( &rq->shandle );
	    MPID_SendFree( rq );
	}
	else {
	    MPID_Request_free( rq );
	}
	*request = 0;
	break;
    case MPIR_RECV:
	if (rq->rhandle.is_complete) {
	    MPID_RecvFree( rq );
	}
	else {
	    MPID_Request_free( rq );
	}
	*request = 0;
	break;
    case MPIR_PERSISTENT_SEND:
	if (!rq->persistent_shandle.active) {
	    /* Must also free references to perm objects */
	    MPI_Comm ctmp = rq->persistent_shandle.perm_comm->self;
	    MPIR_Type_free( &rq->persistent_shandle.perm_datatype );
	    MPI_Comm_free( &ctmp );
	    MPID_PSendFree( rq );
	}
	else {
	    MPID_Request_free( rq );
	}
	*request = 0;
	break;
    case MPIR_PERSISTENT_RECV:
	if (rq->persistent_rhandle.active) {
	    /* Catch the case of a cancelled request (complete but not 
	       finished, and hence still active) */
		if (rq->persistent_rhandle.rhandle.is_complete &&
		    rq->persistent_rhandle.rhandle.s.MPI_TAG ==
		    MPIR_MSG_CANCELLED) 
		    rq->persistent_rhandle.active = 0;
	}
	if (!rq->persistent_rhandle.active) {
	    MPI_Comm ctmp = rq->persistent_rhandle.perm_comm->self;
	    MPIR_Type_free( &rq->persistent_rhandle.perm_datatype );
	    MPI_Comm_free( &ctmp );
	    MPID_PRecvFree( rq );
	}
	else {
	    MPID_Request_free( rq );
	}
	*request = 0;
	break;
    }
    TR_POP;
    return mpi_errno;
}

