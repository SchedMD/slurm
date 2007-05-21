/*
 *  $Id: comm_dup.c,v 1.12 2001/11/14 19:54:19 ashton Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Comm_dup = PMPI_Comm_dup
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Comm_dup  MPI_Comm_dup
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Comm_dup as PMPI_Comm_dup
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

#define DBG(a)
#define OUTFILE stdout

/*@

MPI_Comm_dup - Duplicates an existing communicator with all its cached
               information

Input Parameter:
. comm - communicator (handle) 

Output Parameter:
. newcomm - A new communicator over the same group as 'comm' but with a new
  context. See notes.  (handle) 

Notes:
  This routine is used to create a new communicator that has a new
  communication context but contains the same group of processes as
  the input communicator.  Since all MPI communication is performed
  within a communicator (specifies as the group of processes `plus`
  the context), this routine provides an effective way to create a
  private communicator for use by a software module or library.  In
  particular, no library routine should use 'MPI_COMM_WORLD' as the
  communicator; instead, a duplicate of a user-specified communicator
  should always be used.  For more information, see Using MPI, 2nd
  edition. 

  Because this routine essentially produces a copy of a communicator,
  it also copies any attributes that have been defined on the input
  communicator, using the attribute copy function specified by the
  'copy_function' argument to 'MPI_Keyval_create'.  This is
  particularly useful for (a) attributes that describe some property
  of the group associated with the communicator, such as its
  interconnection topology and (b) communicators that are given back
  to the user; the attibutes in this case can track subsequent
  'MPI_Comm_dup' operations on this communicator.

.N fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_COMM
.N MPI_ERR_EXHAUSTED

.seealso: MPI_Comm_free, MPI_Keyval_create, MPI_Attr_set, MPI_Attr_delete

@*/
int MPI_Comm_dup ( 
	MPI_Comm comm, 
	MPI_Comm *comm_out )
{
  struct MPIR_COMMUNICATOR *new_comm, *comm_ptr;
  int mpi_errno;
  MPIR_ERROR_DECL;
  static char myname[] = "MPI_COMM_DUP";

  TR_PUSH(myname);

  comm_ptr = MPIR_GET_COMM_PTR(comm);
  /* Check for non-null communicator */
  if ( MPIR_TEST_COMM_NOTOK(comm,comm_ptr) ) {
      (*comm_out) = MPI_COMM_NULL;
      return MPIR_ERROR( comm_ptr, MPI_ERR_COMM, myname);
  }

  /* Duplicate the communicator */
  MPIR_ALLOC(new_comm,NEW(struct MPIR_COMMUNICATOR),comm_ptr,MPI_ERR_EXHAUSTED, 
	     "MPI_COMM_DUP" );
    MPIR_Comm_init( new_comm, comm_ptr, comm_ptr->comm_type );
  MPIR_Group_dup ( comm_ptr->group,       &(new_comm->group) );
  MPIR_Group_dup ( comm_ptr->local_group, &(new_comm->local_group) );
  new_comm->local_rank     = new_comm->local_group->local_rank;
  new_comm->lrank_to_grank = new_comm->group->lrank_to_grank;
  new_comm->np             = new_comm->group->np;
  new_comm->comm_name	   = 0;
  DBG(FPRINTF(OUTFILE,"Dup:About to copy attr for comm %ld\n",(long)comm);)
  /* Also free at least some of the parts of the commuicator */      

  if ((mpi_errno = MPIR_Attr_copy ( comm_ptr, new_comm ) )) {
      MPI_Group gtmp1, gtmp2;
      *comm_out = MPI_COMM_NULL;
      /* This should really use a more organized "delete-incomplete-object"
	 call */
      gtmp1 = new_comm->group->self;
      gtmp2 = new_comm->local_group->self;
      MPI_Group_free( &gtmp1 );
      MPI_Group_free( &gtmp2 );
      MPI_Errhandler_free( &new_comm->error_handler );
      MPIR_CLR_COOKIE(new_comm);
      MPIR_RmPointer( new_comm->self );
      FREE( new_comm );
      /* MPIR_Attr_copy converts the user's code into an MPI_ERR_OTHER class */
      /* What is the appropriate error return here?  Does Attr_copy
	 return an MPI error class, or a user error code? */
      return MPIR_ERROR( comm_ptr, mpi_errno, myname );
  }
  if ((mpi_errno = MPID_CommInit( comm_ptr, new_comm )))
    return mpi_errno;

  /* Duplicate intra-communicators */
  if ( comm_ptr->comm_type == MPIR_INTRA ) {
	(void) MPIR_Context_alloc ( comm_ptr, 2, &(new_comm->send_context) );
	new_comm->recv_context    = new_comm->send_context;
	DBG(FPRINTF(OUTFILE,"Dup:About to make collcomm for %ld\n",(long)new_comm);)
	(void) MPIR_Comm_make_coll ( new_comm, MPIR_INTRA );
  }

  /* Duplicate inter-communicators */
  else {
	struct MPIR_COMMUNICATOR *inter_comm = comm_ptr->comm_coll;
	struct MPIR_COMMUNICATOR *intra_comm = comm_ptr->comm_coll->comm_coll;
	int          rank;
	MPIR_CONTEXT recv_context, send_context;

	/* Allocate send context, inter-coll context and intra-coll context */
	MPIR_Context_alloc ( intra_comm, 3, &recv_context );

	/* If I'm the local leader, then swap context info */
	MPIR_ERROR_PUSH(inter_comm);
	MPIR_Comm_rank ( intra_comm, &rank );
	if (rank == 0) {
	  MPI_Status status;
	  
	  MPIR_ERROR_PUSH(inter_comm);
	  mpi_errno = MPI_Sendrecv (&recv_context, 
				    1, MPIR_CONTEXT_TYPE, 0, MPIR_IC_DUP_TAG,
				    &send_context, 
				    1, MPIR_CONTEXT_TYPE, 0, MPIR_IC_DUP_TAG,
				    inter_comm->self, &status);
	  MPIR_ERROR_POP(inter_comm);
	  if (mpi_errno) return MPIR_ERROR(comm_ptr,mpi_errno, myname );
	}
	
	/* Broadcast the send context */
	MPIR_ERROR_PUSH(intra_comm);
	mpi_errno = MPI_Bcast(&send_context, 1, MPIR_CONTEXT_TYPE, 0, 
			      intra_comm->self);
	MPIR_ERROR_POP(intra_comm);
	if (mpi_errno) return MPIR_ERROR(comm_ptr,mpi_errno,myname );

	/* We all now have all the information necessary,finish building the */
	/* inter-communicator */
	new_comm->send_context  = send_context;
	new_comm->recv_context  = recv_context;

	/* Build the collective inter-communicator */
	MPIR_Comm_make_coll( new_comm, MPIR_INTER );
  }
  (*comm_out)               = new_comm->self;

  /* Remember it for the debugger */
  MPIR_Comm_remember ( new_comm );

  DBG(FPRINTF(OUTFILE,"Dup:done for new comm %ld\n", (long)new_comm );)
  TR_POP;
  return(MPI_SUCCESS);
}
