/*
 *  $Id: type_struct.c,v 1.19 2002/07/12 13:53:06 gropp Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Type_struct = PMPI_Type_struct
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Type_struct  MPI_Type_struct
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Type_struct as PMPI_Type_struct
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

/* These values are determined during configure using 
   the util/structlayout.c program */
#ifdef USE_BASIC_TWO_ALIGNMENT
#define ALIGNMENT_VALUE 2
#elif defined(USE_BASIC_FOUR_ALIGNMENT)
#define ALIGNMENT_VALUE 4
#elif defined(USE_BASIC_EIGHT_ALIGNMENT)
#define ALIGNMENT_VALUE 8
#else
#define ALIGNMENT_VALUE 0
#endif

/*@
    MPI_Type_struct - Creates a struct datatype

Input Parameters:
+ count - number of blocks (integer) -- also number of 
entries in arrays array_of_types ,
array_of_displacements  and array_of_blocklengths  
. blocklens - number of elements in each block (array)
. indices - byte displacement of each block (array)
- old_types - type of elements in each block (array 
of handles to datatype objects) 

Output Parameter:
. newtype - new datatype (handle) 

Notes:
If an upperbound is set explicitly by using the MPI datatype 'MPI_UB', the
corresponding index must be positive.

The MPI standard originally made vague statements about padding and alignment;
this was intended to allow the simple definition of structures that could
be sent with a count greater than one.  For example,
.vb
    struct { int a; char b; } foo;
.ve
may have 'sizeof(foo) > sizeof(int) + sizeof(char)'; for example, 
'sizeof(foo) == 2*sizeof(int)'.  The initial version of the MPI standard
defined the extent of a datatype as including an `epsilon` that would have 
allowed an implementation to make the extent an MPI datatype
for this structure equal to '2*sizeof(int)'.  
However, since different systems might define different paddings, there was 
much discussion by the MPI Forum about what was the correct value of
epsilon, and one suggestion was to define epsilon as zero.
This would have been the best thing to do in MPI 1.0, particularly since 
the 'MPI_UB' type allows the user to easily set the end of the structure.
Unfortunately, this change did not make it into the final document.  
Currently, this routine does not add any padding, since the amount of 
padding needed is determined by the compiler that the user is using to
build their code, not the compiler used to construct the MPI library.
A later version of MPICH may provide for some natural choices of padding
(e.g., multiple of the size of the largest basic member), but users are
advised to never depend on this, even with vendor MPI implementations.
Instead, if you define a structure datatype and wish to send or receive
multiple items, you should explicitly include an 'MPI_UB' entry as the
last member of the structure.  For example, the following code can be used
for the structure foo
.vb
    blen[0] = 1; indices[0] = 0; oldtypes[0] = MPI_INT;
    blen[1] = 1; indices[1] = &foo.b - &foo; oldtypes[1] = MPI_CHAR;
    blen[2] = 1; indices[2] = sizeof(foo); oldtypes[2] = MPI_UB;
    MPI_Type_struct( 3, blen, indices, oldtypes, &newtype );
.ve

.N fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_TYPE
.N MPI_ERR_COUNT
.N MPI_ERR_EXHAUSTED
@*/
int MPI_Type_struct( 
	int count, 
	int blocklens[], 
	MPI_Aint indices[], 
	MPI_Datatype old_types[], 
	MPI_Datatype *newtype )
{
  struct MPIR_DATATYPE* dteptr;
  MPI_Aint        ub, lb, high, low, real_ub, real_lb, real_init;
  int             high_init = 0, low_init = 0;
  int             i, mpi_errno = MPI_SUCCESS;
  MPI_Aint        ub_marker = 0, lb_marker = 0; /* to suppress warnings */
  MPI_Aint        ub_found = 0, lb_found = 0;
  int             size, total_count;
  static char myname[] = "MPI_TYPE_STRUCT";

  /* Check for bad arguments */
  if ( count < 0 ) {
	mpi_errno = MPIR_Err_setmsg( MPI_ERR_COUNT, MPIR_ERR_DEFAULT, myname,
				   (char *)0, (char *)0, count );
	return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
  }

  if (count == 0) 
      return MPI_Type_contiguous( 0, MPI_INT, newtype );

  /* Check blocklens and old_types arrays and find number of bound */
  /* markers */
  total_count = 0;
  for (i=0; i<count; i++) {
    total_count += blocklens[i];
    if ( blocklens[i] < 0) {
	mpi_errno = MPIR_Err_setmsg( MPI_ERR_ARG, MPIR_ERR_ARG_ARRAY_VAL,
				     myname, (char *)0, (char *)0,
				     "blocklens", i, blocklens[i] );
	return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno,myname);
    }
    if ( old_types[i] == MPI_DATATYPE_NULL ) {
	mpi_errno = MPIR_Err_setmsg( MPI_ERR_TYPE, MPIR_ERR_TYPE_ARRAY_NULL,
				     myname, (char *)0, (char *)0, 
				     "old_types", i );
      return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
    }
  }
  if (total_count == 0) {
      return MPI_Type_contiguous( 0, MPI_INT, newtype );
  }

  /* Create and fill in the datatype */
  MPIR_ALLOC(dteptr,(struct MPIR_DATATYPE *) MPIR_SBalloc( MPIR_dtes ),MPIR_COMM_WORLD, 
	     MPI_ERR_EXHAUSTED, myname );
  *newtype = (MPI_Datatype) MPIR_FromPointer( dteptr );
  dteptr->self = *newtype;
  MPIR_SET_COOKIE(dteptr,MPIR_DATATYPE_COOKIE)
  dteptr->dte_type    = MPIR_STRUCT;
  dteptr->committed   = 0;
  dteptr->basic       = 0;
  dteptr->permanent   = 0;
  dteptr->is_contig   = 0;
  dteptr->ref_count   = 1;
  dteptr->count       = count;
  dteptr->elements    = 0;
  dteptr->size        = 0;
  dteptr->align       = 1;
  dteptr->has_ub      = 0;
  dteptr->has_lb      = 0;
  dteptr->self        = *newtype;

  /* Create indices and blocklens arrays and fill them */
  dteptr->indices     = ( MPI_Aint * ) MALLOC( count * sizeof( MPI_Aint ) );
  dteptr->blocklens   = ( int * )      MALLOC( count * sizeof( int ) );
  dteptr->old_types   =
       ( struct MPIR_DATATYPE ** )MALLOC(count*sizeof(struct MPIR_DATATYPE *));
  if (!dteptr->indices || !dteptr->blocklens || !dteptr->old_types) 
      return MPIR_ERROR( MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED, 
			 "MPI_TYPE_STRUCT" );
  high = low = ub = lb = 0;
  real_ub   = real_lb = 0;
  real_init = 0;

/* If data alignment is 2, 4, or 8, then assign dteptr->align to that
   value.  If 0, then assign dteptr->align to the maximal alignment 
   requirement. (done below) */
  if (ALIGNMENT_VALUE > 0)
      dteptr->align = ALIGNMENT_VALUE;

  for (i = 0; i < count; i++)  {
      struct MPIR_DATATYPE *old_dtype_ptr;

      old_dtype_ptr   = MPIR_GET_DTYPE_PTR(old_types[i]);
      MPIR_TEST_DTYPE(old_types[i],old_dtype_ptr,MPIR_COMM_WORLD,
		      "MPI_TYPE_STRUCT");
      dteptr->old_types[i]  = MPIR_Type_dup (old_dtype_ptr);
      dteptr->indices[i]    = indices[i];
      dteptr->blocklens[i]  = blocklens[i];

      /* Keep track of maximal alignment requirement */
      if (ALIGNMENT_VALUE == 0) {
	  if (dteptr->align < old_dtype_ptr->align)
	      dteptr->align       = old_dtype_ptr->align; 
      }
      if ( old_dtype_ptr->dte_type == MPIR_UB ) {
	  if (ub_found) {
	      if (indices[i] > ub_marker)
		  ub_marker = indices[i];
	      }
	  else {
	      ub_marker = indices[i];
	      ub_found  = 1;
	      }
	  }
      else if ( old_dtype_ptr->dte_type == MPIR_LB ) {
	   if (lb_found) { 
	      if ( indices[i] < lb_marker ) {
		  lb_marker = indices[i];
	      }
	  }
	  else {
	      lb_marker = indices[i];
	      lb_found  = 1;
	      }
	  }
      else {
	  /* Since the datatype is NOT a UB or LB, save the real limits */
	  if (!real_init) {
	      real_init = 1;
	      real_lb = old_dtype_ptr->real_lb;
	      real_ub = old_dtype_ptr->real_ub;
	      }
	  else {
	      if (old_dtype_ptr->real_lb < real_lb) 
		  real_lb = old_dtype_ptr->real_lb;
	      if (old_dtype_ptr->real_ub > real_ub) 
		  real_ub = old_dtype_ptr->real_ub;
	      }
	  /* Next, check to see if datatype has an MPI_LB or MPI_UB
	     within it... 
	     Make sure to adjust the ub by the selected displacement
	     and blocklens (blocklens is like Type_contiguous)
	   */
	  if (old_dtype_ptr->has_ub) {
	      MPI_Aint ub_test;
	      ub_test = old_dtype_ptr->ub + indices[i] + 
		  (blocklens[i] - 1) * old_dtype_ptr->extent;
	      if (ub_marker < ub_test || !ub_found) ub_marker = ub_test;
	      ub_found = 1;
	      }
	  if (old_dtype_ptr->has_lb) {
	      if (!lb_found || lb_marker > (old_dtype_ptr->lb) + indices[i] ) 
		  lb_marker = old_dtype_ptr->lb + indices[i];
	      lb_found  = 1;
	      }
	  /* Get the ub/lb from the datatype (if a MPI_UB or MPI_LB was
	     found, then these values will be ignored). 
	     We use the lb of the old type and add the indices
	     value to it */
	  lb = indices[i] + old_dtype_ptr->lb;
	  ub = lb + (blocklens[i] * old_dtype_ptr->extent) ;
	  if (!high_init) { high = ub; high_init = 1; }
	  else if (ub > high) high = ub;
	  if (!low_init ) { low  = lb; low_init  = 1; }
	  else if (lb < low) low = lb;
	  if (ub > lb) {
	      if ( high < ub ) high = ub;
	      if ( low  > lb ) low  = lb;
	      }
	  else {
	      if ( high < lb ) high = lb;
	      if ( low  > ub ) low  = ub;
	      }
	  dteptr->elements += (blocklens[i] * old_dtype_ptr->elements);
	  } /* end else */
      if (i < count - 1) {
	  size = old_dtype_ptr->size * blocklens[i];
	  dteptr->size   += size; 
      }
      else {
	  dteptr->size     += (blocklens[i] * old_dtype_ptr->size);
      }
      } /* end for loop */
  
  /* Set the upper/lower bounds and the extent and size */
  if (lb_found) {
      dteptr->lb     = lb_marker;
      dteptr->has_lb = 1;
      }
  else 
      dteptr->lb = (low_init  ? low : 0);
  if (ub_found) {
      dteptr->ub     = ub_marker;
      dteptr->has_ub = 1;
      }
  else 
      dteptr->ub = (high_init ? high: 0);
  dteptr->extent      = dteptr->ub - dteptr->lb ;
  dteptr->real_ub     = real_ub;
  dteptr->real_lb     = real_lb;

  /* If there is no explicit ub/lb marker, make the extent/ub fit the
     alignment of the largest basic item, if that structure alignment is
     chosen */

  if (!lb_found && !ub_found) {
      MPI_Aint eps_offset;
      /* Since data is always offset by the extent, is the extent that
	 we must adjust. */
      eps_offset = dteptr->extent % dteptr->align;
      if (eps_offset > 0) {
	  dteptr->ub += (dteptr->align - eps_offset);
	  dteptr->extent = dteptr->ub - dteptr->lb;
      }
  }

# if defined(MPID_HAS_TYPE_STRUCT)
  {
      mpi_errno = MPID_Type_struct(count,
				   blocklens,
				   indices,
				   old_types,
				   *newtype);
  }
# endif      

  return (mpi_errno);
}
