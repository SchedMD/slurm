/*
 *  $Id: start.c,v 1.7 2001/11/14 20:10:03 ashton Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Start = PMPI_Start
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Start  MPI_Start
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Start as PMPI_Start
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPI_BUILD_PROFILING
#include "mpiprof.h"
/* Insert the prototypes for the PMPI routines */
#undef __MPI_BINDINGS
#include "binding.h"
#endif

/* NOTES 
   We mark all sends and receives as non-blocking because that is safe here;
   unfortunately, we don't have enough information in the current send/recv
   handle to determine if we are blocking or not.
 */

/*@
    MPI_Start - Initiates a communication with a persistent request handle

Input Parameter:
. request - communication request (handle) 

.N fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_REQUEST

@*/
int MPI_Start( 
	MPI_Request *request)
{
    int mpi_errno = MPI_SUCCESS;
    static char myname[] = "MPI_START";
    MPIR_PSHANDLE *pshandle;
    MPIR_PRHANDLE *prhandle;
    
    TR_PUSH(myname);

    if (MPIR_TEST_REQUEST(MPI_COMM_WORLD,*request))
	return MPIR_ERROR(MPIR_COMM_WORLD, mpi_errno, myname );

    switch ((*request)->handle_type) {
    case MPIR_PERSISTENT_SEND:
	pshandle = &(*request)->persistent_shandle;
	if (pshandle->perm_dest == MPI_PROC_NULL) {
	    pshandle->active	          = 1;
	    pshandle->shandle.is_complete = 1;
	    TR_POP;
	    return MPI_SUCCESS;
	}
	if (pshandle->active) {
	    return MPIR_ERROR( MPIR_COMM_WORLD, MPI_ERR_REQUEST, myname );
	}
	/* Since there are many send modes, we save the routine to
	   call in the handle */
	(*pshandle->send)( pshandle->perm_comm, pshandle->perm_buf, 
			  pshandle->perm_count, pshandle->perm_datatype, 
			  pshandle->perm_comm->local_rank, 
			  pshandle->perm_tag, 
			  pshandle->perm_comm->send_context, 
			  pshandle->perm_comm->lrank_to_grank[
			      pshandle->perm_dest], 
			  *request, &mpi_errno );
	if (mpi_errno) 
	    return MPIR_ERROR( pshandle->perm_comm, mpi_errno, myname );
	pshandle->active	 = 1;
	break;
    case MPIR_PERSISTENT_RECV:
	prhandle = &(*request)->persistent_rhandle;
	if (prhandle->perm_source == MPI_PROC_NULL) {
	    prhandle->active		   = 1;
	    prhandle->rhandle.is_complete  = 1;
	    prhandle->rhandle.s.MPI_TAG	   = MPI_ANY_TAG;
	    prhandle->rhandle.s.MPI_SOURCE = MPI_PROC_NULL;
	    prhandle->rhandle.s.count	   = 0;
	    TR_POP;
	    return MPI_SUCCESS;
	}
	if (prhandle->active) {
	    return MPIR_ERROR( MPIR_COMM_WORLD, MPI_ERR_REQUEST, myname );
	}
	MPID_IrecvDatatype( prhandle->perm_comm, prhandle->perm_buf, 
			    prhandle->perm_count, prhandle->perm_datatype, 
			    prhandle->perm_source, prhandle->perm_tag, 
			    prhandle->perm_comm->recv_context, 
			    *request, &mpi_errno );
	if (mpi_errno) 
	    return MPIR_ERROR( prhandle->perm_comm, mpi_errno, myname );
	prhandle->active = 1;
	break;
    default:
	return MPIR_ERROR(MPIR_COMM_WORLD,MPI_ERR_REQUEST,myname );
    }
    TR_POP;
    return mpi_errno;
}


