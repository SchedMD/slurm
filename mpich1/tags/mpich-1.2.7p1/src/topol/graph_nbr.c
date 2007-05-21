/*
 *  $Id: graph_nbr.c,v 1.8 2001/11/14 20:10:54 ashton Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Graph_neighbors = PMPI_Graph_neighbors
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Graph_neighbors  MPI_Graph_neighbors
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Graph_neighbors as PMPI_Graph_neighbors
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

#ifndef MPIR_MIN
#define MPIR_MIN(a,b) ((a)>(b)?(b):(a))
#endif

/*@

MPI_Graph_neighbors - Returns the neighbors of a node associated 
                      with a graph topology

Input Parameters:
+ comm - communicator with graph topology (handle) 
. rank - rank of process in group of comm (integer) 
- maxneighbors - size of array neighbors (integer) 

Output Parameters:
. neighbors - ranks of processes that are neighbors to specified process
 (array of integer) 

.N fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_TOPOLOGY
.N MPI_ERR_COMM
.N MPI_ERR_ARG
.N MPI_ERR_RANK
@*/
int MPI_Graph_neighbors ( MPI_Comm comm, int rank, int maxneighbors, 
			  int *neighbors )
{
  int i, begin, end, flag;
  int mpi_errno = MPI_SUCCESS;
  MPIR_TOPOLOGY *topo;
  struct MPIR_COMMUNICATOR *comm_ptr;
  static char myname[] = "MPI_GRAPH_NEIGHBORS";

  TR_PUSH(myname);

  comm_ptr = MPIR_GET_COMM_PTR(comm);

#ifndef MPIR_NO_ERROR_CHECKING
  MPIR_TEST_MPI_COMM(comm,comm_ptr,comm_ptr,myname);
  if (rank < 0) mpi_errno = MPI_ERR_RANK; /* ???? */
  MPIR_TEST_ARG(neighbors);
  if (mpi_errno) 
      return MPIR_ERROR( comm_ptr, mpi_errno, myname );
#endif
  /* Get topology information from the communicator */
  MPI_Attr_get ( comm, MPIR_TOPOLOGY_KEYVAL, (void **)&topo, &flag );

#ifndef MPIR_NO_ERROR_CHECKING
  /* Check for valid topology */
  if ( ( (flag != 1)                  && (mpi_errno = MPI_ERR_TOPOLOGY))  ||
       ( (topo->type != MPI_GRAPH)    && (mpi_errno = MPI_ERR_TOPOLOGY))  ||
       ( (rank >= topo->graph.nnodes) && (mpi_errno = MPI_ERR_RANK))      )
    return MPIR_ERROR( comm_ptr, mpi_errno, myname );
#endif

  /* Get neighbors */
  if ( rank == 0 ) begin = 0;
  else             begin = topo->graph.index[rank-1];
  end = topo->graph.index[rank];
  for ( i=begin; i<end; i++ )
    neighbors[i-begin] = topo->graph.edges[i];

  TR_POP;
  return (mpi_errno);
}
