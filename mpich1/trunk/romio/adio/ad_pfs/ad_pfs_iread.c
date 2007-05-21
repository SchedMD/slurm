/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_pfs.h"

void ADIOI_PFS_IreadContig(ADIO_File fd, void *buf, int count, 
			   MPI_Datatype datatype, int file_ptr_type,
			   ADIO_Offset offset, ADIO_Request *request,
			   int *error_code)
{
    long *id_sys;
    int len, typesize, err=-1;
    ADIO_Offset off;
    static char myname[] = "ADIOI_PFS_IREADCONTIG";

    *request = ADIOI_Malloc_request();
    (*request)->optype = ADIOI_READ;
    (*request)->fd = fd;
    (*request)->datatype = datatype;

    MPI_Type_size(datatype, &typesize);
    len = count * typesize;

    id_sys = (long *) ADIOI_Malloc(sizeof(long));
    (*request)->handle = (void *) id_sys;

    off = (file_ptr_type == ADIO_INDIVIDUAL) ? fd->fp_ind : offset;

    lseek(fd->fd_sys, off, SEEK_SET);
    *id_sys = _iread(fd->fd_sys, buf, len);

    if ((*id_sys == -1) && (errno == EQNOMID)) {
     /* the man pages say EMREQUEST, but in reality errno is set to EQNOMID! */

        /* exceeded the max. no. of outstanding requests. */

        /* complete all previous async. requests */
        ADIOI_Complete_async(error_code);
	if (*error_code != MPI_SUCCESS) return;

        /* try again */
        *id_sys = _iread(fd->fd_sys, buf, len);

        if ((*id_sys == -1) && (errno == EQNOMID)) {
	    *error_code = MPIO_Err_create_code(MPI_SUCCESS,
					       MPIR_ERR_RECOVERABLE, myname,
					       __LINE__, MPI_ERR_IO, "**io",
					       "**io %s", strerror(errno));
	    return;
        }
    }
    else if (*id_sys == -1) {
	*error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					   myname, __LINE__, MPI_ERR_IO,
					   "**io",
					   "**io %s", strerror(errno));
	return;
    }

    if (file_ptr_type == ADIO_INDIVIDUAL) fd->fp_ind += len; 

    (*request)->queued = 1;
    (*request)->nbytes = len;
    ADIOI_Add_req_to_list(request);
    fd->async_count++;

    fd->fp_sys_posn = -1;   /* set it to null. */

    if (*id_sys == -1) {
	*error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					   myname, __LINE__, MPI_ERR_IO,
					   "**io",
					   "**io %s", strerror(errno));
    }
    else *error_code = MPI_SUCCESS;
}
