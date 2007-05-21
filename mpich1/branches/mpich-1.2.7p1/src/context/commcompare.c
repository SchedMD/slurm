/*
 *  $Id: commcompare.c,v 1.9 2001/11/14 19:54:22 ashton Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Comm_compare = PMPI_Comm_compare
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Comm_compare  MPI_Comm_compare
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Comm_compare as PMPI_Comm_compare
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

MPI_Comm_compare - Compares two communicators

Input Parameters:
+ comm1 - comm1 (handle) 
- comm2 - comm2 (handle) 

Output Parameter:
. result - integer which is 'MPI_IDENT' if the contexts and groups are the
same, 'MPI_CONGRUENT' if different contexts but identical groups, 'MPI_SIMILAR'
if different contexts but similar groups, and 'MPI_UNEQUAL' otherwise

Using 'MPI_COMM_NULL' with 'MPI_Comm_compare':

It is an error to use 'MPI_COMM_NULL' as one of the arguments to
'MPI_Comm_compare'.  The relevant sections of the MPI standard are 

$(2.4.1 Opaque Objects)
A null handle argument is an erroneous 'IN' argument in MPI calls, unless an
exception is explicitly stated in the text that defines the function.

$(5.4.1. Communicator Accessors)
<no text in 'MPI_COMM_COMPARE' allowing a null handle>

.N fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_ARG
@*/
int MPI_Comm_compare ( 
	MPI_Comm  comm1,
	MPI_Comm  comm2,
	int *result)
{
  int       mpi_errno = MPI_SUCCESS;
  MPI_Group group1, group2;
  struct MPIR_COMMUNICATOR *comm1_ptr, *comm2_ptr;
  static char myname[] = "MPI_COMM_COMPARE";

  comm1_ptr = MPIR_GET_COMM_PTR(comm1);
  MPIR_TEST_MPI_COMM(comm1,comm1_ptr,comm1_ptr,myname);
  comm2_ptr = MPIR_GET_COMM_PTR(comm2);
  MPIR_TEST_MPI_COMM(comm2,comm2_ptr,comm2_ptr,myname);

  /* Check for bad arguments */
  if ( ( (result == (int *)0)     && (mpi_errno = MPI_ERR_ARG) ) )
    return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );

  /* The original code checked for either comm1 or comm2 begin null, and
     in those cases returned either MPI_IDENT (both null) or MPI_UNEQUAL (only
     one null).  As described above, that isn't correct; if either 
     arg is MPI_COMM_NULL, the only allowed (by the MPI Standard) response
     is to signal an error 
   */

  /* Are they the same kind of communicator */
  if (comm1_ptr->comm_type != comm2_ptr->comm_type) {
	(*result) = MPI_UNEQUAL;
	return (mpi_errno);
  }

  /* See if they are identical */
  if (comm1 == comm2) {
	(*result) = MPI_IDENT;
	return (mpi_errno);
  }
	
  /* Comparison for intra-communicators */
  if (comm1_ptr->comm_type == MPIR_INTRA) {

	/* Get the groups and see what their relationship is */
	MPI_Comm_group (comm1, &group1);
	MPI_Comm_group (comm2, &group2);
	MPI_Group_compare ( group1, group2, result );

	/* They can't be identical since they're not the same
	   handle, they are congruent instead */
	if ((*result) == MPI_IDENT)
	  (*result) = MPI_CONGRUENT;

	/* Free the groups */
	MPI_Group_free (&group1);
	MPI_Group_free (&group2);
  }
  
  /* Comparison for inter-communicators */
  else {
	int       lresult, rresult;
	MPI_Group rgroup1, rgroup2;
	
	/* Get the groups and see what their relationship is */
	MPI_Comm_group (comm1, &group1);
	MPI_Comm_group (comm2, &group2);
	MPI_Group_compare ( group1, group2, &lresult );

	MPI_Comm_remote_group (comm1, &rgroup1);
	MPI_Comm_remote_group (comm2, &rgroup2);
	MPI_Group_compare ( rgroup1, rgroup2, &rresult );

	/* Choose the result that is "least" strong. This works 
	   due to the ordering of result types in mpi.h */
	(*result) = (rresult > lresult) ? rresult : lresult;

	/* They can't be identical since they're not the same
	   handle, they are congruent instead */
	if ((*result) == MPI_IDENT)
	  (*result) = MPI_CONGRUENT;

	/* Free the groups */
	MPI_Group_free (&group1);
	MPI_Group_free (&group2);
	MPI_Group_free (&rgroup1);
	MPI_Group_free (&rgroup2);
  }

  return (mpi_errno);
}
