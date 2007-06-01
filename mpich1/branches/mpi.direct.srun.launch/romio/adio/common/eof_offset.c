/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "adio.h"
#include "adio_extern.h"

/* return the current end of file in etype units relative to the 
   current view */

void ADIOI_Get_eof_offset(ADIO_File fd, ADIO_Offset *eof_offset)
{
    int error_code, filetype_is_contig, etype_size, filetype_size;
    ADIO_Offset fsize, disp, sum=0, size_in_file;
    int n_filetypes, flag, i, rem;
    ADIO_Fcntl_t *fcntl_struct;
    MPI_Aint filetype_extent;
    ADIOI_Flatlist_node *flat_file;

    /* find the eof in bytes */
    fcntl_struct = (ADIO_Fcntl_t *) ADIOI_Malloc(sizeof(ADIO_Fcntl_t));
    ADIO_Fcntl(fd, ADIO_FCNTL_GET_FSIZE, fcntl_struct, &error_code);
    fsize = fcntl_struct->fsize;
    ADIOI_Free(fcntl_struct);
	
    /* Find the offset in etype units corresponding to eof.
       The eof could lie in a hole in the current view, or in the 
       middle of an etype. In that case the offset will be the offset
       corresponding to the start of the next etype in the current view.*/

    ADIOI_Datatype_iscontig(fd->filetype, &filetype_is_contig);
    etype_size = fd->etype_size;

    if (filetype_is_contig) 
	*eof_offset = (fsize - fd->disp + etype_size - 1)/etype_size;
    /* ceiling division in case fsize is not a multiple of etype_size;*/
    else {
	/* filetype already flattened in ADIO_Open */
	flat_file = ADIOI_Flatlist;
	while (flat_file->type != fd->filetype) 
	    flat_file = flat_file->next;
	
	MPI_Type_size(fd->filetype, &filetype_size);
	MPI_Type_extent(fd->filetype, &filetype_extent);

	disp = fd->disp;
	n_filetypes = -1;
	flag = 0;
	while (!flag) {
	    sum = 0;
	    n_filetypes++;
	    for (i=0; i<flat_file->count; i++) {
		sum += flat_file->blocklens[i];
		if (disp + flat_file->indices[i] + 
		    (ADIO_Offset) n_filetypes*filetype_extent + 
		       flat_file->blocklens[i] >= fsize) {
		    if (disp + flat_file->indices[i] + 
			   (ADIO_Offset) n_filetypes*filetype_extent >= fsize)
			sum -= flat_file->blocklens[i];
		    else {
			rem = (int) (disp + flat_file->indices[i] + 
				(ADIO_Offset) n_filetypes*filetype_extent
				+ flat_file->blocklens[i] - fsize);
			sum -= rem;
		    }
		    flag = 1;
		    break;
		}
	    }
	}
	size_in_file = (ADIO_Offset) n_filetypes*filetype_size + sum;
	*eof_offset = (size_in_file+etype_size-1)/etype_size; /* ceiling division */
    }
}
