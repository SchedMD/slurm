/*
 *  $Id: cart_map.c,v 1.10 2002/03/28 20:38:44 gropp Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Cart_map = PMPI_Cart_map
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Cart_map  MPI_Cart_map
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Cart_map as PMPI_Cart_map
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

MPI_Cart_map - Maps process to Cartesian topology information 

Input Parameters:
+ comm - input communicator (handle) 
. ndims - number of dimensions of Cartesian structure (integer) 
. dims - integer array of size 'ndims' specifying the number of processes in 
  each coordinate direction 
- periods - logical array of size 'ndims' specifying the periodicity 
  specification in each coordinate direction 

Output Parameter:
. newrank - reordered rank of the calling process; 'MPI_UNDEFINED' if 
  calling process does not belong to grid (integer) 

.N fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_COMM
.N MPI_ERR_DIMS
.N MPI_ERR_ARG
@*/
int MPI_Cart_map ( 
	MPI_Comm comm_old,
	int ndims,
	int *dims,
	int *periods,
	int *newrank)
{
  int i;
  int nranks = 1;
  int rank, size;
  int mpi_errno = MPI_SUCCESS;
  struct MPIR_COMMUNICATOR *comm_old_ptr;
  static char myname[] = "MPI_CART_MAP";

  TR_PUSH(myname);
  comm_old_ptr = MPIR_GET_COMM_PTR(comm_old);
  MPIR_TEST_MPI_COMM(comm_old,comm_old_ptr,comm_old_ptr,myname);

  /* Check for valid arguments */
  if (((ndims < 1)                 && (mpi_errno = MPI_ERR_DIMS))      ||
      ((newrank == (int *)0)       && (mpi_errno = MPI_ERR_ARG))      ||
      ((dims == (int *)0)          && (mpi_errno = MPI_ERR_ARG))       )
    return MPIR_ERROR( comm_old_ptr, mpi_errno, myname );
  
  /* Determine number of processes needed for topology */
  for ( i=0; i<ndims; i++ )
    nranks *= dims[i];
  
  /* Test that the communicator is large enough */
  MPIR_Comm_size( comm_old_ptr, &size );
  if (size < nranks) {
      mpi_errno = MPIR_Err_setmsg( MPI_ERR_TOPOLOGY, MPIR_ERR_TOPO_TOO_LARGE,
				   myname, (char *)0, (char *)0, 
				   nranks, size );
      return MPIR_ERROR(comm_old_ptr, mpi_errno, myname );
  }

  /* Am I in this range? */
  MPIR_Comm_rank ( comm_old_ptr, &rank ); 
  if ( rank < nranks )
    (*newrank) = rank;
  else
    (*newrank) = MPI_UNDEFINED;

  TR_POP;
  return (mpi_errno);
}
