/*
 *  $Id: type_hvec.c,v 1.9 2001/11/14 20:10:07 ashton Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Type_hvector = PMPI_Type_hvector
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Type_hvector  MPI_Type_hvector
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Type_hvector as PMPI_Type_hvector
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
    MPI_Type_hvector - Creates a vector (strided) datatype with offset in bytes

Input Parameters:
+ count - number of blocks (nonnegative integer) 
. blocklength - number of elements in each block 
(nonnegative integer) 
. stride - number of bytes between start of each block (integer) 
- old_type - old datatype (handle) 

Output Parameter:
. newtype - new datatype (handle) 

.N fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_TYPE
.N MPI_ERR_COUNT
.N MPI_ERR_ARG
.N MPI_ERR_EXHAUSTED
@*/
int MPI_Type_hvector( 
	int count, 
	int blocklen, 
	MPI_Aint stride, 
	MPI_Datatype old_type, 
	MPI_Datatype *newtype )
{
  struct MPIR_DATATYPE  *dteptr;
  int           mpi_errno = MPI_SUCCESS;
  struct MPIR_DATATYPE *old_dtype_ptr;
  static char myname[] = "MPI_TYPE_HVECTOR";

  /* Check for bad arguments */
  old_dtype_ptr   = MPIR_GET_DTYPE_PTR(old_type);
  MPIR_TEST_DTYPE(old_type,old_dtype_ptr,MPIR_COMM_WORLD,myname);
  if ( 
   ( (count   <  0)                  && (mpi_errno = MPI_ERR_COUNT) ) ||
   ( (blocklen <  0)                 && (mpi_errno = MPI_ERR_ARG) )   ||
   ( (old_dtype_ptr->dte_type == MPIR_UB) && (mpi_errno = MPI_ERR_TYPE) )  ||
   ( (old_dtype_ptr->dte_type == MPIR_LB) && (mpi_errno = MPI_ERR_TYPE) ) )
	return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );

  /* Are we making a null datatype? */
  if (count*blocklen == 0) {
      return MPI_Type_contiguous( 0, MPI_INT, newtype );
      }
	
  /* Handle the case where blocklen & stride make a contiguous type */
  if ( ((blocklen * old_dtype_ptr->extent) == stride) ||
	   (count                         == 1) )
	return MPI_Type_contiguous ( count * blocklen, old_type, newtype );

  /* Create and fill in the datatype */
  MPIR_ALLOC(dteptr,(struct MPIR_DATATYPE *) MPIR_SBalloc( MPIR_dtes ),MPIR_COMM_WORLD, 
	     MPI_ERR_EXHAUSTED, "MPI_TYPE_HVECTOR" );
  *newtype = (MPI_Datatype) MPIR_FromPointer( dteptr );
  dteptr->self = *newtype;
  MPIR_SET_COOKIE(dteptr,MPIR_DATATYPE_COOKIE)
  dteptr->dte_type    = MPIR_HVECTOR;
  dteptr->committed   = 0;
  dteptr->basic       = 0;
  dteptr->permanent   = 0;
  dteptr->is_contig   = 0;
  dteptr->ref_count   = 1;
  dteptr->align       = old_dtype_ptr->align;
  dteptr->elements    = count * blocklen * old_dtype_ptr->elements;
  dteptr->stride      = stride;
  dteptr->blocklen    = blocklen;
  dteptr->old_type    = MPIR_Type_dup (old_dtype_ptr);
  dteptr->count       = count;
  dteptr->has_ub      = old_dtype_ptr->has_ub;
  dteptr->has_lb      = old_dtype_ptr->has_lb;
  dteptr->self        = *newtype;

  if (old_dtype_ptr->has_ub) {
      if (stride > 0) {
	  dteptr->ub = old_dtype_ptr->ub + 
	      ((count-1) * stride) + ((blocklen - 1)* old_dtype_ptr->extent);
      }
      else {
	  dteptr->ub = old_dtype_ptr->ub;
      }
  }

  if (old_dtype_ptr->has_lb) {
      if (stride < 0) {
	  dteptr->lb = old_dtype_ptr->lb + 
	      ((count-1) * stride) + ((blocklen-1) * old_dtype_ptr->extent);
      }
      else {
	  dteptr->lb = old_dtype_ptr->lb;
      }
  }
  

  /* Set the upper/lower bounds and the extent and size */
  dteptr->extent      = ((count-1) * stride) + (blocklen * old_dtype_ptr->extent);
  if (dteptr->extent < 0) {
      if (! old_dtype_ptr->has_ub)
	  dteptr->ub     = old_dtype_ptr->lb;
      if (! old_dtype_ptr->has_lb) 
	  dteptr->lb     = dteptr->ub + dteptr->extent; 
	dteptr->real_ub= old_dtype_ptr->real_lb;
	dteptr->real_lb= dteptr->real_ub + 
	    ((count-1) * stride) + 
		(blocklen * (old_dtype_ptr->real_ub - old_dtype_ptr->real_lb));
	dteptr->extent = -dteptr->extent;
  }
  else {
      if (! old_dtype_ptr->has_lb) 
	  dteptr->lb     = old_dtype_ptr->lb;
      if (! old_dtype_ptr->has_ub)
	  dteptr->ub = dteptr->lb + dteptr->extent;
	dteptr->real_lb = old_dtype_ptr->real_lb;
	dteptr->real_ub = dteptr->real_lb + ((count-1) * stride) + 
		(blocklen * (old_dtype_ptr->real_ub - old_dtype_ptr->real_lb));  
  }
  dteptr->extent  = dteptr->ub - dteptr->lb;
  dteptr->size    = count * blocklen * dteptr->old_type->size;
  
# if defined(MPID_HAS_TYPE_HVECTOR)
  {
      mpi_errno = MPID_Type_hvector(count, 
				    blocklen, 
				    stride, 
				    old_type, 
				    *newtype);
  }
# endif      

  return (mpi_errno);
}
