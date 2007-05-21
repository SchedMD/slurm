/*
 *  $Id: errget.c,v 1.9 2001/11/14 19:56:38 ashton Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Errhandler_get = PMPI_Errhandler_get
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Errhandler_get  MPI_Errhandler_get
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Errhandler_get as PMPI_Errhandler_get
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
  MPI_Errhandler_get - Gets the error handler for a communicator

Input Parameter:
. comm - communicator to get the error handler from (handle) 

Output Parameter:
. errhandler - MPI error handler currently associated with communicator
(handle) 

.N fortran

Note on Implementation:

The MPI Standard was unclear on whether this routine required the user to call 
'MPI_Errhandler_free' once for each call made to this routine in order to 
free the error handler.  After some debate, the MPI Forum added an explicit
statement that users are required to call 'MPI_Errhandler_free' when the
return value from this routine is no longer needed.  This behavior is similar
to the other MPI routines for getting objects; for example, 'MPI_Comm_group' 
requires that the user call 'MPI_Group_free' when the group returned
by 'MPI_Comm_group' is no longer needed.

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_COMM
.N MPI_ERR_ARG
@*/
int MPI_Errhandler_get( MPI_Comm comm, MPI_Errhandler *errhandler )
{
    struct MPIR_COMMUNICATOR *comm_ptr;
    static char myname[] = "MPI_ERRHANDLER_GET";
    int mpi_errno = MPI_SUCCESS;

    TR_PUSH(myname);
    comm_ptr = MPIR_GET_COMM_PTR(comm);
    MPIR_TEST_MPI_COMM(comm,comm_ptr,comm_ptr,myname);

    *errhandler = comm_ptr->error_handler;
#ifdef OLD_INTERP
#else
    /* A get creates a reference to an error handler; the user must 
       explicitly free this reference */
    MPIR_Errhandler_mark( *errhandler, 1 );
#endif
    
    TR_POP;
    return MPI_SUCCESS;
}
