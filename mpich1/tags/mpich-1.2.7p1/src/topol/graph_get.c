/*
 *  $Id: graph_get.c,v 1.10 2002/01/04 22:42:26 gropp Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Graph_get = PMPI_Graph_get
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Graph_get  MPI_Graph_get
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Graph_get as PMPI_Graph_get
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPI_BUILD_PROFILING
#include "mpiprof.h"
/* Insert the prototypes for the PMPI routines */
#undef __MPI_BINDINGS
#include "binding.h"
#endif
/* index is a function in string.h.  Define this to suppress warnings about
   shadowed symbols from the C compiler */
#ifndef index
#define index idx
#endif

#include "mpitopo.h"


/*@

MPI_Graph_get - Retrieves graph topology information associated with a 
                communicator

Input Parameters:
+ comm - communicator with graph structure (handle) 
. maxindex - length of vector 'index' in the calling program  (integer) 
- maxedges - length of vector 'edges' in the calling program  (integer) 

Output Parameter:
+ index - array of integers containing the graph structure (for details see the definition of 'MPI_GRAPH_CREATE') 
- edges - array of integers containing the graph structure 

.N fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_TOPOLOGY
.N MPI_ERR_COMM
.N MPI_ERR_ARG
@*/
int MPI_Graph_get ( MPI_Comm comm, int maxindex, int maxedges, 
		    int *index, int *edges )
{
  int i, num, flag;
  int *array;
  int mpi_errno = MPI_SUCCESS;
  MPIR_TOPOLOGY *topo;
  struct MPIR_COMMUNICATOR *comm_ptr;
  static char myname[] = "MPI_GRAPH_GET";
  MPIR_ERROR_DECL;

  TR_PUSH(myname);
  comm_ptr = MPIR_GET_COMM_PTR(comm);

#ifndef MPIR_NO_ERROR_CHECKING
  MPIR_TEST_MPI_COMM(comm,comm_ptr,comm_ptr,myname);
  MPIR_TEST_ARG(index);
  MPIR_TEST_ARG(edges);
  if (mpi_errno)
      return MPIR_ERROR(comm_ptr, mpi_errno, myname );
#endif

  /* Get topology information from the communicator */
  MPIR_ERROR_PUSH(comm_ptr);
  mpi_errno = MPI_Attr_get ( comm, MPIR_TOPOLOGY_KEYVAL, (void **)&topo, &flag );
  MPIR_ERROR_POP(comm_ptr);
  if ( ( (flag != 1)               && (mpi_errno = MPI_ERR_TOPOLOGY) ) ||
       ( (topo->type != MPI_GRAPH) && (mpi_errno = MPI_ERR_TOPOLOGY) )  )
      return MPIR_ERROR( comm_ptr, mpi_errno, myname );

  /* Get index */
  num = topo->graph.nnodes;
  array = topo->graph.index;
  if ( index != (int *)0 )
    for ( i=0; (i<maxindex) && (i<num); i++ )
      (*index++) = (*array++);

  /* Get edges */
  num = topo->graph.nedges;
  array = topo->graph.edges;
  if ( edges != (int *)0 )
    for ( i=0; (i<maxedges) && (i<num); i++ )
      (*edges++) = (*array++);

  TR_POP;
  return (mpi_errno);
}
