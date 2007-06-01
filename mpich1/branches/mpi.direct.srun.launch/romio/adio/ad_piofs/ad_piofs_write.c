/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_piofs.h"
#include "adio_extern.h"
#ifdef PROFILE
#include "mpe.h"
#endif

void ADIOI_PIOFS_WriteContig(ADIO_File fd, void *buf, int count, 
                     MPI_Datatype datatype, int file_ptr_type,
		     ADIO_Offset offset, ADIO_Status *status, int *error_code)
{
    int err=-1, datatype_size, len;
#ifndef PRINT_ERR_MSG
    static char myname[] = "ADIOI_PIOFS_WRITECONTIG";
#endif

    MPI_Type_size(datatype, &datatype_size);
    len = datatype_size * count;

    if (file_ptr_type == ADIO_EXPLICIT_OFFSET) {
	if (fd->fp_sys_posn != offset) {
#ifdef PROFILE
            MPE_Log_event(11, 0, "start seek");
#endif
	    llseek(fd->fd_sys, offset, SEEK_SET);
#ifdef PROFILE
            MPE_Log_event(12, 0, "end seek");
#endif
	}
#ifdef PROFILE
        MPE_Log_event(5, 0, "start write");
#endif
	err = write(fd->fd_sys, buf, len);
#ifdef PROFILE
        MPE_Log_event(6, 0, "end write");
#endif
	fd->fp_sys_posn = offset + err;
	/* individual file pointer not updated */        
    }
    else { /* write from curr. location of ind. file pointer */
	if (fd->fp_sys_posn != fd->fp_ind) {
#ifdef PROFILE
            MPE_Log_event(11, 0, "start seek");
#endif
	    llseek(fd->fd_sys, fd->fp_ind, SEEK_SET);
#ifdef PROFILE
            MPE_Log_event(12, 0, "end seek");
#endif
	}
#ifdef PROFILE
        MPE_Log_event(5, 0, "start write");
#endif
	err = write(fd->fd_sys, buf, len);
#ifdef PROFILE
        MPE_Log_event(6, 0, "end write");
#endif
	fd->fp_ind += err;
	fd->fp_sys_posn = fd->fp_ind;
    }

#ifdef HAVE_STATUS_SET_BYTES
    if (err != -1) MPIR_Status_set_bytes(status, datatype, err);
#endif

    if (err == -1) {
#ifdef MPICH2
	*error_code = MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE, myname, __LINE__, MPI_ERR_IO, "**io",
	    "**io %s", strerror(errno));
#elif defined(PRINT_ERR_MSG)
	*error_code =  MPI_ERR_UNKNOWN;
#else
	*error_code = MPIR_Err_setmsg(MPI_ERR_IO, MPIR_ADIO_ERROR,
			      myname, "I/O Error", "%s", strerror(errno));
	ADIOI_Error(fd, *error_code, myname);
#endif
    }
    else *error_code = MPI_SUCCESS;
}



void ADIOI_PIOFS_WriteStrided(ADIO_File fd, void *buf, int count,
                       MPI_Datatype datatype, int file_ptr_type,
                       ADIO_Offset offset, ADIO_Status *status, int
                       *error_code)
{
/* Since PIOFS does not support file locking, can't do buffered writes
   as on Unix */

/* offset is in units of etype relative to the filetype. */

    ADIOI_Flatlist_node *flat_buf, *flat_file;
    struct iovec *iov;
    int i, j, k, err=-1, bwr_size, fwr_size=0, st_index=0;
    int bufsize, num, size, sum, n_etypes_in_filetype, size_in_filetype;
    int n_filetypes, etype_in_filetype;
    ADIO_Offset abs_off_in_filetype=0;
    int filetype_size, etype_size, buftype_size;
    MPI_Aint filetype_extent, buftype_extent, indx;
    int buf_count, buftype_is_contig, filetype_is_contig;
    ADIO_Offset off, disp;
    int flag, new_bwr_size, new_fwr_size, err_flag=0;
#ifndef PRINT_ERR_MSG
    static char myname[] = "ADIOI_PIOFS_WRITESTRIDED";
#endif

    if (fd->atomicity) {
	FPRINTF(stderr, "ROMIO cannot guarantee atomicity of noncontiguous accesses in atomic mode, as PIOFS doesn't support file locking. Use nonatomic mode and its associated semantics.\n");
	MPI_Abort(MPI_COMM_WORLD, 1);
    }

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

/* noncontiguous in memory, contiguous in file. use writev */

	ADIOI_Flatten_datatype(datatype);
	flat_buf = ADIOI_Flatlist;
	while (flat_buf->type != datatype) flat_buf = flat_buf->next;

/* There is a limit of 16 on the number of iovecs for readv/writev! */

	iov = (struct iovec *) ADIOI_Malloc(16*sizeof(struct iovec));

	if (file_ptr_type == ADIO_EXPLICIT_OFFSET) {
	    off = fd->disp + etype_size * offset;
	    llseek(fd->fd_sys, off, SEEK_SET);
	}
	else off = llseek(fd->fd_sys, fd->fp_ind, SEEK_SET);

	k = 0;
	for (j=0; j<count; j++) 
	    for (i=0; i<flat_buf->count; i++) {
		iov[k].iov_base = ((char *) buf) + j*buftype_extent +
		    flat_buf->indices[i]; 
		iov[k].iov_len = flat_buf->blocklens[i];
		/*FPRINTF(stderr, "%d %d\n", iov[k].iov_base, iov[k].iov_len);*/

		off += flat_buf->blocklens[i];
		k = (k+1)%16;

		if (!k) {
		    err = writev(fd->fd_sys, iov, 16);
		    if (err == -1) err_flag = 1;
		}
	    }

	if (k) {
	    err = writev(fd->fd_sys, iov, k);
	    if (err == -1) err_flag = 1;
	}

	if (file_ptr_type == ADIO_INDIVIDUAL) fd->fp_ind = off;

	ADIOI_Free(iov);
	if (err_flag) {
#ifdef MPICH2
	    *error_code = MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE, myname, __LINE__, MPI_ERR_IO, "**io",
		"**io %s", strerror(errno));
#elif defined(PRINT_ERR_MSG) 
	    *error_code =  MPI_ERR_UNKNOWN;
#else /* MPICH-1 */
	    *error_code = MPIR_Err_setmsg(MPI_ERR_IO, MPIR_ADIO_ERROR,
			      myname, "I/O Error", "%s", strerror(errno));
	    ADIOI_Error(fd, *error_code, myname);
#endif
	}
	else *error_code = MPI_SUCCESS;
    } /* if (!buftype_is_contig && filetype_is_contig) ... */

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
		    llseek(fd->fd_sys, off, SEEK_SET);
#ifdef PROFILE
		    MPE_Log_event(12, 0, "end seek");
		    MPE_Log_event(5, 0, "start write");
#endif
		    err = write(fd->fd_sys, ((char *) buf) + i, fwr_size);
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
		    llseek(fd->fd_sys, off, SEEK_SET);
#ifdef PROFILE
		    MPE_Log_event(12, 0, "end seek");
		    MPE_Log_event(5, 0, "start write");
#endif
		    err = write(fd->fd_sys, ((char *) buf) + indx, size);
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
#ifdef MPICH2
	    *error_code = MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE, myname, __LINE__, MPI_ERR_IO, "**io",
		"**io %s", strerror(errno));
#elif defined(PRINT_ERR_MSG)
	    *error_code = MPI_ERR_UNKNOWN;
#else /* MPICH-1 */
	    *error_code = MPIR_Err_setmsg(MPI_ERR_IO, MPIR_ADIO_ERROR,
			      myname, "I/O Error", "%s", strerror(errno));
	    ADIOI_Error(fd, *error_code, myname);
#endif
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
