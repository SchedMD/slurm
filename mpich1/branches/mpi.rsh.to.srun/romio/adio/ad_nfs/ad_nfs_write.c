/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_nfs.h"
#include "adio_extern.h"

void ADIOI_NFS_WriteContig(ADIO_File fd, void *buf, int count, 
                     MPI_Datatype datatype, int file_ptr_type,
		     ADIO_Offset offset, ADIO_Status *status, int *error_code)
{
    int err=-1, datatype_size, len;
    static char myname[] = "ADIOI_NFS_WRITECONTIG";

    MPI_Type_size(datatype, &datatype_size);
    len = datatype_size * count;

    if (file_ptr_type == ADIO_EXPLICIT_OFFSET) {
	if (fd->fp_sys_posn != offset)
	    lseek(fd->fd_sys, offset, SEEK_SET);
	ADIOI_WRITE_LOCK(fd, offset, SEEK_SET, len);
	err = write(fd->fd_sys, buf, len);
	ADIOI_UNLOCK(fd, offset, SEEK_SET, len);
	fd->fp_sys_posn = offset + err;
	/* individual file pointer not updated */        
    }
    else { /* write from curr. location of ind. file pointer */
	offset = fd->fp_ind;
	if (fd->fp_sys_posn != fd->fp_ind)
	    lseek(fd->fd_sys, fd->fp_ind, SEEK_SET);
	ADIOI_WRITE_LOCK(fd, offset, SEEK_SET, len);
	err = write(fd->fd_sys, buf, len);
	ADIOI_UNLOCK(fd, offset, SEEK_SET, len);
	fd->fp_ind += err;
	fd->fp_sys_posn = fd->fp_ind;
    }

    /* --BEGIN ERROR HANDLING-- */
    if (err == -1) {
	*error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					   myname, __LINE__, MPI_ERR_IO,
					   "**io",
					   "**io %s", strerror(errno));
	return;
    }
    /* --END ERROR HANDLING-- */

#ifdef HAVE_STATUS_SET_BYTES
    MPIR_Status_set_bytes(status, datatype, err);
#endif

    *error_code = MPI_SUCCESS;
}




#define ADIOI_BUFFERED_WRITE \
{ \
    if (req_off >= writebuf_off + writebuf_len) { \
	lseek(fd->fd_sys, writebuf_off, SEEK_SET); \
	err = write(fd->fd_sys, writebuf, writebuf_len); \
        if (!(fd->atomicity)) ADIOI_UNLOCK(fd, writebuf_off, SEEK_SET, writebuf_len); \
        if (err == -1) err_flag = 1; \
	writebuf_off = req_off; \
        writebuf_len = (int) (ADIOI_MIN(max_bufsize,end_offset-writebuf_off+1));\
	if (!(fd->atomicity)) ADIOI_WRITE_LOCK(fd, writebuf_off, SEEK_SET, writebuf_len); \
	lseek(fd->fd_sys, writebuf_off, SEEK_SET); \
	err = read(fd->fd_sys, writebuf, writebuf_len); \
        if (err == -1) { \
            *error_code = MPIO_Err_create_code(MPI_SUCCESS, \
					       MPIR_ERR_RECOVERABLE, myname, \
					       __LINE__, MPI_ERR_IO, \
					       "**ioRMWrdwr", 0); \
	    return; \
        } \
    } \
    write_sz = (int) (ADIOI_MIN(req_len, writebuf_off + writebuf_len - req_off)); \
    memcpy(writebuf+req_off-writebuf_off, (char *)buf +userbuf_off, write_sz);\
    while (write_sz != req_len) { \
	lseek(fd->fd_sys, writebuf_off, SEEK_SET); \
	err = write(fd->fd_sys, writebuf, writebuf_len); \
        if (!(fd->atomicity)) ADIOI_UNLOCK(fd, writebuf_off, SEEK_SET, writebuf_len); \
        if (err == -1) err_flag = 1; \
        req_len -= write_sz; \
        userbuf_off += write_sz; \
        writebuf_off += writebuf_len; \
        writebuf_len = (int) (ADIOI_MIN(max_bufsize,end_offset-writebuf_off+1));\
	if (!(fd->atomicity)) ADIOI_WRITE_LOCK(fd, writebuf_off, SEEK_SET, writebuf_len); \
	lseek(fd->fd_sys, writebuf_off, SEEK_SET); \
	err = read(fd->fd_sys, writebuf, writebuf_len); \
        if (err == -1) { \
	    *error_code = MPIO_Err_create_code(MPI_SUCCESS, \
					       MPIR_ERR_RECOVERABLE, myname, \
					       __LINE__, MPI_ERR_IO, \
					       "**ioRMWrdwr", 0); \
	    return; \
        } \
        write_sz = ADIOI_MIN(req_len, writebuf_len); \
        memcpy(writebuf, (char *)buf + userbuf_off, write_sz);\
    } \
}


/* this macro is used when filetype is contig and buftype is not contig.
   it does not do a read-modify-write and does not lock*/
#define ADIOI_BUFFERED_WRITE_WITHOUT_READ \
{ \
    if (req_off >= writebuf_off + writebuf_len) { \
	lseek(fd->fd_sys, writebuf_off, SEEK_SET); \
	if (!(fd->atomicity)) ADIOI_WRITE_LOCK(fd, writebuf_off, SEEK_SET, writebuf_len); \
	err = write(fd->fd_sys, writebuf, writebuf_len); \
        if (!(fd->atomicity)) ADIOI_UNLOCK(fd, writebuf_off, SEEK_SET, writebuf_len); \
        if (err == -1) err_flag = 1; \
	writebuf_off = req_off; \
        writebuf_len = (int) (ADIOI_MIN(max_bufsize,end_offset-writebuf_off+1));\
    } \
    write_sz = (int) (ADIOI_MIN(req_len, writebuf_off + writebuf_len - req_off)); \
    memcpy(writebuf+req_off-writebuf_off, (char *)buf +userbuf_off, write_sz);\
    while (write_sz != req_len) { \
	lseek(fd->fd_sys, writebuf_off, SEEK_SET); \
	if (!(fd->atomicity)) ADIOI_WRITE_LOCK(fd, writebuf_off, SEEK_SET, writebuf_len); \
	err = write(fd->fd_sys, writebuf, writebuf_len); \
        if (!(fd->atomicity)) ADIOI_UNLOCK(fd, writebuf_off, SEEK_SET, writebuf_len); \
        if (err == -1) err_flag = 1; \
        req_len -= write_sz; \
        userbuf_off += write_sz; \
        writebuf_off += writebuf_len; \
        writebuf_len = (int) (ADIOI_MIN(max_bufsize,end_offset-writebuf_off+1));\
        write_sz = ADIOI_MIN(req_len, writebuf_len); \
        memcpy(writebuf, (char *)buf + userbuf_off, write_sz);\
    } \
}



void ADIOI_NFS_WriteStrided(ADIO_File fd, void *buf, int count,
                       MPI_Datatype datatype, int file_ptr_type,
                       ADIO_Offset offset, ADIO_Status *status, int
                       *error_code)
{
/* offset is in units of etype relative to the filetype. */

    ADIOI_Flatlist_node *flat_buf, *flat_file;
    int i, j, k, err=-1, bwr_size, fwr_size=0, st_index=0;
    int bufsize, num, size, sum, n_etypes_in_filetype, size_in_filetype;
    int n_filetypes, etype_in_filetype;
    ADIO_Offset abs_off_in_filetype=0;
    int filetype_size, etype_size, buftype_size, req_len;
    MPI_Aint filetype_extent, buftype_extent; 
    int buf_count, buftype_is_contig, filetype_is_contig;
    ADIO_Offset userbuf_off;
    ADIO_Offset off, req_off, disp, end_offset=0, writebuf_off, start_off;
    char *writebuf, *value;
    int flag, st_fwr_size, st_n_filetypes, writebuf_len, write_sz;
    int new_bwr_size, new_fwr_size, err_flag=0, info_flag, max_bufsize;
    static char myname[] = "ADIOI_NFS_WRITESTRIDED";

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

/* get max_bufsize from the info object. */

    value = (char *) ADIOI_Malloc((MPI_MAX_INFO_VAL+1)*sizeof(char));
    MPI_Info_get(fd->info, "ind_wr_buffer_size", MPI_MAX_INFO_VAL, value, 
                 &info_flag);
    max_bufsize = atoi(value);
    ADIOI_Free(value);

    if (!buftype_is_contig && filetype_is_contig) {

/* noncontiguous in memory, contiguous in file. */

	ADIOI_Flatten_datatype(datatype);
	flat_buf = ADIOI_Flatlist;
	while (flat_buf->type != datatype) flat_buf = flat_buf->next;

        off = (file_ptr_type == ADIO_INDIVIDUAL) ? fd->fp_ind : 
                 fd->disp + etype_size * offset;

        start_off = off;
	end_offset = off + bufsize - 1;
        writebuf_off = off;
        writebuf = (char *) ADIOI_Malloc(max_bufsize);
        writebuf_len = (int) (ADIOI_MIN(max_bufsize,end_offset-writebuf_off+1));

/* if atomicity is true, lock the region to be accessed */
        if (fd->atomicity) 
            ADIOI_WRITE_LOCK(fd, start_off, SEEK_SET, end_offset-start_off+1);

        for (j=0; j<count; j++) 
            for (i=0; i<flat_buf->count; i++) {
                userbuf_off = j*buftype_extent + flat_buf->indices[i];
		req_off = off;
		req_len = flat_buf->blocklens[i];
		ADIOI_BUFFERED_WRITE_WITHOUT_READ
                off += flat_buf->blocklens[i];
            }

        /* write the buffer out finally */
	lseek(fd->fd_sys, writebuf_off, SEEK_SET); 
	if (!(fd->atomicity)) ADIOI_WRITE_LOCK(fd, writebuf_off, SEEK_SET, writebuf_len);
	err = write(fd->fd_sys, writebuf, writebuf_len); 
        if (!(fd->atomicity)) ADIOI_UNLOCK(fd, writebuf_off, SEEK_SET, writebuf_len);
        if (err == -1) err_flag = 1; 

        if (fd->atomicity) 
            ADIOI_UNLOCK(fd, start_off, SEEK_SET, end_offset-start_off+1);

	ADIOI_Free(writebuf); /* malloced in the buffered_write macro */

        if (file_ptr_type == ADIO_INDIVIDUAL) fd->fp_ind = off;
	if (err_flag) {
	    *error_code = MPIO_Err_create_code(MPI_SUCCESS,
					       MPIR_ERR_RECOVERABLE, myname,
					       __LINE__, MPI_ERR_IO, "**io",
					       "**io %s", strerror(errno));
	}
	else *error_code = MPI_SUCCESS;
    }

    else {  /* noncontiguous in file */

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
			fwr_size = (int) (disp + flat_file->indices[i] + 
			        (ADIO_Offset) n_filetypes*filetype_extent
			         + flat_file->blocklens[i] - offset);
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

        start_off = offset;

       /* Calculate end_offset, the last byte-offset that will be accessed.
         e.g., if start_offset=0 and 100 bytes to be write, end_offset=99*/

	st_fwr_size = fwr_size;
	st_n_filetypes = n_filetypes;
	i = 0;
	j = st_index;
	off = offset;
	fwr_size = ADIOI_MIN(st_fwr_size, bufsize);
	while (i < bufsize) {
	    i += fwr_size;
	    end_offset = off + fwr_size - 1;

	    if (j < (flat_file->count - 1)) j++;
	    else {
		j = 0;
		n_filetypes++;
	    }

	    off = disp + flat_file->indices[j] + (ADIO_Offset) n_filetypes*filetype_extent;
	    fwr_size = ADIOI_MIN(flat_file->blocklens[j], bufsize-i);
	}

/* if atomicity is true, lock the region to be accessed */
        if (fd->atomicity) 
            ADIOI_WRITE_LOCK(fd, start_off, SEEK_SET, end_offset-start_off+1);

        /* initial read for the read-modify-write */
        writebuf_off = offset;
        writebuf = (char *) ADIOI_Malloc(max_bufsize);
        writebuf_len = (int)(ADIOI_MIN(max_bufsize,end_offset-writebuf_off+1));
	if (!(fd->atomicity)) ADIOI_WRITE_LOCK(fd, writebuf_off, SEEK_SET, writebuf_len);
	lseek(fd->fd_sys, writebuf_off, SEEK_SET); 
	err = read(fd->fd_sys, writebuf, writebuf_len); 
        if (err == -1) {
	    *error_code = MPIO_Err_create_code(MPI_SUCCESS,
					       MPIR_ERR_RECOVERABLE,
					       myname, __LINE__,
					       MPI_ERR_IO,
					       "ADIOI_NFS_WriteStrided: ROMIO tries to optimize this access by doing a read-modify-write, but is unable to read the file. Please give the file read permission and open it with MPI_MODE_RDWR.", 0);
	    return;
        } 

	if (buftype_is_contig && !filetype_is_contig) {

/* contiguous in memory, noncontiguous in file. should be the most
   common case. */

	    i = 0;
	    j = st_index;
	    off = offset;
	    n_filetypes = st_n_filetypes;
	    fwr_size = ADIOI_MIN(st_fwr_size, bufsize);
	    while (i < bufsize) {
                if (fwr_size) { 
                    /* TYPE_UB and TYPE_LB can result in 
                       fwr_size = 0. save system call in such cases */ 
		    /* lseek(fd->fd_sys, off, SEEK_SET);
		    err = write(fd->fd_sys, ((char *) buf) + i, fwr_size);*/

		    req_off = off;
		    req_len = fwr_size;
		    userbuf_off = i;
		    ADIOI_BUFFERED_WRITE
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
	    i = (int) (flat_buf->indices[0]);
	    j = st_index;
	    off = offset;
	    n_filetypes = st_n_filetypes;
	    fwr_size = st_fwr_size;
	    bwr_size = flat_buf->blocklens[0];

	    while (num < bufsize) {
		size = ADIOI_MIN(fwr_size, bwr_size);
		if (size) {
		    /* lseek(fd->fd_sys, off, SEEK_SET);
		    err = write(fd->fd_sys, ((char *) buf) + i, size); */

		    req_off = off;
		    req_len = size;
		    userbuf_off = i;
		    ADIOI_BUFFERED_WRITE
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
			i += size;
			new_bwr_size -= size;
		    }
		}

		if (size == bwr_size) {
/* reached end of contiguous block in memory */

		    k = (k + 1)%flat_buf->count;
		    buf_count++;
		    i = (int) (buftype_extent*(buf_count/flat_buf->count) +
			flat_buf->indices[k]); 
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

        /* write the buffer out finally */	
	lseek(fd->fd_sys, writebuf_off, SEEK_SET); 
	if (!(fd->atomicity)) ADIOI_WRITE_LOCK(fd, writebuf_off, SEEK_SET, writebuf_len);
	err = write(fd->fd_sys, writebuf, writebuf_len); 

        if (!(fd->atomicity))
	    ADIOI_UNLOCK(fd, writebuf_off, SEEK_SET, writebuf_len);
	else ADIOI_UNLOCK(fd, start_off, SEEK_SET, end_offset-start_off+1);

        if (err == -1) err_flag = 1; 

	ADIOI_Free(writebuf); /* malloced in the buffered_write macro */

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
