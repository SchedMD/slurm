/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_pvfs.h"
#include "adio_extern.h"

#ifdef HAVE_PVFS_LISTIO
void ADIOI_PVFS_WriteStridedListIO(ADIO_File fd, void *buf, int count,
                       MPI_Datatype datatype, int file_ptr_type,
                       ADIO_Offset offset, ADIO_Status *status, int
                       *error_code);
#endif

void ADIOI_PVFS_WriteContig(ADIO_File fd, void *buf, int count, 
			    MPI_Datatype datatype, int file_ptr_type,
			    ADIO_Offset offset, ADIO_Status *status,
			    int *error_code)
{
    int err=-1, datatype_size, len;
    static char myname[] = "ADIOI_PVFS_WRITECONTIG";

    MPI_Type_size(datatype, &datatype_size);
    len = datatype_size * count;

    if (file_ptr_type == ADIO_EXPLICIT_OFFSET) {
	if (fd->fp_sys_posn != offset)
	    pvfs_lseek64(fd->fd_sys, offset, SEEK_SET);
	err = pvfs_write(fd->fd_sys, buf, len);
	fd->fp_sys_posn = offset + err;
	/* individual file pointer not updated */        
    }
    else { /* write from curr. location of ind. file pointer */
	if (fd->fp_sys_posn != fd->fp_ind)
	    pvfs_lseek64(fd->fd_sys, fd->fp_ind, SEEK_SET);
	err = pvfs_write(fd->fd_sys, buf, len);
	fd->fp_ind += err;
	fd->fp_sys_posn = fd->fp_ind;
    }

#ifdef HAVE_STATUS_SET_BYTES
    if (err != -1) MPIR_Status_set_bytes(status, datatype, err);
#endif

    if (err == -1) {
	*error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					   myname, __LINE__, MPI_ERR_IO,
					   "**io",
					   "**io %s", strerror(errno));
    }
    else *error_code = MPI_SUCCESS;
}



void ADIOI_PVFS_WriteStrided(ADIO_File fd, void *buf, int count,
			     MPI_Datatype datatype, int file_ptr_type,
			     ADIO_Offset offset, ADIO_Status *status, int
			     *error_code)
{
/* Since PVFS does not support file locking, can't do buffered writes
   as on Unix */

/* offset is in units of etype relative to the filetype. */

    ADIOI_Flatlist_node *flat_buf, *flat_file;
    int i, j, k, err=-1, bwr_size, fwr_size=0, st_index=0;
    int bufsize, num, size, sum, n_etypes_in_filetype, size_in_filetype;
    int n_filetypes, etype_in_filetype;
    ADIO_Offset abs_off_in_filetype=0;
    int filetype_size, etype_size, buftype_size;
    MPI_Aint filetype_extent, buftype_extent, indx;
    int buf_count, buftype_is_contig, filetype_is_contig;
    ADIO_Offset off, disp;
    int flag, new_bwr_size, new_fwr_size, err_flag=0;
    static char myname[] = "ADIOI_PVFS_WRITESTRIDED";

#ifdef HAVE_PVFS_LISTIO
    if ( fd->hints->fs_hints.pvfs.listio_write == ADIOI_HINT_ENABLE ) {
	    ADIOI_PVFS_WriteStridedListIO(fd, buf, count, datatype, 
			    file_ptr_type, offset, status, error_code);
	    return;
    }
#endif
    /* if hint set to DISABLE or AUTOMATIC, don't use listio */

    /* --BEGIN ERROR HANDLING-- */
    if (fd->atomicity) {
	*error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					   myname, __LINE__,
					   MPI_ERR_INTERN,
					   "Atomic mode set in PVFS I/O function", 0);
	return;
    }
    /* --END ERROR HANDLING-- */

    ADIOI_Datatype_iscontig(datatype, &buftype_is_contig);
    ADIOI_Datatype_iscontig(fd->filetype, &filetype_is_contig);

    MPI_Type_size(fd->filetype, &filetype_size);
    if ( ! filetype_size ) {
	*error_code = MPI_SUCCESS; 
	return;
    }

    MPI_Type_extent(fd->filetype, &filetype_extent);
    MPI_Type_size(datatype, &buftype_size);
    MPI_Type_extent(datatype, &buftype_extent);
    etype_size = fd->etype_size;
    
    bufsize = buftype_size * count;

    if (!buftype_is_contig && filetype_is_contig) {
	char *combine_buf, *combine_buf_ptr;
	ADIO_Offset combine_buf_remain;
/* noncontiguous in memory, contiguous in file. use writev */

	ADIOI_Flatten_datatype(datatype);
	flat_buf = ADIOI_Flatlist;
	while (flat_buf->type != datatype) flat_buf = flat_buf->next;

	/* allocate our "combine buffer" to pack data into before writing */
	combine_buf = (char *) ADIOI_Malloc(fd->hints->ind_wr_buffer_size);
	combine_buf_ptr = combine_buf;
	combine_buf_remain = fd->hints->ind_wr_buffer_size;

	/* seek to the right spot in the file */
	if (file_ptr_type == ADIO_EXPLICIT_OFFSET) {
	    off = fd->disp + etype_size * offset;
	    pvfs_lseek64(fd->fd_sys, off, SEEK_SET);
	}
	else off = pvfs_lseek64(fd->fd_sys, fd->fp_ind, SEEK_SET);

	/* loop through all the flattened pieces.  combine into buffer until
	 * no more will fit, then write.
	 *
	 * special case of a given piece being bigger than the combine buffer
	 * is also handled.
	 */
	for (j=0; j<count; j++) {
	    for (i=0; i<flat_buf->count; i++) {
		if (flat_buf->blocklens[i] > combine_buf_remain && combine_buf != combine_buf_ptr) {
		    /* there is data in the buffer; write out the buffer so far */
		    err = pvfs_write(fd->fd_sys,
				     combine_buf,
				     fd->hints->ind_wr_buffer_size - combine_buf_remain);
		    if (err == -1) err_flag = 1;

		    /* reset our buffer info */
		    combine_buf_ptr = combine_buf;
		    combine_buf_remain = fd->hints->ind_wr_buffer_size;
		}

		/* TODO: heuristic for when to not bother to use combine buffer? */
		if (flat_buf->blocklens[i] >= combine_buf_remain) {
		    /* special case: blocklen is as big as or bigger than the combine buf;
		     * write directly
		     */
		    err = pvfs_write(fd->fd_sys,
				     ((char *) buf) + j*buftype_extent + flat_buf->indices[i],
				     flat_buf->blocklens[i]);
		    if (err == -1) err_flag = 1;
		    off += flat_buf->blocklens[i]; /* keep up with the final file offset too */
		}
		else {
		    /* copy more data into combine buffer */
		    memcpy(combine_buf_ptr,
			   ((char *) buf) + j*buftype_extent + flat_buf->indices[i],
			   flat_buf->blocklens[i]);
		    combine_buf_ptr += flat_buf->blocklens[i];
		    combine_buf_remain -= flat_buf->blocklens[i];
		    off += flat_buf->blocklens[i]; /* keep up with the final file offset too */
		}
	    }
	}

	if (combine_buf_ptr != combine_buf) {
	    /* data left in buffer to write */
	    err = pvfs_write(fd->fd_sys,
			     combine_buf,
			     fd->hints->ind_wr_buffer_size - combine_buf_remain);
	    if (err == -1) err_flag = 1;
	}

	if (file_ptr_type == ADIO_INDIVIDUAL) fd->fp_ind = off;

	ADIOI_Free(combine_buf);

	if (err_flag) {
	    *error_code = MPIO_Err_create_code(MPI_SUCCESS,
					       MPIR_ERR_RECOVERABLE, myname,
					       __LINE__, MPI_ERR_IO, "**io",
					       "**io %s", strerror(errno));
	}
	else *error_code = MPI_SUCCESS;
    } /* if (!buftype_is_contig && filetype_is_contig)  ... */

    else {  /* noncontiguous in file */

/* split up into several contiguous writes */

/* find starting location in the file */

/* filetype already flattened in ADIO_Open */
	flat_file = ADIOI_Flatlist;
	while (flat_file->type != fd->filetype) flat_file = flat_file->next;
        disp = fd->disp;

	if (file_ptr_type == ADIO_INDIVIDUAL) {
	    offset = fd->fp_ind; /* in bytes */
            n_filetypes = -1;
            flag = 0;
            while (!flag) {
                n_filetypes++;
                for (i=0; i<flat_file->count; i++) {
                    if (disp + flat_file->indices[i] + 
                        (ADIO_Offset) n_filetypes*filetype_extent + flat_file->blocklens[i] 
                            >= offset) {
                        st_index = i;
                        fwr_size = disp + flat_file->indices[i] + 
                                (ADIO_Offset) n_filetypes*filetype_extent
                                 + flat_file->blocklens[i] - offset;
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
	    for (i=0; i<flat_file->count; i++) {
		sum += flat_file->blocklens[i];
		if (sum > size_in_filetype) {
		    st_index = i;
		    fwr_size = sum - size_in_filetype;
		    abs_off_in_filetype = flat_file->indices[i] +
			size_in_filetype - (sum - flat_file->blocklens[i]);
		    break;
		}
	    }

	    /* abs. offset in bytes in the file */
            offset = disp + (ADIO_Offset) n_filetypes*filetype_extent + abs_off_in_filetype;
	}

	if (buftype_is_contig && !filetype_is_contig) {

/* contiguous in memory, noncontiguous in file. should be the most
   common case. */

	    i = 0;
	    j = st_index;
	    off = offset;
	    fwr_size = ADIOI_MIN(fwr_size, bufsize);
	    while (i < bufsize) {
                if (fwr_size) { 
                    /* TYPE_UB and TYPE_LB can result in 
                       fwr_size = 0. save system call in such cases */ 
#ifdef PROFILE
		    MPE_Log_event(11, 0, "start seek");
#endif
		    pvfs_lseek64(fd->fd_sys, off, SEEK_SET);
#ifdef PROFILE
		    MPE_Log_event(12, 0, "end seek");
		    MPE_Log_event(5, 0, "start write");
#endif
		    err = pvfs_write(fd->fd_sys, ((char *) buf) + i, fwr_size);
#ifdef PROFILE
		    MPE_Log_event(6, 0, "end write");
#endif
		    if (err == -1) err_flag = 1;
		}
		i += fwr_size;

                if (off + fwr_size < disp + flat_file->indices[j] +
                   flat_file->blocklens[j] + (ADIO_Offset) n_filetypes*filetype_extent)
                       off += fwr_size;
                /* did not reach end of contiguous block in filetype.
                   no more I/O needed. off is incremented by fwr_size. */
                else {
		    if (j < (flat_file->count - 1)) j++;
		    else {
			j = 0;
			n_filetypes++;
		    }
		    off = disp + flat_file->indices[j] + 
                                        (ADIO_Offset) n_filetypes*filetype_extent;
		    fwr_size = ADIOI_MIN(flat_file->blocklens[j], bufsize-i);
		}
	    }
	}
	else {
/* noncontiguous in memory as well as in file */

	    ADIOI_Flatten_datatype(datatype);
	    flat_buf = ADIOI_Flatlist;
	    while (flat_buf->type != datatype) flat_buf = flat_buf->next;

	    k = num = buf_count = 0;
	    indx = flat_buf->indices[0];
	    j = st_index;
	    off = offset;
	    bwr_size = flat_buf->blocklens[0];

	    while (num < bufsize) {
		size = ADIOI_MIN(fwr_size, bwr_size);
		if (size) {
#ifdef PROFILE
		    MPE_Log_event(11, 0, "start seek");
#endif
		    pvfs_lseek64(fd->fd_sys, off, SEEK_SET);
#ifdef PROFILE
		    MPE_Log_event(12, 0, "end seek");
		    MPE_Log_event(5, 0, "start write");
#endif
		    err = pvfs_write(fd->fd_sys, ((char *) buf) + indx, size);
#ifdef PROFILE
		    MPE_Log_event(6, 0, "end write");
#endif
		    if (err == -1) err_flag = 1;
		}

		new_fwr_size = fwr_size;
		new_bwr_size = bwr_size;

		if (size == fwr_size) {
/* reached end of contiguous block in file */
                    if (j < (flat_file->count - 1)) j++;
                    else {
                        j = 0;
                        n_filetypes++;
                    }

                    off = disp + flat_file->indices[j] + 
                                   (ADIO_Offset) n_filetypes*filetype_extent;

		    new_fwr_size = flat_file->blocklens[j];
		    if (size != bwr_size) {
			indx += size;
			new_bwr_size -= size;
		    }
		}

		if (size == bwr_size) {
/* reached end of contiguous block in memory */

		    k = (k + 1)%flat_buf->count;
		    buf_count++;
		    indx = buftype_extent*(buf_count/flat_buf->count) +
			flat_buf->indices[k]; 
		    new_bwr_size = flat_buf->blocklens[k];
		    if (size != fwr_size) {
			off += size;
			new_fwr_size -= size;
		    }
		}
		num += size;
		fwr_size = new_fwr_size;
                bwr_size = new_bwr_size;
	    }
	}

        if (file_ptr_type == ADIO_INDIVIDUAL) fd->fp_ind = off;
	if (err_flag) {
	    *error_code = MPIO_Err_create_code(MPI_SUCCESS,
					       MPIR_ERR_RECOVERABLE, myname,
					       __LINE__, MPI_ERR_IO, "**io",
					       "**io %s", strerror(errno));
	}
	else *error_code = MPI_SUCCESS;
    }

    fd->fp_sys_posn = -1;   /* set it to null. */

#ifdef HAVE_STATUS_SET_BYTES
    MPIR_Status_set_bytes(status, datatype, bufsize);
/* This is a temporary way of filling in status. The right way is to 
   keep track of how much data was actually written by ADIOI_BUFFERED_WRITE. */
#endif

    if (!buftype_is_contig) ADIOI_Delete_flattened(datatype);
}

#ifdef HAVE_PVFS_LISTIO
void ADIOI_PVFS_WriteStridedListIO(ADIO_File fd, void *buf, int count,
                       MPI_Datatype datatype, int file_ptr_type,
                       ADIO_Offset offset, ADIO_Status *status, int
                       *error_code) 
{
/* Since PVFS does not support file locking, can't do buffered writes
   as on Unix */

/* offset is in units of etype relative to the filetype. */

    ADIOI_Flatlist_node *flat_buf, *flat_file;
    int i, j, k, err=-1, bwr_size, fwr_size=0, st_index=0;
    int bufsize, size, sum, n_etypes_in_filetype, size_in_filetype;
    int n_filetypes, etype_in_filetype;
    ADIO_Offset abs_off_in_filetype=0;
    int filetype_size, etype_size, buftype_size;
    MPI_Aint filetype_extent, buftype_extent;
    int buf_count, buftype_is_contig, filetype_is_contig;
    ADIO_Offset userbuf_off;
    ADIO_Offset off, disp, start_off;
    int flag, st_fwr_size, st_n_filetypes;
    int new_bwr_size, new_fwr_size, err_flag=0;

    int mem_list_count, file_list_count;
    char ** mem_offsets;
    int64_t *file_offsets;
    int *mem_lengths;
    int32_t *file_lengths;
    int total_blks_to_write;

    int max_mem_list, max_file_list;

    int b_blks_wrote;
    int f_data_wrote;
    int size_wrote=0, n_write_lists, extra_blks;

    int end_bwr_size, end_fwr_size;
    int start_k, start_j, new_file_write, new_buffer_write;
    int start_mem_offset;
#define MAX_ARRAY_SIZE 1024
    static char myname[] = "ADIOI_PVFS_WRITESTRIDED";

/* PFS file pointer modes are not relevant here, because PFS does
   not support strided accesses. */

    /* --BEGIN ERROR HANDLING-- */
    if (fd->atomicity) {
	*error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					   myname, __LINE__,
					   MPI_ERR_INTERN,
					   "Atomic mode set in PVFS I/O function", 0);
	return;
    }
    /* --END ERROR HANDLING-- */

    ADIOI_Datatype_iscontig(datatype, &buftype_is_contig);
    ADIOI_Datatype_iscontig(fd->filetype, &filetype_is_contig);

    MPI_Type_size(fd->filetype, &filetype_size);
    if ( ! filetype_size ) {
	*error_code = MPI_SUCCESS; 
	return;
    }

    MPI_Type_extent(fd->filetype, &filetype_extent);
    MPI_Type_size(datatype, &buftype_size);
    MPI_Type_extent(datatype, &buftype_extent);
    etype_size = fd->etype_size;
    
    bufsize = buftype_size * count;

    if (!buftype_is_contig && filetype_is_contig) {

/* noncontiguous in memory, contiguous in file.  */
        int64_t file_offsets;
	int32_t file_lengths;

	ADIOI_Flatten_datatype(datatype);
	flat_buf = ADIOI_Flatlist;
	while (flat_buf->type != datatype) flat_buf = flat_buf->next;
	
	if (file_ptr_type == ADIO_EXPLICIT_OFFSET) {
	    off = fd->disp + etype_size * offset;
	    pvfs_lseek64(fd->fd_sys, fd->fp_ind, SEEK_SET);
	}
	else off = pvfs_lseek64(fd->fd_sys, fd->fp_ind, SEEK_SET);

	file_list_count = 1;
	file_offsets = off;
	file_lengths = 0;
	total_blks_to_write = count*flat_buf->count;
	b_blks_wrote = 0;

	/* allocate arrays according to max usage */
	if (total_blks_to_write > MAX_ARRAY_SIZE)
	    mem_list_count = MAX_ARRAY_SIZE;
	else mem_list_count = total_blks_to_write;
	mem_offsets = (char**)ADIOI_Malloc(mem_list_count*sizeof(char*));
	mem_lengths = (int*)ADIOI_Malloc(mem_list_count*sizeof(int));

	j = 0;
	/* step through each block in memory, filling memory arrays */
	while (b_blks_wrote < total_blks_to_write) {
	    for (i=0; i<flat_buf->count; i++) {
		mem_offsets[b_blks_wrote % MAX_ARRAY_SIZE] = 
		    ((char*)buf + j*buftype_extent + flat_buf->indices[i]);
		mem_lengths[b_blks_wrote % MAX_ARRAY_SIZE] = 
		    flat_buf->blocklens[i];
		file_lengths += flat_buf->blocklens[i];
		b_blks_wrote++;
		if (!(b_blks_wrote % MAX_ARRAY_SIZE) ||
		    (b_blks_wrote == total_blks_to_write)) {

		    /* in the case of the last read list call,
		       adjust mem_list_count */
		    if (b_blks_wrote == total_blks_to_write) {
		        mem_list_count = total_blks_to_write % MAX_ARRAY_SIZE;
			/* in case last read list call fills max arrays */
			if (!mem_list_count) mem_list_count = MAX_ARRAY_SIZE;
		    }

		    pvfs_write_list(fd->fd_sys ,mem_list_count, mem_offsets,
				   mem_lengths, file_list_count,
				   &file_offsets, &file_lengths);
		  
		    /* in the case of the last read list call, leave here */
		    if (b_blks_wrote == total_blks_to_write) break;

		    file_offsets += file_lengths;
		    file_lengths = 0;
		} 
	    } /* for (i=0; i<flat_buf->count; i++) */
	    j++;
	} /* while (b_blks_wrote < total_blks_to_write) */
	ADIOI_Free(mem_offsets);
	ADIOI_Free(mem_lengths);

	if (file_ptr_type == ADIO_INDIVIDUAL) fd->fp_ind = off;

	if (err_flag) {
	    *error_code = MPIO_Err_create_code(MPI_SUCCESS,
					       MPIR_ERR_RECOVERABLE, myname,
					       __LINE__, MPI_ERR_IO, "**io",
					       "**io %s", strerror(errno));
	}
	else *error_code = MPI_SUCCESS;

	fd->fp_sys_posn = -1;   /* clear this. */

#ifdef HAVE_STATUS_SET_BYTES
	MPIR_Status_set_bytes(status, datatype, bufsize);
/* This is a temporary way of filling in status. The right way is to 
   keep track of how much data was actually written by ADIOI_BUFFERED_WRITE. */
#endif

	ADIOI_Delete_flattened(datatype);
	return;
    } /* if (!buftype_is_contig && filetype_is_contig) */

    /* already know that file is noncontiguous from above */
    /* noncontiguous in file */

/* filetype already flattened in ADIO_Open */
    flat_file = ADIOI_Flatlist;
    while (flat_file->type != fd->filetype) flat_file = flat_file->next;

    disp = fd->disp;

    /* for each case - ADIO_Individual pointer or explicit, find offset
       (file offset in bytes), n_filetypes (how many filetypes into file 
       to start), fwr_size (remaining amount of data in present file
       block), and st_index (start point in terms of blocks in starting
       filetype) */
    if (file_ptr_type == ADIO_INDIVIDUAL) {
        offset = fd->fp_ind; /* in bytes */
	n_filetypes = -1;
	flag = 0;
	while (!flag) {
	    n_filetypes++;
	    for (i=0; i<flat_file->count; i++) {
	        if (disp + flat_file->indices[i] + 
		    (ADIO_Offset) n_filetypes*filetype_extent +
		      flat_file->blocklens[i] >= offset) {
		  st_index = i;
		  fwr_size = disp + flat_file->indices[i] + 
		    (ADIO_Offset) n_filetypes*filetype_extent
		    + flat_file->blocklens[i] - offset;
		  flag = 1;
		  break;
		}
	    }
	} /* while (!flag) */
    } /* if (file_ptr_type == ADIO_INDIVIDUAL) */
    else {
        n_etypes_in_filetype = filetype_size/etype_size;
	n_filetypes = (int) (offset / n_etypes_in_filetype);
	etype_in_filetype = (int) (offset % n_etypes_in_filetype);
	size_in_filetype = etype_in_filetype * etype_size;
	
	sum = 0;
	for (i=0; i<flat_file->count; i++) {
	    sum += flat_file->blocklens[i];
	    if (sum > size_in_filetype) {
	        st_index = i;
		fwr_size = sum - size_in_filetype;
		abs_off_in_filetype = flat_file->indices[i] +
		    size_in_filetype - (sum - flat_file->blocklens[i]);
		break;
	    }
	}

	/* abs. offset in bytes in the file */
	offset = disp + (ADIO_Offset) n_filetypes*filetype_extent +
	    abs_off_in_filetype;
    } /* else [file_ptr_type != ADIO_INDIVIDUAL] */

    start_off = offset;
    st_fwr_size = fwr_size;
    st_n_filetypes = n_filetypes;
    
    if (buftype_is_contig && !filetype_is_contig) {

/* contiguous in memory, noncontiguous in file. should be the most
   common case. */

        int mem_lengths;
	char *mem_offsets;
        
	i = 0;
	j = st_index;
	off = offset;
	n_filetypes = st_n_filetypes;
        
	mem_list_count = 1;
        
	/* determine how many blocks in file to read */
	f_data_wrote = ADIOI_MIN(st_fwr_size, bufsize);
	total_blks_to_write = 1;
	j++;
	while (f_data_wrote < bufsize) {
	    f_data_wrote += flat_file->blocklens[j];
	    total_blks_to_write++;
	    if (j<(flat_file->count-1)) j++;
	    else j = 0; 
	}
	    
	j = st_index;
	n_filetypes = st_n_filetypes;
	n_write_lists = total_blks_to_write/MAX_ARRAY_SIZE;
	extra_blks = total_blks_to_write%MAX_ARRAY_SIZE;
        
	mem_offsets = buf;
	mem_lengths = 0;
        
	/* if at least one full readlist, allocate file arrays
	   at max array size and don't free until very end */
	if (n_write_lists) {
	    file_offsets = (int64_t*)ADIOI_Malloc(MAX_ARRAY_SIZE*
						  sizeof(int64_t));
	    file_lengths = (int32_t*)ADIOI_Malloc(MAX_ARRAY_SIZE*
						  sizeof(int32_t));
	}
	/* if there's no full readlist allocate file arrays according
	   to needed size (extra_blks) */
	else {
	    file_offsets = (int64_t*)ADIOI_Malloc(extra_blks*
                                                  sizeof(int64_t));
            file_lengths = (int32_t*)ADIOI_Malloc(extra_blks*
                                                  sizeof(int32_t));
        }
        
        /* for file arrays that are of MAX_ARRAY_SIZE, build arrays */
        for (i=0; i<n_write_lists; i++) {
            file_list_count = MAX_ARRAY_SIZE;
            if(!i) {
                file_offsets[0] = offset;
                file_lengths[0] = st_fwr_size;
                mem_lengths = st_fwr_size;
            }
            for (k=0; k<MAX_ARRAY_SIZE; k++) {
                if (i || k) {
                    file_offsets[k] = disp + n_filetypes*filetype_extent
                      + flat_file->indices[j];
                    file_lengths[k] = flat_file->blocklens[j];
                    mem_lengths += file_lengths[k];
                }
                if (j<(flat_file->count - 1)) j++;
                else {
                    j = 0;
                    n_filetypes++;
                }
            } /* for (k=0; k<MAX_ARRAY_SIZE; k++) */
            pvfs_write_list(fd->fd_sys, mem_list_count,
                           &mem_offsets, &mem_lengths,
                           file_list_count, file_offsets,
                           file_lengths);
            mem_offsets += mem_lengths;
            mem_lengths = 0;
        } /* for (i=0; i<n_write_lists; i++) */

        /* for file arrays smaller than MAX_ARRAY_SIZE (last read_list call) */
        if (extra_blks) {
            file_list_count = extra_blks;
            if(!i) {
                file_offsets[0] = offset;
                file_lengths[0] = st_fwr_size;
            }
            for (k=0; k<extra_blks; k++) {
                if(i || k) {
                    file_offsets[k] = disp + n_filetypes*filetype_extent +
                      flat_file->indices[j];
                    if (k == (extra_blks - 1)) {
                        file_lengths[k] = bufsize - (int32_t) mem_lengths
                          - (int32_t) mem_offsets + (int32_t)  buf;
                    }
                    else file_lengths[k] = flat_file->blocklens[j];
                } /* if(i || k) */
                mem_lengths += file_lengths[k];
                if (j<(flat_file->count - 1)) j++;
                else {
                    j = 0;
                    n_filetypes++;
                }
            } /* for (k=0; k<extra_blks; k++) */
            pvfs_write_list(fd->fd_sys, mem_list_count, &mem_offsets,
                           &mem_lengths, file_list_count, file_offsets,
                           file_lengths);
        }
    } 
    else {
        /* noncontiguous in memory as well as in file */

        ADIOI_Flatten_datatype(datatype);
	flat_buf = ADIOI_Flatlist;
	while (flat_buf->type != datatype) flat_buf = flat_buf->next;

	size_wrote = 0;
	n_filetypes = st_n_filetypes;
	fwr_size = st_fwr_size;
	bwr_size = flat_buf->blocklens[0];
	buf_count = 0;
	start_mem_offset = 0;
	start_k = k = 0;
	start_j = st_index;
	max_mem_list = 0;
	max_file_list = 0;

	/* run through and file max_file_list and max_mem_list so that you 
	   can allocate the file and memory arrays less than MAX_ARRAY_SIZE
	   if possible */

	while (size_wrote < bufsize) {
	    k = start_k;
	    new_buffer_write = 0;
	    mem_list_count = 0;
	    while ((mem_list_count < MAX_ARRAY_SIZE) && 
		   (new_buffer_write < bufsize-size_wrote)) {
	        /* find mem_list_count and file_list_count such that both are
		   less than MAX_ARRAY_SIZE, the sum of their lengths are
		   equal, and the sum of all the data read and data to be
		   read in the next immediate read list is less than
		   bufsize */
	        if(mem_list_count) {
		    if((new_buffer_write + flat_buf->blocklens[k] + 
			size_wrote) > bufsize) {
		        end_bwr_size = new_buffer_write + 
			    flat_buf->blocklens[k] - (bufsize - size_wrote);
			new_buffer_write = bufsize - size_wrote;
		    }
		    else {
		        new_buffer_write += flat_buf->blocklens[k];
			end_bwr_size = flat_buf->blocklens[k];
		    }
		}
		else {
		    if (bwr_size > (bufsize - size_wrote)) {
		        new_buffer_write = bufsize - size_wrote;
			bwr_size = new_buffer_write;
		    }
		    else new_buffer_write = bwr_size;
		}
		mem_list_count++;
		k = (k + 1)%flat_buf->count;
	     } /* while ((mem_list_count < MAX_ARRAY_SIZE) && 
	       (new_buffer_write < bufsize-size_wrote)) */
	    j = start_j;
	    new_file_write = 0;
	    file_list_count = 0;
	    while ((file_list_count < MAX_ARRAY_SIZE) && 
		   (new_file_write < new_buffer_write)) {
	        if(file_list_count) {
		    if((new_file_write + flat_file->blocklens[j]) > 
		       new_buffer_write) {
		        end_fwr_size = new_buffer_write - new_file_write;
			new_file_write = new_buffer_write;
			j--;
		    }
		    else {
		        new_file_write += flat_file->blocklens[j];
			end_fwr_size = flat_file->blocklens[j];
		    }
		}
		else {
		    if (fwr_size > new_buffer_write) {
		        new_file_write = new_buffer_write;
			fwr_size = new_file_write;
		    }
		    else new_file_write = fwr_size;
		}
		file_list_count++;
		if (j < (flat_file->count - 1)) j++;
		else j = 0;
		
		k = start_k;
		if ((new_file_write < new_buffer_write) && 
		    (file_list_count == MAX_ARRAY_SIZE)) {
		    new_buffer_write = 0;
		    mem_list_count = 0;
		    while (new_buffer_write < new_file_write) {
		        if(mem_list_count) {
			    if((new_buffer_write + flat_buf->blocklens[k]) >
			       new_file_write) {
			        end_bwr_size = new_file_write - 
				    new_buffer_write;
				new_buffer_write = new_file_write;
				k--;
			    }
			    else {
			        new_buffer_write += flat_buf->blocklens[k];
				end_bwr_size = flat_buf->blocklens[k];
			    }
			}
			else {
			    new_buffer_write = bwr_size;
			    if (bwr_size > (bufsize - size_wrote)) {
			        new_buffer_write = bufsize - size_wrote;
				bwr_size = new_buffer_write;
			    }
			}
			mem_list_count++;
			k = (k + 1)%flat_buf->count;
		    } /* while (new_buffer_write < new_file_write) */
		} /* if ((new_file_write < new_buffer_write) &&
		     (file_list_count == MAX_ARRAY_SIZE)) */
	    } /* while ((mem_list_count < MAX_ARRAY_SIZE) && 
		 (new_buffer_write < bufsize-size_wrote)) */

	    /*  fakes filling the writelist arrays of lengths found above  */
	    k = start_k;
	    j = start_j;
	    for (i=0; i<mem_list_count; i++) {	     
		if(i) {
		    if (i == (mem_list_count - 1)) {
			if (flat_buf->blocklens[k] == end_bwr_size)
			    bwr_size = flat_buf->blocklens[(k+1)%
							  flat_buf->count];
			else {
			    bwr_size = flat_buf->blocklens[k] - end_bwr_size;
			    k--;
			    buf_count--;
			}
		    }
		}
		buf_count++;
		k = (k + 1)%flat_buf->count;
	    } /* for (i=0; i<mem_list_count; i++) */
	    for (i=0; i<file_list_count; i++) {
		if (i) {
		    if (i == (file_list_count - 1)) {
			if (flat_file->blocklens[j] == end_fwr_size)
			    fwr_size = flat_file->blocklens[(j+1)%
							  flat_file->count];   
			else {
			    fwr_size = flat_file->blocklens[j] - end_fwr_size;
			    j--;
			}
		    }
		}
		if (j < flat_file->count - 1) j++;
		else {
		    j = 0;
		    n_filetypes++;
		}
	    } /* for (i=0; i<file_list_count; i++) */
	    size_wrote += new_buffer_write;
	    start_k = k;
	    start_j = j;
	    if (max_mem_list < mem_list_count)
	        max_mem_list = mem_list_count;
	    if (max_file_list < file_list_count)
	        max_file_list = file_list_count;
	    if (max_mem_list == max_mem_list == MAX_ARRAY_SIZE)
	        break;
	} /* while (size_wrote < bufsize) */

	mem_offsets = (char **)ADIOI_Malloc(max_mem_list*sizeof(char *));
	mem_lengths = (int *)ADIOI_Malloc(max_mem_list*sizeof(int));
	file_offsets = (int64_t *)ADIOI_Malloc(max_file_list*sizeof(int64_t));
	file_lengths = (int32_t *)ADIOI_Malloc(max_file_list*sizeof(int32_t));
	    
	size_wrote = 0;
	n_filetypes = st_n_filetypes;
	fwr_size = st_fwr_size;
	bwr_size = flat_buf->blocklens[0];
	buf_count = 0;
	start_mem_offset = 0;
	start_k = k = 0;
	start_j = st_index;

	/*  this section calculates mem_list_count and file_list_count
	    and also finds the possibly odd sized last array elements
	    in new_fwr_size and new_bwr_size  */
	
	while (size_wrote < bufsize) {
	    k = start_k;
	    new_buffer_write = 0;
	    mem_list_count = 0;
	    while ((mem_list_count < MAX_ARRAY_SIZE) && 
		   (new_buffer_write < bufsize-size_wrote)) {
	        /* find mem_list_count and file_list_count such that both are
		   less than MAX_ARRAY_SIZE, the sum of their lengths are
		   equal, and the sum of all the data read and data to be
		   read in the next immediate read list is less than
		   bufsize */
	        if(mem_list_count) {
		    if((new_buffer_write + flat_buf->blocklens[k] + 
			size_wrote) > bufsize) {
		        end_bwr_size = new_buffer_write + 
			    flat_buf->blocklens[k] - (bufsize - size_wrote);
			new_buffer_write = bufsize - size_wrote;
		    }
		    else {
		        new_buffer_write += flat_buf->blocklens[k];
			end_bwr_size = flat_buf->blocklens[k];
		    }
		}
		else {
		    if (bwr_size > (bufsize - size_wrote)) {
		        new_buffer_write = bufsize - size_wrote;
			bwr_size = new_buffer_write;
		    }
		    else new_buffer_write = bwr_size;
		}
		mem_list_count++;
		k = (k + 1)%flat_buf->count;
	     } /* while ((mem_list_count < MAX_ARRAY_SIZE) && 
	       (new_buffer_write < bufsize-size_wrote)) */
	    j = start_j;
	    new_file_write = 0;
	    file_list_count = 0;
	    while ((file_list_count < MAX_ARRAY_SIZE) && 
		   (new_file_write < new_buffer_write)) {
	        if(file_list_count) {
		    if((new_file_write + flat_file->blocklens[j]) > 
		       new_buffer_write) {
		        end_fwr_size = new_buffer_write - new_file_write;
			new_file_write = new_buffer_write;
			j--;
		    }
		    else {
		        new_file_write += flat_file->blocklens[j];
			end_fwr_size = flat_file->blocklens[j];
		    }
		}
		else {
		    if (fwr_size > new_buffer_write) {
		        new_file_write = new_buffer_write;
			fwr_size = new_file_write;
		    }
		    else new_file_write = fwr_size;
		}
		file_list_count++;
		if (j < (flat_file->count - 1)) j++;
		else j = 0;
		
		k = start_k;
		if ((new_file_write < new_buffer_write) && 
		    (file_list_count == MAX_ARRAY_SIZE)) {
		    new_buffer_write = 0;
		    mem_list_count = 0;
		    while (new_buffer_write < new_file_write) {
		        if(mem_list_count) {
			    if((new_buffer_write + flat_buf->blocklens[k]) >
			       new_file_write) {
			        end_bwr_size = new_file_write -
				  new_buffer_write;
				new_buffer_write = new_file_write;
				k--;
			    }
			    else {
			        new_buffer_write += flat_buf->blocklens[k];
				end_bwr_size = flat_buf->blocklens[k];
			    }
			}
			else {
			    new_buffer_write = bwr_size;
			    if (bwr_size > (bufsize - size_wrote)) {
			        new_buffer_write = bufsize - size_wrote;
				bwr_size = new_buffer_write;
			    }
			}
			mem_list_count++;
			k = (k + 1)%flat_buf->count;
		    } /* while (new_buffer_write < new_file_write) */
		} /* if ((new_file_write < new_buffer_write) &&
		     (file_list_count == MAX_ARRAY_SIZE)) */
	    } /* while ((mem_list_count < MAX_ARRAY_SIZE) && 
		 (new_buffer_write < bufsize-size_wrote)) */

	    /*  fills the allocated readlist arrays  */
	    k = start_k;
	    j = start_j;
	    for (i=0; i<mem_list_count; i++) {	     
	        mem_offsets[i] = ((char*)buf + buftype_extent*
					 (buf_count/flat_buf->count) +
					 (int)flat_buf->indices[k]);
		
		if(!i) {
		    mem_lengths[0] = bwr_size;
		    mem_offsets[0] += flat_buf->blocklens[k] - bwr_size;
		}
		else {
		    if (i == (mem_list_count - 1)) {
		        mem_lengths[i] = end_bwr_size;
			if (flat_buf->blocklens[k] == end_bwr_size)
			    bwr_size = flat_buf->blocklens[(k+1)%
							  flat_buf->count];
			else {
			    bwr_size = flat_buf->blocklens[k] - end_bwr_size;
			    k--;
			    buf_count--;
			}
		    }
		    else {
		        mem_lengths[i] = flat_buf->blocklens[k];
		    }
		}
		buf_count++;
		k = (k + 1)%flat_buf->count;
	    } /* for (i=0; i<mem_list_count; i++) */
	    for (i=0; i<file_list_count; i++) {
	        file_offsets[i] = disp + flat_file->indices[j] + n_filetypes *
		    filetype_extent;
	        if (!i) {
		    file_lengths[0] = fwr_size;
		    file_offsets[0] += flat_file->blocklens[j] - fwr_size;
		}
		else {
		    if (i == (file_list_count - 1)) {
		        file_lengths[i] = end_fwr_size;
			if (flat_file->blocklens[j] == end_fwr_size)
			    fwr_size = flat_file->blocklens[(j+1)%
							  flat_file->count];   
			else {
			    fwr_size = flat_file->blocklens[j] - end_fwr_size;
			    j--;
			}
		    }
		    else file_lengths[i] = flat_file->blocklens[j];
		}
		if (j < flat_file->count - 1) j++;
		else {
		    j = 0;
		    n_filetypes++;
		}
	    } /* for (i=0; i<file_list_count; i++) */
	    pvfs_write_list(fd->fd_sys,mem_list_count, mem_offsets,
			   mem_lengths, file_list_count, file_offsets,
			   file_lengths);
	    size_wrote += new_buffer_write;
	    start_k = k;
	    start_j = j;
	} /* while (size_wrote < bufsize) */
	ADIOI_Free(mem_offsets);
	ADIOI_Free(mem_lengths);
    }
    ADIOI_Free(file_offsets);
    ADIOI_Free(file_lengths);

    if (file_ptr_type == ADIO_INDIVIDUAL) fd->fp_ind = off;
    if (err_flag) {
	*error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					   myname, __LINE__, MPI_ERR_IO,
					   "**io",
					   "**io %s", strerror(errno));
    }
    else *error_code = MPI_SUCCESS;

    fd->fp_sys_posn = -1;   /* set it to null. */

#ifdef HAVE_STATUS_SET_BYTES
    MPIR_Status_set_bytes(status, datatype, bufsize);
/* This is a temporary way of filling in status. The right way is to 
   keep track of how much data was actually written by ADIOI_BUFFERED_WRITE. */
#endif

    if (!buftype_is_contig) ADIOI_Delete_flattened(datatype);
}
#endif /* HAVE_PVFS_LISTIO */
