/*
 *  $Id: groupcompare.c,v 1.8 2001/11/14 19:54:26 ashton Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Group_compare = PMPI_Group_compare
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Group_compare  MPI_Group_compare
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Group_compare as PMPI_Group_compare
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

MPI_Group_compare - Compares two groups

Input Parameters:
+ group1 - group1 (handle) 
- group2 - group2 (handle) 

Output Parameter:
. result - integer which is 'MPI_IDENT' if the order and members of
the two groups are the same, 'MPI_SIMILAR' if only the members are the same,
and 'MPI_UNEQUAL' otherwise

.N fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_GROUP
.N MPI_ERR_ARG
@*/
int MPI_Group_compare ( MPI_Group group1, MPI_Group group2, int *result )
{
  int       mpi_errno = MPI_SUCCESS;
  int       size1, size2;
  struct MPIR_GROUP *group1_ptr, *group2_ptr;
  MPI_Group group_int;
  int       size_int, i;
  static char myname[] = "MPI_GROUP_COMPARE";

  TR_PUSH(myname);

  group1_ptr = MPIR_GET_GROUP_PTR(group1);
  group2_ptr = MPIR_GET_GROUP_PTR(group2);

#ifndef MPIR_NO_ERROR_CHECKING
/*  MPIR_TEST_MPI_GROUP(group1,group1_ptr,MPIR_COMM_WORLD,myname);
  MPIR_TEST_MPI_GROUP(grou2p,group2_ptr,MPIR_COMM_WORLD,myname);*/
  MPIR_TEST_GROUP(group1_ptr);
  MPIR_TEST_GROUP(group2_ptr);
  MPIR_TEST_ARG(result);
  if (mpi_errno)
      return MPIR_ERROR(MPIR_COMM_WORLD, mpi_errno, myname );
#endif

  /* See if their sizes are equal */
  MPI_Group_size ( group1, &size1 );
  MPI_Group_size ( group2, &size2 );
  if ( size1 != size2 ) {
	(*result) = MPI_UNEQUAL;
	TR_POP;
	return MPI_SUCCESS;
  }

  /* Is their intersection the same size as the original group */
  MPI_Group_intersection ( group1, group2, &group_int );
  MPI_Group_size ( group_int, &size_int );
  MPI_Group_free ( &group_int );
  if ( size_int != size1 ) {
	(*result) = MPI_UNEQUAL;
	TR_POP;
	return MPI_SUCCESS;
  }

  /* Do a 1-1 comparison */
  (*result) = MPI_SIMILAR;
  for ( i=0; i<size1; i++ )
      if ( group1_ptr->lrank_to_grank[i] != group2_ptr->lrank_to_grank[i] ) {
	  TR_POP;
	  return MPI_SUCCESS;
      }
  (*result) = MPI_IDENT;

  TR_POP;
  return MPI_SUCCESS;
}
