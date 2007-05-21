/*
 *  $Id: type_ind.c,v 1.9 2001/11/14 20:10:08 ashton Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Type_indexed = PMPI_Type_indexed
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Type_indexed  MPI_Type_indexed
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Type_indexed as PMPI_Type_indexed
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

/*@
    MPI_Type_indexed - Creates an indexed datatype

Input Parameters:
+ count - number of blocks -- also number of entries in indices and blocklens
. blocklens - number of elements in each block (array of nonnegative integers) 
. indices - displacement of each block in multiples of old_type (array of 
  integers)
- old_type - old datatype (handle) 

Output Parameter:
. newtype - new datatype (handle) 

.N fortran

The indices are displacements, and are based on a zero origin.  A common error
is to do something like to following
.vb
    integer a(100)
    integer blens(10), indices(10)
    do i=1,10
         blens(i)   = 1
10       indices(i) = 1 + (i-1)*10
    call MPI_TYPE_INDEXED(10,blens,indices,MPI_INTEGER,newtype,ierr)
    call MPI_TYPE_COMMIT(newtype,ierr)
    call MPI_SEND(a,1,newtype,...)
.ve
expecting this to send 'a(1),a(11),...' because the indices have values 
'1,11,...'.   Because these are `displacements` from the beginning of 'a',
it actually sends 'a(1+1),a(1+11),...'.

If you wish to consider the displacements as indices into a Fortran array,
consider declaring the Fortran array with a zero origin
.vb
    integer a(0:99)
.ve

.N Errors
.N MPI_ERR_COUNT
.N MPI_ERR_TYPE
.N MPI_ERR_ARG
.N MPI_ERR_EXHAUSTED
@*/
int MPI_Type_indexed( 
	int count, 
	int blocklens[], 
	int indices[], 
	MPI_Datatype old_type, 
	MPI_Datatype *newtype )
{
  MPI_Aint      *hindices;
  int           i, mpi_errno = MPI_SUCCESS;
  int           total_count;
  struct MPIR_DATATYPE *old_dtype_ptr;
  static char myname[] = "MPI_TYPE_INDEXED";
  MPIR_ERROR_DECL;

  TR_PUSH(myname);
  /* Check for bad arguments */
  old_dtype_ptr   = MPIR_GET_DTYPE_PTR(old_type);
  MPIR_TEST_DTYPE(old_type,old_dtype_ptr,MPIR_COMM_WORLD,myname);
  if ( 
   ( (count    <  0)                 && (mpi_errno = MPI_ERR_COUNT) ) ||
   ( (old_dtype_ptr->dte_type == MPIR_UB) && (mpi_errno = MPI_ERR_TYPE) )  ||
   ( (old_dtype_ptr->dte_type == MPIR_LB) && (mpi_errno = MPI_ERR_TYPE) ) )
	return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno,myname);
	
  /* Are we making a null datatype? */
  total_count = 0;
  for (i=0; i<count; i++) {
      total_count += blocklens[i];
      if (blocklens[i] < 0) {
	  mpi_errno = MPIR_Err_setmsg( MPI_ERR_ARG, MPIR_ERR_ARG_ARRAY_VAL,
				       myname, (char *)0, (char *)0,
				       "blocklens", i, blocklens[i] );
	  return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno,myname);
      }
  }
  if (total_count == 0) {
      return MPI_Type_contiguous( 0, MPI_INT, newtype );
      }

  /* Generate a call to MPI_Type_hindexed instead.  This means allocating
     a temporary displacement array, multiplying all displacements
     by extent(old_type), and using that */
  MPIR_ALLOC(hindices,(MPI_Aint *)MALLOC(count*sizeof(MPI_Aint)),
	     MPIR_COMM_WORLD,MPI_ERR_EXHAUSTED,myname);
  for (i=0; i<count; i++) {
      hindices[i] = (MPI_Aint)indices[i] * old_dtype_ptr->extent;
  }
  MPIR_ERROR_PUSH(MPIR_COMM_WORLD);
  mpi_errno = MPI_Type_hindexed( count, blocklens, hindices, old_type, 
				 newtype );
  MPIR_ERROR_POP(MPIR_COMM_WORLD);
  FREE(hindices);
  TR_POP;
  MPIR_RETURN(MPIR_COMM_WORLD,mpi_errno, myname);
}
