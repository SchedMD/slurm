/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_xfs.h"
#include "adio_extern.h"

void ADIOI_XFS_Fcntl(ADIO_File fd, int flag, ADIO_Fcntl_t *fcntl_struct, int *error_code)
{
    int i, err;
#if defined(LINUX) && defined(MPISGI)
    struct xfs_flock64 fl;
#else
    struct flock64 fl;
#endif
    static char myname[] = "ADIOI_XFS_FCNTL";

    switch(flag) {
    case ADIO_FCNTL_GET_FSIZE:
	fcntl_struct->fsize = lseek64(fd->fd_sys, 0, SEEK_END);
	if (fcntl_struct->fsize == -1) {
	    *error_code = MPIO_Err_create_code(MPI_SUCCESS,
					       MPIR_ERR_RECOVERABLE, myname,
					       __LINE__, MPI_ERR_IO, "**io",
					       "**io %s", strerror(errno));
	}
	else *error_code = MPI_SUCCESS;
	break;

    case ADIO_FCNTL_SET_DISKSPACE:
	i = 0;
	fl.l_start = 0;
	fl.l_whence = SEEK_SET;
	fl.l_len = fcntl_struct->diskspace;

#if defined(LINUX) && defined(MPISGI)
	err = fcntl(fd->fd_sys, XFS_IOC_RESVSP64, &fl);
#else
	err = fcntl(fd->fd_sys, F_RESVSP64, &fl);
#endif

	if (err) i = 1;
	if (fcntl_struct->diskspace > lseek64(fd->fd_sys, 0, SEEK_END)) {
	    /* also need to set the file size */
	    err = ftruncate64(fd->fd_sys, fcntl_struct->diskspace);
	    if (err) i = 1;
	}

	if (i == 1) {
	    *error_code = MPIO_Err_create_code(MPI_SUCCESS,
					       MPIR_ERR_RECOVERABLE, myname,
					       __LINE__, MPI_ERR_IO, "**io",
					       "**io %s", strerror(errno));
	}
	else *error_code = MPI_SUCCESS;
	break;

    case ADIO_FCNTL_SET_ATOMICITY:
	fd->atomicity = (fcntl_struct->atomicity == 0) ? 0 : 1;
	*error_code = MPI_SUCCESS;
	break;

    default:
	/* --BEGIN ERROR HANDLING-- */
	*error_code = MPIO_Err_create_code(MPI_SUCCESS,
					   MPIR_ERR_RECOVERABLE,
					   myname, __LINE__,
					   MPI_ERR_ARG,
					   "**flag", "**flag %d", flag);
	return;
	/* --END ERROR HANDLING-- */
    }
}
