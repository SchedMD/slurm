/*
 *  $Id: comm_free.c,v 1.9 2002/10/31 21:00:52 gropp Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Comm_free = PMPI_Comm_free
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Comm_free  MPI_Comm_free
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Comm_free as PMPI_Comm_free
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

/* For MPIR_COLLOPS */
#include "mpicoll.h"

#define DBG(a) 
#define OUTFILE stdout

/*@

MPI_Comm_free - Marks the communicator object for deallocation

Input Parameter:
. comm - communicator to be destroyed (handle) 

Null Handles:
The MPI 1.1 specification, in the section on opaque objects, explicitly
disallows freeing a null communicator.  The text from the standard is:
.vb
 A null handle argument is an erroneous IN argument in MPI calls, unless an
 exception is explicitly stated in the text that defines the function. Such
 exception is allowed for handles to request objects in Wait and Test calls
 (sections Communication Completion and Multiple Completions ). Otherwise, a
 null handle can only be passed to a function that allocates a new object and
 returns a reference to it in the handle.
.ve

.N fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_COMM
.N MPI_ERR_ARG
@*/
int MPI_Comm_free ( MPI_Comm *commp )
{
  int mpi_errno = MPI_SUCCESS;
  int attr_free_err = 0;
  struct MPIR_COMMUNICATOR *comm;
  static char myname[] = "MPI_COMM_FREE";

  TR_PUSH(myname);
  DBG(FPRINTF(OUTFILE, "Freeing communicator %ld\n", (long)comm );)
  DBG(FPRINTF(OUTFILE,"About to check for null comm\n");fflush(OUTFILE);)

  /* Check for null communicator */
  /* The actual effect of freeing a null communicator is clearly defined
     by the standard as an error. */
  if (*commp == MPI_COMM_NULL) {
      TR_POP;
      mpi_errno = MPIR_ERRCLASS_TO_CODE(MPI_ERR_COMM, MPIR_ERR_COMM_NULL);
      return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
  }

  comm = MPIR_GET_COMM_PTR(*commp);
  MPIR_TEST_MPI_COMM(*commp,comm,comm,myname);
      
  DBG(FPRINTF(OUTFILE,"About to check args\n");fflush(OUTFILE);)

#ifdef MPIR_MEMDEBUG
  if (commp == &comm->self) {
      FPRINTF( stderr, 
	       "Cannot pass address of self pointer to MPI_Comm_free\n" );
      MPI_Abort( (MPI_Comm)0, 2 );
  }
#endif

  DBG(FPRINTF(OUTFILE,"About to free group\n");fflush(OUTFILE);)

  MPIR_REF_DECR(comm);
  if ( comm->ref_count <= 0 ) {

      DBG(FPRINTF(OUTFILE,"About to check for perm comm\n");fflush(OUTFILE);)
      /* We can't free permanent objects unless finalize has been called */
      if  ( ( comm->permanent == 1 ) && (MPIR_Has_been_initialized == 1) )
	  return MPIR_ERROR( comm,
		MPIR_ERRCLASS_TO_CODE(MPI_ERR_ARG,MPIR_ERR_PERM_KEY), myname );

      /* Remove it from the debuggers list of active communicators */
      MPIR_Comm_forget( comm );

        (void)MPID_CommFree( comm );

	/* Delete the virtual function table if it was allocated and
	 * is now no longer referenced. Ones which are statically
	 * set up have the ref count boosted beforehand, so they're
	 * never freed !
	 */
	if (comm->collops) {
	    MPIR_REF_DECR(comm->collops);
	    if (comm->collops->ref_count == 0) {
		FREE(comm->collops);
	    }
	}
	comm->collops = NULL;

        DBG(FPRINTF(OUTFILE,"About to free context\n");fflush(OUTFILE);)
	/* Free the context used by this communicator */
	(void) MPIR_Context_dealloc ( comm, 1, comm->recv_context );
	
        DBG(FPRINTF(OUTFILE,"About to finish lock on comm\n");fflush(OUTFILE);)
	/* Free lock on collective comm, if it's not a self-reference */
	if ( comm->comm_coll != comm ) {
	  MPID_THREAD_LOCK_FINISH(comm->ADIctx,comm->comm_coll);
	    }

        DBG(FPRINTF(OUTFILE,"About to free coll comm\n");fflush(OUTFILE);)
	/* Free collective communicator (unless it refers back to myself) */
	if ( comm->comm_coll != comm ) {
	    MPI_Comm ctmp = comm->comm_coll->self;
	    MPI_Comm_free ( &ctmp );
	}

	/* Put this after freeing the collective comm because it may have
	   incremented the ref count of the attribute tree.
	   Grumble.  If we want an error return from the delete-attribute
           to prevent freeing a communicator, we'd need to do this FIRST 
         */
        DBG(FPRINTF(OUTFILE,"About to free cache info\n");fflush(OUTFILE);)
	/* Free cache information */
	attr_free_err = MPIR_Attr_free_tree ( comm );
	
        DBG(FPRINTF(OUTFILE,"About to free groups\n");fflush(OUTFILE);)
	/* Free groups */
	    /* Note that since group and local group might be the same
	       value, we can't pass the self entries directly (if we
	       did, the first group_free would cause the second to
	       use (MPI_Group)0) */
	    {MPI_Group tmp;
	tmp = comm->group->self; MPI_Group_free ( &tmp );
	tmp = comm->local_group->self; MPI_Group_free ( &tmp );
	    }
	MPI_Errhandler_free( &(comm->error_handler) );

        /* Free off any name string that may be present */
        if (comm->comm_name)
	  {
	    FREE(comm->comm_name);
	    comm->comm_name = 0;
	  }

        DBG(FPRINTF(OUTFILE,"About to free comm structure\n");fflush(OUTFILE);)
	/* Free comm structure */
	MPIR_CLR_COOKIE(comm);
	MPIR_RmPointer( *commp );
	FREE( comm );
  }

  DBG(FPRINTF(OUTFILE,"About to set comm to comm_null\n");fflush(OUTFILE);)
  /* Set comm to null */
  *commp = MPI_COMM_NULL;

  /* If the attribute delete routine returned an error, then
     invoke the error handler with that */
  if (mpi_errno == MPI_SUCCESS && attr_free_err) {
      mpi_errno = attr_free_err;
      return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
  }
  TR_POP;
  return (mpi_errno);
}


