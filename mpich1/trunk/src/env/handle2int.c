/*
 *  $Id: handle2int.c,v 1.5 1999/08/20 02:26:57 ashton Exp $
 *
 *  (C) 1996 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

/* @C
  MPI_Handle2int - Convert a C handle to an integer (Fortran) MPI handle

Input Parameters:
+ c_handle - C handle
- handle_kind - Type of handle 

Return value:
. f_handle - Fortran version of handle.

Notes for Fortran users:
There is no Fortran version of this routine.

.N MPI2
@ */
MPI_Fint MPI_Handle2int( c_handle, handle_kind )
MPI_Handle_type c_handle;
MPI_Handle_enum handle_kind;
{
    switch (handle_kind) {
    case MPI_OP_HANDLE:
    case MPI_COMM_HANDLE:
    case MPI_DATATYPE_HANDLE:
    case MPI_ERRHANDLE_HANDLE:
    case MPI_GROUP_HANDLE:
	return (MPI_Fint) c_handle;
    default:
	/* Should only be requests */
	return (MPI_Fint)MPIR_FromPointer( c_handle );
    }
}
