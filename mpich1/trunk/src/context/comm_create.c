/*
 *  $Id: comm_create.c,v 1.12 2001/11/14 19:54:18 ashton Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Comm_create = PMPI_Comm_create
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Comm_create  MPI_Comm_create
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Comm_create as PMPI_Comm_create
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

/*@

MPI_Comm_create - Creates a new communicator

Input Parameters:
+ comm - communicator (handle) 
- group - group, which is a subset of the group of 'comm'  (handle) 

Output Parameter:
. comm_out - new communicator (handle) 

.N fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_COMM
.N MPI_ERR_GROUP
.N MPI_ERR_EXHAUSTED

.seealso: MPI_Comm_free
@*/
int MPI_Comm_create ( MPI_Comm comm, MPI_Group group, MPI_Comm *comm_out )
{
  int mpi_errno = MPI_SUCCESS;
  struct MPIR_COMMUNICATOR *comm_ptr, *new_comm;
  struct MPIR_GROUP *group_ptr;
  static char myname[] = "MPI_COMM_CREATE";

  TR_PUSH(myname);

  comm_ptr  = MPIR_GET_COMM_PTR(comm);
  group_ptr = MPIR_GET_GROUP_PTR(group);
  /* MPIR_TEST_MPI_GROUP(group,group_ptr,comm_ptr,myname); */
#ifndef MPIR_NO_ERROR_CHECKING
  /* Check for invalid arguments */
  if ( MPIR_TEST_COMM_NOTOK(comm,comm_ptr)) {
      (*comm_out) = MPI_COMM_NULL;
      return MPIR_ERROR( MPIR_COMM_WORLD, MPI_ERR_COMM, myname );
      }

  MPIR_TEST_GROUP(group_ptr);
    if (mpi_errno)
	return MPIR_ERROR(comm_ptr, mpi_errno, myname );

  if ( (MPIR_TEST_GROUP_NOTOK(group,group_ptr) && (mpi_errno = MPI_ERR_COMM))||
      ((comm_ptr->comm_type == MPIR_INTER) && (mpi_errno = MPI_ERR_COMM))  ) {
      (*comm_out) = MPI_COMM_NULL;
      return MPIR_ERROR( comm_ptr, mpi_errno, myname );
  }
#endif

  /* Create the communicator */
  if (group_ptr->local_rank == MPI_UNDEFINED) {
    MPIR_CONTEXT tmp_context;
    /* I'm not in the new communciator but I'll participate in the */
    /* context creation anyway, then deallocate the context that was */
    /* allocated.  I may not need do this, but until I think about */
    /* the consequences a bit more ... */
    mpi_errno = MPID_CommInit( comm_ptr, NULL );
    (void) MPIR_Context_alloc  ( comm_ptr, 2, &tmp_context ); 
    (void) MPIR_Context_dealloc( comm_ptr, 2, tmp_context );
    (*comm_out) = MPI_COMM_NULL;
  }
  else {
    MPIR_ALLOC(new_comm,NEW(struct MPIR_COMMUNICATOR),comm_ptr, MPI_ERR_EXHAUSTED,
					"MPI_COMM_CREATE" );
    (void) MPIR_Comm_init( new_comm, comm_ptr, MPIR_INTRA );
    *comm_out = new_comm->self;
    MPIR_Group_dup( group_ptr, &(new_comm->group) );
    MPIR_Group_dup( group_ptr, &(new_comm->local_group) );
    /* Initialize the communicator with the device */
    new_comm->local_rank     = new_comm->local_group->local_rank;
    new_comm->lrank_to_grank = new_comm->group->lrank_to_grank;
    new_comm->np             = new_comm->group->np;
    new_comm->comm_name      = 0;

    (void) MPIR_Attr_create_tree ( new_comm );
    if ((mpi_errno = MPID_CommInit( comm_ptr, new_comm )))
	return mpi_errno;

    (void) MPIR_Context_alloc( comm_ptr, 2, &(new_comm->send_context) );
    new_comm->recv_context = new_comm->send_context;
    (void) MPIR_Comm_make_coll( new_comm, MPIR_INTRA );

    /* Remember it for the debugger */
    MPIR_Comm_remember ( new_comm );
  }
  
  TR_POP;
  return(mpi_errno);
}
