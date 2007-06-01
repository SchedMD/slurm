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

/* ADIOI_GEN_IODone
 *
 * This code handles two distinct cases.  If ROMIO_HAVE_WORKING_AIO is not
 * defined, then I/O was done as a blocking call earlier.  In that case
 * we have nothing much to do other than set the bytes transferred and
 * free the request.
 *
 * If ROMIO_HAVE_WORKING_AIO is defined, then we may need to wait for I/O
 * to complete.
 */
int ADIOI_GEN_IODone(ADIO_Request *request, ADIO_Status *status,
		     int *error_code)  
{
#ifdef ROMIO_HAVE_WORKING_AIO
    int done=0;
    int err;
    static char myname[] = "ADIOI_GEN_IODONE";
#ifdef ROMIO_HAVE_STRUCT_AIOCB_WITH_AIO_HANDLE
    struct aiocb *tmp1;
#endif
#endif

    if (*request == ADIO_REQUEST_NULL) {
	*error_code = MPI_SUCCESS;
	return 1;
    }

#ifndef ROMIO_HAVE_WORKING_AIO
#ifdef HAVE_STATUS_SET_BYTES
    MPIR_Status_set_bytes(status, (*request)->datatype, (*request)->nbytes);
#endif
    (*request)->fd->async_count--;
    ADIOI_Free_request((ADIOI_Req_node *) (*request));
    *request = ADIO_REQUEST_NULL;
    *error_code = MPI_SUCCESS;
    return 1;

#else  /* matches ifndef ROMIO_HAVE_WORKING_AIO */

#ifndef ROMIO_HAVE_STRUCT_AIOCB_WITH_AIO_FILDES
/* old IBM API */
    if ((*request)->queued) {
	tmp1 = (struct aiocb *) (*request)->handle;
	errno = aio_error(tmp1->aio_handle);
	if (errno == EINPROG) {
	    done = 0;
	    *error_code = MPI_SUCCESS;
	}
	else {
	    err = aio_return(tmp1->aio_handle);
	    (*request)->nbytes = err;
	    errno = aio_error(tmp1->aio_handle);
	
	    done = 1;

	    if (err == -1) {
		*error_code = MPIO_Err_create_code(MPI_SUCCESS,
						   MPIR_ERR_RECOVERABLE,
						   myname, __LINE__,
						   MPI_ERR_IO, "**io",
						   "**io %s", strerror(errno));
		return;
	    }
	    else *error_code = MPI_SUCCESS;
	}
    } /* if ((*request)->queued) */
    else {
	done = 1;
	*error_code = MPI_SUCCESS;
    }
#ifdef HAVE_STATUS_SET_BYTES
    if (done && ((*request)->nbytes != -1))
	MPIR_Status_set_bytes(status, (*request)->datatype, (*request)->nbytes);
#endif

#else  /* matches ifndef ROMIO_HAVE_STRUCT_AIOCB_WITH_AIO_HANDLE */

/* everthing other than old IBM */
    if ((*request)->queued) {
	errno = aio_error((const struct aiocb *) (*request)->handle);
	if (errno == EINPROGRESS) {
	    done = 0;
	    *error_code = MPI_SUCCESS;
	}
	else {
	    err = aio_return((struct aiocb *) (*request)->handle); 
	    (*request)->nbytes = err;
	    errno = aio_error((struct aiocb *) (*request)->handle);

	    done = 1;

	    if (err == -1) {
		*error_code = MPIO_Err_create_code(MPI_SUCCESS,
						   MPIR_ERR_RECOVERABLE,
						   myname, __LINE__,
						   MPI_ERR_IO, "**io",
						   "**io %s", strerror(errno));
		return 1;
	    }
	    else *error_code = MPI_SUCCESS;
	}
    } /* if ((*request)->queued) */
    else {
	done = 1;
	*error_code = MPI_SUCCESS;
    }
#ifdef HAVE_STATUS_SET_BYTES
    if (done && ((*request)->nbytes != -1))
	MPIR_Status_set_bytes(status, (*request)->datatype, (*request)->nbytes);
#endif

#endif   /* matches ifndef ROMIO_HAVE_STRUCT_AIOCB_WITH_AIO_HANDLE */

    if (done) {
	/* if request is still queued in the system, it is also there
           on ADIOI_Async_list. Delete it from there. */
	if ((*request)->queued) ADIOI_Del_req_from_list(request);

	(*request)->fd->async_count--;
	if ((*request)->handle) ADIOI_Free((*request)->handle);
	ADIOI_Free_request((ADIOI_Req_node *) (*request));
	*request = ADIO_REQUEST_NULL;
    }
    return done;

#endif /* matches ifndef ROMIO_HAVE_WORKING_AIO */
}
