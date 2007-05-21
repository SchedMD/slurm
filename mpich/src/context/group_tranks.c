/*
 *  $Id: group_tranks.c,v 1.9 2001/11/14 19:54:26 ashton Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Group_translate_ranks = PMPI_Group_translate_ranks
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Group_translate_ranks  MPI_Group_translate_ranks
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Group_translate_ranks as PMPI_Group_translate_ranks
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPI_BUILD_PROFILING
#include "mpiprof.h"
/* Insert the prototypes for the PMPI routines */
#undef __MPI_BINDINGS
#include "binding.h"
#endif

/*@

MPI_Group_translate_ranks - Translates the ranks of processes in one group to 
                            those in another group

Input Parameters:
+ group1 - group1 (handle) 
. n - number of ranks in  'ranks1' and 'ranks2'  arrays (integer) 
. ranks1 - array of zero or more valid ranks in 'group1' 
- group2 - group2 (handle) 

Output Parameter:
. ranks2 - array of corresponding ranks in group2,  'MPI_UNDEFINED'  when no 
correspondence exists. 

.N fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_GROUP
.N MPI_ERR_ARG
.N MPI_ERR_RANK

@*/
int MPI_Group_translate_ranks ( MPI_Group group_a, int n, int *ranks_a, 
				MPI_Group group_b, int *ranks_b )
{
  int i, j;
  int pid_a, rank_a;
  struct MPIR_GROUP *group_a_ptr, *group_b_ptr;
  int mpi_errno = MPI_SUCCESS;
  static char myname[] = "MPI_GROUP_TRANSLATE_RANKS";

  TR_PUSH(myname);

  group_a_ptr = MPIR_GET_GROUP_PTR(group_a);

  group_b_ptr = MPIR_GET_GROUP_PTR(group_b);

  /* Check for bad arguments */
#ifndef MPIR_NO_ERROR_CHECKING
  MPIR_TEST_GROUP(group_a_ptr);
  MPIR_TEST_GROUP(group_b_ptr );
/*  MPIR_TEST_MPI_GROUP(group_a,group_a_ptr,MPIR_COMM_WORLD,myname);
  MPIR_TEST_MPI_GROUP(group_b,group_b_ptr,MPIR_COMM_WORLD,myname); */
  if (n <= 0) {
      mpi_errno = MPIR_Err_setmsg( MPI_ERR_ARG, MPIR_ERR_ARG_NAMED, myname,
				   (char *)0, (char *)0, "n", n );
  }
  MPIR_TEST_ARG(ranks_a);
  MPIR_TEST_ARG(ranks_b);
  if (mpi_errno)
	return MPIR_ERROR(MPIR_COMM_WORLD, mpi_errno, myname );
#endif

  /* Set ranks_b array to MPI_UNDEFINED */
  for ( i=0; i<n; i++ )
    ranks_b[i] = MPI_UNDEFINED;
 
  /* Translate the ranks in ranks_a to ranks_b */
  for ( i=0 ; i<n; i++ ) {
      if ( ((rank_a = ranks_a[i]) >= group_a_ptr->np) || (rank_a < 0) ) {
	  mpi_errno = MPIR_Err_setmsg( MPI_ERR_RANK, MPIR_ERR_DEFAULT, myname, 
				       (char *)0, (char *)0, rank_a );
	  return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
      }
    pid_a = group_a_ptr->lrank_to_grank[rank_a];
    for ( j=0; j<group_b_ptr->np; j++ ) 
      if ( pid_a == group_b_ptr->lrank_to_grank[j] ) {
        ranks_b[i] = j;
        break;
      }
  }
  TR_POP;
  return MPI_SUCCESS;
}
