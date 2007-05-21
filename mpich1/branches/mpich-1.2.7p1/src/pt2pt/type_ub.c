/*
 *  $Id: type_ub.c,v 1.8 2001/11/14 20:10:09 ashton Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Type_ub = PMPI_Type_ub
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Type_ub  MPI_Type_ub
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Type_ub as PMPI_Type_ub
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
    MPI_Type_ub - Returns the upper bound of a datatype

Input Parameters:
. datatype - datatype (handle) 

Output Parameter:
. displacement - displacement of upper bound from origin, 
                             in bytes (integer) 

.N fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_TYPE
.N MPI_ERR_ARG
@*/
int MPI_Type_ub ( MPI_Datatype datatype, MPI_Aint *displacement )
{
  int mpi_errno = MPI_SUCCESS;
  struct MPIR_DATATYPE *dtype_ptr;
  static char myname[] = "MPI_TYPE_UB";

  TR_PUSH(myname);
  MPIR_TEST_ARG(displacement);
  if (mpi_errno)
	return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );

  /* Assign the ub and return */

  dtype_ptr   = MPIR_GET_DTYPE_PTR(datatype);
  MPIR_TEST_DTYPE(datatype,dtype_ptr,MPIR_COMM_WORLD, myname);

  (*displacement) = dtype_ptr->ub;
  TR_POP;
  return (MPI_SUCCESS);
}
