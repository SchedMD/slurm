/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "mpioimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Type_create_darray = PMPI_Type_create_darray
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Type_create_darray MPI_Type_create_darray
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Type_create_darray as PMPI_Type_create_darray
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPIO_BUILD_PROFILING
#include "mpioprof.h"
#endif

/*@
MPI_Type_create_darray - Creates a datatype corresponding to a distributed, multidimensional array

Input Parameters:
. size - size of process group (positive integer)
. rank - rank in process group (nonnegative integer)
. ndims - number of array dimensions as well as process grid dimensions (positive integer)
. array_of_gsizes - number of elements of type oldtype in each dimension of global array (array of positive integers)
. array_of_distribs - distribution of array in each dimension (array of state)
. array_of_dargs - distribution argument in each dimension (array of positive integers)
. array_of_psizes - size of process grid in each dimension (array of positive integers)
. order - array storage order flag (state)
. oldtype - old datatype (handle)

Output Parameters:
. newtype - new datatype (handle)

.N fortran
@*/
int MPI_Type_create_darray(int size, int rank, int ndims, 
     	                   int *array_of_gsizes, int *array_of_distribs, 
                           int *array_of_dargs, int *array_of_psizes, 
                           int order, MPI_Datatype oldtype, 
                           MPI_Datatype *newtype) 
{
    int err, error_code;
    MPI_Datatype type_old, type_new, types[3];
    int procs, tmp_rank, i, tmp_size, blklens[3], *coords;
    MPI_Aint *st_offsets, orig_extent, disps[3], size_with_aint;
    MPI_Offset size_with_offset;
    static char myname[] = "MPI_TYPE_CREATE_DARRAY";

    /* --BEGIN ERROR HANDLING-- */
    if (size <= 0) {
	error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					  myname, __LINE__, MPI_ERR_ARG,
					  "Invalid size argument", 0);
	return MPIO_Err_return_comm(MPI_COMM_SELF, error_code);
    }
    if (rank < 0) {
	error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					  myname, __LINE__, MPI_ERR_ARG,
					  "Invalid rank argument", 0);
	return MPIO_Err_return_comm(MPI_COMM_SELF, error_code);
    }
    if (ndims <= 0) {
	error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					  myname, __LINE__, MPI_ERR_ARG,
					  "Invalid ndoms argument", 0);
	return MPIO_Err_return_comm(MPI_COMM_SELF, error_code);
    }
    if (array_of_gsizes <= (int *) 0) {
	error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					  myname, __LINE__, MPI_ERR_ARG,
					  "Invalid array_of_gsizes argument", 0);
	return MPIO_Err_return_comm(MPI_COMM_SELF, error_code);
    }
    if (array_of_distribs <= (int *) 0) {
	error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					  myname, __LINE__, MPI_ERR_ARG,
					  "Invalid array_of_distribs argument", 0);
	return MPIO_Err_return_comm(MPI_COMM_SELF, error_code);
    }
    if (array_of_dargs <= (int *) 0) {
	error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					  myname, __LINE__, MPI_ERR_ARG,
					  "Invalid array_of_dargs argument", 0);
	return MPIO_Err_return_comm(MPI_COMM_SELF, error_code);
    }
    if (array_of_psizes <= (int *) 0) {
	error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					  myname, __LINE__, MPI_ERR_ARG,
					  "Invalid array_of_psizes argument", 0);
	return MPIO_Err_return_comm(MPI_COMM_SELF, error_code);
    }

    for (i=0; i<ndims; i++) {
	if (array_of_gsizes[i] <= 0) {
	    error_code = MPIO_Err_create_code(MPI_SUCCESS,MPIR_ERR_RECOVERABLE,
					      myname, __LINE__, MPI_ERR_ARG,
					      "Invalid gsize argument", 0);
	    return MPIO_Err_return_comm(MPI_COMM_SELF, error_code);
	}

	/* array_of_distribs checked below */

	if ((array_of_dargs[i] != MPI_DISTRIBUTE_DFLT_DARG) && 
	    (array_of_dargs[i] <= 0))
	{
	    error_code = MPIO_Err_create_code(MPI_SUCCESS,
					      MPIR_ERR_RECOVERABLE,
					      myname, __LINE__, MPI_ERR_ARG,
					      "Invalid darg argument", 0);
	    return MPIO_Err_return_comm(MPI_COMM_SELF, error_code);
	}

	if (array_of_psizes[i] <= 0) {
	    error_code = MPIO_Err_create_code(MPI_SUCCESS,
					      MPIR_ERR_RECOVERABLE,
					      myname, __LINE__, MPI_ERR_ARG,
					      "Invalid psize argument", 0);
	    return MPIO_Err_return_comm(MPI_COMM_SELF, error_code);
	}
	if (array_of_distribs[i] != MPI_DISTRIBUTE_BLOCK &&
	    array_of_distribs[i] != MPI_DISTRIBUTE_CYCLIC &&
	    array_of_distribs[i] != MPI_DISTRIBUTE_NONE)
	{
	    error_code = MPIO_Err_create_code(MPI_SUCCESS,
					      MPIR_ERR_RECOVERABLE,
					      myname, __LINE__,
					      MPI_ERR_ARG,
					      "Invalid distrib argument",
					      0);
	    return MPIO_Err_return_comm(MPI_COMM_SELF, error_code);
	}
	if (array_of_distribs[i] == MPI_DISTRIBUTE_NONE &&
	    array_of_psizes[i] != 1)
	{
		    error_code = MPIO_Err_create_code(MPI_SUCCESS,
						      MPIR_ERR_RECOVERABLE,
						      myname, __LINE__,
						      MPI_ERR_ARG,
						      "For MPI_DISTRIBUTE_NONE, the number of processes in that dimension of the grid must be 1",
						      0);
		    return MPIO_Err_return_comm(MPI_COMM_SELF, error_code);
	}
    }

    /* order argument checked below */

    if (oldtype == MPI_DATATYPE_NULL) {
	error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					  myname, __LINE__, MPI_ERR_ARG,
					  "Invalid type argument", 0);
	return MPIO_Err_return_comm(MPI_COMM_SELF, error_code);
    }

    MPI_Type_extent(oldtype, &orig_extent);

/* check if MPI_Aint is large enough for size of global array. 
   if not, complain. */

    size_with_aint = orig_extent;
    for (i=0; i<ndims; i++) size_with_aint *= array_of_gsizes[i];
    size_with_offset = orig_extent;
    for (i=0; i<ndims; i++) size_with_offset *= array_of_gsizes[i];

    if (size_with_aint != size_with_offset) {
	error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					  myname, __LINE__, MPI_ERR_ARG,
					  "Invalid array size", 0);
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

    err = ADIO_Type_create_darray(size,
				  rank,
				  ndims,
				  array_of_gsizes,
				  array_of_distribs,
				  array_of_dargs,
				  array_of_psizes,
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

