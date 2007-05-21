/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 2004 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "adio.h"

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_SIGNAL_H
#include <signal.h>
#endif
#ifdef HAVE_AIO_H
#include <aio.h>
#endif
#ifdef HAVE_SYS_AIO_H
#include <sys/aio.h>
#endif

/* Workaround for incomplete set of definitions if __REDIRECT is not 
   defined and large file support is used in aio.h */
#if !defined(__REDIRECT) && defined(__USE_FILE_OFFSET64)
#define aiocb aiocb64
#endif

/* ADIOI_GEN_IwriteContig
 *
 * This code handles two distinct cases.  If ROMIO_HAVE_WORKING_AIO is not
 * defined, then I/O is performed in a blocking manner.  Otherwise we post
 * an asynchronous I/O operations using the appropriate aio routines.
 */
void ADIOI_GEN_IwriteContig(ADIO_File fd, void *buf, int count, 
			    MPI_Datatype datatype, int file_ptr_type,
			    ADIO_Offset offset, ADIO_Request *request,
			    int *error_code)  
{
    int len, typesize;
#ifndef ROMIO_HAVE_WORKING_AIO
    ADIO_Status status;
#else
    int aio_errno = 0;
    static char myname[] = "ADIOI_GEN_IWRITECONTIG";
#endif

    *request = ADIOI_Malloc_request();
    (*request)->optype = ADIOI_WRITE;
    (*request)->fd = fd;
    (*request)->datatype = datatype;

    MPI_Type_size(datatype, &typesize);
    len = count * typesize;

#ifndef ROMIO_HAVE_WORKING_AIO
    /* no support for nonblocking I/O. Use blocking I/O. */

    ADIO_WriteContig(fd, buf, len, MPI_BYTE, file_ptr_type, offset, 
		     &status, error_code);  
    (*request)->queued = 0;
# ifdef HAVE_STATUS_SET_BYTES
    if (*error_code == MPI_SUCCESS) {
	MPI_Get_elements(&status, MPI_BYTE, &len);
	(*request)->nbytes = len;
    }
# endif

    fd->fp_sys_posn = -1;

#else
    if (file_ptr_type == ADIO_INDIVIDUAL) offset = fd->fp_ind;
    aio_errno = ADIOI_GEN_aio(fd, buf, len, offset, 1, &((*request)->handle));
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
#endif /* NO_AIO */

    fd->async_count++;
}


/* This function is for implementation convenience.
 * It takes care of the differences in the interface for nonblocking I/O
 * on various Unix machines! If wr==1 write, wr==0 read.
 *
 * Returns 0 on success, -errno on failure.
 */
#ifdef ROMIO_HAVE_WORKING_AIO
int ADIOI_GEN_aio(ADIO_File fd, void *buf, int len, ADIO_Offset offset,
		  int wr, void *handle)
{
    int err=-1, fd_sys;

    int error_code;
    struct aiocb *aiocbp;

    fd_sys = fd->fd_sys;

    aiocbp = (struct aiocb *) ADIOI_Calloc(sizeof(struct aiocb), 1);
    aiocbp->aio_offset = offset;
    aiocbp->aio_buf    = buf;
    aiocbp->aio_nbytes = len;

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

#ifdef ROMIO_HAVE_STRUCT_AIOCB_WITH_AIO_FILDES
    if (wr) err = aio_write(aiocbp);
    else err = aio_read(aiocbp);
#else
    /* Broken IBM interface */
    if (wr) err = aio_write(fd_sys, aiocbp);
    else err = aio_read(fd_sys, aiocbp);
#endif

    if (err == -1) {
	if (errno == EAGAIN) {
        /* exceeded the max. no. of outstanding requests.
           complete all previous async. requests and try again. */

	    ADIOI_Complete_async(&error_code);
	    if (error_code != MPI_SUCCESS) return -EIO;

	    while (err == -1 && errno == EAGAIN) {

#ifdef ROMIO_HAVE_STRUCT_AIOCB_WITH_AIO_FILDES
		if (wr) err = aio_write(aiocbp);
		else err = aio_read(aiocbp);
#else
		/* Broken IBM interface */
		if (wr) err = aio_write(fd_sys, aiocbp);
		else err = aio_read(fd_sys, aiocbp);
#endif

		if (err == -1 && errno == EAGAIN) {
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
	    return -errno;
	}
    }

    *((struct aiocb **) handle) = aiocbp;

    return 0;
}
#endif


/* Generic implementation of IwriteStrided calls the blocking WriteStrided
 * immediately.
 */
void ADIOI_GEN_IwriteStrided(ADIO_File fd, void *buf, int count, 
			     MPI_Datatype datatype, int file_ptr_type,
			     ADIO_Offset offset, ADIO_Request *request,
			     int *error_code)
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

    /* Call the blocking function.  It will create an error code 
     * if necessary.
     */
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
