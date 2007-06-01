/*
 *  $Id: group_excl.c,v 1.10 2001/11/14 19:54:23 ashton Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Group_excl = PMPI_Group_excl
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Group_excl  MPI_Group_excl
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Group_excl as PMPI_Group_excl
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

MPI_Group_excl - Produces a group by reordering an existing group and taking
        only unlisted members

Input Parameters:
+ group - group (handle) 
. n - number of elements in array 'ranks' (integer) 
- ranks - array of integer ranks in 'group' not to appear in 'newgroup' 

Output Parameter:
. newgroup - new group derived from above, preserving the order defined by 
 'group' (handle) 

Note:  
Currently, each of the ranks to exclude must be
a valid rank in the group and all elements must be distinct or the
function is erroneous.  This restriction is per the draft.

.N fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_GROUP
.N MPI_ERR_EXHAUSTED
.N MPI_ERR_ARG
.N MPI_ERR_RANK

.seealso: MPI_Group_free
@*/
int MPI_Group_excl ( MPI_Group group, int n, int *ranks, MPI_Group *newgroup )
{
  int i, j, rank;
  struct MPIR_GROUP *new_group_ptr;
  struct MPIR_GROUP *group_ptr;
  int mpi_errno = MPI_SUCCESS;
  static char myname[] = "MPI_GROUP_EXCL";

  TR_PUSH(myname);

  group_ptr = MPIR_GET_GROUP_PTR(group);
/*  MPIR_TEST_MPI_GROUP(group,group_ptr,MPIR_COMM_WORLD,myname); */
#ifndef MPIR_NO_ERROR_CHECKING
  MPIR_TEST_GROUP( group_ptr );
  if (mpi_errno)
      return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );

  if (n < 0) 
      return MPIR_ERROR( MPIR_COMM_WORLD, MPI_ERR_ARG, myname );

  if ( n > 0 ) {
      MPIR_TEST_ARG(ranks);
      if (mpi_errno)
	  return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
  }

  /* Error if we're excluding more than the size of the group */
  if ( n > group_ptr->np ) {
      return MPIR_ERROR( MPIR_COMM_WORLD, MPI_ERR_ARG, myname );
  }
#endif

  /* Check for a EMPTY input group */
  if ( (group == MPI_GROUP_EMPTY) || (n == group_ptr->np) ) {
      MPIR_Group_dup ( MPIR_GROUP_EMPTY, &new_group_ptr );
      *newgroup = new_group_ptr->self;
      return MPI_SUCCESS;
  }
  
  /* Check for no ranks to exclude */
  if ( n <= 0 ) {
    MPIR_Group_dup ( group_ptr, &new_group_ptr );
    *newgroup = new_group_ptr->self;
    return (mpi_errno);
  }

  /* Check that the ranks are in range, at least (still need to check for
     duplicates) */
  for (i=0; i<n; i++) {
      if (ranks[i] < 0 || ranks[i] >= group_ptr->np) {
	  mpi_errno = MPIR_Err_setmsg( MPI_ERR_RANK, MPIR_ERR_RANK_ARRAY, 
			       myname, 
			 (char *)0, (char *)0, i, ranks[i], group_ptr->np );
	  return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
	  }
  /*** Check for duplicate ranks - Debbie Swider 11/18/97 ***/
      else {
          for (j=i+1; j<n; j++) {
	      if (ranks[i] == ranks[j]) {
		  mpi_errno = MPIR_Err_setmsg( MPI_ERR_RANK, MPIR_ERR_DUP_RANK,
					       myname,
					       (char *)0,(char *)0,i,ranks[i],
					       j );
		  return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
	      }
	  }
      }
  }


  /* Create the new group */
  MPIR_ALLOC(new_group_ptr,NEW(struct MPIR_GROUP),MPIR_COMM_WORLD, 
	     MPI_ERR_EXHAUSTED, myname );
  new_group_ptr->self = MPIR_FromPointer( new_group_ptr );
  *newgroup = new_group_ptr->self;
  MPIR_SET_COOKIE(new_group_ptr,MPIR_GROUP_COOKIE)
  new_group_ptr->ref_count	    = 1;
  new_group_ptr->local_rank	    = MPI_UNDEFINED;
  new_group_ptr->permanent      = 0;
  new_group_ptr->set_mark	    = (int *)0;
  new_group_ptr->np             = group_ptr->np - n;
  MPIR_ALLOC(new_group_ptr->lrank_to_grank,
	     (int *)MALLOC(new_group_ptr->np * sizeof(int)),
	     MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED, myname );

  /* Allocate set marking space for group if necessary */
  if (group_ptr->set_mark == NULL) {
      MPIR_ALLOC(group_ptr->set_mark,(int *) MALLOC( group_ptr->np * sizeof(int) ),
		 MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED, myname );
  }
  (void) memset( group_ptr->set_mark, (char)0, group_ptr->np * sizeof(int) );

  /* Mark the ranks to be excluded */
  for (i=0; i<n; i++) {
    if ( ((rank = ranks[i]) < group_ptr->np) && (rank >= 0) ) 
	/* Is this test duplicate code? */
	if (group_ptr->set_mark[rank] == MPIR_MARKED) {
	    /* Do not allow duplicate ranks */
	    mpi_errno = MPIR_Err_setmsg( MPI_ERR_RANK, MPIR_ERR_DUP_RANK, 
					 myname, 
					 (char *)0, (char *)0, i, rank, -1 );
	    return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
	}
	else
	    group_ptr->set_mark[rank] = MPIR_MARKED;
    else {
	/* Out of range rank in input */
	mpi_errno = MPIR_Err_setmsg( MPI_ERR_RANK, MPIR_ERR_DEFAULT, myname, 
				     (char *)0, (char *)0, rank );
        return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
    }
  }

  /* Fill in the lrank_to_grank list */
  for (i=j=0; i < group_ptr->np ; i++) 
    if ( (group_ptr->set_mark[i] == MPIR_UNMARKED) && (j < new_group_ptr->np ) ) {
      if (group_ptr->local_rank == i)
        new_group_ptr->local_rank = j;
      new_group_ptr->lrank_to_grank[j++] = group_ptr->lrank_to_grank[i];
    }

  /* Determine the previous and next powers of 2 */
  MPIR_Powers_of_2 ( new_group_ptr->np, &(new_group_ptr->N2_next), &(new_group_ptr->N2_prev) );

  TR_POP;

  return MPI_SUCCESS;
}
