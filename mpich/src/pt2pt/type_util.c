/*
 *  $Id: type_util.c,v 1.9 2004/04/29 19:11:46 karonis Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"
#include "sbcnst2.h"
#define MPIR_SBfree MPID_SBfree
/* pt2pt for MPIR_Type_xxx */
#include "mpipt2pt.h"

#ifndef MPIR_TRUE
#define MPIR_TRUE  1
#define MPIR_FALSE 0
#endif


/*+
    MPIR_Type_dup - Utility function used to "touch" a type

  Algorithm:
  Only non-permanent types can be touched.  Since we don't free
  permanent types until the finalize stage, the reference count
  is not used to determine whether or not the type is freed
  during normal program execution.
+*/
struct MPIR_DATATYPE * MPIR_Type_dup ( struct MPIR_DATATYPE *dtype_ptr )
{
  /* We increment the reference count even for the permanent types, so that 
     an eventual free (in MPI_Finalize) will correctly free these types */
    MPIR_REF_INCR(dtype_ptr);
  return (dtype_ptr);
}


/*+
  MPIR_Type_permanent - Utility function to mark a type as permanent
+*/
int MPIR_Type_permanent ( struct MPIR_DATATYPE *dtype_ptr )
{
  if (dtype_ptr)
    dtype_ptr->permanent = 1;
  return (MPI_SUCCESS);
}

/* 
   This version is used to free types that may include permanent types
   (as part of a derived datatype)

   Note that it is not necessary to commit a datatype (for example, one 
   that is used to define another datatype); but to free it, we must not
   require that it have been committed.
 */
int MPIR_Type_free ( struct MPIR_DATATYPE **dtype_ptr2 )
{
  int mpi_errno = MPI_SUCCESS;
  struct MPIR_DATATYPE *dtype_ptr;
  static char myname[] = "MPI_TYPE_FREE";

  /* Check for bad arguments */
  MPIR_TEST_ARG(dtype_ptr2);
  if (mpi_errno)
      return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );

  /* Freeing null datatypes succeeds silently */
  if ( *dtype_ptr2 == MPI_DATATYPE_NULL )
	return (MPI_SUCCESS);

  dtype_ptr   = *dtype_ptr2;
  MPIR_TEST_DTYPE(dtype_ptr->self,dtype_ptr,MPIR_COMM_WORLD,myname);

  /* We can't free permanent objects unless finalize has been called */
  if  ( ( dtype_ptr->permanent ) && MPIR_Has_been_initialized == 1) {
      if (dtype_ptr->ref_count > 1) {
	  MPIR_REF_DECR(dtype_ptr);
      }
      return MPI_SUCCESS;
      }

  /* Free datatype */
  if ( dtype_ptr->ref_count <= 1 ) {

      /* It would be better if each type new how to free itself */
      switch (dtype_ptr->dte_type) {
      case MPIR_INDEXED:
      case MPIR_HINDEXED:
	  FREE( dtype_ptr->indices );
	  FREE( dtype_ptr->blocklens );
	  if (!dtype_ptr->basic)
	      MPIR_Type_free( &dtype_ptr->old_type );
	  break;
      case MPIR_STRUCT:
	  MPIR_Free_struct_internals( dtype_ptr );
	  break;
      default:
	  if (!dtype_ptr->basic)
	      MPIR_Type_free( &dtype_ptr->old_type );
      }

#     if defined(MPID_HAS_TYPE_FREE)
      {
	  /* Give the device a chance to free up any resources it may have
	     associated with this type */
	  mpi_errno = MPID_Type_free(dtype_ptr->self);
      }
#     endif    


	/* Free the datatype structure */
	MPIR_CLR_COOKIE(dtype_ptr);
	/* If the type is permanent and in static storage, we can't
	   free it here */
	if  ( !dtype_ptr->permanent ) {
	    MPIR_RmPointer( dtype_ptr->self );
	    MPIR_SBfree ( MPIR_dtes, dtype_ptr );
	    }
	else if (MPIR_Has_been_initialized == 2) {
	    /* We're in finalize, so delete the pointer mapping */
	    MPIR_RmPointer( dtype_ptr->self );
	}
  }
  else {
	MPIR_REF_DECR(dtype_ptr);
  }

  /* We have to do this because the permanent types are constants */
  if ( !dtype_ptr->permanent )
      (*dtype_ptr2) = 0;
  return (mpi_errno);
}

#ifdef FOO
/* Free the parts of a structure datatype */
void MPIR_Type_free_struct( struct MPIR_DATATYPE *dtype )
{
/* Free malloc'd memory for various datatypes */
    if ( (dtype->dte_type == MPIR_INDEXED)  ||
	 (dtype->dte_type == MPIR_HINDEXED) || 
	 (dtype->dte_type == MPIR_STRUCT)   ) {
	FREE ( dtype->indices );
	FREE ( dtype->blocklens );
    }

/* Free the old_type if not a struct */
    if ( (dtype->dte_type != MPIR_STRUCT) && (!dtype->basic) )
	MPIR_Type_free ( &(dtype->old_type) );

/* Free the old_types of a struct */
    if (dtype->dte_type == MPIR_STRUCT) {
	int i;

	/* Decrease the reference count */
	for (i=0; i<dtype->count; i++)
	    MPIR_Type_free ( &(dtype->old_types[i]) );
    
	/* Free the malloc'd memory */
	FREE ( dtype->old_types );
    }

/* Free the datatype structure */
    MPIR_CLR_COOKIE(dtype);
}
#endif

/*
   This routine returns the "real" lb and ub, ignoring any explicitly set 
   TYPE_LB or TYPE_UB.  This is needed when allocating space for a 
   datatype that includes all of the "holes" (note that MPI_TYPE_SIZE
   gives only the number of bytes that the selected elements occupy).
   This is needed for some of the collective routines.

   STILL NEEDS TO BE IMPLEMENTED IN THE TYPE ROUTINES
 */
void MPIR_Type_get_limits( 
	struct MPIR_DATATYPE *dtype_ptr,
	MPI_Aint *lb, 
	MPI_Aint *ub)
{
/*
    *lb = dtype->real_lb;
    *ub = dtype->real_ub;
 */
    *lb = dtype_ptr->lb;
    *ub = dtype_ptr->ub;
}

/* 
 * Routine to free a datatype
 */
void MPIR_Free_perm_type( MPI_Datatype datatype )
{
    struct MPIR_DATATYPE *dtype_ptr;

    dtype_ptr = MPIR_ToPointer( datatype );
    /* 
     * In some cases, particularly when Fortran is modified to make
     * REAL and DOUBLE PRECISION the same datatype, the datatype may
     * already have been freed.  This test causes free to return without
     * trying to free an already freed type.
     */
    if (dtype_ptr == NULL) 
	return;
    /* We can't set the type to not permanent, because the datatypes structures
       are in static storage, and permanent is how we know this */
       /* dtype_ptr->permanent = 0;*/
    /* Basic is used to determine what types don't have subtypes.
       If the actual type is MPIR_STRUCT, we can set this to 0 */
    if (dtype_ptr->dte_type == MPIR_STRUCT)
	dtype_ptr->basic = 0;
    /* We use dtype instead of dtype_ptr->self directly since
       if we DON'T free the type (ref count > 0), we will still need
       the self value.

       Alternately, we could simply delete the permtypes without paying
       any attention to the reference counts or the associated types.
     */
    MPIR_Type_free( &dtype_ptr );
}

/*
 * Routine to free INTERNALS of type struct, including the locally referenced
 * datatypes.
 */
void MPIR_Free_struct_internals( 
	struct MPIR_DATATYPE *dtype_ptr)
{
    int i;

    FREE ( dtype_ptr->indices );
    FREE ( dtype_ptr->blocklens );
    
    /* Decrease the reference count */
    for (i=0; i<dtype_ptr->count; i++)
	MPIR_Type_free ( &(dtype_ptr->old_types[i]) );
    
    /* Free the malloc'd memory */
    FREE ( dtype_ptr->old_types );
}

/*
 * Routine to return whether a datatype is contiguous
 */
void MPIR_Datatype_iscontig( MPI_Datatype dtype, int *flag )
{
    MPIR_DATATYPE_ISCONTIG(dtype,flag);
}
