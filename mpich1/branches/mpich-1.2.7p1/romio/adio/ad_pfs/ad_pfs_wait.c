/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_pfs.h"

void ADIOI_PFS_ReadComplete(ADIO_Request *request, ADIO_Status *status,
			    int *error_code)
{
    int err=0;
    static char myname[] = "ADIOI_PFS_READCOMPLETE";

    if (*request == ADIO_REQUEST_NULL) {
        *error_code = MPI_SUCCESS;
        return;
    }

    if ((*request)->queued) {
	err = _iowait(*((long *) (*request)->handle));
	if (err == -1) {
	    *error_code = MPIO_Err_create_code(MPI_SUCCESS,
					       MPIR_ERR_RECOVERABLE, myname,
					       __LINE__, MPI_ERR_IO, "**io",
					       "**io %s", strerror(errno));
	}
	else *error_code = MPI_SUCCESS;
    } /* if ((*request)->queued) ... */
    else *error_code = MPI_SUCCESS;
#ifdef HAVE_STATUS_SET_BYTES
    if ((*request)->nbytes != -1)
	MPIR_Status_set_bytes(status, (*request)->datatype, (*request)->nbytes);
#endif

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
}


void ADIOI_PFS_WriteComplete(ADIO_Request *request, ADIO_Status *status, int *error_code)  
{
    ADIOI_PFS_ReadComplete(request, status, error_code);
}
