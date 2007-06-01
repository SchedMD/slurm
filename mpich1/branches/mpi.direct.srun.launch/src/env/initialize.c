/*
 *  $Id: initialize.c,v 1.6 2001/11/14 19:56:41 ashton Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Initialized = PMPI_Initialized
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Initialized  MPI_Initialized
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Initialized as PMPI_Initialized
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
   MPI_Initialized - Indicates whether 'MPI_Init' has been called.

Output Parameter:
. flag - Flag is true if 'MPI_Init' has been called and false otherwise. 

.N fortran
@*/
int MPI_Initialized( int *flag )
{
/* 
   MPI_Init sets MPIR_Has_been_initialized to 1, MPI_Finalize sets to 2.
   Currently, there is no way to determine, if MPI_Finalize has been called,
   other than trapping references to free'd memory.  Perhaps we should set
   MPI_COMM_WORLD to 0 after freeing it?
 */
    *flag = MPIR_Has_been_initialized > 0;
    return MPI_SUCCESS;
}
