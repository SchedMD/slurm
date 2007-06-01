/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_hfs.h"

void ADIOI_HFS_Open(ADIO_File fd, int *error_code)
{
    int perm, old_mask, amode;
#ifndef PRINT_ERR_MSG
    static char myname[] = "ADIOI_HFS_OPEN";
#endif

    if (fd->perm == ADIO_PERM_NULL) {
	old_mask = umask(022);
	umask(old_mask);
	perm = old_mask ^ 0666;
    }
    else perm = fd->perm;

    amode = 0;
    if (fd->access_mode & ADIO_CREATE)
	amode = amode | O_CREAT;
    if (fd->access_mode & ADIO_RDONLY)
	amode = amode | O_RDONLY;
    if (fd->access_mode & ADIO_WRONLY)
	amode = amode | O_WRONLY;
    if (fd->access_mode & ADIO_RDWR)
	amode = amode | O_RDWR;
    if (fd->access_mode & ADIO_EXCL)
	amode = amode | O_EXCL;

    fd->fd_sys = open64(fd->filename, amode, perm);
    fd->fd_direct = -1;

    if ((fd->fd_sys != -1) && (fd->access_mode & ADIO_APPEND)) {
	fd->fp_ind = lseek64(fd->fd_sys, 0, SEEK_END);
#ifdef HPUX
	fd->fp_sys_posn = fd->fp_ind;
#endif
    }

#ifdef SPPUX
	fd->fp_sys_posn = -1;  /* set it to null bec. we use pread, pwrite*/
#endif

	if (fd->fd_sys == -1 ) {
#ifdef MPICH2
	    *error_code = MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE, myname, __LINE__, MPI_ERR_IO, "**io", 
		"**io %s", strerror(errno));
#elif defined(PRINT_ERR_MSG)
	    *error_code = MPI_ERR_UNKNOWN;
#else /* MPICH-1 */
	*error_code = MPIR_Err_setmsg(MPI_ERR_IO, MPIR_ADIO_ERROR,
			      myname, "I/O Error", "%s", strerror(errno));
	ADIOI_Error(ADIO_FILE_NULL, *error_code, myname);	    
#endif
    }
    else *error_code = MPI_SUCCESS;
}
