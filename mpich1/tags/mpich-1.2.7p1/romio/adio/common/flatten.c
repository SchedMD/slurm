/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *   $Id: flatten.c,v 1.22 2005/08/12 18:56:56 thakur Exp $
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "adio.h"
#include "adio_extern.h"
/* #ifdef MPISGI
#include "mpisgi2.h"
#endif */

void ADIOI_Optimize_flattened(ADIOI_Flatlist_node *flat_type);
void ADIOI_Flatten_copy_type(ADIOI_Flatlist_node *flat,
			     int old_type_start,
			     int old_type_end,
			     int new_type_start,
			     ADIO_Offset offset_adjustment);

/* flatten datatype and add it to Flatlist */
void ADIOI_Flatten_datatype(MPI_Datatype datatype)
{
#ifdef HAVE_MPIR_TYPE_FLATTEN
    MPI_Aint flatten_idx;
#endif
    int curr_index=0, is_contig;
    ADIOI_Flatlist_node *flat, *prev=0;

    /* check if necessary to flatten. */
 
    /* is it entirely contiguous? */
    ADIOI_Datatype_iscontig(datatype, &is_contig);
    if (is_contig) return;

    /* has it already been flattened? */
    flat = ADIOI_Flatlist;
    while (flat) {
	if (flat->type == datatype) {
		return;
	}
	else {
	    prev = flat;
	    flat = flat->next;
	}
    }

    /* flatten and add to the list */
    flat = prev;
    flat->next = (ADIOI_Flatlist_node *)ADIOI_Malloc(sizeof(ADIOI_Flatlist_node));
    flat = flat->next;

    flat->type = datatype;
    flat->next = NULL;
    flat->blocklens = NULL;
    flat->indices = NULL;

    flat->count = ADIOI_Count_contiguous_blocks(datatype, &curr_index);
#if 0
    FPRINTF(stderr, "cur_idx = %d\n", curr_index);
#endif
/*    FPRINTF(stderr, "%d\n", flat->count);*/

    if (flat->count) {
	flat->blocklens = (int *) ADIOI_Malloc(flat->count * sizeof(int));
	flat->indices = (ADIO_Offset *) ADIOI_Malloc(flat->count * \
						  sizeof(ADIO_Offset));
    }
	
    curr_index = 0;
#ifdef HAVE_MPIR_TYPE_FLATTEN
    flatten_idx = (MPI_Aint) flat->count;
    MPIR_Type_flatten(datatype, flat->indices, flat->blocklens, &flatten_idx);
#else
    ADIOI_Flatten(datatype, flat, 0, &curr_index);

    ADIOI_Optimize_flattened(flat);
#endif
/* debug */
#if 0
    {
	int i;
	FPRINTF(stderr, "blens: ");
	for (i=0; i<flat->count; i++) 
	    FPRINTF(stderr, "%d ", flat->blocklens[i]);
	FPRINTF(stderr, "\n\n");
	FPRINTF(stderr, "indices: ");
	for (i=0; i<flat->count; i++) 
	    FPRINTF(stderr, "%ld ", (long) flat->indices[i]);
	FPRINTF(stderr, "\n\n");
    }
#endif

}

/* ADIOI_Flatten()
 *
 * Assumption: input datatype is not a basic!!!!
 */
void ADIOI_Flatten(MPI_Datatype datatype, ADIOI_Flatlist_node *flat, 
		  ADIO_Offset st_offset, int *curr_index)  
{
    int i, j, k, m, n, num, basic_num, prev_index;
    int top_count, combiner, old_combiner, old_is_contig;
    int old_size, nints, nadds, ntypes, old_nints, old_nadds, old_ntypes;
    MPI_Aint old_extent;
    int *ints;
    MPI_Aint *adds;
    MPI_Datatype *types;

    MPI_Type_get_envelope(datatype, &nints, &nadds, &ntypes, &combiner);
    ints = (int *) ADIOI_Malloc((nints+1)*sizeof(int));
    adds = (MPI_Aint *) ADIOI_Malloc((nadds+1)*sizeof(MPI_Aint));
    types = (MPI_Datatype *) ADIOI_Malloc((ntypes+1)*sizeof(MPI_Datatype));
    MPI_Type_get_contents(datatype, nints, nadds, ntypes, ints, adds, types);

    switch (combiner) {
#ifdef MPIIMPL_HAVE_MPI_COMBINER_DUP
    case MPI_COMBINER_DUP:
        MPI_Type_get_envelope(types[0], &old_nints, &old_nadds,
			      &old_ntypes, &old_combiner); 
        ADIOI_Datatype_iscontig(types[0], &old_is_contig);
	if ((old_combiner != MPI_COMBINER_NAMED) && (!old_is_contig))
            ADIOI_Flatten(types[0], flat, st_offset, curr_index);
        break;
#endif
#ifdef MPIIMPL_HAVE_MPI_COMBINER_SUBARRAY
    case MPI_COMBINER_SUBARRAY:
        {
	    int dims = ints[0];
	    MPI_Datatype stype;

	    ADIO_Type_create_subarray(dims,
				      &ints[1],        /* sizes */
				      &ints[dims+1],   /* subsizes */
				      &ints[2*dims+1], /* starts */
				      ints[3*dims+1],  /* order */
				      types[0],        /* type */
				      &stype);
	    ADIOI_Flatten(stype, flat, st_offset, curr_index);
	    MPI_Type_free(&stype);
	}
	break;
#endif
#ifdef MPIIMPL_HAVE_MPI_COMBINER_DARRAY
    case MPI_COMBINER_DARRAY:
	{
	    int dims = ints[2];
	    MPI_Datatype dtype;

	    ADIO_Type_create_darray(ints[0],         /* size */
				    ints[1],         /* rank */
				    dims,
				    &ints[3],        /* gsizes */
				    &ints[dims+3],   /* distribs */
				    &ints[2*dims+3], /* dargs */
				    &ints[3*dims+3], /* psizes */
				    ints[4*dims+3],  /* order */
				    types[0],
				    &dtype);
	    ADIOI_Flatten(dtype, flat, st_offset, curr_index);
	    MPI_Type_free(&dtype);
	}
	break;
#endif
    case MPI_COMBINER_CONTIGUOUS:
	top_count = ints[0];
        MPI_Type_get_envelope(types[0], &old_nints, &old_nadds,
			      &old_ntypes, &old_combiner); 
        ADIOI_Datatype_iscontig(types[0], &old_is_contig);

	prev_index = *curr_index;
	if ((old_combiner != MPI_COMBINER_NAMED) && (!old_is_contig))
	    ADIOI_Flatten(types[0], flat, st_offset, curr_index);

	if (prev_index == *curr_index) {
/* simplest case, made up of basic or contiguous types */
	    j = *curr_index;
	    flat->indices[j] = st_offset;
	    MPI_Type_size(types[0], &old_size);
	    flat->blocklens[j] = top_count * old_size;
	    (*curr_index)++;
	}
	else {
/* made up of noncontiguous derived types */
	    j = *curr_index;
	    num = *curr_index - prev_index;

/* The noncontiguous types have to be replicated count times */
	    MPI_Type_extent(types[0], &old_extent);
	    for (m=1; m<top_count; m++) {
		for (i=0; i<num; i++) {
		    flat->indices[j] = flat->indices[j-num] + old_extent;
		    flat->blocklens[j] = flat->blocklens[j-num];
		    j++;
		}
	    }
	    *curr_index = j;
	}
	break;

    case MPI_COMBINER_VECTOR: 
	top_count = ints[0];
        MPI_Type_get_envelope(types[0], &old_nints, &old_nadds,
			      &old_ntypes, &old_combiner); 
        ADIOI_Datatype_iscontig(types[0], &old_is_contig);

	prev_index = *curr_index;
	if ((old_combiner != MPI_COMBINER_NAMED) && (!old_is_contig))
	    ADIOI_Flatten(types[0], flat, st_offset, curr_index);

	if (prev_index == *curr_index) {
/* simplest case, vector of basic or contiguous types */
	    j = *curr_index;
	    flat->indices[j] = st_offset;
	    MPI_Type_size(types[0], &old_size);
	    flat->blocklens[j] = ints[1] * old_size;
	    for (i=j+1; i<j+top_count; i++) {
		flat->indices[i] = flat->indices[i-1] + ints[2]*old_size;
		flat->blocklens[i] = flat->blocklens[j];
	    }
	    *curr_index = i;
	}
	else {
/* vector of noncontiguous derived types */

	    j = *curr_index;
	    num = *curr_index - prev_index;

/* The noncontiguous types have to be replicated blocklen times
   and then strided. Replicate the first one. */
	    MPI_Type_extent(types[0], &old_extent);
	    for (m=1; m<ints[1]; m++) {
		for (i=0; i<num; i++) {
		    flat->indices[j] = flat->indices[j-num] + old_extent;
		    flat->blocklens[j] = flat->blocklens[j-num];
		    j++;
		}
	    }
	    *curr_index = j;

/* Now repeat with strides. */
	    num = *curr_index - prev_index;
	    for (i=1; i<top_count; i++) {
 		for (m=0; m<num; m++) {
		   flat->indices[j] =  flat->indices[j-num] + ints[2]
		       *old_extent;
		   flat->blocklens[j] = flat->blocklens[j-num];
		   j++;
		}
	    }
	    *curr_index = j;
	}
	break;

    case MPI_COMBINER_HVECTOR: 
	top_count = ints[0];
        MPI_Type_get_envelope(types[0], &old_nints, &old_nadds,
			      &old_ntypes, &old_combiner); 
        ADIOI_Datatype_iscontig(types[0], &old_is_contig);

	prev_index = *curr_index;
	if ((old_combiner != MPI_COMBINER_NAMED) && (!old_is_contig))
	    ADIOI_Flatten(types[0], flat, st_offset, curr_index);

	if (prev_index == *curr_index) {
/* simplest case, vector of basic or contiguous types */
	    j = *curr_index;
	    flat->indices[j] = st_offset;
	    MPI_Type_size(types[0], &old_size);
	    flat->blocklens[j] = ints[1] * old_size;
	    for (i=j+1; i<j+top_count; i++) {
		flat->indices[i] = flat->indices[i-1] + adds[0];
		flat->blocklens[i] = flat->blocklens[j];
	    }
	    *curr_index = i;
	}
	else {
/* vector of noncontiguous derived types */

	    j = *curr_index;
	    num = *curr_index - prev_index;

/* The noncontiguous types have to be replicated blocklen times
   and then strided. Replicate the first one. */
	    MPI_Type_extent(types[0], &old_extent);
	    for (m=1; m<ints[1]; m++) {
		for (i=0; i<num; i++) {
		    flat->indices[j] = flat->indices[j-num] + old_extent;
		    flat->blocklens[j] = flat->blocklens[j-num];
		    j++;
		}
	    }
	    *curr_index = j;

/* Now repeat with strides. */
	    num = *curr_index - prev_index;
	    for (i=1; i<top_count; i++) {
 		for (m=0; m<num; m++) {
		   flat->indices[j] =  flat->indices[j-num] + adds[0];
		   flat->blocklens[j] = flat->blocklens[j-num];
		   j++;
		}
	    }
	    *curr_index = j;
	}
	break;

    case MPI_COMBINER_INDEXED: 
	top_count = ints[0];
        MPI_Type_get_envelope(types[0], &old_nints, &old_nadds,
			      &old_ntypes, &old_combiner); 
        ADIOI_Datatype_iscontig(types[0], &old_is_contig);
	MPI_Type_extent(types[0], &old_extent);

	prev_index = *curr_index;
	if ((old_combiner != MPI_COMBINER_NAMED) && (!old_is_contig))
	    ADIOI_Flatten(types[0], flat,
			 st_offset+ints[top_count+1]*old_extent, curr_index);

	if (prev_index == *curr_index) {
/* simplest case, indexed type made up of basic or contiguous types */
	    j = *curr_index;
	    for (i=j; i<j+top_count; i++) {
		flat->indices[i] = st_offset + ints[top_count+1+i-j]*old_extent;
		flat->blocklens[i] = (int) (ints[1+i-j]*old_extent);
	    }
	    *curr_index = i;
	}
	else {
/* indexed type made up of noncontiguous derived types */

	    j = *curr_index;
	    num = *curr_index - prev_index;
	    basic_num = num;

/* The noncontiguous types have to be replicated blocklens[i] times
   and then strided. Replicate the first one. */
	    for (m=1; m<ints[1]; m++) {
		for (i=0; i<num; i++) {
		    flat->indices[j] = flat->indices[j-num] + old_extent;
		    flat->blocklens[j] = flat->blocklens[j-num];
		    j++;
		}
	    }
	    *curr_index = j;

/* Now repeat with strides. */
	    for (i=1; i<top_count; i++) {
		num = *curr_index - prev_index;
		prev_index = *curr_index;
		for (m=0; m<basic_num; m++) {
		    flat->indices[j] = flat->indices[j-num] + 
                        (ints[top_count+1+i]-ints[top_count+i])*old_extent;
		    flat->blocklens[j] = flat->blocklens[j-num];
		    j++;
		}
		*curr_index = j;
		for (m=1; m<ints[1+i]; m++) {
                    for (k=0; k<basic_num; k++) {
                        flat->indices[j] = flat->indices[j-basic_num] + old_extent;
                        flat->blocklens[j] = flat->blocklens[j-basic_num];
                        j++;
                    }
                }
		*curr_index = j;
	    }
	}
	break;

    case MPI_COMBINER_HINDEXED: 
	top_count = ints[0];
        MPI_Type_get_envelope(types[0], &old_nints, &old_nadds,
			      &old_ntypes, &old_combiner); 
        ADIOI_Datatype_iscontig(types[0], &old_is_contig);

	prev_index = *curr_index;
	if ((old_combiner != MPI_COMBINER_NAMED) && (!old_is_contig))
	    ADIOI_Flatten(types[0], flat, st_offset+adds[0], curr_index); 

	if (prev_index == *curr_index) {
/* simplest case, indexed type made up of basic or contiguous types */
	    j = *curr_index;
	    MPI_Type_size(types[0], &old_size);
	    for (i=j; i<j+top_count; i++) {
		flat->indices[i] = st_offset + adds[i-j];
		flat->blocklens[i] = ints[1+i-j]*old_size;
	    }
	    *curr_index = i;
	}
	else {
/* indexed type made up of noncontiguous derived types */

	    j = *curr_index;
	    num = *curr_index - prev_index;
	    basic_num = num;

/* The noncontiguous types have to be replicated blocklens[i] times
   and then strided. Replicate the first one. */
	    MPI_Type_extent(types[0], &old_extent);
	    for (m=1; m<ints[1]; m++) {
		for (i=0; i<num; i++) {
		    flat->indices[j] = flat->indices[j-num] + old_extent;
		    flat->blocklens[j] = flat->blocklens[j-num];
		    j++;
		}
	    }
	    *curr_index = j;

/* Now repeat with strides. */
	    for (i=1; i<top_count; i++) {
		num = *curr_index - prev_index;
		prev_index = *curr_index;
		for (m=0; m<basic_num; m++) {
		    flat->indices[j] = flat->indices[j-num] + adds[i] - adds[i-1];
		    flat->blocklens[j] = flat->blocklens[j-num];
		    j++;
		}
		*curr_index = j;
		for (m=1; m<ints[1+i]; m++) {
                    for (k=0; k<basic_num; k++) {
                        flat->indices[j] = flat->indices[j-basic_num] + old_extent;
                        flat->blocklens[j] = flat->blocklens[j-basic_num];
		    j++;
                    }
		}
		*curr_index = j;
	    }
	}
	break;

    case MPI_COMBINER_STRUCT: 
	top_count = ints[0];
	for (n=0; n<top_count; n++) {
	    MPI_Type_get_envelope(types[n], &old_nints, &old_nadds,
				  &old_ntypes, &old_combiner); 
            ADIOI_Datatype_iscontig(types[n], &old_is_contig);

	    prev_index = *curr_index;
            if ((old_combiner != MPI_COMBINER_NAMED) && (!old_is_contig))
		ADIOI_Flatten(types[n], flat, st_offset+adds[n], curr_index);

	    if (prev_index == *curr_index) {
/* simplest case, current type is basic or contiguous types */
		j = *curr_index;
		flat->indices[j] = st_offset + adds[n];
		MPI_Type_size(types[n], &old_size);
		flat->blocklens[j] = ints[1+n] * old_size;
		(*curr_index)++;
	    }
	    else {
/* current type made up of noncontiguous derived types */

		j = *curr_index;
		num = *curr_index - prev_index;

/* The current type has to be replicated blocklens[n] times */
		MPI_Type_extent(types[n], &old_extent);
		for (m=1; m<ints[1+n]; m++) {
		    for (i=0; i<num; i++) {
			flat->indices[j] = flat->indices[j-num] + old_extent;
			flat->blocklens[j] = flat->blocklens[j-num];
			j++;
		    }
		}
		*curr_index = j;
	    }
	}
 	break;

    default:
	/* TODO: FIXME (requires changing prototypes to return errors...) */
	FPRINTF(stderr, "Error: Unsupported datatype passed to ADIOI_Flatten\n");
	MPI_Abort(MPI_COMM_WORLD, 1);
    }

#ifndef MPISGI
/* There is a bug in SGI's impl. of MPI_Type_get_contents. It doesn't
   return new datatypes. Therefore no need to free. */
    for (i=0; i<ntypes; i++) {
 	MPI_Type_get_envelope(types[i], &old_nints, &old_nadds, &old_ntypes,
 			      &old_combiner);
 	if (old_combiner != MPI_COMBINER_NAMED) MPI_Type_free(types+i);
    }
#endif

    ADIOI_Free(ints);
    ADIOI_Free(adds);
    ADIOI_Free(types);

}

/********************************************************/

/* ADIOI_Count_contiguous_blocks
 *
 * Returns number of contiguous blocks in type, and also updates
 * curr_index to reflect the space for the additional blocks.
 *
 * ASSUMES THAT TYPE IS NOT A BASIC!!!
 */
int ADIOI_Count_contiguous_blocks(MPI_Datatype datatype, int *curr_index)
{
#ifdef HAVE_MPIR_TYPE_GET_CONTIG_BLOCKS
    /* MPICH2 can get us this value without all the envelope/contents calls */
    int blks;
    MPIR_Type_get_contig_blocks(datatype, &blks);
    *curr_index = blks;
    return blks;
#else
    int count=0, i, n, num, basic_num, prev_index;
    int top_count, combiner, old_combiner, old_is_contig;
    int nints, nadds, ntypes, old_nints, old_nadds, old_ntypes;
    int *ints;
    MPI_Aint *adds;
    MPI_Datatype *types;

    MPI_Type_get_envelope(datatype, &nints, &nadds, &ntypes, &combiner);
    ints = (int *) ADIOI_Malloc((nints+1)*sizeof(int));
    adds = (MPI_Aint *) ADIOI_Malloc((nadds+1)*sizeof(MPI_Aint));
    types = (MPI_Datatype *) ADIOI_Malloc((ntypes+1)*sizeof(MPI_Datatype));
    MPI_Type_get_contents(datatype, nints, nadds, ntypes, ints, adds, types);

    switch (combiner) {
#ifdef MPIIMPL_HAVE_MPI_COMBINER_DUP
    case MPI_COMBINER_DUP:
        MPI_Type_get_envelope(types[0], &old_nints, &old_nadds,
                              &old_ntypes, &old_combiner); 
	ADIOI_Datatype_iscontig(types[0], &old_is_contig);
	if ((old_combiner != MPI_COMBINER_NAMED) && (!old_is_contig))
	    count = ADIOI_Count_contiguous_blocks(types[0], curr_index);
	else {
		count = 1;
		(*curr_index)++;
	}
        break;
#endif
#ifdef MPIIMPL_HAVE_MPI_COMBINER_SUBARRAY
    case MPI_COMBINER_SUBARRAY:
        {
	    int dims = ints[0];
	    MPI_Datatype stype;

	    ADIO_Type_create_subarray(dims,
				      &ints[1],        /* sizes */
				      &ints[dims+1],   /* subsizes */
				      &ints[2*dims+1], /* starts */
				      ints[3*dims+1],  /* order */
				      types[0],        /* type */
				      &stype);
	    count = ADIOI_Count_contiguous_blocks(stype, curr_index);
	    /* curr_index will have already been updated; just pass
	     * count back up.
	     */
	    MPI_Type_free(&stype);

	}
	break;
#endif
#ifdef MPIIMPL_HAVE_MPI_COMBINER_DARRAY
    case MPI_COMBINER_DARRAY:
	{
	    int dims = ints[2];
	    MPI_Datatype dtype;

	    ADIO_Type_create_darray(ints[0],         /* size */
				    ints[1],         /* rank */
				    dims,
				    &ints[3],        /* gsizes */
				    &ints[dims+3],   /* distribs */
				    &ints[2*dims+3], /* dargs */
				    &ints[3*dims+3], /* psizes */
				    ints[4*dims+3],  /* order */
				    types[0],
				    &dtype);
	    count = ADIOI_Count_contiguous_blocks(dtype, curr_index);
	    /* curr_index will have already been updated; just pass
	     * count back up.
	     */
	    MPI_Type_free(&dtype);
	}
	break;
#endif
    case MPI_COMBINER_CONTIGUOUS:
        top_count = ints[0];
        MPI_Type_get_envelope(types[0], &old_nints, &old_nadds,
                              &old_ntypes, &old_combiner); 
	ADIOI_Datatype_iscontig(types[0], &old_is_contig);

	prev_index = *curr_index;
	if ((old_combiner != MPI_COMBINER_NAMED) && (!old_is_contig))
	    count = ADIOI_Count_contiguous_blocks(types[0], curr_index);
	else count = 1;

	if (prev_index == *curr_index) 
/* simplest case, made up of basic or contiguous types */
	    (*curr_index)++;
	else {
/* made up of noncontiguous derived types */
	    num = *curr_index - prev_index;
	    count *= top_count;
	    *curr_index += (top_count - 1)*num;
	}
	break;

    case MPI_COMBINER_VECTOR:
    case MPI_COMBINER_HVECTOR:
        top_count = ints[0];
        MPI_Type_get_envelope(types[0], &old_nints, &old_nadds,
                              &old_ntypes, &old_combiner); 
	ADIOI_Datatype_iscontig(types[0], &old_is_contig);

	prev_index = *curr_index;
	if ((old_combiner != MPI_COMBINER_NAMED) && (!old_is_contig))
	    count = ADIOI_Count_contiguous_blocks(types[0], curr_index);
	else count = 1;

	if (prev_index == *curr_index) {
/* simplest case, vector of basic or contiguous types */
	    count = top_count;
	    *curr_index += count;
	}
	else {
/* vector of noncontiguous derived types */
	    num = *curr_index - prev_index;

/* The noncontiguous types have to be replicated blocklen times
   and then strided. */
	    count *= ints[1] * top_count;

/* First one */
	    *curr_index += (ints[1] - 1)*num;

/* Now repeat with strides. */
	    num = *curr_index - prev_index;
	    *curr_index += (top_count - 1)*num;
	}
	break;

    case MPI_COMBINER_INDEXED: 
    case MPI_COMBINER_HINDEXED:
        top_count = ints[0];
        MPI_Type_get_envelope(types[0], &old_nints, &old_nadds,
                              &old_ntypes, &old_combiner); 
	ADIOI_Datatype_iscontig(types[0], &old_is_contig);

	prev_index = *curr_index;
	if ((old_combiner != MPI_COMBINER_NAMED) && (!old_is_contig))
	    count = ADIOI_Count_contiguous_blocks(types[0], curr_index);
	else count = 1;

	if (prev_index == *curr_index) {
/* simplest case, indexed type made up of basic or contiguous types */
	    count = top_count;
	    *curr_index += count;
	}
	else {
/* indexed type made up of noncontiguous derived types */
	    basic_num = *curr_index - prev_index;

/* The noncontiguous types have to be replicated blocklens[i] times
   and then strided. */
	    *curr_index += (ints[1]-1) * basic_num;
	    count *= ints[1];

/* Now repeat with strides. */
	    for (i=1; i<top_count; i++) {
		count += ints[1+i] * basic_num;
		*curr_index += ints[1+i] * basic_num;
	    }
	}
	break;

    case MPI_COMBINER_STRUCT: 
        top_count = ints[0];
	count = 0;
	for (n=0; n<top_count; n++) {
            MPI_Type_get_envelope(types[n], &old_nints, &old_nadds,
                                  &old_ntypes, &old_combiner); 
	    ADIOI_Datatype_iscontig(types[n], &old_is_contig);

	    prev_index = *curr_index;
	    if ((old_combiner != MPI_COMBINER_NAMED) && (!old_is_contig))
	    count += ADIOI_Count_contiguous_blocks(types[n], curr_index);

	    if (prev_index == *curr_index) {
/* simplest case, current type is basic or contiguous types */
		count++;
		(*curr_index)++;
	    }
	    else {
/* current type made up of noncontiguous derived types */
/* The current type has to be replicated blocklens[n] times */

		num = *curr_index - prev_index;
		count += (ints[1+n]-1)*num;
		(*curr_index) += (ints[1+n]-1)*num;
	    }
	}
	break;
    default:
	/* TODO: FIXME */
	FPRINTF(stderr, "Error: Unsupported datatype passed to ADIOI_Count_contiguous_blocks\n");
	MPI_Abort(MPI_COMM_WORLD, 1);
    }

#ifndef MPISGI
/* There is a bug in SGI's impl. of MPI_Type_get_contents. It doesn't
   return new datatypes. Therefore no need to free. */
    for (i=0; i<ntypes; i++) {
 	MPI_Type_get_envelope(types[i], &old_nints, &old_nadds, &old_ntypes,
 			      &old_combiner);
 	if (old_combiner != MPI_COMBINER_NAMED) MPI_Type_free(types+i);
    }
#endif

    ADIOI_Free(ints);
    ADIOI_Free(adds);
    ADIOI_Free(types);
    return count;
#endif /* HAVE_MPIR_TYPE_GET_CONTIG_BLOCKS */
}


/****************************************************************/

/* ADIOI_Optimize_flattened()
 *
 * Scans the blocks of a flattened type and merges adjacent blocks 
 * together, resulting in a shorter blocklist (and thus fewer
 * contiguous operations).
 *
 * Q: IS IT SAFE TO REMOVE THE 0-LENGTH BLOCKS TOO?
 */
void ADIOI_Optimize_flattened(ADIOI_Flatlist_node *flat_type)
{
    int i, j, opt_blocks;
    int *opt_blocklens;
    ADIO_Offset *opt_indices;
    
    opt_blocks = 1;
    
    /* save number of noncontiguous blocks in opt_blocks */
    for (i=0; i < (flat_type->count - 1); i++) {
        if ((flat_type->indices[i] + flat_type->blocklens[i] !=
	     flat_type->indices[i + 1]))
	    opt_blocks++;
    }

    /* if we can't reduce the number of blocks, quit now */
    if (opt_blocks == flat_type->count) return;

    opt_blocklens = (int *) ADIOI_Malloc(opt_blocks * sizeof(int));
    opt_indices = (ADIO_Offset *)ADIOI_Malloc(opt_blocks*sizeof(ADIO_Offset));

    /* fill in new blocklists */
    opt_blocklens[0] = flat_type->blocklens[0];
    opt_indices[0] = flat_type->indices[0];
    j = 0;
    for (i=0; i < (flat_type->count - 1); i++) {
	if ((flat_type->indices[i] + flat_type->blocklens[i] ==
	     flat_type->indices[i + 1]))
	    opt_blocklens[j] += flat_type->blocklens[i + 1];
	else {
	    j++;
	    opt_indices[j] = flat_type->indices[i + 1];
	    opt_blocklens[j] = flat_type->blocklens[i + 1];
	} 
    }
    flat_type->count = opt_blocks;
    ADIOI_Free(flat_type->blocklens);
    ADIOI_Free(flat_type->indices);
    flat_type->blocklens = opt_blocklens;
    flat_type->indices = opt_indices;
    return;
}

void ADIOI_Delete_flattened(MPI_Datatype datatype)
{
    ADIOI_Flatlist_node *flat, *prev;

    prev = flat = ADIOI_Flatlist;
    while (flat && (flat->type != datatype)) {
	prev = flat;
	flat = flat->next;
    }
    if (flat) {
	prev->next = flat->next;
	if (flat->blocklens) ADIOI_Free(flat->blocklens);
	if (flat->indices) ADIOI_Free(flat->indices);
	ADIOI_Free(flat);
    }
}

/* ADIOI_Flatten_copy_type()
 * flat - pointer to flatlist node holding offset and lengths
 * start - starting index of src type in arrays
 * end - one larger than ending index of src type (makes loop clean)
 * offset_adjustment - amount to add to "indices" (offset) component
 *                     of each off/len pair copied
 */
void ADIOI_Flatten_copy_type(ADIOI_Flatlist_node *flat,
			     int old_type_start,
			     int old_type_end,
			     int new_type_start,
			     ADIO_Offset offset_adjustment)
{
    int i, out_index = new_type_start;

    for (i=old_type_start; i < old_type_end; i++) {
	flat->indices[out_index]   = flat->indices[i] + offset_adjustment;
	flat->blocklens[out_index] = flat->blocklens[i];
	out_index++;
    }
}

