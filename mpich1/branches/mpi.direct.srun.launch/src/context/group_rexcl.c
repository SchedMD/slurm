/*
 *  $Id: group_rexcl.c,v 1.9 2001/11/14 19:54:25 ashton Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Group_range_excl = PMPI_Group_range_excl
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Group_range_excl  MPI_Group_range_excl
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Group_range_excl as PMPI_Group_range_excl
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

MPI_Group_range_excl - Produces a group by excluding ranges of processes from
       an existing group

Input Parameters:
+ group - group (handle) 
. n - number of elements in array 'ranks' (integer) 
- ranges - a one-dimensional 
array of integer triplets of the
form (first rank, last rank, stride), indicating the ranks in
'group'  of processes to be excluded
from the output group 'newgroup' .  

Output Parameter:
. newgroup - new group derived from above, preserving the 
order in 'group'  (handle) 

Note:  
Currently, each of the ranks to exclude must be
a valid rank in the group and all elements must be distinct or the
function is erroneous.  This restriction is per the draft.

.N fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_GROUP
.N MPI_ERR_EXHAUSTED
.N MPI_ERR_RANK
.N MPI_ERR_ARG

.seealso: MPI_Group_free
@*/
int MPI_Group_range_excl ( MPI_Group group, int n, int ranges[][3], 
			   MPI_Group *newgroup )
{
  int i, j, first, last, stride;
  int np;
  struct MPIR_GROUP *group_ptr, *new_group_ptr;
  int mpi_errno = MPI_SUCCESS;
  static char myname[] = "MPI_GROUP_RANGE_EXCL";

  TR_PUSH(myname);

  /* Check for bad arguments */
  group_ptr = MPIR_GET_GROUP_PTR(group);
#ifndef MPIR_NO_ERROR_CHECKING
  /* MPIR_TEST_MPI_GROUP(group,group_ptr,MPIR_COMM_WORLD,myname); */
  MPIR_TEST_GROUP(group_ptr);
    if (mpi_errno)
	return MPIR_ERROR(MPIR_COMM_WORLD, mpi_errno, myname );
#endif

  /* Check for a EMPTY input group */
  if ( (group == MPI_GROUP_EMPTY) ) {
      MPIR_Group_dup ( MPIR_GROUP_EMPTY, &new_group_ptr );
      *newgroup = new_group_ptr->self;
      TR_POP;
      return (mpi_errno);
  }

  /* Check for no range ranks to exclude */
  if ( n == 0 ) {
    MPIR_Group_dup ( group_ptr, &new_group_ptr );
    *newgroup = new_group_ptr->self;
    return (mpi_errno);
  }

  if (n < 0) 
      return MPIR_ERROR( MPIR_COMM_WORLD, MPI_ERR_ARG, myname );

  /* Allocate set marking space for group if necessary */
  if (group_ptr->set_mark == NULL) {
      MPIR_ALLOC(group_ptr->set_mark,(int *) MALLOC( group_ptr->np * sizeof(int) ),
		 MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED, myname );
  }
  (void) memset( group_ptr->set_mark, (char)0, group_ptr->np * sizeof(int) );

  /* Mark the ranks to be excluded */
  np = group_ptr->np;  
  for (i=0; i<n; i++) {
    first = ranges[i][0]; last = ranges[i][1]; stride = ranges[i][2];
    if (stride != 0) {
	if ( (stride > 0 && first > last) ||
	     (stride < 0 && first < last) ) {
	    mpi_errno = MPIR_Err_setmsg( MPI_ERR_ARG, MPIR_ERR_ARG_STRIDE,
					 myname, 
		 "Range does not terminate", 
		 "Range (%d,%d,%d) does not terminate", first, last, stride );
	    return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
	}
	for ( j=first; j*stride <= last*stride; j += stride )
	  if ( (j < group_ptr->np) && (j >= 0) ) {
	      if (group_ptr->set_mark[j] == MPIR_UNMARKED) {
		  group_ptr->set_mark[j] = MPIR_MARKED;
		  np--;
	      }
	  }
	  else{
	      mpi_errno = MPIR_Err_setmsg( MPI_ERR_RANK, MPIR_ERR_DEFAULT, 
					   myname, (char *)0,(char *)0, j );
	      return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
	  }
    }
    else {
	mpi_errno = MPIR_Err_setmsg( MPI_ERR_ARG, MPIR_ERR_ARG_ZERO_STRIDE, 
				     myname, "Zero stride is incorrect",
				     (char *)0 );
	return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
    }
  }

  /* Check np to see if we have original group or if we have null group */
  if (np == 0) {
      MPIR_Group_dup ( MPIR_GROUP_EMPTY, &new_group_ptr );
      *newgroup = new_group_ptr->self;
      return (mpi_errno);
  }
  if (np == group_ptr->np) {
    MPIR_Group_dup ( group_ptr, &new_group_ptr );
    *newgroup = new_group_ptr->self;
    return (mpi_errno);
  }

  /* Create the new group */
  MPIR_ALLOC(new_group_ptr,NEW(struct MPIR_GROUP),MPIR_COMM_WORLD, 
	     MPI_ERR_EXHAUSTED, myname );
  *newgroup = (MPI_Group) MPIR_FromPointer( new_group_ptr );
  new_group_ptr->self = *newgroup;
  MPIR_SET_COOKIE(new_group_ptr,MPIR_GROUP_COOKIE)
  new_group_ptr->ref_count      = 1;
  new_group_ptr->permanent      = 0;
  new_group_ptr->local_rank     = MPI_UNDEFINED;
  new_group_ptr->set_mark       = (int *)0;
  new_group_ptr->np             = np;
  new_group_ptr->lrank_to_grank = (int *) MALLOC( np * sizeof(int) );
  if (!new_group_ptr->lrank_to_grank) {
	return MPIR_ERROR( MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED, myname );
  }
  
  /* Fill in new group */
  for (i=j=0; i < group_ptr->np ; i++) 
    if ( (group_ptr->set_mark[i] == MPIR_UNMARKED) && (j < new_group_ptr->np ) ) {
      if (group_ptr->local_rank == i)
        new_group_ptr->local_rank = j;
      new_group_ptr->lrank_to_grank[j++] = group_ptr->lrank_to_grank[i];
    }

  /* Determine the previous and next powers of 2 */
  MPIR_Powers_of_2 ( new_group_ptr->np, &(new_group_ptr->N2_next), 
		     &(new_group_ptr->N2_prev) );

  TR_POP;
  return (mpi_errno);
}

