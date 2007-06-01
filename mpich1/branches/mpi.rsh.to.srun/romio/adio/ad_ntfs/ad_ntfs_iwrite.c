/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_ntfs.h"

void ADIOI_NTFS_IwriteContig(ADIO_File fd, void *buf, int count, 
			     MPI_Datatype datatype, int file_ptr_type,
			     ADIO_Offset offset, ADIO_Request *request,
			     int *error_code)  
{
    int len, typesize;
    int err;
    static char myname[] = "ADIOI_NTFS_IwriteContig";

    *request = ADIOI_Malloc_request();
    if ((*request) == NULL)
    {
	*error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
	    myname, __LINE__, MPI_ERR_IO,
	    "**nomem", "**nomem %s", "ADIOI_Request");
	return;
    }
    (*request)->optype = ADIOI_WRITE;
    (*request)->fd = fd;
    (*request)->datatype = datatype;

    MPI_Type_size(datatype, &typesize);
    len = count * typesize;

    if (file_ptr_type == ADIO_INDIVIDUAL)
    {
	offset = fd->fp_ind;
    }
    err = ADIOI_NTFS_aio(fd, buf, len, offset, 1, &((*request)->handle));
    if (file_ptr_type == ADIO_INDIVIDUAL)
    {
	fd->fp_ind += len;
    }

    (*request)->queued = 1;
    ADIOI_Add_req_to_list(request);

    /* --BEGIN ERROR HANDLING-- */
    if (err != MPI_SUCCESS)
    {
	*error_code = MPIO_Err_create_code(err, MPIR_ERR_RECOVERABLE,
					   myname, __LINE__, MPI_ERR_IO,
					   "**io", 0);
	return;
    }
    /* --END ERROR HANDLING-- */
    *error_code = MPI_SUCCESS;

    fd->fp_sys_posn = -1;   /* set it to null. */
    fd->async_count++;
}


/* This function is for implementation convenience. It is not user-visible.
 * If wr==1 write, wr==0 read.
 *
 * Returns MPI_SUCCESS on success, mpi_errno on failure.
 */
int ADIOI_NTFS_aio(ADIO_File fd, void *buf, int len, ADIO_Offset offset,
		   int wr, void *handle)
{
    static char myname[] = "ADIOI_NTFS_aio";
    static DWORD dwNumWritten, dwNumRead;
    BOOL ret_val = FALSE;
    FDTYPE fd_sys;
    int mpi_errno = MPI_SUCCESS;
    OVERLAPPED *pOvl;
    DWORD err;

    fd_sys = fd->fd_sys;

    pOvl = (OVERLAPPED *) ADIOI_Calloc(sizeof(OVERLAPPED), 1);
    if (pOvl == NULL)
    {
	mpi_errno = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
	    myname, __LINE__, MPI_ERR_IO,
	    "**nomem", "**nomem %s", "OVERLAPPED");
	return mpi_errno;
    }
    pOvl->hEvent = CreateEvent(NULL, TRUE, TRUE, NULL);
    if (pOvl->hEvent == NULL)
    {
	err = GetLastError();
	mpi_errno = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
	    myname, __LINE__, MPI_ERR_IO,
	    "**io", "**io %s", ADIOI_NTFS_Strerror(err));
	ADIOI_Free(pOvl);
	return mpi_errno;
    }
    pOvl->Offset = DWORDLOW(offset);
    pOvl->OffsetHigh = DWORDHIGH(offset);

    if (wr)
    {
	ret_val = WriteFile(fd_sys, buf, len, &dwNumWritten, pOvl);
    }
    else
    {
	ret_val = ReadFile(fd_sys, buf, len, &dwNumRead, pOvl);
    }

    /* --BEGIN ERROR HANDLING-- */
    if (ret_val == FALSE) 
    {
	mpi_errno = GetLastError();
	if (mpi_errno != ERROR_IO_PENDING)
	{
	    mpi_errno = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
		myname, __LINE__, MPI_ERR_IO,
		"**io",
		"**io %s", ADIOI_NTFS_Strerror(mpi_errno));
	    return mpi_errno;
	}
	mpi_errno = MPI_SUCCESS;
    }
    /* --END ERROR HANDLING-- */

    *((OVERLAPPED **) handle) = pOvl;

    return mpi_errno;
}

const char * ADIOI_NTFS_Strerror(int error)
{
    /* obviously not thread safe to store a message like this */
    static char msg[1024];
    HLOCAL str;
    int num_bytes;
    num_bytes = FormatMessage(
	FORMAT_MESSAGE_FROM_SYSTEM |
	FORMAT_MESSAGE_ALLOCATE_BUFFER,
	0,
	error,
	MAKELANGID( LANG_NEUTRAL, SUBLANG_DEFAULT ),
	(LPTSTR) &str,
	0,0);
    if (num_bytes == 0)
    {
	*msg = '\0';
    }
    else
    {
	memcpy(msg, str, num_bytes+1);
	LocalFree(str);
	strtok(msg, "\r\n");
    }
    return msg;
}
