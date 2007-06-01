/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_nfs.h"

void ADIOI_NFS_ReadComplete(ADIO_Request *request, ADIO_Status *status,
			    int *error_code)
{
#ifdef ROMIO_HAVE_WORKING_AIO
    int err;
    static char myname[] = "ADIOI_NFS_READCOMPLETE";
#ifdef ROMIO_HAVE_STRUCT_AIOCB_WITH_AIO_HANDLE
    struct aiocb *tmp1;
#endif
#endif

    if (*request == ADIO_REQUEST_NULL) {
	*error_code = MPI_SUCCESS;
	return;
    }
    
#ifdef ROMIO_HAVE_AIO_SUSPEND_TWO_ARGS
/* old IBM */
    if ((*request)->queued) {
	do {
	    err = aio_suspend(1, (struct aiocb **) &((*request)->handle));
	} while ((err == -1) && (errno == EINTR));

	tmp1 = (struct aiocb *) (*request)->handle;
	if (err != -1) {
	    err = aio_return(tmp1->aio_handle);
	    (*request)->nbytes = err;
	    errno = aio_error(tmp1->aio_handle);
	}
	else (*request)->nbytes = -1;

/* on DEC, it is required to call aio_return to dequeue the request.
   IBM man pages don't indicate what function to use for dequeue.
   I'm assuming it is aio_return! */

	if (err == -1) {
	    *error_code = MPIO_Err_create_code(MPI_SUCCESS,
					       MPIR_ERR_RECOVERABLE, myname,
					       __LINE__, MPI_ERR_IO, "**io",
					       "**io %s", strerror(errno));
	}
	else *error_code = MPI_SUCCESS;
    }
    else *error_code = MPI_SUCCESS;  /* if ( (*request)->queued ) */

#ifdef HAVE_STATUS_SET_BYTES
    if ((*request)->nbytes != -1)
	MPIR_Status_set_bytes(status, (*request)->datatype, (*request)->nbytes);
#endif

#elif defined(ROMIO_HAVE_WORKING_AIO)
/* all other aio types */
    if ((*request)->queued) {
	do {
	    err = aio_suspend((const struct aiocb **) &((*request)->handle), 1, 0);
	} while ((err == -1) && (errno == EINTR));

	if (err != -1) {
	    err = aio_return((struct aiocb *) (*request)->handle); 
	    (*request)->nbytes = err;
	    errno = aio_error((struct aiocb *) (*request)->handle);
	}
	else (*request)->nbytes = -1;

	if (err == -1) {
	    *error_code = MPIO_Err_create_code(MPI_SUCCESS,
					       MPIR_ERR_RECOVERABLE, myname,
					       __LINE__, MPI_ERR_IO, "**io",
					       "**io %s", strerror(errno));
	}
	else *error_code = MPI_SUCCESS;
    }
    else *error_code = MPI_SUCCESS;  /* if ((*request)->queued) ... */
#ifdef HAVE_STATUS_SET_BYTES
    if ((*request)->nbytes != -1)
	MPIR_Status_set_bytes(status, (*request)->datatype, (*request)->nbytes);
#endif
#endif

#ifdef ROMIO_HAVE_WORKING_AIO
    if ((*request)->queued != -1) {

	/* queued = -1 is an internal hack used when the request must
	   be completed, but the request object should not be
	   freed. This is used in ADIOI_Complete_async, because the user
	   will call MPI_Wait later, which would require status to
	   be filled. Ugly but works. queued = -1 should be used only
	   in ADIOI_Complete_async. 
           This should not affect the user in any way. */

	/* if request is still queued in the system, it is also there
           on ADIOI_Async_list. Delete it from there. */
	if ((*request)->queued) ADIOI_Del_req_from_list(request);

	(*request)->fd->async_count--;
	if ((*request)->handle) ADIOI_Free((*request)->handle);
	ADIOI_Free_request((ADIOI_Req_node *) (*request));
	*request = ADIO_REQUEST_NULL;
    }

#else
/* no aio */

#ifdef HAVE_STATUS_SET_BYTES
    MPIR_Status_set_bytes(status, (*request)->datatype, (*request)->nbytes);
#endif
    (*request)->fd->async_count--;
    ADIOI_Free_request((ADIOI_Req_node *) (*request));
    *request = ADIO_REQUEST_NULL;
    *error_code = MPI_SUCCESS;
#endif    
}


void ADIOI_NFS_WriteComplete(ADIO_Request *request, ADIO_Status *status,
			     int *error_code)
{
    ADIOI_NFS_ReadComplete(request, status, error_code);
}
