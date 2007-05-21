/*
 *  $Id: group_inter.c,v 1.9 2001/11/14 19:54:24 ashton Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Group_intersection = PMPI_Group_intersection
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Group_intersection  MPI_Group_intersection
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Group_intersection as PMPI_Group_intersection
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

MPI_Group_intersection - Produces a group as the intersection of two existing
                         groups

Input Parameters:
+ group1 - first group (handle) 
- group2 - second group (handle) 

Output Parameter:
. newgroup - intersection group (handle) 

.N fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_GROUP
.N MPI_ERR_EXHAUSTED

.seealso: MPI_Group_free
@*/
int MPI_Group_intersection ( MPI_Group group1, MPI_Group group2, 
			     MPI_Group *group_out )
{
  int        i, j, global_rank;
  struct MPIR_GROUP *group1_ptr, *group2_ptr, *new_group_ptr;
  int        n;
  int        mpi_errno = MPI_SUCCESS;
  static char myname[] = "MPI_GROUP_INTERSECTION";

  TR_PUSH(myname);

  group1_ptr = MPIR_GET_GROUP_PTR(group1);

  group2_ptr = MPIR_GET_GROUP_PTR(group2);

#ifndef MPIR_NO_ERROR_CHECKING
  /* MPIR_TEST_MPI_GROUP(group1,group1_ptr,MPIR_COMM_WORLD,myname); */
  /* MPIR_TEST_MPI_GROUP(group2,group2_ptr,MPIR_COMM_WORLD,myname); */
  MPIR_TEST_GROUP(group1_ptr);
  MPIR_TEST_GROUP(group2_ptr);
  if (mpi_errno)
      return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
#endif
  /* Check for EMPTY groups */
  if ( (group1 == MPI_GROUP_EMPTY) || (group2 == MPI_GROUP_EMPTY) ) {
      MPIR_Group_dup ( MPIR_GROUP_EMPTY, &new_group_ptr );
      *group_out = new_group_ptr->self;
      TR_POP;
      return (mpi_errno);
  }
  
  /* Set the number in the intersection */
  n = 0;

  /* Allocate set marking space for group1 if necessary */
  if (group1_ptr->set_mark == NULL) {
      MPIR_ALLOC(group1_ptr->set_mark,(int *) MALLOC( group1_ptr->np * sizeof(int) ),
		 MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED, myname );
  }

  /* Mark the intersection */
  for ( i=0; i<group1_ptr->np; i++ ) {
    group1_ptr->set_mark[i] = MPIR_UNMARKED;
    for ( j=0; j<group2_ptr->np; j++ ) 
      if ( group1_ptr->lrank_to_grank[i] == group2_ptr->lrank_to_grank[j] ) {
        group1_ptr->set_mark[i] = MPIR_MARKED;
        n++;
        break;
      }
  }

  /* If there is a null intersection */
  if ( n <= 0 ) {
	MPIR_Group_dup ( MPIR_GROUP_EMPTY, &new_group_ptr );
	*group_out = new_group_ptr->self;
	TR_POP;
	return (mpi_errno);
  }

  /* Create the new group */
  MPIR_ALLOC(new_group_ptr,NEW(struct MPIR_GROUP),MPIR_COMM_WORLD, 
	     MPI_ERR_EXHAUSTED, myname );
  *group_out = (MPI_Group) MPIR_FromPointer( new_group_ptr );
  new_group_ptr->self = *group_out;
  MPIR_SET_COOKIE(new_group_ptr,MPIR_GROUP_COOKIE)
  new_group_ptr->ref_count     = 1;
  new_group_ptr->permanent     = 0;
  new_group_ptr->local_rank    = MPI_UNDEFINED;
  new_group_ptr->set_mark      = (int *)0;

  /* Alloc memory for lrank_to_grank array */
  new_group_ptr->np             = n;
  new_group_ptr->lrank_to_grank = (int *) MALLOC( n * sizeof(int) );
  if (!new_group_ptr->lrank_to_grank) {
	return MPIR_ERROR( MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED, myname );
  }
    
  /* Fill in the space */
  for ( n=0, i=0; i<group1_ptr->np; i++ ) 
    if ( (group1_ptr->set_mark[i]==MPIR_MARKED) && (n < new_group_ptr->np) ) 
      new_group_ptr->lrank_to_grank[n++] = group1_ptr->lrank_to_grank[i];

  /* Find the local rank */
  global_rank = MPID_MyWorldRank;
  for( i=0; i<new_group_ptr->np; i++ )
    if ( global_rank == new_group_ptr->lrank_to_grank[i] ) {
      new_group_ptr->local_rank = i;
      break;
    }

  /* Determine the previous and next powers of 2 */
  MPIR_Powers_of_2 ( new_group_ptr->np, &(new_group_ptr->N2_next),
		     &(new_group_ptr->N2_prev) );
  TR_POP;

  return (mpi_errno);
}
