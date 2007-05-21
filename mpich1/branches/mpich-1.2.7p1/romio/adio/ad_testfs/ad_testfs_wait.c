/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 2001 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_testfs.h"
#include "adioi.h"

void ADIOI_TESTFS_ReadComplete(ADIO_Request *request, ADIO_Status *status, int
			       *error_code)
{
    int myrank, nprocs;

    *error_code = MPI_SUCCESS;

    if (*request == ADIO_REQUEST_NULL) {
	FPRINTF(stdout, "[xx/xx] ADIOI_TESTFS_ReadComplete called on ADIO_REQUEST_NULL\n");
	return;
    }

    MPI_Comm_size((*request)->fd->comm, &nprocs);
    MPI_Comm_rank((*request)->fd->comm, &myrank);
    FPRINTF(stdout, "[%d/%d] ADIOI_TESTFS_ReadComplete called on %s\n", 
	    myrank, nprocs, (*request)->fd->filename);

#ifdef HAVE_STATUS_SET_BYTES
    MPIR_Status_set_bytes(status, (*request)->datatype, (*request)->nbytes);
#endif
    (*request)->fd->async_count--;
    ADIOI_Free_request((ADIOI_Req_node *) (*request));
    *request = ADIO_REQUEST_NULL;
}

void ADIOI_TESTFS_WriteComplete(ADIO_Request *request, ADIO_Status *status, int
				*error_code)
{
    int myrank, nprocs;

    *error_code = MPI_SUCCESS;

    if (*request == ADIO_REQUEST_NULL) {
	FPRINTF(stdout, "[xx/xx] ADIOI_TESTFS_WriteComplete called on ADIO_REQUEST_NULL\n");
	return;
    }

    MPI_Comm_size((*request)->fd->comm, &nprocs);
    MPI_Comm_rank((*request)->fd->comm, &myrank);
    FPRINTF(stdout, "[%d/%d] ADIOI_TESTFS_WriteComplete called on %s\n", 
	    myrank, nprocs, (*request)->fd->filename);

#ifdef HAVE_STATUS_SET_BYTES
    MPIR_Status_set_bytes(status, (*request)->datatype, (*request)->nbytes);
#endif
    (*request)->fd->async_count--;
    ADIOI_Free_request((ADIOI_Req_node *) (*request));
    *request = ADIO_REQUEST_NULL;
}
