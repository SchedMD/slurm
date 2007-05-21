/*
 *  $Id: wtick.c,v 1.8 2004/08/20 21:37:15 gropp Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */


#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Wtick = PMPI_Wtick
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Wtick  MPI_Wtick
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Wtick as PMPI_Wtick
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPI_BUILD_PROFILING
#include "mpiprof.h"
/* Insert the prototypes for the PMPI routines */
#undef __MPI_BINDINGS
#include "binding.h"
#endif
#include "mpid_time.h"

/*@
  MPI_Wtick - Returns the resolution of MPI_Wtime

  Return value:
  Time in seconds of the resolution of MPI_Wtime .

  Notes for Fortran:
  This is a function, declared as 'DOUBLE PRECISION MPI_WTICK()' in Fortran.
  
@*/
double MPI_Wtick()
{
    double t1;
    MPID_Wtick( &t1 );
    return t1;
}
