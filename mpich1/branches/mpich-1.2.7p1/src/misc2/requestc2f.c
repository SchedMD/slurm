/*
 *  $Id: requestc2f.c,v 1.4 1999/08/30 15:47:50 swider Exp $
 *
 *  (C) 1997 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Request_c2f = PMPI_Request_c2f
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Request_c2f  MPI_Request_c2f
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Request_c2f as PMPI_Request_c2f
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
  MPI_Request_c2f - Convert a C request to a Fortran request

Input Parameters:
. c_request - Request value in C (handle)

Output Value:
. f_request - Status value in Fortran (Integer)
  
.N Errors
.N MPI_SUCCESS
.N MPI_ERR_ARG
@*/
MPI_Fint MPI_Request_c2f( c_request )
MPI_Request  c_request;
{
    MPI_Fint f_request;
    
    if (c_request == MPI_REQUEST_NULL) return 0;

    /* If we've registered this request, return the current value */
    if (c_request->chandle.self_index)
	return c_request->chandle.self_index;
    f_request = MPIR_FromPointer( c_request );
    c_request->chandle.self_index = f_request;
    return f_request;
}
