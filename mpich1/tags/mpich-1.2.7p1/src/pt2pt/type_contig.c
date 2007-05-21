/*
 *  $Id: type_contig.c,v 1.9 2001/11/14 20:10:06 ashton Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Type_contiguous = PMPI_Type_contiguous
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Type_contiguous  MPI_Type_contiguous
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Type_contiguous as PMPI_Type_contiguous
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
    MPI_Type_contiguous - Creates a contiguous datatype

Input Parameters:
+ count - replication count (nonnegative integer) 
- oldtype - old datatype (handle) 

Output Parameter:
. newtype - new datatype (handle) 

.N fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_TYPE
.N MPI_ERR_COUNT
.N MPI_ERR_EXHAUSTED
@*/
int MPI_Type_contiguous( 
	int count,
	MPI_Datatype old_type,
	MPI_Datatype *newtype)
{
  struct MPIR_DATATYPE *dteptr;
  int mpi_errno = MPI_SUCCESS;
  struct MPIR_DATATYPE *old_dtype_ptr;
  static char myname[] = "MPI_TYPE_CONTIGUOUS";

  TR_PUSH(myname);
  /* Check for bad arguments */
  old_dtype_ptr   = MPIR_GET_DTYPE_PTR(old_type);
  MPIR_TEST_DTYPE(old_type,old_dtype_ptr,MPIR_COMM_WORLD,myname);
  if ( 
   ( (count   <  0)                  && (mpi_errno = MPI_ERR_COUNT) ) ||
   ( (old_dtype_ptr->dte_type == MPIR_UB) && (mpi_errno = MPI_ERR_TYPE) )  ||
   ( (old_dtype_ptr->dte_type == MPIR_LB) && (mpi_errno = MPI_ERR_TYPE) ) )
	return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno,
					  "MPI_TYPE_CONTIGUOUS" );

  MPIR_ALLOC(dteptr,(struct MPIR_DATATYPE *) MPIR_SBalloc( MPIR_dtes ),
	     MPIR_COMM_WORLD,MPI_ERR_EXHAUSTED, myname );
  *newtype = (MPI_Datatype) MPIR_FromPointer( dteptr );
  dteptr->self = *newtype;
  /* Are we making a null datatype? */
  if (count == 0) {
      /* (*newtype) = MPI_DATATYPE_NULL; */
      MPIR_SET_COOKIE(dteptr,MPIR_DATATYPE_COOKIE)
      dteptr->dte_type    = MPIR_CONTIG;
      dteptr->committed   = 0;
      dteptr->basic       = 0;
      dteptr->permanent   = 0;
      dteptr->ref_count   = 1;
      dteptr->align       = 4;
      dteptr->stride      = 1;
      dteptr->blocklen    = 1;
      dteptr->is_contig   = 1;
      dteptr->elements    = 0;
      dteptr->has_ub      = 0;
      dteptr->has_lb      = 0;
      dteptr->count       = 0;
      dteptr->lb          = 0;
      dteptr->has_lb      = 0;
      dteptr->extent      = 0;
      dteptr->ub          = 0;
      dteptr->has_ub      = 0;
      dteptr->size        = 0;
      dteptr->real_lb     = 0;
      dteptr->real_ub     = 0;
      dteptr->self        = *newtype;
      dteptr->old_type    = MPIR_Type_dup (old_dtype_ptr);
#     if defined(MPID_HAS_TYPE_CONTIGUOUS)
      {
	  mpi_errno = MPID_Type_contiguous(count, old_type, *newtype);
      }
#     endif      
      TR_POP;
      return (mpi_errno);
      }

  /* Create and fill in the datatype */
  MPIR_SET_COOKIE(dteptr,MPIR_DATATYPE_COOKIE)
  dteptr->dte_type    = MPIR_CONTIG;
  dteptr->committed   = 0;
  dteptr->basic       = 0;
  dteptr->permanent   = 0;
  dteptr->ref_count   = 1;
  dteptr->align       = old_dtype_ptr->align;
  dteptr->stride      = 1;
  dteptr->blocklen    = 1;
  dteptr->is_contig   = old_dtype_ptr->is_contig;
  dteptr->elements    = count * old_dtype_ptr->elements;
  dteptr->self        = *newtype;
  dteptr->has_ub      = 0;
  dteptr->has_lb      = 0;

  /* Take care of contiguous vs non-contiguous case.
     Note that some datatypes (MPIR_STRUCT) that are marked 
     as contiguous (byt the code in MPI_Type_commit) may have not have
     an old_type.
   */
  if (old_dtype_ptr->is_contig && old_dtype_ptr->old_type) {
	dteptr->old_type  = MPIR_Type_dup (old_dtype_ptr->old_type);
	dteptr->count     = count * old_dtype_ptr->count;
  }
  else {
	dteptr->old_type  = MPIR_Type_dup (old_dtype_ptr);
	dteptr->count     = count;
  }

  /* Set the upper/lower bounds and the extent and size */
  dteptr->lb          = dteptr->old_type->lb;
  dteptr->has_lb      = dteptr->old_type->has_lb;
  dteptr->extent      = (MPI_Aint)dteptr->count * dteptr->old_type->extent;
  /* 
     If the old type has an explicit ub, then the ub for this type is
     the location of that ub as updated by the count of this datatype.
     I.e., if there is 2 x {(int,0),(ub,8)}, the effective type signature is
     {(int,0),(ub,8),(int,8),(ub,16)}, and the ub is at 16, not 8.
     Note that the offset of each datatype's displacement is in terms of
     the extent of the original type.  This applies even to the ub and
     lb (since the extend is non-negative, we don't need to adjust the
     lb).
   */
  if (dteptr->old_type->has_ub) {
      dteptr->ub     = dteptr->old_type->ub + 
	  (count - 1) *dteptr->old_type->extent;
      dteptr->has_ub = 1;
      }
  else 
      dteptr->ub          = dteptr->lb + dteptr->extent;
  dteptr->size        = (dteptr->count * dteptr->old_type->size);
  dteptr->real_lb     = dteptr->old_type->real_lb;
  /* The value for real_ub here is an overestimate, but getting the
     "best" value is a bit complicated.  Note that for count == 1,
      this formula gives old_type->real_ub, independent of real_lb. */
  dteptr->real_ub     = (MPI_Aint)dteptr->count * 
      (dteptr->old_type->real_ub - dteptr->old_type->real_lb) + 
	  dteptr->old_type->real_lb;
  
# if defined(MPID_HAS_TYPE_CONTIGUOUS)
  {
      mpi_errno = MPID_Type_contiguous(count, old_type, *newtype);
  }
# endif      

  TR_POP;
  return (mpi_errno);
}
