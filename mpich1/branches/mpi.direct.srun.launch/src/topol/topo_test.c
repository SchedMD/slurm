/*
 *  $Id: topo_test.c,v 1.7 2001/11/14 20:10:56 ashton Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Topo_test = PMPI_Topo_test
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Topo_test  MPI_Topo_test
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Topo_test as PMPI_Topo_test
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

MPI_Topo_test - Determines the type of topology (if any) associated with a 
                communicator

Input Parameter:
. comm - communicator (handle) 

Output Parameter:
. top_type - topology type of communicator 'comm' (choice).

.N fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_COMM
.N MPI_ERR_ARG

.seealso: MPI_Graph_create, MPI_Cart_create
@*/
int MPI_Topo_test ( MPI_Comm comm, int *top_type )
{
  int mpi_errno = MPI_SUCCESS, flag;
  MPIR_TOPOLOGY *topo;
  struct MPIR_COMMUNICATOR *comm_ptr;
  static char myname[] = "MPI_TOPO_TEST";

  TR_PUSH(myname);

  comm_ptr = MPIR_GET_COMM_PTR(comm);
  MPIR_TEST_MPI_COMM(comm,comm_ptr,comm_ptr,myname);

#ifndef MPIR_NO_ERROR_CHECKING
    MPIR_TEST_ARG(top_type);
    if (mpi_errno)
	return MPIR_ERROR(comm_ptr, mpi_errno, myname );
#endif
  
  /* Set the top_type */
  /* Get topology information from the communicator */
  mpi_errno = MPI_Attr_get ( comm, MPIR_TOPOLOGY_KEYVAL, (void **)&topo, 
			     &flag );
  if (mpi_errno) return MPIR_ERROR( comm_ptr, mpi_errno, myname );

  /* Check for topology information */
  if ( flag == 1 )
    (*top_type) = topo->type;
  else
    (*top_type) = MPI_UNDEFINED;

  TR_POP;
  return (MPI_SUCCESS);
}
