/*
 *  $Id: opfree.c,v 1.8 2001/11/14 19:50:13 ashton Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Op_free = PMPI_Op_free
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Op_free  MPI_Op_free
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Op_free as PMPI_Op_free
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPI_BUILD_PROFILING
#include "mpiprof.h"
/* Insert the prototypes for the PMPI routines */
#undef __MPI_BINDINGS
#include "binding.h"
#endif
#include "mpimem.h"
#include "mpiops.h"

/*@
  MPI_Op_free - Frees a user-defined combination function handle

Input Parameter:
. op - operation (handle) 

Notes:
'op' is set to 'MPI_OP_NULL' on exit.

.N NULL

.N fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_ARG
.N MPI_ERR_PERM_OP

.seealso: MPI_Op_create
@*/
int MPI_Op_free( MPI_Op *op )
{
    int mpi_errno = MPI_SUCCESS;
    struct MPIR_OP *old;
    static char myname[] = "MPI_OP_FREE";

#ifndef MPIR_NO_ERROR_CHECKING
    /* Freeing a NULL op should not return successfully */
    MPIR_TEST_ARG(op);
    if ( (*op) == MPI_OP_NULL ) {
	mpi_errno = MPIR_ERRCLASS_TO_CODE(MPI_ERR_OP,MPIR_ERR_OP_NULL);
    }
    if (mpi_errno)
	return MPIR_ERROR(MPIR_COMM_WORLD, mpi_errno, myname );
#endif

    old = MPIR_GET_OP_PTR( *op );
    MPIR_TEST_MPI_OP(*op,old,MPIR_COMM_WORLD,myname);

    /* We can't free permanent objects unless finalize has been called */
    if  ( ( old->permanent == 1 ) && (MPIR_Has_been_initialized == 1) )
	return MPIR_ERROR( MPIR_COMM_WORLD, 
	   MPIR_ERRCLASS_TO_CODE(MPI_ERR_ARG,MPIR_ERR_PERM_OP),myname );
    MPIR_CLR_COOKIE(old);
    FREE( old );
    MPIR_RmPointer( *op );

    (*op) = MPI_OP_NULL;

    TR_POP;
    return (MPI_SUCCESS);
}
