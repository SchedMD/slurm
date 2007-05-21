/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_nfs.h"

void ADIOI_NFS_IwriteContig(ADIO_File fd, void *buf, int count, 
                MPI_Datatype datatype, int file_ptr_type,
                ADIO_Offset offset, ADIO_Request *request, int *error_code)  
{
    int len, typesize;
#ifndef ROMIO_HAVE_WORKING_AIO
    ADIO_Status status;
#else
    int aio_errno = 0;
    static char myname[] = "ADIOI_NFS_IWRITECONTIG";
#endif

    *request = ADIOI_Malloc_request();
    (*request)->optype = ADIOI_WRITE;
    (*request)->fd = fd;
    (*request)->datatype = datatype;

    MPI_Type_size(datatype, &typesize);
    len = count * typesize;

#ifndef ROMIO_HAVE_WORKING_AIO
    /* HP, FreeBSD, Linux */
    /* no support for nonblocking I/O. Use blocking I/O. */

    ADIOI_NFS_WriteContig(fd, buf, len, MPI_BYTE, file_ptr_type, offset,
			  &status,
			  error_code);  
    (*request)->queued = 0;
#ifdef HAVE_STATUS_SET_BYTES
    if (*error_code == MPI_SUCCESS) {
	MPI_Get_elements(&status, MPI_BYTE, &len);
	(*request)->nbytes = len;
    }
#endif

    fd->fp_sys_posn = -1;

#else
    if (file_ptr_type == ADIO_INDIVIDUAL) offset = fd->fp_ind;
    aio_errno = ADIOI_NFS_aio(fd, buf, len, offset, 1, &((*request)->handle));
    if (file_ptr_type == ADIO_INDIVIDUAL) fd->fp_ind += len;

    (*request)->queued = 1;
    ADIOI_Add_req_to_list(request);

    fd->fp_sys_posn = -1;

    if (aio_errno != 0) {
	/* --BEGIN ERROR HANDLING-- */
	MPIO_ERR_CREATE_CODE_ERRNO(myname, aio_errno, error_code);
	return;
	/* --END ERROR HANDLING-- */
    }
    else *error_code = MPI_SUCCESS;
#endif

    fd->async_count++;
}


/* This function is for implementation convenience. It is not user-visible.
 * It takes care of the differences in the interface for nonblocking I/O
 * on various Unix machines! If wr==1 write, wr==0 read.
 *
 * Returns 0 on success, -errno on failure.
 */
#ifdef ROMIO_HAVE_WORKING_AIO
int ADIOI_NFS_aio(ADIO_File fd, void *buf, int len, ADIO_Offset offset,
		  int wr, void *handle)
{
    int err=-1, fd_sys;
    int error_code, this_errno;

    struct aiocb *aiocbp;
    
    fd_sys = fd->fd_sys;

    aiocbp = (struct aiocb *) ADIOI_Calloc(sizeof(struct aiocb), 1);
    aiocbp->aio_offset = offset;
    aiocbp->aio_buf    = buf;
    aiocbp->aio_nbytes = len;

    /* This madness is mostly here to deal with IBM AIO implementation */
#ifdef ROMIO_HAVE_STRUCT_AIOCB_WITH_AIO_WHENCE
    aiocbp->aio_whence = SEEK_SET;
#endif
#ifdef ROMIO_HAVE_STRUCT_AIOCB_WITH_AIO_FILDES
    aiocbp->aio_fildes = fd_sys;
#endif
#ifdef ROMIO_HAVE_STRUCT_AIOCB_WITH_AIO_SIGEVENT
# ifdef AIO_SIGNOTIFY_NONE
    aiocbp->aio_sigevent.sigev_notify = SIGEV_NONE;
# endif
    aiocbp->aio_sigevent.sigev_signo = 0;
#endif
#ifdef ROMIO_HAVE_STRUCT_AIOCB_WITH_AIO_REQPRIO
# ifdef AIO_PRIO_DFL
    aiocbp->aio_reqprio = AIO_PRIO_DFL;   /* not needed in DEC Unix 4.0 */
# else
    aiocbp->aio_reqprio = 0;
# endif
#endif

    if (wr) ADIOI_WRITE_LOCK(fd, offset, SEEK_SET, len);
    else ADIOI_READ_LOCK(fd, offset, SEEK_SET, len);

#ifdef ROMIO_HAVE_STRUCT_AIOCB_WITH_AIO_FILDES
    if (wr) err = aio_write(aiocbp);
    else err = aio_read(aiocbp);
#else
    /* Broken IBM interface */
    if (wr) err = aio_write(fd_sys, aiocbp);
    else err = aio_read(fd_sys, aiocbp);
#endif

    this_errno = errno;
    ADIOI_UNLOCK(fd, offset, SEEK_SET, len);

    if (err == -1) {
	if (this_errno == EAGAIN) {
        /* exceeded the max. no. of outstanding requests.
           complete all previous async. requests and try again. */

	    ADIOI_Complete_async(&error_code);
	    if (error_code != MPI_SUCCESS) return -EIO;

	    while (err == -1 && this_errno == EAGAIN) {

		if (wr) ADIOI_WRITE_LOCK(fd, offset, SEEK_SET, len);
		else ADIOI_READ_LOCK(fd, offset, SEEK_SET, len);

#ifdef ROMIO_HAVE_STRUCT_AIOCB_WITH_AIO_FILDES
		if (wr) err = aio_write(aiocbp);
		else err = aio_read(aiocbp);
#else
		/* Broken IBM interface */
		if (wr) err = aio_write(fd_sys, aiocbp);
		else err = aio_read(fd_sys, aiocbp);
#endif
		this_errno = errno;
		ADIOI_UNLOCK(fd, offset, SEEK_SET, len);

		if (err == -1 && this_errno == EAGAIN) {
		    /* sleep and try again */
		    sleep(1);
		}
		else if (err == -1) {
		    /* real error */
		    return -errno;
		}
	    }
        }
	else {
	    return -this_errno;
	}
    }

    *((struct aiocb **) handle) = aiocbp;

    return 0;
}
#endif
