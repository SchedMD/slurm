/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_pfs.h"

int ADIOI_PFS_ReadDone(ADIO_Request *request, ADIO_Status *status,
		       int *error_code)  
{
    int done=0;
    static char myname[] = "ADIOI_PFS_READDONE";

    if (*request == ADIO_REQUEST_NULL) {
        *error_code = MPI_SUCCESS;
        return 1;
    }

    if ((*request)->queued)
	done = _iodone(*((long *) (*request)->handle));
    else done = 1; /* ADIOI_Complete_Async completed this request, 
                      but request object was not freed. */

#ifdef HAVE_STATUS_SET_BYTES
    if ((done >= 0) && ((*request)->nbytes != -1))
	MPIR_Status_set_bytes(status, (*request)->datatype, (*request)->nbytes);
#endif

    if (done >= 0) {
        /* if request is still queued in the system, it is also there
           on ADIOI_Async_list. Delete it from there. */
        if ((*request)->queued) ADIOI_Del_req_from_list(request);

        (*request)->fd->async_count--;
        if ((*request)->handle) ADIOI_Free((*request)->handle);
        ADIOI_Free_request((ADIOI_Req_node *) (*request));
        *request = ADIO_REQUEST_NULL;
    }
    
    if (done == -1 && errno != 0) {
	*error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					   myname, __LINE__, MPI_ERR_IO,
					   "**io",
					   "**io %s", strerror(errno));
    }
    else *error_code = MPI_SUCCESS;
    return done;
}


int ADIOI_PFS_WriteDone(ADIO_Request *request, ADIO_Status *status,
			int *error_code)
{
    return ADIOI_PFS_ReadDone(request, status, error_code);
} 
