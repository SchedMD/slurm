/* -*- Mode: C; c-basic-offset:4 ; -*-
 * vim: ts=8 sts=4 sw=4 noexpandtab
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_pvfs2.h"
#include "ad_pvfs2_common.h"

/* open_status is helpful for bcasting values around */
struct open_status_s {
    int error;
    PVFS_object_ref object_ref;
};
typedef struct open_status_s open_status;
    
    /* steps for getting a handle:  (it gets a little convoluted, but at least
     * it's deterministic) 
     * . lookup the file.  
     * . if lookup succeeds, but we were passed MPI_MODE_EXCL, that's an error
     * . if lookup fails, the file might not exist. 
     *		in that case, create the file if we were passed MPI_MODE_CREATE 
     * . if the create fails, that means someone else created the file between
     *    our call to lookup and our call to create (like if N processors all
     *    open the same file with MPI_COMM_SELF)
     *
     * the good news is that only one processor does this and broadcasts the
     * handle to everyone else in the communicator
     */
static void fake_an_open(PVFS_fs_id fs_id, char *pvfs_name, int access_mode,
	                 int nr_datafiles, ADIOI_PVFS2_fs *pvfs2_fs, 
			 open_status *o_status)
{
    int ret;
    PVFS_sysresp_lookup resp_lookup;
    PVFS_sysresp_getparent resp_getparent;
    PVFS_sysresp_create resp_create;
    PVFS_sys_attr attribs;

    ADIOI_PVFS2_makeattribs(&attribs);
    attribs.dfile_count = nr_datafiles;

    memset(&resp_lookup, 0, sizeof(resp_lookup));
    memset(&resp_getparent, 0, sizeof(resp_getparent));
    memset(&resp_create, 0, sizeof(resp_create));


    ret = PVFS_sys_lookup(fs_id, pvfs_name,
	    &(pvfs2_fs->credentials), &resp_lookup, PVFS2_LOOKUP_LINK_FOLLOW);
    if ( (ret < 0) ) { /* XXX: check what the error was */
	if (access_mode & MPI_MODE_CREATE)  {
	    ret = PVFS_sys_getparent(fs_id, pvfs_name,
		    &(pvfs2_fs->credentials), &resp_getparent); 
	    if (ret < 0) {
		FPRINTF(stderr, "pvfs_sys_getparent returns with %d\n", ret);
		o_status->error = ret;
		return;
	    } 
	    ret = PVFS_sys_create(resp_getparent.basename, 
		    resp_getparent.parent_ref, attribs, 
		    &(pvfs2_fs->credentials), NULL, &resp_create); 

	    if (ret < 0) { /* XXX: should only do this for EEXISTS */
		ret = PVFS_sys_lookup(fs_id, pvfs_name,
			&(pvfs2_fs->credentials), &resp_lookup, 
			PVFS2_LOOKUP_LINK_FOLLOW);
		if ( ret < 0 ) {
		    o_status->error = ret;
		    return;
		}
		o_status->error = ret;
		o_status->object_ref = resp_lookup.ref;
		return;
	    }
	    o_status->object_ref = resp_create.ref;
	} else {
	    FPRINTF(stderr, "cannot create file without MPI_MODE_CREATE\n");
	    o_status->error = ret;
	    return;
	}
    } else if (access_mode & MPI_MODE_EXCL) {
	/* lookup should not succeed if opened with EXCL */
	o_status->error = -1; /* XXX: what should it be? */
	return;
    } else {
	o_status->object_ref = resp_lookup.ref;
    }
    o_status->error = ret;
    return;

}


/* if MPI_File_open was called with MPI_MODE_CREATE|MPI_MODE_EXCL, then we have
 * a little problem: our usual open-and-broadcast test will not work because
 * only one person (the first aggregator) will perform the open w/ CREATE|EXCL
 */
void ADIOI_PVFS2_Open(ADIO_File fd, int *error_code)
{
    int rank, ret;
    PVFS_fs_id cur_fs;
    static char myname[] = "ADIOI_PVFS2_OPEN";
    char pvfs_path[PVFS_NAME_MAX] = {0};

    ADIOI_PVFS2_fs *pvfs2_fs;

    /* since one process is doing the open, that means one process is also
     * doing the error checking.  define a struct for both the object reference
     * and the error code to broadcast to all the processors */

    open_status o_status;
    MPI_Datatype open_status_type;
    MPI_Datatype types[2] = {MPI_INT, MPI_BYTE};
    int lens[2] = {1, sizeof(PVFS_object_ref)};
    MPI_Aint offsets[2];
    
    pvfs2_fs = (ADIOI_PVFS2_fs *) ADIOI_Malloc(sizeof(ADIOI_PVFS2_fs));

    /* --BEGIN ERROR HANDLING-- */
    if (pvfs2_fs == NULL) {
	*error_code = MPIO_Err_create_code(MPI_SUCCESS,
					   MPIR_ERR_RECOVERABLE,
					   myname, __LINE__,
					   MPI_ERR_UNKNOWN,
					   "Error allocating memory", 0);
	return;
    }
    /* --END ERROR HANDLING-- */

    MPI_Comm_rank(fd->comm, &rank);

    ADIOI_PVFS2_Init(error_code);
    if (*error_code != MPI_SUCCESS)
    {
	/* ADIOI_PVFS2_INIT handles creating error codes on its own */
	return;
    }

    ADIOI_PVFS2_makecredentials(&(pvfs2_fs->credentials));

    /* we only have to do this on one node. we'll broadcast the handle to
     * everyone else in the communicator */

    if (rank == fd->hints->ranklist[0]) {
	/* given the filename, figure out which pvfs filesystem it is on */
	ret = PVFS_util_resolve(fd->filename, &cur_fs, 
		pvfs_path, PVFS_NAME_MAX);
	if (ret < 0 ) {
	    PVFS_perror("PVFS_util_resolve", ret);
	    /* TODO: pick a good error for this */
	    o_status.error = -1;
	} else  {
	    fake_an_open(cur_fs, pvfs_path,
		    fd->access_mode, fd->hints->striping_factor, 
		    pvfs2_fs, &o_status);
	}
    }

    /* NOTE: if MPI_MODE_EXCL was set, ADIO_Open will call
     * ADIOI_PVFS2_Open from just one processor.  This really confuses MPI when
     * one procesor on a communicator broadcasts to no listners.  
     *
     * Since ADIO_Open will close the file and call ADIOI_PVFS2_Open again (but
     * w/o EXCL), we can bail out right here and return early */
    if ((fd->access_mode & MPI_MODE_EXCL)) {
	if (o_status.error == 0)
	{
	    *error_code = MPI_SUCCESS;
	    fd->fs_ptr = pvfs2_fs;
	}
	else
	{
	    /* --BEGIN ERROR HANDLING-- */
	    ADIOI_Free(pvfs2_fs);
	    *error_code = MPIO_Err_create_code(MPI_SUCCESS,
					       MPIR_ERR_RECOVERABLE,
					       myname, __LINE__,
					       ADIOI_PVFS2_error_convert(o_status.error),
					       "Unknown error", 0);
	    /* TODO: FIX STRING */
	    /* --END ERROR HANDLING-- */
	} 
	MPI_Type_free(&open_status_type);
	return;
    } 

    /* broadcast status and (possibly valid) object reference */
    MPI_Address(&o_status.error, &offsets[0]);
    MPI_Address(&o_status.object_ref, &offsets[1]);

    MPI_Type_struct(2, lens, offsets, types, &open_status_type);
    MPI_Type_commit(&open_status_type);

    MPI_Bcast(MPI_BOTTOM, 1, open_status_type, 0, fd->comm);
    MPI_Type_free(&open_status_type);

    /* --BEGIN ERROR HANDLING-- */
    if (o_status.error != 0)
    { 
	ADIOI_Free(pvfs2_fs);
	*error_code = MPIO_Err_create_code(MPI_SUCCESS,
					   MPIR_ERR_RECOVERABLE,
					   myname, __LINE__,
					   ADIOI_PVFS2_error_convert(o_status.error),
					   "Unknown error", 0);
	/* TODO: FIX STRING */
	return;
    }
    /* --END ERROR HANDLING-- */

    pvfs2_fs->object_ref = o_status.object_ref;
    fd->fs_ptr = pvfs2_fs;

    *error_code = MPI_SUCCESS;
    return;
}
