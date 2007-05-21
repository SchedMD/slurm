/*
 *  $Id: comm_testic.c,v 1.7 2001/11/14 19:54:22 ashton Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Comm_test_inter = PMPI_Comm_test_inter
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Comm_test_inter  MPI_Comm_test_inter
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Comm_test_inter as PMPI_Comm_test_inter
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

MPI_Comm_test_inter - Tests to see if a comm is an inter-communicator

Input Parameter:
. comm - communicator (handle) 

Output Parameter:
. flag - (logical) 

.N fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_COMM
.N MPI_ERR_ARG
@*/
int MPI_Comm_test_inter ( MPI_Comm comm, int *flag )
{
    int mpi_errno = MPI_SUCCESS;
    struct MPIR_COMMUNICATOR *comm_ptr;
    static char myname[] = "MPI_COMM_TEST_INTER";

    comm_ptr = MPIR_GET_COMM_PTR(comm);
#ifndef MPIR_NO_ERROR_CHECKING
    MPIR_TEST_MPI_COMM(comm,comm_ptr,comm_ptr,myname);
    MPIR_TEST_ARG(flag);
    if (mpi_errno)
	return MPIR_ERROR( comm_ptr, mpi_errno, myname );
#endif
  
    *flag = (comm_ptr->comm_type == MPIR_INTER);

    return (mpi_errno);
}
