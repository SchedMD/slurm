/* 
 *   $Id: subarray.c,v 1.11 2001/11/14 20:08:07 ashton Exp $    
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Type_create_subarray = PMPI_Type_create_subarray
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Type_create_subarray  MPI_Type_create_subarray
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Type_create_subarray as PMPI_Type_create_subarray
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
MPI_Type_create_subarray - Creates a datatype describing a subarray of a multidimensional array

Input Parameters:
+ ndims - number of array dimensions (positive integer)
. array_of_sizes - number of elements of type oldtype in each dimension of the full array (array of positive integers)
. array_of_subsizes - number of elements of type oldtype in each dimension of the subarray (array of positive integers)
. array_of_starts - starting coordinates of the subarray in each dimension (array of nonnegative integers)
. order - array storage order flag (state)
- oldtype - old datatype (handle)

Output Parameters:
. newtype - new datatype (handle)

.N fortran
@*/
int MPI_Type_create_subarray(
	int ndims, 
	int *array_of_sizes, 
	int *array_of_subsizes, 
	int *array_of_starts, 
	int order, 
	MPI_Datatype oldtype, 
	MPI_Datatype *newtype)
{
    MPI_Aint extent, disps[3], size;
    int i, blklens[3];
    MPI_Datatype tmp1, tmp2, types[3];
    int mpi_errno=0;
    static char myname[] = "MPI_TYPE_CREATE_SUBARRAY";

    if (ndims <= 0) {
	mpi_errno = MPIR_Err_setmsg( MPI_ERR_ARG, MPIR_ERR_ARG_NAMED, myname, 
				     (char *)0, 
			     "Invalid %s argument = %d", "ndims", ndims );
	return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
    }
    MPIR_TEST_ARG(array_of_sizes);
    MPIR_TEST_ARG(array_of_subsizes);
    MPIR_TEST_ARG(array_of_starts);
    if (mpi_errno) 
	return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );

    for (i=0; i<ndims; i++) {
        if (array_of_sizes[i] <= 0) {
	    mpi_errno = MPIR_Err_setmsg( MPI_ERR_ARG, MPIR_ERR_ARG_ARRAY_VAL,
					 myname, (char *)0, (char *)0,
					 "array_of_sizes", i, 
					 array_of_sizes[i] );
	    return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
        }
        if (array_of_subsizes[i] <= 0) {
	    mpi_errno = MPIR_Err_setmsg( MPI_ERR_ARG, MPIR_ERR_ARG_ARRAY_VAL,
					 myname, (char *)0, (char *)0,
					 "array_of_subsizes", i, 
					 array_of_subsizes[i] );
	    return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
        }
        if (array_of_starts[i] < 0) {
	    mpi_errno = MPIR_Err_setmsg( MPI_ERR_ARG, MPIR_ERR_ARG_ARRAY_VAL,
					 myname, (char *)0, (char *)0,
					 "array_of_starts", i, 
					 array_of_starts[i] );
	    return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
        }
    }

    /* order argument checked below */

    if (oldtype == MPI_DATATYPE_NULL) {
	mpi_errno = MPIR_Err_setmsg( MPI_ERR_TYPE, MPIR_ERR_TYPE_NULL, 
				     myname, (char *)0, (char *)0 );
	return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
    }

    MPI_Type_extent(oldtype, &extent);

    if (order == MPI_ORDER_FORTRAN) {
      /* dimension 0 changes fastest */
	if (ndims == 1)
	    MPI_Type_contiguous(array_of_subsizes[0], oldtype, &tmp1);
	else {
	    MPI_Type_vector(array_of_subsizes[1], array_of_subsizes[0],
			    array_of_sizes[0], oldtype, &tmp1);
	    
	    size = array_of_sizes[0]*extent;
	    for (i=2; i<ndims; i++) {
		size *= array_of_sizes[i-1];
		MPI_Type_hvector(array_of_subsizes[i], 1, size, tmp1, &tmp2);
		MPI_Type_free(&tmp1);
		tmp1 = tmp2;
	    }
	}
	
	/* add displacement and UB */
	
	disps[1] = array_of_starts[0];
	size = 1;
	for (i=1; i<ndims; i++) {
	    size *= array_of_sizes[i-1];
	    disps[1] += size*array_of_starts[i];
	}  
        /* rest done below for both Fortran and C order */
    }

    else if (order == MPI_ORDER_C) {
	/* dimension ndims-1 changes fastest */
	if (ndims == 1)
	    MPI_Type_contiguous(array_of_subsizes[0], oldtype, &tmp1);
	else {
	    MPI_Type_vector(array_of_subsizes[ndims-2],
			    array_of_subsizes[ndims-1],
			    array_of_sizes[ndims-1], oldtype, &tmp1);
	    
	    size = array_of_sizes[ndims-1]*extent;
	    for (i=ndims-3; i>=0; i--) {
		size *= array_of_sizes[i+1];
		MPI_Type_hvector(array_of_subsizes[i], 1, size, tmp1, &tmp2);
		MPI_Type_free(&tmp1);
		tmp1 = tmp2;
	    }
	}
	
	/* add displacement and UB */
	
	disps[1] = array_of_starts[ndims-1];
	size = 1;
	for (i=ndims-2; i>=0; i--) {
	    size *= array_of_sizes[i+1];
	    disps[1] += size*array_of_starts[i];
	}
    }
    else {
	mpi_errno = MPIR_Err_setmsg( MPI_ERR_ARG, MPIR_ERR_ORDER, myname, 
				     (char *)0, (char *)0, order );
	return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
    }
    
    disps[1] *= extent;
    
    disps[2] = extent;
    for (i=0; i<ndims; i++) disps[2] *= array_of_sizes[i];
    
    disps[0] = 0;
    blklens[0] = blklens[1] = blklens[2] = 1;
    types[0] = MPI_LB;
    types[1] = tmp1;
    types[2] = MPI_UB;
    
    MPI_Type_struct(3, blklens, disps, types, newtype);

    MPI_Type_free(&tmp1);

    return MPI_SUCCESS;
}
