/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 2003 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_pvfs2.h"
#include "adio.h"

#include "ad_pvfs2_common.h"

void ADIOI_PVFS2_Delete(char *filename, int *error_code)
{
    PVFS_credentials credentials;
    PVFS_sysresp_getparent resp_getparent;
    int ret;
    PVFS_fs_id cur_fs;
    static char myname[] = "ADIOI_PVFS2_DELETE";
    char pvfs_path[PVFS_NAME_MAX] = {0};

    ADIOI_PVFS2_Init(error_code);
    /* --BEGIN ERROR HANDLING-- */
    if (*error_code != MPI_SUCCESS) 
    {
	/* ADIOI_PVFS2_INIT handles creating error codes itself */
	return;
    }
    /* --END ERROR HANDLING-- */

    /* in most cases we'll store the credentials in the fs struct, but we don't
     * have one of those in Delete  */
    ADIOI_PVFS2_makecredentials(&credentials);

    /* given the filename, figure out which pvfs filesystem it is on */
    ret = PVFS_util_resolve(filename, &cur_fs, pvfs_path, PVFS_NAME_MAX);
    /* --BEGIN ERROR HANDLING-- */
    if (ret != 0) {
	*error_code = MPIO_Err_create_code(MPI_SUCCESS,
					   MPIR_ERR_RECOVERABLE,
					   myname, __LINE__,
					   ADIOI_PVFS2_error_convert(ret),
					   "Error in PVFS_util_resolve", 0);
	return;
    }
    /* --END ERROR HANDLING-- */

    ret = PVFS_sys_getparent(cur_fs, pvfs_path, &credentials, &resp_getparent);

    ret = PVFS_sys_remove(resp_getparent.basename, 
			  resp_getparent.parent_ref, &credentials);
    /* --BEGIN ERROR HANDLING-- */
    if (ret != 0) {
	*error_code = MPIO_Err_create_code(MPI_SUCCESS,
					   MPIR_ERR_RECOVERABLE,
					   myname, __LINE__,
					   ADIOI_PVFS2_error_convert(ret),
					   "Error in PVFS_sys_remove", 0);
	return;
    }
    /* --END ERROR HANDLING-- */

    *error_code = MPI_SUCCESS;
    return;
}

/* 
 * vim: ts=8 sts=4 sw=4 noexpandtab 
 */
