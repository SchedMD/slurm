/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "adio.h"
#include "adio_extern.h"
#include "ad_pvfs.h"

#ifdef HAVE_PVFS_LISTIO
void ADIOI_PVFS_ReadStridedListIO(ADIO_File fd, void *buf, int count,
                       MPI_Datatype datatype, int file_ptr_type,
                       ADIO_Offset offset, ADIO_Status *status, int
                       *error_code);
#endif

void ADIOI_PVFS_ReadContig(ADIO_File fd, void *buf, int count, 
                     MPI_Datatype datatype, int file_ptr_type,
		     ADIO_Offset offset, ADIO_Status *status, int *error_code)
{
    int err=-1, datatype_size, len;
    static char myname[] = "ADIOI_PVFS_READCONTIG";

    MPI_Type_size(datatype, &datatype_size);
    len = datatype_size * count;

    if (file_ptr_type == ADIO_EXPLICIT_OFFSET) {
	if (fd->fp_sys_posn != offset)
	    pvfs_lseek64(fd->fd_sys, offset, SEEK_SET);
	err = pvfs_read(fd->fd_sys, buf, len);
	fd->fp_sys_posn = offset + err;
	/* individual file pointer not updated */        
    }
    else {  /* read from curr. location of ind. file pointer */
	if (fd->fp_sys_posn != fd->fp_ind)
	    pvfs_lseek64(fd->fd_sys, fd->fp_ind, SEEK_SET);
	err = pvfs_read(fd->fd_sys, buf, len);
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


void ADIOI_PVFS_ReadStrided(ADIO_File fd, void *buf, int count,
			    MPI_Datatype datatype, int file_ptr_type,
			    ADIO_Offset offset, ADIO_Status *status, int
			    *error_code)
{
#ifdef HAVE_PVFS_LISTIO
    if ( fd->hints->fs_hints.pvfs.listio_read == ADIOI_HINT_ENABLE) {
	    ADIOI_PVFS_ReadStridedListIO(fd, buf, count, datatype, file_ptr_type,
			    offset, status, error_code);
	    return;
    }
#endif
/* If hint set to DISABLE or AUTOMATIC, don't use listio */
    ADIOI_GEN_ReadStrided(fd, buf, count, datatype, file_ptr_type,
		    offset, status, error_code);
}

#ifdef HAVE_PVFS_LISTIO
void ADIOI_PVFS_ReadStridedListIO(ADIO_File fd, void *buf, int count,
                       MPI_Datatype datatype, int file_ptr_type,
                       ADIO_Offset offset, ADIO_Status *status, int
                       *error_code)
{
/* offset is in units of etype relative to the filetype. */

    ADIOI_Flatlist_node *flat_buf, *flat_file;
    int i, j, k, l, brd_size, frd_size=0, st_index=0;
    int bufsize, sum, n_etypes_in_filetype, size_in_filetype;
    int n_filetypes, etype_in_filetype;
    ADIO_Offset abs_off_in_filetype=0;
    int filetype_size, etype_size, buftype_size;
    MPI_Aint filetype_extent, buftype_extent; 
    int buf_count, buftype_is_contig, filetype_is_contig;
    ADIO_Offset userbuf_off;
    ADIO_Offset off, disp, start_off;
    int flag, st_frd_size, st_n_filetypes;
    int new_brd_size, new_frd_size;

    int mem_list_count, file_list_count;
    char **mem_offsets;
    int64_t *file_offsets;
    int *mem_lengths;
    int32_t *file_lengths;
    int total_blks_to_read;

    int max_mem_list, max_file_list;

    int b_blks_read;
    int f_data_read;
    int size_read=0, n_read_lists, extra_blks;

    int end_brd_size, end_frd_size;
    int start_k, start_j, new_file_read, new_buffer_read;
    int start_mem_offset;

#define MAX_ARRAY_SIZE 1024

#ifndef PRINT_ERR_MESG
  static char myname[] = "ADIOI_PVFS_ReadStrided";
#endif

    *error_code = MPI_SUCCESS;  /* changed below if error */

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

/* noncontiguous in memory, contiguous in file. */
        int64_t file_offsets;
	int32_t file_lengths;

	ADIOI_Flatten_datatype(datatype);
	flat_buf = ADIOI_Flatlist;
	while (flat_buf->type != datatype) flat_buf = flat_buf->next;

	off = (file_ptr_type == ADIO_INDIVIDUAL) ? fd->fp_ind : 
	    fd->disp + etype_size * offset;

	file_list_count = 1;
	file_offsets = off;
	file_lengths = 0;
	total_blks_to_read = count*flat_buf->count;
	b_blks_read = 0;

	/* allocate arrays according to max usage */
	if (total_blks_to_read > MAX_ARRAY_SIZE)
	    mem_list_count = MAX_ARRAY_SIZE;
	else mem_list_count = total_blks_to_read;
	mem_offsets = (char**)ADIOI_Malloc(mem_list_count*sizeof(char*));
	mem_lengths = (int*)ADIOI_Malloc(mem_list_count*sizeof(int));

	j = 0;
	/* step through each block in memory, filling memory arrays */
	while (b_blks_read < total_blks_to_read) {
	    for (i=0; i<flat_buf->count; i++) {
		mem_offsets[b_blks_read % MAX_ARRAY_SIZE] = 
		    (char*)((char *)buf + j*buftype_extent + flat_buf->indices[i]);
		mem_lengths[b_blks_read % MAX_ARRAY_SIZE] = 
		    flat_buf->blocklens[i];
		file_lengths += flat_buf->blocklens[i];
		b_blks_read++;
		if (!(b_blks_read % MAX_ARRAY_SIZE) ||
		    (b_blks_read == total_blks_to_read)) {

		    /* in the case of the last read list call,
		       adjust mem_list_count */
		    if (b_blks_read == total_blks_to_read) {
		        mem_list_count = total_blks_to_read % MAX_ARRAY_SIZE;
			/* in case last read list call fills max arrays */
			if (!mem_list_count) mem_list_count = MAX_ARRAY_SIZE;
		    }

		    pvfs_read_list(fd->fd_sys ,mem_list_count, mem_offsets,
				   mem_lengths, file_list_count,
				   &file_offsets, &file_lengths);
		  
		    /* in the case of the last read list call, leave here */
		    if (b_blks_read == total_blks_to_read) break;

		    file_offsets += file_lengths;
		    file_lengths = 0;
		} 
	    } /* for (i=0; i<flat_buf->count; i++) */
	    j++;
	} /* while (b_blks_read < total_blks_to_read) */
	ADIOI_Free(mem_offsets);
	ADIOI_Free(mem_lengths);

        if (file_ptr_type == ADIO_INDIVIDUAL) fd->fp_ind = off;

	fd->fp_sys_posn = -1;  /* set it to null. */

#ifdef HAVE_STATUS_SET_BYTES
	MPIR_Status_set_bytes(status, datatype, bufsize);
	/* This isa temporary way of filling in status.  The right way is to
	   keep tracke of how much data was actually read adn placed in buf
	   by ADIOI_BUFFERED_READ. */
#endif
	ADIOI_Delete_flattened(datatype);

	return;
    } /* if (!buftype_is_contig && filetype_is_contig) */

    /* know file is noncontiguous from above */
    /* noncontiguous in file */

    /* filetype already flattened in ADIO_Open */
    flat_file = ADIOI_Flatlist;
    while (flat_file->type != fd->filetype) flat_file = flat_file->next;

    disp = fd->disp;

    /* for each case - ADIO_Individual pointer or explicit, find the file
       offset in bytes (offset), n_filetypes (how many filetypes into
       file to start), frd_size (remaining amount of data in present
       file block), and st_index (start point in terms of blocks in
       starting filetype) */
    if (file_ptr_type == ADIO_INDIVIDUAL) {
        offset = fd->fp_ind; /* in bytes */
	n_filetypes = -1;
	flag = 0;
	while (!flag) {
	    n_filetypes++;
	    for (i=0; i<flat_file->count; i++) {
	        if (disp + flat_file->indices[i] + 
		    (ADIO_Offset) n_filetypes*filetype_extent +
		    flat_file->blocklens[i]  >= offset) {
		    st_index = i;
		    frd_size = (int) (disp + flat_file->indices[i] + 
				      (ADIO_Offset) n_filetypes*filetype_extent
				      + flat_file->blocklens[i] - offset);
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
		frd_size = sum - size_in_filetype;
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
    st_frd_size = frd_size;
    st_n_filetypes = n_filetypes;
    
    if (buftype_is_contig && !filetype_is_contig) {

/* contiguous in memory, noncontiguous in file. should be the most
   common case. */

        int mem_lengths;
	char *mem_offsets;
	
	i = 0;
	j = st_index;
	n_filetypes = st_n_filetypes;
	
	mem_list_count = 1;
	
	/* determine how many blocks in file to read */
	f_data_read = ADIOI_MIN(st_frd_size, bufsize);
	total_blks_to_read = 1;
	j++;
	while (f_data_read < bufsize) {
	    f_data_read += flat_file->blocklens[j];
	    total_blks_to_read++;
	    if (j<(flat_file->count-1)) j++;
	    else j = 0;	
	}
      
	j = st_index;
	n_filetypes = st_n_filetypes;
	n_read_lists = total_blks_to_read/MAX_ARRAY_SIZE;
	extra_blks = total_blks_to_read%MAX_ARRAY_SIZE;
	
	mem_offsets = buf;
	mem_lengths = 0;
	
	/* if at least one full readlist, allocate file arrays
	   at max array size and don't free until very end */
	if (n_read_lists) {
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
	for (i=0; i<n_read_lists; i++) {
	    file_list_count = MAX_ARRAY_SIZE;
	    if(!i) {
	        file_offsets[0] = offset;
		file_lengths[0] = st_frd_size;
		mem_lengths = st_frd_size;
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
	    pvfs_read_list(fd->fd_sys, mem_list_count,
			   &mem_offsets, &mem_lengths,
			   file_list_count, file_offsets,
			   file_lengths);
	    mem_offsets += mem_lengths;
	    mem_lengths = 0;
	} /* for (i=0; i<n_read_lists; i++) */

	/* for file arrays smaller than MAX_ARRAY_SIZE (last read_list call) */
	if (extra_blks) {
	    file_list_count = extra_blks;
	    if(!i) {
	        file_offsets[0] = offset;
		file_lengths[0] = st_frd_size;
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
	    pvfs_read_list(fd->fd_sys, mem_list_count, &mem_offsets,
			   &mem_lengths, file_list_count, file_offsets,
			   file_lengths);
	}
    }
    else {
/* noncontiguous in memory as well as in file */
      
        ADIOI_Flatten_datatype(datatype);
	flat_buf = ADIOI_Flatlist;
	while (flat_buf->type != datatype) flat_buf = flat_buf->next;

	size_read = 0;
	n_filetypes = st_n_filetypes;
	frd_size = st_frd_size;
	brd_size = flat_buf->blocklens[0];
	buf_count = 0;
	start_mem_offset = 0;
	start_k = k = 0;
	start_j = st_index;
	max_mem_list = 0;
	max_file_list = 0;

	/* run through and file max_file_list and max_mem_list so that you 
	   can allocate the file and memory arrays less than MAX_ARRAY_SIZE
	   if possible */

	while (size_read < bufsize) {
	    k = start_k;
	    new_buffer_read = 0;
	    mem_list_count = 0;
	    while ((mem_list_count < MAX_ARRAY_SIZE) && 
		   (new_buffer_read < bufsize-size_read)) {
	        /* find mem_list_count and file_list_count such that both are
		   less than MAX_ARRAY_SIZE, the sum of their lengths are
		   equal, and the sum of all the data read and data to be
		   read in the next immediate read list is less than
		   bufsize */
	        if(mem_list_count) {
		    if((new_buffer_read + flat_buf->blocklens[k] + 
			size_read) > bufsize) {
		        end_brd_size = new_buffer_read + 
			    flat_buf->blocklens[k] - (bufsize - size_read);
			new_buffer_read = bufsize - size_read;
		    }
		    else {
		        new_buffer_read += flat_buf->blocklens[k];
			end_brd_size = flat_buf->blocklens[k];
		    }
		}
		else {
		    if (brd_size > (bufsize - size_read)) {
		        new_buffer_read = bufsize - size_read;
			brd_size = new_buffer_read;
		    }
		    else new_buffer_read = brd_size;
		}
		mem_list_count++;
		k = (k + 1)%flat_buf->count;
	     } /* while ((mem_list_count < MAX_ARRAY_SIZE) && 
	       (new_buffer_read < bufsize-size_read)) */
	    j = start_j;
	    new_file_read = 0;
	    file_list_count = 0;
	    while ((file_list_count < MAX_ARRAY_SIZE) && 
		   (new_file_read < new_buffer_read)) {
	        if(file_list_count) {
		    if((new_file_read + flat_file->blocklens[j]) > 
		       new_buffer_read) {
		        end_frd_size = new_buffer_read - new_file_read;
			new_file_read = new_buffer_read;
			j--;
		    }
		    else {
		        new_file_read += flat_file->blocklens[j];
			end_frd_size = flat_file->blocklens[j];
		    }
		}
		else {
		    if (frd_size > new_buffer_read) {
		        new_file_read = new_buffer_read;
			frd_size = new_file_read;
		    }
		    else new_file_read = frd_size;
		}
		file_list_count++;
		if (j < (flat_file->count - 1)) j++;
		else j = 0;
		
		k = start_k;
		if ((new_file_read < new_buffer_read) && 
		    (file_list_count == MAX_ARRAY_SIZE)) {
		    new_buffer_read = 0;
		    mem_list_count = 0;
		    while (new_buffer_read < new_file_read) {
		        if(mem_list_count) {
			    if((new_buffer_read + flat_buf->blocklens[k]) >
			       new_file_read) {
			        end_brd_size = new_file_read - new_buffer_read;
				new_buffer_read = new_file_read;
				k--;
			    }
			    else {
			        new_buffer_read += flat_buf->blocklens[k];
				end_brd_size = flat_buf->blocklens[k];
			    }
			}
			else {
			    new_buffer_read = brd_size;
			    if (brd_size > (bufsize - size_read)) {
			        new_buffer_read = bufsize - size_read;
				brd_size = new_buffer_read;
			    }
			}
			mem_list_count++;
			k = (k + 1)%flat_buf->count;
		    } /* while (new_buffer_read < new_file_read) */
		} /* if ((new_file_read < new_buffer_read) && (file_list_count
		     == MAX_ARRAY_SIZE)) */
	    } /* while ((mem_list_count < MAX_ARRAY_SIZE) && 
		 (new_buffer_read < bufsize-size_read)) */

	    /*  fakes filling the readlist arrays of lengths found above  */
	    k = start_k;
	    j = start_j;
	    for (i=0; i<mem_list_count; i++) {	     
		if(i) {
		    if (i == (mem_list_count - 1)) {
			if (flat_buf->blocklens[k] == end_brd_size)
			    brd_size = flat_buf->blocklens[(k+1)%
							  flat_buf->count];
			else {
			    brd_size = flat_buf->blocklens[k] - end_brd_size;
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
			if (flat_file->blocklens[j] == end_frd_size)
			    frd_size = flat_file->blocklens[(j+1)%
							  flat_file->count];   
			else {
			    frd_size = flat_file->blocklens[j] - end_frd_size;
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
	    size_read += new_buffer_read;
	    start_k = k;
	    start_j = j;
	    if (max_mem_list < mem_list_count)
	        max_mem_list = mem_list_count;
	    if (max_file_list < file_list_count)
	        max_file_list = file_list_count;
	    if (max_mem_list == max_mem_list == MAX_ARRAY_SIZE)
	        break;
	} /* while (size_read < bufsize) */

	mem_offsets = (char **)ADIOI_Malloc(max_mem_list*sizeof(char *));
	mem_lengths = (int *)ADIOI_Malloc(max_mem_list*sizeof(int));
	file_offsets = (int64_t *)ADIOI_Malloc(max_file_list*sizeof(int64_t));
	file_lengths = (int32_t *)ADIOI_Malloc(max_file_list*sizeof(int32_t));
	    
	size_read = 0;
	n_filetypes = st_n_filetypes;
	frd_size = st_frd_size;
	brd_size = flat_buf->blocklens[0];
	buf_count = 0;
	start_mem_offset = 0;
	start_k = k = 0;
	start_j = st_index;

	/*  this section calculates mem_list_count and file_list_count
	    and also finds the possibly odd sized last array elements
	    in new_frd_size and new_brd_size  */
	
	while (size_read < bufsize) {
	    k = start_k;
	    new_buffer_read = 0;
	    mem_list_count = 0;
	    while ((mem_list_count < MAX_ARRAY_SIZE) && 
		   (new_buffer_read < bufsize-size_read)) {
	        /* find mem_list_count and file_list_count such that both are
		   less than MAX_ARRAY_SIZE, the sum of their lengths are
		   equal, and the sum of all the data read and data to be
		   read in the next immediate read list is less than
		   bufsize */
	        if(mem_list_count) {
		    if((new_buffer_read + flat_buf->blocklens[k] + 
			size_read) > bufsize) {
		        end_brd_size = new_buffer_read + 
			    flat_buf->blocklens[k] - (bufsize - size_read);
			new_buffer_read = bufsize - size_read;
		    }
		    else {
		        new_buffer_read += flat_buf->blocklens[k];
			end_brd_size = flat_buf->blocklens[k];
		    }
		}
		else {
		    if (brd_size > (bufsize - size_read)) {
		        new_buffer_read = bufsize - size_read;
			brd_size = new_buffer_read;
		    }
		    else new_buffer_read = brd_size;
		}
		mem_list_count++;
		k = (k + 1)%flat_buf->count;
	     } /* while ((mem_list_count < MAX_ARRAY_SIZE) && 
	       (new_buffer_read < bufsize-size_read)) */
	    j = start_j;
	    new_file_read = 0;
	    file_list_count = 0;
	    while ((file_list_count < MAX_ARRAY_SIZE) && 
		   (new_file_read < new_buffer_read)) {
	        if(file_list_count) {
		    if((new_file_read + flat_file->blocklens[j]) > 
		       new_buffer_read) {
		        end_frd_size = new_buffer_read - new_file_read;
			new_file_read = new_buffer_read;
			j--;
		    }
		    else {
		        new_file_read += flat_file->blocklens[j];
			end_frd_size = flat_file->blocklens[j];
		    }
		}
		else {
		    if (frd_size > new_buffer_read) {
		        new_file_read = new_buffer_read;
			frd_size = new_file_read;
		    }
		    else new_file_read = frd_size;
		}
		file_list_count++;
		if (j < (flat_file->count - 1)) j++;
		else j = 0;
		
		k = start_k;
		if ((new_file_read < new_buffer_read) && 
		    (file_list_count == MAX_ARRAY_SIZE)) {
		    new_buffer_read = 0;
		    mem_list_count = 0;
		    while (new_buffer_read < new_file_read) {
		        if(mem_list_count) {
			    if((new_buffer_read + flat_buf->blocklens[k]) >
			       new_file_read) {
			        end_brd_size = new_file_read - new_buffer_read;
				new_buffer_read = new_file_read;
				k--;
			    }
			    else {
			        new_buffer_read += flat_buf->blocklens[k];
				end_brd_size = flat_buf->blocklens[k];
			    }
			}
			else {
			    new_buffer_read = brd_size;
			    if (brd_size > (bufsize - size_read)) {
			        new_buffer_read = bufsize - size_read;
				brd_size = new_buffer_read;
			    }
			}
			mem_list_count++;
			k = (k + 1)%flat_buf->count;
		    } /* while (new_buffer_read < new_file_read) */
		} /* if ((new_file_read < new_buffer_read) && (file_list_count
		     == MAX_ARRAY_SIZE)) */
	    } /* while ((mem_list_count < MAX_ARRAY_SIZE) && 
		 (new_buffer_read < bufsize-size_read)) */

	    /*  fills the allocated readlist arrays  */
	    k = start_k;
	    j = start_j;
	    for (i=0; i<mem_list_count; i++) {	     
	        mem_offsets[i] = (char*)((char *)buf + buftype_extent*
					 (buf_count/flat_buf->count) +
					 (int)flat_buf->indices[k]);
		if(!i) {
		    mem_lengths[0] = brd_size;
		    mem_offsets[0] += flat_buf->blocklens[k] - brd_size;
		}
		else {
		    if (i == (mem_list_count - 1)) {
		        mem_lengths[i] = end_brd_size;
			if (flat_buf->blocklens[k] == end_brd_size)
			    brd_size = flat_buf->blocklens[(k+1)%
							  flat_buf->count];
			else {
			    brd_size = flat_buf->blocklens[k] - end_brd_size;
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
		    file_lengths[0] = frd_size;
		    file_offsets[0] += flat_file->blocklens[j] - frd_size;
		}
		else {
		    if (i == (file_list_count - 1)) {
		        file_lengths[i] = end_frd_size;
			if (flat_file->blocklens[j] == end_frd_size)
			    frd_size = flat_file->blocklens[(j+1)%
							  flat_file->count];   
			else {
			    frd_size = flat_file->blocklens[j] - end_frd_size;
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

	    /* 
	    printf("about to call read_list in noncontig/noncontig\n");
	    printf("offsets and lengths in terms of integers\n");
	    printf("\nmem_list_count = %d\n", mem_list_count);
	    for (i=0; i<mem_list_count; i++) {
	      printf("mem_offsets[%2d] = %2d   ", i, (int)(mem_offsets[i] - (int)buf)/4);
	      printf("mem_lengths[%2d] = %2d\n", i, mem_lengths[i]/4);
	    }
	    printf("\nfile_list_count = %d\n", file_list_count);
	    for (i=0; i<file_list_count; i++) {
	      printf("file_offsets[%2d] = %2d   ", i, (int)file_offsets[i]/4);
	      printf("file_lengths[%2d] = %2d\n", i, file_lengths[i]/4);
	    }
	    printf("\n\n");
	    */
	    pvfs_read_list(fd->fd_sys,mem_list_count, mem_offsets,
			   mem_lengths, file_list_count, file_offsets,
			   file_lengths);
	    size_read += new_buffer_read;
	    start_k = k;
	    start_j = j;
	} /* while (size_read < bufsize) */
	ADIOI_Free(mem_offsets);
	ADIOI_Free(mem_lengths);
    }
    ADIOI_Free(file_offsets);
    ADIOI_Free(file_lengths);
    
    if (file_ptr_type == ADIO_INDIVIDUAL) fd->fp_ind = off;
    fd->fp_sys_posn = -1;   /* set it to null. */
    
#ifdef HAVE_STATUS_SET_BYTES
    MPIR_Status_set_bytes(status, datatype, bufsize);
    /* This is a temporary way of filling in status. The right way is to 
       keep track of how much data was actually read and placed in buf 
       by ADIOI_BUFFERED_READ. */
#endif
    
    if (!buftype_is_contig) ADIOI_Delete_flattened(datatype);
}
#endif /* HAVE_PVFS_LISTIO */
