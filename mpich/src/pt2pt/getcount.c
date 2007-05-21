/*
 *  $Id: getcount.c,v 1.9 2001/11/14 20:09:57 ashton Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Get_count = PMPI_Get_count
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Get_count  MPI_Get_count
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Get_count as PMPI_Get_count
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
  MPI_Get_count - Gets the number of "top level" elements

Input Parameters:
+ status - return status of receive operation (Status) 
- datatype - datatype of each receive buffer element (handle) 

Output Parameter:
. count - number of received elements (integer) 
Notes:
If the size of the datatype is zero, this routine will return a count of
zero.  If the amount of data in 'status' is not an exact multiple of the 
size of 'datatype' (so that 'count' would not be integral), a 'count' of
'MPI_UNDEFINED' is returned instead.

.N fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_TYPE
@*/
int MPI_Get_count( 
	MPI_Status *status, 
	MPI_Datatype datatype, 
	int *count )
{
  struct MPIR_DATATYPE *dtype_ptr;
  static char myname[] = "MPI_GET_COUNT";
  int         mpi_errno = MPI_SUCCESS;

  TR_PUSH(myname);

#ifdef MPID_HAS_GET_COUNT
    mpi_errno = MPID_Get_count( status, datatype, count );
#else
  dtype_ptr   = MPIR_GET_DTYPE_PTR(datatype);
  MPIR_TEST_DTYPE(datatype,dtype_ptr,MPIR_COMM_WORLD,myname);

  /* Check for correct number of bytes */
  if (dtype_ptr->size == 0) {
      if (status->count > 0)
	  (*count) = MPI_UNDEFINED;
      else
	  /* This is ambiguous */
	  (*count) = 0;
      }
  else {
      if ((status->count % (dtype_ptr->size)) != 0)
	  (*count) = MPI_UNDEFINED;
      else
	  (*count) = status->count / (dtype_ptr->size);
      }
#endif
  TR_POP;
  MPIR_RETURN( MPIR_COMM_WORLD, mpi_errno, myname );
}


