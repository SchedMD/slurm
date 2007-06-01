/*
 *  $Id: errcreate.c,v 1.7 2001/11/14 19:56:38 ashton Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Errhandler_create = PMPI_Errhandler_create
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Errhandler_create  MPI_Errhandler_create
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Errhandler_create as PMPI_Errhandler_create
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
#define MPIR_SBalloc MPID_SBalloc

/*@
  MPI_Errhandler_create - Creates an MPI-style errorhandler

Input Parameter:
. function - user defined error handling procedure 

Output Parameter:
. errhandler - MPI error handler (handle) 

Notes:
The MPI Standard states that an implementation may make the output value 
(errhandler) simply the address of the function.  However, the action of 
'MPI_Errhandler_free' makes this impossible, since it is required to set the
value of the argument to 'MPI_ERRHANDLER_NULL'.  In addition, the actual
error handler must remain until all communicators that use it are 
freed.

.N fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_EXHAUSTED
@*/
int MPI_Errhandler_create( 
	MPI_Handler_function *function,
	MPI_Errhandler       *errhandler)
{
    struct MPIR_Errhandler *new;

    MPIR_ALLOC(new,(struct MPIR_Errhandler*) MPIR_SBalloc( MPIR_errhandlers ),
	       MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED, "MPI_ERRHANDLER_CREATE" );

    MPIR_SET_COOKIE(new,MPIR_ERRHANDLER_COOKIE);
    new->routine   = function;
    new->ref_count = 1;

    *errhandler = (MPI_Errhandler)MPIR_FromPointer( new );
    return MPI_SUCCESS;
}
