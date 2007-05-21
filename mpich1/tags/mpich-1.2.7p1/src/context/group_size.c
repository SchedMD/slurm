/*
 *  $Id: group_size.c,v 1.8 2001/11/14 19:54:25 ashton Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Group_size = PMPI_Group_size
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Group_size  MPI_Group_size
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Group_size as PMPI_Group_size
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

MPI_Group_size - Returns the size of a group

Input Parameters:
+ group - group (handle) 
Output Parameter:
- size - number of processes in the group (integer) 

.N fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_GROUP
.N MPI_ERR_ARG

@*/
int MPI_Group_size ( MPI_Group group, int *size )
{
  int mpi_errno = MPI_SUCCESS;
  struct MPIR_GROUP *group_ptr;
  static char myname[] = "MPI_GROUP_SIZE";

  TR_PUSH(myname);

  group_ptr = MPIR_GET_GROUP_PTR(group);
  /* MPIR_TEST_MPI_GROUP(group,group_ptr,MPIR_COMM_WORLD,myname); */

  /* Check for invalid arguments */
#ifndef MPIR_NO_ERROR_CHECKING
  MPIR_TEST_GROUP(group_ptr);
  MPIR_TEST_ARG(size);
  if (mpi_errno)
      return MPIR_ERROR(MPIR_COMM_WORLD, mpi_errno, myname );
#endif


  /* Get the size of the group */
  (*size) = group_ptr->np;

  TR_POP;
  return (mpi_errno);
}

