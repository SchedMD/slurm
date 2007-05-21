/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_sfs.h"
#include "adio_extern.h"

void ADIOI_SFS_Fcntl(ADIO_File fd, int flag, ADIO_Fcntl_t *fcntl_struct, int *error_code)
{
    int  i, ntimes, len;
    ADIO_Offset curr_fsize, alloc_size, size, done;
    ADIO_Status status;
    char *buf;
#ifndef PRINT_ERR_MSG
    static char myname[] = "ADIOI_SFS_FCNTL";
#endif

    switch(flag) {
    case ADIO_FCNTL_GET_FSIZE:
        /* On SFS, I find that a write from one process, which changes
           the file size, does not automatically make the new file size 
           visible to other processes. Therefore, a sync-barrier-sync is 
           needed. (Other processes are able to read the data written 
           though; only file size is returned incorrectly.) */

	fsync(fd->fd_sys);
	MPI_Barrier(fd->comm);
	fsync(fd->fd_sys);
	
	fcntl_struct->fsize = llseek(fd->fd_sys, 0, SEEK_END);
	if (fd->fp_sys_posn != -1) 
	     llseek(fd->fd_sys, fd->fp_sys_posn, SEEK_SET);
	if (fcntl_struct->fsize == -1) {
#ifdef MPICH2
	    *error_code = MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE, myname, __LINE__, MPI_ERR_IO, "**io",
		"**io %s", strerror(errno));
#elif defined(PRINT_ERR_MSG)
	    *error_code = MPI_ERR_UNKNOWN;
#else /* MPICH-1 */
	    *error_code = MPIR_Err_setmsg(MPI_ERR_IO, MPIR_ADIO_ERROR,
			      myname, "I/O Error", "%s", strerror(errno));
	    ADIOI_Error(fd, *error_code, myname);	    
#endif
	}
	else *error_code = MPI_SUCCESS;
	break;

    case ADIO_FCNTL_SET_DISKSPACE:
	ADIOI_GEN_Prealloc(fd, fcntl_struct->diskspace, error_code);
	break;

    case ADIO_FCNTL_SET_ATOMICITY:
	fd->atomicity = (fcntl_struct->atomicity == 0) ? 0 : 1;
	*error_code = MPI_SUCCESS;
	break;

    default:
	FPRINTF(stderr, "Unknown flag passed to ADIOI_SFS_Fcntl\n");
	MPI_Abort(MPI_COMM_WORLD, 1);
    }
}
