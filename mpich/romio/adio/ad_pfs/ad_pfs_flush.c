/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_pfs.h"

void ADIOI_PFS_Flush(ADIO_File fd, int *error_code)
{
    int err, np_total, np_comm;
    static char myname[] = "ADIOI_PFS_FLUSH";

/* fsync is not actually needed in PFS, because it uses something
   called fast-path I/O. However, it doesn't do any harm either. */
    err = fsync(fd->fd_sys);
    if (err == -1) {
	*error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					   myname, __LINE__, MPI_ERR_IO,
					   "**io",
					   "**io %s", strerror(errno));
    }
    else *error_code = MPI_SUCCESS;

/* MPI-IO requires that after an fsync all processes must see the same
   file size. In PFS M_ASYNC mode, this doesn't automatically happen.
   Therefore, if M_ASYNC mode, temporarily change it to M_UNIX mode
   and then switch back to M_ASYNC. That updates the file size! */

    MPI_Comm_size(MPI_COMM_WORLD, &np_total);
    MPI_Comm_size(fd->comm, &np_comm);
    if ((np_total == np_comm) && (!(fd->atomicity))) {
	err = _setiomode(fd->fd_sys, M_UNIX);
	err = _setiomode(fd->fd_sys, M_ASYNC);
    }
    /* else it is M_UNIX anyway. don't do anything. */
}
