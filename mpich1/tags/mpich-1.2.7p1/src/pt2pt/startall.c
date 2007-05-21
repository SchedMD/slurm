/*
 *  $Id: startall.c,v 1.8 2001/11/14 20:10:03 ashton Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Startall = PMPI_Startall
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Startall  MPI_Startall
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Startall as PMPI_Startall
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
  MPI_Startall - Starts a collection of requests 

Input Parameters:
+ count - list length (integer) 
- array_of_requests - array of requests (array of handle) 

.N fortran
@*/
int MPI_Startall( int count, MPI_Request array_of_requests[] )
{
    int i;
    int mpi_errno;
    static char myname[] = "MPI_STARTALL";
    MPIR_ERROR_DECL;

    TR_PUSH(myname);

    MPIR_ERROR_PUSH(MPIR_COMM_WORLD);
    for (i=0; i<count; i++) {
	MPIR_CALL_POP(MPI_Start( array_of_requests + i ),
		      MPIR_COMM_WORLD,myname);
    }

    MPIR_ERROR_POP(MPIR_COMM_WORLD);
    TR_POP;
    return MPI_SUCCESS;
}
