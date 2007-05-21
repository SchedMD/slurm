/*
 *  $Id: attr_util.c,v 1.9 2004/06/07 12:57:39 gropp Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"
#include "attr.h"
#ifndef MPID_NO_FORTRAN
/* This is needed only for FLOG, and should be moved into src/fortran */
#include "mpifort.h"
#endif
#include "mpimem.h"

/* #define DEBUG_ATTR */

/*
 * Comments on the attribute mechanism
 *
 * When a communicator is dup'ed, we need to copy the attributes (as long
 * as they want to be copied).  In addition, the communicator implementation
 * uses a "private" communicator which is a shallow dup of the communicator;
 * this is implemented by using a reference count on the attribute TREE.
 *
 * Also note that the keyvals are shared, and are actually freed only when 
 * there are no references to them (this is the attr_key->ref_count).
 * 
 */

/* 

MPIR_Attr_copy_node -

 */
int MPIR_Attr_copy_node ( 
	struct MPIR_COMMUNICATOR *comm, 
	struct MPIR_COMMUNICATOR *comm_new,
	MPIR_HBT_node *node)
{
  void          *attr_val;
  MPIR_Attr_key *attr_key;
  MPIR_HBT_node *attr;
  int            flag;
#ifndef MPID_NO_FORTRAN
  int            attr_ival;
#endif
  int            mpi_errno = MPI_SUCCESS;
  int            copy_errno = 0;

  attr_key = node->keyval;

#ifndef MPIR_NO_ERROR_CHECKING
  if (!attr_key MPIR_TEST_COOKIE(attr_key,MPIR_ATTR_COOKIE)) {
      mpi_errno = MPIR_Err_setmsg( MPI_ERR_INTERN, MPIR_ERR_ATTR_CORRUPT,
				   (char *)0, 
				   (char *)0, (char *)0, attr_key->cookie );
      return MPIR_ERROR( comm, mpi_errno, (char *)0 );
  }
#endif
#ifdef FOO
  MPIR_REF_INCR(attr_key);
#ifdef DEBUG_ATTR
  PRINTF( "incr attr_key ref to %d for %ld in %ld, copy to comm %ld\n", 
	  attr_key->ref_count, (long)attr_key, (long)comm, (long)comm_new );
#endif /* DEBUG_ATTR */
#endif /* FOO */
  if (attr_key->copy_fn.c_copy_fn) {
#ifndef MPID_NO_FORTRAN
      if (attr_key->FortranCalling) {
	  /* The following code attempts to suppress warnings about 
	     converting an int, stored in a void *, back to an int. */
	  /* We may also need to do something about the "comm" argument */
	  /* This needs to use Fints! and c2f for communicator */
	  MPI_Aint invall = (MPI_Aint)node->value;
          MPI_Fint inval = (int)invall;
	  MPI_Fint fcomm = MPI_Comm_c2f( comm->self );
          (*(attr_key->copy_fn.f77_copy_fn))( &fcomm,
					      &node->keyval->self, 
					      attr_key->extra_state,
					      &inval, 
					      &attr_ival, &flag, &copy_errno );
          attr_val = (void *)(MPI_Aint)attr_ival;
          flag = MPIR_FROM_FLOG(flag);
	  }
      else 
#endif
     {
          copy_errno = (*(attr_key->copy_fn.c_copy_fn))(comm->self, 
						       node->keyval->self, 
                                             attr_key->extra_state,
                                             node->value, &attr_val, &flag );
      }
      if (flag && !copy_errno) {
#ifdef DEBUG_ATTR
	  PRINTF( ".. inserting attr into comm %ld\n", comm_new );
#endif	  
  MPIR_REF_INCR(attr_key);
#ifdef DEBUG_ATTR
  PRINTF( "incr attr_key ref to %d for %ld in %ld, copy to comm %ld\n", 
	  attr_key->ref_count, (long)attr_key, (long)comm, (long)comm_new );
#endif
          (void) MPIR_HBT_new_node ( node->keyval, attr_val, &attr );
          (void) MPIR_HBT_insert ( comm_new->attr_cache, attr );
      }
  }

  /* Return this to the calling routine which should handle the error
     message delivery */
  if (copy_errno) {
      mpi_errno = MPIR_Err_setmsg( MPI_ERR_OTHER, MPIR_ERR_ATTR_COPY,
				   (char *)0,
    "User defined attribute copy routine returned non-zero error code",
    "User defined attribute copy routine returned error code %d", copy_errno );
      return mpi_errno;
  }
   return MPI_SUCCESS;
}

/*+

MPIR_Attr_copy_subtree -

+*/
int MPIR_Attr_copy_subtree ( 
	struct MPIR_COMMUNICATOR *comm, 
	struct MPIR_COMMUNICATOR *comm_new, 
	MPIR_HBT tree,
	MPIR_HBT_node *subtree)
{
  int tmp_mpi_errno, mpi_errno = MPI_SUCCESS;

  if(subtree != (MPIR_HBT_node *)0) {
      tmp_mpi_errno=MPIR_Attr_copy_node ( comm, comm_new, subtree );
      if (tmp_mpi_errno != MPI_SUCCESS) mpi_errno = tmp_mpi_errno;
      
      tmp_mpi_errno=MPIR_Attr_copy_subtree(comm,comm_new,tree,subtree->left);
      if (tmp_mpi_errno != MPI_SUCCESS) mpi_errno = tmp_mpi_errno;

      tmp_mpi_errno=MPIR_Attr_copy_subtree(comm,comm_new,tree,subtree->right);
      if (tmp_mpi_errno != MPI_SUCCESS) mpi_errno = tmp_mpi_errno;
  }
  return (mpi_errno);
}

/*+

MPIR_Attr_copy - copy a tree of attributes 

+*/
int MPIR_Attr_copy ( 
	struct MPIR_COMMUNICATOR *comm, 
	struct MPIR_COMMUNICATOR *comm_new )
{
  int mpi_errno = MPI_SUCCESS;

#ifdef DEBUG_ATTR
  PRINTF( "Copy: copying attr tree to comm %ld from %ld\n", 
	  (long)comm_new, (long)comm );
#endif
  (void) MPIR_HBT_new_tree ( &(comm_new->attr_cache) );
  if ( comm_new->attr_cache != (MPIR_HBT)0 ) {
#ifdef DEBUG_ATTR
      PRINTF( "setting attr_cache %ld ref_count to 1 in comm %ld\n", 
	      (long)comm_new->attr_cache, (long)comm_new );
#endif
    comm_new->attr_cache->ref_count = 1;
    mpi_errno = MPIR_Attr_copy_subtree (comm, comm_new, comm_new->attr_cache,
                                        comm->attr_cache->root);
  }
#ifdef DEBUG_ATTR
  PRINTF( "Copy: done copying attr tree\n" );
#endif
  return (mpi_errno);
}


/*+

MPIR_Attr_free_node -

+*/
int MPIR_Attr_free_node ( 
	struct MPIR_COMMUNICATOR *comm, 
	MPIR_HBT_node *node)
{
  MPIR_Attr_key *attr_key;
  int           mpi_errno = MPI_SUCCESS;

  attr_key = node->keyval;

#ifndef MPIR_NO_ERROR_CHECKING
  if (!attr_key MPIR_TEST_COOKIE(attr_key,MPIR_ATTR_COOKIE)) {
      mpi_errno = MPIR_Err_setmsg( MPI_ERR_INTERN, MPIR_ERR_ATTR_CORRUPT,
				   (char *)0, 
				   (char *)0, (char *)0, attr_key->cookie );
      return MPIR_ERROR( comm, mpi_errno, (char *)0 );
  }
#endif

  if ( (node != (MPIR_HBT_node *)0) && (attr_key != 0) ) {
      MPIR_REF_DECR(attr_key);
#ifdef DEBUG_ATTR
  PRINTF( "decr attr_key ref to %d for attr %ld in comm %ld\n", 
	  attr_key->ref_count, (long)attr_key, (long)comm );
#endif
    if ( attr_key->delete_fn.c_delete_fn ) {
#ifndef MPID_NO_FORTRAN
	if (attr_key->FortranCalling) {
	    MPI_Aint invall = (MPI_Aint)node->value;
	    MPI_Fint inval = (int)invall;
	    MPI_Fint fcomm = MPI_Comm_c2f( comm->self );
	    /* We may also need to do something about the "comm" argument */
	    (void ) (*(attr_key->delete_fn.f77_delete_fn))( &fcomm,
					     &node->keyval->self, 
					     &inval, 
					   attr_key->extra_state, &mpi_errno );
	    node->value = (void *)(MPI_Aint)inval;
	    }
	else
#endif
	    mpi_errno = (*(attr_key->delete_fn.c_delete_fn))(comm->self, 
					     node->keyval->self, 
					     node->value, 
					     attr_key->extra_state);
	}
    if (attr_key->ref_count <= 0) {
	MPIR_CLR_COOKIE(attr_key);
	MPIR_RmPointer( node->keyval->self );
	FREE( attr_key );
    }
  }
  return (mpi_errno);
}

/*+

MPIR_Attr_free_subtree -

+*/
int MPIR_Attr_free_subtree ( 
	struct MPIR_COMMUNICATOR *comm, 
	MPIR_HBT_node *subtree )
{
    int mpi_errno, rc;

    mpi_errno = MPI_SUCCESS;
    if(subtree != (MPIR_HBT_node *)0) {
	rc = MPIR_Attr_free_subtree ( comm, subtree -> left );
	if (rc) mpi_errno = rc;
	rc = MPIR_Attr_free_subtree ( comm, subtree -> right );
	if (rc) mpi_errno = rc;
	rc = MPIR_Attr_free_node ( comm, subtree );
	if (rc) mpi_errno = rc;
    }
    return mpi_errno;
}

/*+

MPIR_Attr_free_tree -

+*/
int MPIR_Attr_free_tree ( 
	struct MPIR_COMMUNICATOR *comm)
{
    int mpi_errno = MPI_SUCCESS;
    int rc;
#ifdef DEBUG_ATTR
   PRINTF( "FreeTree:Freeing attr tree for %ld, attr cache %ld\n", (long) comm,
	   (long)comm->attr_cache );
#endif
  if ( ( comm != MPI_COMM_NULL ) && ( comm->attr_cache != (MPIR_HBT)0 ) ) {
    if (comm->attr_cache->ref_count <= 1) {
	if ( comm->attr_cache->root != (MPIR_HBT_node *)0 ) {
	  rc = MPIR_Attr_free_subtree ( comm, comm->attr_cache->root );
	  if (rc) mpi_errno = rc;
	}
      rc = MPIR_HBT_free_tree ( comm->attr_cache );
      if (rc) mpi_errno = rc;
    }
    else {
#ifdef DEBUG_ATTR
	PRINTF( "Decrementing attr_cache %ld ref count for comm %ld to %d\n", 
		(long)comm->attr_cache, 
		(long)comm, comm->attr_cache->ref_count-1  );
#endif	
      MPIR_REF_DECR(comm->attr_cache);
    }
  }
#ifdef DEBUG_ATTR
  if (comm->attr_cache)
      PRINTF( "attr_cache count is %d\n", comm->attr_cache->ref_count );
  else
      PRINTF( "No attr cache\n" );
  PRINTF( "FreeTree: done\n" );
#endif
  return mpi_errno;
}

/*+

MPIR_Attr_dup_tree -

Dups a tree.  Used ONLY in the creation of a communicator for the 
implementation of the collective routines by point-to-point routines
(see comm_util.c/MPIR_Comm_make_coll)

+*/
int MPIR_Attr_dup_tree ( 
	struct MPIR_COMMUNICATOR *comm, 
	struct MPIR_COMMUNICATOR *new_comm)
{
    if ( comm->attr_cache != (MPIR_HBT)0 ) {
	MPIR_REF_INCR(comm->attr_cache);
    }
    new_comm->attr_cache = comm->attr_cache;
#ifdef DEBUG_ATTR
    PRINTF( "Incr attr_cache (%ld) ref count to %d in comm %ld for dup\n", 
	    (long)comm->attr_cache, comm->attr_cache->ref_count, (long)comm );
#endif
  return (MPI_SUCCESS);
}

/*+

MPIR_Attr_create_tree -

+*/
int MPIR_Attr_create_tree ( struct MPIR_COMMUNICATOR *comm )
{
  (void) MPIR_HBT_new_tree ( &(comm->attr_cache) );
#ifdef DEBUG_ATTR
  PRINTF( "Setting attr cache (%ld) ref_count to 1 for comm %ld\n", 
	  (long)comm->attr_cache, (long) comm );
#endif
  comm->attr_cache->ref_count = 1;
  return (MPI_SUCCESS);
}

/* 
 * Special feature - if *keyval is not 0, then use that value 
 * as a predefined value.
 */
int MPIR_Keyval_create ( 
	MPI_Copy_function *copy_fn, 
	MPI_Delete_function *delete_fn, 
	int *keyval, 
	void *extra_state, 
	int is_fortran )
{
  MPIR_Attr_key *new_key;

  MPIR_ALLOC(new_key,NEW(MPIR_Attr_key),MPIR_COMM_WORLD,MPI_ERR_EXHAUSTED, 
				  "MPI_KEYVAL_CREATE" );
  /* This still requires work in the Fortran interface, in case
     sizeof(int) == sizeof(double) = sizeof(void*) */
  if (*keyval)
      MPIR_RegPointerIdx( *keyval, new_key );
  else
      (*keyval)		  = MPIR_FromPointer( (void *)new_key );
  new_key->self = *keyval;

  /* SEE ALSO THE CODE IN ENV/INIT.C; IT RELIES ON USING KEY AS THE
     POINTER TO SET THE PERMANENT FIELD */
#ifndef MPID_NO_FORTRAN
  if (is_fortran) {
      new_key->copy_fn.f77_copy_fn      = 
	  (void (*)( int *, int *, int *, int *, int *, 
		     int *, int * ))copy_fn;
      new_key->delete_fn.f77_delete_fn  = 
	  (void (*)( int *, int *, int *, void *, int*)) delete_fn;
  }
  else 
#endif
  {
      new_key->copy_fn.c_copy_fn      = copy_fn;
      new_key->delete_fn.c_delete_fn  = delete_fn;
  }
  new_key->ref_count	  = 1;
  new_key->extra_state	  = extra_state;
  new_key->permanent	  = 0;
  new_key->FortranCalling = is_fortran;
  MPIR_SET_COOKIE(new_key,MPIR_ATTR_COOKIE)
  return (MPI_SUCCESS);
}

/*
 * This routine is called to make a keyval permanent (used in the init routine)
 */
void MPIR_Attr_make_perm( int keyval )
{
    MPIR_Attr_key *attr_key;

    attr_key = MPIR_GET_KEYVAL_PTR( keyval );
    attr_key->permanent = 1;
}
