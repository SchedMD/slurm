/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_xfs.h"

void ADIOI_XFS_IwriteContig(ADIO_File fd, void *buf, int count, 
			    MPI_Datatype datatype, int file_ptr_type,
			    ADIO_Offset offset, ADIO_Request *request,
			    int *error_code)  
{
    int len, typesize, aio_errno = 0;
    static char myname[] = "ADIOI_XFS_IWRITECONTIG";

    *request = ADIOI_Malloc_request();
    (*request)->optype = ADIOI_WRITE;
    (*request)->fd = fd;
    (*request)->datatype = datatype;

    MPI_Type_size(datatype, &typesize);
    len = count * typesize;

    if (file_ptr_type == ADIO_INDIVIDUAL) offset = fd->fp_ind;
    aio_errno = ADIOI_XFS_aio(fd, buf, len, offset, 1, &((*request)->handle));
    if (file_ptr_type == ADIO_INDIVIDUAL) fd->fp_ind += len;

    (*request)->queued = 1;
    ADIOI_Add_req_to_list(request);

    fd->fp_sys_posn = -1;

    /* --BEGIN ERROR HANDLING-- */
    if (aio_errno != 0) {
	MPIO_ERR_CREATE_CODE_ERRNO(myname, aio_errno, error_code);
	return;
    }
    /* --END ERROR HANDLING-- */

    *error_code = MPI_SUCCESS;
    fd->async_count++;
}


void ADIOI_XFS_IwriteStrided(ADIO_File fd, void *buf, int count, 
		       MPI_Datatype datatype, int file_ptr_type,
                       ADIO_Offset offset, ADIO_Request *request, int
                       *error_code)
{
    ADIO_Status status;
#ifdef HAVE_STATUS_SET_BYTES
    int typesize;
#endif

    *request = ADIOI_Malloc_request();
    (*request)->optype = ADIOI_WRITE;
    (*request)->fd = fd;
    (*request)->datatype = datatype;
    (*request)->queued = 0;
    (*request)->handle = 0;

/* call the blocking version. It is faster because it does data sieving. */
    ADIO_WriteStrided(fd, buf, count, datatype, file_ptr_type, 
		      offset, &status, error_code);  

    fd->async_count++;

#ifdef HAVE_STATUS_SET_BYTES
    if (*error_code == MPI_SUCCESS) {
	MPI_Type_size(datatype, &typesize);
	(*request)->nbytes = count * typesize;
    }
#endif
}


/* This function is for implementation convenience. It is not user-visible.
 * It takes care of the differences in the interface for nonblocking I/O
 * on various Unix machines! If wr==1 write, wr==0 read.
 *
 * Returns 0 on success, -errno on failure.
 */
int ADIOI_XFS_aio(ADIO_File fd, void *buf, int len, ADIO_Offset offset,
		  int wr, void *handle)
{
    int err, error_code;
    aiocb64_t *aiocbp;

    aiocbp = (aiocb64_t *) ADIOI_Calloc(sizeof(aiocb64_t), 1);

    if (((wr && fd->direct_write) || (!wr && fd->direct_read))
	&& !(((long) buf) % fd->d_mem) && !(offset % fd->d_miniosz) && 
	!(len % fd->d_miniosz) && (len >= fd->d_miniosz) && 
	(len <= fd->d_maxiosz))
	aiocbp->aio_fildes = fd->fd_direct;
    else aiocbp->aio_fildes = fd->fd_sys;

    aiocbp->aio_offset = offset;
    aiocbp->aio_buf = buf;
    aiocbp->aio_nbytes = len;
    aiocbp->aio_reqprio = 0;

#ifdef AIO_SIGNOTIFY_NONE
/* SGI IRIX 6 */
    aiocbp->aio_sigevent.sigev_notify = SIGEV_NONE;
#else
    aiocbp->aio_sigevent.sigev_signo = 0;
#endif

    if (wr) err = aio_write64(aiocbp);
    else err = aio_read64(aiocbp);

    if (err != 0) {
	if (errno == EAGAIN) {
        /* exceeded the max. no. of outstanding requests.
	   complete all previous async. requests and try again. */

	    ADIOI_Complete_async(&error_code);
	    if (error_code != MPI_SUCCESS) return -EIO;

	    if (wr) err = aio_write64(aiocbp);
	    else err = aio_read64(aiocbp);

	    while (err != 0) {
		if (errno == EAGAIN) {
		    /* sleep and try again */
		    sleep(1);
		    if (wr) err = aio_write64(aiocbp);
		    else err = aio_read64(aiocbp);
		}
		else {
		    return -errno;
		}
	    }
        }
        else {
	    return -errno;
        }
    }

    *((aiocb64_t **) handle) = aiocbp;
    return 0;
}
