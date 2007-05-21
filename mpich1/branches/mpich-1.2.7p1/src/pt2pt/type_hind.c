/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  $Id: type_hind.c,v 1.13 2003/04/07 13:34:18 gropp Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Type_hindexed = PMPI_Type_hindexed
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Type_hindexed  MPI_Type_hindexed
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Type_hindexed as PMPI_Type_hindexed
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPI_BUILD_PROFILING
#include "mpiprof.h"
/* Insert the prototypes for the PMPI routines */
#undef __MPI_BINDINGS
#include "binding.h"
#endif
#include "sbcnst2.h"
#define MPIR_SBalloc MPID_SBalloc
/* pt2pt for MPIR_Type_dup */
#include "mpipt2pt.h"

/*@
    MPI_Type_hindexed - Creates an indexed datatype with offsets in bytes

Input Parameters:
+ count - number of blocks -- also number of entries in indices and blocklens
. blocklens - number of elements in each block (array of nonnegative integers) 
. indices - byte displacement of each block (array of MPI_Aint) 
- old_type - old datatype (handle) 

Output Parameter:
. newtype - new datatype (handle) 

.N fortran

Also see the discussion for 'MPI_Type_indexed' about the 'indices' in Fortran.

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_TYPE
.N MPI_ERR_COUNT
.N MPI_ERR_EXHAUSTED
.N MPI_ERR_ARG
@*/
int MPI_Type_hindexed( 
	int count, 
	int blocklens[], 
	MPI_Aint indices[], 
	MPI_Datatype old_type, 
	MPI_Datatype *newtype )
{
  struct MPIR_DATATYPE *dteptr;
  MPI_Aint      ub, lb, high, low, real_ub, real_lb;
  int           i, mpi_errno = MPI_SUCCESS;
  int           total_count;
  struct MPIR_DATATYPE *old_dtype_ptr;
  MPI_Aint        ub_marker = 0, lb_marker = 0; /* to suppress warnings */
  MPI_Aint        ub_found = 0, lb_found = 0;
  static char myname[] = "MPI_TYPE_HINDEXED";
  
  /* Check for bad arguments */
  old_dtype_ptr   = MPIR_GET_DTYPE_PTR(old_type);
  MPIR_TEST_DTYPE(old_type,old_dtype_ptr,MPIR_COMM_WORLD,myname);
  if ( 
   ( (count    <  0)                 && (mpi_errno = MPI_ERR_COUNT) ) ||
   ( (old_dtype_ptr->dte_type == MPIR_UB) && (mpi_errno = MPI_ERR_TYPE) )  ||
   ( (old_dtype_ptr->dte_type == MPIR_LB) && (mpi_errno = MPI_ERR_TYPE) ) )
	return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno,
					  myname );
	
  /* Are we making a null datatype? */
  total_count = 0;
  for (i=0; i<count; i++) {
      if (blocklens[i] < 0) {
	  /* Should indicate specific arg type and value and element */
	  return MPIR_ERROR( MPIR_COMM_WORLD, MPI_ERR_ARG, myname );
      }
      total_count += blocklens[i];
  }
  if (total_count == 0) {
      return MPI_Type_contiguous( 0, MPI_INT, newtype );
      }

  /* Create and fill in the datatype */
  MPIR_ALLOC(dteptr,(struct MPIR_DATATYPE *) MPIR_SBalloc( MPIR_dtes ),
	     MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED,myname);
  *newtype = (MPI_Datatype)MPIR_FromPointer( dteptr );
  dteptr->self = *newtype;
  MPIR_SET_COOKIE(dteptr,MPIR_DATATYPE_COOKIE)
  dteptr->dte_type    = MPIR_HINDEXED;
  dteptr->committed   = 0;
  dteptr->basic       = 0;
  dteptr->permanent   = 0;
  dteptr->is_contig   = 0;
  dteptr->ref_count   = 1;
  dteptr->align       = old_dtype_ptr->align;
  dteptr->old_type    = MPIR_Type_dup (old_dtype_ptr);
  dteptr->count       = count;
  dteptr->elements    = 0;
  dteptr->has_ub      = old_dtype_ptr->has_ub;
  dteptr->has_lb      = old_dtype_ptr->has_lb;
  dteptr->self        = *newtype;

  /* Create indices and blocklens arrays and fill them */
  dteptr->indices     = ( MPI_Aint * ) MALLOC( count * sizeof( MPI_Aint ) );
  dteptr->blocklens   = ( int * ) MALLOC( count * sizeof( int ) );
  if (!dteptr->indices || !dteptr->blocklens) 
      return MPIR_ERROR( MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED, 
			 "MPI_TYPE_HINDEXED" );
  low                 = indices[0];
  high                = indices[0] + 
      ((MPI_Aint)blocklens[0] * old_dtype_ptr->extent);
  real_lb             = indices[0];
  real_ub             = real_lb;
  /*
   * Compute the ub and lb based on the indices and extent of the 
   * base type.  Note that these are *relative* to the old type.
   * The final lb and ub must be offset by the lb of the old type.
   */
  for (i = 0; i < count; i++)  {
	dteptr->indices[i]    = indices[i];
	dteptr->blocklens[i]  = blocklens[i];
	ub = indices[i] + (blocklens[i] * old_dtype_ptr->extent) ;
	lb = indices[i];
	if (ub > lb) {
	  if ( high < ub ) high = ub;
	  if ( low  > lb ) low  = lb;
	}
	else {
	  if ( high < lb ) high = lb;
	  if ( low  > ub ) low  = ub;
	}
	if (indices[i] < real_lb) real_lb = indices[i];
	if (indices[i] + 
	  ((MPI_Aint)blocklens[i] * (old_dtype_ptr->real_ub - old_dtype_ptr->real_lb)) >
	real_ub)
	    real_ub = indices[i] + 
	   (blocklens[i] * (old_dtype_ptr->real_ub - old_dtype_ptr->real_lb));

	/* Check for the datatype contains an explicit UB/LB */
	if (old_dtype_ptr->has_ub) {
	    MPI_Aint ub_test;
	    ub_test = old_dtype_ptr->ub + indices[i] + 
		(blocklens[i] - 1) * old_dtype_ptr->extent;
	    if (!ub_found || ub_marker < ub_test) ub_marker = ub_test;
	    ub_found = 1;
	}
	if (old_dtype_ptr->has_lb) {
	    MPI_Aint lb_test;
	    lb_test = old_dtype_ptr->lb + indices[i];
	    if (!lb_found || lb_marker > lb_test) lb_marker = lb_test;
	    lb_found = 1;
	}
	dteptr->elements     += blocklens[i];
  }

  /* Set the upper/lower bounds and the extent and size.
     Update all of these to reflect the lb of the old type */
  if (old_dtype_ptr->real_lb != 0) {
      low  += old_dtype_ptr->real_lb;
      high += old_dtype_ptr->real_lb;
      real_lb += old_dtype_ptr->real_lb;
      real_ub =+ old_dtype_ptr->real_lb;
  }
  if (old_dtype_ptr->has_lb) 
      dteptr->lb = lb_marker;
  else
      dteptr->lb = low;
  if (old_dtype_ptr->has_ub)
      dteptr->ub = ub_marker;
  else
      dteptr->ub = high;
  dteptr->extent  = dteptr->ub - dteptr->lb;
  dteptr->size	  = dteptr->elements * old_dtype_ptr->size;
  dteptr->real_ub = real_ub;
  dteptr->real_lb = real_lb;

  /* 
    dteptr->elements contains the number of elements in the top level
	type.  to get the total elements, we multiply by the number of elements
	in the old type.
  */
  dteptr->elements   *= old_dtype_ptr->elements;
  
# if defined(MPID_HAS_TYPE_HINDEXED)
  {
      mpi_errno = MPID_Type_hindexed(count,
				     blocklens,
				     indices,
				     old_type,
				     *newtype);
  }
# endif      

  return (mpi_errno);
}
