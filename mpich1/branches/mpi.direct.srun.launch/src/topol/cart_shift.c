/*
 *  $Id: cart_shift.c,v 1.12 2001/11/14 20:10:52 ashton Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Cart_shift = PMPI_Cart_shift
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Cart_shift  MPI_Cart_shift
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Cart_shift as PMPI_Cart_shift
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

MPI_Cart_shift - Returns the shifted source and destination ranks, given a 
                 shift direction and amount

Input Parameters:
+ comm - communicator with cartesian structure (handle) 
. direction - coordinate dimension of shift (integer) 
- disp - displacement (> 0: upwards shift, < 0: downwards shift) (integer) 

Output Parameters:
+ rank_source - rank of source process (integer) 
- rank_dest - rank of destination process (integer) 

Notes:
The 'direction' argument is in the range '[0,n-1]' for an n-dimensional 
Cartesian mesh.

.N fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_TOPOLOGY
.N MPI_ERR_COMM
.N MPI_ERR_ARG
@*/
int MPI_Cart_shift ( MPI_Comm comm, int direction, int displ, 
		     int *source, int *dest )
{
  int rank, flag;
  int source_position, dest_position, save_position, periodic;
  int mpi_errno = MPI_SUCCESS;
  MPIR_TOPOLOGY *topo;
  struct MPIR_COMMUNICATOR *comm_ptr;
  static char myname[] = "MPI_CART_SHIFT";

  TR_PUSH(myname);
  comm_ptr = MPIR_GET_COMM_PTR(comm);

#ifndef MPIR_NO_ERROR_CHECKING
  MPIR_TEST_MPI_COMM(comm,comm_ptr,comm_ptr,myname);

  /* Check for valid arguments */
  if (direction < 0) mpi_errno = MPI_ERR_ARG;
  MPIR_TEST_ARG(dest);
  MPIR_TEST_ARG(source);
  if (mpi_errno) 
      return MPIR_ERROR( comm_ptr, mpi_errno, myname );
#endif


  /* Get topology information from the communicator */
  MPI_Attr_get ( comm, MPIR_TOPOLOGY_KEYVAL, (void **)&topo, &flag );

#ifndef MPIR_NO_ERROR_CHECKING
  /* Check for valid topology */
  if ( ( (flag != 1)                      && (mpi_errno = MPI_ERR_TOPOLOGY)) ||
       ( (topo->type != MPI_CART)         && (mpi_errno = MPI_ERR_TOPOLOGY)) ||
       ( (direction  >= topo->cart.ndims) && (mpi_errno=MPI_ERR_ARG))        )
    return MPIR_ERROR( comm_ptr, mpi_errno, myname );
#endif
  
  /* Check the case for a 0 displacement */
  MPIR_Comm_rank (comm_ptr, &rank);
  if (displ == 0) {
    (*source) = (*dest) = rank;
    return (mpi_errno);
  }

  /* Get ready for shifting */
  periodic = topo->cart.periods[direction];
  save_position = source_position = dest_position = 
      topo->cart.position[direction];
  
  /* Shift for the destination */
  dest_position += displ;
  if ( dest_position >= topo->cart.dims[direction] ) {
    if ( periodic )
      dest_position %= topo->cart.dims[direction];
    else
      dest_position = MPI_PROC_NULL;
  }
  else if ( dest_position < 0 ) {
    if ( periodic )
      dest_position += topo->cart.dims[direction];
    else
      dest_position = MPI_PROC_NULL;
  }
  topo->cart.position[direction] = dest_position;
  /* MPI_Cart_rank returns an error for a coordinate that is off the end
     in any non-periodic dimension.  Instead, we want to return MPI_PROC_NULL
     for those directions */
  if (dest_position != MPI_PROC_NULL) 
      MPI_Cart_rank ( comm, topo->cart.position, dest );
  else
      *dest = MPI_PROC_NULL;

  /* Shift for the source */
  source_position -= displ;
  if ( source_position >= topo->cart.dims[direction] ) {
    if ( periodic )
      source_position %= topo->cart.dims[direction];
    else
      source_position = MPI_PROC_NULL;
  }
  else if ( source_position < 0 ) {
    if ( periodic )
      source_position += topo->cart.dims[direction];
    else
      source_position = MPI_PROC_NULL;
  }
  topo->cart.position[direction] = source_position;
  if (source_position != MPI_PROC_NULL) 
      MPI_Cart_rank ( comm, topo->cart.position, source );
  else
      *source = MPI_PROC_NULL;

  /* Restore my position */
  topo->cart.position[direction] = save_position;

  TR_POP;
  return (mpi_errno);
}


