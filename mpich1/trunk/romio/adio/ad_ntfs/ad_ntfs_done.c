/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_ntfs.h"

int ADIOI_NTFS_ReadDone(ADIO_Request *request, ADIO_Status *status,
			int *error_code)
{
    DWORD ret_val;
    int done = 0;
    static char myname[] = "ADIOI_NTFS_ReadDone";

    if (*request == ADIO_REQUEST_NULL)
    {
	*error_code = MPI_SUCCESS;
	return 1;
    }

    if ((*request)->queued) 
    {
	(*request)->nbytes = 0;
	ret_val = GetOverlappedResult((*request)->fd, (*request)->handle, &(*request)->nbytes, FALSE);

	if (!ret_val)
	{
	    /* --BEGIN ERROR HANDLING-- */
	    ret_val = GetLastError();
	    if (ret_val == ERROR_IO_INCOMPLETE)
	    {
		done = 0;
		*error_code = MPI_SUCCESS;
	    }
	    else
	    {
		*error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
		    myname, __LINE__, MPI_ERR_IO,
		    "**io", "**io %s", ADIOI_NTFS_Strerror(ret_val));
	    }
	    /* --END ERROR HANDLING-- */
	}
	else 
	{
	    done = 1;		
	    *error_code = MPI_SUCCESS;
	}
    }
    else
    {
	done = 1;
	*error_code = MPI_SUCCESS;
    }
#ifdef HAVE_STATUS_SET_BYTES
    if (done && ((*request)->nbytes != -1))
	MPIR_Status_set_bytes(status, (*request)->datatype, (*request)->nbytes);
#endif
    
    if (done) 
    {
	/* if request is still queued in the system, it is also there
	   on ADIOI_Async_list. Delete it from there. */
	if ((*request)->queued) ADIOI_Del_req_from_list(request);
	
	(*request)->fd->async_count--;
	if ((*request)->handle) 
	{
	    if (!CloseHandle(((OVERLAPPED*)((*request)->handle))->hEvent))
	    {
		ret_val = GetLastError();
		*error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
		    myname, __LINE__, MPI_ERR_IO,
		    "**io", "**io %s", ADIOI_NTFS_Strerror(ret_val));
	    }
	    ADIOI_Free((*request)->handle);
	}
	ADIOI_Free_request((ADIOI_Req_node *) (*request));
	*request = ADIO_REQUEST_NULL;
    }
    return done;
}


int ADIOI_NTFS_WriteDone(ADIO_Request *request, ADIO_Status *status,
			 int *error_code)
{
    static char myname[] = "ADIOI_NTFS_WriteDone";
    int ret_val;
    ret_val = ADIOI_NTFS_ReadDone(request, status, error_code);
    /* --BEGIN ERROR HANDLING-- */
    if (*error_code != MPI_SUCCESS)
    {
	*error_code = MPIO_Err_create_code(*error_code, MPIR_ERR_RECOVERABLE,
					   myname, __LINE__, MPI_ERR_IO,
					   "**io", 0);
    }
    /* --END ERROR HANDLING-- */
    return ret_val;
}
