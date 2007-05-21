/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_pvfs2.h"
#include "adio_extern.h"
#include "ad_pvfs2_common.h"

void ADIOI_PVFS2_Fcntl(ADIO_File fd, int flag, ADIO_Fcntl_t *fcntl_struct,
		       int *error_code)
{
    int ret;
    ADIOI_PVFS2_fs *pvfs_fs;
    PVFS_sysresp_getattr resp_getattr;
    static char myname[] = "ADIOI_PVFS2_FCNTL";

    pvfs_fs = (ADIOI_PVFS2_fs*)fd->fs_ptr;

    switch(flag) {
    case ADIO_FCNTL_GET_FSIZE:
	ret = PVFS_sys_getattr(pvfs_fs->object_ref, PVFS_ATTR_SYS_SIZE, 
		&(pvfs_fs->credentials), &resp_getattr);
	if (ret != 0 ) {
	    /* --BEGIN ERROR HANDLING-- */
	    *error_code = MPIO_Err_create_code(MPI_SUCCESS,
					       MPIR_ERR_RECOVERABLE,
					       myname, __LINE__,
					       ADIOI_PVFS2_error_convert(ret),
					       "Error in PVFS_sys_getattr", 0);
	    /* --END ERROR HANDLING-- */
	}
	else {
	    *error_code = MPI_SUCCESS;
	}
	fcntl_struct->fsize = resp_getattr.attr.size;
	return;

    case ADIO_FCNTL_SET_DISKSPACE:
	ADIOI_GEN_Prealloc(fd, fcntl_struct->diskspace, error_code);
	break;

    /* --BEGIN ERROR HANDLING-- */
    case ADIO_FCNTL_SET_ATOMICITY:
    default:
	*error_code = MPIO_Err_create_code(MPI_SUCCESS,
					   MPIR_ERR_RECOVERABLE,
					   myname, __LINE__,
					   MPI_ERR_ARG,
					   "**flag", "**flag %d", flag);
    /* --END ERROR HANDLING-- */
    }
}

/* 
 * vim: ts=8 sts=4 sw=4 noexpandtab 
 */
