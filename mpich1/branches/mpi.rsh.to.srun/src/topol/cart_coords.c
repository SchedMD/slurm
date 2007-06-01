/*
 *  $Id: cart_coords.c,v 1.9 2002/03/28 20:38:44 gropp Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Cart_coords = PMPI_Cart_coords
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Cart_coords  MPI_Cart_coords
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Cart_coords as PMPI_Cart_coords
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPI_BUILD_PROFILING
#include "mpiprof.h"
/* Insert the prototypes for the PMPI routines */
#undef __MPI_BINDINGS
#include "binding.h"
#endif
#include "mpitopo.h"

/*@

MPI_Cart_coords - Determines process coords in cartesian topology given
                  rank in group

Input Parameters:
+ comm - communicator with cartesian structure (handle) 
. rank - rank of a process within group of 'comm' (integer) 
- maxdims - length of vector 'coords' in the calling program (integer) 

Output Parameter:
. coords - integer array (of size 'ndims') containing the Cartesian 
  coordinates of specified process (integer) 

.N fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_TOPOLOGY
.N MPI_ERR_RANK
.N MPI_ERR_DIMS
.N MPI_ERR_ARG

@*/
int MPI_Cart_coords ( MPI_Comm comm, int rank, int maxdims, int *coords )
{
  int i, flag;
  int mpi_errno = MPI_SUCCESS;
  MPIR_TOPOLOGY *topo;
  int nnodes;
  struct MPIR_COMMUNICATOR *comm_ptr;
  static char myname[] = "MPI_CART_COORDS";

  TR_PUSH(myname);
  comm_ptr = MPIR_GET_COMM_PTR(comm);

#ifndef MPIR_NO_ERROR_CHECKING
  MPIR_TEST_MPI_COMM(comm,comm_ptr,comm_ptr,myname);

  /* Check for valid arguments */
  if (rank < 0) mpi_errno = MPI_ERR_RANK; /* ???? */
  if (maxdims < 1) mpi_errno = MPI_ERR_DIMS;
  MPIR_TEST_ARG(coords);
  if (mpi_errno)
      return MPIR_ERROR(comm_ptr, mpi_errno, myname );
#endif

  /* Get topology information from the communicator */
  MPI_Attr_get ( comm, MPIR_TOPOLOGY_KEYVAL, (void **)&topo, &flag );

  /* Check for valid topology */
#ifndef MPIR_NO_ERROR_CHECKING
#endif
  if ( ( (flag != 1)                 && (mpi_errno = MPI_ERR_TOPOLOGY))  ||
       ( (topo->type != MPI_CART)    && (mpi_errno = MPI_ERR_TOPOLOGY))  ||
       ( (rank >= topo->cart.nnodes) && (mpi_errno = MPI_ERR_RANK))      )
    return MPIR_ERROR( comm_ptr, mpi_errno, myname );

  /* Calculate coords */
  nnodes = topo->cart.nnodes;
  for ( i=0; (i < topo->cart.ndims) && (i < maxdims); i++ ) {
    nnodes    = nnodes / topo->cart.dims[i];
    coords[i] = rank / nnodes;
    rank      = rank % nnodes;
  }

  TR_POP;
  return (mpi_errno);
}
