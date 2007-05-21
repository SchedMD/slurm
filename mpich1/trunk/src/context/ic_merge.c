/*
 *  $Id: ic_merge.c,v 1.11 2001/11/14 19:54:27 ashton Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Intercomm_merge = PMPI_Intercomm_merge
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Intercomm_merge  MPI_Intercomm_merge
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Intercomm_merge as PMPI_Intercomm_merge
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPI_BUILD_PROFILING
#include "mpiprof.h"
/* Insert the prototypes for the PMPI routines */
#undef __MPI_BINDINGS
#include "binding.h"
#endif
#include "mpimem.h"
#include "ic.h"

/*@

MPI_Intercomm_merge - Creates an intracommuncator from an intercommunicator

Input Parameters:
+ comm - Intercommunicator
- high - Used to order the groups of the two intracommunicators within comm
  when creating the new communicator.  

Output Parameter:
. comm_out - Created intracommunicator

.N fortran

Algorithm:
.vb
 1) Allocate two contexts 
 2) Local and remote group leaders swap high values
 3) Determine the high value.
 4) Merge the two groups and make the intra-communicator
.ve

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_COMM
.N MPI_ERR_EXHAUSTED

.seealso: MPI_Intercomm_create, MPI_Comm_free
@*/
int MPI_Intercomm_merge ( MPI_Comm comm, int high, MPI_Comm *comm_out )
{
  int              mpi_errno = MPI_SUCCESS;
  struct MPIR_COMMUNICATOR *new_comm, *comm_ptr;
  MPI_Group new_group;
  int              flag;
  MPIR_ERROR_DECL;
  static char myname[] = "MPI_INTERCOMM_MERGE";

  comm_ptr = MPIR_GET_COMM_PTR(comm);

#ifndef MPIR_NO_ERROR_CHECKING
  MPIR_TEST_MPI_COMM(comm,comm_ptr,comm_ptr,myname);

  /* Check for valid arguments to function */
  if (comm == MPI_COMM_NULL)
    return MPIR_ERROR(MPIR_COMM_WORLD, 
         MPIR_ERRCLASS_TO_CODE(MPI_ERR_COMM,MPIR_ERR_COMM_NULL),myname);
#endif

  MPIR_ERROR_PUSH(comm_ptr);

  /* Check for intra-communicator */
  MPI_Comm_test_inter ( comm, &flag );
  if (!flag) MPIR_RETURN_POP(comm_ptr,MPI_ERR_COMM,
		       "Intra-communicator invalid in MPI_INTERCOMM_MERGE");

  /* Make the new communicator */
  MPIR_ALLOC_POP(new_comm,NEW(struct MPIR_COMMUNICATOR),comm_ptr, 
		 MPI_ERR_EXHAUSTED,"MPI_COMM_CREATE" );
  MPIR_Comm_init( new_comm, comm_ptr, MPIR_INTRA );
  (void) MPIR_Attr_create_tree ( new_comm );

  /* Get the high value for our side */
  MPIR_Intercomm_high ( comm_ptr, &high );

  /* Merge the two groups according to high value */
  if (high) {
      MPIR_CALL_POP(MPI_Group_union(comm_ptr->group->self, 
				    comm_ptr->local_group->self, 
				    &new_group ),comm_ptr,myname);
      new_comm->group = MPIR_GET_GROUP_PTR(new_group);
  }
  else {
      MPIR_CALL_POP(MPI_Group_union(comm_ptr->local_group->self, 
				    comm_ptr->group->self, 
				    &new_group ),comm_ptr,myname);
      new_comm->group = MPIR_GET_GROUP_PTR(new_group);
  }
  MPIR_Group_dup ( new_comm->group, &(new_comm->local_group) );

  MPIR_ERROR_POP(comm_ptr);

  new_comm->local_rank     = new_comm->local_group->local_rank;
  new_comm->lrank_to_grank = new_comm->group->lrank_to_grank;
  new_comm->np             = new_comm->group->np;

  MPIR_CALL_POP(MPID_CommInit( comm_ptr, new_comm ),comm_ptr,myname);
  /* Allocate 2 contexts for intra-communicator */
  MPIR_Context_alloc ( comm_ptr, 2, &(new_comm->send_context));
  new_comm->recv_context = new_comm->send_context;
  new_comm->comm_name    = 0;

  /* Make the collective communicator */
  (void) MPIR_Comm_make_coll( new_comm, MPIR_INTRA );
  
  *comm_out = new_comm->self;

  /* Remember it for the debugger */
  MPIR_Comm_remember ( new_comm );

  return (mpi_errno);
}
