/*
 *  $Id: comm_size.c,v 1.7 2001/11/14 19:54:21 ashton Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Comm_size = PMPI_Comm_size
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Comm_size  MPI_Comm_size
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Comm_size as PMPI_Comm_size
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

MPI_Comm_size - Determines the size of the group associated with a communictor

Input Parameter:
. comm - communicator (handle) 

Output Parameter:
. size - number of processes in the group of 'comm'  (integer) 

Notes:
   'MPI_COMM_NULL' is `not` considered a valid argument to this function.

.N fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_COMM
.N MPI_ERR_ARG
@*/
int MPI_Comm_size ( MPI_Comm comm, int *size )
{
    int mpi_errno = MPI_SUCCESS;
    struct MPIR_COMMUNICATOR *comm_ptr;
    static char myname[] = "MPI_COMM_SIZE";

    comm_ptr = MPIR_GET_COMM_PTR(comm);

#ifndef MPIR_NO_ERROR_CHECKING
    MPIR_TEST_MPI_COMM(comm,comm_ptr,comm_ptr,myname );
    MPIR_TEST_ARG(size);
    if (mpi_errno)
	return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
#endif
    (*size) = comm_ptr->local_group->np;

    return (MPI_SUCCESS);
}
