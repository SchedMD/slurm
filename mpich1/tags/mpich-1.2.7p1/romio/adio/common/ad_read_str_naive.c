/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 2001 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "adio.h"
#include "adio_extern.h"

void ADIOI_GEN_ReadStrided_naive(ADIO_File fd, void *buf, int count,
                       MPI_Datatype buftype, int file_ptr_type,
                       ADIO_Offset offset, ADIO_Status *status, int
                       *error_code)
{
    /* offset is in units of etype relative to the filetype. */

    ADIOI_Flatlist_node *flat_buf, *flat_file;
    int brd_size, frd_size=0, b_index;
    int bufsize, size, sum, n_etypes_in_filetype, size_in_filetype;
    int n_filetypes, etype_in_filetype;
    ADIO_Offset abs_off_in_filetype=0;
    int filetype_size, etype_size, buftype_size, req_len;
    MPI_Aint filetype_extent, buftype_extent; 
    int buf_count, buftype_is_contig, filetype_is_contig;
    ADIO_Offset userbuf_off;
    ADIO_Offset off, req_off, disp, end_offset=0, start_off;
    ADIO_Status status1;

    *error_code = MPI_SUCCESS;  /* changed below if error */

    ADIOI_Datatype_iscontig(buftype, &buftype_is_contig);
    ADIOI_Datatype_iscontig(fd->filetype, &filetype_is_contig);

    MPI_Type_size(fd->filetype, &filetype_size);
    if ( ! filetype_size ) {
	*error_code = MPI_SUCCESS; 
	return;
    }

    MPI_Type_extent(fd->filetype, &filetype_extent);
    MPI_Type_size(buftype, &buftype_size);
    MPI_Type_extent(buftype, &buftype_extent);
    etype_size = fd->etype_size;

    bufsize = buftype_size * count;

    /* contiguous in buftype and filetype is handled elsewhere */

    if (!buftype_is_contig && filetype_is_contig) {
    	int b_count;
	/* noncontiguous in memory, contiguous in file. */

	ADIOI_Flatten_datatype(buftype);
	flat_buf = ADIOI_Flatlist;
	while (flat_buf->type != buftype) flat_buf = flat_buf->next;

        off = (file_ptr_type == ADIO_INDIVIDUAL) ? fd->fp_ind : 
              fd->disp + etype_size * offset;

	start_off = off;
	end_offset = off + bufsize - 1;

	/* if atomicity is true, lock (exclusive) the region to be accessed */
        if ((fd->atomicity) && (fd->file_system != ADIO_PIOFS) && 
	   (fd->file_system != ADIO_PVFS))
	{
            ADIOI_WRITE_LOCK(fd, start_off, SEEK_SET, end_offset-start_off+1);
	}

	/* for each region in the buffer, grab the data and put it in
	 * place
	 */
        for (b_count=0; b_count < count; b_count++) {
            for (b_index=0; b_index < flat_buf->count; b_index++) {
                userbuf_off = b_count*buftype_extent + 
		              flat_buf->indices[b_index];
		req_off = off;
		req_len = flat_buf->blocklens[b_index];

		ADIO_ReadContig(fd, 
				(char *) buf + userbuf_off,
				req_len, 
				MPI_BYTE, 
		    		ADIO_EXPLICIT_OFFSET,
				req_off,
				&status1,
				error_code);
		if (*error_code != MPI_SUCCESS) return;

		/* off is (potentially) used to save the final offset later */
                off += flat_buf->blocklens[b_index];
            }
	}

        if ((fd->atomicity) && (fd->file_system != ADIO_PIOFS) && 
	   (fd->file_system != ADIO_PVFS))
	{
            ADIOI_UNLOCK(fd, start_off, SEEK_SET, end_offset-start_off+1);
	}

        if (file_ptr_type == ADIO_INDIVIDUAL) fd->fp_ind = off;

    }

    else {  /* noncontiguous in file */
    	int f_index, st_frd_size, st_index = 0, st_n_filetypes;
	int flag;

        /* First we're going to calculate a set of values for use in all
	 * the noncontiguous in file cases:
	 * start_off - starting byte position of data in file
	 * end_offset - last byte offset to be acessed in the file
	 * st_n_filetypes - how far into the file we start in terms of
	 *                  whole filetypes
	 * st_index - index of block in first filetype that we will be
	 *            starting in (?)
	 * st_frd_size - size of the data in the first filetype block
	 *               that we will read (accounts for being part-way
	 *               into reading this block of the filetype
	 *
	 */

	/* filetype already flattened in ADIO_Open */
	flat_file = ADIOI_Flatlist;
	while (flat_file->type != fd->filetype) flat_file = flat_file->next;
	disp = fd->disp;

	if (file_ptr_type == ADIO_INDIVIDUAL) {
	    start_off = fd->fp_ind; /* in bytes */
	    n_filetypes = -1;
	    flag = 0;
	    while (!flag) {
                n_filetypes++;
		for (f_index=0; f_index < flat_file->count; f_index++) {
		    if (disp + flat_file->indices[f_index] + 
                       (ADIO_Offset) n_filetypes*filetype_extent + 
		       flat_file->blocklens[f_index] >= start_off) 
		    {
		    	/* this block contains our starting position */

			st_index = f_index;
			frd_size = (int) (disp + flat_file->indices[f_index] + 
		 	           (ADIO_Offset) n_filetypes*filetype_extent + 
				   flat_file->blocklens[f_index] - start_off);
			flag = 1;
			break;
		    }
		}
	    }
	}
	else {
	    n_etypes_in_filetype = filetype_size/etype_size;
	    n_filetypes = (int) (offset / n_etypes_in_filetype);
	    etype_in_filetype = (int) (offset % n_etypes_in_filetype);
	    size_in_filetype = etype_in_filetype * etype_size;
 
	    sum = 0;
	    for (f_index=0; f_index < flat_file->count; f_index++) {
		sum += flat_file->blocklens[f_index];
		if (sum > size_in_filetype) {
		    st_index = f_index;
		    frd_size = sum - size_in_filetype;
		    abs_off_in_filetype = flat_file->indices[f_index] +
			                  size_in_filetype - 
			                  (sum - flat_file->blocklens[f_index]);
		    break;
		}
	    }

	    /* abs. offset in bytes in the file */
	    start_off = disp + (ADIO_Offset) n_filetypes*filetype_extent + 
	    	        abs_off_in_filetype;
	}

	st_frd_size = frd_size;
	st_n_filetypes = n_filetypes;

	/* start_off, st_n_filetypes, st_index, and st_frd_size are 
	 * all calculated at this point
	 */

        /* Calculate end_offset, the last byte-offset that will be accessed.
         * e.g., if start_off=0 and 100 bytes to be read, end_offset=99
	 */
	userbuf_off = 0;
	f_index = st_index;
	off = start_off;
	frd_size = ADIOI_MIN(st_frd_size, bufsize);
	while (userbuf_off < bufsize) {
	    userbuf_off += frd_size;
	    end_offset = off + frd_size - 1;

	    if (f_index < (flat_file->count - 1)) f_index++;
	    else {
		f_index = 0;
		n_filetypes++;
	    }

	    off = disp + flat_file->indices[f_index] + 
	          (ADIO_Offset) n_filetypes*filetype_extent;
	    frd_size = ADIOI_MIN(flat_file->blocklens[f_index], 
	                         bufsize-(int)userbuf_off);
	}

	/* End of calculations.  At this point the following values have
	 * been calculated and are ready for use:
	 * - start_off
	 * - end_offset
	 * - st_n_filetypes
	 * - st_index
	 * - st_frd_size
	 */

	/* if atomicity is true, lock (exclusive) the region to be accessed */
        if ((fd->atomicity) && (fd->file_system != ADIO_PIOFS) && 
	   (fd->file_system != ADIO_PVFS))
	{
            ADIOI_WRITE_LOCK(fd, start_off, SEEK_SET, end_offset-start_off+1);
	}

	if (buftype_is_contig && !filetype_is_contig) {
	    /* contiguous in memory, noncontiguous in file. should be the
	     * most common case.
	     */

	    userbuf_off = 0;
	    f_index = st_index;
	    off = start_off;
	    n_filetypes = st_n_filetypes;
	    frd_size = ADIOI_MIN(st_frd_size, bufsize);

	    /* while there is still space in the buffer, read more data */
	    while (userbuf_off < bufsize) {
                if (frd_size) { 
                    /* TYPE_UB and TYPE_LB can result in 
                       frd_size = 0. save system call in such cases */ 
		    req_off = off;
		    req_len = frd_size;

		    ADIO_ReadContig(fd, 
				    (char *) buf + userbuf_off,
				    req_len, 
				    MPI_BYTE, 
				    ADIO_EXPLICIT_OFFSET,
				    req_off,
				    &status1,
				    error_code);
		    if (*error_code != MPI_SUCCESS) return;
		}
		userbuf_off += frd_size;

                if (off + frd_size < disp + flat_file->indices[f_index] +
                   flat_file->blocklens[f_index] + 
		   (ADIO_Offset) n_filetypes*filetype_extent)
		{
		    /* important that this value be correct, as it is
		     * used to set the offset in the fd near the end of
		     * this function.
		     */
                    off += frd_size;
		}
                /* did not reach end of contiguous block in filetype.
                 * no more I/O needed. off is incremented by frd_size.
		 */
                else {
		    if (f_index < (flat_file->count - 1)) f_index++;
		    else {
			f_index = 0;
			n_filetypes++;
		    }
		    off = disp + flat_file->indices[f_index] + 
                          (ADIO_Offset) n_filetypes*filetype_extent;
		    frd_size = ADIOI_MIN(flat_file->blocklens[f_index], 
		                         bufsize-(int)userbuf_off);
		}
	    }
	}
	else {
	    int i, tmp_bufsize = 0;
	    /* noncontiguous in memory as well as in file */

	    ADIOI_Flatten_datatype(buftype);
	    flat_buf = ADIOI_Flatlist;
	    while (flat_buf->type != buftype) flat_buf = flat_buf->next;

	    b_index = buf_count = 0;
	    i = (int) (flat_buf->indices[0]);
	    f_index = st_index;
	    off = start_off;
	    n_filetypes = st_n_filetypes;
	    frd_size = st_frd_size;
	    brd_size = flat_buf->blocklens[0];

	    /* while we haven't read size * count bytes, keep going */
	    while (tmp_bufsize < bufsize) {
    		int new_brd_size = brd_size, new_frd_size = frd_size;

		size = ADIOI_MIN(frd_size, brd_size);
		if (size) {
		    req_off = off;
		    req_len = size;
		    userbuf_off = i;

		    ADIO_ReadContig(fd, 
				    (char *) buf + userbuf_off,
				    req_len, 
				    MPI_BYTE, 
				    ADIO_EXPLICIT_OFFSET,
				    req_off,
				    &status1,
				    error_code);
		    if (*error_code != MPI_SUCCESS) return;
		}

		if (size == frd_size) {
		    /* reached end of contiguous block in file */
		    if (f_index < (flat_file->count - 1)) f_index++;
		    else {
			f_index = 0;
			n_filetypes++;
		    }

		    off = disp + flat_file->indices[f_index] + 
                          (ADIO_Offset) n_filetypes*filetype_extent;

		    new_frd_size = flat_file->blocklens[f_index];
		    if (size != brd_size) {
			i += size;
			new_brd_size -= size;
		    }
		}

		if (size == brd_size) {
		    /* reached end of contiguous block in memory */

		    b_index = (b_index + 1)%flat_buf->count;
		    buf_count++;
		    i = (int) (buftype_extent*(buf_count/flat_buf->count) +
			flat_buf->indices[b_index]);
		    new_brd_size = flat_buf->blocklens[b_index];
		    if (size != frd_size) {
			off += size;
			new_frd_size -= size;
		    }
		}
		tmp_bufsize += size;
		frd_size = new_frd_size;
                brd_size = new_brd_size;
	    }
	}

	/* unlock the file region if we locked it */
        if ((fd->atomicity) && (fd->file_system != ADIO_PIOFS) && 
	   (fd->file_system != ADIO_PVFS))
	{
            ADIOI_UNLOCK(fd, start_off, SEEK_SET, end_offset-start_off+1);
	}

	if (file_ptr_type == ADIO_INDIVIDUAL) fd->fp_ind = off;
    } /* end of (else noncontiguous in file) */

    fd->fp_sys_posn = -1;   /* mark it as invalid. */

#ifdef HAVE_STATUS_SET_BYTES
    MPIR_Status_set_bytes(status, buftype, bufsize);
    /* This is a temporary way of filling in status. The right way is to 
     * keep track of how much data was actually read and placed in buf 
     */
#endif

    if (!buftype_is_contig) ADIOI_Delete_flattened(buftype);
}
