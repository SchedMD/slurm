/*
 *  $Id: type_extent.c,v 1.9 2001/11/14 20:10:06 ashton Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Type_extent = PMPI_Type_extent
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Type_extent  MPI_Type_extent
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Type_extent as PMPI_Type_extent
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
    MPI_Type_extent - Returns the extent of a datatype

Input Parameters:
. datatype - datatype (handle) 

Output Parameter:
. extent - datatype extent (integer) 

.N fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_TYPE
@*/
int MPI_Type_extent( MPI_Datatype datatype, MPI_Aint *extent )
{
  struct MPIR_DATATYPE *dtype_ptr;
  static char myname[] = "MPI_TYPE_EXTENT";
    int mpi_errno = MPI_SUCCESS;

  dtype_ptr   = MPIR_GET_DTYPE_PTR(datatype);
  MPIR_TEST_DTYPE(datatype,dtype_ptr,MPIR_COMM_WORLD,myname);

  /* Assign the extent and return */
  (*extent) = dtype_ptr->extent;
  return (MPI_SUCCESS);
}







