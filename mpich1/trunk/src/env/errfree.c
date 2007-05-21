/*
 *  $Id: errfree.c,v 1.8 2001/11/14 19:56:38 ashton Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Errhandler_free = PMPI_Errhandler_free
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Errhandler_free  MPI_Errhandler_free
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Errhandler_free as PMPI_Errhandler_free
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPI_BUILD_PROFILING
#include "mpiprof.h"
/* Insert the prototypes for the PMPI routines */
#undef __MPI_BINDINGS
#include "binding.h"
#endif
#include "sbcnst2.h"
#define MPIR_SBfree MPID_SBfree

/*@
  MPI_Errhandler_free - Frees an MPI-style errorhandler

Input Parameter:
. errhandler - MPI error handler (handle).  Set to 'MPI_ERRHANDLER_NULL' on 
exit.

.N fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_ARG
@*/
int MPI_Errhandler_free( MPI_Errhandler *errhandler )
{
    struct MPIR_Errhandler *old;
    static char myname[] = "MPI_ERRHANDLER_FREE";
    int mpi_errno = MPI_SUCCESS;

    TR_PUSH(myname);

    old = MPIR_GET_ERRHANDLER_PTR(*errhandler);
#ifndef MPIR_NO_ERROR_CHECKING
    MPIR_TEST_ERRHANDLER(old);
    if (mpi_errno)
	return MPIR_ERROR(MPIR_COMM_WORLD, mpi_errno, myname );
#endif

    MPIR_REF_DECR(old);
    if (old->ref_count <= 0) {
	MPIR_CLR_COOKIE(old);
	MPIR_SBfree ( MPIR_errhandlers, old );
	MPIR_RmPointer( *errhandler );
	}

    *errhandler = MPI_ERRHANDLER_NULL;
    TR_POP;
    return MPI_SUCCESS;
}
