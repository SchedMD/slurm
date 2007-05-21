/*
 *  $Id: cart_sub.c,v 1.9 2001/11/14 20:10:53 ashton Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Cart_sub = PMPI_Cart_sub
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Cart_sub  MPI_Cart_sub
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Cart_sub as PMPI_Cart_sub
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

/*@

MPI_Cart_sub - Partitions a communicator into subgroups which 
               form lower-dimensional cartesian subgrids

Input Parameters:
+ comm - communicator with cartesian structure (handle) 
- remain_dims - the  'i'th entry of remain_dims specifies whether the 'i'th 
dimension is kept in the subgrid (true) or is dropped (false) (logical 
vector) 

Output Parameter:
. newcomm - communicator containing the subgrid that includes the calling 
process (handle) 

.N fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_TOPOLOGY
.N MPI_ERR_COMM
.N MPI_ERR_ARG
@*/
int MPI_Cart_sub ( MPI_Comm comm, int *remain_dims, MPI_Comm *comm_new )
{
  int i, j, ndims, flag;
  int num_remain_dims = 0;
  int remain_total = 1;
  int rank;
  int color = 0;
  int key = 0;
  int mpi_errno = MPI_SUCCESS;
  MPIR_TOPOLOGY *topo, *new_topo;
  static char myname[] = "MPI_CART_SUB";
  struct MPIR_COMMUNICATOR *comm_ptr;

  TR_PUSH(myname);

  comm_ptr = MPIR_GET_COMM_PTR(comm);

#ifndef MPIR_NO_ERROR_CHECKING
  MPIR_TEST_MPI_COMM(comm,comm_ptr,comm_ptr,myname);

  /* Check for valid arguments */
  MPIR_TEST_ARG(remain_dims);
  MPIR_TEST_ARG(comm_new);
  if (mpi_errno)
      return MPIR_ERROR( comm_ptr, mpi_errno, myname );
#endif

  /* Get topology information from the communicator */
  MPI_Attr_get ( comm, MPIR_TOPOLOGY_KEYVAL, (void **)&topo, &flag );

#ifndef MPIR_NO_ERROR_CHECKING
  /* Check for valid topology */
  if ( ( (flag != 1)                      && (mpi_errno = MPI_ERR_TOPOLOGY)) ||
       ( (topo->type != MPI_CART)         && (mpi_errno = MPI_ERR_TOPOLOGY)) )
    return MPIR_ERROR( comm_ptr, mpi_errno, myname );
#endif
  
  /* Determine the remaining dimension total */
  ndims = topo->cart.ndims;
  for ( i=0; i<ndims; i++ )
      if ( remain_dims[i] ) {
	  num_remain_dims++;
	  remain_total *= topo->cart.dims[i];
      }

  /* Check for special case */
  if ( num_remain_dims == 0 ) {
      /* In this case, we consider this a 0-dimensional point.  In this
	 case, we make EACH member of the communicator a separate 
	 new communicator (basically a dup of comm_self) */
      mpi_errno = MPI_Comm_dup( MPI_COMM_SELF, comm_new );
      /* Eventually, we should attach some sort of 0-dimensional 
	 cartesian topology to this */
      return (mpi_errno);
  }
  
  /* Split the old communicator */
  for ( i=0; i< ndims; i++ ) 
	if ( remain_dims[i] ) {
	  key = (key * topo->cart.dims[i]) + topo->cart.position[i];
	}
	else {
	  color = (color * topo->cart.dims[i]) + topo->cart.position[i];
	}
  MPI_Comm_split ( comm, color, key, comm_new );

  /* Store topology information in new communicator */
  if ( (*comm_new) != MPI_COMM_NULL ) {
      MPIR_ALLOC(new_topo,(MPIR_TOPOLOGY *) MPIR_SBalloc ( MPIR_topo_els ),
		 comm_ptr,MPI_ERR_EXHAUSTED,myname);
    MPIR_SET_COOKIE(&new_topo->cart,MPIR_CART_TOPOL_COOKIE)
    new_topo->cart.type           = MPI_CART;
    new_topo->cart.nnodes         = remain_total;
    new_topo->cart.ndims          = num_remain_dims;
    MPIR_ALLOC(new_topo->cart.dims,
	(int *)MALLOC(sizeof(int)*3*num_remain_dims),
	       comm_ptr,MPI_ERR_EXHAUSTED,myname);
    new_topo->cart.periods        = new_topo->cart.dims + num_remain_dims;
    new_topo->cart.position       = new_topo->cart.periods + num_remain_dims;
    for ( i=j=0; i<ndims; i++ )
      if ( remain_dims[i] ) {
        new_topo->cart.dims[j]    = topo->cart.dims[i];
        new_topo->cart.periods[j] = topo->cart.periods[i];
        j++;
      }
  
    /* Compute my position */
    MPI_Comm_rank ( (*comm_new), &rank );
    for ( i=0; i < num_remain_dims; i++ ) {
      remain_total = remain_total / new_topo->cart.dims[i];
      new_topo->cart.position[i]  = rank / remain_total;
      rank = rank % remain_total;
    }

    /* cache topology information */
    MPI_Attr_put ( (*comm_new), MPIR_TOPOLOGY_KEYVAL, (void *)new_topo );
  }
  TR_POP;
  return (mpi_errno);
}

