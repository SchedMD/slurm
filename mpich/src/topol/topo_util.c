/*
 *  $Id: topo_util.c,v 1.8 2005/05/04 21:13:04 gropp Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"
/* index is a function in string.h.  Define this to suppress warnings about
   shadowed symbols from the C compiler */
#ifndef index
#define index idx
#endif
#include "mpitopo.h"
#include "sbcnst2.h"
#define MPIR_SBinit MPID_SBinit
#define MPIR_SBfree MPID_SBfree
#define MPIR_SBalloc MPID_SBalloc
#define MPIR_SBdestroy MPID_SBdestroy
/* 
   Keyval for topologies.
 */
int MPIR_TOPOLOGY_KEYVAL = MPI_KEYVAL_INVALID;

/* 
  We want to use the same name mapping for internal routines as in the
  rest of the package.  Note that this file calls a few MPI routines,
  such as MPI_Keyval_create.  
*/
#ifndef MPI_BUILD_PROFILING
#define MPI_BUILD_PROFILING
#endif
#include "mpiprof.h"

/* 
   Topology implementation uses small blocks; for efficiency, these are
   managed with the small-block allocator
 */
void MPIR_Topology_Init()
{
    MPIR_topo_els   = MPIR_SBinit( sizeof( MPIR_TOPOLOGY ),  4,  4);
}
void MPIR_Topology_Free()
{
    MPIR_SBdestroy( MPIR_topo_els );
}

/*
  MPIR_Topology_copy_fn - copies topology information.
 */
int MPIR_Topology_copy_fn(
	MPI_Comm old_comm, 
	int keyval, 
	void *extra, 
	void *attr_in, 
	void *attr_out, 
	int *flag)
{
  MPIR_TOPOLOGY *old_topo = (MPIR_TOPOLOGY *) attr_in;
  MPIR_TOPOLOGY *new_topo = (MPIR_TOPOLOGY *) MPIR_SBalloc ( MPIR_topo_els );

  if (!new_topo)
      return MPI_ERR_EXHAUSTED;

  /* Copy topology info */
  new_topo->type = old_topo->type;
  if (old_topo->type == MPI_CART) {
    int i, ndims;
    MPIR_SET_COOKIE(&new_topo->cart,MPIR_CART_TOPOL_COOKIE)
    new_topo->cart.nnodes        = old_topo->cart.nnodes; 
    new_topo->cart.ndims = ndims = old_topo->cart.ndims;
    new_topo->cart.dims          = (int *)MALLOC( sizeof(int) * 3 * ndims );
    if (!new_topo) return MPI_ERR_EXHAUSTED;
    new_topo->cart.periods       = new_topo->cart.dims + ndims;
    new_topo->cart.position      = new_topo->cart.periods + ndims;
    for ( i=0; i<ndims; i++ ) {
      new_topo->cart.dims[i]     = old_topo->cart.dims[i];
      new_topo->cart.periods[i]  = old_topo->cart.periods[i];
    }
    for ( i=0; i < ndims; i++ ) 
      new_topo->cart.position[i] = old_topo->cart.position[i];
  }
  else if (old_topo->type == MPI_GRAPH) {
    int  i, nnodes;
    int *index;
    MPIR_SET_COOKIE(&new_topo->graph,MPIR_GRAPH_TOPOL_COOKIE)
    new_topo->graph.nnodes = nnodes = old_topo->graph.nnodes;
    new_topo->graph.nedges        = old_topo->graph.nedges;
    index = old_topo->graph.index;
    new_topo->graph.index         = 
      (int *)MALLOC(sizeof(int) * (nnodes + index[nnodes-1]) );
    if (!new_topo->graph.index) return MPI_ERR_EXHAUSTED;
    new_topo->graph.edges         = new_topo->graph.index + nnodes;
    for ( i=0; i<nnodes; i++ )
      new_topo->graph.index[i]    = old_topo->graph.index[i];
    for ( i=0; i<index[nnodes-1]; i++ )
      new_topo->graph.edges[i]    = old_topo->graph.edges[i];
  }

  /* Set attr_out and return a "1" to indicate information was copied */
  (*(void **)attr_out) = (void *) new_topo;
  (*flag)     = 1;
  return (MPI_SUCCESS);
}


/*
  MPIR_Topology_delete_fn - deletes topology information.
 */
int MPIR_Topology_delete_fn(
	MPI_Comm comm, 
	int keyval, 
	void *attr_val, 
	void *extra)
{
  MPIR_TOPOLOGY *topo = (MPIR_TOPOLOGY *)attr_val;

  /* Free topology specific data */
  if ( topo->type == MPI_CART ) {
      MPIR_CLR_COOKIE( &topo->cart );
      FREE( topo->cart.dims );
  }
  else if ( topo->type == MPI_GRAPH ) {
      MPIR_CLR_COOKIE( &topo->graph );
      FREE( topo->graph.index );
  }
  
  /* Free topology structure */
  MPIR_SBfree ( MPIR_topo_els, topo );

  return (MPI_SUCCESS);
}


/*
MPIR_Topology_init - Initializes topology code.
 */
void MPIR_Topology_init()
{
  MPI_Keyval_create ( MPIR_Topology_copy_fn, 
                      MPIR_Topology_delete_fn,
                      &MPIR_TOPOLOGY_KEYVAL,
                      (void *)0);
}


/*
MPIR_Topology_finalize - Un-initializes topology code.
 */
void MPIR_Topology_finalize()
{
  MPI_Keyval_free ( &MPIR_TOPOLOGY_KEYVAL );
}
