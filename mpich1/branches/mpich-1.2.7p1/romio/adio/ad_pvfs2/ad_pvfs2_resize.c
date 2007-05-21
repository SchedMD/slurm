/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_pvfs2.h"
#include "ad_pvfs2_common.h"

/* as with ADIOI_PVFS2_Flush, implement the resize operation in a scalable
 * manner. one process does the work, then broadcasts the result to everyone
 * else.  fortunately, this operation is defined to be collective */
void ADIOI_PVFS2_Resize(ADIO_File fd, ADIO_Offset size, int *error_code)
{
    int ret, rank;
    ADIOI_PVFS2_fs *pvfs_fs;
    static char myname[] = "ADIOI_PVFS2_RESIZE";

    *error_code = MPI_SUCCESS;

    pvfs_fs = (ADIOI_PVFS2_fs*)fd->fs_ptr;

    MPI_Comm_rank(fd->comm, &rank);

    /* We desginate one node in the communicator to be an 'io_worker' in 
     * ADIO_Open.  This node can perform operations on files and then 
     * inform the other nodes of the result */

    /* MPI-IO semantics treat conflicting MPI_File_set_size requests the
     * same as conflicting write requests. Thus, a resize from one
     * process does not have to be visible to the other processes until a
     * syncronization point is reached */

    if (rank == fd->hints->ranklist[0]) {
	ret = PVFS_sys_truncate(pvfs_fs->object_ref, 
		size, &(pvfs_fs->credentials));
	MPI_Bcast(&ret, 1, MPI_INT, 0, fd->comm);
    } else  {
	MPI_Bcast(&ret, 1, MPI_INT, 0, fd->comm);
    }
    /* --BEGIN ERROR HANDLING-- */
    if (ret != 0) {
	*error_code = MPIO_Err_create_code(MPI_SUCCESS,
					   MPIR_ERR_RECOVERABLE,
					   myname, __LINE__,
					   ADIOI_PVFS2_error_convert(ret),
					   "Error in PVFS_sys_truncate", 0);
	return;
    }
    /* --END ERROR HANDLING-- */
}

/*
 * vim: ts=8 sts=4 sw=4 noexpandtab 
 */
