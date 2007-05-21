/*
 *  $Id: type_vec.c,v 1.9 2001/11/14 20:10:09 ashton Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Type_vector = PMPI_Type_vector
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Type_vector  MPI_Type_vector
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Type_vector as PMPI_Type_vector
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
    MPI_Type_vector - Creates a vector (strided) datatype

Input Parameters:
+ count - number of blocks (nonnegative integer) 
. blocklength - number of elements in each block 
(nonnegative integer) 
. stride - number of elements between start of each block (integer) 
- oldtype - old datatype (handle) 

Output Parameter:
. newtype - new datatype (handle) 

.N fortran
@*/
int MPI_Type_vector( 
	int count, 
	int blocklen, 
	int stride, 
	MPI_Datatype old_type, 
	MPI_Datatype *newtype )
{
  int           mpi_errno = MPI_SUCCESS;
  struct MPIR_DATATYPE *old_dtype_ptr;
  static char myname[] = "MPI_TYPE_VECTOR";
  MPIR_ERROR_DECL;

  TR_PUSH(myname);
  /* Check for bad arguments */
  old_dtype_ptr   = MPIR_GET_DTYPE_PTR(old_type);
  MPIR_TEST_DTYPE(old_type,old_dtype_ptr,MPIR_COMM_WORLD,myname);
  if ( 
   ( (count   <  0)                  && (mpi_errno = MPI_ERR_COUNT) ) ||
   ( (blocklen <  0)                 && (mpi_errno = MPI_ERR_ARG) )   ||
   ( (old_dtype_ptr->dte_type == MPIR_UB) && (mpi_errno = MPI_ERR_TYPE) )  ||
   ( (old_dtype_ptr->dte_type == MPIR_LB) && (mpi_errno = MPI_ERR_TYPE) ) )
	return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
	
  /* Handle the case where blocklen & stride make a contiguous type */
  MPIR_ERROR_PUSH(MPIR_COMM_WORLD);

  if ( (blocklen == stride) || (count    == 1) ) {
	MPIR_CALL_POP(MPI_Type_contiguous ( 
	    count * blocklen, old_type, newtype ),MPIR_COMM_WORLD,myname);
  }
  else {
      /* Reduce this to the hvector case */
  
      mpi_errno = MPI_Type_hvector ( count, blocklen, 
				     (MPI_Aint)stride * old_dtype_ptr->extent,
				     old_type, newtype );
  }
  MPIR_ERROR_POP(MPIR_COMM_WORLD);
  TR_POP;
  MPIR_RETURN(MPIR_COMM_WORLD,mpi_errno,myname);
}
