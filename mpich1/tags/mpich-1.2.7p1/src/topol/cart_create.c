/*
 *  $Id: cart_create.c,v 1.10 2001/11/14 20:10:51 ashton Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Cart_create = PMPI_Cart_create
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Cart_create  MPI_Cart_create
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Cart_create as PMPI_Cart_create
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
#include "sbcnst2.h"
#define MPIR_SBalloc MPID_SBalloc

/*ARGSUSED*/

/*@

MPI_Cart_create - Makes a new communicator to which topology information
                  has been attached

Input Parameters:
+ comm_old - input communicator (handle) 
. ndims - number of dimensions of cartesian grid (integer) 
. dims - integer array of size ndims specifying the number of processes in 
  each dimension 
. periods - logical array of size ndims specifying whether the grid is 
  periodic (true) or not (false) in each dimension 
- reorder - ranking may be reordered (true) or not (false) (logical) 

Output Parameter:
. comm_cart - communicator with new cartesian topology (handle) 

Algorithm:
We ignore 'reorder' info currently.

.N fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_TOPOLOGY
.N MPI_ERR_DIMS
.N MPI_ERR_ARG
@*/
int MPI_Cart_create ( MPI_Comm comm_old, int ndims, int *dims, int *periods, 
		      int reorder, MPI_Comm *comm_cart )
{
  int range[1][3];
  MPI_Group group_old, group;
  int i, rank, num_ranks = 1;
  int mpi_errno = MPI_SUCCESS;
  int flag, size;
  MPIR_TOPOLOGY *topo;
  struct MPIR_COMMUNICATOR *comm_old_ptr;
  static char myname[] = "MPI_CART_CREATE";

  TR_PUSH(myname);
  comm_old_ptr = MPIR_GET_COMM_PTR(comm_old);

  /* Check validity of arguments */
#ifndef MPIR_NO_ERROR_CHECKING
  MPIR_TEST_MPI_COMM(comm_old,comm_old_ptr,comm_old_ptr,myname);
  MPIR_TEST_ARG(comm_cart);
  MPIR_TEST_ARG(periods);
  if (ndims < 1 || dims == (int *)0) mpi_errno = MPI_ERR_DIMS;
  if (mpi_errno)
	return MPIR_ERROR(comm_old_ptr, mpi_errno, myname );
  
  /* Check for Intra-communicator */
  MPI_Comm_test_inter ( comm_old, &flag );
  if (flag)
    return MPIR_ERROR(comm_old_ptr, 
            MPIR_ERRCLASS_TO_CODE(MPI_ERR_COMM,MPIR_ERR_COMM_INTER), myname );
#endif

  /* Determine number of ranks in topology */
  for ( i=0; i<ndims; i++ )
    num_ranks    *= (dims[i]>0)?dims[i]:-dims[i];
  if ( num_ranks < 1 ) {
    (*comm_cart)  = MPI_COMM_NULL;
    return MPIR_ERROR( comm_old_ptr, MPI_ERR_TOPOLOGY, myname );
  }

  /* Is the old communicator big enough? */
  MPIR_Comm_size (comm_old_ptr, &size);
  if (num_ranks > size) {
      mpi_errno = MPIR_Err_setmsg( MPI_ERR_TOPOLOGY, MPIR_ERR_TOPO_TOO_LARGE, 
				   myname, 
                  "Topology size is larger than size of communicator",
		  "Topology size %d is greater than communicator size %d", 
				   num_ranks, size );
	return MPIR_ERROR(comm_old_ptr, mpi_errno, myname );
  }
	
  /* Make new comm */
  range[0][0] = 0; range[0][1] = num_ranks - 1; range[0][2] = 1;
  MPI_Comm_group ( comm_old, &group_old );
  MPI_Group_range_incl ( group_old, 1, range, &group );
  MPI_Comm_create  ( comm_old, group, comm_cart );
  MPI_Group_free( &group );
  MPI_Group_free( &group_old );

  /* Store topology information in new communicator */
  if ( (*comm_cart) != MPI_COMM_NULL ) {
      MPIR_ALLOC(topo,(MPIR_TOPOLOGY *) MPIR_SBalloc ( MPIR_topo_els ),
		 comm_old_ptr,MPI_ERR_EXHAUSTED,myname);
      MPIR_SET_COOKIE(&topo->cart,MPIR_CART_TOPOL_COOKIE)
	  topo->cart.type         = MPI_CART;
      topo->cart.nnodes       = num_ranks;
      topo->cart.ndims        = ndims;
      MPIR_ALLOC(topo->cart.dims,(int *)MALLOC( sizeof(int) * 3 * ndims ),
		 comm_old_ptr,MPI_ERR_EXHAUSTED,myname);
      topo->cart.periods      = topo->cart.dims + ndims;
      topo->cart.position     = topo->cart.periods + ndims;
      for ( i=0; i<ndims; i++ ) {
	  topo->cart.dims[i]    = dims[i];
	  topo->cart.periods[i] = periods[i];
      }

    /* Compute my position */
    MPI_Comm_rank ( (*comm_cart), &rank );
    for ( i=0; i < ndims; i++ ) {
      num_ranks = num_ranks / dims[i];
      topo->cart.position[i]  = rank / num_ranks;
      rank   = rank % num_ranks;
    }

    /* cache topology information */
    MPI_Attr_put ( (*comm_cart), MPIR_TOPOLOGY_KEYVAL, (void *)topo );
  }
  TR_POP;
  return (mpi_errno);
}
