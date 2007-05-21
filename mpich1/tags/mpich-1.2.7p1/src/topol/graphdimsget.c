/*
 *  $Id: graphdimsget.c,v 1.9 2001/11/14 20:10:55 ashton Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Graphdims_get = PMPI_Graphdims_get
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Graphdims_get  MPI_Graphdims_get
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Graphdims_get as PMPI_Graphdims_get
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

MPI_Graphdims_get - Retrieves graph topology information associated with a 
                    communicator

Input Parameters:
. comm - communicator for group with graph structure (handle) 

Output Parameter:
+ nnodes - number of nodes in graph (integer) 
- nedges - number of edges in graph (integer) 

.N fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_TOPOLOGY
.N MPI_ERR_COMM
.N MPI_ERR_ARG
@*/
int MPI_Graphdims_get ( MPI_Comm comm, int *nnodes, int *nedges )
{
  int mpi_errno = MPI_SUCCESS, flag;
  MPIR_TOPOLOGY *topo;
  struct MPIR_COMMUNICATOR *comm_ptr;
  static char myname[] = "MPI_GRAPHDIMS_GET";
  MPIR_ERROR_DECL;

  TR_PUSH(myname);

  comm_ptr = MPIR_GET_COMM_PTR(comm);

#ifndef MPIR_NO_ERROR_CHECKING
  MPIR_TEST_MPI_COMM(comm,comm_ptr,comm_ptr,myname);
  MPIR_TEST_ARG(nnodes);
  MPIR_TEST_ARG(nedges);
  if (mpi_errno)
      return MPIR_ERROR(comm_ptr, mpi_errno, myname );
#endif

  /* Get topology information from the communicator */
  MPIR_ERROR_PUSH( comm_ptr );
  mpi_errno = 
      MPI_Attr_get ( comm, MPIR_TOPOLOGY_KEYVAL, (void **)&topo, &flag );
  MPIR_ERROR_POP( comm_ptr );
  if (mpi_errno) {
      return MPIR_ERROR( comm_ptr, mpi_errno, myname );
  }

  /* Set nnodes */
  if ( nnodes != (int *)0 ) {
    if ( (flag == 1) && (topo->type == MPI_GRAPH) )
      (*nnodes) = topo->graph.nnodes;
    else
      (*nnodes) = MPI_UNDEFINED;
  }

  /* Set nedges */
  if ( nedges != (int *)0 ) {
    if ( (flag == 1) && (topo->type == MPI_GRAPH) )
      (*nedges) = topo->graph.nedges;
    else
      (*nedges) = MPI_UNDEFINED;
  }

  TR_POP;
  return (MPI_SUCCESS);
}
