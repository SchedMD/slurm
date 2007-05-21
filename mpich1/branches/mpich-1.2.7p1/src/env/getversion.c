/*
 *  $Id: getversion.c,v 1.8 2001/11/14 19:56:40 ashton Exp $
 *
 *  (C) 1997 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Get_version = PMPI_Get_version
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Get_version  MPI_Get_version
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Get_version as PMPI_Get_version
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
  MPI_Get_version - Gets the version of MPI

Output Parameters:
+ version - Major version of MPI (1 or 2)
- subversion - Minor version of MPI.  

Notes:
The defined values 'MPI_VERSION' and 'MPI_SUBVERSION' contain the same 
information.  This routine allows you to check that the library matches the 
version specified in the 'mpi.h' and 'mpif.h' files.

.N fortran
@*/
int MPI_Get_version( 
	int *version, 
	int *subversion )
{
    *version    = MPI_VERSION;
    *subversion = MPI_SUBVERSION;
    return MPI_SUCCESS;
}
