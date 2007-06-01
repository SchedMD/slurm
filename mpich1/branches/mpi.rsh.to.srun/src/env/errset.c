/*
 *  $Id: errset.c,v 1.8 2001/11/14 19:56:39 ashton Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Errhandler_set = PMPI_Errhandler_set
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Errhandler_set  MPI_Errhandler_set
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Errhandler_set as PMPI_Errhandler_set
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
  MPI_Errhandler_set - Sets the error handler for a communicator

Input Parameters:
+ comm - communicator to set the error handler for (handle) 
- errhandler - new MPI error handler for communicator (handle) 

.N fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_COMM
.N MPI_ERR_ARG
@*/
int MPI_Errhandler_set( MPI_Comm comm, MPI_Errhandler errhandler )
{
    struct MPIR_COMMUNICATOR *comm_ptr;
    struct MPIR_Errhandler *old;
    static char myname[] = "MPI_ERRHANDLER_SET";
    int mpi_errno = MPI_SUCCESS;

    TR_PUSH(myname);

    comm_ptr = MPIR_GET_COMM_PTR(comm);
    MPIR_TEST_MPI_COMM(comm,comm_ptr,comm_ptr,myname);

    old = MPIR_GET_ERRHANDLER_PTR( errhandler );
#ifndef MPIR_NO_ERROR_CHECKING
    MPIR_TEST_ERRHANDLER(old);
    if (mpi_errno)
	return MPIR_ERROR(comm_ptr, mpi_errno, myname );
#endif
    
    MPIR_REF_INCR(old);

    if (comm_ptr->error_handler) 
	MPI_Errhandler_free( &comm_ptr->error_handler );
    comm_ptr->error_handler = errhandler;

    TR_POP;
    return MPI_SUCCESS;
}
