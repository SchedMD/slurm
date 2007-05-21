/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "mpioimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Type_create_subarray = PMPI_Type_create_subarray
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Type_create_subarray MPI_Type_create_subarray
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Type_create_subarray as PMPI_Type_create_subarray
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPIO_BUILD_PROFILING
#include "mpioprof.h"
#endif

/*@
MPI_Type_create_subarray - Creates a datatype describing a subarray of a multidimensional array

Input Parameters:
. ndims - number of array dimensions (positive integer)
. array_of_sizes - number of elements of type oldtype in each dimension of the full array (array of positive integers)
. array_of_subsizes - number of elements of type oldtype in each dimension of the subarray (array of positive integers)
. array_of_starts - starting coordinates of the subarray in each dimension (array of nonnegative integers)
. order - array storage order flag (state)
. oldtype - old datatype (handle)

Output Parameters:
. newtype - new datatype (handle)

.N fortran
@*/
int MPI_Type_create_subarray(int ndims, int *array_of_sizes, 
                             int *array_of_subsizes, int *array_of_starts,
                             int order, MPI_Datatype oldtype, 
                             MPI_Datatype *newtype)
{
    MPI_Aint extent, disps[3], size, size_with_aint;
    int i, blklens[3], err;
    MPI_Datatype tmp1, tmp2, types[3];
    MPI_Offset size_with_offset;

    /* --BEGIN ERROR HANDLING-- */
    if (ndims <= 0) {
	error_code = MPIO_Err_create_code(MPI_SUCCESS,
					  MPIR_ERR_RECOVERABLE,
					  myname, __LINE__, MPI_ERR_ARG,
					  "Invalid ndims argument", 0);
	return MPIO_Err_return_comm(MPI_COMM_SELF, error_code);
    }
    if (array_of_sizes <= (int *) 0) {
	error_code = MPIO_Err_create_code(MPI_SUCCESS,
					  MPIR_ERR_RECOVERABLE,
					  myname, __LINE__, MPI_ERR_ARG,
					  "Invalid array_of_sizes argument",
					  0);
	return MPIO_Err_return_comm(MPI_COMM_SELF, error_code);
    }
    if (array_of_subsizes <= (int *) 0) {
	error_code = MPIO_Err_create_code(MPI_SUCCESS,
					  MPIR_ERR_RECOVERABLE,
					  myname, __LINE__, MPI_ERR_ARG,
					  "Invalid array_of_subsizes argument",
					  0);
	return MPIO_Err_return_comm(MPI_COMM_SELF, error_code);
    }
    if (array_of_starts <= (int *) 0) {
	error_code = MPIO_Err_create_code(MPI_SUCCESS,
					  MPIR_ERR_RECOVERABLE,
					  myname, __LINE__, MPI_ERR_ARG,
					  "Invalid array_of_starts argument",
					  0);
	return MPIO_Err_return_comm(MPI_COMM_SELF, error_code);
    }

    for (i=0; i<ndims; i++) {
        if (array_of_sizes[i] <= 0) {
	    error_code = MPIO_Err_create_code(MPI_SUCCESS,
					      MPIR_ERR_RECOVERABLE,
					      myname, __LINE__, MPI_ERR_ARG,
					      "Invalid size argument", 0);
	    return MPIO_Err_return_comm(MPI_COMM_SELF, error_code);
        }
        if (array_of_subsizes[i] <= 0) {
	    error_code = MPIO_Err_create_code(MPI_SUCCESS,
					      MPIR_ERR_RECOVERABLE,
					      myname, __LINE__, MPI_ERR_ARG,
					      "Invalid subsize argument", 0);
	    return MPIO_Err_return_comm(MPI_COMM_SELF, error_code);
        }
        if (array_of_starts[i] < 0) {
	    error_code = MPIO_Err_create_code(MPI_SUCCESS,
					      MPIR_ERR_RECOVERABLE,
					      myname, __LINE__, MPI_ERR_ARG,
					      "Invalid start argument", 0);
	    return MPIO_Err_return_comm(MPI_COMM_SELF, error_code);
        }
        if (array_of_subsizes[i] > array_of_sizes[i]) {
	    error_code = MPIO_Err_create_code(MPI_SUCCESS,
					      MPIR_ERR_RECOVERABLE,
					      myname, __LINE__, MPI_ERR_ARG,
					      "Invalid subsize argument", 0);
	    return MPIO_Err_return_comm(MPI_COMM_SELF, error_code);
        }
        if (array_of_starts[i] > (array_of_sizes[i] - array_of_subsizes[i])) {
	    error_code = MPIO_Err_create_code(MPI_SUCCESS,
					      MPIR_ERR_RECOVERABLE,
					      myname, __LINE__, MPI_ERR_ARG,
					      "Invalid start argument", 0);
	    return MPIO_Err_return_comm(MPI_COMM_SELF, error_code);
        }
    }

    /* order argument checked below */

    if (oldtype == MPI_DATATYPE_NULL) {
	error_code = MPIO_Err_create_code(MPI_SUCCESS,
					  MPIR_ERR_RECOVERABLE,
					  myname, __LINE__, MPI_ERR_ARG,
					  "Invalid type argument", 0);
	return MPIO_Err_return_comm(MPI_COMM_SELF, error_code);
    }

    MPI_Type_extent(oldtype, &extent);

/* check if MPI_Aint is large enough for size of global array. 
   if not, complain. */

    size_with_aint = extent;
    for (i=0; i<ndims; i++) size_with_aint *= array_of_sizes[i];
    size_with_offset = extent;
    for (i=0; i<ndims; i++) size_with_offset *= array_of_sizes[i];
    if (size_with_aint != size_with_offset) {
	error_code = MPIO_Err_create_code(MPI_SUCCESS,
					  MPIR_ERR_RECOVERABLE,
					  myname, __LINE__, MPI_ERR_ARG,
					  "Invalid size argument", 0);
	return MPIO_Err_return_comm(MPI_COMM_SELF, error_code);
    }

    if (order != MPI_ORDER_FORTRAN && order != MPI_ORDER_C) {
	error_code = MPIO_Err_create_code(MPI_SUCCESS,
					  MPIR_ERR_RECOVERABLE,
					  myname, __LINE__, MPI_ERR_ARG,
					  "Invalid order argument", 0);
	return MPIO_Err_return_comm(MPI_COMM_SELF, error_code);
    }
    /* --END ERROR HANDLING-- */

    err = ADIO_Type_create_subarray(ndims,
				    array_of_sizes,
				    array_of_subsizes,
				    array_of_starts,
				    order,
				    oldtype,
				    newtype);
    /* --BEGIN ERROR HANDLING-- */
    if (err != MPI_SUCCESS) {
	error_code = MPIO_Err_create_code(MPI_SUCCESS,
					  MPIR_ERR_RECOVERABLE,
					  myname, __LINE__, err,
					  "Internal error", 0);
    }
    /* --END ERROR HANDLING-- */

    return MPI_SUCCESS;
}
