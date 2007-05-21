/*
 *  $Id: int2handle.c,v 1.4 1999/08/30 15:54:07 swider Exp $
 *
 *  (C) 1996 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Int2handle = PMPI_Int2handle
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Int2handle  MPI_Int2handle
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Int2handle as PMPI_Int2handle
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPI_BUILD_PROFILING
#include "mpiprof.h"
/* Insert the prototypes for the PMPI routines */
#undef __MPI_BINDINGS
#include "binding.h"
#endif

/*@C
  MPI_Int2handle - Convert an integer (Fortran) MPI handle to a C handle

Input Parameters:
+ f_handle - Fortran integer handle
- handle_kind - Type of handle 

Return value:
. c_handle - C version of handle; should be cast to the correct type.

Notes:
The returned handle should be cast to the correct type by the user.

Notes for Fortran users:
There is no Fortran version of this routine.

.N MPI2
@*/
MPI_Handle_type MPI_Int2handle( f_handle, handle_kind )
MPI_Fint        f_handle;
MPI_Handle_enum handle_kind;
{
    switch (handle_kind) {
    case MPI_OP_HANDLE:
    case MPI_COMM_HANDLE:
    case MPI_DATATYPE_HANDLE:
    case MPI_ERRHANDLE_HANDLE:
    case MPI_GROUP_HANDLE:
	return (MPI_Handle_type) f_handle;
    default:
	/* Should only be requests */
	return (MPI_Handle_type)MPIR_ToPointer( f_handle );
    }
}
