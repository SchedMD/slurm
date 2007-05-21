/*
 *  $Id: test.c,v 1.10 2003/01/09 20:48:41 gropp Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Test = PMPI_Test
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Test  MPI_Test
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Test as PMPI_Test
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
    MPI_Test  - Tests for the completion of a send or receive

Input Parameter:
. request - communication request (handle) 

Output Parameter:
+ flag - true if operation completed (logical) 
- status - status object (Status).  May be 'MPI_STATUS_IGNORE'.

.N waitstatus

.N fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_REQUEST
.N MPI_ERR_ARG
@*/
int MPI_Test ( 
	MPI_Request  *request,
	int          *flag,
	MPI_Status   *status)
{
    int mpi_errno;
    MPIR_ERROR_DECL;
    MPI_Status s;

    MPIR_ERROR_PUSH(MPIR_COMM_WORLD);
    /* We let Testall detect errors */
    if (status) {
	mpi_errno = MPI_Testall( 1, request, flag, status );
    }
    else {
	mpi_errno = MPI_Testall( 1, request, flag, &s );
    }
    MPIR_ERROR_POP(MPIR_COMM_WORLD);
    if (mpi_errno == MPI_ERR_IN_STATUS) {
	if (status) 
	    mpi_errno = status->MPI_ERROR;
	else
	    mpi_errno = s.MPI_ERROR;
    }
    MPIR_RETURN(MPIR_COMM_WORLD, mpi_errno, "MPI_TEST" );
}
