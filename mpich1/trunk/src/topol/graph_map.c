/*
 *  $Id: graph_map.c,v 1.10 2002/01/04 22:42:26 gropp Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Graph_map = PMPI_Graph_map
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Graph_map  MPI_Graph_map
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Graph_map as PMPI_Graph_map
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

/*@

MPI_Graph_map - Maps process to graph topology information

Input Parameters:
+ comm - input communicator (handle) 
. nnodes - number of graph nodes (integer) 
. index - integer array specifying the graph structure, see 'MPI_GRAPH_CREATE' 
- edges - integer array specifying the graph structure 

Output Parameter:
. newrank - reordered rank of the calling process; 'MPI_UNDEFINED' if the 
calling process does not belong to graph (integer) 
 
.N fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_TOPOLOGY
.N MPI_ERR_COMM
.N MPI_ERR_ARG
@*/
int MPI_Graph_map ( MPI_Comm comm_old, int nnodes, int *index, int *edges, 
		    int *newrank )
{
  int rank, size;
  int mpi_errno = MPI_SUCCESS;
  struct MPIR_COMMUNICATOR *comm_old_ptr;
  static char myname[] = "MPI_GRAPH_MAP";

  TR_PUSH(myname);
  comm_old_ptr = MPIR_GET_COMM_PTR(comm_old);

#ifndef MPIR_NO_ERROR_CHECKING
  MPIR_TEST_MPI_COMM(comm_old,comm_old_ptr,comm_old_ptr,myname);
  if (nnodes < 1) mpi_errno = MPI_ERR_ARG;
  MPIR_TEST_ARG(newrank);
  MPIR_TEST_ARG(index);
  MPIR_TEST_ARG(edges);
  if (mpi_errno)
      return MPIR_ERROR(comm_old_ptr, mpi_errno, myname );
#endif
  
  /* Test that the communicator is large enough */
  MPIR_Comm_size( comm_old_ptr, &size );
  if (size < nnodes) {
      return MPIR_ERROR( comm_old_ptr, MPI_ERR_ARG, myname );
  }

  /* Am I in this topology? */
  MPIR_Comm_rank ( comm_old_ptr, &rank );
  if ( rank < nnodes )
    (*newrank) = rank;
  else
    (*newrank) = MPI_UNDEFINED;

  TR_POP;
  return (mpi_errno);
}
