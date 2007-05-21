/*
 *  $Id: cartdim_get.c,v 1.8 2001/11/14 20:10:53 ashton Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Cartdim_get = PMPI_Cartdim_get
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Cartdim_get  MPI_Cartdim_get
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Cartdim_get as PMPI_Cartdim_get
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

MPI_Cartdim_get - Retrieves Cartesian topology information associated with a 
                  communicator

Input Parameter:
. comm - communicator with cartesian structure (handle) 

Output Parameter:
. ndims - number of dimensions of the cartesian structure (integer) 

.N fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_COMM
.N MPI_ERR_ARG
@*/
int MPI_Cartdim_get ( MPI_Comm comm, int *ndims )
{
  int mpi_errno = MPI_SUCCESS, flag;
  MPIR_TOPOLOGY *topo;
  struct MPIR_COMMUNICATOR *comm_ptr;
  static char myname[] = "MPI_CARTDIM_GET";
  MPIR_ERROR_DECL;

  TR_PUSH(myname);

  comm_ptr = MPIR_GET_COMM_PTR(comm);
  MPIR_TEST_MPI_COMM(comm,comm_ptr,comm_ptr,myname);

  /* Check for valid arguments */
#ifndef MPIR_NO_ERROR_CHECKING
  MPIR_TEST_ARG(ndims);
  if (mpi_errno)
      return MPIR_ERROR(comm_ptr, mpi_errno, myname );
#endif

  /* Get topology information from the communicator */
  MPIR_ERROR_PUSH( comm_ptr );
  mpi_errno = MPI_Attr_get ( comm, MPIR_TOPOLOGY_KEYVAL, (void **)&topo, &flag );
  MPIR_ERROR_POP( comm_ptr );
  /* Really need to convert this into a non-attr_get message */
  if (mpi_errno) {
      return MPIR_ERROR( comm_ptr, mpi_errno, myname );
  }
  /* Set dims */
  if ( ndims != (int *)0 ) {
    if ( (flag == 1) && (topo->type == MPI_CART) )
      (*ndims) = topo->cart.ndims;
    else
      (*ndims) = MPI_UNDEFINED;
  }

  TR_POP;
  return (MPI_SUCCESS);
}
