/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_pvfs.h"

void ADIOI_PVFS_Resize(ADIO_File fd, ADIO_Offset size, int *error_code)
{
    int err;
    int rank;
    static char myname[] = "ADIOI_PVFS_RESIZE";

    /* because MPI_File_set_size is a collective operation, and PVFS1 clients
     * do not cache metadata locally, one client can resize and broadcast the
     * result to the others */
    MPI_Comm_rank(fd->comm, &rank);
    if (rank == fd->hints->ranklist[0]) {
	err = pvfs_ftruncate64(fd->fd_sys, size);
    }
    MPI_Bcast(&err, 1, MPI_INT, 0, fd->comm);

    if (err == -1) {
	*error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					   myname, __LINE__, MPI_ERR_IO,
					   "**io",
					   "**io %s", strerror(errno));
    }
    else *error_code = MPI_SUCCESS;
}
