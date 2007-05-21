/*
 *  $Id: keyvalcreate.c,v 1.6 2001/11/14 19:54:28 ashton Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Keyval_create = PMPI_Keyval_create
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Keyval_create  MPI_Keyval_create
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Keyval_create as PMPI_Keyval_create
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

MPI_Keyval_create - Generates a new attribute key

Input Parameters:
. copy_fn - Copy callback function for 'keyval' 
. delete_fn - Delete callback function for 'keyval' 
. extra_state - Extra state for callback functions 

Output Parameter:
. keyval - key value for future access (integer) 

Notes:
Key values are global (available for any and all communicators).

There are subtle differences between C and Fortran that require that the
copy_fn be written in the same language that 'MPI_Keyval_create'
is called from.
This should not be a problem for most users; only programers using both 
Fortran and C in the same program need to be sure that they follow this rule.

.N fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_EXHAUSTED
.N MPI_ERR_ARG
@*/
int MPI_Keyval_create ( 
	MPI_Copy_function *copy_fn, 
	MPI_Delete_function *delete_fn, 
	int *keyval, 
	void *extra_state )
{
    *keyval = 0;
    return MPIR_Keyval_create( copy_fn, delete_fn, keyval, extra_state, 0 );
}

