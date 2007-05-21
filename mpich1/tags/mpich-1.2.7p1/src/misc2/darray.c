/* 
 *   $Id: darray.c,v 1.15 2004/08/27 19:08:25 thakur Exp $    
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Type_create_darray = PMPI_Type_create_darray
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Type_create_darray  MPI_Type_create_darray
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Type_create_darray as PMPI_Type_create_darray
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPI_BUILD_PROFILING
#include "mpiprof.h"
/* Insert the prototypes for the PMPI routines */
#undef __MPI_BINDINGS
#include "binding.h"

#undef MPI_BUILD_PROFILING  /* so that MPIOI_Type_block and MPIOI_Type_cyclic
                               get compiled below */

#endif

#include "mpimem.h"

int MPIOI_Type_block(int *array_of_gsizes, int dim, int ndims, int nprocs,
		      int rank, int darg, int order, MPI_Aint orig_extent,
		      MPI_Datatype type_old, MPI_Datatype *type_new,
		      MPI_Aint *st_offset);
int MPIOI_Type_cyclic(int *array_of_gsizes, int dim, int ndims, int nprocs,
		      int rank, int darg, int order, MPI_Aint orig_extent,
		      MPI_Datatype type_old, MPI_Datatype *type_new,
		      MPI_Aint *st_offset);


/*@
MPI_Type_create_darray - Creates a datatype corresponding to a distributed, multidimensional array

Input Parameters:
+ size - size of process group (positive integer)
. rank - rank in process group (nonnegative integer)
. ndims - number of array dimensions as well as process grid dimensions (positive integer)
. array_of_gsizes - number of elements of type oldtype in each dimension of global array (array of positive integers)
. array_of_distribs - distribution of array in each dimension (array of state)
. array_of_dargs - distribution argument in each dimension (array of positive integers)
. array_of_psizes - size of process grid in each dimension (array of positive integers)
. order - array storage order flag (state)
- oldtype - old datatype (handle)

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
    MPI_Datatype type_old, type_new, types[3];
    int procs, tmp_rank, i, tmp_size, blklens[3], *coords;
    MPI_Aint *st_offsets, orig_extent, disps[3];
    static char myname[] = "MPI_TYPE_CREATE_DARRAY";
    int mpi_errno = MPI_SUCCESS;

    if (size <= 0) {
	mpi_errno = MPIR_Err_setmsg( MPI_ERR_ARG, MPIR_ERR_ARG_NAMED, myname, 
				     "Invalid argument", 
			     "Invalid %s argument = %d", "size", size );
	return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
    }
    if (rank < 0) {
	mpi_errno = MPIR_Err_setmsg( MPI_ERR_ARG, MPIR_ERR_ARG_NAMED, myname, 
				     (char *)0, 
			     "Invalid %s argument = %d", "rank", rank );
	return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
    }
    if (ndims <= 0) {
	mpi_errno = MPIR_Err_setmsg( MPI_ERR_ARG, MPIR_ERR_ARG_NAMED, myname, 
				     (char *)0, 
			     "Invalid %s argument = %d", "ndims", ndims );
	return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
    }
    MPIR_TEST_ARG(array_of_gsizes);
    MPIR_TEST_ARG(array_of_distribs);
    MPIR_TEST_ARG(array_of_dargs);
    MPIR_TEST_ARG(array_of_psizes);
    if (mpi_errno) 
	return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );

    for (i=0; i<ndims; i++) {
	if (array_of_gsizes[i] <= 0) {
	    mpi_errno = MPIR_Err_setmsg( MPI_ERR_ARG, MPIR_ERR_ARG_ARRAY_VAL,
					 myname,
					 "Invalid value in array", 
			 "Invalid value in %s[%d] = %d", "array_of_gsizes", i, 
					 array_of_gsizes[i] );
	    return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
	}

	/* array_of_distribs checked below */

	if ((array_of_dargs[i] != MPI_DISTRIBUTE_DFLT_DARG) && 
	                 (array_of_dargs[i] <= 0)) {
	    mpi_errno = MPIR_Err_setmsg( MPI_ERR_ARG, MPIR_ERR_ARG_ARRAY_VAL,
					 myname,
					 (char *)0, 
			 "Invalid value in %s[%d] = %d", "array_of_dargs", i, 
					 array_of_dargs[i] );
	    return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
	}

	if (array_of_psizes[i] <= 0) {
	    mpi_errno = MPIR_Err_setmsg( MPI_ERR_ARG, MPIR_ERR_ARG_ARRAY_VAL,
					 myname,
					 (char *)0, 
			 "Invalid value in %s[%d] = %d", "array_of_psizes", i, 
					 array_of_psizes[i] );
	    return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
	}
    }

    /* order argument checked below */

    if (oldtype == MPI_DATATYPE_NULL) {
	return MPIR_ERROR( MPIR_COMM_WORLD, MPIR_ERR_TYPE_NULL, myname );
    }

    MPI_Type_extent(oldtype, &orig_extent);

/* calculate position in Cartesian grid as MPI would (row-major
   ordering) */
    coords = (int *) MALLOC(ndims*sizeof(int));
    procs = size;
    tmp_rank = rank;
    for (i=0; i<ndims; i++) {
	procs = procs/array_of_psizes[i];
	coords[i] = tmp_rank/procs;
	tmp_rank = tmp_rank % procs;
    }

    st_offsets = (MPI_Aint *) MALLOC(ndims*sizeof(MPI_Aint));
    type_old = oldtype;

    if (order == MPI_ORDER_FORTRAN) {
      /* dimension 0 changes fastest */
	for (i=0; i<ndims; i++) {
	    switch(array_of_distribs[i]) {
	    case MPI_DISTRIBUTE_BLOCK:
		mpi_errno = MPIOI_Type_block(array_of_gsizes, i, ndims, 
					     array_of_psizes[i],
			 coords[i], array_of_dargs[i], order, orig_extent, 
			      type_old, &type_new, st_offsets+i); 
		break;
	    case MPI_DISTRIBUTE_CYCLIC:
		mpi_errno = MPIOI_Type_cyclic(array_of_gsizes, i, ndims, 
		   array_of_psizes[i], coords[i], array_of_dargs[i], order,
                        orig_extent, type_old, &type_new, st_offsets+i);
		break;
	    case MPI_DISTRIBUTE_NONE:
		if (array_of_psizes[i] != 1) {
		    mpi_errno = MPIR_Err_setmsg( MPI_ERR_ARG, 
					 MPIR_ERR_DARRAY_DIST_NONE, myname,
"For MPI_DISTRIBUTE_NONE, the number of processes in that dimension of the grid must be 1",
"For MPI_DISTRIBUTE_NONE, the number of processes in that dimension of the grid must be 1 (array_of_psizes[%d] = %d)", i, array_of_psizes[i]);
		    return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
		}
		/* treat it as a block distribution on 1 process */
		mpi_errno = MPIOI_Type_block(array_of_gsizes, i, ndims, 1, 0, 
		      MPI_DISTRIBUTE_DFLT_DARG, order, orig_extent, 
                           type_old, &type_new, st_offsets+i); 
		break;
	    default:
		mpi_errno = MPIR_Err_setmsg( MPI_ERR_ARG, 
					 MPIR_ERR_DARRAY_DIST_UNKNOWN, myname,
				     "Unknown distribution type",(char *)0);
		return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
	    }
	    if (mpi_errno) 
		return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );

	    if (i) MPI_Type_free(&type_old);
	    type_old = type_new;
	}

	/* add displacement and UB */
	disps[1] = st_offsets[0];
	tmp_size = 1;
	for (i=1; i<ndims; i++) {
	    tmp_size *= array_of_gsizes[i-1];
	    disps[1] += tmp_size*st_offsets[i];
	}
        /* rest done below for both Fortran and C order */
    }

    else if (order == MPI_ORDER_C) {
        /* dimension ndims-1 changes fastest */
	for (i=ndims-1; i>=0; i--) {
	    switch(array_of_distribs[i]) {
	    case MPI_DISTRIBUTE_BLOCK:
		MPIOI_Type_block(array_of_gsizes, i, ndims, array_of_psizes[i],
		      coords[i], array_of_dargs[i], order, orig_extent, 
                           type_old, &type_new, st_offsets+i); 
		break;
	    case MPI_DISTRIBUTE_CYCLIC:
		MPIOI_Type_cyclic(array_of_gsizes, i, ndims, 
		      array_of_psizes[i], coords[i], array_of_dargs[i], order, 
			  orig_extent, type_old, &type_new, st_offsets+i);
		break;
	    case MPI_DISTRIBUTE_NONE:
		if (array_of_psizes[i] != 1) {
		    mpi_errno = MPIR_Err_setmsg( MPI_ERR_ARG, 
						 MPIR_ERR_DARRAY_DIST_NONE, 
						 myname,
			       (char *)0, (char *)0,i, array_of_psizes[i] );
		    return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
		}
		/* treat it as a block distribution on 1 process */
		MPIOI_Type_block(array_of_gsizes, i, ndims, array_of_psizes[i],
		      coords[i], MPI_DISTRIBUTE_DFLT_DARG, order, orig_extent, 
                           type_old, &type_new, st_offsets+i); 
		break;
	    default:
		mpi_errno = MPIR_Err_setmsg( MPI_ERR_ARG, 
                       MPIR_ERR_DARRAY_ARRAY_DIST_UNKNOWN, myname, 
		   "Invalid value in array_of_distribs",
		   "Invalid value in array_of_distribs[%d] = %d", 
					     i, array_of_distribs[i] );
		return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
		/* Invalid value in array_of_distribs */
	    }
	    if (i != ndims-1) MPI_Type_free(&type_old);
	    type_old = type_new;
	}

	/* add displacement and UB */
	disps[1] = st_offsets[ndims-1];
	tmp_size = 1;
	for (i=ndims-2; i>=0; i--) {
	    tmp_size *= array_of_gsizes[i+1];
	    disps[1] += tmp_size*st_offsets[i];
	}
    }
    else {
	mpi_errno = MPIR_Err_setmsg( MPI_ERR_ARG, MPIR_ERR_ORDER, myname, 
				     "Invalid order argument",
				     "Invalid order argument = %d", order );
	return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
    }

    disps[1] *= orig_extent;

    disps[2] = orig_extent;
    for (i=0; i<ndims; i++) disps[2] *= array_of_gsizes[i];

    disps[0] = 0;
    blklens[0] = blklens[1] = blklens[2] = 1;
    types[0] = MPI_LB;
    types[1] = type_new;
    types[2] = MPI_UB;
    
    MPI_Type_struct(3, blklens, disps, types, newtype);

    MPI_Type_free(&type_new);
    FREE(st_offsets);
    FREE(coords);
    return MPI_SUCCESS;
}


#ifndef MPI_BUILD_PROFILING
int MPIOI_Type_block(int *array_of_gsizes, int dim, int ndims, int nprocs,
		      int rank, int darg, int order, MPI_Aint orig_extent,
		      MPI_Datatype type_old, MPI_Datatype *type_new,
		      MPI_Aint *st_offset) 
{
/* nprocs = no. of processes in dimension dim of grid
   rank = coordinate of this process in dimension dim */

    int blksize, global_size, mysize, i, j;
    int mpi_errno;
    MPI_Aint stride;
    
    global_size = array_of_gsizes[dim];

    if (darg == MPI_DISTRIBUTE_DFLT_DARG)
	blksize = (global_size + nprocs - 1)/nprocs;
    else {
	blksize = darg;
	if (blksize <= 0) {
	    mpi_errno = MPIR_Err_setmsg( MPI_ERR_ARG, 
				 MPIR_ERR_DARRAY_INVALID_BLOCK, (char *)0,
        "m must be positive for a block(m) distribution",
	"m = %d must be positive for a block(m) distribution", blksize );
	    return mpi_errno;
	}
	if (blksize * nprocs < global_size) {
	    mpi_errno = MPIR_Err_setmsg( MPI_ERR_ARG, 
				 MPIR_ERR_DARRAY_INVALID_BLOCK2, (char *)0, 
    "m * nprocs is < array_size and is not valid for a block(m) distribution", 
    "m * nprocs = %d is < array_size = %d and is not valid for a block(m) distribution", 
					 blksize * nprocs, global_size );
	    return mpi_errno;
	}
    }

    j = global_size - blksize*rank;
    mysize = blksize < j ? blksize : j;
    if (mysize < 0) mysize = 0;

    stride = orig_extent;
    if (order == MPI_ORDER_FORTRAN) {
	if (dim == 0) 
	    MPI_Type_contiguous(mysize, type_old, type_new);
	else {
	    for (i=0; i<dim; i++) stride *= array_of_gsizes[i];
	    MPI_Type_hvector(mysize, 1, stride, type_old, type_new);
	}
    }
    else {
	if (dim == ndims-1) 
	    MPI_Type_contiguous(mysize, type_old, type_new);
	else {
	    for (i=ndims-1; i>dim; i--) stride *= array_of_gsizes[i];
	    MPI_Type_hvector(mysize, 1, stride, type_old, type_new);
	}

    }

    *st_offset = blksize * rank;
     /* in terms of no. of elements of type oldtype in this dimension */
    if (mysize == 0) *st_offset = 0;
    return MPI_SUCCESS;
}


int MPIOI_Type_cyclic(int *array_of_gsizes, int dim, int ndims, int nprocs,
		      int rank, int darg, int order, MPI_Aint orig_extent,
		      MPI_Datatype type_old, MPI_Datatype *type_new,
		      MPI_Aint *st_offset) 
{
/* nprocs = no. of processes in dimension dim of grid
   rank = coordinate of this process in dimension dim */

    int blksize, i, blklens[3], st_index, end_index, local_size, rem, count;
    int mpi_errno;
    MPI_Aint stride, disps[3];
    MPI_Datatype type_tmp, types[3];

    if (darg == MPI_DISTRIBUTE_DFLT_DARG) blksize = 1;
    else blksize = darg;

    if (blksize <= 0) {
	mpi_errno = MPIR_Err_setmsg( MPI_ERR_ARG, 
				     MPIR_ERR_DARRAY_INVALID_BLOCK3, (char *)0,
	"m must be positive for a cyclic(m) distribution", 
	"m = %d must be positive for a cyclic(m) distribution", blksize );
	return mpi_errno;
    }
    
    st_index = rank*blksize;
    end_index = array_of_gsizes[dim] - 1;

    if (end_index < st_index) local_size = 0;
    else {
	local_size = ((end_index - st_index + 1)/(nprocs*blksize))*blksize;
	rem = (end_index - st_index + 1) % (nprocs*blksize);
	local_size += (rem < blksize ? rem : blksize);
    }

    count = local_size/blksize;
    rem = local_size % blksize;
    
    stride = nprocs*blksize*orig_extent;
    if (order == MPI_ORDER_FORTRAN)
	for (i=0; i<dim; i++) stride *= array_of_gsizes[i];
    else for (i=ndims-1; i>dim; i--) stride *= array_of_gsizes[i];

    MPI_Type_hvector(count, blksize, stride, type_old, type_new);

    if (rem) {
	/* if the last block is of size less than blksize, include
	   it separately using MPI_Type_struct */

	types[0] = *type_new;
	types[1] = type_old;
	disps[0] = 0;
	disps[1] = count*stride;
	blklens[0] = 1;
	blklens[1] = rem;

	MPI_Type_struct(2, blklens, disps, types, &type_tmp);

	MPI_Type_free(type_new);
	*type_new = type_tmp;
    }

    /* In the first iteration, we need to set the displacement in that
       dimension correctly. */ 
    if ( ((order == MPI_ORDER_FORTRAN) && (dim == 0)) ||
         ((order == MPI_ORDER_C) && (dim == ndims-1)) ) {
        types[0] = MPI_LB;
        disps[0] = 0;
        types[1] = *type_new;
        disps[1] = rank * blksize * orig_extent;
        types[2] = MPI_UB;
        disps[2] = orig_extent * array_of_gsizes[dim];
        blklens[0] = blklens[1] = blklens[2] = 1;
        MPI_Type_struct(3, blklens, disps, types, &type_tmp);
        MPI_Type_free(type_new);
        *type_new = type_tmp;

        *st_offset = 0;  /* set it to 0 because it is taken care of in
                            the struct above */
    }
    else {
        *st_offset = rank * blksize; 
        /* st_offset is in terms of no. of elements of type oldtype in
         * this dimension */ 
    }

    if (local_size == 0) *st_offset = 0;
    
    return MPI_SUCCESS;
}
#endif
