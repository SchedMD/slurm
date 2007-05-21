/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_ntfs.h"

void ADIOI_NTFS_Resize(ADIO_File fd, ADIO_Offset size, int *error_code)
{
    LONG dwTemp;
    DWORD err;
    BOOL result;
    static char myname[] = "ADIOI_NTFS_Resize";

    dwTemp = DWORDHIGH(size);
    err = SetFilePointer(fd->fd_sys, DWORDLOW(size), &dwTemp, FILE_BEGIN);
    /* --BEGIN ERROR HANDLING-- */
    if (err == INVALID_SET_FILE_POINTER)
    {
	err = GetLastError();
	if (err != NO_ERROR)
	{
	    *error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					   myname, __LINE__, MPI_ERR_IO,
					   "**io",
					   "**io %s", ADIOI_NTFS_Strerror(err));
	    return;
	}
    }
    /* --END ERROR HANDLING-- */
    result = SetEndOfFile(fd->fd_sys);
    /* --BEGIN ERROR HANDLING-- */
    if (result == FALSE)
    {
	err = GetLastError();
	*error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					   myname, __LINE__, MPI_ERR_IO,
					   "**io",
					   "**io %s", ADIOI_NTFS_Strerror(err));
	return;
    }
    /* --END ERROR HANDLING-- */
    *error_code = MPI_SUCCESS;
}
