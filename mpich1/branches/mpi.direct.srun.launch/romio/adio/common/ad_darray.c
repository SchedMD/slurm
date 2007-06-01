/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "adio.h"
#include "adio_extern.h"

int MPIOI_Type_block(int *array_of_gsizes, int dim, int ndims, int nprocs,
		     int rank, int darg, int order, MPI_Aint orig_extent,
		     MPI_Datatype type_old, MPI_Datatype *type_new,
		     MPI_Aint *st_offset);
int MPIOI_Type_cyclic(int *array_of_gsizes, int dim, int ndims, int nprocs,
		      int rank, int darg, int order, MPI_Aint orig_extent,
		      MPI_Datatype type_old, MPI_Datatype *type_new,
		      MPI_Aint *st_offset);


int ADIO_Type_create_darray(int size, int rank, int ndims, 
			    int *array_of_gsizes, int *array_of_distribs, 
			    int *array_of_dargs, int *array_of_psizes, 
			    int order, MPI_Datatype oldtype, 
			    MPI_Datatype *newtype) 
{
    MPI_Datatype type_old, type_new=MPI_DATATYPE_NULL, types[3];
    int procs, tmp_rank, i, tmp_size, blklens[3], *coords;
    MPI_Aint *st_offsets, orig_extent, disps[3];

    MPI_Type_extent(oldtype, &orig_extent);

/* calculate position in Cartesian grid as MPI would (row-major
   ordering) */
    coords = (int *) ADIOI_Malloc(ndims*sizeof(int));
    procs = size;
    tmp_rank = rank;
    for (i=0; i<ndims; i++) {
	procs = procs/array_of_psizes[i];
	coords[i] = tmp_rank/procs;
	tmp_rank = tmp_rank % procs;
    }

    st_offsets = (MPI_Aint *) ADIOI_Malloc(ndims*sizeof(MPI_Aint));
    type_old = oldtype;

    if (order == MPI_ORDER_FORTRAN) {
      /* dimension 0 changes fastest */
	for (i=0; i<ndims; i++) {
	    switch(array_of_distribs[i]) {
	    case MPI_DISTRIBUTE_BLOCK:
		MPIOI_Type_block(array_of_gsizes, i, ndims,
				 array_of_psizes[i],
				 coords[i], array_of_dargs[i],
				 order, orig_extent, 
				 type_old, &type_new,
				 st_offsets+i); 
		break;
	    case MPI_DISTRIBUTE_CYCLIC:
		MPIOI_Type_cyclic(array_of_gsizes, i, ndims, 
				  array_of_psizes[i], coords[i],
				  array_of_dargs[i], order,
				  orig_extent, type_old,
				  &type_new, st_offsets+i);
		break;
	    case MPI_DISTRIBUTE_NONE:
		/* treat it as a block distribution on 1 process */
		MPIOI_Type_block(array_of_gsizes, i, ndims, 1, 0, 
				 MPI_DISTRIBUTE_DFLT_DARG, order,
				 orig_extent, 
				 type_old, &type_new,
				 st_offsets+i); 
		break;
	    }
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

    else /* order == MPI_ORDER_C */ {
        /* dimension ndims-1 changes fastest */
	for (i=ndims-1; i>=0; i--) {
	    switch(array_of_distribs[i]) {
	    case MPI_DISTRIBUTE_BLOCK:
		MPIOI_Type_block(array_of_gsizes, i, ndims, array_of_psizes[i],
				 coords[i], array_of_dargs[i], order,
				 orig_extent, type_old, &type_new,
				 st_offsets+i); 
		break;
	    case MPI_DISTRIBUTE_CYCLIC:
		MPIOI_Type_cyclic(array_of_gsizes, i, ndims, 
				  array_of_psizes[i], coords[i],
				  array_of_dargs[i], order, 
				  orig_extent, type_old, &type_new,
				  st_offsets+i);
		break;
	    case MPI_DISTRIBUTE_NONE:
		/* treat it as a block distribution on 1 process */
		MPIOI_Type_block(array_of_gsizes, i, ndims, array_of_psizes[i],
		      coords[i], MPI_DISTRIBUTE_DFLT_DARG, order, orig_extent, 
                           type_old, &type_new, st_offsets+i); 
		break;
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
    ADIOI_Free(st_offsets);
    ADIOI_Free(coords);
    return MPI_SUCCESS;
}


/* Returns MPI_SUCCESS on success, an MPI error code on failure.  Code above
 * needs to call MPIO_Err_return_xxx.
 */
int MPIOI_Type_block(int *array_of_gsizes, int dim, int ndims, int nprocs,
		     int rank, int darg, int order, MPI_Aint orig_extent,
		     MPI_Datatype type_old, MPI_Datatype *type_new,
		     MPI_Aint *st_offset) 
{
/* nprocs = no. of processes in dimension dim of grid
   rank = coordinate of this process in dimension dim */
    int blksize, global_size, mysize, i, j;
    MPI_Aint stride;
    
    global_size = array_of_gsizes[dim];

    if (darg == MPI_DISTRIBUTE_DFLT_DARG)
	blksize = (global_size + nprocs - 1)/nprocs;
    else {
	blksize = darg;

	/* --BEGIN ERROR HANDLING-- */
	if (blksize <= 0) {
	    return MPI_ERR_ARG;
	}

	if (blksize * nprocs < global_size) {
	    return MPI_ERR_ARG;
	}
	/* --END ERROR HANDLING-- */
    }

    j = global_size - blksize*rank;
    mysize = ADIOI_MIN(blksize, j);
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


/* Returns MPI_SUCCESS on success, an MPI error code on failure.  Code above
 * needs to call MPIO_Err_return_xxx.
 */
int MPIOI_Type_cyclic(int *array_of_gsizes, int dim, int ndims, int nprocs,
		      int rank, int darg, int order, MPI_Aint orig_extent,
		      MPI_Datatype type_old, MPI_Datatype *type_new,
		      MPI_Aint *st_offset) 
{
/* nprocs = no. of processes in dimension dim of grid
   rank = coordinate of this process in dimension dim */
    int blksize, i, blklens[3], st_index, end_index, local_size, rem, count;
    MPI_Aint stride, disps[3];
    MPI_Datatype type_tmp, types[3];

    if (darg == MPI_DISTRIBUTE_DFLT_DARG) blksize = 1;
    else blksize = darg;

    /* --BEGIN ERROR HANDLING-- */
    if (blksize <= 0) {
	return MPI_ERR_ARG;
    }
    /* --END ERROR HANDLING-- */
    
    st_index = rank*blksize;
    end_index = array_of_gsizes[dim] - 1;

    if (end_index < st_index) local_size = 0;
    else {
	local_size = ((end_index - st_index + 1)/(nprocs*blksize))*blksize;
	rem = (end_index - st_index + 1) % (nprocs*blksize);
	local_size += ADIOI_MIN(rem, blksize);
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
