/*
 *  $Id: wait.c,v 1.9 2003/01/09 20:48:42 gropp Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */


#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Wait = PMPI_Wait
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Wait  MPI_Wait
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Wait as PMPI_Wait
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
    MPI_Wait  - Waits for an MPI send or receive to complete

Input Parameter:
. request - request (handle) 

Output Parameter:
. status - status object (Status) .  May be 'MPI_STATUS_IGNORE'.

.N waitstatus

.N fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_REQUEST
.N MPI_ERR_ARG
@*/
int MPI_Wait ( 
	MPI_Request  *request,
	MPI_Status   *status)
{
    int mpi_errno;
    MPIR_ERROR_DECL;

    MPIR_ERROR_PUSH(MPIR_COMM_WORLD);
    /* We'll let MPI_Waitall catch the errors */
    mpi_errno = MPI_Waitall( 1, request, status );

    MPIR_ERROR_POP(MPIR_COMM_WORLD);
    if (mpi_errno == MPI_ERR_IN_STATUS)
	mpi_errno = status->MPI_ERROR;

    MPIR_RETURN(MPIR_COMM_WORLD,mpi_errno,"MPI_WAIT");
}
