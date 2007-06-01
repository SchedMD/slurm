/*
 *  $Id: errclass.c,v 1.7 2001/11/14 19:56:37 ashton Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */


#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Error_class = PMPI_Error_class
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Error_class  MPI_Error_class
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Error_class as PMPI_Error_class
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
   MPI_Error_class - Converts an error code into an error class

Input Parameter:
. errorcode - Error code returned by an MPI routine 

Output Parameter:
. errorclass - Error class associated with 'errorcode'

.N fortran
@*/
int MPI_Error_class( 
	int errorcode, 
	int *errorclass)
{
    /* We could check for invalid error code here */
    *errorclass = errorcode & MPIR_ERR_CLASS_MASK;
    return MPI_SUCCESS;
}
